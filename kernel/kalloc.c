// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define PA2PGREF_ID(p) (((p)-KERNBASE)/PGSIZE)  // 物理地址获取物理页号
#define PGREF_MAX_ID PA2PGREF_ID(PHYSTOP)       // 最大物理页号

int pageref[PGREF_MAX_ID];                      // 页引用结构
struct spinlock pgreflock;                      // 页引用锁

#define PA2PGREF(p) pageref[PA2PGREF_ID((uint64)(p))] // 获取当前物理地址，页引用数量


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

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pgreflock,"pgref");
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

  // 引用数为1的时候，释放
  acquire(&pgreflock);
  if(--PA2PGREF(pa)<=0){
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&pgreflock);
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

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    PA2PGREF(r) = 1;
  }
    
  return (void*)r;
}

// 添加物理页的引用（如写时复制）
void pgaddref(uint64 pa){
  acquire(&pgreflock);
  PA2PGREF(pa)++;
  release(&pgreflock);
}

// 复制物理页
// 成功返回物理页地址，失败返回0
uint64
uvm_copycowpage_deref(uint64 pa){
  uint64 mem;  

  acquire(&pgreflock);

  // 只有当前页表引用，直接用当前物理页
  if(PA2PGREF(pa)<=1){
    release(&pgreflock);
    return pa;
  }
    
  // 申请新物理页，复制内容
  if((mem = (uint64) kalloc()) == 0){
    release(&pgreflock);
    return 0;
  }
  memmove((void*)mem, (char*)pa, PGSIZE);

  PA2PGREF(pa)--;
  release(&pgreflock);
  return mem;
}