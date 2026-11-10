/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device objects, the stacks they form and the references on them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
IopInsertDeviceOnDriver(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    DeviceObject->NextDevice = DriverObject->DeviceObject;
    DriverObject->DeviceObject = DeviceObject;
}

static
VOID
IopRemoveDeviceFromDriver(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT *Previous = &DriverObject->DeviceObject;

    while (*Previous != NULL)
    {
        if (*Previous == DeviceObject)
        {
            *Previous = DeviceObject->NextDevice;
            DeviceObject->NextDevice = NULL;
            return;
        }

        Previous = &(*Previous)->NextDevice;
    }
}

VOID
IopEditDeviceList(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ IOP_DEVICE_LIST_OPERATION Operation)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    if (Operation == IopAdd)
    {
        IopInsertDeviceOnDriver(DriverObject, DeviceObject);
    }
    else
    {
        IopRemoveDeviceFromDriver(DriverObject, DeviceObject);
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

PDEVICE_OBJECT
IopGetLowestDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension;
    PDEVICE_OBJECT Lowest = DeviceObject;

    while (TRUE)
    {
        Extension = IoGetDevObjExtension(Lowest);
        if (Extension->AttachedTo == NULL) return Lowest;

        Lowest = Extension->AttachedTo;
    }
}

PDEVICE_OBJECT
IopGetDevicePDO(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Pdo;

    Pdo = IopGetLowestDevice(DeviceObject);

    if (IoGetDevObjExtension(Pdo)->DeviceNode == NULL) return NULL;

    ObReferenceObject(Pdo);
    return Pdo;
}

PDEVICE_OBJECT
NTAPI
IopGetDeviceAttachmentBase(
    IN PDEVICE_OBJECT DeviceObject)
{
    return IopGetLowestDevice(DeviceObject);
}

NTSTATUS
NTAPI
IopReferenceDeviceObject(
    IN PDEVICE_OBJECT DeviceObject)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    Extension = IoGetDevObjExtension(DeviceObject);

    if (Extension->ExtensionFlags & (DOE_UNLOAD_PENDING |
                                     DOE_DELETE_PENDING |
                                     DOE_REMOVE_PENDING |
                                     DOE_REMOVE_PROCESSED))
    {
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
        return STATUS_DELETE_PENDING;
    }

    DeviceObject->ReferenceCount++;

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    return STATUS_SUCCESS;
}

VOID
NTAPI
IopDereferenceDeviceObject(
    IN PDEVICE_OBJECT DeviceObject,
    IN BOOLEAN ForceUnload)
{
    IopDecrementDeviceObjectRef(DeviceObject, ForceUnload);
}

VOID
IopDecrementDeviceObjectRef(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN UnloadIfUnused)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension;
    PDRIVER_OBJECT DriverObject;
    BOOLEAN Delete = FALSE;
    BOOLEAN Unload = FALSE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    ASSERT(DeviceObject->ReferenceCount > 0);
    DeviceObject->ReferenceCount--;

    Extension = IoGetDevObjExtension(DeviceObject);
    DriverObject = DeviceObject->DriverObject;

    if (DeviceObject->ReferenceCount == 0)
    {
        if (Extension->ExtensionFlags & DOE_DELETE_PENDING) Delete = TRUE;

        if ((UnloadIfUnused ||
             (Extension->ExtensionFlags & DOE_UNLOAD_PENDING)) &&
            DriverObject->DeviceObject == DeviceObject &&
            DeviceObject->NextDevice == NULL)
        {
            Unload = TRUE;
        }
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    if (Delete) IopUnloadDevice(DeviceObject);
    if (Unload) IopUnloadDevice(DeviceObject);
}

VOID
IopDecrementDeviceObjectHandleCount(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension = IoGetDevObjExtension(DeviceObject);
    KIRQL OldIrql;
    BOOLEAN Closed = FALSE;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    if (Extension->ExtensionFlags & DOE_REMOVE_PENDING) Closed = TRUE;

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    if (Closed) PiDeviceObjectClosed(DeviceObject);
}

