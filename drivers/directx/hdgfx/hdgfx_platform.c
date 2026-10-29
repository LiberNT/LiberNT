/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LVDS scan-out, display state readout, VBT and panel backlight on the softgpu engine
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx.h"
#include "softgpu_2d_core.h"
#include <pseh/pseh2.h>

#define HDGFX_PCI_BAR0              0x10UL
#define HDGFX_PCI_BAR2              0x18UL
#define HDGFX_PCI_ASLS              0xFCUL
#define HDGFX_MMIO_SIZE             0x100000UL
#define HDGFX_OPREGION_SIZE         0x2000UL
#define HDGFX_BLC_LEVEL_COUNT       100UL
#define HDGFX_VBT_MINIMUM_LIMIT     64UL
#define HDGFX_VBT_MINIMUM_SCALE     255UL
#define HDGFX_VBT_BACKLIGHT_PWM     2UL
#define HDGFX_ACPI_ID_INTERNAL_PANEL 0x0400UL
#define HDGFX_WORKING_SURFACE_COUNT 12UL
#define HDGFX_STRIDE_ALIGNMENT      64UL
#define HDGFX_VBLANK_POLL_US        20UL
#define HDGFX_VBLANK_TIMEOUT_US     50000UL
#define HDGFX_CRT_CHILD_UID         1UL
#define HDGFX_CRT_PIPE              1UL
#define HDGFX_BLT_TEST_PITCH        128UL
#define HDGFX_CURSOR_PAGES          (HDGFX_CURSOR_SIZE * HDGFX_CURSOR_SIZE * 4 / PAGE_SIZE)
#define HDGFX_DISPLAY_PIPES         2UL
#define HDGFX_BLT_TEST_WIDTH        (HDGFX_BLT_TEST_PITCH / 4)
#define HDGFX_BLT_TEST_HEIGHT       16UL
#define HDGFX_RING_TEST_TIMEOUT_US  100000UL
#define HDGFX_BLT_MAX_COORDINATE    0x7FFFUL
#define HDGFX_PRESENT_BATCH         16UL
#define HDGFX_PRESENT_TIMEOUT_US    100000UL

static const GUID HdgfxGuidBrightness =
    { 0xFDE5BBA4, 0xB3F9, 0x46FB, { 0xBD, 0xAA, 0x07, 0x28, 0xCE, 0x31, 0x00, 0xB4 } };

static const USHORT HdgfxScaledModes[][2] =
{
    { 1280, 720 }, { 1024, 768 }, { 800, 600 }, { 640, 480 },
};

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

static void
HdgfxSleep(
    _In_ void *Context,
    _In_ uint32_t Milliseconds)
{
    LARGE_INTEGER Interval;

    UNREFERENCED_PARAMETER(Context);
    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

VOID
HdgfxMmioInit(
    _In_ PVOID Base,
    _Out_ HDGFX_MMIO *Mmio)
{
    Mmio->Read = HdgfxReadRegister;
    Mmio->Context = Base;
    Mmio->Write = HdgfxWriteRegister;
    Mmio->Stall = HdgfxStall;
    Mmio->Sleep = HdgfxSleep;
}

static NTSTATUS
HdgfxReadConfig(
    _In_ PDXGK_INTERFACE Interface,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Size)
{
    ULONG BytesRead = 0;
    NTSTATUS Status;

    if (Interface->DxgkCbReadDeviceSpace == NULL)
        return STATUS_NOT_SUPPORTED;
    Status = Interface->DxgkCbReadDeviceSpace(Interface->DeviceHandle,
                                              DXGK_WHICHSPACE_CONFIG,
                                              Buffer,
                                              Offset,
                                              Size,
                                              &BytesRead);
    if (NT_SUCCESS(Status) && BytesRead != Size)
        Status = STATUS_DEVICE_DATA_ERROR;
    return Status;
}

static PCM_PARTIAL_RESOURCE_DESCRIPTOR
HdgfxFindMemoryResource(
    _In_ PDXGK_INTERFACE Interface,
    _In_ ULONGLONG Address,
    _In_ ULONG Size)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    DXGK_DEVICE_INFO DeviceInfo;
    ULONG FullIndex;
    ULONG Index;

    if (Interface->DxgkCbGetDeviceInformation == NULL ||
        !NT_SUCCESS(Interface->DxgkCbGetDeviceInformation(Interface->DeviceHandle, &DeviceInfo)) ||
        DeviceInfo.TranslatedResourceList == NULL)
    {
        return NULL;
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
                return Descriptor;
            }
        }
        Full = (PCM_FULL_RESOURCE_DESCRIPTOR)&Full->PartialResourceList.PartialDescriptors[Index];
    }
    return NULL;
}

static BOOLEAN
HdgfxResourceOwned(
    _In_ PDXGK_INTERFACE Interface,
    _In_ ULONGLONG Address,
    _In_ ULONG Size)
{
    return HdgfxFindMemoryResource(Interface, Address, Size) != NULL;
}

static PUCHAR
HdgfxMapOpRegion(
    _In_ PDXGK_INTERFACE Interface,
    _Out_ HDGFX_VBT *Vbt,
    _Out_ PBOOLEAN VbtPresent)
{
    PHYSICAL_ADDRESS Physical;
    MEMORY_CACHING_TYPE CacheType;
    const uint8_t *Table;
    size_t TableSize;
    PUCHAR OpRegion;
    ULONG Asls = 0;

    *VbtPresent = FALSE;
    if (!NT_SUCCESS(HdgfxReadConfig(Interface, &Asls, HDGFX_PCI_ASLS, sizeof(Asls))) || Asls == 0)
    {
        DPRINT1("No OpRegion (ASLS is 0)\n");
        return NULL;
    }

    Physical.QuadPart = Asls;
    if (!NT_SUCCESS(HalGetMemoryCachingRequirements(Physical, HDGFX_OPREGION_SIZE, &CacheType)))
        CacheType = MmNonCached;
    OpRegion = MmMapIoSpace(Physical, HDGFX_OPREGION_SIZE, CacheType);
    if (OpRegion == NULL)
        return NULL;

    if (HdgfxOpRegionFindVbt(OpRegion, HDGFX_OPREGION_SIZE, &Table, &TableSize) &&
        HdgfxVbtParse(Table, TableSize, Vbt))
    {
        *VbtPresent = TRUE;
    }
    else
    {
        DPRINT1("OpRegion at 0x%lx has no usable VBT\n", Asls);
    }
    return OpRegion;
}

static VOID
HdgfxReadVbt(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    Adapter->OpRegion = HdgfxMapOpRegion(&Device->DxgkInterface, &Adapter->Vbt, &Adapter->VbtPresent);
    if (!Adapter->VbtPresent)
        return;
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
    DPRINT1("CRT: enable %u transcoder %c %chsync %cvsync; reference SSC %u non-spread %u\n",
            State->Crt.Enabled, 'A' + State->Crt.Transcoder,
            State->Crt.HSyncActiveHigh ? '+' : '-', State->Crt.VSyncActiveHigh ? '+' : '-',
            State->SscReference, State->NonSpreadReference);
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

static BOOLEAN
HdgfxReadEdid(
    _In_ PCSTR Name,
    _In_ ULONG Pins,
    _In_ const HDGFX_MMIO *Mmio,
    _Out_writes_bytes_(HDGFX_EDID_SIZE) PUCHAR Edid,
    _Out_ HDGFX_EDID *Parsed)
{
    HDGFX_GMBUS_STATUS Status;
    ULONG Index;

    Status = HdgfxGmbusRead(Mmio, Pins, HDGFX_DDC_ADDRESS, 0, Edid, HDGFX_EDID_SIZE);
    if (Status != HdgfxGmbusSuccess)
    {
        DPRINT1("%s EDID: GMBUS pins %lu: %s\n", Name, Pins, HdgfxGmbusStatusName(Status));
        return FALSE;
    }

    for (Index = 0; Index < HDGFX_EDID_SIZE; Index += 16)
    {
        DPRINT1("%s EDID %02lx: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                Name, Index,
                Edid[Index + 0], Edid[Index + 1], Edid[Index + 2], Edid[Index + 3],
                Edid[Index + 4], Edid[Index + 5], Edid[Index + 6], Edid[Index + 7],
                Edid[Index + 8], Edid[Index + 9], Edid[Index + 10], Edid[Index + 11],
                Edid[Index + 12], Edid[Index + 13], Edid[Index + 14], Edid[Index + 15]);
    }
    if (!HdgfxEdidParse(Edid, Parsed))
    {
        DPRINT1("%s EDID: bad header or checksum\n", Name);
        return FALSE;
    }

    DPRINT1("%s EDID %s %04lx serial %08lx, version %lu.%lu, %lux%lu cm, %lu extensions\n",
            Name, Parsed->Manufacturer, Parsed->ProductCode, Parsed->SerialNumber,
            Parsed->Version, Parsed->Revision, Parsed->WidthCm, Parsed->HeightCm, Parsed->Extensions);
    for (Index = 0; Index < Parsed->DtdCount; Index++)
    {
        const HDGFX_DTD *Dtd = &Parsed->Dtds[Index];

        DPRINT1("%s EDID DTD %lu: %lu kHz %lu %lu %lu %lu %lu %lu %lu %lu %chsync %cvsync %lux%lu mm\n",
                Name, Index, Dtd->DotKHz,
                Dtd->Timings.HActive, Dtd->Timings.HSyncStart, Dtd->Timings.HSyncEnd, Dtd->Timings.HTotal,
                Dtd->Timings.VActive, Dtd->Timings.VSyncStart, Dtd->Timings.VSyncEnd, Dtd->Timings.VTotal,
                Dtd->HSyncPositive ? '+' : '-', Dtd->VSyncPositive ? '+' : '-',
                Dtd->WidthMm, Dtd->HeightMm);
    }
    return TRUE;
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

static VOID
HdgfxOpRegionIo(
    _In_ PHDGFX_ADAPTER Adapter,
    _Out_ HDGFX_MMIO *Io)
{
    HdgfxMmioInit(Adapter->OpRegion, Io);
}

static PULONG
HdgfxBacklightDutyRegister(
    _In_ PHDGFX_ADAPTER Adapter)
{
    return (PULONG)(Adapter->Registers + HDGFX_REG(BLC_PWM_CPU_CTL, 0));
}

static VOID
HdgfxBacklightWrite(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG Level,
    _In_ ULONG Scale)
{
    ULONG Duty;

    Duty = Adapter->BacklightMinimum +
           (ULONG)(((ULONGLONG)(Adapter->BacklightMaximum - Adapter->BacklightMinimum) * Level +
                    Scale / 2) / Scale);
    WRITE_REGISTER_ULONG(HdgfxBacklightDutyRegister(Adapter), Duty);
}

static UCHAR
HdgfxBacklightRead(
    _In_ PHDGFX_ADAPTER Adapter)
{
    ULONGLONG Level;
    ULONG Range;

    Level = HDGFX_GET(READ_REGISTER_ULONG(HdgfxBacklightDutyRegister(Adapter)), BLC_CPU_DUTY);
    Range = Adapter->BacklightMaximum - Adapter->BacklightMinimum;
    Level = Level > Adapter->BacklightMinimum ? Level - Adapter->BacklightMinimum : 0;
    Level = Range ? (Level * HDGFX_BLC_LEVEL_COUNT + Range / 2) / Range : HDGFX_BLC_LEVEL_COUNT;
    return (UCHAR)min(max(Level, 1), HDGFX_BLC_LEVEL_COUNT);
}

static bool
HdgfxAsleSetBacklight(
    _In_ void *Context,
    _In_ uint32_t Level)
{
    HdgfxBacklightWrite(Context, Level, HDGFX_ASLE_LEVEL_MAX);
    return true;
}

static BOOLEAN
NTAPI
HdgfxInterruptDisconnect(
    _In_opt_ PVOID Context)
{
    ((PHDGFX_ADAPTER)Context)->AsleInterrupt = FALSE;
    ((PHDGFX_ADAPTER)Context)->HotplugInterrupt = FALSE;
    ((PHDGFX_ADAPTER)Context)->RingInterrupt = FALSE;
    return TRUE;
}

static VOID
HdgfxWriteRegister32(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Adapter->Registers + Offset), Value);
}

static ULONG
HdgfxReadRegister32(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Adapter->Registers + Offset));
}

