/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V vector register file save and restore
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text
    .option push
    .option arch, +v

    .p2align 2
    .global KiRiscvSaveVectorRegisters
KiRiscvSaveVectorRegisters:
    csrr t0, vstart
    sd t0, 0(a0)
    csrw vstart, zero
    csrr t0, vtype
    sd t0, 8(a0)
    csrr t0, vl
    sd t0, 16(a0)
    csrr t0, vcsr
    sd t0, 24(a0)
    addi a0, a0, 48
    slli t1, a1, 3
    vs8r.v v0, (a0)
    add a0, a0, t1
    vs8r.v v8, (a0)
    add a0, a0, t1
    vs8r.v v16, (a0)
    add a0, a0, t1
    vs8r.v v24, (a0)
    ret

    .p2align 2
    .global KiRiscvRestoreVectorRegisters
KiRiscvRestoreVectorRegisters:
    csrw vstart, zero
    addi t2, a0, 48
    slli t1, a1, 3
    vl8re8.v v0, (t2)
    add t2, t2, t1
    vl8re8.v v8, (t2)
    add t2, t2, t1
    vl8re8.v v16, (t2)
    add t2, t2, t1
    vl8re8.v v24, (t2)
    ld t0, 16(a0)
    ld t1, 8(a0)
    vsetvl zero, t0, t1
    ld t0, 24(a0)
    csrw vcsr, t0
    ld t0, 0(a0)
    csrw vstart, t0
    ret

    .p2align 2
    .global KiRiscvReadVectorLength
KiRiscvReadVectorLength:
    csrr a0, vlenb
    ret

    .option pop
