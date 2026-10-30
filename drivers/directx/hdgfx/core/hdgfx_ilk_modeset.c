/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Ironlake and Ivy Bridge mode computation, commit and readback comparison
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#include <string.h>

#define ILK_LVDS_PIPE               0
#define ILK_DPLL_REFERENCE_KHZ      120000
#define ILK_DPLL_N_MIN              1
#define ILK_DPLL_N_MAX              6
#define ILK_DPLL_M_MIN              79
#define ILK_DPLL_M_MAX              127
#define ILK_DPLL_M1_MIN             12
#define ILK_DPLL_M1_MAX             22
#define ILK_DPLL_M2_MIN             5
#define ILK_DPLL_M2_MAX             9
#define ILK_DPLL_P1_MAX             8
#define ILK_DPLL_P_MIN              28
#define ILK_DPLL_P_MAX              112
#define ILK_DAC_P_MIN               5
#define ILK_DAC_P_MAX               80
#define ILK_DAC_P2                  10
#define ILK_DAC_MIN_KHZ             25000
#define ILK_DAC_MAX_KHZ             225000
#define ILK_DAC_BPC                 8
#define ILK_DPLL_VCO_MIN_KHZ        1760000
#define ILK_DPLL_VCO_MAX_KHZ        3510000
#define ILK_LVDS_SINGLE_MIN_KHZ     25000
#define ILK_LVDS_SINGLE_MAX_KHZ     112000
#define ILK_LVDS_DUAL_MIN_KHZ       80000
#define ILK_LVDS_DUAL_MAX_KHZ       224000
#define ILK_CB_TUNE_SINGLE_RATIO    21
#define ILK_CB_TUNE_DUAL_RATIO      25
#define ILK_CB_TUNE_ON              3
#define ILK_DPLL_MODE_NON_LVDS      1
#define ILK_DPLL_MODE_LVDS          2
#define ILK_DPLL_REFERENCE_DREF     0
#define ILK_DPLL_REFERENCE_SSC      3
#define ILK_FDI_MAX_LANES           4
#define ILK_FDI_TU_SIZE             64
#define ILK_FDI_DATA_N              0x800000
#define ILK_FDI_LINK_N              0x80000
#define ILK_FDI_PATTERN_1           0
#define ILK_FDI_PATTERN_2           1
#define ILK_FDI_NORMAL              3
#define ILK_PLANE_FORMAT_XRGB8888   6
#define ILK_PF_FILTER_MEDIUM        1
#define ILK_PP_UNLOCK_KEY           0xABCD
#define ILK_WM0_DEFAULT             0x00783818
#define ILK_WM_LP_COUNT             3
#define ILK_LVDS_PAIRS_ACTIVE       3
#define ILK_MAX_ACTIVE              4096
#define ILK_MAX_TIMING              8192
#define ILK_MAX_STRIDE              32768
#define ILK_STRIDE_ALIGNMENT        64
#define ILK_SURFACE_ALIGNMENT       4096
#define ILK_BYTES_PER_PIXEL         4

#define ILK_FDI_RX_PLL_WARMUP_US    25
#define ILK_DMI_LATENCY_US          20
#define ILK_FDI_IDLE_PATTERN_US     31
#define ILK_DPLL_WARMUP_US          50
#define ILK_FDI_LOCK_TIMEOUT_US     1000
#define ILK_FDI_LOCK_POLL_US        10
#define ILK_PIPE_TIMEOUT_US         100000
#define ILK_PANEL_TIMEOUT_MARGIN_US 100000
#define ILK_POLL_US                 1000
#define ILK_VBLANK_EVASION_US       250
#define ILK_SCANLINE_POLL_US        10
#define ILK_CRT_DETECT_TIMEOUT_US   1000000
#define ILK_DREF_WARMUP_US          1
#define ILK_DREF_INTEGRATED         2

#define IVB_FDI_TX_PLL_WARMUP_US    100
#define IVB_FDI_RX_PCDCLK_US        200
#define IVB_FDI_IMR_SETTLE_US       150
#define IVB_FDI_TP1_POLL_US         1
#define IVB_FDI_TP2_POLL_US         2
#define IVB_FDI_POLLS               4
#define IVB_FDI_ATTEMPTS            2
#define IVB_FDI_RX_TP1_TO_TP2_48    2
#define IVB_FDI_RX_DELAY_90         0x90
#define IVB_FDI_BC_LANES            2
#define CPT_DPLL_SEL_STRIDE         4
#define CPT_DPLL_SEL_B              0
#define CPT_DPLL_SEL_ENABLE         3

static uint32_t
IlkRead(
    const HDGFX_MMIO *Mmio,
    uint32_t Offset)
{
    return Mmio->Read(Mmio->Context, Offset);
}

static void
IlkWrite(
    const HDGFX_MMIO *Mmio,
    uint32_t Offset,
    uint32_t Value)
{
    Mmio->Write(Mmio->Context, Offset, Value);
}

static void
IlkDelay(
    const HDGFX_MMIO *Mmio,
    uint32_t Microseconds)
{
    if (Microseconds >= 1000 && Mmio->Sleep)
        Mmio->Sleep(Mmio->Context, Microseconds / 1000);
    else
        Mmio->Stall(Mmio->Context, Microseconds);
}

static bool
IlkWaitFor(
    const HDGFX_MMIO *Mmio,
    uint32_t Offset,
    uint32_t Mask,
    uint32_t Value,
    uint32_t TimeoutUs,
    uint32_t PollUs)
{
    uint32_t Elapsed;

    for (Elapsed = 0;; Elapsed += PollUs)
    {
        if ((IlkRead(Mmio, Offset) & Mask) == Value)
            return true;
        if (Elapsed >= TimeoutUs)
            return false;
        IlkDelay(Mmio, PollUs);
    }
}

static uint32_t
IlkBpcEncoding(
    uint32_t Bpc)
{
    switch (Bpc)
    {
        case 10: return 1;
        case 6: return 2;
        case 12: return 3;
        default: return 0;
    }
}

static uint32_t
IlkFdiWidthEncoding(
    uint32_t Lanes)
{
    return Lanes - 1;
}

static uint32_t
IlkDivide(
    uint64_t Numerator,
    uint64_t Denominator)
{
    return (uint32_t)((Numerator + Denominator / 2) / Denominator);
}

static bool
IlkFindDpll(
    uint32_t TargetKHz,
    uint32_t P2,
    uint32_t PMin,
    uint32_t PMax,
    HDGFX_DPLL *Dpll)
{
    uint64_t BestError = UINT64_MAX;
    uint32_t N;
    uint32_t M1;
    uint32_t M2;
    uint32_t P1;

    for (N = ILK_DPLL_N_MIN; N <= ILK_DPLL_N_MAX; N++)
    {
        for (M1 = ILK_DPLL_M1_MIN; M1 <= ILK_DPLL_M1_MAX; M1++)
        {
            for (M2 = ILK_DPLL_M2_MIN; M2 <= ILK_DPLL_M2_MAX && M2 < M1; M2++)
            {
                uint32_t M = 5 * (M1 + 2) + (M2 + 2);
                uint64_t Vco = (uint64_t)ILK_DPLL_REFERENCE_KHZ * M;

                if (M < ILK_DPLL_M_MIN || M > ILK_DPLL_M_MAX ||
                    Vco < (uint64_t)ILK_DPLL_VCO_MIN_KHZ * (N + 2) ||
                    Vco > (uint64_t)ILK_DPLL_VCO_MAX_KHZ * (N + 2))
                {
                    continue;
                }

                for (P1 = 1; P1 <= ILK_DPLL_P1_MAX; P1++)
                {
                    uint64_t Divisor = (uint64_t)(N + 2) * P1 * P2;
                    uint64_t Scaled = (uint64_t)TargetKHz * Divisor;
                    uint64_t Error = Scaled > Vco ? Scaled - Vco : Vco - Scaled;

                    if (P1 * P2 < PMin || P1 * P2 > PMax)
                        continue;
                    Error = Error * 1000000 / Divisor;
                    if (Error >= BestError)
                        continue;

                    BestError = Error;
                    Dpll->N = N + 2;
                    Dpll->M1 = M1 + 2;
                    Dpll->M2 = M2 + 2;
                    Dpll->P1 = P1;
                    Dpll->P2 = P2;
                }
            }
        }
    }

    if (BestError == UINT64_MAX)
        return false;

    Dpll->ReferenceKHz = ILK_DPLL_REFERENCE_KHZ;
    Dpll->VcoKHz = IlkDivide((uint64_t)ILK_DPLL_REFERENCE_KHZ * (5 * Dpll->M1 + Dpll->M2), Dpll->N);
    Dpll->DotKHz = IlkDivide((uint64_t)ILK_DPLL_REFERENCE_KHZ * (5 * Dpll->M1 + Dpll->M2),
                             (uint64_t)Dpll->N * Dpll->P1 * Dpll->P2);
    return true;
}

