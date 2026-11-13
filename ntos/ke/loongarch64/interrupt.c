/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 line-interrupt dispatch and the DPC interrupt
 *
 * Entered from KiLa64TrapHandler with Ecode 0 ([V1-7.4.6]: "INT" is the
 * exception class of every line interrupt, and only exists because
 * ECFG.VS=0) and with CRMD.IE clear. The frame sits on the interrupted
 * stack and Pcr->CurrentIrql is still the interrupted level, recorded in
 * TrapFrame->PreviousIrql.
 *
 * The lines are dispatched highest int number first, which is the
 * architectural priority order ([V1-6.1.2]: IPI highest, SWI0 lowest), and
 * only lines whose ECFG.LIE bit is set are considered: ECFG.LIE & ESTAT.IS
 * is the set of interrupts the core was allowed to take ([V1-6.1.4]). A
 * line that is pending but masked stays pending in ESTAT and is delivered
 * when the IRQL is lowered, which is exactly NT's software-interrupt model.
 *
 *   SWI0        APC_LEVEL        local APC request, writable IS bit 0
 *   SWI1        DISPATCH_LEVEL   local DPC request, writable IS bit 1
 *   HWI0-HWI7   IRQL 4+n         external controller, claimed through the HAL
 *   PMCOV       PROFILE_LEVEL    no consumer on this port
 *   TI          CLOCK_LEVEL      stable timer, cleared by the HAL with TICLR
 *   IPI         IPI_LEVEL        interprocessor requests, ESTAT.IS[12]
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#include "la64ke.h"
#define NDEBUG
#include <debug.h>

/* The HAL numbers its vectors as EIOINTC sources 0..255, PCH-MSI 32..255 and
 * LIOINTC 256..287 (its own contract); 1024 leaves room for a second
 * cascaded controller without changing this table's stride. */
#define LA64_MAX_EXTERNAL_SOURCE    1024
#define LA64_SECONDARY_VECTORS      512
#define LA64_INTERRUPT_VECTORS      (LA64_MAX_EXTERNAL_SOURCE + LA64_SECONDARY_VECTORS)

/* A level source that keeps re-asserting with no owner would spin here
 * forever; after this many passes the mask is resynchronised and the
 * dispatch returns, leaving the line pending for the next IRQL change. */
#define LA64_MAX_DISPATCH_PASSES    64

static KSPIN_LOCK KiLa64InterruptTableLock;
static PKINTERRUPT KiLa64BootInterruptTable[LA64_INTERRUPT_VECTORS];
static PKINTERRUPT *KiLa64InterruptTables[MAXIMUM_PROCESSORS] = {KiLa64BootInterruptTable};
static ULONG KiLa64InterruptConnections[LA64_INTERRUPT_VECTORS];
static BOOLEAN KiSecondaryInterruptServicesEnabled;

static inline
PKINTERRUPT
KiLa64LoadInterruptHead(
    _In_ ULONG Vector,
    _In_ ULONG Cpu)
{
    PKINTERRUPT *Table = (PKINTERRUPT *)ReadPointerNoFence((PVOID const volatile *)&KiLa64InterruptTables[Cpu]);

    return Table ? (PKINTERRUPT)ReadPointerNoFence((PVOID const volatile *)&Table[Vector]) : NULL;
}

BOOLEAN
NTAPI
KiLa64AllocateInterruptTable(
    _In_ ULONG Number)
{
    PKINTERRUPT *Table;

    if (Number >= MAXIMUM_PROCESSORS)
        return FALSE;
    if (KiLa64InterruptTables[Number])
        return TRUE;
    Table = ExAllocatePoolZero(NonPagedPool, sizeof(KiLa64BootInterruptTable), TAG_KERNEL);
    if (!Table)
        return FALSE;
    InterlockedExchangePointer((PVOID volatile *)&KiLa64InterruptTables[Number], Table);
    return TRUE;
}

