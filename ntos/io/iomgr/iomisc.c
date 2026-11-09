/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The small answers the I/O manager gives about threads and paths
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

PEPROCESS
NTAPI
IoGetCurrentProcess(VOID)
{
    return PsGetCurrentProcess();
}

PEPROCESS
NTAPI
IoThreadToProcess(
    _In_ PETHREAD Thread)
{
    return PsGetThreadProcess(Thread);
}

PEPROCESS
NTAPI
IoGetRequestorProcess(
    _In_ PIRP Irp)
{
    if (Irp->Tail.Overlay.Thread == NULL) return NULL;

    return PsGetThreadProcess(Irp->Tail.Overlay.Thread);
}

ULONG
NTAPI
IoGetRequestorProcessId(
    _In_ PIRP Irp)
{
    PEPROCESS Process;

    Process = IoGetRequestorProcess(Irp);
    if (Process == NULL) return 0;

    return (ULONG)(ULONG_PTR)PsGetProcessId(Process);
}

NTSTATUS
NTAPI
IoGetRequestorSessionId(
    _In_ PIRP Irp,
    _Out_ PULONG pSessionId)
{
    PEPROCESS Process;

    Process = IoGetRequestorProcess(Irp);
    if (Process == NULL)
    {
        *pSessionId = (ULONG)-1;
        return STATUS_UNSUCCESSFUL;
    }

    *pSessionId = PsGetProcessSessionId(Process);

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
IoIsSystemThread(
    _In_ PETHREAD Thread)
{
    return PsIsSystemThread(Thread);
}

#ifdef _WIN64
BOOLEAN
NTAPI
IoIs32bitProcess(
    _In_opt_ PIRP Irp)
{
    PEPROCESS Process;

    if (Irp == NULL) return FALSE;

    Process = IoGetRequestorProcess(Irp);
    if (Process == NULL) return FALSE;

    return PsGetProcessWow64Process(Process) != NULL;
}
#endif

PVOID
NTAPI
IoGetInitialStack(VOID)
{
    return PsGetCurrentThread()->Tcb.InitialStack;
}

VOID
NTAPI
IoGetStackLimits(
    _Out_ PULONG_PTR LowLimit,
    _Out_ PULONG_PTR HighLimit)
{
    PKTHREAD Thread = KeGetCurrentThread();

    *LowLimit = (ULONG_PTR)Thread->StackLimit;
    *HighLimit = (ULONG_PTR)Thread->InitialStack;
}

LOGICAL
NTAPI
IoWithinStackLimits(
    _In_ ULONG_PTR RegionStart,
    _In_ SIZE_T RegionSize)
{
    ULONG_PTR LowLimit = 0;
    ULONG_PTR HighLimit = 0;

    IoGetStackLimits(&LowLimit, &HighLimit);

    if (RegionStart < LowLimit) return FALSE;
    if (RegionStart + RegionSize < RegionStart) return FALSE;
    if (RegionStart + RegionSize > HighLimit) return FALSE;

    return TRUE;
}

PIRP
NTAPI
IoGetTopLevelIrp(VOID)
{
    return (PIRP)PsGetCurrentThread()->TopLevelIrp;
}

VOID
NTAPI
IoSetTopLevelIrp(
    _In_opt_ PIRP Irp)
{
    PsGetCurrentThread()->TopLevelIrp = (ULONG_PTR)Irp;
}

PTXN_PARAMETER_BLOCK
NTAPI
IoGetTransactionParameterBlock(
    _In_ PFILE_OBJECT FileObject)
{
    /* TODO: no transaction manager plumbing on the create path */
    UNREFERENCED_PARAMETER(FileObject);

    return NULL;
}

NTSTATUS
NTAPI
IoCreateSystemThread(
    _In_ PVOID IoObject,
    _Out_ PHANDLE ThreadHandle,
    _In_ ULONG DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ HANDLE ProcessHandle,
    _Out_opt_ PCLIENT_ID ClientId,
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(IoObject);

    return PsCreateSystemThread(ThreadHandle,
                                DesiredAccess,
                                ObjectAttributes,
                                ProcessHandle,
                                ClientId,
                                StartRoutine,
                                StartContext);
}

NTSTATUS
NTAPI
IoValidateDeviceIoControlAccess(
    _In_ PIRP Irp,
    _In_ ULONG RequiredAccess)
{
    PIO_STACK_LOCATION StackPtr;
    ULONG GrantedAccess;

    StackPtr = IoGetCurrentIrpStackLocation(Irp);

    if (StackPtr->MajorFunction != IRP_MJ_DEVICE_CONTROL &&
        StackPtr->MajorFunction != IRP_MJ_FILE_SYSTEM_CONTROL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    GrantedAccess =
        (StackPtr->Parameters.DeviceIoControl.IoControlCode >> 14) & 0x3;

    if ((GrantedAccess & RequiredAccess) != RequiredAccess)
    {
        return STATUS_ACCESS_DENIED;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoOpenDriverRegistryKey(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ DRIVER_REGKEY_TYPE RegKeyType,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG Flags,
    _Out_ PHANDLE DriverRegKey)
{
    PEXTENDED_DRIVER_EXTENSION Extension;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyPath;
    PWCHAR Buffer;
    NTSTATUS Status;
    USHORT Length;

    PAGED_CODE();

    static const UNICODE_STRING ServicesPath = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\");
    static const UNICODE_STRING ParametersName =
        RTL_CONSTANT_STRING(L"\\Parameters");
    static const UNICODE_STRING PersistentName =
        RTL_CONSTANT_STRING(L"\\Persistent");

    if (Flags != 0) return STATUS_INVALID_PARAMETER;

    *DriverRegKey = NULL;

    Extension = IoGetDrvObjExtension(DriverObject);
    if (Extension->ServiceKeyName.Length == 0)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Length = ServicesPath.Length + Extension->ServiceKeyName.Length;

    if (RegKeyType == DriverRegKeyParameters)
    {
        Length += ParametersName.Length;
    }
    else if (RegKeyType == DriverRegKeyPersistentState)
    {
        Length += PersistentName.Length;
    }
    else
    {
        /* TODO: the state shared between the drivers of one device has no
           key of its own here. */
        return STATUS_INVALID_PARAMETER;
    }

    Buffer = ExAllocatePoolZero(PagedPool,
                                Length + sizeof(WCHAR),
                                TAG_IO_DRIVER_NAME);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    KeyPath.Buffer = Buffer;
    KeyPath.Length = 0;
    KeyPath.MaximumLength = Length + sizeof(WCHAR);

    RtlAppendUnicodeStringToString(&KeyPath, &ServicesPath);
    RtlAppendUnicodeStringToString(&KeyPath, &Extension->ServiceKeyName);

    if (RegKeyType == DriverRegKeyParameters)
    {
        RtlAppendUnicodeStringToString(&KeyPath, &ParametersName);
    }
    else if (RegKeyType == DriverRegKeyPersistentState)
    {
        RtlAppendUnicodeStringToString(&KeyPath, &PersistentName);
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwCreateKey(DriverRegKey,
                         DesiredAccess,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         NULL);

    ExFreePoolWithTag(Buffer, TAG_IO_DRIVER_NAME);

    return Status;
}

NTSTATUS
NTAPI
IoGetDriverDirectory(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ DRIVER_DIRECTORY_TYPE DirectoryType,
    _In_ ULONG Flags,
    _Out_ PHANDLE DriverDirectoryHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Directory;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);

    static const UNICODE_STRING DriverStore =
        RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\Drivers");

    if (Flags != 0) return STATUS_INVALID_PARAMETER;
    if (DirectoryType != DriverDirectoryImage)
    {
        /* TODO: only the image directory is published */
        return STATUS_NOT_SUPPORTED;
    }

    Directory = DriverStore;

    InitializeObjectAttributes(&ObjectAttributes,
                               &Directory,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    return ZwOpenFile(DriverDirectoryHandle,
                      FILE_LIST_DIRECTORY | FILE_TRAVERSE | SYNCHRONIZE,
                      &ObjectAttributes,
                      &IoStatusBlock,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
}

NTSTATUS
NTAPI
IoQueryFullDriverPath(
    _In_ PDRIVER_OBJECT DriverObject,
    _Out_ PUNICODE_STRING FullPath)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    PWCHAR Buffer;

    PAGED_CODE();

    Entry = DriverObject->DriverSection;
    if (Entry == NULL) return STATUS_INVALID_PARAMETER;

    if (Entry->FullDllName.Length == 0) return STATUS_NOT_FOUND;

    Buffer = ExAllocatePoolZero(PagedPool,
                                Entry->FullDllName.Length + sizeof(WCHAR),
                                TAG_IO_FULL_DRIVER_PATH);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer,
                  Entry->FullDllName.Buffer,
                  Entry->FullDllName.Length);

    FullPath->Buffer = Buffer;
    FullPath->Length = Entry->FullDllName.Length;
    FullPath->MaximumLength = Entry->FullDllName.Length + sizeof(WCHAR);

    return STATUS_SUCCESS;
}

KIRQL
NTAPI
IopAcquireDatabaseLockShared(VOID)
{
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&IopDatabaseResource, TRUE);

    return PASSIVE_LEVEL;
}

VOID
NTAPI
IopReleaseDatabaseLockShared(
    _In_ KIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(OldIrql);

    ExReleaseResourceLite(&IopDatabaseResource);
    KeLeaveCriticalRegion();
}

KIRQL
NTAPI
IopAcquireDatabaseLockExclusive(VOID)
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&IopDatabaseResource, TRUE);

    return PASSIVE_LEVEL;
}

VOID
NTAPI
IopReleaseDatabaseLockExclusive(
    _In_ KIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(OldIrql);

    ExReleaseResourceLite(&IopDatabaseResource);
    KeLeaveCriticalRegion();
}
