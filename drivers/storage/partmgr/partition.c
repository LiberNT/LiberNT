/*
 * PROJECT:     Partition manager driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Partition device code
 * COPYRIGHT:   2020 Victor Perevertkin (victor.perevertkin@reactos.org)
 */

#include "partmgr.h"

static const WCHAR PartitionSymLinkFormat[] = L"\\Device\\Harddisk%lu\\Partition%lu";


static
BOOLEAN
PartitionReleaseHold(
    _In_ PPARTITION_EXTENSION PartExt,
    _In_ NTSTATUS HoldStatus,
    _In_ BOOLEAN FailWrites)
{
    LIST_ENTRY writes;
    PLIST_ENTRY entry;
    PIRP holdIrp;
    PIRP irp;
    KIRQL oldIrql;

    InitializeListHead(&writes);

    KeAcquireSpinLock(&PartExt->HoldLock, &oldIrql);
    holdIrp = PartExt->HoldIrp;
    PartExt->HoldIrp = NULL;
    while (!IsListEmpty(&PartExt->HeldWrites))
    {
        entry = RemoveHeadList(&PartExt->HeldWrites);
        InsertTailList(&writes, entry);
    }
    KeReleaseSpinLock(&PartExt->HoldLock, oldIrql);

    if (holdIrp == NULL)
        return FALSE;

    KeCancelTimer(&PartExt->HoldTimer);

    while (!IsListEmpty(&writes))
    {
        irp = CONTAINING_RECORD(RemoveHeadList(&writes), IRP, Tail.Overlay.ListEntry);
        if (FailWrites)
            PartMgrFailIrp(irp, STATUS_DEVICE_DOES_NOT_EXIST);
        else
            PartMgrReadWrite(PartExt->DeviceObject, irp);
    }

    holdIrp->IoStatus.Status = HoldStatus;
    holdIrp->IoStatus.Information = 0;
    IoCompleteRequest(holdIrp, IO_NO_INCREMENT);
    return TRUE;
}

static
VOID
NTAPI
PartitionHoldTimeoutWorker(
    _In_ PVOID Context)
{
    PPARTITION_EXTENSION partExt = Context;

    if (PartitionReleaseHold(partExt, STATUS_IO_TIMEOUT, FALSE))
        WARN("Released the writes held on %wZ after %u seconds\n", &partExt->DeviceName, PARTMGR_HOLD_TIMEOUT_SECONDS);
    ObDereferenceObject(partExt->DeviceObject);
}

static
VOID
NTAPI
PartitionHoldTimeoutDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PPARTITION_EXTENSION partExt = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ObReferenceObject(partExt->DeviceObject);
    ExQueueWorkItem(&partExt->HoldWorkItem, DelayedWorkQueue);
}

VOID
PartitionInitializeHold(
    _In_ PPARTITION_EXTENSION PartExt)
{
    KeInitializeSpinLock(&PartExt->HoldLock);
    InitializeListHead(&PartExt->HeldWrites);
    KeInitializeTimer(&PartExt->HoldTimer);
    KeInitializeDpc(&PartExt->HoldDpc, PartitionHoldTimeoutDpc, PartExt);
    ExInitializeWorkItem(&PartExt->HoldWorkItem, PartitionHoldTimeoutWorker, PartExt);
}

BOOLEAN
PartitionQueueHeldWrite(
    _In_ PPARTITION_EXTENSION PartExt,
    _In_ PIRP Irp)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&PartExt->HoldLock, &oldIrql);
    if (PartExt->HoldIrp == NULL)
    {
        KeReleaseSpinLock(&PartExt->HoldLock, oldIrql);
        return FALSE;
    }

    IoMarkIrpPending(Irp);
    InsertTailList(&PartExt->HeldWrites, &Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&PartExt->HoldLock, oldIrql);
    return TRUE;
}

static
NTSTATUS
PartitionHoldWrites(
    _In_ PPARTITION_EXTENSION PartExt,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    LARGE_INTEGER dueTime;
    KIRQL oldIrql;

    KeAcquireSpinLock(&PartExt->HoldLock, &oldIrql);
    if (PartExt->HoldIrp != NULL)
    {
        KeReleaseSpinLock(&PartExt->HoldLock, oldIrql);
        return STATUS_DEVICE_BUSY;
    }

    IoMarkIrpPending(Irp);
    PartExt->HoldIrp = Irp;
    KeReleaseSpinLock(&PartExt->HoldLock, oldIrql);

    dueTime.QuadPart = -(LONGLONG)PARTMGR_HOLD_TIMEOUT_SECONDS * 10000000;
    KeSetTimer(&PartExt->HoldTimer, dueTime, &PartExt->HoldDpc);

    if (Irp->RequestorMode == KernelMode &&
        ioStack->Parameters.DeviceIoControl.InputBufferLength == sizeof(PKEVENT) &&
        Irp->AssociatedIrp.SystemBuffer != NULL)
    {
        KeSetEvent(*(PKEVENT *)Irp->AssociatedIrp.SystemBuffer, IO_NO_INCREMENT, FALSE);
    }

    return STATUS_PENDING;
}

