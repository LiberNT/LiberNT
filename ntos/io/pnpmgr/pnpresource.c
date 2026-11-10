/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Choosing the resources a device will be started with
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include "../pnpio.h"
#include <reactos/hal/msi.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
IopDescribeFromRequirement(
    _In_ PIO_RESOURCE_DESCRIPTOR Requirement,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor)
{
    RtlZeroMemory(Descriptor, sizeof(*Descriptor));

    Descriptor->Type = Requirement->Type;
    Descriptor->ShareDisposition = Requirement->ShareDisposition;
    Descriptor->Flags = Requirement->Flags;

    switch (Requirement->Type)
    {
        case CmResourceTypePort:
            Descriptor->u.Port.Start = Requirement->u.Port.MinimumAddress;
            Descriptor->u.Port.Length = Requirement->u.Port.Length;
            break;

        case CmResourceTypeMemory:
            Descriptor->u.Memory.Start = Requirement->u.Memory.MinimumAddress;
            Descriptor->u.Memory.Length = Requirement->u.Memory.Length;
            break;

        case CmResourceTypeInterrupt:
            Descriptor->u.Interrupt.Level = Requirement->u.Interrupt.MinimumVector;
            Descriptor->u.Interrupt.Vector =
                Requirement->u.Interrupt.MinimumVector;
            Descriptor->u.Interrupt.Affinity =
                Requirement->u.Interrupt.TargetedProcessors;
            break;

        case CmResourceTypeDma:
            Descriptor->u.Dma.Channel = Requirement->u.Dma.MinimumChannel;
            Descriptor->u.Dma.Port = 0;
            break;

        case CmResourceTypeBusNumber:
            Descriptor->u.BusNumber.Start =
                Requirement->u.BusNumber.MinBusNumber;
            Descriptor->u.BusNumber.Length = Requirement->u.BusNumber.Length;
            break;

        case CmResourceTypeDevicePrivate:
            RtlCopyMemory(Descriptor->u.DevicePrivate.Data,
                          Requirement->u.DevicePrivate.Data,
                          sizeof(Descriptor->u.DevicePrivate.Data));
            break;

        default:
            break;
    }
}

static
BOOLEAN
IopAdvanceCandidate(
    _In_ PIO_RESOURCE_DESCRIPTOR Requirement,
    _Inout_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor)
{
    ULONGLONG Alignment;
    ULONGLONG Next;

    switch (Requirement->Type)
    {
        case CmResourceTypePort:
            Alignment = Requirement->u.Port.Alignment;
            if (Alignment == 0) Alignment = 1;

            Next = (ULONGLONG)Descriptor->u.Port.Start.QuadPart + Alignment;

            if (Next + Requirement->u.Port.Length - 1 >
                (ULONGLONG)Requirement->u.Port.MaximumAddress.QuadPart)
            {
                return FALSE;
            }

            Descriptor->u.Port.Start.QuadPart = (LONGLONG)Next;
            return TRUE;

        case CmResourceTypeMemory:
            Alignment = Requirement->u.Memory.Alignment;
            if (Alignment == 0) Alignment = 1;

            Next = (ULONGLONG)Descriptor->u.Memory.Start.QuadPart + Alignment;

            if (Next + Requirement->u.Memory.Length - 1 >
                (ULONGLONG)Requirement->u.Memory.MaximumAddress.QuadPart)
            {
                return FALSE;
            }

            Descriptor->u.Memory.Start.QuadPart = (LONGLONG)Next;
            return TRUE;

        case CmResourceTypeInterrupt:
            if (Descriptor->u.Interrupt.Vector >=
                Requirement->u.Interrupt.MaximumVector)
            {
                return FALSE;
            }

            Descriptor->u.Interrupt.Vector++;
            Descriptor->u.Interrupt.Level =
                Descriptor->u.Interrupt.Vector;
            return TRUE;

        case CmResourceTypeDma:
            if (Descriptor->u.Dma.Channel >=
                Requirement->u.Dma.MaximumChannel)
            {
                return FALSE;
            }

            Descriptor->u.Dma.Channel++;
            return TRUE;

        case CmResourceTypeBusNumber:
            if (Descriptor->u.BusNumber.Start +
                    Descriptor->u.BusNumber.Length >
                Requirement->u.BusNumber.MaxBusNumber)
            {
                return FALSE;
            }

            Descriptor->u.BusNumber.Start++;
            return TRUE;

        default:
            return FALSE;
    }
}

