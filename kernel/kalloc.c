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
} kmem[NCPU];

char* kmem_lock_name[] = {
  "kmem_cpu_0",
  "kmem_cpu_1",
  "kmem_cpu_2",
  "kmem_cpu_3",
  "kmem_cpu_4",
  "kmem_cpu_5",
  "kmem_cpu_6",
  "kmem_cpu_7",
};

void
kinit(void)
{
  for(int i=0;i<NCPU;i++){
    initlock(&kmem[i].lock, kmem_lock_name[i]);
  }
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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();                 //cpuid调用需要，关闭中断，保证期间安全。
  int cpu = cpuid();          // 获取cpu编号。

  acquire(&kmem[cpu].lock);   // 将释放的页插入当前cpu的空闲页表
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);

  pop_off();                  // 开中断
}

struct run*
ksteal(int cur_cpuid){
  struct run *r;

  for(int i=0;i<NCPU;i++){
    if(cur_cpuid == i)continue;

    acquire(&kmem[i].lock);
    r = kmem[i].freelist;
    if(r){
      kmem[i].freelist = r->next;
      release(&kmem[i].lock);
      return r;
    }
    release(&kmem[i].lock);
  }
  return 0;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();                 //cpuid调用需要，关闭中断，保证期间安全。
  int cpu = cpuid();          // 获取cpu编号。

  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if(r){
    kmem[cpu].freelist = r->next;
  }else{
    // 为防止A偷B，B偷A，死锁。这里先释放自己的锁。处于关中断，不会进程切换。
    release(&kmem[cpu].lock);
    r = ksteal(cpu);
    acquire(&kmem[cpu].lock);
  }
  release(&kmem[cpu].lock);
  pop_off();                  // 开中断

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;

  
}
