/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Passive-level interrupt service routines for secondary interrupts
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

#define TAG_PASSIVE_INTERRUPT 'iPeK'

typedef struct _KI_PASSIVE_INTERRUPT
{
    LIST_ENTRY Link;
    PKINTERRUPT Interrupt;
    KDPC Dpc;
    WORK_QUEUE_ITEM WorkItem;
    KEVENT Lock;
    EX_RUNDOWN_REF Rundown;
    volatile LONG Pending;
    volatile LONG Queued;
    BOOLEAN Disconnecting;
} KI_PASSIVE_INTERRUPT, *PKI_PASSIVE_INTERRUPT;

static LIST_ENTRY KiPassiveInterruptList = {&KiPassiveInterruptList, &KiPassiveInterruptList};
static KSPIN_LOCK KiPassiveInterruptLock;

static
KIRQL
KiAcquirePassiveInterruptList(VOID)
{
    KIRQL OldIrql = KfRaiseIrql(HIGH_LEVEL);

    KeAcquireSpinLockAtDpcLevel(&KiPassiveInterruptLock);
    return OldIrql;
}

static
VOID
KiReleasePassiveInterruptList(
    _In_ KIRQL OldIrql)
{
    KeReleaseSpinLockFromDpcLevel(&KiPassiveInterruptLock);
    KeLowerIrql(OldIrql);
}

static
PKI_PASSIVE_INTERRUPT
KiFindPassiveInterrupt(
    _In_opt_ PKINTERRUPT Interrupt,
    _In_ ULONG Vector)
{
    PLIST_ENTRY Entry;

    for (Entry = KiPassiveInterruptList.Flink; Entry != &KiPassiveInterruptList; Entry = Entry->Flink)
    {
        PKI_PASSIVE_INTERRUPT Passive = CONTAINING_RECORD(Entry, KI_PASSIVE_INTERRUPT, Link);

        if (Interrupt != NULL ? Passive->Interrupt == Interrupt : Passive->Interrupt->Vector == Vector)
            return Passive;
    }
    return NULL;
}

static
VOID
NTAPI
KiPassiveInterruptWorker(
    _In_ PVOID Context)
{
    PKI_PASSIVE_INTERRUPT Passive = Context;
    PKINTERRUPT Interrupt = Passive->Interrupt;

    do
    {
        KeWaitForSingleObject(&Passive->Lock, Executive, KernelMode, FALSE, NULL);
        while (InterlockedExchange(&Passive->Pending, 0) != 0 && !Passive->Disconnecting)
        {
            Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
            if (Interrupt->Mode == LevelSensitive)
                HalEnableSystemInterrupt(Interrupt->Vector, PASSIVE_LEVEL, LevelSensitive);
        }
        KeSetEvent(&Passive->Lock, IO_NO_INCREMENT, FALSE);
        InterlockedExchange(&Passive->Queued, 0);
        KeMemoryBarrier();
    } while (Passive->Pending != 0 && InterlockedCompareExchange(&Passive->Queued, 1, 0) == 0);

    ExReleaseRundownProtection(&Passive->Rundown);
}

static
VOID
NTAPI
KiPassiveInterruptDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PKI_PASSIVE_INTERRUPT Passive = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (InterlockedCompareExchange(&Passive->Queued, 1, 0) != 0)
        return;
    if (!ExAcquireRundownProtection(&Passive->Rundown))
    {
        InterlockedExchange(&Passive->Queued, 0);
        return;
    }
    ExQueueWorkItem(&Passive->WorkItem, CriticalWorkQueue);
}

