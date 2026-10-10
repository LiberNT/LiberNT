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

    .p2align 2
    .globl Felix86NtProbeV
Felix86NtProbeV:
    csrr a0, vlenb
    vsetivli t0, 1, e8, m1, ta, ma
    snez a0, a0
    ret

    .option arch, +zvbb
    .p2align 2
    .globl Felix86NtProbeZvbb
Felix86NtProbeZvbb:
    vsetivli t0, 1, e8, m1, ta, ma
    vandn.vv v1, v1, v1
    li a0, 1
    ret

    .option arch, +zvbc
    .p2align 2
    .globl Felix86NtProbeZvbc
Felix86NtProbeZvbc:
    vsetivli t0, 1, e64, m1, ta, ma
    vclmul.vv v1, v1, v1
    li a0, 1
    ret

    .option arch, +zvkned
    .p2align 2
    .globl Felix86NtProbeZvkned
Felix86NtProbeZvkned:
    vsetivli t0, 4, e32, m1, ta, ma
    vaesz.vs v1, v2
    li a0, 1
    ret

    .option arch, +zvknha
    .p2align 2
    .globl Felix86NtProbeZvknha
Felix86NtProbeZvknha:
    vsetivli t0, 4, e32, m1, ta, ma
    vsha2ms.vv v1, v2, v3
    li a0, 1
    ret

    .option arch, +zvfhmin
    .p2align 2
    .globl Felix86NtProbeZvfhmin
Felix86NtProbeZvfhmin:
    vsetivli t0, 1, e16, mf2, ta, ma
    vfwcvt.f.f.v v2, v1
    li a0, 1
    ret

    .option pop

    .option push
    .option arch, +zba, +zbb, +zbs, +zbc, +zicond, +zfa, +zacas, +zabha, +zknd, +zicbom

    .p2align 2
    .globl Felix86NtProbeZba
Felix86NtProbeZba:
    sh1add a0, zero, zero
    li a0, 1
    ret

    .p2align 2
    .globl Felix86NtProbeZbb
Felix86NtProbeZbb:
    clz a0, zero
    li a0, 1
    ret

    .p2align 2
    .globl Felix86NtProbeZbs
Felix86NtProbeZbs:
    bseti a0, zero, 1
    li a0, 1
    ret

    .p2align 2
    .globl Felix86NtProbeZbc
Felix86NtProbeZbc:
    clmul a0, zero, zero
    li a0, 1
    ret

    .p2align 2
    .globl Felix86NtProbeZicond
Felix86NtProbeZicond:
    czero.eqz a0, zero, zero
    li a0, 1
    ret

    .p2align 2
    .globl Felix86NtProbeZfa
Felix86NtProbeZfa:
    fli.d ft0, 1.0
    li a0, 1
    ret

    .p2align 2
    .globl Felix86NtProbeZacas
Felix86NtProbeZacas:
    la t2, Felix86NtProbeScratch
    li t0, 0
    li t1, 1
    amocas.d t0, t1, (t2)
    li a0, 1
    ret

    .p2align 2
    .globl Felix86NtProbeZabha
Felix86NtProbeZabha:
    la t2, Felix86NtProbeScratch
    amoadd.b t0, zero, (t2)
    li a0, 1
    ret

    .p2align 2
    .globl Felix86NtProbeZknd
Felix86NtProbeZknd:
    aes64ds a0, zero, zero
    li a0, 1
    ret

    .p2align 2
    .globl Felix86NtProbeZicbom
Felix86NtProbeZicbom:
    la t0, Felix86NtProbeScratch
    cbo.clean (t0)
    li a0, 1
    ret

    .option pop

    .p2align 2
    .globl Felix86NtProbeZicclsm
Felix86NtProbeZicclsm:
    la t1, Felix86NtProbeScratch
    ld t0, 1(t1)
    sd t0, 3(t1)
    li a0, 1
    ret

    .bss
    .p2align 6
Felix86NtProbeScratch:
    .space 128
