/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon core power-down in WFI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .balign 4
    .global AppleIdleReadCycleOverride
AppleIdleReadCycleOverride:
    mrs     x0, S3_5_C15_C5_0
    ret

    .balign 4
    .global AppleIdleWriteCycleOverride
AppleIdleWriteCycleOverride:
    msr     S3_5_C15_C5_0, x0
    isb
    ret

    .balign 4
    .global AppleIdleReadAuxiliaryId
AppleIdleReadAuxiliaryId:
    mrs     x0, aidr_el1
    ret

    .balign 4
    .global AppleIdlePowerDownWfi
AppleIdlePowerDownWfi:
    stp     x29, x30, [sp, #-0xc0]!
    mov     x29, sp
    stp     x18, x19, [sp, #0x10]
    stp     x20, x21, [sp, #0x20]
    stp     x22, x23, [sp, #0x30]
    stp     x24, x25, [sp, #0x40]
    stp     x26, x27, [sp, #0x50]
    stp     x28, x0, [sp, #0x60]
    stp     d8, d9, [sp, #0x70]
    stp     d10, d11, [sp, #0x80]
    stp     d12, d13, [sp, #0x90]
    stp     d14, d15, [sp, #0xa0]
    mrs     x9, fpcr
    mrs     x10, fpsr
    stp     x9, x10, [sp, #0xb0]

    ldr     x9, [x0, #0x10]
    fmov    d16, x9
    ldr     x9, [x0, #0x00]
    msr     S3_5_C15_C5_0, x9
    isb

1:
    dsb     sy
    wfi
    ldr     x0, [sp, #0x68]
    ldr     x9, [x0, #0x20]
    add     x9, x9, #1
    str     x9, [x0, #0x20]
    mrs     x9, isr_el1
    cbz     x9, 1b

    ldr     x9, [x0, #0x08]
    msr     S3_5_C15_C5_0, x9
    isb
    fmov    x9, d16
    str     x9, [x0, #0x18]

    ldp     x9, x10, [sp, #0xb0]
    msr     fpcr, x9
    msr     fpsr, x10
    ldp     d14, d15, [sp, #0xa0]
    ldp     d12, d13, [sp, #0x90]
    ldp     d10, d11, [sp, #0x80]
    ldp     d8, d9, [sp, #0x70]
    ldr     x28, [sp, #0x60]
    ldp     x26, x27, [sp, #0x50]
    ldp     x24, x25, [sp, #0x40]
    ldp     x22, x23, [sp, #0x30]
    ldp     x20, x21, [sp, #0x20]
    ldp     x18, x19, [sp, #0x10]
    ldp     x29, x30, [sp], #0xc0
    ret
