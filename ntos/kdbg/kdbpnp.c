/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The Plug and Play device tree as the debugger sees it: !devtree and !devnode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#define NDEBUG
#include <debug.h>

#define KDB_MAXIMUM_DEVICE_NODES 4096
#define KDB_MAXIMUM_INDENT 32
#define KDB_MAXIMUM_FULL_LISTS 16
#define KDB_MAXIMUM_ALTERNATIVES 64
#define KDB_MAXIMUM_DESCRIPTORS 256

/* GLOBALS ********************************************************************/

static const PCSTR KdbpDeviceNodeStates[] =
{
    "unspecified",
    "uninitialized",
    "initialized",
    "drivers added",
    "resources assigned",
    "start pending",
    "start completion",
    "start post work",
    "started",
    "query stopped",
    "stopped",
    "restart completion",
    "enumerate pending",
    "enumerate completion",
    "awaiting queued deletion",
    "awaiting queued removal",
    "query removed",
    "remove pending closes",
    "removed",
    "delete pending closes",
    "deleted"
};

C_ASSERT(RTL_NUMBER_OF(KdbpDeviceNodeStates) == MaxDeviceNodeState - DeviceNodeUnspecified);

/* PRIVATE FUNCTIONS **********************************************************/

static
PCSTR
KdbpDeviceNodeStateName(
    _In_ PNP_DEVNODE_STATE State)
{
    if ((State < DeviceNodeUnspecified) || (State >= MaxDeviceNodeState))
        return "unknown";

    return KdbpDeviceNodeStates[State - DeviceNodeUnspecified];
}

static
VOID
KdbpPrintNodeString(
    _In_ PCUNICODE_STRING String)
{
    if ((String->Length == 0) || (String->Buffer == NULL))
    {
        KdbPuts("(none)");
        return;
    }

    KdbpPrintCountedString(String);
}

static
PDEVICE_NODE
KdbpNextDeviceNode(
    _In_ PDEVICE_NODE Address,
    _Inout_ PDEVICE_NODE Node)
{
    ULONG Depth;

    if (Node->Child != NULL)
        return Node->Child;

    for (Depth = 0; Depth < KDB_MAXIMUM_DEVICE_NODES; Depth++)
    {
        if (Address == IopRootDeviceNode)
            return NULL;

        if (Node->Sibling != NULL)
            return Node->Sibling;

        Address = Node->Parent;
        if ((Address == NULL) || !NT_SUCCESS(KdbpSafeReadMemory(Node, Address, sizeof(*Node))))
            return NULL;
    }

    return NULL;
}

