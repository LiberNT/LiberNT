/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 kernel stack switch, idle loop, address space switch and 64-bit stubs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

SIZE_T KeXStateLength = sizeof(XSAVE_FORMAT);

static
BOOLEAN
KiFrameOnOldStack(
    _In_ ULONG_PTR Frame,
    _In_ ULONG_PTR OldLimit,
    _In_ PVOID OldBase)
{
    return (BOOLEAN)((Frame >= OldLimit) && (Frame <= (ULONG_PTR)OldBase - sizeof(KTRAP_FRAME)));
}

static
VOID
KiRelocateTrapFrames(
    _Inout_ PKTHREAD Thread,
    _In_ ULONG_PTR OldLimit,
    _In_ PVOID OldBase,
    _In_ LONG_PTR Delta,
    _In_ SIZE_T Used)
{
    PKTRAP_FRAME Frame = Thread->TrapFrame;
    SIZE_T Budget = Used / sizeof(KTRAP_FRAME);

    if (Frame == NULL)
        return;

    ASSERT(KiFrameOnOldStack((ULONG_PTR)Frame, OldLimit, OldBase));

    Frame = (PKTRAP_FRAME)Add2Ptr(Frame, Delta);
    Thread->TrapFrame = Frame;

    /* Follow the chain of frames on the copied stack; the budget bounds a corrupt chain */
    while (Budget-- != 0)
    {
        ULONG_PTR Link = Frame->TrapFrame;

        if ((Link == 0) || !KiFrameOnOldStack(Link, OldLimit, OldBase))
            break;

        Frame->TrapFrame = (ULONG_PTR)Add2Ptr((PVOID)Link, Delta);
        Frame = (PKTRAP_FRAME)Frame->TrapFrame;
    }
}

PVOID
NTAPI
KiSwitchKernelStack(
    _In_ PVOID StackBase,
    _In_ PVOID StackLimit)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PVOID OldBase = Thread->StackBase;
    ULONG_PTR OldLimit = Thread->StackLimit;
    SIZE_T Used = (ULONG_PTR)OldBase - OldLimit;
    LONG_PTR Delta = (PUCHAR)StackBase - (PUCHAR)OldBase;
    PKIPCR Pcr;

    ASSERT(Used <= (ULONG_PTR)StackBase - (ULONG_PTR)StackLimit);

    RtlCopyMemory((PUCHAR)StackBase - Used, (PVOID)OldLimit, Used);
    KiRelocateTrapFrames(Thread, OldLimit, OldBase, Delta, Used);

    Thread->InitialStack = Add2Ptr(Thread->InitialStack, Delta);
    Thread->StateSaveArea = Add2Ptr(Thread->StateSaveArea, Delta);
    Thread->StackBase = StackBase;
    Thread->StackLimit = (ULONG_PTR)StackLimit;
    Thread->LargeStack = TRUE;

    /* Interrupts and system calls enter on the stack these two name */
    Pcr = (PKIPCR)KeGetPcr();
    Pcr->Prcb.RspBase += Delta;
    Pcr->TssBase->Rsp0 += Delta;

    return OldBase;
}

static
BOOLEAN
KiIdleHasWork(
    _In_ PKPRCB Prcb)
{
    return (BOOLEAN)((Prcb->DpcData[0].DpcQueueDepth != 0) ||
                     (Prcb->TimerRequest != 0) ||
                     (Prcb->DeferredReadyListHead.Next != NULL));
}

_Requires_lock_held_(Prcb->PrcbLock)
static
PKTHREAD
KiIdleTakeNextThread(
    _Inout_ PKPRCB Prcb)
{
    PKTHREAD Next = Prcb->NextThread;

    if (Next != NULL)
    {
        Prcb->NextThread = NULL;
        return Next;
    }

    Next = KiIdleSchedule(Prcb);
    if (Next != Prcb->IdleThread)
        return Next;

    /* Scheduling may have placed a thread in NextThread instead of returning it */
    Next = Prcb->NextThread;
    if (Next != NULL)
        Prcb->NextThread = NULL;
    return Next;
}

