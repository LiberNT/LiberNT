/*
 * PROJECT:     LiberNT Storage Drivers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LSI Fusion-MPT SAS StorPort miniport entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "lsi_sas.h"

static
ULONG
LsiLayoutFrames(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PMPI_IOC_FACTS_REPLY Facts)
{
    ULONG Inline;
    ULONG PerSegment;
    ULONG Segments;

    Device->FrameSize = ((ULONG)Facts->RequestFrameSize * sizeof(ULONG)) & ~7UL;
    if (Device->FrameSize < sizeof(MPI_SCSI_IO_REQUEST) + 2 * sizeof(MPI_SGE_SIMPLE64) ||
        Device->FrameSize > PAGE_SIZE)
    {
        return 0;
    }

    Inline = (Device->FrameSize - sizeof(MPI_SCSI_IO_REQUEST)) / sizeof(MPI_SGE_SIMPLE64);
    PerSegment = Device->FrameSize / sizeof(MPI_SGE_SIMPLE64);

    Segments = 0;
    if (LSI_MAX_SGE > Inline)
        Segments = (LSI_MAX_SGE - Inline + PerSegment - 2) / (PerSegment - 1);
    Segments = min(Segments, (ULONG)Facts->MaxChainDepth);

    Device->ChainSegments = Segments;
    Device->MaxSge = min((ULONG)LSI_MAX_SGE, Inline + Segments * (PerSegment - 1));
    return Device->FrameSize * (1 + Segments);
}

static
BOOLEAN
NTAPI
LsiHwInterrupt(
    _In_ PVOID DeviceExtension)
{
    PLSI_DEVICE_EXTENSION Device = (PLSI_DEVICE_EXTENSION)DeviceExtension;
    ULONG Status;

    if (Device->Registers == NULL)
        return FALSE;

    Status = LsiReadRegister(Device, MPI_HOST_INTERRUPT_STATUS_OFFSET);
    if (Status == MAXULONG || !(Status & MPI_HIS_REPLY_MESSAGE_INTERRUPT))
        return FALSE;

    LsiProcessReplies(Device);
    return TRUE;
}

static
BOOLEAN
NTAPI
LsiHwMSInterrupt(
    _In_ PVOID DeviceExtension,
    _In_ ULONG MessageId)
{
    UNREFERENCED_PARAMETER(MessageId);
    return LsiHwInterrupt(DeviceExtension);
}

static
ULONG
NTAPI
LsiHwFindAdapter(
    _In_ PVOID DeviceExtension,
    _In_ PVOID HwContext,
    _In_ PVOID BusInformation,
    _In_ PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _Out_ PBOOLEAN Again)
{
    PLSI_DEVICE_EXTENSION Device = (PLSI_DEVICE_EXTENSION)DeviceExtension;
    MPI_IOC_FACTS_REPLY Facts;
    PACCESS_RANGE AccessRange = NULL;
    STOR_PHYSICAL_ADDRESS Physical;
    PUCHAR Uncached;
    ULONGLONG SensePhysical;
    ULONG SlotStride;
    ULONG SenseOffset;
    ULONG ReplyOffset;
    ULONG Length;
    ULONG Index;
    ULONG Bus;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);

    *Again = FALSE;
    RtlZeroMemory(Device, sizeof(*Device));

    for (Index = 0; Index < ConfigInfo->NumberOfAccessRanges; Index++)
    {
        if ((*ConfigInfo->AccessRanges)[Index].RangeInMemory &&
            (*ConfigInfo->AccessRanges)[Index].RangeLength >= MPI_REPLY_POST_FIFO_OFFSET + sizeof(ULONG))
        {
            AccessRange = &(*ConfigInfo->AccessRanges)[Index];
            break;
        }
    }
    if (AccessRange == NULL)
        return SP_RETURN_NOT_FOUND;

    Device->Registers = (PUCHAR)StorPortGetDeviceBase(Device,
                                                      ConfigInfo->AdapterInterfaceType,
                                                      ConfigInfo->SystemIoBusNumber,
                                                      AccessRange->RangeStart,
                                                      AccessRange->RangeLength,
                                                      FALSE);
    if (Device->Registers == NULL)
        return SP_RETURN_ERROR;

    LsiWriteRegister(Device, MPI_HOST_INTERRUPT_MASK_OFFSET, MPI_HIM_DIM | MPI_HIM_RIM);

    if (!LsiMakeReady(Device))
    {
        DPRINT1("lsi_sas: IOC did not reach the ready state\n");
        return SP_RETURN_ERROR;
    }

    if (!LsiGetIocFacts(Device, &Facts))
    {
        DPRINT1("lsi_sas: IOCFacts failed\n");
        return SP_RETURN_ERROR;
    }

    if ((Facts.MsgVersion >> 8) != (MPI_VERSION_01_05 >> 8))
    {
        DPRINT1("lsi_sas: unsupported MPI version 0x%04x\n", Facts.MsgVersion);
        return SP_RETURN_ERROR;
    }

    SlotStride = LsiLayoutFrames(Device, &Facts);
    Device->SlotCount = min(min((ULONG)Facts.GlobalCredits, (ULONG)Facts.ReplyQueueDepth), LSI_MAX_SLOTS);
    if (SlotStride == 0 || Device->SlotCount < 2)
    {
        DPRINT1("lsi_sas: unusable IOC facts (frame %u words, credits %u, reply depth %u)\n",
                Facts.RequestFrameSize, Facts.GlobalCredits, Facts.ReplyQueueDepth);
        return SP_RETURN_ERROR;
    }

    Device->RequestFrameSize = Facts.RequestFrameSize;
    Device->MaxDevices = Facts.MaxDevices;
    Device->MaxBuses = Facts.MaxBuses;
    Device->ReplyFrameCount = Device->SlotCount;
    Device->TargetCount = min(Facts.MaxDevices ? (ULONG)Facts.MaxDevices : 256UL, (ULONG)LSI_MAX_TARGETS);
    Device->BusCount = Facts.MaxBuses ? min((ULONG)Facts.MaxBuses, (ULONG)SCSI_MAXIMUM_BUSES) : 1;

    SenseOffset = Device->SlotCount * SlotStride;
    ReplyOffset = SenseOffset + Device->SlotCount * LSI_SENSE_STRIDE;
    Device->UncachedSize = ReplyOffset + Device->ReplyFrameCount * LSI_REPLY_FRAME_SIZE;

    ConfigInfo->MaximumTransferLength = (Device->MaxSge - 1) * PAGE_SIZE;
    ConfigInfo->NumberOfPhysicalBreaks = Device->MaxSge - 1;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Master = TRUE;
    ConfigInfo->CachesData = FALSE;
    ConfigInfo->AlignmentMask = 0x3;
    if (ConfigInfo->Dma64BitAddresses & SCSI_DMA64_SYSTEM_SUPPORTED)
        ConfigInfo->Dma64BitAddresses = SCSI_DMA64_MINIPORT_FULL64BIT_SUPPORTED;

    Uncached = (PUCHAR)StorPortGetUncachedExtension(Device, ConfigInfo, Device->UncachedSize);
    if (Uncached == NULL)
    {
        DPRINT1("lsi_sas: no uncached extension of %lu bytes\n", Device->UncachedSize);
        return SP_RETURN_ERROR;
    }
    RtlZeroMemory(Uncached, Device->UncachedSize);

    Physical = StorPortGetPhysicalAddress(Device, NULL, Uncached, &Length);
    if (Physical.QuadPart == 0 || Length < Device->UncachedSize ||
        (Physical.QuadPart >> 32) != ((Physical.QuadPart + Device->UncachedSize - 1) >> 32))
    {
        DPRINT1("lsi_sas: uncached extension 0x%I64x is not usable\n", Physical.QuadPart);
        return SP_RETURN_ERROR;
    }

    Device->Uncached = Uncached;
    Device->UncachedPhysical = (ULONGLONG)Physical.QuadPart;
    Device->FrameHighAddress = (ULONG)(Device->UncachedPhysical >> 32);
    Device->SenseHighAddress = Device->FrameHighAddress;
    Device->ReplyFrames = Uncached + ReplyOffset;
    Device->ReplyFramesPhysical = (ULONG)(Device->UncachedPhysical + ReplyOffset);

    for (Index = 0; Index < Device->SlotCount; Index++)
    {
        Device->Slots[Index].Frame = Uncached + Index * SlotStride;
        Device->Slots[Index].FramePhysical = Device->UncachedPhysical + Index * SlotStride;
        Device->Slots[Index].Sense = Uncached + SenseOffset + Index * LSI_SENSE_STRIDE;
        SensePhysical = Device->UncachedPhysical + SenseOffset + Index * LSI_SENSE_STRIDE;
        Device->Slots[Index].SensePhysical = (ULONG)SensePhysical;
    }

    if (!LsiStartIoc(Device))
        return SP_RETURN_ERROR;

    if (Device->PortType == MPI_PORTFACTS_PORTTYPE_SCSI)
    {
        StorPortSetAdapterBusType(Device, BusTypeScsi);
        if (Device->PortMaxDevices != 0)
            Device->TargetCount = min(Device->TargetCount, (ULONG)Device->PortMaxDevices);
    }

    ConfigInfo->NumberOfBuses = (UCHAR)Device->BusCount;
    ConfigInfo->MaximumNumberOfTargets = (UCHAR)Device->TargetCount;
    ConfigInfo->MaximumNumberOfLogicalUnits = SCSI_MAXIMUM_LOGICAL_UNITS;
    for (Bus = 0; Bus < Device->BusCount; Bus++)
        ConfigInfo->InitiatorBusId[Bus] = (CCHAR)Device->PortSCSIID;
    ConfigInfo->MaxNumberOfIO = Device->SlotCount - 1;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    ConfigInfo->HwMSInterruptRoutine = LsiHwMSInterrupt;
    ConfigInfo->InterruptSynchronizationMode = InterruptSynchronizeAll;

    DPRINT1("lsi_sas: IOC ready, MPI 0x%04x, firmware 0x%08lx, %lu slots, frame %lu, %lu SGEs, %lu targets\n",
            Facts.MsgVersion, Facts.FWVersion, Device->SlotCount, Device->FrameSize,
            Device->MaxSge, Device->TargetCount);

    return SP_RETURN_FOUND;
}

static
VOID
NTAPI
LsiHwTimer(
    _In_ PVOID DeviceExtension)
{
    PLSI_DEVICE_EXTENSION Device = (PLSI_DEVICE_EXTENSION)DeviceExtension;
    STOR_LOCK_HANDLE LockHandle;
    BOOLEAN Rearm = TRUE;
    ULONG Doorbell;

    StorPortAcquireSpinLock(Device, InterruptLock, NULL, &LockHandle);
    InterlockedExchange(&Device->WatchdogBusy, 1);
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Device->WatchdogActive, 0, 0) != 0)
    {
        Doorbell = LsiReadRegister(Device, MPI_DOORBELL_OFFSET);
        if (Doorbell != MAXULONG && (Doorbell & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_FAULT)
        {
            DPRINT1("lsi_sas: IOC fault code 0x%04lx, reinitializing the IOC\n", Doorbell & MPI_DOORBELL_DATA_MASK);
            if (!LsiReinitializeIoc(Device))
            {
                DPRINT1("lsi_sas: IOC recovery failed, doorbell 0x%08lx\n", LsiReadRegister(Device, MPI_DOORBELL_OFFSET));
                Rearm = FALSE;
            }
        }
        if (Rearm)
            StorPortNotification(RequestTimerCall, Device, LsiHwTimer, LSI_WATCHDOG_INTERVAL_US);
    }
    InterlockedExchange(&Device->WatchdogBusy, 0);
    StorPortReleaseSpinLock(Device, &LockHandle);
}

static
VOID
LsiStartWatchdog(
    _In_ PLSI_DEVICE_EXTENSION Device)
{
    InterlockedExchange(&Device->WatchdogActive, 1);
    StorPortNotification(RequestTimerCall, Device, LsiHwTimer, LSI_WATCHDOG_INTERVAL_US);
}

static
VOID
LsiStopWatchdog(
    _In_ PLSI_DEVICE_EXTENSION Device)
{
    InterlockedExchange(&Device->WatchdogActive, 0);
    KeMemoryBarrier();
    while (InterlockedCompareExchange(&Device->WatchdogBusy, 0, 0) != 0)
        StorPortStallExecution(10);
}

static
BOOLEAN
NTAPI
LsiHwInitialize(
    _In_ PVOID DeviceExtension)
{
    PLSI_DEVICE_EXTENSION Device = (PLSI_DEVICE_EXTENSION)DeviceExtension;

    if (!Device->Operational)
        return FALSE;

    Device->InterruptsEnabled = TRUE;
    LsiWriteRegister(Device, MPI_HOST_INTERRUPT_MASK_OFFSET, MPI_HIM_DIM);
    LsiStartWatchdog(Device);
    return TRUE;
}

static
BOOLEAN
NTAPI
LsiHwStartIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PLSI_DEVICE_EXTENSION Device = (PLSI_DEVICE_EXTENSION)DeviceExtension;

    switch (Srb->Function)
    {
        case SRB_FUNCTION_EXECUTE_SCSI:
            LsiStartScsi(Device, Srb);
            break;

        case SRB_FUNCTION_ABORT_COMMAND:
        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_RESET_LOGICAL_UNIT:
        case SRB_FUNCTION_RESET_BUS:
            LsiStartTask(Device, Srb);
            break;

        case SRB_FUNCTION_FLUSH:
        case SRB_FUNCTION_SHUTDOWN:
            LsiCompleteRequest(Device, Srb, SRB_STATUS_SUCCESS);
            break;

        default:
            LsiCompleteRequest(Device, Srb, SRB_STATUS_INVALID_REQUEST);
            break;
    }

    return TRUE;
}

static
BOOLEAN
NTAPI
LsiHwResetBus(
    _In_ PVOID DeviceExtension,
    _In_ ULONG PathId)
{
    PLSI_DEVICE_EXTENSION Device = (PLSI_DEVICE_EXTENSION)DeviceExtension;
    STOR_LOCK_HANDLE LockHandle;
    BOOLEAN Result;

    StorPortAcquireSpinLock(Device, InterruptLock, NULL, &LockHandle);
    Result = LsiResetBusLocked(Device, PathId);
    StorPortReleaseSpinLock(Device, &LockHandle);
    return Result;
}

static
SCSI_ADAPTER_CONTROL_STATUS
NTAPI
LsiHwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    PLSI_DEVICE_EXTENSION Device = (PLSI_DEVICE_EXTENSION)DeviceExtension;
    PSCSI_SUPPORTED_CONTROL_TYPE_LIST List;
    PBOOLEAN SupportedTypes;

    switch (ControlType)
    {
        case ScsiQuerySupportedControlTypes:
            List = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
            SupportedTypes = (PBOOLEAN)((PUCHAR)List + FIELD_OFFSET(SCSI_SUPPORTED_CONTROL_TYPE_LIST, SupportedTypeList));
            if (ScsiQuerySupportedControlTypes < List->MaxControlType)
                SupportedTypes[ScsiQuerySupportedControlTypes] = TRUE;
            if (ScsiStopAdapter < List->MaxControlType)
                SupportedTypes[ScsiStopAdapter] = TRUE;
            if (ScsiRestartAdapter < List->MaxControlType)
                SupportedTypes[ScsiRestartAdapter] = TRUE;
            return ScsiAdapterControlSuccess;

        case ScsiStopAdapter:
            LsiStopWatchdog(Device);
            LsiStopIoc(Device);
            return ScsiAdapterControlSuccess;

        case ScsiRestartAdapter:
            if (!LsiReinitializeIoc(Device))
                return ScsiAdapterControlUnsuccessful;
            Device->InterruptsEnabled = TRUE;
            LsiWriteRegister(Device, MPI_HOST_INTERRUPT_MASK_OFFSET, MPI_HIM_DIM);
            LsiStartWatchdog(Device);
            return ScsiAdapterControlSuccess;

        default:
            return ScsiAdapterControlUnsuccessful;
    }
}

ULONG
NTAPI
DriverEntry(
    _In_ PVOID DriverObject,
    _In_ PVOID RegistryPath)
{
    HW_INITIALIZATION_DATA InitData;

    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.HwInitializationDataSize = sizeof(InitData);
    InitData.AdapterInterfaceType = PCIBus;
    InitData.HwInitialize = LsiHwInitialize;
    InitData.HwStartIo = LsiHwStartIo;
    InitData.HwInterrupt = LsiHwInterrupt;
    InitData.HwFindAdapter = LsiHwFindAdapter;
    InitData.HwResetBus = LsiHwResetBus;
    InitData.HwAdapterControl = LsiHwAdapterControl;
    InitData.DeviceExtensionSize = sizeof(LSI_DEVICE_EXTENSION);
    InitData.NumberOfAccessRanges = LSI_ACCESS_RANGES;
    InitData.MapBuffers = STOR_MAP_NO_BUFFERS;
    InitData.NeedPhysicalAddresses = TRUE;
    InitData.TaggedQueuing = TRUE;
    InitData.AutoRequestSense = TRUE;
    InitData.MultipleRequestPerLu = TRUE;

    return StorPortInitialize(DriverObject, RegistryPath, &InitData, NULL);
}