static
VOID
KiLa64UpdateInterruptChain(
    _Inout_ PKINTERRUPT Interrupt,
    _In_ BOOLEAN Connect)
{
    PKINTERRUPT *Table = KiLa64InterruptTables[KeGetCurrentProcessorNumber()];
    PKINTERRUPT Head;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentProcessorNumber() == (UCHAR)Interrupt->Number);
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    Head = Table[Interrupt->Vector];
    if (Connect)
    {
        if (Head)
        {
            InsertTailList(&Head->InterruptListEntry, &Interrupt->InterruptListEntry);
        }
        else
        {
            InitializeListHead(&Interrupt->InterruptListEntry);
            KeMemoryBarrier();
            Table[Interrupt->Vector] = Interrupt;
        }
    }
    else
    {
        if (IsListEmpty(&Head->InterruptListEntry))
        {
            ASSERT(Head == Interrupt);
            Table[Interrupt->Vector] = NULL;
        }
        else
        {
            if (Head == Interrupt)
            {
                Table[Interrupt->Vector] = CONTAINING_RECORD(Head->InterruptListEntry.Flink,
                                                             KINTERRUPT, InterruptListEntry);
            }
            RemoveEntryList(&Interrupt->InterruptListEntry);
        }
        InitializeListHead(&Interrupt->InterruptListEntry);
    }
    Interrupt->Connected = Connect;
    KeMemoryBarrier();
    KeLowerIrql(OldIrql);
}

static
BOOLEAN
KiLa64CallInterruptServiceRoutine(
    _In_ PKINTERRUPT Interrupt)
{
    KIRQL OldIrql = KeGetCurrentIrql();
    BOOLEAN Raise = (OldIrql < Interrupt->SynchronizeIrql);
    BOOLEAN Handled;

    if (Raise)
        KfRaiseIrql(Interrupt->SynchronizeIrql);
    KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);
    Handled = Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
    KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);
    if (Raise)
        KfLowerIrql(OldIrql);
    return Handled;
}

static
VOID
KiLa64DispatchChain(
    _In_ PKINTERRUPT Head)
{
    PKINTERRUPT Interrupt = Head;
    PLIST_ENTRY Entry = &Head->InterruptListEntry;

    for (;;)
    {
        if (KiLa64CallInterruptServiceRoutine(Interrupt) &&
            Interrupt->Mode == LevelSensitive)
        {
            break;
        }
        Entry = Entry->Flink;
        if (Entry == &Head->InterruptListEntry)
            break;
        Interrupt = CONTAINING_RECORD(Entry, KINTERRUPT, InterruptListEntry);
    }
}

VOID
NTAPI
KeInitializeInterrupt(
    PKINTERRUPT Interrupt,
    PKSERVICE_ROUTINE ServiceRoutine,
    PVOID ServiceContext,
    PKSPIN_LOCK SpinLock,
    ULONG Vector,
    KIRQL Irql,
    KIRQL SynchronizeIrql,
    KINTERRUPT_MODE InterruptMode,
    BOOLEAN ShareVector,
    CHAR ProcessorNumber,
    BOOLEAN FloatingSave)
{
    Interrupt->Type = InterruptObject;
    Interrupt->Size = sizeof(KINTERRUPT);
    KeInitializeSpinLock(&Interrupt->SpinLock);
    Interrupt->ActualLock = SpinLock ? SpinLock : &Interrupt->SpinLock;
    Interrupt->ServiceRoutine = ServiceRoutine;
    Interrupt->ServiceContext = ServiceContext;
    Interrupt->Vector = Vector;
    Interrupt->Irql = Irql;
    Interrupt->SynchronizeIrql = SynchronizeIrql;
    Interrupt->Mode = InterruptMode;
    Interrupt->ShareVector = ShareVector;
    Interrupt->Number = ProcessorNumber;
    Interrupt->FloatingSave = FloatingSave;
    Interrupt->TickCount = 0;
    Interrupt->Connected = FALSE;
    Interrupt->ServiceCount = 0;
    Interrupt->DispatchCount = 0;
    Interrupt->DispatchAddress = NULL;
    InitializeListHead(&Interrupt->InterruptListEntry);
}

