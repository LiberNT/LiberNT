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

    .p2align 2
    .globl EmuInvokeNative
    .seh_proc EmuInvokeNative
EmuInvokeNative:
    addi sp, sp, -16
    .seh_set_cfa x2, 16
    sd ra, 8(sp)
    .seh_save_gpr x1, -8
    sd s0, 0(sp)
    .seh_save_gpr x8, -16
    addi s0, sp, 16
    .seh_set_cfa x8, 0
    .seh_endprologue
    slli t0, a3, 3
    addi t0, t0, 15
    andi t0, t0, -16
    sub sp, sp, t0
    li t1, 0
1:  bgeu t1, a3, 2f
    slli t2, t1, 3
    add t3, a2, t2
    ld t4, 0(t3)
    add t3, sp, t2
    sd t4, 0(t3)
    addi t1, t1, 1
    j 1b
2:  mv t5, a0
    mv t6, a1
    .set slot, 0
    .irp reg, a0,a1,a2,a3,a4,a5,a6,a7
    ld \reg, slot * 8(t6)
    .set slot, slot + 1
    .endr
    jalr ra, t5, 0
    .seh_startepilogue
    addi sp, s0, -16
    .seh_set_cfa x2, 16
    ld s0, 0(sp)
    .seh_same_gpr x8
    ld ra, 8(sp)
    .seh_same_gpr x1
    addi sp, sp, 16
    .seh_set_cfa x2, 0
    ret
    .seh_endepilogue
    .seh_endproc

    .macro save_run_frame
    addi sp, sp, -128
    .seh_set_cfa x2, 128
    sd ra, 120(sp)
    .seh_save_gpr x1, -8
    .set slot, 0
    .irp reg,8,9,18,19,20,21,22,23,24,25,26,27
    sd x\reg, slot(sp)
    .seh_save_gpr x\reg, slot - 128
    .set slot, slot + 8
    .endr
    .endm

    .p2align 2
    .globl EmuRunGuest
    .seh_proc EmuRunGuest
EmuRunGuest:
    save_run_frame
    mv s0, sp
    .seh_set_cfa x8, 128
    .seh_endprologue
    sd sp, 0(a1)
    beqz a2, 1f
    mv sp, a2
1:
    call EmuCoreRun
    .word 0
    .seh_endproc

    /* Unwinds to the run frame of EmuRunGuest, over recompiled code; EmuBoundaryHandler walks the AMD64 frames. */
    .p2align 2
    .globl EmuEnterBoundary
    .seh_proc EmuEnterBoundary
EmuEnterBoundary:
    .seh_handler EmuBoundaryHandler, @except, @unwind
    ld t0, 16(a0)
    addi t0, t0, -48
    sd sp, 32(t0)
    sd sp, 64(a0)
    mv sp, t0
    sd ra, 24(sp)
    sd s0, 16(sp)
    sd s1, 8(sp)
    sd a0, 0(sp)
    ld s1, 16(a0)
    .seh_set_cfa x9, 128
    .seh_save_gpr x1, -8
    .set slot, 0
    .irp reg,8,9,18,19,20,21,22,23,24,25,26,27
    .seh_save_gpr x\reg, slot - 128
    .set slot, slot + 8
    .endr
    .seh_endprologue
    call EmuBoundaryRoutine
1:
    ld s1, 8(sp)
    ld s0, 16(sp)
    ld ra, 24(sp)
    ld sp, 32(sp)
    ret
    .globl EmuBoundaryResume
EmuBoundaryResume:
    call EmuBoundaryContinue
    j 1b
    .seh_endproc
