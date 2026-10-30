/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Adapter lifecycle, interrupts, LVDS scan-out, display state readout, VBT and panel backlight
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx.h"
#include "softgpu_2d_core.h"
#include <pseh/pseh2.h>

#define HDGFX_PCI_BAR0              0x10UL
#define HDGFX_PCI_BAR2              0x18UL
#define HDGFX_PCI_ASLS              0xFCUL
#define HDGFX_MMIO_SIZE             0x200000UL
#define HDGFX_BLT_RING_BASE         0x20000UL
#define HDGFX_OPREGION_SIZE         0x2000UL
#define HDGFX_BLC_LEVEL_COUNT       100UL
#define HDGFX_VBT_MINIMUM_LIMIT     64UL
#define HDGFX_VBT_MINIMUM_SCALE     255UL
#define HDGFX_VBT_BACKLIGHT_PWM     2UL
#define HDGFX_ACPI_ID_INTERNAL_PANEL 0x0400UL
#define HDGFX_WORKING_SURFACE_COUNT 12UL
#define HDGFX_BLT_TEST_PITCH        128UL
#define HDGFX_CURSOR_PAGES          (HDGFX_CURSOR_SIZE * HDGFX_CURSOR_SIZE * 4 / PAGE_SIZE)
#define HDGFX_DISPLAY_PIPES         2UL
#define HDGFX_BLT_TEST_WIDTH        (HDGFX_BLT_TEST_PITCH / 4)
#define HDGFX_BLT_TEST_HEIGHT       16UL
#define HDGFX_RING_TEST_TIMEOUT_US  100000UL
#define HDGFX_BLT_MAX_COORDINATE    0x7FFFUL
#define HDGFX_ACPI_OUTPUT_SIZE      512UL
#define HDGFX_ACPI_DOD_ID_SCHEME    0x80000000UL
#define HDGFX_ACPI_DOD_TYPE_MASK    0x00000F00UL
#define HDGFX_ACPI_DOD_TYPE_PANEL   0x00000400UL
#define HDGFX_ACPI_DOD_ADR_MASK     0x0000FFFFUL
#define HDGFX_VSYNC_PERIOD          166667LL
#define HDGFX_DEAD_ADAPTER_MAGIC    0xDEAD4146UL

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
HdgfxGttFlush(
    _In_ PUCHAR Registers,
    _In_ const HDGFX_GENERATION *Generation)
{
    if (Registers != NULL && Generation->Gen >= 6)
        WRITE_REGISTER_ULONG((PULONG)(Registers + HDGFX_REG(GFX_FLUSH_CONTROL, 0)), 1);
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
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    Adapter->OpRegion = HdgfxMapOpRegion(&Adapter->DxgkInterface, &Adapter->Vbt, &Adapter->VbtPresent);
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
    _In_ PHDGFX_ADAPTER Adapter)
{
    ULONG InputStorage[(FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) +
                        ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) + sizeof(ULONG) - 1) / sizeof(ULONG)];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX Input = (PACPI_EVAL_INPUT_BUFFER_COMPLEX)InputStorage;

    if (Adapter->DxgkInterface.DxgkCbEvalAcpiMethod == NULL)
        return;

    RtlZeroMemory(InputStorage, sizeof(InputStorage));
    Input->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    Input->MethodNameAsUlong = ACPI_METHOD_DISPLAY_DOS;
    Input->Size = sizeof(InputStorage) - FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
    Input->ArgumentCount = 1;
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Input->Argument,
                                     ACPI_ARG_ENABLE_SWITCH_EVENT | ACPI_ARG_DISABLE_AUTO_LCD_BRIGHTNESS);
    (VOID)Adapter->DxgkInterface.DxgkCbEvalAcpiMethod(Adapter->DxgkInterface.DeviceHandle,
                                                      DISPLAY_ADAPTER_HW_ID,
                                                      Input,
                                                      sizeof(InputStorage),
                                                      NULL,
                                                      0);
}