static VOID
HdgfxStartInterrupts(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PHDGFX_ADAPTER Adapter)
{
    const HDGFX_GENERATION *Generation = Adapter->Generation;
    BOOLEAN Asle = Adapter->OpRegion != NULL && Adapter->InternalPanel && Adapter->BacklightMaximum != 0;
    BOOLEAN Hotplug = Generation->CrtHotplugInterrupt != 0 && Device->ExtraOutputCount != 0;
    ULONG Crt = Generation->CrtHotplugInterrupt;
    HDGFX_MMIO Mmio;
    HDGFX_MMIO Io;
    ULONG Mask;

    ULONG Gt = Adapter->RingStarted ? HDGFX_SET(GT_RENDER_USER, 1) : 0;

    Mask = (Asle ? Generation->GseInterrupt : 0) | (Hotplug ? Generation->PchInterrupt : 0);
    if (Mask == 0 && Gt == 0)
        return;

    Adapter->SavedDeimr = HdgfxReadRegister32(Adapter, HDGFX_REG(DEIMR, 0));
    Adapter->SavedDeier = HdgfxReadRegister32(Adapter, HDGFX_REG(DEIER, 0));
    Adapter->InterruptMask = Mask;
    Adapter->AsleInterrupt = Asle;
    if (Gt != 0)
    {
        Adapter->SavedGtimr = HdgfxReadRegister32(Adapter, HDGFX_REG(GTIMR, 0));
        Adapter->SavedGtier = HdgfxReadRegister32(Adapter, HDGFX_REG(GTIER, 0));
        Adapter->GtInterruptMask = Gt;
        Adapter->RingInterrupt = TRUE;
        HdgfxWriteRegister32(Adapter, HDGFX_REG(GTIIR, 0), Gt);
        HdgfxWriteRegister32(Adapter, HDGFX_REG(GTIMR, 0), ~Gt);
        HdgfxWriteRegister32(Adapter, HDGFX_REG(GTIER, 0), Gt);
    }
    if (Hotplug)
    {
        HdgfxMmioInit(Adapter->Registers, &Mmio);
        Adapter->SavedSdeimr = HdgfxReadRegister32(Adapter, HDGFX_REG(SDEIMR, 0));
        Adapter->SavedSdeier = HdgfxReadRegister32(Adapter, HDGFX_REG(SDEIER, 0));
        Adapter->HotplugInterrupt = TRUE;
        Generation->EnableCrtHotplug(&Mmio, true);
        HdgfxWriteRegister32(Adapter, HDGFX_REG(SDEIIR, 0), Crt);
        HdgfxWriteRegister32(Adapter, HDGFX_REG(SDEIMR, 0), ~Crt);
        HdgfxWriteRegister32(Adapter, HDGFX_REG(SDEIER, 0), Crt);
        DPRINT1("CRT hot plug interrupt enabled, SDEIMR 0x%08lx SDEIER 0x%08lx before\n",
                Adapter->SavedSdeimr, Adapter->SavedSdeier);
    }
    HdgfxWriteRegister32(Adapter, HDGFX_REG(DEIIR, 0), Mask);
    HdgfxWriteRegister32(Adapter, HDGFX_REG(DEIMR, 0), ~Mask);
    HdgfxWriteRegister32(Adapter, HDGFX_REG(DEIER, 0), HDGFX_SET(DE_MASTER, 1) | Mask);
    if (!Asle)
        return;

    HdgfxOpRegionIo(Adapter, &Io);
    Adapter->AsleReady = HdgfxOpRegionDriverReady(&Io, HDGFX_ACPI_ID_INTERNAL_PANEL, HdgfxBacklightRead(Adapter));
    DPRINT1("OpRegion ASLE %s, display ID 0x%lx, DEIMR 0x%08lx DEIER 0x%08lx before\n",
            Adapter->AsleReady ? "ready" : "unsupported", HDGFX_ACPI_ID_INTERNAL_PANEL,
            Adapter->SavedDeimr, Adapter->SavedDeier);
}

static VOID
HdgfxStopInterrupts(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    BOOLEAN Ignored;
    HDGFX_MMIO Io;

    if (Adapter->AsleReady)
    {
        HdgfxOpRegionIo(Adapter, &Io);
        HdgfxOpRegionDriverNotReady(&Io);
        Adapter->AsleReady = FALSE;
    }
    if (Adapter->InterruptMask == 0 && Adapter->GtInterruptMask == 0)
        return;

    HdgfxWriteRegister32(Adapter, HDGFX_REG(DEIER, 0), Adapter->SavedDeier);
    HdgfxWriteRegister32(Adapter, HDGFX_REG(DEIMR, 0), Adapter->SavedDeimr);
    HdgfxWriteRegister32(Adapter, HDGFX_REG(DEIIR, 0), Adapter->InterruptMask);
    if (Adapter->GtInterruptMask != 0)
    {
        HdgfxWriteRegister32(Adapter, HDGFX_REG(GTIER, 0), Adapter->SavedGtier);
        HdgfxWriteRegister32(Adapter, HDGFX_REG(GTIMR, 0), Adapter->SavedGtimr);
        HdgfxWriteRegister32(Adapter, HDGFX_REG(GTIIR, 0), Adapter->GtInterruptMask);
        Adapter->GtInterruptMask = 0;
    }
    if (Adapter->HotplugInterrupt)
    {
        HdgfxWriteRegister32(Adapter, HDGFX_REG(SDEIER, 0), Adapter->SavedSdeier);
        HdgfxWriteRegister32(Adapter, HDGFX_REG(SDEIMR, 0), Adapter->SavedSdeimr);
        HdgfxWriteRegister32(Adapter, HDGFX_REG(SDEIIR, 0), Adapter->Generation->CrtHotplugInterrupt);
    }
    Adapter->InterruptMask = 0;
    if (Device->DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
        !NT_SUCCESS(Device->DxgkInterface.DxgkCbSynchronizeExecution(Device->DxgkInterface.DeviceHandle,
                                                                     HdgfxInterruptDisconnect,
                                                                     Adapter,
                                                                     0,
                                                                     &Ignored)))
    {
        Adapter->AsleInterrupt = FALSE;
        Adapter->HotplugInterrupt = FALSE;
        Adapter->RingInterrupt = FALSE;
    }
}

static VOID
HdgfxCrtRefresh(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ BOOLEAN Indicate);

static VOID
NTAPI
HdgfxHotplugWorker(
    _In_ PVOID Context)
{
    PHDGFX_ADAPTER Adapter = Context;

    InterlockedExchange(&Adapter->HotplugQueued, 0);
    HdgfxCrtRefresh(Adapter->Device, TRUE);
    ExReleaseRundownProtection(&Adapter->Rundown);
}

static VOID
HdgfxQueueHotplug(
    _In_ PHDGFX_ADAPTER Adapter)
{
    if (InterlockedCompareExchange(&Adapter->HotplugQueued, 1, 0) != 0)
        return;
    if (!ExAcquireRundownProtection(&Adapter->Rundown))
    {
        InterlockedExchange(&Adapter->HotplugQueued, 0);
        return;
    }
    ExQueueWorkItem(&Adapter->HotplugWork, DelayedWorkQueue);
}

static VOID
HdgfxStopRender(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    HDGFX_MMIO Mmio;
    ULONG Index;

    if (Adapter->RingStarted)
    {
        HdgfxMmioInit(Adapter->Registers, &Mmio);
        if (!HdgfxIlkStopRing(&Mmio, &Adapter->Ring))
        {
            DPRINT1("Render ring busy at stop, head 0x%lx tail 0x%lx; keeping its memory\n",
                    HdgfxIlkRingHead(&Mmio), Adapter->Ring.Tail);
            return;
        }
        Adapter->RingStarted = FALSE;
    }
    if (Adapter->GttWindow != NULL)
    {
        for (Index = 0; Index < HDGFX_RENDER_GTT_PAGES; Index++)
            WRITE_REGISTER_ULONG(&Adapter->GttWindow[Index], Adapter->SavedGtt[Index]);
        READ_REGISTER_ULONG(&Adapter->GttWindow[HDGFX_RENDER_GTT_PAGES - 1]);
        MmUnmapIoSpace(Adapter->GttWindow, HDGFX_RENDER_GTT_PAGES * sizeof(ULONG));
        Adapter->GttWindow = NULL;
        Adapter->DmaBase = Adapter->DmaPhysical;
    }
    if (Adapter->RingMemory != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Adapter->RingMemory, HDGFX_RING_SIZE, MmNonCached);
        Adapter->RingMemory = NULL;
    }
    if (Adapter->StatusPage != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Adapter->StatusPage, PAGE_SIZE, MmCached);
        Adapter->StatusPage = NULL;
    }
    if (Adapter->ScratchPage != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Adapter->ScratchPage, PAGE_SIZE, MmNonCached);
        Adapter->ScratchPage = NULL;
    }
}

static VOID
HdgfxStartDma(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    PHYSICAL_ADDRESS Lowest;
    PHYSICAL_ADDRESS Highest;
    PHYSICAL_ADDRESS Boundary;

    if (Adapter->DmaMemory == NULL)
    {
        Lowest.QuadPart = 0;
        Highest.QuadPart = MAXULONG;
        Boundary.QuadPart = 0;
        Adapter->DmaMemory = MmAllocateContiguousMemorySpecifyCache(HDGFX_DMA_SIZE, Lowest, Highest, Boundary,
                                                                    MmWriteCombined);
        if (Adapter->DmaMemory == NULL)
        {
            DPRINT1("No memory for the %lu-byte DMA segment\n", HDGFX_DMA_SIZE);
            return;
        }
        Adapter->DmaPhysical = (ULONGLONG)MmGetPhysicalAddress(Adapter->DmaMemory).QuadPart;
    }
    RtlZeroMemory(Adapter->DmaMemory, HDGFX_DMA_SIZE);
    Adapter->DmaBase = Adapter->DmaPhysical;
}

static VOID
HdgfxStopDma(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    if (Adapter->DmaMemory == NULL || Adapter->RingStarted)
        return;
    MmFreeContiguousMemorySpecifyCache(Adapter->DmaMemory, HDGFX_DMA_SIZE, MmWriteCombined);
    Adapter->DmaMemory = NULL;
    Adapter->DmaPhysical = 0;
    Adapter->DmaBase = 0;
}

static VOID
HdgfxStartRender(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Aperture;
    PHYSICAL_ADDRESS Lowest;
    PHYSICAL_ADDRESS Highest;
    PHYSICAL_ADDRESS Boundary;
    PHYSICAL_ADDRESS Address;
    ULONGLONG Bar0 = 0;
    ULONGLONG Bar2 = 0;
    HDGFX_MMIO Mmio;
    ULONG GttFirst;
    ULONG Index;

    if (!NT_SUCCESS(HdgfxReadConfig(&Device->DxgkInterface, &Bar0, HDGFX_PCI_BAR0, sizeof(Bar0))) ||
        !NT_SUCCESS(HdgfxReadConfig(&Device->DxgkInterface, &Bar2, HDGFX_PCI_BAR2, sizeof(Bar2))))
    {
        return;
    }
    Bar0 &= ~0xFULL;
    Bar2 &= ~0xFULL;
    Aperture = HdgfxFindMemoryResource(&Device->DxgkInterface, Bar2, PAGE_SIZE);
    if (Aperture == NULL || (ULONGLONG)Aperture->u.Memory.Start.QuadPart != Bar2 ||
        Aperture->u.Memory.Length < HDGFX_RENDER_GTT_PAGES * PAGE_SIZE)
    {
        DPRINT1("No aperture at 0x%I64x for the render ring\n", Bar2);
        return;
    }

    Lowest.QuadPart = 0;
    Highest.QuadPart = MAXULONG;
    Boundary.QuadPart = 0;
    Adapter->RingMemory = MmAllocateContiguousMemorySpecifyCache(HDGFX_RING_SIZE, Lowest, Highest, Boundary,
                                                                 MmNonCached);
    Adapter->StatusPage = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, Lowest, Highest, Boundary, MmCached);
    Adapter->ScratchPage = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, Lowest, Highest, Boundary, MmNonCached);
    GttFirst = Aperture->u.Memory.Length / PAGE_SIZE - HDGFX_RENDER_GTT_PAGES;
    Address.QuadPart = (LONGLONG)(Bar0 + HDGFX_REG(GTT_PTE, GttFirst));
    if (Adapter->RingMemory == NULL || Adapter->StatusPage == NULL || Adapter->ScratchPage == NULL ||
        !HdgfxResourceOwned(&Device->DxgkInterface, (ULONGLONG)Address.QuadPart,
                            HDGFX_RENDER_GTT_PAGES * sizeof(ULONG)))
    {
        HdgfxStopRender(Adapter);
        return;
    }
    Adapter->GttWindow = MmMapIoSpace(Address, HDGFX_RENDER_GTT_PAGES * sizeof(ULONG), MmNonCached);
    if (Adapter->GttWindow == NULL)
    {
        HdgfxStopRender(Adapter);
        return;
    }
    RtlZeroMemory(Adapter->RingMemory, HDGFX_RING_SIZE);
    RtlZeroMemory(Adapter->StatusPage, PAGE_SIZE);
    RtlZeroMemory(Adapter->ScratchPage, PAGE_SIZE);
    ((volatile ULONG *)Adapter->StatusPage)[HDGFX_RING_FENCE_INDEX] = Adapter->CompletedFence;

    for (Index = 0; Index < HDGFX_RENDER_GTT_PAGES; Index++)
    {
        PVOID Page = Index < HDGFX_DMA_PAGES ?
                         (Adapter->DmaMemory != NULL ? Adapter->DmaMemory + Index * PAGE_SIZE : Adapter->ScratchPage) :
                     Index < HDGFX_STATUS_GTT_INDEX ?
                         (PUCHAR)Adapter->RingMemory + (Index - HDGFX_RING_GTT_INDEX) * PAGE_SIZE :
                     Index == HDGFX_STATUS_GTT_INDEX ? Adapter->StatusPage : Adapter->ScratchPage;

        Adapter->SavedGtt[Index] = READ_REGISTER_ULONG(&Adapter->GttWindow[Index]);
        WRITE_REGISTER_ULONG(&Adapter->GttWindow[Index], HdgfxIlkGttEntry(MmGetPhysicalAddress(Page).LowPart));
    }
    READ_REGISTER_ULONG(&Adapter->GttWindow[HDGFX_RENDER_GTT_PAGES - 1]);
    if (Adapter->DmaMemory != NULL)
        Adapter->DmaBase = GttFirst * PAGE_SIZE;

    Adapter->Ring.Commands = Adapter->RingMemory;
    Adapter->Ring.Size = HDGFX_RING_SIZE;
    Adapter->Ring.Address = (GttFirst + HDGFX_RING_GTT_INDEX) * PAGE_SIZE;
    Adapter->Ring.StatusAddress = (GttFirst + HDGFX_STATUS_GTT_INDEX) * PAGE_SIZE;
    Adapter->ScratchAddress = (GttFirst + HDGFX_SCRATCH_GTT_INDEX) * PAGE_SIZE;
    HdgfxMmioInit(Adapter->Registers, &Mmio);
    Adapter->RingStarted = HdgfxIlkStartRing(&Mmio, &Adapter->Ring);
    if (!Adapter->RingStarted)
    {
        DPRINT1("Render ring did not start: start 0x%08lx ctl 0x%08lx head 0x%08lx\n",
                HdgfxReadRegister32(Adapter, HDGFX_REG(RING_START, 0)),
                HdgfxReadRegister32(Adapter, HDGFX_REG(RING_CTL, 0)),
                HdgfxReadRegister32(Adapter, HDGFX_REG(RING_HEAD, 0)));
        HdgfxWriteRegister32(Adapter, HDGFX_REG(RING_CTL, 0), 0);
        HdgfxStopRender(Adapter);
        return;
    }
}

