/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 interrupt objects and interrupt dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KI_ARM_INTERRUPT_VECTORS 1020

static PKINTERRUPT KiArmInterruptChain[KI_ARM_INTERRUPT_VECTORS];
static KSPIN_LOCK KiArmInterruptLock;

/* Run every interrupt object connected to a vector; the IRQL is already raised. */
VOID
NTAPI
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

    TrapFrame->PreviousIrql = Pcr->CurrentIrql;
    while (KiArmDispatchDeviceInterrupt(TrapFrame))
        NOTHING;

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
            if (KiArmEnableInterruptLine(Vector, Interrupt->Irql, Interrupt->Mode))
            {
                Interrupt->Connected = TRUE;
                KiArmInterruptChain[Vector] = Interrupt;
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
            KiArmDisableInterruptLine(Vector);
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
