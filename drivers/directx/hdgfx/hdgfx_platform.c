/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Display state readout, VBT and panel backlight on the softgpu engine
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "softgpu.h"
#include "core/hdgfx_core.h"
#include "core/hdgfx_regs.h"

#define HDGFX_PCI_BAR0              0x10UL
#define HDGFX_PCI_ASLS              0xFCUL
#define HDGFX_MMIO_SIZE             0x100000UL
#define HDGFX_OPREGION_SIZE         0x2000UL
#define HDGFX_BLC_LEVEL_COUNT       100UL
#define HDGFX_VBT_MINIMUM_LIMIT     64UL
#define HDGFX_VBT_MINIMUM_SCALE     255UL
#define HDGFX_VBT_BACKLIGHT_PWM     2UL
#define HDGFX_TAG                   'xfDH'

typedef struct _HDGFX_ADAPTER
{
    PUCHAR Registers;
    const HDGFX_GENERATION *Generation;
    HDGFX_VBT Vbt;
    BOOLEAN VbtPresent;
    ULONG BacklightMaximum;
    ULONG BacklightMinimum;
    EX_RUNDOWN_REF Rundown;
} HDGFX_ADAPTER, *PHDGFX_ADAPTER;

static const GUID HdgfxGuidBrightness =
    { 0xFDE5BBA4, 0xB3F9, 0x46FB, { 0xBD, 0xAA, 0x07, 0x28, 0xCE, 0x31, 0x00, 0xB4 } };

static uint32_t
HdgfxReadRegister(
    _In_ void *Context,
    _In_ uint32_t Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Context + Offset));
}

static void
HdgfxWriteRegister(
    _In_ void *Context,
    _In_ uint32_t Offset,
    _In_ uint32_t Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Context + Offset), Value);
}

static void
HdgfxStall(
    _In_ void *Context,
    _In_ uint32_t Microseconds)
{
    UNREFERENCED_PARAMETER(Context);
    KeStallExecutionProcessor(Microseconds);
}

static NTSTATUS
HdgfxReadConfig(
    _In_ PSOFTGPU_DEVICE Device,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Size)
{
    ULONG BytesRead = 0;
    NTSTATUS Status;

    if (Device->DxgkInterface.DxgkCbReadDeviceSpace == NULL)
        return STATUS_NOT_SUPPORTED;
    Status = Device->DxgkInterface.DxgkCbReadDeviceSpace(Device->DxgkInterface.DeviceHandle,
                                                         DXGK_WHICHSPACE_CONFIG,
                                                         Buffer,
                                                         Offset,
                                                         Size,
                                                         &BytesRead);
    if (NT_SUCCESS(Status) && BytesRead != Size)
        Status = STATUS_DEVICE_DATA_ERROR;
    return Status;
}

static BOOLEAN
HdgfxResourceOwned(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ ULONGLONG Address,
    _In_ ULONG Size)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    DXGK_DEVICE_INFO DeviceInfo;
    ULONG FullIndex;
    ULONG Index;

    if (Device->DxgkInterface.DxgkCbGetDeviceInformation == NULL ||
        !NT_SUCCESS(Device->DxgkInterface.DxgkCbGetDeviceInformation(Device->DxgkInterface.DeviceHandle,
                                                                     &DeviceInfo)) ||
        DeviceInfo.TranslatedResourceList == NULL)
    {
        return FALSE;
    }

    Full = DeviceInfo.TranslatedResourceList->List;
    for (FullIndex = 0; FullIndex < DeviceInfo.TranslatedResourceList->Count; FullIndex++)
    {
        for (Index = 0; Index < Full->PartialResourceList.Count; Index++)
        {
            Descriptor = &Full->PartialResourceList.PartialDescriptors[Index];
            if (Descriptor->Type == CmResourceTypeMemory &&
                (ULONGLONG)Descriptor->u.Memory.Start.QuadPart <= Address &&
                Address + Size <= (ULONGLONG)Descriptor->u.Memory.Start.QuadPart + Descriptor->u.Memory.Length)
            {
                return TRUE;
            }
        }
        Full = (PCM_FULL_RESOURCE_DESCRIPTOR)&Full->PartialResourceList.PartialDescriptors[Index];
    }
    return FALSE;
}