static BOOLEAN
HdgfxRingRun(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_reads_(Count) const ULONG *Dwords,
    _In_ ULONG Count,
    _Out_ PULONGLONG ElapsedUs)
{
    volatile ULONG *Status = (volatile ULONG *)Adapter->StatusPage;
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Start;
    LARGE_INTEGER Now;
    HDGFX_MMIO Mmio;
    LONG Interrupts = Adapter->RingInterrupts;
    ULONG Seqno = Adapter->Seqno + 1;
    BOOLEAN Done = FALSE;

    *ElapsedUs = 0;
    HdgfxMmioInit(Adapter->Registers, &Mmio);
    if ((Count != 0 && !HdgfxRingEmit(&Adapter->Ring, HdgfxIlkRingHead(&Mmio), (const uint32_t *)Dwords, Count)) ||
        !HdgfxRingEmitBreadcrumb(&Adapter->Ring, HdgfxIlkRingHead(&Mmio), HDGFX_RING_SEQNO_INDEX, Seqno))
    {
        return FALSE;
    }
    Adapter->Seqno = Seqno;
    Start = KeQueryPerformanceCounter(&Frequency);
    HdgfxIlkAdvanceRing(&Mmio, &Adapter->Ring);
    do
    {
        Done = Status[HDGFX_RING_SEQNO_INDEX] == Seqno && Adapter->RingInterrupts != Interrupts;
        Now = KeQueryPerformanceCounter(NULL);
        *ElapsedUs = (ULONGLONG)(Now.QuadPart - Start.QuadPart) * 1000000ULL / (ULONGLONG)Frequency.QuadPart;
        if (!Done)
            KeStallExecutionProcessor(10);
    } while (!Done && *ElapsedUs < HDGFX_RING_TEST_TIMEOUT_US);
    return Done;
}

static ULONG
HdgfxBltTestExpected(
    _In_ ULONG X,
    _In_ ULONG Y)
{
    if (X >= HDGFX_BLT_TEST_WIDTH - 2 || Y >= HDGFX_BLT_TEST_HEIGHT - 1)
        return 0;
    return X >= 8 && X < 24 && Y >= 4 && Y < 12 ? 0x55667788UL : 0x11223344UL;
}

static VOID
HdgfxTestRing(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    HDGFX_BLT_SURFACE Source = { Adapter->ScratchAddress, HDGFX_BLT_TEST_PITCH };
    HDGFX_BLT_SURFACE Destination = { Adapter->ScratchAddress + PAGE_SIZE / 2, HDGFX_BLT_TEST_PITCH };
    HDGFX_BLT_RECT Full = { 0, 0, HDGFX_BLT_TEST_WIDTH, HDGFX_BLT_TEST_HEIGHT };
    HDGFX_BLT_RECT Inner = { 8, 4, 24, 12 };
    HDGFX_BLT_RECT Copy = { 0, 0, HDGFX_BLT_TEST_WIDTH - 2, HDGFX_BLT_TEST_HEIGHT - 1 };
    volatile ULONG *Pixels = (volatile ULONG *)((PUCHAR)Adapter->ScratchPage + PAGE_SIZE / 2);
    uint32_t Dwords[24];
    ULONGLONG BreadcrumbUs;
    ULONGLONG BlitUs = 0;
    HDGFX_MMIO Mmio;
    BOOLEAN Breadcrumb;
    BOOLEAN Blit = FALSE;
    ULONG Mismatches = 0;
    ULONG Count = 0;
    ULONG Index;

    if (!Adapter->RingStarted)
        return;

    Breadcrumb = HdgfxRingRun(Adapter, NULL, 0, &BreadcrumbUs);
    Count += HdgfxBltFill(Dwords + Count, &Source, &Full, 0x11223344);
    Count += HdgfxBltFill(Dwords + Count, &Source, &Inner, 0x55667788);
    Count += HdgfxMiFlush(Dwords + Count);
    Count += HdgfxBltCopy(Dwords + Count, &Destination, &Copy, &Source, 0, 0);
    Count += HdgfxMiFlush(Dwords + Count);
    if (Breadcrumb && Count == 22)
    {
        Blit = HdgfxRingRun(Adapter, (const ULONG *)Dwords, Count, &BlitUs);
        for (Index = 0; Blit && Index < HDGFX_BLT_TEST_WIDTH * HDGFX_BLT_TEST_HEIGHT; Index++)
        {
            ULONG Expected = HdgfxBltTestExpected(Index % HDGFX_BLT_TEST_WIDTH, Index / HDGFX_BLT_TEST_WIDTH);

            if (Pixels[Index] != Expected && Mismatches++ == 0)
            {
                DPRINT1("Blit test pixel %lu,%lu is 0x%08lx, expected 0x%08lx\n",
                        Index % HDGFX_BLT_TEST_WIDTH, Index / HDGFX_BLT_TEST_WIDTH, Pixels[Index], Expected);
            }
        }
    }
    HdgfxMmioInit(Adapter->Registers, &Mmio);
    DPRINT1("Render ring at 0x%lx, %lu KB: breadcrumb %s after %I64u us, fill and copy %s after %I64u us "
            "(%lu wrong pixels), %ld interrupts, head 0x%lx tail 0x%lx, ESR 0x%lx\n",
            Adapter->Ring.Address, HDGFX_RING_SIZE / 1024, Breadcrumb ? "done" : "missing", BreadcrumbUs,
            Blit ? "done" : "missing", BlitUs, Mismatches, Adapter->RingInterrupts,
            HdgfxIlkRingHead(&Mmio), Adapter->Ring.Tail, HdgfxReadRegister32(Adapter, HDGFX_REG(ESR, 0)));
}

BOOLEAN
HdgfxRingFence(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ const HDGFX_MMIO *Mmio,
    _In_ ULONG Head)
{
    uint32_t Flush[2] = { 0, 0 };

    HdgfxMiFlush(Flush);
    if (!HdgfxRingEmit(&Adapter->Ring, Head, Flush, RTL_NUMBER_OF(Flush)) ||
        !HdgfxRingEmitBreadcrumb(&Adapter->Ring, Head, HDGFX_RING_SEQNO_INDEX, Adapter->Seqno + 1))
    {
        return FALSE;
    }
    Adapter->Seqno++;
    Adapter->RingUnfenced = FALSE;
    HdgfxIlkAdvanceRing(Mmio, &Adapter->Ring);
    return TRUE;
}

NTSTATUS
HdgfxResetRender(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    HDGFX_MMIO Mmio;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    PAGED_CODE();

    if (!ExAcquireRundownProtection(&Adapter->Rundown))
        return STATUS_SUCCESS;
    KeAcquireSpinLock(&Adapter->RingLock, &OldIrql);
    Adapter->RingResetting = TRUE;
    Adapter->RingResets++;
    KeReleaseSpinLock(&Adapter->RingLock, OldIrql);
    if (Adapter->RingStarted)
    {
        HdgfxMmioInit(Adapter->Registers, &Mmio);
        DPRINT1("Render reset: head 0x%lx tail 0x%lx seqno %lu of %lu, ESR 0x%lx\n",
                HdgfxIlkRingHead(&Mmio), Adapter->Ring.Tail,
                ((volatile ULONG *)Adapter->StatusPage)[HDGFX_RING_SEQNO_INDEX], Adapter->Seqno,
                HdgfxReadRegister32(Adapter, HDGFX_REG(ESR, 0)));
        if (!HdgfxIlkResetRender(&Mmio))
        {
            DPRINT1("Render domain reset did not complete\n");
            Status = STATUS_TIMEOUT;
        }
    }
    ExReleaseRundownProtection(&Adapter->Rundown);
    return Status;
}

VOID
HdgfxRestartRender(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    HDGFX_MMIO Mmio;
    KIRQL OldIrql;

    PAGED_CODE();

    if (!ExAcquireRundownProtection(&Adapter->Rundown))
        return;
    if (Adapter->RingStarted)
    {
        HdgfxMmioInit(Adapter->Registers, &Mmio);
        ((volatile ULONG *)Adapter->StatusPage)[HDGFX_RING_SEQNO_INDEX] = 0;
        Adapter->Seqno = 0;
        Adapter->RingUnfenced = FALSE;
        Adapter->RingStarted = HdgfxIlkStartRing(&Mmio, &Adapter->Ring);
        if (Adapter->GtInterruptMask != 0)
        {
            HdgfxWriteRegister32(Adapter, HDGFX_REG(GTIIR, 0), Adapter->GtInterruptMask);
            HdgfxWriteRegister32(Adapter, HDGFX_REG(GTIMR, 0), ~Adapter->GtInterruptMask);
            HdgfxWriteRegister32(Adapter, HDGFX_REG(GTIER, 0), Adapter->GtInterruptMask);
        }
        DPRINT1("Render ring %s after reset\n", Adapter->RingStarted ? "restarted" : "did not restart");
    }
    KeAcquireSpinLock(&Adapter->RingLock, &OldIrql);
    Adapter->RingResetting = FALSE;
    KeReleaseSpinLock(&Adapter->RingLock, OldIrql);
    ExReleaseRundownProtection(&Adapter->Rundown);
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
        KeInitializeSpinLock(&Adapter->RingLock);
        KeInitializeSpinLock(&Adapter->QueueLock);
        ExInitializeWorkItem(&Adapter->HotplugWork, HdgfxHotplugWorker, Adapter);
        Adapter->Device = Device;
        Device->PlatformServicesContext = Adapter;
    }
    else
    {
        ExReInitializeRundownProtection(&Adapter->Rundown);
    }
    HdgfxQueueStart(Adapter);
    HdgfxStartDma(Adapter);

    if (!NT_SUCCESS(HdgfxReadConfig(&Device->DxgkInterface, Ids, 0, sizeof(Ids))))
        return;
    Adapter->Generation = HdgfxFindGeneration(Ids[0], Ids[1]);
    if (Adapter->Generation == NULL)
    {
        DPRINT1("%04x:%04x is not a supported device\n", Ids[0], Ids[1]);
        return;
    }

    if (!NT_SUCCESS(HdgfxReadConfig(&Device->DxgkInterface, &Bar, HDGFX_PCI_BAR0, sizeof(Bar))))
        return;
    Address.QuadPart = (LONGLONG)(Bar & ~0xFULL);
    if (!HdgfxResourceOwned(&Device->DxgkInterface, (ULONGLONG)Address.QuadPart, HDGFX_MMIO_SIZE))
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
    HdgfxMmioInit(Adapter->Registers, &Mmio);
    Adapter->Generation->ReadDisplayState(&Mmio, State);
    HdgfxLogDisplayState(State);
    Adapter->InternalPanel = State->Lvds.Detected;
    HdgfxStartBacklight(Device, Adapter, State);
    HdgfxStartRender(Device, Adapter);
    HdgfxStartInterrupts(Device, Adapter);
    HdgfxTestRing(Adapter);
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
    HdgfxQueueStop(Adapter);
    if (Adapter->Registers == NULL)
    {
        HdgfxStopDma(Adapter);
        return;
    }

    HdgfxStopInterrupts(Device, Adapter);
    HdgfxStopRender(Adapter);
    HdgfxStopDma(Adapter);
    if (Adapter->OpRegion != NULL)
    {
        MmUnmapIoSpace(Adapter->OpRegion, HDGFX_OPREGION_SIZE);
        Adapter->OpRegion = NULL;
    }
    MmUnmapIoSpace(Adapter->Registers, HDGFX_MMIO_SIZE);
    Adapter->Registers = NULL;
    Adapter->BacklightMaximum = 0;
    Adapter->InternalPanel = FALSE;
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
    HDGFX_MMIO Io;

    PAGED_CODE();

    if (Brightness == 0 || Brightness > HDGFX_BLC_LEVEL_COUNT)
        return STATUS_INVALID_PARAMETER;

    Adapter = HdgfxBacklightAcquire(Context);
    if (Adapter == NULL)
        return STATUS_DEVICE_NOT_READY;

    HdgfxBacklightWrite(Adapter, Brightness, HDGFX_BLC_LEVEL_COUNT);
    if (Adapter->AsleReady)
    {
        HdgfxOpRegionIo(Adapter, &Io);
        HdgfxOpRegionSetBrightness(&Io, Brightness);
    }
    ExReleaseRundownProtection(&Adapter->Rundown);
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxBacklightGet(
    _In_ PVOID Context,
    _Out_ PUCHAR Brightness)
{
    PHDGFX_ADAPTER Adapter;

    PAGED_CODE();

    *Brightness = 0;
    Adapter = HdgfxBacklightAcquire(Context);
    if (Adapter == NULL)
        return STATUS_DEVICE_NOT_READY;

    *Brightness = HdgfxBacklightRead(Adapter);
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

BOOLEAN
SoftGpuPlatformQueryInternalPanel(
    _In_ PSOFTGPU_DEVICE Device,
    _Out_ PULONG AcpiUid)
{
    PHDGFX_ADAPTER Adapter = Device->PlatformServicesContext;

    PAGED_CODE();

    *AcpiUid = 0;
    if (Adapter == NULL || !Adapter->InternalPanel)
        return FALSE;

    *AcpiUid = HDGFX_ACPI_ID_INTERNAL_PANEL;
    return TRUE;
}

NTSTATUS
SoftGpuPlatformNotifyAcpiEvent(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ DXGK_EVENT_TYPE EventType,
    _In_ ULONG Event,
    _In_opt_ PVOID Argument,
    _Out_ PULONG AcpiFlags)
{
    PHDGFX_ADAPTER Adapter;
    uint32_t Events;
    HDGFX_MMIO Io;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Argument);

    *AcpiFlags = 0;
    if (EventType != DxgkAcpiEvent || Event != ACPI_NOTIFY_PANEL_SWITCH)
        return STATUS_SUCCESS;

    Adapter = HdgfxBacklightAcquire(Device);
    if (Adapter == NULL)
        return STATUS_SUCCESS;

    if (Adapter->AsleReady)
    {
        HdgfxOpRegionIo(Adapter, &Io);
        if (HdgfxOpRegionBeginNotification(&Io, &Events))
        {
            if (Events & (HDGFX_OPREGION_EVENT_LID | HDGFX_OPREGION_EVENT_DOCK))
                *AcpiFlags = DXGK_ACPI_POLL_DISPLAY_CHILDREN;
            HdgfxOpRegionEndNotification(&Io, (Events & HDGFX_OPREGION_EVENT_HOTKEY) == 0);
            DPRINT1("OpRegion display event 0x%lx, %s\n", Events,
                    (Events & HDGFX_OPREGION_EVENT_HOTKEY) ? "no other output to switch to" : "children polled");
        }
    }
    ExReleaseRundownProtection(&Adapter->Rundown);
    return STATUS_SUCCESS;
}

