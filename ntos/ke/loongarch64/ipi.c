/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 interprocessor requests and broadcast rendezvous
 *
 * A request is a bit in Prcb->RequestSummary; the doorbell is the IPI line,
 * ESTAT.IS[12], which the HAL raises and acknowledges through its IPI block
 * ([V1-6.1.1]: "the interrupt source for inter-processor interrupts comes
 * from an interrupt controller outside the core"). ke therefore never talks
 * to the doorbell registers: KiIpiSend records the request and asks the HAL
 * to ring the target cores, and the receive path in interrupt.c
 * acknowledges through the HAL before it services anything.
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#include "la64ke.h"

static struct
{
    PKIPI_BROADCAST_WORKER Function;
    ULONG_PTR Argument;
    volatile LONG Arriving;
    volatile LONG Remaining;
    volatile LONG Release;
    volatile LONG Busy;
} KiLa64GenericCall;

VOID FASTCALL
KiIpiSend(
    KAFFINITY Targets,
    ULONG Request)
{
    KAFFINITY Remaining = Targets & KeActiveProcessors;
    ULONG Number;

    while (BitScanForwardAffinity(&Number, Remaining))
    {
        PKPRCB Prcb = KiProcessorBlock[Number];

        ASSERT(Prcb != NULL);
        ASSERT(Prcb->Number == Number);
        ASSERT(Prcb->SetMember == AFFINITY_MASK(Number));
        InterlockedOr(&Prcb->RequestSummary, (LONG)Request);
        Remaining &= ~AFFINITY_MASK(Number);
    }

    /* The request bits are published before the doorbell so a target that
     * takes the IPI immediately cannot observe a summary without its bit. */
    KeMemoryBarrier();
    if (Targets & KeActiveProcessors)
        HalpLa64IpiSend(Targets & KeActiveProcessors);
}

/* Behind HalSendSoftwareInterrupt: request an APC or DPC interrupt on each
 * target processor, the current one included. */
VOID
NTAPI
KiLa64SendSoftwareInterrupt(
    _In_ KAFFINITY TargetSet,
    _In_ KIRQL Irql)
{
    KIRQL OldIrql = KeGetCurrentIrql();
    PKPRCB Prcb;

    if ((Irql != APC_LEVEL) && (Irql != DISPATCH_LEVEL))
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, Irql, OldIrql, 0, 0);

    /* Stay on this processor while it is taken out of the target set. */
    if (OldIrql < DISPATCH_LEVEL)
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    Prcb = KeGetCurrentPrcb();
    if (TargetSet & Prcb->SetMember)
    {
        if (Irql == DISPATCH_LEVEL)
            KiSetDpcInterruptRequested(Prcb);
        /* The local line is the software interrupt itself: SWI0 for APC,
         * SWI1 for DISPATCH, [V1-6.1.1]. */
        KiLa64RequestSoftwareLine((Irql == APC_LEVEL) ? LA64_IRQ_SWI0 : LA64_IRQ_SWI1);
    }

    TargetSet &= ~Prcb->SetMember;
    if (TargetSet)
        KiIpiSend(TargetSet, (Irql == APC_LEVEL) ? IPI_APC : IPI_DPC);

    if (OldIrql < DISPATCH_LEVEL)
        KeLowerIrql(OldIrql);
}

/* A target runs the broadcast worker at IPI_LEVEL once every target has
 * arrived, so the whole machine is quiesced while any worker runs. */
