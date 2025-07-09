// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// ELF 文件头结构（ELF Header）
// 描述整个 ELF 文件的基本信息，是 ELF 文件的开头部分
struct elfhdr {
  uint   magic;       // 魔数，必须为 ELF_MAGIC（用于标识这是一个 ELF 文件）
  uchar  elf[12];     // ELF 标识信息（包含 ELF 版本、架构等），通常固定格式
  ushort type;        // 文件类型（如可执行文件 ET_EXEC，目标文件 ET_REL 等）
  ushort machine;     // 指定运行该 ELF 文件所需的机器架构（如 RISC-V、x86）
  uint   version;     // ELF 文件格式的版本号（通常为 1）
  uint64 entry;       // 程序入口地址（进程开始执行的虚拟地址）
  uint64 phoff;       // 程序头表（Program Header Table）在文件中的偏移
  uint64 shoff;       // 节头表（Section Header Table）在文件中的偏移（xv6 不使用）
  uint   flags;       // 特定架构相关的标志位（一般为 0）
  ushort ehsize;      // ELF 文件头本身的大小（单位：字节，通常是 64）
  ushort phentsize;   // 每个程序头（Program Header）的大小（单位：字节）
  ushort phnum;       // 程序头的个数（即可加载段的数量）
  ushort shentsize;   // 每个节头（Section Header）的大小（单位：字节）
  ushort shnum;       // 节头的数量（Section Header 数量）
  ushort shstrndx;    // 节名称字符串表在节头表中的索引
};

// 程序段头结构（Program Header），用于描述 ELF 文件中每一个可加载段。
// 用elf hdr 中phoff[phnum]顺序读取加载段
struct proghdr {
  uint32 type;     // 段的类型，比如 LOAD（可加载段）、DYNAMIC、INTERP 等
  uint32 flags;    // 段的访问权限，比如可读（R）、可写（W）、可执行（X）
  uint64 off;      // 段在文件中的偏移（从文件起始位置算起）
  uint64 vaddr;    // 段在内存中的虚拟地址（加载后放置的位置）
  uint64 paddr;    // 段的物理地址（通常忽略，在某些平台/嵌入系统中用到）
  uint64 filesz;   // 段在文件中占用的字节数（即实际存储在 ELF 文件中的大小）
  uint64 memsz;    // 段在内存中应占用的字节数（可能比 filesz 大，比如包含 .bss）
  uint64 align;    // 段对齐方式，通常是页大小（如 0x1000），用于内存和文件中对齐
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
