/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Theme & Animation
 * COPYRIGHT:   Copyright 2007 Alex Ionescu (alex.ionescu@reactos.org)
 *              Copyright 2007 Hervé Poussineau (hpoussin@reactos.org)
 *              Copyright 2012-2022 Hermès Bélusca-Maïto
 *              Copyright 2017-2018 Stanislav Motylkov
 *              Copyright 2019-2020 Yaroslav Kibysh
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <reactos/arc/arc.h>
#include "inbv/logo.h"
#include "inbv/inbvgop.h"

#define MM_READONLY     1   // PAGE_READONLY
#define MM_READWRITE    4   // PAGE_WRITECOPY

/* GLOBALS *******************************************************************/

/*
 * BitBltAligned() alignments
 */
typedef enum _BBLT_VERT_ALIGNMENT
{
    AL_VERTICAL_TOP = 0,
    AL_VERTICAL_CENTER,
    AL_VERTICAL_BOTTOM
} BBLT_VERT_ALIGNMENT;

typedef enum _BBLT_HORZ_ALIGNMENT
{
    AL_HORIZONTAL_LEFT = 0,
    AL_HORIZONTAL_CENTER,
    AL_HORIZONTAL_RIGHT
} BBLT_HORZ_ALIGNMENT;

/* FADE-IN FUNCTION **********************************************************/

/* RGBQUAD and BITMAPINFOHEADER are now defined in logo.h */

static RGBQUAD MainPalette[16];

#define PALETTE_FADE_STEPS  12
#define PALETTE_FADE_TIME   (15 * 1000) /* 15 ms */

