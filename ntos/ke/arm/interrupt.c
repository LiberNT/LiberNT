/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 interrupt objects and interrupt dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/hal/halext.h>
#define NDEBUG
#include <debug.h>

#define KI_ARM_INTERRUPT_VECTORS 1020

/* Generic timer (ARM ARM B8): the virtual timer, which interrupt controllers present as PPI 27. */
#define KI_ARM_CLOCK_VECTOR 27
#define CNT_CTL_ENABLE  0x1
#define CNT_CTL_ISTATUS 0x4

static PKINTERRUPT KiArmInterruptChain[KI_ARM_INTERRUPT_VECTORS];
static KSPIN_LOCK KiArmInterruptLock;
static ULONG KiArmTimerReload;
static BOOLEAN KiArmSecondaryInterruptServices;

static
VOID
KiArmProgramTimer(VOID)
{
    KiArmWriteCp15(14, 0, 3, 0, KiArmTimerReload);
    KiArmWriteCp15(14, 0, 3, 1, CNT_CTL_ENABLE);
    KiArmIsb();
}

static
BOOLEAN
KiArmClockInterrupt(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KIRQL OldIrql)
{
    if (!(KiArmReadCp15(14, 0, 3, 1) & CNT_CTL_ISTATUS))
        return FALSE;

    KiArmProgramTimer();
    KeUpdateSystemTime(TrapFrame, KeMaximumIncrement, OldIrql);
    return TRUE;
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

    Head = KiArmInterruptChain[Vector];
    if (!Head)
        return;

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
            Head = KiArmInterruptChain[Vector];
            Irql = Head ? Head->Irql : DISPATCH_LEVEL;
        }

        ASSERT(Pcr->Prcb.NestingLevel != MAXUCHAR);
        Pcr->Prcb.NestingLevel++;
        if (HalBeginSystemInterrupt(Irql, Vector, &OldIrql))
        {
            Pcr->Prcb.InterruptCount++;
            if (Vector == HAL_ARM64_SGI_IPI)
                KiIpiProcessRequests();
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
    PKINTERRUPT Head;
    BOOLEAN Connected = FALSE;
    ULONG Vector;
    KIRQL OldIrql;

    if (Interrupt && Interrupt->ServiceRoutine && (Interrupt->Irql == PASSIVE_LEVEL))
        return KiConnectPassiveInterrupt(Interrupt);

    if (!Interrupt || !Interrupt->ServiceRoutine)
        return FALSE;

    Vector = Interrupt->Vector;
    if ((Vector < 16) || (Vector >= KI_ARM_INTERRUPT_VECTORS) ||
        (Interrupt->Irql <= DISPATCH_LEVEL) ||
        (Interrupt->SynchronizeIrql < Interrupt->Irql) ||
        (Interrupt->SynchronizeIrql > HIGH_LEVEL))
    {
        return FALSE;
    }

    KeAcquireSpinLock(&KiArmInterruptLock, &OldIrql);
    if (!Interrupt->Connected)
    {
        Head = KiArmInterruptChain[Vector];
        if (!Head)
        {
            InitializeListHead(&Interrupt->InterruptListEntry);
            HalArm64ConnectInterrupt(Vector);
            if (HalEnableSystemInterrupt(Vector, Interrupt->Irql, Interrupt->Mode))
            {
                Interrupt->Connected = TRUE;
                KiArmInterruptChain[Vector] = Interrupt;
            }
            else
            {
                HalArm64DisconnectInterrupt(Vector);
            }
        }
        else if (Interrupt->ShareVector && Head->ShareVector &&
                 (Interrupt->Mode == Head->Mode) && (Interrupt->Irql == Head->Irql))
        {
            InsertTailList(&Head->InterruptListEntry, &Interrupt->InterruptListEntry);
            Interrupt->Connected = TRUE;
        }
    }
    Connected = Interrupt->Connected;
    KeReleaseSpinLock(&KiArmInterruptLock, OldIrql);
    return Connected;
}

BOOLEAN
NTAPI
KeDisconnectInterrupt(
    _Inout_ PKINTERRUPT Interrupt)
{
    PKINTERRUPT Head;
    BOOLEAN Connected;
    ULONG Vector;
    KIRQL OldIrql;

    if (Interrupt && (Interrupt->Irql == PASSIVE_LEVEL))
        return KiDisconnectPassiveInterrupt(Interrupt);

    if (!Interrupt || (Interrupt->Vector >= KI_ARM_INTERRUPT_VECTORS))
        return FALSE;

    Vector = Interrupt->Vector;
    KeAcquireSpinLock(&KiArmInterruptLock, &OldIrql);
    Connected = Interrupt->Connected;
    Head = KiArmInterruptChain[Vector];
    if (Connected && Head)
    {
        if (IsListEmpty(&Head->InterruptListEntry))
        {
            ASSERT(Head == Interrupt);
            HalArm64DisconnectInterrupt(Vector);
            HalDisableSystemInterrupt(Vector, Interrupt->Irql);
            KiArmInterruptChain[Vector] = NULL;
        }
        else
        {
            if (Head == Interrupt)
            {
                KiArmInterruptChain[Vector] = CONTAINING_RECORD(Interrupt->InterruptListEntry.Flink,
                                                                KINTERRUPT, InterruptListEntry);
            }
            RemoveEntryList(&Interrupt->InterruptListEntry);
        }
        InitializeListHead(&Interrupt->InterruptListEntry);
        Interrupt->Connected = FALSE;
    }
    KeReleaseSpinLock(&KiArmInterruptLock, OldIrql);
    return Connected;
}

/* Start the periodic clock of the current processor, at the increment the HAL set. */
VOID
NTAPI
KiArmStartClock(VOID)
{
    ULONG Frequency = KiArmReadCp15(14, 0, 0, 0);

    ASSERT(KiArmHalInitialized);
    ASSERT(KeMaximumIncrement != 0);
    if (!Frequency)
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x41524D03, 0, 0, 0);

    KiArmTimerReload = (ULONG)(((ULONGLONG)Frequency * KeMaximumIncrement) / 10000000ULL);
    ASSERT(KiArmTimerReload != 0);
    if (!HalEnableSystemInterrupt(KI_ARM_CLOCK_VECTOR, CLOCK_LEVEL, LevelSensitive))
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x41524D04, KI_ARM_CLOCK_VECTOR, 0, 0);
    KiArmProgramTimer();
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
    Connected = KiArmInterruptChain[Vector] != NULL;
    if (Connected)
        KiArmServiceInterruptVector(Vector, NULL);
    if (Raised)
        KfLowerIrql(OldIrql);
    return Connected;
}
