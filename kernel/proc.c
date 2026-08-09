#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

static struct vma*
findvma(struct proc *p, uint64 va)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vma[i];
    if(v->length && va >= v->addr && va < v->addr + v->length)
      return v;
  }
  return 0;
}

static uint64
vma_lowest_addr(struct proc *p)
{
  uint64 low = TRAPFRAME;

  for(int i = 0; i < NVMA; i++)
    if(p->vma[i].length && p->vma[i].addr < low)
      low = p->vma[i].addr;
  return low;
}

// Choose the highest available hole below the trapframe.  Mappings are kept
// outside p->sz, so heap growth and mmap never silently overlap.
uint64
vma_map(struct proc *p, uint64 length, uint64 offset, int prot, int flags,
        struct file *file)
{
  struct vma *slot = 0;
  uint64 end = TRAPFRAME;
  uint64 requested = length;

  if(length == 0 || length > ~0ULL - (PGSIZE - 1))
    return 0;
  length = PGROUNDUP(length);

  for(int i = 0; i < NVMA; i++){
    if(p->vma[i].length == 0 && slot == 0)
      slot = &p->vma[i];
  }
  if(slot == 0 || length == 0 || length > TRAPFRAME)
    return 0;

  for(;;){
    if(end < length)
      return 0;
    uint64 start = end - length;
    if(start < PGROUNDUP(p->sz))
      return 0;

    int overlap = 0;
    for(int i = 0; i < NVMA; i++){
      struct vma *v = &p->vma[i];
      if(v->length && start < v->addr + v->length && end > v->addr){
        end = v->addr;
        overlap = 1;
        break;
      }
    }
    if(!overlap){
      slot->addr = start;
      slot->length = length;
      slot->file_length = requested;
      slot->offset = offset;
      slot->prot = prot;
      slot->flags = flags;
      slot->file = filedup(file);
      return start;
    }
  }
}

// Write a resident shared page back using its VMA-relative file offset.
// This deliberately does not use filewrite(): a mapping must not alter f->off.
static int
vma_writeback_page(struct proc *p, struct vma *v, uint64 va)
{
  pte_t *pte = walk(p->pagetable, va, 0);
  uint64 off = v->offset + va - v->addr;
  uint64 pa;
  uint64 delta = va - v->addr;
  uint64 remaining;
  int done = 0;
  int total;
  uint file_size;
  int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;

  if(pte == 0 || (*pte & PTE_V) == 0)
    return 0;
  if(delta >= v->file_length)
    return 0;
  remaining = v->file_length - delta;
  total = remaining < PGSIZE ? (int)remaining : PGSIZE;
  ilock(v->file->ip);
  file_size = v->file->ip->size;
  iunlock(v->file->ip);
  if(off >= file_size)
    return 0;
  if(total > file_size - off)
    total = file_size - off;
  pa = PTE2PA(*pte);
  while(done < total){
    int n = total - done;
    int r;
    if(n > max)
      n = max;
    begin_op();
    ilock(v->file->ip);
    r = writei(v->file->ip, 0, pa + done, (uint)(off + done), n);
    iunlock(v->file->ip);
    end_op();
    if(r != n)
      return -1;
    done += n;
  }
  return 0;
}

static int
vma_release(struct proc *p, struct vma *v, uint64 addr, uint64 length)
{
  int err = 0;

  for(uint64 va = addr; va < addr + length; va += PGSIZE){
    pte_t *pte = walk(p->pagetable, va, 0);
    if(pte && (*pte & PTE_V)){
      if((v->flags & MAP_SHARED) && (v->prot & PROT_WRITE) &&
         vma_writeback_page(p, v, va) < 0)
        err = -1;
      uvmunmap(p->pagetable, va, 1, 1);
    }
  }
  return err;
}

int
vma_unmap(struct proc *p, uint64 addr, uint64 length)
{
  struct vma *v;
  uint64 end;
  int err;

  if(length == 0 || (addr % PGSIZE) ||
     length > ~0ULL - (PGSIZE - 1))
    return -1;
  length = PGROUNDUP(length);
  if((end = addr + length) < addr)
    return -1;
  if((v = findvma(p, addr)) == 0 || end > v->addr + v->length)
    return -1;
  // The lab interface permits only removal from either end of one VMA.
  if(addr != v->addr && end != v->addr + v->length)
    return -1;

  err = vma_release(p, v, addr, length);
  if(addr == v->addr && end == v->addr + v->length){
    fileclose(v->file);
    memset(v, 0, sizeof(*v));
  } else if(addr == v->addr){
    v->addr = end;
    v->offset += length;
    v->length -= length;
    v->file_length = v->file_length > length ?
                     v->file_length - length : 0;
  } else {
    v->length -= length;
    if(v->file_length > v->length)
      v->file_length = v->length;
  }
  sfence_vma();
  return err;
}

