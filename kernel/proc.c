#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;   // pid锁，保护进程pid的分配

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// 有助于确保不会丢失对调用 wait() 的父进程的唤醒操作。
// 在使用 p->parent 时，有助于遵守内存模型的要求。（内存屏障，多线程变化可见）
// 必须在获取任何 p->lock 之前先获取该锁。
struct spinlock wait_lock;  


// 为每个进程,分配一个内核栈页，并完成的va到pa的PTE映射
// 且每个栈页后紧跟一个守护页（溢出非法验证）
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  // 从高到低安排栈顺序，
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc)); // 内核栈页+守护页的起始地址。2页。
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);// 不主动映射guard page，访问会触发 page fault。 
  }
}

// 在启动的时候初始化进程表(proc table)
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->kstack = KSTACK((int) (p - proc));
  }
}

// 为了保证 r_tp() 获取的是当前实际运行的核心 ID，调用这个函数时必须先禁用中断（interrupts disabled）。
// 以防止进程在调用过程中被迁移到其他CPU，而异常。
int
cpuid()
{
  int id = r_tp();
  return id;
}

// 返回当前cpu的结构
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// 返回当前cpu运行的进程proc
struct proc*
myproc(void) {
  push_off();               
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();                
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// 在进程表中查找一个状态为 UNUSED 的进程。
// 如果找到，则初始化其在内核中运行所需的状态，
// 并在返回时保持对 p->lock 的持有。
// 如果没有空闲进程，或者内存分配失败，则返回 0。
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // 分配一个 trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 分配一个空的页表
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 设置新的上下文，使得调度器切换到该进程时，从 forkret 开始执行，
  // 而 forkret 会最终返回到用户态。
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// 释放一个进程结构及其关联的数据，
// 包括用户空间的内存页。
// 必须持有 p->lock 锁。
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

//  为指定进程创建一个用户页表
//  不包含用户内存，但包含 trampoline 页。
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // 申请一个空页表
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // 将 trampoline 代码（用于系统调用返回）映射到用户虚拟地址空间的最高地址。
  // 只有内核（supervisor）在进入/退出用户空间时使用它，
  // 所以页表项不设置 PTE_U 标志（即用户态不可访问）。
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // 将 trapframe 映射在 TRAMPOLINE 页的正下方，供 trampoline.S 使用。
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// 释放进程页表,并且释放对应的物理内存
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);      //  蹦床代码。由内核映射、进程共享，因此不在这里回收
  uvmunmap(pagetable, TRAPFRAME, 1, 0);       //  跳跃帧，与进程池结构相关，不由用户释放。
  uvmfree(pagetable, sz);                     //  释放页表
}

// 机器码硬编码进内核的数据数组,用于在系统启动时,调用 exec() 启动第一个真正的用户程序 /init
// 因为这是 第一个用户程序，xv6 还没挂载文件系统、没有用户代码可执行。必须靠内核“手动”把这段程序塞进用户内存中。
// od -t xC initcode
// li a0, 0                # argv[0] = 0
// li a1, 0                # argv = 0
// la a2, initpath         # exec 的路径参数
// li a7, SYS_exec         # 系统调用号：exec
// ecall                   # 执行 exec("/init")
// li a0, 0                # exit(0)
// li a7, SYS_exit         # 系统调用号：exit
// ecall
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// 初始化第一个用户进程
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // 分配一个用户页，并将 initcode 的机器码复制到用户空间中
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // 设置 trapframe，为第一次从内核返回到用户态做准备
  p->trapframe->epc = 0;      // 用户的程序计数器pc
  p->trapframe->sp = PGSIZE;  // 用户的栈指针，页顶

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// 增长、收缩用户内存n字节
// 成功返回0，失败返回-1
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// 创建一个新线程，代码和数据为父进程的副本
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // 分配进程
  if((np = allocproc()) == 0){
    return -1;
  }

  // 拷贝用户内存从父进程到子进程
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // 拷贝已保存的用户寄存器
  *(np->trapframe) = *(p->trapframe);

  // 在子进程中fork()返回0
  np->trapframe->a0 = 0;

  // 增加打开文件描述符的引用计数，使子进程共享父进程的文件。
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// 将孤儿子进程交给init进程
// reparent() 保证了不会出现“没人认领”的僵尸子进程，符合 UNIX 的进程生命周期模型：所有孤儿进程最终由 init 收养。
// 调用者必须持有该子进程的wait锁
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// 终止当前进程。该函数不会返回。
// 退出的进程会变成 ZOMBIE 状态，直到其父进程调用 wait() 来回收它。
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  // 释放当前工作目录
  begin_op();     // 启动文件系统操作（加日志锁），可能会因nlink为0，而涉及fs删除文件，需log。
  iput(p->cwd);   // 释放当前工作目录的 inode
  end_op();       // 结束文件系统操作
  p->cwd = 0;

  acquire(&wait_lock);    // 获取 wait_lock 以保护父子关系

  // 将当前进程的所有子进程重新交给 init 进程托管
  reparent(p);

  // 唤醒父进程（可能正在 wait() 等待子进程）
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // 进入进程调度，不再return
  sched();
  panic("zombie exit");
}

