/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 UEFI application entry that enables VFP/Advanced SIMD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

    .global ArmEfiEntry
    .thumb_func
ArmEfiEntry:
    mrc     p15, 0, r12, c1, c0, 2
    orr     r12, r12, #0x00F00000
    mcr     p15, 0, r12, c1, c0, 2
    isb
    mov     r12, #0x40000000
    vmsr    fpexc, r12
    b       ARM_EFI_ENTRY
