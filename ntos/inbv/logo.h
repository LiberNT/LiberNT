/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot logo and progress bar layout
 * COPYRIGHT:   Copyright 2020 Dmitry Borisov <di.sean@protonmail.com>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

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

#define VID_SHUTDOWN_LOGO_LEFT    225

#define VID_SHUTDOWN_LOGO_TOP     114

#define VID_SHUTDOWN_MSG_LEFT     213

#define VID_SHUTDOWN_MSG_TOP      354

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
