/*
 * PROJECT:     LiberNT Monitor Class Function Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Monitor WMI data blocks built from the monitor descriptor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "monitor.h"
#include <windef.h>
#include <d3dkmddi.h>
#include <initguid.h>
#include <wmidata.h>
#include <ntstrsafe.h>

#define NDEBUG
#include <debug.h>

#define EDID_BLOCK_SIZE 128
#define EDID_DESCRIPTOR_OFFSET 0x36
#define EDID_DESCRIPTOR_SIZE 18
#define EDID_DESCRIPTOR_COUNT 4
#define EDID_STANDARD_TIMING_OFFSET 0x26
#define EDID_STANDARD_TIMING_COUNT 8
#define EDID_ESTABLISHED_TIMING_COUNT 17
#define MONITOR_MAX_SOURCE_MODES (EDID_ESTABLISHED_TIMING_COUNT + EDID_DESCRIPTOR_COUNT + EDID_STANDARD_TIMING_COUNT)
#define MONITOR_RAW_EDID_OUTPUT_SIZE (FIELD_OFFSET(WmiGetMonitorRawEEdidV1Block_OUT, BlockContent) + EDID_BLOCK_SIZE)

typedef struct _MONITOR_ESTABLISHED_TIMING
{
    UCHAR Index;
    USHORT Width;
    USHORT Height;
    ULONG PixelClock;
    ULONG VerticalNumerator;
    ULONG VerticalDenominator;
    ULONG HorizontalRate;
    USHORT HorizontalBlank;
    USHORT VerticalBlank;
    USHORT HorizontalSyncOffset;
    USHORT VerticalSyncOffset;
    USHORT HorizontalSyncWidth;
    USHORT VerticalSyncWidth;
    USHORT HorizontalBorder;
    USHORT VerticalBorder;
} MONITOR_ESTABLISHED_TIMING;

static const MONITOR_ESTABLISHED_TIMING MonitorEstablishedTimings[] =
{
    { 12, 1024, 768, 65000000, 60004, 1000, 48363, 320, 38, 24, 3, 136, 6, 0, 0 },
    { 7, 800, 600, 40000000, 60317, 1000, 37879, 256, 28, 40, 1, 128, 4, 0, 0 },
    { 2, 640, 480, 25175000, 5994, 100, 31469, 160, 46, 16, 10, 96, 3, 8, 8 },
};

static const GUID *MonitorWmiGuids[MonitorWmiBlockCount] =
{
    &WmiMonitorID_GUID,
    &WmiMonitorBasicDisplayParams_GUID,
    &WmiMonitorConnectionParams_GUID,
    &WmiMonitorAnalogVideoInputParams_GUID,
    &WmiMonitorDigitalVideoInputParams_GUID,
    &WmiMonitorColorCharacteristics_GUID,
    &WmiMonitorListedSupportedSourceModes_GUID,
    &WmiMonitorDescriptorMethods_GUID,
};

static
NTSTATUS
MonitorQueryDescriptor(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _Outptr_ PRXGK_MONITOR_DESCRIPTOR *OutDescriptor)
{
    PRXGK_MONITOR_DESCRIPTOR Descriptor;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    *OutDescriptor = NULL;
    Descriptor = ExAllocatePoolZero(PagedPool, sizeof(*Descriptor), MONITOR_TAG);
    if (!Descriptor)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_RXGK_QUERY_MONITOR_DESCRIPTOR,
                                        DeviceExtension->LowerDevice,
                                        NULL,
                                        0,
                                        Descriptor,
                                        sizeof(*Descriptor),
                                        TRUE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
    {
        ExFreePoolWithTag(Descriptor, MONITOR_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (NT_SUCCESS(Status) &&
        (IoStatus.Information < sizeof(*Descriptor) || Descriptor->EdidLength > sizeof(Descriptor->Edid) ||
         (Descriptor->EdidLength % EDID_BLOCK_SIZE) != 0))
    {
        Status = STATUS_DEVICE_DATA_ERROR;
    }
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Descriptor, MONITOR_TAG);
        return Status;
    }

    *OutDescriptor = Descriptor;
    return STATUS_SUCCESS;
}

static
VOID
MonitorReduce(
    _Inout_ PULONG Numerator,
    _Inout_ PULONG Denominator)
{
    ULONG First = *Numerator, Second = *Denominator, Remainder;

    while (Second)
    {
        Remainder = First % Second;
        First = Second;
        Second = Remainder;
    }
    if (First > 1)
    {
        *Numerator /= First;
        *Denominator /= First;
    }
}

static
BOOLEAN
MonitorEdidText(
    _In_reads_(EDID_BLOCK_SIZE) const UCHAR *Edid,
    _In_ UCHAR Tag,
    _Out_writes_(14) PUSHORT Text,
    _Out_ PULONG Length)
{
    const UCHAR *Descriptor;
    ULONG Index, Count;

    Text[0] = 0;
    *Length = 0;
    for (Index = 0; Index < EDID_DESCRIPTOR_COUNT; Index++)
    {
        Descriptor = Edid + EDID_DESCRIPTOR_OFFSET + Index * EDID_DESCRIPTOR_SIZE;
        if (Descriptor[0] || Descriptor[1] || Descriptor[2] || Descriptor[3] != Tag)
            continue;
        for (Count = 0; Count < 13 && Descriptor[5 + Count] != 0x0A; Count++)
            Text[Count] = Descriptor[5 + Count];
        Text[Count] = 0;
        *Length = Count;
        return TRUE;
    }
    return FALSE;
}

static
VOID
MonitorAsciiToWide(
    _In_z_ PCSTR Source,
    _Out_writes_(16) PUSHORT Destination)
{
    ULONG Index;

    for (Index = 0; Index < 15 && Source[Index]; Index++)
        Destination[Index] = (UCHAR)Source[Index];
}

static
ULONG
MonitorBuildId(
    _In_reads_(EDID_BLOCK_SIZE) const UCHAR *Edid,
    _Out_writes_bytes_opt_(sizeof(WmiMonitorID) + 13 * sizeof(USHORT)) PWmiMonitorID Id)
{
    USHORT Name[14], Serial[14];
    ULONG NameLength, SerialLength;
    CHAR Text[16];
    BOOLEAN HasName;

    HasName = MonitorEdidText(Edid, 0xFC, Name, &NameLength);
    if (!Id)
        return sizeof(WmiMonitorID) + (HasName ? (NameLength + 1) * sizeof(USHORT) : 0);

    RtlZeroMemory(Id, sizeof(WmiMonitorID) + (HasName ? (NameLength + 1) * sizeof(USHORT) : 0));
    RtlStringCbPrintfA(Text, sizeof(Text), "%04X", Edid[0x0A] | (Edid[0x0B] << 8));
    MonitorAsciiToWide(Text, Id->ProductCodeID);
    if (MonitorEdidText(Edid, 0xFF, Serial, &SerialLength))
    {
        RtlCopyMemory(Id->SerialNumberID, Serial, SerialLength * sizeof(USHORT));
    }
    else
    {
        RtlStringCbPrintfA(Text, sizeof(Text), "%lu",
                           Edid[0x0C] | (Edid[0x0D] << 8) | (Edid[0x0E] << 16) | ((ULONG)Edid[0x0F] << 24));
        MonitorAsciiToWide(Text, Id->SerialNumberID);
    }
    Id->ManufacturerName[0] = ((Edid[0x08] >> 2) & 0x1F) + 'A' - 1;
    Id->ManufacturerName[1] = (((Edid[0x08] & 0x03) << 3) | (Edid[0x09] >> 5)) + 'A' - 1;
    Id->ManufacturerName[2] = (Edid[0x09] & 0x1F) + 'A' - 1;
    Id->WeekOfManufacture = Edid[0x10];
    Id->YearOfManufacture = Edid[0x11] + 1990;
    if (HasName)
    {
        Id->UserFriendlyNameLength = (USHORT)(NameLength + 1);
        RtlCopyMemory(Id->UserFriendlyName, Name, (NameLength + 1) * sizeof(USHORT));
    }
    return sizeof(WmiMonitorID) + (HasName ? (NameLength + 1) * sizeof(USHORT) : 0);
}

static
VOID
MonitorBuildBasicDisplayParams(
    _In_reads_(EDID_BLOCK_SIZE) const UCHAR *Edid,
    _Out_ PWmiMonitorBasicDisplayParams Params)
{
    UCHAR Features = Edid[0x18];

    RtlZeroMemory(Params, WmiMonitorBasicDisplayParams_SIZE);
    Params->VideoInputType = (Edid[0x14] >> 7) & 1;
    Params->MaxHorizontalImageSize = Edid[0x15];
    Params->MaxVerticalImageSize = Edid[0x16];
    Params->DisplayTransferCharacteristic = Edid[0x17];
    Params->SupportedDisplayFeatures.StandbySupported = (Features >> 7) & 1;
    Params->SupportedDisplayFeatures.SuspendSupported = (Features >> 6) & 1;
    Params->SupportedDisplayFeatures.ActiveOffSupported = (Features >> 5) & 1;
    Params->SupportedDisplayFeatures.DisplayType = (Features >> 3) & 3;
    Params->SupportedDisplayFeatures.sRGBSupported = (Features >> 2) & 1;
    Params->SupportedDisplayFeatures.HasPreferredTimingMode = (Features >> 1) & 1;
    Params->SupportedDisplayFeatures.GTFSupported = Features & 1;
}

static
VOID
MonitorBuildAnalogVideoInputParams(
    _In_reads_(EDID_BLOCK_SIZE) const UCHAR *Edid,
    _Out_ PWmiMonitorAnalogVideoInputParams Params)
{
    UCHAR Input = Edid[0x14];

    Params->SignalLevelStandard = (Input >> 5) & 3;
    Params->SetupExpected = (Input >> 4) & 1;
    Params->SeparateSyncsSupported = (Input >> 3) & 1;
    Params->CompositeSyncSupported = (Input >> 2) & 1;
    Params->SyncOnGreenVideoSupported = (Input >> 1) & 1;
    Params->SerrationOfVsyncRequired = Input & 1;
}

static
VOID
MonitorBuildColorCharacteristics(
    _In_reads_(EDID_BLOCK_SIZE) const UCHAR *Edid,
    _Out_ PWmiMonitorColorCharacteristics Color)
{
    Color->Red.X = Edid[0x1B];
    Color->Red.Y = Edid[0x1C];
    Color->Green.X = Edid[0x1D];
    Color->Green.Y = Edid[0x1E];
    Color->Blue.X = Edid[0x1F];
    Color->Blue.Y = Edid[0x20];
    Color->DefaultWhite.X = Edid[0x21];
    Color->DefaultWhite.Y = Edid[0x22];
}

static
VOID
MonitorEstablishedMode(
    _In_reads_(EDID_BLOCK_SIZE) const UCHAR *Edid,
    _In_ const MONITOR_ESTABLISHED_TIMING *Timing,
    _Out_ PVideoModeDescriptor Mode)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->PixelClockRate = Timing->PixelClock;
    Mode->VerticalRefreshRateNumerator = Timing->VerticalNumerator;
    Mode->VerticalRefreshRateDenominator = Timing->VerticalDenominator;
    Mode->HorizontalRefreshRateNumerator = Timing->HorizontalRate;
    Mode->HorizontalRefreshRateDenominator = 1;
    Mode->HorizontalActivePixels = Timing->Width;
    Mode->VerticalActivePixels = Timing->Height;
    Mode->HorizontalBlankingPixels = Timing->HorizontalBlank;
    Mode->VerticalBlankingPixels = Timing->VerticalBlank;
    Mode->HorizontalSyncOffset = Timing->HorizontalSyncOffset;
    Mode->VerticalSyncOffset = Timing->VerticalSyncOffset;
    Mode->HorizontalSyncPulseWidth = Timing->HorizontalSyncWidth;
    Mode->VerticalSyncPulseWidth = Timing->VerticalSyncWidth;
    Mode->HorizontalImageSize = Edid[0x15];
    Mode->VerticalImageSize = Edid[0x16];
    Mode->HorizontalBorder = Timing->HorizontalBorder;
    Mode->VerticalBorder = Timing->VerticalBorder;
    Mode->IsSerrationRequired = 2;
    Mode->IsSyncOnRGB = 2;
    Mode->CompositePolarityType = 2;
    Mode->VerticalPolarityType = 2;
    Mode->HorizontalPolarityType = 2;
    Mode->VideoStandardType = D3DKMDT_VSS_VESA_DMT;
    Mode->Origin = D3DKMDT_MCO_MONITORDESCRIPTOR;
    Mode->TimingType = D3DKMDT_MTT_ESTABLISHED;
}

static
BOOLEAN
MonitorDetailedMode(
    _In_reads_(EDID_DESCRIPTOR_SIZE) const UCHAR *Dtd,
    _Out_ PVideoModeDescriptor Mode)
{
    UCHAR Flags = Dtd[17];
    ULONG HorizontalTotal, VerticalTotal;

    if (Dtd[0] == 0 && Dtd[1] == 0)
        return FALSE;

    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->PixelClockRate = (Dtd[0] | (Dtd[1] << 8)) * 10000;
    Mode->HorizontalActivePixels = Dtd[2] | ((Dtd[4] & 0xF0) << 4);
    Mode->HorizontalBlankingPixels = Dtd[3] | ((Dtd[4] & 0x0F) << 8);
    Mode->VerticalActivePixels = Dtd[5] | ((Dtd[7] & 0xF0) << 4);
    Mode->VerticalBlankingPixels = Dtd[6] | ((Dtd[7] & 0x0F) << 8);
    Mode->HorizontalSyncOffset = Dtd[8] | ((Dtd[11] & 0xC0) << 2);
    Mode->HorizontalSyncPulseWidth = Dtd[9] | ((Dtd[11] & 0x30) << 4);
    Mode->VerticalSyncOffset = (Dtd[10] >> 4) | ((Dtd[11] & 0x0C) << 2);
    Mode->VerticalSyncPulseWidth = (Dtd[10] & 0x0F) | ((Dtd[11] & 0x03) << 4);
    Mode->HorizontalImageSize = Dtd[12] | ((Dtd[14] & 0xF0) << 4);
    Mode->VerticalImageSize = Dtd[13] | ((Dtd[14] & 0x0F) << 8);
    Mode->HorizontalBorder = Dtd[15];
    Mode->VerticalBorder = Dtd[16];
    Mode->IsInterlaced = (Flags >> 7) & 1;
    Mode->StereoModeType = ((Flags >> 4) & 6) | (Flags & 1);
    Mode->SyncSignalType = (Flags >> 3) & 3;
    Mode->IsSerrationRequired = 2;
    Mode->IsSyncOnRGB = 2;
    Mode->CompositePolarityType = 2;
    Mode->VerticalPolarityType = 2;
    Mode->HorizontalPolarityType = 2;
    switch (Mode->SyncSignalType)
    {
        case 3:
            Mode->VerticalPolarityType = (Flags & 0x04) ? 0 : 1;
            Mode->HorizontalPolarityType = (Flags & 0x02) ? 0 : 1;
            break;
        case 2:
            Mode->IsSerrationRequired = (Flags >> 2) & 1;
            Mode->CompositePolarityType = (Flags & 0x02) ? 0 : 1;
            break;
        default:
            Mode->IsSerrationRequired = (Flags >> 2) & 1;
            Mode->IsSyncOnRGB = (Flags >> 1) & 1;
            break;
    }

    HorizontalTotal = Mode->HorizontalActivePixels + Mode->HorizontalBlankingPixels;
    VerticalTotal = Mode->VerticalActivePixels + Mode->VerticalBlankingPixels;
    if (HorizontalTotal && VerticalTotal)
    {
        Mode->HorizontalRefreshRateNumerator = Mode->PixelClockRate;
        Mode->HorizontalRefreshRateDenominator = HorizontalTotal;
        MonitorReduce(&Mode->HorizontalRefreshRateNumerator, &Mode->HorizontalRefreshRateDenominator);
        Mode->VerticalRefreshRateNumerator = Mode->PixelClockRate;
        Mode->VerticalRefreshRateDenominator = HorizontalTotal * VerticalTotal;
        MonitorReduce(&Mode->VerticalRefreshRateNumerator, &Mode->VerticalRefreshRateDenominator);
    }
    Mode->VideoStandardType = D3DKMDT_VSS_UNINITIALIZED;
    Mode->Origin = D3DKMDT_MCO_MONITORDESCRIPTOR;
    Mode->TimingType = D3DKMDT_MTT_DETAILED;
    return TRUE;
}

static
BOOLEAN
MonitorStandardMode(
    _In_reads_(EDID_BLOCK_SIZE) const UCHAR *Edid,
    _In_ ULONG Index,
    _Out_ PVideoModeDescriptor Mode)
{
    UCHAR First = Edid[EDID_STANDARD_TIMING_OFFSET + Index * 2];
    UCHAR Second = Edid[EDID_STANDARD_TIMING_OFFSET + Index * 2 + 1];
    ULONGLONG LineRate, Numerator, Denominator;
    ULONG Width, Height, Rate, VerticalSync, Blank;

    if (First == 0x00 || (First == 0x01 && Second == 0x01))
        return FALSE;

    Width = (First + 31) * 8;
    Rate = (Second & 0x3F) + 60;
    switch (Second >> 6)
    {
        case 0:
            Height = (Edid[0x12] == 1 && Edid[0x13] < 3) ? Width : Width * 10 / 16;
            break;
        case 1:
            Height = Width * 3 / 4;
            break;
        case 2:
            Height = Width * 4 / 5;
            break;
        default:
            Height = Width * 9 / 16;
            break;
    }

    Numerator = 550ULL * (Height + 1) * Rate;
    Denominator = 1000000ULL - 550ULL * Rate;
    VerticalSync = (ULONG)((2 * Numerator + Denominator) / (2 * Denominator));
    LineRate = (ULONGLONG)(Height + VerticalSync + 1) * Rate;
    if (LineRate * 30 <= 300000)
        return FALSE;
    Numerator = Width * (LineRate * 30 - 300000);
    Denominator = 16 * (LineRate * 70 + 300000);
    Blank = (ULONG)((2 * Numerator + Denominator) / (2 * Denominator)) * 16;

    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->PixelClockRate = (ULONG)((Width + Blank) * LineRate);
    Mode->VerticalRefreshRateNumerator = Rate;
    Mode->VerticalRefreshRateDenominator = 1;
    Mode->HorizontalRefreshRateNumerator = (ULONG)LineRate;
    Mode->HorizontalRefreshRateDenominator = 1;
    Mode->HorizontalActivePixels = (USHORT)Width;
    Mode->VerticalActivePixels = (USHORT)Height;
    Mode->HorizontalBlankingPixels = (USHORT)Blank;
    Mode->VerticalBlankingPixels = (USHORT)(VerticalSync + 1);
    Mode->VideoStandardType = D3DKMDT_VSS_VESA_GTF;
    Mode->Origin = D3DKMDT_MCO_MONITORDESCRIPTOR;
    Mode->TimingType = D3DKMDT_MTT_STANDARD;
    return TRUE;
}

static
ULONG
MonitorBuildSourceModes(
    _In_reads_(EDID_BLOCK_SIZE) const UCHAR *Edid,
    _Out_writes_(MONITOR_MAX_SOURCE_MODES) PVideoModeDescriptor Modes,
    _Out_ PUSHORT Preferred)
{
    ULONG Count = 0, Index, Table;
    LONG Bit;
    BOOLEAN HavePreferred = FALSE;

    *Preferred = 0;
    for (Bit = EDID_ESTABLISHED_TIMING_COUNT - 1; Bit >= 0; Bit--)
    {
        if (!(Edid[0x23 + Bit / 8] & (0x80 >> (Bit % 8))))
            continue;
        for (Table = 0; Table < RTL_NUMBER_OF(MonitorEstablishedTimings); Table++)
        {
            if (MonitorEstablishedTimings[Table].Index == Bit)
                MonitorEstablishedMode(Edid, &MonitorEstablishedTimings[Table], &Modes[Count++]);
        }
    }
    for (Index = 0; Index < EDID_DESCRIPTOR_COUNT; Index++)
    {
        if (!MonitorDetailedMode(Edid + EDID_DESCRIPTOR_OFFSET + Index * EDID_DESCRIPTOR_SIZE, &Modes[Count]))
            continue;
        if (!HavePreferred)
        {
            *Preferred = (USHORT)Count;
            HavePreferred = TRUE;
        }
        Count++;
    }
    for (Index = 0; Index < EDID_STANDARD_TIMING_COUNT; Index++)
    {
        if (MonitorStandardMode(Edid, Index, &Modes[Count]))
            Count++;
    }
    return Count;
}

static
NTSTATUS
MonitorBuildBlock(
    _In_ MONITOR_WMI_BLOCK Block,
    _In_ PRXGK_MONITOR_DESCRIPTOR Descriptor,
    _Out_writes_bytes_opt_(BufferSize) PUCHAR Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG Needed)
{
    PWmiMonitorListedSupportedSourceModes List;
    PVideoModeDescriptor Modes;
    const UCHAR *Edid = Descriptor->Edid;
    ULONG Count;
    USHORT Preferred;

    *Needed = 0;
    if (Block == MonitorWmiConnectionParams)
    {
        *Needed = WmiMonitorConnectionParams_SIZE;
        if (Buffer && BufferSize >= *Needed)
            ((PWmiMonitorConnectionParams)Buffer)->VideoOutputTechnology = Descriptor->VideoOutputTechnology;
        return STATUS_SUCCESS;
    }
    if (Descriptor->EdidLength < EDID_BLOCK_SIZE)
        return STATUS_MONITOR_NO_DESCRIPTOR;

    switch (Block)
    {
        case MonitorWmiId:
            *Needed = MonitorBuildId(Edid, NULL);
            if (Buffer && BufferSize >= *Needed)
                MonitorBuildId(Edid, (PWmiMonitorID)Buffer);
            break;

        case MonitorWmiBasicDisplayParams:
            *Needed = WmiMonitorBasicDisplayParams_SIZE;
            if (Buffer && BufferSize >= *Needed)
                MonitorBuildBasicDisplayParams(Edid, (PWmiMonitorBasicDisplayParams)Buffer);
            break;

        case MonitorWmiAnalogVideoInputParams:
            *Needed = WmiMonitorAnalogVideoInputParams_SIZE;
            if (Buffer && BufferSize >= *Needed)
                MonitorBuildAnalogVideoInputParams(Edid, (PWmiMonitorAnalogVideoInputParams)Buffer);
            break;

        case MonitorWmiDigitalVideoInputParams:
            *Needed = WmiMonitorDigitalVideoInputParams_SIZE;
            if (Buffer && BufferSize >= *Needed)
                ((PWmiMonitorDigitalVideoInputParams)Buffer)->IsDFP1xCompatible = Edid[0x14] & 1;
            break;

        case MonitorWmiColorCharacteristics:
            *Needed = WmiMonitorColorCharacteristics_SIZE;
            if (Buffer && BufferSize >= *Needed)
                MonitorBuildColorCharacteristics(Edid, (PWmiMonitorColorCharacteristics)Buffer);
            break;

        case MonitorWmiListedSupportedSourceModes:
            Modes = ExAllocatePoolZero(PagedPool, MONITOR_MAX_SOURCE_MODES * sizeof(VideoModeDescriptor), MONITOR_TAG);
            if (!Modes)
                return STATUS_INSUFFICIENT_RESOURCES;
            Count = MonitorBuildSourceModes(Edid, Modes, &Preferred);
            *Needed = FIELD_OFFSET(WmiMonitorListedSupportedSourceModes, MonitorSourceModes) + Count * sizeof(VideoModeDescriptor);
            if (Buffer && BufferSize >= *Needed)
            {
                List = (PWmiMonitorListedSupportedSourceModes)Buffer;
                List->NumOfMonitorSourceModes = (USHORT)Count;
                List->PreferredMonitorSourceModeIndex = Preferred;
                RtlCopyMemory(List->MonitorSourceModes, Modes, Count * sizeof(VideoModeDescriptor));
            }
            ExFreePoolWithTag(Modes, MONITOR_TAG);
            break;

        default:
            break;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
MonitorQueryWmiRegInfo(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PULONG RegFlags,
    _Inout_ PUNICODE_STRING InstanceName,
    _Outptr_result_maybenull_ PUNICODE_STRING *RegistryPath,
    _Inout_ PUNICODE_STRING MofResourceName,
    _Outptr_result_maybenull_ PDEVICE_OBJECT *Pdo)
{
    PMONITOR_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    UNREFERENCED_PARAMETER(InstanceName);
    UNREFERENCED_PARAMETER(MofResourceName);

    *RegFlags = WMIREG_FLAG_INSTANCE_PDO;
    *RegistryPath = &MonitorRegistryPath;
    *Pdo = DeviceExtension->PhysicalDevice;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
MonitorQueryWmiDataBlock(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ ULONG GuidIndex,
    _In_ ULONG InstanceIndex,
    _In_ ULONG InstanceCount,
    _Out_writes_opt_(InstanceCount) PULONG InstanceLengthArray,
    _In_ ULONG BufferAvail,
    _Out_writes_bytes_opt_(BufferAvail) PUCHAR Buffer)
{
    PMONITOR_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PRXGK_MONITOR_DESCRIPTOR Descriptor;
    ULONG Needed = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (GuidIndex >= DeviceExtension->WmiLibContext.GuidCount)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_GUID_NOT_FOUND, 0, IO_NO_INCREMENT);
    if (InstanceIndex != 0 || InstanceCount != 1)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_INSTANCE_NOT_FOUND, 0, IO_NO_INCREMENT);

    Status = MonitorQueryDescriptor(DeviceExtension, &Descriptor);
    if (!NT_SUCCESS(Status))
        return WmiCompleteRequest(DeviceObject, Irp, Status, 0, IO_NO_INCREMENT);

    Status = MonitorBuildBlock(DeviceExtension->Blocks[GuidIndex], Descriptor, Buffer, BufferAvail, &Needed);
    ExFreePoolWithTag(Descriptor, MONITOR_TAG);
    if (NT_SUCCESS(Status))
    {
        if (!Buffer || BufferAvail < Needed)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else if (InstanceLengthArray)
        {
            InstanceLengthArray[0] = Needed;
        }
    }
    return WmiCompleteRequest(DeviceObject, Irp, Status, Needed, IO_NO_INCREMENT);
}

static
NTSTATUS
NTAPI
MonitorExecuteWmiMethod(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ ULONG GuidIndex,
    _In_ ULONG InstanceIndex,
    _In_ ULONG MethodId,
    _In_ ULONG InBufferSize,
    _In_ ULONG OutBufferSize,
    _Inout_updates_bytes_(max(InBufferSize, OutBufferSize)) PUCHAR Buffer)
{
    PMONITOR_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PWmiGetMonitorRawEEdidV1Block_OUT Output;
    PRXGK_MONITOR_DESCRIPTOR Descriptor;
    ULONG BlockId, Needed = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (GuidIndex >= DeviceExtension->WmiLibContext.GuidCount || InstanceIndex != 0)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_INSTANCE_NOT_FOUND, 0, IO_NO_INCREMENT);
    if (DeviceExtension->Blocks[GuidIndex] != MonitorWmiDescriptorMethods)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_INVALID_DEVICE_REQUEST, 0, IO_NO_INCREMENT);
    if (MethodId != WmiGetMonitorRawEEdidV1Block)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_ITEMID_NOT_FOUND, 0, IO_NO_INCREMENT);
    if (InBufferSize < WmiGetMonitorRawEEdidV1Block_IN_SIZE)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_INVALID_PARAMETER, 0, IO_NO_INCREMENT);

    BlockId = ((PWmiGetMonitorRawEEdidV1Block_IN)Buffer)->BlockId;
    Status = MonitorQueryDescriptor(DeviceExtension, &Descriptor);
    if (!NT_SUCCESS(Status))
        return WmiCompleteRequest(DeviceObject, Irp, Status, 0, IO_NO_INCREMENT);

    if ((BlockId + 1) * EDID_BLOCK_SIZE > Descriptor->EdidLength)
    {
        Status = STATUS_MONITOR_NO_DESCRIPTOR;
    }
    else
    {
        Needed = MONITOR_RAW_EDID_OUTPUT_SIZE;
        if (OutBufferSize < Needed)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            Output = (PWmiGetMonitorRawEEdidV1Block_OUT)Buffer;
            Output->BlockType = BlockId == 0 ? 1 : 0xFF;
            RtlCopyMemory(Output->BlockContent, Descriptor->Edid + BlockId * EDID_BLOCK_SIZE, EDID_BLOCK_SIZE);
        }
    }
    ExFreePoolWithTag(Descriptor, MONITOR_TAG);
    return WmiCompleteRequest(DeviceObject, Irp, Status, Needed, IO_NO_INCREMENT);
}

VOID
MonitorWmiRegister(
    _Inout_ PMONITOR_DEVICE_EXTENSION DeviceExtension)
{
    PRXGK_MONITOR_DESCRIPTOR Descriptor;
    ULONG Count = 0, Index;
    NTSTATUS Status;

    PAGED_CODE();

    if (DeviceExtension->WmiRegistered)
        return;

    Status = MonitorQueryDescriptor(DeviceExtension, &Descriptor);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MONITOR: no descriptor from the monitor device: 0x%08lx\n", Status);
        return;
    }

    if (Descriptor->EdidLength >= EDID_BLOCK_SIZE)
    {
        DeviceExtension->Blocks[Count++] = MonitorWmiId;
        DeviceExtension->Blocks[Count++] = MonitorWmiBasicDisplayParams;
        DeviceExtension->Blocks[Count++] = MonitorWmiConnectionParams;
        DeviceExtension->Blocks[Count++] = (Descriptor->Edid[0x14] & 0x80) ? MonitorWmiDigitalVideoInputParams :
                                                                              MonitorWmiAnalogVideoInputParams;
        DeviceExtension->Blocks[Count++] = MonitorWmiColorCharacteristics;
        DeviceExtension->Blocks[Count++] = MonitorWmiListedSupportedSourceModes;
        DeviceExtension->Blocks[Count++] = MonitorWmiDescriptorMethods;
    }
    else
    {
        DeviceExtension->Blocks[Count++] = MonitorWmiConnectionParams;
    }
    ExFreePoolWithTag(Descriptor, MONITOR_TAG);

    for (Index = 0; Index < Count; Index++)
    {
        DeviceExtension->GuidList[Index].Guid = MonitorWmiGuids[DeviceExtension->Blocks[Index]];
        DeviceExtension->GuidList[Index].InstanceCount = 1;
        DeviceExtension->GuidList[Index].Flags = 0;
    }
    DeviceExtension->WmiLibContext.GuidCount = Count;
    DeviceExtension->WmiLibContext.GuidList = DeviceExtension->GuidList;
    DeviceExtension->WmiLibContext.QueryWmiRegInfo = MonitorQueryWmiRegInfo;
    DeviceExtension->WmiLibContext.QueryWmiDataBlock = MonitorQueryWmiDataBlock;
    DeviceExtension->WmiLibContext.ExecuteWmiMethod = MonitorExecuteWmiMethod;

    DeviceExtension->WmiRegistered = TRUE;
    Status = IoWMIRegistrationControl(DeviceExtension->DeviceObject, WMIREG_ACTION_REGISTER);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MONITOR: WMI registration failed: 0x%08lx\n", Status);
        DeviceExtension->WmiRegistered = FALSE;
    }
}

VOID
MonitorWmiDeregister(
    _Inout_ PMONITOR_DEVICE_EXTENSION DeviceExtension)
{
    PAGED_CODE();

    if (!DeviceExtension->WmiRegistered)
        return;
    IoWMIRegistrationControl(DeviceExtension->DeviceObject, WMIREG_ACTION_DEREGISTER);
    DeviceExtension->WmiRegistered = FALSE;
}
