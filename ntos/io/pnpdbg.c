/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Printing what the PnP manager is holding, for a human to read
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "ioint.h"
#include "pnpio.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

PCM_PARTIAL_RESOURCE_DESCRIPTOR
NTAPI
PipGetNextCmPartialDescriptor(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Next = CmDescriptor + 1;

    if (CmDescriptor->Type != CmResourceTypeDeviceSpecific) return Next;

    return (PCM_PARTIAL_RESOURCE_DESCRIPTOR)
        ((PUCHAR)Next + CmDescriptor->u.DeviceSpecificData.DataSize);
}

VOID
NTAPI
PipDumpCmResourceDescriptor(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
    _In_ ULONG DebugLevel)
{
    UNREFERENCED_PARAMETER(DebugLevel);

    switch (Descriptor->Type)
    {
        case CmResourceTypePort:
            DPRINT1("  port %I64x for %lx, share %x, flags %x\n",
                    Descriptor->u.Port.Start.QuadPart,
                    Descriptor->u.Port.Length,
                    Descriptor->ShareDisposition,
                    Descriptor->Flags);
            break;

        case CmResourceTypeInterrupt:
            DPRINT1("  interrupt level %lx vector %lx affinity %p\n",
                    Descriptor->u.Interrupt.Level,
                    Descriptor->u.Interrupt.Vector,
                    (PVOID)Descriptor->u.Interrupt.Affinity);
            break;

        case CmResourceTypeMemory:
            DPRINT1("  memory %I64x for %lx, share %x, flags %x\n",
                    Descriptor->u.Memory.Start.QuadPart,
                    Descriptor->u.Memory.Length,
                    Descriptor->ShareDisposition,
                    Descriptor->Flags);
            break;

        case CmResourceTypeDma:
            DPRINT1("  dma channel %lx port %lx\n",
                    Descriptor->u.Dma.Channel,
                    Descriptor->u.Dma.Port);
            break;

        case CmResourceTypeDeviceSpecific:
            DPRINT1("  device specific, %lx bytes\n",
                    Descriptor->u.DeviceSpecificData.DataSize);
            break;

        case CmResourceTypeBusNumber:
            DPRINT1("  bus numbers %lx for %lx\n",
                    Descriptor->u.BusNumber.Start,
                    Descriptor->u.BusNumber.Length);
            break;

        case CmResourceTypeDevicePrivate:
            DPRINT1("  device private %lx %lx %lx\n",
                    Descriptor->u.DevicePrivate.Data[0],
                    Descriptor->u.DevicePrivate.Data[1],
                    Descriptor->u.DevicePrivate.Data[2]);
            break;

        case CmResourceTypeConnection:
            DPRINT1("  connection class %x type %x id %lx:%lx\n",
                    Descriptor->u.Connection.Class,
                    Descriptor->u.Connection.Type,
                    Descriptor->u.Connection.IdHighPart,
                    Descriptor->u.Connection.IdLowPart);
            break;

        default:
            DPRINT1("  resource type %x\n", Descriptor->Type);
            break;
    }
}

VOID
NTAPI
PipDumpCmResourceList(
    _In_ PCM_RESOURCE_LIST CmResource,
    _In_ ULONG DebugLevel)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    ULONG Index;
    ULONG List;

    if (CmResource == NULL)
    {
        DPRINT1("PnP: no resource list\n");
        return;
    }

    DPRINT1("PnP: resource list %p, %lu full descriptors\n",
            CmResource,
            CmResource->Count);

    Full = &CmResource->List[0];

    for (List = 0; List < CmResource->Count; List++)
    {
        DPRINT1(" bus %x number %lx, %lu descriptors\n",
                Full->InterfaceType,
                Full->BusNumber,
                Full->PartialResourceList.Count);

        Descriptor = Full->PartialResourceList.PartialDescriptors;

        for (Index = 0; Index < Full->PartialResourceList.Count; Index++)
        {
            PipDumpCmResourceDescriptor(Descriptor, DebugLevel);
            Descriptor = PipGetNextCmPartialDescriptor(Descriptor);
        }

        Full = (PCM_FULL_RESOURCE_DESCRIPTOR)Descriptor;
    }
}