VOID
NTAPI
IopUnloadDevice(
    IN PDEVICE_OBJECT DeviceObject)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension = IoGetDevObjExtension(DeviceObject);
    PDRIVER_OBJECT DriverObject = DeviceObject->DriverObject;
    PVPB Vpb;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    if (DeviceObject->ReferenceCount != 0 ||
        !(Extension->ExtensionFlags & (DOE_DELETE_PENDING |
                                       DOE_UNLOAD_PENDING)))
    {
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
        return;
    }

    Vpb = Extension->Vpb;
    Extension->Vpb = NULL;

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    if (Vpb != NULL) IopDereferenceVpbAndFree(Vpb);

    if (DeviceObject->Timer != NULL)
    {
        IopRemoveTimerFromTimerList(DeviceObject->Timer);
        ExFreePoolWithTag(DeviceObject->Timer, TAG_IO_TIMER);
        DeviceObject->Timer = NULL;
    }

    if (Extension->ExtensionFlags & DOE_DELETE_PENDING)
    {
        IopEditDeviceList(DriverObject, DeviceObject, IopRemove);
        ObDereferenceObject(DeviceObject);
    }

    if (DriverObject->DeviceObject == NULL &&
        (IoGetDrvObjExtension(DriverObject)->PnpUnloadPending ||
         DriverObject->DriverUnload != NULL))
    {
        IopTryUnloadDriver(DriverObject, FALSE);
    }
}

VOID
NTAPI
IopDeleteDevice(
    IN PVOID ObjectBody)
{
    PDEVICE_OBJECT DeviceObject = ObjectBody;
    PDEVICE_NODE DeviceNode;

    DeviceNode = IoGetDevObjExtension(DeviceObject)->DeviceNode;
    if (DeviceNode != NULL && DeviceNode != IopRootDeviceNode)
    {
        IoGetDevObjExtension(DeviceObject)->DeviceNode = NULL;
        IopFreeDeviceNode(DeviceNode);
    }

    if (DeviceObject->DriverObject != NULL)
    {
        ObDereferenceObject(DeviceObject->DriverObject);
    }
}

BOOLEAN
IopCheckDeviceAndDriver(
    _In_ POPEN_PACKET OpenPacket,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension = IoGetDevObjExtension(DeviceObject);

    if (Extension->ExtensionFlags & (DOE_UNLOAD_PENDING |
                                     DOE_DELETE_PENDING |
                                     DOE_REMOVE_PENDING |
                                     DOE_REMOVE_PROCESSED))
    {
        return FALSE;
    }

    if (DeviceObject->DriverObject->Flags & DRVO_UNLOAD_INVOKED) return FALSE;

    if (OpenPacket != NULL &&
        (DeviceObject->Flags & DO_EXCLUSIVE) &&
        DeviceObject->ReferenceCount != 0 &&
        !OpenPacket->Override)
    {
        return FALSE;
    }

    return TRUE;
}

NTSTATUS
IopGetDeviceObjectPointer(
    _In_ PUNICODE_STRING ObjectName,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PFILE_OBJECT *FileObject,
    _Out_ PDEVICE_OBJECT *DeviceObject,
    _In_ ULONG AttachFlag)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    PFILE_OBJECT LocalFileObject;
    PDEVICE_OBJECT LocalDevice;
    NTSTATUS Status;
    HANDLE Handle;

    InitializeObjectAttributes(&ObjectAttributes,
                               ObjectName,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = IoCreateFile(&Handle,
                          DesiredAccess,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN,
                          0,
                          NULL,
                          0,
                          CreateFileTypeNone,
                          NULL,
                          IO_NO_PARAMETER_CHECKING | AttachFlag);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(Handle,
                                       0,
                                       IoFileObjectType,
                                       KernelMode,
                                       (PVOID *)&LocalFileObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Handle);
        return Status;
    }

    LocalDevice = IoGetRelatedDeviceObject(LocalFileObject);
    if (LocalDevice == NULL)
    {
        ObDereferenceObject(LocalFileObject);
        NtClose(Handle);
        return STATUS_NO_SUCH_DEVICE;
    }

    NtClose(Handle);

    *FileObject = LocalFileObject;
    *DeviceObject = LocalDevice;

    return STATUS_SUCCESS;
}

