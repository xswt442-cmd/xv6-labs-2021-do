#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
uint64
sys_pgaccess(void)
{
  uint64 base, pagebase, maskaddr;
  int len;
  uint32 mask = 0;
  struct proc *p = myproc();

  if(argaddr(0, &base) < 0 || argint(1, &len) < 0 ||
     argaddr(2, &maskaddr) < 0)
    return -1;

  // The ABI counts pages.  malloc() may return an address within the first
  // requested page, so align that address down before validating the range.
  pagebase = PGROUNDDOWN(base);
  if(len < 1 || len > 32 || base >= MAXVA || pagebase >= p->sz ||
     (uint64)len * PGSIZE > p->sz - pagebase)
    return -1;

  // Validate the entire request before changing any accessed bits.
  for(int i = 0; i < len; i++){
    pte_t *pte = walk(p->pagetable, pagebase + (uint64)i * PGSIZE, 0);
    if(pte == 0 || (*pte & (PTE_V | PTE_U)) != (PTE_V | PTE_U) ||
       (*pte & (PTE_R | PTE_W | PTE_X)) == 0)
      return -1;
  }

  // Build and publish the result before changing the observed PTEs.  A bad
  // destination must not consume the access information.
  for(int i = 0; i < len; i++){
    pte_t *pte = walk(p->pagetable, pagebase + (uint64)i * PGSIZE, 0);
    if(*pte & PTE_A)
      mask |= (uint32)1 << i;
  }
  if(copyout(p->pagetable, maskaddr, (char *)&mask, sizeof(mask)) < 0)
    return -1;

  int flushed = 0;
  for(int i = 0; i < len; i++){
    pte_t *pte = walk(p->pagetable, pagebase + (uint64)i * PGSIZE, 0);
    if(*pte & PTE_A){
      *pte &= ~PTE_A;
      flushed = 1;
    }
  }
  if(flushed)
    sfence_vma();
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
