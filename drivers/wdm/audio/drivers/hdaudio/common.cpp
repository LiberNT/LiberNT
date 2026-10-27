/*
 * PROJECT:         ReactOS HDAudio Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Common adapter
 * COPYRIGHT:       Copyright 2025 Johannes Anderwald <johannes.anderwald@reactos.org>
 *                  Copyright 2025-2026 Oleg Dubinskiy <oleg.dubinskiy@reactos.org>
 */

#include "private.h"

#define NDEBUG
#include <debug.h>

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag)
{
    PVOID P = ExAllocatePoolWithTag(PoolType, Size, Tag);
    if (P)
        RtlZeroMemory(P, Size);
    return P;
}

void __cdecl
operator delete(PVOID ptr)
{
    if (ptr)
        ExFreePool(ptr);
}

void __cdecl
operator delete(PVOID ptr, UINT_PTR Unused)
{
    if (ptr)
        ExFreePool(ptr);
}

NTSTATUS
NTAPI
CAdapterCommon::GetInterface(PHDAUDIO_BUS_INTERFACE_V2 iface)
{
    RtlCopyMemory(iface, &Interface, sizeof(HDAUDIO_BUS_INTERFACE_V2));
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAdapterCommon::QueryInterface(IN REFIID refiid, OUT PVOID *Output)
{
    UNICODE_STRING GuidString;

    if (IsEqualGUIDAligned(refiid, IID_IAdapterPowerManagement) ||
        IsEqualGUIDAligned(refiid, IID_IPinCount) ||
        IsEqualGUIDAligned(refiid, IID_IUnknown))
    {
        *Output = PVOID(PADAPTERPOWERMANAGEMENT(this));
        PUNKNOWN(*Output)->AddRef();
        return STATUS_SUCCESS;
    }

    if (RtlStringFromGUID(refiid, &GuidString) == STATUS_SUCCESS)
    {
        DPRINT("CAdapterCommon::QueryInterface no interface!!! iface %S\n", GuidString.Buffer);
        RtlFreeUnicodeString(&GuidString);
    }
    return STATUS_UNSUCCESSFUL;
}

//----------------------------------------------------------------------------

VOID NTAPI
CAdapterCommon::PowerChangeState(IN POWER_STATE NewState)
{
    ULONG Response;

    if (NewState.DeviceState < PowerDeviceD0 || NewState.DeviceState > PowerDeviceD3)
        return;

    if (NewState.DeviceState == m_PowerState)
        return;

    if (!NT_SUCCESS(TransferVerb(((ULONG)m_CodecAddress << 28) | ((ULONG)m_FunctionGroupStartNode << 20) |
                                     (AC_VERB_SET_POWER_STATE << 8) | (NewState.DeviceState - PowerDeviceD0),
                                 &Response)))
    {
        return;
    }

    m_PowerState = NewState.DeviceState;
}

NTSTATUS
NTAPI
CAdapterCommon::QueryDeviceCapabilities(IN PDEVICE_CAPABILITIES PowerDeviceCaps)
{
    UNREFERENCED_PARAMETER(PowerDeviceCaps);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAdapterCommon::QueryPowerChangeState(IN POWER_STATE NewStateQuery)
{
    if (NewStateQuery.DeviceState < PowerDeviceD0 || NewStateQuery.DeviceState > PowerDeviceD3)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------

NTSTATUS
NTAPI
CAdapterCommon::AcquireBusInterface(IN PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS Status;
    PIRP Irp;
    PIO_STACK_LOCATION Stack;

    DPRINT("AcquireBusInterface\n");
    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_HDAUDIO_BUS_INTERFACE_V2;
    Stack->Parameters.QueryInterface.Size = sizeof(HDAUDIO_BUS_INTERFACE_V2);
    Stack->Parameters.QueryInterface.Version = 0x0100;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)&Interface;
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Status = PcForwardIrpSynchronous(DeviceObject, Irp);
    IoFreeIrp(Irp);
    return Status;
}

VOID NTAPI
CAdapterCommon::GetResourceInformation()
{
    Interface.GetResourceInformation(Interface.Context, &m_CodecAddress, &m_FunctionGroupStartNode);
}

NTSTATUS
NTAPI
CAdapterCommon::GetVendorRevisionId()
{
    HDAUDIO_CODEC_TRANSFER Verbs[2];
    UINT32 cmdTmpl = (m_CodecAddress << 28) | (AC_NODE_ROOT << 20) | (AC_VERB_PARAMETERS << 8);
    NTSTATUS Status;

    Verbs[0].Output.Command = cmdTmpl | AC_PAR_VENDOR_ID;
    Verbs[1].Output.Command = cmdTmpl | AC_PAR_REV_ID;

    Status = Interface.TransferCodecVerbs(Interface.Context, 2, Verbs, NULL, NULL);
    if (NT_SUCCESS(Status))
    {
        VendorId = (Verbs[0].Input.Response >> 16) & 0xFFFF;
        RevisionId = (Verbs[1].Input.Response >> 8) & 0xFFFF;
    }
    return Status;
}

NTSTATUS
NTAPI
CAdapterCommon::ReadRegistryKey(
    IN PDEVICE_OBJECT DeviceObject,
    IN LPCWSTR KeyName,
    IN ULONG BufferLength,
    OUT PREGISTRYKEY *ParentKey,
    OUT PULONG Type,
    OUT PVOID Value)
{
    NTSTATUS Status;
    PREGISTRYKEY RegKey = NULL, SubKey = NULL;
    UNICODE_STRING Name, SubKeyNameString;
    ULONG Index, ResultLength;
    PKEY_BASIC_INFORMATION BasicInformation;
    PKEY_VALUE_PARTIAL_INFORMATION PartialInformation = NULL;
    LPWSTR SubKeyName = NULL;

    // construct registry key
    Status = PcNewRegistryKey(&RegKey, NULL, DriverRegistryKey, GENERIC_READ, DeviceObject, NULL, NULL, 0, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PcNewRegistryKey failed with %x\n", Status);
        return Status;
    }

    // init key
    RtlInitUnicodeString(&Name, KeyName);

    Index = 0;
    do
    {
        SubKey = NULL;
        SubKeyName = NULL;
        PartialInformation = NULL;

        Status = RegKey->EnumerateKey(Index, KeyBasicInformation, NULL, 0, &ResultLength);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
        {
            RegKey->Release();
            return Status;
        }
        BasicInformation = (PKEY_BASIC_INFORMATION)ExAllocatePoolZero(NonPagedPool, ResultLength, TAG_HDAUDIO);
        if (!BasicInformation)
        {
            RegKey->Release();
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Status = RegKey->EnumerateKey(Index, KeyBasicInformation, (PVOID)BasicInformation, ResultLength, &ResultLength);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: EnumerateKey failed with %x\n", Status);
            RegKey->Release();
            ExFreePool(BasicInformation);
            return Status;
        }
        SubKeyName =
            (LPWSTR)ExAllocatePoolZero(PagedPool, (BasicInformation->NameLength + 1) * sizeof(WCHAR), TAG_HDAUDIO);
        if (!SubKeyName)
        {
            DPRINT1("HDAUDIO: no resources\n");
            RegKey->Release();
            ExFreePool(BasicInformation);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(SubKeyName, BasicInformation->Name, BasicInformation->NameLength);
        RtlInitUnicodeString(&SubKeyNameString, SubKeyName);
        Status = RegKey->NewSubKey(&SubKey, NULL, GENERIC_READ, &SubKeyNameString, 0, NULL);
        ExFreePool(BasicInformation);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: NewSubKey failed with %x\n", Status);
            RegKey->Release();
            ExFreePool(SubKeyName);
            return Status;
        }
        Status = SubKey->QueryValueKey(&Name, KeyValuePartialInformation, NULL, 0, &ResultLength);
        DPRINT("HDAUDIO: QueryValueKey Status %x\n", Status);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            PartialInformation =
                (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolZero(PagedPool, ResultLength, TAG_HDAUDIO);
            if (!PartialInformation)
            {
                SubKey->Release();
                ExFreePool(SubKeyName);
                RegKey->Release();
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            Status = SubKey->QueryValueKey(
                &Name, KeyValuePartialInformation, PartialInformation, ResultLength, &ResultLength);
            if (NT_SUCCESS(Status) && PartialInformation->DataLength == BufferLength)
            {
                *Type = PartialInformation->Type;
                *ParentKey = SubKey;
                RtlCopyMemory(Value, PartialInformation->Data, PartialInformation->DataLength);
                ExFreePool(PartialInformation);
                ExFreePool(SubKeyName);
                RegKey->Release();
                return STATUS_SUCCESS;
            }
            ExFreePool(PartialInformation);
        }
        SubKey->Release();
        ExFreePool(SubKeyName);
        Index++;
    } while (TRUE);
    RegKey->Release();
    return STATUS_NOT_FOUND;
}

NTSTATUS
NTAPI
CAdapterCommon::TransferInitVerbs(IN PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS Status;
    ULONG NumVerbs;
    ULONG Index;
    WCHAR Buffer[64];
    ULONG Type, ResultLength;
    PREGISTRYKEY Key;
    UNICODE_STRING KeyName;

    // get number of verbs
    Status = ReadRegistryKey(DeviceObject, L"NumVerbs", sizeof(ULONG), &Key, &Type, &NumVerbs);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("HDAUDIO: Warning failed to read NumVerbs key with %x\n", Status);
        return STATUS_SUCCESS;
    }
    for (Index = 0; Index < NumVerbs; Index++)
    {
        RtlStringCchPrintfW(Buffer, RTL_NUMBER_OF(Buffer), L"%04d", Index);
        RtlInitUnicodeString(&KeyName, Buffer);

        // read key
        Status = Key->QueryValueKey(&KeyName, KeyValuePartialInformation, NULL, 0, &ResultLength);
        DPRINT("Status %x\n", Status);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            PKEY_VALUE_PARTIAL_INFORMATION PartialInformation =
                (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolZero(PagedPool, ResultLength, TAG_HDAUDIO);
            if (!PartialInformation)
            {
                Key->Release();
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            Status = Key->QueryValueKey(
                &KeyName, KeyValuePartialInformation, PartialInformation, ResultLength, &ResultLength);
            if (NT_SUCCESS(Status) && PartialInformation->DataLength == sizeof(ULONG))
            {
                ULONG Verb = 0;
                Verb |= PartialInformation->Data[0] << 0;
                Verb |= PartialInformation->Data[1] << 8;
                Verb |= PartialInformation->Data[2] << 16;
                Verb |= PartialInformation->Data[3] << 24;
                ExFreePool(PartialInformation);
                DPRINT("KeyName %wZ Value %x\n", &KeyName, Verb);
                HDAUDIO_CODEC_TRANSFER Transfer;
                Transfer.Output.Command = Verb | m_CodecAddress << 28;
                Status = Interface.TransferCodecVerbs(Interface.Context, 1, &Transfer, NULL, NULL);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("HDAUDIO TransferInitVerbs Verb %u failed with %x\n", Index, Status);
                }
            }
        }
    }
    Key->Release();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAdapterCommon::TransferVerb(ULONG Verb, PULONG Response)
{
    HDAUDIO_CODEC_TRANSFER Transfer;
    NTSTATUS Status;

    Transfer.Output.Command = Verb;

    Status = Interface.TransferCodecVerbs(Interface.Context, 1, &Transfer, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO TransferVerb Verb %x failed with %x\n", Verb, Status);
        return Status;
    }
    *Response = Transfer.Input.Response;
    return Status;
}

NTSTATUS
NTAPI
CAdapterCommon::ChooseSubdeviceName(UCHAR DefaultDevice, UCHAR Wave, OUT LPWSTR *OutSubdeviceName)
{
    WCHAR Buffer[64];

    LPCWSTR DeviceTypes[] = {
        L"eLineOut",          // 0x00
        L"eSpeaker",          // 0x01
        L"eHeadphone",        // 0x02
        L"eCDIn",             // 0x03
        L"eSpdifOut",         // 0x04
        L"eDigitalOtherOut",  // 0x05
        L"eModemLineSide",    // 0x06
        L"eModemHandsetSide", // 0x7
        L"eLineIn",           // 0x8
        L"eAuxIn",            // 0x9
        L"eMicIn",            // 0xA
        L"eTelephony",        // 0xB
        L"eSpdifIn",          // 0xC
        L"eDigitalOtherIn",   // 0xD
        L"eReserved",         // 0xE
        L"eOther",            // 0xF
        L"eHDMIOut"
    };

    UCHAR Ordinal = ++m_SubdeviceNames[Wave ? 1 : 0][DefaultDevice];

    if (Ordinal > 1)
    {
        RtlStringCchPrintfW(Buffer, RTL_NUMBER_OF(Buffer), L"%s%u%s", DeviceTypes[DefaultDevice], Ordinal, Wave ? L"Wave" : L"Topo");
    }
    else
    {
        RtlStringCchPrintfW(Buffer, RTL_NUMBER_OF(Buffer), L"%s%s", DeviceTypes[DefaultDevice], Wave ? L"Wave" : L"Topo");
    }
    LPWSTR Name = (LPWSTR)ExAllocatePoolZero(NonPagedPool, (wcslen(Buffer) + 1) * sizeof(WCHAR), TAG_HDAUDIO);
    if (!Name)
    {
        // no mem
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(Name, Buffer, wcslen(Buffer) * sizeof(WCHAR));
    *OutSubdeviceName = Name;
    return STATUS_SUCCESS;
}

UCHAR
NTAPI
CAdapterCommon::GetSubdeviceType(IN PVOID Node, IN ULONG PinNode, IN UCHAR DefaultDevice)
{
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;
    PIN_CAPABILITIES PinCaps;

    if (DefaultDevice == 0x5 && NT_SUCCESS(OutNode->GetPinCapabilities(PinNode, &PinCaps)) &&
        (PinCaps.HdmiCapable || PinCaps.DisplayPortCapable))
    {
        return HDAUDIO_SUBDEVICE_HDMI_OUT;
    }
    return DefaultDevice;
}

NTSTATUS
NTAPI
CAdapterCommon::InstallDevice(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN LPWSTR Name,
    IN BOOLEAN bTopology,
    IN REFGUID PortClassId,
    IN PRESOURCELIST ResourceList,
    IN ULONG AssociatedPinsCount,
    IN PULONG AssociatedPinIds,
    IN PVOID FunctionGroupNode,
    IN PPCFILTER_DESCRIPTOR FilterDescription,
    OUT PUNKNOWN *OutPortUnknown)
{
    NTSTATUS Status;
    PPORT Port;
    PMINIPORTTOPOLOGY MiniportTopology;
    PMINIPORTWAVERT MiniportWaveRT;
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)FunctionGroupNode;

    // allocate new port
    Status = PcNewPort(&Port, PortClassId);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: PcNewPort failed with %x\n", Status);
        return Status;
    }

    if (bTopology)
    {
        DPRINT("Topology\n");
        // allocate new topology
        Status =
            HDAUDIO_NewMiniportTopology(&MiniportTopology, AssociatedPinsCount, AssociatedPinIds, OutNode, this, FilterDescription);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: HDAUDIO_NewMiniportTopology failed with %x\n", Status);
            Port->Release();
            return Status;
        }
    }
    else
    {
        DPRINT("WaveRT\n");
        // allocate new miniport
        Status =
            HDAUDIO_NewMiniportWaveRT(&MiniportWaveRT, AssociatedPinsCount, AssociatedPinIds, OutNode, this, FilterDescription);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: HDAUDIO_NewMiniportWaveRT failed with %x\n", Status);
            Port->Release();
            return Status;
        }
    }

    // initialize the port driver
    Status = Port->Init(DeviceObject, Irp, bTopology ? (PUNKNOWN)MiniportTopology : (PUNKNOWN)MiniportWaveRT, this, ResourceList);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: Init failed for Name %S with %x\n", Name, Status);
        Port->Release();
        if (bTopology)
            MiniportTopology->Release();
        else
            MiniportWaveRT->Release();
        return Status;
    }

    // register as subdevice
    Status = PcRegisterSubdevice(DeviceObject, Name, Port);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: PcRegisterSubdevice failed for Name %S with %x\n", Name, Status);
        Port->Release();
        if (bTopology)
            MiniportTopology->Release();
        else
            MiniportWaveRT->Release();
        return Status;
    }

    // query output port
    if (OutPortUnknown)
    {
        Status = Port->QueryInterface(IID_IUnknown,
                                      (PVOID *)OutPortUnknown);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: QueryInterface failed for Name %S with %x\n", Name, Status);
            Port->Release();
            if (bTopology)
                MiniportTopology->Release();
            else
                MiniportWaveRT->Release();
            return Status;
        }
    }

    Port->Release();
    if (bTopology)
        MiniportTopology->Release();
    else
        MiniportWaveRT->Release();

    // done
    return Status;
}

