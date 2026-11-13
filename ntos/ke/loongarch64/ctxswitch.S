/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 thread context switch, first-run entry and the
 *              stack-transition wrappers
 *
 * KiSwapContext saves the psABI callee-saved bank (s0-s8, fp, ra) in a
 * KSWITCH_FRAME on the outgoing kernel stack, publishes that stack in
 * KTHREAD.KernelStack, loads the incoming thread's KernelStack and resumes
 * it through the same frame. A thread that has never run has a frame whose
 * Ra is KiThreadStartup and a KSTART_FRAME directly above it, which
 * KiInitializeContextThread builds.
 *
 * $tp is the psABI thread pointer and the kernel's NtCurrentTeb() reads it,
 * so it is reloaded from the incoming thread's KTHREAD.Teb on every switch:
 * a kernel thread therefore runs with $tp == NULL.
 *
 * $r21 is the KPCR base. The PCR is a property of the processor, not of the
 * thread, so a switch inside one processor never writes it.
 */

#include "asmoffsets.h"

    .section .text$KiRetireDpcListInDpcStack, "ax", @progbits
    .balign 4
    .global KiRetireDpcListInDpcStack
    .type   KiRetireDpcListInDpcStack, @function
/* VOID KiRetireDpcListInDpcStack(PKPRCB Prcb, PVOID DpcStack)
 *
 * Retire the deferred-call list on the processor-owned DPC stack. KiRetireDpcList
 * preserves the ordinary psABI callee-saved bank, so this wrapper only has to
 * move $sp and keep the two words it must not lose. The thread stack is
 * restored before the caller can consider a quantum-end switch. */
KiRetireDpcListInDpcStack:
    move    $t0, $sp
    move    $sp, $a1
    addi.d  $sp, $sp, -16
    st.d    $t0, $sp, 0
    st.d    $ra, $sp, 8
    bl      KiRetireDpcList
    ld.d    $t0, $sp, 0
    ld.d    $ra, $sp, 8
    move    $sp, $t0
    jr      $ra
    .size   KiRetireDpcListInDpcStack, . - KiRetireDpcListInDpcStack

    .section .text$KiCallOnKernelStack, "ax", @progbits
    .balign 4
    .global KiCallOnKernelStack
    .type   KiCallOnKernelStack, @function
/* VOID KiCallOnKernelStack(PVOID Context, PVOID StackTop)
 *
 * Run KiRunStackCallout(Context) with $sp == StackTop. */
KiCallOnKernelStack:
    move    $t0, $sp
    move    $sp, $a1
    addi.d  $sp, $sp, -16
    st.d    $t0, $sp, 0
    st.d    $ra, $sp, 8
    bl      KiRunStackCallout
    ld.d    $t0, $sp, 0
    ld.d    $ra, $sp, 8
    move    $sp, $t0
    jr      $ra
    .size   KiCallOnKernelStack, . - KiCallOnKernelStack

    .section .text$KiSwapContext, "ax", @progbits
    .balign 4
    .global KiSwapContext
    .type   KiSwapContext, @function
/* BOOLEAN KiSwapContext(KIRQL WaitIrql, PKTHREAD OldThread)
 *
 * The scheduler has already stored the target thread in Prcb.CurrentThread.
 * Returns TRUE when the resumed thread must deliver kernel APCs. CRMD.IE is
 * a processor property: it is masked across the stack switch and the state
 * the caller entered with is restored on the incoming stack, so a switch
 * never silently enables interrupts on a thread that masked them. */
