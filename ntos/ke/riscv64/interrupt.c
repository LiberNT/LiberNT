/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V supervisor interrupt dispatch and the DPC interrupt
 *
 * Entered from KiRiscvTrapHandler with sstatus.SIE clear, the frame on the
 * interrupted kernel stack and Pcr->CurrentIrql still at the interrupted
 * level (TrapFrame->PreviousIrql). Every source is dispatched at its own
 * IRQL through KfRaiseIrql/KfLowerIrql. Raising the IRQL does not write
 * `sie`: a source that arrives at or below the current IRQL is masked in
 * `sie` then, left pending, and delivered when the IRQL is lowered. Nested
 * traps are allowed at any time; another interrupt can only nest while SIE
 * is deliberately re-enabled.
 *
 *   code 5 (STIP): clock. Raise to CLOCK_LEVEL, HAL rearms and updates time.
 *   code 1 (SSIP): APC/DPC request channel (Pcr->SoftwareInterrupts).
 *   code 9 (SEIP): claim a PLIC source and dispatch its KINTERRUPT chain.
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#define NDEBUG
#include <debug.h>

#define RISCV_SCAUSE_INTERRUPT   (1ULL << 63)
#define RISCV_INTERRUPT_SOFTWARE 1
#define RISCV_INTERRUPT_TIMER    5
#define RISCV_INTERRUPT_EXTERNAL 9
#define RISCV_INTERRUPT_COUNTER_OVERFLOW 13
#define RISCV_PLIC_MAX_SOURCE    1023
#define RISCV_SECONDARY_VECTORS  512
#define RISCV_INTERRUPT_VECTORS  (RISCV_PLIC_MAX_SOURCE + 1 + RISCV_SECONDARY_VECTORS)

static KSPIN_LOCK KiRiscvInterruptTableLock;
static PKINTERRUPT KiRiscvBootInterruptTable[RISCV_INTERRUPT_VECTORS];
static PKINTERRUPT *KiRiscvInterruptTables[MAXIMUM_PROCESSORS] = {KiRiscvBootInterruptTable};
static ULONG KiRiscvInterruptConnections[RISCV_INTERRUPT_VECTORS];
static BOOLEAN KiSecondaryInterruptServicesEnabled;

static inline
PKINTERRUPT
KiRiscvLoadInterruptHead(
    _In_ ULONG Vector,
    _In_ ULONG Cpu)
{
    PKINTERRUPT *Table = (PKINTERRUPT *)ReadPointerNoFence((PVOID const volatile *)&KiRiscvInterruptTables[Cpu]);

    return Table ? (PKINTERRUPT)ReadPointerNoFence((PVOID const volatile *)&Table[Vector]) : NULL;
}

BOOLEAN
NTAPI
KiRiscvAllocateInterruptTable(
    _In_ ULONG Number)
{
    PKINTERRUPT *Table;

    if (Number >= MAXIMUM_PROCESSORS)
        return FALSE;
    if (KiRiscvInterruptTables[Number])
        return TRUE;
    Table = ExAllocatePoolZero(NonPagedPool, sizeof(KiRiscvBootInterruptTable), TAG_KERNEL);
    if (!Table)
        return FALSE;
    InterlockedExchangePointer((PVOID volatile *)&KiRiscvInterruptTables[Number], Table);
    return TRUE;
}