CODE_SEG("PAGE")
NTSTATUS
PartitionCreateDevice(
    _In_ PDEVICE_OBJECT FDObject,
    _In_ PPARTITION_INFORMATION_EX PartitionEntry,
    _In_ UINT32 PdoNumber,
    _In_ PARTITION_STYLE PartitionStyle,
    _Out_ PDEVICE_OBJECT *PDO)
{
    PAGED_CODE();

    static LONG HarddiskVolumeLastId = 0; // this is 1-based

    WCHAR nameBuf[64];
    UNICODE_STRING deviceName;
    UINT32 volumeNum;

    volumeNum = (UINT32)InterlockedIncrement(&HarddiskVolumeLastId);
    _swprintf(nameBuf, L"\\Device\\HarddiskVolume%lu", volumeNum);
    if (!RtlCreateUnicodeString(&deviceName, nameBuf))
        return STATUS_INSUFFICIENT_RESOURCES;

    /*
     * Create the partition/volume device object.
     *
     * Due to the fact we are also a (basic) volume manager, this device is
     * ALSO a volume device. Because of this, we need to assign it a device
     * name, and a specific device type for IoCreateDevice() to create a VPB
     * for this device, so that a filesystem can be mounted on it.
     * Once we get a separate volume manager, this partition DO can become
     * anonymous, have a different device type, and without any associated VPB.
     * (The attached volume, on the contrary, would require a VPB.)
     */
    PDEVICE_OBJECT partitionDevice;
    NTSTATUS status = IoCreateDevice(FDObject->DriverObject,
                                     sizeof(PARTITION_EXTENSION),
                                     &deviceName,
                                     FILE_DEVICE_DISK, // FILE_DEVICE_MASS_STORAGE,
                                     FILE_DEVICE_SECURE_OPEN,
                                     FALSE,
                                     &partitionDevice);
    if (!NT_SUCCESS(status))
    {
        ERR("Unable to create device object %wZ\n", &deviceName);
        RtlFreeUnicodeString(&deviceName);
        return status;
    }

    INFO("Created device object %p %wZ\n", partitionDevice, &deviceName);

    PPARTITION_EXTENSION partExt = partitionDevice->DeviceExtension;
    RtlZeroMemory(partExt, sizeof(*partExt));

    partExt->DeviceObject = partitionDevice;
    partExt->LowerDevice = FDObject;
    partExt->VerifyGeneration = ((PFDO_EXTENSION)FDObject->DeviceExtension)->VerifyGeneration;
    ObReferenceObject(FDObject);
    IoInitializeRemoveLock(&partExt->RemoveLock, TAG_PARTMGR, 0, 0);

    // NOTE: See comment above.
    // PFDO_EXTENSION fdoExtension = FDObject->DeviceExtension;
    // partitionDevice->DeviceType = /*fdoExtension->LowerDevice*/FDObject->DeviceType;

    // Both the PDO and parent FDO now install a completion routine.
    partitionDevice->StackSize = FDObject->StackSize + 1;
    partitionDevice->AlignmentRequirement = FDObject->AlignmentRequirement;
    partitionDevice->Flags |= DO_DIRECT_IO;

    if (PartitionStyle == PARTITION_STYLE_MBR)
    {
        partExt->Mbr.PartitionType = PartitionEntry->Mbr.PartitionType;
        partExt->Mbr.BootIndicator = PartitionEntry->Mbr.BootIndicator;
        partExt->Mbr.RecognizedPartition = PartitionEntry->Mbr.RecognizedPartition;
        partExt->Mbr.HiddenSectors = PartitionEntry->Mbr.HiddenSectors;
    }
    else
    {
        partExt->Gpt.PartitionType = PartitionEntry->Gpt.PartitionType;
        partExt->Gpt.PartitionId = PartitionEntry->Gpt.PartitionId;
        partExt->Gpt.Attributes = PartitionEntry->Gpt.Attributes;

        RtlCopyMemory(partExt->Gpt.Name, PartitionEntry->Gpt.Name, sizeof(partExt->Gpt.Name));
    }

    partExt->DeviceName = deviceName;
    partExt->StartingOffset = PartitionEntry->StartingOffset.QuadPart;
    partExt->PartitionLength = PartitionEntry->PartitionLength.QuadPart;
    partExt->OnDiskNumber = PartitionEntry->PartitionNumber; // the "physical" partition number
    PartitionInitializeHold(partExt);
    partExt->DetectedNumber = PdoNumber; // counts only partitions with PDO created
    partExt->VolumeNumber = volumeNum;

    // The device is initialized
    partitionDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    *PDO = partitionDevice;
    return status;
}

CODE_SEG("PAGE")
VOID
PartitionDeleteSymlink(
    _In_ PPARTITION_EXTENSION PartExt)
{
    WCHAR nameBuf[64];
    UNICODE_STRING partitionSymlink;
    PFDO_EXTENSION fdoExtension = PartExt->LowerDevice->DeviceExtension;
    NTSTATUS status;

    PAGED_CODE();

    if (!PartExt->SymlinkCreated)
        return;

    _swprintf(nameBuf, PartitionSymLinkFormat,
        fdoExtension->DiskData.DeviceNumber, PartExt->DetectedNumber);

    RtlInitUnicodeString(&partitionSymlink, nameBuf);

    status = IoDeleteSymbolicLink(&partitionSymlink);
    if (!NT_SUCCESS(status))
    {
        ERR("Failed to remove partition symlink: 0x%08lx\n", status);
    }
    PartExt->SymlinkCreated = FALSE;

    INFO("Symlink removed %wZ -> %wZ\n", &partitionSymlink, &PartExt->DeviceName);
}

