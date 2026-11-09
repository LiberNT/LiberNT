/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The one-second timer a device object can ask to be called on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

KSPIN_LOCK IopTimerLock;
LIST_ENTRY IopTimerQueueHead;
KDPC IopTimerDpc;
KTIMER IopTimer;
ULONG IopTimerCount = 0;

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
VOID
FASTCALL
IopInitTimerImplementation(VOID)
{
    LARGE_INTEGER DueTime;

    KeInitializeSpinLock(&IopTimerLock);
    InitializeListHead(&IopTimerQueueHead);

    KeInitializeDpc(&IopTimerDpc, IopTimerDispatch, NULL);
    KeInitializeTimerEx(&IopTimer, SynchronizationTimer);

    DueTime.QuadPart = -10 * 1000 * 1000;

    KeSetTimerEx(&IopTimer, DueTime, 1000, &IopTimerDpc);
}

VOID
NTAPI
IopTimerDispatch(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PLIST_ENTRY Entry;
    PIO_TIMER Timer;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (IopTimerCount == 0) return;

    KeAcquireSpinLock(&IopTimerLock, &OldIrql);

    Entry = IopTimerQueueHead.Flink;
    while (Entry != &IopTimerQueueHead)
    {
        Timer = CONTAINING_RECORD(Entry, IO_TIMER, IoTimerList);
        Entry = Entry->Flink;

        if (!Timer->TimerEnabled) continue;

        Timer->TimerRoutine(Timer->DeviceObject, Timer->Context);
    }

    KeReleaseSpinLock(&IopTimerLock, OldIrql);
}

VOID
NTAPI
IopRemoveTimerFromTimerList(
    IN PIO_TIMER Timer)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopTimerLock, &OldIrql);

    if (Timer->TimerEnabled)
    {
        Timer->TimerEnabled = FALSE;
        IopTimerCount--;
    }

    RemoveEntryList(&Timer->IoTimerList);

    KeReleaseSpinLock(&IopTimerLock, OldIrql);
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoInitializeTimer(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIO_TIMER_ROUTINE TimerRoutine,
    _In_opt_ PVOID Context)
{
    PIO_TIMER Timer;
    KIRQL OldIrql;

    Timer = DeviceObject->Timer;

    if (Timer == NULL)
    {
        Timer = ExAllocatePoolZero(NonPagedPool,
                                   sizeof(IO_TIMER),
                                   TAG_IO_TIMER);
        if (Timer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Timer->Type = IO_TYPE_TIMER;
        Timer->DeviceObject = DeviceObject;
        DeviceObject->Timer = Timer;

        KeAcquireSpinLock(&IopTimerLock, &OldIrql);
        InsertTailList(&IopTimerQueueHead, &Timer->IoTimerList);
        KeReleaseSpinLock(&IopTimerLock, OldIrql);
    }

    Timer->TimerEnabled = FALSE;
    Timer->TimerRoutine = TimerRoutine;
    Timer->Context = Context;

    return STATUS_SUCCESS;
}

VOID
NTAPI
IoStartTimer(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PIO_TIMER Timer = DeviceObject->Timer;
    KIRQL OldIrql;

    if (Timer == NULL) return;

    KeAcquireSpinLock(&IopTimerLock, &OldIrql);

    if (!Timer->TimerEnabled)
    {
        Timer->TimerEnabled = TRUE;
        IopTimerCount++;
    }

    KeReleaseSpinLock(&IopTimerLock, OldIrql);
}

VOID
NTAPI
IoStopTimer(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PIO_TIMER Timer = DeviceObject->Timer;
    KIRQL OldIrql;

    if (Timer == NULL) return;

    KeAcquireSpinLock(&IopTimerLock, &OldIrql);

    if (Timer->TimerEnabled)
    {
        Timer->TimerEnabled = FALSE;
        IopTimerCount--;
    }

    KeReleaseSpinLock(&IopTimerLock, OldIrql);
}
