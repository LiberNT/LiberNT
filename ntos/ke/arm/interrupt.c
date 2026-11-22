/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 interrupt objects and interrupt dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/hal/halext.h>
#include <reactos/arm64/sysreg.h>
#define NDEBUG
#include <debug.h>

#define KI_ARM_INTERRUPT_VECTORS 1020

/* Generic timer (ARM ARM B8): the virtual timer, which interrupt controllers present as PPI 27. */
#define KI_ARM_CLOCK_VECTOR 27
#define CNT_CTL_ENABLE  0x1
#define CNT_CTL_ISTATUS 0x4

#define KI_ARM_INTERRUPT_TABLE_TAG 'tnIK'

/*
 * An interrupt object belongs to one processor, so every processor has its own
 * table of chains. A table is published once and never freed; a chain is
 * changed on its processor with interrupts masked, where its dispatcher cannot
 * be walking it.
 */
static PKINTERRUPT *KiArmInterruptTable[MAXIMUM_PROCESSORS];
static USHORT KiArmInterruptConnections[KI_ARM_INTERRUPT_VECTORS];
static KSPIN_LOCK KiArmInterruptLock;
static ULONG KiArmTimerReload;
static ULONGLONG KiArmClockDeadline[MAXIMUM_PROCESSORS];
static BOOLEAN KiArmSecondaryInterruptServices;

static
VOID
KiArmProgramTimer(
    _In_ ULONGLONG Deadline)
{
    ARM64_WRITE_SYSREG(CNTV_CVAL_EL0, Deadline);
    ARM64_WRITE_SYSREG(CNTV_CTL_EL0, CNT_CTL_ENABLE);
    KiArmIsb();
}

static
BOOLEAN
KiArmClockInterrupt(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KIRQL OldIrql)
{
    ULONG Number = KeGetCurrentProcessorNumber();
    ULONGLONG Now, Deadline, Periods;

    if (!(KiArmReadCp15(14, 0, 3, 1) & CNT_CTL_ISTATUS))
        return FALSE;

    ASSERT(Number < MAXIMUM_PROCESSORS);
    ASSERT(KiArmTimerReload != 0);

    /* The deadlines stay on the counter's own grid, so a late interrupt loses no time: it reports every period that passed. */
    Now = ARM64_READ_SYSREG(CNTVCT_EL0);
    Deadline = KiArmClockDeadline[Number];
    ASSERT(Now >= Deadline);
    Periods = min((Now - Deadline) / KiArmTimerReload + 1, MAXULONG / KeMaximumIncrement);
    Deadline += Periods * KiArmTimerReload;
    if (Deadline <= Now)
        Deadline = Now + KiArmTimerReload;
    KiArmClockDeadline[Number] = Deadline;
    KiArmProgramTimer(Deadline);

    if (Number == 0)
        KeUpdateSystemTime(TrapFrame, (ULONG)Periods * KeMaximumIncrement, OldIrql);
    else
        KeUpdateRunTime(TrapFrame, OldIrql);
    return TRUE;
}

FORCEINLINE
PKINTERRUPT
KiArmInterruptHead(
    _In_ ULONG Vector)
{
    PKINTERRUPT *Table = KiArmInterruptTable[KeGetCurrentProcessorNumber()];

    ASSERT(Vector < KI_ARM_INTERRUPT_VECTORS);
    return Table ? Table[Vector] : NULL;
}

