/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver replacement through the kernel debugger (.kdfiles)
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <hi@ostapbrehin.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KDP_FILE_IO_DATA_MAXIMUM (PACKET_MAX_SIZE - sizeof(DBGKD_FILE_IO))

static
BOOLEAN
KdpSendFileIo(
    _Inout_ PDBGKD_FILE_IO FileIo,
    _In_opt_ PSTRING RequestData,
    _Inout_opt_ PSTRING ResponseData)
{
    STRING Header, Data;
    DBGKD_FILE_IO Request = *FileIo;
    ULONG Length;
    KDSTATUS Status;
    BOOLEAN Enable;

    Enable = KdEnterDebugger(NULL, NULL);

    do
    {
        *FileIo = Request;
        Header.Length = sizeof(DBGKD_FILE_IO);
        Header.Buffer = (PCHAR)FileIo;
        KdSendPacket(PACKET_TYPE_KD_FILE_IO, &Header, RequestData, &KdpContext);

        Header.MaximumLength = sizeof(DBGKD_FILE_IO);
        if (ResponseData)
        {
            Data = *ResponseData;
        }
        else
        {
            Data.Buffer = KdpMessageBuffer;
            Data.MaximumLength = sizeof(KdpMessageBuffer);
        }
        Data.Length = 0;

        do
        {
            Status = KdReceivePacket(PACKET_TYPE_KD_FILE_IO,
                                     &Header,
                                     &Data,
                                     &Length,
                                     &KdpContext);
        } while ((Status == KdPacketReceived) && (FileIo->ApiNumber != Request.ApiNumber));
    } while (Status == KdPacketNeedsResend);

    KdExitDebugger(Enable);

    if (Status != KdPacketReceived)
        return FALSE;

    if (ResponseData)
        ResponseData->Length = (USHORT)min(Length, ResponseData->MaximumLength);

    return TRUE;
}

