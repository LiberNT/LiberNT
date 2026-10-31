/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Ironlake, Sandy Bridge and Ivy Bridge register and field table
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#pragma once

#include <stdint.h>

#define HDGFX_ILK_REGISTERS(R) \
    R(RING_TAIL,            0x2030, 0) \
    R(RING_HEAD,            0x2034, 0) \
    R(RING_START,           0x2038, 0) \
    R(RING_CTL,             0x203C, 0) \
    R(HWS_PGA,              0x2080, 0) \
    R(ESR,                  0x20B8, 0) \
    R(GDSR,                 0x12CA4, 0) \
    R(SNB_3D_CHICKEN,       0x2084, 0) \
    R(CHICKEN_3D3,          0x2090, 0) \
    R(RING_HWSTAM,          0x2098, 0) \
    R(RING_MI_MODE,         0x209C, 0) \
    R(FF_THREAD_MODE,       0x20A0, 0) \
    R(RING_IMR,             0x20A8, 0) \
    R(RING_INSTPM,          0x20C0, 0) \
    R(SNB_GT_MODE,          0x20D0, 0) \
    R(SNB_CACHE_MODE_0,     0x2120, 0) \
    R(RENDER_RING_MODE,     0x229C, 0) \
    R(SNB_GFX_MODE,         0x2520, 0) \
    R(RENDER_HWS_PGA,       0x4080, 0) \
    R(BLT_HWS_PGA,          0x4280, 0) \
    R(CACHE_MODE_0,         0x7000, 0) \
    R(CACHE_MODE_1,         0x7004, 0) \
    R(GT_MODE,              0x7008, 0) \
    R(COMMON_SLICE_CHICKEN1, 0x7010, 0) \
    R(SNB_UCGCTL1,          0x9400, 0) \
    R(SNB_UCGCTL2,          0x9404, 0) \
    R(GDRST,                0x941C, 0) \
    R(ECOBUS,               0xA180, 0) \
    R(FORCEWAKE_MT,         0xA188, 0) \
    R(FORCEWAKE,            0xA18C, 0) \
    R(L3_CONTROL,           0xB01C, 0) \
    R(L3_CHICKEN_MODE,      0xB030, 0) \
    R(L3SQ_CONTROL4,        0xB034, 0) \
    R(FORCEWAKE_MT_ACK,     0x130040, 0) \
    R(FORCEWAKE_ACK,        0x130090, 0) \
    R(GT_THREAD_STATUS,     0x13805C, 0) \
    R(SNB_BLT_HWS_PGA,      0x24080, 0) \
    R(SOUTH_CHICKEN1,       0xC2000, 0) \
    R(CPU_VGACNTRL,         0x41000, 0) \
    R(GTIMR,                0x44014, 0) \
    R(GTIIR,                0x44018, 0) \
    R(GTIER,                0x4401C, 0) \
    R(DEIMR,                0x44004, 0) \
    R(DEIIR,                0x44008, 0) \
    R(DEIER,                0x4400C, 0) \
    R(WM0_PIPE,             0x45100, 4) \
    R(WM_LP,                0x45108, 4) \
    R(GTT_PTE,              0x200000, 4) \
    R(GFX_FLUSH_CONTROL,    0x101008, 0) \
    R(FDI_PLL_BIOS_0,       0x46000, 0) \
    R(BLC_PWM_CPU_CTL2,     0x48250, 0) \
    R(BLC_PWM_CPU_CTL,      0x48254, 0) \
    R(PIPE_HTOTAL,          0x60000, 0x1000) \
    R(PIPE_HBLANK,          0x60004, 0x1000) \
    R(PIPE_HSYNC,           0x60008, 0x1000) \
    R(PIPE_VTOTAL,          0x6000C, 0x1000) \
    R(PIPE_VBLANK,          0x60010, 0x1000) \
    R(PIPE_VSYNC,           0x60014, 0x1000) \
    R(PIPE_SRC,             0x6001C, 0x1000) \
    R(PIPE_DATA_M1,         0x60030, 0x1000) \
    R(PIPE_DATA_N1,         0x60034, 0x1000) \
    R(PIPE_LINK_M1,         0x60040, 0x1000) \
    R(PIPE_LINK_N1,         0x60044, 0x1000) \
    R(FDI_TX_CTL,           0x60100, 0x1000) \
    R(PF_WIN_POS,           0x68070, 0x800) \
    R(PF_WIN_SZ,            0x68074, 0x800) \
    R(PF_CTL_1,             0x68080, 0x800) \
    R(PIPE_DSL,             0x70000, 0x1000) \
    R(CUR_CNTR,             0x70080, 0x40) \
    R(CUR_BASE,             0x70084, 0x40) \
    R(CUR_POS,              0x70088, 0x40) \
    R(PIPE_CONF,            0x70008, 0x1000) \
    R(DSP_CNTR,             0x70180, 0x1000) \
    R(DSP_LINOFF,           0x70184, 0x1000) \
    R(DSP_STRIDE,           0x70188, 0x1000) \
    R(DSP_SURF,             0x7019C, 0x1000) \
    R(DSP_TILEOFF,          0x701A4, 0x1000) \
    R(PCH_DPLL,             0xC6014, 4) \
    R(PCH_FP0,              0xC6040, 8) \
    R(PCH_FP1,              0xC6044, 8) \
    R(PCH_DREF_CONTROL,     0xC6200, 0) \
    R(PCH_RAWCLK_FREQ,      0xC6204, 0) \
    R(PCH_DPLL_SEL,         0xC7000, 0) \
    R(PCH_PP_STATUS,        0xC7200, 0) \
    R(PCH_PP_CONTROL,       0xC7204, 0) \
    R(PCH_PP_ON_DELAYS,     0xC7208, 0) \
    R(PCH_PP_OFF_DELAYS,    0xC720C, 0) \
    R(PCH_PP_DIVISOR,       0xC7210, 0) \
    R(GMBUS0,               0xC5100, 0) \
    R(GMBUS1,               0xC5104, 0) \
    R(GMBUS2,               0xC5108, 0) \
    R(GMBUS3,               0xC510C, 0) \
    R(GMBUS5,               0xC5120, 0) \
    R(BLC_PWM_PCH_CTL1,     0xC8250, 0) \
    R(BLC_PWM_PCH_CTL2,     0xC8254, 0) \
    R(TRANS_HTOTAL,         0xE0000, 0x1000) \
    R(TRANS_HBLANK,         0xE0004, 0x1000) \
    R(TRANS_HSYNC,          0xE0008, 0x1000) \
    R(TRANS_VTOTAL,         0xE000C, 0x1000) \
    R(TRANS_VBLANK,         0xE0010, 0x1000) \
    R(TRANS_VSYNC,          0xE0014, 0x1000) \
    R(SDEIMR,               0xC4004, 0) \
    R(SDEIIR,               0xC4008, 0) \
    R(SDEIER,               0xC400C, 0) \
    R(TRANS_DATA_M1,        0xE0030, 0x1000) \
    R(TRANS_DATA_N1,        0xE0034, 0x1000) \
    R(TRANS_LINK_M1,        0xE0040, 0x1000) \
    R(TRANS_LINK_N1,        0xE0044, 0x1000) \
    R(TRANS_DP_CTL,         0xE0300, 0x1000) \
    R(PCH_ADPA,             0xE1100, 0) \
    R(PCH_LVDS,             0xE1180, 0) \
    R(DP_CTL,               0xE4100, 0x100) \
    R(DP_AUX_CTL,           0xE4110, 0x100) \
    R(DP_AUX_DATA,          0xE4114, 0x100) \
    R(TRANS_CONF,           0xF0008, 0x1000) \
    R(FDI_RX_CTL,           0xF000C, 0x1000) \
    R(FDI_RX_MISC,          0xF0010, 0x1000) \
    R(FDI_RX_ISR,           0xF0014, 0x1000) \
    R(FDI_RX_IMR,           0xF0018, 0x1000) \
    R(FDI_RX_TUSIZE1,       0xF0030, 0x1000) \
    R(TRANS_CHICKEN2,       0xF0064, 0x1000)

