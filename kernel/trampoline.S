        #
        # 在用户空间和内核空间之间切换的代码。
        #
        # 这段代码在用户页表和内核页表中映射到相同的虚拟地址
        # （TRAMPOLINE），这样在切换页表后仍然能够继续执行。
        #
        # kernel.ld 文件确保这段代码被对齐到页边界。
        #
	.section trampsec
.globl trampoline
trampoline:
.align 4
.globl uservec
uservec:    
        #
        # trap.c 将 stvec 设置为指向这里，
        # 所以来自用户空间的陷阱（trap）会从这里开始执行，
        # 此时处理器处于管理模式（supervisor mode），
        # 但页表仍然是用户进程的用户页表。
        #
        # sscratch 寄存器此时保存的是该进程的 trapframe 的地址，
        # trapframe 已被映射到用户空间的 TRAPFRAME 虚拟地址。
        #
        
        # 交换a0和sscratch。 原sscratch保存当前进程p->trapframe地址,a0是传入参数。
        csrrw a0, sscratch, a0

        # 将用户寄存器状态保存到trapframe中
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# 将用户a0寄存器值保存到p->trapframe->a0
        csrr t0, sscratch
        sd t0, 112(a0)
        
        ld sp, 8(a0)      # trapframe->kernel_sp，切换内核栈
        ld tp, 32(a0)     # trapframe->kernel_hartid，放入 tp。（安全考虑，防止用户进程引导内核破坏结构，如1000的hartid,从而去除别处的数据）
        ld t0, 16(a0)     # trapframe->kernel_trap，加载跳转地址usertrap()

        ld t1, 0(a0)      # trapframe->kernel_satp，加载内核页表地址
        csrw satp, t1
        sfence.vma zero, zero   # 刷新TLB快表

        #
        # 从这一步开始，就不能再访问用户页表中的地址了（比如 trapframe），因为用户页表已经失效。
        #

        # 无条件跳转到usertrap(),无return
        jr t0

.globl userret
userret:
        # userret(TRAPFRAME, pagetable)
        # 从内核切换回用户态。
        # 由 usertrapret() 调用到这里。
        # a0：TRAPFRAME 的地址（在用户页表中）
        # a1：用户页表的 satp 值。

        # 切换到用户页表
        csrw satp, a1
        sfence.vma zero, zero

        # 将用户保存的 a0 放入 sscratch，这样我们在最后一步可以将它与当前的 a0（即 TRAPFRAME）交换回来
        ld t0, 112(a0)
        csrw sscratch, t0

        # 从TRAPFRAME恢复除 a0 以外的寄存器
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

        # 恢复用户的 a0 寄存器值，并将 TRAPFRAME 地址保存到 sscratch 中。
        csrrw a0, sscratch, a0
        
        # 返回到用户模式和用户程序计数器（PC）。
        # usertrapret() 已经设置好了 sstatus 和 sepc。
        sret
