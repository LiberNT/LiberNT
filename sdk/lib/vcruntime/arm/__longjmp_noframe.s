/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM longjmp helper
 * COPYRIGHT:   Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

    .global __longjmp_noframe
    .thumb_func
__longjmp_noframe:
    ldr     r2, [r0, #44]
    vmsr    fpscr, r2
    add     r2, r0, #48
    vldmia  r2, {d8-d15}
    add     r2, r0, #4
    ldmia   r2, {r4-r11}
    ldr     r2, [r0, #36]
    mov     sp, r2
    ldr     r2, [r0, #40]
    movs    r0, r1
    it      eq
    moveq   r0, #1
    bx      r2
