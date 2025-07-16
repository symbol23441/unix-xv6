// 进程的长周期锁
struct sleeplock {
  uint locked;       // 是否已加锁
  struct spinlock lk; // 保护此sleep lock的自旋锁
  
  // For debugging:
  char *name;        // 锁的名称
  int pid;           // 持有锁的进程
};