static
CODE_SEG("PAGE")
NTSTATUS
PartitionHandleStartDevice(
    _In_ PPARTITION_EXTENSION PartExt,
    _In_ PIRP Irp)
{
    PAGED_CODE();

    // first, create a symbolic link for our device
    WCHAR nameBuf[64];
    UNICODE_STRING partitionSymlink, interfaceName;
    PFDO_EXTENSION fdoExtension = PartExt->LowerDevice->DeviceExtension;

    // \\Device\\Harddisk%lu\\Partition%lu
    _swprintf(nameBuf, PartitionSymLinkFormat,
        fdoExtension->DiskData.DeviceNumber, PartExt->DetectedNumber);

    RtlInitUnicodeString(&partitionSymlink, nameBuf);

    NTSTATUS status = IoCreateSymbolicLink(&partitionSymlink, &PartExt->DeviceName);

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    PartExt->SymlinkCreated = TRUE;

    INFO("Symlink created %wZ -> %wZ\n", &partitionSymlink, &PartExt->DeviceName);

    // Our partition device will have two interfaces:
    // GUID_DEVINTERFACE_PARTITION and GUID_DEVINTERFACE_VOLUME
    // (aka. MOUNTDEV_MOUNTED_DEVICE_GUID).
    // The latter one is used to notify MountMgr about the new volume.

    status = IoRegisterDeviceInterface(PartExt->DeviceObject,
                                       &GUID_DEVINTERFACE_PARTITION,
                                       NULL,
                                       &interfaceName);
    if (!NT_SUCCESS(status))
    {
        PartitionDeleteSymlink(PartExt);
        return status;
    }

    INFO("Partition interface %wZ\n", &interfaceName);
    PartExt->PartitionInterfaceName = interfaceName;
    status = IoSetDeviceInterfaceState(&interfaceName, TRUE);
    if (!NT_SUCCESS(status))
    {
        RtlFreeUnicodeString(&interfaceName);
        RtlInitUnicodeString(&PartExt->PartitionInterfaceName, NULL);
        PartitionDeleteSymlink(PartExt);
        return status;
    }

    if (fdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_GPT &&
        IsEqualGUID(&PartExt->Gpt.PartitionType, &PARTITION_MSFT_RESERVED_GUID))
    {
        return STATUS_SUCCESS;
    }

    status = IoRegisterDeviceInterface(PartExt->DeviceObject,
                                       &GUID_DEVINTERFACE_VOLUME,
                                       NULL,
                                       &interfaceName);
    if (!NT_SUCCESS(status))
    {
        if (!NT_SUCCESS(IoSetDeviceInterfaceState(&PartExt->PartitionInterfaceName, FALSE)))
            ERR("Failed to disable partition interface\n");
        RtlFreeUnicodeString(&PartExt->PartitionInterfaceName);
        RtlInitUnicodeString(&PartExt->PartitionInterfaceName, NULL);
        PartitionDeleteSymlink(PartExt);
        return status;
    }

    INFO("Volume interface %wZ\n", &interfaceName);
    PartExt->VolumeInterfaceName = interfaceName;
    status = IoSetDeviceInterfaceState(&interfaceName, TRUE);
    if (!NT_SUCCESS(status))
    {
        RtlFreeUnicodeString(&interfaceName);
        RtlInitUnicodeString(&PartExt->VolumeInterfaceName, NULL);
        if (!NT_SUCCESS(IoSetDeviceInterfaceState(&PartExt->PartitionInterfaceName, FALSE)))
            ERR("Failed to disable partition interface\n");
        RtlFreeUnicodeString(&PartExt->PartitionInterfaceName);
        RtlInitUnicodeString(&PartExt->PartitionInterfaceName, NULL);
        PartitionDeleteSymlink(PartExt);
        return status;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief
 * Notifies MountMgr to delete all mount points
 * associated with the given volume.
 *
 * @note    This should belong to volmgr.sys and act on a PVOLUME_EXTENSION.
 **/
static
CODE_SEG("PAGE")
NTSTATUS
VolumeDeleteMountPoints(
    _In_ PPARTITION_EXTENSION PartExt)
{
    NTSTATUS Status;
    UNICODE_STRING MountMgr;
    ULONG InputSize, OutputSize;
    LOGICAL Retry;
    PUNICODE_STRING DeviceName;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject = NULL;
    PMOUNTMGR_MOUNT_POINT InputBuffer = NULL;
    PMOUNTMGR_MOUNT_POINTS OutputBuffer = NULL;

    PAGED_CODE();

    /* Get the device pointer to the MountMgr */
    RtlInitUnicodeString(&MountMgr, MOUNTMGR_DEVICE_NAME);
    Status = IoGetDeviceObjectPointer(&MountMgr,
                                      FILE_READ_ATTRIBUTES,
                                      &FileObject,
                                      &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Setup the volume device name for deleting its mount points */
    DeviceName = &PartExt->DeviceName;

    /* Allocate the input buffer */
    InputSize = sizeof(*InputBuffer) + DeviceName->Length;
    InputBuffer = ExAllocatePoolWithTag(PagedPool, InputSize, TAG_PARTMGR);
    if (!InputBuffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Quit;
    }

    /* Fill it in */
    RtlZeroMemory(InputBuffer, sizeof(*InputBuffer));
    InputBuffer->DeviceNameOffset = sizeof(*InputBuffer);
    InputBuffer->DeviceNameLength = DeviceName->Length;
    RtlCopyMemory(&InputBuffer[1], DeviceName->Buffer, DeviceName->Length);

    /*
     * IOCTL_MOUNTMGR_DELETE_POINTS needs a large-enough scratch output buffer
     * to work with. (It uses it to query the mount points, before deleting
     * them.) Start with a guessed size and call the IOCTL. If the buffer is
     * not big enough, use the value retrieved in MOUNTMGR_MOUNT_POINTS::Size
     * to re-allocate a larger buffer and call the IOCTL once more.
     */
    OutputSize = max(PAGE_SIZE, sizeof(*OutputBuffer));
    for (Retry = 0; Retry < 2; ++Retry)
    {
        OutputBuffer = ExAllocatePoolWithTag(PagedPool, OutputSize, TAG_PARTMGR);
        if (!OutputBuffer)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        /* Call the MountMgr to delete the drive letter */
        Status = IssueSyncIoControlRequest(IOCTL_MOUNTMGR_DELETE_POINTS,
                                           DeviceObject,
                                           InputBuffer,
                                           InputSize,
                                           OutputBuffer,
                                           OutputSize,
                                           FALSE);

        /* Adjust the allocation size if it was too small */
        if (Status == STATUS_BUFFER_OVERFLOW)
        {
            OutputSize = OutputBuffer->Size;
            ExFreePoolWithTag(OutputBuffer, TAG_PARTMGR);
            continue;
        }
        /* Success or failure: stop the loop */
        break;
    }

Quit:
    if (OutputBuffer)
        ExFreePoolWithTag(OutputBuffer, TAG_PARTMGR);
    if (InputBuffer)
        ExFreePoolWithTag(InputBuffer, TAG_PARTMGR);
    if (FileObject)
        ObDereferenceObject(FileObject);
    return Status;
}

CODE_SEG("PAGE")
NTSTATUS
PartitionHandleRemove(
    _In_ PPARTITION_EXTENSION PartExt,
    _In_ BOOLEAN FinalRemove)
{
    NTSTATUS status;
    PFDO_EXTENSION fdoExtension = PartExt->LowerDevice->DeviceExtension;

    PAGED_CODE();

    if (PartExt->VolumeInterfaceName.Buffer && !PartExt->Removed && !fdoExtension->Removed)
    {
        /* Notify MountMgr to delete all associated mount points.
         * MountMgr does not automatically remove these in order to support
         * drive letter persistence for online/offline volume transitions,
         * or volumes arrival/removal on removable devices. */
        status = VolumeDeleteMountPoints(PartExt);
        if (!NT_SUCCESS(status))
        {
            ERR("VolumeDeleteMountPoints(%wZ) failed with status 0x%08lx\n",
                &PartExt->DeviceName, status);
            /* Failure isn't major, continue proceeding with volume removal */
        }
    }

    InterlockedExchange(&PartExt->Removed, TRUE);
    PartitionCancelUniqueIdChangeNotify(PartExt);
    PartitionReleaseHold(PartExt, STATUS_DEVICE_DOES_NOT_EXIST, TRUE);
    if (FinalRemove)
    {
        IoAcquireRemoveLock(&PartExt->RemoveLock, PartExt);
        IoReleaseRemoveLockAndWait(&PartExt->RemoveLock, PartExt);

        if (PartExt->Attached)
        {
            PartMgrAcquireLayoutLock(fdoExtension);
            PSINGLE_LIST_ENTRY previous = &fdoExtension->PartitionList;
            while (previous->Next && previous->Next != &PartExt->ListEntry)
                previous = previous->Next;
            if (previous->Next)
            {
                previous->Next = PartExt->ListEntry.Next;
                fdoExtension->EnumeratedPartitionsTotal--;
            }
            PartExt->Attached = FALSE;
            PartMgrReleaseLayoutLock(fdoExtension);
        }
    }

    // remove the symbolic link
    PartitionDeleteSymlink(PartExt);

    // release device interfaces
    if (PartExt->PartitionInterfaceName.Buffer)
    {
        status = IoSetDeviceInterfaceState(&PartExt->PartitionInterfaceName, FALSE);
        if (!NT_SUCCESS(status))
        {
            ERR("Failed to disable partition interface: 0x%08lx\n", status);
        }
        RtlFreeUnicodeString(&PartExt->PartitionInterfaceName);
        RtlInitUnicodeString(&PartExt->PartitionInterfaceName, NULL);
    }

    if (PartExt->VolumeInterfaceName.Buffer)
    {
        /* Notify MountMgr of volume removal */
        status = IoSetDeviceInterfaceState(&PartExt->VolumeInterfaceName, FALSE);
        if (!NT_SUCCESS(status))
        {
            ERR("Failed to disable volume interface: 0x%08lx\n", status);
        }
        RtlFreeUnicodeString(&PartExt->VolumeInterfaceName);
        RtlInitUnicodeString(&PartExt->VolumeInterfaceName, NULL);
    }

    if (FinalRemove)
    {
        ASSERT(PartExt->DeviceName.Buffer);
        if (PartExt->DeviceName.Buffer)
        {
            INFO("Removed device %wZ\n", &PartExt->DeviceName);
            RtlFreeUnicodeString(&PartExt->DeviceName);
        }

        PDEVICE_OBJECT parent = PartExt->LowerDevice;
        IoDeleteDevice(PartExt->DeviceObject);
        ObDereferenceObject(parent);
    }

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
PartitionHandleDeviceRelations(
    _In_ PPARTITION_EXTENSION PartExt,
    _In_ PIRP Irp)
{
    PAGED_CODE();

    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    DEVICE_RELATION_TYPE type = ioStack->Parameters.QueryDeviceRelations.Type;

    if (type == TargetDeviceRelation)
    {
        // Device relations have one entry built into their size.
        PDEVICE_RELATIONS deviceRelations =
            ExAllocatePoolZero(PagedPool, sizeof(DEVICE_RELATIONS), TAG_PARTMGR);

        if (deviceRelations != NULL)
        {
            deviceRelations->Count = 1;
            deviceRelations->Objects[0] = PartExt->DeviceObject;
            ObReferenceObject(deviceRelations->Objects[0]);

            Irp->IoStatus.Information = (ULONG_PTR)deviceRelations;
            return STATUS_SUCCESS;
        }
        else
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        Irp->IoStatus.Information = 0;
        return Irp->IoStatus.Status;
    }
}

static
CODE_SEG("PAGE")
NTSTATUS
PartitionHandleQueryId(
    _In_ PPARTITION_EXTENSION PartExt,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    BUS_QUERY_ID_TYPE idType = ioStack->Parameters.QueryId.IdType;
    UNICODE_STRING idString;
    NTSTATUS status;

    PAGED_CODE();

    switch (idType)
    {
        case BusQueryDeviceID:
            status = RtlCreateUnicodeString(&idString, L"STORAGE\\Partition")
                     ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
            break;
        case BusQueryHardwareIDs:
        {
            static WCHAR volumeID[] = L"STORAGE\\Volume\0";

            idString.Buffer = ExAllocatePoolWithTag(PagedPool, sizeof(volumeID), TAG_PARTMGR);
            if (!idString.Buffer)
            {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            RtlCopyMemory(idString.Buffer, volumeID, sizeof(volumeID));

            status = STATUS_SUCCESS;
            break;
        }
        case BusQueryInstanceID:
        {
            WCHAR string[128];
            PFDO_EXTENSION fdoExtension = PartExt->LowerDevice->DeviceExtension;

            PartMgrAcquireLayoutLock(fdoExtension);

            if (fdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_MBR)
            {
                _swprintf(string, L"S%08lx_O%I64x_L%I64x",
                          fdoExtension->DiskData.Mbr.Signature,
                          PartExt->StartingOffset,
                          PartExt->PartitionLength);
            }
            else
            {
                _swprintf(string,
                          L"S%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02xS_O%I64x_L%I64x"
                          L"_P%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
                          fdoExtension->DiskData.Gpt.DiskId.Data1,
                          fdoExtension->DiskData.Gpt.DiskId.Data2,
                          fdoExtension->DiskData.Gpt.DiskId.Data3,
                          fdoExtension->DiskData.Gpt.DiskId.Data4[0],
                          fdoExtension->DiskData.Gpt.DiskId.Data4[1],
                          fdoExtension->DiskData.Gpt.DiskId.Data4[2],
                          fdoExtension->DiskData.Gpt.DiskId.Data4[3],
                          fdoExtension->DiskData.Gpt.DiskId.Data4[4],
                          fdoExtension->DiskData.Gpt.DiskId.Data4[5],
                          fdoExtension->DiskData.Gpt.DiskId.Data4[6],
                          fdoExtension->DiskData.Gpt.DiskId.Data4[7],
                          PartExt->StartingOffset,
                          PartExt->PartitionLength,
                          PartExt->Gpt.PartitionId.Data1,
                          PartExt->Gpt.PartitionId.Data2,
                          PartExt->Gpt.PartitionId.Data3,
                          PartExt->Gpt.PartitionId.Data4[0],
                          PartExt->Gpt.PartitionId.Data4[1],
                          PartExt->Gpt.PartitionId.Data4[2],
                          PartExt->Gpt.PartitionId.Data4[3],
                          PartExt->Gpt.PartitionId.Data4[4],
                          PartExt->Gpt.PartitionId.Data4[5],
                          PartExt->Gpt.PartitionId.Data4[6],
                          PartExt->Gpt.PartitionId.Data4[7]);
            }

            PartMgrReleaseLayoutLock(fdoExtension);

            status = RtlCreateUnicodeString(&idString, string)
                     ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
        default:
            status = STATUS_NOT_SUPPORTED;
            break;
    }

    Irp->IoStatus.Information = NT_SUCCESS(status) ? (ULONG_PTR) idString.Buffer : 0;
    return status;
}

static
CODE_SEG("PAGE")
NTSTATUS
PartitionHandleQueryCapabilities(
    _In_ PPARTITION_EXTENSION PartExt,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_CAPABILITIES devCaps = ioStack->Parameters.DeviceCapabilities.Capabilities;

    PAGED_CODE();
    ASSERT(devCaps);

    devCaps->SilentInstall = TRUE;
    devCaps->RawDeviceOK = TRUE;
    devCaps->NoDisplayInUI = TRUE;
    devCaps->Address = PartExt->OnDiskNumber;
    devCaps->UniqueID = FALSE;

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
PartitionHandlePnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PPARTITION_EXTENSION partExt = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;

    PAGED_CODE();

    switch (ioStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            status = PartitionHandleStartDevice(partExt, Irp);
            break;
        }
        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            status = PartitionHandleDeviceRelations(partExt, Irp);
            break;
        }
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_STOP_DEVICE:
        {
            status = STATUS_SUCCESS;
            break;
        }
        case IRP_MN_SURPRISE_REMOVAL:
        {
            status = PartitionHandleRemove(partExt, FALSE);
            break;
        }
        case IRP_MN_REMOVE_DEVICE:
        {
            status = PartitionHandleRemove(partExt, TRUE);
            break;
        }
        case IRP_MN_QUERY_ID:
        {
            status = PartitionHandleQueryId(partExt, Irp);
            break;
        }
        case IRP_MN_QUERY_CAPABILITIES:
        {
            status = PartitionHandleQueryCapabilities(partExt, Irp);
            break;
        }
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
        {
            return ForwardIrpAndForget(DeviceObject, Irp);
        }
        default:
        {
            status = Irp->IoStatus.Status;
        }
    }

    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

static
USHORT
PartitionGetBasicUniqueId(
    _In_ PPARTITION_EXTENSION PartExt,
    _In_ PFDO_EXTENSION FdoExtension,
    _Out_ PBASIC_VOLUME_UNIQUE_ID UniqueId)
{
    if ((FdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_MBR) &&
        !FdoExtension->IsSuperFloppy)
    {
        UniqueId->Mbr.Signature = FdoExtension->DiskData.Mbr.Signature;
        UniqueId->Mbr.StartingOffset = PartExt->StartingOffset;
        return sizeof(UniqueId->Mbr);
    }

    if (FdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_GPT)
    {
        UniqueId->Gpt.Signature = DMIO_ID_SIGNATURE;
        UniqueId->Gpt.PartitionGuid = PartExt->Gpt.PartitionId;
        return sizeof(UniqueId->Gpt);
    }

    return 0;
}

static
NTSTATUS
PartitionFillUniqueIdChange(
    _Inout_ PIRP Irp,
    _In_reads_bytes_(NewLength) PVOID NewId,
    _In_ USHORT NewLength)
{
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    PMOUNTDEV_UNIQUE_ID_CHANGE_NOTIFY_OUTPUT output = Irp->AssociatedIrp.SystemBuffer;
    USHORT oldLength = ((PMOUNTDEV_UNIQUE_ID)output)->UniqueIdLength;
    ULONG size = sizeof(*output) + oldLength + NewLength;

    if (size > ioStack->Parameters.DeviceIoControl.OutputBufferLength)
    {
        output->Size = size;
        Irp->IoStatus.Information = sizeof(output->Size);
        return STATUS_BUFFER_OVERFLOW;
    }

    RtlMoveMemory((PUCHAR)output + sizeof(*output), ((PMOUNTDEV_UNIQUE_ID)output)->UniqueId, oldLength);
    RtlCopyMemory((PUCHAR)output + sizeof(*output) + oldLength, NewId, NewLength);
    output->Size = size;
    output->OldUniqueIdOffset = sizeof(*output);
    output->OldUniqueIdLength = oldLength;
    output->NewUniqueIdOffset = (USHORT)(sizeof(*output) + oldLength);
    output->NewUniqueIdLength = NewLength;
    Irp->IoStatus.Information = size;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
PartitionCancelUniqueIdChange(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ _IRQL_uses_cancel_ PIRP Irp)
{
    PPARTITION_EXTENSION partExt = DeviceObject->DeviceExtension;

    if (partExt->UniqueIdChangeIrp == Irp)
        partExt->UniqueIdChangeIrp = NULL;
    IoReleaseCancelSpinLock(Irp->CancelIrql);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
PIRP
PartitionTakeUniqueIdChangeIrp(
    _In_ PPARTITION_EXTENSION PartExt)
{
    KIRQL oldIrql;
    PIRP irp;

    IoAcquireCancelSpinLock(&oldIrql);
    irp = PartExt->UniqueIdChangeIrp;
    if (irp && IoSetCancelRoutine(irp, NULL))
        PartExt->UniqueIdChangeIrp = NULL;
    else
        irp = NULL;
    IoReleaseCancelSpinLock(oldIrql);
    return irp;
}

VOID
PartitionCancelUniqueIdChangeNotify(
    _In_ PPARTITION_EXTENSION PartExt)
{
    PIRP irp = PartitionTakeUniqueIdChangeIrp(PartExt);

    if (irp)
    {
        irp->IoStatus.Status = STATUS_CANCELLED;
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
    }
}

VOID
PartitionNotifyUniqueIdChange(
    _In_ PPARTITION_EXTENSION PartExt)
{
    BASIC_VOLUME_UNIQUE_ID uniqueId;
    USHORT length;
    PIRP irp;

    length = PartitionGetBasicUniqueId(PartExt, PartExt->LowerDevice->DeviceExtension, &uniqueId);
    if (length == 0)
        return;

    irp = PartitionTakeUniqueIdChangeIrp(PartExt);
    if (!irp)
        return;

    irp->IoStatus.Status = PartitionFillUniqueIdChange(irp, &uniqueId, length);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
}

static
NTSTATUS
PartitionHandleDataSetAttributes(
    _In_ PPARTITION_EXTENSION PartExt,
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_MANAGE_DATA_SET_ATTRIBUTES attributes = Irp->AssociatedIrp.SystemBuffer;
    ULONG inLength = ioStack->Parameters.DeviceIoControl.InputBufferLength;
    PDEVICE_MANAGE_DATA_SET_ATTRIBUTES translated;
    PDEVICE_DATA_SET_RANGE ranges;
    IO_STATUS_BLOCK ioStatus;
    UINT64 partOffset, partLength;
    ULONG translatedLength, rangesOffset = 0, rangeCount, i;
    KEVENT event;
    NTSTATUS status;
    PIRP irp;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;
    if (inLength < sizeof(*attributes) ||
        attributes->Size < sizeof(*attributes) ||
        attributes->ParameterBlockOffset > inLength ||
        attributes->ParameterBlockLength > inLength - attributes->ParameterBlockOffset ||
        attributes->DataSetRangesOffset > inLength ||
        attributes->DataSetRangesLength > inLength - attributes->DataSetRangesOffset ||
        (attributes->DataSetRangesLength % sizeof(DEVICE_DATA_SET_RANGE)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    PartMgrAcquireLayoutLock(FdoExtension);
    partOffset = PartExt->StartingOffset;
    partLength = PartExt->PartitionLength;
    PartMgrReleaseLayoutLock(FdoExtension);

    if (attributes->Flags & DEVICE_DSM_FLAG_ENTIRE_DATA_SET_RANGE)
    {
        if (attributes->DataSetRangesLength != 0)
            return STATUS_INVALID_PARAMETER;

        rangesOffset = max((ULONG)sizeof(*attributes),
                           attributes->ParameterBlockOffset + attributes->ParameterBlockLength);
        rangesOffset = ALIGN_UP_BY(rangesOffset, sizeof(ULONGLONG));
        translatedLength = rangesOffset + sizeof(DEVICE_DATA_SET_RANGE);
    }
    else
    {
        translatedLength = inLength;
    }

    translated = ExAllocatePoolZero(NonPagedPool, translatedLength, TAG_PARTMGR);
    if (!translated)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(translated, attributes, min(inLength, translatedLength));
    if (attributes->Flags & DEVICE_DSM_FLAG_ENTIRE_DATA_SET_RANGE)
    {
        translated->Flags &= ~DEVICE_DSM_FLAG_ENTIRE_DATA_SET_RANGE;
        translated->DataSetRangesOffset = rangesOffset;
        translated->DataSetRangesLength = sizeof(DEVICE_DATA_SET_RANGE);
        ranges = (PDEVICE_DATA_SET_RANGE)((PUCHAR)translated + rangesOffset);
        ranges->StartingOffset = (LONGLONG)partOffset;
        ranges->LengthInBytes = partLength;
    }
    else
    {
        ranges = (PDEVICE_DATA_SET_RANGE)((PUCHAR)translated + translated->DataSetRangesOffset);
        rangeCount = translated->DataSetRangesLength / sizeof(DEVICE_DATA_SET_RANGE);
        for (i = 0; i < rangeCount; i++)
        {
            if (ranges[i].StartingOffset < 0 ||
                (UINT64)ranges[i].StartingOffset > partLength ||
                ranges[i].LengthInBytes > partLength - (UINT64)ranges[i].StartingOffset)
            {
                ExFreePoolWithTag(translated, TAG_PARTMGR);
                return STATUS_INVALID_PARAMETER;
            }
            ranges[i].StartingOffset += (LONGLONG)partOffset;
        }
    }

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    irp = IoBuildDeviceIoControlRequest(IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES,
                                        PartExt->LowerDevice,
                                        translated,
                                        translatedLength,
                                        Irp->AssociatedIrp.SystemBuffer,
                                        ioStack->Parameters.DeviceIoControl.OutputBufferLength,
                                        FALSE,
                                        &event,
                                        &ioStatus);
    if (!irp)
    {
        ExFreePoolWithTag(translated, TAG_PARTMGR);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = IoCallDriver(PartExt->LowerDevice, irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }

    ExFreePoolWithTag(translated, TAG_PARTMGR);
    if (NT_SUCCESS(status))
        Irp->IoStatus.Information = ioStatus.Information;
    return status;
}

NTSTATUS
PartitionHandleDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    PPARTITION_EXTENSION partExt = DeviceObject->DeviceExtension;
    PFDO_EXTENSION fdoExtension = partExt->LowerDevice->DeviceExtension;
    NTSTATUS status;

    ASSERT(!partExt->IsFDO);

    if (!partExt->IsEnumerated)
    {
        Irp->IoStatus.Status = STATUS_DEVICE_DOES_NOT_EXIST;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    switch (ioStack->Parameters.DeviceIoControl.IoControlCode)
    {
        // disk stuff
        case IOCTL_DISK_GET_PARTITION_INFO:
        {
            if (!VerifyIrpOutBufferSize(Irp, sizeof(PARTITION_INFORMATION)))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PartMgrAcquireLayoutLock(fdoExtension);

            // not supported on anything other than MBR
            if (fdoExtension->DiskData.PartitionStyle != PARTITION_STYLE_MBR)
            {
                status = STATUS_INVALID_DEVICE_REQUEST;
                PartMgrReleaseLayoutLock(fdoExtension);
                break;
            }

            PPARTITION_INFORMATION partInfo = Irp->AssociatedIrp.SystemBuffer;

            *partInfo = (PARTITION_INFORMATION){
                .PartitionType = partExt->Mbr.PartitionType,
                .StartingOffset.QuadPart = partExt->StartingOffset,
                .PartitionLength.QuadPart = partExt->PartitionLength,
                .HiddenSectors = partExt->Mbr.HiddenSectors,
                .PartitionNumber = partExt->DetectedNumber,
                .BootIndicator = partExt->Mbr.BootIndicator,
                .RecognizedPartition = partExt->Mbr.RecognizedPartition,
                .RewritePartition = FALSE,
            };

            PartMgrReleaseLayoutLock(fdoExtension);

            Irp->IoStatus.Information = sizeof(*partInfo);
            status = STATUS_SUCCESS;
            break;
        }
        case IOCTL_DISK_GET_PARTITION_INFO_EX:
        {
            if (!VerifyIrpOutBufferSize(Irp, sizeof(PARTITION_INFORMATION_EX)))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PPARTITION_INFORMATION_EX partInfoEx = Irp->AssociatedIrp.SystemBuffer;

            PartMgrAcquireLayoutLock(fdoExtension);

            *partInfoEx = (PARTITION_INFORMATION_EX){
                .StartingOffset.QuadPart = partExt->StartingOffset,
                .PartitionLength.QuadPart = partExt->PartitionLength,
                .PartitionNumber = partExt->DetectedNumber,
                .PartitionStyle = fdoExtension->DiskData.PartitionStyle,
                .RewritePartition = FALSE,
            };

            if (fdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_MBR)
            {
                partInfoEx->Mbr = (PARTITION_INFORMATION_MBR){
                    .PartitionType = partExt->Mbr.PartitionType,
                    .HiddenSectors = partExt->Mbr.HiddenSectors,
                    .BootIndicator = partExt->Mbr.BootIndicator,
                    .RecognizedPartition = partExt->Mbr.RecognizedPartition,
                };
            }
            else
            {
                partInfoEx->Gpt = (PARTITION_INFORMATION_GPT){
                    .PartitionType = partExt->Gpt.PartitionType,
                    .PartitionId = partExt->Gpt.PartitionId,
                    .Attributes = partExt->Gpt.Attributes,
                };

                RtlCopyMemory(partInfoEx->Gpt.Name,
                              partExt->Gpt.Name,
                              sizeof(partInfoEx->Gpt.Name));
            }

            PartMgrReleaseLayoutLock(fdoExtension);

            Irp->IoStatus.Information = sizeof(*partInfoEx);
            status = STATUS_SUCCESS;
            break;
        }
        case IOCTL_DISK_SET_PARTITION_INFO:
        {
            PSET_PARTITION_INFORMATION inputBuffer = Irp->AssociatedIrp.SystemBuffer;
            if (!VerifyIrpInBufferSize(Irp, sizeof(*inputBuffer)))
            {
                status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            PartMgrAcquireLayoutLock(fdoExtension);

            // these functions use on disk numbers, not detected ones
            status = IoSetPartitionInformation(fdoExtension->LowerDevice,
                                               fdoExtension->DiskData.BytesPerSector,
                                               partExt->OnDiskNumber,
                                               inputBuffer->PartitionType);

            if (NT_SUCCESS(status))
            {
                partExt->Mbr.PartitionType = inputBuffer->PartitionType;
                fdoExtension->LayoutValid = FALSE;
            }

            PartMgrReleaseLayoutLock(fdoExtension);

            Irp->IoStatus.Information = 0;
            break;
        }
        case IOCTL_DISK_SET_PARTITION_INFO_EX:
        {
            PSET_PARTITION_INFORMATION_EX inputBuffer = Irp->AssociatedIrp.SystemBuffer;
            if (!VerifyIrpInBufferSize(Irp, sizeof(*inputBuffer)))
            {
                status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            PartMgrAcquireLayoutLock(fdoExtension);

            // these functions use on disk numbers, not detected ones
            status = IoSetPartitionInformationEx(fdoExtension->LowerDevice,
                                                 partExt->OnDiskNumber,
                                                 inputBuffer);

            BOOLEAN identityChanged = FALSE;

            if (NT_SUCCESS(status))
            {
                fdoExtension->LayoutValid = FALSE;

                if (fdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_MBR)
                {
                    partExt->Mbr.PartitionType = inputBuffer->Mbr.PartitionType;
                }
                else if (!IsEqualGUID(&partExt->Gpt.PartitionId, &inputBuffer->Gpt.PartitionId))
                {
                    identityChanged = TRUE;
                }
                else
                {
                    partExt->Gpt.PartitionType = inputBuffer->Gpt.PartitionType;
                    partExt->Gpt.Attributes = inputBuffer->Gpt.Attributes;

                    RtlMoveMemory(partExt->Gpt.Name,
                                  inputBuffer->Gpt.Name,
                                  sizeof(partExt->Gpt.Name));
                }
            }

            PartMgrReleaseLayoutLock(fdoExtension);

            if (identityChanged)
                IoInvalidateDeviceRelations(fdoExtension->PhysicalDiskDO, BusRelations);

            Irp->IoStatus.Information = 0;
            break;
        }
        case IOCTL_DISK_GET_LENGTH_INFO:
        {
            PGET_LENGTH_INFORMATION lengthInfo = Irp->AssociatedIrp.SystemBuffer;
            if (!VerifyIrpOutBufferSize(Irp, sizeof(*lengthInfo)))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PartMgrAcquireLayoutLock(fdoExtension);

            lengthInfo->Length.QuadPart = partExt->PartitionLength;

            PartMgrReleaseLayoutLock(fdoExtension);

            status = STATUS_SUCCESS;
            Irp->IoStatus.Information = sizeof(*lengthInfo);
            break;
        }
        case IOCTL_DISK_VERIFY:
        {
            PVERIFY_INFORMATION verifyInfo = Irp->AssociatedIrp.SystemBuffer;
            if (!VerifyIrpInBufferSize(Irp, sizeof(*verifyInfo)))
            {
                status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            UINT64 verifyOffset = (UINT64)verifyInfo->StartingOffset.QuadPart;

            if (verifyOffset > partExt->PartitionLength ||
                verifyInfo->Length > partExt->PartitionLength - verifyOffset)
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            // Partition device should just adjust the starting offset
            verifyInfo->StartingOffset.QuadPart += partExt->StartingOffset;
            return ForwardIrpAndForget(DeviceObject, Irp);
        }
        case IOCTL_DISK_UPDATE_PROPERTIES:
        {
            fdoExtension->LayoutValid = FALSE;
            IoInvalidateDeviceRelations(fdoExtension->PhysicalDiskDO, BusRelations);

            status = STATUS_SUCCESS;
            break;
        }
        case IOCTL_STORAGE_GET_DEVICE_NUMBER:
        {
            PSTORAGE_DEVICE_NUMBER deviceNumber = Irp->AssociatedIrp.SystemBuffer;
            if (!VerifyIrpOutBufferSize(Irp, sizeof(*deviceNumber)))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PartMgrAcquireLayoutLock(fdoExtension);

            deviceNumber->DeviceType = partExt->DeviceObject->DeviceType;
            deviceNumber->DeviceNumber = fdoExtension->DiskData.DeviceNumber;
            deviceNumber->PartitionNumber = partExt->DetectedNumber;

            PartMgrReleaseLayoutLock(fdoExtension);

            status = STATUS_SUCCESS;
            Irp->IoStatus.Information = sizeof(*deviceNumber);
            break;
        }
        case IOCTL_STORAGE_MEDIA_REMOVAL:
        {
            return ForwardIrpAndForget(DeviceObject, Irp);
        }
        // volume stuff (most of that should be in volmgr.sys once it is implemented)
        case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
        {
            PVOLUME_DISK_EXTENTS volExts = Irp->AssociatedIrp.SystemBuffer;

            // we fill only one extent entry so sizeof(*volExts) is enough
            if (!VerifyIrpOutBufferSize(Irp, sizeof(*volExts)))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PartMgrAcquireLayoutLock(fdoExtension);

            // the only type of volume we support right now is disk partition
            // so this structure is simple

            *volExts = (VOLUME_DISK_EXTENTS) {
                .NumberOfDiskExtents = 1,
                .Extents = {{
                    .DiskNumber = fdoExtension->DiskData.DeviceNumber,
                    .StartingOffset.QuadPart = partExt->StartingOffset,
                    .ExtentLength.QuadPart = partExt->PartitionLength
                }}
            };

            PartMgrReleaseLayoutLock(fdoExtension);

            status = STATUS_SUCCESS;
            Irp->IoStatus.Information = sizeof(*volExts);
            break;
        }
        case IOCTL_VOLUME_QUERY_VOLUME_NUMBER:
        {
            PVOLUME_NUMBER volNum = Irp->AssociatedIrp.SystemBuffer;
            if (!VerifyIrpOutBufferSize(Irp, sizeof(*volNum)))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PartMgrAcquireLayoutLock(fdoExtension);

            volNum->VolumeNumber = partExt->VolumeNumber;
            RtlCopyMemory(volNum->VolumeManagerName,
                          L"VOLMGR  ", // Must be 8 space-padded characters
                          sizeof(volNum->VolumeManagerName));

            PartMgrReleaseLayoutLock(fdoExtension);

            status = STATUS_SUCCESS;
            Irp->IoStatus.Information = sizeof(*volNum);
            break;
        }
        case IOCTL_VOLUME_IS_PARTITION:
        {
            // The only type of volume we support right now is disk partition
            // so we just return success. A more robust algorithm would be
            // to check whether the volume has only one single extent, that
            // covers the whole partition on which it lies upon. If this is
            // not the case, return STATUS_UNSUCCESSFUL instead.
            status = STATUS_SUCCESS;
            break;
        }
        case IOCTL_VOLUME_ONLINE:
        {
            status = STATUS_SUCCESS;
            break;
        }
        case IOCTL_VOLUME_GET_GPT_ATTRIBUTES:
        {
            PVOLUME_GET_GPT_ATTRIBUTES_INFORMATION gptAttrs = Irp->AssociatedIrp.SystemBuffer;
            if (!VerifyIrpOutBufferSize(Irp, sizeof(*gptAttrs)))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (fdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_GPT)
                gptAttrs->GptAttributes = partExt->Gpt.Attributes;
            else
                gptAttrs->GptAttributes = 0;

            status = STATUS_SUCCESS;
            Irp->IoStatus.Information = sizeof(*gptAttrs);
            break;
        }
        // mountmgr notifications (these should be in volmgr.sys once it is implemented)
        case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
        {
            PMOUNTDEV_NAME name = Irp->AssociatedIrp.SystemBuffer;

            if (!VerifyIrpOutBufferSize(Irp, sizeof(MOUNTDEV_NAME)))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            name->NameLength = partExt->DeviceName.Length;

            // return NameLength back
            if (!VerifyIrpOutBufferSize(Irp, sizeof(USHORT) + name->NameLength))
            {
                Irp->IoStatus.Information = sizeof(USHORT);
                status = STATUS_BUFFER_OVERFLOW;
                break;
            }

            RtlCopyMemory(name->Name, partExt->DeviceName.Buffer, name->NameLength);

            status = STATUS_SUCCESS;
            Irp->IoStatus.Information = sizeof(USHORT) + name->NameLength;
            break;
        }
        case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
        {
            const SIZE_T headerSize = FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId);
            PMOUNTDEV_UNIQUE_ID uniqueId = Irp->AssociatedIrp.SystemBuffer;
            PBASIC_VOLUME_UNIQUE_ID basicVolId = (PBASIC_VOLUME_UNIQUE_ID)&uniqueId->UniqueId;
            PUNICODE_STRING InterfaceName;

            // Check whether the minimal header size was provided
            if (!VerifyIrpOutBufferSize(Irp, sizeof(MOUNTDEV_UNIQUE_ID)))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            PartMgrAcquireLayoutLock(fdoExtension);

            InterfaceName = &partExt->VolumeInterfaceName;
            if (fdoExtension->IsSuperFloppy)
                InterfaceName = &fdoExtension->DiskInterfaceName;

            // Calculate and return the necessary data size
            if ((fdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_MBR) &&
                !fdoExtension->IsSuperFloppy)
            {
                uniqueId->UniqueIdLength = sizeof(basicVolId->Mbr);
            }
            else if (fdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_GPT)
            {
                uniqueId->UniqueIdLength = sizeof(basicVolId->Gpt);
            }
            else
            {
                if (!InterfaceName->Buffer || !InterfaceName->Length)
                {
                    PartMgrReleaseLayoutLock(fdoExtension);
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }
                uniqueId->UniqueIdLength = InterfaceName->Length;
            }

            // Return UniqueIdLength back
            if (!VerifyIrpOutBufferSize(Irp, headerSize + uniqueId->UniqueIdLength))
            {
                PartMgrReleaseLayoutLock(fdoExtension);
                Irp->IoStatus.Information = headerSize;
                status = STATUS_BUFFER_OVERFLOW;
                break;
            }

            //
            // Write the UniqueId
            //
            // Format:
            // - Basic volume on MBR disk: disk Mbr.Signature + partition StartingOffset (length: 0x0C)
            // - Basic volume on GPT disk: "DMIO:ID:" + Gpt.PartitionGuid (length: 0x18)
            // - Volume on Basic disk (NT <= 4): 8-byte FTDisk identifier (length: 0x08)
            // - Volume on Dynamic disk (NT 5+): "DMIO:ID:" + dmio VolumeGuid (length: 0x18)
            // - Super-floppy (single-partition with StartingOffset == 0),
            //   or Removable media: DiskInterfaceName.
            // - As fallback, we use the VolumeInterfaceName.
            //
            if ((fdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_MBR) &&
                !fdoExtension->IsSuperFloppy)
            {
                basicVolId->Mbr.Signature = fdoExtension->DiskData.Mbr.Signature;
                basicVolId->Mbr.StartingOffset = partExt->StartingOffset;
            }
            else if (fdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_GPT)
            {
                basicVolId->Gpt.Signature = DMIO_ID_SIGNATURE;
                basicVolId->Gpt.PartitionGuid = partExt->Gpt.PartitionId;
            }
            else
            {
                RtlCopyMemory(uniqueId->UniqueId,
                              InterfaceName->Buffer,
                              uniqueId->UniqueIdLength);
            }

            PartMgrReleaseLayoutLock(fdoExtension);

            status = STATUS_SUCCESS;
            Irp->IoStatus.Information = headerSize + uniqueId->UniqueIdLength;
            break;
        }
        case IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME:
        {
            if (!VerifyIrpOutBufferSize(Irp, sizeof(MOUNTDEV_SUGGESTED_LINK_NAME)))
                status = STATUS_INVALID_PARAMETER;
            else
                status = STATUS_NOT_FOUND;
            break;
        }
        case IOCTL_MOUNTDEV_LINK_CREATED:
        case IOCTL_MOUNTDEV_LINK_DELETED:
#if (NTDDI_VERSION >= NTDDI_WS03)
        /* Deprecated Windows 2000/XP versions of IOCTL_MOUNTDEV_LINK_[CREATED|DELETED]
         * without access protection, that were updated in Windows 2003 */
        case CTL_CODE(MOUNTDEVCONTROLTYPE, 4, METHOD_BUFFERED, FILE_ANY_ACCESS):
        case CTL_CODE(MOUNTDEVCONTROLTYPE, 5, METHOD_BUFFERED, FILE_ANY_ACCESS):
#endif
        case IOCTL_MOUNTDEV_QUERY_STABLE_GUID:
        {
            PMOUNTDEV_STABLE_GUID stableGuid = Irp->AssociatedIrp.SystemBuffer;

            if (!VerifyIrpOutBufferSize(Irp, sizeof(*stableGuid)))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            PartMgrAcquireLayoutLock(fdoExtension);

            if (fdoExtension->DiskData.PartitionStyle == PARTITION_STYLE_GPT)
            {
                stableGuid->StableGuid = partExt->Gpt.PartitionId;
                Irp->IoStatus.Information = sizeof(*stableGuid);
                status = STATUS_SUCCESS;
            }
            else
            {
                status = STATUS_NOT_IMPLEMENTED;
            }

            PartMgrReleaseLayoutLock(fdoExtension);
            break;
        }
        case IOCTL_MOUNTDEV_UNIQUE_ID_CHANGE_NOTIFY:
        {
            PMOUNTDEV_UNIQUE_ID knownId = Irp->AssociatedIrp.SystemBuffer;
            BASIC_VOLUME_UNIQUE_ID currentId;
            USHORT currentLength;
            KIRQL oldIrql;

            if (!VerifyIrpInBufferSize(Irp, sizeof(MOUNTDEV_UNIQUE_ID)) ||
                !VerifyIrpInBufferSize(Irp, FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId) + knownId->UniqueIdLength) ||
                !VerifyIrpOutBufferSize(Irp, sizeof(MOUNTDEV_UNIQUE_ID_CHANGE_NOTIFY_OUTPUT)))
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            PartMgrAcquireLayoutLock(fdoExtension);

            currentLength = PartitionGetBasicUniqueId(partExt, fdoExtension, &currentId);
            if (currentLength == 0)
            {
                PartMgrReleaseLayoutLock(fdoExtension);
                status = STATUS_NOT_SUPPORTED;
                break;
            }

            if (knownId->UniqueIdLength != currentLength ||
                !RtlEqualMemory(knownId->UniqueId, &currentId, currentLength))
            {
                PartMgrReleaseLayoutLock(fdoExtension);
                status = PartitionFillUniqueIdChange(Irp, &currentId, currentLength);
                break;
            }

            IoAcquireCancelSpinLock(&oldIrql);
            if (partExt->UniqueIdChangeIrp)
            {
                IoReleaseCancelSpinLock(oldIrql);
                PartMgrReleaseLayoutLock(fdoExtension);
                status = STATUS_DEVICE_BUSY;
                break;
            }

            IoSetCancelRoutine(Irp, PartitionCancelUniqueIdChange);
            if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
            {
                IoReleaseCancelSpinLock(oldIrql);
                PartMgrReleaseLayoutLock(fdoExtension);
                status = STATUS_CANCELLED;
                break;
            }

            IoMarkIrpPending(Irp);
            partExt->UniqueIdChangeIrp = Irp;
            IoReleaseCancelSpinLock(oldIrql);
            PartMgrReleaseLayoutLock(fdoExtension);
            return STATUS_PENDING;
        }
        case IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES:
            status = PartitionHandleDataSetAttributes(partExt, fdoExtension, Irp);
            break;
        case IOCTL_VOLSNAP_FLUSH_AND_HOLD_WRITES:
            status = PartitionHoldWrites(partExt, Irp);
            if (status == STATUS_PENDING)
                return status;
            break;
        case IOCTL_VOLSNAP_RELEASE_WRITES:
            status = PartitionReleaseHold(partExt, STATUS_SUCCESS, FALSE) ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
            break;
        default:
            return ForwardIrpAndForget(DeviceObject, Irp);
    }

    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}
