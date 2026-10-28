/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Ironlake display state readout
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#include <string.h>

#define ILK_PIPES 2
#define ILK_TRANSCODERS 2
#define ILK_DPLLS 2
#define ILK_DPLL_REFERENCE_KHZ 120000
#define ILK_FDI_PLL_REFERENCE_KHZ 100000
#define ILK_DREF_INTEGRATED 2

static uint32_t
IlkRead(
    const HDGFX_MMIO *Mmio,
    uint32_t Offset)
{
    return Mmio->Read(Mmio->Context, Offset);
}

static uint32_t
IlkBpc(
    uint32_t Encoding)
{
    static const uint8_t Bpc[] = { 8, 10, 6, 12 };

    return Encoding < sizeof(Bpc) ? Bpc[Encoding] : 0;
}

static uint32_t
IlkFdiLanes(
    uint32_t Encoding)
{
    static const uint8_t Lanes[] = { 1, 2, 3, 4, 0, 0, 0, 8 };

    return Lanes[Encoding & 7];
}

static uint32_t
IlkP1(
    uint32_t OneHot)
{
    uint32_t Divisor;

    if (OneHot == 0 || (OneHot & (OneHot - 1)) != 0)
        return 0;
    for (Divisor = 1; (OneHot & 1) == 0; OneHot >>= 1)
        Divisor++;
    return Divisor;
}

static void
IlkReadTimings(
    const HDGFX_MMIO *Mmio,
    uint32_t HTotal,
    uint32_t HBlank,
    uint32_t HSync,
    uint32_t VTotal,
    uint32_t VBlank,
    uint32_t VSync,
    HDGFX_TIMINGS *Timings)
{
    uint32_t Value;

    Value = IlkRead(Mmio, HTotal);
    Timings->HActive = HDGFX_GET(Value, TIMING_ACTIVE) + 1;
    Timings->HTotal = HDGFX_GET(Value, TIMING_HIGH) + 1;
    Value = IlkRead(Mmio, HBlank);
    Timings->HBlankStart = HDGFX_GET(Value, TIMING_LOW) + 1;
    Timings->HBlankEnd = HDGFX_GET(Value, TIMING_HIGH) + 1;
    Value = IlkRead(Mmio, HSync);
    Timings->HSyncStart = HDGFX_GET(Value, TIMING_LOW) + 1;
    Timings->HSyncEnd = HDGFX_GET(Value, TIMING_HIGH) + 1;
    Value = IlkRead(Mmio, VTotal);
    Timings->VActive = HDGFX_GET(Value, TIMING_ACTIVE) + 1;
    Timings->VTotal = HDGFX_GET(Value, TIMING_HIGH) + 1;
    Value = IlkRead(Mmio, VBlank);
    Timings->VBlankStart = HDGFX_GET(Value, TIMING_LOW) + 1;
    Timings->VBlankEnd = HDGFX_GET(Value, TIMING_HIGH) + 1;
    Value = IlkRead(Mmio, VSync);
    Timings->VSyncStart = HDGFX_GET(Value, TIMING_LOW) + 1;
    Timings->VSyncEnd = HDGFX_GET(Value, TIMING_HIGH) + 1;
}

