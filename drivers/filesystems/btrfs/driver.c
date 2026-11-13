/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, dispatch table and the file system device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"

BTRFS_GLOBAL BtrfsGlobal;

NTSTATUS
BtrfsCompleteRequest(
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
BtrfsIsFsDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return DeviceObject == BtrfsGlobal.FsDeviceObject;
}

PBTRFS_VCB
BtrfsDeviceVcb(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PBTRFS_VCB Vcb;

    if (DeviceObject == NULL || DeviceObject == BtrfsGlobal.FsDeviceObject) return NULL;
    if (DeviceObject->DriverObject != BtrfsGlobal.DriverObject) return NULL;

    Vcb = DeviceObject->DeviceExtension;
    return (Vcb != NULL && Vcb->Signature == BTRFS_VCB_SIGNATURE) ? Vcb : NULL;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\BtrfsFs");
    NTSTATUS Status;
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);

    RtlZeroMemory(&BtrfsGlobal, sizeof(BtrfsGlobal));
    BtrfsGlobal.DriverObject = DriverObject;
    ExInitializeResourceLite(&BtrfsGlobal.Resource);
    InitializeListHead(&BtrfsGlobal.VcbList);
    BtrfsInitializeFastIo(&BtrfsGlobal.FastIoDispatch, &BtrfsGlobal.CacheCallbacks);

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
        DriverObject->MajorFunction[Index] = BtrfsDispatchUnsupported;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = BtrfsDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = BtrfsDispatchCleanup;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = BtrfsDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = BtrfsDispatchRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = BtrfsDispatchWrite;
    DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] = BtrfsDispatchQueryInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION] = BtrfsDispatchSetInformation;
    DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = BtrfsDispatchQueryVolumeInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION] = BtrfsDispatchSetVolumeInformation;
    DriverObject->MajorFunction[IRP_MJ_DIRECTORY_CONTROL] = BtrfsDispatchDirectoryControl;
    DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = BtrfsDispatchFileSystemControl;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = BtrfsDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_LOCK_CONTROL] = BtrfsDispatchLockControl;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = BtrfsDispatchFlushBuffers;
    DriverObject->MajorFunction[IRP_MJ_PNP] = BtrfsDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = BtrfsDispatchShutdown;
    DriverObject->FastIoDispatch = &BtrfsGlobal.FastIoDispatch;

    Status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_DISK_FILE_SYSTEM, 0, FALSE, &BtrfsGlobal.FsDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        ExDeleteResourceLite(&BtrfsGlobal.Resource);
        return Status;
    }

    BtrfsGlobal.FsDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    IoRegisterFileSystem(BtrfsGlobal.FsDeviceObject);
    return STATUS_SUCCESS;
}