static bool
IlkTimingsFit(
    const HDGFX_TIMINGS *Timings)
{
    return Timings->HActive != 0 && Timings->VActive != 0 &&
           Timings->HActive <= ILK_MAX_ACTIVE && Timings->VActive <= ILK_MAX_ACTIVE &&
           Timings->HActive < Timings->HSyncStart && Timings->HSyncStart < Timings->HSyncEnd &&
           Timings->HSyncEnd <= Timings->HTotal && Timings->HTotal <= ILK_MAX_TIMING &&
           Timings->VActive < Timings->VSyncStart && Timings->VSyncStart < Timings->VSyncEnd &&
           Timings->VSyncEnd <= Timings->VTotal && Timings->VTotal <= ILK_MAX_TIMING;
}

static void
IlkComputeFitter(
    const HDGFX_TIMINGS *Panel,
    uint32_t SourceWidth,
    uint32_t SourceHeight,
    HDGFX_SCALING Scaling,
    HDGFX_PANEL_FITTER *Fitter)
{
    uint64_t SourceAspect = (uint64_t)SourceWidth * Panel->VActive;
    uint64_t PanelAspect = (uint64_t)Panel->HActive * SourceHeight;

    memset(Fitter, 0, sizeof(*Fitter));
    if (SourceWidth == Panel->HActive && SourceHeight == Panel->VActive)
        return;

    Fitter->Enabled = true;
    Fitter->Width = Panel->HActive;
    Fitter->Height = Panel->VActive;
    if (Scaling == HdgfxScalingCenter)
    {
        Fitter->Width = SourceWidth;
        Fitter->Height = SourceHeight;
    }
    else if (Scaling == HdgfxScalingAspect && SourceAspect > PanelAspect)
    {
        Fitter->Height = IlkDivide((uint64_t)SourceHeight * Panel->HActive, SourceWidth) & ~1u;
    }
    else if (Scaling == HdgfxScalingAspect && SourceAspect < PanelAspect)
    {
        Fitter->Width = IlkDivide((uint64_t)SourceWidth * Panel->VActive, SourceHeight) & ~1u;
    }
    Fitter->X = (Panel->HActive - Fitter->Width) / 2;
    Fitter->Y = (Panel->VActive - Fitter->Height) / 2;
}

static bool
IlkSurfaceFits(
    uint32_t Width,
    uint32_t Stride,
    uint32_t Surface)
{
    return Stride >= Width * ILK_BYTES_PER_PIXEL && Stride <= ILK_MAX_STRIDE &&
           Stride % ILK_STRIDE_ALIGNMENT == 0 && Surface % ILK_SURFACE_ALIGNMENT == 0;
}

static HDGFX_MODESET_STATUS
IlkComputePipe(
    const HDGFX_DISPLAY_STATE *Current,
    uint32_t Index,
    const HDGFX_TIMINGS *Timings,
    uint32_t Bpc,
    uint32_t SourceWidth,
    uint32_t SourceHeight,
    uint32_t Stride,
    uint32_t Surface,
    HDGFX_DISPLAY_STATE *Target)
{
    HDGFX_PIPE *Pipe = &Target->Pipes[Index];
    HDGFX_TRANSCODER *Transcoder = &Target->Transcoders[Index];
    const HDGFX_DPLL *Dpll = &Target->Dplls[Index];
    uint32_t LinkKHz = Current->FdiPllVcoKHz / 10;
    uint32_t Lanes;

    if (LinkKHz == 0)
        return HdgfxModesetFdiBandwidth;
    for (Lanes = 1; Lanes <= ILK_FDI_MAX_LANES; Lanes++)
    {
        if ((uint64_t)Dpll->DotKHz * Bpc * 3 <= (uint64_t)LinkKHz * 8 * Lanes)
            break;
    }
    if (Lanes > ILK_FDI_MAX_LANES)
        return HdgfxModesetFdiBandwidth;

    memset(Pipe, 0, sizeof(*Pipe));
    Pipe->Enabled = true;
    Pipe->Active = true;
    Pipe->Bpc = Bpc;
    Pipe->Dither = Bpc != 8;
    Pipe->Timings = *Timings;
    Pipe->Timings.HBlankStart = Timings->HActive;
    Pipe->Timings.HBlankEnd = Timings->HTotal;
    Pipe->Timings.VBlankStart = Timings->VActive;
    Pipe->Timings.VBlankEnd = Timings->VTotal;
    Pipe->SourceWidth = SourceWidth;
    Pipe->SourceHeight = SourceHeight;
    Pipe->Fdi.TuSize = ILK_FDI_TU_SIZE;
    Pipe->Fdi.DataN = ILK_FDI_DATA_N;
    Pipe->Fdi.DataM = IlkDivide((uint64_t)Dpll->DotKHz * Bpc * 3 * ILK_FDI_DATA_N, (uint64_t)LinkKHz * 8 * Lanes);
    Pipe->Fdi.LinkN = ILK_FDI_LINK_N;
    Pipe->Fdi.LinkM = IlkDivide((uint64_t)Dpll->DotKHz * ILK_FDI_LINK_N, LinkKHz);
    Pipe->FdiTx.Enabled = true;
    Pipe->FdiTx.Training = ILK_FDI_NORMAL;
    Pipe->FdiTx.Lanes = Lanes;
    Pipe->Primary.Enabled = true;
    Pipe->Primary.Format = ILK_PLANE_FORMAT_XRGB8888;
    Pipe->Primary.Stride = Stride;
    Pipe->Primary.Surface = Surface;

    Transcoder->Enabled = true;
    Transcoder->Active = true;
    Transcoder->InterlaceMode = 0;
    Transcoder->Dpll = Index;
    Transcoder->Timings = Pipe->Timings;
    Transcoder->FdiRx.Enabled = true;
    Transcoder->FdiRx.PllEnabled = true;
    Transcoder->FdiRx.Training = ILK_FDI_NORMAL;
    Transcoder->FdiRx.Lanes = Lanes;
    Transcoder->FdiRx.Bpc = Bpc;
    return HdgfxModesetSuccess;
}

HDGFX_MODESET_STATUS
HdgfxIlkComputeLvds(
    const HDGFX_DISPLAY_STATE *Current,
    const HDGFX_LVDS_MODE *Mode,
    HDGFX_DISPLAY_STATE *Target)
{
    HDGFX_DPLL *Dpll = &Target->Dplls[ILK_LVDS_PIPE];
    const HDGFX_TIMINGS *Panel;
    bool Dual = Current->Lvds.DualChannel;
    HDGFX_MODESET_STATUS Status;

    *Target = *Current;
    if (Mode->Panel == NULL || !Current->Lvds.Detected || !IlkTimingsFit(&Mode->Panel->Timings))
        return HdgfxModesetInvalidMode;

    Panel = &Mode->Panel->Timings;
    if (Mode->SourceWidth == 0 || Mode->SourceHeight == 0 ||
        Mode->SourceWidth > Panel->HActive || Mode->SourceHeight > Panel->VActive ||
        !IlkSurfaceFits(Mode->SourceWidth, Mode->Stride, Mode->Surface))
    {
        return HdgfxModesetInvalidMode;
    }

    if ((Dual && (Mode->Panel->DotKHz < ILK_LVDS_DUAL_MIN_KHZ || Mode->Panel->DotKHz > ILK_LVDS_DUAL_MAX_KHZ)) ||
        (!Dual && (Mode->Panel->DotKHz < ILK_LVDS_SINGLE_MIN_KHZ || Mode->Panel->DotKHz > ILK_LVDS_SINGLE_MAX_KHZ)) ||
        !(Mode->Ssc ? Current->SscReference : Current->NonSpreadReference))
    {
        return HdgfxModesetNoClock;
    }

    memset(Dpll, 0, sizeof(*Dpll));
    if (!IlkFindDpll(Mode->Panel->DotKHz, Dual ? 7 : 14, ILK_DPLL_P_MIN, ILK_DPLL_P_MAX, Dpll))
        return HdgfxModesetNoClock;
    Dpll->Enabled = true;
    Dpll->Mode = HdgfxDpllModeLvds;
    Dpll->Reference = Mode->Ssc ? ILK_DPLL_REFERENCE_SSC : ILK_DPLL_REFERENCE_DREF;

    Status = IlkComputePipe(Current, ILK_LVDS_PIPE, Panel, Current->Lvds.Bpc,
                            Mode->SourceWidth, Mode->SourceHeight, Mode->Stride, Mode->Surface, Target);
    if (Status != HdgfxModesetSuccess)
        return Status;
    IlkComputeFitter(&Target->Pipes[ILK_LVDS_PIPE].Timings, Mode->SourceWidth, Mode->SourceHeight, Mode->Scaling,
                     &Target->Pipes[ILK_LVDS_PIPE].PanelFitter);

    Target->Lvds.Enabled = true;
    Target->Lvds.Transcoder = ILK_LVDS_PIPE;
    Target->Lvds.HSyncActiveLow = !Mode->Panel->HSyncPositive;
    Target->Lvds.VSyncActiveLow = !Mode->Panel->VSyncPositive;

    Target->PanelPower.TargetOn = true;
    Target->PanelPower.On = true;
    return HdgfxModesetSuccess;
}