static void
IlkReadPipe(
    const HDGFX_MMIO *Mmio,
    uint32_t Index,
    HDGFX_PIPE *Pipe)
{
    uint32_t Value;

    Value = IlkRead(Mmio, HDGFX_REG(PIPE_CONF, Index));
    Pipe->Enabled = HDGFX_GET(Value, PIPE_CONF_ENABLE) != 0;
    Pipe->Active = HDGFX_GET(Value, PIPE_CONF_STATE) != 0;
    Pipe->Dither = HDGFX_GET(Value, PIPE_CONF_DITHER) != 0;
    Pipe->Bpc = IlkBpc(HDGFX_GET(Value, PIPE_CONF_BPC));
    Pipe->InterlaceMode = HDGFX_GET(Value, PIPE_CONF_INTERLACE);

    IlkReadTimings(Mmio,
                   HDGFX_REG(PIPE_HTOTAL, Index),
                   HDGFX_REG(PIPE_HBLANK, Index),
                   HDGFX_REG(PIPE_HSYNC, Index),
                   HDGFX_REG(PIPE_VTOTAL, Index),
                   HDGFX_REG(PIPE_VBLANK, Index),
                   HDGFX_REG(PIPE_VSYNC, Index),
                   &Pipe->Timings);

    Value = IlkRead(Mmio, HDGFX_REG(PIPE_SRC, Index));
    Pipe->SourceWidth = HDGFX_GET(Value, PIPE_SRC_WIDTH) + 1;
    Pipe->SourceHeight = HDGFX_GET(Value, PIPE_SRC_HEIGHT) + 1;

    Value = IlkRead(Mmio, HDGFX_REG(PIPE_DATA_M1, Index));
    Pipe->Fdi.TuSize = HDGFX_GET(Value, M_N_TU_SIZE) + 1;
    Pipe->Fdi.DataM = HDGFX_GET(Value, M_N_VALUE);
    Pipe->Fdi.DataN = HDGFX_GET(IlkRead(Mmio, HDGFX_REG(PIPE_DATA_N1, Index)), M_N_VALUE);
    Pipe->Fdi.LinkM = HDGFX_GET(IlkRead(Mmio, HDGFX_REG(PIPE_LINK_M1, Index)), M_N_VALUE);
    Pipe->Fdi.LinkN = HDGFX_GET(IlkRead(Mmio, HDGFX_REG(PIPE_LINK_N1, Index)), M_N_VALUE);

    Value = IlkRead(Mmio, HDGFX_REG(FDI_TX_CTL, Index));
    Pipe->FdiTx.Enabled = HDGFX_GET(Value, FDI_TX_ENABLE) != 0;
    Pipe->FdiTx.Training = HDGFX_GET(Value, FDI_TX_TRAINING);
    Pipe->FdiTx.Lanes = IlkFdiLanes(HDGFX_GET(Value, FDI_TX_WIDTH));

    Pipe->PanelFitter.Enabled = HDGFX_GET(IlkRead(Mmio, HDGFX_REG(PF_CTL_1, Index)), PF_ENABLE) != 0;
    Value = IlkRead(Mmio, HDGFX_REG(PF_WIN_POS, Index));
    Pipe->PanelFitter.X = HDGFX_GET(Value, PF_X);
    Pipe->PanelFitter.Y = HDGFX_GET(Value, PF_Y);
    Value = IlkRead(Mmio, HDGFX_REG(PF_WIN_SZ, Index));
    Pipe->PanelFitter.Width = HDGFX_GET(Value, PF_X);
    Pipe->PanelFitter.Height = HDGFX_GET(Value, PF_Y);

    Value = IlkRead(Mmio, HDGFX_REG(DSP_CNTR, Index));
    Pipe->Primary.Enabled = HDGFX_GET(Value, DSP_ENABLE) != 0;
    Pipe->Primary.Format = HDGFX_GET(Value, DSP_FORMAT);
    Pipe->Primary.XTiled = HDGFX_GET(Value, DSP_TILED) != 0;
    Pipe->Primary.Stride = HDGFX_GET(IlkRead(Mmio, HDGFX_REG(DSP_STRIDE, Index)), DSP_STRIDE) << 6;
    Pipe->Primary.Surface = HDGFX_GET(IlkRead(Mmio, HDGFX_REG(DSP_SURF, Index)), DSP_SURFACE) << 12;
    Pipe->Primary.LinearOffset = IlkRead(Mmio, HDGFX_REG(DSP_LINOFF, Index));
    Value = IlkRead(Mmio, HDGFX_REG(DSP_TILEOFF, Index));
    Pipe->Primary.TileX = HDGFX_GET(Value, DSP_TILE_X);
    Pipe->Primary.TileY = HDGFX_GET(Value, DSP_TILE_Y);
}

