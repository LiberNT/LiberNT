/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Enter a native system service with a marshalled argument vector
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text
    .p2align 2
    .globl EmuInvokeSystemService
    .seh_proc EmuInvokeSystemService
EmuInvokeSystemService:
    addi sp, sp, -208
    .seh_set_cfa x2, 208
    sd ra, 200(sp)
    .seh_save_gpr x1, -8
    .seh_endprologue
    mv t0, a0
    mv t1, a1
    .set slot, 0
    .rept 24
    ld t2, 64 + slot * 8(t1)
    sd t2, slot * 8(sp)
    .set slot, slot + 1
    .endr
    .set slot, 0
    .irp reg, a0,a1,a2,a3,a4,a5,a6,a7
    ld \reg, slot * 8(t1)
    .set slot, slot + 1
    .endr
    ecall
    .seh_startepilogue
    ld ra, 200(sp)
    .seh_same_gpr x1
    addi sp, sp, 208
    .seh_set_cfa x2, 0
    ret
    .seh_endepilogue
    .seh_endproc
