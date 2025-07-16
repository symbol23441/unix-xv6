// Mutual exclusion lock. 互斥锁
struct spinlock {
  uint locked;       // 是否已被锁

  // For debugging:
  char *name;        // 锁的名称
  struct cpu *cpu;   // 持有锁的CPU
};

