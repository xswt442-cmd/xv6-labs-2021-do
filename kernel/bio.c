// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define EMPTY_DEV ((uint)-1)

struct bucket {
  struct spinlock lock;
  struct buf *head;
};

struct {
  struct bucket bucket[NBUCKET];
  // Serializes misses while they inspect and move buffers between buckets.
  struct spinlock evictlock;
  struct buf buf[NBUF];
  uint clock;
} bcache;

static uint
bhash(uint dev, uint blockno)
{
  return (dev + blockno) % NBUCKET;
}

static uint
btime(void)
{
  // A timestamp is only an LRU ordering aid.  Making it atomic avoids a
  // second cache-wide spinlock on the hit/release paths.
  return __sync_fetch_and_add(&bcache.clock, 1);
}

static struct buf*
blookup(uint bucket, uint dev, uint blockno)
{
  struct buf *b;

  for(b = bcache.bucket[bucket].head; b; b = b->hnext)
    if(b->dev == dev && b->blockno == blockno)
      return b;
  return 0;
}

static void
bremove(struct buf *b)
{
  struct buf **pp;

  for(pp = &bcache.bucket[b->bucket].head; *pp; pp = &(*pp)->hnext) {
    if(*pp == b) {
      *pp = b->hnext;
      return;
    }
  }
  panic("bremove");
}

static void
binsert(struct buf *b, uint bucket)
{
  b->bucket = bucket;
  b->hnext = bcache.bucket[bucket].head;
  bcache.bucket[bucket].head = b;
}

void
binit(void)
{
  struct buf *b;

  for(int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head = 0;
  }
  initlock(&bcache.evictlock, "bcache.evict");

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->dev = EMPTY_DEV;
    b->valid = 0;
    b->refcnt = 0;
    b->timestamp = 0;
    initsleeplock(&b->lock, "buffer");
    binsert(b, (b - bcache.buf) % NBUCKET);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b, *victim;
  uint bucket = bhash(dev, blockno);
  uint victim_bucket, victim_time, victim_dev, victim_blockno;

  // The common case uses exactly one cache spinlock.
  acquire(&bcache.bucket[bucket].lock);
  b = blookup(bucket, dev, blockno);
  if(b) {
    b->refcnt++;
    release(&bcache.bucket[bucket].lock);
    acquiresleep(&b->lock);
    return b;
  }
  release(&bcache.bucket[bucket].lock);

  // Misses are serialized.  Holding the target bucket for the entire miss
  // makes the recheck and insertion atomic for this (dev, blockno), while
  // the scan below takes at most one additional bucket lock at a time.
  acquire(&bcache.evictlock);
  acquire(&bcache.bucket[bucket].lock);
  b = blookup(bucket, dev, blockno);
  if(b) {
    b->refcnt++;
    release(&bcache.bucket[bucket].lock);
    release(&bcache.evictlock);
    acquiresleep(&b->lock);
    return b;
  }

  for(;;) {
    victim = 0;
    victim_bucket = 0;
    victim_time = 0;

    // Find the oldest free buffer.  evictlock prevents another miss from
    // moving a candidate while its bucket is briefly unlocked; hits may
    // still change refcnt, which is checked again below.
    for(uint i = 0; i < NBUCKET; i++) {
      if(i != bucket)
        acquire(&bcache.bucket[i].lock);
      for(b = bcache.bucket[i].head; b; b = b->hnext) {
        if(b->refcnt == 0 &&
           (victim == 0 || b->timestamp < victim_time)) {
          victim = b;
          victim_bucket = i;
          victim_time = b->timestamp;
          victim_dev = b->dev;
          victim_blockno = b->blockno;
        }
      }
      if(i != bucket)
        release(&bcache.bucket[i].lock);
    }

    if(victim == 0) {
      release(&bcache.bucket[bucket].lock);
      release(&bcache.evictlock);
      panic("bget: no buffers");
    }

    // A concurrent hit may have claimed the candidate after the scan.  If
    // so, retry; the target bucket remains locked throughout.
    if(victim_bucket != bucket)
      acquire(&bcache.bucket[victim_bucket].lock);
    if(victim->bucket != victim_bucket || victim->refcnt != 0 ||
       victim->timestamp != victim_time || victim->dev != victim_dev ||
       victim->blockno != victim_blockno) {
      if(victim_bucket != bucket)
        release(&bcache.bucket[victim_bucket].lock);
      continue;
    }

    bremove(victim);
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    victim->refcnt = 1;
    victim->timestamp = btime();
    binsert(victim, bucket);

    if(victim_bucket != bucket)
      release(&bcache.bucket[victim_bucket].lock);
    release(&bcache.bucket[bucket].lock);
    release(&bcache.evictlock);
    acquiresleep(&victim->lock);
    return victim;
  }
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.bucket[b->bucket].lock);
  b->refcnt--;
  if(b->refcnt == 0)
    b->timestamp = btime();
  release(&bcache.bucket[b->bucket].lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.bucket[b->bucket].lock);
  b->refcnt++;
  release(&bcache.bucket[b->bucket].lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.bucket[b->bucket].lock);
  b->refcnt--;
  if(b->refcnt == 0)
    b->timestamp = btime();
  release(&bcache.bucket[b->bucket].lock);
}
