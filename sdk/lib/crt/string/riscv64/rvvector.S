/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 RVA23 vector string and memory routines
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text
    .option push
    .option arch, +v

    .p2align 2
    .global RvStrlenVector
RvStrlenVector:
    mv a1, a0
1:
    vsetvli t0, zero, e8, m8, ta, ma
    vle8ff.v v8, (a1)
    csrr t0, vl
    vmseq.vi v0, v8, 0
    vfirst.m t1, v0
    add a1, a1, t0
    bltz t1, 1b
    sub a1, a1, t0
    add a1, a1, t1
    sub a0, a1, a0
    ret

    .p2align 2
    .global RvStrcmpVector
RvStrcmpVector:
    li t1, 0
1:
    vsetvli t0, zero, e8, m8, ta, ma
    add a0, a0, t1
    vle8ff.v v8, (a0)
    add a1, a1, t1
    vle8ff.v v16, (a1)
    vmseq.vi v0, v8, 0
    vmsne.vv v1, v8, v16
    vmor.mm v0, v0, v1
    vfirst.m a2, v0
    csrr t1, vl
    bltz a2, 1b
    add a0, a0, a2
    lbu a3, (a0)
    add a1, a1, a2
    lbu a4, (a1)
    sltu a0, a4, a3
    sltu a1, a3, a4
    sub a0, a0, a1
    ret

    .p2align 2
    .global RvStrcpyVector
RvStrcpyVector:
    mv a2, a0
1:
    vsetvli t0, zero, e8, m8, ta, ma
    vle8ff.v v8, (a1)
    csrr t1, vl
    vmseq.vi v1, v8, 0
    vfirst.m a3, v1
    add a1, a1, t1
    vmsif.m v0, v1
    vse8.v v8, (a2), v0.t
    add a2, a2, t1
    bltz a3, 1b
    ret

#if defined(RV_CRT_RVA23) && defined(RV_CRT_FAST_UNALIGNED)
    .p2align 4
    .global memcpy
    .global memmove
memcpy:
memmove:
    li t1, 64
    bgtu a2, t1, 3f
    vsetvli t0, a2, e8, m1, ta, ma
    bne t0, a2, 1f
    vle8.v v8, (a1)
    vse8.v v8, (a0)
    ret
1:
    vsetvli t0, a2, e8, m2, ta, ma
    bne t0, a2, 2f
    vle8.v v8, (a1)
    vse8.v v8, (a0)
    ret
2:
    vsetvli zero, a2, e8, m4, ta, ma
    vle8.v v8, (a1)
    vse8.v v8, (a0)
    ret
3:
    addi t1, a2, -129
    li t0, 128
    bltu t1, t0, RvMemmoveRva23
    tail RvMemmoveScalar

    .p2align 4
    .global memset
memset:
    li t1, 256
    bleu a2, t1, RvMemsetRva23
    tail RvMemsetScalar
#endif

    .p2align 2
    .global RvMemcpyRva23
#if defined(RV_CRT_RVA23) && !defined(RV_CRT_FAST_UNALIGNED)
    .global memcpy
memcpy:
#endif
RvMemcpyRva23:
    li t1, 64
    bgtu a2, t1, 3f
    vsetvli t0, a2, e8, m1, ta, ma
    bne t0, a2, 1f
    vle8.v v8, (a1)
    vse8.v v8, (a0)
    ret
1:
    vsetvli t0, a2, e8, m2, ta, ma
    bne t0, a2, 2f
    vle8.v v8, (a1)
    vse8.v v8, (a0)
    ret
2:
    vsetvli zero, a2, e8, m4, ta, ma
    vle8.v v8, (a1)
    vse8.v v8, (a0)
    ret
3:
    mv a3, a0
    li t1, 1024
    bltu a2, t1, 4f
    neg t1, a3
    andi t1, t1, 63
    beqz t1, 4f
    vsetvli zero, t1, e8, m4, ta, ma
    vle8.v v8, (a1)
    sub a2, a2, t1
    add a1, a1, t1
    vse8.v v8, (a3)
    add a3, a3, t1
