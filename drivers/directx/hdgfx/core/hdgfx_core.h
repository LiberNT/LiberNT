/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hardware-independent display state core
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HDGFX_MAX_PIPES 3
#define HDGFX_MAX_TRANSCODERS 3
#define HDGFX_MAX_DPLLS 2
#define HDGFX_EDID_SIZE 128
#define HDGFX_EDID_MAX_DTDS 4
#define HDGFX_DDC_ADDRESS 0x50

#define HDGFX_ASLE_LEVEL_MAX 255
#define HDGFX_OPREGION_EVENT_HOTKEY 0x01
#define HDGFX_OPREGION_EVENT_LID    0x02
#define HDGFX_OPREGION_EVENT_DOCK   0x04

typedef uint32_t (*HDGFX_READ_REGISTER)(void *Context, uint32_t Offset);
typedef void (*HDGFX_WRITE_REGISTER)(void *Context, uint32_t Offset, uint32_t Value);
typedef void (*HDGFX_STALL)(void *Context, uint32_t Microseconds);
typedef void (*HDGFX_SLEEP)(void *Context, uint32_t Milliseconds);

typedef struct _HDGFX_MMIO
{
    HDGFX_READ_REGISTER Read;
    void *Context;
    HDGFX_WRITE_REGISTER Write;
    HDGFX_STALL Stall;
    HDGFX_SLEEP Sleep;
} HDGFX_MMIO;

typedef bool (*HDGFX_SET_BACKLIGHT)(void *Context, uint32_t Level);

typedef struct _HDGFX_TIMINGS
{
    uint32_t HActive;
    uint32_t HBlankStart;
    uint32_t HBlankEnd;
    uint32_t HSyncStart;
    uint32_t HSyncEnd;
    uint32_t HTotal;
    uint32_t VActive;
    uint32_t VBlankStart;
    uint32_t VBlankEnd;
    uint32_t VSyncStart;
    uint32_t VSyncEnd;
    uint32_t VTotal;
} HDGFX_TIMINGS;

typedef struct _HDGFX_DTD
{
    uint32_t DotKHz;
    HDGFX_TIMINGS Timings;
    bool HSyncPositive;
    bool VSyncPositive;
    uint32_t WidthMm;
    uint32_t HeightMm;
} HDGFX_DTD;

typedef struct _HDGFX_M_N
{
    uint32_t TuSize;
    uint32_t DataM;
    uint32_t DataN;
    uint32_t LinkM;
    uint32_t LinkN;
} HDGFX_M_N;

typedef struct _HDGFX_PLANE
{
    bool Enabled;
    bool XTiled;
    uint32_t Format;
    uint32_t Stride;
    uint32_t Surface;
    uint32_t LinearOffset;
    uint32_t TileX;
    uint32_t TileY;
} HDGFX_PLANE;

typedef struct _HDGFX_PANEL_FITTER
{
    bool Enabled;
    uint32_t X;
    uint32_t Y;
    uint32_t Width;
    uint32_t Height;
} HDGFX_PANEL_FITTER;

typedef struct _HDGFX_FDI_TX
{
    bool Enabled;
    uint32_t Training;
    uint32_t Lanes;
} HDGFX_FDI_TX;

typedef struct _HDGFX_PIPE
{
    bool Enabled;
    bool Active;
    bool Dither;
    uint32_t Bpc;
    uint32_t InterlaceMode;
    HDGFX_TIMINGS Timings;
    uint32_t SourceWidth;
    uint32_t SourceHeight;
    HDGFX_M_N Fdi;
    HDGFX_FDI_TX FdiTx;
    HDGFX_PANEL_FITTER PanelFitter;
    HDGFX_PLANE Primary;
} HDGFX_PIPE;

typedef enum _HDGFX_DPLL_MODE
{
    HdgfxDpllModeInvalid,
    HdgfxDpllModeNonLvds,
    HdgfxDpllModeLvds
} HDGFX_DPLL_MODE;

typedef struct _HDGFX_DPLL
{
    bool Enabled;
    HDGFX_DPLL_MODE Mode;
    uint32_t Reference;
    uint32_t N;
    uint32_t M1;
    uint32_t M2;
    uint32_t P1;
    uint32_t P2;
    uint32_t ReferenceKHz;
    uint32_t VcoKHz;
    uint32_t DotKHz;
} HDGFX_DPLL;

typedef struct _HDGFX_FDI_RX
{
    bool Enabled;
    bool PllEnabled;
    uint32_t Training;
    uint32_t Lanes;
    uint32_t Bpc;
} HDGFX_FDI_RX;

