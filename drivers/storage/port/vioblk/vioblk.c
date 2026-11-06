/*
 * PROJECT:     LiberNT StorPort VirtIO block miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Adapter discovery, interrupts and StorPort entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "vioblk.h"

static
VOID
VblkDrainQueue(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    PSCSI_REQUEST_BLOCK Srb;

    while ((Srb = VblkTakeCompletedRequest(Device)) != NULL)
        VblkCompleteRequest(Device, Srb);
}

static
BOOLEAN
VblkRestartLocked(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    ULONG Index;

    VblkStopDevice(Device);
    for (Index = 0; Index < Device->QueueSize; Index++)
    {
        PSCSI_REQUEST_BLOCK Srb = Device->Requests[Index];

        if (Srb != NULL)
        {
            Device->Requests[Index] = NULL;
            VblkCompleteSrb(Device, Srb, SRB_STATUS_BUS_RESET);
        }
    }
    return VblkStartDevice(Device);
}

static
VOID
VblkConfigurationChanged(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    UCHAR Generation = VblkReadConfigGeneration(Device);
    ULONGLONG Capacity;

    if (StorPortReadRegisterUchar(Device, Device->CommonCfg + VIRTIO_PCI_COMMON_STATUS) & VIRTIO_STATUS_DEVICE_NEEDS_RESET)
    {
        DPRINT1("vioblk: device asked to be reset\n");
        VblkRestartLocked(Device);
        return;
    }

    if (Generation == Device->ConfigGeneration)
        return;
    Device->ConfigGeneration = Generation;

    Capacity = VblkReadCapacity(Device);
    if (Capacity != Device->CapacitySectors)
    {
        DPRINT1("vioblk: capacity changed from %I64u to %I64u sectors\n", Device->CapacitySectors, Capacity);
        Device->CapacitySectors = Capacity;
        VblkUpdateGeometry(Device);
        Device->CapacityChanged = TRUE;
    }
}

static
BOOLEAN
NTAPI
VblkHwInterrupt(_In_ PVOID DeviceExtension)
{
    PVBLK_DEVICE_EXTENSION Device = (PVBLK_DEVICE_EXTENSION)DeviceExtension;
    UCHAR Isr;

    if (!Device->DeviceReady || Device->MessageInterrupts)
        return FALSE;

    Isr = StorPortReadRegisterUchar(Device, Device->IsrStatus);
    if (Isr == 0)
        return FALSE;
    if (Isr & VIRTIO_ISR_QUEUE)
        VblkDrainQueue(Device);
    if (Isr & VIRTIO_ISR_CONFIG)
        VblkConfigurationChanged(Device);
    return TRUE;
}

static
BOOLEAN
NTAPI
VblkHwMSInterrupt(_In_ PVOID DeviceExtension, _In_ ULONG MessageId)
{
    PVBLK_DEVICE_EXTENSION Device = (PVBLK_DEVICE_EXTENSION)DeviceExtension;

    if (!Device->DeviceReady)
        return FALSE;
    if (MessageId == Device->QueueVector)
        VblkDrainQueue(Device);
    if (MessageId == Device->ConfigVector)
        VblkConfigurationChanged(Device);
    return TRUE;
}

static
ULONG
NTAPI
VblkHwFindAdapter(_In_ PVOID DeviceExtension,
                  _In_ PVOID HwContext,
                  _In_ PVOID BusInformation,
                  _In_ PCHAR ArgumentString,
                  _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                  _Out_ PBOOLEAN Again)
{
    PVBLK_DEVICE_EXTENSION Device = (PVBLK_DEVICE_EXTENSION)DeviceExtension;
    STOR_PHYSICAL_ADDRESS Physical;
    PUCHAR Ring;
    ULONG Length;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);

    *Again = FALSE;
    Device->SystemIoBusNumber = ConfigInfo->SystemIoBusNumber;
    Device->SlotNumber = ConfigInfo->SlotNumber;

    if (!VblkMapDevice(Device, ConfigInfo))
        return SP_RETURN_NOT_FOUND;
    if (!VblkProbeDevice(Device))
        return SP_RETURN_ERROR;

    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Master = TRUE;
    if (ConfigInfo->Dma64BitAddresses & SCSI_DMA64_SYSTEM_SUPPORTED)
        ConfigInfo->Dma64BitAddresses = SCSI_DMA64_MINIPORT_FULL64BIT_NO_BOUNDARY_REQ_SUPPORTED;
    ConfigInfo->MaximumTransferLength = Device->MaxTransferLength;
    ConfigInfo->NumberOfPhysicalBreaks = Device->MaxTransferLength / PAGE_SIZE;
    ConfigInfo->AlignmentMask = 0;
    ConfigInfo->CachesData = Device->Flush;
    ConfigInfo->MaximumNumberOfTargets = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->MaxNumberOfIO = Device->MaxRequests;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    ConfigInfo->HwMSInterruptRoutine = VblkHwMSInterrupt;
    ConfigInfo->InterruptSynchronizationMode = InterruptSynchronizeAll;

    Ring = (PUCHAR)StorPortGetUncachedExtension(Device, ConfigInfo, Device->RingBytes);
    if (Ring == NULL)
        return SP_RETURN_ERROR;
    Physical = StorPortGetPhysicalAddress(Device, NULL, Ring, &Length);
    if (Physical.QuadPart == 0 || Length < Device->RingBytes || (Physical.QuadPart & (sizeof(VIRTQ_DESC) - 1)) != 0)
        return SP_RETURN_ERROR;
    VblkSetRingMemory(Device, Ring, (ULONGLONG)Physical.QuadPart);

    return SP_RETURN_FOUND;
}

static
BOOLEAN
NTAPI
VblkHwInitialize(_In_ PVOID DeviceExtension)
{
    PVBLK_DEVICE_EXTENSION Device = (PVBLK_DEVICE_EXTENSION)DeviceExtension;
    MESSAGE_INTERRUPT_INFORMATION Message;
    ULONG Messages;

    for (Messages = 0; Messages < 2; Messages++)
    {
        RtlZeroMemory(&Message, sizeof(Message));
        if (StorPortGetMSIInfo(Device, Messages, &Message) != STOR_STATUS_SUCCESS)
            break;
    }

    Device->MessageInterrupts = Messages != 0;
    Device->ConfigVector = Messages != 0 ? 0 : VIRTIO_MSI_NO_VECTOR;
    Device->QueueVector = Messages > 1 ? 1 : Device->ConfigVector;

    if (!VblkStartDevice(Device))
        return FALSE;

    DPRINT1("vioblk: %I64u blocks of %lu bytes%s, %u-entry queue with %s descriptors, %lu requests of up to %lu bytes, %s interrupts\n",
            Device->Blocks,
            Device->BlockSize,
            Device->ReadOnly ? " (read-only)" : "",
            Device->QueueSize,
            Device->Indirect ? "indirect" : "direct",
            Device->MaxRequests,
            Device->MaxTransferLength,
            Device->MessageInterrupts ? "message" : "line");
    return TRUE;
}

static
BOOLEAN
NTAPI
VblkResetSynchronized(_In_ PVOID DeviceExtension, _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return VblkRestartLocked((PVBLK_DEVICE_EXTENSION)DeviceExtension);
}

static
BOOLEAN
NTAPI
VblkHwResetBus(_In_ PVOID DeviceExtension, _In_ ULONG PathId)
{
    UNREFERENCED_PARAMETER(PathId);
    return StorPortSynchronizeAccess(DeviceExtension, VblkResetSynchronized, NULL);
}

static
SCSI_ADAPTER_CONTROL_STATUS
NTAPI
VblkHwAdapterControl(_In_ PVOID DeviceExtension,
                     _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
                     _In_ PVOID Parameters)
{
    PVBLK_DEVICE_EXTENSION Device = (PVBLK_DEVICE_EXTENSION)DeviceExtension;

    switch (ControlType)
    {
        case ScsiQuerySupportedControlTypes:
        {
            PSCSI_SUPPORTED_CONTROL_TYPE_LIST List = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
            PBOOLEAN Supported = (PBOOLEAN)((PUCHAR)List + FIELD_OFFSET(SCSI_SUPPORTED_CONTROL_TYPE_LIST, SupportedTypeList));

            if (ScsiQuerySupportedControlTypes < List->MaxControlType)
                Supported[ScsiQuerySupportedControlTypes] = TRUE;
            if (ScsiStopAdapter < List->MaxControlType)
                Supported[ScsiStopAdapter] = TRUE;
            if (ScsiRestartAdapter < List->MaxControlType)
                Supported[ScsiRestartAdapter] = TRUE;
            return ScsiAdapterControlSuccess;
        }

        case ScsiStopAdapter:
            VblkStopDevice(Device);
            return ScsiAdapterControlSuccess;

        case ScsiRestartAdapter:
            return VblkStartDevice(Device) ? ScsiAdapterControlSuccess : ScsiAdapterControlUnsuccessful;

        default:
            return ScsiAdapterControlUnsuccessful;
    }
}

ULONG
NTAPI
DriverEntry(_In_ PVOID DriverObject, _In_ PVOID RegistryPath)
{
    HW_INITIALIZATION_DATA InitData;

    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.HwInitializationDataSize = sizeof(InitData);
    InitData.AdapterInterfaceType = PCIBus;
    InitData.HwInitialize = VblkHwInitialize;
    InitData.HwStartIo = VblkHwStartIo;
    InitData.HwInterrupt = VblkHwInterrupt;
    InitData.HwFindAdapter = VblkHwFindAdapter;
    InitData.HwResetBus = VblkHwResetBus;
    InitData.HwAdapterControl = VblkHwAdapterControl;
    InitData.DeviceExtensionSize = sizeof(VBLK_DEVICE_EXTENSION);
    InitData.SrbExtensionSize = sizeof(VBLK_SRB_EXTENSION);
    InitData.NumberOfAccessRanges = PCI_TYPE0_ADDRESSES;
    InitData.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    InitData.NeedPhysicalAddresses = TRUE;
    InitData.TaggedQueuing = TRUE;
    InitData.AutoRequestSense = TRUE;
    InitData.MultipleRequestPerLu = TRUE;

    return StorPortInitialize(DriverObject, RegistryPath, &InitData, NULL);
}
