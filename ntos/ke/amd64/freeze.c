/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Processor freeze support for x64
 * COPYRIGHT:   Copyright 2023-2024 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/*

 IpiFrozen state graph (based on Windows behavior):

    +-----------------+     Freeze request      +-----------------+
    | RUNNING         |------------------------>| TARGET_FREEZE   |
    +-----------------+<---------               +-----------------+
            |^                  | Resume                |
     Freeze || Thaw        +-----------+ Thaw request   | Freeze IPI
            v|             | THAW      |<-----------\   v
    +-----------------+    +-----------+        +-----------------+
    | OWNER + ACTIVE  |         ^               | FROZEN          |
    +-----------------+         |               +-----------------+
            ^                   |                       ^
            | Kd proc switch    |                       | Kd proc switch
            v                   |                       v
    +-----------------+         |               +-----------------+
    | OWNER           |---------+               | FROZEN + ACTIVE |
    +-----------------+ Thaw request            +-----------------+

 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* NOT INCLUDES ANYMORE ******************************************************/

PKPRCB KiFreezeOwner;
static PKPRCB KiFreezeActiveSetOwner;
static volatile LONG64 KiFreezeEpoch;

typedef struct _KI_FREEZE_WAIT
{
    ULONG64 Start;
    ULONG64 Now;
    ULONG64 Frequency;
    ULONG64 Epoch;
    BOOLEAN Valid;
} KI_FREEZE_WAIT;

static VOID KiFreezeWaitForThaw(_In_ PKPRCB CurrentPrcb);

static VOID
KiNoteFreezeEpoch(VOID)
{
    InterlockedIncrement64(&KiFreezeEpoch);
}

ULONG64
KiQueryFreezeEpoch(VOID)
{
    return (ULONG64)InterlockedCompareExchange64(&KiFreezeEpoch, 0, 0);
}

BOOLEAN
KiIsFreezeActive(VOID)
{
    return InterlockedCompareExchangePointer((volatile PVOID *)&KiFreezeOwner,
                                             NULL,
                                             NULL) != NULL;
}

static BOOLEAN
KiFreezeFailureRequiresReturn(PKPRCB Prcb)
{
    if (!KiHasFreezeFailure())
        return FALSE;
    if (KiIsBugCheckEntryOwner(Prcb))
        return TRUE;
    for (;;)
        HalHaltSystem();
}

static BOOLEAN
KiFreezeWaitExpired(KI_FREEZE_WAIT *Wait)
{
    LARGE_INTEGER Counter, Frequency;
    ULONG64 Before, After;

    if (KiRelaxedTiming || KdEnteredDebugger)
    {
        Wait->Valid = FALSE;
        return FALSE;
    }

    Before = KiQueryFreezeEpoch();
    Counter = KeQueryPerformanceCounter(&Frequency);
    After = KiQueryFreezeEpoch();
    if (Before != After || Frequency.QuadPart <= 0)
    {
        Wait->Valid = FALSE;
        return FALSE;
    }

    Wait->Now = (ULONG64)Counter.QuadPart;
    Wait->Frequency = (ULONG64)Frequency.QuadPart;
    if (!Wait->Valid || Wait->Epoch != After || Wait->Now < Wait->Start)
    {
        Wait->Start = Wait->Now;
        Wait->Epoch = After;
        Wait->Valid = TRUE;
        return FALSE;
    }

    return (Wait->Now - Wait->Start) / Wait->Frequency >= 10;
}

static BOOLEAN
KiFreezeWaitFailed(PKPRCB Prcb, KI_FREEZE_WAIT *Wait)
{
    if (KiFreezeFailureRequiresReturn(Prcb))
        return TRUE;
    if (KiRelaxedTiming || KdEnteredDebugger ||
        KiQueryFreezeEpoch() != Wait->Epoch)
    {
        Wait->Valid = FALSE;
        return FALSE;
    }

    KiSetFreezeFailure();
    if (KiIsBugCheckEntryOwner(Prcb))
        return TRUE;

    KeBugCheckEx(IPI_WATCHDOG_TIMEOUT,
                 Wait->Frequency,
                 Wait->Now,
                 Wait->Start,
                 0);
}

