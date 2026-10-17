/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     State and services the graphical boot menu depends on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <gui.h>
#include <drivers/acpi/acpi.h>

#define GUI_MESSAGE_CHARS     512
#define GUI_PROGRESS_FULL     100
#define GUI_PROGRESS_EXPECTED 25
#define GUI_BMP_SIGNATURE     0x4D42
#define GUI_BMP_RGB           0
#define GUI_BGRT_ORIENTATION  0x06

#pragma pack(push, 1)
typedef struct _GUI_BMP_FILE_HEADER
{
    USHORT Type;
    ULONG Size;
    USHORT Reserved1;
    USHORT Reserved2;
    ULONG BitsOffset;
} GUI_BMP_FILE_HEADER, *PGUI_BMP_FILE_HEADER;

typedef struct _GUI_BMP_INFO_HEADER
{
    ULONG Size;
    LONG Width;
    LONG Height;
    USHORT Planes;
    USHORT BitCount;
    ULONG Compression;
    ULONG SizeImage;
    LONG XPelsPerMeter;
    LONG YPelsPerMeter;
    ULONG ClrUsed;
    ULONG ClrImportant;
} GUI_BMP_INFO_HEADER, *PGUI_BMP_INFO_HEADER;
#pragma pack(pop)

C_ASSERT(sizeof(GUI_BMP_FILE_HEADER) == 14);
C_ASSERT(sizeof(GUI_BMP_INFO_HEADER) == 40);

ULONG UiScreenWidth;
ULONG UiScreenHeight;
BOOLEAN UiKeepFirmwareScreen;
UiMenuIdleCallback UiMenuIdle;
UI_PROGRESS_BAR UiProgressBar;
BOOLEAN UiShowTime;
BOOLEAN UiMenuBox;
BOOLEAN UiCenterMenu;
BOOLEAN UiUseSpecialEffects;
UCHAR UiBackdropFgColor;
UCHAR UiBackdropBgColor;
UCHAR UiBackdropFillStyle;
UCHAR UiStatusBarFgColor;
UCHAR UiStatusBarBgColor;
UCHAR UiTitleBoxFgColor;
UCHAR UiTitleBoxBgColor;
UCHAR UiMessageBoxFgColor;
UCHAR UiMessageBoxBgColor;
UCHAR UiMenuFgColor;
UCHAR UiMenuBgColor;
UCHAR UiTextColor;
UCHAR UiSelectedTextColor;
UCHAR UiSelectedTextBgColor;
UCHAR UiEditBoxTextColor;
UCHAR UiEditBoxBgColor;

TIMEINFO*
ArcGetTime(VOID)
{
    static TIMEINFO TimeInfo;
    TIME_FIELDS Time;

    if (FwGetTime(&Time))
    {
        TimeInfo.Year = Time.Year;
        TimeInfo.Month = Time.Month;
        TimeInfo.Day = Time.Day;
        TimeInfo.Hour = Time.Hour;
        TimeInfo.Minute = Time.Minute;
        TimeInfo.Second = Time.Second;
    }

    return &TimeInfo;
}

VOID
UiInitProgressBar(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ PCSTR ProgressText)
{
    UiProgressBar.Left = Left;
    UiProgressBar.Top = Top;
    UiProgressBar.Right = Right;
    UiProgressBar.Bottom = Bottom;
    UiProgressBar.State.Floor = 0;
    UiProgressBar.State.Bias = GUI_PROGRESS_FULL;
    UiProgressBar.Indicator.Count = 0;
    UiProgressBar.Indicator.Expected = GUI_PROGRESS_EXPECTED;
    UiProgressBar.Indicator.Percentage = 0;
    UiProgressBar.Show = TRUE;
    FbGuiVtbl.SetProgressBarText(ProgressText);
    FbGuiVtbl.TickProgressBar(0);
}

BOOLEAN
GuiInitialize(VOID)
{
    ULONG Width, Height, Depth, CellWidth, CellHeight;

    VidFbGetDisplaySize(&Width, &Height, &Depth);
    FbConsGetCellSize(&CellWidth, &CellHeight);
    if (Width == 0 || Height == 0 || CellWidth == 0 || CellHeight == 0)
        return FALSE;

    UiScreenWidth = Width / CellWidth;
    UiScreenHeight = Height / CellHeight;
    return FbGuiVtbl.Initialize();
}

BOOLEAN
UiDisplayMenu(
    IN PCSTR MenuHeader,
    IN PCSTR MenuFooter OPTIONAL,
    IN PCSTR MenuItemList[],
    IN ULONG MenuItemCount,
    IN ULONG DefaultMenuItem,
    IN LONG MenuTimeOut,
    OUT PULONG SelectedMenuItem,
    IN BOOLEAN CanEscape,
    IN UiMenuKeyPressFilterCallback KeyPressFilter OPTIONAL,
    IN PVOID Context OPTIONAL)
{
    return FbGuiVtbl.DisplayMenu(MenuHeader,
                                 MenuFooter,
                                 MenuItemList,
                                 MenuItemCount,
                                 DefaultMenuItem,
                                 MenuTimeOut,
                                 SelectedMenuItem,
                                 CanEscape,
                                 NULL,
                                 KeyPressFilter,
                                 Context);
}