typedef struct _HDGFX_TRANSCODER
{
    bool Enabled;
    bool Active;
    uint32_t InterlaceMode;
    uint32_t Dpll;
    HDGFX_TIMINGS Timings;
    HDGFX_FDI_RX FdiRx;
} HDGFX_TRANSCODER;

typedef struct _HDGFX_LVDS
{
    bool Enabled;
    bool Detected;
    bool DualChannel;
    bool HSyncActiveLow;
    bool VSyncActiveLow;
    uint32_t Transcoder;
    uint32_t Bpc;
} HDGFX_LVDS;

typedef struct _HDGFX_CRT
{
    bool Enabled;
    bool HSyncActiveHigh;
    bool VSyncActiveHigh;
    uint32_t Transcoder;
} HDGFX_CRT;

typedef struct _HDGFX_PANEL_POWER
{
    bool On;
    bool Ready;
    bool CycleDelayActive;
    bool TargetOn;
    bool BacklightEnabled;
    bool PowerDownOnReset;
    uint32_t Sequence;
    uint32_t PowerUpUs;
    uint32_t BacklightOnUs;
    uint32_t PowerDownUs;
    uint32_t BacklightOffUs;
    uint32_t PowerCycleUs;
    uint32_t ReferenceDivider;
} HDGFX_PANEL_POWER;

typedef struct _HDGFX_BACKLIGHT
{
    bool CpuEnabled;
    bool PchEnabled;
    bool ActiveLow;
    uint32_t CpuPipe;
    uint32_t Duty;
    uint32_t Period;
    uint32_t FrequencyHz;
} HDGFX_BACKLIGHT;

typedef struct _HDGFX_DISPLAY_STATE
{
    uint32_t PipeCount;
    uint32_t TranscoderCount;
    uint32_t DpllCount;
    uint32_t RawClockKHz;
    uint32_t FdiPllVcoKHz;
    bool SscReference;
    bool NonSpreadReference;
    HDGFX_PIPE Pipes[HDGFX_MAX_PIPES];
    HDGFX_TRANSCODER Transcoders[HDGFX_MAX_TRANSCODERS];
    HDGFX_DPLL Dplls[HDGFX_MAX_DPLLS];
    HDGFX_LVDS Lvds;
    HDGFX_CRT Crt;
    HDGFX_PANEL_POWER PanelPower;
    HDGFX_BACKLIGHT Backlight;
} HDGFX_DISPLAY_STATE;

typedef enum _HDGFX_SCALING
{
    HdgfxScalingAspect,
    HdgfxScalingStretch,
    HdgfxScalingCenter
} HDGFX_SCALING;

typedef struct _HDGFX_LVDS_MODE
{
    const HDGFX_DTD *Panel;
    uint32_t SourceWidth;
    uint32_t SourceHeight;
    uint32_t Stride;
    uint32_t Surface;
    HDGFX_SCALING Scaling;
    bool Ssc;
} HDGFX_LVDS_MODE;

typedef struct _HDGFX_CRT_MODE
{
    const HDGFX_DTD *Timing;
    uint32_t Pipe;
    uint32_t SourceWidth;
    uint32_t SourceHeight;
    uint32_t Stride;
    uint32_t Surface;
    HDGFX_SCALING Scaling;
} HDGFX_CRT_MODE;

typedef enum _HDGFX_MODESET_STATUS
{
    HdgfxModesetSuccess,
    HdgfxModesetInvalidMode,
    HdgfxModesetNoClock,
    HdgfxModesetFdiBandwidth,
    HdgfxModesetPanelOffTimeout,
    HdgfxModesetPipeOffTimeout,
    HdgfxModesetTranscoderOffTimeout,
    HdgfxModesetFdiBitLockFailed,
    HdgfxModesetFdiSymbolLockFailed,
    HdgfxModesetTranscoderOnTimeout,
    HdgfxModesetPanelOnTimeout,
    HdgfxModesetVblankTimeout,
    HdgfxModesetDetectTimeout
} HDGFX_MODESET_STATUS;

