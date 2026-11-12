/*
 * PROJECT:     ReactOS Universal Serial Bus Bulk Enhanced Host Controller Interface
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        drivers/usb/usbccgp/descriptor.c
 * PURPOSE:     USB  device driver.
 * PROGRAMMERS:
 *              Michael Martin (michael.martin@reactos.org)
 *              Johannes Anderwald (johannes.anderwald@reactos.org)
 *              Cameron Gutman
 */

#include "usbccgp.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
USBCCGP_QueryInterface(
    IN PDEVICE_OBJECT DeviceObject,
    OUT PUSBC_DEVICE_CONFIGURATION_INTERFACE_V1 BusInterface)
{
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;

    /* Sanity checks */
    ASSERT(DeviceObject);

    /* Initialize event */
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    /* Init interface */
    RtlZeroMemory(BusInterface, sizeof(USBC_DEVICE_CONFIGURATION_INTERFACE_V1));
    BusInterface->Version = USBC_DEVICE_CONFIGURATION_INTERFACE_VERSION_1;
    BusInterface->Size = sizeof(USBC_DEVICE_CONFIGURATION_INTERFACE_V1);

    /* Create irp */
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       DeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatus);

    //
    // was irp built
    //
    if (Irp == NULL)
    {
        //
        // no memory
        //
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // initialize request
    //
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.Size = sizeof(USBC_DEVICE_CONFIGURATION_INTERFACE_V1);
    Stack->Parameters.QueryInterface.InterfaceType = (LPGUID)&USB_BUS_INTERFACE_USBC_CONFIGURATION_GUID;
    Stack->Parameters.QueryInterface.Version = USBC_DEVICE_CONFIGURATION_INTERFACE_VERSION_1;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)BusInterface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    //
    // call driver
    //
    Status = IoCallDriver(DeviceObject, Irp);

    //
    // did operation complete
    //
    if (Status == STATUS_PENDING)
    {
        //
        // wait for completion
        //
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

        //
        // collect status
        //
        Status = IoStatus.Status;
    }

    return Status;
}

NTSTATUS
USBCCGP_CustomEnumWithInterface(
    IN PDEVICE_OBJECT DeviceObject)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    ULONG FunctionDescriptorBufferLength = 0;
    NTSTATUS Status;
    PUSBC_FUNCTION_DESCRIPTOR  FunctionDescriptorBuffer = NULL;

    //
    // get device extension
    //
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    if (FDODeviceExtension->BusInterface.StartDeviceCallback == NULL)
    {
        //
        // not supported
        //
        return STATUS_NOT_SUPPORTED;
    }

    //
    // invoke callback
    //
    Status = FDODeviceExtension->BusInterface.StartDeviceCallback(FDODeviceExtension->DeviceDescriptor,
                                                                  FDODeviceExtension->ConfigurationDescriptor,
                                                                  &FunctionDescriptorBuffer,
                                                                  &FunctionDescriptorBufferLength,
                                                                  DeviceObject,
                                                                  FDODeviceExtension->PhysicalDeviceObject);

    DPRINT("USBCCGP_CustomEnumWithInterface Status %lx\n", Status);
    if (!NT_SUCCESS(Status))
    {
        //
        // failed
        //
        return Status;
    }

    DPRINT("FunctionDescriptorBufferLength %lu\n", FunctionDescriptorBufferLength);
    DPRINT("FunctionDescriptorBuffer %p\n", FunctionDescriptorBuffer);

    //
    // assume length % function buffer size
    //
    ASSERT(FunctionDescriptorBufferLength);
    ASSERT(FunctionDescriptorBufferLength % sizeof(USBC_FUNCTION_DESCRIPTOR) == 0);

    //
    // store result
    //
    FDODeviceExtension->FunctionDescriptor = FunctionDescriptorBuffer;
    FDODeviceExtension->FunctionDescriptorCount = FunctionDescriptorBufferLength / sizeof(USBC_FUNCTION_DESCRIPTOR);
    FDODeviceExtension->FunctionDescriptorOwned = FALSE;

    //
    // success
    //
    return STATUS_SUCCESS;
}

ULONG
USBCCGP_CountAssociationDescriptors(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor)
{
    PUSB_INTERFACE_ASSOCIATION_DESCRIPTOR Descriptor;
    PUCHAR Offset, End;
    ULONG Count = 0;

    //
    // init offsets
    //
    Offset = (PUCHAR)ConfigurationDescriptor + ConfigurationDescriptor->bLength;
    End = (PUCHAR)ConfigurationDescriptor + ConfigurationDescriptor->wTotalLength;

    while (Offset < End)
    {
        //
        // get association descriptor
        //
        Descriptor = (PUSB_INTERFACE_ASSOCIATION_DESCRIPTOR)Offset;

        if (Descriptor->bLength == 0)
            break;

        if (Descriptor->bLength == sizeof(USB_INTERFACE_ASSOCIATION_DESCRIPTOR) && Descriptor->bDescriptorType == USB_INTERFACE_ASSOCIATION_DESCRIPTOR_TYPE)
        {
            //
            // found descriptor
            //
            Count++;
        }

        //
        // move to next descriptor
        //
        Offset += Descriptor->bLength;
    }

    //
    // done
    //
    return Count;
}

PUSB_INTERFACE_ASSOCIATION_DESCRIPTOR
USBCCGP_GetAssociationDescriptorAtIndex(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN ULONG Index)
{
    PUSB_INTERFACE_ASSOCIATION_DESCRIPTOR Descriptor;
    PUCHAR Offset, End;
    ULONG Count = 0;

    //
    // init offsets
    //
    Offset = (PUCHAR)ConfigurationDescriptor + ConfigurationDescriptor->bLength;
    End = (PUCHAR)ConfigurationDescriptor + ConfigurationDescriptor->wTotalLength;

    while (Offset < End)
    {
        //
        // get association descriptor
        //
        Descriptor = (PUSB_INTERFACE_ASSOCIATION_DESCRIPTOR)Offset;

        if (Descriptor->bLength == 0)
            break;

        if (Descriptor->bLength == sizeof(USB_INTERFACE_ASSOCIATION_DESCRIPTOR) && Descriptor->bDescriptorType == USB_INTERFACE_ASSOCIATION_DESCRIPTOR_TYPE)
        {
            if (Index == Count)
            {
                //
                // found descriptor
                //
                return Descriptor;
            }

            //
            // not the searched one
            //
            Count++;
        }

        //
        // move to next descriptor
        //
        Offset += Descriptor->bLength;
    }

    //
    // failed to find descriptor at the specified index
    //
    return NULL;
}