/* Run every interrupt object connected to a vector; the IRQL is already raised. */
static
VOID
KiArmServiceInterruptVector(
    _In_ ULONG Vector,
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKINTERRUPT Head, Interrupt;
    PLIST_ENTRY Entry;
    BOOLEAN Handled;

    UNREFERENCED_PARAMETER(TrapFrame);

    if (Vector >= KI_ARM_INTERRUPT_VECTORS)
        return;

    Head = KiArmInterruptHead(Vector);
    if (!Head)
        return;

    ASSERT(KeGetCurrentIrql() > DISPATCH_LEVEL);

    /* A handler runs at its own level: what outranks it, an interprocessor request included, is taken while it runs or waits for its lock. */
    _enable();
    Interrupt = Head;
    Entry = &Head->InterruptListEntry;
    do
    {
        KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);
        Handled = Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
        KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);
        Interrupt->ServiceCount++;
        if (Handled && (Interrupt->Mode == Latched))
            break;
        Entry = Entry->Flink;
        Interrupt = CONTAINING_RECORD(Entry, KINTERRUPT, InterruptListEntry);
    } while (Entry != &Head->InterruptListEntry);
    _disable();
}

/* IRQ exception: device interrupts, then the software requests they left. */
VOID
NTAPI
KiArmInterruptDispatch(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    PKINTERRUPT Head;
    ULONG Vector;
    KIRQL Irql, OldIrql;

    TrapFrame->PreviousIrql = Pcr->CurrentIrql;

    Vector = HalGetInterruptSource();
    if (Vector < KI_ARM_INTERRUPT_VECTORS)
    {
        if (Vector == HAL_ARM64_SGI_APC)
        {
            Irql = APC_LEVEL;
        }
        else if (Vector == HAL_ARM64_SGI_DPC)
        {
            Irql = DISPATCH_LEVEL;
        }
        else if (Vector == HAL_ARM64_SGI_IPI)
        {
            Irql = IPI_LEVEL;
        }
        else if (Vector == KI_ARM_CLOCK_VECTOR)
        {
            Irql = CLOCK_LEVEL;
        }
        else
        {
            Head = KiArmInterruptHead(Vector);
            Irql = Head ? Head->Irql : DISPATCH_LEVEL;
        }

        ASSERT(Pcr->Prcb.NestingLevel != MAXUCHAR);
        Pcr->Prcb.NestingLevel++;
        if (HalBeginSystemInterrupt(Irql, Vector, &OldIrql))
        {
            Pcr->Prcb.InterruptCount++;
            if (Vector == HAL_ARM64_SGI_IPI)
                KiArmServiceIpi(TrapFrame);
            else if ((Vector >= 16) && ((Vector != KI_ARM_CLOCK_VECTOR) || !KiArmClockInterrupt(TrapFrame, OldIrql)))
                KiArmServiceInterruptVector(Vector, TrapFrame);
            HalEndSystemInterrupt(OldIrql, TrapFrame);
        }
        ASSERT(Pcr->Prcb.NestingLevel != 0);
        Pcr->Prcb.NestingLevel--;
    }

    /* The APC and DPC interrupts only mark their request; it is delivered here, on the thread's own stack. */
    if ((Pcr->CurrentIrql < DISPATCH_LEVEL) && Pcr->SoftwareInterruptPending)
        KiArmDispatchSoftwareInterrupts(TrapFrame);
}