static
NTSTATUS
IopCreateDeviceObject(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ ULONG DeviceExtensionSize,
    _In_opt_ PUNICODE_STRING DeviceName,
    _In_ DEVICE_TYPE DeviceType,
    _In_ ULONG DeviceCharacteristics,
    _In_ BOOLEAN Exclusive,
    _Outptr_ PDEVICE_OBJECT *DeviceObject)
{
    PSECURITY_DESCRIPTOR SecurityDescriptor = NULL;
    PEXTENDED_DEVOBJ_EXTENSION DeviceExtension;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UCHAR DescriptorBuffer[SIZE_OF_SUBSET_SD];
    BOOLEAN DefaultSdPresent = FALSE;
    PDEVICE_OBJECT CreatedDevice;
    NTSTATUS Status;
    ULONG AlignedExtensionSize;
    ULONG TotalSize;

    PAGED_CODE();

    *DeviceObject = NULL;

    if (DeviceExtensionSize > MAXUSHORT - sizeof(DEVICE_OBJECT) -
                                  sizeof(EXTENDED_DEVOBJ_EXTENSION))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = IopCreateDefaultDeviceSecurityDescriptor(DeviceType,
                                                      DeviceCharacteristics,
                                                      DeviceName != NULL,
                                                      DescriptorBuffer,
                                                      &DefaultSdPresent,
                                                      &SecurityDescriptor);
    if (!NT_SUCCESS(Status)) return Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               DeviceName,
                               OBJ_KERNEL_HANDLE | OBJ_PERMANENT |
                                   OBJ_CASE_INSENSITIVE,
                               NULL,
                               SecurityDescriptor);

    AlignedExtensionSize = (DeviceExtensionSize +
                            MEMORY_ALLOCATION_ALIGNMENT - 1) &
                           ~(MEMORY_ALLOCATION_ALIGNMENT - 1);

    TotalSize = sizeof(DEVICE_OBJECT) + AlignedExtensionSize +
                sizeof(EXTENDED_DEVOBJ_EXTENSION);

    Status = ObCreateObject(KernelMode,
                            IoDeviceObjectType,
                            &ObjectAttributes,
                            KernelMode,
                            NULL,
                            TotalSize,
                            0,
                            0,
                            (PVOID *)&CreatedDevice);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(CreatedDevice, TotalSize);

    CreatedDevice->Type = IO_TYPE_DEVICE;
    CreatedDevice->Size = (USHORT)(sizeof(DEVICE_OBJECT) +
                                   AlignedExtensionSize);
    CreatedDevice->DeviceType = DeviceType;
    CreatedDevice->Characteristics = DeviceCharacteristics;
    CreatedDevice->DriverObject = DriverObject;
    CreatedDevice->StackSize = 1;
    CreatedDevice->AlignmentRequirement = FILE_BYTE_ALIGNMENT;
    CreatedDevice->SectorSize = 0;
    CreatedDevice->Flags = DO_DEVICE_INITIALIZING;

    if (DeviceName != NULL) CreatedDevice->Flags |= DO_DEVICE_HAS_NAME;
    if (Exclusive) CreatedDevice->Flags |= DO_EXCLUSIVE;

    if (DeviceExtensionSize != 0)
    {
        CreatedDevice->DeviceExtension = CreatedDevice + 1;
    }

    DeviceExtension = (PEXTENDED_DEVOBJ_EXTENSION)
        ((PUCHAR)(CreatedDevice + 1) + AlignedExtensionSize);

    DeviceExtension->Type = IO_TYPE_DEVICE_OBJECT_EXTENSION;
    DeviceExtension->Size = 0;
    DeviceExtension->DeviceObject = CreatedDevice;
    CreatedDevice->DeviceObjectExtension = (PDEVOBJ_EXTENSION)DeviceExtension;

    if (DefaultSdPresent)
    {
        DeviceExtension->ExtensionFlags |= DOE_DEFAULT_SD_PRESENT;
    }

    KeInitializeDeviceQueue(&CreatedDevice->DeviceQueue);
    KeInitializeEvent(&CreatedDevice->DeviceLock, SynchronizationEvent, TRUE);
    InitializeListHead(&CreatedDevice->Queue.ListEntry);

    if (DeviceType == FILE_DEVICE_DISK ||
        DeviceType == FILE_DEVICE_VIRTUAL_DISK ||
        DeviceType == FILE_DEVICE_CD_ROM ||
        DeviceType == FILE_DEVICE_TAPE)
    {
        Status = IopCreateVpb(CreatedDevice);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(CreatedDevice);
            return Status;
        }

        KeInitializeEvent(&CreatedDevice->DeviceLock,
                          SynchronizationEvent,
                          TRUE);
    }

    Status = ObInsertObject(CreatedDevice,
                            NULL,
                            FILE_READ_DATA,
                            1,
                            (PVOID *)&CreatedDevice,
                            NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ObReferenceObject(DriverObject);
    IopEditDeviceList(DriverObject, CreatedDevice, IopAdd);

    InterlockedIncrement((PLONG)&IopDeviceObjectNumber);

    *DeviceObject = CreatedDevice;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoCreateDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ ULONG DeviceExtensionSize,
    _In_opt_ PUNICODE_STRING DeviceName,
    _In_ DEVICE_TYPE DeviceType,
    _In_ ULONG DeviceCharacteristics,
    _In_ BOOLEAN Exclusive,
    _Outptr_ PDEVICE_OBJECT *DeviceObject)
{
    WCHAR NameBuffer[sizeof("\\Device\\XXXXXXXX")];
    UNICODE_STRING GeneratedName;
    NTSTATUS Status;
    ULONG Attempt;

    PAGED_CODE();

    if (DeviceName != NULL ||
        !(DeviceCharacteristics & FILE_AUTOGENERATED_DEVICE_NAME))
    {
        return IopCreateDeviceObject(DriverObject,
                                     DeviceExtensionSize,
                                     DeviceName,
                                     DeviceType,
                                     DeviceCharacteristics,
                                     Exclusive,
                                     DeviceObject);
    }

    /* The caller left the naming to us, so the name has to be a free one */
    for (Attempt = 0; Attempt < 65536; Attempt++)
    {
        Status = RtlStringCchPrintfW(NameBuffer,
                                     RTL_NUMBER_OF(NameBuffer),
                                     L"\\Device\\%08x",
                                     InterlockedIncrement(
                                         (PLONG)&IopAutoGeneratedDeviceNumber));
        if (!NT_SUCCESS(Status)) return Status;

        RtlInitUnicodeString(&GeneratedName, NameBuffer);

        Status = IopCreateDeviceObject(DriverObject,
                                       DeviceExtensionSize,
                                       &GeneratedName,
                                       DeviceType,
                                       DeviceCharacteristics,
                                       Exclusive,
                                       DeviceObject);
        if (Status != STATUS_OBJECT_NAME_COLLISION) return Status;
    }

    return Status;
}