static void
IlkReadTranscoder(
    const HDGFX_MMIO *Mmio,
    uint32_t Index,
    HDGFX_TRANSCODER *Transcoder)
{
    uint32_t Value;

    Value = IlkRead(Mmio, HDGFX_REG(TRANS_CONF, Index));
    Transcoder->Enabled = HDGFX_GET(Value, TRANS_CONF_ENABLE) != 0;
    Transcoder->Active = HDGFX_GET(Value, TRANS_CONF_STATE) != 0;
    Transcoder->InterlaceMode = HDGFX_GET(Value, TRANS_CONF_INTERLACE);
    Transcoder->Dpll = Index;

    IlkReadTimings(Mmio,
                   HDGFX_REG(TRANS_HTOTAL, Index),
                   HDGFX_REG(TRANS_HBLANK, Index),
                   HDGFX_REG(TRANS_HSYNC, Index),
                   HDGFX_REG(TRANS_VTOTAL, Index),
                   HDGFX_REG(TRANS_VBLANK, Index),
                   HDGFX_REG(TRANS_VSYNC, Index),
                   &Transcoder->Timings);

    Value = IlkRead(Mmio, HDGFX_REG(FDI_RX_CTL, Index));
    Transcoder->FdiRx.Enabled = HDGFX_GET(Value, FDI_RX_ENABLE) != 0;
    Transcoder->FdiRx.PllEnabled = HDGFX_GET(Value, FDI_RX_PLL_ENABLE) != 0;
    Transcoder->FdiRx.Training = HDGFX_GET(Value, FDI_RX_TRAINING);
    Transcoder->FdiRx.Lanes = IlkFdiLanes(HDGFX_GET(Value, FDI_RX_WIDTH));
    Transcoder->FdiRx.Bpc = IlkBpc(HDGFX_GET(Value, FDI_RX_BPC));
}

static void
IlkReadDpll(
    const HDGFX_MMIO *Mmio,
    uint32_t Index,
    HDGFX_DPLL *Dpll)
{
    uint32_t Control;
    uint32_t Divisors;
    uint32_t P2;
    uint64_t Numerator;
    uint64_t Denominator;

    Control = IlkRead(Mmio, HDGFX_REG(PCH_DPLL, Index));
    Divisors = IlkRead(Mmio, HDGFX_REG(PCH_FP0, Index));

    Dpll->Enabled = HDGFX_GET(Control, DPLL_VCO_ENABLE) != 0;
    switch (HDGFX_GET(Control, DPLL_MODE))
    {
        case 1: Dpll->Mode = HdgfxDpllModeNonLvds; break;
        case 2: Dpll->Mode = HdgfxDpllModeLvds; break;
        default: Dpll->Mode = HdgfxDpllModeInvalid; break;
    }

    P2 = HDGFX_GET(Control, DPLL_P2);
    if (Dpll->Mode == HdgfxDpllModeLvds)
        Dpll->P2 = P2 == 0 ? 14 : P2 == 1 ? 7 : 0;
    else
        Dpll->P2 = P2 == 0 ? 10 : 0;

    Dpll->P1 = IlkP1(HDGFX_GET(Control, DPLL_P1_FP0));
    Dpll->Reference = HDGFX_GET(Control, DPLL_REFERENCE);
    Dpll->ReferenceKHz = (Dpll->Reference == 0 || Dpll->Reference == 1 || Dpll->Reference == 3) ?
                         ILK_DPLL_REFERENCE_KHZ : 0;
    Dpll->N = HDGFX_GET(Divisors, FP_N) + 2;
    Dpll->M1 = HDGFX_GET(Divisors, FP_M1) + 2;
    Dpll->M2 = HDGFX_GET(Divisors, FP_M2) + 2;

    Numerator = (uint64_t)Dpll->ReferenceKHz * (5 * Dpll->M1 + Dpll->M2);
    Dpll->VcoKHz = (uint32_t)((Numerator + Dpll->N / 2) / Dpll->N);
    Denominator = (uint64_t)Dpll->N * Dpll->P1 * Dpll->P2;
    Dpll->DotKHz = Denominator ? (uint32_t)((Numerator + Denominator / 2) / Denominator) : 0;
}

