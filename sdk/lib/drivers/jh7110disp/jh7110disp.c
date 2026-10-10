/*
 * PROJECT:     LiberNT StarFive JH7110 display library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     JH7110 DC8200 display controller and HDMI transmitter bring-up shared by the loader and the display miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "jh7110disp.h"

#define JH_POLL_STEP_US 10

const JH7110DISP_MODE Jh7110DispMode1080p60 =
{
    1920, 1080, 148500000, 2200, 2008, 2052, 1125, 1084, 1089, TRUE, TRUE,
    1, 99, 1, 1, 1, 1, 2, 2, 2,
    1, 20, 1,
    0x02, 0x22
};

ULONG
Jh7110DispRead(
    _In_ PUCHAR Base,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Base + Offset));
}

VOID
Jh7110DispFlushCache(
    _In_ PUCHAR Ccache,
    _In_ ULONGLONG Address,
    _In_ SIZE_T Length)
{
    ULONGLONG Line = Address & ~(ULONGLONG)(JH_CCACHE_LINE - 1);
    ULONGLONG End = Address + Length;

    for (; Line < End; Line += JH_CCACHE_LINE)
        WRITE_REGISTER_ULONG64((volatile ULONG64 *)(Ccache + JH_CCACHE_FLUSH64), Line);
}

static VOID
JhWrite(_In_ PUCHAR Base, _In_ ULONG Offset, _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Base + Offset), Value);
}

static ULONG
JhHdmiRead(_In_ PJH7110DISP Display, _In_ ULONG Register)
{
    return Jh7110DispRead(Display->Hdmi, Register * sizeof(ULONG)) & 0xFF;
}

static VOID
JhHdmiWrite(_In_ PJH7110DISP Display, _In_ ULONG Register, _In_ ULONG Value)
{
    JhWrite(Display->Hdmi, Register * sizeof(ULONG), Value & 0xFF);
}

static BOOLEAN
JhPoll(_In_ PJH7110DISP Display, _In_ PUCHAR Base, _In_ ULONG Offset, _In_ ULONG Mask, _In_ ULONG Value,
       _In_ ULONG TimeoutUs)
{
    ULONG Elapsed;

    for (Elapsed = 0; Elapsed <= TimeoutUs; Elapsed += JH_POLL_STEP_US)
    {
        if ((Jh7110DispRead(Base, Offset) & Mask) == Value)
            return TRUE;
        Display->Stall(JH_POLL_STEP_US);
    }
    return FALSE;
}

static VOID
JhClockSet(_In_ PUCHAR Crg, _In_ ULONG Id, _In_ ULONG Clear, _In_ ULONG Set)
{
    ULONG Offset = Id * sizeof(ULONG);

    JhWrite(Crg, Offset, (Jh7110DispRead(Crg, Offset) & ~Clear) | Set);
}

static BOOLEAN
JhClockEnabled(_In_ PUCHAR Crg, _In_ ULONG Id)
{
    return (Jh7110DispRead(Crg, Id * sizeof(ULONG)) & JH_CLK_ENABLE) != 0;
}

static BOOLEAN
JhResetRelease(_In_ PJH7110DISP Display, _In_ PUCHAR Crg, _In_ ULONG Assert, _In_ ULONG Status, _In_ ULONG Id)
{
    ULONG Offset = (Id / 32) * sizeof(ULONG);
    ULONG Mask = 1UL << (Id % 32);

    JhWrite(Crg, Assert + Offset, Jh7110DispRead(Crg, Assert + Offset) & ~Mask);
    return JhPoll(Display, Crg, Status + Offset, Mask, Mask, 1000);
}

static BOOLEAN
JhResetReleased(_In_ PUCHAR Crg, _In_ ULONG Status, _In_ ULONG Id)
{
    return (Jh7110DispRead(Crg, Status + (Id / 32) * sizeof(ULONG)) & (1UL << (Id % 32))) != 0;
}

NTSTATUS
Jh7110DispPowerOn(
    _In_ PJH7110DISP Display)
{
    PUCHAR Crg = Display->VoutCrg;

    if (!(Jh7110DispRead(Display->Pmu, JH_PMU_CURR_POWER_MODE) & JH_PMU_DOMAIN_VOUT))
    {
        JhWrite(Display->Pmu, JH_PMU_SW_TURN_ON_POWER, JH_PMU_DOMAIN_VOUT);
        JhWrite(Display->Pmu, JH_PMU_SW_ENCOURAGE, JH_PMU_ENCOURAGE_RESET);
        JhWrite(Display->Pmu, JH_PMU_SW_ENCOURAGE, JH_PMU_ENCOURAGE_ON_LO);
        JhWrite(Display->Pmu, JH_PMU_SW_ENCOURAGE, JH_PMU_ENCOURAGE_ON_HI);
        if (!JhPoll(Display, Display->Pmu, JH_PMU_CURR_POWER_MODE, JH_PMU_DOMAIN_VOUT, JH_PMU_DOMAIN_VOUT, 1000))
            return STATUS_IO_TIMEOUT;
    }

    JhClockSet(Display->SysCrg, JH_SYSCLK_VOUT_SRC, 0, JH_CLK_ENABLE);
    JhClockSet(Display->SysCrg, JH_SYSCLK_NOC_BUS_DISP_AXI, 0, JH_CLK_ENABLE);
    JhClockSet(Display->SysCrg, JH_SYSCLK_VOUT_TOP_AHB, 0, JH_CLK_ENABLE);
    JhClockSet(Display->SysCrg, JH_SYSCLK_VOUT_TOP_AXI, 0, JH_CLK_ENABLE);
    JhClockSet(Display->SysCrg, JH_SYSCLK_VOUT_TOP_HDMITX0_MCLK, 0, JH_CLK_ENABLE);
    if (!JhResetRelease(Display, Display->SysCrg, JH_SYSCRG_RESET_ASSERT, JH_SYSCRG_RESET_STATUS,
                        JH_SYSRST_NOC_BUS_DISP_AXI) ||
        !JhResetRelease(Display, Display->SysCrg, JH_SYSCRG_RESET_ASSERT, JH_SYSCRG_RESET_STATUS,
                        JH_SYSRST_VOUT_TOP_SRC))
    {
        return STATUS_IO_TIMEOUT;
    }

    JhClockSet(Crg, JH_VOUTCLK_DC8200_PIX, JH_CLK_DIV_MASK, JH_VOUT_SRC_HZ / Display->Mode->PixelClock);
    JhClockSet(Crg, JH_VOUTCLK_DC8200_PIX0, JH_CLK_MUX_MASK,
               JH_CLK_ENABLE | (JH_PIX_MUX_DC8200_PIX << JH_CLK_MUX_SHIFT));
    JhClockSet(Crg, JH_VOUTCLK_DC8200_PIX1, JH_CLK_MUX_MASK,
               JH_CLK_ENABLE | (JH_PIX_MUX_DC8200_PIX << JH_CLK_MUX_SHIFT));
    JhClockSet(Crg, JH_VOUTCLK_DC8200_AXI, 0, JH_CLK_ENABLE);
    JhClockSet(Crg, JH_VOUTCLK_DC8200_CORE, 0, JH_CLK_ENABLE);
    JhClockSet(Crg, JH_VOUTCLK_DC8200_AHB, 0, JH_CLK_ENABLE);
    JhClockSet(Crg, JH_VOUTCLK_HDMI_TX_SYS, 0, JH_CLK_ENABLE);
    JhClockSet(Crg, JH_VOUTCLK_HDMI_TX_MCLK, 0, JH_CLK_ENABLE);
    JhClockSet(Crg, JH_VOUTCLK_HDMI_TX_BCLK, 0, JH_CLK_ENABLE);
    if (!JhResetRelease(Display, Crg, JH_VOUTCRG_RESET_ASSERT, JH_VOUTCRG_RESET_STATUS, JH_VOUTRST_DC8200_AXI) ||
        !JhResetRelease(Display, Crg, JH_VOUTCRG_RESET_ASSERT, JH_VOUTCRG_RESET_STATUS, JH_VOUTRST_DC8200_AHB) ||
        !JhResetRelease(Display, Crg, JH_VOUTCRG_RESET_ASSERT, JH_VOUTCRG_RESET_STATUS, JH_VOUTRST_DC8200_CORE) ||
        !JhResetRelease(Display, Crg, JH_VOUTCRG_RESET_ASSERT, JH_VOUTCRG_RESET_STATUS, JH_VOUTRST_HDMI_TX))
    {
        return STATUS_IO_TIMEOUT;
    }
    return STATUS_SUCCESS;
}

static BOOLEAN
JhHdmiWaitPllLock(_In_ PJH7110DISP Display)
{
    return JhPoll(Display, Display->Hdmi, JH_HDMI_PRE_PLL_LOCK * sizeof(ULONG), JH_HDMI_PLL_LOCKED,
                  JH_HDMI_PLL_LOCKED, JH_HDMI_PLL_LOCK_TIMEOUT_US) &&
           JhPoll(Display, Display->Hdmi, JH_HDMI_POST_PLL_LOCK * sizeof(ULONG), JH_HDMI_PLL_LOCKED,
                  JH_HDMI_PLL_LOCKED, JH_HDMI_PLL_LOCK_TIMEOUT_US);
}

NTSTATUS
Jh7110DispStartHdmi(
    _In_ PJH7110DISP Display)
{
    const JH7110DISP_MODE *Mode = Display->Mode;
    ULONG SystemControl = JH_HDMI_SYS_NOT_RST_ANALOG | JH_HDMI_SYS_NOT_RST_DIGITAL | JH_HDMI_SYS_INT_POL_HIGH;
    ULONG Value;

    JhHdmiWrite(Display, JH_HDMI_BANDGAP, JhHdmiRead(Display, JH_HDMI_BANDGAP) | JH_HDMI_BANDGAP_ENABLE);
    JhHdmiWrite(Display, JH_HDMI_TMDS_CHANNELS, JH_HDMI_TMDS_CHANNELS_ALL);
    JhHdmiWrite(Display, JH_HDMI_PRE_PLL_CTRL, JhHdmiRead(Display, JH_HDMI_PRE_PLL_CTRL) & ~JH_HDMI_PLL_POWER_DOWN);
    JhHdmiWrite(Display, JH_HDMI_POST_PLL_CTRL, JhHdmiRead(Display, JH_HDMI_POST_PLL_CTRL) & ~JH_HDMI_PLL_POWER_DOWN);
    if (!JhHdmiWaitPllLock(Display))
        return STATUS_IO_TIMEOUT;
    JhHdmiWrite(Display, JH_HDMI_LDO, JH_HDMI_LDO_ENABLE);
    JhHdmiWrite(Display, JH_HDMI_SERIALIZER, JH_HDMI_SERIALIZER_ENABLE);
    JhHdmiWrite(Display, JH_HDMI_SYS_CTRL, SystemControl | JH_HDMI_SYS_POWER_OFF);

    JhHdmiWrite(Display, JH_HDMI_PRE_PLL_CTRL, JH_HDMI_PLL_POWER_DOWN);
    JhHdmiWrite(Display, JH_HDMI_POST_PLL_CTRL,
                JH_HDMI_POST_PLL_DIV_ENABLE | JH_HDMI_POST_PLL_REF_TMDS | JH_HDMI_PLL_POWER_DOWN);
    JhHdmiWrite(Display, JH_HDMI_PRE_PLL_PRE_DIV, Mode->PrePllPreDiv);
    JhHdmiWrite(Display, JH_HDMI_PRE_PLL_FB_DIV_HIGH, JH_HDMI_PRE_PLL_INTEGER | (Mode->PrePllFbDiv >> 8));
    JhHdmiWrite(Display, JH_HDMI_PRE_PLL_FB_DIV_LOW, Mode->PrePllFbDiv);
    JhHdmiWrite(Display, JH_HDMI_PRE_PLL_TMDS_DIV, (Mode->TmdsDivA << 4) | (Mode->TmdsDivB << 2) | Mode->TmdsDivC);
    JhHdmiWrite(Display, JH_HDMI_PRE_PLL_PCLK_DIV_AB, (Mode->PclkDivB << 5) | Mode->PclkDivA);
    JhHdmiWrite(Display, JH_HDMI_PRE_PLL_PCLK_DIV_CD, (Mode->PclkDivC << 5) | Mode->PclkDivD);
    JhHdmiWrite(Display, JH_HDMI_POST_PLL_PRE_DIV, Mode->PostPllPreDiv);
    JhHdmiWrite(Display, JH_HDMI_POST_PLL_FB_DIV, Mode->PostPllFbDiv);
    JhHdmiWrite(Display, JH_HDMI_POST_PLL_POST_DIV, Mode->PostPllPostDiv);
    JhHdmiWrite(Display, JH_HDMI_POST_PLL_CTRL, JH_HDMI_POST_PLL_DIV_ENABLE | JH_HDMI_POST_PLL_REF_TMDS);
    JhHdmiWrite(Display, JH_HDMI_PRE_PLL_CTRL, 0);
    if (!JhHdmiWaitPllLock(Display))
        return STATUS_IO_TIMEOUT;
    Display->Sleep(10000);

    JhHdmiWrite(Display, JH_HDMI_DRIVE_STRENGTH, Mode->DriveStrength);
    JhHdmiWrite(Display, JH_HDMI_PRE_EMPHASIS, Mode->PreEmphasis);
    Value = Mode->HTotal;
    JhHdmiWrite(Display, JH_HDMI_EXT_HTOTAL_L, Value);
    JhHdmiWrite(Display, JH_HDMI_EXT_HTOTAL_H, Value >> 8);
    Value = Mode->HTotal - Mode->Width;
    JhHdmiWrite(Display, JH_HDMI_EXT_HBLANK_L, Value);
    JhHdmiWrite(Display, JH_HDMI_EXT_HBLANK_H, Value >> 8);
    Value = Mode->HTotal - Mode->HSyncStart;
    JhHdmiWrite(Display, JH_HDMI_EXT_HDELAY_L, Value);
    JhHdmiWrite(Display, JH_HDMI_EXT_HDELAY_H, Value >> 8);
    Value = Mode->HSyncEnd - Mode->HSyncStart;
    JhHdmiWrite(Display, JH_HDMI_EXT_HDURATION_L, Value);
    JhHdmiWrite(Display, JH_HDMI_EXT_HDURATION_H, Value >> 8);
    Value = Mode->VTotal;
    JhHdmiWrite(Display, JH_HDMI_EXT_VTOTAL_L, Value);
    JhHdmiWrite(Display, JH_HDMI_EXT_VTOTAL_H, Value >> 8);
    JhHdmiWrite(Display, JH_HDMI_EXT_VBLANK, Mode->VTotal - Mode->Height);
    JhHdmiWrite(Display, JH_HDMI_EXT_VDELAY, Mode->VTotal - Mode->VSyncStart);
    JhHdmiWrite(Display, JH_HDMI_EXT_VDURATION, Mode->VSyncEnd - Mode->VSyncStart);
    JhHdmiWrite(Display, JH_HDMI_VIDEO_TIMING_CTL,
                JH_HDMI_TIMING_EXTERNAL |
                (Mode->HSyncPositive ? JH_HDMI_TIMING_HSYNC_POSITIVE : 0) |
                (Mode->VSyncPositive ? JH_HDMI_TIMING_VSYNC_POSITIVE : 0));
    JhHdmiWrite(Display, JH_HDMI_COLORBAR, JH_HDMI_COLORBAR_OFF);

    JhHdmiWrite(Display, JH_HDMI_SYS_CTRL, SystemControl);
    JhHdmiWrite(Display, JH_HDMI_TMDS_DRIVER, JH_HDMI_TMDS_DRIVER_ENABLE);
    Display->Sleep(50000);
    JhHdmiWrite(Display, JH_HDMI_PHY_SYNC, 0);
    JhHdmiWrite(Display, JH_HDMI_PHY_SYNC, 1);
    return STATUS_SUCCESS;
}

VOID
Jh7110DispStartController(
    _In_ PJH7110DISP Display,
    _In_ ULONG FrameBuffer,
    _In_ ULONG Pitch)
{
    const JH7110DISP_MODE *Mode = Display->Mode;
    PUCHAR Dc = Display->Dc;
    ULONG Size = Mode->Width | (Mode->Height << JH_DC_POSITION_Y_SHIFT);

    JhClockSet(Display->VoutCrg, JH_VOUTCLK_DOM_VOUT_TOP_LCD, JH_CLK_MUX_MASK,
               JH_CLK_ENABLE | (JH_LCD_MUX_PIX0 << JH_CLK_MUX_SHIFT));

    JhWrite(Dc, JH_DC_CLK_GATING, 0);
    JhWrite(Dc, JH_DC_DPI_CONFIG, JH_DC_DPI_RGB888);
    JhWrite(Dc, JH_DC_DP_CONFIG, JH_DC_DP_RGB888);
    JhWrite(Dc, JH_DC_PANEL_START, 0);
    JhWrite(Dc, JH_DC_DISPLAY_H, Mode->Width | (Mode->HTotal << JH_DC_TOTAL_SHIFT));
    JhWrite(Dc, JH_DC_DISPLAY_H_SYNC,
            Mode->HSyncStart | (Mode->HSyncEnd << JH_DC_SYNC_END_SHIFT) | JH_DC_SYNC_ENABLE |
            (Mode->HSyncPositive ? 0 : JH_DC_SYNC_NEGATIVE));
    JhWrite(Dc, JH_DC_DISPLAY_V, Mode->Height | (Mode->VTotal << JH_DC_TOTAL_SHIFT));
    JhWrite(Dc, JH_DC_DISPLAY_V_SYNC,
            Mode->VSyncStart | (Mode->VSyncEnd << JH_DC_SYNC_END_SHIFT) | JH_DC_SYNC_ENABLE |
            (Mode->VSyncPositive ? 0 : JH_DC_SYNC_NEGATIVE));
    JhWrite(Dc, JH_DC_FB_BG_COLOR, 0);
    JhWrite(Dc, JH_DC_DITHER_CONFIG, 0);
    JhWrite(Dc, JH_DC_PANEL_CONFIG, JH_DC_PANEL_CONFIG_DEFAULT | JH_DC_PANEL_ENABLE);
    JhWrite(Dc, JH_DC_PANEL_START, JH_DC_PANEL_START_0);

    JhWrite(Dc, JH_DC_PANEL_CONFIG_EX, JH_DC_PANEL_EX_HOLD);
    JhWrite(Dc, JH_DC_FB_ADDRESS, FrameBuffer);
    JhWrite(Dc, JH_DC_FB_STRIDE, Pitch);
    JhWrite(Dc, JH_DC_FB_SIZE, Size);
    JhWrite(Dc, JH_DC_FB_TOP_LEFT, 0);
    JhWrite(Dc, JH_DC_FB_BOTTOM_RIGHT, Size);
    JhWrite(Dc, JH_DC_FB_WATER_MARK, 0);
    JhWrite(Dc, JH_DC_FB_SRC_GLOBAL_COLOR, JH_DC_GLOBAL_ALPHA_OPAQUE);
    JhWrite(Dc, JH_DC_FB_DST_GLOBAL_COLOR, JH_DC_GLOBAL_ALPHA_OPAQUE);
    JhWrite(Dc, JH_DC_FB_BLEND_CONFIG, JH_DC_BLEND_PIXEL_NONE);
    JhWrite(Dc, JH_DC_FB_CONFIG, JH_DC_FB_FORMAT_X8R8G8B8 << JH_DC_FB_FORMAT_SHIFT);
    JhWrite(Dc, JH_DC_FB_CONFIG_EX, JH_DC_FB_EX_ENABLE);
    JhWrite(Dc, JH_DC_FB_CONFIG_EX, JH_DC_FB_EX_ENABLE | JH_DC_FB_EX_SHADOW);
    JhWrite(Dc, JH_DC_PANEL_CONFIG_EX, 0);
}

VOID
Jh7110DispSetFrameBuffer(
    _In_ PJH7110DISP Display,
    _In_ ULONG FrameBuffer,
    _In_ ULONG Pitch)
{
    PUCHAR Dc = Display->Dc;

    JhWrite(Dc, JH_DC_PANEL_CONFIG_EX, JH_DC_PANEL_EX_HOLD);
    JhWrite(Dc, JH_DC_FB_ADDRESS, FrameBuffer);
    JhWrite(Dc, JH_DC_FB_STRIDE, Pitch);
    JhWrite(Dc, JH_DC_FB_CONFIG_EX, JH_DC_FB_EX_ENABLE | JH_DC_FB_EX_SHADOW);
    JhWrite(Dc, JH_DC_PANEL_CONFIG_EX, 0);
}

NTSTATUS
Jh7110DispStart(
    _In_ PJH7110DISP Display,
    _In_ ULONG FrameBuffer,
    _In_ ULONG Pitch)
{
    NTSTATUS Status;

    Status = Jh7110DispPowerOn(Display);
    if (NT_SUCCESS(Status))
        Status = Jh7110DispStartHdmi(Display);
    if (!NT_SUCCESS(Status))
        return Status;
    Jh7110DispStartController(Display, FrameBuffer, Pitch);
    return STATUS_SUCCESS;
}

VOID
Jh7110DispStop(
    _In_ PJH7110DISP Display)
{
    JhWrite(Display->Dc, JH_DC_PANEL_CONFIG_EX, JH_DC_PANEL_EX_HOLD);
    JhWrite(Display->Dc, JH_DC_PANEL_START, 0);
    JhWrite(Display->Dc, JH_DC_PANEL_CONFIG, JH_DC_PANEL_CONFIG_DEFAULT);
    JhWrite(Display->Dc, JH_DC_FB_CONFIG_EX, 0);
    JhWrite(Display->Dc, JH_DC_PANEL_CONFIG_EX, 0);
    JhHdmiWrite(Display, JH_HDMI_TMDS_DRIVER, 0);
    JhHdmiWrite(Display, JH_HDMI_SYS_CTRL,
                JH_HDMI_SYS_NOT_RST_ANALOG | JH_HDMI_SYS_NOT_RST_DIGITAL | JH_HDMI_SYS_POWER_OFF |
                JH_HDMI_SYS_INT_POL_HIGH);
    Display->Sleep(40000);
}

BOOLEAN
Jh7110DispIsScanningOut(
    _In_ PJH7110DISP Display)
{
    if (!(Jh7110DispRead(Display->Pmu, JH_PMU_CURR_POWER_MODE) & JH_PMU_DOMAIN_VOUT) ||
        !JhClockEnabled(Display->SysCrg, JH_SYSCLK_VOUT_TOP_AHB) ||
        !JhClockEnabled(Display->SysCrg, JH_SYSCLK_VOUT_TOP_AXI) ||
        !JhResetReleased(Display->SysCrg, JH_SYSCRG_RESET_STATUS, JH_SYSRST_VOUT_TOP_SRC) ||
        !JhClockEnabled(Display->VoutCrg, JH_VOUTCLK_DC8200_AHB) ||
        !JhClockEnabled(Display->VoutCrg, JH_VOUTCLK_HDMI_TX_SYS) ||
        !JhResetReleased(Display->VoutCrg, JH_VOUTCRG_RESET_STATUS, JH_VOUTRST_DC8200_AHB) ||
        !JhResetReleased(Display->VoutCrg, JH_VOUTCRG_RESET_STATUS, JH_VOUTRST_HDMI_TX))
    {
        return FALSE;
    }
    return (Jh7110DispRead(Display->Dc, JH_DC_PANEL_CONFIG) & JH_DC_PANEL_ENABLE) &&
           (Jh7110DispRead(Display->Dc, JH_DC_FB_CONFIG_EX) & JH_DC_FB_EX_ENABLE) &&
           JhHdmiRead(Display, JH_HDMI_TMDS_DRIVER) == JH_HDMI_TMDS_DRIVER_ENABLE;
}

BOOLEAN
Jh7110DispHotPlug(
    _In_ PJH7110DISP Display)
{
    return (JhHdmiRead(Display, JH_HDMI_STATUS) & JH_HDMI_STATUS_HOTPLUG) != 0;
}
