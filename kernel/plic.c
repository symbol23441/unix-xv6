#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
// 平台级别中断控制
//

void
plicinit(void)
{
  // set desired IRQ priorities non-zero (otherwise disabled).
  // 设置目标中断的优先级为非零值（否则该中断会被禁用）。
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
}

// 中断能成功的条件,例如UART0
// 1、UART0 优先级为非 0（例如 1）
// 2、使能 hart 0 对 UART0 的中断接收（中断请求使能位）
// 3、hart 优先级阈值 小于 UART0优先值
// 4、CPU 本身打开外部中断（启用中断，启用外部中断信号）
// 外设中断（设备产生）
//       ↓
// 优先级 > 0？ ────✘→ 丢弃
//       ↓ ✔
// SENABLE 打开？ ─✘→ 忽略
//       ↓ ✔
// 优先级 > threshold？ ─✘→ 延迟 delivery
//       ↓ ✔
// CPU 中断打开（SIE）？ ─✘→ 等待
//       ↓ ✔
// →→→ 被 trap() 捕获 → devintr() 处理

// PLIC初始化（硬件线程）
void
plicinithart(void)
{
  int hart = cpuid();
  
  // set uart's enable bit for this hart's S-mode. 
  // 为该 hart 的 S 模式设置 UART 和 VirtIO 的中断使能位。
  *(uint32*)PLIC_SENABLE(hart)= (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);

  // set this hart's S-mode priority threshold to 0.
  // 将该 hart 的 S 模式优先级阈值设为 0（允许所有优先级 > 0 的中断）。
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// 询问PLIC 应该响应哪个中断请求
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
