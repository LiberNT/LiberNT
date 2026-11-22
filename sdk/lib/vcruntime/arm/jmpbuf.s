/*
 * PROJECT:     LiberNT vcruntime library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filling a jump buffer and resuming from one on ARM
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text

    .equ JumpFrame, 0
    .equ JumpR4, 4
    .equ JumpSp, 36
    .equ JumpPc, 40
    .equ JumpFpscr, 44
    .equ JumpD8, 48

    .balign 4
    .global _setjmp
    .global _setjmpex
    .global __intrinsic_setjmp
    .global __intrinsic_setjmpex
    .thumb_func
_setjmp:
    .thumb_func
_setjmpex:
    .thumb_func
__intrinsic_setjmp:
    .thumb_func
__intrinsic_setjmpex:
    str     r1, [r0, #JumpFrame]
    add     r12, r0, #JumpR4
    stmia   r12, {r4-r11}
    str     sp, [r0, #JumpSp]
    str     lr, [r0, #JumpPc]
    vmrs    r3, fpscr
    str     r3, [r0, #JumpFpscr]
    add     r12, r0, #JumpD8
    vstmia  r12, {d8-d15}
    movs    r0, #0
    bx      lr

    .balign 4
    .global _JumpBufferResume
    .thumb_func
_JumpBufferResume:
    add     r12, r0, #JumpD8
    vldmia  r12, {d8-d15}
    ldr     r3, [r0, #JumpFpscr]
    vmsr    fpscr, r3
    add     r12, r0, #JumpR4
    ldmia   r12, {r4-r11}
    ldr     lr, [r0, #JumpPc]
    ldr     sp, [r0, #JumpSp]
    mov     r0, r1
    bx      lr
