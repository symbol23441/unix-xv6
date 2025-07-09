//
// QEMU 的 virtio 磁盘设备的驱动程序。
// 使用 QEMU 提供的 virtio 的 MMIO 接口。
// QEMU 提供的是一个“传统”（legacy）virtio 接口。
//
// QEMU 启动命令示例：
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"

// virtio MMIO 寄存器 r 的地址。   MMIO 内存映射输入输出
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

static struct disk {
  // virtio 驱动和设备主要通过一组驻留在内存中的结构体进行通信。
  // pages[] 用于分配这块内存。
  // pages[] 被定义为一个全局变量（而不是每次调用 kalloc() 动态分配），
  // 是因为它必须是两个连续的、页对齐的物理内存页。
  char pages[2*PGSIZE];

  // pages[] 被划分为三个区域（描述符区、可用队列区和已用队列区），
  // 具体内容可参考 virtio 规范（传统接口）第 2.6 节。
  // https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf

  // pages[] 的第一个区域是一组 DMA 描述符（不是环形结构），
  // 驱动通过这些描述符告诉设备在哪读写每一个磁盘操作的数据。
  // 一共分配了 NUM 个描述符。
  // 大多数命令由若干个描述符“链”（即一个链表）组成。
  // desc 指针指向 pages[] 的这一部分。
  struct virtq_desc *desc;      // 描述”内存哪块数据“用来发送或接收数据。

  // 接下来是一个环形队列（ring），驱动在其中写入希望设备处理的描述符编号。这里只写入每个描述符链的头部描述符编号。
  // 该环形队列共有 NUM 个元素。
  // avail 指针指向 pages[] 中的这一部分。
  struct virtq_avail *avail;

  // 最后是一个环形队列，设备在其中写入它已处理完成的。描述符编号（仅包括每个链的头部编号）。
  // 这个 used 队列中有 NUM 个条目。used 指针指向 pages[] 中的这一部分。
  struct virtq_used *used;

  // our own book-keeping.
  char free[NUM];    // 描述符空闲状态标志
  uint16 used_idx;   // 驱动已处理处理回调的索引

  // 跟踪正在进行的磁盘操作，以便在中断到来时进行处理。
  // 通过描述符链的第一个描述符索引来索引该数组。只与一组操作链的链头对应。
  struct {
    struct buf *b;   // 指向对应的缓存块（请求的数据缓冲区）
    char status;     // 请求完成状态（由设备写入）
  } info[NUM];


  // command headers. 磁盘的命令头disk. 描述符链desc第一个操作指令内容，下标对应。
  struct virtio_blk_req ops[NUM]; // 存放你要发给磁盘设备的“指令”内容
  
  struct spinlock vdisk_lock;
  
} __attribute__ ((aligned (PGSIZE))) disk;

void
virtio_disk_init(void)
{
  uint32 status = 0;

  initlock(&disk.vdisk_lock, "virtio_disk");              // 初始化磁盘锁（保护磁盘操作）

  // 检查 virtio 设备的标识信息，确保是 virtio 磁盘
  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||     // "virt" 的 魔术码
     *R(VIRTIO_MMIO_VERSION) != 1 ||                  // 使用 legacy 接口
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||                // 设备类型：2 表示块设备
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){        // QEMU 提供的厂商 ID
    panic("could not find virtio disk");
  }
  
  // 设置状态寄存器：ACKNOWLEDGE（已识别设备）
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 设置状态寄存器：DRIVER（已加载驱动）
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // -----------------------------
  // 协商特性（features negotiation）
  // -----------------------------
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);// 获取设备支持的特性
  // 去除驱动不支持的功能位（这些位为1表示设备支持，但驱动不想用）
  features &= ~(1 << VIRTIO_BLK_F_RO);             // 不接受只读设备
  features &= ~(1 << VIRTIO_BLK_F_SCSI);           // 不支持 SCSI 指令
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);     // 不启用写缓存控制
  features &= ~(1 << VIRTIO_BLK_F_MQ);             // 不启用多队列
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);         // 不使用任意内存布局
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);     // 不使用事件索引
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC); // 不使用间接描述符
  // 将协商后的特性写入寄存器
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // 告诉设备协商已完成
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 告诉设备驱动已准备就绪
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 告诉设备页大小（用于 PFN）
  *R(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;

  // -----------------------------
  // 初始化 virtqueue 0
  // -----------------------------
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;                  // 选择队列 0
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);     // 获取设备支持的最大队列大小
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;                // 设置VIRTIO队列大小（NUM 是我们定义的描述符数量）
  memset(disk.pages, 0, sizeof(disk.pages));      // 清零用于队列的内存区域
  *R(VIRTIO_MMIO_QUEUE_PFN) = ((uint64)disk.pages) >> PGSHIFT;    // 设置队列的物理页帧号（PFN）

  // desc = pages -- num 个 virtq_desc 结构（描述符数组）
  // avail = pages + 0x40 -- 2 个 uint16（flags 和 idx），然后是 num 个 uint16（ring[]）
  // used = pages + 4096 -- 2 个 uint16（flags 和 idx），然后是 num 个 vRingUsedElem（ring[]）
  disk.desc = (struct virtq_desc *) disk.pages;
  disk.avail = (struct virtq_avail *)(disk.pages + NUM*sizeof(struct virtq_desc));
  disk.used = (struct virtq_used *) (disk.pages + PGSIZE);

  // 初始化描述符状态为“空闲”
  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // 剩下的部分：中断绑定在 plic.c 和 trap.c 中注册。 plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}

