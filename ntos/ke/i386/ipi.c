/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     x86 interprocessor requests, packets and broadcast rendezvous
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* One call is in flight at a time: a broadcast rendezvous or a packet */
typedef struct _KI_X86_IPI_CALL
{
    PKIPI_BROADCAST_WORKER Function;
    ULONG_PTR Argument;
    PKIPI_WORKER Worker;
    PKIPI_BROADCAST_WORKER BroadcastFunction;
    ULONG_PTR Context;
    PULONG Count;
    volatile LONG Arriving;
    volatile LONG Remaining;
    volatile LONG Release;
    volatile LONG Busy;
} KI_X86_IPI_CALL, *PKI_X86_IPI_CALL;

static KI_X86_IPI_CALL KiX86IpiCall;

static
LONG
KiX86CountTargets(
    _In_ KAFFINITY Targets)
{
    LONG Count = 0;

    for (; Targets != 0; Targets &= Targets - 1)
        Count++;

    return Count;
}

VOID
FASTCALL
KiIpiSend(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG IpiRequest)
{
    KAFFINITY Remaining = TargetSet & KeActiveProcessors;
    ULONG Number;

    TargetSet = Remaining;
    while (BitScanForwardAffinity(&Number, Remaining))
    {
        PKPRCB Prcb = KiProcessorBlock[Number];

        ASSERT(Prcb != NULL);
        ASSERT(Prcb->Number == Number);
        InterlockedOr((PLONG)&Prcb->RequestSummary, (LONG)IpiRequest);
        Remaining &= ~AFFINITY_MASK(Number);
    }

    if (TargetSet != 0)
        HalRequestIpi(TargetSet);
}

static
VOID
KiX86ExecuteGenericCall(
    _In_opt_ PKTRAP_FRAME TrapFrame)
{
    PKIPI_BROADCAST_WORKER Function;
    ULONG_PTR Argument;
    LONG Arriving;
    LONG Remaining;

    ASSERT(KeGetCurrentIrql() == IPI_LEVEL);
    KeMemoryBarrier();
    Function = KiX86IpiCall.Function;
    Argument = KiX86IpiCall.Argument;
    ASSERT((Function != NULL) && (KiX86IpiCall.Busy != 0));

    Arriving = InterlockedDecrement(&KiX86IpiCall.Arriving);
    UNREFERENCED_PARAMETER(Arriving);
    ASSERT(Arriving >= 0);

    while (InterlockedCompareExchange(&KiX86IpiCall.Release, 0, 0) == 0)
        YieldProcessor();

    if (TrapFrame != NULL)
        _enable();
    Function(Argument);
    if (TrapFrame != NULL)
        _disable();

    Remaining = InterlockedDecrement(&KiX86IpiCall.Remaining);
    UNREFERENCED_PARAMETER(Remaining);
    ASSERT(Remaining >= 0);
}

static
VOID
KiX86ExecutePacket(VOID)
{
    PKIPI_WORKER Worker;

    ASSERT(KeGetCurrentIrql() == IPI_LEVEL);
    KeMemoryBarrier();
    Worker = KiX86IpiCall.Worker;
    ASSERT((Worker != NULL) && (KiX86IpiCall.Busy != 0));

    Worker(&KiX86IpiCall,
           (PVOID)KiX86IpiCall.BroadcastFunction,
           (PVOID)KiX86IpiCall.Context,
           (PVOID)KiX86IpiCall.Count);
}

BOOLEAN
NTAPI
KiIpiServiceRoutine(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    UNREFERENCED_PARAMETER(ExceptionFrame);
    ASSERT(KeGetCurrentIrql() == IPI_LEVEL);

    /* TODO: the x86 freeze is not implemented, so nothing sends IPI_FREEZE yet */
    ASSERT((Prcb->RequestSummary & IPI_FREEZE) == 0);

    if (InterlockedAnd((PLONG)&Prcb->RequestSummary, ~IPI_APC) & IPI_APC)
        HalRequestSoftwareInterrupt(APC_LEVEL);

    if (InterlockedAnd((PLONG)&Prcb->RequestSummary, ~IPI_DPC) & IPI_DPC)
    {
        KiSetDpcInterruptRequested(Prcb);
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }

    if (InterlockedAnd((PLONG)&Prcb->RequestSummary, ~IPI_PACKET_READY) & IPI_PACKET_READY)
        KiX86ExecutePacket();

    if (InterlockedAnd((PLONG)&Prcb->RequestSummary, ~IPI_SYNCH_REQUEST) & IPI_SYNCH_REQUEST)
        KiX86ExecuteGenericCall(TrapFrame);

    return TRUE;
}

static
VOID
KiX86ServiceRequests(VOID)
{
    KIRQL OldIrql;

    OldIrql = KfRaiseIrql(IPI_LEVEL);
    KiIpiServiceRoutine(NULL, NULL);
    KfLowerIrql(OldIrql);
}