BOOLEAN
SoftGpuPlatformInterruptRoutine(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PHDGFX_ADAPTER Adapter = Device->PlatformServicesContext;
    ULONG Pending;
    ULONG Gt;
    ULONG Sde;

    if (Adapter == NULL || (!Adapter->AsleInterrupt && !Adapter->HotplugInterrupt && !Adapter->RingInterrupt))
        return FALSE;

    Pending = HdgfxReadRegister32(Adapter, HDGFX_REG(DEIIR, 0)) &
              ((Adapter->AsleInterrupt ? Adapter->Generation->GseInterrupt : 0) |
               (Adapter->HotplugInterrupt ? Adapter->Generation->PchInterrupt : 0));
    Gt = Adapter->RingInterrupt ? HdgfxReadRegister32(Adapter, HDGFX_REG(GTIIR, 0)) & Adapter->GtInterruptMask : 0;
    if (Pending == 0 && Gt == 0)
        return FALSE;

    if (Gt != 0)
    {
        HdgfxWriteRegister32(Adapter, HDGFX_REG(GTIIR, 0), Gt);
        InterlockedIncrement(&Adapter->RingInterrupts);
        InterlockedExchange(&Adapter->RingPending, 1);
        HdgfxQueueInterrupt(Adapter);
    }

    if (Pending & Adapter->Generation->PchInterrupt)
    {
        Sde = HdgfxReadRegister32(Adapter, HDGFX_REG(SDEIIR, 0));
        HdgfxWriteRegister32(Adapter, HDGFX_REG(SDEIIR, 0), Sde);
        if (Sde & Adapter->Generation->CrtHotplugInterrupt)
            InterlockedExchange(&Adapter->HotplugPending, 1);
    }
    if (Pending != 0)
        HdgfxWriteRegister32(Adapter, HDGFX_REG(DEIIR, 0), Pending);
    if (Pending & Adapter->Generation->GseInterrupt)
        InterlockedExchange(&Adapter->AslePending, 1);
    Device->DxgkInterface.DxgkCbQueueDpc(Device->DxgkInterface.DeviceHandle);
    return TRUE;
}

VOID
SoftGpuPlatformDpcRoutine(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PHDGFX_ADAPTER Adapter = Device->PlatformServicesContext;
    HDGFX_MMIO Io;

    if (Adapter == NULL)
        return;
    if (InterlockedExchange(&Adapter->RingPending, 0) != 0)
        HdgfxQueueDpc(Adapter);
    if (InterlockedExchange(&Adapter->HotplugPending, 0) != 0)
        HdgfxQueueHotplug(Adapter);
    if (InterlockedExchange(&Adapter->AslePending, 0) == 0)
        return;
    Adapter = HdgfxBacklightAcquire(Device);
    if (Adapter == NULL)
        return;

    if (Adapter->AsleReady)
    {
        HdgfxOpRegionIo(Adapter, &Io);
        HdgfxOpRegionServiceAsle(&Io, HdgfxAsleSetBacklight, Adapter);
    }
    ExReleaseRundownProtection(&Adapter->Rundown);
}

static VOID
HdgfxLock(
    _In_ PHDGFX_SCANOUT Scanout)
{
    KeWaitForSingleObject(&Scanout->Lock, Executive, KernelMode, FALSE, NULL);
}

static VOID
HdgfxUnlock(
    _In_ PHDGFX_SCANOUT Scanout)
{
    KeReleaseMutex(&Scanout->Lock, FALSE);
}

static VOID
HdgfxCursorCommit(
    _In_ PHDGFX_SCANOUT Scanout);

static NTSTATUS
HdgfxScanoutCommit(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ const HDGFX_DTD *Panel,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Pitch)
{
    HDGFX_DISPLAY_STATE *States;
    HDGFX_MODESET_STATUS Result;
    HDGFX_LVDS_MODE Mode;
    HDGFX_MMIO Mmio;
    const HDGFX_PIPE *Pipe;
    const HDGFX_DPLL *Dpll;
    const char *Mismatch;
    ULONGLONG Start;
    NTSTATUS Status = STATUS_SUCCESS;

    States = ExAllocatePoolZero(NonPagedPoolNx, 2 * sizeof(*States), HDGFX_TAG);
    if (States == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&Mode, sizeof(Mode));
    Mode.Panel = Panel;
    Mode.SourceWidth = Width;
    Mode.SourceHeight = Height;
    Mode.Stride = Pitch;
    Mode.Surface = Scanout->Surface;
    Mode.Scaling = HdgfxScalingAspect;
    Mode.Ssc = Scanout->Ssc;

    HdgfxMmioInit(Scanout->Registers, &Mmio);
    Scanout->Generation->ReadDisplayState(&Mmio, &States[0]);
    if (!States[0].PanelPower.TargetOn)
        States[0].PanelPower.BacklightEnabled = Scanout->PanelBacklight;
    Result = Scanout->Generation->ComputeLvds(&States[0], &Mode, &States[1]);
    if (Result != HdgfxModesetSuccess)
    {
        DPRINT1("LVDS %lux%lu pitch %lu: %s\n", Width, Height, Pitch, HdgfxModesetStatusName(Result));
        ExFreePoolWithTag(States, HDGFX_TAG);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }

    Scanout->Panel = *Panel;
    Mode.Panel = &Scanout->Panel;
    Scanout->Width = Width;
    Scanout->Height = Height;
    Scanout->Pitch = Pitch;
    if (Scanout->DisplayOff || !Scanout->LvdsActive)
    {
        DPRINT1("LVDS %lux%lu pitch %lu deferred until the panel is on\n", Width, Height, Pitch);
        ExFreePoolWithTag(States, HDGFX_TAG);
        return STATUS_SUCCESS;
    }

    Start = KeQueryInterruptTime();
    Result = Scanout->Generation->CommitLvds(&Mmio, &States[0], &States[1]);
    Pipe = &States[1].Pipes[States[1].Lvds.Transcoder];
    Dpll = &States[1].Dplls[States[1].Transcoders[States[1].Lvds.Transcoder].Dpll];
    if (Result != HdgfxModesetSuccess)
    {
        DPRINT1("LVDS %lux%lu commit failed after %I64u ms: %s\n",
                Width, Height, (KeQueryInterruptTime() - Start) / 10000, HdgfxModesetStatusName(Result));
        Status = STATUS_DEVICE_HARDWARE_ERROR;
    }
    else
    {
        Scanout->Generation->ReadDisplayState(&Mmio, &States[0]);
        Mismatch = HdgfxCompareLvdsState(&States[1], &States[0]);
        DPRINT1("LVDS %lux%lu at %lu mHz pitch %lu at 0x%lx, fitter %u %lu,%lu %lux%lu; DPLL N %lu M1 %lu M2 %lu "
                "P1 %lu P2 %lu ref %lu = %lu kHz, FDI x%lu data %lu link %lu: %I64u ms, readback %s%s\n",
                Width, Height, HdgfxRefreshMilliHz(&Pipe->Timings, Dpll->DotKHz), Pitch, Pipe->Primary.Surface,
                Pipe->PanelFitter.Enabled, Pipe->PanelFitter.X, Pipe->PanelFitter.Y,
                Pipe->PanelFitter.Width, Pipe->PanelFitter.Height,
                Dpll->N, Dpll->M1, Dpll->M2, Dpll->P1, Dpll->P2, Dpll->Reference, Dpll->DotKHz,
                Pipe->FdiTx.Lanes, Pipe->Fdi.DataM, Pipe->Fdi.LinkM,
                (KeQueryInterruptTime() - Start) / 10000,
                Mismatch ? "differs in " : "matches", Mismatch ? Mismatch : "");
        if (Mismatch)
            Status = STATUS_DEVICE_HARDWARE_ERROR;
    }

    ExFreePoolWithTag(States, HDGFX_TAG);
    return Status;
}

static NTSTATUS
HdgfxSetMode(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const SOFTGPU_DISPLAY_MODE *Mode)
{
    PHDGFX_SCANOUT Scanout = Device->PlatformContext;
    NTSTATUS Status;

    if (Scanout == NULL || Mode == NULL || Mode->PlatformMode >= Scanout->PanelModeCount)
        return STATUS_INVALID_PARAMETER;
    HdgfxLock(Scanout);
    Status = HdgfxScanoutCommit(Scanout, &Scanout->PanelModes[Mode->PlatformMode], Mode->Width, Mode->Height,
                                Mode->Pitch);
    if (NT_SUCCESS(Status) && Scanout->SlabGtt != NULL)
        HdgfxCursorCommit(Scanout);
    HdgfxUnlock(Scanout);
    return Status;
}

static NTSTATUS
HdgfxLvdsOff(
    _Inout_ PHDGFX_SCANOUT Scanout)
{
    HDGFX_DISPLAY_STATE *State;
    HDGFX_MODESET_STATUS Result;
    HDGFX_MMIO Mmio;
    ULONGLONG Start;

    State = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*State), HDGFX_TAG);
    if (State == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    HdgfxMmioInit(Scanout->Registers, &Mmio);
    Scanout->Generation->ReadDisplayState(&Mmio, State);
    Scanout->PanelBacklight = State->PanelPower.BacklightEnabled;
    Start = KeQueryInterruptTime();
    Result = Scanout->Generation->DisableLvds(&Mmio, State);
    Scanout->Generation->ReadDisplayState(&Mmio, State);
    DPRINT1("Display off in %I64u ms: %s; panel %u, pipe %u, transcoder %u, DPLL %u, LVDS %u\n",
            (KeQueryInterruptTime() - Start) / 10000, HdgfxModesetStatusName(Result),
            State->PanelPower.On, State->Pipes[State->Lvds.Transcoder].Enabled,
            State->Transcoders[State->Lvds.Transcoder].Enabled,
            State->Dplls[State->Transcoders[State->Lvds.Transcoder].Dpll].Enabled, State->Lvds.Enabled);
    ExFreePoolWithTag(State, HDGFX_TAG);
    return Result == HdgfxModesetSuccess ? STATUS_SUCCESS : STATUS_DEVICE_HARDWARE_ERROR;
}

static NTSTATUS
HdgfxCrtCommit(
    _Inout_ PHDGFX_SCANOUT Scanout)
{
    HDGFX_DISPLAY_STATE *States;
    HDGFX_MODESET_STATUS Result;
    HDGFX_CRT_MODE Mode;
    HDGFX_MMIO Mmio;
    const HDGFX_PIPE *Pipe;
    const HDGFX_DPLL *Dpll;
    const char *Mismatch;
    ULONGLONG Start;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Scanout->CrtActive || Scanout->CrtDisplayOff)
        return STATUS_SUCCESS;
    if (!Scanout->CrtTimingValid)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET_MODE;

    States = ExAllocatePoolZero(NonPagedPoolNx, 2 * sizeof(*States), HDGFX_TAG);
    if (States == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&Mode, sizeof(Mode));
    Mode.Timing = &Scanout->CrtTiming;
    Mode.Pipe = HDGFX_CRT_PIPE;
    Mode.SourceWidth = Scanout->Width;
    Mode.SourceHeight = Scanout->Height;
    Mode.Stride = Scanout->Pitch;
    Mode.Surface = Scanout->Surface;
    Mode.Scaling = HdgfxScalingAspect;

    HdgfxMmioInit(Scanout->Registers, &Mmio);
    Scanout->Generation->ReadDisplayState(&Mmio, &States[0]);
    Result = Scanout->Generation->ComputeCrt(&States[0], &Mode, &States[1]);
    if (Result != HdgfxModesetSuccess)
    {
        DPRINT1("CRT %lux%lu on %lux%lu: %s\n", Scanout->Width, Scanout->Height,
                Scanout->CrtTiming.Timings.HActive, Scanout->CrtTiming.Timings.VActive,
                HdgfxModesetStatusName(Result));
        ExFreePoolWithTag(States, HDGFX_TAG);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET_MODE;
    }

    Start = KeQueryInterruptTime();
    Result = Scanout->Generation->CommitCrt(&Mmio, &States[0], &States[1]);
    Pipe = &States[1].Pipes[HDGFX_CRT_PIPE];
    Dpll = &States[1].Dplls[States[1].Transcoders[HDGFX_CRT_PIPE].Dpll];
    if (Result != HdgfxModesetSuccess)
    {
        DPRINT1("CRT %lux%lu commit failed after %I64u ms: %s\n", Scanout->Width, Scanout->Height,
                (KeQueryInterruptTime() - Start) / 10000, HdgfxModesetStatusName(Result));
        Status = STATUS_DEVICE_HARDWARE_ERROR;
    }
    else
    {
        Scanout->Generation->ReadDisplayState(&Mmio, &States[0]);
        Mismatch = HdgfxCompareCrtState(&States[1], &States[0]);
        DPRINT1("CRT %lux%lu on %lux%lu at %lu mHz pitch %lu at 0x%lx, fitter %u %lu,%lu %lux%lu; DPLL N %lu M1 %lu "
                "M2 %lu P1 %lu P2 %lu = %lu kHz, FDI x%lu: %I64u ms, readback %s%s\n",
                Scanout->Width, Scanout->Height, Pipe->Timings.HActive, Pipe->Timings.VActive,
                HdgfxRefreshMilliHz(&Pipe->Timings, Dpll->DotKHz), Pipe->Primary.Stride, Pipe->Primary.Surface,
                Pipe->PanelFitter.Enabled, Pipe->PanelFitter.X, Pipe->PanelFitter.Y,
                Pipe->PanelFitter.Width, Pipe->PanelFitter.Height,
                Dpll->N, Dpll->M1, Dpll->M2, Dpll->P1, Dpll->P2, Dpll->DotKHz, Pipe->FdiTx.Lanes,
                (KeQueryInterruptTime() - Start) / 10000,
                Mismatch ? "differs in " : "matches", Mismatch ? Mismatch : "");
        if (Mismatch)
            Status = STATUS_DEVICE_HARDWARE_ERROR;
    }

    ExFreePoolWithTag(States, HDGFX_TAG);
    return Status;
}