static void
IlkWriteTimings(
    const HDGFX_MMIO *Mmio,
    uint32_t HTotal,
    uint32_t HBlank,
    uint32_t HSync,
    uint32_t VTotal,
    uint32_t VBlank,
    uint32_t VSync,
    const HDGFX_TIMINGS *Timings)
{
    IlkWrite(Mmio, HTotal, HDGFX_SET(TIMING_ACTIVE, Timings->HActive - 1) | HDGFX_SET(TIMING_HIGH, Timings->HTotal - 1));
    IlkWrite(Mmio, HBlank, HDGFX_SET(TIMING_LOW, Timings->HBlankStart - 1) |
                           HDGFX_SET(TIMING_HIGH, Timings->HBlankEnd - 1));
    IlkWrite(Mmio, HSync, HDGFX_SET(TIMING_LOW, Timings->HSyncStart - 1) | HDGFX_SET(TIMING_HIGH, Timings->HSyncEnd - 1));
    IlkWrite(Mmio, VTotal, HDGFX_SET(TIMING_ACTIVE, Timings->VActive - 1) | HDGFX_SET(TIMING_HIGH, Timings->VTotal - 1));
    IlkWrite(Mmio, VBlank, HDGFX_SET(TIMING_LOW, Timings->VBlankStart - 1) |
                           HDGFX_SET(TIMING_HIGH, Timings->VBlankEnd - 1));
    IlkWrite(Mmio, VSync, HDGFX_SET(TIMING_LOW, Timings->VSyncStart - 1) | HDGFX_SET(TIMING_HIGH, Timings->VSyncEnd - 1));
}

static void
IlkWritePanelControl(
    const HDGFX_MMIO *Mmio,
    bool TargetOn,
    bool Backlight)
{
    uint32_t Value = IlkRead(Mmio, HDGFX_REG(PCH_PP_CONTROL, 0));

    Value = HDGFX_REPLACE(Value, PP_CONTROL_KEY, ILK_PP_UNLOCK_KEY);
    Value = HDGFX_REPLACE(Value, PP_CONTROL_TARGET_ON, TargetOn);
    Value = HDGFX_REPLACE(Value, PP_CONTROL_BACKLIGHT, Backlight);
    IlkWrite(Mmio, HDGFX_REG(PCH_PP_CONTROL, 0), Value);
}

static void
IlkClearBit(
    const HDGFX_MMIO *Mmio,
    uint32_t Offset,
    uint32_t Mask)
{
    IlkWrite(Mmio, Offset, IlkRead(Mmio, Offset) & ~Mask);
}

static HDGFX_MODESET_STATUS
IlkDisablePipe(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Target,
    uint32_t Index,
    uint32_t Port,
    uint32_t PortEnable)
{
    uint32_t Other = Index ^ 1;

    IlkClearBit(Mmio, HDGFX_REG(DSP_CNTR, Index), HDGFX_MASK(DSP_ENABLE));
    IlkWrite(Mmio, HDGFX_REG(DSP_SURF, Index), IlkRead(Mmio, HDGFX_REG(DSP_SURF, Index)));
    IlkWrite(Mmio, HDGFX_REG(PF_CTL_1, Index), 0);
    IlkWrite(Mmio, HDGFX_REG(PF_WIN_POS, Index), 0);
    IlkWrite(Mmio, HDGFX_REG(PF_WIN_SZ, Index), 0);
    IlkClearBit(Mmio, HDGFX_REG(PIPE_CONF, Index), HDGFX_MASK(PIPE_CONF_ENABLE));
    if (!IlkWaitFor(Mmio, HDGFX_REG(PIPE_CONF, Index), HDGFX_MASK(PIPE_CONF_STATE), 0, ILK_PIPE_TIMEOUT_US, ILK_POLL_US))
        return HdgfxModesetPipeOffTimeout;

    IlkClearBit(Mmio, HDGFX_REG(FDI_TX_CTL, Index), HDGFX_MASK(FDI_TX_ENABLE));
    IlkClearBit(Mmio, HDGFX_REG(FDI_RX_CTL, Index), HDGFX_MASK(FDI_RX_ENABLE));
    IlkClearBit(Mmio, Port, PortEnable);
    IlkClearBit(Mmio, HDGFX_REG(TRANS_CONF, Index), HDGFX_MASK(TRANS_CONF_ENABLE));
    if (!IlkWaitFor(Mmio, HDGFX_REG(TRANS_CONF, Index), HDGFX_MASK(TRANS_CONF_STATE), 0,
                    ILK_PIPE_TIMEOUT_US, ILK_POLL_US))
    {
        return HdgfxModesetTranscoderOffTimeout;
    }
    if (Target->Gen >= 6)
    {
        IlkClearBit(Mmio, HDGFX_REG(TRANS_CHICKEN2, Index), HDGFX_MASK(TRANS_CHICKEN2_OVERRIDE));
        IlkClearBit(Mmio, HDGFX_REG(PCH_DPLL_SEL, 0),
                    ((1u << CPT_DPLL_SEL_ENABLE) | (1u << CPT_DPLL_SEL_B)) << (Index * CPT_DPLL_SEL_STRIDE));
    }
    IlkClearBit(Mmio, HDGFX_REG(PCH_DPLL, Target->Transcoders[Index].Dpll), HDGFX_MASK(DPLL_VCO_ENABLE));

    if (HDGFX_GET(IlkRead(Mmio, HDGFX_REG(TRANS_CONF, Other)), TRANS_CONF_ENABLE) == 0)
    {
        IlkClearBit(Mmio, HDGFX_REG(FDI_RX_CTL, Index), HDGFX_MASK(FDI_RX_PCDCLK));
        if (Target->Gen >= 6)
        {
            IlkClearBit(Mmio, HDGFX_REG(FDI_TX_CTL, Index), HDGFX_MASK(FDI_TX_PLL_ENABLE));
            IlkDelay(Mmio, IVB_FDI_TX_PLL_WARMUP_US);
        }
        IlkClearBit(Mmio, HDGFX_REG(FDI_RX_CTL, Index), HDGFX_MASK(FDI_RX_PLL_ENABLE));
    }
    return HdgfxModesetSuccess;
}

static HDGFX_MODESET_STATUS
IlkDisableLvds(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Target)
{
    const HDGFX_PANEL_POWER *Power = &Target->PanelPower;

    IlkWritePanelControl(Mmio, HDGFX_GET(IlkRead(Mmio, HDGFX_REG(PCH_PP_CONTROL, 0)), PP_CONTROL_TARGET_ON), false);
    IlkWritePanelControl(Mmio, false, false);
    if (!IlkWaitFor(Mmio, HDGFX_REG(PCH_PP_STATUS, 0),
                    HDGFX_MASK(PP_STATUS_ON) | HDGFX_MASK(PP_STATUS_SEQUENCE), 0,
                    2 * (Power->BacklightOffUs + Power->PowerDownUs) + ILK_PANEL_TIMEOUT_MARGIN_US, ILK_POLL_US))
    {
        return HdgfxModesetPanelOffTimeout;
    }
    return IlkDisablePipe(Mmio, Target, Target->Lvds.Transcoder, HDGFX_REG(PCH_LVDS, 0), HDGFX_MASK(LVDS_ENABLE));
}

static uint32_t
IlkFdiTxControl(
    uint32_t Value,
    uint32_t Pattern,
    uint32_t Lanes)
{
    Value = HDGFX_REPLACE(Value, FDI_TX_ENABLE, 1);
    Value = HDGFX_REPLACE(Value, FDI_TX_TRAINING, Pattern);
    Value = HDGFX_REPLACE(Value, FDI_TX_WIDTH, IlkFdiWidthEncoding(Lanes));
    Value = HDGFX_REPLACE(Value, FDI_TX_ENHANCED_FRAMING, 1);
    return HDGFX_REPLACE(Value, FDI_TX_PLL_ENABLE, 1);
}