#define HDGFX_ILK_FIELDS(F) \
    F(VGA_DISPLAY_DISABLE,      31, 31) \
    F(DE_MASTER,                31, 31) \
    F(DE_GSE_IVB,               29, 29) \
    F(DE_PCH_EVENT_IVB,         28, 28) \
    F(DE_PCH_EVENT,             21, 21) \
    F(DE_GSE,                   18, 18) \
    F(GT_RENDER_USER,           0, 0) \
    F(GT_BLT_USER,              22, 22) \
    F(SDE_CRT_HOTPLUG,          11, 11) \
    F(SDE_CRT_HOTPLUG_CPT,      19, 19) \
    F(TIMING_LOW,               12, 0) \
    F(TIMING_HIGH,              28, 16) \
    F(TIMING_ACTIVE,            11, 0) \
    F(PIPE_SRC_WIDTH,           27, 16) \
    F(PIPE_SRC_HEIGHT,          11, 0) \
    F(M_N_TU_SIZE,              30, 25) \
    F(M_N_VALUE,                23, 0) \
    F(PIPE_CONF_ENABLE,         31, 31) \
    F(PIPE_CONF_STATE,          30, 30) \
    F(PIPE_CONF_INTERLACE,      23, 21) \
    F(PIPE_CONF_BPC,            7, 5) \
    F(PIPE_CONF_DITHER,         4, 4) \
    F(PIPE_DSL_LINE,            12, 0) \
    F(WM_LP_ENABLE,             31, 31) \
    F(GTT_PTE_ADDRESS,          31, 12) \
    F(GTT_PTE_VALID,            0, 0) \
    F(GTT_PTE_ADDRESS_HIGH,     11, 4) \
    F(GTT_PTE_CACHE,            2, 1) \
    F(RING_TAIL_OFFSET,         20, 3) \
    F(RING_HEAD_OFFSET,         20, 2) \
    F(RING_START_ADDRESS,       31, 12) \
    F(RING_CTL_LENGTH,          20, 12) \
    F(RING_CTL_ENABLE,          0, 0) \
    F(GDSR_DOMAIN,              2, 1) \
    F(GDSR_RESET,               0, 0) \
    F(MI_OPCODE,                28, 23) \
    F(BLT_CLIENT,               31, 29) \
    F(BLT_OPCODE,               28, 22) \
    F(BLT_WRITE_ALPHA,          21, 21) \
    F(BLT_WRITE_RGB,            20, 20) \
    F(BLT_COLOR_DEPTH,          25, 24) \
    F(BLT_ROP,                  23, 16) \
    F(BLT_PITCH,                15, 0) \
    F(BLT_Y,                    31, 16) \
    F(BLT_X,                    15, 0) \
    F(MI_LENGTH,                7, 0) \
    F(MI_STORE_INDEX_OFFSET,    11, 2) \
    F(PF_ENABLE,                31, 31) \
    F(PF_PIPE_IVB,              30, 29) \
    F(PF_FILTER,                24, 23) \
    F(PF_X,                     28, 16) \
    F(PF_Y,                     11, 0) \
    F(CUR_MODE_SELECT,          5, 5) \
    F(CUR_MODE,                 2, 0) \
    F(CUR_POS_Y_SIGN,           31, 31) \
    F(CUR_POS_Y,                27, 16) \
    F(CUR_POS_X_SIGN,           15, 15) \
    F(CUR_POS_X,                11, 0) \
    F(DSP_ENABLE,               31, 31) \
    F(DSP_FORMAT,               29, 26) \
    F(DSP_TRICKLE_FEED_DISABLE, 14, 14) \
    F(DSP_TILED,                10, 10) \
    F(DSP_STRIDE,               15, 6) \
    F(DSP_SURFACE,              31, 12) \
    F(DSP_TILE_Y,               27, 16) \
    F(DSP_TILE_X,               11, 0) \
    F(DPLL_VCO_ENABLE,          31, 31) \
    F(DPLL_HIGH_SPEED,          30, 30) \
    F(DPLL_MODE,                27, 26) \
    F(DPLL_P2,                  25, 24) \
    F(DPLL_P1_FP0,              23, 16) \
    F(DPLL_REFERENCE,           15, 13) \
    F(DPLL_P1_FP1,              7, 0) \
    F(FP_CB_TUNE,               24, 22) \
    F(FP_N,                     21, 16) \
    F(FP_M1,                    13, 8) \
    F(FP_M2,                    5, 0) \
    F(FDI_PLL_FB_DIVIDER,       7, 0) \
    F(RAWCLK_MHZ,               9, 0) \
    F(PP_STATUS_ON,             31, 31) \
    F(PP_STATUS_READY,          30, 30) \
    F(PP_STATUS_SEQUENCE,       29, 28) \
    F(PP_STATUS_CYCLE_ACTIVE,   27, 27) \
    F(PP_CONTROL_KEY,           31, 16) \
    F(PP_CONTROL_BACKLIGHT,     2, 2) \
    F(PP_CONTROL_RESET,         1, 1) \
    F(PP_CONTROL_TARGET_ON,     0, 0) \
    F(PP_DELAY_HIGH,            28, 16) \
    F(PP_DELAY_LOW,             12, 0) \
    F(PP_DIVISOR_REFERENCE,     31, 8) \
    F(PP_DIVISOR_CYCLE,         4, 0) \
    F(GMBUS0_RATE,              10, 8) \
    F(GMBUS0_PINS,              2, 0) \
    F(GMBUS1_SW_CLR_INT,        31, 31) \
    F(GMBUS1_SW_RDY,            30, 30) \
    F(GMBUS1_ENABLE_TIMEOUT,    29, 29) \
    F(GMBUS1_CYCLE,             27, 25) \
    F(GMBUS1_BYTE_COUNT,        24, 16) \
    F(GMBUS1_INDEX,             15, 8) \
    F(GMBUS1_ADDRESS,           7, 1) \
    F(GMBUS1_READ,              0, 0) \
    F(GMBUS2_INUSE,             15, 15) \
    F(GMBUS2_STALL_TIMEOUT,     13, 13) \
    F(GMBUS2_HW_RDY,            11, 11) \
    F(GMBUS2_NAK,               10, 10) \
    F(GMBUS2_ACTIVE,            9, 9) \
    F(BLC_CPU_ENABLE,           31, 31) \
    F(BLC_CPU_PIPE,             29, 29) \
    F(BLC_CPU_PIPE_IVB,         30, 29) \
    F(BLC_CPU_DUTY,             15, 0) \
    F(BLC_PCH_ENABLE,           31, 31) \
    F(BLC_PCH_ACTIVE_LOW,       29, 29) \
    F(BLC_PCH_PERIOD,           31, 16) \
    F(TRANS_DP_ENABLE,          31, 31) \
    F(TRANS_DP_PORT,            30, 29) \
    F(TRANS_DP_ENHANCED,        18, 18) \
    F(TRANS_DP_BPC,             11, 9) \
    F(TRANS_DP_VSYNC_HIGH,      4, 4) \
    F(TRANS_DP_HSYNC_HIGH,      3, 3) \
    F(DP_ENABLE,                31, 31) \
    F(DP_SWING,                 27, 25) \
    F(DP_EMPHASIS,              24, 22) \
    F(DP_WIDTH,                 21, 19) \
    F(DP_TRAINING,              10, 8) \
    F(DP_DETECTED,              2, 2) \
    F(DP_AUX_BUSY,              31, 31) \
    F(DP_AUX_DONE,              30, 30) \
    F(DP_AUX_TIMEOUT,           28, 28) \
    F(DP_AUX_RECEIVE_ERROR,     25, 25) \
    F(DP_AUX_SIZE,              24, 20) \
    F(DP_AUX_PRECHARGE,         19, 16) \
    F(DP_AUX_CLOCK,             10, 0) \
    F(TRANS_CONF_ENABLE,        31, 31) \
    F(TRANS_CONF_STATE,         30, 30) \
    F(TRANS_CONF_INTERLACE,     23, 21) \
    F(TRANS_CONF_BPC,           7, 5) \
    F(FDI_RX_ENABLE,            31, 31) \
    F(FDI_RX_TRAINING,          29, 28) \
    F(FDI_RX_TRAINING_CPT,      9, 8) \
    F(FDI_RX_AUTO_TRAINING,     10, 10) \
    F(FDI_RX_COMPOSITE_SYNC,    11, 11) \
    F(FDI_RX_FS_ERROR_CORRECT,  27, 27) \
    F(FDI_RX_FE_ERROR_CORRECT,  26, 26) \
    F(FDI_RX_ENHANCED_FRAMING,  6, 6) \
    F(FDI_RX_WIDTH,             21, 19) \
    F(FDI_RX_BPC,               18, 16) \
    F(FDI_RX_PLL_ENABLE,        13, 13) \
    F(FDI_RX_PCDCLK,            4, 4) \
    F(FDI_RX_SYMBOL_LOCK,       9, 9) \
    F(FDI_RX_BIT_LOCK,          8, 8) \
    F(FDI_RX_TP1_TO_TP2,        21, 20) \
    F(FDI_RX_DELAY,             7, 0) \
    F(SOUTH_CHICKEN1_FDI_BC,    12, 12) \
    F(TRANS_CHICKEN2_OVERRIDE,  31, 31) \
    F(FDI_TX_ENABLE,            31, 31) \
    F(FDI_TX_TRAINING,          29, 28) \
    F(FDI_TX_TRAINING_IVB,      9, 8) \
    F(FDI_TX_AUTO_TRAINING,     10, 10) \
    F(FDI_TX_COMPOSITE_SYNC,    11, 11) \
    F(FDI_TX_SWING_EMPHASIS,    27, 22) \
    F(FDI_TX_WIDTH,             21, 19) \
    F(FDI_TX_ENHANCED_FRAMING,  18, 18) \
    F(FDI_TX_PLL_ENABLE,        14, 14) \
    F(DREF_SSC_SOURCE,          12, 11) \
    F(DREF_NONSPREAD_SOURCE,    10, 9) \
    F(ADPA_ENABLE,              31, 31) \
    F(ADPA_TRANSCODER,          30, 30) \
    F(ADPA_TRANSCODER_CPT,      30, 29) \
    F(ADPA_HOTPLUG_STATUS,      25, 24) \
    F(ADPA_HOTPLUG_ENABLE,      23, 23) \
    F(ADPA_FORCE_TRIGGER,       16, 16) \
    F(ADPA_VSYNC_HIGH,          4, 4) \
    F(ADPA_HSYNC_HIGH,          3, 3) \
    F(LVDS_ENABLE,              31, 31) \
    F(LVDS_TRANSCODER,          30, 30) \
    F(LVDS_TRANSCODER_CPT,      30, 29) \
    F(LVDS_VSYNC_INVERT,        21, 21) \
    F(LVDS_HSYNC_INVERT,        20, 20) \
    F(LVDS_A_CONTROL,           9, 8) \
    F(LVDS_A3_CONTROL,          7, 6) \
    F(LVDS_CLKB_CONTROL,        5, 4) \
    F(LVDS_B_CONTROL,           3, 2) \
    F(LVDS_DETECTED,            1, 1)

