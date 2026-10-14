/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UEFI keyboard, display and text console for the boot menu
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>
#include <gui.h>

#define FW_COLOR_DEPTH     32
#define FW_BACKGROUND_MASK 0x7
#define FW_CELL_WIDTH      8
#define FW_CELL_HEIGHT     16
#define FW_IDLE_STALL_US   1000

static EFI_GUID FwGopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GRAPHICS_OUTPUT_PROTOCOL* FwGop;
static EFI_INPUT_KEY FwPendingKey;
static BOOLEAN FwHasPendingKey;
static int FwPendingExtended;

static
EFI_GRAPHICS_OUTPUT_PROTOCOL*
FwGetGop(VOID)
{
    if (!FwGop &&
        EFI_ERROR(GlobalSystemTable->BootServices->LocateProtocol(&FwGopGuid, NULL, (VOID**)&FwGop)))
    {
        FwGop = NULL;
    }

    return (FwGop && FwGop->Mode && FwGop->Mode->Info) ? FwGop : NULL;
}

BOOLEAN
FwKeyAvailable(VOID)
{
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn = GlobalSystemTable->ConIn;

    if (FwHasPendingKey || FwPendingExtended)
        return TRUE;

    if (!ConIn || EFI_ERROR(ConIn->ReadKeyStroke(ConIn, &FwPendingKey)))
        return FALSE;

    FwHasPendingKey = TRUE;
    return TRUE;
}

static
int
FwTranslateScanCode(
    _In_ UINT16 ScanCode)
{
    if (ScanCode >= SCAN_F1 && ScanCode <= SCAN_F10)
        return KEY_F1 + (ScanCode - SCAN_F1);

    switch (ScanCode)
    {
        case SCAN_UP:     return KEY_UP;
        case SCAN_DOWN:   return KEY_DOWN;
        case SCAN_LEFT:   return KEY_LEFT;
        case SCAN_RIGHT:  return KEY_RIGHT;
        case SCAN_HOME:   return KEY_HOME;
        case SCAN_END:    return KEY_END;
        case SCAN_DELETE: return KEY_DELETE;
    }

    return 0;
}

int
FwReadKey(VOID)
{
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn = GlobalSystemTable->ConIn;
    UINTN Index;
    int Code;

    for (;;)
    {
        if (FwPendingExtended)
        {
            Code = FwPendingExtended;
            FwPendingExtended = 0;
            return Code;
        }

        while (!FwKeyAvailable())
        {
            if (!ConIn)
                return KEY_ENTER;

            GlobalSystemTable->BootServices->WaitForEvent(1, &ConIn->WaitForKey, &Index);
        }

        FwHasPendingKey = FALSE;

        if (FwPendingKey.UnicodeChar != 0)
            return FwPendingKey.UnicodeChar & 0xFF;

        if (FwPendingKey.ScanCode == SCAN_ESC)
            return KEY_ESC;

        FwPendingExtended = FwTranslateScanCode(FwPendingKey.ScanCode);
        if (FwPendingExtended)
            return KEY_EXTENDED;
    }
}

VOID
VidFbGetDisplaySize(
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Depth)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop = FwGetGop();

    *Width = Gop ? Gop->Mode->Info->HorizontalResolution : 0;
    *Height = Gop ? Gop->Mode->Info->VerticalResolution : 0;
    *Depth = FW_COLOR_DEPTH;
}

VOID
VidFbPresent(
    _In_ const ULONG* Pixels,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop = FwGetGop();
    ULONG ScreenWidth, ScreenHeight;

    if (!Gop)
        return;

    ScreenWidth = Gop->Mode->Info->HorizontalResolution;
    ScreenHeight = Gop->Mode->Info->VerticalResolution;
    if (Left >= ScreenWidth || Top >= ScreenHeight)
        return;

    Width = min(Width, ScreenWidth - Left);
    Height = min(Height, ScreenHeight - Top);
    if (Width == 0 || Height == 0)
        return;

    Gop->Blt(Gop,
             (EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)Pixels,
             EfiBltBufferToVideo,
             Left,
             Top,
             Left,
             Top,
             Width,
             Height,
             ScreenWidth * sizeof(ULONG));
}

static
VOID
FwSetTextAttribute(
    _In_ UCHAR Attr)
{
    GlobalSystemTable->ConOut->SetAttribute(GlobalSystemTable->ConOut,
                                            (Attr & 0xF) | (((Attr >> 4) & FW_BACKGROUND_MASK) << 4));
}

VOID
FbConsClearScreen(
    _In_ UCHAR Attr)
{
    if (!GlobalSystemTable->ConOut)
        return;

    FwSetTextAttribute(Attr);
    GlobalSystemTable->ConOut->ClearScreen(GlobalSystemTable->ConOut);
}

VOID
FbConsPutChar(
    _In_ UCHAR Char,
    _In_ UCHAR Attr,
    _In_ ULONG Column,
    _In_ ULONG Row)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut = GlobalSystemTable->ConOut;
    CHAR16 Text[2] = {Char, 0};

    if (!ConOut)
        return;

    FwSetTextAttribute(Attr);
    if (!EFI_ERROR(ConOut->SetCursorPosition(ConOut, Column, Row)))
        ConOut->OutputString(ConOut, Text);
}

VOID
FbConsGetCellSize(
    _Out_ PULONG CellWidth,
    _Out_ PULONG CellHeight)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut = GlobalSystemTable->ConOut;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop = FwGetGop();
    UINTN Columns, Rows;

    *CellWidth = FW_CELL_WIDTH;
    *CellHeight = FW_CELL_HEIGHT;

    if (!Gop || !ConOut || !ConOut->Mode ||
        EFI_ERROR(ConOut->QueryMode(ConOut, ConOut->Mode->Mode, &Columns, &Rows)) ||
        Columns == 0 || Rows == 0)
    {
        return;
    }

    if (Gop->Mode->Info->HorizontalResolution / Columns != 0)
        *CellWidth = (ULONG)(Gop->Mode->Info->HorizontalResolution / Columns);
    if (Gop->Mode->Info->VerticalResolution / Rows != 0)
        *CellHeight = (ULONG)(Gop->Mode->Info->VerticalResolution / Rows);
}

VOID
FwVideoSync(VOID)
{
}

VOID
VideoFreeOffScreenBuffer(VOID)
{
}

VOID
FwIdle(VOID)
{
    GlobalSystemTable->BootServices->Stall(FW_IDLE_STALL_US);
}

VOID
Reboot(VOID)
{
    GlobalSystemTable->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
}
