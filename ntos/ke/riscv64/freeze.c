/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later),
 *              with portions under MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     riscv64 processor freeze and thaw for the kernel debugger
 * COPYRIGHT:   Copyright 2023-2024 Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KI_BUGCHECK_FREEZE_WAIT_US 1000000

PKPRCB KiFreezeOwner;
static KAFFINITY KiFreezeTargetSet;

static VOID KiFreezeWaitForThaw(_In_ PKPRCB CurrentPrcb);

BOOLEAN
KiProcessorFreezeHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();

    if ((CurrentPrcb == NULL) ||
        (TrapFrame == NULL) ||
        (CurrentPrcb->IpiFrozen != IPI_FROZEN_STATE_TARGET_FREEZE))
    {
        return FALSE;
    }

    /* This processor is inside the KD port critical section without owning
     * the freeze (KdpPrint / KdPollBreakIn take KdpDebuggerLock without
     * freezing).  Freezing it here leaves the port lock held by a frozen
     * CPU: the freeze owner then fails its try-lock in KdEnterDebugger
     * ("Port lock was not acquired!") and both CPUs drive the transport.
     * Leave the request pending; the lock release path completes it
     * through KiFreezeIfRequested a few microseconds later. */
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

    KiSaveProcessorState(TrapFrame, ExceptionFrame);
    KeMemoryBarrier();

    /* The owner may consume ProcessorState only after this publication. */
    InterlockedExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_FROZEN);

    KiFreezeWaitForThaw(CurrentPrcb);

    KiRestoreProcessorState(TrapFrame, ExceptionFrame);

    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;

    return TRUE;
}

/* Frozen wait shared by the IPI path and the deferred path: spin until the
 * owner thaws us, servicing a Kd processor switch while frozen. */
static VOID
KiFreezeWaitForThaw(
    _In_ PKPRCB CurrentPrcb)
{
    while (CurrentPrcb->IpiFrozen != IPI_FROZEN_STATE_THAW)
    {
        if (CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE)
        {
            KCONTINUE_STATUS ContinueStatus;

            /* Enter the debugger */
            ContinueStatus = KdReportProcessorChange();

            CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_FROZEN;

            if (ContinueStatus == ContinueSuccess)
            {
                KiFreezeOwner->IpiFrozen = IPI_FROZEN_STATE_THAW;
            }
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
KiFreezeIfRequested(VOID)
{
    BOOLEAN Enable = KeDisableInterrupts();
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();

    if (CurrentPrcb->IpiFrozen != IPI_FROZEN_STATE_TARGET_FREEZE)
    {
        KeRestoreInterrupts(Enable);
        return;
    }

    if (InterlockedCompareExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_SAVING, IPI_FROZEN_STATE_TARGET_FREEZE) ==
        IPI_FROZEN_STATE_TARGET_FREEZE)
    {
        RtlCaptureContext(&CurrentPrcb->ProcessorState.ContextFrame);
        KiSaveProcessorControlState(&CurrentPrcb->ProcessorState);
        KeMemoryBarrier();

        /* The owner may consume ProcessorState only after this publication. */
        InterlockedExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_FROZEN);

        KiFreezeWaitForThaw(CurrentPrcb);

        CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;
    }
    KeRestoreInterrupts(Enable);
}

VOID
NTAPI
KxFreezeExecution(VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    ULONG Stalls = 0;

    /* Avoid blocking on recursive debug action */
    if (KiFreezeOwner == CurrentPrcb)
    {
        return;
    }

    while (InterlockedCompareExchangePointer((volatile PVOID *)&KiFreezeOwner, CurrentPrcb, NULL))
    {
        /* Someone else was faster. Supervisor IPIs are maskable and this
           entrant already masked them, so answer the owner's freeze request
           from here with the caller's own register state. */
        while (KiFreezeOwner != NULL)
        {
            KTRAP_FRAME Frame = {0};

            RtlCaptureContext(&Frame.Context);
            KiProcessorFreezeHandler(&Frame, NULL);
            YieldProcessor();
            KeMemoryBarrier();
        }
    }

    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE;

    KiFreezeTargetSet = 0;
    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if (TargetPrcb != CurrentPrcb)
        {
            if (KeBugCheckActive && (TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_RUNNING))
                continue;

            ASSERT(TargetPrcb->IpiFrozen == IPI_FROZEN_STATE_RUNNING);

            RtlZeroMemory(&TargetPrcb->ProcessorState.ContextFrame, sizeof(TargetPrcb->ProcessorState.ContextFrame));
            KeMemoryBarrier();

            TargetPrcb->IpiFrozen = IPI_FROZEN_STATE_TARGET_FREEZE;
            KiFreezeTargetSet |= TargetPrcb->SetMember;
        }
    }

    KiIpiSend(KeActiveProcessors & ~CurrentPrcb->SetMember, IPI_FREEZE);

    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if ((TargetPrcb != CurrentPrcb) && (KiFreezeTargetSet & TargetPrcb->SetMember))
        {
            while (TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_FROZEN)
            {
                if (KeBugCheckActive)
                {
                    if (Stalls++ >= KI_BUGCHECK_FREEZE_WAIT_US)
                    {
                        KiFreezeTargetSet &= ~TargetPrcb->SetMember;
                        break;
                    }
                    KeStallExecutionProcessor(1);
                }
                YieldProcessor();
                KeMemoryBarrier();
            }
        }
    }

}

VOID
NTAPI
KxThawExecution(VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    ASSERT(CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE);

    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if ((TargetPrcb != CurrentPrcb) && (KiFreezeTargetSet & TargetPrcb->SetMember))
        {
            ASSERT(TargetPrcb->IpiFrozen == IPI_FROZEN_STATE_FROZEN);

            TargetPrcb->IpiFrozen = IPI_FROZEN_STATE_THAW;
        }
    }

    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if ((TargetPrcb != CurrentPrcb) && (KiFreezeTargetSet & TargetPrcb->SetMember))
        {
            while (TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_RUNNING)
            {
                YieldProcessor();
                KeMemoryBarrier();
            }
        }
    }

    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;

    InterlockedExchangePointer((volatile PVOID *)&KiFreezeOwner, NULL);
}

BOOLEAN
NTAPI
KxIsFreezeOwner(
    _In_ PKPRCB Prcb)
{
    return KiFreezeOwner == Prcb;
}

KCONTINUE_STATUS
NTAPI
KxSwitchKdProcessor(
    _In_ ULONG ProcessorIndex)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    PKPRCB TargetPrcb;

    ASSERT(ProcessorIndex < KeNumberProcessors);

    ASSERT(CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE);
    CurrentPrcb->IpiFrozen &= ~IPI_FROZEN_FLAG_ACTIVE;

    TargetPrcb = KiProcessorBlock[ProcessorIndex];
    TargetPrcb->IpiFrozen |= IPI_FROZEN_FLAG_ACTIVE;

    if (KiFreezeOwner != CurrentPrcb)
    {
        return ContinueNextProcessor;
    }

    while (CurrentPrcb->IpiFrozen == IPI_FROZEN_STATE_OWNER)
    {
        YieldProcessor();
        KeMemoryBarrier();
    }

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