BOOLEAN
NTAPI
KeConnectInterrupt(
    _Inout_ PKINTERRUPT Interrupt)
{
    PKINTERRUPT *Table, *NewTable = NULL;
    PKINTERRUPT Head;
    KAFFINITY PreviousAffinity = 0;
    BOOLEAN Connected, Enabled, RestoreAffinity = FALSE;
    ULONG Vector, Cpu;
    KIRQL OldIrql;

    if (!Interrupt || !Interrupt->ServiceRoutine || (Interrupt->Vector < 16))
        return FALSE;

    if (Interrupt->Irql == PASSIVE_LEVEL)
        return KiConnectPassiveInterrupt(Interrupt);

    Vector = Interrupt->Vector;
    Cpu = (UCHAR)Interrupt->Number;
    if ((Vector >= KI_ARM_INTERRUPT_VECTORS) || (Cpu >= MAXIMUM_PROCESSORS) ||
        (Interrupt->Irql <= DISPATCH_LEVEL) ||
        (Interrupt->SynchronizeIrql < Interrupt->Irql) ||
        (Interrupt->SynchronizeIrql > HIGH_LEVEL))
    {
        return FALSE;
    }

    if (!KiArmInterruptTable[Cpu])
    {
        NewTable = ExAllocatePoolZero(NonPagedPool,
                                      KI_ARM_INTERRUPT_VECTORS * sizeof(PKINTERRUPT),
                                      KI_ARM_INTERRUPT_TABLE_TAG);
        if (!NewTable)
            return FALSE;
    }

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Cpu);
        RestoreAffinity = TRUE;
    }
    else if (Cpu != KeGetCurrentProcessorNumber())
    {
        if (NewTable)
            ExFreePoolWithTag(NewTable, KI_ARM_INTERRUPT_TABLE_TAG);
        return FALSE;
    }

    KeAcquireSpinLock(&KiArmInterruptLock, &OldIrql);
    ASSERT(KeGetCurrentProcessorNumber() == Cpu);
    if (!KiArmInterruptTable[Cpu])
    {
        KeMemoryBarrier();
        KiArmInterruptTable[Cpu] = NewTable;
        NewTable = NULL;
    }

    Table = KiArmInterruptTable[Cpu];
    if (!Interrupt->Connected)
    {
        Head = Table[Vector];
        if (!Head)
        {
            InitializeListHead(&Interrupt->InterruptListEntry);
            HalArm64ConnectInterrupt(Vector);
            if (((Vector >= 32) && (KiArmInterruptConnections[Vector] != 0)) ||
                HalEnableSystemInterrupt(Vector, Interrupt->Irql, Interrupt->Mode))
            {
                ASSERT(KiArmInterruptConnections[Vector] != MAXUSHORT);
                KiArmInterruptConnections[Vector]++;
                Interrupt->Connected = TRUE;
                KeMemoryBarrier();
                Table[Vector] = Interrupt;
            }
            else
            {
                HalArm64DisconnectInterrupt(Vector);
            }
        }
        else if (Interrupt->ShareVector && Head->ShareVector &&
                 (Interrupt->Mode == Head->Mode) && (Interrupt->Irql == Head->Irql))
        {
            Enabled = KeDisableInterrupts();
            InsertTailList(&Head->InterruptListEntry, &Interrupt->InterruptListEntry);
            Interrupt->Connected = TRUE;
            KeRestoreInterrupts(Enabled);
        }
    }
    Connected = Interrupt->Connected;
    KeReleaseSpinLock(&KiArmInterruptLock, OldIrql);

    if (RestoreAffinity)
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
    if (NewTable)
        ExFreePoolWithTag(NewTable, KI_ARM_INTERRUPT_TABLE_TAG);
    return Connected;
}