static
VOID
KdbpPrintPartialDescriptor(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
    _In_ BOOLEAN Translated)
{
    ULONGLONG Start = 0;
    ULONGLONG Length;

    switch (Descriptor->Type)
    {
        case CmResourceTypePort:
        case CmResourceTypeMemory:
        case CmResourceTypeMemoryLarge:
            Length = RtlCmDecodeMemIoResource(Descriptor, &Start);
            KdbpPrint("%s 0x%I64x length 0x%I64x",
                      (Descriptor->Type == CmResourceTypePort) ? "port" : "memory",
                      Start,
                      Length);
            break;

        case CmResourceTypeInterrupt:
            if (!(Descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            {
                KdbpPrint("%s interrupt vector 0x%lx level %lu affinity 0x%Ix",
                          (Descriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? "latched" : "level",
                          Descriptor->u.Interrupt.Vector,
                          (ULONG)Descriptor->u.Interrupt.Level,
                          Descriptor->u.Interrupt.Affinity);
            }
            else if (Translated)
            {
                KdbpPrint("message interrupt vector 0x%lx level %lu affinity 0x%Ix",
                          Descriptor->u.MessageInterrupt.Translated.Vector,
                          (ULONG)Descriptor->u.MessageInterrupt.Translated.Level,
                          Descriptor->u.MessageInterrupt.Translated.Affinity);
            }
            else
            {
                KdbpPrint("message interrupt, %u messages, vector 0x%lx affinity 0x%Ix",
                          Descriptor->u.MessageInterrupt.Raw.MessageCount,
                          Descriptor->u.MessageInterrupt.Raw.Vector,
                          Descriptor->u.MessageInterrupt.Raw.Affinity);
            }
            break;

        case CmResourceTypeDma:
            KdbpPrint("DMA channel %lu port %lu", Descriptor->u.Dma.Channel, Descriptor->u.Dma.Port);
            break;

        case CmResourceTypeBusNumber:
            KdbpPrint("bus numbers %lu length %lu", Descriptor->u.BusNumber.Start, Descriptor->u.BusNumber.Length);
            break;

        case CmResourceTypeDeviceSpecific:
            KdbpPrint("device-specific data, %lu bytes", Descriptor->u.DeviceSpecificData.DataSize);
            break;

        case CmResourceTypeConnection:
            KdbpPrint("connection class %u type %u id 0x%08lx%08lx",
                      Descriptor->u.Connection.Class,
                      Descriptor->u.Connection.Type,
                      Descriptor->u.Connection.IdHighPart,
                      Descriptor->u.Connection.IdLowPart);
            break;

        default:
            KdbpPrint("type %u data 0x%08lx 0x%08lx 0x%08lx",
                      Descriptor->Type,
                      Descriptor->u.DevicePrivate.Data[0],
                      Descriptor->u.DevicePrivate.Data[1],
                      Descriptor->u.DevicePrivate.Data[2]);
            break;
    }

    KdbpPrint(", share %u flags 0x%04x\n", Descriptor->ShareDisposition, Descriptor->Flags);
}

static
VOID
KdbpPrintResourceList(
    _In_ PCSTR Label,
    _In_opt_ PCM_RESOURCE_LIST Address,
    _In_ BOOLEAN Translated)
{
    CM_FULL_RESOURCE_DESCRIPTOR Full;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    PUCHAR Cursor;
    ULONG FullCount;
    ULONG FullIndex;
    ULONG Index;

    KdbpPrint("  %s:", Label);

    if (Address == NULL)
    {
        KdbPuts(" none\n");
        return;
    }

    if (!NT_SUCCESS(KdbpSafeReadMemory(&FullCount, &Address->Count, sizeof(FullCount))) ||
        (FullCount > KDB_MAXIMUM_FULL_LISTS))
    {
        KdbpPrint(" %p cannot be read\n", Address);
        return;
    }

    KdbpPrint(" %p, %lu lists\n", Address, FullCount);

    Cursor = (PUCHAR)&Address->List[0];
    for (FullIndex = 0; (FullIndex < FullCount) && !KdbpIsOutputAborted(); FullIndex++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Full,
                                           Cursor,
                                           FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR,
                                                        PartialResourceList.PartialDescriptors))) ||
            (Full.PartialResourceList.Count > KDB_MAXIMUM_DESCRIPTORS))
        {
            KdbpPrint("    list %lu at %p cannot be read\n", FullIndex, Cursor);
            return;
        }

        KdbpPrint("    interface %ld bus %lu, %lu descriptors\n",
                  (LONG)Full.InterfaceType,
                  Full.BusNumber,
                  Full.PartialResourceList.Count);

        Cursor += FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList.PartialDescriptors);

        for (Index = 0; (Index < Full.PartialResourceList.Count) && !KdbpIsOutputAborted(); Index++)
        {
            if (!NT_SUCCESS(KdbpSafeReadMemory(&Partial, Cursor, sizeof(Partial))))
            {
                KdbpPrint("      descriptor at %p cannot be read\n", Cursor);
                return;
            }

            KdbPuts("      ");
            KdbpPrintPartialDescriptor(&Partial, Translated);

            Cursor += sizeof(Partial);
            if (Partial.Type == CmResourceTypeDeviceSpecific)
                Cursor += Partial.u.DeviceSpecificData.DataSize;
        }
    }
}

