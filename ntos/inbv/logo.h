/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Theme & Animation header
 * COPYRIGHT:   Copyright 2020 Dmitry Borisov (di.sean@protonmail.com)
 */

#pragma once

/* Shared bitmap structures used by bootanim.c and inbvgop.c */
typedef struct tagRGBQUAD
{
    UCHAR rgbBlue;
    UCHAR rgbGreen;
    UCHAR rgbRed;
    UCHAR rgbReserved;
} RGBQUAD, *LPRGBQUAD;

typedef struct tagBITMAPINFOHEADER
{
    ULONG  biSize;
    LONG   biWidth;
    LONG   biHeight;
    USHORT biPlanes;
    USHORT biBitCount;
    ULONG  biCompression;
    ULONG  biSizeImage;
    LONG   biXPelsPerMeter;
    LONG   biYPelsPerMeter;
    ULONG  biClrUsed;
    ULONG  biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

//
// Positions of areas and images
//

/* 16px space between shutdown logo and message */
#define VID_SHUTDOWN_LOGO_LEFT    225
#define VID_SHUTDOWN_LOGO_TOP     114
#define VID_SHUTDOWN_MSG_LEFT     213
#define VID_SHUTDOWN_MSG_TOP      354



//
// Boot Splash-Screen Functions
//

CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(VOID);

CODE_SEG("INIT")
VOID
NTAPI
FinalizeBootLogo(VOID);

VOID
NTAPI
DisplayShutdownBitmap(VOID);

VOID
NTAPI
DisplayShutdownText(VOID);