static VOID
HdgfxStartBacklight(
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
    HdgfxDisableFirmwareBrightness(Adapter);
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
    _In_ PHDGFX_ADAPTER Adapter)
{
    const HDGFX_GENERATION *Generation = Adapter->Generation;
    BOOLEAN Asle = Adapter->OpRegion != NULL && Adapter->InternalPanel && Adapter->BacklightMaximum != 0;
    PHDGFX_SCANOUT Scanout = Adapter->Scanout;
    BOOLEAN Hotplug = Generation->CrtHotplugInterrupt != 0 && Scanout != NULL && Scanout->CrtSupported;
    ULONG Crt = Generation->CrtHotplugInterrupt;
    HDGFX_MMIO Mmio;
    HDGFX_MMIO Io;
    ULONG Mask;

    ULONG Gt = Adapter->RingStarted ? Generation->GtInterrupts : 0;

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
    if (Adapter->DxgkInterface.DxgkCbSynchronizeExecution == NULL ||
        !NT_SUCCESS(Adapter->DxgkInterface.DxgkCbSynchronizeExecution(Adapter->DxgkInterface.DeviceHandle,
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
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ BOOLEAN Indicate);

static VOID
NTAPI
HdgfxHotplugWorker(
    _In_ PVOID Context)
{
    PHDGFX_ADAPTER Adapter = Context;

    InterlockedExchange(&Adapter->HotplugQueued, 0);
    HdgfxCrtRefresh(Adapter, TRUE);
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
        if (!HdgfxIlkStopRing(&Mmio, &Adapter->Ring) ||
            (Adapter->Generation->Gen >= 7 && !HdgfxIlkStopRing(&Mmio, &Adapter->BltRing)))
        {
            DPRINT1("Render ring busy at stop, head 0x%lx tail 0x%lx; keeping its memory\n",
                    HdgfxIlkRingHead(&Mmio, Adapter->ActiveRing), Adapter->ActiveRing->Tail);
            return;
        }
        Adapter->RingStarted = FALSE;
        if (Adapter->Generation->Gen >= 7)
            HdgfxIvbForceWakeRelease(&Mmio);
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
        MmFreeContiguousMemorySpecifyCache(Adapter->RingMemory, HDGFX_RING_COUNT * HDGFX_RING_SIZE, MmNonCached);
        Adapter->RingMemory = NULL;
    }
    if (Adapter->StatusPage != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Adapter->StatusPage, PAGE_SIZE,
                                           Adapter->Generation->Gen >= 6 ? MmNonCached : MmCached);
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

    if (!NT_SUCCESS(HdgfxReadConfig(&Adapter->DxgkInterface, &Bar0, HDGFX_PCI_BAR0, sizeof(Bar0))) ||
        !NT_SUCCESS(HdgfxReadConfig(&Adapter->DxgkInterface, &Bar2, HDGFX_PCI_BAR2, sizeof(Bar2))))
    {
        return;
    }
    Bar0 &= ~0xFULL;
    Bar2 &= ~0xFULL;
    Aperture = HdgfxFindMemoryResource(&Adapter->DxgkInterface, Bar2, PAGE_SIZE);
    if (Aperture == NULL || (ULONGLONG)Aperture->u.Memory.Start.QuadPart != Bar2 ||
        Aperture->u.Memory.Length < HDGFX_RENDER_GTT_PAGES * PAGE_SIZE)
    {
        DPRINT1("No aperture at 0x%I64x for the render ring\n", Bar2);
        return;
    }

    Lowest.QuadPart = 0;
    Highest.QuadPart = MAXULONG;
    Boundary.QuadPart = 0;
    Adapter->ActiveRing = &Adapter->Ring;
    Adapter->RingMemory = MmAllocateContiguousMemorySpecifyCache(HDGFX_RING_COUNT * HDGFX_RING_SIZE, Lowest, Highest,
                                                                 Boundary, MmNonCached);
    Adapter->StatusPage = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, Lowest, Highest, Boundary,
                                                                 Adapter->Generation->Gen >= 6 ? MmNonCached : MmCached);
    Adapter->ScratchPage = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, Lowest, Highest, Boundary, MmNonCached);
    GttFirst = Aperture->u.Memory.Length / PAGE_SIZE - HDGFX_RENDER_GTT_PAGES;
    Address.QuadPart = (LONGLONG)(Bar0 + HDGFX_REG(GTT_PTE, GttFirst));
    if (Adapter->RingMemory == NULL || Adapter->StatusPage == NULL || Adapter->ScratchPage == NULL ||
        !HdgfxResourceOwned(&Adapter->DxgkInterface, (ULONGLONG)Address.QuadPart,
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
    RtlZeroMemory(Adapter->RingMemory, HDGFX_RING_COUNT * HDGFX_RING_SIZE);
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
        WRITE_REGISTER_ULONG(&Adapter->GttWindow[Index],
                             Adapter->Generation->GttEntry((ULONGLONG)MmGetPhysicalAddress(Page).QuadPart));
    }
    READ_REGISTER_ULONG(&Adapter->GttWindow[HDGFX_RENDER_GTT_PAGES - 1]);
    HdgfxGttFlush(Adapter->Registers, Adapter->Generation);
    if (Adapter->DmaMemory != NULL)
        Adapter->DmaBase = GttFirst * PAGE_SIZE;

    Adapter->Ring.Commands = Adapter->RingMemory;
    Adapter->Ring.Size = HDGFX_RING_SIZE;
    Adapter->Ring.Address = (GttFirst + HDGFX_RING_GTT_INDEX) * PAGE_SIZE;
    Adapter->Ring.StatusAddress = (GttFirst + HDGFX_STATUS_GTT_INDEX) * PAGE_SIZE;
    Adapter->Ring.Gen = Adapter->Generation->Gen;
    Adapter->Ring.UserInterrupt = HDGFX_SET(GT_RENDER_USER, 1);
    Adapter->ScratchAddress = (GttFirst + HDGFX_SCRATCH_GTT_INDEX) * PAGE_SIZE;
    if (!Adapter->Generation->Render)
        return;
    HdgfxMmioInit(Adapter->Registers, &Mmio);
    if (Adapter->Generation->Gen >= 7)
    {
        Adapter->BltRing = Adapter->Ring;
        Adapter->BltRing.Commands = (uint32_t *)((PUCHAR)Adapter->RingMemory + HDGFX_RING_SIZE);
        Adapter->BltRing.Address = Adapter->Ring.Address + HDGFX_RING_SIZE;
        Adapter->BltRing.Base = HDGFX_BLT_RING_BASE;
        Adapter->BltRing.StatusRegister = HDGFX_REG(BLT_HWS_PGA, 0);
        Adapter->BltRing.UserInterrupt = HDGFX_SET(GT_BLT_USER, 1);
        Adapter->BltRing.Blitter = TRUE;
        Adapter->Ring.StatusRegister = HDGFX_REG(RENDER_HWS_PGA, 0);
        if (!HdgfxIvbForceWake(&Mmio))
        {
            DPRINT1("Forcewake failed, ack 0x%lx thread status 0x%lx\n",
                    HdgfxReadRegister32(Adapter, HDGFX_REG(FORCEWAKE_MT_ACK, 0)),
                    HdgfxReadRegister32(Adapter, HDGFX_REG(GT_THREAD_STATUS, 0)));
            HdgfxStopRender(Adapter);
            return;
        }
        HdgfxIvbInitEngine(&Mmio);
    }
    Adapter->RingStarted = HdgfxIlkStartRing(&Mmio, &Adapter->Ring) &&
                           (Adapter->Generation->Gen < 7 || HdgfxIlkStartRing(&Mmio, &Adapter->BltRing));
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

VOID
HdgfxApertureClear(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    ULONG Entry;
    ULONG Index;

    if (Adapter->ApertureGtt == NULL || Adapter->ScratchPage == NULL)
        return;
    Entry = Adapter->Generation->GttEntry((ULONGLONG)MmGetPhysicalAddress(Adapter->ScratchPage).QuadPart);
    for (Index = 0; Index < Adapter->ApertureSize / PAGE_SIZE; Index++)
        WRITE_REGISTER_ULONG(&Adapter->ApertureGtt[Index], Entry);
    READ_REGISTER_ULONG(&Adapter->ApertureGtt[Adapter->ApertureSize / PAGE_SIZE - 1]);
    HdgfxGttFlush(Adapter->Registers, Adapter->Generation);
}

static VOID
HdgfxStopAperture(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    ULONG Pages = Adapter->ApertureSize / PAGE_SIZE;
    ULONG Index;

    if (Adapter->ApertureGtt != NULL)
    {
        for (Index = 0; Index < Pages; Index++)
            WRITE_REGISTER_ULONG(&Adapter->ApertureGtt[Index], 0);
        READ_REGISTER_ULONG(&Adapter->ApertureGtt[Pages - 1]);
        MmUnmapIoSpace(Adapter->ApertureGtt, Pages * sizeof(ULONG));
    }
    if (Adapter->ApertureCpu != NULL)
        MmUnmapIoSpace(Adapter->ApertureCpu, Adapter->ApertureSize);
    Adapter->ApertureGtt = NULL;
    Adapter->ApertureCpu = NULL;
    Adapter->ApertureBase = 0;
    Adapter->ApertureSize = 0;
}

static VOID
HdgfxStartAperture(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    PHDGFX_SCANOUT Scanout = Adapter->Scanout;
    PHYSICAL_ADDRESS Address;
    ULONGLONG Bar2 = 0;
    ULONG First;
    ULONG Pages;

    if (Scanout == NULL || Scanout->SlabGtt == NULL || Adapter->ScratchPage == NULL ||
        !NT_SUCCESS(HdgfxReadConfig(&Adapter->DxgkInterface, &Bar2, HDGFX_PCI_BAR2, sizeof(Bar2))))
    {
        return;
    }
    Bar2 &= ~0xFULL;
    First = (ULONG)(ALIGN_UP_BY((ULONGLONG)Scanout->FirmwareSurface + Scanout->FirmwareSize, PAGE_SIZE) / PAGE_SIZE);
    if (First == 0)
        First = 1;
    if (First >= Scanout->SlabAddress / PAGE_SIZE)
        return;
    Pages = Scanout->SlabAddress / PAGE_SIZE - First;
    Address.QuadPart = (LONGLONG)(Scanout->Bar0 + HDGFX_REG(GTT_PTE, First));
    if (!HdgfxResourceOwned(&Adapter->DxgkInterface, (ULONGLONG)Address.QuadPart, Pages * sizeof(ULONG)) ||
        !HdgfxResourceOwned(&Adapter->DxgkInterface, Bar2 + (ULONGLONG)First * PAGE_SIZE, Pages * PAGE_SIZE))
    {
        DPRINT1("GTT pages %lu-%lu are not in the adapter's resources\n", First, First + Pages - 1);
        return;
    }
    Adapter->ApertureGtt = MmMapIoSpace(Address, Pages * sizeof(ULONG), MmNonCached);
    Address.QuadPart = (LONGLONG)(Bar2 + (ULONGLONG)First * PAGE_SIZE);
    if (Adapter->ApertureGtt != NULL)
        Adapter->ApertureCpu = MmMapIoSpace(Address, Pages * PAGE_SIZE, MmWriteCombined);
    Adapter->ApertureBase = First * PAGE_SIZE;
    Adapter->ApertureSize = Pages * PAGE_SIZE;
    if (Adapter->ApertureCpu == NULL)
    {
        DPRINT1("Cannot map the %lu-page aperture segment\n", Pages);
        HdgfxStopAperture(Adapter);
        return;
    }
    HdgfxApertureClear(Adapter);
    DPRINT1("Aperture segment: %lu MB at 0x%lx\n", Adapter->ApertureSize >> 20, Adapter->ApertureBase);
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
    if ((Count != 0 && !HdgfxRingEmit(Adapter->ActiveRing, HdgfxIlkRingHead(&Mmio, Adapter->ActiveRing),
                                      (const uint32_t *)Dwords, Count)) ||
        !HdgfxRingEmitBreadcrumb(Adapter->ActiveRing, HdgfxIlkRingHead(&Mmio, Adapter->ActiveRing),
                                 HDGFX_RING_SEQNO_INDEX, Seqno))
    {
        return FALSE;
    }
    Adapter->Seqno = Seqno;
    Start = KeQueryPerformanceCounter(&Frequency);
    HdgfxIlkAdvanceRing(&Mmio, Adapter->ActiveRing);
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
    BOOLEAN Gen7 = Adapter->Generation->Gen >= 7;

    if (!Adapter->RingStarted)
        return;

    Breadcrumb = HdgfxRingRun(Adapter, NULL, 0, &BreadcrumbUs);
    Count += HdgfxBltFill(Dwords + Count, &Source, &Full, 0x11223344);
    Count += HdgfxBltFill(Dwords + Count, &Source, &Inner, 0x55667788);
    if (Gen7)
        Adapter->ActiveRing = &Adapter->BltRing;
    else
        Count += HdgfxMiFlush(Dwords + Count);
    Count += HdgfxBltCopy(Dwords + Count, &Destination, &Copy, &Source, 0, 0);
    if (!Gen7)
        Count += HdgfxMiFlush(Dwords + Count);
    if (Breadcrumb && Count == (Gen7 ? 20UL : 22UL))
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
            Adapter->ActiveRing->Address, HDGFX_RING_SIZE / 1024, Breadcrumb ? "done" : "missing", BreadcrumbUs,
            Blit ? "done" : "missing", BlitUs, Mismatches, Adapter->RingInterrupts,
            HdgfxIlkRingHead(&Mmio, Adapter->ActiveRing), Adapter->ActiveRing->Tail,
            HdgfxReadRegister32(Adapter, HDGFX_REG(ESR, 0)));
}

BOOLEAN
HdgfxRingFence(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ const HDGFX_MMIO *Mmio,
    _In_ ULONG Head)
{
    uint32_t Flush[2] = { 0, 0 };

    HdgfxMiFlush(Flush);
    if ((Adapter->Generation->Gen < 6 && !HdgfxRingEmit(Adapter->ActiveRing, Head, Flush, RTL_NUMBER_OF(Flush))) ||
        !HdgfxRingEmitBreadcrumb(Adapter->ActiveRing, Head, HDGFX_RING_SEQNO_INDEX, Adapter->Seqno + 1))
    {
        return FALSE;
    }
    Adapter->Seqno++;
    Adapter->RingUnfenced = FALSE;
    HdgfxIlkAdvanceRing(Mmio, Adapter->ActiveRing);
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
                HdgfxIlkRingHead(&Mmio, Adapter->ActiveRing), Adapter->ActiveRing->Tail,
                ((volatile ULONG *)Adapter->StatusPage)[HDGFX_RING_SEQNO_INDEX], Adapter->Seqno,
                HdgfxReadRegister32(Adapter, HDGFX_REG(ESR, 0)));
        if (!Adapter->Generation->ResetGpu(&Mmio))
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
        Adapter->ActiveRing = &Adapter->Ring;
        if (Adapter->Generation->Gen >= 7)
        {
            if (!HdgfxIvbForceWake(&Mmio))
                DPRINT1("Forcewake failed after reset\n");
            HdgfxIvbInitEngine(&Mmio);
        }
        Adapter->RingStarted = HdgfxIlkStartRing(&Mmio, &Adapter->Ring) &&
                               (Adapter->Generation->Gen < 7 || HdgfxIlkStartRing(&Mmio, &Adapter->BltRing));
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

static VOID
HdgfxHardwareStart(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    HDGFX_DISPLAY_STATE *State;
    PHYSICAL_ADDRESS Address;
    HDGFX_MMIO Mmio;
    ULONGLONG Bar = 0;
    USHORT Ids[2] = { 0, 0 };

    PAGED_CODE();

    ExReInitializeRundownProtection(&Adapter->Rundown);
    Adapter->Started = TRUE;
    HdgfxQueueStart(Adapter);
    HdgfxStartDma(Adapter);

    if (!NT_SUCCESS(HdgfxReadConfig(&Adapter->DxgkInterface, Ids, 0, sizeof(Ids))))
        return;
    Adapter->Generation = HdgfxFindGeneration(Ids[0], Ids[1]);
    if (Adapter->Generation == NULL)
    {
        DPRINT1("%04x:%04x is not a supported device\n", Ids[0], Ids[1]);
        return;
    }

    if (!NT_SUCCESS(HdgfxReadConfig(&Adapter->DxgkInterface, &Bar, HDGFX_PCI_BAR0, sizeof(Bar))))
        return;
    Address.QuadPart = (LONGLONG)(Bar & ~0xFULL);
    if (!HdgfxResourceOwned(&Adapter->DxgkInterface, (ULONGLONG)Address.QuadPart, HDGFX_MMIO_SIZE))
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
    HdgfxReadVbt(Adapter);

    State = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*State), HDGFX_TAG);
    if (State == NULL)
        return;
    HdgfxMmioInit(Adapter->Registers, &Mmio);
    Adapter->Generation->ReadDisplayState(&Mmio, State);
    HdgfxLogDisplayState(State);
    Adapter->InternalPanel = State->Lvds.Detected;
    HdgfxStartBacklight(Adapter, State);
    HdgfxStartRender(Adapter);
    HdgfxStartAperture(Adapter);
    HdgfxStartInterrupts(Adapter);
    HdgfxTestRing(Adapter);
    ExFreePoolWithTag(State, HDGFX_TAG);
}