/* FUNCTIONS *****************************************************************/

BOOLEAN
KiProcessorFreezeHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();

    /* Make sure this is a freeze request */
    if ((CurrentPrcb == NULL) ||
        (TrapFrame == NULL) ||
        (CurrentPrcb->IpiFrozen != IPI_FROZEN_STATE_TARGET_FREEZE))
    {
        /* Not a freeze request, return FALSE to signal it is unhandled */
        return FALSE;
    }

    if (KiIsBugCheckEntryOwner(CurrentPrcb))
        return TRUE;

    /* This processor is inside the KD port critical section without owning
     * the freeze (KdpPrint / KdPollBreakIn take KdpDebuggerLock without
     * freezing).  Freezing it here leaves the port lock held by a frozen
     * CPU: the freeze owner then fails its try-lock in KdEnterDebugger
     * ("Port lock was not acquired!") and both CPUs drive the transport.
     * Leave the request pending; the lock release path completes it
     * through KiFreezeIfRequested a few microseconds later. */
    if (KdpPortOwnerPrcb == CurrentPrcb)
    {
        return TRUE;
    }

    /* Claim the request, but do not publish a stable context before saving it. */
    if (InterlockedCompareExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_SAVING, IPI_FROZEN_STATE_TARGET_FREEZE) !=
        IPI_FROZEN_STATE_TARGET_FREEZE)
    {
        return FALSE;
    }

    /* Save the processor state */
    KiSaveProcessorState(TrapFrame, ExceptionFrame);
    KeMemoryBarrier();

    /* The owner may consume ProcessorState only after this publication. */
    InterlockedExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_FROZEN);

    /* Wait for the freeze owner to release us */
    KiFreezeWaitForThaw(CurrentPrcb);

    /* Restore the processor state */
    KiRestoreProcessorState(TrapFrame, ExceptionFrame);

    /* We are running again now */
    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;

    /* Return TRUE to signal that we handled the freeze */
    return TRUE;
}

/* Frozen wait shared by the NMI path and the deferred path: spin until the
 * owner thaws us, servicing a Kd processor switch while frozen. */
static VOID
KiFreezeWaitForThaw(
    _In_ PKPRCB CurrentPrcb)
{
    while (CurrentPrcb->IpiFrozen != IPI_FROZEN_STATE_THAW)
    {
        if (KiFreezeFailureRequiresReturn(CurrentPrcb))
            return;

        /* Check for Kd processor switch */
        if (CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE)
        {
            KCONTINUE_STATUS ContinueStatus;

            /* Enter the debugger */
            ContinueStatus = KdReportProcessorChange();

            /* Set the state back to frozen */
            CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_FROZEN;

            /* If the status is ContinueSuccess, we need to release the freeze owner */
            if (ContinueStatus == ContinueSuccess)
            {
                /* Release the freeze owner */
                KiFreezeOwner->IpiFrozen = IPI_FROZEN_STATE_THAW;
            }
        }

        for (ULONG Source = 0; Source < KeNumberProcessors; Source++)
        {
            MiAmd64ProcessTlbRequest(Source);
        }

        YieldProcessor();
        KeMemoryBarrier();
    }
}

/* Complete a freeze request that KiProcessorFreezeHandler deferred because
 * this processor was inside the KD port critical section.  There is no trap
 * frame here: the published context is the caller's own register state. */