typedef struct _HDGFX_GENERATION
{
    const char *Name;
    uint32_t Gen;
    uint32_t PanelDdcPins;
    uint32_t CrtDdcPins;
    uint32_t GseInterrupt;
    uint32_t PchInterrupt;
    uint32_t CrtHotplugInterrupt;
    void (*ReadDisplayState)(const HDGFX_MMIO *Mmio, HDGFX_DISPLAY_STATE *State);
    HDGFX_MODESET_STATUS (*ComputeLvds)(const HDGFX_DISPLAY_STATE *Current,
                                        const HDGFX_LVDS_MODE *Mode,
                                        HDGFX_DISPLAY_STATE *Target);
    HDGFX_MODESET_STATUS (*CommitLvds)(const HDGFX_MMIO *Mmio,
                                       const HDGFX_DISPLAY_STATE *Current,
                                       const HDGFX_DISPLAY_STATE *Target);
    HDGFX_MODESET_STATUS (*DisableLvds)(const HDGFX_MMIO *Mmio, const HDGFX_DISPLAY_STATE *Current);
    HDGFX_MODESET_STATUS (*DetectCrt)(const HDGFX_MMIO *Mmio, bool *Attached);
    HDGFX_MODESET_STATUS (*ComputeCrt)(const HDGFX_DISPLAY_STATE *Current,
                                       const HDGFX_CRT_MODE *Mode,
                                       HDGFX_DISPLAY_STATE *Target);
    HDGFX_MODESET_STATUS (*CommitCrt)(const HDGFX_MMIO *Mmio,
                                      const HDGFX_DISPLAY_STATE *Current,
                                      const HDGFX_DISPLAY_STATE *Target);
    HDGFX_MODESET_STATUS (*DisableCrt)(const HDGFX_MMIO *Mmio, const HDGFX_DISPLAY_STATE *Current);
    void (*EnableCrtHotplug)(const HDGFX_MMIO *Mmio, bool Enable);
} HDGFX_GENERATION;

typedef struct _HDGFX_RING
{
    uint32_t *Commands;
    uint32_t Size;
    uint32_t Address;
    uint32_t StatusAddress;
    uint32_t Tail;
} HDGFX_RING;

typedef struct _HDGFX_BLT_SURFACE
{
    uint32_t Address;
    uint32_t Pitch;
} HDGFX_BLT_SURFACE;

typedef struct _HDGFX_BLT_RECT
{
    uint32_t Left;
    uint32_t Top;
    uint32_t Right;
    uint32_t Bottom;
} HDGFX_BLT_RECT;

typedef struct _HDGFX_VBT_PANEL
{
    bool Present;
    uint32_t PanelType;
    uint32_t DotKHz;
    HDGFX_TIMINGS Timings;
    bool HSyncPositive;
    bool VSyncPositive;
    uint32_t WidthMm;
    uint32_t HeightMm;
    bool BacklightPresent;
    uint32_t BacklightType;
    bool BacklightActiveLow;
    uint32_t BacklightPwmHz;
    uint32_t BacklightMinimum;
} HDGFX_VBT_PANEL;

typedef struct _HDGFX_EDID
{
    char Manufacturer[4];
    uint32_t ProductCode;
    uint32_t SerialNumber;
    uint32_t Version;
    uint32_t Revision;
    uint32_t WidthCm;
    uint32_t HeightCm;
    uint32_t Extensions;
    uint32_t DtdCount;
    HDGFX_DTD Dtds[HDGFX_EDID_MAX_DTDS];
} HDGFX_EDID;

typedef enum _HDGFX_GMBUS_STATUS
{
    HdgfxGmbusSuccess,
    HdgfxGmbusInvalidParameter,
    HdgfxGmbusBusy,
    HdgfxGmbusNoAcknowledge,
    HdgfxGmbusSlaveTimeout,
    HdgfxGmbusTimeout
} HDGFX_GMBUS_STATUS;

typedef struct _HDGFX_VBT
{
    char Signature[21];
    uint32_t BdbVersion;
    bool LvdsSscEnabled;
    bool LvdsSscFrequencyBit;
    HDGFX_VBT_PANEL Panel;
} HDGFX_VBT;

const HDGFX_GENERATION *
HdgfxFindGeneration(
    uint16_t VendorId,
    uint16_t DeviceId);

void
HdgfxIlkReadDisplayState(
    const HDGFX_MMIO *Mmio,
    HDGFX_DISPLAY_STATE *State);

HDGFX_MODESET_STATUS
HdgfxIlkComputeLvds(
    const HDGFX_DISPLAY_STATE *Current,
    const HDGFX_LVDS_MODE *Mode,
    HDGFX_DISPLAY_STATE *Target);

HDGFX_MODESET_STATUS
HdgfxIlkCommitLvds(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Current,
    const HDGFX_DISPLAY_STATE *Target);

HDGFX_MODESET_STATUS
HdgfxIlkDisableLvds(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Current);

