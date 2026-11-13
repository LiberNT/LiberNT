/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A registry read is not held up by a hive flush waiting on its disk
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <fltkernel.h>

#define NDEBUG
#include <debug.h>

#include "CmFlushReaders.h"

#define HELD_LIMIT_MS     5000
#define REACH_LIMIT_MS    2000
#define WRITER_QUEUE_MS   50
#define READER_LIMIT_MS   1000

static KMT_MESSAGE_HANDLER TestMessageHandler;

static UNICODE_STRING HiveFileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\KmtFlushReaders.hiv");
static UNICODE_STRING HiveLog1Name = RTL_CONSTANT_STRING(L"\\SystemRoot\\KmtFlushReaders.hiv.LOG1");
static UNICODE_STRING HiveLog2Name = RTL_CONSTANT_STRING(L"\\SystemRoot\\KmtFlushReaders.hiv.LOG2");
static UNICODE_STRING HiveLogName = RTL_CONSTANT_STRING(L"\\SystemRoot\\KmtFlushReaders.hiv.LOG");
static UNICODE_STRING MountName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\KmtFlushReaders");
static UNICODE_STRING SourceName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\KmtFlushReadersSource");
static UNICODE_STRING WriterName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\KmtFlushReadersWriter");
static UNICODE_STRING ReaderName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE");

static WCHAR ServicePath[260];
static PDRIVER_OBJECT TestDriverObject;
static PFLT_FILTER Filter;

static PETHREAD volatile HeldThread;
static volatile LONG HeldCount;
static KEVENT IoHeld;
static KEVENT IoRelease;
static HANDLE FlushRoot;
static NTSTATUS FlushStatus;
static NTSTATUS WriterStatus;
static NTSTATUS ReaderStatus;
static KEVENT WriterDone;
static KEVENT ReaderDone;

static
LARGE_INTEGER
Milliseconds(
    _In_ ULONG Count)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -(LONGLONG)Count * 10000;
    return Timeout;
}

static
FLT_PREOP_CALLBACK_STATUS
FLTAPI
PreHiveIo(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Outptr_result_maybenull_ PVOID *CompletionContext)
{
    LARGE_INTEGER Timeout;

    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(FltObjects);

    *CompletionContext = NULL;

    if (PsGetCurrentThread() != HeldThread)
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
    if (InterlockedIncrement(&HeldCount) != 1)
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    ok_irql(PASSIVE_LEVEL);
    KeSetEvent(&IoHeld, IO_NO_INCREMENT, FALSE);
    Timeout = Milliseconds(HELD_LIMIT_MS);
    KeWaitForSingleObject(&IoRelease, Executive, KernelMode, FALSE, &Timeout);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static
NTSTATUS
FLTAPI
InstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

    if (VolumeDeviceType != FILE_DEVICE_DISK_FILE_SYSTEM)
    {
        return STATUS_FLT_DO_NOT_ATTACH;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
FLTAPI
InstanceQueryTeardown(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);

    return STATUS_SUCCESS;
}

static CONST FLT_OPERATION_REGISTRATION Operations[] =
{
    { IRP_MJ_WRITE, 0, PreHiveIo, NULL },
    { IRP_MJ_FLUSH_BUFFERS, 0, PreHiveIo, NULL },
    { IRP_MJ_OPERATION_END }
};

static CONST FLT_REGISTRATION Registration =
{
    sizeof(FLT_REGISTRATION),
    FLT_REGISTRATION_VERSION,
    FLTFL_REGISTRATION_DO_NOT_SUPPORT_SERVICE_STOP,
    NULL,
    Operations,
    NULL,
    InstanceSetup,
    InstanceQueryTeardown,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static
NTSTATUS
WriteInstanceRegistry(VOID)
{
    static WCHAR InstanceName[] = L"CmFlushReaders Instance";
    static WCHAR Altitude[] = L"320126";
    WCHAR Path[320];
    ULONG Flags = 0;
    NTSTATUS Status;

    RtlStringCbPrintfW(Path, sizeof(Path), L"%ls\\Instances", ServicePath);
    Status = RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, Path);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, Path, L"DefaultInstance", REG_SZ,
                                   InstanceName, sizeof(InstanceName));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlStringCbPrintfW(Path, sizeof(Path), L"%ls\\Instances\\%ls", ServicePath, InstanceName);
    Status = RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, Path);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, Path, L"Altitude", REG_SZ, Altitude, sizeof(Altitude));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    return RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, Path, L"Flags", REG_DWORD, &Flags, sizeof(Flags));
}