static VOID
HdgfxHardwareStop(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    PAGED_CODE();

    if (!Adapter->Started)
        return;
    Adapter->Started = FALSE;

    ExWaitForRundownProtectionRelease(&Adapter->Rundown);
    HdgfxQueueStop(Adapter);
    if (Adapter->Registers == NULL)
    {
        HdgfxStopDma(Adapter);
        return;
    }

    HdgfxStopInterrupts(Adapter);
    HdgfxStopAperture(Adapter);
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

static PHDGFX_ADAPTER
HdgfxBacklightAcquire(
    _In_ PVOID Context)
{
    PHDGFX_ADAPTER Adapter = Context;

    if (!ExAcquireRundownProtection(&Adapter->Rundown))
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
APIENTRY
HdgfxDdiQueryInterface(
    _In_ PVOID MiniportDeviceContext,
    _In_ PQUERY_INTERFACE QueryInterface)
{
    PDXGK_BRIGHTNESS_INTERFACE Interface;
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;

    PAGED_CODE();

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC || QueryInterface == NULL ||
        QueryInterface->InterfaceType == NULL || QueryInterface->Interface == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (QueryInterface->DeviceUid != DISPLAY_ADAPTER_HW_ID ||
        !IsEqualGUID(QueryInterface->InterfaceType, &HdgfxGuidBrightness) ||
        QueryInterface->Version != DXGK_BRIGHTNESS_INTERFACE_VERSION_1 ||
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
    Interface->Context = Adapter;
    Interface->InterfaceReference = HdgfxBacklightReference;
    Interface->InterfaceDereference = HdgfxBacklightReference;
    Interface->GetPossibleBrightness = HdgfxBacklightGetPossible;
    Interface->SetBrightness = HdgfxBacklightSet;
    Interface->GetBrightness = HdgfxBacklightGet;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiNotifyAcpiEvent(
    _In_ PVOID MiniportDeviceContext,
    _In_ DXGK_EVENT_TYPE EventType,
    _In_ ULONG Event,
    _In_ PVOID Argument,
    _Out_ PULONG AcpiFlags)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    uint32_t Events;
    HDGFX_MMIO Io;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Argument);

    if (AcpiFlags != NULL)
        *AcpiFlags = 0;
    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC)
        return STATUS_INVALID_PARAMETER;
    if (EventType != DxgkAcpiEvent || Event != ACPI_NOTIFY_PANEL_SWITCH)
        return STATUS_SUCCESS;

    Adapter = HdgfxBacklightAcquire(Adapter);
    if (Adapter == NULL)
        return STATUS_SUCCESS;

    if (Adapter->AsleReady)
    {
        HdgfxOpRegionIo(Adapter, &Io);
        if (HdgfxOpRegionBeginNotification(&Io, &Events))
        {
            if ((Events & (HDGFX_OPREGION_EVENT_LID | HDGFX_OPREGION_EVENT_DOCK)) && AcpiFlags != NULL)
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
APIENTRY
HdgfxDdiInterruptRoutine(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG MessageNumber)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    ULONG Pending;
    ULONG Gt;
    ULONG Sde;

    UNREFERENCED_PARAMETER(MessageNumber);

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC ||
        (!Adapter->AsleInterrupt && !Adapter->HotplugInterrupt && !Adapter->RingInterrupt))
    {
        return FALSE;
    }

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
    Adapter->DxgkInterface.DxgkCbQueueDpc(Adapter->DxgkInterface.DeviceHandle);
    return TRUE;
}

VOID
APIENTRY
HdgfxDdiDpcRoutine(
    _In_ PVOID MiniportDeviceContext)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    HDGFX_MMIO Io;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC)
        return;
    if (InterlockedExchange(&Adapter->RingPending, 0) != 0)
        HdgfxQueueDpc(Adapter);
    if (InterlockedExchange(&Adapter->HotplugPending, 0) != 0)
        HdgfxQueueHotplug(Adapter);
    if (InterlockedExchange(&Adapter->AslePending, 0) == 0)
        return;
    Adapter = HdgfxBacklightAcquire(Adapter);
    if (Adapter == NULL)
        return;

    if (Adapter->AsleReady)
    {
        HdgfxOpRegionIo(Adapter, &Io);
        HdgfxOpRegionServiceAsle(&Io, HdgfxAsleSetBacklight, Adapter);
    }
    ExReleaseRundownProtection(&Adapter->Rundown);
}

