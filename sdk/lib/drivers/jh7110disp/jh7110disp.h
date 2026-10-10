/*
 * PROJECT:     LiberNT StarFive JH7110 display library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     JH7110 DC8200 display controller and HDMI transmitter bring-up shared by the loader and the display miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define JH_CCACHE_BASE                      0x02010000ULL
#define JH_CCACHE_SIZE                      0x1000
#define JH_CCACHE_FLUSH64                   0x200
#define JH_CCACHE_LINE                      64UL

#define JH_PMU_BASE                         0x17030000ULL
#define JH_PMU_SIZE                         0x100
#define JH_PMU_SW_TURN_ON_POWER             0x0C
#define JH_PMU_SW_ENCOURAGE                 0x44
#define JH_PMU_CURR_POWER_MODE              0x80
#define JH_PMU_ENCOURAGE_RESET              0xFFUL
#define JH_PMU_ENCOURAGE_ON_LO              0x05UL
#define JH_PMU_ENCOURAGE_ON_HI              0x50UL
#define JH_PMU_DOMAIN_VOUT                  (1UL << 4)

#define JH_SYSCRG_BASE                      0x13020000ULL
#define JH_SYSCRG_SIZE                      0x1000
#define JH_SYSCRG_RESET_ASSERT              0x2F8
#define JH_SYSCRG_RESET_STATUS              0x308
#define JH_SYSCLK_VOUT_SRC                  58
#define JH_SYSCLK_NOC_BUS_DISP_AXI          60
#define JH_SYSCLK_VOUT_TOP_AHB              61
#define JH_SYSCLK_VOUT_TOP_AXI              62
#define JH_SYSCLK_VOUT_TOP_HDMITX0_MCLK     63
#define JH_SYSRST_NOC_BUS_DISP_AXI          26
#define JH_SYSRST_VOUT_TOP_SRC              43

#define JH_VOUTCRG_BASE                     0x295C0000ULL
#define JH_VOUTCRG_SIZE                     0x100
#define JH_VOUTCRG_RESET_ASSERT             0x48
#define JH_VOUTCRG_RESET_STATUS             0x4C
#define JH_VOUTCLK_DC8200_PIX               1
#define JH_VOUTCLK_DC8200_AXI               4
#define JH_VOUTCLK_DC8200_CORE              5
#define JH_VOUTCLK_DC8200_AHB               6
#define JH_VOUTCLK_DC8200_PIX0              7
#define JH_VOUTCLK_DC8200_PIX1              8
#define JH_VOUTCLK_DOM_VOUT_TOP_LCD         9
#define JH_VOUTCLK_HDMI_TX_MCLK             15
#define JH_VOUTCLK_HDMI_TX_BCLK             16
#define JH_VOUTCLK_HDMI_TX_SYS              17
#define JH_VOUTRST_DC8200_AXI               0
#define JH_VOUTRST_DC8200_AHB               1
#define JH_VOUTRST_DC8200_CORE              2
#define JH_VOUTRST_HDMI_TX                  9
#define JH_VOUT_SRC_HZ                      1188000000UL
#define JH_PIX_MUX_DC8200_PIX               0UL
#define JH_LCD_MUX_PIX0                     0UL

#define JH_CLK_ENABLE                       (1UL << 31)
#define JH_CLK_MUX_SHIFT                    24
#define JH_CLK_MUX_MASK                     (0xFUL << JH_CLK_MUX_SHIFT)
#define JH_CLK_DIV_MASK                     0xFFFFFFUL

#define JH_DC_BASE                          0x29400000ULL
#define JH_DC_SIZE                          0x2800
#define JH_DC_HW_REVISION                   0x0024
#define JH_DC_HW_CHIP_CID                   0x0030
#define JH_DC_FB_ADDRESS                    0x1400
#define JH_DC_FB_STRIDE                     0x1408
#define JH_DC_DITHER_CONFIG                 0x1410
#define JH_DC_PANEL_CONFIG                  0x1418
#define JH_DC_DISPLAY_H                     0x1430
#define JH_DC_DISPLAY_H_SYNC                0x1438
#define JH_DC_DISPLAY_V                     0x1440
#define JH_DC_DISPLAY_V_SYNC                0x1448
#define JH_DC_DPI_CONFIG                    0x14B8
#define JH_DC_FB_CONFIG                     0x1518
#define JH_DC_FB_BG_COLOR                   0x1528
#define JH_DC_FB_SIZE                       0x1810
#define JH_DC_CLK_GATING                    0x1A28
#define JH_DC_FB_CONFIG_EX                  0x1CC0
#define JH_DC_PANEL_START                   0x1CCC
#define JH_DC_DP_CONFIG                     0x1CD0
#define JH_DC_FB_WATER_MARK                 0x1CE8
#define JH_DC_FB_TOP_LEFT                   0x24D8
#define JH_DC_FB_BOTTOM_RIGHT               0x24E0
#define JH_DC_FB_SRC_GLOBAL_COLOR           0x2500
#define JH_DC_FB_DST_GLOBAL_COLOR           0x2508
#define JH_DC_FB_BLEND_CONFIG               0x2510
#define JH_DC_PANEL_CONFIG_EX               0x2518

#define JH_DC_FB_FORMAT_SHIFT               26
#define JH_DC_FB_FORMAT_X8R8G8B8            5UL
#define JH_DC_FB_EX_SHADOW                  (1UL << 12)
#define JH_DC_FB_EX_ENABLE                  (1UL << 13)
#define JH_DC_POSITION_Y_SHIFT              15
#define JH_DC_TOTAL_SHIFT                   16
#define JH_DC_SYNC_END_SHIFT                15
#define JH_DC_SYNC_ENABLE                   (1UL << 30)
#define JH_DC_SYNC_NEGATIVE                 (1UL << 31)
#define JH_DC_PANEL_CONFIG_DEFAULT          0x111UL
#define JH_DC_PANEL_ENABLE                  (1UL << 12)
#define JH_DC_PANEL_START_0                 (1UL << 0)
#define JH_DC_PANEL_EX_HOLD                 (1UL << 0)
#define JH_DC_DPI_RGB888                    5UL
#define JH_DC_DP_RGB888                     2UL
#define JH_DC_BLEND_PIXEL_NONE              0x3548UL
#define JH_DC_GLOBAL_ALPHA_OPAQUE           0xFF000000UL
#define JH_DC_HIGHEST_ADDRESS               0xFFFFFFFFULL

#define JH_HDMI_BASE                        0x29590000ULL
#define JH_HDMI_SIZE                        0x4000
#define JH_HDMI_SYS_CTRL                    0x000
#define JH_HDMI_SYS_NOT_RST_ANALOG          (1UL << 6)
#define JH_HDMI_SYS_NOT_RST_DIGITAL         (1UL << 5)
#define JH_HDMI_SYS_POWER_OFF               (1UL << 1)
#define JH_HDMI_SYS_INT_POL_HIGH            (1UL << 0)
#define JH_HDMI_VIDEO_TIMING_CTL            0x008
#define JH_HDMI_TIMING_EXTERNAL             (1UL << 0)
#define JH_HDMI_TIMING_HSYNC_POSITIVE       (1UL << 2)
#define JH_HDMI_TIMING_VSYNC_POSITIVE       (1UL << 3)
#define JH_HDMI_EXT_HTOTAL_L                0x009
#define JH_HDMI_EXT_HTOTAL_H                0x00A
#define JH_HDMI_EXT_HBLANK_L                0x00B
#define JH_HDMI_EXT_HBLANK_H                0x00C
#define JH_HDMI_EXT_HDELAY_L                0x00D
#define JH_HDMI_EXT_HDELAY_H                0x00E
#define JH_HDMI_EXT_HDURATION_L             0x00F
#define JH_HDMI_EXT_HDURATION_H             0x010
#define JH_HDMI_EXT_VTOTAL_L                0x011
#define JH_HDMI_EXT_VTOTAL_H                0x012
#define JH_HDMI_EXT_VBLANK                  0x013
#define JH_HDMI_EXT_VDELAY                  0x014
#define JH_HDMI_EXT_VDURATION               0x015
#define JH_HDMI_STATUS                      0x0C8
#define JH_HDMI_STATUS_HOTPLUG              (1UL << 7)
#define JH_HDMI_COLORBAR                    0x0C9
#define JH_HDMI_COLORBAR_OFF                0x10UL
#define JH_HDMI_PHY_SYNC                    0x0CE
#define JH_HDMI_PRE_PLL_CTRL                0x1A0
#define JH_HDMI_PRE_PLL_PRE_DIV             0x1A1
#define JH_HDMI_PRE_PLL_FB_DIV_HIGH         0x1A2
#define JH_HDMI_PRE_PLL_FB_DIV_LOW          0x1A3
#define JH_HDMI_PRE_PLL_TMDS_DIV            0x1A4
#define JH_HDMI_PRE_PLL_PCLK_DIV_AB         0x1A5
#define JH_HDMI_PRE_PLL_PCLK_DIV_CD         0x1A6
#define JH_HDMI_PRE_PLL_LOCK                0x1A9
#define JH_HDMI_POST_PLL_CTRL               0x1AA
#define JH_HDMI_POST_PLL_PRE_DIV            0x1AB
#define JH_HDMI_POST_PLL_FB_DIV             0x1AC
#define JH_HDMI_POST_PLL_POST_DIV           0x1AD
#define JH_HDMI_POST_PLL_LOCK               0x1AF
#define JH_HDMI_BANDGAP                     0x1B0
#define JH_HDMI_TMDS_DRIVER                 0x1B2
#define JH_HDMI_LDO                         0x1B4
#define JH_HDMI_SERIALIZER                  0x1BE
#define JH_HDMI_DRIVE_STRENGTH              0x1BF
#define JH_HDMI_PRE_EMPHASIS                0x1C0
#define JH_HDMI_TMDS_CHANNELS               0x1CC

#define JH_HDMI_PLL_POWER_DOWN              (1UL << 0)
#define JH_HDMI_PLL_LOCKED                  (1UL << 0)
#define JH_HDMI_PRE_PLL_INTEGER             0xF0UL
#define JH_HDMI_POST_PLL_DIV_ENABLE         (3UL << 2)
#define JH_HDMI_POST_PLL_REF_TMDS           (1UL << 1)
#define JH_HDMI_BANDGAP_ENABLE              (1UL << 2)
#define JH_HDMI_TMDS_DRIVER_ENABLE          0x8FUL
#define JH_HDMI_LDO_ENABLE                  0x07UL
#define JH_HDMI_SERIALIZER_ENABLE           0x71UL
#define JH_HDMI_TMDS_CHANNELS_ALL           0x0FUL
#define JH_HDMI_PLL_LOCK_TIMEOUT_US         100000UL

typedef struct _JH7110DISP_MODE
{
    ULONG Width;
    ULONG Height;
    ULONG PixelClock;
    ULONG HTotal;
    ULONG HSyncStart;
    ULONG HSyncEnd;
    ULONG VTotal;
    ULONG VSyncStart;
    ULONG VSyncEnd;
    BOOLEAN HSyncPositive;
    BOOLEAN VSyncPositive;
    UCHAR PrePllPreDiv;
    USHORT PrePllFbDiv;
    UCHAR TmdsDivA;
    UCHAR TmdsDivB;
    UCHAR TmdsDivC;
    UCHAR PclkDivA;
    UCHAR PclkDivB;
    UCHAR PclkDivC;
    UCHAR PclkDivD;
    UCHAR PostPllPreDiv;
    UCHAR PostPllFbDiv;
    UCHAR PostPllPostDiv;
    UCHAR DriveStrength;
    UCHAR PreEmphasis;
} JH7110DISP_MODE, *PJH7110DISP_MODE;

typedef VOID (*PJH7110DISP_DELAY)(_In_ ULONG Microseconds);

typedef struct _JH7110DISP
{
    PUCHAR Pmu;
    PUCHAR SysCrg;
    PUCHAR VoutCrg;
    PUCHAR Dc;
    PUCHAR Hdmi;
    const JH7110DISP_MODE *Mode;
    PJH7110DISP_DELAY Stall;
    PJH7110DISP_DELAY Sleep;
} JH7110DISP, *PJH7110DISP;

extern const JH7110DISP_MODE Jh7110DispMode1080p60;

NTSTATUS
Jh7110DispPowerOn(
    _In_ PJH7110DISP Display);

NTSTATUS
Jh7110DispStartHdmi(
    _In_ PJH7110DISP Display);

VOID
Jh7110DispStartController(
    _In_ PJH7110DISP Display,
    _In_ ULONG FrameBuffer,
    _In_ ULONG Pitch);

VOID
Jh7110DispSetFrameBuffer(
    _In_ PJH7110DISP Display,
    _In_ ULONG FrameBuffer,
    _In_ ULONG Pitch);

NTSTATUS
Jh7110DispStart(
    _In_ PJH7110DISP Display,
    _In_ ULONG FrameBuffer,
    _In_ ULONG Pitch);

VOID
Jh7110DispStop(
    _In_ PJH7110DISP Display);

BOOLEAN
Jh7110DispIsScanningOut(
    _In_ PJH7110DISP Display);

BOOLEAN
Jh7110DispHotPlug(
    _In_ PJH7110DISP Display);

VOID
Jh7110DispFlushCache(
    _In_ PUCHAR Ccache,
    _In_ ULONGLONG Address,
    _In_ SIZE_T Length);

ULONG
Jh7110DispRead(
    _In_ PUCHAR Base,
    _In_ ULONG Offset);