static
VOID
KiRiscvUpdateInterruptChain(
    _Inout_ PKINTERRUPT Interrupt,
    _In_ BOOLEAN Connect)
{
    PKINTERRUPT *Table = KiRiscvInterruptTables[KeGetCurrentProcessorNumber()];
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
                Table[Interrupt->Vector] = CONTAINING_RECORD(Head->InterruptListEntry.Flink,
                                                             KINTERRUPT, InterruptListEntry);
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
KiRiscvCallInterruptServiceRoutine(
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
KiRiscvDispatchChain(
    _In_ PKINTERRUPT Head)
{
    PKINTERRUPT Interrupt = Head;
    PLIST_ENTRY Entry = &Head->InterruptListEntry;

    for (;;)
    {
        if (KiRiscvCallInterruptServiceRoutine(Interrupt) &&
            Interrupt->Mode == LevelSensitive)
            break;
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
    if (Vector == 0 || Vector >= RISCV_INTERRUPT_VECTORS ||
        Cpu >= (ULONG)KeNumberProcessors || !KiRiscvInterruptTables[Cpu] ||
        (Vector <= RISCV_PLIC_MAX_SOURCE && Cpu != 0) ||
        Interrupt->SynchronizeIrql < Interrupt->Irql ||
        Interrupt->SynchronizeIrql > HIGH_LEVEL)
        return FALSE;

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Cpu);
        RestoreAffinity = TRUE;
    }
    else if (Cpu != KeGetCurrentProcessorNumber())
    {
        return FALSE;
    }

    KeAcquireSpinLock(&KiRiscvInterruptTableLock, &OldIrql);
    if (!Interrupt->Connected)
    {
        Head = KiRiscvInterruptTables[Cpu][Vector];
        if (!Head)
        {
            KiRiscvUpdateInterruptChain(Interrupt, TRUE);
            if (KiRiscvInterruptConnections[Vector] != 0 ||
                HalEnableSystemInterrupt(Vector, Interrupt->Irql, Interrupt->Mode))
                KiRiscvInterruptConnections[Vector]++;
            else
                KiRiscvUpdateInterruptChain(Interrupt, FALSE);
        }
        else if (Interrupt->ShareVector && Head->ShareVector &&
                 Interrupt->Mode == Head->Mode && Interrupt->Irql == Head->Irql)
        {
            KiRiscvUpdateInterruptChain(Interrupt, TRUE);
        }
    }
    Connected = Interrupt->Connected;
    KeReleaseSpinLock(&KiRiscvInterruptTableLock, OldIrql);
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
    if (Vector == 0 || Vector >= RISCV_INTERRUPT_VECTORS ||
        Cpu >= (ULONG)KeNumberProcessors || !KiRiscvInterruptTables[Cpu])
        return FALSE;

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Cpu);
        RestoreAffinity = TRUE;
    }
    else if (Cpu != KeGetCurrentProcessorNumber())
    {
        return FALSE;
    }

    KeAcquireSpinLock(&KiRiscvInterruptTableLock, &OldIrql);
    Head = KiRiscvInterruptTables[Cpu][Vector];
    Connected = Interrupt->Connected;
    if (Head && Connected)
    {
        if (Head == Interrupt && IsListEmpty(&Head->InterruptListEntry))
        {
            ASSERT(KiRiscvInterruptConnections[Vector] != 0);
            if (--KiRiscvInterruptConnections[Vector] == 0)
                HalDisableSystemInterrupt(Vector, Interrupt->Irql);
        }
        KiRiscvUpdateInterruptChain(Interrupt, FALSE);
    }
    KeReleaseSpinLock(&KiRiscvInterruptTableLock, OldIrql);
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

    if (!KiSecondaryInterruptServicesEnabled || Vector >= RISCV_INTERRUPT_VECTORS)
        return FALSE;
    if (KiDispatchPassiveInterrupt(Vector, &IsLevel))
        return !IsLevel;

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        Raised = TRUE;
    }
    Head = KiRiscvLoadInterruptHead(Vector, KeGetCurrentProcessorNumber());
    if (Head)
        KiRiscvDispatchChain(Head);
    if (Raised)
        KeLowerIrql(OldIrql);
    return Head != NULL;
}

/* The debugger lists connected sources with the other processors frozen. */
PKINTERRUPT
NTAPI
KiRiscvQueryInterrupt(_In_ ULONG Source)
{
    if ((Source == 0) || (Source >= RISCV_INTERRUPT_VECTORS))
        return NULL;
    return KiRiscvLoadInterruptHead(Source, KeGetCurrentProcessorNumber());
}

ULONG
NTAPI
KiRiscvQueryInterruptLimit(VOID)
{
    return RISCV_INTERRUPT_VECTORS;
}