static VOID
HdgfxReadVbt(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    PHYSICAL_ADDRESS Physical;
    MEMORY_CACHING_TYPE CacheType;
    const uint8_t *Vbt;
    size_t VbtSize;
    PUCHAR OpRegion;
    ULONG Asls = 0;

    Adapter->VbtPresent = FALSE;
    if (!NT_SUCCESS(HdgfxReadConfig(Device, &Asls, HDGFX_PCI_ASLS, sizeof(Asls))) || Asls == 0)
    {
        DPRINT1("No OpRegion (ASLS is 0)\n");
        return;
    }

    Physical.QuadPart = Asls;
    if (!NT_SUCCESS(HalGetMemoryCachingRequirements(Physical, HDGFX_OPREGION_SIZE, &CacheType)))
        CacheType = MmNonCached;
    OpRegion = MmMapIoSpace(Physical, HDGFX_OPREGION_SIZE, CacheType);
    if (OpRegion == NULL)
        return;

    if (HdgfxOpRegionFindVbt(OpRegion, HDGFX_OPREGION_SIZE, &Vbt, &VbtSize) &&
        HdgfxVbtParse(Vbt, VbtSize, &Adapter->Vbt))
    {
        Adapter->VbtPresent = TRUE;
    }
    MmUnmapIoSpace(OpRegion, HDGFX_OPREGION_SIZE);

    if (!Adapter->VbtPresent)
    {
        DPRINT1("OpRegion at 0x%lx has no usable VBT\n", Asls);
        return;
    }
    DPRINT1("VBT '%s' BDB %lu, LVDS SSC %u/%u, panel type %lu\n",
            Adapter->Vbt.Signature,
            Adapter->Vbt.BdbVersion,
            Adapter->Vbt.LvdsSscEnabled,
            Adapter->Vbt.LvdsSscFrequencyBit,
            Adapter->Vbt.Panel.PanelType);
    if (Adapter->Vbt.Panel.Present)
    {
        const HDGFX_TIMINGS *Timings = &Adapter->Vbt.Panel.Timings;

        DPRINT1("VBT panel %lu kHz %lu %lu %lu %lu %lu %lu %lu %lu %chsync %cvsync %lux%lu mm\n",
                Adapter->Vbt.Panel.DotKHz,
                Timings->HActive, Timings->HSyncStart, Timings->HSyncEnd, Timings->HTotal,
                Timings->VActive, Timings->VSyncStart, Timings->VSyncEnd, Timings->VTotal,
                Adapter->Vbt.Panel.HSyncPositive ? '+' : '-',
                Adapter->Vbt.Panel.VSyncPositive ? '+' : '-',
                Adapter->Vbt.Panel.WidthMm,
                Adapter->Vbt.Panel.HeightMm);
    }
    if (Adapter->Vbt.Panel.BacklightPresent)
    {
        DPRINT1("VBT backlight type %lu, %lu Hz, minimum %lu, active %s\n",
                Adapter->Vbt.Panel.BacklightType,
                Adapter->Vbt.Panel.BacklightPwmHz,
                Adapter->Vbt.Panel.BacklightMinimum,
                Adapter->Vbt.Panel.BacklightActiveLow ? "low" : "high");
    }
}

