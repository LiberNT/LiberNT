/*
 * PROJECT:     LiberNT Apple AOP Microphone Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry and PortCls adapter registration of the built-in microphones
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
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
AopMicInstallSubdevice(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PCWSTR Name,
    REFGUID PortClassId,
    PUNKNOWN Miniport,
    CAopMicAdapter *Adapter,
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

static NTSTATUS
NTAPI
AopMicStartDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp, PRESOURCELIST ResourceList)
{
    PAOP_MIC_DEVICE_EXTENSION DeviceExtension;
    CAopMicAdapter *Adapter;
    PDEVICE_OBJECT Physical = NULL;
    PUNKNOWN TopologyMiniport = NULL;
    PUNKNOWN WaveMiniport = NULL;
    PUNKNOWN TopologyPort = NULL;
    PUNKNOWN WavePort = NULL;
    NTSTATUS Status;

    DeviceExtension = static_cast<PAOP_MIC_DEVICE_EXTENSION>(DeviceObject->DeviceExtension);
    Adapter = new (NonPagedPool, TAG_AOP_MIC) CAopMicAdapter();
    if (!Adapter)
        return STATUS_INSUFFICIENT_RESOURCES;
    Adapter->AddRef();

    Status = PcGetPhysicalDeviceObject(DeviceObject, &Physical);
    if (NT_SUCCESS(Status))
        Status = Adapter->Initialize(Physical, ResourceList);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = AopMicCreateTopology(&TopologyMiniport, Adapter);
    if (NT_SUCCESS(Status))
        Status = AopMicInstallSubdevice(DeviceObject, Irp, L"Topology", CLSID_PortTopology, TopologyMiniport, Adapter,
                                        ResourceList, &TopologyPort);
    if (NT_SUCCESS(Status))
        Status = AopMicCreateWave(&WaveMiniport, Adapter);
    if (NT_SUCCESS(Status))
        Status = AopMicInstallSubdevice(DeviceObject, Irp, L"Wave", CLSID_PortWaveRT, WaveMiniport, Adapter,
                                        ResourceList, &WavePort);
    if (NT_SUCCESS(Status))
        Status = PcRegisterPhysicalConnection(DeviceObject, TopologyPort, AOP_MIC_TOPOLOGY_WAVE_PIN, WavePort,
                                              AOP_MIC_WAVE_BRIDGE_PIN);
    DbgPrint("appleaopmic: capture endpoint registered: %lx\n", Status);
    if (NT_SUCCESS(Status))
        DeviceExtension->Adapter = Adapter;

Cleanup:
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
        DbgPrint("appleaopmic: start failed: %lx\n", Status);
        Adapter->Shutdown();
        Adapter->Release();
    }
    return Status;
}

static NTSTATUS
NTAPI
AopMicAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
    if (!DriverObject || !PhysicalDeviceObject)
        return STATUS_INVALID_PARAMETER;
    return PcAddAdapterDevice(DriverObject, PhysicalDeviceObject, AopMicStartDevice, 2,
                              sizeof(AOP_MIC_DEVICE_EXTENSION));
}

static NTSTATUS
NTAPI
AopMicPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PAOP_MIC_DEVICE_EXTENSION DeviceExtension;
    CAopMicAdapter *Adapter;
    NTSTATUS Status;

    if (Stack->MinorFunction == IRP_MN_FILTER_RESOURCE_REQUIREMENTS)
    {
        Status = PcForwardIrpSynchronous(DeviceObject, Irp);
        if ((NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED) && NT_SUCCESS(AopMicHwFilterRequirements(Irp)))
            Status = STATUS_SUCCESS;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    if (Stack->MinorFunction == IRP_MN_STOP_DEVICE || Stack->MinorFunction == IRP_MN_SURPRISE_REMOVAL ||
        Stack->MinorFunction == IRP_MN_REMOVE_DEVICE)
    {
        DeviceExtension = static_cast<PAOP_MIC_DEVICE_EXTENSION>(DeviceObject->DeviceExtension);
        Adapter = DeviceExtension->Adapter;
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
AopMicUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

extern "C" DRIVER_INITIALIZE DriverEntry;

extern "C"
NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status = PcInitializeAdapterDriver(DriverObject, RegistryPath, AopMicAddDevice);

    if (NT_SUCCESS(Status))
    {
        DriverObject->DriverUnload = AopMicUnload;
        DriverObject->MajorFunction[IRP_MJ_PNP] = AopMicPnp;
    }
    return Status;
}