static NTSTATUS
HdgfxCrtOff(
    _Inout_ PHDGFX_SCANOUT Scanout)
{
    HDGFX_DISPLAY_STATE *State;
    HDGFX_MODESET_STATUS Result;
    HDGFX_MMIO Mmio;

    State = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*State), HDGFX_TAG);
    if (State == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    HdgfxMmioInit(Scanout->Registers, &Mmio);
    Scanout->Generation->ReadDisplayState(&Mmio, State);
    Result = Scanout->Generation->DisableCrt(&Mmio, State);
    DPRINT1("CRT off: %s\n", HdgfxModesetStatusName(Result));
    ExFreePoolWithTag(State, HDGFX_TAG);
    return Result == HdgfxModesetSuccess ? STATUS_SUCCESS : STATUS_DEVICE_HARDWARE_ERROR;
}

static NTSTATUS
HdgfxSetOutputs(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_reads_(Count) const SOFTGPU_OUTPUT_TIMING *Timings,
    _In_ ULONG Count)
{
    PHDGFX_SCANOUT Scanout = Device->PlatformContext;
    const SOFTGPU_DISPLAY_MODE *Source = &Timings[0].Mode;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Scanout == NULL || Count != 2 || Timings[1].ChildUid != HDGFX_CRT_CHILD_UID ||
        Source->PlatformMode >= Scanout->PanelModeCount)
    {
        return STATUS_INVALID_PARAMETER;
    }

    HdgfxLock(Scanout);
    if (!Timings[1].Active && Scanout->CrtActive)
    {
        if (!Scanout->CrtDisplayOff)
            Status = HdgfxCrtOff(Scanout);
        if (NT_SUCCESS(Status))
            Scanout->CrtActive = FALSE;
    }
    if (NT_SUCCESS(Status) && !Timings[0].Active && Scanout->LvdsActive)
    {
        if (!Scanout->DisplayOff)
            Status = HdgfxLvdsOff(Scanout);
        if (NT_SUCCESS(Status))
            Scanout->LvdsActive = FALSE;
    }
    if (NT_SUCCESS(Status))
    {
        Scanout->LvdsActive = Timings[0].Active;
        Status = HdgfxScanoutCommit(Scanout, &Scanout->PanelModes[Source->PlatformMode],
                                    Source->Width, Source->Height, Source->Pitch);
    }
    if (NT_SUCCESS(Status) && Timings[1].Active)
    {
        Scanout->CrtActive = TRUE;
        Status = HdgfxCrtCommit(Scanout);
    }
    if (NT_SUCCESS(Status) && Scanout->SlabGtt != NULL)
        HdgfxCursorCommit(Scanout);
    HdgfxUnlock(Scanout);
    DPRINT1("Outputs: panel %s, CRT %s, source %lux%lu: 0x%08lx\n",
            Timings[0].Active ? "on" : "off", Timings[1].Active ? "on" : "off",
            Source->Width, Source->Height, Status);
    return Status;
}

NTSTATUS
SoftGpuPlatformSetMonitorPower(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ ULONG ChildUid,
    _In_ DEVICE_POWER_STATE DevicePowerState)
{
    PHDGFX_SCANOUT Scanout = Device->PlatformContext;
    BOOLEAN Off = DevicePowerState != PowerDeviceD0;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Scanout == NULL)
        return STATUS_SUCCESS;

    HdgfxLock(Scanout);
    if (ChildUid == HDGFX_CRT_CHILD_UID && Off != Scanout->CrtDisplayOff)
    {
        if (Off && Scanout->CrtActive)
            Status = HdgfxCrtOff(Scanout);
        if (NT_SUCCESS(Status))
            Scanout->CrtDisplayOff = Off;
        if (NT_SUCCESS(Status) && !Off)
            Status = HdgfxCrtCommit(Scanout);
    }
    else if (ChildUid == SOFTGPU_CHILD_UID && Off != Scanout->DisplayOff)
    {
        if (Off && Scanout->LvdsActive)
            Status = HdgfxLvdsOff(Scanout);
        if (NT_SUCCESS(Status))
            Scanout->DisplayOff = Off;
        if (NT_SUCCESS(Status) && !Off)
            Status = HdgfxScanoutCommit(Scanout, &Scanout->Panel, Scanout->Width, Scanout->Height, Scanout->Pitch);
    }
    HdgfxUnlock(Scanout);
    return Status;
}

static VOID
HdgfxScanoutFree(
    _In_ PHDGFX_SCANOUT Scanout)
{
    if (Scanout->Registers != NULL)
        MmUnmapIoSpace(Scanout->Registers, HDGFX_MMIO_SIZE);
    ExFreePoolWithTag(Scanout, HDGFX_TAG);
}

static VOID
HdgfxAddMode(
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config,
    _In_ const HDGFX_SCANOUT *Scanout,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG PanelMode)
{
    const HDGFX_DTD *Panel = &Scanout->PanelModes[PanelMode];
    SOFTGPU_DISPLAY_MODE *Mode;
    ULONG Index;

    for (Index = 0; Index < Config->ModeCount; Index++)
    {
        if (Config->Modes[Index].Width == Width && Config->Modes[Index].Height == Height &&
            Config->Modes[Index].PlatformMode == PanelMode)
        {
            return;
        }
    }
    if (Config->ModeCount >= SOFTGPU_MAX_DISPLAY_MODES)
        return;

    Mode = &Config->Modes[Config->ModeCount];
    Mode->Width = Width;
    Mode->Height = Height;
    Mode->Pitch = ALIGN_UP_BY(Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL, HDGFX_STRIDE_ALIGNMENT);
    Mode->PlatformMode = PanelMode;
    Mode->TotalWidth = Panel->Timings.HTotal;
    Mode->TotalHeight = Panel->Timings.VTotal;
    Mode->PixelRate = Panel->DotKHz * 1000;
    Config->ScanoutSize = max(Config->ScanoutSize, (ULONGLONG)Mode->Pitch * Height);
    Config->ModeCount++;
}

static ULONG
HdgfxCrtModes(
    _In_ const HDGFX_DTD *Timing,
    _In_reads_(SourceCount) const SOFTGPU_DISPLAY_MODE *Sources,
    _In_ ULONG SourceCount,
    _Out_writes_(SOFTGPU_MAX_DISPLAY_MODES) SOFTGPU_DISPLAY_MODE *Modes)
{
    ULONG Count = 0;
    ULONG Index;
    ULONG Other;

    for (Index = 0; Index < SourceCount && Count < SOFTGPU_MAX_DISPLAY_MODES; Index++)
    {
        if (Sources[Index].Width > Timing->Timings.HActive || Sources[Index].Height > Timing->Timings.VActive)
            continue;
        for (Other = 0; Other < Count; Other++)
        {
            if (Modes[Other].Width == Sources[Index].Width && Modes[Other].Height == Sources[Index].Height)
                break;
        }
        if (Other != Count)
            continue;

        Modes[Count] = Sources[Index];
        Modes[Count].PlatformMode = 0;
        Modes[Count].AllocationSlabSize = 0;
        Modes[Count].TotalWidth = Timing->Timings.HTotal;
        Modes[Count].TotalHeight = Timing->Timings.VTotal;
        Modes[Count].PixelRate = Timing->DotKHz * 1000;
        Count++;
    }
    return Count;
}

static BOOLEAN
HdgfxCrtDetect(
    _Inout_ PHDGFX_SCANOUT Scanout,
    _Out_writes_bytes_(HDGFX_EDID_SIZE) PUCHAR Edid,
    _Out_ PULONG EdidLength)
{
    HDGFX_MODESET_STATUS Status;
    HDGFX_EDID Parsed;
    HDGFX_MMIO Mmio;
    BOOLEAN EdidFound;
    bool Attached = false;

    *EdidLength = 0;
    HdgfxMmioInit(Scanout->Registers, &Mmio);
    Status = Scanout->Generation->DetectCrt(&Mmio, &Attached);
    if (Status != HdgfxModesetSuccess)
        DPRINT1("CRT load detection: %s\n", HdgfxModesetStatusName(Status));
    EdidFound = HdgfxReadEdid("CRT", Scanout->Generation->CrtDdcPins, &Mmio, Edid, &Parsed);
    if (EdidFound)
        *EdidLength = HDGFX_EDID_SIZE;
    if (EdidFound && Parsed.DtdCount != 0)
    {
        Scanout->CrtTiming = Parsed.Dtds[0];
        Scanout->CrtTimingValid = TRUE;
    }
    else if (!Attached)
    {
        Scanout->CrtTimingValid = FALSE;
    }
    DPRINT1("CRT load detection: %s, EDID %s\n", Attached ? "attached" : "nothing attached",
            EdidFound ? "read" : "not read");
    return Attached || EdidFound;
}

static VOID
HdgfxCrtStart(
    _Inout_ PSOFTGPU_DEVICE Device,
    _Inout_ PHDGFX_SCANOUT Scanout,
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    PSOFTGPU_OUTPUT Output = &Device->ExtraOutputs[0];

    if (Scanout->Generation->DetectCrt == NULL)
        return;

    Output->ChildUid = HDGFX_CRT_CHILD_UID;
    Output->OutputTechnology = D3DKMDT_VOT_HD15;
    Output->HpdAwareness = HpdAwarenessInterruptible;
    Output->Connected = HdgfxCrtDetect(Scanout, Output->Edid, &Output->EdidLength);
    if (Output->Connected && Scanout->CrtTimingValid)
        Output->ModeCount = HdgfxCrtModes(&Scanout->CrtTiming, Config->Modes, Config->ModeCount, Output->Modes);
    Config->ExtraOutputCount = 1;
    Config->SetOutputs = HdgfxSetOutputs;
}

static VOID
HdgfxCrtRefresh(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ BOOLEAN Indicate)
{
    PHDGFX_SCANOUT Scanout = Device->PlatformContext;
    SOFTGPU_DISPLAY_MODE *Modes;
    UCHAR Edid[HDGFX_EDID_SIZE];
    ULONG EdidLength;
    ULONG Count = 0;
    BOOLEAN Connected;
    NTSTATUS Status;

    PAGED_CODE();

    if (Scanout == NULL || Device->ExtraOutputCount == 0)
        return;
    Modes = ExAllocatePoolZero(PagedPool, SOFTGPU_MAX_DISPLAY_MODES * sizeof(*Modes), HDGFX_TAG);
    if (Modes == NULL)
        return;

    HdgfxLock(Scanout);
    Connected = HdgfxCrtDetect(Scanout, Edid, &EdidLength);
    if (Connected && Scanout->CrtTimingValid)
        Count = HdgfxCrtModes(&Scanout->CrtTiming, Device->Modes, Device->ModeCount, Modes);
    HdgfxUnlock(Scanout);

    Status = SoftGpuUpdateOutput(Device, HDGFX_CRT_CHILD_UID, Connected, Modes, Count, 0, Edid, EdidLength, Indicate);
    DPRINT1("CRT hot plug: %s, %lu mode(s): 0x%08lx\n", Connected ? "connected" : "disconnected", Count, Status);
    ExFreePoolWithTag(Modes, HDGFX_TAG);
}

static BOOLEAN
HdgfxGttMapped(
    _In_ PDXGK_INTERFACE Interface,
    _In_ ULONGLONG Bar0,
    _In_ ULONG Surface,
    _In_ ULONGLONG Size)
{
    ULONG Pages = (ULONG)(Size / PAGE_SIZE);
    PHYSICAL_ADDRESS Address;
    PULONG Gtt;
    ULONG Entry;
    ULONG Index;

    Address.QuadPart = (LONGLONG)(Bar0 + HDGFX_REG(GTT_PTE, Surface / PAGE_SIZE));
    if (!HdgfxResourceOwned(Interface, (ULONGLONG)Address.QuadPart, Pages * sizeof(ULONG)))
        return FALSE;
    Gtt = MmMapIoSpace(Address, Pages * sizeof(ULONG), MmNonCached);
    if (Gtt == NULL)
        return FALSE;

    for (Index = 0; Index < Pages; Index++)
    {
        Entry = READ_REGISTER_ULONG(&Gtt[Index]);
        if (!HDGFX_GET(Entry, GTT_PTE_VALID))
        {
            DPRINT1("GTT entry for 0x%lx is 0x%08lx; the firmware maps only %lu KB of the surface\n",
                    Surface + Index * PAGE_SIZE, Entry, Index * (PAGE_SIZE / 1024));
            break;
        }
    }
    MmUnmapIoSpace(Gtt, Pages * sizeof(ULONG));
    return Index == Pages;
}