static VOID
HdgfxLogTimings(
    _In_ PCSTR Name,
    _In_ ULONG Index,
    _In_ const HDGFX_TIMINGS *Timings)
{
    DPRINT1("%s %c: %lu %lu %lu %lu (blank %lu %lu) %lu %lu %lu %lu (blank %lu %lu)\n",
            Name, 'A' + Index,
            Timings->HActive, Timings->HSyncStart, Timings->HSyncEnd, Timings->HTotal,
            Timings->HBlankStart, Timings->HBlankEnd,
            Timings->VActive, Timings->VSyncStart, Timings->VSyncEnd, Timings->VTotal,
            Timings->VBlankStart, Timings->VBlankEnd);
}

static VOID
HdgfxLogDisplayState(
    _In_ const HDGFX_DISPLAY_STATE *State)
{
    ULONG Index;

    for (Index = 0; Index < State->PipeCount; Index++)
    {
        const HDGFX_PIPE *Pipe = &State->Pipes[Index];

        DPRINT1("Pipe %c: enable %u active %u bpc %lu dither %u interlace %lu source %lux%lu\n",
                'A' + Index, Pipe->Enabled, Pipe->Active, Pipe->Bpc, Pipe->Dither,
                Pipe->InterlaceMode, Pipe->SourceWidth, Pipe->SourceHeight);
        if (!Pipe->Enabled)
            continue;
        HdgfxLogTimings("Pipe", Index, &Pipe->Timings);
        DPRINT1("Pipe %c: FDI TU %lu data %lu/%lu link %lu/%lu, fitter %u %lu,%lu %lux%lu\n",
                'A' + Index, Pipe->Fdi.TuSize, Pipe->Fdi.DataM, Pipe->Fdi.DataN,
                Pipe->Fdi.LinkM, Pipe->Fdi.LinkN, Pipe->PanelFitter.Enabled,
                Pipe->PanelFitter.X, Pipe->PanelFitter.Y,
                Pipe->PanelFitter.Width, Pipe->PanelFitter.Height);
        DPRINT1("Plane %c: enable %u format %lu x-tiled %u stride %lu surface 0x%08lx offset %lu tile %lu,%lu\n",
                'A' + Index, Pipe->Primary.Enabled, Pipe->Primary.Format, Pipe->Primary.XTiled,
                Pipe->Primary.Stride, Pipe->Primary.Surface, Pipe->Primary.LinearOffset,
                Pipe->Primary.TileX, Pipe->Primary.TileY);
    }

    for (Index = 0; Index < State->TranscoderCount; Index++)
    {
        const HDGFX_TRANSCODER *Transcoder = &State->Transcoders[Index];

        DPRINT1("Transcoder %c: enable %u active %u DPLL %c, FDI RX enable %u PLL %u training %lu lanes %lu bpc %lu\n",
                'A' + Index, Transcoder->Enabled, Transcoder->Active, 'A' + Transcoder->Dpll,
                Transcoder->FdiRx.Enabled, Transcoder->FdiRx.PllEnabled, Transcoder->FdiRx.Training,
                Transcoder->FdiRx.Lanes, Transcoder->FdiRx.Bpc);
        if (Transcoder->Enabled)
            HdgfxLogTimings("Transcoder", Index, &Transcoder->Timings);
    }

    for (Index = 0; Index < State->DpllCount; Index++)
    {
        const HDGFX_DPLL *Dpll = &State->Dplls[Index];

        DPRINT1("DPLL %c: enable %u mode %u reference %lu (%lu kHz) N %lu M1 %lu M2 %lu P1 %lu P2 %lu, VCO %lu kHz, dot %lu kHz\n",
                'A' + Index, Dpll->Enabled, Dpll->Mode, Dpll->Reference, Dpll->ReferenceKHz,
                Dpll->N, Dpll->M1, Dpll->M2, Dpll->P1, Dpll->P2, Dpll->VcoKHz, Dpll->DotKHz);
    }

    DPRINT1("LVDS: enable %u detected %u transcoder %c dual %u bpc %lu %chsync %cvsync\n",
            State->Lvds.Enabled, State->Lvds.Detected, 'A' + State->Lvds.Transcoder,
            State->Lvds.DualChannel, State->Lvds.Bpc,
            State->Lvds.HSyncActiveLow ? '-' : '+', State->Lvds.VSyncActiveLow ? '-' : '+');
    DPRINT1("Panel power: on %u ready %u sequence %lu target %u backlight %u, T1+T2 %lu T5 %lu T3 %lu Tx %lu T4 %lu us\n",
            State->PanelPower.On, State->PanelPower.Ready, State->PanelPower.Sequence,
            State->PanelPower.TargetOn, State->PanelPower.BacklightEnabled,
            State->PanelPower.PowerUpUs, State->PanelPower.BacklightOnUs,
            State->PanelPower.PowerDownUs, State->PanelPower.BacklightOffUs,
            State->PanelPower.PowerCycleUs);
    DPRINT1("Backlight: CPU %u pipe %c duty %lu, PCH %u active %s period %lu, %lu Hz (rawclk %lu kHz)\n",
            State->Backlight.CpuEnabled, 'A' + State->Backlight.CpuPipe, State->Backlight.Duty,
            State->Backlight.PchEnabled, State->Backlight.ActiveLow ? "low" : "high",
            State->Backlight.Period, State->Backlight.FrequencyHz, State->RawClockKHz);
}

