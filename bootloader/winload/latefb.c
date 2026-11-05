/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Last-resort screen output after ExitBootServices()
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 *
 * The firmware console dies with ExitBootServices(), and a machine may have
 * neither a firmware serial port nor a network stack to send the debug log to.
 * This module keeps a small text console that writes straight into the GOP
 * framebuffer, which stays visible on the screen, and also stays mapped for
 * the kernel through the KSEG0 window the loader builds here.
 */

#include "winload.h"
#include <bootfb.h>

extern ULONG_PTR VramAddress;
extern ULONG VramSize;

static BOOTFB_CONSOLE FwLateConsole;
static BOOLEAN FwLateScreenReady;

VOID
FwLateScreenPutChar(
    _In_ UCHAR Character);

VOID
FwLateScreenInitialize(
    _Inout_ PLOADER_PARAMETER_EXTENSION Extension)
{
    PCM_FRAMEBUF_DEVICE_DATA Buffer = FrameBufferData;
    ULONGLONG Base, Visible;
    ULONG Size;
    ULONG Scale;

    if (FwLateScreenReady || !Buffer)
        return;

    Base = (ULONGLONG)VramAddress;
    Size = VramSize;

    if (Buffer->BitsPerPixel != 32 ||
        Buffer->ScreenWidth == 0 ||
        Buffer->ScreenHeight == 0 ||
        Buffer->PixelsPerScanLine < Buffer->ScreenWidth ||
        (ULONGLONG)Buffer->PixelsPerScanLine * Buffer->ScreenHeight * 4 > Size)
    {
        FwPrint(L"The framebuffer has no usable 32bpp linear layout; no early console.\r\n");
        return;
    }

    Base += Buffer->FrameBufferOffset;
    if (Base == 0)
        return;

    /* Only the visible part of the framebuffer is ever touched */
    Visible = (ULONGLONG)Buffer->PixelsPerScanLine * Buffer->ScreenHeight * 4;
    if (Visible < Size)
        Size = (ULONG)Visible;

    /* Map it 1:1 and through KSEG0, where the kernel keeps it */
    if (!MempSetupPaging((PFN_NUMBER)(Base >> MM_PAGE_SHIFT),
                         MM_SIZE_TO_PAGES((SIZE_T)Size),
                         TRUE))
    {
        FwPrint(L"The framebuffer could not be mapped; no early console.\r\n");
        return;
    }

    Scale = (Buffer->ScreenHeight >= 1000) ? 2 : 1;

    FwLateConsole.Base = (PUCHAR)(ULONG_PTR)Base;
    FwLateConsole.Width = Buffer->ScreenWidth;
    FwLateConsole.Height = Buffer->ScreenHeight;
    FwLateConsole.Pitch = Buffer->PixelsPerScanLine;
    FwLateConsole.LineBytes = Buffer->PixelsPerScanLine * 4;
    FwLateConsole.Scale = Scale;
    FwLateConsole.CellWidth = Scale * BOOTFB_FONT_WIDTH;
    FwLateConsole.CellHeight = Scale * BOOTFB_FONT_HEIGHT;
    FwLateConsole.Columns = Buffer->ScreenWidth / FwLateConsole.CellWidth;
    FwLateConsole.Rows = Buffer->ScreenHeight / FwLateConsole.CellHeight;
    FwLateConsole.Enabled = (FwLateConsole.Columns != 0 && FwLateConsole.Rows != 0);

    FwLateScreenReady = FwLateConsole.Enabled;
    if (FwLateScreenReady)
        FwLateScreenWriter = FwLateScreenPutChar;

    BootFbPrint(&FwLateConsole,
                "\r\nLiberNT: framebuffer console on, size ");
    {
        CHAR Text[32];
        ULONG i = 0, Width = Buffer->ScreenWidth, Height = Buffer->ScreenHeight;

        Text[i++] = (CHAR)('0' + (Width / 1000) % 10);
        Text[i++] = (CHAR)('0' + (Width / 100) % 10);
        Text[i++] = (CHAR)('0' + (Width / 10) % 10);
        Text[i++] = (CHAR)('0' + Width % 10);
        Text[i++] = 'x';
        Text[i++] = (CHAR)('0' + (Height / 1000) % 10);
        Text[i++] = (CHAR)('0' + (Height / 100) % 10);
        Text[i++] = (CHAR)('0' + (Height / 10) % 10);
        Text[i++] = (CHAR)('0' + Height % 10);
        Text[i] = ANSI_NULL;
        BootFbPrint(&FwLateConsole, Text);
    }
    BootFbPrint(&FwLateConsole, "\r\n");

#ifdef KSEG0_BASE
    Extension->EarlyConsole.Magic = LOADER_EARLY_CONSOLE_MAGIC;
    Extension->EarlyConsole.Size = sizeof(LOADER_PARAMETER_EARLY_CONSOLE);
    Extension->EarlyConsole.Base.QuadPart = (ULONG64)KSEG0_BASE + Base;
    Extension->EarlyConsole.Width = Buffer->ScreenWidth;
    Extension->EarlyConsole.Height = Buffer->ScreenHeight;
    Extension->EarlyConsole.Pitch = Buffer->PixelsPerScanLine;
    Extension->EarlyConsole.BitsPerPixel = Buffer->BitsPerPixel;
#else
    UNREFERENCED_PARAMETER(Extension);
#endif
}

VOID
FwLateScreenPutChar(
    _In_ UCHAR Character)
{
    if (FwLateScreenReady)
        BootFbPutChar(&FwLateConsole, Character);
}