BOOLEAN
NTAPI
KeConnectInterrupt(PKINTERRUPT Interrupt)
{
    KAFFINITY PreviousAffinity = 0;
    BOOLEAN RestoreAffinity = FALSE;
    BOOLEAN Connected;
    PKINTERRUPT Head;
    ULONG Vector, Cpu;
    KIRQL OldIrql;

    if (Interrupt && Interrupt->ServiceRoutine && (Interrupt->Irql == PASSIVE_LEVEL))
        return KiConnectPassiveInterrupt(Interrupt);

    if (!Interrupt || !Interrupt->ServiceRoutine)
        return FALSE;
    Vector = Interrupt->Vector;
    Cpu = (UCHAR)Interrupt->Number;
    if ((Vector >= LA64_INTERRUPT_VECTORS) || (Vector >= LA64_MAX_EXTERNAL_SOURCE) ||
        (Cpu >= (ULONG)KeNumberProcessors) || !KiLa64InterruptTables[Cpu] ||
        (Interrupt->SynchronizeIrql < Interrupt->Irql) ||
        (Interrupt->SynchronizeIrql > HIGH_LEVEL))
    {
        return FALSE;
    }

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Cpu);
        RestoreAffinity = TRUE;
    }
    else if (Cpu != KeGetCurrentProcessorNumber())
    {
        return FALSE;
    }

    KeAcquireSpinLock(&KiLa64InterruptTableLock, &OldIrql);
    if (!Interrupt->Connected)
    {
        Head = KiLa64InterruptTables[Cpu][Vector];
        if (!Head)
        {
            KiLa64UpdateInterruptChain(Interrupt, TRUE);
            if ((KiLa64InterruptConnections[Vector] != 0) ||
                HalEnableSystemInterrupt(Vector, Interrupt->Irql, Interrupt->Mode))
            {
                KiLa64InterruptConnections[Vector]++;
            }
            else
            {
                KiLa64UpdateInterruptChain(Interrupt, FALSE);
            }
        }
        else if (Interrupt->ShareVector && Head->ShareVector &&
                 (Interrupt->Mode == Head->Mode) && (Interrupt->Irql == Head->Irql))
        {
            KiLa64UpdateInterruptChain(Interrupt, TRUE);
        }
    }
    Connected = Interrupt->Connected;
    KeReleaseSpinLock(&KiLa64InterruptTableLock, OldIrql);
    if (RestoreAffinity)
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
    return Connected;
}

BOOLEAN
NTAPI
KeDisconnectInterrupt(PKINTERRUPT Interrupt)
{
    KAFFINITY PreviousAffinity = 0;
    BOOLEAN RestoreAffinity = FALSE;
    BOOLEAN Connected;
    PKINTERRUPT Head;
    ULONG Vector, Cpu;
    KIRQL OldIrql;

    if (Interrupt && (Interrupt->Irql == PASSIVE_LEVEL))
        return KiDisconnectPassiveInterrupt(Interrupt);

    if (!Interrupt)
        return FALSE;
    Vector = Interrupt->Vector;
    Cpu = (UCHAR)Interrupt->Number;
    if ((Vector >= LA64_INTERRUPT_VECTORS) || (Vector >= LA64_MAX_EXTERNAL_SOURCE) ||
        (Cpu >= (ULONG)KeNumberProcessors) || !KiLa64InterruptTables[Cpu])
    {
        return FALSE;
    }

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Cpu);
        RestoreAffinity = TRUE;
    }
    else if (Cpu != KeGetCurrentProcessorNumber())
    {
        return FALSE;
    }

    KeAcquireSpinLock(&KiLa64InterruptTableLock, &OldIrql);
    Head = KiLa64InterruptTables[Cpu][Vector];
    Connected = Interrupt->Connected;
    if (Head && Connected)
    {
        if ((Head == Interrupt) && IsListEmpty(&Head->InterruptListEntry))
        {
            ASSERT(KiLa64InterruptConnections[Vector] != 0);
            if (--KiLa64InterruptConnections[Vector] == 0)
                HalDisableSystemInterrupt(Vector, Interrupt->Irql);
        }
        KiLa64UpdateInterruptChain(Interrupt, FALSE);
    }
    KeReleaseSpinLock(&KiLa64InterruptTableLock, OldIrql);
    if (RestoreAffinity)
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
    return Connected;
}

