/*
 * PROJECT:     LiberNT Display Driver Model
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Advanced colour (HDR) state of a display output, between user
 *              mode (DisplayConfig, DXGI) and dxgkrnl
 * COPYRIGHT:   Copyright 2026 LiberNT Contributors
 */

#pragma once

/*
 * Carried by D3DKMTEscape on an adapter handle, with Type set to
 * RXGK_ESCAPE_ADVANCED_COLOR and the packet as the private data.  dxgkrnl
 * answers it itself; it never reaches the miniport.  The value lies outside
 * every D3DKMT_ESCAPETYPE Windows defines.
 *
 * The output is named by its VidPN source, as DisplayConfig and DXGI know
 * it; a SET applies to every target the source drives.  Only fixed-width
 * fields, laid out alike on x86, amd64 and ARM64.
 */
#define RXGK_ESCAPE_ADVANCED_COLOR          0x52580001U

#define RXGK_ADVANCED_COLOR_VERSION_1       1U

#define RXGK_ADVANCED_COLOR_GET             0U
#define RXGK_ADVANCED_COLOR_SET             1U

/* RXGK_ADVANCED_COLOR_PACKET.Flags (GET, out) */
#define RXGK_ADVANCED_COLOR_SUPPORTED       0x00000001U /* the output can be driven in HDR */
#define RXGK_ADVANCED_COLOR_ENABLED         0x00000002U /* it is driven in HDR now */
#define RXGK_ADVANCED_COLOR_REQUESTED       0x00000004U /* HDR was asked for */
#define RXGK_ADVANCED_COLOR_EDID_LUMINANCE  0x00000008U /* luminance fields are from the monitor */

typedef struct _RXGK_ADVANCED_COLOR_PACKET
{
    UINT32 Size;                    /* sizeof(RXGK_ADVANCED_COLOR_PACKET) */
    UINT32 Version;                 /* RXGK_ADVANCED_COLOR_VERSION_1 */
    UINT32 Operation;               /* RXGK_ADVANCED_COLOR_GET / _SET */
    UINT32 VidPnSourceId;           /* in */
    UINT32 Enable;                  /* SET, in: 1 HDR on, 0 off */
    UINT32 Flags;                   /* GET, out: RXGK_ADVANCED_COLOR_* */
    UINT32 ColorEncoding;           /* GET, out: DISPLAYCONFIG_COLOR_ENCODING of the wire */
    UINT32 BitsPerColorChannel;     /* GET, out: of the wire; 0 unknown */
    UINT32 SdrWhiteLevel;           /* GET, out: nits sRGB white maps to in HDR */
    UINT32 Reserved;
    /* GET, out: the monitor's primaries and white point in 1/1024 (EDID
     * chromaticity), and its luminance range in 1/10000 nit. */
    UINT32 RedPrimary[2];
    UINT32 GreenPrimary[2];
    UINT32 BluePrimary[2];
    UINT32 WhitePoint[2];
    UINT32 MinLuminance;
    UINT32 MaxLuminance;
    UINT32 MaxFullFrameLuminance;
    UINT32 Reserved2;
} RXGK_ADVANCED_COLOR_PACKET;

C_ASSERT(sizeof(RXGK_ADVANCED_COLOR_PACKET) == 88);
