//
// virtio device definitions.
// for both the mmio interface, and virtio descriptors.
// only tested with qemu.
// this is the "legacy" virtio interface.
//
// the virtio spec:
// https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf
//

// virtio MMIO 控制寄存器，从 0x10001000 开始映射
// 来自 qemu 的 virtio_mmio.h
#define VIRTIO_MMIO_MAGIC_VALUE         0x000 // 魔数：固定值 0x74726976（"virt"）
#define VIRTIO_MMIO_VERSION             0x004 // 版本号：1 表示使用 legacy 接口
#define VIRTIO_MMIO_DEVICE_ID           0x008 // 设备类型：1=网络，2=磁盘
#define VIRTIO_MMIO_VENDOR_ID           0x00c // 厂商 ID：0x554d4551（"QEMU"）
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010 // 设备支持的功能位（驱动读取）
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020 // 驱动支持的功能位（驱动写入）
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028 // 驱动设置的页大小（单位：字节）
#define VIRTIO_MMIO_QUEUE_SEL           0x030 // 选择要操作的队列编号（写入）
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034 // 当前队列的最大长度（读取）
#define VIRTIO_MMIO_QUEUE_NUM           0x038 // 设置实际使用的队列长度（写入）
#define VIRTIO_MMIO_QUEUE_ALIGN         0x03c // 设置 used 队列的对齐方式（写入）
#define VIRTIO_MMIO_QUEUE_PFN           0x040 // 设置队列的页帧号（物理页地址 >> 12）
#define VIRTIO_MMIO_QUEUE_READY         0x044 // 设置队列是否就绪（写 1 表示 ready）
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050 // 通知设备处理某个队列（写入队列号）
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060 // 中断状态寄存器（读取）
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064 // 确认中断已处理（写入）
#define VIRTIO_MMIO_STATUS              0x070 // 设备状态寄存器（读/写）

// 状态寄存器的各个位定义，来源于 qemu 的 virtio_config.h
#define VIRTIO_CONFIG_S_ACKNOWLEDGE   1   // 驱动已检测到设备存在（初始握手第一步）
#define VIRTIO_CONFIG_S_DRIVER        2   // 驱动已经准备好并理解了设备
#define VIRTIO_CONFIG_S_DRIVER_OK     4   // 驱动已初始化完成，可以开始传输数据
#define VIRTIO_CONFIG_S_FEATURES_OK   8   // 功能协商已完成（驱动与设备支持的功能达成一致）

// 设备功能位（来自 virtio 规范）
#define VIRTIO_BLK_F_RO               5   // 设备为只读磁盘（Read-Only）
#define VIRTIO_BLK_F_SCSI             7   // 支持 SCSI 命令透传（scsi passthrough）
#define VIRTIO_BLK_F_CONFIG_WCE      11   // 配置区可设置写回缓存（Writeback Cache Enable）
#define VIRTIO_BLK_F_MQ              12   // 支持多个 virtqueue（多队列 I/O 支持）
#define VIRTIO_F_ANY_LAYOUT          27   // 支持任意描述符布局（非固定格式）
#define VIRTIO_RING_F_INDIRECT_DESC  28   // 支持间接描述符（提升性能）
#define VIRTIO_RING_F_EVENT_IDX      29   // 支持事件通知索引（减少中断）

// 使用的 virtio 描述符数量。必须是2的幂，目的 % NUM 运算可以用按位与替代。
#define NUM 8

// 一个单独的描述符，来自 virtio 规范。
struct virtq_desc {
  uint64 addr;
  uint32 len;
  uint16 flags;
  uint16 next;
};
#define VRING_DESC_F_READ 0   // 表示设备将从该地址读数据
#define VRING_DESC_F_NEXT  1  // 表示该描述符后面还有下一个（即形成一个链条）
#define VRING_DESC_F_WRITE 2  // 表示设备将往该地址写数据

// （完整的）avail 队列结构，来源于 virtio 规范。
//  设备开始操作的idx编号由，设备自己记录
struct virtq_avail {
  uint16 flags;        // 永远为 0（保留字段）
  uint16 idx;          // 驱动将在 ring[idx] 位置写入下一个请求
  uint16 ring[NUM];    // 描述符链的头部编号数组（告诉设备要处理哪些请求）
  uint16 unused;       // 未使用（对齐用）
};

// “used” 队列中的一个条目，
// 设备通过它告知驱动哪些请求已完成。
struct virtq_used_elem {
  uint32 id;   // 已完成的描述符链的起始索引（即链头 desc 的编号）
  uint32 len;  // 实际传输的字节数（由设备填写）
};

// “used” 队列的结构体，由设备填写。
// 用于通知驱动：哪些请求已经完成。
struct virtq_used {
  uint16 flags; // 永远为 0（保留字段）
  uint16 idx;   // 当前写入到 ring[] 的位置（设备每写入一个元素就加 1）
  struct virtq_used_elem ring[NUM]; // 存储设备已完成的请求条目（一个个 virtq_used_elem）
};

// 这些是 virtio 块设备（如磁盘）特有的定义，
// 详见 virtio 规范第 5.2 节。
#define VIRTIO_BLK_T_IN   0  // 从磁盘读取（读请求）
#define VIRTIO_BLK_T_OUT  1  // 向磁盘写入（写请求）

// 磁盘请求中第一个描述符的数据结构格式。
// 后面将跟两个描述符：一个用于数据块，一个用于 1 字节状态字节。
// to be followed by two more descriptors containing,the block, and a one-byte status.
struct virtio_blk_req {
  uint32 type;     // 请求类型：读（IN）或写（OUT）
  uint32 reserved; // 保留字段（必须为 0）
  uint64 sector;   // 要读/写的磁盘扇区号（以 512 字节为单位）
};