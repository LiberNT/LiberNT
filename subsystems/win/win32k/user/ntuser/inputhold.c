/*
 * PROJECT:     LiberNT Win32k subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device files of the raw input thread that close for their device's removal
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserInput);

static const GUID InputHoldQueryRemove =
    { 0xCB3A4006, 0x46F0, 0x11D0, { 0xB0, 0x8F, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3F } };
static const GUID InputHoldRemoveCancelled =
    { 0xCB3A4007, 0x46F0, 0x11D0, { 0xB0, 0x8F, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3F } };
static const GUID InputHoldRemoveComplete =
    { 0xCB3A4008, 0x46F0, 0x11D0, { 0xB0, 0x8F, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3F } };

/* A registration for target device change on a held file; queued requests and the registration hold it */
typedef struct _INPUT_HOLD_TARGET
{
    LIST_ENTRY ListEntry;
    PINPUT_HOLD Hold;
    PINPUT_HOLD_FILE File;
    PVOID Owner;
    PVOID Notification;
    volatile LONG References;
    BOOLEAN Retired;
    UNICODE_STRING Path;
} INPUT_HOLD_TARGET, *PINPUT_HOLD_TARGET;

typedef struct _INPUT_HOLD_REQUEST
{
    LIST_ENTRY ListEntry;
    BOOLEAN Reopen;
    PINPUT_HOLD_TARGET Target;
} INPUT_HOLD_REQUEST, *PINPUT_HOLD_REQUEST;

static
VOID
InputHoldLock(
    _In_ PINPUT_HOLD Hold)
{
    KeWaitForSingleObject(&Hold->Lock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
InputHoldUnlock(
    _In_ PINPUT_HOLD Hold)
{
    KeReleaseMutex(&Hold->Lock, FALSE);
}

static
VOID
InputHoldRelease(
    _In_ PINPUT_HOLD_TARGET Target)
{
    if (InterlockedDecrement(&Target->References) != 0)
        return;

    ASSERT(Target->Retired);
    ASSERT(Target->File == NULL);
    ExFreePoolWithTag(Target, USERTAG_DEVICEINFO);
}

static
VOID
InputHoldQueue(
    _In_ PINPUT_HOLD_TARGET Target,
    _In_ BOOLEAN Reopen)
{
    PINPUT_HOLD Hold = Target->Hold;
    PINPUT_HOLD_REQUEST Request;

    Request = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Request), USERTAG_DEVICEINFO);
    if (!Request)
    {
        ERR("Input device %wZ stays closed: no memory to hand its removal over\n", &Target->Path);
        return;
    }

    InterlockedIncrement(&Target->References);
    Request->Reopen = Reopen;
    Request->Target = Target;

    ExAcquireFastMutex(&Hold->QueueLock);
    InsertTailList(&Hold->Requests, &Request->ListEntry);
    ExReleaseFastMutex(&Hold->QueueLock);
    KeSetEvent(Hold->Wake, IO_NO_INCREMENT, FALSE);
}

/* The file is closed before its removal goes on, reopened if the removal is cancelled and forgotten once it is gone
 * (handling-a-guid-target-device-*-event). The handle closes here, without the raw input thread: that thread may be
 * waiting for the USER lock, whose holder may be waiting for this removal. */
