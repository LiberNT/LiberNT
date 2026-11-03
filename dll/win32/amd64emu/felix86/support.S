/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Register-level support for the felix86 recompiler on RISC-V64 NT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .text

    .p2align 2
    .globl Felix86NtSetJump
Felix86NtSetJump:
    sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    sd s1, 24(a0)
    .set slot, 0
    .irp reg, s2,s3,s4,s5,s6,s7,s8,s9,s10,s11
    sd \reg, 32 + slot * 8(a0)
    .set slot, slot + 1
    .endr
    fsd fs0, 112(a0)
    fsd fs1, 120(a0)
    .set slot, 0
    .irp reg, fs2,fs3,fs4,fs5,fs6,fs7,fs8,fs9,fs10,fs11
    fsd \reg, 128 + slot * 8(a0)
    .set slot, slot + 1
    .endr
    li a0, 0
    ret

    .p2align 2
    .globl Felix86NtLongJump
Felix86NtLongJump:
    ld ra, 0(a0)
    ld sp, 8(a0)
    ld s0, 16(a0)
    ld s1, 24(a0)
    .set slot, 0
    .irp reg, s2,s3,s4,s5,s6,s7,s8,s9,s10,s11
    ld \reg, 32 + slot * 8(a0)
    .set slot, slot + 1
    .endr
    fld fs0, 112(a0)
    fld fs1, 120(a0)
    .set slot, 0
    .irp reg, fs2,fs3,fs4,fs5,fs6,fs7,fs8,fs9,fs10,fs11
    fld \reg, 128 + slot * 8(a0)
    .set slot, slot + 1
    .endr
    mv a0, a1
    ret

    .option push
    .option arch, +v

    .p2align 2
    .globl Felix86NtStoreVectors
Felix86NtStoreVectors:
    csrw vstart, zero
    .irp reg, v16,v17,v18,v19,v20,v21,v22,v23,v24,v25,v26,v27,v28,v29,v30,v31
    vs1r.v \reg, (a0)
    add a0, a0, a1
    .endr
    ret

    .p2align 2
    .globl Felix86NtReadVectorBytes
Felix86NtReadVectorBytes:
    csrr a0, vlenb
    ret

    .option pop

    .p2align 2
    .globl Felix86NtEnterDispatcher
Felix86NtEnterDispatcher:
    sub t0, sp, a3
    sd t0, 0(a1)
    jr a2