NTSTATUS
NTAPI
IoCreateDeviceSecure(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ ULONG DeviceExtensionSize,
    _In_opt_ PUNICODE_STRING DeviceName,
    _In_ DEVICE_TYPE DeviceType,
    _In_ ULONG DeviceCharacteristics,
    _In_ BOOLEAN Exclusive,
    _In_ PCUNICODE_STRING DefaultSDDLString,
    _In_opt_ LPCGUID DeviceClassGuid,
    _Outptr_ PDEVICE_OBJECT *DeviceObject)
{
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DefaultSDDLString);
    UNREFERENCED_PARAMETER(DeviceClassGuid);

    /* TODO: no SDDL parser in the kernel; the default descriptor is applied */
    Status = IoCreateDevice(DriverObject,
                            DeviceExtensionSize,
                            DeviceName,
                            DeviceType,
                            DeviceCharacteristics,
                            Exclusive,
                            DeviceObject);

    return Status;
}

VOID
NTAPI
IoDeleteDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension;
    PDRIVER_OBJECT DriverObject;
    BOOLEAN Unload = FALSE;
    KIRQL OldIrql;
    PVPB Vpb;

    if (DeviceObject == NULL) return;

    IoUnregisterShutdownNotification(DeviceObject);

    if (DeviceObject->Flags & DO_BUS_ENUMERATED_DEVICE)
    {
        IopDisableDeviceInterfaces(
            IoGetDevObjExtension(DeviceObject)->DeviceNode);
    }

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    Extension = IoGetDevObjExtension(DeviceObject);
    DriverObject = DeviceObject->DriverObject;
    Extension->ExtensionFlags |= DOE_DELETE_PENDING;

    if (DeviceObject->ReferenceCount != 0)
    {
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
        return;
    }

    Vpb = Extension->Vpb;
    Extension->Vpb = NULL;

    if (DriverObject->DeviceObject == DeviceObject &&
        DeviceObject->NextDevice == NULL &&
        (IoGetDrvObjExtension(DriverObject)->PnpUnloadPending ||
         (DriverObject->Flags & DRVO_UNLOAD_INVOKED)))
    {
        Unload = TRUE;
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    if (Vpb != NULL) IopDereferenceVpbAndFree(Vpb);

    if (DeviceObject->Timer != NULL)
    {
        IopRemoveTimerFromTimerList(DeviceObject->Timer);
        ExFreePoolWithTag(DeviceObject->Timer, TAG_IO_TIMER);
        DeviceObject->Timer = NULL;
    }

    IopEditDeviceList(DriverObject, DeviceObject, IopRemove);

    ObDereferenceObject(DeviceObject);

    if (Unload) IopTryUnloadDriver(DriverObject, FALSE);
}