NTSTATUS
NTAPI
KeInitializeSecondaryInterruptServices(VOID)
{
    KiSecondaryInterruptServicesEnabled = TRUE;
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
KeDispatchSecondaryInterrupt(
    _In_ ULONG Vector,
    _In_ ULONG_PTR Flags,
    _In_opt_ PVOID Reserved)
{
    PKINTERRUPT Head;
    KIRQL OldIrql;
    BOOLEAN Raised = FALSE;
    BOOLEAN IsLevel;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Reserved);

    if (!KiSecondaryInterruptServicesEnabled || (Vector >= LA64_INTERRUPT_VECTORS))
        return FALSE;
    if (KiDispatchPassiveInterrupt(Vector, &IsLevel))
        return !IsLevel;

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        Raised = TRUE;
    }
    Head = KiLa64LoadInterruptHead(Vector, KeGetCurrentProcessorNumber());
    if (Head)
        KiLa64DispatchChain(Head);
    if (Raised)
        KeLowerIrql(OldIrql);
    return Head != NULL;
}

/* The debugger lists connected sources with the other processors frozen. */
PKINTERRUPT
NTAPI
KiLa64QueryInterrupt(
    _In_ ULONG Source)
{
    if (Source >= LA64_INTERRUPT_VECTORS)
        return NULL;
    return KiLa64LoadInterruptHead(Source, KeGetCurrentProcessorNumber());
}

ULONG
NTAPI
KiLa64QueryInterruptLimit(VOID)
{
    return LA64_INTERRUPT_VECTORS;
}

/* ---------------------------------------------------------------------------
 * External controller lines (HWI0-HWI7)
 *
 * The HAL owns the controller: it claims the highest-priority pending source
 * (0xFFFFFFFF means "nothing"), and completes it. Vector 0 is a real source
 * on EIOINTC, which is why the spurious value is not zero.
 * ------------------------------------------------------------------------ */
static
BOOLEAN
KiLa64ExternalInterrupt(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKINTERRUPT Head;
    KIRQL OldIrql;
    ULONG Source;
    BOOLEAN Claimed = FALSE;

    if (KeGetCurrentIrql() >= SYNCH_LEVEL)
    {
        /* A line arrived above its own IRQL: leave it pending and make the
         * mask agree with the current IRQL. */
        KiLa64SynchronizeInterruptMask();
        return FALSE;
    }

    for (;;)
    {
        Source = HalpLa64ClaimInterrupt();
        if (Source == LA64_SPURIOUS_SOURCE)
            break;
        Claimed = TRUE;

        Prcb->InterruptCount++;
        Prcb->DeviceInterrupts++;
        OldIrql = KfRaiseIrql(SYNCH_LEVEL);
        Head = (Source < LA64_MAX_EXTERNAL_SOURCE) ?
               KiLa64LoadInterruptHead(Source, KeGetCurrentProcessorNumber()) : NULL;
        if (Head)
        {
            _enable();      /* the clock may preempt a device ISR */
            KiLa64DispatchChain(Head);
            _disable();
        }
        else
        {
            BOOLEAN IsLevel = FALSE;

            if (KiDispatchPassiveInterrupt(Source, &IsLevel) && IsLevel)
                HalDisableSystemInterrupt(Source, PASSIVE_LEVEL);
        }
        HalpLa64CompleteInterrupt(Source);
        KfLowerIrql(OldIrql);
    }
    return Claimed;
}

/* ---------------------------------------------------------------------------
 * Clock
 * ------------------------------------------------------------------------ */