BOOLEAN
KiConnectPassiveInterrupt(
    _Inout_ PKINTERRUPT Interrupt)
{
    PKI_PASSIVE_INTERRUPT Passive;
    KIRQL OldIrql;

    if (Interrupt->Connected)
        return TRUE;

    Passive = ExAllocatePoolZero(NonPagedPool, sizeof(*Passive), TAG_PASSIVE_INTERRUPT);
    if (Passive == NULL)
        return FALSE;
    Passive->Interrupt = Interrupt;
    KeInitializeDpc(&Passive->Dpc, KiPassiveInterruptDpc, Passive);
    ExInitializeWorkItem(&Passive->WorkItem, KiPassiveInterruptWorker, Passive);
    KeInitializeEvent(&Passive->Lock, SynchronizationEvent, TRUE);
    ExInitializeRundownProtection(&Passive->Rundown);

    OldIrql = KiAcquirePassiveInterruptList();
    InsertTailList(&KiPassiveInterruptList, &Passive->Link);
    KiReleasePassiveInterruptList(OldIrql);

    if (!HalEnableSystemInterrupt(Interrupt->Vector, PASSIVE_LEVEL, Interrupt->Mode))
    {
        OldIrql = KiAcquirePassiveInterruptList();
        RemoveEntryList(&Passive->Link);
        KiReleasePassiveInterruptList(OldIrql);
        ExFreePoolWithTag(Passive, TAG_PASSIVE_INTERRUPT);
        return FALSE;
    }

    Interrupt->Connected = TRUE;
    return TRUE;
}

BOOLEAN
KiDisconnectPassiveInterrupt(
    _Inout_ PKINTERRUPT Interrupt)
{
    PKI_PASSIVE_INTERRUPT Passive;
    KIRQL OldIrql;

    OldIrql = KiAcquirePassiveInterruptList();
    Passive = KiFindPassiveInterrupt(Interrupt, 0);
    KiReleasePassiveInterruptList(OldIrql);

    if (Passive == NULL)
        return TRUE;

    KeWaitForSingleObject(&Passive->Lock, Executive, KernelMode, FALSE, NULL);
    Passive->Disconnecting = TRUE;
    KeSetEvent(&Passive->Lock, IO_NO_INCREMENT, FALSE);

    HalDisableSystemInterrupt(Interrupt->Vector, PASSIVE_LEVEL);

    OldIrql = KiAcquirePassiveInterruptList();
    RemoveEntryList(&Passive->Link);
    KiReleasePassiveInterruptList(OldIrql);

    KeRemoveQueueDpc(&Passive->Dpc);
    KeFlushQueuedDpcs();
    ExWaitForRundownProtectionRelease(&Passive->Rundown);

    ExFreePoolWithTag(Passive, TAG_PASSIVE_INTERRUPT);
    Interrupt->Connected = FALSE;
    return TRUE;
}

BOOLEAN
KiSynchronizePassiveInterrupt(
    _Inout_ PKINTERRUPT Interrupt,
    _In_ PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
    _In_opt_ PVOID SynchronizeContext)
{
    PKI_PASSIVE_INTERRUPT Passive;
    BOOLEAN Result;
    KIRQL OldIrql;

    OldIrql = KiAcquirePassiveInterruptList();
    Passive = KiFindPassiveInterrupt(Interrupt, 0);
    KiReleasePassiveInterruptList(OldIrql);

    if (Passive == NULL)
        return SynchronizeRoutine(SynchronizeContext);

    KeWaitForSingleObject(&Passive->Lock, Executive, KernelMode, FALSE, NULL);
    Result = SynchronizeRoutine(SynchronizeContext);
    KeSetEvent(&Passive->Lock, IO_NO_INCREMENT, FALSE);
    return Result;
}

BOOLEAN
KiDispatchPassiveInterrupt(
    _In_ ULONG Vector,
    _Out_opt_ PBOOLEAN IsLevel)
{
    PKI_PASSIVE_INTERRUPT Passive;
    KIRQL OldIrql;

    OldIrql = KiAcquirePassiveInterruptList();
    Passive = KiFindPassiveInterrupt(NULL, Vector);
    if (Passive != NULL)
    {
        if (IsLevel != NULL)
            *IsLevel = (Passive->Interrupt->Mode == LevelSensitive);
        InterlockedExchange(&Passive->Pending, 1);
        KeInsertQueueDpc(&Passive->Dpc, NULL, NULL);
    }
    KiReleasePassiveInterruptList(OldIrql);
    return Passive != NULL;
}
