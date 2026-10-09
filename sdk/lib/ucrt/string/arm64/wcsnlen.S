/*
 * PROJECT:     LiberNT Universal CRT
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     wcslen and wcsnlen for ARMv8.0-A with Advanced SIMD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .balign 64
    .global wcslen
wcslen:
    tbnz    x0, #0, .Lwcslen_unaligned
    bic     x2, x0, #15
    ldr     q0, [x2]
    cmeq    v0.8h, v0.8h, #0
    shrn    v0.8b, v0.8h, #4
    fmov    x3, d0
    lsl     x4, x0, #2
    lsr     x3, x3, x4
    cbz     x3, .Lwcslen_next
    rbit    x3, x3
    clz     x3, x3
    lsr     x0, x3, #3
    ret

.Lwcslen_next:
    add     x2, x2, #16
    tbz     x2, #4, .Lwcslen_loop
    ldr     q0, [x2]
    cmeq    v0.8h, v0.8h, #0
    shrn    v0.8b, v0.8h, #4
    fmov    x3, d0
    cbnz    x3, .Lwcslen_found
    add     x2, x2, #16

.Lwcslen_loop:
    ldp     q0, q1, [x2], #32
    cmeq    v0.8h, v0.8h, #0
    cmeq    v1.8h, v1.8h, #0
    orr     v2.16b, v0.16b, v1.16b
    umaxp   v2.16b, v2.16b, v2.16b
    fmov    x3, d2
    cbz     x3, .Lwcslen_loop
    sub     x2, x2, #32
    shrn    v0.8b, v0.8h, #4
    fmov    x3, d0
    cbnz    x3, .Lwcslen_found
    add     x2, x2, #16
    shrn    v1.8b, v1.8h, #4
    fmov    x3, d1

.Lwcslen_found:
    rbit    x3, x3
    clz     x3, x3
    sub     x2, x2, x0
    add     x2, x2, x3, lsr #2
    lsr     x0, x2, #1
    ret

.Lwcslen_unaligned:
    mov     x2, x0
1:
    ldrh    w3, [x2], #2
    cbnz    w3, 1b
    sub     x2, x2, x0
    sub     x2, x2, #2
    lsr     x0, x2, #1
    ret

    .balign 64
    .global wcsnlen
wcsnlen:
    cbz     x1, .Lwcsnlen_zero
    lsr     x9, x1, #62
    cbnz    x9, wcslen
    add     x10, x0, x1, lsl #1
    cmp     x10, x0
    b.lo    wcslen
    tbnz    x0, #0, .Lwcsnlen_unaligned
    bic     x2, x0, #15
    ldr     q0, [x2]
    cmeq    v0.8h, v0.8h, #0
    shrn    v0.8b, v0.8h, #4
    fmov    x3, d0
    lsl     x4, x0, #2
    lsr     x3, x3, x4
    cbz     x3, .Lwcsnlen_next
    rbit    x3, x3
    clz     x3, x3
    lsr     x0, x3, #3
    cmp     x0, x1
    csel    x0, x0, x1, lo
    ret

.Lwcsnlen_next:
    add     x2, x2, #16
    tbz     x2, #4, .Lwcsnlen_loop
    cmp     x2, x10
    b.hs    .Lwcsnlen_max
    ldr     q0, [x2]
    cmeq    v0.8h, v0.8h, #0
    shrn    v0.8b, v0.8h, #4
    fmov    x3, d0
    cbnz    x3, .Lwcsnlen_found
    add     x2, x2, #16

.Lwcsnlen_loop:
    cmp     x2, x10
    b.hs    .Lwcsnlen_max
    ldp     q0, q1, [x2], #32
    cmeq    v0.8h, v0.8h, #0
    cmeq    v1.8h, v1.8h, #0
    orr     v2.16b, v0.16b, v1.16b
    umaxp   v2.16b, v2.16b, v2.16b
    fmov    x3, d2
    cbz     x3, .Lwcsnlen_loop
    sub     x2, x2, #32
    shrn    v0.8b, v0.8h, #4
    fmov    x3, d0
    cbnz    x3, .Lwcsnlen_found
    add     x2, x2, #16
    shrn    v1.8b, v1.8h, #4
    fmov    x3, d1

.Lwcsnlen_found:
    rbit    x3, x3
    clz     x3, x3
    sub     x2, x2, x0
    add     x2, x2, x3, lsr #2
    lsr     x0, x2, #1
    cmp     x0, x1
    csel    x0, x0, x1, lo
    ret

.Lwcsnlen_max:
    mov     x0, x1
    ret

.Lwcsnlen_zero:
    mov     x0, #0
    ret

.Lwcsnlen_unaligned:
    mov     x2, #0
1:
    ldrh    w3, [x0, x2, lsl #1]
    cbz     w3, 2f
    add     x2, x2, #1
    cmp     x2, x1
    b.lo    1b
2:
    mov     x0, x2
    ret
