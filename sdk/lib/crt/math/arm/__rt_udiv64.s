/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of __rt_udiv64
 * COPYRIGHT:   Copyright 2015 Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2021 Roman Masanin <36927roma@gmail.com>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

    .global __rt_udiv64
    .thumb_func
    .seh_proc __rt_udiv64
__rt_udiv64:
    push    {r4, lr}
    .seh_save_regs {r4, lr}
    sub     sp, sp, #24
    .seh_stackalloc 24
    .seh_endprologue
    strd    r2, r3, [sp]
    mov     r3, r1
    mov     r2, r0
    add     r0, sp, #8
    bl      __rt_udiv64_worker
    ldrd    r0, r1, [sp, #8]
    ldrd    r2, r3, [sp, #16]
    .seh_startepilogue
    add     sp, sp, #24
    .seh_stackalloc 24
    pop     {r4, pc}
    .seh_save_regs {r4, lr}
    .seh_endepilogue
    .seh_endproc