static void
IlkReadPanel(
    const HDGFX_MMIO *Mmio,
    HDGFX_DISPLAY_STATE *State)
{
    HDGFX_PANEL_POWER *Power = &State->PanelPower;
    HDGFX_BACKLIGHT *Backlight = &State->Backlight;
    HDGFX_LVDS *Lvds = &State->Lvds;
    uint32_t Value;

    Value = IlkRead(Mmio, HDGFX_REG(PCH_LVDS, 0));
    Lvds->Enabled = HDGFX_GET(Value, LVDS_ENABLE) != 0;
    Lvds->Detected = HDGFX_GET(Value, LVDS_DETECTED) != 0;
    Lvds->Transcoder = HDGFX_GET(Value, LVDS_TRANSCODER);
    Lvds->HSyncActiveLow = HDGFX_GET(Value, LVDS_HSYNC_INVERT) != 0;
    Lvds->VSyncActiveLow = HDGFX_GET(Value, LVDS_VSYNC_INVERT) != 0;
    Lvds->DualChannel = HDGFX_GET(Value, LVDS_B_CONTROL) == 3;
    Lvds->Bpc = HDGFX_GET(Value, LVDS_A3_CONTROL) == 3 ? 8 : 6;

    Value = IlkRead(Mmio, HDGFX_REG(PCH_ADPA, 0));
    State->Crt.Enabled = HDGFX_GET(Value, ADPA_ENABLE) != 0;
    State->Crt.Transcoder = HDGFX_GET(Value, ADPA_TRANSCODER);
    State->Crt.HSyncActiveHigh = HDGFX_GET(Value, ADPA_HSYNC_HIGH) != 0;
    State->Crt.VSyncActiveHigh = HDGFX_GET(Value, ADPA_VSYNC_HIGH) != 0;

    Value = IlkRead(Mmio, HDGFX_REG(PCH_PP_STATUS, 0));
    Power->On = HDGFX_GET(Value, PP_STATUS_ON) != 0;
    Power->Ready = HDGFX_GET(Value, PP_STATUS_READY) != 0;
    Power->Sequence = HDGFX_GET(Value, PP_STATUS_SEQUENCE);
    Power->CycleDelayActive = HDGFX_GET(Value, PP_STATUS_CYCLE_ACTIVE) != 0;
    Value = IlkRead(Mmio, HDGFX_REG(PCH_PP_CONTROL, 0));
    Power->TargetOn = HDGFX_GET(Value, PP_CONTROL_TARGET_ON) != 0;
    Power->BacklightEnabled = HDGFX_GET(Value, PP_CONTROL_BACKLIGHT) != 0;
    Power->PowerDownOnReset = HDGFX_GET(Value, PP_CONTROL_RESET) != 0;
    Value = IlkRead(Mmio, HDGFX_REG(PCH_PP_ON_DELAYS, 0));
    Power->PowerUpUs = HDGFX_GET(Value, PP_DELAY_HIGH) * 100;
    Power->BacklightOnUs = HDGFX_GET(Value, PP_DELAY_LOW) * 100;
    Value = IlkRead(Mmio, HDGFX_REG(PCH_PP_OFF_DELAYS, 0));
    Power->PowerDownUs = HDGFX_GET(Value, PP_DELAY_HIGH) * 100;
    Power->BacklightOffUs = HDGFX_GET(Value, PP_DELAY_LOW) * 100;
    Value = IlkRead(Mmio, HDGFX_REG(PCH_PP_DIVISOR, 0));
    Power->ReferenceDivider = HDGFX_GET(Value, PP_DIVISOR_REFERENCE);
    Power->PowerCycleUs = HDGFX_GET(Value, PP_DIVISOR_CYCLE) * 100000;

    Value = IlkRead(Mmio, HDGFX_REG(BLC_PWM_CPU_CTL2, 0));
    Backlight->CpuEnabled = HDGFX_GET(Value, BLC_CPU_ENABLE) != 0;
    Backlight->CpuPipe = HDGFX_GET(Value, BLC_CPU_PIPE);
    Backlight->Duty = HDGFX_GET(IlkRead(Mmio, HDGFX_REG(BLC_PWM_CPU_CTL, 0)), BLC_CPU_DUTY);
    Value = IlkRead(Mmio, HDGFX_REG(BLC_PWM_PCH_CTL1, 0));
    Backlight->PchEnabled = HDGFX_GET(Value, BLC_PCH_ENABLE) != 0;
    Backlight->ActiveLow = HDGFX_GET(Value, BLC_PCH_ACTIVE_LOW) != 0;
    Backlight->Period = HDGFX_GET(IlkRead(Mmio, HDGFX_REG(BLC_PWM_PCH_CTL2, 0)), BLC_PCH_PERIOD);
    Backlight->FrequencyHz = Backlight->Period ?
        (uint32_t)(((uint64_t)State->RawClockKHz * 1000 + Backlight->Period * 64) / ((uint64_t)Backlight->Period * 128)) : 0;
}

