// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 哈希表 桶好索引数
#define NBUF_BUCKET 13

// hash f
#define BUFBUCKET_HASH(dev,blockno)  ((((dev)<<27)|(blockno))%NBUF_BUCKET)

// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   struct buf head;
// } bcache;

struct {
  struct buf buf[NBUF];
  struct buf bufbucket[NBUF_BUCKET];     // buf hash 桶, 哨兵位
  struct spinlock bufbucketlock[NBUF_BUCKET];   // 桶锁
  struct spinlock eviction_lock;            // 驱逐锁. 解决AB、AA 死锁
                                            // 改用时间戳，实现LRU. b->visited_timestamp
} bcache;                                   

void
binit(void)
{
  struct buf *b;

  for(int i=0;i<NBUF_BUCKET;i++){
    initlock(&bcache.bufbucketlock[i], "bcache.bufbucket");
    bcache.bufbucket[i].next = 0;
  }

  // 初始化全加到bucket0中
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->visited_timestamp = 0;
    b->refcnt = 0;
    b->valid = 0;

    b->next = bcache.bufbucket[0].next;
    bcache.bufbucket[0].next = b; 
    initsleeplock(&b->lock, "buffer");
  }
  initlock(&bcache.eviction_lock,"bcache.eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint bucketno = BUFBUCKET_HASH(dev, blockno);
  acquire(&bcache.bufbucketlock[bucketno]);

  // 该缓存块是否已缓存？
  for(b = bcache.bufbucket[bucketno].next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bufbucketlock[bucketno]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  //-------------未缓存-------------
  // 从全局驱逐一个LRU缓存块，进行复用
  release(&bcache.bufbucketlock[bucketno]); // 释放A锁。解决AB，BA的死锁问题。
  acquire(&bcache.eviction_lock);           // 驱逐锁。

  // 再次检查，防止释放A锁和驱逐锁的间隙，出现多副本创建
  acquire(&bcache.bufbucketlock[bucketno]);
  for(b = bcache.bufbucket[bucketno].next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bufbucketlock[bucketno]);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bufbucketlock[bucketno]);


  // 仍无缓存
  // 全局遍历寻找可用LRU块。 并一直持有LRU块的桶锁，保证不重复复用
  struct buf *leastbuf_prenode = 0;
  uint64 least_buf_timestamp = 0xffffffffffffffff;
  int whichbucket = -1;
  for(int i = 0 ; i<NBUF_BUCKET;i++){
    acquire(&bcache.bufbucketlock[i]);
    for(b = &bcache.bufbucket[i];b->next!=0;b=b->next){
      if(b->next->refcnt == 0 && b->next->visited_timestamp < least_buf_timestamp){
        leastbuf_prenode = b;
        least_buf_timestamp = b->next->visited_timestamp;
        if(whichbucket != -1 && whichbucket != i)
          release(&bcache.bufbucketlock[whichbucket]);
        whichbucket = i;
      }
    }
    if(whichbucket != i)
      release(&bcache.bufbucketlock[i]);
  }// 最终获取一个LRU的前向节点，并持有所在桶的锁

  if(!leastbuf_prenode)
    panic("bget: no buffers");  // 无可用节点
  
  // 驱逐操作
  if(whichbucket == bucketno){  // 驱逐节点在本桶上
    b = leastbuf_prenode->next;
    b->dev = dev;
    b->blockno = blockno;
    b->refcnt = 1;
    b->valid  = 0;
    release(&bcache.bufbucketlock[whichbucket]);
  }else{                        // 驱逐节点在其他桶上
    // 从B移除
    b = leastbuf_prenode->next;
    leastbuf_prenode->next = b->next;
    release(&bcache.bufbucketlock[whichbucket]);

    //初始化
    b->dev = dev;
    b->blockno = blockno;
    b->refcnt = 1;
    b->valid  = 0;

    
    // 加入A
    acquire(&bcache.bufbucketlock[bucketno]);
    b->next = bcache.bufbucket[bucketno].next;
    bcache.bufbucket[bucketno].next = b;

    release(&bcache.bufbucketlock[bucketno]);
  }

  release(&bcache.eviction_lock);
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint bucketno = BUFBUCKET_HASH(b->dev, b->blockno);
  acquire(&bcache.bufbucketlock[bucketno]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->visited_timestamp = ticks;
  }
  release(&bcache.bufbucketlock[bucketno]);
}

// 用于文件系统，在事务提交前，不被brelse释放（引用+1）
void
bpin(struct buf *b) {
  uint bucketno = BUFBUCKET_HASH(b->dev, b->blockno);
  acquire(&bcache.bufbucketlock[bucketno]);
  b->refcnt++;
  release(&bcache.bufbucketlock[bucketno]);
}

void
bunpin(struct buf *b) {
  uint bucketno = BUFBUCKET_HASH(b->dev, b->blockno);
  acquire(&bcache.bufbucketlock[bucketno]);
  b->refcnt--;
  release(&bcache.bufbucketlock[bucketno]);
}


