/*
 * PROJECT:     LiberNT ext4 file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, dispatch table, the file system device and what lwext4 needs from its host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ext4p.h"

#include <stdarg.h>

EXT4_GLOBAL Ext4Global;

typedef struct _EXT4_ALLOCATION
{
    SIZE_T Size;
    SIZE_T Reserved;
} EXT4_ALLOCATION, *PEXT4_ALLOCATION;

/* lwext4 buffers go to the storage stack as they are, and its journal and cache run under the paging path */
void *
ext4_user_malloc(
    size_t Size)
{
    PEXT4_ALLOCATION Allocation;

    if (Size > MAXULONG - sizeof(EXT4_ALLOCATION)) return NULL;
    Allocation = ExAllocatePoolUninitialized(NonPagedPoolNx, sizeof(EXT4_ALLOCATION) + Size, TAG_EXT4_CORE);
    if (Allocation == NULL) return NULL;

    Allocation->Size = Size;
    return Allocation + 1;
}

void *
ext4_user_calloc(
    size_t Count,
    size_t Size)
{
    void *Block;

    if (Size != 0 && Count > MAXULONG / Size) return NULL;
    Block = ext4_user_malloc(Count * Size);
    if (Block != NULL) RtlZeroMemory(Block, Count * Size);
    return Block;
}

void
ext4_user_free(
    void *Block)
{
    if (Block != NULL) ExFreePoolWithTag((PEXT4_ALLOCATION)Block - 1, TAG_EXT4_CORE);
}

void *
ext4_user_realloc(
    void *Block,
    size_t Size)
{
    PEXT4_ALLOCATION Old = Block ? (PEXT4_ALLOCATION)Block - 1 : NULL;
    void *New;

    New = ext4_user_malloc(Size);
    if (New == NULL) return NULL;
    if (Old != NULL)
    {
        RtlCopyMemory(New, Block, min(Old->Size, Size));
        ext4_user_free(Block);
    }
    return New;
}

void
ext4_port_dbg(
    const char *Format,
    ...)
{
    va_list Arguments;

    va_start(Arguments, Format);
    vDbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, Format, Arguments);
    va_end(Arguments);
}

void
ext4_port_assert_fail(
    const char *File,
    int Line)
{
    DbgPrint("ext4: lwext4 assertion failed at %s:%d\n", File, Line);
    ASSERTMSG("ext4: an lwext4 assertion failed\n", FALSE);
}

NTSTATUS
Ext4CompleteRequest(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    ASSERT(Status != STATUS_PENDING);
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, NT_SUCCESS(Status) ? IO_DISK_INCREMENT : IO_NO_INCREMENT);
    return Status;
}

BOOLEAN
Ext4IsFsDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return DeviceObject == Ext4Global.FsDeviceObject;
}

PEXT4_VCB
Ext4DeviceVcb(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PEXT4_VCB Vcb;

    if (DeviceObject == NULL || DeviceObject == Ext4Global.FsDeviceObject) return NULL;
    if (DeviceObject->DriverObject != Ext4Global.DriverObject) return NULL;

    Vcb = DeviceObject->DeviceExtension;
    return (Vcb != NULL && Vcb->Signature == EXT4_VCB_SIGNATURE) ? Vcb : NULL;
}

NTSTATUS
NTAPI
Ext4DispatchUnsupported(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return Ext4CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Ext4Fs");
    NTSTATUS Status;
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);

    RtlZeroMemory(&Ext4Global, sizeof(Ext4Global));
    Ext4Global.DriverObject = DriverObject;
    ExInitializeResourceLite(&Ext4Global.Resource);
    InitializeListHead(&Ext4Global.VcbList);
    Ext4InitializeFastIo(&Ext4Global.FastIoDispatch, &Ext4Global.CacheCallbacks);

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
        DriverObject->MajorFunction[Index] = Ext4DispatchUnsupported;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = Ext4DispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = Ext4DispatchCleanup;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = Ext4DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = Ext4DispatchRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = Ext4DispatchWrite;
    DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] = Ext4DispatchQueryInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION] = Ext4DispatchSetInformation;
    DriverObject->MajorFunction[IRP_MJ_QUERY_EA] = Ext4DispatchQueryEa;
    DriverObject->MajorFunction[IRP_MJ_SET_EA] = Ext4DispatchSetEa;
    DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = Ext4DispatchQueryVolumeInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION] = Ext4DispatchSetVolumeInformation;
    DriverObject->MajorFunction[IRP_MJ_DIRECTORY_CONTROL] = Ext4DispatchDirectoryControl;
    DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = Ext4DispatchFileSystemControl;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Ext4DispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_LOCK_CONTROL] = Ext4DispatchLockControl;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = Ext4DispatchFlushBuffers;
    DriverObject->MajorFunction[IRP_MJ_QUERY_SECURITY] = Ext4DispatchQuerySecurity;
    DriverObject->MajorFunction[IRP_MJ_SET_SECURITY] = Ext4DispatchSetSecurity;
    DriverObject->MajorFunction[IRP_MJ_PNP] = Ext4DispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = Ext4DispatchShutdown;
    DriverObject->FastIoDispatch = &Ext4Global.FastIoDispatch;

    Status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_DISK_FILE_SYSTEM, 0, FALSE, &Ext4Global.FsDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        ExDeleteResourceLite(&Ext4Global.Resource);
        return Status;
    }

    Ext4Global.FsDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    IoRegisterFileSystem(Ext4Global.FsDeviceObject);
    return STATUS_SUCCESS;
}
