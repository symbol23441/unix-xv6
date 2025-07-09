// file ：是inode引用+打开状态
struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// inode 的内存副本
struct inode {
  uint dev;              // 设备号（所在的块设备）
  uint inum;             // inode 编号（在磁盘 inode 表中的位置）
  int ref;               // 引用计数（有多少地方引用了这个 inode）
  struct sleeplock lock; // 睡眠锁，用于保护下方所有字段

  int valid;             // 是否已从磁盘读取 inode 内容？

  short type;            // 文件类型（磁盘inode的副本）
  short major;           // 主设备号（仅用于设备文件）
  short minor;           // 次设备号（仅用于设备文件）
  short nlink;           // 硬链接数量（有多少路径名指向该 inode）
  uint size;             // 文件大小（单位：字节）
  uint addrs[NDIRECT+1]; // 数据块地址（NDIRECT 是直接块数量，+1 是间接块）
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
