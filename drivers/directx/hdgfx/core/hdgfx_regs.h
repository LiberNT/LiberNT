/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Ironlake display register and field table
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#pragma once

#include <stdint.h>

#define HDGFX_ILK_REGISTERS(R) \
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
    R(PF_WIN_POS,           0x68070, 0x800) \
    R(PF_WIN_SZ,            0x68074, 0x800) \
    R(PF_CTL_1,             0x68080, 0x800) \
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
    R(PCH_PP_STATUS,        0xC7200, 0) \
    R(PCH_PP_CONTROL,       0xC7204, 0) \
    R(PCH_PP_ON_DELAYS,     0xC7208, 0) \
    R(PCH_PP_OFF_DELAYS,    0xC720C, 0) \
    R(PCH_PP_DIVISOR,       0xC7210, 0) \
    R(BLC_PWM_PCH_CTL1,     0xC8250, 0) \
    R(BLC_PWM_PCH_CTL2,     0xC8254, 0) \
    R(TRANS_HTOTAL,         0xE0000, 0x1000) \
    R(TRANS_HBLANK,         0xE0004, 0x1000) \
    R(TRANS_HSYNC,          0xE0008, 0x1000) \
    R(TRANS_VTOTAL,         0xE000C, 0x1000) \
    R(TRANS_VBLANK,         0xE0010, 0x1000) \
    R(TRANS_VSYNC,          0xE0014, 0x1000) \
    R(PCH_LVDS,             0xE1180, 0) \
    R(TRANS_CONF,           0xF0008, 0x1000) \
    R(FDI_RX_CTL,           0xF000C, 0x1000)

#define HDGFX_ILK_FIELDS(F) \
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
    F(PF_ENABLE,                31, 31) \
    F(PF_X,                     28, 16) \
    F(PF_Y,                     11, 0) \
    F(DSP_ENABLE,               31, 31) \
    F(DSP_FORMAT,               29, 26) \
    F(DSP_TILED,                10, 10) \
    F(DSP_STRIDE,               15, 6) \
    F(DSP_SURFACE,              31, 12) \
    F(DSP_TILE_Y,               27, 16) \
    F(DSP_TILE_X,               11, 0) \
    F(DPLL_VCO_ENABLE,          31, 31) \
    F(DPLL_MODE,                27, 26) \
    F(DPLL_P2,                  25, 24) \
    F(DPLL_P1_FP0,              23, 16) \
    F(DPLL_REFERENCE,           15, 13) \
    F(FP_N,                     21, 16) \
    F(FP_M1,                    13, 8) \
    F(FP_M2,                    5, 0) \
    F(FDI_PLL_FB_DIVIDER,       7, 0) \
    F(RAWCLK_MHZ,               9, 0) \
    F(PP_STATUS_ON,             31, 31) \
    F(PP_STATUS_READY,          30, 30) \
    F(PP_STATUS_SEQUENCE,       29, 28) \
    F(PP_STATUS_CYCLE_ACTIVE,   27, 27) \
    F(PP_CONTROL_BACKLIGHT,     2, 2) \
    F(PP_CONTROL_RESET,         1, 1) \
    F(PP_CONTROL_TARGET_ON,     0, 0) \
    F(PP_DELAY_HIGH,            28, 16) \
    F(PP_DELAY_LOW,             12, 0) \
    F(PP_DIVISOR_REFERENCE,     31, 8) \
    F(PP_DIVISOR_CYCLE,         4, 0) \
    F(BLC_CPU_ENABLE,           31, 31) \
    F(BLC_CPU_PIPE,             29, 29) \
    F(BLC_CPU_DUTY,             15, 0) \
    F(BLC_PCH_ENABLE,           31, 31) \
    F(BLC_PCH_ACTIVE_LOW,       29, 29) \
    F(BLC_PCH_PERIOD,           31, 16) \
    F(TRANS_CONF_ENABLE,        31, 31) \
    F(TRANS_CONF_STATE,         30, 30) \
    F(TRANS_CONF_INTERLACE,     23, 21) \
    F(FDI_RX_ENABLE,            31, 31) \
    F(FDI_RX_TRAINING,          29, 28) \
    F(FDI_RX_WIDTH,             21, 19) \
    F(FDI_RX_BPC,               18, 16) \
    F(FDI_RX_PLL_ENABLE,        13, 13) \
    F(LVDS_ENABLE,              31, 31) \
    F(LVDS_TRANSCODER,          30, 30) \
    F(LVDS_VSYNC_INVERT,        21, 21) \
    F(LVDS_HSYNC_INVERT,        20, 20) \
    F(LVDS_A3_CONTROL,          7, 6) \
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