VOID
HdgfxLock(
    _In_ PHDGFX_SCANOUT Scanout)
{
    KeWaitForSingleObject(&Scanout->Lock, Executive, KernelMode, FALSE, NULL);
}

VOID
HdgfxUnlock(
    _In_ PHDGFX_SCANOUT Scanout)
{
    KeReleaseMutex(&Scanout->Lock, FALSE);
}

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

NTSTATUS
HdgfxSetOutputs(
    _Inout_ PHDGFX_SCANOUT Scanout,
    _In_ const HDGFX_MODE *Source,
    _In_ BOOLEAN Panel,
    _In_ BOOLEAN Crt)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Source->Timing >= Scanout->PanelModeCount || (Crt && !Scanout->CrtSupported))
        return STATUS_INVALID_PARAMETER;

    HdgfxLock(Scanout);
    if (!Crt && Scanout->CrtActive)
    {
        if (!Scanout->CrtDisplayOff)
            Status = HdgfxCrtOff(Scanout);
        if (NT_SUCCESS(Status))
            Scanout->CrtActive = FALSE;
    }
    if (NT_SUCCESS(Status) && !Panel && Scanout->LvdsActive)
    {
        if (!Scanout->DisplayOff)
            Status = HdgfxLvdsOff(Scanout);
        if (NT_SUCCESS(Status))
            Scanout->LvdsActive = FALSE;
    }
    if (NT_SUCCESS(Status))
    {
        Scanout->LvdsActive = Panel;
        Status = HdgfxScanoutCommit(Scanout, &Scanout->PanelModes[Source->Timing],
                                    Source->Width, Source->Height, Source->Pitch);
    }
    if (NT_SUCCESS(Status) && Crt)
    {
        Scanout->CrtActive = TRUE;
        Status = HdgfxCrtCommit(Scanout);
    }
    if (NT_SUCCESS(Status) && Scanout->SlabGtt != NULL)
        HdgfxCursorCommit(Scanout);
    HdgfxUnlock(Scanout);
    DPRINT1("Outputs: panel %s, CRT %s, source %lux%lu: 0x%08lx\n",
            Panel ? "on" : "off", Crt ? "on" : "off", Source->Width, Source->Height, Status);
    return Status;
}

