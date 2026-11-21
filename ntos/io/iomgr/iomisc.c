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

typedef struct _IOP_SYSTEM_THREAD_START
{
    PVOID IoObject;
    PKSTART_ROUTINE StartRoutine;
    PVOID StartContext;
} IOP_SYSTEM_THREAD_START, *PIOP_SYSTEM_THREAD_START;

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
NTAPI
IopSystemThreadStart(
    _In_ PVOID Context)
{
    IOP_SYSTEM_THREAD_START Start = *(PIOP_SYSTEM_THREAD_START)Context;

    PAGED_CODE();

    ExFreePoolWithTag(Context, TAG_IO);

    Start.StartRoutine(Start.StartContext);

    ObDereferenceObject(Start.IoObject);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

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
    PETHREAD Thread = Irp->Tail.Overlay.Thread;

    if (Thread == NULL) return NULL;

    if (Irp->ApcEnvironment == OriginalApcEnvironment) return PsGetThreadProcess(Thread);

    return (PEPROCESS)Thread->Tcb.ApcState.Process;
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

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    if (Irp != NULL && Irp->RequestorMode == KernelMode)
        return FALSE;

    Process = (Irp != NULL) ? IoGetRequestorProcess(Irp) : PsGetCurrentProcess();
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
    *HighLimit = (ULONG_PTR)Thread->StackBase;
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
    PFILE_OBJECT_EXTENSION Extension;

    if (!(FileObject->Flags & FO_FILE_OBJECT_HAS_EXTENSION)) return NULL;

    Extension = FileObject->FileObjectExtension;
    if (Extension == NULL || Extension->TxnParameters.TransactionObject == NULL) return NULL;

    return &Extension->TxnParameters;
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
    PIOP_SYSTEM_THREAD_START Start;
    NTSTATUS Status;

    PAGED_CODE();

    if (IoObject == NULL || StartRoutine == NULL ||
        (((PDEVICE_OBJECT)IoObject)->Type != IO_TYPE_DEVICE &&
         ((PDRIVER_OBJECT)IoObject)->Type != IO_TYPE_DRIVER))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Start = ExAllocatePoolWithTag(PagedPool, sizeof(*Start), TAG_IO);
    if (Start == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Start->IoObject = IoObject;
    Start->StartRoutine = StartRoutine;
    Start->StartContext = StartContext;

    ObReferenceObject(IoObject);

    Status = PsCreateSystemThread(ThreadHandle,
                                  DesiredAccess,
                                  ObjectAttributes,
                                  ProcessHandle,
                                  ClientId,
                                  IopSystemThreadStart,
                                  Start);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(IoObject);
        ExFreePoolWithTag(Start, TAG_IO);
    }

    return Status;
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

/* Windows 11 keeps the device directory name in this property; it is absent from the public headers (measured on WinPE 26100) */
static const DEVPROPKEY IopDeviceDirectoryKey =
    { { 0x83da6326, 0x97a6, 0x4088, { 0x94, 0x53, 0xa1, 0x92, 0x3f, 0x57, 0x3b, 0x29 } }, 21 };

#define IOP_GUID_TEXT_CHARS 38

static EX_PUSH_LOCK IopDeviceDirectoryLock;

static
NTSTATUS
IopOpenStateDirectory(
    _In_ PCWSTR Base,
    _In_ PCUNICODE_STRING Name,
    _In_ PCWSTR Leaf,
    _Out_ PHANDLE DirectoryHandle)
{
    static const UNICODE_STRING SystemRoot = RTL_CONSTANT_STRING(L"\\SystemRoot\\");
    UNICODE_STRING BaseString, LeafString, Path, Prefix;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status = STATUS_SUCCESS;
    HANDLE Directory;
    SIZE_T Length;
    USHORT Index;

    PAGED_CODE();

    RtlInitUnicodeString(&BaseString, Base);
    RtlInitUnicodeString(&LeafString, Leaf);
    ASSERT(BaseString.Length > SystemRoot.Length);
    ASSERT(RtlPrefixUnicodeString(&SystemRoot, &BaseString, TRUE));
    ASSERT((Name->Length % sizeof(WCHAR)) == 0);

    Length = (SIZE_T)BaseString.Length + Name->Length + LeafString.Length;
    if (Length > UNICODE_STRING_MAX_BYTES) return STATUS_NAME_TOO_LONG;

    Path.Buffer = ExAllocatePoolWithTag(PagedPool, Length, TAG_IO_DRIVER_NAME);
    if (Path.Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    Path.Length = 0;
    Path.MaximumLength = (USHORT)Length;
    RtlAppendUnicodeStringToString(&Path, &BaseString);
    RtlAppendUnicodeStringToString(&Path, Name);
    RtlAppendUnicodeStringToString(&Path, &LeafString);
    ASSERT(Path.Length == Length);

    /* TODO: the state directories take the inherited security; Windows 11's descriptor is not measured */
    Prefix.Buffer = Path.Buffer;
    for (Index = SystemRoot.Length / sizeof(WCHAR); Index < Path.Length / sizeof(WCHAR) && NT_SUCCESS(Status); Index++)
    {
        if (Path.Buffer[Index] != OBJ_NAME_PATH_SEPARATOR) continue;

        Prefix.Length = Prefix.MaximumLength = Index * sizeof(WCHAR);
        InitializeObjectAttributes(&ObjectAttributes, &Prefix, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
        Status = ZwCreateFile(&Directory,
                              FILE_TRAVERSE | SYNCHRONIZE,
                              &ObjectAttributes,
                              &IoStatusBlock,
                              NULL,
                              FILE_ATTRIBUTE_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              FILE_OPEN_IF,
                              FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                              NULL,
                              0);
        if (NT_SUCCESS(Status)) ZwClose(Directory);
    }

    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
        Status = ZwCreateFile(DirectoryHandle,
                              FILE_LIST_DIRECTORY | SYNCHRONIZE,
                              &ObjectAttributes,
                              &IoStatusBlock,
                              NULL,
                              FILE_ATTRIBUTE_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              FILE_OPEN_IF,
                              FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                              NULL,
                              0);
    }

    ExFreePoolWithTag(Path.Buffer, TAG_IO_DRIVER_NAME);
    return Status;
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
    static const UNICODE_STRING ServicesPath = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\");
    static const UNICODE_STRING SubkeyNames[] =
    {
        RTL_CONSTANT_STRING(L"\\Parameters"),
        RTL_CONSTANT_STRING(L"\\State"),
        RTL_CONSTANT_STRING(L"\\SharedState"),
    };
    PEXTENDED_DRIVER_EXTENSION Extension;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyPath;
    NTSTATUS Status;
    SIZE_T Length;

    PAGED_CODE();

    C_ASSERT(DriverRegKeyParameters == 0);
    C_ASSERT(DriverRegKeyPersistentState == 1);
    C_ASSERT(DriverRegKeySharedPersistentState == 2);

    if (DriverObject == NULL || DriverRegKey == NULL || Flags != 0 ||
        (ULONG)RegKeyType >= RTL_NUMBER_OF(SubkeyNames))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *DriverRegKey = NULL;

    Extension = IoGetDrvObjExtension(DriverObject);
    if (Extension->ServiceKeyName.Length == 0)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Length = (SIZE_T)ServicesPath.Length + Extension->ServiceKeyName.Length + SubkeyNames[RegKeyType].Length;
    if (Length > UNICODE_STRING_MAX_BYTES) return STATUS_NAME_TOO_LONG;

    KeyPath.Buffer = ExAllocatePoolWithTag(PagedPool, Length, TAG_IO_DRIVER_NAME);
    if (KeyPath.Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    KeyPath.Length = 0;
    KeyPath.MaximumLength = (USHORT)Length;
    RtlAppendUnicodeStringToString(&KeyPath, &ServicesPath);
    RtlAppendUnicodeStringToString(&KeyPath, &Extension->ServiceKeyName);
    RtlAppendUnicodeStringToString(&KeyPath, &SubkeyNames[RegKeyType]);
    ASSERT(KeyPath.Length == Length);

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    if (RegKeyType == DriverRegKeyParameters)
    {
        Status = ZwOpenKey(DriverRegKey, DesiredAccess, &ObjectAttributes);
    }
    else
    {
        Status = ZwCreateKey(DriverRegKey,
                             DesiredAccess,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_NON_VOLATILE,
                             NULL);
    }

    ExFreePoolWithTag(KeyPath.Buffer, TAG_IO_DRIVER_NAME);
    return Status;
}

static
NTSTATUS
IopOpenDriverImageDirectory(
    _In_ PDRIVER_OBJECT DriverObject,
    _Out_ PHANDLE DriverDirectoryHandle)
{
    PKLDR_DATA_TABLE_ENTRY Entry = DriverObject->DriverSection;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Directory;

    PAGED_CODE();

    if (Entry == NULL || Entry->FullDllName.Length == 0) return STATUS_INVALID_PARAMETER;

    Directory = Entry->FullDllName;
    while (Directory.Length != 0 &&
           Directory.Buffer[Directory.Length / sizeof(WCHAR) - 1] != OBJ_NAME_PATH_SEPARATOR)
    {
        Directory.Length -= sizeof(WCHAR);
    }
    if (Directory.Length == 0) return STATUS_INVALID_PARAMETER;

    Directory.Length -= sizeof(WCHAR);
    Directory.MaximumLength = Directory.Length;
    ASSERT(Directory.Length < Entry->FullDllName.Length);

    InitializeObjectAttributes(&ObjectAttributes,
                               &Directory,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    return ZwOpenFile(DriverDirectoryHandle,
                      FILE_LIST_DIRECTORY | SYNCHRONIZE,
                      &ObjectAttributes,
                      &IoStatusBlock,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
}

NTSTATUS
NTAPI
IoGetDriverDirectory(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ DRIVER_DIRECTORY_TYPE DirectoryType,
    _In_ ULONG Flags,
    _Out_ PHANDLE DriverDirectoryHandle)
{
    PEXTENDED_DRIVER_EXTENSION Extension;

    PAGED_CODE();

    if (DriverObject == NULL || DriverDirectoryHandle == NULL || Flags != 0 ||
        (DirectoryType != DriverDirectoryImage &&
         DirectoryType != DriverDirectoryData &&
         DirectoryType != DriverDirectorySharedData))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *DriverDirectoryHandle = NULL;

    if (DirectoryType == DriverDirectoryImage)
        return IopOpenDriverImageDirectory(DriverObject, DriverDirectoryHandle);

    Extension = IoGetDrvObjExtension(DriverObject);
    if (Extension->ServiceKeyName.Length == 0)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    return IopOpenStateDirectory(L"\\SystemRoot\\ServiceState\\",
                                 &Extension->ServiceKeyName,
                                 DirectoryType == DriverDirectoryData ? L"\\Data" : L"\\SharedData",
                                 DriverDirectoryHandle);
}

static
BOOLEAN
IopIsDeviceDirectoryName(
    _In_reads_(IOP_GUID_TEXT_CHARS) PCWSTR Text)
{
    ULONG Index;

    if (Text[0] != L'{' || Text[IOP_GUID_TEXT_CHARS - 1] != L'}') return FALSE;
    for (Index = 1; Index < IOP_GUID_TEXT_CHARS - 1; Index++)
    {
        if (Index == 9 || Index == 14 || Index == 19 || Index == 24)
        {
            if (Text[Index] != L'-') return FALSE;
        }
        else if (!((Text[Index] >= L'0' && Text[Index] <= L'9') || (Text[Index] >= L'a' && Text[Index] <= L'f')))
        {
            return FALSE;
        }
    }
    return TRUE;
}

static
NTSTATUS
IopGetDeviceDirectoryName(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_writes_(IOP_GUID_TEXT_CHARS + 1) PWCHAR Text)
{
    DEVPROPTYPE Type = DEVPROP_TYPE_EMPTY;
    ULONG Required = 0;
    NTSTATUS Status;
    GUID Guid;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&IopDeviceDirectoryLock);

    RtlZeroMemory(Text, (IOP_GUID_TEXT_CHARS + 1) * sizeof(WCHAR));
    Status = IoGetDevicePropertyData(PhysicalDeviceObject,
                                     &IopDeviceDirectoryKey,
                                     0,
                                     0,
                                     (IOP_GUID_TEXT_CHARS + 1) * sizeof(WCHAR),
                                     Text,
                                     &Required,
                                     &Type);
    if (!NT_SUCCESS(Status) || Type != DEVPROP_TYPE_STRING ||
        Required != (IOP_GUID_TEXT_CHARS + 1) * sizeof(WCHAR) || !IopIsDeviceDirectoryName(Text))
    {
        Status = ExUuidCreate(&Guid);
        if (NT_SUCCESS(Status))
        {
            Status = RtlStringCchPrintfW(Text,
                                         IOP_GUID_TEXT_CHARS + 1,
                                         L"{%08lx-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                                         Guid.Data1, Guid.Data2, Guid.Data3,
                                         Guid.Data4[0], Guid.Data4[1], Guid.Data4[2], Guid.Data4[3],
                                         Guid.Data4[4], Guid.Data4[5], Guid.Data4[6], Guid.Data4[7]);
        }
        if (NT_SUCCESS(Status))
        {
            ASSERT(IopIsDeviceDirectoryName(Text));
            Status = IoSetDevicePropertyData(PhysicalDeviceObject,
                                             &IopDeviceDirectoryKey,
                                             0,
                                             PLUGPLAY_PROPERTY_PERSISTENT,
                                             DEVPROP_TYPE_STRING,
                                             (IOP_GUID_TEXT_CHARS + 1) * sizeof(WCHAR),
                                             Text);
        }
    }

    ExReleasePushLockExclusive(&IopDeviceDirectoryLock);
    KeLeaveCriticalRegion();
    return Status;
}

NTSTATUS
NTAPI
IoGetDeviceDirectory(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ DEVICE_DIRECTORY_TYPE DirectoryType,
    _In_ ULONG Flags,
    _In_ PVOID Reserved,
    _Out_ PHANDLE DeviceDirectoryHandle)
{
    WCHAR Text[IOP_GUID_TEXT_CHARS + 1];
    UNICODE_STRING Name;
    NTSTATUS Status;

    PAGED_CODE();

    if (PhysicalDeviceObject == NULL || DeviceDirectoryHandle == NULL || Flags != 0 || Reserved != NULL ||
        DirectoryType != DeviceDirectoryData || IoGetDevObjExtension(PhysicalDeviceObject)->DeviceNode == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *DeviceDirectoryHandle = NULL;

    Status = IopGetDeviceDirectoryName(PhysicalDeviceObject, Text);
    if (!NT_SUCCESS(Status)) return Status;

    Name.Buffer = Text;
    Name.Length = Name.MaximumLength = IOP_GUID_TEXT_CHARS * sizeof(WCHAR);
    return IopOpenStateDirectory(L"\\SystemRoot\\System32\\DriverState\\Devices\\",
                                 &Name,
                                 L"\\Data",
                                 DeviceDirectoryHandle);
}

NTSTATUS
NTAPI
IoQueryFullDriverPath(
    _In_ PDRIVER_OBJECT DriverObject,
    _Out_ PUNICODE_STRING FullPath)
{
    PKLDR_DATA_TABLE_ENTRY Entry;
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