static
VOID
KiRiscvExternalInterrupt(_Inout_ PKTRAP_FRAME TrapFrame)
{
    PKINTERRUPT Head;
    KIRQL OldIrql;
    ULONG Source;

    if (KeGetCurrentIrql() >= SYNCH_LEVEL)
    {
        KiRiscvSynchronizeInterruptMask();
        return;
    }
    Source = HalpRiscvClaimPlicInterrupt();
    if (Source == 0)
        return; /* A claim of zero is a PLIC spurious notification. */

    KeGetCurrentPrcb()->InterruptCount++;
    KeGetCurrentPrcb()->DeviceInterrupts++;
    OldIrql = KfRaiseIrql(SYNCH_LEVEL);
    Head = Source <= RISCV_PLIC_MAX_SOURCE ?
           KiRiscvLoadInterruptHead(Source, KeGetCurrentProcessorNumber()) : NULL;
    if (Head)
    {
        _enable(); /* The clock may preempt a device ISR. SEIP stays masked. */
        KiRiscvDispatchChain(Head);
        _disable();
    }
    else
    {
        BOOLEAN IsLevel = FALSE;

        if (KiDispatchPassiveInterrupt(Source, &IsLevel) && IsLevel)
            HalDisableSystemInterrupt(Source, PASSIVE_LEVEL);
    }
    HalpRiscvCompletePlicInterrupt(Source);
    KfLowerIrql(OldIrql);
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

/* SSIP carries remote IPIs and local APC/DPC requests. Service the IPI first,
 * then re-evaluate local work after each dispatch or APC delivery, since a
 * context switch may resume this trap on another processor. */
static
VOID
KiRiscvDeliverSoftwareInterrupts(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKPCR Pcr = KeGetPcr();
    KIRQL Irql;

    if (Pcr->CurrentIrql >= IPI_LEVEL)
    {
        KiRiscvSynchronizeInterruptMask();
        return;
    }

    /* Acknowledge first: mask updates re-raise SSIP when a deferred local
     * APC/DPC becomes eligible. Remote requests remain in RequestSummary. */
    __asm__ __volatile__("csrci sip, 2" ::: "memory");
    KeMemoryBarrier();
    KeGetCurrentPrcb()->InterruptCount++;
    Irql = KfRaiseIrql(IPI_LEVEL);
    KiIpiServiceRoutine(TrapFrame, NULL);
    KfLowerIrql(Irql);
    for (;;)
    {
        /* Dispatch/APC delivery may resume this thread on another CPU. */
        Pcr = KeGetPcr();
        Irql = Pcr->CurrentIrql;
        if ((Irql < DISPATCH_LEVEL) &&
            (Pcr->SoftwareInterrupts & (1 << DISPATCH_LEVEL)))
        {
            KiRiscvClearSoftwareInterrupt(DISPATCH_LEVEL);
            KiClearDpcInterruptRequested(KeGetCurrentPrcb());
            KeMemoryBarrier();
            KfRaiseIrql(DISPATCH_LEVEL);
            _enable();
            KiDispatchInterrupt();
            _disable();
            KfLowerIrql(Irql);
            continue;
        }

        if ((Irql < APC_LEVEL) &&
            (Pcr->SoftwareInterrupts & (1 << APC_LEVEL)))
        {
            KiRiscvClearSoftwareInterrupt(APC_LEVEL);
            KfRaiseIrql(APC_LEVEL);
            _enable();
            KiDeliverApc(KiUserTrap(TrapFrame) ? UserMode : KernelMode, NULL, TrapFrame);
            _disable();
            KfLowerIrql(Irql);
            continue;
        }
        break;
    }
}

static
VOID
KiRiscvClockInterrupt(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    KIRQL OldIrql;

    if (KeGetCurrentIrql() >= CLOCK_LEVEL)
    {
        KiRiscvSynchronizeInterruptMask();
        return;
    }

    /* The HAL rearms the deadline (which clears STIP) and updates time. */
    KeGetCurrentPrcb()->InterruptCount++;
    KeGetCurrentPrcb()->ClockInterrupts++;
    OldIrql = KfRaiseIrql(CLOCK_LEVEL);
    HalpRiscvClockInterrupt(TrapFrame);
    KfLowerIrql(OldIrql);
}

static
VOID
KiRiscvProfileInterrupt(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    KIRQL OldIrql;

    if (KeGetCurrentIrql() >= PROFILE_LEVEL)
    {
        KiRiscvSynchronizeInterruptMask();
        return;
    }

    KeGetCurrentPrcb()->InterruptCount++;
    OldIrql = KfRaiseIrql(PROFILE_LEVEL);
    HalpRiscvProfileInterrupt(TrapFrame);
    KfLowerIrql(OldIrql);
}

VOID
NTAPI
KiRiscvInterruptDispatch(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    ULONG64 Code = TrapFrame->Scause & ~RISCV_SCAUSE_INTERRUPT;

    ASSERT(TrapFrame->Scause & RISCV_SCAUSE_INTERRUPT);
    ASSERT(TrapFrame->PreviousIrql == KeGetCurrentIrql());

    switch (Code)
    {
        case RISCV_INTERRUPT_TIMER:
            KiRiscvClockInterrupt(TrapFrame);
            return;

        case RISCV_INTERRUPT_SOFTWARE:
            KiRiscvDeliverSoftwareInterrupts(TrapFrame);
            return;

        case RISCV_INTERRUPT_EXTERNAL:
            KiRiscvExternalInterrupt(TrapFrame);
            return;

        case RISCV_INTERRUPT_COUNTER_OVERFLOW:
            KiRiscvProfileInterrupt(TrapFrame);
            return;

        default:
            DbgPrint("\n*** Unknown supervisor interrupt cause %I64u\n", Code);
            break;
    }
    KiRiscvTrapStop(TrapFrame);
}

BOOLEAN
NTAPI
KiSetClockDeadline(
    _In_ ULONGLONG InterruptTime)
{
    UNREFERENCED_PARAMETER(InterruptTime);
    return FALSE;
}
