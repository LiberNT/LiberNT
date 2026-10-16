/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */

    .section .text$HalpRiscvApTrampoline, "xr"
    .balign 4
    .global HalpRiscvApTrampoline
    .global HalpRiscvApTrampolineEnd
    .global HalpRiscvApVirtualEntry
/* SBI HSM: a0 = hart ID, a1 = physical startup data, satp = 0. */
HalpRiscvApTrampoline:
    csrci sstatus, 2
    csrw sie, zero
    lla t5, HalpRiscvApPark
    csrw stvec, t5
    addi t5, a0, 1
    sd t5, 64(a1)
    csrw sscratch, zero
    fence rw, rw
    ld t0, 0(a1)                 /* Temporary root with identity trampoline */
    ld t3, 8(a1)                 /* Kernel root */
    ld sp, 16(a1)
    ld t1, 24(a1)                /* Kernel AP entry */
    ld t2, 56(a1)                /* Virtual continuation in HAL .text */
    ld t4, 40(a1)
    csrw sscratch, t4
    ld t4, 48(a1)
    csrw sstatus, t4
    ld a0, 32(a1)                /* Kernel-owned startup context */
    mv gp, zero
    mv tp, zero
    fence.i
    sfence.vma zero, zero
    csrw satp, t0
    sfence.vma zero, zero
    jr t2

    .balign 4
HalpRiscvApPark:
    wfi
    j HalpRiscvApPark
HalpRiscvApTrampolineEnd:

    .balign 4
HalpRiscvApVirtualEntry:
    csrw satp, t3
    sfence.vma zero, zero
    fence.i
    jr t1