static
VOID
KdbpPrintIoDescriptor(
    _In_ PIO_RESOURCE_DESCRIPTOR Descriptor)
{
    ULONGLONG Alignment = 0;
    ULONGLONG Minimum = 0;
    ULONGLONG Maximum = 0;
    ULONGLONG Length;

    switch (Descriptor->Type)
    {
        case CmResourceTypePort:
        case CmResourceTypeMemory:
        case CmResourceTypeMemoryLarge:
            Length = RtlIoDecodeMemIoResource(Descriptor, &Alignment, &Minimum, &Maximum);
            KdbpPrint("%s length 0x%I64x alignment 0x%I64x in 0x%I64x-0x%I64x",
                      (Descriptor->Type == CmResourceTypePort) ? "port" : "memory",
                      Length,
                      Alignment,
                      Minimum,
                      Maximum);
            break;

        case CmResourceTypeInterrupt:
            KdbpPrint("%s interrupt vectors 0x%lx-0x%lx",
                      (Descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) ? "message" :
                          ((Descriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? "latched" : "level"),
                      Descriptor->u.Interrupt.MinimumVector,
                      Descriptor->u.Interrupt.MaximumVector);
            break;

        case CmResourceTypeDma:
            KdbpPrint("DMA channels %lu-%lu", Descriptor->u.Dma.MinimumChannel, Descriptor->u.Dma.MaximumChannel);
            break;

        case CmResourceTypeBusNumber:
            KdbpPrint("bus numbers %lu-%lu length %lu",
                      Descriptor->u.BusNumber.MinBusNumber,
                      Descriptor->u.BusNumber.MaxBusNumber,
                      Descriptor->u.BusNumber.Length);
            break;

        default:
            KdbpPrint("type %u", Descriptor->Type);
            break;
    }

    KdbpPrint(", option 0x%02x share %u flags 0x%04x\n",
              Descriptor->Option,
              Descriptor->ShareDisposition,
              Descriptor->Flags);
}

static
VOID
KdbpPrintRequirements(
    _In_opt_ PIO_RESOURCE_REQUIREMENTS_LIST Address)
{
    IO_RESOURCE_REQUIREMENTS_LIST Header;
    IO_RESOURCE_LIST List;
    IO_RESOURCE_DESCRIPTOR Descriptor;
    PUCHAR Cursor;
    PUCHAR End;
    ULONG Alternative;
    ULONG Index;

    KdbPuts("  requirements:");

    if (Address == NULL)
    {
        KdbPuts(" none\n");
        return;
    }

    if (!NT_SUCCESS(KdbpSafeReadMemory(&Header, Address, FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List))))
    {
        KdbpPrint(" %p cannot be read\n", Address);
        return;
    }

    KdbpPrint(" %p, interface %ld bus %lu slot 0x%lx, %lu alternatives\n",
              Address,
              (LONG)Header.InterfaceType,
              Header.BusNumber,
              Header.SlotNumber,
              Header.AlternativeLists);

    Cursor = (PUCHAR)&Address->List[0];
    End = (PUCHAR)Address + Header.ListSize;

    for (Alternative = 0;
         (Alternative < Header.AlternativeLists) && (Alternative < KDB_MAXIMUM_ALTERNATIVES) && !KdbpIsOutputAborted();
         Alternative++)
    {
        if ((Cursor + FIELD_OFFSET(IO_RESOURCE_LIST, Descriptors) > End) ||
            !NT_SUCCESS(KdbpSafeReadMemory(&List, Cursor, FIELD_OFFSET(IO_RESOURCE_LIST, Descriptors))) ||
            (List.Count > KDB_MAXIMUM_DESCRIPTORS))
        {
            KdbpPrint("    alternative %lu at %p cannot be read\n", Alternative, Cursor);
            return;
        }

        KdbpPrint("    alternative %lu, %lu descriptors\n", Alternative, List.Count);
        Cursor += FIELD_OFFSET(IO_RESOURCE_LIST, Descriptors);

        for (Index = 0; (Index < List.Count) && !KdbpIsOutputAborted(); Index++, Cursor += sizeof(Descriptor))
        {
            if ((Cursor + sizeof(Descriptor) > End) ||
                !NT_SUCCESS(KdbpSafeReadMemory(&Descriptor, Cursor, sizeof(Descriptor))))
            {
                KdbpPrint("      descriptor at %p cannot be read\n", Cursor);
                return;
            }

            KdbPuts("      ");
            KdbpPrintIoDescriptor(&Descriptor);
        }
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
KdbpCmdDeviceTree(
    ULONG Argc,
    PCHAR Argv[])
{
    DEVICE_NODE Node;
    PDEVICE_NODE Address = IopRootDeviceNode;
    ULONG Count;
    ULONG Indent;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    for (Count = 0; (Address != NULL) && (Count < KDB_MAXIMUM_DEVICE_NODES) && !KdbpIsOutputAborted(); Count++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Node, Address, sizeof(Node))))
        {
            KdbpPrint("The device node at %p cannot be read\n", Address);
            break;
        }

        for (Indent = 0; (Indent < Node.Level) && (Indent < KDB_MAXIMUM_INDENT); Indent++)
            KdbPuts("  ");

        KdbpPrint("%p ", Address);
        KdbpPrintNodeString(&Node.InstancePath);

        if (Node.ServiceName.Length != 0)
        {
            KdbPuts(" [");
            KdbpPrintCountedString(&Node.ServiceName);
            KdbPuts("]");
        }

        KdbpPrint(" %s", KdbpDeviceNodeStateName(Node.State));
        if (Node.Problem != 0)
            KdbpPrint(" problem %lu", Node.Problem);
        KdbPuts("\n");

        Address = KdbpNextDeviceNode(Address, &Node);
    }

    KdbpPrint("%lu device nodes\n", Count);
    return TRUE;
}

