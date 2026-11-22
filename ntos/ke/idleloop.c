/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Idle loop of the processors that halt between interrupts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

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

    if (KiIsProcessorInSet(KiIdleSummary, Prcb->Number))
        KiInterlockedRemoveProcessorFromSet(KiIdleSummary, Prcb->Number);

    Prcb->CurrentThread = NewThread;
    NewThread->State = Running;
    KiReleasePrcbLock(Prcb);

    /* The processor the thread is leaving may be held in a rendezvous that waits for this one */
    _enable();
    KiWaitForThreadSwapOut(NewThread);

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
    if (!KiIsProcessorInSet(KiIdleSummary, Prcb->Number))
        KiInterlockedAddProcessorToSet(KiIdleSummary, Prcb->Number);

    /* Advertise sleeping before the last look, so a waker either sees it or we see its work */
    Prcb->Sleeping = TRUE;
    KeMemoryBarrier();

    if (KiDpcWorkPending(Prcb) || KiIsDpcInterruptRequested(Prcb))
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

_Requires_lock_not_held_(Prcb->PrcbLock)
_Acquires_lock_(Prcb->PrcbLock)
static
VOID
KiIdleAcquirePrcbLock(
    _Inout_ PKPRCB Prcb)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    /* The holder may be held in a rendezvous that waits for this processor, so the wait takes interrupts */
    while (InterlockedExchange((PLONG)&Prcb->PrcbLock, 1) != 0)
    {
        _enable();
        while (ReadNoFence((PLONG)&Prcb->PrcbLock) != 0)
            YieldProcessor();
        _disable();
    }
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

        if (KiDpcWorkPending(Prcb))
        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            KiRetireDpcList(Prcb);
        }

        _disable();
        KiIdleAcquirePrcbLock(Prcb);

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
