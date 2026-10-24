/*
 * PROJECT:     LiberNT NT Layer DLL
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64EC call checker and x64 call helper for MSVC builds
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright 1999, 2005, 2023 Alexandre Julliard
 */

    .text
    .align 4

    .global ChpepArm64EcCheckCall
ChpepArm64EcCheckCall:
1:
    ldr     x16, [x18, #0x60]
    ldr     x16, [x16, #0x368]
    cbz     x16, 4f
    lsr     x17, x11, #18
    ldr     x16, [x16, x17, lsl #3]
    lsr     x9, x11, #12
    lsr     x16, x16, x9
    tbnz    x16, #0, 5f
    tst     x11, #15
    b.ne    3f
    ldr     x16, [x11]
    ldr     x17, 6f
    cmp     x16, x17
    b.ne    3f
    ldr     x16, [x11, #8]
    ldr     x17, 7f
    eor     x17, x16, x17
    tst     x17, #0xffff
    b.ne    4f
    add     x11, x11, #14
    lsr     x9, x16, #16
    add     x11, x11, w9, sxtw
    ret
3:
    ldrb    w16, [x11]
    cmp     w16, #0xff
    b.ne    4f
    ldrb    w16, [x11, #1]
    cmp     w16, #0x25
    b.ne    4f
    ldr     w9, [x11, #2]
    add     x16, x11, #6
    ldr     x11, [x16, w9, sxtw]
    b       1b
4:
    mov     x9, x11
    mov     x11, x10
5:
    ret
    .balign 8
6:
    .byte 0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x20, 0x55
7:
    .byte 0x5d, 0xe9, 0, 0, 0, 0, 0, 0

    .align 4
    .global ChpepInvokeX64Routine
    .seh_proc ChpepInvokeX64Routine
ChpepInvokeX64Routine:
    stp     x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov     x29, sp
    .seh_set_fp
    .seh_endprologue
    sub     sp, sp, #0x110
    stp     x19, x20, [sp, #0x30]
    stp     x21, x22, [sp, #0x40]
    stp     x23, x24, [sp, #0x50]
    stp     x25, x26, [sp, #0x60]
    stp     x27, x28, [sp, #0x70]
    str     x29, [sp, #0x80]
    stp     q8, q9, [sp, #0x90]
    stp     q10, q11, [sp, #0xb0]
    stp     q12, q13, [sp, #0xd0]
    stp     q14, q15, [sp, #0xf0]
    str     x4, [sp, #0x20]
    mov     x9, x5
    mov     x16, x6
    blr     x16
    ldp     q14, q15, [sp, #0xf0]
    ldp     q12, q13, [sp, #0xd0]
    ldp     q10, q11, [sp, #0xb0]
    ldp     q8, q9, [sp, #0x90]
    ldr     x29, [sp, #0x80]
    ldp     x27, x28, [sp, #0x70]
    ldp     x25, x26, [sp, #0x60]
    ldp     x23, x24, [sp, #0x50]
    ldp     x21, x22, [sp, #0x40]
    ldp     x19, x20, [sp, #0x30]
    add     sp, sp, #0x110
    mov     x0, x8
    .seh_startepilogue
    ldp     x29, x30, [sp], #16
    .seh_save_fplr_x 16
    .seh_endepilogue
    ret
    .seh_endproc
