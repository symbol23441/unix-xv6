// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

// 文件系统的实现。共分为五层：
//   + 块层：原始磁盘块的分配器。
//   + 日志层：用于多步骤更新的崩溃恢复机制。
//   + 文件层：inode 分配器、读写操作、元数据管理。
//   + 目录层：特殊内容的 inode（存储其他 inode 的列表！）
//   + 名字层：处理像 /usr/rtm/xv6/fs.c 这样的路径，提供方便的命名方式。
//
// 本文件包含文件系统的底层操作函数。
// 更高层的系统调用实现位于 sysfile.c 文件中。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

// 每个磁盘设备应该对应一个超级块（superblock），但我们实际上只使用一个设备
struct superblock sb; 

// Read the super block.
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Init fs
void
fsinit(int dev) {
  readsb(dev, &sb);
  if(sb.magic != FSMAGIC)
    panic("invalid file system");
  initlog(dev, &sb);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a table of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The in-memory
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in table: an entry in the inode table
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a table entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   table entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays in the table and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The itable.lock spin-lock protects the allocation of itable
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold itable.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

// Inode（索引节点）
//
// 一个 inode 描述了一个未命名的文件。
// inode 的磁盘结构保存了元数据：文件的类型、大小、引用它的链接数，
// 以及保存文件内容的块号列表。
//
// inode 在磁盘上的布局是顺序排列的，从 sb.startinode 开始。
// 每个 inode 都有一个编号，表示它在磁盘中的位置。
//
// 内核在内存中维护了一个“正在使用的 inode 表”，
// 用于同步多个进程对 inode 的访问。
// 内存中的 inode 包含一些磁盘上没有的信息：比如 ip->ref 和 ip->valid。
//
// 一个 inode 及其内存表示在文件系统中使用之前，会经历一系列状态转换：
//
// * 分配（Allocation）：当 inode 的类型（在磁盘上）非零时，表示它已被分配。
//   ialloc() 用于分配 inode，iput() 用于释放（当引用计数和链接数都为 0）。
//
// * 引用表中（Referencing in table）：
//   如果 ip->ref 为 0，则该 inode 表项空闲；否则，ip->ref 表示当前有多少个内存指针引用它
//   （如打开的文件、当前目录等）。
//   iget() 查找或创建一个表项，并增加其 ref；iput() 则减少 ref。
//
// * 有效（Valid）：
//   inode 表项中的信息（类型、大小等）只有在 ip->valid 为 1 时才是正确的。
//   ilock() 会从磁盘读取 inode 并设置 ip->valid，
//   而 iput() 如果发现 ref 变为 0，就清除 ip->valid。
//
// * 加锁（Locked）：
//   文件系统代码在访问或修改 inode 及其内容前，必须先获取该 inode 的锁。
//
// 因此，典型的使用流程如下：
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... 访问或修改 ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() 与 iget() 是分开的，这样系统调用可以长期持有 inode 引用
// （例如打开的文件），但只在需要时短暂加锁（例如在 read() 中）。
// 这种分离还可以避免死锁和路径查找过程中的竞态条件。
// iget() 会增加 ip->ref，确保 inode 保持在表中，指针始终有效。
//
// 很多文件系统内部函数要求调用者在操作 inode 前已经加锁；
// 这样可以让调用者实现多步骤的原子操作。
//
// itable.lock（自旋锁）用于保护 inode 表中条目的分配。
// 由于 ip->ref 表示一个条目是否空闲，ip->dev 和 ip->inum 表示该条目属于哪个 inode，
// 所以在访问这些字段时必须持有 itable.lock。
//
// ip->lock（睡眠锁）保护 ip 的其他字段，除了 ref、dev 和 inum 之外。
// 只有持有 ip->lock 才能读写该 inode 的 ip->valid、ip->size、ip->type 等字段。
struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} itable;  // 实际上是运行时inode缓冲池（表）。管理活跃的inode

void
iinit()
{
  int i = 0;
  
  initlock(&itable.lock, "itable");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&itable.inode[i].lock, "inode");
  }
}

static struct inode* iget(uint dev, uint inum);

// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode.
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// 根据指定的 设备号 dev 和 inode 编号 inum，从内核 inode 表（itable）中返回一个对应的 内存 inode 结构指针。
// 如果已经存在，则增加引用计数；如果不存在，则从空槽中分配一个，并初始化。
// 不会对该 inode 加锁，也不会从磁盘读取该 inode 的内容。
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock);

  // inode 已存在itable表中
  empty = 0;
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&itable.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // 记录一个可用的空inode slot
      empty = ip;
  }

  // 回收（重用）一个 inode 项目
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&itable.lock);

  return ip;
}

// inode duplicate，增加 ip 的引用计数。
// 返回 ip，以支持 ip = idup(ip1) 这种用法（编程习惯）。
struct inode*
idup(struct inode *ip)
{
  acquire(&itable.lock);
  ip->ref++;
  release(&itable.lock);
  return ip;
}


// 锁住给出的inode
// 如果必要，从磁盘读出inode
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// 解锁给定的inode
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// 释放一个对内存中 inode 的引用。（用于是否保留inode）
// 索引-1 ；若是最后一个索引、inode有效、硬链接为0，则释放磁盘空间、更新inode
// 如果同时该 inode 在磁盘上也没有链接（nlink 为 0），则释放磁盘上的 inode 及其内容块。
// 所有对 iput() 的调用都必须放在事务中（即 begin_op/end_op 之间），以防需要释放 inode 时修改磁盘数据。
void
iput(struct inode *ip)
{
  acquire(&itable.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    // 表示该 inode 没有人引用，也没有文件指向它，可以释放它了

    // 因为 ref == 1，说明没有其他进程使用该 inode，
    // 所以可以放心加锁，不会死锁或阻塞
    acquiresleep(&ip->lock);

    release(&itable.lock);

    itrunc(ip);       // 释放 inode 对应的数据块
    ip->type = 0;     // 标记 inode 类型为 0，表示空闲
    iupdate(ip);      // 将更改写回磁盘
    ip->valid = 0;    // 失效标志，表明内存中的内容也不再有效

    releasesleep(&ip->lock);

    acquire(&itable.lock);
  }

  ip->ref--;
  // 内存活跃inode不会在 iput() 里直接释放（free）或移除，而是等下一次需要分配 inode 时，统一在 iget() 里寻找 ref == 0 的条目进行复用
  release(&itable.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// 截断inode (丢弃内容)。它负责删除 inode 关联的所有数据块，使文件变为空文件（长度为 0）。
// 调用者必须持有锁
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// 从inode复制file stat信息
// 调用者必须持有ip->lock
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

// 从 inode 中读取数据。
// 调用者必须持有 ip->lock 锁。
// 如果 user_dst 为 1，则 dst 是用户虚拟地址；否则，dst 是内核地址。
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

// Write data to inode.
// Caller must hold ip->lock.
// If user_src==1, then src is a user virtual address;
// otherwise, src is a kernel address.
// Returns the number of bytes successfully written.
// If the return value is less than the requested n,
// there was an error of some kind.
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if(off > ip->size)
    ip->size = off;

  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  iupdate(ip);

  return tot;
}

// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// 在一个目录中查找指定的目录项。
//如果找到，返回该节点的inode指针，将该目录项在目录文件中的偏移量写入 *poff。
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0) // inum 0 表示该目录项未使用、或unlink删除
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}


// 写一个新目录(name,inum)项到父目录中
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

// 路径处理

// 将路径中的下一个路径元素复制到 name 中。
// 返回指向下一个未处理路径部分的指针。
// 返回的路径字符串中不会包含前导的斜杠，
// 因此调用者可以通过检查 *path == '\0' 来判断当前 name 是否是最后一个路径部分。
// 如果路径中没有可提取的名字（例如路径为空或仅包含斜杠），则返回 0。
// 示例：
//   skipelem("a/bb/c", name) = "bb/c"，name 被设为 "a"
//   skipelem("///a//bb", name) = "bb"，name 被设为 "a"
//   skipelem("a", name) = ""，name 被设为 "a"
//   skipelem("") = skipelem("////", name) = 0（无有效路径元素）
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// 查找并返回某个路径名对应的 inode。
// 如果 parent != 0，则返回父目录的 inode，并将路径中的最后一个部分（文件名）复制到 name 中，name 必须有足够的空间容纳 DIRSIZ 字节。
// 必须在文件系统事务（transaction）中调用本函数，因为它会调用 iput()（释放 inode 可能会修改磁盘元数据）。
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO); // '/'inode固定
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