static
BOOLEAN
KiLa64ClockInterrupt(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    KIRQL OldIrql;

    if (KeGetCurrentIrql() >= CLOCK_LEVEL)
    {
        KiLa64SynchronizeInterruptMask();
        return FALSE;
    }

    /* The HAL clears CSR.TICLR.TI ([V1-7.6.5] write-1-to-clear) and rearms
     * TCFG/TVAL; ke only owns the IRQL. */
    KeGetCurrentPrcb()->InterruptCount++;
    KeGetCurrentPrcb()->ClockInterrupts++;
    OldIrql = KfRaiseIrql(CLOCK_LEVEL);
    HalpLa64ClockInterrupt(TrapFrame);
    KfLowerIrql(OldIrql);
    return TRUE;
}

/* ---------------------------------------------------------------------------
 * Software lines: local APC and DPC requests
 *
 * The IS bit is the latch and ECFG.LIE is the mask, so a request raised
 * while the IRQL is too high simply stays pending; nothing has to be
 * remembered in a software array. Every served bit is cleared before the
 * work runs, and the loop re-reads ESTAT so a request that arrived during
 * the work is served by the next pass.
 * ------------------------------------------------------------------------ */
static
VOID
KiLa64DeliverSoftwareInterrupts(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    for (;;)
    {
        PKPCR Pcr = KeGetPcr();
        KIRQL Irql = Pcr->CurrentIrql;
        ULONG Pending = KiLa64EstatLines(KiLa64ReadEstat());

        if ((Irql < DISPATCH_LEVEL) && (Pending & LA64_IRQ_BIT(LA64_IRQ_SWI1)))
        {
            KiLa64ClearSoftwareLine(LA64_IRQ_SWI1);
            KiClearDpcInterruptRequested(KeGetCurrentPrcb());
            KeMemoryBarrier();
            KfRaiseIrql(DISPATCH_LEVEL);
            _enable();
            KiDispatchInterrupt();
            _disable();
            KfLowerIrql(Irql);
            continue;
        }

        if ((Irql < APC_LEVEL) && (Pending & LA64_IRQ_BIT(LA64_IRQ_SWI0)))
        {
            KiLa64ClearSoftwareLine(LA64_IRQ_SWI0);
            KfRaiseIrql(APC_LEVEL);
            _enable();
            KiDeliverApc(KiLa64UserTrap(TrapFrame) ? UserMode : KernelMode, NULL, TrapFrame);
            _disable();
            KfLowerIrql(Irql);
            continue;
        }
        break;
    }
}

/* ---------------------------------------------------------------------------
 * Line dispatch
 * ------------------------------------------------------------------------ */

/* Service one line. Returns TRUE when the line was handled and the dispatch
 * should re-read ESTAT, FALSE when the line cannot make progress at this
 * IRQL and must be left pending. */
static
BOOLEAN
KiLa64ServiceLine(
    _In_ ULONG Line,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    if ((Line == LA64_IRQ_SWI0) || (Line == LA64_IRQ_SWI1))
    {
        KiLa64DeliverSoftwareInterrupts(TrapFrame);
        return TRUE;
    }

    if ((Line >= LA64_IRQ_HWI0) && (Line < LA64_IRQ_HWI0 + 8))
        return KiLa64ExternalInterrupt(TrapFrame);

    switch (Line)
    {
        case LA64_IRQ_TIMER:
            return KiLa64ClockInterrupt(TrapFrame);

        case LA64_IRQ_IPI:
            if (KeGetCurrentIrql() >= IPI_LEVEL)
            {
                KiLa64SynchronizeInterruptMask();
                return FALSE;
            }
            KeGetCurrentPrcb()->InterruptCount++;
            /* The HAL owns the IPI block: acknowledge the source first, then
             * service every request the sender recorded. */
            HalpLa64IpiAcknowledge();
            KfRaiseIrql(IPI_LEVEL);
            KiIpiServiceRoutine(TrapFrame, NULL);
            KfLowerIrql(TrapFrame->PreviousIrql);
            return TRUE;

        case LA64_IRQ_PMCOV:
            /* No performance monitor consumer exists on this port, and the
             * HAL enables no PMCFG counter, so a PMCOV line is a hardware or
             * HAL error rather than an interrupt to deliver. */
            KiLa64TrapStop(TrapFrame);
            return FALSE;

        default:
            DbgPrint("LoongArch64: unexpected interrupt line %lu (ESTAT %lx)\n",
                     Line, TrapFrame->Estat);
            return FALSE;
    }
}

