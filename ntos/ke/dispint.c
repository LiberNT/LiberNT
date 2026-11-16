/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     End of the dispatch-level software interrupt on the processors that take it as an interrupt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

BOOLEAN
KiDpcWorkPending(
    _In_ PKPRCB Prcb)
{
    return (BOOLEAN)((Prcb->DpcData[0].DpcQueueDepth != 0) ||
                     (Prcb->TimerRequest != 0) ||
                     (Prcb->DeferredReadyListHead.Next != NULL));
}

_Requires_lock_not_held_(Prcb->PrcbLock)
static
VOID
KiSwitchFromDispatchInterrupt(
    _Inout_ PKPRCB Prcb)
{
    PKTHREAD OldThread, NewThread;

    KiAcquirePrcbLock(Prcb);
    OldThread = Prcb->CurrentThread;

    /* The idle thread hands over to the best ready thread through standby */
    if ((Prcb->NextThread == NULL) && (OldThread == Prcb->IdleThread) && (Prcb->ReadySummary != 0))
    {
        NewThread = KiSelectReadyThread(0, Prcb);
        if (NewThread != NULL)
        {
            NewThread->State = Standby;
            Prcb->NextThread = NewThread;
        }
    }

    if ((Prcb->NextThread == NULL) || KiConsumeSelfNextThread(Prcb, OldThread))
    {
        KiReleasePrcbLock(Prcb);
        return;
    }

    KfRaiseIrql(SYNCH_LEVEL);

    NewThread = Prcb->NextThread;
    Prcb->NextThread = NULL;
    if (KiIsProcessorInSet(KiIdleSummary, Prcb->Number))
        KiInterlockedRemoveProcessorFromSet(KiIdleSummary, Prcb->Number);
    Prcb->CurrentThread = NewThread;
    NewThread->State = Running;
    OldThread->WaitReason = WrDispatchInt;

    /* Queues the old thread and drops the PRCB lock */
    KxQueueReadyThread(OldThread, Prcb);

    /* A thread placed here by another processor may still be leaving it */
    KiWaitForThreadSwapOut(NewThread);

    KiSwapContext(APC_LEVEL, OldThread);
}

/* After the DPCs: end the quantum, or switch to the thread picked for this processor */
_Requires_lock_not_held_(Prcb->PrcbLock)
VOID
KiFinishDispatchInterrupt(
    _Inout_ PKPRCB Prcb)
{
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    if (Prcb->QuantumEnd)
    {
        Prcb->QuantumEnd = FALSE;
        KiQuantumEnd();
    }
    else if ((Prcb->NextThread != NULL) ||
             ((Prcb->CurrentThread == Prcb->IdleThread) && (Prcb->ReadySummary != 0)))
    {
        KiSwitchFromDispatchInterrupt(Prcb);
    }
}