static VOID
HdgfxReadPanelEdid(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ const HDGFX_MMIO *Mmio)
{
    UCHAR Edid[HDGFX_EDID_SIZE];
    HDGFX_GMBUS_STATUS Status;
    HDGFX_EDID Parsed;
    ULONG Index;

    Status = HdgfxGmbusRead(Mmio, Adapter->Generation->PanelDdcPins, HDGFX_DDC_ADDRESS, 0, Edid, sizeof(Edid));
    if (Status != HdgfxGmbusSuccess)
    {
        DPRINT1("Panel EDID: GMBUS pins %lu: %s\n",
                Adapter->Generation->PanelDdcPins, HdgfxGmbusStatusName(Status));
        return;
    }

    for (Index = 0; Index < sizeof(Edid); Index += 16)
    {
        DPRINT1("EDID %02lx: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                Index,
                Edid[Index + 0], Edid[Index + 1], Edid[Index + 2], Edid[Index + 3],
                Edid[Index + 4], Edid[Index + 5], Edid[Index + 6], Edid[Index + 7],
                Edid[Index + 8], Edid[Index + 9], Edid[Index + 10], Edid[Index + 11],
                Edid[Index + 12], Edid[Index + 13], Edid[Index + 14], Edid[Index + 15]);
    }
    if (!HdgfxEdidParse(Edid, &Parsed))
    {
        DPRINT1("Panel EDID: bad header or checksum\n");
        return;
    }

    DPRINT1("Panel EDID %s %04lx serial %08lx, version %lu.%lu, %lux%lu cm, %lu extensions\n",
            Parsed.Manufacturer, Parsed.ProductCode, Parsed.SerialNumber,
            Parsed.Version, Parsed.Revision, Parsed.WidthCm, Parsed.HeightCm, Parsed.Extensions);
    for (Index = 0; Index < Parsed.DtdCount; Index++)
    {
        const HDGFX_DTD *Dtd = &Parsed.Dtds[Index];

        DPRINT1("Panel EDID DTD %lu: %lu kHz %lu %lu %lu %lu %lu %lu %lu %lu %chsync %cvsync %lux%lu mm\n",
                Index, Dtd->DotKHz,
                Dtd->Timings.HActive, Dtd->Timings.HSyncStart, Dtd->Timings.HSyncEnd, Dtd->Timings.HTotal,
                Dtd->Timings.VActive, Dtd->Timings.VSyncStart, Dtd->Timings.VSyncEnd, Dtd->Timings.VTotal,
                Dtd->HSyncPositive ? '+' : '-', Dtd->VSyncPositive ? '+' : '-',
                Dtd->WidthMm, Dtd->HeightMm);
    }
    DPRINT1("Panel EDID %s the firmware copy (%lu bytes)\n",
            Device->EdidLength == sizeof(Edid) && RtlEqualMemory(Device->Edid, Edid, sizeof(Edid)) ?
                "matches" : "differs from",
            Device->EdidLength);
}

