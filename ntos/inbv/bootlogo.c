/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the screen shows while the system comes up and goes down
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "inbvgop.h"
#include "logo.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

static const CHAR InbvShutdownText[] =
    "\r\nIt is now safe to turn off your computer.\r\n";

/* PRIVATE FUNCTIONS **********************************************************/

/* How wide and how tall the bitmap in a resource is, if it is one we can draw */
static
BOOLEAN
InbvBitmapExtent(
    _In_ PUCHAR Bitmap,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    PBITMAPINFOHEADER Header = (PBITMAPINFOHEADER)Bitmap;

    *Width = 0;
    *Height = 0;

    if (Bitmap == NULL) return FALSE;
    if (Header->biSize < sizeof(BITMAPINFOHEADER)) return FALSE;
    if (Header->biWidth <= 0) return FALSE;

    *Width = (ULONG)Header->biWidth;

    *Height = (Header->biHeight < 0) ? (ULONG)(-Header->biHeight)
                                     : (ULONG)Header->biHeight;

    return *Height != 0;
}

/* Draws a bitmap resource with its centre on the point the caller names */
static
BOOLEAN
InbvDrawResourceCentered(
    _In_ ULONG ResourceNumber,
    _In_ ULONG CenterX,
    _In_ ULONG CenterY)
{
    PUCHAR Bitmap = InbvGetResourceAddress(ResourceNumber);
    ULONG Width;
    ULONG Height;
    ULONG Left;
    ULONG Top;

    if (!InbvBitmapExtent(Bitmap, &Width, &Height)) return FALSE;

    Left = (CenterX > Width / 2) ? (CenterX - Width / 2) : 0;
    Top = (CenterY > Height / 2) ? (CenterY - Height / 2) : 0;

    InbvBitBlt(Bitmap, Left, Top);
    return TRUE;
}

/* Draws a bitmap resource with its top left corner where the caller says */
static
BOOLEAN
InbvDrawResourceAt(
    _In_ ULONG ResourceNumber,
    _In_ ULONG Left,
    _In_ ULONG Top)
{
    PUCHAR Bitmap = InbvGetResourceAddress(ResourceNumber);
    ULONG Width;
    ULONG Height;

    if (!InbvBitmapExtent(Bitmap, &Width, &Height)) return FALSE;

    InbvBitBlt(Bitmap, Left, Top);
    return TRUE;
}

/* Takes the display, clears it and gives back what size it turned out to be */
static
BOOLEAN
InbvClaimScreen(
    _Out_ PVID_DISPLAY_INFO DisplayInfo)
{
    if (!InbvIsBootDriverInstalled()) return FALSE;

    if (InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED)
    {
        InbvAcquireDisplayOwnership();
    }

    if (!InbvQueryDisplayInfo(DisplayInfo)) return FALSE;
    if (DisplayInfo->Width == 0 || DisplayInfo->Height == 0) return FALSE;

    InbvResetDisplay();
    InbvSolidColorFill(0,
                       0,
                       DisplayInfo->Width - 1,
                       DisplayInfo->Height - 1,
                       BV_COLOR_BLACK);

    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
DisplayBootBitmap(VOID)
{
    VID_DISPLAY_INFO DisplayInfo;

    if (InbvGopHandleBootBitmap()) return;

    if (!InbvClaimScreen(&DisplayInfo)) return;

    InbvEnableDisplayString(FALSE);

    if (!InbvDrawResourceCentered(IDB_LOGO_DEFAULT,
                                  DisplayInfo.Width / 2,
                                  DisplayInfo.Height / 2))
    {
        InbvDrawResourceCentered(IDB_BOOT_SCREEN,
                                 DisplayInfo.Width / 2,
                                 DisplayInfo.Height / 2);
    }

    InbvDrawResourceCentered(IDB_COPYRIGHT,
                             DisplayInfo.Width / 2,
                             DisplayInfo.Height - DisplayInfo.Height / 8);

    InbvSetTextColor(BV_COLOR_WHITE);
    InbvSetScrollRegion(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1);
}

CODE_SEG("INIT")
VOID
NTAPI
FinalizeBootLogo(VOID)
{
    VID_DISPLAY_INFO DisplayInfo;

    InbvGopLogoComplete();

    if (!InbvIsBootDriverInstalled()) return;

    if (InbvGetDisplayState() == INBV_DISPLAY_STATE_OWNED &&
        InbvQueryDisplayInfo(&DisplayInfo) &&
        DisplayInfo.Width != 0 && DisplayInfo.Height != 0)
    {
        InbvSolidColorFill(0,
                           0,
                           DisplayInfo.Width - 1,
                           DisplayInfo.Height - 1,
                           BV_COLOR_BLACK);
    }

    InbvEnableDisplayString(FALSE);
}

VOID
NTAPI
DisplayShutdownBitmap(VOID)
{
    VID_DISPLAY_INFO DisplayInfo;

    InbvGopLogoStop();

    if (!InbvClaimScreen(&DisplayInfo)) return;

    InbvEnableDisplayString(FALSE);

    if (DisplayInfo.Width == SCREEN_WIDTH && DisplayInfo.Height == SCREEN_HEIGHT)
    {
        InbvDrawResourceAt(IDB_LOGO_DEFAULT,
                           VID_SHUTDOWN_LOGO_LEFT,
                           VID_SHUTDOWN_LOGO_TOP);
        InbvDrawResourceAt(IDB_SHUTDOWN_MSG,
                           VID_SHUTDOWN_MSG_LEFT,
                           VID_SHUTDOWN_MSG_TOP);
        return;
    }

    InbvDrawResourceCentered(IDB_LOGO_DEFAULT,
                             DisplayInfo.Width / 2,
                             DisplayInfo.Height / 3);
    InbvDrawResourceCentered(IDB_SHUTDOWN_MSG,
                             DisplayInfo.Width / 2,
                             DisplayInfo.Height - DisplayInfo.Height / 4);
}

VOID
NTAPI
DisplayShutdownText(VOID)
{
    VID_DISPLAY_INFO DisplayInfo;

    InbvGopLogoStop();

    if (!InbvClaimScreen(&DisplayInfo)) return;

    InbvSetTextColor(BV_COLOR_WHITE);
    InbvSetScrollRegion(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1);
    InbvInstallDisplayStringFilter(NULL);
    InbvEnableDisplayString(TRUE);
    InbvDisplayString(InbvShutdownText);
}

/* EOF */
