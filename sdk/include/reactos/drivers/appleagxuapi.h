/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The render commands Mesa's Apple graphics drivers submit
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              The command layout mirrors Mesa's include/drm-uapi/asahi_drm.h:
 *              Copyright (C) The Asahi Linux Contributors
 *              Copyright (C) 2018-2023 Collabora Ltd.
 *              Copyright (C) 2014-2018 Broadcom
 *              SPDX-License-Identifier: MIT
 */

#pragma once

#define DRM_ASAHI_RENDER_VERTEX_SCRATCH         0x00000001
#define DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES    0x00000002
#define DRM_ASAHI_RENDER_NO_VERTEX_CLUSTERING   0x00000004
#define DRM_ASAHI_RENDER_DBIAS_IS_INT           0x00040000

typedef struct _DRM_ASAHI_ZLS_BUFFER
{
    ULONG64 Base;
    ULONG64 CompBase;
    ULONG Stride;
    ULONG CompStride;
} DRM_ASAHI_ZLS_BUFFER, *PDRM_ASAHI_ZLS_BUFFER;

typedef struct _DRM_ASAHI_TIMESTAMP
{
    ULONG Handle;
    ULONG Offset;
} DRM_ASAHI_TIMESTAMP, *PDRM_ASAHI_TIMESTAMP;

typedef struct _DRM_ASAHI_TIMESTAMPS
{
    DRM_ASAHI_TIMESTAMP Start;
    DRM_ASAHI_TIMESTAMP End;
} DRM_ASAHI_TIMESTAMPS, *PDRM_ASAHI_TIMESTAMPS;

typedef struct _DRM_ASAHI_HELPER_PROGRAM
{
    ULONG Binary;
    ULONG Cfg;
    ULONG64 Data;
} DRM_ASAHI_HELPER_PROGRAM, *PDRM_ASAHI_HELPER_PROGRAM;

typedef struct _DRM_ASAHI_BG_EOT
{
    ULONG Usc;
    ULONG RsrcSpec;
} DRM_ASAHI_BG_EOT, *PDRM_ASAHI_BG_EOT;

typedef struct _DRM_ASAHI_CMD_RENDER
{
    ULONG Flags;
    ULONG IspZlsPixels;
    ULONG64 VdmCtrlStreamBase;
    DRM_ASAHI_HELPER_PROGRAM VertexHelper;
    DRM_ASAHI_HELPER_PROGRAM FragmentHelper;
    ULONG64 IspScissorBase;
    ULONG64 IspDbiasBase;
    ULONG64 IspOclqryBase;
    DRM_ASAHI_ZLS_BUFFER Depth;
    DRM_ASAHI_ZLS_BUFFER Stencil;
    ULONG64 ZlsCtrl;
    ULONG64 PppMultisamplectl;
    ULONG64 SamplerHeap;
    ULONG PppCtrl;
    USHORT WidthPx;
    USHORT HeightPx;
    USHORT Layers;
    USHORT SamplerCount;
    UCHAR UtileWidthPx;
    UCHAR UtileHeightPx;
    UCHAR Samples;
    UCHAR SampleSizeB;
    ULONG IspMergeUpperX;
    ULONG IspMergeUpperY;
    DRM_ASAHI_BG_EOT Bg;
    DRM_ASAHI_BG_EOT Eot;
    DRM_ASAHI_BG_EOT PartialBg;
    DRM_ASAHI_BG_EOT PartialEot;
    ULONG IspBgobjdepth;
    ULONG IspBgobjvals;
    DRM_ASAHI_TIMESTAMPS TsVtx;
    DRM_ASAHI_TIMESTAMPS TsFrag;
} DRM_ASAHI_CMD_RENDER, *PDRM_ASAHI_CMD_RENDER;

C_ASSERT(sizeof(DRM_ASAHI_ZLS_BUFFER) == 0x18);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_ZLS_BUFFER, Stride) == 0x10);
C_ASSERT(sizeof(DRM_ASAHI_HELPER_PROGRAM) == 0x10);
C_ASSERT(sizeof(DRM_ASAHI_TIMESTAMPS) == 0x10);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, VdmCtrlStreamBase) == 0x08);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, VertexHelper) == 0x10);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, IspScissorBase) == 0x30);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, Depth) == 0x48);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, Stencil) == 0x60);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, ZlsCtrl) == 0x78);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, PppCtrl) == 0x90);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, WidthPx) == 0x94);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, UtileWidthPx) == 0x9c);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, IspMergeUpperX) == 0xa0);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, Bg) == 0xa8);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, PartialEot) == 0xc0);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, IspBgobjdepth) == 0xc8);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, TsVtx) == 0xd0);
C_ASSERT(FIELD_OFFSET(DRM_ASAHI_CMD_RENDER, TsFrag) == 0xe0);
C_ASSERT(sizeof(DRM_ASAHI_CMD_RENDER) == 0xf0);