VOID
NTAPI
KiFreezeIfRequested(
    VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    BOOLEAN Enable;

    if (CurrentPrcb->IpiFrozen != IPI_FROZEN_STATE_TARGET_FREEZE)
    {
        return;
    }

    if (KiIsBugCheckEntryOwner(CurrentPrcb))
        return;

    /* Same rule as the NMI path: never freeze while holding the KD port.
     * The port release path calls us again once the lock is dropped. */
    if (KdpPortOwnerPrcb == CurrentPrcb)
    {
        return;
    }

    Enable = KeDisableInterrupts();
    if (InterlockedCompareExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_SAVING, IPI_FROZEN_STATE_TARGET_FREEZE) ==
        IPI_FROZEN_STATE_TARGET_FREEZE)
    {
        RtlCaptureContext(&CurrentPrcb->ProcessorState.ContextFrame);
        KiSaveProcessorControlState(&CurrentPrcb->ProcessorState);
        CurrentPrcb->ProcessorState.ContextFrame.Dr0 = CurrentPrcb->ProcessorState.SpecialRegisters.KernelDr0;
        CurrentPrcb->ProcessorState.ContextFrame.Dr1 = CurrentPrcb->ProcessorState.SpecialRegisters.KernelDr1;
        CurrentPrcb->ProcessorState.ContextFrame.Dr2 = CurrentPrcb->ProcessorState.SpecialRegisters.KernelDr2;
        CurrentPrcb->ProcessorState.ContextFrame.Dr3 = CurrentPrcb->ProcessorState.SpecialRegisters.KernelDr3;
        CurrentPrcb->ProcessorState.ContextFrame.Dr6 = CurrentPrcb->ProcessorState.SpecialRegisters.KernelDr6;
        CurrentPrcb->ProcessorState.ContextFrame.Dr7 = CurrentPrcb->ProcessorState.SpecialRegisters.KernelDr7;
        KeMemoryBarrier();

        /* The owner may consume ProcessorState only after this publication. */
        InterlockedExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_FROZEN);

        KiFreezeWaitForThaw(CurrentPrcb);

        /* We are running again now */
        CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;
    }
    KeRestoreInterrupts(Enable);
}

static
BOOLEAN
KiAcquireFreezeActiveSet(
    _In_ PKPRCB Prcb)
{
    KI_FREEZE_WAIT Wait = {0};

    ASSERT(Prcb != NULL);
    ASSERT(Prcb == KeGetCurrentPrcb());

    if (KiFreezeFailureRequiresReturn(Prcb))
        return FALSE;

    if (InterlockedCompareExchangePointer((volatile PVOID *)&KiFreezeActiveSetOwner,
                                          NULL,
                                          NULL) == Prcb)
        return FALSE;

    while (InterlockedCompareExchangePointer((volatile PVOID *)&KiFreezeActiveSetOwner,
                                             Prcb,
                                             NULL) != NULL)
    {
        if (KiFreezeFailureRequiresReturn(Prcb))
            return FALSE;
        if (KiFreezeWaitExpired(&Wait) &&
            InterlockedCompareExchangePointer((volatile PVOID *)&KiFreezeActiveSetOwner,
                                              NULL,
                                              NULL) != NULL &&
            KiFreezeWaitFailed(Prcb, &Wait))
            return FALSE;
        YieldProcessor();
    }

    return TRUE;
}

static
VOID
KiReleaseFreezeActiveSet(
    _In_ PKPRCB Prcb,
    _In_ BOOLEAN Acquired)
{
    ASSERT(Prcb == KeGetCurrentPrcb());
    ASSERT(KiFreezeActiveSetOwner == Prcb);

    if (Acquired)
        InterlockedExchangePointer((volatile PVOID *)&KiFreezeActiveSetOwner, NULL);
}

CODE_SEG("INIT")
VOID
NTAPI
KiPublishActiveProcessor(
    _In_ PKPRCB Prcb)
{
    BOOLEAN Acquired;

    ASSERT(Prcb != NULL);
    ASSERT(Prcb == KeGetCurrentPrcb());

    for (;;)
    {
        Acquired = KiAcquireFreezeActiveSet(Prcb);
        if (KiHasFreezeFailure())
        {
            for (;;)
                HalHaltSystem();
        }
        ASSERT(Acquired);

        if (InterlockedCompareExchangePointer((volatile PVOID *)&KiFreezeOwner,
                                              NULL,
                                              NULL) == NULL)
        {
            KeActiveProcessors |= Prcb->SetMember;
            KiReleaseFreezeActiveSet(Prcb, Acquired);
            break;
        }

        KiReleaseFreezeActiveSet(Prcb, Acquired);
        YieldProcessor();
    }
}