NTSTATUS
HdgfxSetMonitorPower(
    _Inout_ PHDGFX_SCANOUT Scanout,
    _In_ ULONG ChildUid,
    _In_ BOOLEAN Off)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

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
    else if (ChildUid == HDGFX_PANEL_CHILD_UID && Off != Scanout->DisplayOff)
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
    _Inout_ PHDGFX_SCANOUT Scanout,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG PanelMode)
{
    const HDGFX_DTD *Panel = &Scanout->PanelModes[PanelMode];
    PHDGFX_MODE Mode;
    ULONG Index;

    for (Index = 0; Index < Scanout->ModeCount; Index++)
    {
        if (Scanout->Modes[Index].Width == Width && Scanout->Modes[Index].Height == Height &&
            Scanout->Modes[Index].Timing == PanelMode)
        {
            return;
        }
    }
    if (Scanout->ModeCount >= HDGFX_MAX_MODES)
        return;

    Mode = &Scanout->Modes[Scanout->ModeCount];
    Mode->Width = Width;
    Mode->Height = Height;
    Mode->Pitch = ALIGN_UP_BY(Width * HDGFX_BYTES_PER_PIXEL, HDGFX_STRIDE_ALIGNMENT);
    Mode->Timing = PanelMode;
    Mode->TotalWidth = Panel->Timings.HTotal;
    Mode->TotalHeight = Panel->Timings.VTotal;
    Mode->PixelRate = Panel->DotKHz * 1000;
    Scanout->FirmwareSize = max(Scanout->FirmwareSize, Mode->Pitch * Height);
    Scanout->ModeCount++;
}

static ULONG
HdgfxCrtModes(
    _In_ const HDGFX_SCANOUT *Scanout,
    _Out_writes_(HDGFX_MAX_MODES) PHDGFX_MODE Modes)
{
    const HDGFX_DTD *Timing = &Scanout->CrtTiming;
    ULONG Count = 0;
    ULONG Index;
    ULONG Other;

    for (Index = 0; Index < Scanout->ModeCount && Count < HDGFX_MAX_MODES; Index++)
    {
        const HDGFX_MODE *Source = &Scanout->Modes[Index];

        if (Source->Width > Timing->Timings.HActive || Source->Height > Timing->Timings.VActive)
            continue;
        for (Other = 0; Other < Count; Other++)
        {
            if (Modes[Other].Width == Source->Width && Modes[Other].Height == Source->Height)
                break;
        }
        if (Other != Count)
            continue;

        Modes[Count] = *Source;
        Modes[Count].Timing = 0;
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
    _Inout_ PHDGFX_SCANOUT Scanout)
{
    if (Scanout->Generation->DetectCrt == NULL)
        return;

    Scanout->CrtSupported = TRUE;
    Scanout->CrtConnected = HdgfxCrtDetect(Scanout, Scanout->CrtEdid, &Scanout->CrtEdidLength);
    if (Scanout->CrtConnected && Scanout->CrtTimingValid)
        Scanout->CrtModeCount = HdgfxCrtModes(Scanout, Scanout->CrtModes);
}

static VOID
HdgfxCrtRefresh(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ BOOLEAN Indicate)
{
    PHDGFX_SCANOUT Scanout = Adapter->Scanout;
    DXGK_CHILD_STATUS ChildStatus;
    UCHAR Edid[HDGFX_EDID_SIZE];
    ULONG EdidLength;
    ULONG Count;
    BOOLEAN Connected;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Scanout == NULL || !Scanout->CrtSupported)
        return;

    KeWaitForSingleObject(&Scanout->OutputLock, Executive, KernelMode, FALSE, NULL);
    HdgfxLock(Scanout);
    Connected = HdgfxCrtDetect(Scanout, Edid, &EdidLength);
    HdgfxUnlock(Scanout);
    Scanout->CrtConnected = Connected;
    Scanout->CrtModeCount = Connected && Scanout->CrtTimingValid ? HdgfxCrtModes(Scanout, Scanout->CrtModes) : 0;
    Scanout->CrtStale = TRUE;
    RtlZeroMemory(Scanout->CrtEdid, sizeof(Scanout->CrtEdid));
    RtlCopyMemory(Scanout->CrtEdid, Edid, EdidLength);
    Scanout->CrtEdidLength = EdidLength;
    Count = Scanout->CrtModeCount;
    KeReleaseMutex(&Scanout->OutputLock, FALSE);

    if (Indicate && Adapter->DxgkInterface.DxgkCbIndicateChildStatus != NULL)
    {
        RtlZeroMemory(&ChildStatus, sizeof(ChildStatus));
        ChildStatus.Type = StatusConnection;
        ChildStatus.ChildUid = HDGFX_CRT_CHILD_UID;
        ChildStatus.HotPlug.Connected = Connected;
        Status = Adapter->DxgkInterface.DxgkCbIndicateChildStatus(Adapter->DxgkInterface.DeviceHandle,
                                                                  &ChildStatus);
    }
    DPRINT1("CRT hot plug: %s, %lu mode(s), %lu-byte EDID: 0x%08lx\n", Connected ? "connected" : "disconnected",
            Count, EdidLength, Status);
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
    _Inout_ PHDGFX_SCANOUT Scanout)
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

    if (HdgfxReadEdid("Panel", Scanout->Generation->PanelDdcPins, &Mmio, Scanout->PanelEdid, &Edid) &&
        Edid.DtdCount != 0)
    {
        Scanout->PanelEdidLength = HDGFX_EDID_SIZE;
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
        HdgfxAddMode(Scanout, Scanout->Panel.Timings.HActive, Scanout->Panel.Timings.VActive, PanelMode);
    for (Index = 0; Index < RTL_NUMBER_OF(HdgfxScaledModes); Index++)
    {
        if (HdgfxScaledModes[Index][0] > Scanout->Panel.Timings.HActive ||
            HdgfxScaledModes[Index][1] > Scanout->Panel.Timings.VActive)
        {
            continue;
        }
        for (PanelMode = 0; PanelMode < Scanout->PanelModeCount; PanelMode++)
            HdgfxAddMode(Scanout, HdgfxScaledModes[Index][0], HdgfxScaledModes[Index][1], PanelMode);
    }
    Scanout->FirmwareSize = ALIGN_UP_BY(Scanout->FirmwareSize, PAGE_SIZE);

    if (!HdgfxResourceOwned(Interface, Bar2 + Scanout->Surface, Scanout->FirmwareSize) ||
        !HdgfxGttMapped(Interface, Bar0, Scanout->Surface, Scanout->FirmwareSize))
    {
        DPRINT1("Aperture 0x%I64x cannot hold a %lu-byte surface\n", Bar2, Scanout->FirmwareSize);
        return STATUS_NOT_SUPPORTED;
    }

    Address.QuadPart = (LONGLONG)(Bar2 + Scanout->Surface);
    Mapping = MmMapIoSpace(Address, Scanout->FirmwareSize, MmWriteCombined);
    if (Mapping == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Mapping, Scanout->FirmwareSize);
    MmUnmapIoSpace(Mapping, Scanout->FirmwareSize);

    Status = HdgfxScanoutCommit(Scanout, &Scanout->PanelModes[0], Scanout->Modes[0].Width, Scanout->Modes[0].Height,
                                Scanout->Modes[0].Pitch);
    if (!NT_SUCCESS(Status))
        return Status;

    Scanout->FirmwareSurface = Scanout->Surface;
    Scanout->Mode = 0;
    return STATUS_SUCCESS;
}

