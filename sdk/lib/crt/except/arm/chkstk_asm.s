/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     __chkstk for ARM (Thumb-2)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

/*
 * Windows on ARM: r4 holds the frame size in words and returns it in bytes;
 * every page between sp and the new stack bottom is touched in order, so the
 * guard page grows the stack. Only r4 and r12 change.
 */
    .global __chkstk
    .thumb_func
__chkstk:
    lsl r4, r4, #2
    push {r5, r6}
    add r5, sp, #8
    sub r6, r5, r4
1:  sub r5, r5, #0x1000
    cmp r5, r6
    blo 2f
    ldr r12, [r5]
    b 1b
2:  ldr r12, [r6]
    pop {r5, r6}
    bx lr
