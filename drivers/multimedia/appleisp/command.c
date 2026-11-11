/*
 * PROJECT:     LiberNT Apple ISP Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ISP firmware commands, sensor detection, capture setup and frame buffer exchange
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appleisp.h"

C_ASSERT(FIELD_OFFSET(ISP_BUFFER_LIST, Entry) + (ISP_META_COUNT + ISP_FRAME_COUNT) * sizeof(ISP_BUFFER_ENTRY) <=
         ISP_COMMAND_SPACE);
C_ASSERT(ISP_BUFFER_EXCHANGE_SIZE <= ISP_COMMAND_SPACE);
C_ASSERT(sizeof(ISP_CMD_CHANNEL_INFO) <= ISP_COMMAND_SPACE);

/* FIXME: the tuning file is picked by sensor version only; module variants (1921 vs 1922) are not told apart */
static const ISP_SENSOR IspSensors[] =
{
    { 0x248, 0x18200103, L"isp_1820_01XX.dat", 0x442c },
    { 0x364, 0x87200103, L"isp_8720_01XX.dat", 0x36ac },
    { 0x558, 0x19210106, L"isp_1921_01XX.dat", 0xad40 },
};

NTSTATUS
IspCommand(
    _In_ PISP_DEVICE Isp,
    _Inout_updates_bytes_(InSize) PVOID Arguments,
    _In_ ULONG InSize,
    _In_ ULONG OutSize)
{
    PISP_CHANNEL Io;
    ULONG64 Opcode;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(InSize >= sizeof(ULONG64) && (OutSize == 0 || OutSize == InSize));
    RtlCopyMemory(&Opcode, Arguments, sizeof(Opcode));
    KeWaitForSingleObject(&Isp->CommandLock, Executive, KernelMode, FALSE, NULL);
    Io = Isp->Io;
    if (Isp->FirmwareState == IspFirmwareOff || !Io || !Isp->Command || InSize > Isp->CommandSpace)
    {
        KeReleaseMutex(&Isp->CommandLock, FALSE);
        return STATUS_DEVICE_NOT_READY;
    }
    RtlCopyMemory(Isp->Command, Arguments, InSize);
    RtlZeroMemory(&Io->Request, sizeof(Io->Request));
    Io->Request.Argument[0] = Isp->CommandIova;
    Io->Request.Argument[1] = InSize;
    Io->Request.Argument[2] = OutSize;
    Status = IspSend(Isp, Io, ISP_COMMAND_TIMEOUT_MS);
    if (NT_SUCCESS(Status) && OutSize)
        RtlCopyMemory(Arguments, Isp->Command, OutSize);
    KeReleaseMutex(&Isp->CommandLock, FALSE);
    if (!NT_SUCCESS(Status))
        DbgPrint("appleisp: command %04I64x failed: %lx\n", Opcode >> 32, Status);
    return Status;
}

static
NTSTATUS
IspChannelCommand(
    _In_ PISP_DEVICE Isp,
    _In_ ULONG Code)
{
    ISP_CMD_CHANNEL Command;

    Command.Opcode = ISP_OPCODE(Code);
    Command.Channel = ISP_CAMERA_CHANNEL;
    return IspCommand(Isp, &Command, sizeof(Command), 0);
}

static
NTSTATUS
IspChannelValue(
    _In_ PISP_DEVICE Isp,
    _In_ ULONG Code,
    _In_ ULONG Value)
{
    ISP_CMD_CHANNEL_VALUE Command;

    Command.Opcode = ISP_OPCODE(Code);
    Command.Channel = ISP_CAMERA_CHANNEL;
    Command.Value = Value;
    return IspCommand(Isp, &Command, sizeof(Command), 0);
}

static
NTSTATUS
IspFlickerSensor(
    _In_ PISP_DEVICE Isp)
{
    ISP_CMD_VALUE Command;

    Command.Opcode = ISP_OPCODE(CISP_CMD_FLICKER_SENSOR_SET);
    Command.Value = 0;
    return IspCommand(Isp, &Command, sizeof(Command), sizeof(Command));
}