VOID
UiMessageBox(
    _In_ PCSTR Format,
    ...)
{
    CHAR Text[GUI_MESSAGE_CHARS];
    va_list Arguments;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Text, sizeof(Text), Format, Arguments);
    va_end(Arguments);

    FbGuiVtbl.MessageBox(Text);
}

static
PVOID
GuiAcpiAddress(
    _In_ ULONGLONG Address)
{
    return (Address != 0 && Address == (ULONG_PTR)Address) ? (PVOID)(ULONG_PTR)Address : NULL;
}

static
PBGRT_TABLE
GuiFindBgrt(VOID)
{
    PRSDP Rsdp = FwGetAcpiRootPointer();
    PDESCRIPTION_HEADER Root, Table;
    ULONGLONG Address;
    ULONG Entry, Count, i;

    if (!Rsdp)
        return NULL;

    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress.QuadPart != 0)
    {
        Root = GuiAcpiAddress(Rsdp->XsdtAddress.QuadPart);
        Entry = sizeof(ULONGLONG);
    }
    else
    {
        Root = GuiAcpiAddress(Rsdp->RsdtAddress);
        Entry = sizeof(ULONG);
    }

    if (!Root || Root->Length < sizeof(*Root))
        return NULL;

    Count = (Root->Length - sizeof(*Root)) / Entry;
    for (i = 0; i < Count; i++)
    {
        if (Entry == sizeof(ULONGLONG))
            Address = ((PXSDT)Root)->Tables[i].QuadPart;
        else
            Address = ((PRSDT)Root)->Tables[i];

        Table = GuiAcpiAddress(Address);
        if (Table && Table->Signature == BGRT_SIGNATURE && Table->Length >= sizeof(BGRT_TABLE))
            return (PBGRT_TABLE)Table;
    }

    return NULL;
}

static
VOID
GuiDrawBgrt(
    _Inout_ PULONG Pixels,
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight)
{
    PBGRT_TABLE Bgrt = GuiFindBgrt();
    PGUI_BMP_FILE_HEADER File;
    PGUI_BMP_INFO_HEADER Info;
    PUCHAR Row;
    ULONG Width, Height, Bytes, Stride, X, Y;

    if (!Bgrt || Bgrt->ImageType != BgrtImageTypeBitmap || (Bgrt->Status & GUI_BGRT_ORIENTATION))
        return;

    File = GuiAcpiAddress(Bgrt->LogoAddress);
    if (!File || File->Type != GUI_BMP_SIGNATURE || File->Size < sizeof(*File) + sizeof(*Info))
        return;

    Info = (PGUI_BMP_INFO_HEADER)(File + 1);
    if (Info->Size < sizeof(*Info) || Info->Size > File->Size - sizeof(*File) ||
        Info->Planes != 1 || (Info->BitCount != 24 && Info->BitCount != 32) ||
        Info->Compression != GUI_BMP_RGB ||
        Info->Width <= 0 || (ULONG)Info->Width > ScreenWidth ||
        Info->Height == 0 || Info->Height > (LONG)ScreenHeight || Info->Height < -(LONG)ScreenHeight)
    {
        return;
    }

    Width = (ULONG)Info->Width;
    Height = (ULONG)(Info->Height < 0 ? -Info->Height : Info->Height);
    Bytes = Info->BitCount / 8;
    Stride = (Width * Bytes + 3) & ~3UL;

    if (Bgrt->OffsetX > ScreenWidth - Width || Bgrt->OffsetY > ScreenHeight - Height ||
        File->BitsOffset < sizeof(*File) + Info->Size || File->BitsOffset > File->Size ||
        (ULONGLONG)Stride * Height > File->Size - File->BitsOffset)
    {
        return;
    }

    for (Y = 0; Y < Height; Y++)
    {
        Row = (PUCHAR)File + File->BitsOffset + (SIZE_T)(Info->Height < 0 ? Y : Height - 1 - Y) * Stride;
        for (X = 0; X < Width; X++, Row += Bytes)
        {
            Pixels[(SIZE_T)(Bgrt->OffsetY + Y) * ScreenWidth + Bgrt->OffsetX + X] =
                ((ULONG)Row[2] << 16) | ((ULONG)Row[1] << 8) | Row[0];
        }
    }
}

VOID
GuiShowFirmwareLogo(VOID)
{
    ULONG Width, Height, Depth;
    PULONG Pixels;
    SIZE_T Size;

    VidFbGetDisplaySize(&Width, &Height, &Depth);
    if (Depth != 32 || Width == 0 || Height == 0 || Width > MAXULONG / sizeof(ULONG) / Height)
        return;

    Size = (SIZE_T)Width * Height * sizeof(ULONG);
    Pixels = FwAllocatePages(Size);
    if (!Pixels)
        return;

    RtlZeroMemory(Pixels, Size);
    GuiDrawBgrt(Pixels, Width, Height);
    VidFbPresent(Pixels, 0, 0, Width, Height);
    FwFreePages(Pixels, Size);
}
