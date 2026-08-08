// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// One reference count for every page that can be allocated by kalloc().
#define NPAGES ((PHYSTOP - KERNBASE) / PGSIZE)
static int refcnt[NPAGES];
static struct spinlock reflock;

static uint
paindex(uint64 pa)
{
  if(pa < KERNBASE || pa >= PHYSTOP || (pa % PGSIZE) != 0)
    panic("paindex");
  return (pa - KERNBASE) / PGSIZE;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&reflock, "refcnt");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // kfree() drops a reference. Give each initially free page one first.
    kaddref(p);
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int refs;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&reflock);
  if(refcnt[paindex((uint64)pa)] < 1)
    panic("kfree: refcount");
  refs = --refcnt[paindex((uint64)pa)];
  release(&reflock);

  // A shared page remains allocated until its last mapping disappears.
  if(refs > 0)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
  {
    acquire(&reflock);
    if(refcnt[paindex((uint64)r)] != 0)
      panic("kalloc: refcount");
    refcnt[paindex((uint64)r)] = 1;
    release(&reflock);
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}

// Add one mapping of a physical page.
void
kaddref(void *pa)
{
  acquire(&reflock);
  refcnt[paindex((uint64)pa)]++;
  release(&reflock);
}

int
kgetref(void *pa)
{
  int refs;

  acquire(&reflock);
  refs = refcnt[paindex((uint64)pa)];
  release(&reflock);
  return refs;
}
