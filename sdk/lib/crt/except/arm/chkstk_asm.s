/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Implementation of __chkstk
 * COPYRIGHT:   Copyright 2014 Timo Kreuzer (timo.kreuzer@reactos.org)
 *              Copyright 2014 Yuntian Zhang (yuntian.zh@gmail.com)
 *              Copyright 2019 Mohamed Mediouni (mmediouni@gmx.fr)
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

    .global __chkstk
    .thumb_func
    .seh_proc __chkstk
__chkstk:
    push    {r4, r5}
    .seh_save_regs {r4, r5}
    .seh_endprologue
    lsl     r4, r4, #2
    add     r5, sp, #8
    sub     r4, r5, r4
1:
    sub     r5, r5, #4096
    cmp     r5, r4
    bls     2f
    ldr     r12, [r5]
    b       1b
2:
    ldr     r12, [r4]
    .seh_startepilogue
    pop     {r4, r5}
    .seh_save_regs {r4, r5}
    .seh_endepilogue
    lsl     r4, r4, #2
    bx      lr
    .seh_endproc