VOID
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
        WRITE_REGISTER_ULONG((PULONG)(Scanout->Registers + HDGFX_REG(CUR_CNTR, 0) + Pipe * Scanout->Generation->CursorStride), Scanout->CursorControl);
        WRITE_REGISTER_ULONG((PULONG)(Scanout->Registers + HDGFX_REG(CUR_POS, 0) + Pipe * Scanout->Generation->CursorStride), Scanout->CursorPosition);
        WRITE_REGISTER_ULONG((PULONG)(Scanout->Registers + HDGFX_REG(CUR_BASE, 0) + Pipe * Scanout->Generation->CursorStride), Scanout->CursorAddress);
    }
}

VOID
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
    _Inout_ PHDGFX_ADAPTER Adapter,
    _Inout_ PHDGFX_SCANOUT Scanout)
{
    LARGE_INTEGER Frame;
    ULONG Index;

    Adapter->HardwarePointer = FALSE;
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

static NTSTATUS
HdgfxDirectScanoutStart(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _Inout_ PHDGFX_SCANOUT Scanout)
{
    PHYSICAL_ADDRESS Lowest;
    PHYSICAL_ADDRESS Highest;
    PHYSICAL_ADDRESS Boundary;
    PHYSICAL_ADDRESS Address;
    ULONGLONG Physical;
    ULONG Pages;
    ULONG First;
    ULONG Index;

    if (Scanout->ApertureSize == 0 || Adapter->Slab == NULL || Adapter->SlabSize % PAGE_SIZE != 0 ||
        (ULONGLONG)Adapter->SlabPhysical.QuadPart + Adapter->SlabSize - 1 > MAXULONG)
    {
        DPRINT1("Direct scan-out unavailable\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Pages = (ULONG)(Adapter->SlabSize / PAGE_SIZE);
    if (Scanout->ApertureSize / PAGE_SIZE < HDGFX_RENDER_GTT_PAGES + HDGFX_CURSOR_PAGES + Pages ||
        (Scanout->ApertureSize / PAGE_SIZE - HDGFX_RENDER_GTT_PAGES - HDGFX_CURSOR_PAGES - Pages) * PAGE_SIZE <
            Scanout->FirmwareSize)
    {
        DPRINT1("Aperture of %lu MB cannot hold the %Iu-byte allocation slab\n",
                Scanout->ApertureSize >> 20, Adapter->SlabSize);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
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
    Scanout->SlabPhysical = (ULONGLONG)Adapter->SlabPhysical.QuadPart;
    Scanout->SlabSize = Adapter->SlabSize;
    Scanout->SlabAddress = First * PAGE_SIZE;
    Scanout->CursorAddress = (First + Pages) * PAGE_SIZE;
    Scanout->CursorGeneration = 0;
    Scanout->CursorControl = HdgfxIlkCursorControl(false);
    Scanout->CursorPosition = 0;
    for (Index = 0; Index < Pages; Index++)
    {
        Physical = Scanout->SlabPhysical + (ULONGLONG)Index * PAGE_SIZE;
        WRITE_REGISTER_ULONG(&Scanout->SlabGtt[Index], Scanout->Generation->GttEntry(Physical));
    }
    for (Index = 0; Index < HDGFX_CURSOR_PAGES; Index++)
    {
        Physical = (ULONGLONG)MmGetPhysicalAddress((PUCHAR)Scanout->Cursor + Index * PAGE_SIZE).QuadPart;
        WRITE_REGISTER_ULONG(&Scanout->SlabGtt[Pages + Index], Scanout->Generation->GttEntry(Physical));
    }
    READ_REGISTER_ULONG(&Scanout->SlabGtt[Pages + HDGFX_CURSOR_PAGES - 1]);
    HdgfxGttFlush(Scanout->Registers, Scanout->Generation);

    Adapter->HardwarePointer = TRUE;
    DPRINT1("Direct scan-out: %Iu-byte slab at 0x%I64x mapped to 0x%lx, cursor at 0x%lx\n",
            Adapter->SlabSize, Scanout->SlabPhysical, Scanout->SlabAddress, Scanout->CursorAddress);
    return STATUS_SUCCESS;
}

static VOID
HdgfxScanoutStop(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    PHDGFX_SCANOUT Scanout = Adapter->Scanout;

    if (Scanout == NULL)
        return;
    HdgfxDirectScanoutStop(Adapter, Scanout);
    Adapter->Scanout = NULL;
    HdgfxScanoutFree(Scanout);
}

ULONG
HdgfxScanLine(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ ULONG Pipe)
{
    return HDGFX_GET(READ_REGISTER_ULONG((PULONG)(Scanout->Registers + HDGFX_REG(PIPE_DSL, Pipe))), PIPE_DSL_LINE);
}

static NTSTATUS
HdgfxSlabStart(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ const HDGFX_MODE *Mode)
{
    PHYSICAL_ADDRESS Lowest;
    PHYSICAL_ADDRESS Highest;
    PHYSICAL_ADDRESS Boundary;
    SIZE_T Size;
    NTSTATUS Status;

    Status = SoftGpu2dComputeAllocationSlabSize(Mode->Width, Mode->Height, HDGFX_WORKING_SURFACE_COUNT, MAXULONG_PTR,
                                                &Size);
    if (!NT_SUCCESS(Status))
        return Status;
    Size = min(Size, SOFTGPU_MAX_ALLOCATION_SLAB_SIZE);

    Lowest.QuadPart = 0;
    Highest.QuadPart = MAXULONG;
    Boundary.QuadPart = 0;
    Adapter->Slab = MmAllocateContiguousMemorySpecifyCache(Size, Lowest, Highest, Boundary, MmWriteCombined);
    if (Adapter->Slab == NULL)
    {
        DPRINT1("No memory for the %Iu-byte allocation slab\n", Size);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Adapter->Slab, Size);
    Adapter->SlabPhysical = MmGetPhysicalAddress(Adapter->Slab);
    Adapter->SlabSize = Size;
    return STATUS_SUCCESS;
}

static VOID
HdgfxSlabStop(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    if (Adapter->Slab == NULL)
        return;
    MmFreeContiguousMemorySpecifyCache(Adapter->Slab, Adapter->SlabSize, MmWriteCombined);
    Adapter->Slab = NULL;
    Adapter->SlabPhysical.QuadPart = 0;
    Adapter->SlabSize = 0;
}

static PACPI_METHOD_ARGUMENT
HdgfxAcpiFirstArgument(
    _In_reads_bytes_(OutputSize) PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG OutputSize,
    _Out_ PUCHAR *End)
{
    *End = (PUCHAR)Output + min(Output->Length, OutputSize);
    if (Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || Output->Count == 0 ||
        (PUCHAR)Output->Argument + FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) > *End)
    {
        return NULL;
    }
    return Output->Argument;
}

static VOID
HdgfxQueryPanelChild(
    _Inout_ PHDGFX_ADAPTER Adapter)
{
    ACPI_EVAL_INPUT_BUFFER_COMPLEX Input;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG Index;

    PAGED_CODE();

    Adapter->ChildAcpiUid = 0;
    Adapter->ChildOutputTechnology = D3DKMDT_VOT_OTHER;
    if (Adapter->DxgkInterface.DxgkCbEvalAcpiMethod == NULL)
        return;

    Output = ExAllocatePoolZero(PagedPool, HDGFX_ACPI_OUTPUT_SIZE, HDGFX_TAG);
    if (Output == NULL)
        return;

    RtlZeroMemory(&Input, sizeof(Input));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    Input.MethodNameAsUlong = ACPI_METHOD_DISPLAY_DOD;
    if (NT_SUCCESS(Adapter->DxgkInterface.DxgkCbEvalAcpiMethod(Adapter->DxgkInterface.DeviceHandle,
                                                               DISPLAY_ADAPTER_HW_ID,
                                                               &Input,
                                                               sizeof(Input),
                                                               Output,
                                                               HDGFX_ACPI_OUTPUT_SIZE)))
    {
        Argument = HdgfxAcpiFirstArgument(Output, HDGFX_ACPI_OUTPUT_SIZE, &End);
        for (Index = 0; Argument != NULL && Index < Output->Count; Index++)
        {
            if ((PUCHAR)Argument + FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) > End ||
                (PUCHAR)ACPI_METHOD_NEXT_ARGUMENT(Argument) > End)
            {
                break;
            }
            if (Argument->Type == ACPI_METHOD_ARGUMENT_INTEGER &&
                Argument->DataLength >= sizeof(ULONG) &&
                (Argument->Argument & HDGFX_ACPI_DOD_ID_SCHEME) != 0 &&
                (Argument->Argument & HDGFX_ACPI_DOD_TYPE_MASK) == HDGFX_ACPI_DOD_TYPE_PANEL)
            {
                Adapter->ChildAcpiUid = Argument->Argument & HDGFX_ACPI_DOD_ADR_MASK;
                Adapter->ChildOutputTechnology = D3DKMDT_VOT_INTERNAL;
                break;
            }
            Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
        }
    }
    ExFreePoolWithTag(Output, HDGFX_TAG);
}

static VOID
NTAPI
HdgfxVsyncTimer(
    _In_ PEX_TIMER Timer,
    _In_opt_ PVOID Context)
{
    PHDGFX_ADAPTER Adapter = Context;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA Notify;

    UNREFERENCED_PARAMETER(Timer);

    if (Adapter == NULL || Adapter->Stopped || Adapter->VsyncPhaseEnabled == 0 || Adapter->VsyncEnabled == 0)
        return;

    RtlZeroMemory(&Notify, sizeof(Notify));
    Notify.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
    Notify.CrtcVsync.VidPnTargetId = Adapter->VsyncTargetId;
    Notify.CrtcVsync.PhysicalAddress.QuadPart = InterlockedCompareExchange64(&Adapter->PrimaryAddress, 0, 0);
    Adapter->DxgkInterface.DxgkCbNotifyInterrupt(Adapter->DxgkInterface.DeviceHandle, &Notify);
    Adapter->DxgkInterface.DxgkCbQueueDpc(Adapter->DxgkInterface.DeviceHandle);
}

NTSTATUS
HdgfxSetVsync(
    _Inout_ PHDGFX_ADAPTER Adapter,
    _In_ DXGK_CRTC_VSYNC_STATE VsyncState)
{
    if (Adapter->VsyncTimer == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    switch (VsyncState)
    {
        case DXGK_VSYNC_ENABLE:
            InterlockedExchange(&Adapter->VsyncEnabled, 1);
            if (InterlockedExchange(&Adapter->VsyncPhaseEnabled, 1) == 0)
                ExSetTimer(Adapter->VsyncTimer, -HDGFX_VSYNC_PERIOD, HDGFX_VSYNC_PERIOD, NULL);
            return STATUS_SUCCESS;

        case DXGK_VSYNC_DISABLE_KEEP_PHASE:
            InterlockedExchange(&Adapter->VsyncEnabled, 0);
            if (InterlockedExchange(&Adapter->VsyncPhaseEnabled, 1) == 0)
                ExSetTimer(Adapter->VsyncTimer, -HDGFX_VSYNC_PERIOD, HDGFX_VSYNC_PERIOD, NULL);
            return STATUS_SUCCESS;

        case DXGK_VSYNC_DISABLE_NO_PHASE:
            InterlockedExchange(&Adapter->VsyncEnabled, 0);
            InterlockedExchange(&Adapter->VsyncPhaseEnabled, 0);
            ExCancelTimer(Adapter->VsyncTimer, NULL);
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS
APIENTRY
HdgfxDdiAddDevice(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PVOID *MiniportDeviceContext)
{
    PHDGFX_ADAPTER Adapter;

    PAGED_CODE();

    if (MiniportDeviceContext == NULL)
        return STATUS_INVALID_PARAMETER;
    *MiniportDeviceContext = NULL;
    if (PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Adapter), HDGFX_TAG);
    if (Adapter == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Adapter->Magic = HDGFX_ADAPTER_MAGIC;
    Adapter->PhysicalDeviceObject = PhysicalDeviceObject;
    Adapter->Stopped = 1;
    ExInitializeRundownProtection(&Adapter->Rundown);
    ExWaitForRundownProtectionRelease(&Adapter->Rundown);
    KeInitializeSpinLock(&Adapter->RingLock);
    KeInitializeSpinLock(&Adapter->QueueLock);
    ExInitializeWorkItem(&Adapter->HotplugWork, HdgfxHotplugWorker, Adapter);
    *MiniportDeviceContext = Adapter;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiStartDevice(
    _In_ PVOID MiniportDeviceContext,
    _In_ PDXGK_START_INFO DxgkStartInfo,
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PULONG NumberOfVideoPresentSources,
    _Out_ PULONG NumberOfChildren)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    DXGK_DISPLAY_INFORMATION PostDisplay;
    PHDGFX_SCANOUT Scanout;
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(DxgkStartInfo);

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC || DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (DxgkInterface->Size < FIELD_OFFSET(DXGK_INTERFACE, DxgkCbAcquirePostDisplayOwnership) +
                                  sizeof(DxgkInterface->DxgkCbAcquirePostDisplayOwnership) ||
        DxgkInterface->DeviceHandle == NULL || DxgkInterface->DxgkCbNotifyInterrupt == NULL ||
        DxgkInterface->DxgkCbNotifyDpc == NULL || DxgkInterface->DxgkCbQueueDpc == NULL ||
        DxgkInterface->DxgkCbAcquirePostDisplayOwnership == NULL)
    {
        DPRINT1("StartDevice: incomplete DxgkInterface\n");
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Adapter->DxgkInterface, sizeof(Adapter->DxgkInterface));
    RtlCopyMemory(&Adapter->DxgkInterface, DxgkInterface,
                  min((SIZE_T)DxgkInterface->Size, sizeof(Adapter->DxgkInterface)));

    RtlZeroMemory(&PostDisplay, sizeof(PostDisplay));
    Status = DxgkInterface->DxgkCbAcquirePostDisplayOwnership(DxgkInterface->DeviceHandle, &PostDisplay);
    if (!NT_SUCCESS(Status))
        goto Failed;

    Scanout = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Scanout), HDGFX_TAG);
    if (Scanout == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failed;
    }
    KeInitializeMutex(&Scanout->OutputLock, 0);
    KeInitializeMutex(&Scanout->Lock, 0);
    Scanout->LvdsActive = TRUE;
    Status = HdgfxScanoutStart(&Adapter->DxgkInterface, Scanout);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Scan-out setup failed 0x%08lx\n", Status);
        HdgfxScanoutFree(Scanout);
        goto Failed;
    }
    Adapter->Scanout = Scanout;
    HdgfxCrtStart(Scanout);

    Status = HdgfxSlabStart(Adapter, &Scanout->Modes[0]);
    if (NT_SUCCESS(Status))
        Status = HdgfxDirectScanoutStart(Adapter, Scanout);
    if (!NT_SUCCESS(Status))
        goto Failed;

    HdgfxQueryPanelChild(Adapter);
    Adapter->VsyncTargetId = HDGFX_PANEL_CHILD_UID;
    Adapter->VsyncEnabled = 0;
    Adapter->VsyncPhaseEnabled = 0;
    Adapter->PrimaryAddress = 0;
    Adapter->VsyncTimer = ExAllocateTimer(HdgfxVsyncTimer, Adapter, EX_TIMER_HIGH_RESOLUTION);
    InterlockedExchange(&Adapter->Stopped, 0);

    HdgfxHardwareStart(Adapter);
    if (Adapter->ChildOutputTechnology != D3DKMDT_VOT_INTERNAL && Adapter->InternalPanel)
    {
        Adapter->ChildAcpiUid = HDGFX_ACPI_ID_INTERNAL_PANEL;
        Adapter->ChildOutputTechnology = D3DKMDT_VOT_INTERNAL;
    }

    *NumberOfVideoPresentSources = 1;
    *NumberOfChildren = Scanout->CrtSupported ? 2 : 1;
    return STATUS_SUCCESS;

Failed:
    HdgfxScanoutStop(Adapter);
    HdgfxSlabStop(Adapter);
    RtlZeroMemory(&Adapter->DxgkInterface, sizeof(Adapter->DxgkInterface));
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiStopDevice(
    _In_ PVOID MiniportDeviceContext)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;

    PAGED_CODE();

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC)
        return STATUS_INVALID_PARAMETER;

    InterlockedExchange(&Adapter->Stopped, 1);
    HdgfxHardwareStop(Adapter);
    InterlockedExchange(&Adapter->VsyncEnabled, 0);
    InterlockedExchange(&Adapter->VsyncPhaseEnabled, 0);
    if (Adapter->VsyncTimer != NULL)
    {
        ExDeleteTimer(Adapter->VsyncTimer, TRUE, TRUE, NULL);
        Adapter->VsyncTimer = NULL;
    }
    HdgfxScanoutStop(Adapter);
    HdgfxSlabStop(Adapter);
    RtlZeroMemory(&Adapter->DxgkInterface, sizeof(Adapter->DxgkInterface));
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiRemoveDevice(
    _In_ PVOID MiniportDeviceContext)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    NTSTATUS Status;

    PAGED_CODE();

    Status = HdgfxDdiStopDevice(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;
    Adapter->Magic = HDGFX_DEAD_ADAPTER_MAGIC;
    ExFreePoolWithTag(Adapter, HDGFX_TAG);
    return STATUS_SUCCESS;
}
