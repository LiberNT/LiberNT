/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of _setjmp for ARM
 * COPYRIGHT:   Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

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
    str     r1, [r0, #0]
    add     r2, r0, #4
    stmia   r2, {r4-r11}
    mov     r3, sp
    str     r3, [r0, #36]
    str     lr, [r0, #40]
    vmrs    r3, fpscr
    str     r3, [r0, #44]
    add     r2, r0, #48
    vstmia  r2, {d8-d15}
    movs    r0, #0
    bx      lr