static void
IlkSetFdiPattern(
    const HDGFX_MMIO *Mmio,
    uint32_t Index,
    uint32_t Pattern,
    uint32_t Lanes)
{
    uint32_t Rx = IlkRead(Mmio, HDGFX_REG(FDI_RX_CTL, Index));

    IlkWrite(Mmio, HDGFX_REG(FDI_TX_CTL, Index),
             IlkFdiTxControl(IlkRead(Mmio, HDGFX_REG(FDI_TX_CTL, Index)), Pattern, Lanes));
    Rx = HDGFX_REPLACE(Rx, FDI_RX_ENABLE, 1);
    IlkWrite(Mmio, HDGFX_REG(FDI_RX_CTL, Index), HDGFX_REPLACE(Rx, FDI_RX_TRAINING, Pattern));
}

static HDGFX_MODESET_STATUS
IlkTrainFdi(
    const HDGFX_MMIO *Mmio,
    uint32_t Index,
    uint32_t Lanes)
{
    IlkSetFdiPattern(Mmio, Index, ILK_FDI_PATTERN_1, Lanes);
    if (!IlkWaitFor(Mmio, HDGFX_REG(FDI_RX_ISR, Index), HDGFX_MASK(FDI_RX_BIT_LOCK), HDGFX_MASK(FDI_RX_BIT_LOCK),
                    ILK_FDI_LOCK_TIMEOUT_US, ILK_FDI_LOCK_POLL_US))
    {
        return HdgfxModesetFdiBitLockFailed;
    }

    IlkSetFdiPattern(Mmio, Index, ILK_FDI_PATTERN_2, Lanes);
    if (!IlkWaitFor(Mmio, HDGFX_REG(FDI_RX_ISR, Index), HDGFX_MASK(FDI_RX_SYMBOL_LOCK),
                    HDGFX_MASK(FDI_RX_SYMBOL_LOCK), ILK_FDI_LOCK_TIMEOUT_US, ILK_FDI_LOCK_POLL_US))
    {
        return HdgfxModesetFdiSymbolLockFailed;
    }

    IlkSetFdiPattern(Mmio, Index, ILK_FDI_NORMAL, Lanes);
    IlkDelay(Mmio, ILK_FDI_IDLE_PATTERN_US);
    return HdgfxModesetSuccess;
}

static bool
IvbFdiLock(
    const HDGFX_MMIO *Mmio,
    uint32_t Index,
    uint32_t Mask,
    uint32_t PollUs)
{
    uint32_t Poll;
    uint32_t Value;

    for (Poll = 0; Poll < IVB_FDI_POLLS; Poll++)
    {
        Value = IlkRead(Mmio, HDGFX_REG(FDI_RX_ISR, Index));
        if ((Value & Mask) != 0 || (IlkRead(Mmio, HDGFX_REG(FDI_RX_ISR, Index)) & Mask) != 0)
        {
            IlkWrite(Mmio, HDGFX_REG(FDI_RX_ISR, Index), Value | Mask);
            return true;
        }
        IlkDelay(Mmio, PollUs);
    }
    return false;
}

static HDGFX_MODESET_STATUS
IvbTrainFdi(
    const HDGFX_MMIO *Mmio,
    uint32_t Index,
    uint32_t Lanes)
{
    static const uint8_t SwingEmphasis[] = { 0x00, 0x3A, 0x39, 0x38 };
    uint32_t LockMask = HDGFX_MASK(FDI_RX_BIT_LOCK) | HDGFX_MASK(FDI_RX_SYMBOL_LOCK);
    HDGFX_MODESET_STATUS Status = HdgfxModesetFdiBitLockFailed;
    uint32_t Attempt;
    uint32_t Tx;
    uint32_t Rx;

    if (Index == 1)
    {
        IlkWrite(Mmio, HDGFX_REG(SOUTH_CHICKEN1, 0),
                 HDGFX_REPLACE(IlkRead(Mmio, HDGFX_REG(SOUTH_CHICKEN1, 0)), SOUTH_CHICKEN1_FDI_BC,
                               Lanes <= IVB_FDI_BC_LANES));
    }
    IlkWrite(Mmio, HDGFX_REG(FDI_RX_TUSIZE1, Index),
             IlkRead(Mmio, HDGFX_REG(PIPE_DATA_M1, Index)) & HDGFX_MASK(M_N_TU_SIZE));
    IlkClearBit(Mmio, HDGFX_REG(FDI_RX_IMR, Index), LockMask);
    IlkDelay(Mmio, IVB_FDI_IMR_SETTLE_US);

    for (Attempt = 0; Attempt < IVB_FDI_ATTEMPTS * sizeof(SwingEmphasis); Attempt++)
    {
        Tx = IlkRead(Mmio, HDGFX_REG(FDI_TX_CTL, Index));
        Tx &= ~(HDGFX_MASK(FDI_TX_ENABLE) | HDGFX_MASK(FDI_TX_AUTO_TRAINING) | HDGFX_MASK(FDI_TX_TRAINING_IVB));
        IlkWrite(Mmio, HDGFX_REG(FDI_TX_CTL, Index), Tx);
        Rx = IlkRead(Mmio, HDGFX_REG(FDI_RX_CTL, Index));
        Rx &= ~(HDGFX_MASK(FDI_RX_ENABLE) | HDGFX_MASK(FDI_RX_AUTO_TRAINING) | HDGFX_MASK(FDI_RX_TRAINING_CPT));
        IlkWrite(Mmio, HDGFX_REG(FDI_RX_CTL, Index), Rx);

        Tx = HDGFX_REPLACE(Tx, FDI_TX_WIDTH, IlkFdiWidthEncoding(Lanes));
        Tx = HDGFX_REPLACE(Tx, FDI_TX_SWING_EMPHASIS, SwingEmphasis[Attempt / IVB_FDI_ATTEMPTS]);
        Tx = HDGFX_REPLACE(Tx, FDI_TX_COMPOSITE_SYNC, 1);
        IlkWrite(Mmio, HDGFX_REG(FDI_TX_CTL, Index), HDGFX_REPLACE(Tx, FDI_TX_ENABLE, 1));
        IlkWrite(Mmio, HDGFX_REG(FDI_RX_MISC, Index),
                 HDGFX_SET(FDI_RX_TP1_TO_TP2, IVB_FDI_RX_TP1_TO_TP2_48) | HDGFX_SET(FDI_RX_DELAY, IVB_FDI_RX_DELAY_90));
        Rx = HDGFX_REPLACE(Rx, FDI_RX_COMPOSITE_SYNC, 1);
        IlkWrite(Mmio, HDGFX_REG(FDI_RX_CTL, Index), HDGFX_REPLACE(Rx, FDI_RX_ENABLE, 1));
        IlkDelay(Mmio, IVB_FDI_TP1_POLL_US);

        if (!IvbFdiLock(Mmio, Index, HDGFX_MASK(FDI_RX_BIT_LOCK), IVB_FDI_TP1_POLL_US))
        {
            Status = HdgfxModesetFdiBitLockFailed;
            continue;
        }

        IlkWrite(Mmio, HDGFX_REG(FDI_TX_CTL, Index),
                 HDGFX_REPLACE(IlkRead(Mmio, HDGFX_REG(FDI_TX_CTL, Index)), FDI_TX_TRAINING_IVB, ILK_FDI_PATTERN_2));
        IlkWrite(Mmio, HDGFX_REG(FDI_RX_CTL, Index),
                 HDGFX_REPLACE(IlkRead(Mmio, HDGFX_REG(FDI_RX_CTL, Index)), FDI_RX_TRAINING_CPT, ILK_FDI_PATTERN_2));
        IlkDelay(Mmio, IVB_FDI_TP2_POLL_US);

        if (IvbFdiLock(Mmio, Index, HDGFX_MASK(FDI_RX_SYMBOL_LOCK), IVB_FDI_TP2_POLL_US))
            return HdgfxModesetSuccess;
        Status = HdgfxModesetFdiSymbolLockFailed;
    }
    return Status;
}