VOID
NTAPI
KiLa64InterruptDispatch(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    ULONG Pass;

    ASSERT(KiLa64EstatEcode(TrapFrame->Estat) == LA64_ECODE_INT);
    ASSERT(TrapFrame->PreviousIrql == KeGetCurrentIrql());

    for (Pass = 0; Pass < LA64_MAX_DISPATCH_PASSES; Pass++)
    {
        ULONG Enabled = KiLa64ReadEcfg() & LA64_ECFG_LIE_MASK;
        ULONG Pending = KiLa64EstatLines(KiLa64ReadEstat()) & Enabled;
        ULONG Line;

        if (Pending == 0)
            return;

        /* Highest int number wins, [V1-6.1.2]. */
        for (Line = LA64_LINE_COUNT; Line-- > 0;)
        {
            if (Pending & LA64_IRQ_BIT(Line))
                break;
        }

        if (!KiLa64ServiceLine(Line, TrapFrame))
            return;
    }

    /* A level source with no owner kept re-asserting. Leave it pending and
     * make the mask agree with the IRQL instead of spinning. */
    DbgPrint("LoongArch64: interrupt line did not quiesce (ESTAT %lx)\n",
             KiLa64ReadEstat());
    KiLa64SynchronizeInterruptMask();
}

/* Retire DPCs and perform the pending thread switch at DISPATCH_LEVEL.
 * Same contract as the amd64/i386 KiDispatchInterrupt: entered with
 * interrupts enabled at DISPATCH_LEVEL, returns at DISPATCH_LEVEL or above
 * (the thread that switches back may have been at SYNCH_LEVEL; the caller
 * lowers). Deferred routines execute on the processor-owned DPC stack; the
 * thread stack is restored before scheduling. */
VOID
NTAPI
KiDispatchInterrupt(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD NewThread, OldThread;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    if (SmpDbgEnabled)
        SmpDbgDispatchInterrupt(Prcb->Number);

    /* KiRetireDpcList expects interrupts disabled and re-enables them
     * around each deferred routine. */
    _disable();
    if ((Prcb->DpcData[0].DpcQueueDepth) ||
        (Prcb->TimerRequest) ||
        (Prcb->DeferredReadyListHead.Next))
    {
        KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);
    }
    _enable();

    if (Prcb->QuantumEnd)
    {
        Prcb->QuantumEnd = FALSE;
        KiQuantumEnd();
        return;
    }

    if (Prcb->NextThread == NULL)
        return;

    KiAcquirePrcbLock(Prcb);
    OldThread = Prcb->CurrentThread;
    if ((Prcb->NextThread == NULL) || KiConsumeSelfNextThread(Prcb, OldThread))
    {
        KiReleasePrcbLock(Prcb);
        return;
    }

    /* The idle thread never runs below DISPATCH_LEVEL, so it can never be
     * the interrupted thread here; its own loop performs its switches. */
    ASSERT(OldThread != Prcb->IdleThread);
    NewThread = Prcb->NextThread;
    KiSetThreadSwapBusy(OldThread);
    Prcb->NextThread = NULL;
    Prcb->CurrentThread = NewThread;
    NewThread->State = Running;
    OldThread->WaitReason = WrDispatchInt;

    /* Releases the PRCB lock. */
    KxQueueReadyThread(OldThread, Prcb);
    KiSwapContext(APC_LEVEL, OldThread);
}

BOOLEAN
NTAPI
KiSetClockDeadline(
    _In_ ULONGLONG InterruptTime)
{
    /* TODO: a one-shot deadline could use TVAL directly ([V1-7.6.3] is the
     * countdown), but the HAL owns the timer and exposes only the periodic
     * rearm, so there is no path to program a different comparand yet. */
    UNREFERENCED_PARAMETER(InterruptTime);
    return FALSE;
}
