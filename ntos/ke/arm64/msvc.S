/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 register and runtime helpers for MSVC builds
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .balign 4
    .global KiArm64SetPcrRegister
KiArm64SetPcrRegister:
    mov     x18, x0
    ret

    .balign 4
    .global KiArm64RelocateStackPointers
KiArm64RelocateStackPointers:
    mov     x16, sp
    add     x16, x16, x0
    mov     sp, x16
    add     x29, x29, x0
    ret

    .balign 4
    .global _abnormal_termination
_abnormal_termination:
    mov     w0, wzr
    ret
