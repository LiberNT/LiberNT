/*
 * COPYRIGHT:         BSD - See COPYING.ARM in the top level directory
 * PROJECT:           ReactOS CRT library
 * PURPOSE:           Implementation of __rt_srsh
 * PROGRAMMER:        Timo Kreuzer (timo.kreuzer@reactos.org)
 *                    Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

    .global __rt_srsh
    .thumb_func
__rt_srsh:
    rsbs    r3, r2, #32
    bmi     1f
    lsr     r0, r0, r2
    lsl     r3, r1, r3
    orr     r0, r0, r3
    asr     r1, r1, r2
    bx      lr
1:
    cmp     r2, #64
    bhs     2f
    sub     r3, r2, #32
    asr     r0, r1, r3
    asr     r1, r1, #31
    bx      lr
2:
    asr     r1, r1, #31
    mov     r0, r1
    bx      lr
