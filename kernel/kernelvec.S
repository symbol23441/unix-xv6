	#
        # 当处于管理模式（Supervisor mode）时发生中断或异常，会跳转到这里。
        #
        # 这里会保存所有寄存器，调用 kerneltrap() 处理，再恢复寄存器，最后返回。
        #
.globl kerneltrap
.globl kernelvec
.align 4
kernelvec:
        # 为保存寄存器开辟栈空间
        addi sp, sp, -256

        # 寄存器值压栈
        sd ra, 0(sp)
        sd sp, 8(sp)
        sd gp, 16(sp)
        sd tp, 24(sp)
        sd t0, 32(sp)
        sd t1, 40(sp)
        sd t2, 48(sp)
        sd s0, 56(sp)
        sd s1, 64(sp)
        sd a0, 72(sp)
        sd a1, 80(sp)
        sd a2, 88(sp)
        sd a3, 96(sp)
        sd a4, 104(sp)
        sd a5, 112(sp)
        sd a6, 120(sp)
        sd a7, 128(sp)
        sd s2, 136(sp)
        sd s3, 144(sp)
        sd s4, 152(sp)
        sd s5, 160(sp)
        sd s6, 168(sp)
        sd s7, 176(sp)
        sd s8, 184(sp)
        sd s9, 192(sp)
        sd s10, 200(sp)
        sd s11, 208(sp)
        sd t3, 216(sp)
        sd t4, 224(sp)
        sd t5, 232(sp)
        sd t6, 240(sp)

	# 调用trap.c的C trap() 
        call kerneltrap

        # 恢复寄存器
        ld ra, 0(sp)
        ld sp, 8(sp)
        ld gp, 16(sp)
        # not this, in case we moved CPUs: ld tp, 24(sp)
        ld t0, 32(sp)
        ld t1, 40(sp)
        ld t2, 48(sp)
        ld s0, 56(sp)
        ld s1, 64(sp)
        ld a0, 72(sp)
        ld a1, 80(sp)
        ld a2, 88(sp)
        ld a3, 96(sp)
        ld a4, 104(sp)
        ld a5, 112(sp)
        ld a6, 120(sp)
        ld a7, 128(sp)
        ld s2, 136(sp)
        ld s3, 144(sp)
        ld s4, 152(sp)
        ld s5, 160(sp)
        ld s6, 168(sp)
        ld s7, 176(sp)
        ld s8, 184(sp)
        ld s9, 192(sp)
        ld s10, 200(sp)
        ld s11, 208(sp)
        ld t3, 216(sp)
        ld t4, 224(sp)
        ld t5, 232(sp)
        ld t6, 240(sp)

        addi sp, sp, 256

        # 返回到上一次内核的被中断的位置
        sret


        #
        # M-Mode下的 定时器中断。
        # 因为定时器中断在M-Mode下的权限 CLINT（Core Local Interruptor）中，因此在这里通过软件中断，再次转发非S-Mode
        #
.globl timervec
.align 4
timervec:
        # start.c 已经设置好了 mscratch 寄存器指向的内存区域：
        # scratch[0,8,16]：用于保存寄存器的区域；
        # scratch[24]：存放 CLINT 的 mtimecmp 寄存器的地址；
        # scratch[32]：存放希望两次中断之间的时间间隔。
        
        csrrw a0, mscratch, a0
        sd a1, 0(a0)
        sd a2, 8(a0)
        sd a3, 16(a0)

        # 通过将间隔值加到 mtimecmp，来安排下一次定时器中断。
        ld a1, 24(a0) # CLINT_MTIMECMP(hart)
        ld a2, 32(a0) # interval
        ld a3, 0(a1)
        add a3, a3, a2
        sd a3, 0(a1)

        # 触发一次管理模式的软件中断。
	li a1, 2
        csrw sip, a1

        ld a3, 16(a0)
        ld a2, 8(a0)
        ld a1, 0(a0)
        csrrw a0, mscratch, a0

        mret
