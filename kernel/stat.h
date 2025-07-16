#define T_DIR     1   // 目录类型
#define T_FILE    2   // 普通文件类型
#define T_DEVICE  3   // 设备文件类型

struct stat {
  int dev;         // 所在文件系统的磁盘设备号
  uint ino;        // inode 编号
  short type;      // 文件类型（T_DIR, T_FILE, T_DEVICE）
  short nlink;     // 指向该文件的硬链接数量
  uint64 size;     // 文件大小（以字节为单位）
};
