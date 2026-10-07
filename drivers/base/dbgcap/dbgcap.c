/*
 * PROJECT:     LiberNT Debug Output Capture
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Captures kernel debug print output for dbglog
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <wdmsec.h>
#include <drivers/dbgcap.h>

#define NDEBUG
#include <debug.h>

#define DBGCAP_TAG          'paCD'
#define DBGCAP_RING_SIZE    (1024 * 1024)
#define DBGCAP_MAX_TEXT     512

typedef struct _DBGCAP_CONTEXT
{
    PUCHAR Ring;
    ULONG Head;
    ULONG Tail;
    ULONG Used;
    ULONG Unread;
    ULONG Lost;
    ULONG Dropped;
    KSPIN_LOCK Lock;
    KDPC Dpc;
    KEVENT DataEvent;
    BOOLEAN Registered;
    BOOLEAN Opened;
} DBGCAP_CONTEXT, *PDBGCAP_CONTEXT;

static DBGCAP_CONTEXT DbgCap;

static
KIRQL
DbgCapAcquire(VOID)
{
    KIRQL OldIrql;

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&DbgCap.Lock);
    return OldIrql;
}

static
VOID
DbgCapRelease(
    _In_ KIRQL OldIrql)
{
    KeReleaseSpinLockFromDpcLevel(&DbgCap.Lock);
    KeLowerIrql(OldIrql);
}

static
VOID
DbgCapRingWrite(
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ ULONG Size)
{
    ULONG First = min(Size, DBGCAP_RING_SIZE - DbgCap.Head);

    RtlCopyMemory(DbgCap.Ring + DbgCap.Head, Data, First);
    if (Size > First)
        RtlCopyMemory(DbgCap.Ring, (const UCHAR *)Data + First, Size - First);
    DbgCap.Head = (DbgCap.Head + Size) % DBGCAP_RING_SIZE;
    DbgCap.Used += Size;
}

static
VOID
DbgCapRingCopy(
    _In_ ULONG Index,
    _Out_writes_bytes_(Size) PVOID Data,
    _In_ ULONG Size)
{
    ULONG First = min(Size, DBGCAP_RING_SIZE - Index);

    RtlCopyMemory(Data, DbgCap.Ring + Index, First);
    if (Size > First)
        RtlCopyMemory((PUCHAR)Data + First, DbgCap.Ring, Size - First);
}

static
VOID
DbgCapPrintCallback(
    _In_ PSTRING Output,
    _In_ ULONG ComponentId,
    _In_ ULONG Level)
{
    DBGCAP_RECORD Header;
    USHORT Length;
    ULONG Size, Oldest;
    KIRQL OldIrql;
    BOOLEAN Wake;
    static const UCHAR Padding[8] = { 0 };

    Length = min(Output->Length, DBGCAP_MAX_TEXT);
    Size = DBGCAP_RECORD_SIZE(Length);

    RtlZeroMemory(&Header, sizeof(Header));
    Header.Size = Size;
    Header.ProcessId = HandleToUlong(PsGetCurrentProcessId());
    KeQuerySystemTimePrecise(&Header.SystemTime);
    Header.ComponentId = ComponentId;
    Header.Level = Level;
    Header.Length = Length;

    OldIrql = DbgCapAcquire();
    while (DbgCap.Used + Size > DBGCAP_RING_SIZE)
    {
        DbgCapRingCopy(DbgCap.Tail, &Oldest, sizeof(Oldest));
        DbgCap.Tail = (DbgCap.Tail + Oldest) % DBGCAP_RING_SIZE;
        DbgCap.Used -= Oldest;
        DbgCap.Dropped++;
        if (DbgCap.Unread > DbgCap.Used)
        {
            DbgCap.Unread = DbgCap.Used;
            DbgCap.Lost++;
        }
    }
    DbgCapRingWrite(&Header, FIELD_OFFSET(DBGCAP_RECORD, Text));
    DbgCapRingWrite(Output->Buffer, Length);
    DbgCapRingWrite(Padding, Size - FIELD_OFFSET(DBGCAP_RECORD, Text) - Length);
    DbgCap.Unread += Size;
    Wake = DbgCap.Opened;
    DbgCapRelease(OldIrql);

    if (Wake)
        KeInsertQueueDpc(&DbgCap.Dpc, NULL, NULL);
}

static
VOID
NTAPI
DbgCapDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeSetEvent(&DbgCap.DataEvent, IO_NO_INCREMENT, FALSE);
}

static
ULONG
DbgCapCopyRecords(
    _Out_writes_bytes_(BufferSize) PUCHAR Buffer,
    _In_ ULONG BufferSize)
{
    PDBGCAP_READ_HEADER ReadHeader = (PDBGCAP_READ_HEADER)Buffer;
    ULONG Offset = sizeof(*ReadHeader), Size, Index;
    KIRQL OldIrql;

    ReadHeader->Count = 0;

    OldIrql = DbgCapAcquire();
    ReadHeader->Lost = DbgCap.Lost;
    DbgCap.Lost = 0;
    while (DbgCap.Unread != 0)
    {
        Index = (DbgCap.Head + DBGCAP_RING_SIZE - DbgCap.Unread) % DBGCAP_RING_SIZE;

        DbgCapRingCopy(Index, &Size, sizeof(Size));
        if (Offset + Size > BufferSize)
            break;
        DbgCapRingCopy(Index, Buffer + Offset, Size);
        DbgCap.Unread -= Size;
        Offset += Size;
        ReadHeader->Count++;
    }
    DbgCapRelease(OldIrql);

    return Offset;
}

static
NTSTATUS
DbgCapRead(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PUCHAR Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG TimeoutMs = 0;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    if (OutputLength < sizeof(DBGCAP_READ_HEADER) + DBGCAP_RECORD_SIZE(DBGCAP_MAX_TEXT))
        return STATUS_BUFFER_TOO_SMALL;

    if (Stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(DBGCAP_READ_REQUEST))
        TimeoutMs = ((PDBGCAP_READ_REQUEST)Buffer)->TimeoutMs;

    if (TimeoutMs != 0 && InterlockedCompareExchange((volatile LONG *)&DbgCap.Unread, 0, 0) == 0)
    {
        Timeout.QuadPart = -(LONGLONG)TimeoutMs * 10000;
        Status = KeWaitForSingleObject(&DbgCap.DataEvent, UserRequest, UserMode, TRUE, &Timeout);
        if (Status == STATUS_USER_APC || Status == STATUS_ALERTED)
            return STATUS_CANCELLED;
    }

    Irp->IoStatus.Information = DbgCapCopyRecords(Buffer, OutputLength);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
DbgCapDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Information = 0;

    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            KeClearEvent(&DbgCap.DataEvent);
            OldIrql = DbgCapAcquire();
            DbgCap.Unread = DbgCap.Lost = 0;
            DbgCap.Opened = TRUE;
            DbgCapRelease(OldIrql);
            break;

        case IRP_MJ_CLEANUP:
            OldIrql = DbgCapAcquire();
            DbgCap.Opened = FALSE;
            DbgCapRelease(OldIrql);
            KeFlushQueuedDpcs();
            break;

        case IRP_MJ_CLOSE:
            break;

        case IRP_MJ_DEVICE_CONTROL:
            if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_DBGCAP_READ)
            {
                Status = DbgCapRead(Irp, Stack);
            }
            else if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_DBGCAP_REWIND)
            {
                OldIrql = DbgCapAcquire();
                DbgCap.Unread = DbgCap.Used;
                DbgCap.Lost = DbgCap.Dropped;
                DbgCapRelease(OldIrql);
            }
            else
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
            }
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
VOID
NTAPI
DbgCapUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING DosName = RTL_CONSTANT_STRING(DBGCAP_DOS_DEVICE_NAME);

    if (DbgCap.Registered)
    {
        DbgSetDebugPrintCallback(DbgCapPrintCallback, FALSE);
        DbgCap.Registered = FALSE;
    }
    KeFlushQueuedDpcs();

    IoDeleteSymbolicLink(&DosName);
    if (DriverObject->DeviceObject)
        IoDeleteDevice(DriverObject->DeviceObject);
    if (DbgCap.Ring)
        ExFreePoolWithTag(DbgCap.Ring, DBGCAP_TAG);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(DBGCAP_DEVICE_NAME);
    UNICODE_STRING DosName = RTL_CONSTANT_STRING(DBGCAP_DOS_DEVICE_NAME);
    UNICODE_STRING Sddl = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);

    DbgCap.Ring = ExAllocatePoolWithTag(NonPagedPool, DBGCAP_RING_SIZE, DBGCAP_TAG);
    if (!DbgCap.Ring)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeSpinLock(&DbgCap.Lock);
    KeInitializeDpc(&DbgCap.Dpc, DbgCapDpcRoutine, NULL);
    KeInitializeEvent(&DbgCap.DataEvent, SynchronizationEvent, FALSE);

    Status = IoCreateDeviceSecure(DriverObject,
                                  0,
                                  &DeviceName,
                                  FILE_DEVICE_UNKNOWN,
                                  FILE_DEVICE_SECURE_OPEN,
                                  TRUE,
                                  &Sddl,
                                  NULL,
                                  &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(DbgCap.Ring, DBGCAP_TAG);
        DbgCap.Ring = NULL;
        return Status;
    }

    Status = IoCreateSymbolicLink(&DosName, &DeviceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        ExFreePoolWithTag(DbgCap.Ring, DBGCAP_TAG);
        DbgCap.Ring = NULL;
        return Status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = DbgCapDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = DbgCapDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DbgCapDispatch;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DbgCapDispatch;
    DriverObject->DriverUnload = DbgCapUnload;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    Status = DbgSetDebugPrintCallback(DbgCapPrintCallback, TRUE);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteSymbolicLink(&DosName);
        IoDeleteDevice(DeviceObject);
        ExFreePoolWithTag(DbgCap.Ring, DBGCAP_TAG);
        DbgCap.Ring = NULL;
        return Status;
    }
    DbgCap.Registered = TRUE;

    return STATUS_SUCCESS;
}
