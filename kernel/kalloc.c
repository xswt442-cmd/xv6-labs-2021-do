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

#define STEAL_BATCH 32

struct {
  struct spinlock lock[NCPU];
  struct run *freelist[NCPU];
} kmem;

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(&kmem.lock[i], "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int id;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // cpuid() is valid only while interrupts are disabled.  Keep them off
  // until this page has been put on this CPU's freelist.
  push_off();
  id = cpuid();
  acquire(&kmem.lock[id]);
  r->next = kmem.freelist[id];
  kmem.freelist[id] = r;
  release(&kmem.lock[id]);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r, *tail, *rest;
  int id;

  // Do not allow this process to move between obtaining its CPU number and
  // touching that CPU's freelist.
  push_off();
  id = cpuid();

  acquire(&kmem.lock[id]);
  r = kmem.freelist[id];
  if(r)
    kmem.freelist[id] = r->next;
  release(&kmem.lock[id]);

  if(r == 0) {
    // Borrow a small batch from one donor.  The donor lock is released
    // before re-acquiring our own lock, so no path holds two kmem locks.
    for(int i = 1; i < NCPU; i++) {
      int donor = (id + i) % NCPU;

      acquire(&kmem.lock[donor]);
      r = kmem.freelist[donor];
      if(r) {
        tail = r;
        for(int n = 1; n < STEAL_BATCH && tail->next; n++)
          tail = tail->next;
        kmem.freelist[donor] = tail->next;
        tail->next = 0;
      }
      release(&kmem.lock[donor]);

      if(r) {
        // Return one page now and cache the rest locally for future calls.
        rest = r->next;
        if(rest) {
          acquire(&kmem.lock[id]);
          tail = rest;
          while(tail->next)
            tail = tail->next;
          tail->next = kmem.freelist[id];
          kmem.freelist[id] = rest;
          release(&kmem.lock[id]);
        }
        r->next = 0;
        break;
      }
    }
  }
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
