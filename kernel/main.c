#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n Welcome to Symbols's OS!!!\n");
    printf("\n");
    kinit();         // physical page allocator    初始化空闲页链表结构
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging       -- hart means hardware Thread硬件线程。例如kvminit()对页表结构初始化（公共有效），而kvminithart() 专为每个线程（即核心的）的单独硬件初始化，如线程寄存器等。
    procinit();      // process table
    trapinit();      // trap vectors                  陷入指令相关的初始化
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller   平台级别中断控制的初始化
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache                  缓冲缓存，
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk v
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}
