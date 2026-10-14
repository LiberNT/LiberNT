/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hardware configuration tree handed to the kernel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"

#define OSL_MAX_COMPONENTS      16
#define OSL_IDENTIFIER_CHARS    128
#define OSL_AFFINITY_ALL        0xFFFFFFFF
#define OSL_FRAMEBUFFER_VERSION 1
#define OSL_FRAMEBUFFER_REVISION 5

static PCONFIGURATION_COMPONENT_DATA OslComponents[OSL_MAX_COMPONENTS];
static ULONG OslComponentCount;

static CM_FRAMEBUF_DEVICE_DATA OslFrameBuffer;

PCM_FRAMEBUF_DEVICE_DATA FrameBufferData;
ULONG_PTR VramAddress;
ULONG VramSize;
BOOLEAN OslAcpiPresent;

PCONFIGURATION_COMPONENT_DATA
OslCreateComponent(
    _In_opt_ PCONFIGURATION_COMPONENT_DATA Parent,
    _In_ CONFIGURATION_CLASS Class,
    _In_ CONFIGURATION_TYPE Type,
    _In_ IDENTIFIER_FLAG Flags,
    _In_ PCSTR Identifier,
    _In_opt_ PVOID Data,
    _In_ ULONG DataSize)
{
    PCONFIGURATION_COMPONENT_DATA Node, Sibling;
    SIZE_T Length = strlen(Identifier) + 1;
    PCHAR Name;

    if (OslComponentCount >= OSL_MAX_COMPONENTS)
        return NULL;

    Node = MmHeapAllocate(sizeof(*Node));
    Name = MmHeapAllocate(Length);
    if (!Node || !Name)
        return NULL;

    RtlCopyMemory(Name, Identifier, Length);

    Node->ComponentEntry.Class = Class;
    Node->ComponentEntry.Type = Type;
    Node->ComponentEntry.Flags = Flags;
    Node->ComponentEntry.AffinityMask = OSL_AFFINITY_ALL;
    Node->ComponentEntry.ConfigurationDataLength = DataSize;
    Node->ComponentEntry.IdentifierLength = (ULONG)Length;
    Node->ComponentEntry.Identifier = Name;
    Node->ConfigurationData = Data;
    Node->Parent = Parent;

    if (Parent)
    {
        if (!Parent->Child)
        {
            Parent->Child = Node;
        }
        else
        {
            for (Sibling = Parent->Child; Sibling->Sibling; Sibling = Sibling->Sibling)
                ;
            Sibling->Sibling = Node;
        }
    }

    OslComponents[OslComponentCount++] = Node;
    return Node;
}

PCM_PARTIAL_RESOURCE_LIST
OslAllocateResourceList(
    _In_ ULONG Descriptors,
    _In_ ULONG ExtraSize,
    _Out_ PULONG Size)
{
    PCM_PARTIAL_RESOURCE_LIST List;

    *Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors) +
            Descriptors * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR) +
            ExtraSize;
    List = MmHeapAllocate(*Size);
    if (List)
        List->Count = Descriptors;

    return List;
}

static
VOID
OslAddDisplay(
    _In_ PCONFIGURATION_COMPONENT_DATA Bus)
{
    PCM_PARTIAL_RESOURCE_LIST List;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_FRAMEBUF_DEVICE_DATA Data;
    PCONFIGURATION_COMPONENT_DATA Controller;
    CM_MONITOR_DEVICE_DATA Monitor;
    ULONGLONG Base;
    ULONG Size, RefreshRate;
    BOOLEAN HasMonitor;

    if (!FwGetFrameBuffer(&OslFrameBuffer, &Base, &VramSize))
        return;

    VramAddress = (ULONG_PTR)Base;
    FrameBufferData = &OslFrameBuffer;

    OslFrameBuffer.Version = OSL_FRAMEBUFFER_VERSION;
    OslFrameBuffer.Revision = OSL_FRAMEBUFFER_REVISION;

    List = OslAllocateResourceList(2, sizeof(*Data), &Size);
    if (!List)
        return;

    List->Version = 1;
    List->Revision = 2;

    Descriptor = &List->PartialDescriptors[0];
    Descriptor->Type = CmResourceTypeMemory;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Descriptor->u.Memory.Start.QuadPart = VramAddress;
    Descriptor->u.Memory.Length = VramSize;

    Descriptor = &List->PartialDescriptors[1];
    Descriptor->Type = CmResourceTypeDeviceSpecific;
    Descriptor->ShareDisposition = CmResourceShareUndetermined;
    Descriptor->u.DeviceSpecificData.DataSize = sizeof(*Data);

    HasMonitor = OslGetMonitorData(&Monitor, &RefreshRate);
    if (HasMonitor)
        OslFrameBuffer.VideoClock = RefreshRate;

    Data = (PCM_FRAMEBUF_DEVICE_DATA)(Descriptor + 1);
    *Data = OslFrameBuffer;

    Controller = OslCreateComponent(Bus,
                                    ControllerClass,
                                    DisplayController,
                                    Output | ConsoleOut,
                                    "UEFI GOP Framebuffer",
                                    List,
                                    Size);
    if (!Controller || !HasMonitor)
        return;

    List = OslAllocateResourceList(1, sizeof(Monitor), &Size);
    if (!List)
        return;

    List->Version = 1;
    List->Revision = 2;

    Descriptor = &List->PartialDescriptors[0];
    Descriptor->Type = CmResourceTypeDeviceSpecific;
    Descriptor->ShareDisposition = CmResourceShareUndetermined;
    Descriptor->u.DeviceSpecificData.DataSize = sizeof(Monitor);
    RtlCopyMemory(Descriptor + 1, &Monitor, sizeof(Monitor));

    OslCreateComponent(Controller,
                       PeripheralClass,
                       MonitorPeripheral,
                       Output | ConsoleOut,
                       "UEFI EDID Monitor",
                       List,
                       Size);
}