NTSTATUS
IspStartCommandProcessor(
    _In_ PISP_DEVICE Isp)
{
    const ISP_HARDWARE *Hardware = Isp->Hardware;
    ISP_CMD_VALUE Value;
    ISP_CMD_ADDRESS Pmu;
    ISP_CMD_DSID_CLEAR Dsid;
    ISP_CMD_PMP_CONTROL Pmp;
    NTSTATUS Status;

    Value.Opcode = ISP_OPCODE(CISP_CMD_PRINT_ENABLE);
    Value.Value = 1;
    Status = IspCommand(Isp, &Value, sizeof(Value), sizeof(Value));
    if (NT_SUCCESS(Status))
    {
        Pmu.Opcode = ISP_OPCODE(CISP_CMD_SET_ISP_PMU_BASE);
        Pmu.Address = Hardware->PmuBase;
        Status = IspCommand(Isp, &Pmu, sizeof(Pmu), 0);
    }
    if (NT_SUCCESS(Status))
    {
        Dsid.Opcode = ISP_OPCODE(CISP_CMD_SET_DSID_CLR_REG_BASE);
        Dsid.Base = Hardware->DsidClearBase;
        Dsid.Range = Hardware->DsidClearRange;
        Status = IspCommand(Isp, &Dsid, sizeof(Dsid), 0);
    }
    if (NT_SUCCESS(Status) && Hardware->ClockScratch)
    {
        RtlZeroMemory(&Pmp, sizeof(Pmp));
        Pmp.Opcode = ISP_OPCODE(CISP_CMD_PMP_CTRL_SET);
        Pmp.ClockScratch = Hardware->ClockScratch;
        Pmp.ClockBase = Hardware->ClockBase;
        Pmp.ClockBit = Hardware->ClockBit;
        Pmp.ClockSize = Hardware->ClockSize;
        Pmp.BandwidthScratch = Hardware->BandwidthScratch;
        Pmp.BandwidthBase = Hardware->BandwidthBase;
        Pmp.BandwidthBit = Hardware->BandwidthBit;
        Pmp.BandwidthSize = Hardware->BandwidthSize;
        Status = IspCommand(Isp, &Pmp, sizeof(Pmp), 0);
    }
    if (NT_SUCCESS(Status))
    {
        Value.Opcode = ISP_OPCODE(CISP_CMD_START);
        Value.Value = 0;
        Status = IspCommand(Isp, &Value, sizeof(Value), 0);
    }
    DbgPrint("appleisp: firmware command processor start: %lx\n", Status);
    return Status;
}