NTSTATUS
NTAPI
KdPullRemoteFile(
    _In_ PUNICODE_STRING FileName)
{
    DBGKD_FILE_IO FileIo;
    STRING Data;
    PCHAR Buffer, FileData = NULL;
    ULONG64 Handle, FileSize, Offset;
    LARGE_INTEGER AllocationSize;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    NTSTATUS Status;

    PAGED_CODE();

    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
        return STATUS_DEBUGGER_INACTIVE;

    if ((ULONG)FileName->Length + sizeof(UNICODE_NULL) > KDP_FILE_IO_DATA_MAXIMUM)
        return STATUS_NAME_TOO_LONG;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, KDP_FILE_IO_DATA_MAXIMUM, TAG_KDBG);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, FileName->Buffer, FileName->Length);
    *(PWCHAR)(Buffer + FileName->Length) = UNICODE_NULL;
    Data.Buffer = Buffer;
    Data.Length = FileName->Length + sizeof(UNICODE_NULL);
    Data.MaximumLength = KDP_FILE_IO_DATA_MAXIMUM;

    RtlZeroMemory(&FileIo, sizeof(FileIo));
    FileIo.ApiNumber = DbgKdCreateFileApi;
    FileIo.u.CreateFile.DesiredAccess = GENERIC_READ | FILE_GENERIC_READ;
    FileIo.u.CreateFile.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    FileIo.u.CreateFile.ShareAccess = FILE_SHARE_READ;
    FileIo.u.CreateFile.CreateDisposition = FILE_OPEN;
    if (!KdpSendFileIo(&FileIo, &Data, NULL))
    {
        Status = STATUS_IO_TIMEOUT;
        goto Exit;
    }

    Status = (NTSTATUS)FileIo.Status;
    if (!NT_SUCCESS(Status))
        goto Exit;

    Handle = FileIo.u.CreateFile.Handle;
    FileSize = FileIo.u.CreateFile.Length;

    DPRINT1("KD: Replacing %wZ with the debugger's copy (%I64u bytes)\n", FileName, FileSize);

    if (FileSize > MAXULONG)
        Status = STATUS_FILE_TOO_LARGE;
    else if (FileSize && !(FileData = ExAllocatePoolWithTag(PagedPool, (SIZE_T)FileSize, TAG_KDBG)))
        Status = STATUS_INSUFFICIENT_RESOURCES;

    for (Offset = 0; NT_SUCCESS(Status) && (Offset < FileSize); Offset += Data.Length)
    {
        RtlZeroMemory(&FileIo, sizeof(FileIo));
        FileIo.ApiNumber = DbgKdReadFileApi;
        FileIo.u.ReadFile.Handle = Handle;
        FileIo.u.ReadFile.Offset = Offset;
        FileIo.u.ReadFile.Length = (ULONG)min(FileSize - Offset, KDP_FILE_IO_DATA_MAXIMUM);
        Data.MaximumLength = (USHORT)FileIo.u.ReadFile.Length;
        if (!KdpSendFileIo(&FileIo, NULL, &Data))
        {
            Status = STATUS_IO_TIMEOUT;
            break;
        }

        Status = (NTSTATUS)FileIo.Status;
        if (NT_SUCCESS(Status))
            RtlCopyMemory(FileData + Offset, Buffer, Data.Length);
    }

    RtlZeroMemory(&FileIo, sizeof(FileIo));
    FileIo.ApiNumber = DbgKdCloseFileApi;
    FileIo.u.CloseFile.Handle = Handle;
    KdpSendFileIo(&FileIo, NULL, NULL);

    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes,
                                   FileName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);
        AllocationSize.QuadPart = (LONGLONG)FileSize;
        Status = ZwCreateFile(&FileHandle,
                              FILE_GENERIC_WRITE,
                              &ObjectAttributes,
                              &IoStatusBlock,
                              &AllocationSize,
                              FILE_ATTRIBUTE_NORMAL,
                              0,
                              FILE_OVERWRITE_IF,
                              FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                              NULL,
                              0);
        if (NT_SUCCESS(Status))
        {
            if (FileData)
            {
                Status = ZwWriteFile(FileHandle,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &IoStatusBlock,
                                     FileData,
                                     (ULONG)FileSize,
                                     NULL,
                                     NULL);
            }
            ZwClose(FileHandle);
        }
    }

    if (!NT_SUCCESS(Status))
        DPRINT1("KD: Replacing %wZ failed with status 0x%lx\n", FileName, Status);

    if (FileData)
        ExFreePoolWithTag(FileData, TAG_KDBG);

Exit:
    ExFreePoolWithTag(Buffer, TAG_KDBG);
    return Status;
}

#define KDP_FILE_REQUEST_NONE       0
#define KDP_FILE_REQUEST_BUSY       0xFF
#define KDP_FILE_REQUEST_NAME       260
#define KDP_RELOAD_DEVICES_MAXIMUM  32

static volatile LONG KdpFileRequest;
static WCHAR KdpFileRequestName[KDP_FILE_REQUEST_NAME];
static BOOLEAN KdpFileServiceRunning;

typedef struct _KDP_RELOAD_CONTEXT
{
    UNICODE_STRING Service;
    PDRIVER_OBJECT Driver;
    PDEVICE_OBJECT Devices[KDP_RELOAD_DEVICES_MAXIMUM];
    ULONG Count;
} KDP_RELOAD_CONTEXT, *PKDP_RELOAD_CONTEXT;

NTSTATUS
NTAPI
KdQueueFileRequest(
    _In_ ULONG Kind,
    _In_z_ PCSTR Name)
{
    ULONG Index;

    if (!KdpFileServiceRunning)
        return STATUS_DEVICE_NOT_READY;
    if ((Kind != KD_FILE_REQUEST_PULL && Kind != KD_FILE_REQUEST_RELOAD) || !Name || !Name[0])
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&KdpFileRequest, KDP_FILE_REQUEST_BUSY, KDP_FILE_REQUEST_NONE) !=
        KDP_FILE_REQUEST_NONE)
    {
        return STATUS_DEVICE_BUSY;
    }

    for (Index = 0; Name[Index] && Index < KDP_FILE_REQUEST_NAME - 1; Index++)
        KdpFileRequestName[Index] = (WCHAR)(UCHAR)Name[Index];
    if (Name[Index])
    {
        InterlockedExchange(&KdpFileRequest, KDP_FILE_REQUEST_NONE);
        return STATUS_NAME_TOO_LONG;
    }
    KdpFileRequestName[Index] = UNICODE_NULL;

