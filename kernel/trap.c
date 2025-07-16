#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// 设置使得在内核态时能够处理异常、中断和trap。
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// 处理来自用户空间的 中断、异常、系统调用
// 跳转自trampoline
//
void
usertrap(void)
{
  int which_dev = 0;

  // 该trap是否来自用户空间 
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // 设置 strap 向量寄存器到 kernelvec。 此期间trap会路由到kernelvec(kernelvec.S)
  // 从此，完全处于内核状态
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // 保存用户pc 到 p->trapframe->epc
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // epc 指向用户进程的ecall执行，我们返回需要调到下一条指令。因此+4，ecall指令宽度。
    p->trapframe->epc += 4;

    // 中断会改变 sstatus 和其他控制寄存器，所以在操作完这些寄存器之前，不要开启中断。
    intr_on();   // 开中断

    syscall();
  } else if((which_dev = devintr()) != 0){ // 设备中断
    // ok
  } else {  // 程序异常，终止进程
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // 其他的设备中断已经在devintr() 处理
  // 定时器中断。则放弃CPU，进入进程切换。
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// 返回用户空间
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // 我们即将把 trap 的处理函数从 kerneltrap() 切换为 usertrap()，
  // 因此在返回用户态之前要关闭中断，确保中断发生时处理函数已经正确地切换成 usertrap()
  intr_off();

  // 将系统调用（syscalls）、中断（interrupts）和异常（exceptions）发送到 trampoline.S 进行处理。
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // 设置trapframe值，为进程下次重新进入内核做准备
  p->trapframe->kernel_satp = r_satp();         // 内核页表
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // 进程内核栈
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // 当前的CPU核心

  // 设置好相关寄存器，为 trampoline.S's sret 返回用户空间
  
  // set S Previous Privilege mode to User. 真正转换在sret是触发。
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // 为用户模式 清空SPP状态置0
  x |= SSTATUS_SPIE; // 启动用户模式下的中断
  w_sstatus(x);

  // 设置用户态返回的地址，恢复sepc
  w_sepc(p->trapframe->epc);

  // 告知 trampoline.S 用户页表, 进行switch
  uint64 satp = MAKE_SATP(p->pagetable);

  // 跳转到内存顶部的 trampoline.S , 
  // 它会：切换到用户页表、恢复用户寄存器，并使用 sret 指令切换回用户模式。

  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// 来自内核代码的中断和异常会通过 kernelvec 跳转到这里，
// 并使用当前内核栈继续处理。
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // 如果定时器中断，则放弃CPU
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // yield() 可能引起一些中断的发生
  // 所以需要恢复 trap 相关寄存器，以便 kernelvec.S 中使用 sepc 指令。
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // 这是一个S-Mode下的外部中断，通过PLIC.

    // irq 指明 是哪个设备的中断
    int irq = plic_claim();

    if(irq == UART0_IRQ){   // 串口中断请求
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();  // virtio中断请求
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // PLIC 每次只允许每个设备触发一个中断；现在要通知 PLIC：该设备已经处理完，可以再次发起中断了。
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // M-Mode机器模式下，定时器的软件中断
    // 跳转自 timervec（kernelvec.S）

    // 为避免多个 CPU 同时重复处理系统时钟逻辑（ticks++ 等），只让 CPU 0 执行 clockintr()，其余 CPU 忽略。
    if(cpuid() == 0){
      clockintr();
    }
    
    // 通过清除 sip 中的 SSIP 位，来确认已处理该软件中断。
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