BOOLEAN
KdbpCmdDeviceNode(
    ULONG Argc,
    PCHAR Argv[])
{
    DEVICE_NODE Node;
    ULONG_PTR Value = (ULONG_PTR)IopRootDeviceNode;
    PDEVICE_NODE Address;

    if ((Argc > 1) && !KdbpGetAddressExpression(Argv[1], &Value))
        return TRUE;

    Address = (PDEVICE_NODE)Value;
    if (!NT_SUCCESS(KdbpSafeReadMemory(&Node, Address, sizeof(Node))))
    {
        KdbpPrint("The device node at %p cannot be read\n", Address);
        return TRUE;
    }

    KdbpPrint("DEVICE_NODE %p level %lu\n  instance ", Address, Node.Level);
    KdbpPrintNodeString(&Node.InstancePath);
    KdbPuts("\n  service ");
    KdbpPrintNodeString(&Node.ServiceName);
    KdbpPrint("\n  state %s, previous %s, problem %lu, completion 0x%08lx\n",
              KdbpDeviceNodeStateName(Node.State),
              KdbpDeviceNodeStateName(Node.PreviousState),
              Node.Problem,
              Node.CompletionStatus);
    KdbpPrint("  flags 0x%08lx, user flags 0x%08lx, capabilities 0x%08lx\n",
              Node.Flags,
              Node.UserFlags,
              Node.CapabilityFlags);
    KdbpPrint("  PDO %p, parent %p, child %p, sibling %p\n",
              Node.PhysicalDeviceObject,
              Node.Parent,
              Node.Child,
              Node.Sibling);
    KdbpPrint("  interface %ld bus %lu, children's interface %ld bus %lu\n",
              (LONG)Node.InterfaceType,
              Node.BusNumber,
              (LONG)Node.ChildInterfaceType,
              Node.ChildBusNumber);

    KdbpPrintResourceList("boot resources", Node.BootResources, FALSE);
    KdbpPrintResourceList("raw resources", Node.ResourceList, FALSE);
    KdbpPrintResourceList("translated resources", Node.ResourceListTranslated, TRUE);
    KdbpPrintRequirements(Node.ResourceRequirements);

    return TRUE;
}

/* EOF */
