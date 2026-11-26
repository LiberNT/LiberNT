/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Single steps and the address-watching registers of x86 processors, for the debugger
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../kdb.h"
#include "../kdbpriv.h"
#define NDEBUG
#include <debug.h>

/*
 * The processor steps one instruction when the trap flag is set in the flags
 * it resumes with.
 */
NTSTATUS
NTAPI
KdbpArchSetSingleStep(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN Enable)
{
    if (Enable)
        Context->EFlags |= EFLAGS_TF;
    else
        Context->EFlags &= ~EFLAGS_TF;

    return STATUS_SUCCESS;
}

ULONG
NTAPI
KdbpArchHardwareBreakPointCount(VOID)
{
    return 4;
}

/*
 * A watch register only watches once the processor holds it. The stopped
 * processor resumes without reloading its own, so it is given them now; the
 * frozen ones reload theirs from the state they saved when they stopped.
 */
/* The watch registers as the debugger set them: a stop's own context does not always hold them */
static ULONG_PTR KdbpWatchAddress[4];
static ULONG_PTR KdbpWatchControl;

static
VOID
KdbpLoadWatchRegisters(
    _Inout_ PCONTEXT Context)
{
    PKPRCB Current = KeGetCurrentPrcb();
    ULONG Processor;

    /* A stop that came through the debug trap goes back through it, and it reloads them from here */
    Context->Dr0 = KdbpWatchAddress[0];
    Context->Dr1 = KdbpWatchAddress[1];
    Context->Dr2 = KdbpWatchAddress[2];
    Context->Dr3 = KdbpWatchAddress[3];
    Context->Dr7 = KdbpWatchControl;

    __writedr(7, 0);
    __writedr(0, Context->Dr0);
    __writedr(1, Context->Dr1);
    __writedr(2, Context->Dr2);
    __writedr(3, Context->Dr3);
    __writedr(7, Context->Dr7);

    for (Processor = 0; Processor < (ULONG)(UCHAR)KeNumberProcessors; Processor++)
    {
        PKPRCB Prcb = KiProcessorBlock[Processor];

        if (Prcb == NULL)
            continue;

        Prcb->ProcessorState.SpecialRegisters.KernelDr0 = Context->Dr0;
        Prcb->ProcessorState.SpecialRegisters.KernelDr1 = Context->Dr1;
        Prcb->ProcessorState.SpecialRegisters.KernelDr2 = Context->Dr2;
        Prcb->ProcessorState.SpecialRegisters.KernelDr3 = Context->Dr3;
        Prcb->ProcessorState.SpecialRegisters.KernelDr7 = Context->Dr7;

        if (Prcb != Current)
        {
            Prcb->ProcessorState.ContextFrame.Dr0 = Context->Dr0;
            Prcb->ProcessorState.ContextFrame.Dr1 = Context->Dr1;
            Prcb->ProcessorState.ContextFrame.Dr2 = Context->Dr2;
            Prcb->ProcessorState.ContextFrame.Dr3 = Context->Dr3;
            Prcb->ProcessorState.ContextFrame.Dr7 = Context->Dr7;
        }
    }
}

/*
 * One of the four address-watching registers. The register itself holds the
 * address; what is watched for and how wide it is live in the control
 * register, two bits and two bits per breakpoint.
 */
NTSTATUS
NTAPI
KdbpArchSetHardwareBreakPoint(
    _Inout_ PCONTEXT Context,
    _In_ ULONG Index,
    _In_ ULONG_PTR Address,
    _In_ UCHAR Size,
    _In_ KDB_ACCESS_TYPE AccessType,
    _In_ BOOLEAN Enable)
{
    ULONG_PTR Control;
    ULONG_PTR ReadWrite;
    ULONG_PTR Length;

    if (Index >= 4)
        return STATUS_INVALID_PARAMETER;

    Control = KdbpWatchControl;

    /* Clearing is only ever a matter of the enable bits */
    if (!Enable)
    {
        KdbpWatchControl = Control & ~((ULONG_PTR)3 << (Index * 2));
        KdbpLoadWatchRegisters(Context);
        return STATUS_SUCCESS;
    }

    switch (AccessType)
    {
        case KdbAccessExec:
            ReadWrite = 0;
            /* An execute breakpoint watches the one byte the instruction starts at */
            Size = 1;
            break;
        case KdbAccessWrite:
            ReadWrite = 1;
            break;
        case KdbAccessRead:
        case KdbAccessReadWrite:
            ReadWrite = 3;
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }

    if (Size > sizeof(ULONG_PTR))
        return STATUS_INVALID_PARAMETER;

    switch (Size)
    {
        case 1: Length = 0; break;
        case 2: Length = 1; break;
        case 4: Length = 3; break;
        case 8: Length = 2; break;
        default: return STATUS_INVALID_PARAMETER;
    }

    /* The watched address has to be aligned to the width being watched */
    if ((Address & (ULONG_PTR)(Size - 1)) != 0)
        return STATUS_DATATYPE_MISALIGNMENT;

    KdbpWatchAddress[Index] = Address;

    Control &= ~((ULONG_PTR)0xF << (16 + (Index * 4)));
    Control |= (ReadWrite | (Length << 2)) << (16 + (Index * 4));

    /* TODO: a processor frozen while it ran user code keeps its own watch registers */
    Control |= ((ULONG_PTR)1 << (Index * 2));

    KdbpWatchControl = Control;
    KdbpLoadWatchRegisters(Context);
    return STATUS_SUCCESS;
}

/*
 * The status register says which of the four watch registers matched; only
 * one the debugger itself armed counts. A user thread's own watchpoint must
 * keep its DR6 status bits intact when handed back to exception dispatch.
 */
LONG
NTAPI
KdbpArchHardwareBreakPointHit(
    _Inout_ PCONTEXT Context)
{
    ULONG_PTR Addresses[4];
    ULONG Index;

    if (KdbpWatchControl == 0)
        return -1;

    Addresses[0] = Context->Dr0;
    Addresses[1] = Context->Dr1;
    Addresses[2] = Context->Dr2;
    Addresses[3] = Context->Dr3;

    for (Index = 0; Index < 4; Index++)
    {
        if (((KdbpWatchControl & ((ULONG_PTR)3 << (Index * 2))) != 0) &&
            ((Context->Dr6 & ((ULONG_PTR)1 << Index)) != 0) &&
            ((Context->Dr7 & ((ULONG_PTR)3 << (Index * 2))) != 0) &&
            (Addresses[Index] == KdbpWatchAddress[Index]))
        {
            Context->Dr6 &= ~0xF;
            return (LONG)Index;
        }
    }

    return -1;
}

/*
 * The engine sets the trace flag of the stopped thread and every processor's
 * DR7 from the answer to a stop: the answer says what the debugger set up, or
 * the engine would clear it on the way out.
 */
VOID
NTAPI
KdbpArchPrepareContinue(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _In_opt_ PCONTEXT Context)
{
    State->u.Continue2.ControlSet.TraceFlag = ((Context != NULL) && ((Context->EFlags & EFLAGS_TF) != 0));
    State->u.Continue2.ControlSet.Dr7 = KdbpWatchControl;
    State->u.Continue2.ControlSet.CurrentSymbolStart = 1;
    State->u.Continue2.ControlSet.CurrentSymbolEnd = 1;
}

/* EOF */