static
VOID
OslAddAcpi(
    _In_ PCONFIGURATION_COMPONENT_DATA System)
{
    PRSDP Rsdp = FwGetAcpiRootPointer();
    PCM_PARTIAL_RESOURCE_LIST List;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PACPI_BIOS_MULTI_NODE Node;
    ULONG Size;

    if (!Rsdp)
        return;

    List = OslAllocateResourceList(1, sizeof(*Node), &Size);
    if (!List)
        return;

    Descriptor = &List->PartialDescriptors[0];
    Descriptor->Type = CmResourceTypeDeviceSpecific;
    Descriptor->ShareDisposition = CmResourceShareUndetermined;
    Descriptor->u.DeviceSpecificData.DataSize = sizeof(*Node);

    Node = (PACPI_BIOS_MULTI_NODE)(Descriptor + 1);
    Node->RsdpAddress.QuadPart = (ULONG_PTR)Rsdp;
    if (Rsdp->Revision > 0)
        Node->RsdtAddress = Rsdp->XsdtAddress;
    else
        Node->RsdtAddress.LowPart = Rsdp->RsdtAddress;

    if (OslCreateComponent(System,
                           AdapterClass,
                           MultiFunctionAdapter,
                           0,
                           "ACPI BIOS",
                           List,
                           Size))
    {
        OslAcpiPresent = TRUE;
    }
}

PCONFIGURATION_COMPONENT_DATA
OslBuildConfigurationTree(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PCONFIGURATION_COMPONENT_DATA System, Bus;
    PCM_PARTIAL_RESOURCE_LIST List;
    CHAR Identifier[OSL_IDENTIFIER_CHARS];
    ULONG Size;

    UNREFERENCED_PARAMETER(LoaderBlock);

    if (!OslGetSystemIdentifier(Identifier, sizeof(Identifier)))
        RtlStringCbCopyA(Identifier, sizeof(Identifier), OslArchSystemIdentifier);

    System = OslCreateComponent(NULL, SystemClass, MaximumType, 0, Identifier, NULL, 0);
    if (!System)
        return NULL;

    List = OslAllocateResourceList(0, 0, &Size);
    if (List)
    {
        List->Version = 1;
        List->Revision = 1;

        Bus = OslCreateComponent(System,
                                 AdapterClass,
                                 MultiFunctionAdapter,
                                 0,
                                 "UEFI Internal",
                                 List,
                                 Size);
        if (Bus)
            OslAddDisplay(Bus);
    }

    OslAddAcpi(System);
    OslArchAddConfiguration(System);
    return System;
}

VOID
OslConvertConfigurationTree(VOID)
{
    PCONFIGURATION_COMPONENT_DATA Node;
    ULONG i;

    for (i = 0; i < OslComponentCount; i++)
    {
        Node = OslComponents[i];
        if (Node->Parent)
            Node->Parent = PaToVa(Node->Parent);
        if (Node->Child)
            Node->Child = PaToVa(Node->Child);
        if (Node->Sibling)
            Node->Sibling = PaToVa(Node->Sibling);
        if (Node->ConfigurationData)
            Node->ConfigurationData = PaToVa(Node->ConfigurationData);
        Node->ComponentEntry.Identifier = PaToVa(Node->ComponentEntry.Identifier);
    }
}
