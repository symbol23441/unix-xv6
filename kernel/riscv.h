// 查询当前CPU的核心ID
static inline uint64
r_mhartid()
{
  uint64 x;
  asm volatile("csrr %0, mhartid" : "=r" (x) );
  return x;
}
// GCC内联汇编格式  asm volatile("汇编指令" : 输出操作数 : 输入操作数 : 被破坏的寄存器);

// Machine Status Register, mstatus

#define MSTATUS_MPP_MASK (3L << 11) // previous mode.
#define MSTATUS_MPP_M (3L << 11)
#define MSTATUS_MPP_S (1L << 11)
#define MSTATUS_MPP_U (0L << 11)
#define MSTATUS_MIE (1L << 3)    // machine-mode interrupt enable.

static inline uint64
r_mstatus()
{
  uint64 x;
  asm volatile("csrr %0, mstatus" : "=r" (x) );
  return x;
}

static inline void 
w_mstatus(uint64 x)
{
  asm volatile("csrw mstatus, %0" : : "r" (x));
}

// machine exception program counter, holds the
// instruction address to which a return from
// exception will go.
static inline void 
w_mepc(uint64 x)
{
  asm volatile("csrw mepc, %0" : : "r" (x));
}

// Supervisor Status Register, sstatus

#define SSTATUS_SPP (1L << 8)  // Previous mode, 1=Supervisor, 0=User
#define SSTATUS_SPIE (1L << 5) // Supervisor Previous Interrupt Enable
#define SSTATUS_UPIE (1L << 4) // User Previous Interrupt Enable
#define SSTATUS_SIE (1L << 1)  // Supervisor Interrupt Enable             监督级别下中断使能
#define SSTATUS_UIE (1L << 0)  // User Interrupt Enable

static inline uint64
r_sstatus()
{
  uint64 x;
  asm volatile("csrr %0, sstatus" : "=r" (x) );
  return x;
}

static inline void 
w_sstatus(uint64 x)
{
  asm volatile("csrw sstatus, %0" : : "r" (x));
}

// Supervisor Interrupt Pending
static inline uint64
r_sip()
{
  uint64 x;
  asm volatile("csrr %0, sip" : "=r" (x) );
  return x;
}

static inline void 
w_sip(uint64 x)
{
  asm volatile("csrw sip, %0" : : "r" (x));
}

// Supervisor Interrupt Enable
#define SIE_SEIE (1L << 9) // external
#define SIE_STIE (1L << 5) // timer
#define SIE_SSIE (1L << 1) // software
static inline uint64
r_sie()
{
  uint64 x;
  asm volatile("csrr %0, sie" : "=r" (x) );
  return x;
}

static inline void 
w_sie(uint64 x)
{
  asm volatile("csrw sie, %0" : : "r" (x));
}

// M-Mode 中断使能状态定义
#define MIE_MEIE (1L << 11) // 外部中断使能
#define MIE_MTIE (1L << 7)  // 定时器中断使能
#define MIE_MSIE (1L << 3)  // 软件中断使能
static inline uint64
r_mie()
{
  uint64 x;
  asm volatile("csrr %0, mie" : "=r" (x) );  // 控制状态寄存器读到x
  return x;
}

static inline void 
w_mie(uint64 x)
{
  asm volatile("csrw mie, %0" : : "r" (x));
}

// supervisor exception program counter, holds the
// instruction address to which a return from
// exception will go.
static inline void 
w_sepc(uint64 x)
{
  asm volatile("csrw sepc, %0" : : "r" (x));
}

static inline uint64
r_sepc()
{
  uint64 x;
  asm volatile("csrr %0, sepc" : "=r" (x) );
  return x;
}

// Machine Exception Delegation
static inline uint64
r_medeleg()
{
  uint64 x;
  asm volatile("csrr %0, medeleg" : "=r" (x) );
  return x;
}

static inline void 
w_medeleg(uint64 x)
{
  asm volatile("csrw medeleg, %0" : : "r" (x));
}

// Machine Interrupt Delegation
static inline uint64
r_mideleg()
{
  uint64 x;
  asm volatile("csrr %0, mideleg" : "=r" (x) );
  return x;
}

static inline void 
w_mideleg(uint64 x)
{
  asm volatile("csrw mideleg, %0" : : "r" (x));
}

// Supervisor Trap-Vector Base Address
// low two bits are mode.
static inline void 
w_stvec(uint64 x)
{
  asm volatile("csrw stvec, %0" : : "r" (x));
}

static inline uint64
r_stvec()
{
  uint64 x;
  asm volatile("csrr %0, stvec" : "=r" (x) );
  return x;
}

// Machine-mode interrupt vector
static inline void 
w_mtvec(uint64 x)
{
  asm volatile("csrw mtvec, %0" : : "r" (x));
}

static inline void
w_pmpcfg0(uint64 x)
{
  asm volatile("csrw pmpcfg0, %0" : : "r" (x));
}

static inline void
w_pmpaddr0(uint64 x)
{
  asm volatile("csrw pmpaddr0, %0" : : "r" (x));
}

