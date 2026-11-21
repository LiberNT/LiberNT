/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IPI code for x64
 * COPYRIGHT:   Copyright 2023 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#define NDEBUG
#include <debug.h>

#define KI_IPI_MEMORY       0x20
#define KI_IPI_GENERIC_CALL 0x40

typedef struct DECLSPEC_CACHEALIGN _KI_GENERIC_CALL_PACKET
{
    PKIPI_BROADCAST_WORKER Function;
    ULONG_PTR Argument;
    volatile LONG TargetsRemaining;
    volatile LONG Release;
    volatile KAFFINITY OutstandingDone[KE_MAXIMUM_GROUPS];
} KI_GENERIC_CALL_PACKET;

static KI_GENERIC_CALL_PACKET KiGenericCallPacket;
static volatile LONG KiGenericCallOwner = -1;

/* A mailbox no longer stands for one sender: the first holds what was asked, the next ones the processors that sent a memory request */
#define KiIpiRequests(Prcb) ((volatile LONG64 *)&(Prcb)->RequestMailbox[0].RequestSummary)
#define KiIpiMemorySenders(Prcb, Word) ((volatile LONG64 *)&(Prcb)->RequestMailbox[1 + (Word)].RequestSummary)

C_ASSERT(KE_MAXIMUM_GROUPS < RTL_NUMBER_OF(((PKPRCB)0)->RequestMailbox) + KI_EXTRA_REQUEST_MAILBOXES);

/* FUNCTIONS *****************************************************************/

static
BOOLEAN
KiIpiQueueRequest(
    _In_ ULONG Target,
    _In_ ULONG IpiRequest)
{
    PKPRCB SourcePrcb = KeGetCurrentPrcb();
    PKPRCB TargetPrcb;

    ASSERT(Target != SourcePrcb->Number);
    if (!KiIsProcessorInSet(KeActiveProcessors.Bitmap, Target))
        return FALSE;

    TargetPrcb = KiProcessorBlock[Target];
    ASSERT(TargetPrcb != NULL);

    if (IpiRequest == KI_IPI_MEMORY)
    {
        InterlockedOr64(KiIpiMemorySenders(TargetPrcb, KI_SET_WORD(SourcePrcb->Number)),
                        (LONG64)KI_SET_BIT(SourcePrcb->Number));
    }

    InterlockedOr64(KiIpiRequests(TargetPrcb), IpiRequest);
    return TRUE;
}

VOID
FASTCALL
KiIpiSendProcessor(
    _In_ ULONG Number,
    _In_ ULONG IpiRequest)
{
    ULONG Word = KI_SET_WORD(Number);
    KAFFINITY Target = KI_SET_BIT(Number);

    /* Check if we can send the IPI directly */
    if (IpiRequest == IPI_APC)
    {
        HalpSendSoftwareInterrupt(Word, Target, APC_LEVEL);
    }
    else if (IpiRequest == IPI_DPC)
    {
        if (KiIpiQueueRequest(Number, IPI_DPC))
            HalpRequestIpi(Word, Target);
    }
    else if (IpiRequest == IPI_FREEZE)
    {
        /* On x64 the freeze IPI is an NMI */
        HalpSendNMI(Word, Target);

        /* Also queue the request behind a normal IPI. Some hypervisors can
         * lose an NMI (VirtualBox on Hyper-V strands it in TRPM forever);
         * KiIpiProcessRequests then completes the freeze through
         * KiFreezeIfRequested. Whichever path claims TARGET_FREEZE first
         * wins, the other one finds nothing to do. */
        if (KiIpiQueueRequest(Number, IPI_FREEZE))
            HalpRequestIpi(Word, Target);
    }
    else
    {
        ASSERT(FALSE);
    }
}

VOID
KiSendMemoryIpi(
    _In_ ULONG Word,
    _In_ ULONG64 Targets)
{
    ULONG64 Remaining, Queued = 0;
    ULONG Bit;

    for (Remaining = Targets; BitScanForward64(&Bit, Remaining); Remaining &= Remaining - 1)
    {
        if (KiIpiQueueRequest(Word * KI_SET_WORD_BITS + Bit, KI_IPI_MEMORY))
            Queued |= 1ULL << Bit;
    }

    if (Queued != 0)
        HalpRequestIpi(Word, (KAFFINITY)Queued);
}

static
VOID
KiIpiParticipateInGenericCall(
    _In_ PKPRCB Prcb)
{
    PKIPI_BROADCAST_WORKER Function = KiGenericCallPacket.Function;
    ULONG_PTR Argument = KiGenericCallPacket.Argument;
    KIRQL OldIrql;
    LONG Remaining;
    BOOLEAN WasOutstanding;

    ASSERT(Function != NULL);
    ASSERT(KiGenericCallOwner >= 0 && KiGenericCallOwner != (LONG)Prcb->Number);
    ASSERT(KiIsProcessorInSet(KiGenericCallPacket.OutstandingDone, Prcb->Number));

    Remaining = InterlockedDecrement(&KiGenericCallPacket.TargetsRemaining);
    UNREFERENCED_PARAMETER(Remaining);
    ASSERT(Remaining >= 0);

    while (ReadAcquire(&KiGenericCallPacket.Release) == 0)
    {
        YieldProcessor();
    }

    OldIrql = KfRaiseIrql(IPI_LEVEL);
    Function(Argument);
    KeLowerIrql(OldIrql);

    WasOutstanding = KiInterlockedRemoveProcessorFromSet(KiGenericCallPacket.OutstandingDone, Prcb->Number);
    UNREFERENCED_PARAMETER(WasOutstanding);
    ASSERT(WasOutstanding);
}

