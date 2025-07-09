OUTPUT_ARCH( "riscv" )
ENTRY( _entry )  /* [1]表明入口程序是_entry该代码段 */

SECTIONS
{
  /*
   * ensure that entry.S / _entry is at 0x80000000,
   * where qemu's -kernel jumps.
   * [2] 入口从0x80000000该地址开始
   */
  . = 0x80000000;

  /*
   * .text 段：包含所有代码段，包括普通函数和 trampoline（页对齐）。
   * trampoline（页表切换时的跳板）强制要求大小不能超过一页。
   */
  .text : {
    *(.text .text.*)
    . = ALIGN(0x1000);
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .);
  }

  /*
   * .rodata 段：只读数据，如字符串常量、const 变量。
   */
  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  /*
   * .data 段：已初始化的全局变量和静态变量。
   */
  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  /*
   * .bss 段：未初始化的全局变量和静态变量，运行时初始化为 0。
   */
  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  /*
   * 提供一个符号 end，表示程序段结束位置，供内核分配堆内存使用。
   */
  PROVIDE(end = .);
}