/* A message interrupt is a block of vectors the HAL hands out; the requirement's
 * range below the message token gives the number of messages */
static
NTSTATUS
IopAllocateMessageInterrupt(
    _In_ PIO_RESOURCE_DESCRIPTOR Requirement,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor)
{
    HAL_MESSAGE_ROUTING_INFO RoutingInfo;
    ULONG MessageCount = 1;
    NTSTATUS Status;

    if (Requirement->u.Interrupt.MaximumVector == CM_RESOURCE_INTERRUPT_MESSAGE_TOKEN &&
        Requirement->u.Interrupt.MinimumVector <= CM_RESOURCE_INTERRUPT_MESSAGE_TOKEN)
    {
        MessageCount = CM_RESOURCE_INTERRUPT_MESSAGE_TOKEN -
                       Requirement->u.Interrupt.MinimumVector + 1;
    }

    RtlZeroMemory(&RoutingInfo, sizeof(RoutingInfo));
    RoutingInfo.Version = HAL_MESSAGE_ROUTING_INFO_VERSION;
    RoutingInfo.Flags = HAL_MSI_ROUTING_ALLOCATE_VECTOR;
    RoutingInfo.MessageCount = MessageCount;
    RoutingInfo.TargetProcessors = Requirement->u.Interrupt.TargetedProcessors;

    /* TODO: the i386 HAL does not export the message router yet */
    Status = HalpGetMessageRoutingInfo(&RoutingInfo);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(Descriptor, sizeof(*Descriptor));
    Descriptor->Type = CmResourceTypeInterrupt;
    Descriptor->ShareDisposition = Requirement->ShareDisposition;
    Descriptor->Flags = Requirement->Flags;
    Descriptor->u.MessageInterrupt.Raw.MessageCount = (USHORT)MessageCount;
    Descriptor->u.MessageInterrupt.Raw.Vector = RoutingInfo.Vector;
    Descriptor->u.MessageInterrupt.Raw.Affinity = RoutingInfo.TargetProcessors;

    return STATUS_SUCCESS;
}