PDEVICE_OBJECT
NTAPI
IoGetAttachedDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Top = DeviceObject;

    while (Top->AttachedDevice != NULL) Top = Top->AttachedDevice;

    return Top;
}

PDEVICE_OBJECT
NTAPI
IoGetAttachedDeviceReference(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Top;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    Top = DeviceObject;
    while (Top->AttachedDevice != NULL) Top = Top->AttachedDevice;

    ObReferenceObject(Top);

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    return Top;
}

PDEVICE_OBJECT
NTAPI
IoGetDeviceAttachmentBaseRef(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Lowest;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    Lowest = IopGetLowestDevice(DeviceObject);
    ObReferenceObject(Lowest);

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    return Lowest;
}

PDEVICE_OBJECT
NTAPI
IoGetLowerDeviceObject(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension;
    PDEVICE_OBJECT Lower = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    Extension = IoGetDevObjExtension(DeviceObject);

    if (!(Extension->ExtensionFlags & (DOE_UNLOAD_PENDING |
                                       DOE_DELETE_PENDING |
                                       DOE_REMOVE_PENDING |
                                       DOE_REMOVE_PROCESSED)) &&
        Extension->AttachedTo != NULL)
    {
        Lower = Extension->AttachedTo;
        ObReferenceObject(Lower);
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    return Lower;
}

PDEVICE_OBJECT
NTAPI
IoGetRelatedDeviceObject(
    _In_ PFILE_OBJECT FileObject)
{
    PDEVICE_OBJECT DeviceObject;

    if (FileObject->Vpb != NULL && FileObject->Vpb->DeviceObject != NULL)
    {
        DeviceObject = FileObject->Vpb->DeviceObject;
    }
    else if (FileObject->RelatedFileObject != NULL &&
             FileObject->RelatedFileObject->Vpb != NULL &&
             FileObject->RelatedFileObject->Vpb->DeviceObject != NULL)
    {
        DeviceObject = FileObject->RelatedFileObject->Vpb->DeviceObject;
    }
    else
    {
        DeviceObject = FileObject->DeviceObject;
    }

    if (DeviceObject == NULL) return NULL;

    if (!(FileObject->Flags & FO_DIRECT_DEVICE_OPEN))
    {
        return IoGetAttachedDevice(DeviceObject);
    }

    return DeviceObject;
}

NTSTATUS
NTAPI
IoAttachDevice(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PUNICODE_STRING TargetDevice,
    _Out_ PDEVICE_OBJECT *AttachedDevice)
{
    PDEVICE_OBJECT TargetDeviceObject;
    PFILE_OBJECT FileObject;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopGetDeviceObjectPointer(TargetDevice,
                                       FILE_READ_ATTRIBUTES,
                                       &FileObject,
                                       &TargetDeviceObject,
                                       IO_ATTACH_DEVICE_API);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IoAttachDeviceToDeviceStackSafe(SourceDevice,
                                             TargetDeviceObject,
                                             AttachedDevice);

    ObDereferenceObject(FileObject);

    return Status;
}

NTSTATUS
NTAPI
IoAttachDeviceByPointer(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PDEVICE_OBJECT TargetDevice)
{
    PDEVICE_OBJECT AttachedDevice;

    return IoAttachDeviceToDeviceStackSafe(SourceDevice,
                                           TargetDevice,
                                           &AttachedDevice);
}

NTSTATUS
NTAPI
IoAttachDeviceToDeviceStackSafe(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PDEVICE_OBJECT TargetDevice,
    _Out_ PDEVICE_OBJECT *AttachedToDeviceObject)
{
    PEXTENDED_DEVOBJ_EXTENSION SourceExtension;
    PEXTENDED_DEVOBJ_EXTENSION TargetExtension;
    PDEVICE_OBJECT Top;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    Top = TargetDevice;
    while (Top->AttachedDevice != NULL) Top = Top->AttachedDevice;

    TargetExtension = IoGetDevObjExtension(Top);

    if ((TargetExtension->ExtensionFlags & (DOE_UNLOAD_PENDING |
                                            DOE_DELETE_PENDING |
                                            DOE_REMOVE_PENDING |
                                            DOE_REMOVE_PROCESSED)) ||
        (Top->Flags & DO_DEVICE_INITIALIZING))
    {
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
        *AttachedToDeviceObject = NULL;
        return STATUS_NO_SUCH_DEVICE;
    }

    Top->AttachedDevice = SourceDevice;

    SourceExtension = IoGetDevObjExtension(SourceDevice);
    SourceExtension->AttachedTo = Top;

    SourceDevice->StackSize = (CCHAR)(Top->StackSize + 1);
    SourceDevice->AlignmentRequirement = Top->AlignmentRequirement;
    SourceDevice->SectorSize = Top->SectorSize;
    SourceDevice->Vpb = Top->Vpb;

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    *AttachedToDeviceObject = Top;

    return STATUS_SUCCESS;
}

PDEVICE_OBJECT
NTAPI
IoAttachDeviceToDeviceStack(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PDEVICE_OBJECT TargetDevice)
{
    PDEVICE_OBJECT AttachedTo = NULL;
    NTSTATUS Status;

    Status = IoAttachDeviceToDeviceStackSafe(SourceDevice,
                                             TargetDevice,
                                             &AttachedTo);
    if (!NT_SUCCESS(Status)) return NULL;

    return AttachedTo;
}

VOID
NTAPI
IoDetachDevice(
    _Inout_ PDEVICE_OBJECT TargetDevice)
{
    PEXTENDED_DEVOBJ_EXTENSION Extension;
    PDEVICE_OBJECT Attached;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    Attached = TargetDevice->AttachedDevice;
    if (Attached == NULL)
    {
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
        return;
    }

    Extension = IoGetDevObjExtension(Attached);
    Extension->AttachedTo = NULL;

    TargetDevice->AttachedDevice = NULL;

    ASSERT(TargetDevice->ReferenceCount > 0);
    TargetDevice->ReferenceCount--;

    Extension = IoGetDevObjExtension(TargetDevice);

    if (TargetDevice->ReferenceCount == 0 &&
        (Extension->ExtensionFlags & (DOE_DELETE_PENDING |
                                      DOE_UNLOAD_PENDING)))
    {
        KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
        IopUnloadDevice(TargetDevice);
        return;
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

VOID
NTAPI
IopReadyDeviceObjects(
    IN PDRIVER_OBJECT Driver)
{
    PDEVICE_OBJECT DeviceObject;
    KIRQL OldIrql;

    Driver->Flags |= DRVO_INITIALIZED;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    DeviceObject = Driver->DeviceObject;
    while (DeviceObject != NULL)
    {
        DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
        DeviceObject = DeviceObject->NextDevice;
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

NTSTATUS
NTAPI
IoEnumerateDeviceObjectList(
    _In_ PDRIVER_OBJECT DriverObject,
    _Out_writes_bytes_opt_(DeviceObjectListSize) PDEVICE_OBJECT *DeviceObjectList,
    _In_ ULONG DeviceObjectListSize,
    _Out_ PULONG ActualNumberDeviceObjects)
{
    PDEVICE_OBJECT DeviceObject;
    ULONG Capacity;
    ULONG Count = 0;
    ULONG Stored = 0;
    KIRQL OldIrql;

    Capacity = DeviceObjectListSize / (ULONG)sizeof(PDEVICE_OBJECT);

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    DeviceObject = DriverObject->DeviceObject;
    while (DeviceObject != NULL)
    {
        Count++;

        if (Stored < Capacity)
        {
            ObReferenceObject(DeviceObject);
            DeviceObjectList[Stored] = DeviceObject;
            Stored++;
        }

        DeviceObject = DeviceObject->NextDevice;
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    *ActualNumberDeviceObjects = Count;

    if (Count > Capacity)
    {
        while (Stored != 0)
        {
            Stored--;
            ObDereferenceObject(DeviceObjectList[Stored]);
        }

        return STATUS_BUFFER_TOO_SMALL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoGetDeviceObjectPointer(
    _In_ PUNICODE_STRING ObjectName,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PFILE_OBJECT *FileObject,
    _Out_ PDEVICE_OBJECT *DeviceObject)
{
    return IopGetDeviceObjectPointer(ObjectName,
                                     DesiredAccess,
                                     FileObject,
                                     DeviceObject,
                                     0);
}

NTSTATUS
NTAPI
IoGetDeviceNumaNode(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_ PUSHORT NodeNumber)
{
    PDEVICE_NODE DeviceNode;

    DeviceNode = IopGetDeviceNode(Pdo);
    if (DeviceNode == NULL) return STATUS_INVALID_PARAMETER;

    *NodeNumber = (USHORT)DeviceNode->NumaNodeIndex;

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
IopVerifyDeviceObjectOnStack(
    IN PDEVICE_OBJECT BaseDeviceObject,
    IN PDEVICE_OBJECT TopDeviceObjectHint)
{
    PDEVICE_OBJECT Current = BaseDeviceObject;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    while (Current != NULL)
    {
        if (Current == TopDeviceObjectHint)
        {
            KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
            return TRUE;
        }

        Current = Current->AttachedDevice;
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);

    return FALSE;
}

BOOLEAN
NTAPI
IoIsWdmVersionAvailable(
    _In_ UCHAR MajorVersion,
    _In_ UCHAR MinorVersion)
{
    if (MajorVersion < 1) return TRUE;
    if (MajorVersion > 1) return FALSE;

    return MinorVersion <= 0x30;
}