NTSTATUS
IspDetectSensor(
    _In_ PISP_DEVICE Isp)
{
    ISP_CMD_CONFIG Config;
    ISP_CMD_CHANNEL_INFO Info;
    ISP_CMD_CAMERA_CONFIG Mode;
    ULONG Index;
    NTSTATUS Status;

    ASSERT(Isp->FirmwareState == IspFirmwareRunning);
    RtlZeroMemory(&Config, sizeof(Config));
    Config.Opcode = ISP_OPCODE(CISP_CMD_CONFIG_GET);
    Status = IspCommand(Isp, &Config, sizeof(Config), sizeof(Config));
    if (!NT_SUCCESS(Status))
        return Status;
    DbgPrint("appleisp: firmware drives %lu camera channel(s), timestamps at %lu Hz\n", Config.ChannelCount,
             Config.TimestampFrequency);
    if (!Config.ChannelCount)
        return STATUS_NO_SUCH_DEVICE;
    Isp->SensorChannels = Config.ChannelCount;
    /* TODO: channels after 0 (other cameras on the same ISP) are not exposed */

    RtlZeroMemory(&Info, sizeof(Info));
    Info.Opcode = ISP_OPCODE(CISP_CMD_CH_INFO_GET);
    Info.Channel = ISP_CAMERA_CHANNEL;
    Status = IspCommand(Isp, &Info, sizeof(Info), sizeof(Info));
    if (!NT_SUCCESS(Status))
        return Status;
    Isp->SensorVersion = Info.Version;
    Isp->SensorModes = Info.PresetCount;
    RtlZeroMemory(Isp->ModuleSerial, sizeof(Isp->ModuleSerial));
    RtlCopyMemory(Isp->ModuleSerial, Info.ModuleSerial, sizeof(Info.ModuleSerial));
    DbgPrint("appleisp: sensor %lx on channel %lu, module %s, %lu sensor modes\n", Isp->SensorVersion,
             ISP_CAMERA_CHANNEL, Isp->ModuleSerial, Isp->SensorModes);
    for (Index = 0; Index < min(Isp->SensorModes, ISP_MAX_PRESETS); ++Index)
    {
        RtlZeroMemory(&Mode, sizeof(Mode));
        Mode.Opcode = ISP_OPCODE(CISP_CMD_CH_CAMERA_CONFIG_GET);
        Mode.Channel = ISP_CAMERA_CHANNEL;
        Mode.Preset = Index;
        if (!NT_SUCCESS(IspCommand(Isp, &Mode, sizeof(Mode), sizeof(Mode))))
            break;
        DbgPrint("appleisp: sensor mode %lu: %ux%u in, %ux%u out, sensor clock %lu Hz\n", Index, Mode.InputWidth,
                 Mode.InputHeight, Mode.OutputWidth, Mode.OutputHeight, Mode.SensorClock);
    }

    Isp->Sensor = NULL;
    for (Index = 0; Index < RTL_NUMBER_OF(IspSensors); ++Index)
    {
        if (IspSensors[Index].Version == Isp->SensorVersion)
            Isp->Sensor = &IspSensors[Index];
    }
    if (!Isp->Sensor)
    {
        DbgPrint("appleisp: sensor %lx has no known tuning; capture is not supported\n", Isp->SensorVersion);
        Status = STATUS_NOT_SUPPORTED;
    }
    IspFlickerSensor(Isp);
    IspChannelCommand(Isp, CISP_CMD_CH_STOP);
    IspChannelCommand(Isp, CISP_CMD_CH_BUFFER_RETURN);
    return Status;
}

static
NTSTATUS
IspLoadSetFile(
    _In_ PISP_DEVICE Isp,
    _Out_ PULONG Size)
{
    const ISP_SENSOR *Sensor = Isp->Sensor;
    FILE_STANDARD_INFORMATION Information;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Path;
    WCHAR PathBuffer[128];
    PUCHAR Buffer = NULL;
    HANDLE Handle;
    ULONG Magic;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Sensor && Sensor->SetFileSize <= Isp->Data->Size);
    *Size = 0;
    Status = RtlStringCbPrintfW(PathBuffer, sizeof(PathBuffer), L"\\SystemRoot\\System32\\drivers\\firmware\\apple\\%s",
                                Sensor->SetFile);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlInitUnicodeString(&Path, PathBuffer);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwCreateFile(&Handle, GENERIC_READ | SYNCHRONIZE, &Attributes, &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleisp: tuning file %wZ not opened: %lx\n", &Path, Status);
        return Status;
    }
    Status = ZwQueryInformationFile(Handle, &IoStatus, &Information, sizeof(Information), FileStandardInformation);
    if (NT_SUCCESS(Status) && Information.EndOfFile.QuadPart < Sensor->SetFileSize)
        Status = STATUS_INVALID_IMAGE_FORMAT;
    if (NT_SUCCESS(Status))
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, Sensor->SetFileSize, ISP_TAG);
        Status = Buffer ? ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus, Buffer, Sensor->SetFileSize, NULL, NULL) :
                          STATUS_INSUFFICIENT_RESOURCES;
        if (NT_SUCCESS(Status) && IoStatus.Information != Sensor->SetFileSize)
            Status = STATUS_END_OF_FILE;
    }
    ZwClose(Handle);
    if (NT_SUCCESS(Status))
    {
        Magic = ((ULONG)Buffer[0] << 24) | ((ULONG)Buffer[1] << 16) | ((ULONG)Buffer[2] << 8) | Buffer[3];
        if (Magic != Sensor->Magic)
        {
            DbgPrint("appleisp: tuning file %wZ starts with %08lx, not %08lx\n", &Path, Magic, Sensor->Magic);
            Status = STATUS_INVALID_IMAGE_FORMAT;
        }
    }
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(Isp->Data->Virtual, Buffer, Sensor->SetFileSize);
        KeMemoryBarrier();
        *Size = Sensor->SetFileSize;
    }
    if (Buffer)
        ExFreePoolWithTag(Buffer, ISP_TAG);
    return Status;
}

