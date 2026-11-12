/*
 * PROJECT:         LiberNT Apple Silicon Audio Driver
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Driver entry, PortCls adapter registration and resource requirements
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag)
{
    PVOID Allocation = ExAllocatePoolWithTag(PoolType, Size, Tag);
    if (Allocation)
        RtlZeroMemory(Allocation, Size);
    return Allocation;
}

void
__cdecl
operator delete(PVOID Allocation)
{
    if (Allocation)
        ExFreePool(Allocation);
}

void
__cdecl
operator delete(PVOID Allocation, UINT_PTR)
{
    if (Allocation)
        ExFreePool(Allocation);
}

static NTSTATUS
InstallSubdevice(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PCWSTR Name,
    REFGUID PortClassId,
    PUNKNOWN Miniport,
    CAppleAudioAdapter *Adapter,
    PRESOURCELIST ResourceList,
    PUNKNOWN *PortUnknown)
{
    PPORT Port;
    NTSTATUS Status;

    Status = PcNewPort(&Port, PortClassId);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Port->Init(DeviceObject, Irp, Miniport, static_cast<PUNKNOWN>(Adapter), ResourceList);
    if (NT_SUCCESS(Status))
        Status = PcRegisterSubdevice(DeviceObject, const_cast<PWSTR>(Name), Port);
    if (NT_SUCCESS(Status) && PortUnknown)
        Status = Port->QueryInterface(IID_IUnknown, reinterpret_cast<PVOID *>(PortUnknown));

    Port->Release();
    return Status;
}

static PDEVICE_OBJECT
AppleAudioPhysicalDevice(PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT Physical = NULL;

    if (NT_SUCCESS(PcGetPhysicalDeviceObject(DeviceObject, &Physical)))
        return Physical;
    return NULL;
}

static NTSTATUS
NTAPI
AppleAudioStartDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp, PRESOURCELIST ResourceList)
{
    PAPPLE_AUDIO_DEVICE_EXTENSION DeviceExtension;
    CAppleAudioAdapter *Adapter;
    PUNKNOWN TopologyMiniport = NULL;
    PUNKNOWN WaveMiniport = NULL;
    PUNKNOWN TopologyPort = NULL;
    PUNKNOWN WavePort = NULL;
    PUNKNOWN SpeakerTopologyMiniport = NULL;
    PUNKNOWN SpeakerWaveMiniport = NULL;
    PUNKNOWN SpeakerTopologyPort = NULL;
    PUNKNOWN SpeakerWavePort = NULL;
    NTSTATUS Status;

    DeviceExtension = static_cast<PAPPLE_AUDIO_DEVICE_EXTENSION>(DeviceObject->DeviceExtension);
    Adapter = new (NonPagedPool, TAG_APPLE_AUDIO) CAppleAudioAdapter();
    if (!Adapter)
        return STATUS_INSUFFICIENT_RESOURCES;
    Adapter->AddRef();

    Status = Adapter->Initialize(DeviceObject, AppleAudioPhysicalDevice(DeviceObject), ResourceList);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = AppleAudioCreateTopology(&TopologyMiniport, Adapter, APPLE_AUDIO_ENDPOINT_HEADPHONES);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = InstallSubdevice(DeviceObject, Irp, L"Topology", CLSID_PortTopology, TopologyMiniport, Adapter,
                              ResourceList, &TopologyPort);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = AppleAudioCreateWave(&WaveMiniport, Adapter, APPLE_AUDIO_ENDPOINT_HEADPHONES);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = InstallSubdevice(DeviceObject, Irp, L"Wave", CLSID_PortWaveRT, WaveMiniport, Adapter, ResourceList,
                              &WavePort);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = PcRegisterPhysicalConnection(DeviceObject, WavePort, 1, TopologyPort, 0);
    if (NT_SUCCESS(Status))
        Status = PcRegisterPhysicalConnection(DeviceObject, TopologyPort, APPLE_AUDIO_TOPOLOGY_CAPTURE_PIN, WavePort,
                                              APPLE_AUDIO_WAVE_CAPTURE_BRIDGE_PIN);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (Adapter->HasSpeakers())
    {
        Status = AppleAudioCreateTopology(&SpeakerTopologyMiniport, Adapter, APPLE_AUDIO_ENDPOINT_SPEAKERS);
        if (NT_SUCCESS(Status))
            Status = InstallSubdevice(DeviceObject, Irp, L"TopologySpeaker", CLSID_PortTopology, SpeakerTopologyMiniport,
                                      Adapter, ResourceList, &SpeakerTopologyPort);
        if (NT_SUCCESS(Status))
            Status = AppleAudioCreateWave(&SpeakerWaveMiniport, Adapter, APPLE_AUDIO_ENDPOINT_SPEAKERS);
        if (NT_SUCCESS(Status))
            Status = InstallSubdevice(DeviceObject, Irp, L"WaveSpeaker", CLSID_PortWaveRT, SpeakerWaveMiniport, Adapter,
                                      ResourceList, &SpeakerWavePort);
        if (NT_SUCCESS(Status))
            Status = PcRegisterPhysicalConnection(DeviceObject, SpeakerWavePort, 1, SpeakerTopologyPort, 0);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        DbgPrint("appleaudio: speaker endpoint registered\n");
    }

    DeviceExtension->Adapter = Adapter;

Cleanup:
    if (SpeakerWavePort)
        SpeakerWavePort->Release();
    if (SpeakerTopologyPort)
        SpeakerTopologyPort->Release();
    if (SpeakerWaveMiniport)
        SpeakerWaveMiniport->Release();
    if (SpeakerTopologyMiniport)
        SpeakerTopologyMiniport->Release();
    if (WavePort)
        WavePort->Release();
    if (TopologyPort)
        TopologyPort->Release();
    if (WaveMiniport)
        WaveMiniport->Release();
    if (TopologyMiniport)
        TopologyMiniport->Release();
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaudio: start failed 0x%08lx\n", Status);
        Adapter->Shutdown();
        Adapter->Release();
    }
    return Status;
}

static NTSTATUS
NTAPI
AppleAudioAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
    if (!DriverObject || !PhysicalDeviceObject)
        return STATUS_INVALID_PARAMETER;

    return PcAddAdapterDevice(DriverObject, PhysicalDeviceObject, AppleAudioStartDevice, APPLE_AUDIO_MAX_SUBDEVICES,
                              sizeof(APPLE_AUDIO_DEVICE_EXTENSION));
}

static NTSTATUS
AppleAudioFilterRequirements(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PIO_RESOURCE_REQUIREMENTS_LIST Old, New;
    PIO_RESOURCE_LIST List;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    PDEVICE_OBJECT Physical;
    ULONG Gsi, Index, Size;
    BOOLEAN Edge;
    NTSTATUS Status;

    Status = PcForwardIrpSynchronous(DeviceObject, Irp);
    Old = (NT_SUCCESS(Status) && Irp->IoStatus.Information)
              ? reinterpret_cast<PIO_RESOURCE_REQUIREMENTS_LIST>(Irp->IoStatus.Information)
              : Stack->Parameters.FilterResourceRequirements.IoResourceRequirementList;

    Physical = AppleAudioPhysicalDevice(DeviceObject);
    Status = Physical ? AppleAudioHwQueryInterrupt(Physical, &Gsi, &Edge) : STATUS_NO_SUCH_DEVICE;
    if (NT_SUCCESS(Status) && Old && Old->AlternativeLists != 1)
        Status = STATUS_NOT_SUPPORTED;
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaudio: no DMA interrupt for the requirements 0x%08lx\n", Status);
        Irp->IoStatus.Information = reinterpret_cast<ULONG_PTR>(Old);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    if (Old)
    {
        List = &Old->List[0];
        for (Index = 0; Index < List->Count; ++Index)
        {
            if (List->Descriptors[Index].Type == CmResourceTypeInterrupt &&
                List->Descriptors[Index].u.Interrupt.MinimumVector == Gsi)
            {
                Irp->IoStatus.Information = reinterpret_cast<ULONG_PTR>(Old);
                AppleAudioHwAddJackRequirement(Physical, Irp);
                Irp->IoStatus.Status = STATUS_SUCCESS;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_SUCCESS;
            }
        }
    }

    Index = Old ? Old->List[0].Count : 0;
    Size = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) + (Index + 1) * sizeof(IO_RESOURCE_DESCRIPTOR);
    New = static_cast<PIO_RESOURCE_REQUIREMENTS_LIST>(ExAllocatePoolZero(PagedPool, Size, TAG_APPLE_AUDIO));
    if (!New)
    {
        Irp->IoStatus.Information = reinterpret_cast<ULONG_PTR>(Old);
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (Index)
    {
        RtlCopyMemory(New, Old, FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) +
                                Index * sizeof(IO_RESOURCE_DESCRIPTOR));
    }
    else
    {
        New->InterfaceType = Internal;
        New->BusNumber = 0;
        New->List[0].Version = 1;
        New->List[0].Revision = 1;
    }
    New->ListSize = Size;
    New->AlternativeLists = 1;
    New->List[0].Count = Index + 1;
    Descriptor = &New->List[0].Descriptors[Index];
    Descriptor->Type = CmResourceTypeInterrupt;
    Descriptor->ShareDisposition = CmResourceShareShared;
    Descriptor->Flags = Edge ? CM_RESOURCE_INTERRUPT_LATCHED : CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
    Descriptor->u.Interrupt.MinimumVector = Gsi;
    Descriptor->u.Interrupt.MaximumVector = Gsi;
    if (Old)
        ExFreePool(Old);
    DbgPrint("appleaudio: requesting the ADMAC interrupt %lu\n", Gsi);

    Irp->IoStatus.Information = reinterpret_cast<ULONG_PTR>(New);
    AppleAudioHwAddJackRequirement(Physical, Irp);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
AppleAudioPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    if (Stack->MinorFunction == IRP_MN_FILTER_RESOURCE_REQUIREMENTS)
        return AppleAudioFilterRequirements(DeviceObject, Irp);

    if (Stack->MinorFunction == IRP_MN_STOP_DEVICE ||
        Stack->MinorFunction == IRP_MN_SURPRISE_REMOVAL ||
        Stack->MinorFunction == IRP_MN_REMOVE_DEVICE)
    {
        PAPPLE_AUDIO_DEVICE_EXTENSION DeviceExtension =
            static_cast<PAPPLE_AUDIO_DEVICE_EXTENSION>(DeviceObject->DeviceExtension);
        CAppleAudioAdapter *Adapter = DeviceExtension->Adapter;
        if (Adapter)
        {
            DeviceExtension->Adapter = NULL;
            Adapter->Shutdown();
            Adapter->Release();
        }
    }

    return PcDispatchIrp(DeviceObject, Irp);
}

static VOID
NTAPI
AppleAudioUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

extern "C" DRIVER_INITIALIZE DriverEntry;

extern "C"
NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status = PcInitializeAdapterDriver(DriverObject, RegistryPath, AppleAudioAddDevice);
    if (NT_SUCCESS(Status))
    {
        DriverObject->DriverUnload = AppleAudioUnload;
        DriverObject->MajorFunction[IRP_MJ_PNP] = AppleAudioPnp;
    }
    return Status;
}