static VOID
HdgfxDisableFirmwareBrightness(
    _In_ PSOFTGPU_DEVICE Device)
{
    ULONG InputStorage[(FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) +
                        ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) + sizeof(ULONG) - 1) / sizeof(ULONG)];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX Input = (PACPI_EVAL_INPUT_BUFFER_COMPLEX)InputStorage;

    if (Device->DxgkInterface.DxgkCbEvalAcpiMethod == NULL)
        return;

    RtlZeroMemory(InputStorage, sizeof(InputStorage));
    Input->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    Input->MethodNameAsUlong = ACPI_METHOD_DISPLAY_DOS;
    Input->Size = sizeof(InputStorage) - FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
    Input->ArgumentCount = 1;
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Input->Argument,
                                     ACPI_ARG_ENABLE_SWITCH_EVENT | ACPI_ARG_DISABLE_AUTO_LCD_BRIGHTNESS);
    (VOID)Device->DxgkInterface.DxgkCbEvalAcpiMethod(Device->DxgkInterface.DeviceHandle,
                                                     DISPLAY_ADAPTER_HW_ID,
                                                     Input,
                                                     sizeof(InputStorage),
                                                     NULL,
                                                     0);
}

static VOID
HdgfxStartBacklight(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ const HDGFX_DISPLAY_STATE *State)
{
    ULONG Minimum = 0;

    Adapter->BacklightMaximum = 0;
    if (!State->Backlight.CpuEnabled || !State->Backlight.PchEnabled || State->Backlight.Period == 0)
    {
        DPRINT1("The panel backlight PWM is not enabled\n");
        return;
    }

    if (Adapter->VbtPresent &&
        Adapter->Vbt.Panel.BacklightPresent &&
        Adapter->Vbt.Panel.BacklightType == HDGFX_VBT_BACKLIGHT_PWM)
    {
        Minimum = min(Adapter->Vbt.Panel.BacklightMinimum, HDGFX_VBT_MINIMUM_LIMIT);
    }
    Adapter->BacklightMaximum = State->Backlight.Period;
    Adapter->BacklightMinimum = (ULONG)(((ULONGLONG)Minimum * Adapter->BacklightMaximum +
                                         HDGFX_VBT_MINIMUM_SCALE / 2) / HDGFX_VBT_MINIMUM_SCALE);
    DPRINT1("Panel backlight period %lu, minimum %lu\n",
            Adapter->BacklightMaximum, Adapter->BacklightMinimum);
    HdgfxDisableFirmwareBrightness(Device);
}