#ifndef _WINKD_
    KdpResetFileIoHost();
#endif
    InterlockedExchange(&KdpFileRequest, (LONG)Kind);
    return STATUS_SUCCESS;
}

static
NTSTATUS
KdpCollectServiceDevice(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PVOID Context)
{
    PKDP_RELOAD_CONTEXT Reload = Context;
    PDEVICE_OBJECT Device;
    BOOLEAN Match;

    if (DeviceNode->PhysicalDeviceObject == NULL ||
        DeviceNode->State != DeviceNodeStarted ||
        Reload->Count >= KDP_RELOAD_DEVICES_MAXIMUM)
    {
        return STATUS_SUCCESS;
    }

    /* A device installed during this boot has no service name in its node */
    Match = RtlEqualUnicodeString(&DeviceNode->ServiceName, &Reload->Service, TRUE);
    for (Device = DeviceNode->PhysicalDeviceObject; !Match && Reload->Driver && Device; Device = Device->AttachedDevice)
        Match = (Device->DriverObject == Reload->Driver);

    if (Match)
    {
        ObReferenceObject(DeviceNode->PhysicalDeviceObject);
        Reload->Devices[Reload->Count++] = DeviceNode->PhysicalDeviceObject;
    }

    return STATUS_SUCCESS;
}

static
PDRIVER_OBJECT
KdpReferenceDriverObject(
    _In_ PUNICODE_STRING Service)
{
    WCHAR Buffer[KDP_FILE_REQUEST_NAME + 8];
    UNICODE_STRING Name;
    PDRIVER_OBJECT Driver;

    RtlInitEmptyUnicodeString(&Name, Buffer, sizeof(Buffer));
    if (!NT_SUCCESS(RtlAppendUnicodeToString(&Name, L"\\Driver\\")) ||
        !NT_SUCCESS(RtlAppendUnicodeStringToString(&Name, Service)) ||
        !NT_SUCCESS(ObReferenceObjectByName(&Name, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType,
                                            KernelMode, NULL, (PVOID *)&Driver)))
    {
        return NULL;
    }

    return Driver;
}

static
BOOLEAN
KdpDriverObjectExists(
    _In_ PUNICODE_STRING Service)
{
    PDRIVER_OBJECT Driver = KdpReferenceDriverObject(Service);

    if (!Driver)
        return FALSE;

    ObDereferenceObject(Driver);
    return TRUE;
}

