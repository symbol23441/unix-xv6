//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100
#define C(x)  ((x)-'@')  // 意为：control键+ X字符 。例，Ctrl+A ascii码为1, '@' 为64，‘A’为65。

//
// 发送一个字符到 UART（串口）。
// 会被 printf 调用，也会用于回显用户输入的字符，
// 但不会被 write() 系统调用使用。
//
void
consputc(int c)
{
  if(c == BACKSPACE){
    // 如果用户按的是退格键（BACKSPACE），不能直接删除字符，因为串口是“只能往前发”的。
    // 因此，先光标左移一格，在覆盖，再光标左移。 其中，\b 光标左移。 
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;
  
// input
#define INPUT_BUF 128
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} cons;

//
// user write()s to the console go here.
// 用户程序输出到控制台的信息处理
//
int
consolewrite(int user_src, uint64 src, int n)
{
  int i;

  for(i = 0; i < n; i++){
    char c;
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    uartputc(c);
  }

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
// 用户对控制台执行 read() 调用时，会进入这里。
// 将（一整行以内的）输入内容复制到 dst 中。
// user_dst 表示 dst 是用户空间地址还是内核地址。
// 
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while(cons.r == cons.w){
      if(myproc()->killed){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF];

    if(c == C('D')){  // end-of-file
      if(n < target){
        // 保留 ^D  到下次处理，确保read()返回0表示EOF
        // 根本原因是--UNIX 设计约定：只有当 read() 返回 0 字节时，才表示 EOF。
        // 若不保留到下次，用户不认为EOF继续读，则会陷入上一个循环死等。
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      // 整行读入已经完成，返回到用户级别去处理
      // 根本原因是UNIX中，终端默认是“规范模式”（canonical mode），输入是以行为单位处理的。
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// 中断触发
// uartintr() 在接收到输入字符时调用它。
// 执行擦除（backspace）/ 清除整行（kill line）等处理，
// 将字符追加到 cons.buf 缓冲区，
// 如果一整行输入完成，则唤醒 consoleread()。
//
void
consoleintr(int c)
{
  acquire(&cons.lock);

  switch(c){
  case C('P'):  // Print process list. （control + P）
    procdump();
    break;
  case C('U'):  // 清空当前行 （control + U） 
    while(cons.e != cons.w &&
          cons.buf[(cons.e-1) % INPUT_BUF] != '\n'){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f':
    if(cons.e != cons.w){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if(c != 0 && cons.e-cons.r < INPUT_BUF){
      c = (c == '\r') ? '\n' : c;

      // echo back to the user. 回显。
      consputc(c);

      // store for consumption by consoleread(). 缓存给控制台读取
      cons.buf[cons.e++ % INPUT_BUF] = c;

      if(c == '\n' || c == C('D') || cons.e == cons.r+INPUT_BUF){
        // 如果一整行（或文件结束）已到达、结束标识符、缓存上限，唤醒 consoleread()
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }
  
  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");

  // 初始化UART---(通用异步收发器)硬件设备,最常见的串口设备,能收发字符
  uartinit();
  

  // 将 read 和 write 系统调用连接到 consoleread 和 consolewrite。
  devsw[CONSOLE].read = consoleread;          // 用于设备读，进程读取控制台的系统调用
  devsw[CONSOLE].write = consolewrite;        // 用于设备写，进程输出到控制台
}