void
vma_cleanup(struct proc *p)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vma[i];
    if(v->length){
      vma_release(p, v, v->addr, v->length);
      fileclose(v->file);
      memset(v, 0, sizeof(*v));
    }
  }
  sfence_vma();
}

int
vma_fault(struct proc *p, uint64 va, uint64 cause)
{
  struct vma *v;
  uint64 page = PGROUNDDOWN(va);
  uint64 fileoff;
  uint64 delta;
  int perm = PTE_U;
  char *mem;
  int n;

  if((v = findvma(p, page)) == 0)
    return -1;
  if((cause == 13 && !(v->prot & PROT_READ)) ||
     (cause == 15 && !(v->prot & PROT_WRITE)) ||
     (cause == 12 && !(v->prot & PROT_EXEC)))
    return -1;
  if(v->prot & PROT_READ)
    perm |= PTE_R;
  if(v->prot & PROT_WRITE)
    // RISC-V reserves writable leaf PTEs that are not also readable.
    perm |= PTE_R | PTE_W;
  if(v->prot & PROT_EXEC)
    perm |= PTE_X;
  if(walk(p->pagetable, page, 0) && (*walk(p->pagetable, page, 0) & PTE_V))
    return -1;
  if((mem = kalloc()) == 0)
    return -1;
  memset(mem, 0, PGSIZE);
  delta = page - v->addr;
  fileoff = v->offset + delta;
  n = 0;
  if(delta < v->file_length){
    uint64 remain = v->file_length - delta;
    int want = remain < PGSIZE ? remain : PGSIZE;
    ilock(v->file->ip);
    n = readi(v->file->ip, 0, (uint64)mem, (uint)fileoff, want);
    iunlock(v->file->ip);
  }
  if(n < 0 || mappages(p->pagetable, page, PGSIZE, (uint64)mem, perm) < 0){
    kfree(mem);
    return -1;
  }
  sfence_vma();
  return 0;
}

int
vma_fork(struct proc *p, struct proc *np)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vma[i];
    struct vma *nv = &np->vma[i];
    if(v->length == 0)
      continue;
    *nv = *v;
    nv->file = filedup(v->file);
    for(uint64 va = v->addr; va < v->addr + v->length; va += PGSIZE){
      pte_t *pte = walk(p->pagetable, va, 0);
      char *mem;
      if(pte == 0 || (*pte & PTE_V) == 0)
        continue;
      if((mem = kalloc()) == 0)
        goto bad;
      memmove(mem, (char *)PTE2PA(*pte), PGSIZE);
      if(mappages(np->pagetable, va, PGSIZE, (uint64)mem, PTE_FLAGS(*pte)) < 0){
        kfree(mem);
        goto bad;
      }
    }
  }
  return 0;

bad:
  // np is not visible yet and its lock is held.  Tear down copied pages and
  // references without file-system writeback, which could sleep here.
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &np->vma[i];
    if(v->length){
      for(uint64 va = v->addr; va < v->addr + v->length; va += PGSIZE){
        pte_t *pte = walk(np->pagetable, va, 0);
        if(pte && (*pte & PTE_V))
          uvmunmap(np->pagetable, va, 1, 1);
      }
      fileclose(v->file);
      memset(v, 0, sizeof(*v));
    }
  }
  return -1;
}

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  memset(p->vma, 0, sizeof(p->vma));
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz, newsz, limit;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    newsz = sz + (uint)n;
    limit = vma_lowest_addr(p);
    if(newsz < sz || newsz > limit || newsz >= TRAPFRAME)
      return -1;
    if((sz = uvmalloc(p->pagetable, sz, newsz)) == 0) {
      return -1;
    }
  } else if(n < 0){
    uint64 shrink = (uint64)(-(long)n);
    if(shrink > sz)
      return -1;
    sz = uvmdealloc(p->pagetable, sz, sz - shrink);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  if(vma_fork(p, np) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  vma_cleanup(p);

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