#define HDGFX_DEFINE_REGISTER(Name, Offset, Stride) \
    HDGFX_REG_##Name = Offset, HDGFX_STRIDE_##Name = Stride,
#define HDGFX_DEFINE_FIELD(Name, High, Low) \
    HDGFX_FIELD_##Name##_HIGH = High, HDGFX_FIELD_##Name##_LOW = Low,

enum { HDGFX_ILK_REGISTERS(HDGFX_DEFINE_REGISTER) };
enum { HDGFX_ILK_FIELDS(HDGFX_DEFINE_FIELD) };

#define HDGFX_REG(Name, Index) \
    ((uint32_t)HDGFX_REG_##Name + (uint32_t)(Index) * (uint32_t)HDGFX_STRIDE_##Name)

#define HDGFX_BITS(Value, High, Low) \
    ((uint32_t)(((uint32_t)(Value) >> (Low)) & (uint32_t)((2ull << ((High) - (Low))) - 1)))

#define HDGFX_GET(Value, Name) \
    HDGFX_BITS(Value, HDGFX_FIELD_##Name##_HIGH, HDGFX_FIELD_##Name##_LOW)

#define HDGFX_SET(Name, Value) \
    (HDGFX_BITS(Value, HDGFX_FIELD_##Name##_HIGH - HDGFX_FIELD_##Name##_LOW, 0) << HDGFX_FIELD_##Name##_LOW)

#define HDGFX_MASK(Name) \
    HDGFX_SET(Name, 0xFFFFFFFFu)

#define HDGFX_REPLACE(Value, Name, Field) \
    (((uint32_t)(Value) & ~HDGFX_MASK(Name)) | HDGFX_SET(Name, Field))