// 获取一个空闲的desc索引idx，标记为非空间
static int
alloc_desc()
{
  for(int i = 0; i < NUM; i++){
    if(disk.free[i]){
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

// 回收描述符
static void
free_desc(int i)
{
  if(i >= NUM)
    panic("free_desc 1");
  if(disk.free[i])
    panic("free_desc 2");
  disk.desc[i].addr = 0;
  disk.desc[i].len = 0;
  disk.desc[i].flags = 0;
  disk.desc[i].next = 0;
  disk.free[i] = 1;
  wakeup(&disk.free[0]); // 唤醒等待空闲描述符的进程
}

// 释放一条描述符链
static void
free_chain(int i)
{
  while(1){
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if(flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// 分配三个描述符（它们不需要是连续的）
// 每次磁盘传输操作总是使用三个描述符
static int
alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

void
virtio_disk_rw(struct buf *b, int write)
{
  // 将逻辑块号转换为磁盘扇区号（每个块为 BSIZE 字节，硬件每扇区 512 字节）
  uint64 sector = b->blockno * (BSIZE / 512);

  acquire(&disk.vdisk_lock);


  // 根据 virtio 规范第 5.2 节，传统 block 请求使用 3 个描述符：
  // 1）命令头部结构 2）数据缓冲区 3）1 byte 状态标志
  //one for type/reserved/sector, one for thedata, one for a 1-byte status result.

  // 分配3个desc
  int idx[3];
  while(1){
    if(alloc3_desc(idx) == 0) {
      break;
    }
    // 如果分配失败，休眠等待 free[] 被唤醒（说明有空闲描述符了）
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  // 构造 3 个描述符
  // QEMU 的 virtio-blk.c 会读取这些内容
  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

  if(write)
    buf0->type = VIRTIO_BLK_T_OUT; // write the disk
  else
    buf0->type = VIRTIO_BLK_T_IN; // read the disk
  buf0->reserved = 0;
  buf0->sector = sector;

  // 第一个描述符指向 ops 命令头
  disk.desc[idx[0]].addr = (uint64) buf0;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  // 第二个描述符指向数据缓冲区
  disk.desc[idx[1]].addr = (uint64) b->data;
  disk.desc[idx[1]].len = BSIZE;
  if(write)
    disk.desc[idx[1]].flags = VRING_DESC_F_READ;  // 写操作：设备读取 b->data
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // 读操作：设备写入 b->data
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  // 第三个描述符：状态字节（由设备写入 0 表示成功）
  disk.info[idx[0]].status = 0xff; // 设备执行成功，会修改为0
  disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // 设备写入该状态
  disk.desc[idx[2]].next = 0;

  // 记录该请求对应的 buf，供中断处理函数使用 virtio_disk_intr().
  b->disk = 1;
  disk.info[idx[0]].b = b;

  // 将描述符链的头索引 idx[0] 加入 avail ring，通知设备准备处理
  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  __sync_synchronize(); // 内存屏障，确保 avail->ring 写入完成

  // 更新 avail->idx，表示新请求已经准备好
  disk.avail->idx += 1; // not % NUM（接口规范表明）

  __sync_synchronize(); // 再次屏障，确保所有内存可见

  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // 通知设备队列 0 中有新请求

  // 睡眠等待中断处理函数唤醒，b->disk == 0 表示请求完成
  while(b->disk == 1) {
    sleep(b, &disk.vdisk_lock);
  }

  // 请求完成，清理状态
  disk.info[idx[0]].b = 0;
  free_chain(idx[0]);           // 释放描述符链

  release(&disk.vdisk_lock);    // 解锁
}

void
virtio_disk_intr()
{
  acquire(&disk.vdisk_lock);

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();        // 内存屏障，防止编译器或 CPU 重排读写操作

  // 
  // 当设备往 used ring 添加新条目时，会增加 used->idx。（DMA外设执行完后，自己修改used->idx++）
  // 

  while(disk.used_idx != disk.used->idx){               // 说明还有未处理的完成条目
    __sync_synchronize();                               // 内存屏障，确保读取顺序
    int id = disk.used->ring[disk.used_idx % NUM].id;   // 得到描述符链的头部索引

    if(disk.info[id].status != 0)                       // 0 表示执行成功
      panic("virtio_disk_intr status");

    struct buf *b = disk.info[id].b;
    b->disk = 0;                                        // 表示磁盘操作已完成，可由唤醒的进程检查使用
    wakeup(b);                                          // 唤醒等待该缓存的进程

    disk.used_idx += 1;
  }

  release(&disk.vdisk_lock);
}