static NTSTATUS
HdgfxScanoutStart(
    _In_ PDXGK_INTERFACE Interface,
    _Inout_ PHDGFX_SCANOUT Scanout,
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Aperture;
    HDGFX_DISPLAY_STATE *State;
    PHYSICAL_ADDRESS Address;
    HDGFX_EDID Edid;
    HDGFX_VBT Vbt;
    HDGFX_MMIO Mmio;
    BOOLEAN VbtPresent;
    BOOLEAN PanelFound = FALSE;
    ULONG PanelMode;
    ULONGLONG Bar0 = 0;
    ULONGLONG Bar2 = 0;
    USHORT Ids[2] = { 0, 0 };
    PUCHAR Mapping;
    ULONG Index;
    NTSTATUS Status;

    if (!NT_SUCCESS(HdgfxReadConfig(Interface, Ids, 0, sizeof(Ids))))
        return STATUS_NOT_SUPPORTED;
    Scanout->Generation = HdgfxFindGeneration(Ids[0], Ids[1]);
    if (Scanout->Generation == NULL || Scanout->Generation->ComputeLvds == NULL)
        return STATUS_NOT_SUPPORTED;

    if (!NT_SUCCESS(HdgfxReadConfig(Interface, &Bar0, HDGFX_PCI_BAR0, sizeof(Bar0))) ||
        !NT_SUCCESS(HdgfxReadConfig(Interface, &Bar2, HDGFX_PCI_BAR2, sizeof(Bar2))))
    {
        return STATUS_NOT_SUPPORTED;
    }
    Bar0 &= ~0xFULL;
    Bar2 &= ~0xFULL;
    if (!HdgfxResourceOwned(Interface, Bar0, HDGFX_MMIO_SIZE))
        return STATUS_NOT_SUPPORTED;
    Aperture = HdgfxFindMemoryResource(Interface, Bar2, PAGE_SIZE);
    if (Aperture != NULL && (ULONGLONG)Aperture->u.Memory.Start.QuadPart == Bar2 &&
        HdgfxResourceOwned(Interface, Bar0 + HDGFX_REG(GTT_PTE, 0),
                           Aperture->u.Memory.Length / PAGE_SIZE * sizeof(ULONG)))
    {
        Scanout->Bar0 = Bar0;
        Scanout->ApertureSize = Aperture->u.Memory.Length;
    }
    Address.QuadPart = (LONGLONG)Bar0;
    Scanout->Registers = MmMapIoSpace(Address, HDGFX_MMIO_SIZE, MmNonCached);
    if (Scanout->Registers == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    State = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*State), HDGFX_TAG);
    if (State == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    HdgfxMmioInit(Scanout->Registers, &Mmio);
    Scanout->Generation->ReadDisplayState(&Mmio, State);
    DPRINT1("Firmware scan-out: pipe A %u source %lux%lu fitter %u, plane stride %lu surface 0x%lx x-tiled %u\n",
            State->Pipes[0].Enabled, State->Pipes[0].SourceWidth, State->Pipes[0].SourceHeight,
            State->Pipes[0].PanelFitter.Enabled, State->Pipes[0].Primary.Stride,
            State->Pipes[0].Primary.Surface, State->Pipes[0].Primary.XTiled);
    if (!State->Lvds.Detected)
    {
        DPRINT1("No LVDS panel detected\n");
        ExFreePoolWithTag(State, HDGFX_TAG);
        return STATUS_NOT_SUPPORTED;
    }

    if (HdgfxReadEdid("Panel", Scanout->Generation->PanelDdcPins, &Mmio, Config->Edid, &Edid) && Edid.DtdCount != 0)
    {
        Config->EdidLength = HDGFX_EDID_SIZE;
        for (Index = 0; Index < Edid.DtdCount; Index++)
        {
            if (Edid.Dtds[Index].Timings.HActive == Edid.Dtds[0].Timings.HActive &&
                Edid.Dtds[Index].Timings.VActive == Edid.Dtds[0].Timings.VActive)
            {
                Scanout->PanelModes[Scanout->PanelModeCount++] = Edid.Dtds[Index];
            }
        }
        PanelFound = TRUE;
    }
    Mapping = HdgfxMapOpRegion(Interface, &Vbt, &VbtPresent);
    if (Mapping != NULL)
        MmUnmapIoSpace(Mapping, HDGFX_OPREGION_SIZE);
    if (!PanelFound && VbtPresent && Vbt.Panel.Present)
    {
        Scanout->PanelModes[0].DotKHz = Vbt.Panel.DotKHz;
        Scanout->PanelModes[0].Timings = Vbt.Panel.Timings;
        Scanout->PanelModes[0].HSyncPositive = Vbt.Panel.HSyncPositive;
        Scanout->PanelModes[0].VSyncPositive = Vbt.Panel.VSyncPositive;
        Scanout->PanelModes[0].WidthMm = Vbt.Panel.WidthMm;
        Scanout->PanelModes[0].HeightMm = Vbt.Panel.HeightMm;
        Scanout->PanelModeCount = 1;
        PanelFound = TRUE;
    }
    Scanout->Ssc = VbtPresent ? Vbt.LvdsSscEnabled :
                   State->Dplls[0].Enabled && State->Dplls[0].Reference == 3;
    ExFreePoolWithTag(State, HDGFX_TAG);
    if (!PanelFound)
    {
        DPRINT1("No panel timing in the EDID or the VBT\n");
        return STATUS_NOT_SUPPORTED;
    }

    Scanout->Panel = Scanout->PanelModes[0];
    for (PanelMode = 0; PanelMode < Scanout->PanelModeCount; PanelMode++)
        HdgfxAddMode(Config, Scanout, Scanout->Panel.Timings.HActive, Scanout->Panel.Timings.VActive, PanelMode);
    for (Index = 0; Index < RTL_NUMBER_OF(HdgfxScaledModes); Index++)
    {
        if (HdgfxScaledModes[Index][0] > Scanout->Panel.Timings.HActive ||
            HdgfxScaledModes[Index][1] > Scanout->Panel.Timings.VActive)
        {
            continue;
        }
        for (PanelMode = 0; PanelMode < Scanout->PanelModeCount; PanelMode++)
            HdgfxAddMode(Config, Scanout, HdgfxScaledModes[Index][0], HdgfxScaledModes[Index][1], PanelMode);
    }
    Config->ScanoutSize = ALIGN_UP_BY(Config->ScanoutSize, PAGE_SIZE);

    if (!HdgfxResourceOwned(Interface, Bar2 + Scanout->Surface, (ULONG)Config->ScanoutSize) ||
        !HdgfxGttMapped(Interface, Bar0, Scanout->Surface, Config->ScanoutSize))
    {
        DPRINT1("Aperture 0x%I64x cannot hold a %I64u-byte surface\n", Bar2, Config->ScanoutSize);
        return STATUS_NOT_SUPPORTED;
    }

    Address.QuadPart = (LONGLONG)(Bar2 + Scanout->Surface);
    Mapping = MmMapIoSpace(Address, (SIZE_T)Config->ScanoutSize, MmWriteCombined);
    if (Mapping == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Mapping, (SIZE_T)Config->ScanoutSize);
    MmUnmapIoSpace(Mapping, (SIZE_T)Config->ScanoutSize);

    Status = HdgfxScanoutCommit(Scanout, &Scanout->PanelModes[0], Config->Modes[0].Width, Config->Modes[0].Height,
                                Config->Modes[0].Pitch);
    if (!NT_SUCCESS(Status))
        return Status;

    Config->Width = Config->Modes[0].Width;
    Config->Height = Config->Modes[0].Height;
    Config->Format = D3DDDIFMT_X8R8G8B8;
    Config->ScanoutPhysicalAddress = Address;
    Config->ScanoutPitch = Config->Modes[0].Pitch;
    Scanout->FirmwareSurface = Scanout->Surface;
    Config->CurrentMode = 0;
    Config->SetMode = HdgfxSetMode;
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxComputeSlabs(
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    SIZE_T WorkingSetSize;
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < Config->ModeCount; Index++)
    {
        Status = SoftGpu2dComputeAllocationSlabSize(Config->Modes[Index].Width, Config->Modes[Index].Height,
                                                    HDGFX_WORKING_SURFACE_COUNT, MAXULONG_PTR, &WorkingSetSize);
        if (!NT_SUCCESS(Status))
            return Status;
        Config->Modes[Index].AllocationSlabSize = min(WorkingSetSize, SOFTGPU_MAX_ALLOCATION_SLAB_SIZE);
    }

    Status = SoftGpu2dComputeAllocationSlabSize(Config->Width, Config->Height,
                                                HDGFX_WORKING_SURFACE_COUNT, MAXULONG_PTR, &WorkingSetSize);
    if (!NT_SUCCESS(Status))
        return Status;
    Config->MinimumAllocationSlabSize = min(WorkingSetSize, SOFTGPU_MAX_ALLOCATION_SLAB_SIZE);
    return STATUS_SUCCESS;
}

NTSTATUS
SoftGpuPlatformValidatePdo(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    return PhysicalDeviceObject != NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS
SoftGpuPlatformQueryStart(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    DXGK_DISPLAY_INFORMATION PostDisplay;
    ULONGLONG VisibleLength;
    PHDGFX_SCANOUT Scanout;
    NTSTATUS Status;

    if (Device == NULL || DxgkInterface == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Config, sizeof(*Config));
    Status = SoftGpuAcquirePostDisplay(DxgkInterface, &PostDisplay, &VisibleLength);
    if (!NT_SUCCESS(Status))
        return Status;

    Scanout = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Scanout), HDGFX_TAG);
    if (Scanout == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    KeInitializeMutex(&Scanout->Lock, 0);
    Scanout->LvdsActive = TRUE;

    Status = HdgfxScanoutStart(DxgkInterface, Scanout, Config);
    if (NT_SUCCESS(Status))
    {
        Device->PlatformContext = Scanout;
        Config->HighestFrameBufferAddress.QuadPart = MAXULONG;
        HdgfxCrtStart(Device, Scanout, Config);
    }
    else
    {
        HdgfxScanoutFree(Scanout);
        if (PostDisplay.Width == 0)
            return Status;

        DPRINT1("Scan-out setup failed 0x%08lx; keeping the %lux%lu firmware framebuffer\n",
                Status, PostDisplay.Width, PostDisplay.Height);
        RtlZeroMemory(Config, sizeof(*Config));
        Config->Width = PostDisplay.Width;
        Config->Height = PostDisplay.Height;
        Config->Format = PostDisplay.ColorFormat;
        Config->ScanoutPhysicalAddress = PostDisplay.PhysicAddress;
        Config->ScanoutPitch = PostDisplay.Pitch;
        Config->ScanoutSize = VisibleLength;
    }
    return HdgfxComputeSlabs(Config);
}

ULONG
SoftGpuPlatformDmaBufferPrivateDataSize(VOID)
{
    return 0;
}

ULONG
SoftGpuPlatformDmaBufferSegmentSet(VOID)
{
    return 1UL << (HDGFX_DMA_SEGMENT_ID - 1);
}

static VOID
HdgfxCursorCommit(
    _In_ PHDGFX_SCANOUT Scanout)
{
    BOOLEAN Active[HDGFX_DISPLAY_PIPES];
    ULONG Pipe;

    Active[0] = Scanout->LvdsActive && !Scanout->DisplayOff;
    Active[1] = Scanout->CrtActive && !Scanout->CrtDisplayOff;
    for (Pipe = 0; Pipe < HDGFX_DISPLAY_PIPES; Pipe++)
    {
        if (!Active[Pipe])
            continue;
        WRITE_REGISTER_ULONG((PULONG)(Scanout->Registers + HDGFX_REG(CUR_CNTR, Pipe)), Scanout->CursorControl);
        WRITE_REGISTER_ULONG((PULONG)(Scanout->Registers + HDGFX_REG(CUR_POS, Pipe)), Scanout->CursorPosition);
        WRITE_REGISTER_ULONG((PULONG)(Scanout->Registers + HDGFX_REG(CUR_BASE, Pipe)), Scanout->CursorAddress);
    }
}

static VOID
HdgfxPlaneCommit(
    _In_ PHDGFX_SCANOUT Scanout)
{
    BOOLEAN Active[HDGFX_DISPLAY_PIPES];
    ULONG Pipe;

    Active[0] = Scanout->LvdsActive && !Scanout->DisplayOff;
    Active[1] = Scanout->CrtActive && !Scanout->CrtDisplayOff;
    for (Pipe = 0; Pipe < HDGFX_DISPLAY_PIPES; Pipe++)
    {
        if (!Active[Pipe])
            continue;
        WRITE_REGISTER_ULONG((PULONG)(Scanout->Registers + HDGFX_REG(DSP_STRIDE, Pipe)), Scanout->Pitch);
        WRITE_REGISTER_ULONG((PULONG)(Scanout->Registers + HDGFX_REG(DSP_SURF, Pipe)), Scanout->Surface);
    }
}