VOID
NTAPI
PipDumpIoResourceDescriptor(
    _In_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ ULONG DebugLevel)
{
    UNREFERENCED_PARAMETER(DebugLevel);

    switch (Descriptor->Type)
    {
        case CmResourceTypePort:
            DPRINT1("  port %I64x-%I64x len %lx align %lx opt %x\n",
                    Descriptor->u.Port.MinimumAddress.QuadPart,
                    Descriptor->u.Port.MaximumAddress.QuadPart,
                    Descriptor->u.Port.Length,
                    Descriptor->u.Port.Alignment,
                    Descriptor->Option);
            break;

        case CmResourceTypeInterrupt:
            DPRINT1("  interrupt %lx-%lx opt %x\n",
                    Descriptor->u.Interrupt.MinimumVector,
                    Descriptor->u.Interrupt.MaximumVector,
                    Descriptor->Option);
            break;

        case CmResourceTypeMemory:
            DPRINT1("  memory %I64x-%I64x len %lx align %lx opt %x\n",
                    Descriptor->u.Memory.MinimumAddress.QuadPart,
                    Descriptor->u.Memory.MaximumAddress.QuadPart,
                    Descriptor->u.Memory.Length,
                    Descriptor->u.Memory.Alignment,
                    Descriptor->Option);
            break;

        case CmResourceTypeDma:
            DPRINT1("  dma %lx-%lx opt %x\n",
                    Descriptor->u.Dma.MinimumChannel,
                    Descriptor->u.Dma.MaximumChannel,
                    Descriptor->Option);
            break;

        case CmResourceTypeBusNumber:
            DPRINT1("  bus %lx-%lx len %lx opt %x\n",
                    Descriptor->u.BusNumber.MinBusNumber,
                    Descriptor->u.BusNumber.MaxBusNumber,
                    Descriptor->u.BusNumber.Length,
                    Descriptor->Option);
            break;

        default:
            DPRINT1("  requirement type %x opt %x\n",
                    Descriptor->Type,
                    Descriptor->Option);
            break;
    }
}

VOID
NTAPI
PipDumpResourceRequirementsList(
    _In_ PIO_RESOURCE_REQUIREMENTS_LIST IoResource,
    _In_ ULONG DebugLevel)
{
    PIO_RESOURCE_LIST List;
    ULONG Alternative;
    ULONG Index;

    if (IoResource == NULL)
    {
        DPRINT1("PnP: no requirements list\n");
        return;
    }

    DPRINT1("PnP: requirements %p, bus %x number %lx slot %lx, %lu lists\n",
            IoResource,
            IoResource->InterfaceType,
            IoResource->BusNumber,
            IoResource->SlotNumber,
            IoResource->AlternativeLists);

    List = &IoResource->List[0];

    for (Alternative = 0;
         Alternative < IoResource->AlternativeLists;
         Alternative++)
    {
        DPRINT1(" list %lu, %lu descriptors\n", Alternative, List->Count);

        for (Index = 0; Index < List->Count; Index++)
        {
            PipDumpIoResourceDescriptor(&List->Descriptors[Index],
                                        DebugLevel);
        }

        List = (PIO_RESOURCE_LIST)
            ((PUCHAR)&List->Descriptors[0] +
             List->Count * sizeof(IO_RESOURCE_DESCRIPTOR));
    }
}

VOID
NTAPI
PipDumpArbiters(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG DebugLevel)
{
    PPI_RESOURCE_ARBITER_ENTRY Arbiter;
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(DebugLevel);

    if (DeviceNode == NULL) return;

    Entry = DeviceNode->DeviceArbiterList.Flink;
    while (Entry != &DeviceNode->DeviceArbiterList)
    {
        Arbiter = CONTAINING_RECORD(Entry,
                                    PI_RESOURCE_ARBITER_ENTRY,
                                    DeviceArbiterList);
        Entry = Entry->Flink;

        DPRINT1("PnP: arbiter for type %x level %lx state %x\n",
                Arbiter->ResourceType,
                Arbiter->Level,
                Arbiter->State);
    }
}

VOID
NTAPI
PipDumpDeviceNode(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG Flags,
    _In_ ULONG DebugLevel)
{
    if (DeviceNode == NULL) return;

    DPRINT1("PnP: node %p level %lu %wZ\n",
            DeviceNode,
            DeviceNode->Level,
            &DeviceNode->InstancePath);
    DPRINT1("     state %s flags %lx user %lx problem %lx\n",
            PipGetDeviceNodeStateName(DeviceNode->State),
            DeviceNode->Flags,
            DeviceNode->UserFlags,
            DeviceNode->Problem);

    if (Flags & PIP_DUMP_FL_RES_ALLOCATED)
    {
        PipDumpCmResourceList(DeviceNode->ResourceList, DebugLevel);
    }

    if (Flags & PIP_DUMP_FL_RES_TRANSLATED)
    {
        PipDumpCmResourceList(DeviceNode->ResourceListTranslated,
                              DebugLevel);
    }

    if (Flags & PIP_DUMP_FL_RES_REQUIREMENTS)
    {
        PipDumpResourceRequirementsList(DeviceNode->ResourceRequirements,
                                        DebugLevel);
    }
}

VOID
NTAPI
PipDumpDeviceNodes(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ ULONG Flags,
    _In_ ULONG DebugLevel)
{
    PDEVICE_NODE Node = DeviceNode;

    if (Node == NULL) Node = IopRootDeviceNode;
    if (Node == NULL) return;

    PipDumpDeviceNode(Node, Flags, DebugLevel);

    if (!(Flags & PIP_DUMP_FL_ALL_NODES)) return;

    if (Node->Child != NULL)
    {
        PipDumpDeviceNodes(Node->Child, Flags, DebugLevel);
    }

    if (Node != DeviceNode && Node->Sibling != NULL)
    {
        PipDumpDeviceNodes(Node->Sibling, Flags, DebugLevel);
    }
}