NTSTATUS
NTAPI
CAdapterCommon::FindConnectedWidgets(
    IN ULONG NodeType,
    IN ULONG RecursionLevel,
    IN ULONG NodeId,
    IN PVOID Node,
    IN ULONG NodeIndex,
    IN ULONG NodeCount,
    OUT PULONG OutNodesCount,
    OUT PULONG OutNodes,
    IN UCHAR Digital)
{
    NTSTATUS Status;
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;

    // DPRINT(
    //     "RecursionLevel %u NodeId %u Node %p NodeIndex %u NodeCount %u\n",
    //     RecursionLevel, NodeId, Node, NodeIndex, NodeCount);

    PNODE_CONTEXT NodeContext = OutNode->FindNodeId(NodeId);
    if (!NodeContext)
    {
        // invalid pin found
        DPRINT1("HDAUDIO: No Pin Node found with id %u\n", NodeId);
        return STATUS_UNSUCCESSFUL;
    }
    if (NodeContext->Visited)
    {
        // already processed
        *OutNodesCount = 0;
        return STATUS_SUCCESS;
    }

    NodeContext->Visited = TRUE;

    ULONG TotalFoundNodes = 0;
    if (NodeContext->NodeType == NodeType && NodeContext->Digital == Digital)
    {
        // found target node type
        ASSERT(NodeIndex < NodeCount);
        for (ULONG Index = 0; Index < NodeCount; Index++)
        {
            if (OutNodes[Index] == NodeContext->NodeId)
            {
                // already added
                *OutNodesCount = 0;
                return STATUS_SUCCESS;
            }
        }
        TotalFoundNodes += 1;
        OutNodes[NodeIndex] = NodeContext->NodeId;
        NodeIndex++;
        DPRINT("HDAUDIO: Adding Node %u Type %u\n", NodeContext->NodeId, NodeType);
    }

    ULONG ConnectionCount = NodeContext->ConnectionCount;
    for (ULONG CNodeIndex = 0; CNodeIndex < ConnectionCount; CNodeIndex++)
    {
        ULONG ConnectedNodeId = NodeContext->Connections[CNodeIndex];
        PNODE_CONTEXT ConnectedNode = OutNode->FindNodeId(ConnectedNodeId);
        if (!ConnectedNode || ConnectedNode->Visited)
            continue;

        ULONG FoundNodes = 0;
        Status = FindConnectedWidgets(
            NodeType, RecursionLevel + 1, ConnectedNodeId, Node, NodeIndex, NodeCount, &FoundNodes, OutNodes, Digital);
        if (NT_SUCCESS(Status))
        {
            if (FoundNodes)
            {
                NodeIndex += FoundNodes;
                TotalFoundNodes += FoundNodes;
            }
        }
    }
    *OutNodesCount = TotalFoundNodes;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAdapterCommon::BuildWaveFormat(
    IN PVOID Node,
    IN ULONG NodeCount,
    IN PULONG TargetWidgets,
    IN BOOLEAN Capture)
{
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;
    PKSDATARANGE_AUDIO DataRange;
    PKSDATARANGE *DataRanges;
    PPCPIN_DESCRIPTOR WavePins;

    ULONG MaximumChannels = 0;
    ULONG MinimumBitsPerSample = (ULONG)-1;
    ULONG MaximumBitsPerSample = 0;
    ULONG MinimumSampleFrequency = (ULONG)-1;
    ULONG MaximumSampleFrequency = 0;
    ULONG AudioFormatIndex;
    UCHAR FormatPCMSupported = 0;
    UCHAR FormatFloatSupported = 0;
    UCHAR FormatAC3Supported = 0;
    NTSTATUS Status;

    if (Capture)
    {
        DataRange = m_WaveInDataRange;
        DataRanges = m_WaveInDataRanges;
        WavePins = m_WaveInPins;
    }
    else
    {
        DataRange = m_WaveOutDataRange;
        DataRanges = m_WaveOutDataRanges;
        WavePins = m_WaveOutPins;
    }

    RtlZeroMemory(DataRange,
                  sizeof(KSDATARANGE_AUDIO) * HDAUDIO_MAX_FORMAT_RANGES);
    RtlZeroMemory(DataRanges,
                  sizeof(PKSDATARANGE) * HDAUDIO_MAX_FORMAT_RANGES);
    WavePins[0].KsPinDescriptor.DataRangesCount = 0;

    for (ULONG NodeIndex = 0; NodeIndex < NodeCount; NodeIndex++)
    {
        PNODE_CONTEXT ConnectedNode = OutNode->FindNodeId(TargetWidgets[NodeIndex]);
        ASSERT(ConnectedNode);
        MaximumChannels += ConnectedNode->ChannelCount;

        NODE_PCM_RATES NodePcmRates;
        Status = OutNode->GetSupportedPCMSizeRates(ConnectedNode->NodeId, &NodePcmRates);
        if (NT_SUCCESS(Status))
        {
            if (NodePcmRates.AudioFormatSupported8Bit)
            {
                MinimumBitsPerSample = 8;
                MaximumBitsPerSample = max(8, MaximumBitsPerSample);
            }
            if (NodePcmRates.AudioFormatSupported16Bit)
            {
                MinimumBitsPerSample = min(16, MinimumBitsPerSample);
                MaximumBitsPerSample = max(16, MaximumBitsPerSample);
            }
            if (NodePcmRates.AudioFormatSupported20Bit)
            {
                MinimumBitsPerSample = min(20, MinimumBitsPerSample);
                MaximumBitsPerSample = max(20, MaximumBitsPerSample);
            }
            if (NodePcmRates.AudioFormatSupported24Bit)
            {
                MinimumBitsPerSample = min(24, MinimumBitsPerSample);
                MaximumBitsPerSample = max(24, MaximumBitsPerSample);
            }
            if (NodePcmRates.AudioFormatSupported32Bit)
            {
                MinimumBitsPerSample = min(32, MinimumBitsPerSample);
                MaximumBitsPerSample = max(32, MaximumBitsPerSample);
            }
            if (NodePcmRates.Supported8Khz)
            {
                MinimumSampleFrequency = 8000;
                MaximumSampleFrequency = max(8000, MaximumSampleFrequency);
            }
            if (NodePcmRates.Supported11Khz)
            {
                MinimumSampleFrequency = min(11025, MinimumSampleFrequency);
                MaximumSampleFrequency = max(11025, MaximumSampleFrequency);
            }
            if (NodePcmRates.Supported16Khz)
            {
                MinimumSampleFrequency = min(16000, MinimumSampleFrequency);
                MaximumSampleFrequency = max(16000, MaximumSampleFrequency);
            }
            if (NodePcmRates.Supported22Khz)
            {
                MinimumSampleFrequency = min(22050, MinimumSampleFrequency);
                MaximumSampleFrequency = max(22050, MaximumSampleFrequency);
            }
            if (NodePcmRates.Supported32Khz)
            {
                MinimumSampleFrequency = min(32000, MinimumSampleFrequency);
                MaximumSampleFrequency = max(32000, MaximumSampleFrequency);
            }
            if (NodePcmRates.Supported44Khz)
            {
                MinimumSampleFrequency = min(44100, MinimumSampleFrequency);
                MaximumSampleFrequency = max(44100, MaximumSampleFrequency);
            }
            if (NodePcmRates.Supported48Khz)
            {
                MinimumSampleFrequency = min(48000, MinimumSampleFrequency);
                MaximumSampleFrequency = max(48000, MaximumSampleFrequency);
            }
            if (NodePcmRates.Supported88Khz)
            {
                MinimumSampleFrequency = min(88200, MinimumSampleFrequency);
                MaximumSampleFrequency = max(88200, MaximumSampleFrequency);
            }
            if (NodePcmRates.Supported96Khz)
            {
                MinimumSampleFrequency = min(96000, MinimumSampleFrequency);
                MaximumSampleFrequency = max(96000, MaximumSampleFrequency);
            }
            if (NodePcmRates.Supported176Khz)
            {
                MinimumSampleFrequency = min(176400, MinimumSampleFrequency);
                MaximumSampleFrequency = max(176400, MaximumSampleFrequency);
            }
            if (NodePcmRates.Supported192Khz)
            {
                MinimumSampleFrequency = min(192000, MinimumSampleFrequency);
                MaximumSampleFrequency = max(192000, MaximumSampleFrequency);
            }
            if (NodePcmRates.Supported384Khz)
            {
                MinimumSampleFrequency = min(384000, MinimumSampleFrequency);
                MaximumSampleFrequency = max(384000, MaximumSampleFrequency);
            }

            if (NodePcmRates.PCMFormatSupported)
            {
                FormatPCMSupported = TRUE;
            }
            if (NodePcmRates.Float32FormatSupported)
            {
                FormatFloatSupported = TRUE;
            }

            if (NodePcmRates.AC3FormatSupported)
            {
                FormatAC3Supported = TRUE;
            }
        }
        else
        {
            DPRINT1("Failed to get supported pcm rates for node %u Status %x\n", TargetWidgets[NodeIndex], Status);
        }
    }

    ULONG NodeType = (ULONG)-1;
    ULONG Digital = 0xFF;
    for (ULONG Index = 0; Index < NodeCount; Index++)
    {
        DPRINT("I/O NodeIndex %u\n", TargetWidgets[Index]);
        PNODE_CONTEXT Node = OutNode->FindNodeId(TargetWidgets[Index]);
        ASSERT(Node);
        if (Node)
        {
            if (!Index)
            {
                NodeType = Node->NodeType;
                Digital = Node->Digital;
            }
            else
            {
                // node type should be same in the collection
                ASSERT(NodeType == Node->NodeType);
                //ASSERT(Digital == Node->Digital);
            }
        }
    }

    DPRINT("Digital %x\n", Digital);
    DPRINT("NodeType %u\n", NodeType);
    DPRINT("MaximumChannels %u\n", MaximumChannels);
    DPRINT("MinimumBitsPerSample %u\n", MinimumBitsPerSample);
    DPRINT("MaximumBitsPerSample %u\n", MaximumBitsPerSample);
    DPRINT("MinimumSampleFrequency %u\n", MinimumSampleFrequency);
    DPRINT("MaximumSampleFrequency %u\n", MaximumSampleFrequency);

    ULONG FormatsSupported = FormatPCMSupported + FormatFloatSupported + FormatAC3Supported;
    DPRINT("FormatsSupported %u\n", FormatsSupported);
    if (FormatsSupported == 0)
    {
        // ignore pin
        DPRINT1("HDAUDIO: NO Formats supported\n");
        return STATUS_UNSUCCESSFUL;
    }
    if (FormatsSupported > HDAUDIO_MAX_FORMAT_RANGES)
        return STATUS_INTERNAL_ERROR;

    for (AudioFormatIndex = 0; AudioFormatIndex < FormatsSupported; AudioFormatIndex++)
    {
        DataRange[AudioFormatIndex].DataRange.FormatSize = sizeof(KSDATARANGE_AUDIO);
        DataRange[AudioFormatIndex].DataRange.Flags = 0;
        DataRange[AudioFormatIndex].DataRange.SampleSize = 0;
        DataRange[AudioFormatIndex].DataRange.Reserved = 0;

        DataRange[AudioFormatIndex].DataRange.MajorFormat = {KSDATAFORMAT_TYPE_AUDIO};
        DataRange[AudioFormatIndex].DataRange.Specifier = {KSDATAFORMAT_SPECIFIER_WAVEFORMATEX};

        if (FormatPCMSupported)
        {
            DPRINT("PCM\n");
            DataRange[AudioFormatIndex].DataRange.SubFormat = {KSDATAFORMAT_SUBTYPE_PCM};
            FormatPCMSupported = FALSE;
        }
        else if (FormatAC3Supported)
        {
            DPRINT("AC3\n");
            DataRange[AudioFormatIndex].DataRange.SubFormat = {KSDATAFORMAT_SUBTYPE_DOLBY_AC3_SPDIF};
            FormatAC3Supported = FALSE;
        }
        else if (FormatFloatSupported)
        {
            DPRINT("Float\n");
            DataRange[AudioFormatIndex].DataRange.SubFormat = {KSDATAFORMAT_SUBTYPE_IEEE_FLOAT};
            FormatFloatSupported = FALSE;
        }

        DataRange[AudioFormatIndex].MaximumChannels = MaximumChannels;
        DataRange[AudioFormatIndex].MinimumBitsPerSample = MinimumBitsPerSample;
        DataRange[AudioFormatIndex].MaximumBitsPerSample = MaximumBitsPerSample;
        DataRange[AudioFormatIndex].MinimumSampleFrequency = MinimumSampleFrequency;
        DataRange[AudioFormatIndex].MaximumSampleFrequency = MaximumSampleFrequency;

        DataRanges[AudioFormatIndex] = (PKSDATARANGE)&DataRange[AudioFormatIndex];
    }

    WavePins[0].KsPinDescriptor.DataRangesCount = FormatsSupported;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAdapterCommon::BuildWaveInFormat(
    IN ULONG AssociatedPinCount,
    IN PULONG AssociatedPins,
    IN PVOID Node)
{
    return BuildWaveFormat(Node, AssociatedPinCount, AssociatedPins, TRUE);
}

NTSTATUS
NTAPI
CAdapterCommon::BuildWaveOutFormat(
    IN ULONG AssociatedPinCount,
    IN PULONG AssociatedPins,
    IN PVOID Node,
    IN UCHAR Digital)
{
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;
    ULONG NodeIndex;
    NTSTATUS Status;

    ULONG TotalInputOutputWidgets = 0;
    Status = OutNode->GetNodesWithType(0x00, &TotalInputOutputWidgets, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    for (ULONG Index = 0; Index < AssociatedPinCount; Index++)
    {
        DPRINT("AssociatedNode %u\n", AssociatedPins[Index]);
    }
    PULONG TargetWidgets =
        (PULONG)ExAllocatePoolZero(NonPagedPool, sizeof(ULONG) * TotalInputOutputWidgets, TAG_HDAUDIO);

    if (!TargetWidgets)
        return STATUS_INSUFFICIENT_RESOURCES;

    ULONG SubNodeCount = 0;
    for (NodeIndex = 0; NodeIndex < AssociatedPinCount; NodeIndex++)
    {
        ULONG NodesFound = 0;
        Status = FindConnectedWidgets(
            0x00, 0, AssociatedPins[NodeIndex], Node, SubNodeCount, TotalInputOutputWidgets, &NodesFound, TargetWidgets,
            Digital);
        if (NT_SUCCESS(Status))
        {
            SubNodeCount += NodesFound;
        }
    }
    DPRINT("Total I/O Nodes Found %u\n", SubNodeCount);
    NTSTATUS Result;
    if (SubNodeCount == 0)
    {
        Result = BuildWaveFormat(Node, AssociatedPinCount, AssociatedPins, FALSE);
    }
    else
    {
        Result = BuildWaveFormat(Node, SubNodeCount, TargetWidgets, FALSE);
    }
    ExFreePoolWithTag(TargetWidgets, TAG_HDAUDIO);
    return Result;
}

VOID NTAPI
CAdapterCommon::ClearRef(IN ULONG RefValue, IN ULONG NodeCount, IN PULONG Nodes)
{
    for (ULONG Index = 0; Index < NodeCount; Index++)
    {
        if (Nodes[Index] == RefValue)
        {
            Nodes[Index] = (ULONG)-1;
            break;
        }
    }
}

NTSTATUS
NTAPI
CAdapterCommon::Initialize(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PRESOURCELIST ResourceList)
{
    NTSTATUS Status;

    // first acquire HDAUDIO bus interface from hdaudbus
    Status = AcquireBusInterface(DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: Failed to acquire bus interface Status %x\n", Status);
        return Status;
    }
    // get resource information
    GetResourceInformation();
    DPRINT("HDAUDIO: CodecAddress %u FunctionGroupStartNode %u\n", m_CodecAddress, m_FunctionGroupStartNode);

    // get vendor revision
    Status = GetVendorRevisionId();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: Failed to get vendor revision %x\n", Status);
        return Status;
    }

    POWER_STATE PowerState;
    PowerState.DeviceState = PowerDeviceD0;
    PowerChangeState(PowerState);

    // transfer init verbs
    Status = TransferInitVerbs(DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: TransferInitVerbs failed %x\n", Status);
        return Status;
    }

    // now enum widgets
    CFunctionGroupNode *OutNode = nullptr;
    Status = HDAUDIO_EnumFunctionGroupWidgets(m_CodecAddress, m_FunctionGroupStartNode, this, &OutNode);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: HDAUDIO_EnumFunctionGroupWidgets failed %x\n", Status);
        return Status;
    }

    // now get number of input / output widgets
    ULONG OutputNodeCount = 0;
    ULONG InputNodeCount = 0;
    PULONG OutputNodes = 0;
    PULONG InputNodes = 0;
    Status = OutNode->GetNodesWithType(0x00, &OutputNodeCount, &OutputNodes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: GetNodesWithType failed for output node with %x\n", Status);
        delete OutNode;
        return Status;
    }

    Status = OutNode->GetNodesWithType(0x01, &InputNodeCount, &InputNodes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: GetNodesWithType failed for input node with %x\n", Status);
        delete OutNode;
        if (OutputNodes)
            ExFreePool(OutputNodes);
        return Status;
    }
    ULONG PinNodeCount = 0;
    PULONG PinNodes = NULL;
    Status = OutNode->GetNodesWithType(0x04, &PinNodeCount, &PinNodes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: GetNodesWithType failed for pin node with %x\n", Status);
        delete OutNode;
        if (InputNodes)
            ExFreePool(InputNodes);
        if (OutputNodes)
            ExFreePool(OutputNodes);
        return Status;
    }
    DPRINT(
        "HDAUDIO: InputNodeCount %u OutputNodeCount %u PinNodeCount %u\n", InputNodeCount, OutputNodeCount,
        PinNodeCount);

    OutNode->ClearVisitedState();
    DPRINT("ProcessOutputNodes----------------------------------\n");
    Status = ProcessOutputNodes(DeviceObject, Irp, ResourceList, PinNodeCount, PinNodes, (PVOID)OutNode, OutputNodeCount, OutputNodes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: ProcessOutputNodes failed for input node with %x\n", Status);
        delete OutNode;
        if (InputNodes)
            ExFreePool(InputNodes);
        if (OutputNodes)
            ExFreePool(OutputNodes);
        if (PinNodes)
            ExFreePool(PinNodes);
        return Status;
    }

    if (PinNodes)
        ExFreePool(PinNodes);
    PinNodes = NULL;

    if (InputNodeCount)
    {
        Status = OutNode->GetNodesWithType(0x04, &PinNodeCount, &PinNodes);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: GetNodesWithType failed for pin node with %x\n", Status);
            delete OutNode;
            if (InputNodes)
                ExFreePool(InputNodes);
            if (OutputNodes)
                ExFreePool(OutputNodes);
            return Status;
        }

        OutNode->ClearVisitedState();
        DPRINT("ProcessInputNodes----------------------------------\n");
        Status = ProcessInputNodes(DeviceObject, Irp, ResourceList, PinNodeCount, PinNodes, (PVOID)OutNode, InputNodeCount, InputNodes);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: ProcessInputNodes failed for input node with %x\n", Status);
            delete OutNode;
            if (InputNodes)
                ExFreePool(InputNodes);
            if (OutputNodes)
                ExFreePool(OutputNodes);
            if (PinNodes)
                ExFreePool(PinNodes);
            return Status;
        }
    }

    if (m_WaveRTOutPortUnknown)
        m_WaveRTOutPortUnknown->Release();
    if (m_WaveRTInPortUnknown)
        m_WaveRTInPortUnknown->Release();
    if (m_TopoOutPortUnknown)
        m_TopoOutPortUnknown->Release();
    if (m_TopoInPortUnknown)
        m_TopoInPortUnknown->Release();

    return Status;
}