VOID
SoftGpuPlatformAdapterStarted(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PHDGFX_ADAPTER Adapter = Device->PlatformServicesContext;
    HDGFX_DISPLAY_STATE *State;
    PHYSICAL_ADDRESS Address;
    HDGFX_MMIO Mmio;
    ULONGLONG Bar = 0;
    USHORT Ids[2] = { 0, 0 };

    PAGED_CODE();

    if (Adapter == NULL)
    {
        Adapter = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Adapter), HDGFX_TAG);
        if (Adapter == NULL)
            return;
        ExInitializeRundownProtection(&Adapter->Rundown);
        Device->PlatformServicesContext = Adapter;
    }
    else
    {
        ExReInitializeRundownProtection(&Adapter->Rundown);
    }

    if (!NT_SUCCESS(HdgfxReadConfig(Device, Ids, 0, sizeof(Ids))))
        return;
    Adapter->Generation = HdgfxFindGeneration(Ids[0], Ids[1]);
    if (Adapter->Generation == NULL)
    {
        DPRINT1("%04x:%04x is not a supported device\n", Ids[0], Ids[1]);
        return;
    }

    if (!NT_SUCCESS(HdgfxReadConfig(Device, &Bar, HDGFX_PCI_BAR0, sizeof(Bar))))
        return;
    Address.QuadPart = (LONGLONG)(Bar & ~0xFULL);
    if (!HdgfxResourceOwned(Device, (ULONGLONG)Address.QuadPart, HDGFX_MMIO_SIZE))
    {
        DPRINT1("BAR0 0x%I64x is not in the adapter's resources\n", (ULONGLONG)Address.QuadPart);
        return;
    }
    Adapter->Registers = MmMapIoSpace(Address, HDGFX_MMIO_SIZE, MmNonCached);
    if (Adapter->Registers == NULL)
        return;

    DPRINT1("%s (Gen %lu) %04x:%04x, registers at 0x%I64x\n",
            Adapter->Generation->Name, Adapter->Generation->Gen, Ids[0], Ids[1],
            (ULONGLONG)Address.QuadPart);
    HdgfxReadVbt(Device, Adapter);

    State = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*State), HDGFX_TAG);
    if (State == NULL)
        return;
    Mmio.Read = HdgfxReadRegister;
    Mmio.Context = Adapter->Registers;
    Mmio.Write = HdgfxWriteRegister;
    Mmio.Stall = HdgfxStall;
    Adapter->Generation->ReadDisplayState(&Mmio, State);
    HdgfxLogDisplayState(State);
    HdgfxReadPanelEdid(Device, Adapter, &Mmio);
    HdgfxStartBacklight(Device, Adapter, State);
    ExFreePoolWithTag(State, HDGFX_TAG);
}

VOID
SoftGpuPlatformAdapterStopping(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PHDGFX_ADAPTER Adapter = Device->PlatformServicesContext;

    PAGED_CODE();

    if (Adapter == NULL)
        return;

    ExWaitForRundownProtectionRelease(&Adapter->Rundown);
    if (Adapter->Registers == NULL)
        return;

    MmUnmapIoSpace(Adapter->Registers, HDGFX_MMIO_SIZE);
    Adapter->Registers = NULL;
    Adapter->BacklightMaximum = 0;
}

VOID
SoftGpuPlatformAdapterRemoved(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PAGED_CODE();

    if (Device->PlatformServicesContext == NULL)
        return;

    ExFreePoolWithTag(Device->PlatformServicesContext, HDGFX_TAG);
    Device->PlatformServicesContext = NULL;
}

static PHDGFX_ADAPTER
HdgfxBacklightAcquire(
    _In_ PVOID Context)
{
    PHDGFX_ADAPTER Adapter = ((PSOFTGPU_DEVICE)Context)->PlatformServicesContext;

    if (Adapter == NULL || !ExAcquireRundownProtection(&Adapter->Rundown))
        return NULL;
    if (Adapter->Registers == NULL || Adapter->BacklightMaximum == 0)
    {
        ExReleaseRundownProtection(&Adapter->Rundown);
        return NULL;
    }
    return Adapter;
}

static PULONG
HdgfxBacklightDutyRegister(
    _In_ PHDGFX_ADAPTER Adapter)
{
    return (PULONG)(Adapter->Registers + HDGFX_REG(BLC_PWM_CPU_CTL, 0));
}

static VOID
NTAPI
HdgfxBacklightReference(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
}