static
NTSTATUS
KdpReloadLegacyDriver(
    _In_ PUNICODE_STRING Service)
{
    WCHAR Buffer[KDP_FILE_REQUEST_NAME + 64];
    UNICODE_STRING RegistryPath;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(&RegistryPath, Buffer, sizeof(Buffer));
    Status = RtlAppendUnicodeToString(&RegistryPath, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\");
    if (NT_SUCCESS(Status))
        Status = RtlAppendUnicodeStringToString(&RegistryPath, Service);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ZwUnloadDriver(&RegistryPath);
    DPRINT1("KD: Unloading %wZ returned 0x%lx\n", Service, Status);
    if (!NT_SUCCESS(Status) && KdpDriverObjectExists(Service))
        return Status;

    Status = ZwLoadDriver(&RegistryPath);
    DPRINT1("KD: Loading %wZ returned 0x%lx\n", Service, Status);
    return Status;
}

static
NTSTATUS
KdpReloadService(
    _In_ PUNICODE_STRING Service)
{
    DEVICETREE_TRAVERSE_CONTEXT Traverse;
    PI_QUERY_REMOVE_DATA RemoveData;
    PKDP_RELOAD_CONTEXT Reload;
    NTSTATUS Status = STATUS_SUCCESS, DeviceStatus;
    ULONG Index;

    Reload = ExAllocatePoolZero(PagedPool, sizeof(*Reload), TAG_KDBG);
    if (!Reload)
        return STATUS_INSUFFICIENT_RESOURCES;

    Reload->Service = *Service;
    Reload->Driver = KdpReferenceDriverObject(Service);
    IopInitDeviceTreeTraverseContext(&Traverse, IopRootDeviceNode, KdpCollectServiceDevice, Reload);
    IopTraverseDeviceTree(&Traverse);
    if (Reload->Driver)
    {
        ObDereferenceObject(Reload->Driver);
        Reload->Driver = NULL;
    }

    if (!Reload->Count)
    {
        ExFreePoolWithTag(Reload, TAG_KDBG);
        return KdpReloadLegacyDriver(Service);
    }

    for (Index = 0; Index < Reload->Count; Index++)
    {
        DeviceStatus = PiQueryRemoveDevice(Reload->Devices[Index], &RemoveData);
        if (!NT_SUCCESS(DeviceStatus))
        {
            DPRINT1("KD: Removing a %wZ device failed 0x%lx (veto %d %wZ)\n",
                    Service, DeviceStatus, RemoveData.VetoType, &RemoveData.VetoName);
            if (NT_SUCCESS(Status))
                Status = DeviceStatus;
        }
        RtlFreeUnicodeString(&RemoveData.VetoName);
    }

    if (KdpDriverObjectExists(Service))
        DPRINT1("KD: %wZ stayed loaded; its devices restart on the image already in memory\n", Service);

    for (Index = 0; Index < Reload->Count; Index++)
    {
        DeviceStatus = PiPerformSyncDeviceAction(Reload->Devices[Index], PiActionStartDevice);
        if (!NT_SUCCESS(DeviceStatus) && NT_SUCCESS(Status))
            Status = DeviceStatus;
        ObDereferenceObject(Reload->Devices[Index]);
    }

    DPRINT1("KD: Reloaded %lu %wZ device(s), status 0x%lx\n", Reload->Count, Service, Status);
    ExFreePoolWithTag(Reload, TAG_KDBG);
    return Status;
}

static
VOID
NTAPI
KdpFileServiceThread(
    _In_ PVOID Context)
{
    LARGE_INTEGER Interval;
    UNICODE_STRING Name;
    NTSTATUS Status;
    LONG Request;

    UNREFERENCED_PARAMETER(Context);

    Interval.QuadPart = -2500000LL;
    for (;;)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);

        Request = InterlockedCompareExchange(&KdpFileRequest, KDP_FILE_REQUEST_NONE, KDP_FILE_REQUEST_NONE);
        if (Request != KD_FILE_REQUEST_PULL && Request != KD_FILE_REQUEST_RELOAD)
            continue;

        RtlInitUnicodeString(&Name, KdpFileRequestName);
        if (Request == KD_FILE_REQUEST_PULL)
            Status = KdPullRemoteFile(&Name);
        else
            Status = KdpReloadService(&Name);
        DPRINT1("KD: %s %wZ finished with 0x%lx\n", (Request == KD_FILE_REQUEST_PULL) ? "Pull of" : "Reload of",
                &Name, Status);

        InterlockedExchange(&KdpFileRequest, KDP_FILE_REQUEST_NONE);
    }
}

VOID
NTAPI
KdInitializeFileService(VOID)
{
    HANDLE ThreadHandle;
    NTSTATUS Status;

    if (!KdDebuggerEnabled || KdpFileServiceRunning)
        return;

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, KdpFileServiceThread, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("KD: File request service not started (0x%08lx)\n", Status);
        return;
    }

    KdpFileServiceRunning = TRUE;
    ZwClose(ThreadHandle);
}