BOOLEAN
NTAPI
CAdapterCommon::FindPinConverter(
    IN PVOID Node,
    IN ULONG NodeId,
    IN UCHAR Digital,
    IN ULONG Depth,
    OUT PULONG Converter)
{
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;
    PNODE_CONTEXT NodeContext = OutNode->FindNodeId(NodeId);

    if (!NodeContext || Depth > 8)
        return FALSE;

    for (ULONG Index = 0; Index < NodeContext->ConnectionCount; Index++)
    {
        PNODE_CONTEXT Connected = OutNode->FindNodeId(NodeContext->Connections[Index]);
        BOOLEAN Found = FALSE;

        if (!Connected)
            continue;
        if (Connected->NodeType == 0x00 && Connected->Digital == Digital)
        {
            ULONG Claimed;

            for (Claimed = 0; Claimed < m_ClaimedConverterCount; Claimed++)
            {
                if (m_ClaimedConverters[Claimed] == Connected->NodeId)
                    break;
            }
            if (Claimed == m_ClaimedConverterCount && m_ClaimedConverterCount < RTL_NUMBER_OF(m_ClaimedConverters))
            {
                m_ClaimedConverters[m_ClaimedConverterCount++] = Connected->NodeId;
                *Converter = Connected->NodeId;
                Found = TRUE;
            }
        }
        else if (Connected->NodeType == 0x02 || Connected->NodeType == 0x03)
        {
            Found = FindPinConverter(Node, Connected->NodeId, Digital, Depth + 1, Converter);
        }

        if (Found)
        {
            if (NodeContext->ConnectionCount > 1 && NodeContext->NodeType != 0x02)
                OutNode->SetConnectionSelect(NodeId, (UCHAR)Index);
            return TRUE;
        }
    }
    return FALSE;
}

