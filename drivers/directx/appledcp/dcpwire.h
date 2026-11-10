/*
 * PROJECT:     LiberNT Apple Display Coprocessor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Records the display coprocessor exchanges with its host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#pragma pack(push, 1)
typedef struct _DCP_TIMING_MODE
{
    ULONG Valid;
    ULONG Width;
    ULONG Height;
    USHORT FpsFraction;
    USHORT FpsInteger;
    UCHAR Padding[8];
} DCP_TIMING_MODE, *PDCP_TIMING_MODE;

typedef struct _DCP_COLOR_MODE
{
    ULONG Valid;
    ULONG Colorimetry;
    ULONG Eotf;
    ULONG Encoding;
    ULONG Bpp;
    UCHAR Padding[4];
} DCP_COLOR_MODE, *PDCP_COLOR_MODE;

typedef struct _DCP_PLANE
{
    ULONG Unknown1;
    ULONG64 Address;
    ULONG TileSize;
    ULONG Stride;
    ULONG Unknown2[4];
    ULONG AddressFormat;
    ULONG Unknown3;
} DCP_PLANE, *PDCP_PLANE;

typedef struct _DCP_LAYER
{
    DCP_PLANE Planes[3];
    ULONG Unknown;
    ULONG PlaneCount;
    ULONG Width;
    ULONG Height;
    ULONG SurfaceFormat;
    ULONG Colorspace;
    ULONG Eotf;
    UCHAR Transform;
    UCHAR Padding[3];
} DCP_LAYER, *PDCP_LAYER;
#pragma pack(pop)

FORCEINLINE
BOOLEAN
DcpNativeMode(
    _In_reads_(Count) const DCP_TIMING_MODE *Mode,
    _In_ ULONG Count,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    ULONG64 Best = 0, Area;
    ULONG Index;

    *Width = 0;
    *Height = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        Area = (ULONG64)Mode[Index].Width * Mode[Index].Height;
        if (Mode[Index].Valid && Area > Best)
        {
            Best = Area;
            *Width = Mode[Index].Width;
            *Height = Mode[Index].Height;
        }
    }
    return Best != 0;
}

C_ASSERT(sizeof(DCP_TIMING_MODE) == 24);
C_ASSERT(sizeof(DCP_COLOR_MODE) == 24);
C_ASSERT(sizeof(DCP_PLANE) == 44);
C_ASSERT(sizeof(DCP_LAYER) == 164);