static void
IvbFinishFdi(
    const HDGFX_MMIO *Mmio,
    uint32_t Index)
{
    uint32_t Tx = IlkRead(Mmio, HDGFX_REG(FDI_TX_CTL, Index));
    uint32_t Rx = IlkRead(Mmio, HDGFX_REG(FDI_RX_CTL, Index));

    Tx = HDGFX_REPLACE(Tx, FDI_TX_TRAINING_IVB, ILK_FDI_NORMAL);
    IlkWrite(Mmio, HDGFX_REG(FDI_TX_CTL, Index), HDGFX_REPLACE(Tx, FDI_TX_ENHANCED_FRAMING, 1));
    Rx = HDGFX_REPLACE(Rx, FDI_RX_TRAINING_CPT, ILK_FDI_NORMAL);
    Rx = HDGFX_REPLACE(Rx, FDI_RX_ENHANCED_FRAMING, 1);
    IlkWrite(Mmio, HDGFX_REG(FDI_RX_CTL, Index), Rx);
    IlkDelay(Mmio, ILK_FDI_IDLE_PATTERN_US);
    IlkWrite(Mmio, HDGFX_REG(FDI_RX_CTL, Index),
             Rx | HDGFX_MASK(FDI_RX_FS_ERROR_CORRECT) | HDGFX_MASK(FDI_RX_FE_ERROR_CORRECT));
}

static uint32_t
IlkFitterControl(
    const HDGFX_DISPLAY_STATE *Target,
    uint32_t Index)
{
    uint32_t Value = HDGFX_SET(PF_ENABLE, 1) | HDGFX_SET(PF_FILTER, ILK_PF_FILTER_MEDIUM);

    return Target->Gen >= 7 ? Value | HDGFX_SET(PF_PIPE_IVB, Index) : Value;
}

static uint32_t
IlkLvdsControl(
    uint32_t Value,
    const HDGFX_DISPLAY_STATE *Target)
{
    const HDGFX_LVDS *Lvds = &Target->Lvds;
    uint32_t Pairs = Lvds->DualChannel ? ILK_LVDS_PAIRS_ACTIVE : 0;

    Value = HDGFX_REPLACE(Value, LVDS_ENABLE, 1);
    Value = Target->Gen >= 6 ? HDGFX_REPLACE(Value, LVDS_TRANSCODER_CPT, Lvds->Transcoder) :
                               HDGFX_REPLACE(Value, LVDS_TRANSCODER, Lvds->Transcoder);
    Value = HDGFX_REPLACE(Value, LVDS_HSYNC_INVERT, Lvds->HSyncActiveLow);
    Value = HDGFX_REPLACE(Value, LVDS_VSYNC_INVERT, Lvds->VSyncActiveLow);
    Value = HDGFX_REPLACE(Value, LVDS_A_CONTROL, ILK_LVDS_PAIRS_ACTIVE);
    Value = HDGFX_REPLACE(Value, LVDS_A3_CONTROL, Lvds->Bpc == 8 ? ILK_LVDS_PAIRS_ACTIVE : 0);
    Value = HDGFX_REPLACE(Value, LVDS_B_CONTROL, Pairs);
    return HDGFX_REPLACE(Value, LVDS_CLKB_CONTROL, Pairs);
}

static void
IlkEnableDpll(
    const HDGFX_MMIO *Mmio,
    uint32_t Index,
    const HDGFX_DPLL *Dpll)
{
    uint32_t Ratio = Dpll->P2 == 7 ? ILK_CB_TUNE_DUAL_RATIO : ILK_CB_TUNE_SINGLE_RATIO;
    uint32_t M = 5 * Dpll->M1 + Dpll->M2;
    uint32_t P1 = 1u << (Dpll->P1 - 1);
    uint32_t Divisors;

    Divisors = HDGFX_SET(FP_N, Dpll->N - 2) | HDGFX_SET(FP_M1, Dpll->M1 - 2) | HDGFX_SET(FP_M2, Dpll->M2 - 2) |
               HDGFX_SET(FP_CB_TUNE, M < Ratio * Dpll->N ? ILK_CB_TUNE_ON : 0);
    IlkWrite(Mmio, HDGFX_REG(PCH_FP0, Index), Divisors);
    IlkWrite(Mmio, HDGFX_REG(PCH_FP1, Index), Divisors);
    IlkWrite(Mmio, HDGFX_REG(PCH_DPLL, Index),
             HDGFX_SET(DPLL_VCO_ENABLE, 1) |
             HDGFX_SET(DPLL_MODE, Dpll->Mode == HdgfxDpllModeLvds ? ILK_DPLL_MODE_LVDS : ILK_DPLL_MODE_NON_LVDS) |
             HDGFX_SET(DPLL_P2, Dpll->P2 == 7 ? 1 : 0) | HDGFX_SET(DPLL_P1_FP0, P1) | HDGFX_SET(DPLL_P1_FP1, P1) |
             HDGFX_SET(DPLL_REFERENCE, Dpll->Reference));
    IlkDelay(Mmio, ILK_DPLL_WARMUP_US);
}

static HDGFX_MODESET_STATUS
IlkEnablePipe(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Target,
    uint32_t Index)
{
    const HDGFX_PIPE *Pipe = &Target->Pipes[Index];
    const HDGFX_TRANSCODER *Transcoder = &Target->Transcoders[Index];
    uint32_t Value;
    uint32_t Wm;

    Value = IlkRead(Mmio, HDGFX_REG(FDI_RX_CTL, Index));
    Value = HDGFX_REPLACE(Value, FDI_RX_WIDTH, IlkFdiWidthEncoding(Transcoder->FdiRx.Lanes));
    Value = HDGFX_REPLACE(Value, FDI_RX_BPC, IlkBpcEncoding(Transcoder->FdiRx.Bpc));
    Value = HDGFX_REPLACE(Value, FDI_RX_PLL_ENABLE, 1);
    IlkWrite(Mmio, HDGFX_REG(FDI_RX_CTL, Index), Value);
    IlkDelay(Mmio, ILK_FDI_RX_PLL_WARMUP_US + ILK_DMI_LATENCY_US);
    IlkWrite(Mmio, HDGFX_REG(FDI_RX_CTL, Index), HDGFX_REPLACE(Value, FDI_RX_PCDCLK, 1));
    if (Target->Gen >= 6)
    {
        IlkDelay(Mmio, IVB_FDI_RX_PCDCLK_US);
        IlkWrite(Mmio, HDGFX_REG(FDI_TX_CTL, Index),
                 HDGFX_REPLACE(IlkRead(Mmio, HDGFX_REG(FDI_TX_CTL, Index)), FDI_TX_PLL_ENABLE, 1));
        IlkDelay(Mmio, IVB_FDI_TX_PLL_WARMUP_US);
    }

    if (Pipe->PanelFitter.Enabled)
    {
        IlkWrite(Mmio, HDGFX_REG(PF_CTL_1, Index), IlkFitterControl(Target, Index));
        IlkWrite(Mmio, HDGFX_REG(PF_WIN_POS, Index),
                 HDGFX_SET(PF_X, Pipe->PanelFitter.X) | HDGFX_SET(PF_Y, Pipe->PanelFitter.Y));
        IlkWrite(Mmio, HDGFX_REG(PF_WIN_SZ, Index),
                 HDGFX_SET(PF_X, Pipe->PanelFitter.Width) | HDGFX_SET(PF_Y, Pipe->PanelFitter.Height));
    }

    IlkWriteTimings(Mmio,
                    HDGFX_REG(PIPE_HTOTAL, Index), HDGFX_REG(PIPE_HBLANK, Index), HDGFX_REG(PIPE_HSYNC, Index),
                    HDGFX_REG(PIPE_VTOTAL, Index), HDGFX_REG(PIPE_VBLANK, Index), HDGFX_REG(PIPE_VSYNC, Index),
                    &Pipe->Timings);
    IlkWrite(Mmio, HDGFX_REG(PIPE_SRC, Index),
             HDGFX_SET(PIPE_SRC_WIDTH, Pipe->SourceWidth - 1) | HDGFX_SET(PIPE_SRC_HEIGHT, Pipe->SourceHeight - 1));
    IlkWrite(Mmio, HDGFX_REG(PIPE_DATA_M1, Index),
             HDGFX_SET(M_N_TU_SIZE, Pipe->Fdi.TuSize - 1) | HDGFX_SET(M_N_VALUE, Pipe->Fdi.DataM));
    IlkWrite(Mmio, HDGFX_REG(PIPE_DATA_N1, Index), HDGFX_SET(M_N_VALUE, Pipe->Fdi.DataN));
    IlkWrite(Mmio, HDGFX_REG(PIPE_LINK_M1, Index), HDGFX_SET(M_N_VALUE, Pipe->Fdi.LinkM));
    IlkWrite(Mmio, HDGFX_REG(PIPE_LINK_N1, Index), HDGFX_SET(M_N_VALUE, Pipe->Fdi.LinkN));

    IlkWrite(Mmio, HDGFX_REG(PIPE_CONF, Index),
             HDGFX_SET(PIPE_CONF_ENABLE, 1) | HDGFX_SET(PIPE_CONF_BPC, IlkBpcEncoding(Pipe->Bpc)) |
             HDGFX_SET(PIPE_CONF_DITHER, Pipe->Dither));

    for (Wm = ILK_WM_LP_COUNT; Wm-- > 0;)
        IlkClearBit(Mmio, HDGFX_REG(WM_LP, Wm), HDGFX_MASK(WM_LP_ENABLE));
    IlkWrite(Mmio, HDGFX_REG(WM0_PIPE, Index), ILK_WM0_DEFAULT);
    IlkWrite(Mmio, HDGFX_REG(DSP_CNTR, Index),
             HDGFX_SET(DSP_ENABLE, 1) | HDGFX_SET(DSP_FORMAT, Pipe->Primary.Format) |
             HDGFX_SET(DSP_TRICKLE_FEED_DISABLE, 1));
    IlkWrite(Mmio, HDGFX_REG(DSP_STRIDE, Index), Pipe->Primary.Stride);
    IlkWrite(Mmio, HDGFX_REG(DSP_LINOFF, Index), 0);
    IlkWrite(Mmio, HDGFX_REG(DSP_TILEOFF, Index), 0);
    IlkWrite(Mmio, HDGFX_REG(DSP_SURF, Index), Pipe->Primary.Surface);

    if (Target->Gen >= 7)
        return IvbTrainFdi(Mmio, Index, Pipe->FdiTx.Lanes);
    return IlkTrainFdi(Mmio, Index, Pipe->FdiTx.Lanes);
}