static
VOID
KiLa64ExecuteGenericCall(
    _In_opt_ PKTRAP_FRAME TrapFrame)
{
    PKIPI_BROADCAST_WORKER Function;
    ULONG_PTR Argument;
    LONG Arriving, Remaining;

    ASSERT(KeGetCurrentIrql() == IPI_LEVEL);
    KeMemoryBarrier();
    Function = KiLa64GenericCall.Function;
    Argument = KiLa64GenericCall.Argument;
    ASSERT(Function != NULL && KiLa64GenericCall.Busy != 0);
    Arriving = InterlockedDecrement(&KiLa64GenericCall.Arriving);
    UNREFERENCED_PARAMETER(Arriving);
    ASSERT(Arriving >= 0);
    while (InterlockedCompareExchange(&KiLa64GenericCall.Release, 0, 0) == 0)
        YieldProcessor();
    if (TrapFrame != NULL)
        _enable();
    Function(Argument);
    if (TrapFrame != NULL)
        _disable();
    Remaining = InterlockedDecrement(&KiLa64GenericCall.Remaining);
    UNREFERENCED_PARAMETER(Remaining);
    ASSERT(Remaining >= 0);
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
    if (SmpDbgEnabled)
        SmpDbgIpi(Prcb->Number);
    if (Prcb->IpiFrozen == IPI_FROZEN_STATE_TARGET_FREEZE)
        KiProcessorFreezeHandler(TrapFrame, ExceptionFrame);
    InterlockedAnd(&Prcb->RequestSummary, ~IPI_FREEZE);
    if (InterlockedAnd(&Prcb->RequestSummary, ~IPI_APC) & IPI_APC)
        HalRequestSoftwareInterrupt(APC_LEVEL);
    if (InterlockedAnd(&Prcb->RequestSummary, ~IPI_DPC) & IPI_DPC)
    {
        if (SmpDbgEnabled)
            SmpDbgRemoteDpc(Prcb->Number, SMPDBG_SOURCE_UNKNOWN);
        KiSetDpcInterruptRequested(Prcb);
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }
    if (InterlockedAnd(&Prcb->RequestSummary, ~IPI_SYNCH_REQUEST) & IPI_SYNCH_REQUEST)
    {
        if (SmpDbgEnabled)
            SmpDbgGenericCallIpi(Prcb->Number);
        KiLa64ExecuteGenericCall(TrapFrame);
    }
    return TRUE;
}

VOID NTAPI
KiIpiProcessRequests(VOID)
{
    KiIpiServiceRoutine(NULL, NULL);
}

ULONG_PTR
NTAPI
KeIpiGenericCall(
    PKIPI_BROADCAST_WORKER Function,
    ULONG_PTR Argument)
{
    KIRQL OldIrql, ExecuteIrql;
    KAFFINITY Targets, Bits;
    LONG Count = 0;
    ULONG_PTR Result;

    ASSERT(Function != NULL);
    ASSERT(KeGetCurrentIrql() < IPI_LEVEL);
    OldIrql = KeGetCurrentIrql();
    /* Keep the packet owner above other interrupt handlers while allowing
     * IPIs. */
    if (OldIrql < IPI_LEVEL - 1)
        KfRaiseIrql(IPI_LEVEL - 1);

    /* A competing caller may itself be a target. Service requests while
     * waiting, including when entered with hardware interrupts masked. */
    while (InterlockedCompareExchange(&KiLa64GenericCall.Busy, 1, 0))
    {
        ExecuteIrql = KfRaiseIrql(IPI_LEVEL);
        KiIpiProcessRequests();
        KfLowerIrql(ExecuteIrql);
        YieldProcessor();
    }

    ASSERT(KiLa64GenericCall.Arriving == 0 && KiLa64GenericCall.Remaining == 0);
    Targets = KeActiveProcessors & ~KeGetCurrentPrcb()->SetMember;
    for (Bits = Targets; Bits; Bits &= Bits - 1)
        ++Count;
    ASSERT((Targets & KeGetCurrentPrcb()->SetMember) == 0);
    ASSERT((Targets == 0) == (Count == 0));
    KiLa64GenericCall.Function = Function;
    KiLa64GenericCall.Argument = Argument;
    KiLa64GenericCall.Arriving = Count;
    KiLa64GenericCall.Remaining = Count;
    KiLa64GenericCall.Release = 0;
    KeMemoryBarrier();
    if (Targets)
        KiIpiSend(Targets, IPI_SYNCH_REQUEST);

    while (KiLa64GenericCall.Arriving)
    {
        ExecuteIrql = KfRaiseIrql(IPI_LEVEL);
        KiIpiProcessRequests();
        KfLowerIrql(ExecuteIrql);
        YieldProcessor();
        KeMemoryBarrier();
    }

    ASSERT(KiLa64GenericCall.Arriving == 0 && KiLa64GenericCall.Remaining == Count);
    ExecuteIrql = KfRaiseIrql(IPI_LEVEL);
    /* Release before calling locally: workers may rendezvous themselves. */
    InterlockedExchange(&KiLa64GenericCall.Release, 1);
    Result = Function(Argument);
    while (KiLa64GenericCall.Remaining)
    {
        YieldProcessor();
        KeMemoryBarrier();
    }
    ASSERT(KiLa64GenericCall.Remaining == 0);
    KfLowerIrql(ExecuteIrql);
    InterlockedExchange(&KiLa64GenericCall.Busy, 0);
    KfLowerIrql(OldIrql);
    return Result;
}
