// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks | free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// 磁盘disk上的inode结构
struct dinode {
  short type;           // 文件类型
  short major;          // 主设备号（仅对设备文件有效，即 T_DEVICE 类型）
  short minor;          // 次设备号（仅对设备文件有效）
  short nlink;          // 链接数（有多少目录项指向这个 inode）
  uint size;            // 文件大小（以字节为单位）
  uint addrs[NDIRECT+1];   // 数据块地址数组（NDIRECT 个直接块 + 1 个间接块）
};

// 每磁盘块 inode 的个数
#define IPB           (BSIZE / sizeof(struct dinode))

// 计算第i个inode所在的磁盘块
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// 每个块上的bitmap 位数
#define BPB           (BSIZE*8)

// 计算磁盘块b对应的bitmap位所在的块数
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

// 目录项 directory entry
struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