// use riscv's sv39 page table scheme.
#define SATP_SV39 (8L << 60)

#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))   // sv39模式+物理页号  作为寄存器的值

// supervisor address translation and protection;  监管级地址转换与保护 寄存器设置
// holds the address of the page table. 持有当前进程页表的根地址
static inline void 
w_satp(uint64 x)
{
  asm volatile("csrw satp, %0" : : "r" (x));
}

static inline uint64
r_satp()
{
  uint64 x;
  asm volatile("csrr %0, satp" : "=r" (x) );
  return x;
}

// Supervisor Scratch register, for early trap handler in trampoline.S.
static inline void 
w_sscratch(uint64 x)
{
  asm volatile("csrw sscratch, %0" : : "r" (x));
}

static inline void 
w_mscratch(uint64 x)
{
  asm volatile("csrw mscratch, %0" : : "r" (x));
}

// Supervisor Trap Cause
static inline uint64
r_scause()
{
  uint64 x;
  asm volatile("csrr %0, scause" : "=r" (x) );
  return x;
}

// Supervisor Trap Value
static inline uint64
r_stval()
{
  uint64 x;
  asm volatile("csrr %0, stval" : "=r" (x) );
  return x;
}

// Machine-mode Counter-Enable
static inline void 
w_mcounteren(uint64 x)
{
  asm volatile("csrw mcounteren, %0" : : "r" (x));
}

static inline uint64
r_mcounteren()
{
  uint64 x;
  asm volatile("csrr %0, mcounteren" : "=r" (x) );
  return x;
}

// machine-mode cycle counter
static inline uint64
r_time()
{
  uint64 x;
  asm volatile("csrr %0, time" : "=r" (x) );
  return x;
}

// enable device interrupts
static inline void
intr_on()
{
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

// disable device interrupts
static inline void
intr_off()
{
  w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

// 检查是否启用了设备中断（SIE 位）
static inline int
intr_get()
{
  uint64 x = r_sstatus();
  return (x & SSTATUS_SIE) != 0;
}

static inline uint64
r_sp()
{
  uint64 x;
  asm volatile("mv %0, sp" : "=r" (x) );
  return x;
}

// 读取和写入线程指针（tp），它保存了当前核心的 hartid（核心编号），即 cpus[] 数组中的索引。
static inline uint64
r_tp()
{
  uint64 x;
  asm volatile("mv %0, tp" : "=r" (x) );
  return x;
}

static inline void 
w_tp(uint64 x)
{
  asm volatile("mv tp, %0" : : "r" (x));
}

static inline uint64
r_ra()
{
  uint64 x;
  asm volatile("mv %0, ra" : "=r" (x) );
  return x;
}

// flush the TLB. 刷新快表（地址转换旁路缓存）
static inline void
sfence_vma()
{
  // the zero, zero means flush all TLB entries.
  asm volatile("sfence.vma zero, zero");
}


#define PGSIZE 4096 // bytes per page
#define PGSHIFT 12  // bits of offset within a page

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))      // 按页大小（PGSIZE）向上对齐
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))                // 按页大小（PGSIZE）向下对齐

// 页表物理分配后初始为0，因此初始标志状态均为0
#define PTE_V (1L << 0) // valid  页表项有效，物理页已经存在于内存中。
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // 1 -> user can access

// shift a physical address to the right place for a PTE.
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)             // 物理地址转页表项。清除12为偏移地址，腾出低10为标志位。

#define PTE2PA(pte) (((pte) >> 10) << 12)                   // 页表项映射到物理地址。右移10位状态标志位，左移12偏移地址0，转化为物理地址。即物理地址有效位数是56位。

#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// 从虚拟地址va中提取，当前级level别页表的页表索引[0~511]
#define PXMASK          0x1FF // 9 bits
#define PXSHIFT(level)  (PGSHIFT+(9*(level)))                           // 提取第 level 级页表索引时，虚拟地址需要右移的位数
#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)    // 虚拟地址转页表项数组索引(PET index)

// 超过所有可能虚拟地址的一个值（即地址空间的上界）
// 实际上，MAXVA 比 Sv39 支持的最大虚拟地址少一个最高位（少1位），
// 是为了避免处理那些高位为1时需要进行符号扩展（sign-extend）的问题。
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))


// RISC-V Sv39 的 64 位页表项 PTE（从高位到低位）结构如下：
//   位 63..54 —— 保留（未使用）
//   位 53..10 —— PPN（Physical Page Number，物理页号）
//   位 9..8   —— RSW（Reserved for Software，软件保留位）
//   位 7      —— D（Dirty）标志，是否被写入过
//   位 6      —— A（Accessed）标志，是否被访问过
//   位 5      —— G（Global），是否为全局映射
//   位 4      —— U（User），用户态是否可访问
//   位 3      —— X（Executable），是否可执行
//   位 2      —— W（Writable），是否可写
//   位 1      —— R（Readable），是否可读
//   位 0      —— V（Valid），是否有效
typedef uint64 pte_t;        // 页表项类型，64位。
typedef uint64 *pagetable_t; // 页表结构定义。数组长度，512长度（4KB/64bit）。
