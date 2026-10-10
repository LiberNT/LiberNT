/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     wcscmp and wcsncmp for ARMv8.0-A with Advanced SIMD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .balign 64
    .global wcscmp
wcscmp:
    and     x9, x0, #0xfff
    cmp     x9, #0xff0
    b.hi    .Lwcscmp_scalar
    and     x9, x1, #0xfff
    cmp     x9, #0xff0
    b.hi    .Lwcscmp_scalar
    ldr     q0, [x0]
    ldr     q1, [x1]
    cmeq    v2.8h, v0.8h, v1.8h
    cmeq    v3.8h, v0.8h, #0
    bic     v2.16b, v2.16b, v3.16b
    mvn     v2.16b, v2.16b
    shrn    v2.8b, v2.8h, #4
    fmov    x2, d2
    cbnz    x2, .Lwcscmp_found
    add     x0, x0, #16
    add     x1, x1, #16
    b       wcscmp

.Lwcscmp_found:
    rbit    x2, x2
    clz     x2, x2
    lsr     x2, x2, #2
    ldrh    w3, [x0, x2]
    ldrh    w4, [x1, x2]
    sub     w0, w3, w4
    ret

.Lwcscmp_scalar:
    ldrh    w3, [x0], #2
    ldrh    w4, [x1], #2
    subs    w5, w3, w4
    b.ne    .Lwcscmp_diff
    cbnz    w3, wcscmp
    mov     w0, #0
    ret

.Lwcscmp_diff:
    mov     w0, w5
    ret

    .balign 64
    .global wcsncmp
wcsncmp:
    cbz     x2, .Lwcsncmp_zero

.Lwcsncmp_loop:
    cmp     x2, #8
    b.lo    .Lwcsncmp_scalar
    and     x9, x0, #0xfff
    cmp     x9, #0xff0
    b.hi    .Lwcsncmp_scalar
    and     x9, x1, #0xfff
    cmp     x9, #0xff0
    b.hi    .Lwcsncmp_scalar
    ldr     q0, [x0]
    ldr     q1, [x1]
    cmeq    v2.8h, v0.8h, v1.8h
    cmeq    v3.8h, v0.8h, #0
    bic     v2.16b, v2.16b, v3.16b
    mvn     v2.16b, v2.16b
    shrn    v2.8b, v2.8h, #4
    fmov    x3, d2
    cbnz    x3, .Lwcsncmp_found
    add     x0, x0, #16
    add     x1, x1, #16
    subs    x2, x2, #8
    b.ne    .Lwcsncmp_loop

.Lwcsncmp_zero:
    mov     w0, #0
    ret

.Lwcsncmp_found:
    rbit    x3, x3
    clz     x3, x3
    lsr     x3, x3, #2
    ldrh    w4, [x0, x3]
    ldrh    w5, [x1, x3]
    sub     w0, w4, w5
    ret

.Lwcsncmp_scalar:
    ldrh    w4, [x0], #2
    ldrh    w5, [x1], #2
    subs    w6, w4, w5
    b.ne    .Lwcsncmp_diff
    cbz     w4, .Lwcsncmp_zero
    subs    x2, x2, #1
    b.ne    .Lwcsncmp_loop
    mov     w0, #0
    ret

.Lwcsncmp_diff:
    mov     w0, w6
    ret
