/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Choosing the resources a device will be started with
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include "../pnpio.h"
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

static
NTSTATUS
IopPickOneDescriptor(
    _In_ PIO_RESOURCE_DESCRIPTOR Requirement,
    _In_opt_ PDEVICE_NODE DeviceNode,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor)
{
    ULONG Attempts = 0;

    IopDescribeFromRequirement(Requirement, Descriptor);

    if (Requirement->Type == CmResourceTypeDevicePrivate ||
        Requirement->Type == CmResourceTypeConfigData ||
        Requirement->Type == CmResourceTypeDeviceSpecific)
    {
        return STATUS_SUCCESS;
    }

    while (Attempts < 0x10000)
    {
        if (IopResDbCheckDescriptor(Descriptor, DeviceNode, NULL))
        {
            return STATUS_SUCCESS;
        }

        if (!IopAdvanceCandidate(Requirement, Descriptor))
        {
            return STATUS_CONFLICTING_ADDRESSES;
        }

        Attempts++;
    }

    return STATUS_CONFLICTING_ADDRESSES;
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
    ULONG Size;
    ULONG Index;

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

        for (Index = 0; Index < List->Count; Index++)
        {
            if (List->Descriptors[Index].Option & IO_RESOURCE_ALTERNATIVE)
            {
                continue;
            }

            Status = IopPickOneDescriptor(&List->Descriptors[Index],
                                          DeviceNode,
                                          &Partial[Index]);
            if (!NT_SUCCESS(Status)) break;
        }

        if (NT_SUCCESS(Status))
        {
            *ResourceList = Allocated;
            return STATUS_SUCCESS;
        }

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
        if (Conflict) return STATUS_CONFLICTING_ADDRESSES;

        Assigned = ExAllocatePoolZero(
            PagedPool,
            PnpDetermineResourceListSize(DeviceNode->BootResources),
            TAG_IO_RESOURCE);
        if (Assigned == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(Assigned,
                      DeviceNode->BootResources,
                      PnpDetermineResourceListSize(
                          DeviceNode->BootResources));

        DeviceNode->ResourceList = Assigned;
        DeviceNode->ResourceListTranslated = Assigned;

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
    if (!NT_SUCCESS(Status) || Conflict)
    {
        ExFreePoolWithTag(Assigned, TAG_IO_RESOURCE);
        return NT_SUCCESS(Status) ? STATUS_CONFLICTING_ADDRESSES : Status;
    }

    DeviceNode->ResourceList = Assigned;
    DeviceNode->ResourceListTranslated = Assigned;

    IopUpdateControlKeyWithResources(DeviceNode);

    return STATUS_SUCCESS;
}

VOID
IopReleaseNewMessageVectors(
    _In_opt_ PCM_RESOURCE_LIST ResourceList,
    _In_ ULONG OldCount)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
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

            if (Seen > OldCount)
            {
                /* TODO: no message-vector pool to give the vector back to */
                DPRINT("PnP: message vector %lu is dropped\n",
                       Descriptor->u.MessageInterrupt.Translated.Vector);
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
