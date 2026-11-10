/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/io/iomgr/iotimer.c
 * PURPOSE:         I/O Wrappers for Executive Timers
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

#define IOP_TIMER_SHARDS 8

typedef struct DECLSPEC_CACHEALIGN _IOP_TIMER_SHARD
{
    KSPIN_LOCK Lock;
    LIST_ENTRY Head;
    LONG Count;
} IOP_TIMER_SHARD, *PIOP_TIMER_SHARD;

/* Timer Database */
static IOP_TIMER_SHARD IopTimerShards[IOP_TIMER_SHARDS];

/* Timer Firing */
KDPC IopTimerDpc;
KTIMER IopTimer;

/* Keep count of how many timers we have */
static LONG IopTimerCount = 0;

/* PRIVATE FUNCTIONS *********************************************************/

static
PIOP_TIMER_SHARD
IopTimerShard(
    _In_ PIO_TIMER Timer)
{
    ULONG_PTR Value = (ULONG_PTR)Timer >> 6;

    return &IopTimerShards[(Value ^ (Value >> 5)) % IOP_TIMER_SHARDS];
}

VOID
FASTCALL
IopInitTimerImplementation(VOID)
{
    ULONG Index;

    for (Index = 0; Index < IOP_TIMER_SHARDS; Index++)
    {
        KeInitializeSpinLock(&IopTimerShards[Index].Lock);
        InitializeListHead(&IopTimerShards[Index].Head);
    }
}

VOID
NTAPI
IopTimerDispatch(IN PKDPC Dpc,
                 IN PVOID DeferredContext,
                 IN PVOID SystemArgument1,
                 IN PVOID SystemArgument2)
{
    KIRQL OldIrql;
    PLIST_ENTRY TimerEntry;
    PIO_TIMER Timer;
    PIOP_TIMER_SHARD Shard;
    ULONG Index, i;

    /* Check if any Timers are actually enabled as of now */
    if (!ReadNoFence(&IopTimerCount)) return;

    for (Index = 0; Index < IOP_TIMER_SHARDS; Index++)
    {
        Shard = &IopTimerShards[Index];
        if (!ReadNoFence(&Shard->Count)) continue;

        /* Lock the Timers */
        KeAcquireSpinLock(&Shard->Lock, &OldIrql);

        /* Call the Timer Routine of each enabled Timer */
        for (TimerEntry = Shard->Head.Flink, i = Shard->Count;
            (TimerEntry != &Shard->Head) && i;
            TimerEntry = TimerEntry->Flink)
        {
            /* Get the timer and check if it's enabled */
            Timer = CONTAINING_RECORD(TimerEntry, IO_TIMER, IoTimerList);
            if (Timer->TimerEnabled)
            {
                /* Call the timer routine */
                Timer->TimerRoutine(Timer->DeviceObject, Timer->Context);
                i--;
            }
        }

        /* Unlock the Timers */
        KeReleaseSpinLock(&Shard->Lock, OldIrql);
    }
}

VOID
NTAPI
IopRemoveTimerFromTimerList(IN PIO_TIMER Timer)
{
    PIOP_TIMER_SHARD Shard = IopTimerShard(Timer);
    KIRQL OldIrql;

    /* Lock Timers */
    KeAcquireSpinLock(&Shard->Lock, &OldIrql);

    /* Remove Timer from the List and Drop the Timer Count if Enabled */
    RemoveEntryList(&Timer->IoTimerList);
    if (Timer->TimerEnabled)
    {
        Shard->Count--;
        InterlockedDecrement(&IopTimerCount);
    }

    /* Unlock the Timers */
    KeReleaseSpinLock(&Shard->Lock, OldIrql);
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
NTSTATUS
NTAPI
IoInitializeTimer(IN PDEVICE_OBJECT DeviceObject,
                  IN PIO_TIMER_ROUTINE TimerRoutine,
                  IN PVOID Context)
{
    PIO_TIMER IoTimer = DeviceObject->Timer;
    PAGED_CODE();

    /* Check if we don't have a timer yet */
    if (!IoTimer)
    {
        /* Allocate Timer */
        IoTimer = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(IO_TIMER),
                                        TAG_IO_TIMER);
        if (!IoTimer) return STATUS_INSUFFICIENT_RESOURCES;

        /* Set up the Timer Structure */
        RtlZeroMemory(IoTimer, sizeof(IO_TIMER));
        IoTimer->Type = IO_TYPE_TIMER;
        IoTimer->DeviceObject = DeviceObject;
        DeviceObject->Timer = IoTimer;
    }

    /* Setup the timer routine and context */
    IoTimer->TimerRoutine = TimerRoutine;
    IoTimer->Context = Context;

    /* Add it to the Timer List */
    ExInterlockedInsertTailList(&IopTimerShard(IoTimer)->Head,
                                &IoTimer->IoTimerList,
                                &IopTimerShard(IoTimer)->Lock);

    /* Return Success */
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
NTAPI
IoStartTimer(IN PDEVICE_OBJECT DeviceObject)
{
    KIRQL OldIrql;
    PIO_TIMER IoTimer = DeviceObject->Timer;

    /* Make sure the device isn't unloading */
    if (!(IoGetDevObjExtension(DeviceObject)->ExtensionFlags &
         (DOE_UNLOAD_PENDING | DOE_DELETE_PENDING |
          DOE_REMOVE_PENDING | DOE_REMOVE_PROCESSED)))
    {
        /* Lock Timers */
        KeAcquireSpinLock(&IopTimerShard(IoTimer)->Lock, &OldIrql);

        /* Check if the timer isn't already enabled */
        if (!IoTimer->TimerEnabled)
        {
            /* Enable it and increase the timer count */
            IoTimer->TimerEnabled = TRUE;
            IopTimerShard(IoTimer)->Count++;
            InterlockedIncrement(&IopTimerCount);
        }

        /* Unlock Timers */
        KeReleaseSpinLock(&IopTimerShard(IoTimer)->Lock, OldIrql);
    }
}

/*
 * @implemented
 */
VOID
NTAPI
IoStopTimer(PDEVICE_OBJECT DeviceObject)
{
    KIRQL OldIrql;
    PIO_TIMER IoTimer = DeviceObject->Timer;

    /* Lock Timers */
    KeAcquireSpinLock(&IopTimerShard(IoTimer)->Lock, &OldIrql);

    /* Check if the timer is enabled */
    if (IoTimer->TimerEnabled)
    {
        /* Disable it and decrease the timer count */
        IoTimer->TimerEnabled = FALSE;
        IopTimerShard(IoTimer)->Count--;
        InterlockedDecrement(&IopTimerCount);
    }

    /* Unlock Timers */
    KeReleaseSpinLock(&IopTimerShard(IoTimer)->Lock, OldIrql);
}

/* EOF */