static
NTSTATUS
NTAPI
InputHoldNotification(
    _In_ PVOID NotificationStructure,
    _Inout_opt_ PVOID Context)
{
    PTARGET_DEVICE_REMOVAL_NOTIFICATION Notification = NotificationStructure;
    PINPUT_HOLD_TARGET Target = Context;
    HANDLE Handle = NULL;

    if (!Target)
        return STATUS_SUCCESS;

    if (IsEqualGUID(&Notification->Event, &InputHoldQueryRemove))
    {
        InputHoldLock(Target->Hold);
        if (Target->File && !Target->File->Closed)
        {
            Handle = Target->File->Handle;
            Target->File->Closed = TRUE;
        }
        InputHoldUnlock(Target->Hold);
        if (Handle)
            ObCloseHandle(Handle, KernelMode);
    }
    else if (IsEqualGUID(&Notification->Event, &InputHoldRemoveCancelled))
    {
        InputHoldQueue(Target, TRUE);
    }
    else if (IsEqualGUID(&Notification->Event, &InputHoldRemoveComplete))
    {
        InputHoldQueue(Target, FALSE);
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
InputHoldInitialize(
    _Out_ PINPUT_HOLD Hold,
    _In_ PKEVENT Wake)
{
    KeInitializeMutex(&Hold->Lock, 0);
    ExInitializeFastMutex(&Hold->QueueLock);
    InitializeListHead(&Hold->Requests);
    InitializeListHead(&Hold->Targets);
    Hold->Wake = Wake;
}

/* Reads complete on an event of the file's own, so a removal can close the file without the raw input thread */
NTSTATUS
NTAPI
InputHoldOpen(
    _Out_ PINPUT_HOLD_FILE File,
    _In_ PUNICODE_STRING Path,
    _In_ ACCESS_MASK DesiredAccess)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    RtlZeroMemory(File, sizeof(*File));

    InitializeObjectAttributes(&Attributes, Path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenFile(&File->Handle, DesiredAccess, &Attributes, &Iosb, FILE_SHARE_READ | FILE_SHARE_WRITE, 0);
    if (!NT_SUCCESS(Status))
    {
        File->Handle = NULL;
        return Status;
    }

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateEvent(&File->EventHandle, EVENT_ALL_ACCESS, &Attributes, NotificationEvent, FALSE);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(File->EventHandle, EVENT_ALL_ACCESS, *ExEventObjectType, KernelMode,
                                           (PVOID *)&File->ReadEvent, NULL);
        if (!NT_SUCCESS(Status))
            ObCloseHandle(File->EventHandle, KernelMode);
    }
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(File->Handle, KernelMode);
        RtlZeroMemory(File, sizeof(*File));
    }

    return Status;
}

/* Only before the file follows its device: the call uses the read event and the handle without the lock */
NTSTATUS
NTAPI
InputHoldIoctl(
    _In_ PINPUT_HOLD_FILE File,
    _In_ ULONG IoControlCode,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    ASSERT(File->Target == NULL);
    ASSERT(!File->Closed);

    Status = ZwDeviceIoControlFile(File->Handle, File->EventHandle, NULL, NULL, &Iosb, IoControlCode, NULL, 0,
                                   Buffer, Length);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(File->ReadEvent, Executive, KernelMode, FALSE, NULL);
        Status = Iosb.Status;
    }
    return Status;
}

/* The file follows its device's removal from now on; a file that cannot still works */
VOID
NTAPI
InputHoldFollow(
    _In_ PINPUT_HOLD Hold,
    _Inout_ PINPUT_HOLD_FILE File,
    _In_ PCUNICODE_STRING Path,
    _In_ PVOID Owner,
    _In_ PDRIVER_OBJECT DriverObject)
{
    PFILE_OBJECT FileObject;
    PINPUT_HOLD_TARGET Target;
    NTSTATUS Status;

    ASSERT(File->Handle != NULL && !File->Closed && File->Target == NULL);

    Target = ExAllocatePoolZero(NonPagedPool, sizeof(*Target) + Path->Length, USERTAG_DEVICEINFO);
    if (!Target)
        return;

    Target->Hold = Hold;
    Target->Owner = Owner;
    Target->References = 1;
    Target->Path.Buffer = (PWCH)(Target + 1);
    Target->Path.Length = Path->Length;
    Target->Path.MaximumLength = Path->Length;
    RtlCopyMemory(Target->Path.Buffer, Path->Buffer, Path->Length);

    Status = ObReferenceObjectByHandle(File->Handle, 0, *IoFileObjectType, KernelMode, (PVOID *)&FileObject, NULL);
    if (NT_SUCCESS(Status))
    {
        InputHoldLock(Hold);
        Target->File = File;
        File->Target = Target;
        InputHoldUnlock(Hold);

        Status = IoRegisterPlugPlayNotification(EventCategoryTargetDeviceChange, 0, FileObject, DriverObject,
                                                InputHoldNotification, Target, &Target->Notification);
        ObDereferenceObject(FileObject);
    }
    if (!NT_SUCCESS(Status))
    {
        WARN("Input device %wZ cannot follow its removal: 0x%08lx\n", Path, Status);
        InputHoldLock(Hold);
        Target->File = NULL;
        File->Target = NULL;
        InputHoldUnlock(Hold);
        ExFreePoolWithTag(Target, USERTAG_DEVICEINFO);
        return;
    }

    InsertTailList(&Hold->Targets, &Target->ListEntry);
}

