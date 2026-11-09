/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stopping the machine with everything written and everybody told
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY PopShutdownQueue;
KGUARDED_MUTEX PopShutdownListMutex;
BOOLEAN PopShutdownListAvailable = FALSE;
PPOP_SHUTDOWN_WAIT_ENTRY PopShutdownThreadList = NULL;
ULONG PopShutdownPowerOffPolicy = 0;
KEVENT PopShutdownEvent;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Runs what asked to run while the machine is stopping, in the order it was
 * asked, and lets go of the list so that nothing new can be added.
 */
VOID
NTAPI
PopProcessShutDownLists(VOID)
{
    PPOP_SHUTDOWN_WAIT_ENTRY Waiting;
    PPOP_SHUTDOWN_WAIT_ENTRY Next;
    PWORK_QUEUE_ITEM WorkItem;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    KeAcquireGuardedMutex(&PopShutdownListMutex);
    PopShutdownListAvailable = FALSE;
    Waiting = PopShutdownThreadList;
    PopShutdownThreadList = NULL;
    KeReleaseGuardedMutex(&PopShutdownListMutex);

    /* Whoever asked to be woken when the machine stops is woken now */
    while (Waiting != NULL)
    {
        Next = Waiting->NextEntry;

        KeSetEvent(&PopShutdownEvent, IO_NO_INCREMENT, FALSE);

        if (Waiting->Thread != NULL) ObDereferenceObject(Waiting->Thread);

        ExFreePoolWithTag(Waiting, 'wPoP');
        Waiting = Next;
    }

    while (!IsListEmpty(&PopShutdownQueue))
    {
        Entry = RemoveHeadList(&PopShutdownQueue);
        WorkItem = CONTAINING_RECORD(Entry, WORK_QUEUE_ITEM, List);

        WorkItem->WorkerRoutine(WorkItem->Parameter);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
PopInitShutdownList(VOID)
{
    PAGED_CODE();

    InitializeListHead(&PopShutdownQueue);
    KeInitializeGuardedMutex(&PopShutdownListMutex);
    KeInitializeEvent(&PopShutdownEvent, NotificationEvent, FALSE);

    PopShutdownListAvailable = TRUE;
}

/* What the machine is to do once it has stopped is read from the registry */
VOID
NTAPI
PopReadShutdownPolicy(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"PowerdownAfterShutdown");
    PKEY_VALUE_PARTIAL_INFORMATION Told;
    UCHAR Room[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 8];
    HANDLE KeyHandle;
    ULONG Length;
    NTSTATUS Status;

    PAGED_CODE();

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return;

    Told = (PKEY_VALUE_PARTIAL_INFORMATION)Room;

    Status = ZwQueryValueKey(KeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             Told,
                             sizeof(Room),
                             &Length);
    if (NT_SUCCESS(Status) && Told->Type == REG_SZ && Told->DataLength >= sizeof(WCHAR))
    {
        /* The value is written as a character, so one is what it says */
        PopShutdownPowerOffPolicy = (*(PWCHAR)Told->Data == L'1') ? 1 : 0;
    }

    ZwClose(KeyHandle);
}

/*
 * Asks to run while the machine is stopping. What is asked after the lists
 * have been closed is refused, because nothing would run it.
 */
NTSTATUS
NTAPI
PoQueueShutdownWorkItem(
    _Inout_ PWORK_QUEUE_ITEM WorkItem)
{
    NTSTATUS Status = STATUS_SYSTEM_SHUTDOWN;

    PAGED_CODE();

    KeAcquireGuardedMutex(&PopShutdownListMutex);

    if (PopShutdownListAvailable)
    {
        InsertTailList(&PopShutdownQueue, &WorkItem->List);
        Status = STATUS_SUCCESS;
    }

    KeReleaseGuardedMutex(&PopShutdownListMutex);

    return Status;
}

NTSTATUS
NTAPI
PoRequestShutdownEvent(
    _Out_ PVOID *Event)
{
    NTSTATUS Status;

    PAGED_CODE();

    if (Event != NULL) *Event = NULL;

    Status = PoRequestShutdownWait(PsGetCurrentThread());
    if (!NT_SUCCESS(Status)) return Status;

    if (Event != NULL)
    {
        ObReferenceObject(&PopShutdownEvent);
        *Event = &PopShutdownEvent;
    }

    return STATUS_SUCCESS;
}

/* Asks to be woken when the machine stops, rather than to run something */
NTSTATUS
NTAPI
PoRequestShutdownWait(
    _In_ PETHREAD Thread)
{
    PPOP_SHUTDOWN_WAIT_ENTRY Waiting;
    NTSTATUS Status = STATUS_SYSTEM_SHUTDOWN;

    PAGED_CODE();

    Waiting = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Waiting), 'wPoP');
    if (Waiting == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    ObReferenceObject(Thread);
    Waiting->Thread = Thread;

    KeAcquireGuardedMutex(&PopShutdownListMutex);

    if (PopShutdownListAvailable)
    {
        Waiting->NextEntry = PopShutdownThreadList;
        PopShutdownThreadList = Waiting;
        Status = STATUS_SUCCESS;
    }

    KeReleaseGuardedMutex(&PopShutdownListMutex);

    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Thread);
        ExFreePoolWithTag(Waiting, 'wPoP');
    }

    return Status;
}

/*
 * Stops the machine with everything in order: what asked to run runs, the
 * volumes are written out, the devices are told, and then it is over.
 */
VOID
NTAPI
PopGracefulShutdown(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    PAGED_CODE();

    PopProcessShutDownLists();

    /* TODO: the processes still running are not asked to stop first */

    PopFlushVolumes(TRUE);

    PopSetSystemPowerState(PowerSystemShutdown, PopShutdownPowerOffPolicy
                                                    ? PowerActionShutdownOff
                                                    : PowerActionShutdown);

    HalReturnToFirmware(PopShutdownPowerOffPolicy ? HalPowerDownRoutine
                                                  : HalHaltRoutine);
}

/*
 * Stopping the machine because something went wrong: there is no asking
 * anybody anything, only writing out what can still be written.
 */
VOID
NTAPI
PoShutdownBugCheck(
    _In_ BOOLEAN PowerOff,
    _In_ ULONG BugCheckCode,
    _In_ ULONG_PTR BugCheckParameter1,
    _In_ ULONG_PTR BugCheckParameter2,
    _In_ ULONG_PTR BugCheckParameter3,
    _In_ ULONG_PTR BugCheckParameter4)
{
    /* TODO: the devices are not told the machine is going down this way */
    UNREFERENCED_PARAMETER(PowerOff);

    KeBugCheckEx(BugCheckCode,
                 BugCheckParameter1,
                 BugCheckParameter2,
                 BugCheckParameter3,
                 BugCheckParameter4);
}