NTSTATUS
USBCCGP_InitInterfaceListOfFunctionDescriptor(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSB_INTERFACE_ASSOCIATION_DESCRIPTOR AssociationDescriptor,
    OUT PUSBC_FUNCTION_DESCRIPTOR FunctionDescriptor)
{
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PUCHAR Offset, End;
    ULONG Count = 0;

    //
    // init offsets
    //
    Offset = (PUCHAR)AssociationDescriptor + AssociationDescriptor->bLength;
    End = (PUCHAR)ConfigurationDescriptor + ConfigurationDescriptor->wTotalLength;

    while (Offset < End)
    {
        //
        // get association descriptor
        //
        Descriptor = (PUSB_INTERFACE_DESCRIPTOR)Offset;

        if (Descriptor->bLength == 0)
        {
            DPRINT1("[USBCCGP] Zero-length descriptor in interface list init\n");
            break;
        }

        if (Descriptor->bLength == sizeof(USB_INTERFACE_DESCRIPTOR) && Descriptor->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE &&
            Descriptor->bAlternateSetting == 0)
        {
            //
            // store interface descriptor
            //
            FunctionDescriptor->InterfaceDescriptorList[Count] = Descriptor;
            Count++;

            if (Count == AssociationDescriptor->bInterfaceCount)
            {
                //
                // got all interfaces
                //
                return STATUS_SUCCESS;
            }
        }

        if (Descriptor->bLength == sizeof(USB_INTERFACE_ASSOCIATION_DESCRIPTOR) && Descriptor->bDescriptorType == USB_INTERFACE_ASSOCIATION_DESCRIPTOR_TYPE)
        {
            //
            // association descriptor which overlaps the next association descriptor
            //
            DPRINT1("Invalid association descriptor\n");
            return STATUS_UNSUCCESSFUL;
        }

        //
        // move to next descriptor
        //
        Offset += Descriptor->bLength;
    }

    //
    // invalid association descriptor
    //
    DPRINT1("Invalid association descriptor\n");
    return STATUS_UNSUCCESSFUL;
}

#define USBCCGP_MS_EXT_COMPAT_ID_INDEX 0x0004
#define USBCCGP_MS_EXT_COMPAT_HEADER_LENGTH 16
#define USBCCGP_MS_EXT_COMPAT_FUNCTION_LENGTH 24
#define USBCCGP_MS_EXT_COMPAT_MAX_LENGTH (USBCCGP_MS_EXT_COMPAT_HEADER_LENGTH + 32 * USBCCGP_MS_EXT_COMPAT_FUNCTION_LENGTH)

static
NTSTATUS
USBCCGP_GetMsCompatDescriptor(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN PUCHAR Buffer,
    IN ULONG Length,
    OUT PULONG Transferred)
{
    struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST *Urb;
    NTSTATUS Status;

    *Transferred = 0;
    Urb = AllocateItem(NonPagedPool, sizeof(*Urb));
    if (!Urb)
        return STATUS_INSUFFICIENT_RESOURCES;

    Urb->Hdr.Length = sizeof(*Urb);
    Urb->Hdr.Function = URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR;
    Urb->TransferBufferLength = Length;
    Urb->TransferBuffer = Buffer;
    Urb->InterfaceNumber = 0;
    Urb->MS_PageIndex = 0;
    Urb->MS_FeatureDescriptorIndex = USBCCGP_MS_EXT_COMPAT_ID_INDEX;

    Status = USBCCGP_SyncUrbRequest(FDODeviceExtension->NextDeviceObject, (PURB)Urb);
    if (NT_SUCCESS(Status))
        *Transferred = Urb->TransferBufferLength;

    FreeItem(Urb);
    return Status;
}

static IO_COMPLETION_ROUTINE USBCCGP_MsOs20Complete;