static
VOID
DeleteByName(
    _In_ PUNICODE_STRING Name)
{
    OBJECT_ATTRIBUTES Attributes;

    InitializeObjectAttributes(&Attributes, Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    ZwDeleteFile(&Attributes);
}

static
NTSTATUS
WriteHiveFile(VOID)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Source, File;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &SourceName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwCreateKey(&Source, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    InitializeObjectAttributes(&Attributes, &HiveFileName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwCreateFile(&File,
                          GENERIC_WRITE | SYNCHRONIZE,
                          &Attributes,
                          &IoStatus,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          0,
                          FILE_OVERWRITE_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL,
                          0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwSaveKey(Source, File);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(File);
    }

    ZwDeleteKey(Source);
    ZwClose(Source);
    return Status;
}

static
VOID
NTAPI
FlushRoutine(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    InterlockedExchangePointer((PVOID volatile *)&HeldThread, PsGetCurrentThread());
    FlushStatus = ZwFlushKey(FlushRoot);
    InterlockedExchangePointer((PVOID volatile *)&HeldThread, NULL);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
NTAPI
WriterRoutine(
    _In_ PVOID Context)
{
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Key;

    UNREFERENCED_PARAMETER(Context);

    InitializeObjectAttributes(&Attributes, &WriterName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    WriterStatus = ZwCreateKey(&Key, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    if (NT_SUCCESS(WriterStatus))
    {
        ZwDeleteKey(Key);
        ZwClose(Key);
    }
    KeSetEvent(&WriterDone, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
NTAPI
ReaderRoutine(
    _In_ PVOID Context)
{
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Key;

    UNREFERENCED_PARAMETER(Context);

    InitializeObjectAttributes(&Attributes, &ReaderName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    ReaderStatus = ZwOpenKey(&Key, KEY_READ, &Attributes);
    if (NT_SUCCESS(ReaderStatus))
    {
        ZwClose(Key);
    }
    KeSetEvent(&ReaderDone, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NTSTATUS
StartThread(
    _Out_ PHANDLE Handle,
    _In_ PKSTART_ROUTINE Routine)
{
    OBJECT_ATTRIBUTES Attributes;

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    return PsCreateSystemThread(Handle, THREAD_ALL_ACCESS, &Attributes, NULL, NULL, Routine, NULL);
}

static
VOID
JoinThread(
    _In_ HANDLE Handle)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    if (Handle == NULL)
    {
        return;
    }
    Timeout = Milliseconds(HELD_LIMIT_MS);
    Status = ZwWaitForSingleObject(Handle, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(Handle);
}

/* A writer queued behind a flush that waits on its disk must not hold up registry readers */
static
VOID
TestReaderDuringHeldFlush(VOID)
{
    static UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"Dirty");
    OBJECT_ATTRIBUTES MountAttributes, FileAttributes;
    HANDLE Flusher = NULL, Writer = NULL, Reader = NULL;
    LARGE_INTEGER Timeout;
    ULONG Data = 1;
    NTSTATUS Status;

    if (!NT_SUCCESS(WriteHiveFile()))
    {
        DeleteByName(&HiveFileName);
        return;
    }

    InitializeObjectAttributes(&MountAttributes, &MountName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    InitializeObjectAttributes(&FileAttributes, &HiveFileName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwLoadKey(&MountAttributes, &FileAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        DeleteByName(&HiveFileName);
        return;
    }

    Status = ZwOpenKey(&FlushRoot, KEY_ALL_ACCESS, &MountAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwSetValueKey(FlushRoot, &ValueName, 0, REG_DWORD, &Data, sizeof(Data));
        ok_eq_hex(Status, STATUS_SUCCESS);

        KeInitializeEvent(&IoHeld, NotificationEvent, FALSE);
        KeInitializeEvent(&IoRelease, NotificationEvent, FALSE);
        KeInitializeEvent(&WriterDone, NotificationEvent, FALSE);
        KeInitializeEvent(&ReaderDone, NotificationEvent, FALSE);
        HeldCount = 0;
        FlushStatus = WriterStatus = ReaderStatus = STATUS_PENDING;

        Status = StartThread(&Flusher, FlushRoutine);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Timeout = Milliseconds(REACH_LIMIT_MS);
            Status = KeWaitForSingleObject(&IoHeld, Executive, KernelMode, FALSE, &Timeout);
            ok(Status == STATUS_SUCCESS, "The flush wrote nothing to the hive files: 0x%lx\n", Status);
        }

        if (Status == STATUS_SUCCESS)
        {
            Status = StartThread(&Writer, WriterRoutine);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Timeout = Milliseconds(WRITER_QUEUE_MS);
            Status = KeWaitForSingleObject(&WriterDone, Executive, KernelMode, FALSE, &Timeout);
            if (Status == STATUS_TIMEOUT)
            {
                trace("A key creation waits for the held flush of another hive\n");
            }

            Status = StartThread(&Reader, ReaderRoutine);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Timeout = Milliseconds(READER_LIMIT_MS);
            Status = KeWaitForSingleObject(&ReaderDone, Executive, KernelMode, FALSE, &Timeout);
            ok(Status == STATUS_SUCCESS, "A registry read waited for a held hive flush: 0x%lx\n", Status);
            ok_eq_hex(ReaderStatus, STATUS_SUCCESS);
        }

        KeSetEvent(&IoRelease, IO_NO_INCREMENT, FALSE);
        JoinThread(Flusher);
        JoinThread(Writer);
        JoinThread(Reader);

        ok(HeldCount >= 1, "No hive I/O of the flush was held\n");
        ok_eq_hex(FlushStatus, STATUS_SUCCESS);
        if (Writer != NULL)
        {
            ok_eq_hex(WriterStatus, STATUS_SUCCESS);
        }
        if (Reader != NULL)
        {
            ok_eq_hex(ReaderStatus, STATUS_SUCCESS);
        }

        ZwClose(FlushRoot);
        FlushRoot = NULL;
    }

    Status = ZwUnloadKey(&MountAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    DeleteByName(&HiveFileName);
    DeleteByName(&HiveLogName);
    DeleteByName(&HiveLog1Name);
    DeleteByName(&HiveLog2Name);
}

static
VOID
TestRegister(VOID)
{
    NTSTATUS Status;

    Status = WriteInstanceRegistry();
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = FltRegisterFilter(TestDriverObject, &Registration, &Filter);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        Filter = NULL;
        return;
    }

    Status = FltStartFiltering(Filter);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestUnregister(VOID)
{
    if (Filter == NULL)
    {
        return;
    }

    FltUnregisterFilter(Filter);
    Filter = NULL;
}

NTSTATUS
TestEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PCUNICODE_STRING RegistryPath,
    OUT PCWSTR *DeviceName,
    IN OUT INT *Flags)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Flags);

    TestDriverObject = DriverObject;
    if (RegistryPath->Length >= sizeof(ServicePath))
    {
        return STATUS_NAME_TOO_LONG;
    }
    RtlCopyMemory(ServicePath, RegistryPath->Buffer, RegistryPath->Length);
    ServicePath[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    *DeviceName = L"CmFlushReaders";
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);

    TestUnregister();
}

static
NTSTATUS
TestMessageHandler(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG ControlCode,
    IN PVOID Buffer OPTIONAL,
    IN SIZE_T InLength,
    IN OUT PSIZE_T OutLength)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    switch (ControlCode)
    {
        case IOCTL_CMFLUSH_REGISTER:
            TestRegister();
            break;

        case IOCTL_CMFLUSH_RUN:
            if (Filter != NULL)
            {
                TestReaderDuringHeldFlush();
            }
            break;

        case IOCTL_CMFLUSH_UNREGISTER:
            TestUnregister();
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}