static HDGFX_MODESET_STATUS
IlkEnableTranscoder(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Target,
    uint32_t Index)
{
    const HDGFX_TRANSCODER *Transcoder = &Target->Transcoders[Index];
    uint32_t Sel;

    if (Target->Gen >= 6)
    {
        Sel = IlkRead(Mmio, HDGFX_REG(PCH_DPLL_SEL, 0));
        Sel &= ~(((1u << CPT_DPLL_SEL_ENABLE) | (1u << CPT_DPLL_SEL_B)) << (Index * CPT_DPLL_SEL_STRIDE));
        Sel |= ((1u << CPT_DPLL_SEL_ENABLE) | (Transcoder->Dpll << CPT_DPLL_SEL_B)) << (Index * CPT_DPLL_SEL_STRIDE);
        IlkWrite(Mmio, HDGFX_REG(PCH_DPLL_SEL, 0), Sel);
    }
    IlkEnableDpll(Mmio, Transcoder->Dpll, &Target->Dplls[Transcoder->Dpll]);

    IlkWriteTimings(Mmio,
                    HDGFX_REG(TRANS_HTOTAL, Index), HDGFX_REG(TRANS_HBLANK, Index), HDGFX_REG(TRANS_HSYNC, Index),
                    HDGFX_REG(TRANS_VTOTAL, Index), HDGFX_REG(TRANS_VBLANK, Index), HDGFX_REG(TRANS_VSYNC, Index),
                    &Transcoder->Timings);
    if (Target->Gen >= 7)
        IvbFinishFdi(Mmio, Index);
    if (Target->Gen >= 6)
    {
        IlkWrite(Mmio, HDGFX_REG(TRANS_CHICKEN2, Index),
                 IlkRead(Mmio, HDGFX_REG(TRANS_CHICKEN2, Index)) | HDGFX_MASK(TRANS_CHICKEN2_OVERRIDE));
        IlkWrite(Mmio, HDGFX_REG(TRANS_CONF, Index), HDGFX_SET(TRANS_CONF_ENABLE, 1));
    }
    else
    {
        IlkWrite(Mmio, HDGFX_REG(TRANS_CONF, Index),
                 HDGFX_SET(TRANS_CONF_ENABLE, 1) | HDGFX_SET(TRANS_CONF_BPC, IlkBpcEncoding(Transcoder->FdiRx.Bpc)));
    }
    if (!IlkWaitFor(Mmio, HDGFX_REG(TRANS_CONF, Index), HDGFX_MASK(TRANS_CONF_STATE), HDGFX_MASK(TRANS_CONF_STATE),
                    ILK_PIPE_TIMEOUT_US, ILK_POLL_US))
    {
        return HdgfxModesetTranscoderOnTimeout;
    }
    return HdgfxModesetSuccess;
}

static HDGFX_MODESET_STATUS
IlkEnableLvds(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Target)
{
    uint32_t Index = Target->Lvds.Transcoder;
    const HDGFX_PANEL_POWER *Power = &Target->PanelPower;
    HDGFX_MODESET_STATUS Status;

    Status = IlkEnablePipe(Mmio, Target, Index);
    if (Status != HdgfxModesetSuccess)
        return Status;
    IlkWrite(Mmio, HDGFX_REG(PCH_LVDS, 0), IlkLvdsControl(IlkRead(Mmio, HDGFX_REG(PCH_LVDS, 0)), Target));
    Status = IlkEnableTranscoder(Mmio, Target, Index);
    if (Status != HdgfxModesetSuccess)
        return Status;

    IlkWritePanelControl(Mmio, true, false);
    if (!IlkWaitFor(Mmio, HDGFX_REG(PCH_PP_STATUS, 0),
                    HDGFX_MASK(PP_STATUS_ON) | HDGFX_MASK(PP_STATUS_SEQUENCE), HDGFX_MASK(PP_STATUS_ON),
                    2 * (Power->PowerCycleUs + Power->PowerUpUs + Power->BacklightOnUs) + ILK_PANEL_TIMEOUT_MARGIN_US,
                    ILK_POLL_US))
    {
        return HdgfxModesetPanelOnTimeout;
    }
    if (Power->BacklightEnabled)
        IlkWritePanelControl(Mmio, true, true);
    return HdgfxModesetSuccess;
}

HDGFX_MODESET_STATUS
HdgfxIlkDisableLvds(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Current)
{
    return IlkDisableLvds(Mmio, Current);
}

static uint32_t
IlkScanLine(
    const HDGFX_MMIO *Mmio,
    uint32_t Index)
{
    return HDGFX_GET(IlkRead(Mmio, HDGFX_REG(PIPE_DSL, Index)), PIPE_DSL_LINE);
}

static HDGFX_MODESET_STATUS
IlkUpdateSource(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Target,
    uint32_t Index)
{
    const HDGFX_PIPE *Pipe = &Target->Pipes[Index];
    const HDGFX_DPLL *Dpll = &Target->Dplls[Target->Transcoders[Index].Dpll];
    uint32_t VActive = Pipe->Timings.VActive;
    uint32_t Evasion = IlkDivide((uint64_t)ILK_VBLANK_EVASION_US * Dpll->DotKHz, (uint64_t)Pipe->Timings.HTotal * 1000) + 1;
    uint32_t Elapsed;
    uint32_t Line;

    if (Evasion >= VActive)
        return HdgfxModesetInvalidMode;
    for (Elapsed = 0;; Elapsed += ILK_SCANLINE_POLL_US)
    {
        Line = IlkScanLine(Mmio, Index);
        if (Line + Evasion < VActive)
            break;
        if (Elapsed >= ILK_PIPE_TIMEOUT_US)
            return HdgfxModesetVblankTimeout;
        IlkDelay(Mmio, ILK_SCANLINE_POLL_US);
    }

    IlkWrite(Mmio, HDGFX_REG(PIPE_SRC, Index),
             HDGFX_SET(PIPE_SRC_WIDTH, Pipe->SourceWidth - 1) | HDGFX_SET(PIPE_SRC_HEIGHT, Pipe->SourceHeight - 1));
    if (Pipe->PanelFitter.Enabled)
    {
        IlkWrite(Mmio, HDGFX_REG(PF_CTL_1, Index), IlkFitterControl(Target, Index));
        IlkWrite(Mmio, HDGFX_REG(PF_WIN_POS, Index),
                 HDGFX_SET(PF_X, Pipe->PanelFitter.X) | HDGFX_SET(PF_Y, Pipe->PanelFitter.Y));
        IlkWrite(Mmio, HDGFX_REG(PF_WIN_SZ, Index),
                 HDGFX_SET(PF_X, Pipe->PanelFitter.Width) | HDGFX_SET(PF_Y, Pipe->PanelFitter.Height));
    }
    else
    {
        IlkWrite(Mmio, HDGFX_REG(PF_CTL_1, Index), 0);
        IlkWrite(Mmio, HDGFX_REG(PF_WIN_POS, Index), 0);
        IlkWrite(Mmio, HDGFX_REG(PF_WIN_SZ, Index), 0);
    }
    IlkWrite(Mmio, HDGFX_REG(DSP_STRIDE, Index), Pipe->Primary.Stride);
    IlkWrite(Mmio, HDGFX_REG(DSP_SURF, Index), Pipe->Primary.Surface);

    for (Elapsed = 0;; Elapsed += ILK_SCANLINE_POLL_US)
    {
        uint32_t Now = IlkScanLine(Mmio, Index);

        if (Now >= VActive || Now < Line)
            return HdgfxModesetSuccess;
        if (Elapsed >= ILK_PIPE_TIMEOUT_US)
            return HdgfxModesetVblankTimeout;
        IlkDelay(Mmio, ILK_SCANLINE_POLL_US);
    }
}