NTSTATUS
NTAPI
CAdapterCommon::ProcessDacNodes(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PRESOURCELIST ResourceList,
    IN UCHAR DefaultDevice,
    IN ULONG DacNodeCount,
    IN PULONG DacNodes,
    IN PVOID Node)
{
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;
    NTSTATUS Status;

    if (m_WaveRTOutPortUnknown)
    {
        m_WaveRTOutPortUnknown->Release();
        m_WaveRTOutPortUnknown = NULL;
    }

    for (ULONG NodeIndex = 0; NodeIndex < DacNodeCount; NodeIndex++)
    {
        // Set GPIO registers (required by some codecs)
        Status = OutNode->SetGPIO(DacNodes[NodeIndex], 1, 1, 1);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: SetPinWidgetControl failed with %x\n", Status);
        }
    }

    PNODE_CONTEXT NodeContext = OutNode->FindNodeId(DacNodes[0]);
    ASSERT(NodeContext);

    // build wave out format
    Status = BuildWaveOutFormat(DacNodeCount, DacNodes, (PVOID)OutNode, NodeContext->Digital);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BuildWaveOutFormat failed with %x\n", Status);
        return Status;
    }

    // lets build a subdevice name
    LPWSTR SubdeviceWave = NULL;
    Status = ChooseSubdeviceName(DefaultDevice, TRUE, &SubdeviceWave);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: ChooseSubdeviceName failed with %x\n", Status);
        return Status;
    }

    // construct wavert port
    DPRINT1("HDAUDIO: SubdeviceName %S\n", SubdeviceWave);
    Status = InstallDevice(
        DeviceObject, Irp, SubdeviceWave, FALSE, CLSID_PortWaveRT, ResourceList, DacNodeCount, DacNodes, (PVOID)OutNode,
        &m_WaveOutFilterDescription, &m_WaveRTOutPortUnknown);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: InstallDevice failed with %x\n", Status);
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAdapterCommon::ProcessAdcNodes(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PRESOURCELIST ResourceList,
    IN UCHAR DefaultDevice,
    IN ULONG AdcNodeCount,
    IN PULONG AdcNodes,
    IN PVOID Node)
{
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;
    NTSTATUS Status;

    if (m_WaveRTInPortUnknown)
    {
        m_WaveRTInPortUnknown->Release();
        m_WaveRTInPortUnknown = NULL;
    }

    for (ULONG NodeIndex = 0; NodeIndex < AdcNodeCount; NodeIndex++)
    {
        // Set GPIO registers (required by some codecs)
        Status = OutNode->SetGPIO(AdcNodes[NodeIndex], 1, 1, 1);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: SetPinWidgetControl failed with %x\n", Status);
        }
    }

    // build wave in format
    Status = BuildWaveInFormat(AdcNodeCount, AdcNodes, (PVOID)OutNode);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BuildWaveInFormat failed with %x\n", Status);
        return Status;
    }

    // lets build a subdevice name
    LPWSTR SubdeviceWave = NULL;
    Status = ChooseSubdeviceName(DefaultDevice, TRUE, &SubdeviceWave);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: ChooseSubdeviceName failed with %x\n", Status);
        return Status;
    }

    // construct wavert port
    DPRINT1("HDAUDIO: SubdeviceName %S\n", SubdeviceWave);
    Status = InstallDevice(
        DeviceObject, Irp, SubdeviceWave, FALSE, CLSID_PortWaveRT, ResourceList, AdcNodeCount, AdcNodes, (PVOID)OutNode,
        &m_WaveInFilterDescription, &m_WaveRTInPortUnknown);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HDAUDIO: InstallDevice failed with %x\n", Status);
        return Status;
    }

    return STATUS_SUCCESS;
}