BOOLEAN
NTAPI
KeDisconnectInterrupt(
    _Inout_ PKINTERRUPT Interrupt)
{
    PKINTERRUPT *Table;
    PKINTERRUPT Head;
    KAFFINITY PreviousAffinity = 0;
    BOOLEAN Connected, Enabled, RestoreAffinity = FALSE;
    ULONG Vector, Cpu;
    KIRQL OldIrql;

    if (Interrupt && (Interrupt->Irql == PASSIVE_LEVEL))
        return KiDisconnectPassiveInterrupt(Interrupt);

    if (!Interrupt || (Interrupt->Vector >= KI_ARM_INTERRUPT_VECTORS))
        return FALSE;

    Vector = Interrupt->Vector;
    Cpu = (UCHAR)Interrupt->Number;
    if ((Cpu >= MAXIMUM_PROCESSORS) || !KiArmInterruptTable[Cpu])
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

    KeAcquireSpinLock(&KiArmInterruptLock, &OldIrql);
    ASSERT(KeGetCurrentProcessorNumber() == Cpu);
    Table = KiArmInterruptTable[Cpu];
    Connected = Interrupt->Connected;
    Head = Table[Vector];
    if (Connected && Head)
    {
        Enabled = KeDisableInterrupts();
        if (IsListEmpty(&Head->InterruptListEntry))
        {
            ASSERT(Head == Interrupt);
            ASSERT(KiArmInterruptConnections[Vector] != 0);
            Table[Vector] = NULL;
            KiArmInterruptConnections[Vector]--;
            HalArm64DisconnectInterrupt(Vector);
            if ((Vector < 32) || (KiArmInterruptConnections[Vector] == 0))
                HalDisableSystemInterrupt(Vector, Interrupt->Irql);
        }
        else
        {
            if (Head == Interrupt)
            {
                Table[Vector] = CONTAINING_RECORD(Interrupt->InterruptListEntry.Flink,
                                                  KINTERRUPT, InterruptListEntry);
            }
            RemoveEntryList(&Interrupt->InterruptListEntry);
        }
        InitializeListHead(&Interrupt->InterruptListEntry);
        Interrupt->Connected = FALSE;
        KeRestoreInterrupts(Enabled);
    }
    KeReleaseSpinLock(&KiArmInterruptLock, OldIrql);

    if (RestoreAffinity)
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
    return Connected;
}

/* Start the periodic clock of the current processor, at the increment the HAL set. */
VOID
NTAPI
KiArmStartClock(VOID)
{
    ULONG Frequency = KiArmReadCp15(14, 0, 0, 0);
    ULONGLONG Deadline;

    ASSERT(KiArmHalInitialized);
    ASSERT(KeMaximumIncrement != 0);
    if (!Frequency)
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x41524D03, 0, 0, 0);

    KiArmTimerReload = (ULONG)(((ULONGLONG)Frequency * KeMaximumIncrement) / 10000000ULL);
    ASSERT(KiArmTimerReload != 0);
    if (!HalEnableSystemInterrupt(KI_ARM_CLOCK_VECTOR, CLOCK_LEVEL, LevelSensitive))
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x41524D04, KI_ARM_CLOCK_VECTOR, 0, 0);
    Deadline = ARM64_READ_SYSREG(CNTVCT_EL0) + KiArmTimerReload;
    KiArmClockDeadline[KeGetCurrentProcessorNumber()] = Deadline;
    KiArmProgramTimer(Deadline);
}

/* HAL phase 1 moved the interrupt controller to its kernel mappings: IRQL now reaches it, and interrupts open. */
VOID
NTAPI
KiArmPostHalInitSystemPhase1(VOID)
{
    ASSERT(KeGetCurrentProcessorNumber() == 0);
    ASSERT(!KiArmHalInitialized);

    KiArmHalInitialized = TRUE;
    HalSetGicPriorityMask(KeGetCurrentIrql());
    KiArmStartClock();
    _enable();
}

NTSTATUS
NTAPI
KeInitializeSecondaryInterruptServices(VOID)
{
    KiArmSecondaryInterruptServices = TRUE;
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
KeDispatchSecondaryInterrupt(
    _In_ ULONG Vector,
    _In_ ULONG_PTR Flags,
    _In_opt_ PVOID Reserved)
{
    KIRQL OldIrql = PASSIVE_LEVEL;
    BOOLEAN Raised = FALSE, Connected, IsLevel;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Reserved);

    if (!KiArmSecondaryInterruptServices || (Vector >= KI_ARM_INTERRUPT_VECTORS))
        return FALSE;

    if (KiDispatchPassiveInterrupt(Vector, &IsLevel))
        return !IsLevel;

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        OldIrql = KfRaiseIrql(DISPATCH_LEVEL);
        Raised = TRUE;
    }
    Connected = KiArmInterruptHead(Vector) != NULL;
    if (Connected)
        KiArmServiceInterruptVector(Vector, NULL);
    if (Raised)
        KfLowerIrql(OldIrql);
    return Connected;
}