static
NTSTATUS
IopPickOneDescriptor(
    _In_ PIO_RESOURCE_DESCRIPTOR Requirement,
    _In_opt_ PDEVICE_NODE DeviceNode,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor)
{
    ULONG Attempts = 0;

    if (Requirement->Type == CmResourceTypeInterrupt &&
        (Requirement->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
    {
        return IopAllocateMessageInterrupt(Requirement, Descriptor);
    }

    IopDescribeFromRequirement(Requirement, Descriptor);

    if (Requirement->Type == CmResourceTypeDevicePrivate ||
        Requirement->Type == CmResourceTypeConfigData ||
        Requirement->Type == CmResourceTypeDeviceSpecific)
    {
        return STATUS_SUCCESS;
    }

    while (Attempts < 0x10000)
    {
        if (!IopResDbCheckDescriptor(Descriptor, DeviceNode, NULL))
        {
            return STATUS_SUCCESS;
        }

        if (!IopAdvanceCandidate(Requirement, Descriptor)) break;

        Attempts++;
    }

    /* The resource database records overlaps without refusing them, so the
     * preferred setting is kept when nothing in the range is free */
    DPRINT1("PnP: no free setting for a type %u requirement of %wZ, the preferred one is shared\n",
            Requirement->Type,
            DeviceNode != NULL ? &DeviceNode->InstancePath : NULL);

    IopDescribeFromRequirement(Requirement, Descriptor);
    return STATUS_SUCCESS;
}

static
PIO_RESOURCE_LIST
IopSelectAlternativeList(
    _In_ PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList,
    _In_ ULONG Alternative)
{
    PIO_RESOURCE_LIST List = &RequirementsList->List[0];
    ULONG Index;

    for (Index = 0; Index < Alternative; Index++)
    {
        List = (PIO_RESOURCE_LIST)
            ((PUCHAR)&List->Descriptors[0] +
             List->Count * sizeof(IO_RESOURCE_DESCRIPTOR));
    }

    return List;
}

NTSTATUS
NTAPI
IopFixupResourceListWithRequirements(
    IN PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList,
    OUT PCM_RESOURCE_LIST *ResourceList,
    _In_opt_ PDEVICE_NODE DeviceNode)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    PCM_RESOURCE_LIST Allocated;
    PIO_RESOURCE_LIST List;
    NTSTATUS Status = STATUS_CONFLICTING_ADDRESSES;
    ULONG Alternative;
    ULONG Choice;
    ULONG Count;
    ULONG Size;
    ULONG Index;
    ULONG Next;

    PAGED_CODE();

    *ResourceList = NULL;

    if (RequirementsList == NULL) return STATUS_INVALID_PARAMETER;
    if (RequirementsList->AlternativeLists == 0) return STATUS_SUCCESS;

    for (Alternative = 0;
         Alternative < RequirementsList->AlternativeLists;
         Alternative++)
    {
        List = IopSelectAlternativeList(RequirementsList, Alternative);

        Size = (ULONG)(FIELD_OFFSET(CM_RESOURCE_LIST, List) +
                       FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR,
                                    PartialResourceList.PartialDescriptors) +
                       List->Count * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));

        Allocated = ExAllocatePoolZero(PagedPool, Size, TAG_IO_RESOURCE);
        if (Allocated == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Allocated->Count = 1;
        Allocated->List[0].InterfaceType = RequirementsList->InterfaceType;
        Allocated->List[0].BusNumber = RequirementsList->BusNumber;
        Allocated->List[0].PartialResourceList.Version = 1;
        Allocated->List[0].PartialResourceList.Revision = 1;
        Allocated->List[0].PartialResourceList.Count = List->Count;

        Partial =
            Allocated->List[0].PartialResourceList.PartialDescriptors;

        Status = STATUS_SUCCESS;
        Count = 0;

        for (Index = 0; Index < List->Count; Index = Next)
        {
            /* A descriptor and the alternatives after it describe one resource */
            Next = Index + 1;
            while (Next < List->Count &&
                   (List->Descriptors[Next].Option & IO_RESOURCE_ALTERNATIVE))
            {
                Next++;
            }

            if (List->Descriptors[Index].Type == CmResourceTypeConfigData) continue;

            Status = STATUS_CONFLICTING_ADDRESSES;
            for (Choice = Index; Choice < Next && !NT_SUCCESS(Status); Choice++)
            {
                Status = IopPickOneDescriptor(&List->Descriptors[Choice],
                                              DeviceNode,
                                              &Partial[Count]);
            }

            if (!NT_SUCCESS(Status)) break;

            Count++;
        }

        Allocated->List[0].PartialResourceList.Count = Count;

        if (NT_SUCCESS(Status))
        {
            *ResourceList = Allocated;
            return STATUS_SUCCESS;
        }

        IopReleaseMessageTokens(Allocated);
        ExFreePoolWithTag(Allocated, TAG_IO_RESOURCE);
    }

    return Status;
}