static VOID
HdgfxDirectScanoutStop(
    _Inout_ PSOFTGPU_DEVICE Device,
    _Inout_ PHDGFX_SCANOUT Scanout)
{
    LARGE_INTEGER Frame;
    ULONG Index;

    Device->PlatformDirectScanout = FALSE;
    Device->PlatformHardwarePointer = FALSE;
    if (Scanout->SlabGtt == NULL)
        return;

    HdgfxLock(Scanout);
    Scanout->Surface = 0;
    Scanout->CursorControl = HdgfxIlkCursorControl(false);
    HdgfxPlaneCommit(Scanout);
    HdgfxCursorCommit(Scanout);
    HdgfxUnlock(Scanout);
    Frame.QuadPart = -50LL * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Frame);

    if (Scanout->WindowGtt != NULL)
    {
        for (Index = 0; Index < Scanout->WindowPages; Index++)
            WRITE_REGISTER_ULONG(&Scanout->WindowGtt[Index], 0);
        READ_REGISTER_ULONG(&Scanout->WindowGtt[0]);
        MmUnmapIoSpace(Scanout->WindowGtt, Scanout->WindowPages * sizeof(ULONG));
        Scanout->WindowGtt = NULL;
    }
    if (Scanout->WindowPfns != NULL)
    {
        ExFreePoolWithTag(Scanout->WindowPfns, HDGFX_TAG);
        Scanout->WindowPfns = NULL;
    }
    for (Index = 0; Index < Scanout->SlabGttPages + HDGFX_CURSOR_PAGES; Index++)
        WRITE_REGISTER_ULONG(&Scanout->SlabGtt[Index], 0);
    READ_REGISTER_ULONG(&Scanout->SlabGtt[0]);
    MmUnmapIoSpace(Scanout->SlabGtt, (Scanout->SlabGttPages + HDGFX_CURSOR_PAGES) * sizeof(ULONG));
    Scanout->SlabGtt = NULL;
    if (Scanout->Cursor != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Scanout->Cursor, HDGFX_CURSOR_PAGES * PAGE_SIZE, MmWriteCombined);
        Scanout->Cursor = NULL;
    }
}

static VOID
HdgfxWindowStart(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PHDGFX_SCANOUT Scanout,
    _In_ ULONG SlabFirst)
{
    PHYSICAL_ADDRESS Address;
    ULONG Pages = (ULONG)(Device->ScanoutSize / PAGE_SIZE) + 1;
    ULONG First;
    ULONG Index;

    if (SlabFirst < Pages)
        return;
    First = SlabFirst - Pages;
    if ((ULONGLONG)First * PAGE_SIZE < (ULONGLONG)Scanout->FirmwareSurface + Device->ScanoutSize)
    {
        DPRINT1("No aperture room for a %lu-page present window\n", Pages);
        return;
    }
    Scanout->WindowPfns = ExAllocatePoolZero(NonPagedPoolNx, Pages * sizeof(ULONG), HDGFX_TAG);
    Address.QuadPart = (LONGLONG)(Scanout->Bar0 + HDGFX_REG(GTT_PTE, First));
    Scanout->WindowGtt = Scanout->WindowPfns == NULL ? NULL :
                         MmMapIoSpace(Address, Pages * sizeof(ULONG), MmNonCached);
    if (Scanout->WindowGtt == NULL)
    {
        if (Scanout->WindowPfns != NULL)
            ExFreePoolWithTag(Scanout->WindowPfns, HDGFX_TAG);
        Scanout->WindowPfns = NULL;
        return;
    }
    for (Index = 0; Index < Pages; Index++)
        WRITE_REGISTER_ULONG(&Scanout->WindowGtt[Index], 0);
    READ_REGISTER_ULONG(&Scanout->WindowGtt[0]);
    Scanout->WindowPages = Pages;
    Scanout->WindowAddress = First * PAGE_SIZE;
}

NTSTATUS
SoftGpuPlatformStartScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PHDGFX_SCANOUT Scanout = Device->PlatformContext;
    PHYSICAL_ADDRESS Lowest;
    PHYSICAL_ADDRESS Highest;
    PHYSICAL_ADDRESS Boundary;
    PHYSICAL_ADDRESS Address;
    ULONGLONG Physical;
    ULONG Pages;
    ULONG First;
    ULONG Index;

    if (Scanout == NULL || Scanout->ApertureSize == 0 || Device->FrameBuffer == NULL ||
        Device->FrameBufferSize % PAGE_SIZE != 0 ||
        (ULONGLONG)Device->FrameBufferPhys.QuadPart + Device->FrameBufferSize - 1 > MAXULONG)
    {
        DPRINT1("Direct scan-out unavailable; copying frames to the firmware surface\n");
        return STATUS_NOT_SUPPORTED;
    }
    Pages = (ULONG)(Device->FrameBufferSize / PAGE_SIZE);
    if (Scanout->ApertureSize / PAGE_SIZE < HDGFX_RENDER_GTT_PAGES + HDGFX_CURSOR_PAGES + Pages ||
        (Scanout->ApertureSize / PAGE_SIZE - HDGFX_RENDER_GTT_PAGES - HDGFX_CURSOR_PAGES - Pages) * PAGE_SIZE <
            Device->ScanoutSize)
    {
        DPRINT1("Aperture of %lu MB cannot hold the %Iu-byte allocation slab\n",
                Scanout->ApertureSize >> 20, Device->FrameBufferSize);
        return STATUS_NOT_SUPPORTED;
    }

    Lowest.QuadPart = 0;
    Highest.QuadPart = MAXULONG;
    Boundary.QuadPart = 0;
    Scanout->Cursor = MmAllocateContiguousMemorySpecifyCache(HDGFX_CURSOR_PAGES * PAGE_SIZE, Lowest, Highest, Boundary,
                                                             MmWriteCombined);
    First = Scanout->ApertureSize / PAGE_SIZE - HDGFX_RENDER_GTT_PAGES - HDGFX_CURSOR_PAGES - Pages;
    Address.QuadPart = (LONGLONG)(Scanout->Bar0 + HDGFX_REG(GTT_PTE, First));
    Scanout->SlabGtt = Scanout->Cursor == NULL ? NULL :
                       MmMapIoSpace(Address, (Pages + HDGFX_CURSOR_PAGES) * sizeof(ULONG), MmNonCached);
    if (Scanout->SlabGtt == NULL)
    {
        if (Scanout->Cursor != NULL)
            MmFreeContiguousMemorySpecifyCache(Scanout->Cursor, HDGFX_CURSOR_PAGES * PAGE_SIZE, MmWriteCombined);
        Scanout->Cursor = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Scanout->Cursor, HDGFX_CURSOR_PAGES * PAGE_SIZE);

    Scanout->SlabGttPages = Pages;
    Scanout->SlabPhysical = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    Scanout->SlabSize = Device->FrameBufferSize;
    Scanout->SlabAddress = First * PAGE_SIZE;
    Scanout->CursorAddress = (First + Pages) * PAGE_SIZE;
    Scanout->CursorGeneration = 0;
    Scanout->CursorControl = HdgfxIlkCursorControl(false);
    Scanout->CursorPosition = 0;
    for (Index = 0; Index < Pages; Index++)
    {
        Physical = Scanout->SlabPhysical + (ULONGLONG)Index * PAGE_SIZE;
        WRITE_REGISTER_ULONG(&Scanout->SlabGtt[Index], HdgfxIlkGttEntry((ULONG)Physical));
    }
    for (Index = 0; Index < HDGFX_CURSOR_PAGES; Index++)
    {
        Physical = (ULONGLONG)MmGetPhysicalAddress((PUCHAR)Scanout->Cursor + Index * PAGE_SIZE).QuadPart;
        WRITE_REGISTER_ULONG(&Scanout->SlabGtt[Pages + Index], HdgfxIlkGttEntry((ULONG)Physical));
    }
    READ_REGISTER_ULONG(&Scanout->SlabGtt[Pages + HDGFX_CURSOR_PAGES - 1]);
    HdgfxWindowStart(Device, Scanout, First);

    Device->PlatformDirectScanout = TRUE;
    Device->PlatformHardwarePointer = TRUE;
    DPRINT1("Direct scan-out: %Iu-byte slab at 0x%I64x mapped to 0x%lx, cursor at 0x%lx, present window at 0x%lx\n",
            Device->FrameBufferSize, Scanout->SlabPhysical, Scanout->SlabAddress, Scanout->CursorAddress,
            Scanout->WindowAddress);
    return STATUS_SUCCESS;
}

NTSTATUS
SoftGpuPlatformStopScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PHDGFX_SCANOUT Scanout;

    if (Device == NULL || Device->PlatformContext == NULL)
        return STATUS_SUCCESS;

    Scanout = Device->PlatformContext;
    HdgfxDirectScanoutStop(Device, Scanout);
    Device->PlatformContext = NULL;
    HdgfxScanoutFree(Scanout);
    return STATUS_SUCCESS;
}