static VOID
BootLogoFadeIn(VOID)
{
    UCHAR PaletteBitmapBuffer[sizeof(BITMAPINFOHEADER) + sizeof(MainPalette)];
    PBITMAPINFOHEADER PaletteBitmap = (PBITMAPINFOHEADER)PaletteBitmapBuffer;
    LPRGBQUAD Palette = (LPRGBQUAD)(PaletteBitmapBuffer + sizeof(BITMAPINFOHEADER));
    ULONG Iteration, Index, ClrUsed;

    LARGE_INTEGER Delay;
    Delay.QuadPart = -(PALETTE_FADE_TIME * 10);

    /* Check if we are installed and we own the display */
    if (!InbvBootDriverInstalled ||
        (InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED))
    {
        return;
    }

    /*
     * Build a bitmap containing the fade-in palette. The palette entries
     * are then processed in a loop and set using VidBitBlt function.
     */
    ClrUsed = RTL_NUMBER_OF(MainPalette);
    RtlZeroMemory(PaletteBitmap, sizeof(BITMAPINFOHEADER));
    PaletteBitmap->biSize = sizeof(BITMAPINFOHEADER);
    PaletteBitmap->biBitCount = 4;
    PaletteBitmap->biClrUsed = ClrUsed;

    /*
     * Main animation loop.
     */
    for (Iteration = 0; Iteration <= PALETTE_FADE_STEPS; ++Iteration)
    {
        for (Index = 0; Index < ClrUsed; Index++)
        {
            Palette[Index].rgbRed = (UCHAR)
                (MainPalette[Index].rgbRed * Iteration / PALETTE_FADE_STEPS);
            Palette[Index].rgbGreen = (UCHAR)
                (MainPalette[Index].rgbGreen * Iteration / PALETTE_FADE_STEPS);
            Palette[Index].rgbBlue = (UCHAR)
                (MainPalette[Index].rgbBlue * Iteration / PALETTE_FADE_STEPS);
        }

        /* Do the animation */
        InbvAcquireLock();
        VidBitBlt(PaletteBitmapBuffer, 0, 0);
        InbvReleaseLock();

        /* Wait for a bit */
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
}

static VOID
BitBltPalette(
    IN PVOID Image,
    IN BOOLEAN NoPalette,
    IN ULONG X,
    IN ULONG Y)
{
    LPRGBQUAD Palette;
    RGBQUAD OrigPalette[RTL_NUMBER_OF(MainPalette)];

    /* If requested, remove the palette from the image */
    if (NoPalette)
    {
        /* Get bitmap header and palette */
        PBITMAPINFOHEADER BitmapInfoHeader = Image;
        Palette = (LPRGBQUAD)((PUCHAR)Image + BitmapInfoHeader->biSize);

        /* Save the image original palette and remove palette information */
        RtlCopyMemory(OrigPalette, Palette, sizeof(OrigPalette));
        RtlZeroMemory(Palette, sizeof(OrigPalette));
    }

    /* Draw the image */
    InbvBitBlt(Image, X, Y);

    /* Restore the image original palette */
    if (NoPalette)
    {
        RtlCopyMemory(Palette, OrigPalette, sizeof(OrigPalette));
    }
}

static VOID
BitBltAligned(
    IN PVOID Image,
    IN BOOLEAN NoPalette,
    IN BBLT_HORZ_ALIGNMENT HorizontalAlignment,
    IN BBLT_VERT_ALIGNMENT VerticalAlignment,
    IN ULONG MarginLeft,
    IN ULONG MarginTop,
    IN ULONG MarginRight,
    IN ULONG MarginBottom)
{
    PBITMAPINFOHEADER BitmapInfoHeader = Image;
    VID_DISPLAY_INFO DisplayInfo;
    ULONG X, Y;

    InbvQueryDisplayInfo(&DisplayInfo);

    /* Calculate X */
    switch (HorizontalAlignment)
    {
        case AL_HORIZONTAL_LEFT:
            X = MarginLeft - MarginRight;
            break;

        case AL_HORIZONTAL_CENTER:
            X = MarginLeft - MarginRight + (DisplayInfo.Width - BitmapInfoHeader->biWidth + 1) / 2;
            break;

        case AL_HORIZONTAL_RIGHT:
            X = MarginLeft - MarginRight + DisplayInfo.Width - BitmapInfoHeader->biWidth;
            break;

        default:
            /* Unknown */
            return;
    }

    /* Calculate Y */
    switch (VerticalAlignment)
    {
        case AL_VERTICAL_TOP:
            Y = MarginTop - MarginBottom;
            break;

        case AL_VERTICAL_CENTER:
            Y = MarginTop - MarginBottom + (DisplayInfo.Height - BitmapInfoHeader->biHeight + 1) / 2;
            break;

        case AL_VERTICAL_BOTTOM:
            Y = MarginTop - MarginBottom + DisplayInfo.Height - BitmapInfoHeader->biHeight;
            break;

        default:
            /* Unknown */
            return;
    }

    /* Finally draw the image */
    BitBltPalette(Image, NoPalette, X, Y);
}

/* FUNCTIONS *****************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(VOID)
{
    PVOID BootCopy = NULL, BootLogo = NULL;

    if (InbvGopHandleBootBitmap())
        return;

    /* Is the boot driver installed? */
    if (!InbvBootDriverInstalled) return;

    /* Legacy VGA path: full classic boot animation */

    /*
     * Make the kernel resource section temporarily writable,
     * as we are going to change the bitmaps' palette in place.
     */
    MmChangeKernelResourceSectionProtection(MM_READWRITE);

    /* Load boot screen logo */
    BootLogo = InbvGetResourceAddress(IDB_LOGO_DEFAULT);

    /* Make sure we have a logo */
    if (BootLogo)
    {
        /* Save the main image palette for implementing the fade-in effect */
        PBITMAPINFOHEADER BitmapInfoHeader = BootLogo;
        LPRGBQUAD Palette = (LPRGBQUAD)((PUCHAR)BootLogo + BitmapInfoHeader->biSize);
        RtlCopyMemory(MainPalette, Palette, sizeof(MainPalette));

        /* Draw the logo at the center of the screen */
        BitBltAligned(BootLogo,
                      TRUE,
                      AL_HORIZONTAL_CENTER,
                      AL_VERTICAL_CENTER,
                      0, 0, 0, 34);
    }

    /* Load and draw copyright text bitmap */
    BootCopy = InbvGetResourceAddress(IDB_COPYRIGHT);
    BitBltAligned(BootCopy,
                  TRUE,
                  AL_HORIZONTAL_LEFT,
                  AL_VERTICAL_BOTTOM,
                  22, 0, 0, 20);

    /* Restore the kernel resource section protection to be read-only */
    MmChangeKernelResourceSectionProtection(MM_READONLY);

    /* Display the boot logo and fade it in */
    BootLogoFadeIn();
}