static
NTSTATUS
IspPoolConfig(
    _In_ PISP_DEVICE Isp,
    _In_ USHORT Type)
{
    ISP_CMD_POOL_CONFIG Pool;

    RtlZeroMemory(&Pool, sizeof(Pool));
    Pool.Opcode = ISP_OPCODE(CISP_CMD_CH_BUFFER_POOL_CONFIG_SET);
    Pool.Channel = ISP_CAMERA_CHANNEL;
    Pool.Type = Type;
    Pool.Count = ISP_META_COUNT;
    Pool.MetaSize0 = Isp->Hardware->MetaSize;
    Pool.MetaSize1 = Isp->Hardware->MetaSize;
    Pool.DataBlocks = 1;
    return IspCommand(Isp, &Pool, sizeof(Pool), sizeof(Pool));
}

NTSTATUS
IspConfigureCapture(
    _In_ PISP_DEVICE Isp,
    _In_ PISP_PRESET Preset)
{
    static const ULONG Metering[] = ISP_FD_SCENE_METERING_VALUES;
    ISP_CMD_FILE_LOAD Load;
    ISP_CMD_CAMERA_SELECT Select;
    ISP_CMD_CROP Crop;
    ISP_CMD_OUTPUT_CONFIG Output;
    ISP_CMD_MBNR Mbnr;
    ISP_CMD_FD_SCENE_METERING Scene;
    ISP_CMD_SIF_FORMAT Sif;
    ISP_CMD_TEMPORAL_FILTER Temporal;
    ULONG SetFileSize;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Isp->FirmwareState == IspFirmwareRunning && Isp->Sensor);
    C_ASSERT(sizeof(Metering) == sizeof(Scene.Value));
    Status = IspFlickerSensor(Isp);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = IspLoadSetFile(Isp, &SetFileSize);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Load, sizeof(Load));
        Load.Opcode = ISP_OPCODE(CISP_CMD_CH_SET_FILE_LOAD);
        Load.Channel = ISP_CAMERA_CHANNEL;
        Load.Address = Isp->Data->Iova;
        Load.Size = SetFileSize;
        Status = IspCommand(Isp, &Load, sizeof(Load), 0);
        DbgPrint("appleisp: sensor tuning %ws (%lx bytes) loaded: %lx\n", Isp->Sensor->SetFile, SetFileSize, Status);
    }
    if (!NT_SUCCESS(Status))
        DbgPrint("appleisp: no sensor tuning (%lx), the picture will be uncalibrated\n", Status);

    Status = IspChannelValue(Isp, CISP_CMD_CH_SBS_ENABLE, 1);
    if (NT_SUCCESS(Status))
    {
        Select.Opcode = ISP_OPCODE(CISP_CMD_CH_CAMERA_CONFIG_SELECT);
        Select.Channel = ISP_CAMERA_CHANNEL;
        Select.Preset = Preset->ConfigIndex;
        Status = IspCommand(Isp, &Select, sizeof(Select), 0);
    }
    if (NT_SUCCESS(Status))
        Status = IspChannelValue(Isp, CISP_CMD_CH_BUFFER_RECYCLE_MODE_SET, CISP_BUFFER_RECYCLE_MODE_EMPTY_ONLY);
    if (NT_SUCCESS(Status))
        Status = IspChannelCommand(Isp, CISP_CMD_CH_BUFFER_RECYCLE_START);
    if (NT_SUCCESS(Status))
    {
        Crop.Opcode = ISP_OPCODE(CISP_CMD_CH_CROP_SCL1_SET);
        Crop.Channel = ISP_CAMERA_CHANNEL;
        Crop.X = Preset->CropX;
        Crop.Y = Preset->CropY;
        Crop.Width = Preset->CropWidth;
        Crop.Height = Preset->CropHeight;
        Status = IspCommand(Isp, &Crop, sizeof(Crop), 0);
    }
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Output, sizeof(Output));
        Output.Opcode = ISP_OPCODE(CISP_CMD_CH_OUTPUT_CONFIG_SCL1_SET);
        Output.Channel = ISP_CAMERA_CHANNEL;
        Output.Width = Preset->OutputWidth;
        Output.Height = Preset->OutputHeight;
        Output.Colorspace = CISP_COLORSPACE_REC709;
        Output.Format = CISP_OUTPUT_FORMAT_YUV_2PLANE;
        Output.Stride[0] = Preset->Stride;
        Output.Stride[1] = Preset->Stride;
        Output.HeightAgain = Preset->OutputHeight;
        Output.WidthAgain = Preset->OutputWidth;
        Status = IspCommand(Isp, &Output, sizeof(Output), 0);
    }
    if (NT_SUCCESS(Status))
        Status = IspChannelValue(Isp, CISP_CMD_CH_PREVIEW_STREAM_SET, 1);
    if (NT_SUCCESS(Status))
        Status = IspChannelCommand(Isp, CISP_CMD_CH_CNR_START);
    if (NT_SUCCESS(Status))
    {
        Mbnr.Opcode = ISP_OPCODE(CISP_CMD_CH_MBNR_ENABLE);
        Mbnr.Channel = ISP_CAMERA_CHANNEL;
        Mbnr.UseCase = 0;
        Mbnr.Mode = CISP_MBNR_MODE_ENABLE;
        Mbnr.EnableChroma = 1;
        Status = IspCommand(Isp, &Mbnr, sizeof(Mbnr), 0);
    }
    if (NT_SUCCESS(Status))
    {
        Scene.Opcode = ISP_OPCODE(CISP_CMD_APPLE_CH_AE_FD_SCENE_METERING_SET);
        Scene.Channel = ISP_CAMERA_CHANNEL;
        RtlCopyMemory(Scene.Value, Metering, sizeof(Scene.Value));
        Status = IspCommand(Isp, &Scene, sizeof(Scene), sizeof(Scene));
    }
    if (NT_SUCCESS(Status))
        Status = IspChannelValue(Isp, CISP_CMD_APPLE_CH_AE_METERING_MODE_SET, CISP_AE_METERING_MODE);
    if (NT_SUCCESS(Status))
        Status = IspChannelValue(Isp, CISP_CMD_CH_AE_STABILITY_SET, CISP_AE_STABILITY);
    if (NT_SUCCESS(Status))
        Status = IspChannelValue(Isp, CISP_CMD_CH_AE_STABILITY_TO_STABLE_SET, CISP_AE_STABILITY_TO_STABLE);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Sif, sizeof(Sif));
        Sif.Opcode = ISP_OPCODE(CISP_CMD_CH_SIF_PIXEL_FORMAT_SET);
        Sif.Channel = ISP_CAMERA_CHANNEL;
        Sif.Format = CISP_SIF_FORMAT;
        Sif.Type = CISP_SIF_TYPE;
        Status = IspCommand(Isp, &Sif, sizeof(Sif), 0);
    }
    if (NT_SUCCESS(Status))
        Status = IspChannelValue(Isp, CISP_CMD_CH_AE_FRAME_RATE_MAX_SET, CISP_FRAME_RATE_MAX);
    if (NT_SUCCESS(Status))
        Status = IspChannelValue(Isp, CISP_CMD_CH_AE_FRAME_RATE_MIN_SET, CISP_FRAME_RATE_MIN);
    if (NT_SUCCESS(Status))
    {
        Temporal.Opcode = ISP_OPCODE(CISP_CMD_APPLE_CH_TEMPORAL_FILTER_START);
        Temporal.Channel = ISP_CAMERA_CHANNEL;
        Temporal.Enable = 1;
        Temporal.Filter = Isp->TemporalFilter;
        Status = IspCommand(Isp, &Temporal, sizeof(Temporal), 0);
    }
    if (NT_SUCCESS(Status))
        Status = IspChannelCommand(Isp, CISP_CMD_APPLE_CH_MOTION_HISTORY_START);
    if (NT_SUCCESS(Status))
        Status = IspChannelCommand(Isp, CISP_CMD_APPLE_CH_TEMPORAL_FILTER_ENABLE);
    if (NT_SUCCESS(Status))
        Status = IspPoolConfig(Isp, CISP_POOL_TYPE_META);
    if (NT_SUCCESS(Status))
        Status = IspPoolConfig(Isp, CISP_POOL_TYPE_META_CAPTURE);
    DbgPrint("appleisp: capture configured for %lux%lu NV12 from sensor mode %lu, crop %lu,%lu %lux%lu: %lx\n",
             Preset->OutputWidth, Preset->OutputHeight, Preset->ConfigIndex, Preset->CropX, Preset->CropY,
             Preset->CropWidth, Preset->CropHeight, Status);
    return Status;
}