NTSTATUS
SoftGpuPlatformSetPrimary(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PHYSICAL_ADDRESS PrimaryAddress,
    _In_ ULONG Pitch,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BOOLEAN Visible)
{
    PHDGFX_SCANOUT Scanout = Device->PlatformContext;
    ULONGLONG Offset;

    UNREFERENCED_PARAMETER(Visible);

    if (Scanout == NULL || Scanout->SlabGtt == NULL)
        return STATUS_NOT_SUPPORTED;
    Offset = (ULONGLONG)PrimaryAddress.QuadPart - Scanout->SlabPhysical;
    if ((ULONGLONG)PrimaryAddress.QuadPart < Scanout->SlabPhysical || Offset % PAGE_SIZE != 0 ||
        Pitch == 0 || Pitch % HDGFX_STRIDE_ALIGNMENT != 0 || Height == 0 ||
        Offset + (ULONGLONG)Pitch * Height > Scanout->SlabSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    HdgfxLock(Scanout);
    Scanout->Surface = Scanout->SlabAddress + (ULONG)Offset;
    if (Width == Scanout->Width && Height == Scanout->Height)
    {
        Scanout->Pitch = Pitch;
        HdgfxPlaneCommit(Scanout);
    }
    HdgfxUnlock(Scanout);
    return STATUS_SUCCESS;
}

static ULONGLONG
HdgfxElapsedUs(
    _In_ LARGE_INTEGER Start,
    _In_ LARGE_INTEGER Frequency)
{
    LARGE_INTEGER Now = KeQueryPerformanceCounter(NULL);

    return (ULONGLONG)(Now.QuadPart - Start.QuadPart) * 1000000ULL / (ULONGLONG)Frequency.QuadPart;
}

static BOOLEAN
HdgfxPresentSubmit(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_reads_(Count) const uint32_t *Dwords,
    _In_ ULONG Count,
    _In_ LONG Resets,
    _Out_opt_ PULONG Seqno)
{
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Start;
    HDGFX_MMIO Mmio;
    KIRQL OldIrql;
    ULONG Head;
    BOOLEAN Done = FALSE;

    Start = KeQueryPerformanceCounter(&Frequency);
    HdgfxMmioInit(Adapter->Registers, &Mmio);
    for (;;)
    {
        KeAcquireSpinLock(&Adapter->RingLock, &OldIrql);
        if (!Adapter->RingStarted || Adapter->RingResetting || Adapter->RingResets != Resets)
        {
            KeReleaseSpinLock(&Adapter->RingLock, OldIrql);
            return FALSE;
        }
        Head = HdgfxIlkRingHead(&Mmio);
        if (HdgfxRingFree(&Adapter->Ring, Head) >= HDGFX_RING_RESERVE + Count * sizeof(uint32_t))
        {
            Done = Count == 0 || HdgfxRingEmit(&Adapter->Ring, Head, Dwords, Count);
            if (Done && Seqno != NULL)
            {
                Done = HdgfxRingFence(Adapter, &Mmio, Head);
                *Seqno = Adapter->Seqno;
            }
            else if (Done)
            {
                Adapter->RingUnfenced = TRUE;
                HdgfxIlkAdvanceRing(&Mmio, &Adapter->Ring);
            }
            KeReleaseSpinLock(&Adapter->RingLock, OldIrql);
            return Done;
        }
        if (Adapter->RingUnfenced)
            HdgfxRingFence(Adapter, &Mmio, Head);
        KeReleaseSpinLock(&Adapter->RingLock, OldIrql);
        if (HdgfxElapsedUs(Start, Frequency) >= HDGFX_PRESENT_TIMEOUT_US)
            return FALSE;
        KeStallExecutionProcessor(10);
    }
}

static BOOLEAN
HdgfxPresentWait(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG Seqno,
    _In_ LONG Resets)
{
    volatile ULONG *Status = (volatile ULONG *)Adapter->StatusPage;
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Start;

    Start = KeQueryPerformanceCounter(&Frequency);
    for (;;)
    {
        if (Adapter->RingResets != Resets)
            return FALSE;
        if ((LONG)(Status[HDGFX_RING_SEQNO_INDEX] - Seqno) >= 0)
            return TRUE;
        if (HdgfxElapsedUs(Start, Frequency) >= HDGFX_PRESENT_TIMEOUT_US)
            return FALSE;
        KeStallExecutionProcessor(10);
    }
}

static PMDL
HdgfxWindowMap(
    _Inout_ PHDGFX_SCANOUT Scanout,
    _In_ const UCHAR *Source,
    _In_ SIZE_T Size)
{
    PPFN_NUMBER Pfns;
    PMDL Mdl;
    ULONG Pages;
    ULONG Index;
    BOOLEAN Locked = FALSE;
    BOOLEAN Changed = FALSE;

    if (Scanout->WindowGtt == NULL || Size > MAXULONG ||
        ADDRESS_AND_SIZE_TO_SPAN_PAGES(Source, Size) > Scanout->WindowPages)
    {
        return NULL;
    }
    Mdl = IoAllocateMdl((PVOID)Source, (ULONG)Size, FALSE, FALSE, NULL);
    if (Mdl == NULL)
        return NULL;
    _SEH2_TRY
    {
        MmProbeAndLockPages(Mdl, KernelMode, IoReadAccess);
        Locked = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;
    if (!Locked)
    {
        IoFreeMdl(Mdl);
        return NULL;
    }

    Pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Source, Size);
    Pfns = MmGetMdlPfnArray(Mdl);
    for (Index = 0; Index < Pages; Index++)
    {
        if (Pfns[Index] > (MAXULONG >> PAGE_SHIFT))
        {
            MmUnlockPages(Mdl);
            IoFreeMdl(Mdl);
            return NULL;
        }
    }
    for (Index = 0; Index < Pages; Index++)
    {
        if (Scanout->WindowPfns[Index] != (ULONG)Pfns[Index])
        {
            Scanout->WindowPfns[Index] = (ULONG)Pfns[Index];
            WRITE_REGISTER_ULONG(&Scanout->WindowGtt[Index], HdgfxIlkGttEntry((ULONG)Pfns[Index] << PAGE_SHIFT));
            Changed = TRUE;
        }
    }
    if (Changed)
        READ_REGISTER_ULONG(&Scanout->WindowGtt[Pages - 1]);
    return Mdl;
}

static VOID
HdgfxFlushRect(
    _In_ const UCHAR *Source,
    _In_ ULONG Pitch,
    _In_ const HDGFX_BLT_RECT *Rect)
{
    ULONG Row;

    for (Row = Rect->Top; Row < Rect->Bottom; Row++)
    {
        KeInvalidateRangeAllCaches((PVOID)(Source + (SIZE_T)Row * Pitch + (SIZE_T)Rect->Left * sizeof(ULONG)),
                                   (Rect->Right - Rect->Left) * sizeof(ULONG));
    }
}

static BOOLEAN
HdgfxPresentRect(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ const RECT *Input,
    _Out_ HDGFX_BLT_RECT *Rect)
{
    RECT Clipped = *Input;

    if (Clipped.left < 0)
        Clipped.left = 0;
    if (Clipped.top < 0)
        Clipped.top = 0;
    if (Clipped.right > (LONG)Device->Width)
        Clipped.right = (LONG)Device->Width;
    if (Clipped.bottom > (LONG)Device->Height)
        Clipped.bottom = (LONG)Device->Height;
    if (Clipped.left >= Clipped.right || Clipped.top >= Clipped.bottom)
        return FALSE;
    Rect->Left = (uint32_t)Clipped.left;
    Rect->Top = (uint32_t)Clipped.top;
    Rect->Right = (uint32_t)Clipped.right;
    Rect->Bottom = (uint32_t)Clipped.bottom;
    return TRUE;
}

NTSTATUS
SoftGpuPlatformPresentDisplayOnly(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly)
{
    PHDGFX_ADAPTER Adapter = Device->PlatformServicesContext;
    PHDGFX_SCANOUT Scanout = Device->PlatformContext;
    const UCHAR *Source = PresentDisplayOnly->pSource;
    uint32_t Dwords[HDGFX_PRESENT_BATCH * 8];
    HDGFX_BLT_SURFACE SourceSurface;
    HDGFX_BLT_SURFACE Destination;
    HDGFX_BLT_RECT Rect;
    ULONGLONG SourcePhysical;
    ULONGLONG DestinationOffset;
    ULONGLONG DestinationSize;
    SIZE_T SourceOffset;
    SIZE_T SourceSize;
    KIRQL OldIrql;
    ULONG DestinationBase;
    ULONG DestinationPitch;
    ULONG Count = 0;
    ULONG Index;
    ULONG Seqno = 0;
    LONG Resets;
    PMDL Mdl = NULL;
    BOOLEAN Submitted = TRUE;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    PAGED_CODE();

    if (Adapter == NULL || Scanout == NULL || Scanout->SlabGtt == NULL ||
        Device->Width > HDGFX_BLT_MAX_COORDINATE || Device->Height > HDGFX_BLT_MAX_COORDINATE ||
        (ULONG)PresentDisplayOnly->Pitch > HDGFX_BLT_MAX_COORDINATE ||
        (ULONG)PresentDisplayOnly->Pitch % sizeof(ULONG) != 0 ||
        !ExAcquireRundownProtection(&Adapter->Rundown))
    {
        return STATUS_NOT_SUPPORTED;
    }
    Resets = Adapter->RingResets;
    if (!Adapter->RingStarted)
        goto Cleanup;

    SourceSize = (SIZE_T)(ULONG)PresentDisplayOnly->Pitch * Device->Height;
    SourcePhysical = (ULONGLONG)MmGetPhysicalAddress((PVOID)Source).QuadPart;
    if (NT_SUCCESS(SoftGpu2dResolveSlabRange(Scanout->SlabPhysical, (SIZE_T)Scanout->SlabSize, SourcePhysical,
                                             SourceSize, &SourceOffset)) &&
        (ULONGLONG)MmGetPhysicalAddress((PVOID)(Source + SourceSize - 1)).QuadPart == SourcePhysical + SourceSize - 1)
    {
        SourceSurface.Address = Scanout->SlabAddress + (ULONG)SourceOffset;
    }
    else
    {
        Mdl = HdgfxWindowMap(Scanout, Source, SourceSize);
        if (Mdl == NULL)
            goto Cleanup;
        SourceSurface.Address = Scanout->WindowAddress + MmGetMdlByteOffset(Mdl);
        Count = HdgfxMiFlush(Dwords);
        Dwords[Count++] = 0;
    }

    KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
    if (Device->CurrentPrimaryValid)
    {
        DestinationBase = Scanout->SlabAddress;
        DestinationOffset = Device->CurrentPrimaryOffset;
        DestinationPitch = Device->CurrentPrimaryPitch;
        DestinationSize = Scanout->SlabSize;
    }
    else
    {
        DestinationBase = Scanout->FirmwareSurface;
        DestinationOffset = 0;
        DestinationPitch = Device->ScanoutPitch;
        DestinationSize = Device->Scanout != NULL ? Device->ScanoutSize : 0;
    }
    KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);
    if (DestinationPitch < Device->Width * sizeof(ULONG) || DestinationPitch > HDGFX_BLT_MAX_COORDINATE ||
        DestinationPitch % sizeof(ULONG) != 0 ||
        DestinationOffset > DestinationSize ||
        (DestinationSize - DestinationOffset) / DestinationPitch < Device->Height)
    {
        goto Cleanup;
    }

    SourceSurface.Pitch = (uint32_t)PresentDisplayOnly->Pitch;
    Destination.Address = DestinationBase + (ULONG)DestinationOffset;
    Destination.Pitch = DestinationPitch;
    for (Index = 0; Submitted && Index < PresentDisplayOnly->NumMoves + PresentDisplayOnly->NumDirtyRects; Index++)
    {
        const RECT *Input = Index < PresentDisplayOnly->NumMoves ?
                            &PresentDisplayOnly->pMoves[Index].DestRect :
                            &PresentDisplayOnly->pDirtyRect[Index - PresentDisplayOnly->NumMoves];

        if (!HdgfxPresentRect(Device, Input, &Rect))
            continue;
        if (Mdl != NULL)
            HdgfxFlushRect(Source, SourceSurface.Pitch, &Rect);
        Count += HdgfxBltCopy(Dwords + Count, &Destination, &Rect, &SourceSurface, Rect.Left, Rect.Top);
        if (Count + 8 > RTL_NUMBER_OF(Dwords))
        {
            KeMemoryBarrier();
            Submitted = HdgfxPresentSubmit(Adapter, Dwords, Count, Resets, NULL);
            Count = 0;
        }
    }
    KeMemoryBarrier();
    if (Submitted && HdgfxPresentSubmit(Adapter, Dwords, Count, Resets, &Seqno) &&
        HdgfxPresentWait(Adapter, Seqno, Resets))
    {
        Status = STATUS_SUCCESS;
    }
    else if (Adapter->RingResets == Resets)
    {
        DPRINT1("Present blit did not complete: seqno %lu of %lu, copying on the CPU\n",
                ((volatile ULONG *)Adapter->StatusPage)[HDGFX_RING_SEQNO_INDEX], Seqno);
    }

Cleanup:
    if (Mdl != NULL)
    {
        MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
    }
    ExReleaseRundownProtection(&Adapter->Rundown);
    return Status;
}

NTSTATUS
SoftGpuPlatformUpdatePointer(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PHDGFX_SCANOUT Scanout = Device->PlatformContext;
    LONG X;
    LONG Y;

    if (Scanout == NULL || Scanout->Cursor == NULL)
        return STATUS_NOT_SUPPORTED;

    HdgfxLock(Scanout);
    if (Device->PointerShapeValid && Scanout->CursorGeneration != Device->PointerShapeGeneration)
    {
        RtlCopyMemory(Scanout->Cursor, Device->PointerPixels, HDGFX_CURSOR_SIZE * HDGFX_CURSOR_SIZE * sizeof(ULONG));
        KeMemoryBarrier();
        Scanout->CursorGeneration = Device->PointerShapeGeneration;
    }
    X = Device->PointerX - (LONG)Device->PointerHotX;
    Y = Device->PointerY - (LONG)Device->PointerHotY;
    Scanout->CursorControl = HdgfxIlkCursorControl(Device->PointerVisible && Device->PointerShapeValid);
    Scanout->CursorPosition = HdgfxIlkCursorPosition(X, Y);
    HdgfxCursorCommit(Scanout);
    HdgfxUnlock(Scanout);
    return STATUS_SUCCESS;
}

VOID
SoftGpuPlatformScanoutWritten(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const RECT *Rect)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Rect);
}

VOID
SoftGpuPlatformFillNodeMetadata(
    _In_ ULONG NodeOrdinal,
    _Out_ DXGKARG_GETNODEMETADATA *GetNodeMetadata)
{
    static const WCHAR FriendlyName[] = L"3D";

    UNREFERENCED_PARAMETER(NodeOrdinal);
    GetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
    RtlCopyMemory(GetNodeMetadata->FriendlyName, FriendlyName, sizeof(FriendlyName));
}

static ULONG
HdgfxScanLine(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ ULONG Pipe)
{
    return HDGFX_GET(READ_REGISTER_ULONG((PULONG)(Scanout->Registers + HDGFX_REG(PIPE_DSL, Pipe))), PIPE_DSL_LINE);
}

static ULONG
HdgfxScanPipe(
    _In_ PHDGFX_SCANOUT Scanout,
    _Out_ PULONG VActive)
{
    if ((!Scanout->LvdsActive || Scanout->DisplayOff) &&
        Scanout->CrtActive && !Scanout->CrtDisplayOff && Scanout->CrtTimingValid)
    {
        *VActive = Scanout->CrtTiming.Timings.VActive;
        return HDGFX_CRT_PIPE;
    }
    *VActive = Scanout->Panel.Timings.VActive;
    return 0;
}

static BOOLEAN
HdgfxWaitForBlank(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ BOOLEAN InBlank)
{
    ULONG Elapsed;
    ULONG VActive;
    ULONG Pipe = HdgfxScanPipe(Scanout, &VActive);

    for (Elapsed = 0; Elapsed < HDGFX_VBLANK_TIMEOUT_US; Elapsed += HDGFX_VBLANK_POLL_US)
    {
        if ((HdgfxScanLine(Scanout, Pipe) >= VActive) == InBlank)
            return TRUE;
        KeStallExecutionProcessor(HDGFX_VBLANK_POLL_US);
    }
    return FALSE;
}

VOID
SoftGpuPlatformInitializeTiming(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PHDGFX_SCANOUT Scanout;

    if (Device == NULL)
        return;

    Scanout = Device->PlatformContext;
    InterlockedExchange(&Device->ScanoutVBlankAvailable,
                        Scanout != NULL && HdgfxWaitForBlank(Scanout, FALSE) && HdgfxWaitForBlank(Scanout, TRUE));
}

BOOLEAN
SoftGpuPlatformWaitForVerticalBlank(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PHDGFX_SCANOUT Scanout;

    if (Device == NULL || InterlockedCompareExchange(&Device->ScanoutVBlankAvailable, 0, 0) == 0)
        return FALSE;

    Scanout = Device->PlatformContext;
    if (Scanout != NULL && HdgfxWaitForBlank(Scanout, FALSE) && HdgfxWaitForBlank(Scanout, TRUE))
        return TRUE;

    InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
    return FALSE;
}

NTSTATUS
SoftGpuPlatformQueryScanLine(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGKARG_GETSCANLINE GetScanLine)
{
    PHDGFX_SCANOUT Scanout;
    ULONG VActive;
    ULONG Pipe;
    ULONG Line;

    if (Device == NULL || GetScanLine == NULL ||
        (GetScanLine->VidPnTargetId != 0 && GetScanLine->VidPnTargetId != HDGFX_CRT_CHILD_UID))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Scanout = Device->PlatformContext;
    if (Scanout == NULL || (GetScanLine->VidPnTargetId == HDGFX_CRT_CHILD_UID && !Scanout->CrtTimingValid))
        return STATUS_NOT_SUPPORTED;

    Pipe = GetScanLine->VidPnTargetId == HDGFX_CRT_CHILD_UID ? HDGFX_CRT_PIPE : 0;
    VActive = Pipe == HDGFX_CRT_PIPE ? Scanout->CrtTiming.Timings.VActive : Scanout->Panel.Timings.VActive;
    Line = HdgfxScanLine(Scanout, Pipe);
    GetScanLine->ScanLine = Line;
    GetScanLine->InVerticalBlank = Line >= VActive;
    return STATUS_SUCCESS;
}

MEMORY_CACHING_TYPE
SoftGpuPlatformSegmentCacheType(VOID)
{
    return MmWriteCombined;
}

NTSTATUS
SoftGpuPlatformQueryDescriptor(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    ULONG Length;

    if (Device->EdidLength == 0)
        return STATUS_MONITOR_NO_DESCRIPTOR;
    if (DeviceDescriptor->DescriptorOffset >= Device->EdidLength)
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    if (DeviceDescriptor->DescriptorBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    Length = min(DeviceDescriptor->DescriptorLength, Device->EdidLength - DeviceDescriptor->DescriptorOffset);
    RtlCopyMemory(DeviceDescriptor->DescriptorBuffer, Device->Edid + DeviceDescriptor->DescriptorOffset, Length);
    DeviceDescriptor->DescriptorLength = Length;
    return STATUS_SUCCESS;
}