// 等待一个子进程的退出，并返回他的pid
// 如果这个进程没有子进程则返回-1
// 通过addr地址，回传进程的退出状态xstate。
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // 扫描进程表，寻找已退出的子进程
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // 确保子进程不处于exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // 如果我们没有任何子进程，等待就没有意义了。
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // 睡眠等待一个子进程退出（会释放wait_lock,唤醒后重新获取）
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// 每个 CPU 的进程调度器。
// 每个 CPU 在完成自身初始化后都会调用 scheduler()。
// scheduler 永不返回。它不断循环，执行以下操作：
//  - 选择一个要运行的进程。
//  - 切换（swtch）到该进程，开始执行。
//  - 最终，该进程通过 swtch 将控制权切换回调度器。
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // 通过确保设备能够被中断，避免死锁
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // 切换到选中的进程。接下来的事情由该进程负责；
        // 它必须释放自己的锁，然后在调回我们(调度器)之前重新获取锁
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // 该进程此时已经暂时运行完毕。
        // 它在返回到这里之前，应该已经修改了它自己的 p->state 状态。
        c->proc = 0;    // 表明当前cpu空闲，并且正在执行调度
      }
      release(&p->lock);
    }
  }
}

// 切换到调度器（scheduler）。必须只持有 p->lock，
// 并且已经修改了 proc->state。
// 之所以保存和恢复 intena，是因为 intena 是这个内核线程的属性，
// 而不是这个 CPU 的属性。严格来说，它应该是 proc->intena 和 proc->noff，
// 但在某些持有锁但当前没有进程的地方，这样做会出错。

// sched 当前进程主动退出、或定时器触发 主动执行进程切换
// 切换回scheduler()
// 由 exit()、yield()、sleep() 其中一个跳入
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)  // 必须在最外层的中断嵌套关闭中
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;  // 暂存中断使能
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// 为了一次进程调度，放弃CPU
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// 调度器第一次调度一个通过 fork 创建的子进程时，会通过 swtch 切换到 forkret 函数。
// 第一个进程初始化，和会从次启动
void
forkret(void)
{
  static int first = 1;

  // 仍然持有进程锁，来自scheduler.
  release(&myproc()->lock);

  if (first) {
    // 文件系统的初始化必须在一个常规进程的上下文中运行（例如，它会调用 sleep），
    // 因此不能在 main() 函数中执行。而在第一次运行进程进行初始化。
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// 原子地释放锁并在 chan 上睡眠。
// 当被唤醒时，会重新获取该锁。
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // 1、必须先获取 p->lock，才能修改 p->state 并调用 sched。
  // 2、一旦我们持有了 p->lock，就可以保证不会错过任何 wakeup（因为 wakeup 也会加锁 p->lock），
  // 防止在此刻到切换到进程调度的期间，错过了wake。这次对应的锁释放回调swich()后，scheduler()中释放
  // 因此，此时释放 lk（sleep 时传入的锁）是安全的。
  acquire(&p->lock);  
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // 清空等待的事件链
  p->chan = 0;

  release(&p->lock);    // 释放调度切换前加上的进程锁
  acquire(lk);          // 重新获取原先的锁。
}

// 唤醒所有在 chan 上睡眠的进程。
// 调用此函数时，不能持有任何 p->lock。
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// 杀死指定 pid 的进程。
// 被杀的进程不会立刻退出，
// 它会在尝试返回用户空间时才会退出（参见 trap.c 中的 usertrap()）。
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // 唤醒睡眠的进程
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// 根据 usr_dst 的值，将数据从内核复制到用户地址或内核地址。
// 成功时返回 0，出错时返回 -1。
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}


// 将数据复制到内核
// 根据 user_src 的值，从用户地址或内核地址复制数据。
// 成功返回 0，失败返回 -1。
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();                            // 获取当前进程，用于访问其页表
  if(user_src){
    return copyin(p->pagetable, dst, src, len);         // 从当前进程页表地址映射，拷贝数据到dst
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// 将进程列表打印到控制台，用于调试。
// 当用户在控制台输入 ^P 时触发运行。
// 不加锁，以避免在系统卡住时进一步死锁。
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
