struct buf {
  int valid;   // has data been read from disk?  数据是否已读入，有效
  int disk;    // does disk "own" buf?
  uint dev;       // 设备号
  uint blockno;   // 硬盘块号
  struct sleeplock lock;
  uint refcnt;    // 引用计数
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