VOID NTAPI
HDAUDIO_UnsolicitedResponseCallback(HDAUDIO_CODEC_RESPONSE Param, PVOID Context)
{
    UNIMPLEMENTED;
}

NTSTATUS
NTAPI
CAdapterCommon::ProcessOutputNodes(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PRESOURCELIST ResourceList,
    IN ULONG PinNodeCount,
    IN PULONG PinNodes,
    IN PVOID Node,
    IN ULONG OutputNodeCount,
    IN PULONG OutputNodes)
{
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;
    NTSTATUS Status;

    for (ULONG NodeIndex = 0; NodeIndex < PinNodeCount; NodeIndex++)
    {
        if (PinNodes[NodeIndex] == (ULONG)-1)
        {
            // already handled by association
            DPRINT("PinNode %u already handled\n", PinNodes[NodeIndex]);
            continue;
        }
        PNODE_CONTEXT NodeContext = OutNode->FindNodeId(PinNodes[NodeIndex]);
        if (NodeContext && NodeContext->ConnectionCount)
        {
            for (ULONG CIndex = 0; CIndex < NodeContext->ConnectionCount; CIndex++)
            {
                DPRINT(
                    "HDAUDIO PinNode %u TotalConnectionCount %u connected to %u\n", PinNodes[NodeIndex],
                    NodeContext->ConnectionCount, NodeContext->Connections[CIndex]);
            }
        }
        else
        {
            // pin node not connected to anything
            // ignore
            DPRINT("PinNode %u not connected\n", PinNodes[NodeIndex]);
            continue;
        }

        // Enable amplifier and in/out paths (required by some codecs)
        {
            PIN_CONFIGURATION_DEFAULT WidgetConfig;
            PIN_CAPABILITIES WidgetCaps;
            if (NT_SUCCESS(OutNode->GetPinConfigurationDefault(PinNodes[NodeIndex], &WidgetConfig)) &&
                NT_SUCCESS(OutNode->GetPinCapabilities(PinNodes[NodeIndex], &WidgetCaps)) &&
                WidgetCaps.OutputCapable && WidgetConfig.DefaultDevice < 0x8)
            {
                Status = OutNode->SetPinWidgetControl(PinNodes[NodeIndex],
                                                      WidgetCaps.HeadphoneDriveCapable && WidgetConfig.DefaultDevice == 0x2,
                                                      1, 0, 0, 0);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("HDAUDIO: SetPinWidgetControl failed with %x\n", Status);
                }
            }
        }

        // Set GPIO registers (required by some codecs)
        Status = OutNode->SetGPIO(PinNodes[NodeIndex], 1, 1, 1);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: SetPinWidgetControl failed with %x\n", Status);
        }

        PIN_CAPABILITIES PinCaps;
        Status = OutNode->GetPinCapabilities(PinNodes[NodeIndex], &PinCaps);
        ULONG DevicePresent = 0;
        if (NT_SUCCESS(Status))
        {
            DPRINT(
                "HDAUDIO: PinNode %u EAPD % x Input %x Output %x PresenceDetectCapable %x\n", PinNodes[NodeIndex],
                PinCaps.EAPDCapable, PinCaps.InputCapable, PinCaps.OutputCapable, PinCaps.PresenceDetectCapable);

            if (!PinCaps.OutputCapable)
            {
                DPRINT("HDAUDIO: PinNode %u not outputcapable %x\n", PinNodes[NodeIndex], PinCaps.OutputCapable);
                continue;
            }

            if (PinCaps.PresenceDetectCapable)
            {
                Status = OutNode->GetPinSense(PinNodes[NodeIndex], &DevicePresent);
                if (NT_SUCCESS(Status))
                {
                    DPRINT("HDAUDIO: PinNode %u DevicePresent %x\n", PinNodes[NodeIndex], DevicePresent);
                    if (!DevicePresent)
                    {
                        UCHAR UnsolCapable, NodeType;
                        Status = OutNode->GetFunctionGroupType(PinNodes[NodeIndex], &UnsolCapable, &NodeType);
                        DPRINT("HDAUDIO: GetFunctionGroupType Status %x Unsolicited %x, NodeType %x\n", Status, UnsolCapable, NodeType);
                        if (NT_SUCCESS(Status) && UnsolCapable)
                        {
                            Status = Interface.RegisterEventCallback(
                            Interface.Context, HDAUDIO_UnsolicitedResponseCallback, (PVOID)this, &m_Tags[m_TagIndex]);
                            if (!NT_SUCCESS(Status))
                            {
                                DPRINT1("Warning: RegisterEventCallback failed with %x\n", Status);
                                continue;
                            }

                            // enable Unsolicited Response
                            DPRINT("Enable Unsolicited Response...\n");
                            Status = OutNode->EnableUnsolicitedResponse(PinNodes[NodeIndex], 1, m_Tags[m_TagIndex]);
                            if (!NT_SUCCESS(Status))
                            {
                                DPRINT1("Warning: EnableUnsolicitedResponse failed with %x\n", Status);
                                continue;
                            }
                            m_TagIndex++;
                        }
                    }
                }
            }

            if (PinCaps.EAPDCapable)
            {
                // enable EAPD
                DPRINT("Enable EAPD...\n");
                Status = OutNode->SetEAPDBTL(PinNodes[NodeIndex], 0, 1, 0);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("Warning: SetEAPDBTL failed with %x\n", Status);
                }
            }
        }
        else
        {
            DPRINT1("PinNode %u failed to retrieve pin caps Status %x\n", PinNodes[NodeIndex], Status);
            continue;
        }

        // retrieve pin configuration
        PIN_CONFIGURATION_DEFAULT PinConfiguration;
        Status = OutNode->GetPinConfigurationDefault(PinNodes[NodeIndex], &PinConfiguration);
        if (NT_SUCCESS(Status))
        {
            if (PinConfiguration.PortConnectivity == 0x1)
            {
                // no connection
                DPRINT("Ignoring PinNode %u\n", PinNodes[NodeIndex]);
                ClearRef(PinNodes[NodeIndex], PinNodeCount, PinNodes);
                continue;
            }

            if (PinConfiguration.DefaultDevice > 7)
            {
                // not an output pin
                DPRINT("Ignoring Pin %u DefaultDevice %u\n", PinNodes[NodeIndex], PinConfiguration.DefaultDevice);
                continue;
            }

            // process output nodes
            ULONG AssociatedPinCount = 0;
            PULONG AssociatedPins = NULL;
            ULONG DacNodeCount = 0;
            PULONG DacNodes = (PULONG)ExAllocatePoolZero(NonPagedPool, sizeof(ULONG) * OutputNodeCount, TAG_HDAUDIO);
            if (DacNodes &&
                NT_SUCCESS(OutNode->GetAssociatedPinNodes(PinNodes[NodeIndex], PinConfiguration.DefaultAssociation,
                                                                      NodeContext->Digital, &AssociatedPinCount,
                                                                      &AssociatedPins)))
            {
                for (ULONG Sequence = 0; Sequence < 16 && DacNodeCount < OutputNodeCount; Sequence++)
                {
                    for (ULONG PinIndex = 0; PinIndex < AssociatedPinCount && DacNodeCount < OutputNodeCount; PinIndex++)
                    {
                        PIN_CONFIGURATION_DEFAULT AssociatedConfiguration;
                        ULONG Converter;

                        if (!NT_SUCCESS(OutNode->GetPinConfigurationDefault(AssociatedPins[PinIndex],
                                                                            &AssociatedConfiguration)) ||
                            AssociatedConfiguration.Sequence != Sequence ||
                            AssociatedConfiguration.PortConnectivity == 0x1 ||
                            AssociatedConfiguration.DefaultDevice > 7)
                        {
                            continue;
                        }
                        PIN_CAPABILITIES AssociatedCaps;
                        if (NT_SUCCESS(OutNode->GetPinCapabilities(AssociatedPins[PinIndex], &AssociatedCaps)) &&
                            AssociatedCaps.OutputCapable)
                        {
                            OutNode->SetPinWidgetControl(AssociatedPins[PinIndex],
                                                         AssociatedCaps.HeadphoneDriveCapable &&
                                                             AssociatedConfiguration.DefaultDevice == 0x2,
                                                         1, 0, 0, 0);
                        }
                        if (FindPinConverter((PVOID)OutNode, AssociatedPins[PinIndex], NodeContext->Digital, 0,
                                             &Converter))
                        {
                            DacNodes[DacNodeCount++] = Converter;
                        }
                    }
                }
                if (AssociatedPins)
                    ExFreePoolWithTag(AssociatedPins, TAG_HDAUDIO);
            }
            Status = ProcessDacNodes(DeviceObject, Irp, ResourceList,
                                     GetSubdeviceType((PVOID)OutNode, PinNodes[NodeIndex], PinConfiguration.DefaultDevice),
                                     DacNodeCount ? DacNodeCount : OutputNodeCount,
                                     DacNodeCount ? DacNodes : OutputNodes, (PVOID)OutNode);
            if (DacNodes)
                ExFreePoolWithTag(DacNodes, TAG_HDAUDIO);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Warning: ProcessDacNodes failed %x\n", Status);
            }

            DPRINT(
                "HDAUDIO: PinNode %u PortConnectivity %x DefaultDevice %x DefaultAssociation %x Sequence %x\n",
                PinNodes[NodeIndex], PinConfiguration.PortConnectivity, PinConfiguration.DefaultDevice,
                PinConfiguration.DefaultAssociation, PinConfiguration.Sequence);
            Status = AssociatePins(
                DeviceObject, Irp, ResourceList, PinNodes[NodeIndex], PinConfiguration.DefaultAssociation, (PVOID)OutNode, PinNodeCount, PinNodes,
                NodeContext->Digital);
        }
        else
        {
            DPRINT1("PinNode %u failed to retrieve config defaults Status %x\n", PinNodes[NodeIndex], Status);
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAdapterCommon::IsInputNodeConnectedToPin(
    IN PVOID Node,
    IN UCHAR Digital,
    ULONG InputNodeId,
    ULONG PinNodeId,
    ULONG PinNodeCount,
    OUT PULONG Result)
{
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;

    PULONG ConnectedPins = (PULONG)ExAllocatePoolZero(NonPagedPool, sizeof(ULONG) * PinNodeCount, TAG_HDAUDIO);
    if (!ConnectedPins)
        return STATUS_INSUFFICIENT_RESOURCES;

    OutNode->ClearVisitedState();
    ULONG FoundNodes = 0;
    NTSTATUS Status = FindConnectedWidgets(0x04, 0, InputNodeId, Node, 0, PinNodeCount, &FoundNodes, ConnectedPins, Digital);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(ConnectedPins);
        return Status;
    }
    *Result = 0;
    DPRINT("Searched for %u FoundNodes %u\n", PinNodeId, FoundNodes);
    for (ULONG Index = 0; Index < FoundNodes; Index++)
    {
        if (ConnectedPins[Index] == PinNodeId)
        {
            DPRINT("Found\n");
            *Result = 1;
            break;
        }
    }
    ExFreePool(ConnectedPins);
    return STATUS_SUCCESS;
}