/* Mark, send to and wait for one snapshot of the published, active processors */
static
KAFFINITY
KiFreezeTargetSet(
    _In_ PKPRCB CurrentPrcb)
{
    KAFFINITY Targets = 0;
    ULONG Index;
    PKPRCB TargetPrcb;

    for (Index = 0; Index < KeNumberProcessors && Index < MAXIMUM_PROCESSORS; Index++)
    {
        TargetPrcb = KiProcessorBlock[Index];
        if (TargetPrcb == NULL || TargetPrcb == CurrentPrcb)
            continue;

        if ((KeActiveProcessors & TargetPrcb->SetMember) == 0)
            continue;

        Targets |= TargetPrcb->SetMember;
    }

    return Targets;
}

VOID
NTAPI
KxFreezeExecution(
    VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    BOOLEAN ReacquireActiveSet = FALSE;
    KI_FREEZE_WAIT Wait = {0};

    if (KiFreezeFailureRequiresReturn(CurrentPrcb))
        return;

    /* Avoid blocking on recursive debug action */
    if (KiFreezeOwner == CurrentPrcb)
    {
        return;
    }

    /* Try to acquire the freeze owner */
    while (InterlockedCompareExchangePointer((volatile PVOID *)&KiFreezeOwner, CurrentPrcb, NULL))
    {
        if (!ReacquireActiveSet &&
            InterlockedCompareExchangePointer((volatile PVOID *)&KiFreezeActiveSetOwner,
                                              NULL,
                                              NULL) == CurrentPrcb)
        {
            InterlockedExchangePointer((volatile PVOID *)&KiFreezeActiveSetOwner, NULL);
            ReacquireActiveSet = TRUE;
        }

        /* Someone else was faster. We expect an NMI to freeze any time.
           Spin here until the freeze owner is available, completing the
           freeze ourselves if that NMI never arrives. */
        while (KiFreezeOwner != NULL)
        {
            if (KiFreezeFailureRequiresReturn(CurrentPrcb))
                return;
            if (KiFreezeWaitExpired(&Wait) &&
                InterlockedCompareExchangePointer((volatile PVOID *)&KiFreezeOwner,
                                                  NULL,
                                                  NULL) != NULL &&
                KiFreezeWaitFailed(CurrentPrcb, &Wait))
                return;
            KiFreezeIfRequested();
            YieldProcessor();
            KeMemoryBarrier();
        }
    }

    KiNoteFreezeEpoch();

    /* We are the owner now and active */
    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE;

    {
    BOOLEAN Acquired = KiAcquireFreezeActiveSet(CurrentPrcb);
    KAFFINITY Targets;

    if (KiFreezeFailureRequiresReturn(CurrentPrcb))
        return;
    Targets = KiFreezeTargetSet(CurrentPrcb);
    Wait.Valid = FALSE;

    KiReleaseFreezeActiveSet(CurrentPrcb, Acquired && !ReacquireActiveSet);

    /* Mark exactly the snapshot */
    for (ULONG i = 0; i < KeNumberProcessors && i < MAXIMUM_PROCESSORS; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if (KiFreezeFailureRequiresReturn(CurrentPrcb))
            return;
        if (TargetPrcb == NULL || (Targets & TargetPrcb->SetMember) == 0)
            continue;

        /* Only the active processor is allowed to change IpiFrozen */
        ASSERT(TargetPrcb->IpiFrozen == IPI_FROZEN_STATE_RUNNING);

        RtlZeroMemory(&TargetPrcb->ProcessorState.ContextFrame, sizeof(TargetPrcb->ProcessorState.ContextFrame));
        KeMemoryBarrier();

        /* Request target to freeze */
        TargetPrcb->IpiFrozen = IPI_FROZEN_STATE_TARGET_FREEZE;
    }

    if (KiFreezeFailureRequiresReturn(CurrentPrcb))
        return;

    /* Send the freeze IPI to exactly the same set */
    if (Targets != 0)
        KiIpiSend(Targets, IPI_FREEZE);

    for (ULONG i = 0; i < KeNumberProcessors && i < MAXIMUM_PROCESSORS; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];

        if (TargetPrcb == NULL || (Targets & TargetPrcb->SetMember) == 0)
            continue;

        while (TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_FROZEN)
        {
            if (KiFreezeFailureRequiresReturn(CurrentPrcb))
                return;
            if (KiFreezeWaitExpired(&Wait) &&
                TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_FROZEN &&
                KiFreezeWaitFailed(CurrentPrcb, &Wait))
                return;
            YieldProcessor();
            KeMemoryBarrier();
        }
    }
    }
    if (KiFreezeFailureRequiresReturn(CurrentPrcb))
        return;
}