NTSTATUS
NTAPI
InputHoldRead(
    _In_ PINPUT_HOLD Hold,
    _Inout_ PINPUT_HOLD_FILE File,
    _Out_ PIO_STATUS_BLOCK Iosb,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    LARGE_INTEGER ByteOffset;
    NTSTATUS Status = STATUS_FILE_CLOSED;

    ByteOffset.QuadPart = 0;

    InputHoldLock(Hold);
    if (!File->Closed)
        Status = ZwReadFile(File->Handle, File->EventHandle, NULL, NULL, Iosb, Buffer, Length, &ByteOffset, NULL);
    InputHoldUnlock(Hold);

    return Status;
}

/* The registration stays until its removal is reported or the input stops; it no longer reaches the file */
VOID
NTAPI
InputHoldClose(
    _In_ PINPUT_HOLD Hold,
    _Inout_ PINPUT_HOLD_FILE File,
    _In_ BOOLEAN ReadPending)
{
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle = NULL;

    InputHoldLock(Hold);
    if (File->Target)
    {
        ASSERT(File->Target->File == File);
        File->Target->File = NULL;
        File->Target = NULL;
    }
    if (!File->Closed)
    {
        Handle = File->Handle;
        File->Closed = TRUE;
    }
    InputHoldUnlock(Hold);

    if (Handle)
    {
        if (ReadPending)
            (void)ZwCancelIoFile(Handle, &Iosb);
        ObCloseHandle(Handle, KernelMode);
    }
    if (ReadPending && File->ReadEvent)
        KeWaitForSingleObject(File->ReadEvent, Executive, KernelMode, FALSE, NULL);
    if (File->ReadEvent)
        ObDereferenceObject(File->ReadEvent);
    if (File->EventHandle)
        ObCloseHandle(File->EventHandle, KernelMode);
    RtlZeroMemory(File, sizeof(*File));
}

static
VOID
InputHoldRetire(
    _In_ PINPUT_HOLD Hold,
    _In_ PINPUT_HOLD_TARGET Target,
    _In_ BOOLEAN Reopen,
    _In_opt_ PINPUT_HOLD_ENDED Ended,
    _In_opt_ PVOID Context)
{
    PINPUT_HOLD_FILE File;

    ASSERT(!Target->Retired);
    Target->Retired = TRUE;

    InputHoldLock(Hold);
    File = Target->File;
    InputHoldUnlock(Hold);

    if (Ended)
        Ended(Target->Owner, File, &Target->Path, Reopen, Context);
    ASSERT(Target->File == NULL);

    IoUnregisterPlugPlayNotificationEx(Target->Notification);
    RemoveEntryList(&Target->ListEntry);
    InputHoldRelease(Target);
}

/* A file whose removal was reported is closed by its owner, which reopens it if the removal was cancelled */
VOID
NTAPI
InputHoldProcessRequests(
    _In_ PINPUT_HOLD Hold,
    _In_opt_ PINPUT_HOLD_ENDED Ended,
    _In_opt_ PVOID Context)
{
    PINPUT_HOLD_REQUEST Request;
    PINPUT_HOLD_TARGET Target;
    PLIST_ENTRY Entry;

    for (;;)
    {
        ExAcquireFastMutex(&Hold->QueueLock);
        Entry = IsListEmpty(&Hold->Requests) ? NULL : RemoveHeadList(&Hold->Requests);
        ExReleaseFastMutex(&Hold->QueueLock);
        if (!Entry)
            break;

        Request = CONTAINING_RECORD(Entry, INPUT_HOLD_REQUEST, ListEntry);
        Target = Request->Target;
        if (!Target->Retired)
            InputHoldRetire(Hold, Target, Request->Reopen, Ended, Context);

        InputHoldRelease(Target);
        ExFreePoolWithTag(Request, USERTAG_DEVICEINFO);
    }
}

/* The owner closed its files; every registration ends before the state that holds them goes */
VOID
NTAPI
InputHoldStop(
    _In_ PINPUT_HOLD Hold)
{
    while (!IsListEmpty(&Hold->Targets))
        InputHoldRetire(Hold, CONTAINING_RECORD(Hold->Targets.Flink, INPUT_HOLD_TARGET, ListEntry), FALSE, NULL, NULL);
    InputHoldProcessRequests(Hold, NULL, NULL);
}