NTSTATUS
IopFilterResourceRequirements(
    IN PDEVICE_NODE DeviceNode)
{
    IO_STACK_LOCATION StackLocation;
    PIO_RESOURCE_REQUIREMENTS_LIST Filtered = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_FILTER_RESOURCE_REQUIREMENTS;
    StackLocation.Parameters.FilterResourceRequirements
        .IoResourceRequirementList = DeviceNode->ResourceRequirements;

    Status = IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                                &StackLocation,
                                (PVOID *)&Filtered);
    if (Status == STATUS_NOT_SUPPORTED) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;

    if (Filtered != NULL && Filtered != DeviceNode->ResourceRequirements)
    {
        if (DeviceNode->ResourceRequirements != NULL)
        {
            ExFreePool(DeviceNode->ResourceRequirements);
        }

        DeviceNode->ResourceRequirements = Filtered;
    }

    PiClearDevNodeFlag(DeviceNode, DNF_RESOURCE_REQUIREMENTS_NEED_FILTERED);

    return STATUS_SUCCESS;
}

NTSTATUS
IopUpdateControlKeyWithResources(
    _In_ PDEVICE_NODE DeviceNode)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;
    HANDLE InstanceKey;
    HANDLE ControlKey;

    PAGED_CODE();

    Status = PnpDeviceObjectToDeviceInstance(DeviceNode->PhysicalDeviceObject,
                                             &InstanceKey,
                                             KEY_ALL_ACCESS);
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&ValueName, L"Control");

    Status = IopCreateRegistryKeyEx(&ControlKey,
                                    InstanceKey,
                                    &ValueName,
                                    KEY_ALL_ACCESS,
                                    REG_OPTION_VOLATILE,
                                    NULL);
    ZwClose(InstanceKey);

    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&ValueName, L"AllocConfig");

    if (DeviceNode->ResourceList == NULL)
    {
        Status = ZwDeleteValueKey(ControlKey, &ValueName);
    }
    else
    {
        Status = ZwSetValueKey(
            ControlKey,
            &ValueName,
            0,
            REG_RESOURCE_LIST,
            DeviceNode->ResourceList,
            PnpDetermineResourceListSize(DeviceNode->ResourceList));
    }

    ZwClose(ControlKey);

    return Status;
}