VOID
NTAPI
CAdapterCommon::CollectPinWithType(
    IN ULONG DeviceType,
    IN ULONG PinNodeCount,
    IN PULONG PinIds,
    IN PULONG DefaultDeviceTypeList,
    OUT PULONG ResultListCount,
    OUT PULONG ResultList)
{
    ULONG Count = 0;
    for (ULONG Index = 0; Index < PinNodeCount; Index++)
    {
        if (DefaultDeviceTypeList[Index] == DeviceType)
            Count++;
    }
    *ResultListCount = Count;
    if (ResultList)
    {
        Count = 0;
        for (ULONG Index = 0; Index < PinNodeCount; Index++)
        {
            if (DefaultDeviceTypeList[Index] == DeviceType)
            {
                ResultList[Count] = PinIds[Index];
                Count++;
            }
        }
    }
}

NTSTATUS
NTAPI
CAdapterCommon::ProcessInputNodes(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PRESOURCELIST ResourceList,
    IN ULONG PinNodeCount,
    IN PULONG PinNodes,
    IN PVOID Node,
    IN ULONG InputNodeCount,
    IN PULONG InputNodes)
{
    NTSTATUS Status;
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;

    PULONG FilteredPinNodes = (PULONG)ExAllocatePoolZero(NonPagedPool, sizeof(ULONG) * PinNodeCount, TAG_HDAUDIO);
    if (!FilteredPinNodes)
        return STATUS_INSUFFICIENT_RESOURCES;

    PULONG DefaultDeviceList = (PULONG)ExAllocatePoolZero(NonPagedPool, sizeof(ULONG) * PinNodeCount, TAG_HDAUDIO);
    if (!DefaultDeviceList)
    {
        ExFreePoolWithTag(FilteredPinNodes, TAG_HDAUDIO);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ULONG FilteredPinNodeCount = 0;

    // filter pin nodes which are not input capable or default device is not input
    for (ULONG NodeIndex = 0; NodeIndex < PinNodeCount; NodeIndex++)
    {
        if (PinNodes[NodeIndex] == (ULONG)-1)
        {
            // already handled by association
            DPRINT("PinNode %u already handled\n", PinNodes[NodeIndex]);
            continue;
        }

        // Enable amplifier and in/out paths (required by some codecs)
        {
            PIN_CONFIGURATION_DEFAULT WidgetConfig;
            PIN_CAPABILITIES WidgetCaps;
            if (NT_SUCCESS(OutNode->GetPinConfigurationDefault(PinNodes[NodeIndex], &WidgetConfig)) &&
                NT_SUCCESS(OutNode->GetPinCapabilities(PinNodes[NodeIndex], &WidgetCaps)) &&
                WidgetCaps.InputCapable && WidgetConfig.DefaultDevice >= 0x8)
            {
                Status = OutNode->SetPinWidgetControl(PinNodes[NodeIndex], 0, 0, 1, 0, 0);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("HDAUDIO: SetPinWidgetControl failed with %x\n", Status);
                }
            }
        }

        // Set GPIO registers (required by some codecs)
        Status = OutNode->SetGPIO(PinNodes[NodeIndex], 1, 1, 1);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: SetPinWidgetControl failed with %x\n", Status);
        }

        PIN_CAPABILITIES PinCaps;
        Status = OutNode->GetPinCapabilities(PinNodes[NodeIndex], &PinCaps);
        if (!NT_SUCCESS(Status) || !PinCaps.InputCapable)
        {
            // ignoring
            DPRINT("Ignoring Pin %u\n", PinNodes[NodeIndex]);
            continue;
        }

        if (PinCaps.PresenceDetectCapable)
        {
            ULONG DevicePresent = 0;
            Status = OutNode->GetPinSense(PinNodes[NodeIndex], &DevicePresent);
            if (NT_SUCCESS(Status))
            {
                DPRINT("HDAUDIO: PinNode %u DevicePresent %x\n", PinNodes[NodeIndex], DevicePresent);
                if (!DevicePresent)
                {
                    UCHAR UnsolCapable, NodeType;
                    Status = OutNode->GetFunctionGroupType(PinNodes[NodeIndex], &UnsolCapable, &NodeType);
                    DPRINT("HDAUDIO: GetFunctionGroupType Status %x Unsolicited %x, NodeType %x\n", Status, UnsolCapable, NodeType);
                    if (NT_SUCCESS(Status) && UnsolCapable)
                    {
                        Status = Interface.RegisterEventCallback(
                        Interface.Context, HDAUDIO_UnsolicitedResponseCallback, (PVOID)this, &m_Tags[m_TagIndex]);
                        if (!NT_SUCCESS(Status))
                        {
                            DPRINT1("Warning: RegisterEventCallback failed with %x\n", Status);
                            continue;
                        }

                        // enable Unsolicited Response
                        DPRINT("Enable Unsolicited Response...\n");
                        Status = OutNode->EnableUnsolicitedResponse(PinNodes[NodeIndex], 1, m_Tags[m_TagIndex]);
                        if (!NT_SUCCESS(Status))
                        {
                            DPRINT1("Warning: EnableUnsolicitedResponse failed with %x\n", Status);
                            continue;
                        }
                        m_TagIndex++;
                    }
                }
            }
        }

        if (PinCaps.EAPDCapable)
        {
            // enable EAPD
            DPRINT("Enable EAPD...\n");
            Status = OutNode->SetEAPDBTL(PinNodes[NodeIndex], 0, 1, 0);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Warning: SetEAPDBTL failed with %x\n", Status);
            }
        }

        // retrieve pin configuration
        PIN_CONFIGURATION_DEFAULT PinConfiguration;
        Status = OutNode->GetPinConfigurationDefault(PinNodes[NodeIndex], &PinConfiguration);
        if (!NT_SUCCESS(Status) || PinConfiguration.DefaultDevice < 0x8 || PinConfiguration.PortConnectivity == 0x1)
        {
            // ignoring
            DPRINT("Ignoring Pin %u DefaultDevice %u\n", PinNodes[NodeIndex], PinConfiguration.DefaultDevice);
            continue;
        }
        DPRINT("Adding Input Pin %u\n", PinNodes[NodeIndex]);
        FilteredPinNodes[FilteredPinNodeCount] = PinNodes[NodeIndex];
        DefaultDeviceList[FilteredPinNodeCount] = PinConfiguration.DefaultDevice;
        FilteredPinNodeCount++;
    }
    if (!FilteredPinNodeCount)
    {
        DPRINT1("HDAUDIO: No PinNodes!\n");
        ExFreePool(FilteredPinNodes);
        ExFreePool(DefaultDeviceList);
        return STATUS_SUCCESS;
    }

    for (ULONG PinIndex = 0; PinIndex < FilteredPinNodeCount; PinIndex++)
    {
        ULONG PinId = FilteredPinNodes[PinIndex];
        PNODE_CONTEXT PinContext = OutNode->FindNodeId(PinId);
        ULONG AdcNode = 0;
        BOOLEAN Found = FALSE;

        if (!PinContext)
            continue;

        for (ULONG Pass = 0; Pass < 2 && !Found; Pass++)
        {
            for (ULONG InputIndex = 0; InputIndex < InputNodeCount && !Found; InputIndex++)
            {
                ULONG Result = 0;
                ULONG Claimed;

                for (Claimed = 0; Claimed < m_ClaimedConverterCount; Claimed++)
                {
                    if (m_ClaimedConverters[Claimed] == InputNodes[InputIndex])
                        break;
                }
                if (Claimed != m_ClaimedConverterCount || m_ClaimedConverterCount >= RTL_NUMBER_OF(m_ClaimedConverters))
                    continue;

                if (Pass == 0)
                {
                    PNODE_CONTEXT Candidate = OutNode->FindNodeId(InputNodes[InputIndex]);
                    for (ULONG CIndex = 0; Candidate && CIndex < Candidate->ConnectionCount && !Result; CIndex++)
                        Result = (Candidate->Connections[CIndex] == PinId);
                }
                else
                {
                    Status = IsInputNodeConnectedToPin(OutNode, PinContext->Digital, InputNodes[InputIndex], PinId,
                                                       PinNodeCount, &Result);
                    if (!NT_SUCCESS(Status))
                    {
                        ExFreePool(FilteredPinNodes);
                        ExFreePool(DefaultDeviceList);
                        return Status;
                    }
                }
                if (!Result)
                    continue;

                AdcNode = InputNodes[InputIndex];
                m_ClaimedConverters[m_ClaimedConverterCount++] = AdcNode;
                Found = TRUE;

                PNODE_CONTEXT AdcContext = OutNode->FindNodeId(AdcNode);
                if (AdcContext && AdcContext->ConnectionCount > 1)
                {
                    for (ULONG CIndex = 0; CIndex < AdcContext->ConnectionCount; CIndex++)
                    {
                        ULONG Reached = (AdcContext->Connections[CIndex] == PinId);
                        if (!Reached &&
                            !NT_SUCCESS(IsInputNodeConnectedToPin(OutNode, PinContext->Digital, AdcContext->Connections[CIndex],
                                                                  PinId, PinNodeCount, &Reached)))
                        {
                            Reached = 0;
                        }
                        if (Reached)
                        {
                            OutNode->SetConnectionSelect(AdcNode, (UCHAR)CIndex);
                            break;
                        }
                    }
                }
            }

        }
        if (!Found)
        {
            DPRINT("No input converter for pin %u\n", PinId);
            continue;
        }

        Status = ProcessAdcNodes(DeviceObject, Irp, ResourceList, (UCHAR)DefaultDeviceList[PinIndex], 1, &AdcNode,
                                 (PVOID)OutNode);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Warning: ProcessAdcNodes failed %x\n", Status);
            continue;
        }

        Status = BuildInstallFilter(DeviceObject, Irp, FALSE, OutNode, ResourceList, 1, &PinId, PinContext->Digital);
    }
    ExFreePool(DefaultDeviceList);
    ExFreePool(FilteredPinNodes);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAdapterCommon::BuildInstallFilter(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN ULONG bOutput,
    IN PVOID Node,
    IN PRESOURCELIST ResourceList,
    IN ULONG AssociatedPinCount,
    IN PULONG AssociatedPins,
    IN UCHAR Digital)
{
    NTSTATUS Status = STATUS_SUCCESS;
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;

    UCHAR DefaultDevice = 0;
    UCHAR bSet = FALSE;
    PIN_CONFIGURATION_DEFAULT PinConfiguration;

    for (ULONG Index = 0; Index < AssociatedPinCount; Index++)
    {
        if (AssociatedPins[Index] == (ULONG)-1)
            continue;

        Status = OutNode->GetPinConfigurationDefault(AssociatedPins[Index], &PinConfiguration);
        if (!NT_SUCCESS(Status))
            continue;

        if (bSet)
        {
            //ASSERT(DefaultDevice == PinConfiguration.DefaultDevice);
        }
        else
        {
            if (PinConfiguration.PortConnectivity != 0x1) // pin is connected
            {
                bSet = TRUE;
                DefaultDevice = GetSubdeviceType((PVOID)OutNode, AssociatedPins[Index], PinConfiguration.DefaultDevice);
            }
        }
    }

    if (bSet)
    {
        // lets build a subdevice name
        LPWSTR SubdeviceTopology = NULL;
        Status = ChooseSubdeviceName(DefaultDevice, FALSE, &SubdeviceTopology);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: ChooseSubdeviceName failed with %x\n", Status);
            return Status;
        }

        // construct topology port
        DPRINT1("HDAUDIO: SubdeviceName %S\n", SubdeviceTopology);
        Status = InstallDevice(
            DeviceObject, Irp, SubdeviceTopology, TRUE, CLSID_PortTopology, ResourceList, AssociatedPinCount, AssociatedPins, (PVOID)OutNode,
            bOutput ? TopoOutFilterDescription : TopoInFilterDescription, bOutput ? &m_TopoOutPortUnknown : &m_TopoInPortUnknown);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HDAUDIO: InstallDevice failed with %x\n", Status);
            return Status;
        }

        PUNKNOWN *WavePort = bOutput ? &m_WaveRTOutPortUnknown : &m_WaveRTInPortUnknown;
        PUNKNOWN *TopoPort = bOutput ? &m_TopoOutPortUnknown : &m_TopoInPortUnknown;

        if (*WavePort)
        {
            if (bOutput)
                Status = PcRegisterPhysicalConnection(DeviceObject, *WavePort, 1, *TopoPort, 0);
            else
                Status = PcRegisterPhysicalConnection(DeviceObject, *TopoPort, 0, *WavePort, 1);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("HDAUDIO: PcRegisterPhysicalConnection failed with %x\n", Status);
            }
            (*WavePort)->Release();
            *WavePort = NULL;
        }
        (*TopoPort)->Release();
        *TopoPort = NULL;
    }
    else
    {
        DPRINT1("HDAUDIO: Pin is not connected\n");
    }

    return Status;
}