/* A competing caller may itself be a target, so requests are served while waiting */
static
KIRQL
KiX86AcquireIpiCall(VOID)
{
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() < IPI_LEVEL);

    OldIrql = KeGetCurrentIrql();
    if (OldIrql < IPI_LEVEL - 1)
        KfRaiseIrql(IPI_LEVEL - 1);

    while (InterlockedCompareExchange(&KiX86IpiCall.Busy, 1, 0) != 0)
    {
        KiX86ServiceRequests();
        YieldProcessor();
    }

    return OldIrql;
}

static
VOID
KiX86ReleaseIpiCall(
    _In_ KIRQL OldIrql)
{
    ASSERT(KiX86IpiCall.Busy != 0);

    KiX86IpiCall.Function = NULL;
    KiX86IpiCall.Worker = NULL;
    InterlockedExchange(&KiX86IpiCall.Busy, 0);
    KfLowerIrql(OldIrql);
}

ULONG_PTR
NTAPI
KeIpiGenericCall(
    _In_ PKIPI_BROADCAST_WORKER Function,
    _In_ ULONG_PTR Argument)
{
    KIRQL OldIrql;
    KIRQL ExecuteIrql;
    KAFFINITY Targets;
    LONG Count;
    ULONG_PTR Result;

    ASSERT(Function != NULL);

    OldIrql = KiX86AcquireIpiCall();

    Targets = KeActiveProcessors & ~KeGetCurrentPrcb()->SetMember;
    Count = KiX86CountTargets(Targets);
    ASSERT((KiX86IpiCall.Arriving == 0) && (KiX86IpiCall.Remaining == 0));

    KiX86IpiCall.Function = Function;
    KiX86IpiCall.Argument = Argument;
    KiX86IpiCall.Arriving = Count;
    KiX86IpiCall.Remaining = Count;
    KiX86IpiCall.Release = 0;
    KeMemoryBarrier();
    if (Targets != 0)
        KiIpiSend(Targets, IPI_SYNCH_REQUEST);

    while (InterlockedCompareExchange(&KiX86IpiCall.Arriving, 0, 0) != 0)
    {
        KiX86ServiceRequests();
        YieldProcessor();
    }

    /* Release before calling here: a worker may rendezvous on its own */
    ExecuteIrql = KfRaiseIrql(IPI_LEVEL);
    InterlockedExchange(&KiX86IpiCall.Release, 1);
    Result = Function(Argument);
    while (InterlockedCompareExchange(&KiX86IpiCall.Remaining, 0, 0) != 0)
        YieldProcessor();
    KfLowerIrql(ExecuteIrql);

    KiX86ReleaseIpiCall(OldIrql);
    return Result;
}

/* Returns once every target has run the worker and signalled the packet done */
VOID
NTAPI
KiIpiSendPacket(
    _In_ KAFFINITY TargetProcessors,
    _In_ PKIPI_WORKER WorkerFunction,
    _In_ PKIPI_BROADCAST_WORKER BroadcastFunction,
    _In_ ULONG_PTR Context,
    _In_ PULONG Count)
{
    KIRQL OldIrql;
    KAFFINITY Targets;

    ASSERT(WorkerFunction != NULL);

    OldIrql = KiX86AcquireIpiCall();

    Targets = TargetProcessors & KeActiveProcessors & ~KeGetCurrentPrcb()->SetMember;
    ASSERT(KiX86IpiCall.Remaining == 0);

    KiX86IpiCall.Worker = WorkerFunction;
    KiX86IpiCall.BroadcastFunction = BroadcastFunction;
    KiX86IpiCall.Context = Context;
    KiX86IpiCall.Count = Count;
    KiX86IpiCall.Remaining = KiX86CountTargets(Targets);
    KeMemoryBarrier();
    if (Targets != 0)
        KiIpiSend(Targets, IPI_PACKET_READY);

    while (InterlockedCompareExchange(&KiX86IpiCall.Remaining, 0, 0) != 0)
    {
        KiX86ServiceRequests();
        YieldProcessor();
    }

    KiX86ReleaseIpiCall(OldIrql);
}

VOID
FASTCALL
KiIpiSignalPacketDone(
    _In_ PKIPI_CONTEXT PacketContext)
{
    PKI_X86_IPI_CALL Call = PacketContext;
    LONG Remaining;

    ASSERT(Call == &KiX86IpiCall);

    Remaining = InterlockedDecrement(&Call->Remaining);
    UNREFERENCED_PARAMETER(Remaining);
    ASSERT(Remaining >= 0);
}

VOID
NTAPI
Kii386SpinOnSpinLock(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    /* TODO: the checked-build diagnostics Flags selects; this only waits for the lock to look free */
    while (*(volatile KSPIN_LOCK *)SpinLock & 1)
        YieldProcessor();
}

/* EOF */