NTSTATUS
IspStartCapture(
    _In_ PISP_DEVICE Isp)
{
    NTSTATUS Status;

    ASSERT(Isp->Streaming && Isp->StreamPreset);
    Status = IspSubmitBuffers(Isp);
    if (NT_SUCCESS(Status))
        Status = IspChannelCommand(Isp, CISP_CMD_CH_START);
    DbgPrint("appleisp: capture start on channel %lu: %lx\n", ISP_CAMERA_CHANNEL, Status);
    return Status;
}

VOID
IspStopCapture(
    _In_ PISP_DEVICE Isp)
{
    NTSTATUS Stop, Return;

    ASSERT(!Isp->Streaming);
    Stop = IspChannelCommand(Isp, CISP_CMD_CH_STOP);
    Return = IspChannelCommand(Isp, CISP_CMD_CH_BUFFER_RETURN);
    DbgPrint("appleisp: capture stop: %lx, buffers returned: %lx\n", Stop, Return);
}

NTSTATUS
IspSubmitBuffers(
    _In_ PISP_DEVICE Isp)
{
    BOOLEAN FrameTaken[ISP_FRAME_COUNT], MetaTaken[ISP_META_COUNT];
    PISP_BUFFER_ENTRY Entry;
    PISP_BUFFER_LIST List;
    PISP_PRESET Preset;
    ULONG Index, Count = 0, Frames = 0, Bytes;
    KIRQL Irql;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeWaitForSingleObject(&Isp->CommandLock, Executive, KernelMode, FALSE, NULL);
    Preset = Isp->StreamPreset;
    if (!Isp->Streaming || Isp->FirmwareState != IspFirmwareRunning || !Isp->BufferToFirmware || !Isp->Command ||
        !Preset)
    {
        KeReleaseMutex(&Isp->CommandLock, FALSE);
        return STATUS_SUCCESS;
    }
    RtlZeroMemory(FrameTaken, sizeof(FrameTaken));
    RtlZeroMemory(MetaTaken, sizeof(MetaTaken));
    KeAcquireSpinLock(&Isp->FrameLock, &Irql);
    for (Index = 0; Index < ISP_FRAME_COUNT; ++Index)
    {
        if (Isp->Frames[Index].Surface && Isp->Frames[Index].State == IspFrameFree)
        {
            Isp->Frames[Index].State = IspFrameSubmitted;
            FrameTaken[Index] = TRUE;
            ++Frames;
        }
    }
    for (Index = 0; Frames && Index < ISP_META_COUNT; ++Index)
    {
        if (Isp->Meta[Index].Surface && !Isp->Meta[Index].Submitted)
        {
            Isp->Meta[Index].Submitted = TRUE;
            MetaTaken[Index] = TRUE;
        }
    }
    KeReleaseSpinLock(&Isp->FrameLock, Irql);
    if (!Frames)
    {
        KeReleaseMutex(&Isp->CommandLock, FALSE);
        return STATUS_SUCCESS;
    }

    List = (PISP_BUFFER_LIST)Isp->Command;
    for (Index = 0; Index < ISP_META_COUNT; ++Index)
    {
        if (!MetaTaken[Index])
            continue;
        Entry = &List->Entry[Count++];
        RtlZeroMemory(Entry, sizeof(*Entry));
        Entry->Iova[0] = Isp->Meta[Index].Surface->Iova;
        Entry->Flags[0] = ISP_BUFFER_PLANE_FLAG;
        Entry->PlaneCount = 1;
        Entry->PoolType = CISP_POOL_TYPE_META;
    }
    for (Index = 0; Index < ISP_FRAME_COUNT; ++Index)
    {
        if (!FrameTaken[Index])
            continue;
        Entry = &List->Entry[Count++];
        RtlZeroMemory(Entry, sizeof(*Entry));
        Entry->Iova[0] = Isp->Frames[Index].Surface->Iova;
        Entry->Iova[1] = Isp->Frames[Index].Surface->Iova + Preset->LumaSize;
        Entry->Flags[0] = ISP_BUFFER_PLANE_FLAG;
        Entry->Flags[1] = ISP_BUFFER_PLANE_FLAG;
        Entry->PlaneCount = 2;
        Entry->PoolType = CISP_POOL_TYPE_RENDERED_SCL1;
    }
    List->Type = ISP_BUFFER_LIST_TYPE;
    List->Count = Count;
    Bytes = FIELD_OFFSET(ISP_BUFFER_LIST, Entry) + Count * sizeof(ISP_BUFFER_ENTRY);
    ASSERT(Bytes <= Isp->CommandSpace);
    RtlZeroMemory(&Isp->BufferToFirmware->Request, sizeof(Isp->BufferToFirmware->Request));
    Isp->BufferToFirmware->Request.Argument[0] = Isp->CommandIova;
    Isp->BufferToFirmware->Request.Argument[1] = max(Bytes, ISP_BUFFER_EXCHANGE_SIZE);
    Isp->BufferToFirmware->Request.Argument[2] = ISP_BUFFER_FLAG_COMMAND;
    Status = IspSend(Isp, Isp->BufferToFirmware, ISP_BUFFER_TIMEOUT_MS);
    if (!NT_SUCCESS(Status))
    {
        KeAcquireSpinLock(&Isp->FrameLock, &Irql);
        for (Index = 0; Index < ISP_FRAME_COUNT; ++Index)
        {
            if (FrameTaken[Index] && Isp->Frames[Index].State == IspFrameSubmitted)
                Isp->Frames[Index].State = IspFrameFree;
        }
        for (Index = 0; Index < ISP_META_COUNT; ++Index)
        {
            if (MetaTaken[Index])
                Isp->Meta[Index].Submitted = FALSE;
        }
        KeReleaseSpinLock(&Isp->FrameLock, Irql);
        DbgPrint("appleisp: %lu frame and %lu total buffers not taken by the firmware: %lx\n", Frames, Count, Status);
    }
    KeReleaseMutex(&Isp->CommandLock, FALSE);
    return Status;
}