4:
    vsetvli t0, a2, e8, m8, ta, ma
    vle8.v v8, (a1)
    sub a2, a2, t0
    add a1, a1, t0
    vse8.v v8, (a3)
    add a3, a3, t0
    bnez a2, 4b
    ret

    .p2align 2
    .global RvMemmoveRva23
#if defined(RV_CRT_RVA23) && defined(RV_CRT_VECTOR_MEMMOVE) && !defined(RV_CRT_FAST_UNALIGNED)
    .global memmove
memmove:
#endif
RvMemmoveRva23:
    li t1, 64
    bleu a2, t1, RvMemcpyRva23
    sub t0, a0, a1
    bgeu t0, a2, RvMemcpyRva23
    add a3, a0, a2
    add a1, a1, a2
1:
    vsetvli t0, a2, e8, m8, ta, ma
    sub a1, a1, t0
    sub a3, a3, t0
    vle8.v v8, (a1)
    sub a2, a2, t0
    vse8.v v8, (a3)
    bnez a2, 1b
    ret

    .p2align 2
    .global RvMemsetRva23
#if defined(RV_CRT_RVA23) && !defined(RV_CRT_FAST_UNALIGNED)
    .global memset
memset:
#endif
RvMemsetRva23:
    li t1, 64
    bgtu a2, t1, 3f
    vsetvli t0, a2, e8, m1, ta, ma
    bne t0, a2, 1f
    vmv.v.x v8, a1
    vse8.v v8, (a0)
    ret
1:
    vsetvli t0, a2, e8, m2, ta, ma
    bne t0, a2, 2f
    vmv.v.x v8, a1
    vse8.v v8, (a0)
    ret
2:
    vsetvli zero, a2, e8, m4, ta, ma
    vmv.v.x v8, a1
    vse8.v v8, (a0)
    ret
3:
    mv a3, a0
    vsetvli t0, zero, e8, m8, ta, ma
    vmv.v.x v8, a1
4:
    vsetvli t0, a2, e8, m8, ta, ma
    vse8.v v8, (a3)
    sub a2, a2, t0
    add a3, a3, t0
    bnez a2, 4b
    ret

    .p2align 2
    .global RvMemcmpRva23
#ifdef RV_CRT_RVA23
    .global memcmp
memcmp:
#endif
RvMemcmpRva23:
    li t1, 64
    bgtu a2, t1, 3f
    vsetvli t0, a2, e8, m1, ta, ma
    bne t0, a2, 1f
    vle8.v v8, (a0)
    vle8.v v9, (a1)
    vmsne.vv v0, v8, v9
    vfirst.m t1, v0
    bgez t1, 5f
    li a0, 0
    ret
1:
    vsetvli t0, a2, e8, m2, ta, ma
    bne t0, a2, 2f
    vle8.v v8, (a0)
    vle8.v v10, (a1)
    vmsne.vv v0, v8, v10
    vfirst.m t1, v0
    bgez t1, 5f
    li a0, 0
    ret
2:
    vsetvli zero, a2, e8, m4, ta, ma
    vle8.v v8, (a0)
    vle8.v v12, (a1)
    vmsne.vv v0, v8, v12
    vfirst.m t1, v0
    bgez t1, 5f
    li a0, 0
    ret
3:
    vsetvli t0, a2, e8, m8, ta, ma
    vle8.v v8, (a0)
    vle8.v v16, (a1)
    vmsne.vv v0, v8, v16
    vfirst.m t1, v0
    bgez t1, 5f
    add a0, a0, t0
    add a1, a1, t0
    sub a2, a2, t0
    bnez a2, 3b
    li a0, 0
    ret
5:
    add a0, a0, t1
    add a1, a1, t1
    lbu a3, (a0)
    lbu a4, (a1)
    sltu a0, a4, a3
    sltu a1, a3, a4
    sub a0, a0, a1
    ret

    .option pop
