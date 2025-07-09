// 内核上下文schedule()的switch()切换，用于保存的上下文结构
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// 每个CPU的状态
struct cpu {
  struct proc *proc;          // 当前cpu中运行的进程 或 null
  struct context context;     // 进入scheduler()时，swtch() 将当前的上下文（寄存器等）保存到这里
  int noff;                   // 表示调用 push_off()（关闭中断）函数的嵌套层数。这样支持多层关闭中断，只有最外层 pop_off() 才真正重新启用中断。
  int intena;                 // 在调用 push_off() 之前，中断是否是启用的？
};

extern struct cpu cpus[NCPU];

// 每个进程用于 trap（陷入）处理代码（位于 trampoline.S）的一块数据。
// 它独占一个页面，位于用户页表中 trampoline 页下面的一页。
// 在内核页表中并没有特别映射这块内存。
// sscratch 寄存器指向这里。
// trampoline.S 中的 uservec 入口会将用户寄存器保存到 trapframe 中，
// 然后从 trapframe 中加载 kernel_sp（内核栈指针）、kernel_hartid（核心ID）、kernel_satp（页表地址），
// 然后跳转到内核的 kernel_trap。
// usertrapret() 函数以及 trampoline.S 中的 userret 代码
// 会设置 trapframe 中的 kernel_* 字段，恢复 trapframe 中保存的用户寄存器，切换到用户页表，然后进入用户态。
// trapframe 包括了一些调用者保存（callee-saved）的寄存器，比如 s0-s11，
// 因为返回用户态时是通过 usertrapret() 直接跳转的，并不会完整返回内核的调用栈，因此必须手动保存这些寄存器。
// 为什么不用栈返回，而用trapframe保存，并直接跳转。因为trap 是异步事件（比如定时器中断、用户程序异常等），你无法预知它发生在什么地方，也就没办法沿着原函数栈“正常 return”。所以需要一个完全可控的“恢复路径”。
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // 内核根页表
  /*   8 */ uint64 kernel_sp;     // 内核栈顶 stack top
  /*  16 */ uint64 kernel_trap;   // usertrap()地址，从 trampoline 跳进去trap()
  /*  24 */ uint64 epc;           // 保存用户程序 PC
  /*  32 */ uint64 kernel_hartid; // 保存内核 tp（CPU ID）
  /*  40 */ uint64 ra;            // 返回地址
  /*  48 */ uint64 sp;            // 栈指针
  /*  56 */ uint64 gp;            // 全局指针
  /*  64 */ uint64 tp;            // 线程指针
  /*  72 */ uint64 t0;            // t_ 临时寄存器
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;            // s_ 保存寄存器 callee保存
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;            // a_ 参数寄存器，a0 默认返回值。  a_ 一般有caller保存
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;           
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

// 进程状态：未使用、已分配、睡眠中、可运行、运行中、僵尸态
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// 每个进程的状态
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // 进程状态
  void *chan;                  // 若非 0，表示正在 chan 上睡眠
  int killed;                  // 若非 0，表示该进程已被标记为终止
  int xstate;                  // 进程退出状态，用于父进程 wait()
  int pid;                     // 进程id

  // wait_lock must be held when using this:
  struct proc *parent;         // 父进程

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // 内核栈的虚拟地址
  uint64 sz;                   // 用户进程的内存大小（bytes）。p->sz 只是记录了用户进程虚拟地址空间的最大边界（从 0 开始到堆顶），不管中间有没有“空洞”或碎片，它都不管。
  pagetable_t pagetable;       // 用户页表
  struct trapframe *trapframe; // 用于trap的数据页trapframe，供 trampoline.S 使用
  struct context context;      // 进程上下文，调度时使用 swtch 切换
  struct file *ofile[NOFILE];  // 打开的文件表
  struct inode *cwd;           // 当前工作目录
  char name[16];               // 进程名（用于调试）
};
