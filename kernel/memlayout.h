// 物理内存布局

// qemu -machine virt 的布局如下，
// 参考自 qemu 的 hw/riscv/virt.c 文件：
//
// 00001000 -- 引导 ROM，由 qemu 提供
// 02000000 -- CLINT（Core Local Interruptor）
// 0C000000 -- PLIC（Platform-Level Interrupt Controller）
// 10000000 -- uart0 串口设备
// 10001000 -- virtio 虚拟磁盘设备
// 80000000 -- 启动 ROM 在机器模式下跳转到这里
//             - 内核从这里加载
// 80000000 之后的未使用内存为可用 RAM。

// 内核使用物理内存的方式如下：
// 80000000 -- entry.S 入口代码，接着是内核代码段和数据段
// end -- 内核页分配区域的起始位置
// PHYSTOP -- 内核使用的 RAM 的结束位置

// qemu 将 UART（串口）寄存器映射在物理内存的这个位置。
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio mmio interface
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// core local interruptor (CLINT), 内核本地中断，包括定时器timer、软件中断
#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8*(hartid))        // 每个cpu核心 定时器比较寄存器的地址
#define CLINT_MTIME (CLINT + 0xBFF8) // 机器时间寄存器地址，用于记录从系统启动以来经过的时钟周期数

// qemu 将platform-level interrupt controller (PLIC) 放置如下位置
#define PLIC 0x0c000000L                       // PLIC 基地址
#define PLIC_PRIORITY (PLIC + 0x0)             // 中断优先级寄存器起始地址
#define PLIC_PENDING (PLIC + 0x1000)           // 中断 pending 位图寄存器
#define PLIC_MENABLE(hart) (PLIC + 0x2000 + (hart)*0x100)     // Machine 模式中断使能寄存器
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)     // Supervisor 模式中断使能寄存器
#define PLIC_MPRIORITY(hart) (PLIC + 0x200000 + (hart)*0x2000) // Machine 模式优先级阈值寄存器
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000) // Supervisor 模式优先级阈值寄存器
#define PLIC_MCLAIM(hart) (PLIC + 0x200004 + (hart)*0x2000)    // Machine 模式中断 claim/complete 寄存器
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)    // Supervisor 模式中断 claim/complete 寄存器


// 内核假设在物理地址 0x80000000 到 PHYSTOP 之间有可用的 RAM，
// 用于内核和用户页的分配。
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024)

// 将trampoline 页映射到最高地址
// 在用户态和内核空间中可访问
#define TRAMPOLINE (MAXVA - PGSIZE)

// 在 trampoline 之下映射内核栈，每个内核栈都被无效的保护页guard pages包围。2页，guard 页不主动映射即可。
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// 用户内存布局。
// 地址从 0 开始：
//   程序代码段（text）
//   初始化的数据段和未初始化的数据段（bss）
//   固定大小的用户栈
//   可扩展的堆
//   ...
//   TRAPFRAME（p->trapframe，由 trampoline 使用）
//   TRAMPOLINE（与内核中相同的一页）
#define TRAPFRAME (TRAMPOLINE - PGSIZE)