_Requires_lock_held_(Prcb->PrcbLock)
_Releases_lock_(Prcb->PrcbLock)
static
VOID
KiIdleRunThread(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD NewThread)
{
    PKTHREAD OldThread = Prcb->CurrentThread;

    KfRaiseIrql(SYNCH_LEVEL);

    if (KiIdleSummary & Prcb->SetMember)
        InterlockedBitTestAndResetAffinity(&KiIdleSummary, Prcb->Number);

    Prcb->CurrentThread = NewThread;
    NewThread->State = Running;
    KiReleasePrcbLock(Prcb);

    KiWaitForThreadSwapOut(NewThread);

    _enable();
    KiSwapContext(APC_LEVEL, OldThread);
    KeLowerIrql(DISPATCH_LEVEL);
}

_Requires_lock_held_(Prcb->PrcbLock)
_Releases_lock_(Prcb->PrcbLock)
static
VOID
KiIdleSleep(
    _Inout_ PKPRCB Prcb)
{
    if (!(KiIdleSummary & Prcb->SetMember))
        InterlockedBitTestAndSetAffinity(&KiIdleSummary, Prcb->Number);

    /* Advertise sleeping before the last look, so a waker either sees it or we see its work */
    Prcb->Sleeping = TRUE;
    KeMemoryBarrier();

    if (KiIdleHasWork(Prcb) || (Prcb->DpcInterruptRequested != FALSE))
    {
        Prcb->Sleeping = FALSE;
        KiReleasePrcbLock(Prcb);
        return;
    }

    /* The HAL idle function executes STI;HLT as one step */
    KiReleasePrcbLock(Prcb);
    Prcb->PowerState.IdleFunction(&Prcb->PowerState);
    Prcb->Sleeping = FALSE;
}

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD NewThread;

    for (;;)
    {
        /* Open a short interrupt window before looking for work */
        _enable();
        YieldProcessor();
        YieldProcessor();
        _disable();

        if (KiIdleHasWork(Prcb))
        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            KiRetireDpcList(Prcb);
        }

        _disable();
        KiAcquirePrcbLock(Prcb);

        NewThread = KiIdleTakeNextThread(Prcb);

        if (NewThread == Prcb->IdleThread)
        {
            /* An affinity change can pick this idle thread again while it already runs */
            ASSERT(Prcb->CurrentThread == NewThread);
            NewThread->State = Running;
            KiReleasePrcbLock(Prcb);
        }
        else if (NewThread != NULL)
        {
            KiIdleRunThread(Prcb, NewThread);
        }
        else
        {
            KiIdleSleep(Prcb);
        }
    }
}

VOID
NTAPI
KiSwapProcess(
    _In_ PKPROCESS NewProcess,
    _In_ PKPROCESS OldProcess)
{
    KAFFINITY Self = ((PKIPCR)KeGetPcr())->Prcb.SetMember;
    BOOLEAN Changed = (BOOLEAN)(NewProcess != OldProcess);

    /* Join the new process before loading its tables, leave the old one only after */
    if (Changed)
        InterlockedOr64((PLONG64)&NewProcess->ActiveProcessors, Self);

    __writecr3(NewProcess->DirectoryTableBase);

    if (Changed)
        InterlockedAnd64((PLONG64)&OldProcess->ActiveProcessors, ~Self);
}

NTSTATUS
NTAPI
NtSetLdtEntries(
    _In_ ULONG Selector1,
    _In_ LDT_ENTRY LdtEntry1,
    _In_ ULONG Selector2,
    _In_ LDT_ENTRY LdtEntry2)
{
    UNREFERENCED_PARAMETER(Selector1);
    UNREFERENCED_PARAMETER(LdtEntry1);
    UNREFERENCED_PARAMETER(Selector2);
    UNREFERENCED_PARAMETER(LdtEntry2);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtVdmControl(
    _In_ ULONG ControlCode,
    _In_ PVOID ControlData)
{
    UNREFERENCED_PARAMETER(ControlCode);
    UNREFERENCED_PARAMETER(ControlData);
    return STATUS_NOT_IMPLEMENTED;
}