HDGFX_MODESET_STATUS
HdgfxIlkDetectCrt(
    const HDGFX_MMIO *Mmio,
    bool *Attached);

HDGFX_MODESET_STATUS
HdgfxIlkComputeCrt(
    const HDGFX_DISPLAY_STATE *Current,
    const HDGFX_CRT_MODE *Mode,
    HDGFX_DISPLAY_STATE *Target);

HDGFX_MODESET_STATUS
HdgfxIlkCommitCrt(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Current,
    const HDGFX_DISPLAY_STATE *Target);

HDGFX_MODESET_STATUS
HdgfxIlkDisableCrt(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DISPLAY_STATE *Current);

void
HdgfxIlkEnableCrtHotplug(
    const HDGFX_MMIO *Mmio,
    bool Enable);

const char *
HdgfxCompareCrtState(
    const HDGFX_DISPLAY_STATE *Target,
    const HDGFX_DISPLAY_STATE *Actual);

const char *
HdgfxCompareLvdsState(
    const HDGFX_DISPLAY_STATE *Target,
    const HDGFX_DISPLAY_STATE *Actual);

const char *
HdgfxModesetStatusName(
    HDGFX_MODESET_STATUS Status);

uint32_t
HdgfxRefreshMilliHz(
    const HDGFX_TIMINGS *Timings,
    uint32_t DotKHz);

void
HdgfxDecodeDtd(
    const uint8_t *Dtd,
    HDGFX_DTD *Result);

bool
HdgfxEdidParse(
    const uint8_t *Edid,
    HDGFX_EDID *Result);

HDGFX_GMBUS_STATUS
HdgfxGmbusRead(
    const HDGFX_MMIO *Mmio,
    uint32_t Pins,
    uint8_t Address,
    uint8_t Index,
    uint8_t *Buffer,
    uint32_t Size);

const char *
HdgfxGmbusStatusName(
    HDGFX_GMBUS_STATUS Status);

bool
HdgfxOpRegionFindVbt(
    const uint8_t *OpRegion,
    size_t Size,
    const uint8_t **Vbt,
    size_t *VbtSize);

bool
HdgfxOpRegionDriverReady(
    const HDGFX_MMIO *OpRegion,
    uint32_t DisplayId,
    uint32_t Percent);

void
HdgfxOpRegionDriverNotReady(
    const HDGFX_MMIO *OpRegion);

void
HdgfxOpRegionSetBrightness(
    const HDGFX_MMIO *OpRegion,
    uint32_t Percent);

bool
HdgfxOpRegionServiceAsle(
    const HDGFX_MMIO *OpRegion,
    HDGFX_SET_BACKLIGHT SetBacklight,
    void *Context);

bool
HdgfxOpRegionBeginNotification(
    const HDGFX_MMIO *OpRegion,
    uint32_t *Events);

void
HdgfxOpRegionEndNotification(
    const HDGFX_MMIO *OpRegion,
    bool Success);

uint32_t
HdgfxIlkGttEntry(
    uint32_t Physical);

bool
HdgfxIlkStartRing(
    const HDGFX_MMIO *Mmio,
    HDGFX_RING *Ring);

bool
HdgfxIlkStopRing(
    const HDGFX_MMIO *Mmio,
    const HDGFX_RING *Ring);

uint32_t
HdgfxIlkRingHead(
    const HDGFX_MMIO *Mmio);

void
HdgfxIlkAdvanceRing(
    const HDGFX_MMIO *Mmio,
    const HDGFX_RING *Ring);

bool
HdgfxRingEmit(
    HDGFX_RING *Ring,
    uint32_t Head,
    const uint32_t *Dwords,
    uint32_t Count);

bool
HdgfxRingEmitBreadcrumb(
    HDGFX_RING *Ring,
    uint32_t Head,
    uint32_t StatusIndex,
    uint32_t Value);

uint32_t
HdgfxMiFlush(
    uint32_t *Dwords);

uint32_t
HdgfxBltFill(
    uint32_t *Dwords,
    const HDGFX_BLT_SURFACE *Destination,
    const HDGFX_BLT_RECT *Rect,
    uint32_t Color);

uint32_t
HdgfxBltCopy(
    uint32_t *Dwords,
    const HDGFX_BLT_SURFACE *Destination,
    const HDGFX_BLT_RECT *Rect,
    const HDGFX_BLT_SURFACE *Source,
    uint32_t SourceX,
    uint32_t SourceY);

bool
HdgfxVbtParse(
    const uint8_t *Vbt,
    size_t Size,
    HDGFX_VBT *Result);

#ifdef __cplusplus
}
#endif