CODE_SEG("INIT")
VOID
NTAPI
FinalizeBootLogo(VOID)
{
    VID_DISPLAY_INFO DisplayInfo;

    InbvQueryDisplayInfo(&DisplayInfo);

    /*
     * Leave whatever is on screen alone: the boot logo stays up until the
     * display driver takes the framebuffer over and InbvNotifyDisplayOwnershipLost
     * fades it out.
     */
    InbvGopLogoComplete();

    InbvAcquireLock();
    if (!InbvBootFadeEnabled && InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED)
    {
        VidSolidColorFill(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1, BV_COLOR_BLACK);
    }

    InbvReleaseLock();
}

VOID
NTAPI
DisplayShutdownBitmap(VOID)
{
    PUCHAR Logo1, Logo2;
    VID_DISPLAY_INFO DisplayInfo;
    ULONG CanvasLeft, CanvasTop;

    InbvQueryDisplayInfo(&DisplayInfo);
    CanvasLeft = (DisplayInfo.Width - SCREEN_WIDTH) / 2;
    CanvasTop = (DisplayInfo.Height - SCREEN_HEIGHT) / 2;

    /* Yes we do, cleanup for shutdown screen */
    // InbvResetDisplay();
    InbvInstallDisplayStringFilter(NULL);
    InbvEnableDisplayString(TRUE);
    InbvSolidColorFill(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1, BV_COLOR_BLACK);
    InbvSetScrollRegion(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1);

    /* Display shutdown logo and message */
    Logo1 = InbvGetResourceAddress(IDB_SHUTDOWN_MSG);
    Logo2 = InbvGetResourceAddress(IDB_LOGO_DEFAULT);

    if (Logo1 && Logo2)
    {
        InbvBitBlt(Logo1, CanvasLeft + VID_SHUTDOWN_MSG_LEFT, CanvasTop + VID_SHUTDOWN_MSG_TOP);
        InbvBitBlt(Logo2, CanvasLeft + VID_SHUTDOWN_LOGO_LEFT, CanvasTop + VID_SHUTDOWN_LOGO_TOP);
    }
}

VOID
NTAPI
DisplayShutdownText(VOID)
{
    static const CHAR Message[] = "The system may be powered off now.";
    CHAR Padding[81];
    VID_DISPLAY_INFO DisplayInfo;
    ULONG Columns, Rows, PaddingLength, Count, i;

    InbvQueryDisplayInfo(&DisplayInfo);
    Columns = DisplayInfo.CharacterWidth ? DisplayInfo.Width / DisplayInfo.CharacterWidth : 80;
    Rows = DisplayInfo.CharacterHeight ? DisplayInfo.Height / DisplayInfo.CharacterHeight : 34;

    for (i = 0; i < (Rows > 2 ? (Rows - 2) / 2 : 0); ++i)
        InbvDisplayString("\r\n");

    PaddingLength = (Columns > sizeof(Message) - 1) ? (Columns - (sizeof(Message) - 1)) / 2 : 0;
    RtlFillMemory(Padding, sizeof(Padding) - 1, ' ');
    while (PaddingLength)
    {
        Count = min(PaddingLength, (ULONG)sizeof(Padding) - 1);
        Padding[Count] = ANSI_NULL;
        InbvDisplayString(Padding);
        PaddingLength -= Count;
    }
    InbvDisplayString(Message);
    InbvDisplayString("\r\n");
}