VOID
NTAPI
KxThawExecution(
    VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    KI_FREEZE_WAIT Wait = {0};

    if (KiFreezeFailureRequiresReturn(CurrentPrcb))
        return;
    ASSERT(CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE);

    /* Thaw the marked set, not whatever KeNumberProcessors says now */
    for (ULONG i = 0; i < KeNumberProcessors && i < MAXIMUM_PROCESSORS; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if (KiFreezeFailureRequiresReturn(CurrentPrcb))
            return;
        if (TargetPrcb == NULL || TargetPrcb == CurrentPrcb)
            continue;

        if (TargetPrcb->IpiFrozen == IPI_FROZEN_STATE_RUNNING)
            continue;

        /* Make sure they are still frozen */
        ASSERT(TargetPrcb->IpiFrozen == IPI_FROZEN_STATE_FROZEN);

        /* Request target to thaw */
        TargetPrcb->IpiFrozen = IPI_FROZEN_STATE_THAW;
    }

    for (ULONG i = 0; i < KeNumberProcessors && i < MAXIMUM_PROCESSORS; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];

        if (TargetPrcb == NULL || TargetPrcb == CurrentPrcb)
            continue;

        while (TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_RUNNING)
        {
            if (KiFreezeFailureRequiresReturn(CurrentPrcb))
                return;
            if (KiFreezeWaitExpired(&Wait) &&
                TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_RUNNING &&
                KiFreezeWaitFailed(CurrentPrcb, &Wait))
                return;
            YieldProcessor();
            KeMemoryBarrier();
        }
    }

    if (KiFreezeFailureRequiresReturn(CurrentPrcb))
        return;

    /* We are running again now */
    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;

    /* Release the freeze owner */
    InterlockedExchangePointer((volatile PVOID *)&KiFreezeOwner, NULL);
}

KCONTINUE_STATUS
NTAPI
KxSwitchKdProcessor(
    _In_ ULONG ProcessorIndex)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    PKPRCB TargetPrcb;

    if (KiFreezeFailureRequiresReturn(CurrentPrcb))
        return ContinueSuccess;

    /* Make sure that the processor index is valid */
    ASSERT(ProcessorIndex < KeNumberProcessors);

    /* We are no longer active */
    ASSERT(CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE);
    CurrentPrcb->IpiFrozen &= ~IPI_FROZEN_FLAG_ACTIVE;

    /* Inform the target processor that it's his turn now */
    TargetPrcb = KiProcessorBlock[ProcessorIndex];
    TargetPrcb->IpiFrozen |= IPI_FROZEN_FLAG_ACTIVE;

    /* If we are not the freeze owner, we return back to the freeze loop */
    if (KiFreezeOwner != CurrentPrcb)
    {
        return ContinueNextProcessor;
    }

    /* Loop until it's our turn again */
    while (CurrentPrcb->IpiFrozen == IPI_FROZEN_STATE_OWNER)
    {
        if (KiFreezeFailureRequiresReturn(CurrentPrcb))
            return ContinueSuccess;
        YieldProcessor();
        KeMemoryBarrier();
    }

    /* Check if we have been thawed */
    if (CurrentPrcb->IpiFrozen == IPI_FROZEN_STATE_THAW)
    {
        /* Another CPU has completed, we can leave the debugger now */
        KdpDprintf("[%u] KxSwitchKdProcessor: ContinueSuccess\n", KeGetCurrentProcessorNumber());
        CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE;
        return ContinueSuccess;
    }

    /* We have been reselected, return to Kd to continue in the debugger */
    ASSERT(CurrentPrcb->IpiFrozen == (IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE));

    return ContinueProcessorReselected;
}
