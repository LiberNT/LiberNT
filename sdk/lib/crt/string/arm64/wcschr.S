/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     wcschr and wcsrchr for ARMv8.0-A with Advanced SIMD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .balign 64
    .global wcschr
wcschr:
    and     w1, w1, #0xffff
    tbnz    x0, #0, .Lwcschr_unaligned
    dup     v1.8h, w1
    bic     x2, x0, #15
    ldr     q0, [x2]
    cmeq    v2.8h, v0.8h, v1.8h
    cmeq    v3.8h, v0.8h, #0
    orr     v3.16b, v3.16b, v2.16b
    shrn    v2.8b, v2.8h, #4
    shrn    v3.8b, v3.8h, #4
    fmov    x3, d3
    fmov    x4, d2
    lsl     x5, x0, #2
    lsr     x3, x3, x5
    lsr     x4, x4, x5
    cbz     x3, .Lwcschr_next
    rbit    x3, x3
    clz     x3, x3
    lsr     x4, x4, x3
    tbz     x4, #0, .Lwcschr_none
    add     x0, x0, x3, lsr #2
    ret

.Lwcschr_next:
    add     x2, x2, #16
    tbz     x2, #4, .Lwcschr_loop
    ldr     q0, [x2]
    cmeq    v2.8h, v0.8h, v1.8h
    cmeq    v3.8h, v0.8h, #0
    orr     v3.16b, v3.16b, v2.16b
    umaxp   v4.16b, v3.16b, v3.16b
    fmov    x3, d4
    cbnz    x3, .Lwcschr_found
    add     x2, x2, #16

.Lwcschr_loop:
    ldp     q0, q5, [x2], #32
    cmeq    v2.8h, v0.8h, v1.8h
    cmeq    v3.8h, v0.8h, #0
    orr     v3.16b, v3.16b, v2.16b
    cmeq    v6.8h, v5.8h, v1.8h
    cmeq    v7.8h, v5.8h, #0
    orr     v7.16b, v7.16b, v6.16b
    orr     v4.16b, v3.16b, v7.16b
    umaxp   v4.16b, v4.16b, v4.16b
    fmov    x3, d4
    cbz     x3, .Lwcschr_loop
    sub     x2, x2, #32
    umaxp   v4.16b, v3.16b, v3.16b
    fmov    x3, d4
    cbnz    x3, .Lwcschr_found
    add     x2, x2, #16
    mov     v2.16b, v6.16b
    mov     v3.16b, v7.16b

.Lwcschr_found:
    shrn    v2.8b, v2.8h, #4
    shrn    v3.8b, v3.8h, #4
    fmov    x3, d3
    fmov    x4, d2
    rbit    x3, x3
    clz     x3, x3
    lsr     x4, x4, x3
    tbz     x4, #0, .Lwcschr_none
    add     x0, x2, x3, lsr #2
    ret

.Lwcschr_none:
    mov     x0, #0
    ret

.Lwcschr_unaligned:
    ldrb    w3, [x0]
    ldrb    w4, [x0, #1]
    orr     w3, w3, w4, lsl #8
    cmp     w3, w1
    b.eq    1f
    cbz     w3, .Lwcschr_none
    add     x0, x0, #2
    b       .Lwcschr_unaligned
1:
    ret

    .balign 64
    .global wcsrchr
wcsrchr:
    and     w1, w1, #0xffff
    mov     x6, #0
    tbnz    x0, #0, .Lwcsrchr_unaligned
    dup     v1.8h, w1
    bic     x2, x0, #15
    ldr     q0, [x2]
    cmeq    v2.8h, v0.8h, v1.8h
    cmeq    v3.8h, v0.8h, #0
    shrn    v2.8b, v2.8h, #4
    shrn    v3.8b, v3.8h, #4
    fmov    x4, d2
    fmov    x3, d3
    lsl     x5, x0, #2
    lsr     x3, x3, x5
    lsr     x4, x4, x5
    mov     x7, x0
    cbnz    x3, .Lwcsrchr_end
    cbz     x4, .Lwcsrchr_next
    clz     x8, x4
    mov     x9, #63
    sub     x8, x9, x8
    lsr     x8, x8, #3
    add     x6, x7, x8, lsl #1

.Lwcsrchr_next:
    add     x2, x2, #16

.Lwcsrchr_loop:
    ldr     q0, [x2]
    cmeq    v2.8h, v0.8h, v1.8h
    cmeq    v3.8h, v0.8h, #0
    orr     v4.16b, v2.16b, v3.16b
    umaxp   v4.16b, v4.16b, v4.16b
    fmov    x3, d4
    cbz     x3, .Lwcsrchr_skip
    shrn    v2.8b, v2.8h, #4
    shrn    v3.8b, v3.8h, #4
    fmov    x4, d2
    fmov    x3, d3
    mov     x7, x2
    cbnz    x3, .Lwcsrchr_end
    clz     x8, x4
    mov     x9, #63
    sub     x8, x9, x8
    lsr     x8, x8, #3
    add     x6, x7, x8, lsl #1

.Lwcsrchr_skip:
    add     x2, x2, #16
    b       .Lwcsrchr_loop

.Lwcsrchr_end:
    rbit    x9, x3
    clz     x9, x9
    add     x9, x9, #7
    mov     x10, #2
    lsl     x10, x10, x9
    sub     x10, x10, #1
    and     x4, x4, x10
    cbz     x4, .Lwcsrchr_ret
    clz     x8, x4
    mov     x9, #63
    sub     x8, x9, x8
    lsr     x8, x8, #3
    add     x6, x7, x8, lsl #1

.Lwcsrchr_ret:
    mov     x0, x6
    ret

.Lwcsrchr_unaligned:
    ldrb    w3, [x0]
    ldrb    w4, [x0, #1]
    orr     w3, w3, w4, lsl #8
    cmp     w3, w1
    csel    x6, x0, x6, eq
    cbz     w3, .Lwcsrchr_ret
    add     x0, x0, #2
    b       .Lwcsrchr_unaligned
