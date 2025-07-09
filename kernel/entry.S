# qemu -kernel 会将内核加载到地址 0x80000000，并使每个 CPU 从该地址开始执行。
# kernel.ld（链接脚本）确保以下代码被放置在 0x80000000

# _entry为C语言代码环境设置栈，运行在 Machine 模式（M-mode）。
# 因为C语言运行必须依赖栈，因为函数调用、局部变量、返回地址等都用到栈；而汇编语言本身不依赖栈
.section .text
.global _entry
_entry:
        
        # stack0 在 start.c 中声明，
        # 每个 CPU 分配一个 4096 字节的栈。
        # sp = stack0 + (hartid * 4096)
        
        la sp, stack0
        li a0, 1024*4
	csrr a1, mhartid        # 读取当前CPU 的硬件线程ID（hartid），到寄存器 a1
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0
	# jump to start() in start.c
        call start
spin:
        j spin
