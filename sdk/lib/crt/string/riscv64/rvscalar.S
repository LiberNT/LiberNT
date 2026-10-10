/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 routines for copies and fills above 64 bytes with fast misaligned scalar access
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text
    .option push
    .option rvc

    .p2align 4
    .global RvMemmoveScalar
RvMemmoveScalar:
    add a3, a1, a2
    add a4, a0, a2
    srli t0, a2, 15
    bnez t0, 26f
    sub t0, a0, a1
    bltu t0, a2, 30f
    li t1, 256
    bltu a2, t1, 21f
    slli t1, t0, 52
    srli t1, t1, 52
    addi t1, t1, -1
    sltiu t1, t1, 191
    bnez t1, 26f
21:
    ld t3, -32(a3)
    ld t4, -24(a3)
    ld t5, -16(a3)
    ld t6, -8(a3)
    ld t1, 0(a1)
    ld t2, 8(a1)
    mv a7, a0
    addi a6, a4, -64
    andi a5, a0, 15
    addi a5, a5, -16
    sub a0, a0, a5
    sub a1, a1, a5
    c.ld a2, 0(a1)
    c.ld a3, 8(a1)
    c.ld a4, 16(a1)
    c.ld a5, 24(a1)
    sd t1, 0(a7)
    sd t2, 8(a7)
    c.sd a2, 0(a0)
    c.sd a3, 8(a0)
    c.sd a4, 16(a0)
    c.sd a5, 24(a0)
    addi a1, a1, 32
    addi a0, a0, 32
    bgeu a0, a6, 23f
    .p2align 4
22:
    c.ld a2, 0(a1)
    c.ld a3, 8(a1)
    c.ld a4, 16(a1)
    c.ld a5, 24(a1)
    c.sd a2, 0(a0)
    c.sd a3, 8(a0)
    c.sd a4, 16(a0)
    c.sd a5, 24(a0)
    c.ld a2, 32(a1)
    c.ld a3, 40(a1)
    c.ld a4, 48(a1)
    c.ld a5, 56(a1)
    c.sd a2, 32(a0)
    c.sd a3, 40(a0)
    c.sd a4, 48(a0)
    c.sd a5, 56(a0)
    addi a1, a1, 64
    addi a0, a0, 64
    bltu a0, a6, 22b
23:
    addi a2, a6, 32
    bgeu a0, a2, 24f
    c.ld a2, 0(a1)
    c.ld a3, 8(a1)
    c.ld a4, 16(a1)
    c.ld a5, 24(a1)
    c.sd a2, 0(a0)
    c.sd a3, 8(a0)
    c.sd a4, 16(a0)
    c.sd a5, 24(a0)
24:
    sd t3, 32(a6)
    sd t4, 40(a6)
    sd t5, 48(a6)
    sd t6, 56(a6)
    mv a0, a7
    ret
26:
    tail RvMemmoveRva23

30:
    li t1, 256
    bltu a2, t1, 31f
    sub t1, a1, a0
    slli t1, t1, 52
    srli t1, t1, 52
    addi t1, t1, -1
    sltiu t1, t1, 191
    bnez t1, 26b
31:
    ld t3, 0(a1)
    ld t4, 8(a1)
    ld t5, 16(a1)
    ld t6, 24(a1)
    ld t1, -16(a3)
    ld t2, -8(a3)
    mv a7, a0
    mv t0, a4
    addi a6, a0, 64
    andi a5, a4, 15
    sub a0, a4, a5
    sub a1, a3, a5
    addi a1, a1, -32
    addi a0, a0, -32
    c.ld a2, 0(a1)
    c.ld a3, 8(a1)
    c.ld a4, 16(a1)
    c.ld a5, 24(a1)
    sd t1, -16(t0)
    sd t2, -8(t0)
    c.sd a2, 0(a0)
    c.sd a3, 8(a0)
    c.sd a4, 16(a0)
    c.sd a5, 24(a0)
    bleu a0, a6, 36f
    .p2align 4
35:
    addi a1, a1, -64
    addi a0, a0, -64
    c.ld a2, 32(a1)
    c.ld a3, 40(a1)
    c.ld a4, 48(a1)
    c.ld a5, 56(a1)
    c.sd a2, 32(a0)
    c.sd a3, 40(a0)
    c.sd a4, 48(a0)
    c.sd a5, 56(a0)
    c.ld a2, 0(a1)
    c.ld a3, 8(a1)
    c.ld a4, 16(a1)
    c.ld a5, 24(a1)
    c.sd a2, 0(a0)
    c.sd a3, 8(a0)
    c.sd a4, 16(a0)
    c.sd a5, 24(a0)
    bgtu a0, a6, 35b
36:
    addi a2, a7, 32
    bleu a0, a2, 37f
    addi a1, a1, -32
    addi a0, a0, -32
    c.ld a2, 0(a1)
    c.ld a3, 8(a1)
    c.ld a4, 16(a1)
    c.ld a5, 24(a1)
    c.sd a2, 0(a0)
    c.sd a3, 8(a0)
    c.sd a4, 16(a0)
    c.sd a5, 24(a0)
37:
    sd t3, 0(a7)
    sd t4, 8(a7)
    sd t5, 16(a7)
    sd t6, 24(a7)
    mv a0, a7
    ret

    .p2align 4
    .global RvMemsetScalar
RvMemsetScalar:
    srli t0, a2, 19
    beqz t0, 55f
    tail RvMemsetRva23
55:
    andi a1, a1, 0xff
    slli t0, a1, 8
    or a1, a1, t0
    slli t0, a1, 16
    or a1, a1, t0
    slli t0, a1, 32
    or a1, a1, t0
    add a4, a0, a2
    sd a1, 0(a0)
    sd a1, 8(a0)
    andi a3, a0, -16
    addi a3, a3, 16
    addi a5, a4, -64
    bgeu a3, a5, 54f
    .p2align 4
53:
    c.sd a1, 0(a3)
    c.sd a1, 8(a3)
    c.sd a1, 16(a3)
    c.sd a1, 24(a3)
    c.sd a1, 32(a3)
    c.sd a1, 40(a3)
    c.sd a1, 48(a3)
    c.sd a1, 56(a3)
    addi a3, a3, 64
    bltu a3, a5, 53b
54:
    c.sd a1, 0(a5)
    c.sd a1, 8(a5)
    c.sd a1, 16(a5)
    c.sd a1, 24(a5)
    c.sd a1, 32(a5)
    c.sd a1, 40(a5)
    c.sd a1, 48(a5)
    c.sd a1, 56(a5)
    ret

    .option pop