static
NTSTATUS
NTAPI
USBCCGP_MsOs20Complete(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
USBCCGP_GetMsOs20Set(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN OUT PROS_USB_MSOS20_DESCRIPTOR_SET_QUERY Query)
{
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Irp = IoAllocateIrp(FDODeviceExtension->NextDeviceObject->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_REACTOS_USB_GET_MSOS20_DESCRIPTOR_SET;
    IoStack->Parameters.Others.Argument1 = Query;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoSetCompletionRoutine(Irp, USBCCGP_MsOs20Complete, &Event, TRUE, TRUE, TRUE);

    if (IoCallDriver(FDODeviceExtension->NextDeviceObject, Irp) == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

static
VOID
USBCCGP_QueryMsOs20Set(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension)
{
    ROS_USB_MSOS20_DESCRIPTOR_SET_QUERY Query;
    ROS_USB_MSOS20_WALK Walk;
    PUCHAR Set;
    NTSTATUS Status;

    if (FDODeviceExtension->MsOs20DescriptorSet)
    {
        FreeItem(FDODeviceExtension->MsOs20DescriptorSet);
        FDODeviceExtension->MsOs20DescriptorSet = NULL;
        FDODeviceExtension->MsOs20DescriptorSetLength = 0;
    }

    RtlZeroMemory(&Query, sizeof(Query));
    Query.Size = sizeof(Query);
    Status = USBCCGP_GetMsOs20Set(FDODeviceExtension, &Query);
    if (Status != STATUS_BUFFER_TOO_SMALL || !Query.ResultLength)
        return;

    Set = AllocateItem(NonPagedPool, Query.ResultLength);
    if (!Set)
        return;
    Query.Buffer = Set;
    Query.BufferLength = Query.ResultLength;
    Status = USBCCGP_GetMsOs20Set(FDODeviceExtension, &Query);
    if (!NT_SUCCESS(Status) || !RosUsbMsOs20BeginWalk(&Walk, Set, Query.ResultLength))
    {
        FreeItem(Set);
        return;
    }

    FDODeviceExtension->MsOs20DescriptorSet = Set;
    FDODeviceExtension->MsOs20DescriptorSetLength = Query.ResultLength;
}

VOID
USBCCGP_ApplyMsOs20Properties(
    IN PDEVICE_OBJECT DeviceObject,
    IN PPDO_DEVICE_EXTENSION PDODeviceExtension)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension = PDODeviceExtension->FDODeviceExtension;
    const MS_OS_20_DESCRIPTOR_HEADER *Feature;
    ROS_USB_MSOS20_WALK Walk;
    HANDLE Key;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (!FDODeviceExtension || !FDODeviceExtension->MsOs20DescriptorSet || !PDODeviceExtension->FunctionDescriptor ||
        !RosUsbMsOs20BeginWalk(&Walk, FDODeviceExtension->MsOs20DescriptorSet,
                               FDODeviceExtension->MsOs20DescriptorSetLength))
    {
        return;
    }

    Status = IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE, KEY_WRITE, &Key);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBCCGP: no device key for MS OS 2.0 properties: %lx\n", Status);
        return;
    }

    while ((Feature = RosUsbMsOs20NextFeature(&Walk)) != NULL)
    {
        if (Walk.FirstInterface == (LONG)PDODeviceExtension->FunctionDescriptor->FunctionNumber &&
            Feature->wDescriptorType == MS_OS_20_FEATURE_REG_PROPERTY)
        {
            Status = RosUsbMsOs20ApplyRegistryProperty(Key, Feature);
            if (!NT_SUCCESS(Status))
                DPRINT1("USBCCGP: MS OS 2.0 registry property failed: %lx\n", Status);
        }
    }
    ZwClose(Key);
}

static
VOID
USBCCGP_QueryMsCompatIds(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension)
{
    PUCHAR Buffer;
    ULONG Transferred, Length;
    NTSTATUS Status;

    if (FDODeviceExtension->MsCompatDescriptor)
    {
        FreeItem(FDODeviceExtension->MsCompatDescriptor);
        FDODeviceExtension->MsCompatDescriptor = NULL;
        FDODeviceExtension->MsCompatDescriptorLength = 0;
    }

    Buffer = AllocateItem(NonPagedPool, USBCCGP_MS_EXT_COMPAT_MAX_LENGTH);
    if (!Buffer)
        return;

    Status = USBCCGP_GetMsCompatDescriptor(FDODeviceExtension, Buffer, USBCCGP_MS_EXT_COMPAT_HEADER_LENGTH, &Transferred);
    if (!NT_SUCCESS(Status) ||
        Transferred != USBCCGP_MS_EXT_COMPAT_HEADER_LENGTH ||
        *(PUSHORT)&Buffer[6] != USBCCGP_MS_EXT_COMPAT_ID_INDEX ||
        Buffer[8] == 0)
    {
        FreeItem(Buffer);
        return;
    }

    Length = min(*(PULONG)&Buffer[0], (ULONG)USBCCGP_MS_EXT_COMPAT_MAX_LENGTH);
    if (Length < USBCCGP_MS_EXT_COMPAT_HEADER_LENGTH + USBCCGP_MS_EXT_COMPAT_FUNCTION_LENGTH)
    {
        FreeItem(Buffer);
        return;
    }

    Status = USBCCGP_GetMsCompatDescriptor(FDODeviceExtension, Buffer, Length, &Transferred);
    if (!NT_SUCCESS(Status) || Transferred < USBCCGP_MS_EXT_COMPAT_HEADER_LENGTH + USBCCGP_MS_EXT_COMPAT_FUNCTION_LENGTH)
    {
        FreeItem(Buffer);
        return;
    }

    FDODeviceExtension->MsCompatDescriptor = Buffer;
    FDODeviceExtension->MsCompatDescriptorLength = Transferred;
}

static
BOOLEAN
USBCCGP_CopyMsCompatString(
    OUT PCHAR Destination,
    IN const UCHAR *Source)
{
    ULONG Index;

    RtlZeroMemory(Destination, 9);
    for (Index = 0; Index < 8 && Source[Index]; Index++)
    {
        if (Source[Index] < 0x20 || Source[Index] > 0x7E || Source[Index] == ',')
        {
            RtlZeroMemory(Destination, 9);
            return FALSE;
        }
        Destination[Index] = (CHAR)Source[Index];
    }
    return Destination[0] != 0;
}

static
ULONG
USBCCGP_FormatMsCompatIds(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN UCHAR FirstInterface,
    OUT PWCHAR Buffer)
{
    PUCHAR Descriptor = FDODeviceExtension->MsCompatDescriptor;
    const MS_OS_20_COMPATIBLE_ID_DESCRIPTOR *Feature;
    ULONG Offset, Count, Index = 0;
    CHAR Compatible[9], SubCompatible[9];
    UCHAR Raw[9];
    ROS_USB_MSOS20_WALK Walk;

    if (FDODeviceExtension->MsOs20DescriptorSet)
    {
        if (!RosUsbMsOs20BeginWalk(&Walk, FDODeviceExtension->MsOs20DescriptorSet,
                                   FDODeviceExtension->MsOs20DescriptorSetLength))
        {
            return 0;
        }
        while ((Feature = (const MS_OS_20_COMPATIBLE_ID_DESCRIPTOR *)RosUsbMsOs20NextFeature(&Walk)) != NULL)
        {
            if (Walk.FirstInterface != FirstInterface || Feature->wDescriptorType != MS_OS_20_FEATURE_COMPATBLE_ID ||
                Feature->wLength != sizeof(*Feature))
            {
                continue;
            }
            RtlZeroMemory(Raw, sizeof(Raw));
            RtlCopyMemory(Raw, Feature->CompatibleID, sizeof(Feature->CompatibleID));
            if (!USBCCGP_CopyMsCompatString(Compatible, Raw))
                return 0;
            RtlZeroMemory(Raw, sizeof(Raw));
            RtlCopyMemory(Raw, Feature->SubCompatibleID, sizeof(Feature->SubCompatibleID));
            if (USBCCGP_CopyMsCompatString(SubCompatible, Raw))
            {
                Index += _swprintf(&Buffer[Index], L"USB\\MS_COMP_%S&MS_SUBCOMP_%S", Compatible, SubCompatible) + 1;
            }
            Index += _swprintf(&Buffer[Index], L"USB\\MS_COMP_%S", Compatible) + 1;
            return Index;
        }
        return 0;
    }

    if (!Descriptor)
        return 0;

    Count = Descriptor[8];
    for (Offset = USBCCGP_MS_EXT_COMPAT_HEADER_LENGTH;
         Count && Offset + USBCCGP_MS_EXT_COMPAT_FUNCTION_LENGTH <= FDODeviceExtension->MsCompatDescriptorLength;
         Offset += USBCCGP_MS_EXT_COMPAT_FUNCTION_LENGTH, Count--)
    {
        if (Descriptor[Offset] != FirstInterface)
            continue;

        if (!USBCCGP_CopyMsCompatString(Compatible, &Descriptor[Offset + 2]))
            return 0;

        if (USBCCGP_CopyMsCompatString(SubCompatible, &Descriptor[Offset + 10]))
        {
            Index += _swprintf(&Buffer[Index],
                               L"USB\\MS_COMP_%S&MS_SUBCOMP_%S",
                               Compatible,
                               SubCompatible) + 1;
        }
        Index += _swprintf(&Buffer[Index], L"USB\\MS_COMP_%S", Compatible) + 1;
        return Index;
    }

    return 0;
}

NTSTATUS
USBCCGP_InitFunctionDescriptor(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN ULONG AssociationIndex,
    OUT PUSBC_FUNCTION_DESCRIPTOR FunctionDescriptor)
{
    PUSB_INTERFACE_ASSOCIATION_DESCRIPTOR Descriptor;
    NTSTATUS Status;
    LPWSTR DescriptionBuffer;
    WCHAR Buffer[320];
    ULONG Index;

    // get association descriptor
    Descriptor = USBCCGP_GetAssociationDescriptorAtIndex(FDODeviceExtension->ConfigurationDescriptor, AssociationIndex);
    ASSERT(Descriptor);

    FunctionDescriptor->FunctionNumber = Descriptor->bFirstInterface;

    // store number interfaces
    FunctionDescriptor->NumberOfInterfaces = Descriptor->bInterfaceCount;

    // allocate array for interface count
    FunctionDescriptor->InterfaceDescriptorList = AllocateItem(NonPagedPool, sizeof(PUSB_INTERFACE_DESCRIPTOR) * Descriptor->bInterfaceCount);
    if (!FunctionDescriptor->InterfaceDescriptorList)
    {
        //
        // no memory
        //
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // init interface list
    Status = USBCCGP_InitInterfaceListOfFunctionDescriptor(FDODeviceExtension->ConfigurationDescriptor, Descriptor, FunctionDescriptor);
    if (!NT_SUCCESS(Status))
    {
        //
        // failed
        //
        return Status;
    }

    //
    // now init interface description
    //
    if (Descriptor->iFunction)
    {
        //
        // get interface description
        //
         Status = USBCCGP_GetStringDescriptor(FDODeviceExtension->NextDeviceObject,
                                              100 * sizeof(WCHAR),
                                              Descriptor->iFunction,
                                              0x0409, //FIXME
                                              (PVOID*)&DescriptionBuffer);
        if (!NT_SUCCESS(Status))
        {
            //
            // no description
            //
            RtlInitUnicodeString(&FunctionDescriptor->FunctionDescription, L"");
        }
        else
        {
            //
            // init description
            //
            RtlInitUnicodeString(&FunctionDescriptor->FunctionDescription, DescriptionBuffer);
        }
        DPRINT("FunctionDescription %wZ\n", &FunctionDescriptor->FunctionDescription);
    }

    //
    // now init hardware id
    //
    Index = _swprintf(Buffer,
                      L"USB\\VID_%04X&PID_%04X&REV_%04X&MI_%02X",
                      FDODeviceExtension->DeviceDescriptor->idVendor,
                      FDODeviceExtension->DeviceDescriptor->idProduct,
                      FDODeviceExtension->DeviceDescriptor->bcdDevice,
                      Descriptor->bFirstInterface) + 1;
    Index += _swprintf(&Buffer[Index],
                       L"USB\\VID_%04X&PID_%04X&MI_%02X",
                       FDODeviceExtension->DeviceDescriptor->idVendor,
                       FDODeviceExtension->DeviceDescriptor->idProduct,
                       Descriptor->bFirstInterface) + 1;

    // allocate result buffer
    DescriptionBuffer = AllocateItem(NonPagedPool, (Index + 1) * sizeof(WCHAR));
    if (!DescriptionBuffer)
    {
        //
        // failed to allocate memory
        //
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // copy description
    RtlCopyMemory(DescriptionBuffer, Buffer, (Index + 1) * sizeof(WCHAR));
    FunctionDescriptor->HardwareId.Buffer = DescriptionBuffer;
    FunctionDescriptor->HardwareId.Length = Index * sizeof(WCHAR);
    FunctionDescriptor->HardwareId.MaximumLength = (Index + 1) * sizeof(WCHAR);

    //
    // now init the compatible id
    //
    Index = USBCCGP_FormatMsCompatIds(FDODeviceExtension, Descriptor->bFirstInterface, Buffer);
    Index += _swprintf(&Buffer[Index],
                       L"USB\\COMPAT_VID_%04x&Class_%02x&SubClass_%02x&Prot_%02x",
                       FDODeviceExtension->DeviceDescriptor->idVendor,
                       Descriptor->bFunctionClass,
                       Descriptor->bFunctionSubClass,
                       Descriptor->bFunctionProtocol) + 1;
    Index += _swprintf(&Buffer[Index],
                       L"USB\\COMPAT_VID_%04x&Class_%02x&SubClass_%02x",
                       FDODeviceExtension->DeviceDescriptor->idVendor,
                       Descriptor->bFunctionClass,
                       Descriptor->bFunctionSubClass) + 1;
    Index += _swprintf(&Buffer[Index],
                       L"USB\\COMPAT_VID_%04x&Class_%02x",
                       FDODeviceExtension->DeviceDescriptor->idVendor,
                       Descriptor->bFunctionClass) + 1;
    Index += _swprintf(&Buffer[Index],
                      L"USB\\Class_%02x&SubClass_%02x&Prot_%02x",
                      Descriptor->bFunctionClass,
                      Descriptor->bFunctionSubClass,
                      Descriptor->bFunctionProtocol) + 1;
    Index += _swprintf(&Buffer[Index],
                       L"USB\\Class_%02x&SubClass_%02x",
                       Descriptor->bFunctionClass,
                       Descriptor->bFunctionSubClass) + 1;
    Index += _swprintf(&Buffer[Index],
                       L"USB\\Class_%02x",
                       Descriptor->bFunctionClass) + 1;

    // allocate result buffer
    DescriptionBuffer = AllocateItem(NonPagedPool, (Index + 1) * sizeof(WCHAR));
    if (!DescriptionBuffer)
    {
        //
        // failed to allocate memory
        //
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // copy description
    RtlCopyMemory(DescriptionBuffer, Buffer, (Index + 1) * sizeof(WCHAR));
    FunctionDescriptor->CompatibleId.Buffer = DescriptionBuffer;
    FunctionDescriptor->CompatibleId.Length = Index * sizeof(WCHAR);
    FunctionDescriptor->CompatibleId.MaximumLength = (Index + 1) * sizeof(WCHAR);

    //
    // done
    //
    return STATUS_SUCCESS;
}

/* Forward declaration: defined later in this file, used for standalone interfaces. */
NTSTATUS
USBCCG_InitIdsWithInterfaceDescriptor(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN PUSB_INTERFACE_DESCRIPTOR Descriptor,
    IN ULONG FunctionIndex,
    OUT PUSBC_FUNCTION_DESCRIPTOR FunctionDescriptor);

static
BOOLEAN
USBCCGP_IsInterfaceInAnyAssociation(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN UCHAR InterfaceNumber)
{
    PUSB_INTERFACE_ASSOCIATION_DESCRIPTOR Descriptor;
    PUCHAR Offset, End;

    Offset = (PUCHAR)ConfigurationDescriptor + ConfigurationDescriptor->bLength;
    End = (PUCHAR)ConfigurationDescriptor + ConfigurationDescriptor->wTotalLength;

    while (Offset + sizeof(USB_INTERFACE_ASSOCIATION_DESCRIPTOR) <= End)
    {
        Descriptor = (PUSB_INTERFACE_ASSOCIATION_DESCRIPTOR)Offset;

        if (Descriptor->bLength == 0)
            break;

        if (Descriptor->bLength == sizeof(USB_INTERFACE_ASSOCIATION_DESCRIPTOR) &&
            Descriptor->bDescriptorType == USB_INTERFACE_ASSOCIATION_DESCRIPTOR_TYPE &&
            InterfaceNumber >= Descriptor->bFirstInterface &&
            (ULONG)InterfaceNumber < (ULONG)Descriptor->bFirstInterface + Descriptor->bInterfaceCount)
        {
            return TRUE;
        }

        Offset += Descriptor->bLength;
    }

    return FALSE;
}

NTSTATUS
USBCCGP_EnumWithAssociationDescriptor(
    IN PDEVICE_OBJECT DeviceObject)
{
    ULONG DescriptorCount, StandaloneCount, TotalCount, Index, FuncIndex, IfIndex;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    NTSTATUS Status = STATUS_SUCCESS;

    //
    // get device extension
    //
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    //
    // count association descriptors
    //
    DescriptorCount = USBCCGP_CountAssociationDescriptors(FDODeviceExtension->ConfigurationDescriptor);
    if (!DescriptorCount)
    {
        //
        // no descriptors found
        //
        return STATUS_NOT_SUPPORTED;
    }

    //
    // A composite device may mix IAD-grouped functions (for example a Bluetooth
    // radio spanning two interfaces) with standalone single-interface functions
    // (for example a vendor Wi-Fi interface). Count the interfaces that are not
    // claimed by any association descriptor so each gets its own function PDO.
    //
    StandaloneCount = 0;
    for (IfIndex = 0; IfIndex < FDODeviceExtension->ConfigurationDescriptor->bNumInterfaces; IfIndex++)
    {
        InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(FDODeviceExtension->ConfigurationDescriptor,
                                                                  FDODeviceExtension->ConfigurationDescriptor,
                                                                  IfIndex, 0, -1, -1, -1);
        if (InterfaceDescriptor != NULL &&
            !USBCCGP_IsInterfaceInAnyAssociation(FDODeviceExtension->ConfigurationDescriptor, InterfaceDescriptor->bInterfaceNumber))
        {
            StandaloneCount++;
        }
    }

    TotalCount = DescriptorCount + StandaloneCount;

    //
    // allocate function descriptor array
    //
    FDODeviceExtension->FunctionDescriptor = AllocateItem(NonPagedPool, sizeof(USBC_FUNCTION_DESCRIPTOR) * TotalCount);
    if (!FDODeviceExtension->FunctionDescriptor)
    {
        //
        // no memory
        //
        DPRINT1("USBCCGP_EnumWithAssociationDescriptor failed to allocate function descriptor count %x\n", TotalCount);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    FDODeviceExtension->FunctionDescriptorOwned = TRUE;

    for (Index = 0; Index < DescriptorCount; Index++)
    {
        //
        // init function descriptors for each association
        //
        Status = USBCCGP_InitFunctionDescriptor(FDODeviceExtension, Index, &FDODeviceExtension->FunctionDescriptor[Index]);
        if (!NT_SUCCESS(Status))
        {
            //
            // failed
            //
            return Status;
        }
    }

    //
    // init a single-interface function descriptor for each standalone interface
    //
    FuncIndex = DescriptorCount;
    for (IfIndex = 0; IfIndex < FDODeviceExtension->ConfigurationDescriptor->bNumInterfaces; IfIndex++)
    {
        InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(FDODeviceExtension->ConfigurationDescriptor,
                                                                  FDODeviceExtension->ConfigurationDescriptor,
                                                                  IfIndex, 0, -1, -1, -1);
        if (InterfaceDescriptor == NULL ||
            USBCCGP_IsInterfaceInAnyAssociation(FDODeviceExtension->ConfigurationDescriptor, InterfaceDescriptor->bInterfaceNumber))
        {
            continue;
        }

        FDODeviceExtension->FunctionDescriptor[FuncIndex].FunctionNumber = InterfaceDescriptor->bInterfaceNumber;
        FDODeviceExtension->FunctionDescriptor[FuncIndex].NumberOfInterfaces = 1;
        FDODeviceExtension->FunctionDescriptor[FuncIndex].InterfaceDescriptorList = AllocateItem(NonPagedPool, sizeof(PUSB_INTERFACE_DESCRIPTOR));
        if (!FDODeviceExtension->FunctionDescriptor[FuncIndex].InterfaceDescriptorList)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        FDODeviceExtension->FunctionDescriptor[FuncIndex].InterfaceDescriptorList[0] = InterfaceDescriptor;

        Status = USBCCG_InitIdsWithInterfaceDescriptor(FDODeviceExtension,
                                                       InterfaceDescriptor,
                                                       InterfaceDescriptor->bInterfaceNumber,
                                                       &FDODeviceExtension->FunctionDescriptor[FuncIndex]);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        FuncIndex++;
    }

    //
    // store function descriptor count
    //
    FDODeviceExtension->FunctionDescriptorCount = TotalCount;

    //
    // done
    //
    return Status;
}

NTSTATUS
USBCCG_InitIdsWithInterfaceDescriptor(
    IN PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN PUSB_INTERFACE_DESCRIPTOR Descriptor,
    IN ULONG FunctionIndex,
    OUT PUSBC_FUNCTION_DESCRIPTOR FunctionDescriptor)
{
    ULONG Index;
    WCHAR Buffer[320];
    LPWSTR DescriptionBuffer;
    NTSTATUS Status;

    //
    // now init interface description
    //
    if (Descriptor->iInterface)
    {
        //
        // get interface description
        //
         Status = USBCCGP_GetStringDescriptor(FDODeviceExtension->NextDeviceObject,
                                              100 * sizeof(WCHAR),
                                              Descriptor->iInterface,
                                              0x0409, //FIXME
                                              (PVOID*)&DescriptionBuffer);
        if (!NT_SUCCESS(Status))
        {
            //
            // no description
            //
            RtlInitUnicodeString(&FunctionDescriptor->FunctionDescription, L"");
        }
        else
        {
            //
            // init description
            //
            RtlInitUnicodeString(&FunctionDescriptor->FunctionDescription, DescriptionBuffer);
        }
        DPRINT("FunctionDescription %wZ\n", &FunctionDescriptor->FunctionDescription);
    }


    //
    // now init hardware id
    //
    Index = _swprintf(Buffer,
                      L"USB\\VID_%04X&PID_%04X&REV_%04X&MI_%02X",
                      FDODeviceExtension->DeviceDescriptor->idVendor,
                      FDODeviceExtension->DeviceDescriptor->idProduct,
                      FDODeviceExtension->DeviceDescriptor->bcdDevice,
                      FunctionIndex) + 1;
    Index += _swprintf(&Buffer[Index],
                       L"USB\\VID_%04X&PID_%04X&MI_%02X",
                       FDODeviceExtension->DeviceDescriptor->idVendor,
                       FDODeviceExtension->DeviceDescriptor->idProduct,
                       FunctionIndex) + 1;

    // allocate result buffer
    DescriptionBuffer = AllocateItem(NonPagedPool, (Index + 1) * sizeof(WCHAR));
    if (!DescriptionBuffer)
    {
        //
        // failed to allocate memory
        //
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // copy description
    RtlCopyMemory(DescriptionBuffer, Buffer, (Index + 1) * sizeof(WCHAR));
    FunctionDescriptor->HardwareId.Buffer = DescriptionBuffer;
    FunctionDescriptor->HardwareId.Length = Index * sizeof(WCHAR);
    FunctionDescriptor->HardwareId.MaximumLength = (Index + 1) * sizeof(WCHAR);

    //
    // now init the compatible id
    //
    Index = USBCCGP_FormatMsCompatIds(FDODeviceExtension, Descriptor->bInterfaceNumber, Buffer);
    Index += _swprintf(&Buffer[Index],
                       L"USB\\COMPAT_VID_%04x&Class_%02x&SubClass_%02x&Prot_%02x",
                       FDODeviceExtension->DeviceDescriptor->idVendor,
                       Descriptor->bInterfaceClass,
                       Descriptor->bInterfaceSubClass,
                       Descriptor->bInterfaceProtocol) + 1;
    Index += _swprintf(&Buffer[Index],
                       L"USB\\COMPAT_VID_%04x&Class_%02x&SubClass_%02x",
                       FDODeviceExtension->DeviceDescriptor->idVendor,
                       Descriptor->bInterfaceClass,
                       Descriptor->bInterfaceSubClass) + 1;
    Index += _swprintf(&Buffer[Index],
                       L"USB\\COMPAT_VID_%04x&Class_%02x",
                       FDODeviceExtension->DeviceDescriptor->idVendor,
                       Descriptor->bInterfaceClass) + 1;
    Index += _swprintf(&Buffer[Index],
                      L"USB\\Class_%02x&SubClass_%02x&Prot_%02x",
                      Descriptor->bInterfaceClass,
                      Descriptor->bInterfaceSubClass,
                      Descriptor->bInterfaceProtocol) + 1;
    Index += _swprintf(&Buffer[Index],
                       L"USB\\Class_%02x&SubClass_%02x",
                       Descriptor->bInterfaceClass,
                       Descriptor->bInterfaceSubClass) + 1;
    Index += _swprintf(&Buffer[Index],
                       L"USB\\Class_%02x",
                       Descriptor->bInterfaceClass) + 1;

    // allocate result buffer
    DescriptionBuffer = AllocateItem(NonPagedPool, (Index + 1) * sizeof(WCHAR));
    if (!DescriptionBuffer)
    {
        //
        // failed to allocate memory
        //
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // copy description
    RtlCopyMemory(DescriptionBuffer, Buffer, (Index + 1) * sizeof(WCHAR));
    FunctionDescriptor->CompatibleId.Buffer = DescriptionBuffer;
    FunctionDescriptor->CompatibleId.Length = Index * sizeof(WCHAR);
    FunctionDescriptor->CompatibleId.MaximumLength = (Index + 1) * sizeof(WCHAR);

    //
    // done
    //
    return STATUS_SUCCESS;
}


NTSTATUS
USBCCGP_LegacyEnum(
    IN PDEVICE_OBJECT DeviceObject)
{
    ULONG Index, SubIndex;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor, NextInterfaceDescriptor;

    //
    // get device extension
    //
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    //
    // sanity check
    //
    ASSERT(FDODeviceExtension->ConfigurationDescriptor->bNumInterfaces);

    //
    // allocate function array
    //
    FDODeviceExtension->FunctionDescriptor = AllocateItem(NonPagedPool, sizeof(USBC_FUNCTION_DESCRIPTOR) * FDODeviceExtension->ConfigurationDescriptor->bNumInterfaces);
    if (!FDODeviceExtension->FunctionDescriptor)
    {
        //
        // no memory
        //
        DPRINT1("USBCCGP_EnumWithAssociationDescriptor failed to allocate function descriptor %lu\n", FDODeviceExtension->ConfigurationDescriptor->bNumInterfaces);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    FDODeviceExtension->FunctionDescriptorOwned = TRUE;

    //
    // init function descriptors
    //
    FDODeviceExtension->FunctionDescriptorCount = FDODeviceExtension->ConfigurationDescriptor->bNumInterfaces;
    for (Index = 0; Index < FDODeviceExtension->ConfigurationDescriptor->bNumInterfaces; Index++)
    {
        // get interface descriptor
        InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(FDODeviceExtension->ConfigurationDescriptor, FDODeviceExtension->ConfigurationDescriptor, Index, 0, -1, -1, -1);
        if (InterfaceDescriptor == NULL)
        {
            //
            // failed to find interface descriptor
            //
            DPRINT1("[USBCCGP] Failed to find interface descriptor index %lu\n", Index);
            return STATUS_UNSUCCESSFUL;
        }

        SubIndex = 0;
        if (InterfaceDescriptor->bInterfaceClass == USB_DEVICE_CLASS_AUDIO)
        {
            // AUDIO CLASS lets group all audio interfaces together
            //
            // init function descriptor
            //
            FDODeviceExtension->FunctionDescriptor[Index].FunctionNumber = Index;
            FDODeviceExtension->FunctionDescriptor[Index].NumberOfInterfaces = 1;
            FDODeviceExtension->FunctionDescriptor[Index].InterfaceDescriptorList = AllocateItem(NonPagedPool, sizeof(PUSB_INTERFACE_DESCRIPTOR) * FDODeviceExtension->ConfigurationDescriptor->bNumInterfaces);
            if (!FDODeviceExtension->FunctionDescriptor[Index].InterfaceDescriptorList)
            {
                //
                // no memory
                //
                FreeItem(FDODeviceExtension->FunctionDescriptor);
                FDODeviceExtension->FunctionDescriptor = NULL;
                FDODeviceExtension->FunctionDescriptorCount = 0;
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            //
            // store interface descriptor
            //
            FDODeviceExtension->FunctionDescriptor[Index].InterfaceDescriptorList[SubIndex] = InterfaceDescriptor;
            while (TRUE)
            {
                NextInterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(FDODeviceExtension->ConfigurationDescriptor, FDODeviceExtension->ConfigurationDescriptor, Index + SubIndex + 1, 0, -1, -1, -1);
                if (!NextInterfaceDescriptor || NextInterfaceDescriptor->bInterfaceClass != USB_DEVICE_CLASS_AUDIO)
                {
                    // end of collection
                    break;
                }
                SubIndex++;
                ASSERT(SubIndex < FDODeviceExtension->ConfigurationDescriptor->bNumInterfaces);
                FDODeviceExtension->FunctionDescriptor[Index].NumberOfInterfaces++;
                FDODeviceExtension->FunctionDescriptor[Index].InterfaceDescriptorList[SubIndex] = NextInterfaceDescriptor;
            }
        }
        else
        {
            //
            // init function descriptor
            //
            FDODeviceExtension->FunctionDescriptor[Index].FunctionNumber = Index;
            FDODeviceExtension->FunctionDescriptor[Index].NumberOfInterfaces = 1;
            FDODeviceExtension->FunctionDescriptor[Index].InterfaceDescriptorList = AllocateItem(NonPagedPool, sizeof(PUSB_INTERFACE_DESCRIPTOR));
            if (!FDODeviceExtension->FunctionDescriptor[Index].InterfaceDescriptorList)
            {
                //
                // no memory
                //
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            //
            // store interface descriptor
            //
            FDODeviceExtension->FunctionDescriptor[Index].InterfaceDescriptorList[0] = InterfaceDescriptor;
        }

        //
        // now init the device ids
        //
        Status = USBCCG_InitIdsWithInterfaceDescriptor(FDODeviceExtension, InterfaceDescriptor, Index, &FDODeviceExtension->FunctionDescriptor[Index]);
        if (!NT_SUCCESS(Status))
        {
            //
            // failed to init ids
            //
            DPRINT1("[USBCCGP] Failed to init ids with %lx\n", Status);
            return Status;
        }
        //
        // increment interface count
        //
        Index += SubIndex;
    }

    //
    // done
    //
    return Status;
}

NTSTATUS
USBCCGP_EnumWithUnionFunctionDescriptors(
    IN PDEVICE_OBJECT DeviceObject)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
USBCCGP_EnumWithAudioLegacy(
    IN PDEVICE_OBJECT DeviceObject)
{
    ULONG Index;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor, FirstDescriptor = NULL;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;

    //
    // get device extension
    //
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);


    //
    // first check if all interfaces belong to the same audio class
    //
    for (Index = 0; Index < FDODeviceExtension->ConfigurationDescriptor->bNumInterfaces; Index++)
    {
        //
        // get interface descriptor
        //
        InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(FDODeviceExtension->ConfigurationDescriptor, FDODeviceExtension->ConfigurationDescriptor, Index, 0, -1, -1, -1);
        DPRINT("Index %lu Descriptor %p\n", Index, InterfaceDescriptor);
        if (!InterfaceDescriptor)
            return STATUS_UNSUCCESSFUL;

        if (InterfaceDescriptor->bInterfaceClass != USB_DEVICE_CLASS_AUDIO)
        {
            //
            // collection contains non audio class
            //
            return STATUS_UNSUCCESSFUL;
        }

        if (FirstDescriptor == NULL)
        {
            //
            // store interface descriptor
            //
            FirstDescriptor = InterfaceDescriptor;
            continue;
        }

        if (FirstDescriptor->bInterfaceSubClass == InterfaceDescriptor->bInterfaceSubClass)
        {
            //
            // interface subclass must be different from the first interface
            //
            return STATUS_UNSUCCESSFUL;
        }
    }

    if (!FirstDescriptor)
        return STATUS_UNSUCCESSFUL;

    //
    // this is an composite audio device
    //
    DPRINT("[USBCCGP] Audio Composite Device detected\n");

    //
    // audio interfaces are all grouped into one single function
    //
    FDODeviceExtension->FunctionDescriptor = AllocateItem(NonPagedPool, sizeof(USBC_FUNCTION_DESCRIPTOR));
    if (!FDODeviceExtension->FunctionDescriptor)
    {
        //
        // no memory
        //
        DPRINT1("USBCCGP_EnumWithAssociationDescriptor failed to allocate function descriptor count\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    FDODeviceExtension->FunctionDescriptorOwned = TRUE;

    //
    // init function number
    //
    FDODeviceExtension->FunctionDescriptor[0].FunctionNumber = 0;

    //
    // store interfaces
    //
    Status = AllocateInterfaceDescriptorsArray(FDODeviceExtension->ConfigurationDescriptor, &FDODeviceExtension->FunctionDescriptor[0].InterfaceDescriptorList);
    if (!NT_SUCCESS(Status))
    {
        //
        // failed to allocate descriptor array
        //
        DPRINT1("[USBCCGP] Failed to allocate descriptor array %lx\n", Status);
        return Status;
    }

    //
    // now init the device ids
    //
    Status = USBCCG_InitIdsWithInterfaceDescriptor(FDODeviceExtension, FirstDescriptor, 0, &FDODeviceExtension->FunctionDescriptor[0]);
    if (!NT_SUCCESS(Status))
    {
        //
        // failed to init ids
        //
        DPRINT1("[USBCCGP] Failed to init ids with %lx\n", Status);
        return Status;
    }

    //
    // number of interfaces
    //
    FDODeviceExtension->FunctionDescriptor[0].NumberOfInterfaces = FDODeviceExtension->ConfigurationDescriptor->bNumInterfaces;

    //
    // store function count
    //
    FDODeviceExtension->FunctionDescriptorCount = 1;

    //
    // done
    //
    return STATUS_SUCCESS;
}

NTSTATUS
USBCCGP_EnumerateFunctions(
    IN PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS Status;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;

    //
    // get device extension
    //
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    USBCCGP_QueryMsOs20Set(FDODeviceExtension);
    if (!FDODeviceExtension->MsOs20DescriptorSet)
        USBCCGP_QueryMsCompatIds(FDODeviceExtension);

    //
    // first try with filter driver
    //
    Status = USBCCGP_CustomEnumWithInterface(DeviceObject);
    if (NT_SUCCESS(Status))
    {
        //
        // succeeded
        //
        return Status;
    }

    //
    // enumerate functions with interface association descriptor
    //
    Status = USBCCGP_EnumWithAssociationDescriptor(DeviceObject);
    if (NT_SUCCESS(Status))
    {
        //
        // succeeded
        //
        return Status;
    }

#if 0
    //
    // try with union function descriptors
    //
    Status = USBCCGP_EnumWithUnionFunctionDescriptors(DeviceObject);
    if (NT_SUCCESS(Status))
    {
        //
        // succeeded
        //
        return Status;
    }
#endif

    //
    // try with legacy audio methods
    //
    Status = USBCCGP_EnumWithAudioLegacy(DeviceObject);
    if (NT_SUCCESS(Status))
    {
        //
        // succeeded
        //
        return Status;
    }

    //
    // try with legacy enumeration
    //
    return USBCCGP_LegacyEnum(DeviceObject);
}
