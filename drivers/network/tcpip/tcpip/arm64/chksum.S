/*
 * PROJECT:     LiberNT TCP/IP protocol driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     lwIP Internet checksum for ARMv8.0-A
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .balign 64
    .global tcpip_chksum
tcpip_chksum:
    sxtw    x1, w1
    mov     x2, xzr
    cmp     x1, #64
    b.lt    .Ltail

.Lloop:
    ldp     x4, x5, [x0]
    ldp     x6, x7, [x0, #16]
    ldp     x8, x9, [x0, #32]
    ldp     x10, x11, [x0, #48]
    adds    x2, x2, x4
    adcs    x2, x2, x5
    adcs    x2, x2, x6
    adcs    x2, x2, x7
    adcs    x2, x2, x8
    adcs    x2, x2, x9
    adcs    x2, x2, x10
    adcs    x2, x2, x11
    adc     x2, x2, xzr
    add     x0, x0, #64
    sub     x1, x1, #64
    cmp     x1, #64
    b.ge    .Lloop

.Ltail:
    cmp     x1, #0
    b.le    .Lfold
    tbz     x1, #5, 1f
    ldp     x4, x5, [x0]
    ldp     x6, x7, [x0, #16]
    adds    x2, x2, x4
    adcs    x2, x2, x5
    adcs    x2, x2, x6
    adcs    x2, x2, x7
    adc     x2, x2, xzr
    add     x0, x0, #32
1:
    tbz     x1, #4, 2f
    ldp     x4, x5, [x0]
    adds    x2, x2, x4
    adcs    x2, x2, x5
    adc     x2, x2, xzr
    add     x0, x0, #16
2:
    tbz     x1, #3, 3f
    ldr     x4, [x0], #8
    adds    x2, x2, x4
    adc     x2, x2, xzr
3:
    tbz     x1, #2, 4f
    ldr     w4, [x0], #4
    adds    x2, x2, x4
    adc     x2, x2, xzr
4:
    tbz     x1, #1, 5f
    ldrh    w4, [x0], #2
    adds    x2, x2, x4
    adc     x2, x2, xzr
5:
    tbz     x1, #0, .Lfold
    ldrb    w4, [x0]
    adds    x2, x2, x4
    adc     x2, x2, xzr

.Lfold:
    lsr     x3, x2, #32
    adds    w2, w2, w3
    adc     w2, w2, wzr
    and     w3, w2, #0xffff
    add     w2, w3, w2, lsr #16
    and     w3, w2, #0xffff
    add     w0, w3, w2, lsr #16
    ret