VOID
NTAPI
KiIpiProcessRequests(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG64 Requests, Sources;
    ULONG Word, Bit;

    ASSERT(Prcb->Number < KE_MAXIMUM_PROCESSORS);

    Requests = (ULONG64)InterlockedExchange64(KiIpiRequests(Prcb), 0);

    if (Requests & KI_IPI_MEMORY)
    {
        if (SmpDbgEnabled)
            SmpDbgTbFlushIpi(Prcb->Number);

        for (Word = 0; Word < KiProcessorSetWords; Word++)
        {
            Sources = (ULONG64)InterlockedExchange64(KiIpiMemorySenders(Prcb, Word), 0);
            for (; BitScanForward64(&Bit, Sources); Sources &= Sources - 1)
                MiAmd64ProcessTlbRequest(Word * KI_SET_WORD_BITS + Bit);
        }
    }

    if (Requests & KI_IPI_GENERIC_CALL)
    {
        if (SmpDbgEnabled)
            SmpDbgGenericCallIpi(Prcb->Number);
        KiIpiParticipateInGenericCall(Prcb);
    }

    ASSERT((Requests & ~(KI_IPI_MEMORY |
                         KI_IPI_GENERIC_CALL |
                         IPI_DPC |
                         IPI_FREEZE)) == 0);

    if (Requests & IPI_DPC)
        KiRequestDispatchInterrupt(Prcb);

    /* Complete a freeze whose NMI has not arrived. This also covers the
     * spin loops that poll here with interrupts disabled (TLB shootdown,
     * generic call), where a lost NMI would otherwise deadlock the owner. */
    KiFreezeIfRequested();
}

ULONG_PTR
NTAPI
KeIpiGenericCall(
    _In_ PKIPI_BROADCAST_WORKER Function,
    _In_ ULONG_PTR Argument)
{
    PKPRCB Prcb;
    KI_PROCESSOR_SET Targets;
    BOOLEAN InterruptsEnabled, AnyTargets, Found, Queued;
    KIRQL OldIrql, ExecuteIrql;
    ULONG_PTR Status;
    ULONG Processor, Word;

    ASSERT(Function != NULL);

    InterruptsEnabled = (__readeflags() & EFLAGS_INTERRUPT_MASK) != 0;
    OldIrql = KeGetCurrentIrql();
    if (OldIrql < SYNCH_LEVEL)
        KeRaiseIrql(SYNCH_LEVEL, &OldIrql);

    Prcb = KeGetCurrentPrcb();

    while (InterlockedCompareExchange(&KiGenericCallOwner,
                                      (LONG)Prcb->Number,
                                      -1) != -1)
    {
        KiIpiProcessRequests();
        YieldProcessor();
    }

    ASSERT(KiGenericCallPacket.TargetsRemaining == 0);
    ASSERT(KiIsProcessorSetEmpty(KiGenericCallPacket.OutstandingDone));

    KiCopyProcessorSet(Targets, KeActiveProcessors.Bitmap);
    KiRemoveProcessorFromSet(Targets, Prcb->Number);
    AnyTargets = !KiIsProcessorSetEmpty(Targets);
    if (AnyTargets)
    {
        KiGenericCallPacket.Function = Function;
        KiGenericCallPacket.Argument = Argument;
        WriteRelease(&KiGenericCallPacket.Release, 0);
        for (Word = 0; Word < KiProcessorSetWords; Word++)
            KiGenericCallPacket.OutstandingDone[Word] = Targets[Word];
        InterlockedExchange(&KiGenericCallPacket.TargetsRemaining, (LONG)KiCountProcessorSet(Targets));

        for (Found = KiFindProcessorInSet(Targets, 0, &Processor);
             Found;
             Found = KiFindProcessorInSet(Targets, Processor + 1, &Processor))
        {
            Queued = KiIpiQueueRequest(Processor, KI_IPI_GENERIC_CALL);
            ASSERT(Queued);
            UNREFERENCED_PARAMETER(Queued);
        }

        for (Word = 0; Word < KiProcessorSetWords; Word++)
        {
            if (Targets[Word] != 0)
                HalpRequestIpi(Word, Targets[Word]);
        }

        while (ReadAcquire(&KiGenericCallPacket.TargetsRemaining) != 0)
        {
            if (!InterruptsEnabled)
                KiIpiProcessRequests();
            YieldProcessor();
        }
    }

    ASSERT(!AnyTargets || KiAreProcessorSetsEqual(KiGenericCallPacket.OutstandingDone, Targets));

    KeRaiseIrql(IPI_LEVEL, &ExecuteIrql);

    if (AnyTargets)
        WriteRelease(&KiGenericCallPacket.Release, 1);

    Status = Function(Argument);

    while (!KiIsProcessorSetEmpty(KiGenericCallPacket.OutstandingDone))
    {
        if (!InterruptsEnabled)
            KiIpiProcessRequests();
        YieldProcessor();
    }

    KeLowerIrql(ExecuteIrql);

    InterlockedExchange(&KiGenericCallOwner, -1);
    KeLowerIrql(OldIrql);

    return Status;
}