/* Turns the bus-relative list into the one the processor uses, a copy of its own */
static
NTSTATUS
IopTranslateResourceList(
    _In_ PCM_RESOURCE_LIST Raw,
    _Out_ PCM_RESOURCE_LIST *Translated)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    PHYSICAL_ADDRESS Address;
    PCM_RESOURCE_LIST List;
    KAFFINITY Affinity;
    ULONG AddressSpace;
    ULONG ListIndex;
    ULONG Index;
    ULONG Vector;
    ULONG Size;
    KIRQL Irql;

    PAGED_CODE();

    *Translated = NULL;

    Size = PnpDetermineResourceListSize(Raw);

    List = ExAllocatePoolZero(PagedPool, Size, TAG_IO_RESOURCE);
    if (List == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(List, Raw, Size);

    Full = &List->List[0];
    for (ListIndex = 0; ListIndex < List->Count; ListIndex++)
    {
        Descriptor = Full->PartialResourceList.PartialDescriptors;

        for (Index = 0; Index < Full->PartialResourceList.Count; Index++)
        {
            switch (Descriptor->Type)
            {
                case CmResourceTypeInterrupt:
                    if (Descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
                    {
                        HAL_MESSAGE_ROUTING_INFO RoutingInfo;

                        RtlZeroMemory(&RoutingInfo, sizeof(RoutingInfo));
                        RoutingInfo.Version = HAL_MESSAGE_ROUTING_INFO_VERSION;
                        RoutingInfo.MessageCount = Descriptor->u.MessageInterrupt.Raw.MessageCount;
                        RoutingInfo.Vector = Descriptor->u.MessageInterrupt.Raw.Vector;
                        RoutingInfo.TargetProcessors = Descriptor->u.MessageInterrupt.Raw.Affinity;

                        if (!NT_SUCCESS(HalpGetMessageRoutingInfo(&RoutingInfo)))
                        {
                            ExFreePoolWithTag(List, TAG_IO_RESOURCE);
                            return STATUS_UNSUCCESSFUL;
                        }

                        Descriptor->u.MessageInterrupt.Translated.Level = RoutingInfo.Irql;
                        Descriptor->u.MessageInterrupt.Translated.Vector = RoutingInfo.Vector;
                        Descriptor->u.MessageInterrupt.Translated.Affinity = RoutingInfo.TargetProcessors;
                        break;
                    }

                    Vector = HalGetInterruptVector(Full->InterfaceType,
                                                   Full->BusNumber,
                                                   Descriptor->u.Interrupt.Level,
                                                   Descriptor->u.Interrupt.Vector,
                                                   &Irql,
                                                   &Affinity);
                    if (Vector == 0)
                    {
                        ExFreePoolWithTag(List, TAG_IO_RESOURCE);
                        return STATUS_UNSUCCESSFUL;
                    }

                    Descriptor->u.Interrupt.Level = Irql;
                    Descriptor->u.Interrupt.Vector = Vector;
                    Descriptor->u.Interrupt.Affinity = Affinity;
                    break;

                case CmResourceTypePort:
                case CmResourceTypeMemory:
                    AddressSpace = (Descriptor->Type == CmResourceTypePort) ? 1 : 0;

                    if (!HalTranslateBusAddress(Full->InterfaceType,
                                                Full->BusNumber,
                                                Descriptor->u.Generic.Start,
                                                &AddressSpace,
                                                &Address))
                    {
                        ExFreePoolWithTag(List, TAG_IO_RESOURCE);
                        return STATUS_UNSUCCESSFUL;
                    }

                    Descriptor->u.Generic.Start = Address;

                    if (Descriptor->Type == CmResourceTypePort && AddressSpace == 0)
                    {
                        Descriptor->Type = CmResourceTypeMemory;
                        Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
                    }
                    break;

                default:
                    break;
            }

            Descriptor = PipGetNextCmPartialDescriptor(Descriptor);
        }

        Full = (PCM_FULL_RESOURCE_DESCRIPTOR)Descriptor;
    }

    *Translated = List;
    return STATUS_SUCCESS;
}

/* Takes ownership of Raw and gives the node its raw and translated lists */
NTSTATUS
IopSetDeviceResources(
    _Inout_ PDEVICE_NODE DeviceNode,
    _In_ PCM_RESOURCE_LIST Raw)
{
    PCM_RESOURCE_LIST Translated;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopTranslateResourceList(Raw, &Translated);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Raw, TAG_IO_RESOURCE);
        return Status;
    }

    DeviceNode->ResourceList = Raw;
    DeviceNode->ResourceListTranslated = Translated;


    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IopAssignDeviceResources(
    IN PDEVICE_NODE DeviceNode)
{
    PCM_RESOURCE_LIST Assigned = NULL;
    PCM_RESOURCE_LIST Boot = NULL;
    BOOLEAN Conflict = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    if (DeviceNode->Flags & DNF_NO_RESOURCE_REQUIRED)
    {
        return STATUS_SUCCESS;
    }

    IopResDbEnsureSeeded();

    if (DeviceNode->BootResources == NULL)
    {
        Status = PiIrpQueryResources(DeviceNode, &Boot);
        if (NT_SUCCESS(Status) && Boot != NULL)
        {
            DeviceNode->BootResources = Boot;
            PiSetDevNodeFlag(DeviceNode, DNF_HAS_BOOT_CONFIG);
        }
    }

    if (DeviceNode->ResourceRequirements == NULL)
    {
        PIO_RESOURCE_REQUIREMENTS_LIST Requirements = NULL;

        Status = PiIrpQueryResourceRequirements(DeviceNode, &Requirements);
        if (NT_SUCCESS(Status) && Requirements != NULL)
        {
            DeviceNode->ResourceRequirements = Requirements;
            PiSetDevNodeFlag(DeviceNode,
                             DNF_RESOURCE_REQUIREMENTS_NEED_FILTERED);
        }
    }

    if (DeviceNode->Flags & DNF_RESOURCE_REQUIREMENTS_NEED_FILTERED)
    {
        IopFilterResourceRequirements(DeviceNode);
    }

    if (DeviceNode->ResourceRequirements == NULL)
    {
        if (DeviceNode->BootResources == NULL)
        {
            PiSetDevNodeFlag(DeviceNode, DNF_NO_RESOURCE_REQUIRED);
            return STATUS_SUCCESS;
        }

        Status = IopResDbReserve(DeviceNode,
                                 DeviceNode->BootResources,
                                 &Conflict);
        if (!NT_SUCCESS(Status)) return Status;

        Assigned = ExAllocatePoolZero(
            PagedPool,
            PnpDetermineResourceListSize(DeviceNode->BootResources),
            TAG_IO_RESOURCE);
        if (Assigned == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(Assigned,
                      DeviceNode->BootResources,
                      PnpDetermineResourceListSize(
                          DeviceNode->BootResources));

        Status = IopSetDeviceResources(DeviceNode, Assigned);
        if (!NT_SUCCESS(Status))
        {
            IopResDbRelease(DeviceNode);
            return Status;
        }

        IopUpdateControlKeyWithResources(DeviceNode);

        return STATUS_SUCCESS;
    }

    Status = IopFixupResourceListWithRequirements(
        DeviceNode->ResourceRequirements,
        &Assigned,
        DeviceNode);
    if (!NT_SUCCESS(Status)) return Status;

    if (Assigned == NULL)
    {
        PiSetDevNodeFlag(DeviceNode, DNF_NO_RESOURCE_REQUIRED);
        return STATUS_SUCCESS;
    }

    Status = IopResDbReserve(DeviceNode, Assigned, &Conflict);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Assigned, TAG_IO_RESOURCE);
        return Status;
    }

    Status = IopSetDeviceResources(DeviceNode, Assigned);
    if (!NT_SUCCESS(Status))
    {
        IopResDbRelease(DeviceNode);
        return Status;
    }

    IopUpdateControlKeyWithResources(DeviceNode);

    return STATUS_SUCCESS;
}