#define HDGFX_COMPARE(Field) \
    do { if (Target->Field != Actual->Field) return #Field; } while (0)

static const char *
CompareTimings(
    const HDGFX_TIMINGS *Target,
    const HDGFX_TIMINGS *Actual)
{
    HDGFX_COMPARE(HActive);
    HDGFX_COMPARE(HBlankStart);
    HDGFX_COMPARE(HBlankEnd);
    HDGFX_COMPARE(HSyncStart);
    HDGFX_COMPARE(HSyncEnd);
    HDGFX_COMPARE(HTotal);
    HDGFX_COMPARE(VActive);
    HDGFX_COMPARE(VBlankStart);
    HDGFX_COMPARE(VBlankEnd);
    HDGFX_COMPARE(VSyncStart);
    HDGFX_COMPARE(VSyncEnd);
    HDGFX_COMPARE(VTotal);
    return NULL;
}

static const char *
IlkComparePipe(
    const HDGFX_DISPLAY_STATE *Target,
    const HDGFX_DISPLAY_STATE *Actual,
    uint32_t Index)
{
    uint32_t Dpll = Target->Transcoders[Index].Dpll;
    const char *Mismatch;

    HDGFX_COMPARE(Pipes[Index].Enabled);
    HDGFX_COMPARE(Pipes[Index].Active);
    HDGFX_COMPARE(Pipes[Index].Bpc);
    HDGFX_COMPARE(Pipes[Index].Dither);
    HDGFX_COMPARE(Pipes[Index].InterlaceMode);
    Mismatch = CompareTimings(&Target->Pipes[Index].Timings, &Actual->Pipes[Index].Timings);
    if (Mismatch)
        return "Pipes[Index].Timings";
    HDGFX_COMPARE(Pipes[Index].Fdi.TuSize);
    HDGFX_COMPARE(Pipes[Index].Fdi.DataM);
    HDGFX_COMPARE(Pipes[Index].Fdi.DataN);
    HDGFX_COMPARE(Pipes[Index].Fdi.LinkM);
    HDGFX_COMPARE(Pipes[Index].Fdi.LinkN);
    HDGFX_COMPARE(Pipes[Index].FdiTx.Enabled);
    HDGFX_COMPARE(Pipes[Index].FdiTx.Training);
    HDGFX_COMPARE(Pipes[Index].FdiTx.Lanes);
    HDGFX_COMPARE(Pipes[Index].Primary.Enabled);
    HDGFX_COMPARE(Pipes[Index].Primary.XTiled);
    HDGFX_COMPARE(Pipes[Index].Primary.Format);
    HDGFX_COMPARE(Pipes[Index].Primary.LinearOffset);

    HDGFX_COMPARE(Transcoders[Index].Enabled);
    HDGFX_COMPARE(Transcoders[Index].Active);
    HDGFX_COMPARE(Transcoders[Index].InterlaceMode);
    Mismatch = CompareTimings(&Target->Transcoders[Index].Timings, &Actual->Transcoders[Index].Timings);
    if (Mismatch)
        return "Transcoders[Index].Timings";
    HDGFX_COMPARE(Transcoders[Index].FdiRx.Enabled);
    HDGFX_COMPARE(Transcoders[Index].FdiRx.PllEnabled);
    HDGFX_COMPARE(Transcoders[Index].FdiRx.Training);
    HDGFX_COMPARE(Transcoders[Index].FdiRx.Lanes);
    HDGFX_COMPARE(Transcoders[Index].FdiRx.Bpc);

    HDGFX_COMPARE(Dplls[Dpll].Enabled);
    HDGFX_COMPARE(Dplls[Dpll].Mode);
    HDGFX_COMPARE(Dplls[Dpll].Reference);
    HDGFX_COMPARE(Dplls[Dpll].N);
    HDGFX_COMPARE(Dplls[Dpll].M1);
    HDGFX_COMPARE(Dplls[Dpll].M2);
    HDGFX_COMPARE(Dplls[Dpll].P1);
    HDGFX_COMPARE(Dplls[Dpll].P2);
    return NULL;
}

static const char *
IlkCompareLink(
    const HDGFX_DISPLAY_STATE *Target,
    const HDGFX_DISPLAY_STATE *Actual)
{
    HDGFX_COMPARE(Lvds.Enabled);
    HDGFX_COMPARE(Lvds.Transcoder);
    HDGFX_COMPARE(Lvds.HSyncActiveLow);
    HDGFX_COMPARE(Lvds.VSyncActiveLow);
    HDGFX_COMPARE(Lvds.DualChannel);
    HDGFX_COMPARE(Lvds.Bpc);
    HDGFX_COMPARE(PanelPower.On);
    HDGFX_COMPARE(PanelPower.TargetOn);
    HDGFX_COMPARE(PanelPower.BacklightEnabled);
    return IlkComparePipe(Target, Actual, Target->Lvds.Transcoder);
}

static const char *
IlkCompareCrtLink(
    const HDGFX_DISPLAY_STATE *Target,
    const HDGFX_DISPLAY_STATE *Actual)
{
    HDGFX_COMPARE(Crt.Enabled);
    HDGFX_COMPARE(Crt.Transcoder);
    HDGFX_COMPARE(Crt.HSyncActiveHigh);
    HDGFX_COMPARE(Crt.VSyncActiveHigh);
    return IlkComparePipe(Target, Actual, Target->Crt.Transcoder);
}

const char *
HdgfxCompareLvdsState(
    const HDGFX_DISPLAY_STATE *Target,
    const HDGFX_DISPLAY_STATE *Actual)
{
    uint32_t Index = Target->Lvds.Transcoder;
    const char *Mismatch;

    Mismatch = IlkCompareLink(Target, Actual);
    if (Mismatch)
        return Mismatch;
    HDGFX_COMPARE(Pipes[Index].SourceWidth);
    HDGFX_COMPARE(Pipes[Index].SourceHeight);
    HDGFX_COMPARE(Pipes[Index].PanelFitter.Enabled);
    if (Target->Pipes[Index].PanelFitter.Enabled)
    {
        HDGFX_COMPARE(Pipes[Index].PanelFitter.X);
        HDGFX_COMPARE(Pipes[Index].PanelFitter.Y);
        HDGFX_COMPARE(Pipes[Index].PanelFitter.Width);
        HDGFX_COMPARE(Pipes[Index].PanelFitter.Height);
    }
    HDGFX_COMPARE(Pipes[Index].Primary.Stride);
    HDGFX_COMPARE(Pipes[Index].Primary.Surface);
    return NULL;
}

HDGFX_MODESET_STATUS
HdgfxIlkCommitLvds(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Current,
    const HDGFX_DISPLAY_STATE *Target)
{
    HDGFX_MODESET_STATUS Status;

    if (IlkCompareLink(Target, Current) == NULL)
        return IlkUpdateSource(Mmio, Target, Target->Lvds.Transcoder);

    Status = IlkDisableLvds(Mmio, Target);
    if (Status != HdgfxModesetSuccess)
        return Status;
    return IlkEnableLvds(Mmio, Target);
}

HDGFX_MODESET_STATUS
HdgfxIlkDetectCrt(
    const HDGFX_MMIO *Mmio,
    bool *Attached)
{
    uint32_t Value = IlkRead(Mmio, HDGFX_REG(PCH_ADPA, 0));

    *Attached = false;
    Value &= ~HDGFX_MASK(ADPA_HOTPLUG_STATUS);
    IlkWrite(Mmio, HDGFX_REG(PCH_ADPA, 0), HDGFX_REPLACE(Value, ADPA_FORCE_TRIGGER, 1));
    if (!IlkWaitFor(Mmio, HDGFX_REG(PCH_ADPA, 0), HDGFX_MASK(ADPA_FORCE_TRIGGER), 0,
                    ILK_CRT_DETECT_TIMEOUT_US, ILK_POLL_US))
    {
        return HdgfxModesetDetectTimeout;
    }

    Value = IlkRead(Mmio, HDGFX_REG(PCH_ADPA, 0));
    *Attached = HDGFX_GET(Value, ADPA_HOTPLUG_STATUS) != 0;
    IlkWrite(Mmio, HDGFX_REG(PCH_ADPA, 0), Value | HDGFX_MASK(ADPA_HOTPLUG_STATUS));
    return HdgfxModesetSuccess;
}

HDGFX_MODESET_STATUS
HdgfxIlkComputeCrt(
    const HDGFX_DISPLAY_STATE *Current,
    const HDGFX_CRT_MODE *Mode,
    HDGFX_DISPLAY_STATE *Target)
{
    uint32_t Index = Mode->Pipe;
    HDGFX_DPLL *Dpll = &Target->Dplls[Index];
    const HDGFX_TIMINGS *Timings;
    uint32_t SourceWidth;
    uint32_t SourceHeight;
    HDGFX_MODESET_STATUS Status;

    *Target = *Current;
    if (Mode->Timing == NULL || Index >= Current->PipeCount || Index >= Current->TranscoderCount ||
        (Current->Lvds.Enabled && Current->Lvds.Transcoder == Index) || !IlkTimingsFit(&Mode->Timing->Timings))
    {
        return HdgfxModesetInvalidMode;
    }

    Timings = &Mode->Timing->Timings;
    SourceWidth = Mode->SourceWidth != 0 ? Mode->SourceWidth : Timings->HActive;
    SourceHeight = Mode->SourceHeight != 0 ? Mode->SourceHeight : Timings->VActive;
    if (SourceWidth > Timings->HActive || SourceHeight > Timings->VActive ||
        !IlkSurfaceFits(SourceWidth, Mode->Stride, Mode->Surface))
    {
        return HdgfxModesetInvalidMode;
    }
    if (Mode->Timing->DotKHz < ILK_DAC_MIN_KHZ || Mode->Timing->DotKHz > ILK_DAC_MAX_KHZ)
        return HdgfxModesetNoClock;
    Target->NonSpreadReference = true;

    memset(Dpll, 0, sizeof(*Dpll));
    if (!IlkFindDpll(Mode->Timing->DotKHz, ILK_DAC_P2, ILK_DAC_P_MIN, ILK_DAC_P_MAX, Dpll))
        return HdgfxModesetNoClock;
    Dpll->Enabled = true;
    Dpll->Mode = HdgfxDpllModeNonLvds;
    Dpll->Reference = ILK_DPLL_REFERENCE_DREF;

    Status = IlkComputePipe(Current, Index, Timings, ILK_DAC_BPC, SourceWidth, SourceHeight,
                            Mode->Stride, Mode->Surface, Target);
    if (Status != HdgfxModesetSuccess)
        return Status;
    IlkComputeFitter(&Target->Pipes[Index].Timings, SourceWidth, SourceHeight, Mode->Scaling,
                     &Target->Pipes[Index].PanelFitter);

    Target->Crt.Enabled = true;
    Target->Crt.Transcoder = Index;
    Target->Crt.HSyncActiveHigh = Mode->Timing->HSyncPositive;
    Target->Crt.VSyncActiveHigh = Mode->Timing->VSyncPositive;
    return HdgfxModesetSuccess;
}

static uint32_t
IlkAdpaControl(
    uint32_t Value,
    const HDGFX_DISPLAY_STATE *Target)
{
    const HDGFX_CRT *Crt = &Target->Crt;

    Value &= ~HDGFX_MASK(ADPA_HOTPLUG_STATUS);
    Value = HDGFX_REPLACE(Value, ADPA_ENABLE, 1);
    Value = Target->Gen >= 6 ? HDGFX_REPLACE(Value, ADPA_TRANSCODER_CPT, Crt->Transcoder) :
                               HDGFX_REPLACE(Value, ADPA_TRANSCODER, Crt->Transcoder);
    Value = HDGFX_REPLACE(Value, ADPA_HSYNC_HIGH, Crt->HSyncActiveHigh);
    return HDGFX_REPLACE(Value, ADPA_VSYNC_HIGH, Crt->VSyncActiveHigh);
}

HDGFX_MODESET_STATUS
HdgfxIlkDisableCrt(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Current)
{
    if (!Current->Crt.Enabled)
        return HdgfxModesetSuccess;
    if (Current->Lvds.Enabled && Current->Lvds.Transcoder == Current->Crt.Transcoder)
        return HdgfxModesetInvalidMode;
    return IlkDisablePipe(Mmio, Current, Current->Crt.Transcoder, HDGFX_REG(PCH_ADPA, 0),
                          HDGFX_MASK(ADPA_ENABLE) | HDGFX_MASK(ADPA_HOTPLUG_STATUS));
}

void
HdgfxIlkEnableCrtHotplug(
    const HDGFX_MMIO *Mmio,
    bool Enable)
{
    uint32_t Value = IlkRead(Mmio, HDGFX_REG(PCH_ADPA, 0)) & ~HDGFX_MASK(ADPA_HOTPLUG_STATUS);

    IlkWrite(Mmio, HDGFX_REG(PCH_ADPA, 0), HDGFX_REPLACE(Value, ADPA_HOTPLUG_ENABLE, Enable));
}

const char *
HdgfxCompareCrtState(
    const HDGFX_DISPLAY_STATE *Target,
    const HDGFX_DISPLAY_STATE *Actual)
{
    uint32_t Index = Target->Crt.Transcoder;
    const char *Mismatch;

    Mismatch = IlkCompareCrtLink(Target, Actual);
    if (Mismatch)
        return Mismatch;
    HDGFX_COMPARE(Pipes[Index].SourceWidth);
    HDGFX_COMPARE(Pipes[Index].SourceHeight);
    HDGFX_COMPARE(Pipes[Index].PanelFitter.Enabled);
    if (Target->Pipes[Index].PanelFitter.Enabled)
    {
        HDGFX_COMPARE(Pipes[Index].PanelFitter.X);
        HDGFX_COMPARE(Pipes[Index].PanelFitter.Y);
        HDGFX_COMPARE(Pipes[Index].PanelFitter.Width);
        HDGFX_COMPARE(Pipes[Index].PanelFitter.Height);
    }
    HDGFX_COMPARE(Pipes[Index].Primary.Stride);
    HDGFX_COMPARE(Pipes[Index].Primary.Surface);
    return NULL;
}

HDGFX_MODESET_STATUS
HdgfxIlkCommitCrt(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Current,
    const HDGFX_DISPLAY_STATE *Target)
{
    uint32_t Index = Target->Crt.Transcoder;
    HDGFX_MODESET_STATUS Status;

    if (IlkCompareCrtLink(Target, Current) == NULL)
        return IlkUpdateSource(Mmio, Target, Index);

    if (Current->Crt.Enabled && Current->Crt.Transcoder != Index)
    {
        Status = HdgfxIlkDisableCrt(Mmio, Current);
        if (Status != HdgfxModesetSuccess)
            return Status;
    }
    Status = IlkDisablePipe(Mmio, Target, Index, HDGFX_REG(PCH_ADPA, 0),
                            HDGFX_MASK(ADPA_ENABLE) | HDGFX_MASK(ADPA_HOTPLUG_STATUS));
    if (Status != HdgfxModesetSuccess)
        return Status;
    if (HDGFX_GET(IlkRead(Mmio, HDGFX_REG(PCH_DREF_CONTROL, 0)), DREF_NONSPREAD_SOURCE) != ILK_DREF_INTEGRATED)
    {
        IlkWrite(Mmio, HDGFX_REG(PCH_DREF_CONTROL, 0),
                 HDGFX_REPLACE(IlkRead(Mmio, HDGFX_REG(PCH_DREF_CONTROL, 0)), DREF_NONSPREAD_SOURCE,
                               ILK_DREF_INTEGRATED));
        IlkDelay(Mmio, ILK_DREF_WARMUP_US);
    }
    Status = IlkEnablePipe(Mmio, Target, Index);
    if (Status != HdgfxModesetSuccess)
        return Status;
    Status = IlkEnableTranscoder(Mmio, Target, Index);
    if (Status != HdgfxModesetSuccess)
        return Status;
    IlkWrite(Mmio, HDGFX_REG(PCH_ADPA, 0), IlkAdpaControl(IlkRead(Mmio, HDGFX_REG(PCH_ADPA, 0)), Target));
    return HdgfxModesetSuccess;
}

const char *
HdgfxModesetStatusName(
    HDGFX_MODESET_STATUS Status)
{
    static const char *const Names[] =
    {
        "success",
        "invalid mode",
        "no usable clock",
        "FDI bandwidth exceeded",
        "panel power-off timeout",
        "pipe-off timeout",
        "transcoder-off timeout",
        "FDI bit lock failed",
        "FDI symbol lock failed",
        "transcoder-on timeout",
        "panel power-on timeout",
        "vertical blank timeout",
        "CRT detection timeout",
    };

    return (uint32_t)Status < sizeof(Names) / sizeof(Names[0]) ? Names[Status] : "unknown";
}
