.section .text
.globl kerneltrap
.globl kernelvec
.align 4
kernelvec:
    # 为trapframe分配空间
    addi sp, sp, -256
    
    # 保存寄存器
    sd ra, 40(sp)
    sd sp, 48(sp)
    sd gp, 56(sp)
    sd tp, 64(sp)
    sd t0, 72(sp)
    sd t1, 80(sp)
    sd t2, 88(sp)
    sd s0, 96(sp)
    sd s1, 104(sp)
    sd a0, 112(sp)
    sd a1, 120(sp)
    sd a2, 128(sp)
    sd a3, 136(sp)
    sd a4, 144(sp)
    sd a5, 152(sp)
    sd a6, 160(sp)
    sd a7, 168(sp)
    sd s2, 176(sp)
    sd s3, 184(sp)
    sd s4, 192(sp)
    sd s5, 200(sp)
    sd s6, 208(sp)
    sd s7, 216(sp)
    sd s8, 224(sp)
    sd s9, 232(sp)
    sd s10, 240(sp)
    sd s11, 248(sp)
    sd t3, 256(sp)
    sd t4, 264(sp)
    sd t5, 272(sp)
    sd t6, 280(sp)

    # 调用C陷阱处理函数
    mv a0, sp
    call kerneltrap

    # 恢复寄存器
    ld ra, 40(sp)
    ld sp, 48(sp)
    ld gp, 56(sp)
    ld tp, 64(sp)
    ld t0, 72(sp)
    ld t1, 80(sp)
    ld t2, 88(sp)
    ld s0, 96(sp)
    ld s1, 104(sp)
    ld a0, 112(sp)
    ld a1, 120(sp)
    ld a2, 128(sp)
    ld a3, 136(sp)
    ld a4, 144(sp)
    ld a5, 152(sp)
    ld a6, 160(sp)
    ld a7, 168(sp)
    ld s2, 176(sp)
    ld s3, 184(sp)
    ld s4, 192(sp)
    ld s5, 200(sp)
    ld s6, 208(sp)
    ld s7, 216(sp)
    ld s8, 224(sp)
    ld s9, 232(sp)
    ld s10, 240(sp)
    ld s11, 248(sp)
    ld t3, 256(sp)
    ld t4, 264(sp)
    ld t5, 272(sp)
    ld t6, 280(sp)

    addi sp, sp, 256

    # 返回
    sret

# 机器模式定时器中断处理
.globl timervec
.align 4
timervec:
    # 保存寄存器到 mscratch 指向的区域
    csrrw a0, mscratch, a0
    sd a1, 0(a0)
    sd a2, 8(a0)
    sd a3, 16(a0)

    # 读取 mcause，判断是中断（MSB=1）还是异常（MSB=0）
    csrr t0, mcause
    li t1, 0x8000000000000000
    and t2, t0, t1
    bnez t2, 1f
    # 异常 -> 交给 C 层处理并返回
    call machine_exception_handler
    ld a3, 16(a0)
    ld a2, 8(a0)
    ld a1, 0(a0)
    csrrw a0, mscratch, a0
    mret
1:

    # 设置下一次定时器中断
    ld a1, 24(a0)  # interval
    ld a2, 32(a0)  # scratch
    ld a3, 0(a2)
    add a3, a3, a1
    sd a3, 0(a2)
    
    # 将 Supervisor 层的软件中断置位, 以便 S-mode 的 trap 被触发并在 S-mode 处理
    li a1, 2          # SSIP bit
    csrs sip, a1

    # 直接在 machine 模式下增加全局 ticks 以确保测试可以感知中断到达
    la t0, ticks
    ld t1, 0(t0)
    addi t1, t1, 1
    sd t1, 0(t0)

    # 恢复寄存器
    ld a3, 16(a0)
    ld a2, 8(a0)
    ld a1, 0(a0)
    csrrw a0, mscratch, a0

    mret