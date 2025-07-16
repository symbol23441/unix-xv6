#define NPROC        64   // 最多允许的进程数量
#define NCPU          8   // 最多支持的 CPU 数量
#define NOFILE       16   // 每个进程最多打开的文件数
#define NFILE       100   // 系统范围内最多打开的文件数
#define NINODE       50   // 活跃 i-node 的最大数量
#define NDEV         10   // 主设备号的最大数量
#define ROOTDEV       1   // 根文件系统所在磁盘的设备号
#define MAXARG       32   // exec 指令的最大参数个数
#define MAXOPBLOCKS  10   // 文件系统操作最多写入的块数
#define LOGSIZE      (MAXOPBLOCKS*3)  // 磁盘日志中最多的数据块数
#define NBUF         (MAXOPBLOCKS*3)  // 磁盘块缓存的大小
#define FSSIZE       1000 // 文件系统的大小（以块为单位）
#define MAXPATH      128  // 文件路径名的最大长度