KiSwapContext:
    addi.d  $sp, $sp, -SwitchFrameLength
    st.d    $s0, $sp, SwS0
    st.d    $s1, $sp, SwS1
    st.d    $s2, $sp, SwS2
    st.d    $s3, $sp, SwS3
    st.d    $s4, $sp, SwS4
    st.d    $s5, $sp, SwS5
    st.d    $s6, $sp, SwS6
    st.d    $s7, $sp, SwS7
    st.d    $s8, $sp, SwS8
    st.d    $fp, $sp, SwFp
    st.d    $ra, $sp, SwRa
    st.b    $a0, $sp, SwApcBypass

    move    $s0, $a1                                  /* OldThread */
    ld.d    $s1, $r21, PcPrcbCurrentThread            /* NewThread */

    /* Clear CRMD.IE for the switch and remember what it was. csrxchg leaves
     * every other CRMD bit (PLV, DA, PG, DATF, DATM, WE) alone, [V1-7.4.1]. */
    li.d    $t0, 0
    li.d    $t1, 0x4
    csrxchg $t0, $t1, LA64_CSR_CRMD
    andi    $s2, $t0, 0x4

    /* An incoming stack must be fully detached from its previous processor
     * before it is loaded (the outgoing thread's publisher may still be
     * writing its frame). Stay responsive to IPIs while waiting. */
.Lwait_for_stack:
    ld.bu   $t2, $s1, ThRunning
    beqz    $t2, .Lstack_ready
    beqz    $s2, .Lwait_for_stack
    li.d    $t2, 0x4
    csrxchg $t2, $t2, LA64_CSR_CRMD
    nop
    li.d    $t2, 0
    csrxchg $t2, $t2, LA64_CSR_CRMD
    b       .Lwait_for_stack

.Lstack_ready:
    /* Order the frame stores before another processor can observe
     * ThKernelStack, then publish and load. */
    dbar    0
    st.d    $sp, $s0, ThKernelStack
    ld.d    $sp, $s1, ThKernelStack
    ld.d    $tp, $s1, ThTeb

    /* Now on the incoming stack: carry the caller's interrupt state over. */
    beqz    $s2, .Lswitch_resume
    li.d    $t0, 0x4
    csrxchg $t0, $t0, LA64_CSR_CRMD
.Lswitch_resume:
    ld.bu     $a0, $sp, SwApcBypass
    move    $a1, $s0
    move    $a2, $s1
    bl      KiSwapContextResume

    ld.d    $s0, $sp, SwS0
    ld.d    $s1, $sp, SwS1
    ld.d    $s2, $sp, SwS2
    ld.d    $s3, $sp, SwS3
    ld.d    $s4, $sp, SwS4
    ld.d    $s5, $sp, SwS5
    ld.d    $s6, $sp, SwS6
    ld.d    $s7, $sp, SwS7
    ld.d    $s8, $sp, SwS8
    ld.d    $fp, $sp, SwFp
    ld.d    $ra, $sp, SwRa
    addi.d  $sp, $sp, SwitchFrameLength
    jr      $ra
    .size   KiSwapContext, . - KiSwapContext

    .section .text$KiThreadStartup, "ax", @progbits
    .balign 4
    .global KiThreadStartup
    .type   KiThreadStartup, @function
/* First instruction of every thread. KiSwapContext has popped the
 * KSWITCH_FRAME, so $sp addresses the KSTART_FRAME. Lower to APC_LEVEL and
 * call SystemRoutine(StartRoutine, StartContext); PspSystemThreadStartup
 * lowers to PASSIVE_LEVEL itself and never returns. A user startup returns
 * here after arranging the loader APC, which is why this calls
 * KiLa64StartUserThread instead of assuming the routine cannot return. */
KiThreadStartup:
    ld.d    $s1, $sp, SfSystemRoutine
    ld.d    $s2, $sp, SfStartRoutine
    ld.d    $s3, $sp, SfStartContext
    addi.d  $sp, $sp, StartFrameLength
    li.d    $a0, LA64_IRQL_APC
    bl      KfLowerIrql

    move    $t0, $s1
    move    $a0, $s2
    move    $a1, $s3
    move    $s1, $zero
    move    $s2, $zero
    move    $s3, $zero
    jirl    $ra, $t0, 0

    /* A returning system routine without a user trap frame is rejected by
     * the helper rather than executed as a kernel continuation. */
    b       KiLa64StartUserThread
    .size   KiThreadStartup, . - KiThreadStartup
