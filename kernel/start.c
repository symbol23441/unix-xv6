#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S 需要，每个CPU一个stack
// stack0 是一个全局静态数组，位于内核的 .bss 段，物理地址固定。
// 仅用于kernel启动早期M-mode阶段。进入 S-mode 后，会使用内核栈（页表映射）。
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// a scratch area per CPU for machine-mode timer interrupts.
uint64 timer_scratch[NCPU][5];

// M-Mode定时器中断代码在kernelvec.S中
extern void timervec();

// 在RISC-V的M-mode下中执行entry.S，通过call start跳转到start()函数。这时程序已经切换到了C语言环境，并且使用的是stack0作为栈空间。
void
start()
{
  // 为mret，设置特权模式为S-Mode
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // 设置机器模式的异常程序计数器（mepc）为 main，用于 mret 跳转。
  // 需要使用 gcc 编译选项：-mcmodel=medany。
  w_mepc((uint64)main);

  // 寄存器设置：关闭页表映射
  w_satp(0);

  // 将所有中断和异常都交给S-Mode代理
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // 给S-Mode 设置所有的物理内存访问权限
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // 设置定时器中断
  timerinit();

  // 将cpu的核心id保存到tp寄存器，供cpuid()读取。
  // CSR控制状态寄存器，没有tp等通用寄存器快
  int id = r_mhartid();
  w_tp(id);

  // 切换到S-Mode，并通过机器异常程序寄存器mepc跳到main执行
  asm volatile("mret");
}

// 设置 Machine 模式下的定时器中断，用于后续转换为 S-mode 软件中断（trap.c 中的 devintr 处理）。
// 中断触发时，会进入 kernelvec.S 中的 timervec 汇编处理函数。
void
timerinit()
{
  // 获取当前CPU的hartID（核编号）
  int id = r_mhartid();

  // 向内核中断控制器CLINT 写入下一次中断触发的时间点。
  // 当前时间 + interval 周期后触发（约 0.1 秒）
  int interval = 1000000; // 定时周期（CPU cycles）
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

  // 为 timervec 函数准备 scratch[] 中的信息。
  // scratch[0..2]：用于 timervec 保存寄存器的空间。
  // scratch[3]：MTIMECMP CLINT寄存器地址。
  // scratch[4]：两次定时器中断之间的周期间隔（以 CPU cycle 计）。
  uint64 *scratch = &timer_scratch[id][0];
  scratch[3] = CLINT_MTIMECMP(id);
  scratch[4] = interval;
  w_mscratch((uint64)scratch);

  // 设置machine-mode的trap向量地址（中断入口）
  w_mtvec((uint64)timervec);

  // 启用 machine-mode 全局中断（MSTATUS_MIE 位）
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // 启用 machine-mode 的 timer 中断（MIE_MTIE 位）
  w_mie(r_mie() | MIE_MTIE);
}
