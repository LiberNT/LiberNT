/*
 * PROJECT:     LiberNT Display Driver Model
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The display target a VidPN source drives, between user mode (DisplayConfig) and dxgkrnl
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/*
 * Carried by D3DKMTEscape on an adapter handle, with Type set to
 * RXGK_ESCAPE_TARGET_INFO and the packet as the private data.  dxgkrnl
 * answers it itself; it never reaches the miniport.  The value lies outside
 * every D3DKMT_ESCAPETYPE Windows defines.  Only fixed-width fields, laid
 * out alike on x86, amd64 and ARM64.
 */
#define RXGK_ESCAPE_TARGET_INFO             0x52580002U

#define RXGK_TARGET_INFO_VERSION_1          1U

/* RXGK_TARGET_INFO_PACKET.Flags (out) */
#define RXGK_TARGET_INFO_CONNECTED          0x00000001U /* a monitor is detected on the target */
#define RXGK_TARGET_INFO_EDID               0x00000002U /* Edid holds the monitor's base EDID block */

typedef struct _RXGK_TARGET_INFO_PACKET
{
    UINT32 Size;                    /* sizeof(RXGK_TARGET_INFO_PACKET) */
    UINT32 Version;                 /* RXGK_TARGET_INFO_VERSION_1 */
    UINT32 VidPnSourceId;           /* in */
    UINT32 VidPnTargetId;           /* out: the first target the source drives in the committed topology */
    UINT32 OutputTechnology;        /* out: D3DKMDT_VIDEO_OUTPUT_TECHNOLOGY of the target */
    UINT32 ConnectorInstance;       /* out: DISPLAYCONFIG_TARGET_DEVICE_NAME.connectorInstance */
    UINT32 Flags;                   /* out: RXGK_TARGET_INFO_* */
    UINT32 Reserved;
    UINT8 Edid[128];                /* out */
} RXGK_TARGET_INFO_PACKET;

C_ASSERT(sizeof(RXGK_TARGET_INFO_PACKET) == 160);
