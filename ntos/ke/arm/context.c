/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 conversion between CONTEXT records and trap records
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * A trap frame holds the volatile registers, R11 and the control state; the
 * exception frame below it holds R4-R10 and the VFP area below that the
 * floating-point state. Every trap saves all three, so a CONTEXT maps onto
 * one record in full.
 */

static
PKEXCEPTION_FRAME
KiArmExceptionFrame(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    return ExceptionFrame ? ExceptionFrame : &KI_ARM_TRAP_RECORD(TrapFrame)->ExceptionFrame;
}

VOID
NTAPI
KeContextToTrapFrame(
    _In_ PCONTEXT Context,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PKARM_VFP_STATE VfpState = TrapFrame->VfpState;
    KIRQL OldIrql = KeGetCurrentIrql();

    if (OldIrql < APC_LEVEL)
        KeRaiseIrql(APC_LEVEL, &OldIrql);

    ExceptionFrame = KiArmExceptionFrame(TrapFrame, ExceptionFrame);

    if ((ContextFlags & CONTEXT_CONTROL) == CONTEXT_CONTROL)
    {
        TrapFrame->Sp = Context->Sp;
        TrapFrame->Lr = Context->Lr;
        TrapFrame->Pc = Context->Pc;
        if (PreviousMode == UserMode)
        {
            /* A user context may only choose flags, IT state and the instruction set. */
            TrapFrame->Cpsr = (Context->Cpsr & CPSR_USER_SETTABLE) | CPSR_MODE_USER;
        }
        else
        {
            TrapFrame->Cpsr = (TrapFrame->Cpsr & ~CPSR_USER_SETTABLE) | (Context->Cpsr & CPSR_USER_SETTABLE);
        }
        if (TrapFrame->Cpsr & CPSR_THUMB)
            TrapFrame->Pc &= ~1UL;
    }

    if ((ContextFlags & CONTEXT_INTEGER) == CONTEXT_INTEGER)
    {
        TrapFrame->R0 = Context->R0;
        TrapFrame->R1 = Context->R1;
        TrapFrame->R2 = Context->R2;
        TrapFrame->R3 = Context->R3;
        TrapFrame->R12 = Context->R12;
        TrapFrame->R11 = Context->R11;
        ExceptionFrame->R4 = Context->R4;
        ExceptionFrame->R5 = Context->R5;
        ExceptionFrame->R6 = Context->R6;
        ExceptionFrame->R7 = Context->R7;
        ExceptionFrame->R8 = Context->R8;
        ExceptionFrame->R9 = Context->R9;
        ExceptionFrame->R10 = Context->R10;
        ExceptionFrame->R11 = Context->R11;
    }

    if (((ContextFlags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT) && VfpState)
    {
        VfpState->Fpscr = Context->Fpscr;
        RtlCopyMemory(VfpState->VfpD, Context->D, sizeof(VfpState->VfpD));
    }

    if ((ContextFlags & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS)
    {
        RtlCopyMemory(TrapFrame->Bvr, Context->Bvr, sizeof(TrapFrame->Bvr));
        RtlCopyMemory(TrapFrame->Bcr, Context->Bcr, sizeof(TrapFrame->Bcr));
        TrapFrame->Wvr[0] = Context->Wvr[0];
        TrapFrame->Wcr[0] = Context->Wcr[0];
        /* No hardware debug registers are programmed yet, so none count as valid. */
        TrapFrame->DebugRegistersValid = FALSE;
    }

    if (OldIrql < APC_LEVEL)
        KeLowerIrql(OldIrql);
}

VOID
NTAPI
KeTrapFrameToContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PCONTEXT Context)
{
    PKARM_VFP_STATE VfpState = TrapFrame->VfpState;
    KIRQL OldIrql = KeGetCurrentIrql();

    if (OldIrql < APC_LEVEL)
        KeRaiseIrql(APC_LEVEL, &OldIrql);

    ExceptionFrame = KiArmExceptionFrame(TrapFrame, ExceptionFrame);

    if ((Context->ContextFlags & CONTEXT_CONTROL) == CONTEXT_CONTROL)
    {
        Context->Sp = TrapFrame->Sp;
        Context->Lr = TrapFrame->Lr;
        Context->Pc = TrapFrame->Pc;
        Context->Cpsr = TrapFrame->Cpsr;
    }

    if ((Context->ContextFlags & CONTEXT_INTEGER) == CONTEXT_INTEGER)
    {
        Context->R0 = TrapFrame->R0;
        Context->R1 = TrapFrame->R1;
        Context->R2 = TrapFrame->R2;
        Context->R3 = TrapFrame->R3;
        Context->R12 = TrapFrame->R12;
        Context->R11 = TrapFrame->R11;
        Context->R4 = ExceptionFrame->R4;
        Context->R5 = ExceptionFrame->R5;
        Context->R6 = ExceptionFrame->R6;
        Context->R7 = ExceptionFrame->R7;
        Context->R8 = ExceptionFrame->R8;
        Context->R9 = ExceptionFrame->R9;
        Context->R10 = ExceptionFrame->R10;
    }

    if ((Context->ContextFlags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT)
    {
        if (VfpState)
        {
            Context->Fpscr = VfpState->Fpscr;
            RtlCopyMemory(Context->D, VfpState->VfpD, sizeof(Context->D));
        }
        else
        {
            Context->Fpscr = 0;
            RtlZeroMemory(Context->D, sizeof(Context->D));
        }
    }

    if ((Context->ContextFlags & CONTEXT_DEBUG_REGISTERS) == CONTEXT_DEBUG_REGISTERS)
    {
        RtlCopyMemory(Context->Bvr, TrapFrame->Bvr, sizeof(Context->Bvr));
        RtlCopyMemory(Context->Bcr, TrapFrame->Bcr, sizeof(Context->Bcr));
        Context->Wvr[0] = TrapFrame->Wvr[0];
        Context->Wcr[0] = TrapFrame->Wcr[0];
    }

    if (OldIrql < APC_LEVEL)
        KeLowerIrql(OldIrql);
}
