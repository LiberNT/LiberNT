/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 UEFI application entry: VFP/Advanced SIMD and the image's own base relocations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .syntax unified
    .thumb
    .text
    .align 2

/*
 * Filled in by the armefi tool: where the base relocations are and how long,
 * the image base they assume and the size of the image. The firmware is shown
 * the last two words, a relocation block with nothing in it.
 */
ArmEfiRelocations:
    .long 0x4D524145
    .long 0x434C5246
    .long 0
    .long 0
    .long 0
    .long 0
    .long 0
    .long 0
    .long 8
ArmEfiLinkedAddress:
    .long ArmEfiRelocations

    .global ArmEfiEntry
    .thumb_func
ArmEfiEntry:
    mrc     p15, 0, r12, c1, c0, 2
    orr     r12, r12, #0x00F00000
    mcr     p15, 0, r12, c1, c0, 2
    isb
    mov     r12, #0x40000000
    vmsr    fpexc, r12

    /*
     * Where this code runs less where it was linked to run is what every
     * relocation is off by. The linker marks an address in a code section as
     * Thumb, which the descriptor's is not.
     */
    push    {r0, r1, r4, lr}
    mov     r2, r1
    adr     r0, ArmEfiRelocations
    ldr     r1, ArmEfiLinkedAddress
    bic     r1, r1, #1
    subs    r1, r0, r1
    it      ne
    blne    ArmEfiRelocate
    pop     {r0, r1, r4, lr}
    b       ARM_EFI_ENTRY