VOID
IopReleaseNewMessageVectors(
    _In_opt_ PCM_RESOURCE_LIST ResourceList,
    _In_ ULONG OldCount)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    HAL_MESSAGE_ROUTING_INFO RoutingInfo;
    ULONG Message;
    ULONG Seen = 0;
    ULONG Index;

    PAGED_CODE();

    if (ResourceList == NULL || ResourceList->Count == 0) return;

    Descriptor =
        ResourceList->List[0].PartialResourceList.PartialDescriptors;

    for (Index = 0;
         Index < ResourceList->List[0].PartialResourceList.Count;
         Index++)
    {
        if (Descriptor->Type == CmResourceTypeInterrupt &&
            (Descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
        {
            Seen++;

            for (Message = 0;
                 Seen > OldCount && Message < Descriptor->u.MessageInterrupt.Raw.MessageCount;
                 Message++)
            {
                RtlZeroMemory(&RoutingInfo, sizeof(RoutingInfo));
                RoutingInfo.Version = HAL_MESSAGE_ROUTING_INFO_VERSION;
                RoutingInfo.Flags = HAL_MSI_ROUTING_RELEASE_VECTOR;
                RoutingInfo.MessageCount = 1;
                RoutingInfo.Vector = Descriptor->u.MessageInterrupt.Raw.Vector + Message;

                HalpGetMessageRoutingInfo(&RoutingInfo);
            }
        }

        Descriptor = PipGetNextCmPartialDescriptor(Descriptor);
    }
}

VOID
IopReleaseMessageTokens(
    _In_opt_ PCM_RESOURCE_LIST ResourceList)
{
    IopReleaseNewMessageVectors(ResourceList, 0);
}