static
VOID
IspFrameReadyLocked(
    _In_ PISP_DEVICE Isp,
    _In_ PISP_FRAME Frame)
{
    ULONG Index;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(Frame->State == IspFrameSubmitted);
    for (Index = 0; Index < ISP_FRAME_COUNT; ++Index)
    {
        if (Isp->Frames[Index].State == IspFrameReady)
        {
            Isp->Frames[Index].State = IspFrameFree;
            Isp->FramesDropped++;
            Isp->Discontinuity = TRUE;
        }
    }
    Frame->State = IspFrameReady;
    Frame->Time = KeQueryInterruptTime();
}

NTSTATUS
IspHandleBufferReturn(
    _In_ PISP_DEVICE Isp,
    _In_ PISP_CHANNEL Channel)
{
    PISP_MESSAGE Request = &Channel->Request;
    BOOLEAN Rendered = Request->Argument[2] == ISP_BUFFER_FLAG_RENDER, Ready = FALSE;
    ISP_BUFFER_ENTRY Entry;
    PISP_BUFFER_LIST List;
    PISP_FRAME Frame;
    PKSPIN Pin = NULL;
    ULONG64 Count, Index;
    ULONG Slot;
    KIRQL Irql;

    if (Request->Argument[1] < FIELD_OFFSET(ISP_BUFFER_LIST, Entry))
        return STATUS_INVALID_BUFFER_SIZE;
    List = IspTranslate(Isp->BufferListSurface, Request->Argument[0], Request->Argument[1]);
    if (!List)
        return STATUS_INVALID_ADDRESS;
    Count = List->Count;
    if (Count > (Request->Argument[1] - FIELD_OFFSET(ISP_BUFFER_LIST, Entry)) / sizeof(ISP_BUFFER_ENTRY))
        return STATUS_INVALID_BUFFER_SIZE;
    KeAcquireSpinLock(&Isp->FrameLock, &Irql);
    for (Index = 0; Index < Count; ++Index)
    {
        RtlCopyMemory(&Entry, &List->Entry[Index], sizeof(Entry));
        if (Entry.PoolType == CISP_POOL_TYPE_META)
        {
            /* TODO: frame metadata (exposure, faces) is recycled unread */
            for (Slot = 0; Slot < ISP_META_COUNT; ++Slot)
            {
                if (Isp->Meta[Slot].Surface && (ULONG)Isp->Meta[Slot].Surface->Iova == (ULONG)Entry.Iova[0])
                {
                    ASSERT(Isp->Meta[Slot].Submitted);
                    Isp->Meta[Slot].Submitted = FALSE;
                }
            }
            continue;
        }
        for (Slot = 0; Slot < ISP_FRAME_COUNT; ++Slot)
        {
            Frame = &Isp->Frames[Slot];
            if (!Frame->Surface || Frame->State != IspFrameSubmitted ||
                (ULONG)Frame->Surface->Iova != (ULONG)Entry.Iova[0])
            {
                continue;
            }
            if (Rendered)
            {
                IspFrameReadyLocked(Isp, Frame);
                Ready = TRUE;
            }
            else
            {
                Frame->State = IspFrameFree;
                Isp->FramesDropped++;
                Isp->Discontinuity = TRUE;
            }
        }
    }
    if (Ready && Isp->Streaming)
        Pin = Isp->Pin;
    KeReleaseSpinLock(&Isp->FrameLock, Irql);
    if (Pin)
        KsPinAttemptProcessing(Pin, TRUE);
    KeSetEvent(&Isp->WorkEvent, IO_NO_INCREMENT, FALSE);
    RtlZeroMemory(&Channel->Reply, sizeof(Channel->Reply));
    Channel->Reply.Argument[0] = Request->Argument[0] | ISP_IPC_FLAG_ACK;
    Channel->Reply.Argument[2] = ISP_BUFFER_FLAG_ACK;
    return STATUS_SUCCESS;
}
