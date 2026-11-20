/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 kernel entry, exception vectors, trap frames and context switch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kiarm.h"

#define MODE_USER   0x10
#define MODE_SVC    0x13
#define MODE_SYSTEM 0x1F

    .syntax unified
    .thumb

/*
 * Every exception enters with SCTLR.TE set (Thumb vectors). The stub stores the
 * return state on the SVC stack with SRS, switches to SVC mode and builds a
 * KI_ARM_TRAP_RECORD: VFP state, exception frame (R4-R11) and trap frame. The
 * pair SRS stored is the trap frame's Pc and Cpsr, so the record ends where
 * the interrupted SVC stack pointer was.
 */
.macro TRAP_STUB Name, Adjust, Vector
    .thumb_func
\Name:
    .if \Adjust
    sub lr, lr, #\Adjust
    .endif
    srsdb sp!, #MODE_SVC
    cps #MODE_SVC
    sub sp, sp, #(KI_REC_SIZE - 8)
    str r0, [sp, #(KI_REC_TF + KI_TF_R0)]
    mov r0, #\Vector
    b KiArmTrapSave
.endm

    .text
    .balign 32
    .global KiArmVectorTable
KiArmVectorTable:
    b.w KiArmResetVector
    b.w KiArmUndefinedVector
    b.w KiArmSvcVector
    b.w KiArmPrefetchAbortVector
    b.w KiArmDataAbortVector
    b.w KiArmResetVector
    b.w KiArmIrqVector
    b.w KiArmFiqVector

    .thumb_func
KiArmResetVector:
    cpsid if
1:  wfi
    b 1b

    .thumb_func
KiArmFiqVector:
    cpsid if
1:  wfi
    b 1b

    TRAP_STUB KiArmUndefinedVector, 0, KI_VECTOR_UNDEFINED
    TRAP_STUB KiArmSvcVector, 0, KI_VECTOR_SVC
    TRAP_STUB KiArmPrefetchAbortVector, 4, KI_VECTOR_PABORT
    TRAP_STUB KiArmDataAbortVector, 8, KI_VECTOR_DABORT
    TRAP_STUB KiArmIrqVector, 4, KI_VECTOR_IRQ

/* r0 = vector, sp = record base, original r0 saved; every other register live. */
    .thumb_func
KiArmTrapSave:
    str r1, [sp, #(KI_REC_TF + KI_TF_R0 + 4)]
    strd r2, r3, [sp, #(KI_REC_TF + KI_TF_R2)]
    str r12, [sp, #(KI_REC_TF + KI_TF_R12)]
    str r11, [sp, #(KI_REC_TF + KI_TF_R11)]
    add r1, sp, #(KI_REC_EF + KI_EF_R4)
    stm r1, {r4-r11}
    vmrs r2, fpscr
    str r2, [sp, #KI_VFP_FPSCR]
    add r1, sp, #KI_VFP_D
    vstmia r1!, {d0-d15}
    vstmia r1, {d16-d31}
    ldr r2, [sp, #(KI_REC_TF + KI_TF_CPSR)]
    and r3, r2, #0x1F
    cmp r3, #MODE_USER
    beq 1f
    add r3, sp, #KI_REC_SIZE
    str r3, [sp, #(KI_REC_TF + KI_TF_SP)]
    str lr, [sp, #(KI_REC_TF + KI_TF_LR)]
    b 2f
1:  cps #MODE_SYSTEM
    mov r3, sp
    mov r12, lr
    cps #MODE_SVC
    str r3, [sp, #(KI_REC_TF + KI_TF_SP)]
    str r12, [sp, #(KI_REC_TF + KI_TF_LR)]
2:  mov r3, #0
    str r3, [sp, #(KI_REC_TF + KI_TF_FLAGS)]
    str r3, [sp, #(KI_REC_EF + KI_EF_RETURN)]
    mov r3, sp
    str r3, [sp, #(KI_REC_TF + KI_TF_VFP_STATE)]
    mov r1, r0
    add r0, sp, #KI_REC_TF
    mov r4, sp
    bic r2, r4, #7
    mov sp, r2
    bl KiArmTrapDispatch
    mov sp, r4
    b KiArmTrapExitRecord

/* sp = record base. Restores every register the record holds and returns. */
    .thumb_func
KiArmTrapExitRecord:
    add r1, sp, #KI_VFP_D
    vldmia r1!, {d0-d15}
    vldmia r1, {d16-d31}
    ldr r2, [sp, #KI_VFP_FPSCR]
    vmsr fpscr, r2
    add r1, sp, #(KI_REC_EF + KI_EF_R4)
    ldm r1, {r4-r10}
    ldr r11, [sp, #(KI_REC_TF + KI_TF_R11)]
    ldr r2, [sp, #(KI_REC_TF + KI_TF_CPSR)]
    and r3, r2, #0x1F
    cmp r3, #MODE_USER
    bne 3f
    ldr r3, [sp, #(KI_REC_TF + KI_TF_SP)]
    ldr r12, [sp, #(KI_REC_TF + KI_TF_LR)]
    cps #MODE_SYSTEM
    mov sp, r3
    mov lr, r12
    cps #MODE_SVC
    clrex
    b 4f
3:  ldr lr, [sp, #(KI_REC_TF + KI_TF_LR)]
    clrex
4:  ldrd r0, r1, [sp, #(KI_REC_TF + KI_TF_R0)]
    ldrd r2, r3, [sp, #(KI_REC_TF + KI_TF_R2)]
    ldr r12, [sp, #(KI_REC_TF + KI_TF_R12)]
    add sp, sp, #(KI_REC_TF + KI_TF_PC)
    rfeia sp!

/* VOID KiArmRestoreTrapFrame(PKTRAP_FRAME TrapFrame): leave through a record. */
    .global KiArmRestoreTrapFrame
    .thumb_func
KiArmRestoreTrapFrame:
    cpsid i
    sub r0, r0, #KI_REC_TF
    mov sp, r0
    b KiArmTrapExitRecord

/*
 * KiSystemStartup(PLOADER_PARAMETER_BLOCK LoaderBlock): the loader enters in
 * SVC mode with the MMU on, interrupts masked and sp on the boot kernel stack.
 */
    .global KiSystemStartup
    .thumb_func
KiSystemStartup:
    cpsid if
    mrc p15, 0, r1, c1, c0, 0
    orr r1, r1, #0x40000000
    mcr p15, 0, r1, c1, c0, 0
    movw r1, #:lower16:KiArmVectorTable
    movt r1, #:upper16:KiArmVectorTable
    mcr p15, 0, r1, c12, c0, 0
    mrc p15, 0, r1, c1, c0, 2
    orr r1, r1, #0x00F00000
    mcr p15, 0, r1, c1, c0, 2
    isb
    mov r1, #0x40000000
    vmsr fpexc, r1
    mov r1, #0
    vmsr fpscr, r1
    mov r1, sp
    bic r1, r1, #7
    mov sp, r1
    mov r11, #0
    mov lr, #0
    bl KiArmSystemStartup
1:  wfi
    b 1b

/*
 * VOID KiArmSwapStack(PVOID *OldStack, PVOID NewStack, PVOID Context, PKARM_SWITCH_ROUTINE Resume)
 * Saves the AAPCS callee-saved state on the current stack, publishes the stack
 * in *OldStack, loads NewStack and returns Resume(Context) on the new stack.
 */
    .global KiArmSwapStack
    .thumb_func
KiArmSwapStack:
    push {r4-r11, lr}
    sub sp, sp, #4
    vpush {d8-d15}
    mov r12, sp
    str r12, [r0]
    mov sp, r1
    mov r0, r2
    blx r3
    vpop {d8-d15}
    add sp, sp, #4
    pop {r4-r11, pc}

/* First code of a new thread; sp addresses its KSTART_FRAME. */
    .global KiThreadStartup
    .thumb_func
KiThreadStartup:
    ldr r4, [sp, #0]
    ldr r5, [sp, #4]
    ldr r6, [sp, #8]
    add sp, sp, #16
    cpsie i
    mov r0, #1
    bl KfLowerIrql
    mov r0, r5
    mov r1, r6
    blx r4
    bl KiArmStartUserThread
1:  wfi
    b 1b

/* VOID KiArmSaveVfpState(PKARM_VFP_STATE State) */
    .global KiArmSaveVfpState
    .thumb_func
KiArmSaveVfpState:
    vmrs r1, fpscr
    str r1, [r0, #KI_VFP_FPSCR]
    add r0, r0, #KI_VFP_D
    vstmia r0!, {d0-d15}
    vstmia r0, {d16-d31}
    bx lr

/* VOID KiArmRestoreVfpState(PKARM_VFP_STATE State) */
    .global KiArmRestoreVfpState
    .thumb_func
KiArmRestoreVfpState:
    ldr r1, [r0, #KI_VFP_FPSCR]
    vmsr fpscr, r1
    add r0, r0, #KI_VFP_D
    vldmia r0!, {d0-d15}
    vldmia r0, {d16-d31}
    bx lr

/* VOID KiArmCallOnStack(PVOID Argument0, PVOID Argument1, PVOID Routine, PVOID StackTop) */
    .global KiArmCallOnStack
    .thumb_func
KiArmCallOnStack:
    push {r4, lr}
    mov r4, sp
    bic r3, r3, #7
    mov sp, r3
    blx r2
    mov sp, r4
    pop {r4, pc}

/*
 * ULONG KiArmInvokeSystemService(PVOID Routine, PULONG Arguments, ULONG StackWords)
 * Arguments[0..3] go to r0-r3 and Arguments[4..] to the stack, as the caller had them.
 */
    .global KiArmInvokeSystemService
    .thumb_func
KiArmInvokeSystemService:
    push {r4-r7, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1
    mov r7, sp
    lsl r6, r2, #2
    add r6, r6, #7
    bic r6, r6, #7
    sub sp, sp, r6
    mov r3, #0
1:  cmp r3, r2
    bhs 2f
    add r0, r3, #4
    ldr r1, [r5, r0, lsl #2]
    str r1, [sp, r3, lsl #2]
    add r3, r3, #1
    b 1b
2:  ldm r5, {r0-r3}
    blx r4
    mov sp, r7
    add sp, sp, #4
    pop {r4-r7, pc}

/*
 * NTSTATUS KiArmCallUserMode(PKTRAP_FRAME Frame, PVOID *Result, PULONG ResultLength)
 * Saves d8-d15 and builds a KCALLOUT_FRAME (R4-R11, return address, result
 * pointers) for KiCallbackReturn, then enters the callback; it never returns here.
 */
    .global KiArmCallUserMode
    .thumb_func
KiArmCallUserMode:
    vpush {d8-d15}
    sub sp, sp, #KI_EF_SIZE
    add r12, sp, #KI_EF_R4
    stm r12, {r4-r11}
    str lr, [sp, #KI_EF_RETURN]
    str r1, [sp, #8]
    str r2, [sp, #12]
    mov r0, sp
    bl KiArmEnterCallout
1:  udf #0xfe
    b 1b

/* VOID KiCallbackReturn(PVOID Stack, NTSTATUS Status): resume KiArmCallUserMode's caller. */
    .global KiCallbackReturn
    .thumb_func
KiCallbackReturn:
    mov sp, r0
    add r2, sp, #KI_EF_R4
    ldm r2, {r4-r11}
    ldr lr, [sp, #KI_EF_RETURN]
    add sp, sp, #KI_EF_SIZE
    vpop {d8-d15}
    mov r0, r1
    bx lr
