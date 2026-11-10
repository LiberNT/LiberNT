/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Page zeroing for ARMv8.0-A
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .balign 64
    .global KeZeroPages
KeZeroPages:
    mov     w1, w1
    cbz     x1, 3f
    mrs     x2, dczid_el0
    tbnz    w2, #4, 2f
    and     w2, w2, #15
    mov     x3, #4
    lsl     x3, x3, x2
1:
    dc      zva, x0
    add     x0, x0, x3
    subs    x1, x1, x3
    b.hi    1b
    ret
2:
    stp     xzr, xzr, [x0]
    stp     xzr, xzr, [x0, #16]
    stp     xzr, xzr, [x0, #32]
    stp     xzr, xzr, [x0, #48]
    add     x0, x0, #64
    subs    x1, x1, #64
    b.hi    2b
3:
    ret