NTSTATUS
NTAPI
CAdapterCommon::AssociatePins(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PRESOURCELIST ResourceList,
    IN ULONG PinId,
    IN UCHAR DefaultAssociation,
    IN PVOID Node,
    IN ULONG PinNodeCount,
    IN PULONG PinNodes,
    IN UCHAR Digital)
{
    CFunctionGroupNode *OutNode = (CFunctionGroupNode *)Node;
    NTSTATUS Status;
    ULONG PinCount;
    ULONG AssociatedPinCount = 0;
    PULONG AssociatedPins = NULL;

    Status =
        OutNode->GetAssociatedPinNodes(PinId, DefaultAssociation, Digital, &AssociatedPinCount, &AssociatedPins);
    DPRINT("GetPinNodesWithDefaultAssociation Status %x, AssociatedPinCount %u\n", Status, AssociatedPinCount);
    if (NT_SUCCESS(Status))
    {
        Status = BuildInstallFilter(DeviceObject, Irp, TRUE, OutNode, ResourceList, AssociatedPinCount, AssociatedPins, Digital);
        if (NT_SUCCESS(Status))
        {
            for(PinCount = 0; PinCount < AssociatedPinCount; PinCount++)
            {
                ClearRef(AssociatedPins[PinCount], PinNodeCount, PinNodes);
            }
        }
    }
    return Status;
}

NTSTATUS
HDAUDIO_AllocateCommonAdapter(OUT CAdapterCommon **OutAdapter)
{
    CAdapterCommon *This;

    This = new (NonPagedPool, TAG_HDAUDIO) CAdapterCommon(NULL);
    if (!This)
    {
        // out of memory
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // add reference
    This->AddRef();

    // return result
    *OutAdapter = This;

    // done
    return STATUS_SUCCESS;
}

NTSTATUS
HDAUDIO_InitializeCommonAdapter(CAdapterCommon *CommonAdapter, IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PRESOURCELIST ResourceList)
{
    NTSTATUS Status;

    // init device
    Status = CommonAdapter->Initialize(DeviceObject, Irp, ResourceList);

    // done
    return Status;
}