void
HdgfxIlkReadDisplayState(
    const HDGFX_MMIO *Mmio,
    HDGFX_DISPLAY_STATE *State)
{
    uint32_t Index;
    uint32_t Value;

    memset(State, 0, sizeof(*State));
    State->PipeCount = ILK_PIPES;
    State->TranscoderCount = ILK_TRANSCODERS;
    State->DpllCount = ILK_DPLLS;
    State->RawClockKHz = HDGFX_GET(IlkRead(Mmio, HDGFX_REG(PCH_RAWCLK_FREQ, 0)), RAWCLK_MHZ) * 1000;
    State->FdiPllVcoKHz = (HDGFX_GET(IlkRead(Mmio, HDGFX_REG(FDI_PLL_BIOS_0, 0)), FDI_PLL_FB_DIVIDER) + 2) *
                          ILK_FDI_PLL_REFERENCE_KHZ;
    Value = IlkRead(Mmio, HDGFX_REG(PCH_DREF_CONTROL, 0));
    State->SscReference = HDGFX_GET(Value, DREF_SSC_SOURCE) == ILK_DREF_INTEGRATED;
    State->NonSpreadReference = HDGFX_GET(Value, DREF_NONSPREAD_SOURCE) == ILK_DREF_INTEGRATED;

    for (Index = 0; Index < ILK_PIPES; Index++)
        IlkReadPipe(Mmio, Index, &State->Pipes[Index]);
    for (Index = 0; Index < ILK_TRANSCODERS; Index++)
        IlkReadTranscoder(Mmio, Index, &State->Transcoders[Index]);
    for (Index = 0; Index < ILK_DPLLS; Index++)
        IlkReadDpll(Mmio, Index, &State->Dplls[Index]);
    IlkReadPanel(Mmio, State);
}

uint32_t
HdgfxIlkCursorControl(
    bool Visible)
{
    return Visible ? HDGFX_SET(CUR_MODE_SELECT, 1) | HDGFX_SET(CUR_MODE, 7) : 0;
}

uint32_t
HdgfxIlkCursorPosition(
    int32_t X,
    int32_t Y)
{
    uint32_t Magnitude;
    uint32_t Value = 0;

    if (X < -HDGFX_CURSOR_POSITION_LIMIT || X > HDGFX_CURSOR_POSITION_LIMIT ||
        Y < -HDGFX_CURSOR_POSITION_LIMIT || Y > HDGFX_CURSOR_POSITION_LIMIT)
    {
        return 0;
    }
    Magnitude = (uint32_t)(X < 0 ? -X : X);
    Value |= HDGFX_SET(CUR_POS_X_SIGN, X < 0) | HDGFX_SET(CUR_POS_X, Magnitude);
    Magnitude = (uint32_t)(Y < 0 ? -Y : Y);
    Value |= HDGFX_SET(CUR_POS_Y_SIGN, Y < 0) | HDGFX_SET(CUR_POS_Y, Magnitude);
    return Value;
}