static NTSTATUS
HdgfxBacklightGetPossible(
    _In_ PVOID Context,
    _In_ ULONG BufferSize,
    _Out_ PUCHAR LevelCount,
    _Out_writes_bytes_to_(BufferSize, *LevelCount) PUCHAR BrightnessLevels)
{
    ULONG Index;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Context);

    *LevelCount = 0;
    if (BufferSize < HDGFX_BLC_LEVEL_COUNT)
        return STATUS_BUFFER_TOO_SMALL;

    for (Index = 0; Index < HDGFX_BLC_LEVEL_COUNT; Index++)
        BrightnessLevels[Index] = (UCHAR)(Index + 1);
    *LevelCount = (UCHAR)HDGFX_BLC_LEVEL_COUNT;
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxBacklightSet(
    _In_ PVOID Context,
    _In_ UCHAR Brightness)
{
    PHDGFX_ADAPTER Adapter;
    ULONG Duty;

    PAGED_CODE();

    if (Brightness == 0 || Brightness > HDGFX_BLC_LEVEL_COUNT)
        return STATUS_INVALID_PARAMETER;

    Adapter = HdgfxBacklightAcquire(Context);
    if (Adapter == NULL)
        return STATUS_DEVICE_NOT_READY;

    Duty = Adapter->BacklightMinimum +
           (ULONG)(((ULONGLONG)(Adapter->BacklightMaximum - Adapter->BacklightMinimum) * Brightness +
                    HDGFX_BLC_LEVEL_COUNT / 2) / HDGFX_BLC_LEVEL_COUNT);
    WRITE_REGISTER_ULONG(HdgfxBacklightDutyRegister(Adapter), Duty);
    ExReleaseRundownProtection(&Adapter->Rundown);
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxBacklightGet(
    _In_ PVOID Context,
    _Out_ PUCHAR Brightness)
{
    PHDGFX_ADAPTER Adapter;
    ULONGLONG Level;
    ULONG Range;

    PAGED_CODE();

    *Brightness = 0;
    Adapter = HdgfxBacklightAcquire(Context);
    if (Adapter == NULL)
        return STATUS_DEVICE_NOT_READY;

    Level = HDGFX_GET(READ_REGISTER_ULONG(HdgfxBacklightDutyRegister(Adapter)), BLC_CPU_DUTY);
    Range = Adapter->BacklightMaximum - Adapter->BacklightMinimum;
    Level = Level > Adapter->BacklightMinimum ? Level - Adapter->BacklightMinimum : 0;
    Level = Range ? (Level * HDGFX_BLC_LEVEL_COUNT + Range / 2) / Range : HDGFX_BLC_LEVEL_COUNT;
    *Brightness = (UCHAR)min(max(Level, 1), HDGFX_BLC_LEVEL_COUNT);
    ExReleaseRundownProtection(&Adapter->Rundown);
    return STATUS_SUCCESS;
}

NTSTATUS
SoftGpuPlatformQueryInterface(
    _Inout_ PSOFTGPU_DEVICE Device,
    _Inout_ PQUERY_INTERFACE QueryInterface)
{
    PDXGK_BRIGHTNESS_INTERFACE Interface;
    PHDGFX_ADAPTER Adapter = Device->PlatformServicesContext;

    PAGED_CODE();

    if (!IsEqualGUID(QueryInterface->InterfaceType, &HdgfxGuidBrightness) ||
        QueryInterface->Version != DXGK_BRIGHTNESS_INTERFACE_VERSION_1 ||
        Adapter == NULL ||
        Adapter->Registers == NULL ||
        Adapter->BacklightMaximum == 0)
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (QueryInterface->Size < sizeof(*Interface))
        return STATUS_BUFFER_TOO_SMALL;

    Interface = (PDXGK_BRIGHTNESS_INTERFACE)QueryInterface->Interface;
    Interface->Size = sizeof(*Interface);
    Interface->Version = DXGK_BRIGHTNESS_INTERFACE_VERSION_1;
    Interface->Context = Device;
    Interface->InterfaceReference = HdgfxBacklightReference;
    Interface->InterfaceDereference = HdgfxBacklightReference;
    Interface->GetPossibleBrightness = HdgfxBacklightGetPossible;
    Interface->SetBrightness = HdgfxBacklightSet;
    Interface->GetBrightness = HdgfxBacklightGet;
    return STATUS_SUCCESS;
}
