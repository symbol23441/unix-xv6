// 物理内存分配器，服务于：
// 用户进程、内核栈、页表页，以及管道缓冲区。
// 以整个 4096 字节的页为单位进行分配。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];  // 内核之后的第一个地址，在kernel.ld中定义


struct run {
  struct run *next;
};
// 物理内存管理结构，空闲页链表
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem; 

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP); // 物理内存的最高地址
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

//
// 释放由 v 指向的那页物理内存，这页内存通常是由调用 kalloc() 返回的。
// （例外情况是在初始化分配器时；见上面的 kinit 函数。）
//
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 填充垃圾数据1,防止悬空引用
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // 将该页释放，头插法到空闲页链表
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate 分配一个4096字节的物理内存
// 返回一个内核可使用的指针；若分配失败，返回0
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
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
