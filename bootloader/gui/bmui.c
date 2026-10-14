/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot manager screens: operating system menu and startup settings
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <bmui.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "bootui_mono.h"

#define BMUI_GRAPHICS_COLUMNS   100
#define BMUI_TEXT_COLUMNS       80
#define BMUI_ROWS               25
#define BMUI_MONO_EM_NUMERATOR  2
#define BMUI_MONO_EM_DENOMINATOR 3
#define BMUI_BLACK              0x000000
#define BMUI_GRAY               0xBFBFBF
#define BMUI_WHITE              0xFFFFFF
#define BMUI_POLL_MICROSECONDS  10000
#define BMUI_POLLS_PER_SECOND   100
#define BMUI_TEXT_CHARS         128

#define BMUI_ROW_TITLE          0
#define BMUI_ROW_HEADING        2
#define BMUI_ROW_HINT           3
#define BMUI_ROW_ENTRIES        5
#define BMUI_ROW_ADVANCED       16
#define BMUI_ROW_COUNTDOWN      17
#define BMUI_ROW_FOOTER         24
#define BMUI_COLUMN_ENTRY       6
#define BMUI_ENTRY_RIGHT_MARGIN 10
#define BMUI_MAX_ENTRIES        (BMUI_ROW_ADVANCED - BMUI_ROW_ENTRIES - 1)

static ULONG BmUiWidth;
static ULONG BmUiHeight;
static ULONG BmUiColumns;
static ULONG BmUiCellWidth;
static ULONG BmUiCellHeight;
static PULONG BmUiPixels;
static SIZE_T BmUiPixelBytes;
static FT_Library BmUiLibrary;
static FT_Face BmUiMono;
static BOOLEAN BmUiGraphics;

static
VOID
BmUiFill(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Color)
{
    ULONG X, Y;

    for (Y = Top; Y < min(Top + Height, BmUiHeight); Y++)
    {
        for (X = Left; X < min(Left + Width, BmUiWidth); X++)
            BmUiPixels[(SIZE_T)Y * BmUiWidth + X] = Color;
    }
}

static
ULONG
BmUiBlend(
    _In_ ULONG Background,
    _In_ ULONG Foreground,
    _In_ ULONG Alpha)
{
    ULONG Result = 0;
    ULONG Shift, Back, Fore;

    for (Shift = 0; Shift < 24; Shift += 8)
    {
        Back = (Background >> Shift) & 0xFF;
        Fore = (Foreground >> Shift) & 0xFF;
        Result |= ((Back * (255 - Alpha) + Fore * Alpha) / 255) << Shift;
    }

    return Result;
}

static
ULONG
BmUiDrawText(
    _In_ FT_Face Face,
    _In_ LONG X,
    _In_ LONG Baseline,
    _In_ PCWSTR Text,
    _In_ ULONG Color)
{
    FT_GlyphSlot Slot = Face->glyph;
    LONG Pen = X;
    LONG PixelX, PixelY;
    ULONG Row, Column, Alpha;
    PULONG Pixel;

    for (; *Text != UNICODE_NULL; Text++)
    {
        if (FT_Load_Char(Face, *Text, FT_LOAD_RENDER) != FT_Err_Ok)
            continue;

        for (Row = 0; Row < Slot->bitmap.rows; Row++)
        {
            PixelY = Baseline - Slot->bitmap_top + (LONG)Row;
            if (PixelY < 0 || PixelY >= (LONG)BmUiHeight)
                continue;

            for (Column = 0; Column < Slot->bitmap.width; Column++)
            {
                PixelX = Pen + Slot->bitmap_left + (LONG)Column;
                Alpha = Slot->bitmap.buffer[Row * Slot->bitmap.pitch + Column];
                if (Alpha == 0 || PixelX < 0 || PixelX >= (LONG)BmUiWidth)
                    continue;

                Pixel = &BmUiPixels[(SIZE_T)PixelY * BmUiWidth + PixelX];
                *Pixel = BmUiBlend(*Pixel, Color, Alpha);
            }
        }

        Pen += Slot->advance.x >> 6;
    }

    return (ULONG)(Pen - X);
}

static
VOID
BmUiPresent(
    _In_ ULONG Top,
    _In_ ULONG Height)
{
    if (BmUiGraphics)
        VidFbPresent(BmUiPixels, 0, Top, BmUiWidth, min(Height, BmUiHeight - Top));
}

static
VOID
BmUiAttrColors(
    _In_ UCHAR Attr,
    _Out_ PULONG Foreground,
    _Out_ PULONG Background)
{
    *Background = (Attr == BMUI_ATTR_INVERSE) ? BMUI_GRAY : BMUI_BLACK;
    *Foreground = (Attr == BMUI_ATTR_INVERSE) ? BMUI_BLACK :
                  (Attr == BMUI_ATTR_BRIGHT) ? BMUI_WHITE : BMUI_GRAY;
}

static
ULONG
BmUiColumnX(
    _In_ ULONG Column)
{
    return (ULONG)((ULONGLONG)Column * BmUiWidth / BmUiColumns);
}

static
ULONG
BmUiRowY(
    _In_ ULONG Row)
{
    return (ULONG)((ULONGLONG)Row * BmUiHeight / BMUI_ROWS);
}

static
VOID
BmUiPutText(
    _In_ ULONG Row,
    _In_ ULONG Column,
    _In_ ULONG Width,
    _In_ PCWSTR Text,
    _In_ UCHAR Attr)
{
    WCHAR Line[BMUI_TEXT_CHARS];
    ULONG Foreground, Background, i;

    Width = min(Width, BmUiColumns - min(Column, BmUiColumns));
    Width = min(Width, BMUI_TEXT_CHARS - 1);

    for (i = 0; i < Width; i++)
        Line[i] = (Text && *Text) ? *Text++ : L' ';
    Line[Width] = UNICODE_NULL;

    if (!BmUiGraphics)
    {
        for (i = 0; i < Width; i++)
            FbConsPutChar(Line[i] <= 0x7F ? (UCHAR)Line[i] : '?', Attr, Column + i, Row);
        return;
    }

    BmUiAttrColors(Attr, &Foreground, &Background);
    BmUiFill(BmUiColumnX(Column),
             BmUiRowY(Row),
             BmUiColumnX(Column + Width) - BmUiColumnX(Column),
             BmUiRowY(Row + 1) - BmUiRowY(Row),
             Background);

    for (i = 0; i < Width; i++)
    {
        WCHAR Glyph[2] = {Line[i], UNICODE_NULL};

        if (Glyph[0] == L' ')
            continue;

        BmUiDrawText(BmUiMono,
                     (LONG)BmUiColumnX(Column + i),
                     (LONG)BmUiRowY(Row) +
                         (LONG)(BmUiCellHeight + (BmUiMono->size->metrics.ascender >> 6) +
                                (BmUiMono->size->metrics.descender >> 6)) / 2,
                     Glyph,
                     Foreground);
    }
}

static
VOID
BmUiPutRow(
    _In_ ULONG Row,
    _In_ PCWSTR Text,
    _In_ UCHAR Attr)
{
    BmUiPutText(Row, 0, BmUiColumns, Text, Attr);
}

static
VOID
BmUiClear(
    _In_ ULONG Color,
    _In_ UCHAR Attr)
{
    if (BmUiGraphics)
        BmUiFill(0, 0, BmUiWidth, BmUiHeight, Color);
    else
        FbConsClearScreen(Attr);
}

static
int
BmUiWaitKey(
    _Inout_opt_ PLONG Seconds)
{
    ULONG Poll;

    if (!Seconds || *Seconds < 0)
        return FwReadKey();

    for (Poll = 0; Poll < BMUI_POLLS_PER_SECOND; Poll++)
    {
        if (FwKeyAvailable())
            return FwReadKey();

        StallExecutionProcessor(BMUI_POLL_MICROSECONDS);
    }

    (*Seconds)--;
    return -1;
}

static
int
BmUiReadKey(
    _Inout_opt_ PLONG Seconds,
    _Out_ PBOOLEAN Extended)
{
    int Key = BmUiWaitKey(Seconds);

    *Extended = (Key == KEY_EXTENDED);
    return *Extended ? FwReadKey() : Key;
}

BOOLEAN
BmUiWaitForKey(
    _In_ LONG Seconds)
{
    BOOLEAN Extended;

    while (Seconds > 0)
    {
        if (BmUiReadKey(&Seconds, &Extended) >= 0)
            return TRUE;
    }

    return FALSE;
}

BOOLEAN
BmUiInitialize(VOID)
{
    ULONG Depth;

    VidFbGetDisplaySize(&BmUiWidth, &BmUiHeight, &Depth);
    BmUiGraphics = FALSE;
    BmUiColumns = BMUI_TEXT_COLUMNS;

    if (BmUiWidth == 0 || BmUiHeight == 0)
        return TRUE;

    BmUiPixelBytes = (SIZE_T)BmUiWidth * BmUiHeight * sizeof(ULONG);
    BmUiPixels = FwAllocatePages(BmUiPixelBytes);
    if (!BmUiPixels)
        return TRUE;

    if (!BmUiLibrary &&
        (FT_Init_FreeType(&BmUiLibrary) != FT_Err_Ok ||
         FT_New_Memory_Face(BmUiLibrary, bootui_mono, bootui_mono_SIZE, 0, &BmUiMono) != FT_Err_Ok))
    {
        FwFreePages(BmUiPixels, BmUiPixelBytes);
        BmUiPixels = NULL;
        return TRUE;
    }

    BmUiColumns = BMUI_GRAPHICS_COLUMNS;
    BmUiCellWidth = BmUiWidth / BmUiColumns;
    BmUiCellHeight = BmUiHeight / BMUI_ROWS;
    BmUiGraphics = (BmUiCellWidth != 0 && BmUiCellHeight != 0);
    return TRUE;
}

VOID
BmUiShutdown(VOID)
{
    BmUiClear(BMUI_BLACK, BMUI_ATTR_NORMAL);
    BmUiPresent(0, BmUiHeight);

    if (BmUiPixels)
        FwFreePages(BmUiPixels, BmUiPixelBytes);

    BmUiPixels = NULL;
    BmUiGraphics = FALSE;
}

static
VOID
BmUiSetMonoSize(VOID)
{
    FT_F26Dot6 Height = (FT_F26Dot6)BmUiCellHeight * BMUI_MONO_EM_NUMERATOR * 64 / BMUI_MONO_EM_DENOMINATOR;
    FT_F26Dot6 Width = (FT_F26Dot6)BmUiCellWidth * BmUiMono->units_per_EM * 64 / BmUiMono->max_advance_width;

    FT_Set_Char_Size(BmUiMono, Width, Height, 72, 72);
}

static
VOID
BmUiDrawEntries(
    _In_reads_(Count) PCWSTR* Names,
    _In_ ULONG Count,
    _In_ ULONG Selected)
{
    WCHAR Text[BMUI_TEXT_CHARS];
    ULONG Width = BmUiColumns - BMUI_COLUMN_ENTRY - BMUI_ENTRY_RIGHT_MARGIN;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        RtlStringCchCopyW(Text, RTL_NUMBER_OF(Text), Names[i]);
        BmUiPutText(BMUI_ROW_ENTRIES + i,
                    BMUI_COLUMN_ENTRY,
                    Width,
                    Text,
                    (i == Selected) ? BMUI_ATTR_INVERSE : BMUI_ATTR_NORMAL);
        if (i == Selected)
            BmUiPutText(BMUI_ROW_ENTRIES + i, BMUI_COLUMN_ENTRY + Width - 1, 1, L">", BMUI_ATTR_INVERSE);
    }
}

static
VOID
BmUiDrawFrame(
    _In_ PCWSTR Title,
    _In_ PCWSTR Heading,
    _In_ PCWSTR Escape)
{
    WCHAR Footer[BMUI_TEXT_CHARS];
    ULONG TitleColumn, Length, i;

    if (BmUiGraphics)
        BmUiSetMonoSize();

    BmUiClear(BMUI_BLACK, BMUI_ATTR_NORMAL);
    TitleColumn = (BmUiColumns - min((ULONG)wcslen(Title), BmUiColumns)) / 2;
    BmUiPutRow(BMUI_ROW_TITLE, NULL, BMUI_ATTR_INVERSE);
    BmUiPutText(BMUI_ROW_TITLE, TitleColumn, BmUiColumns - TitleColumn, Title, BMUI_ATTR_INVERSE);
    BmUiPutRow(BMUI_ROW_HEADING, Heading, BMUI_ATTR_BRIGHT);
    BmUiPutRow(BMUI_ROW_HINT, L"(Use the arrow keys to highlight your choice, then press ENTER.)", BMUI_ATTR_NORMAL);

    for (i = 0; i < BmUiColumns; i++)
        Footer[i] = L' ';
    Footer[BmUiColumns] = UNICODE_NULL;
    RtlCopyMemory(Footer, L"ENTER=Choose", sizeof(L"ENTER=Choose") - sizeof(WCHAR));
    Length = (ULONG)wcslen(Escape);
    RtlCopyMemory(&Footer[BmUiColumns - Length], Escape, Length * sizeof(WCHAR));
    BmUiPutRow(BMUI_ROW_FOOTER, Footer, BMUI_ATTR_INVERSE);
}

static
VOID
BmUiMoveSelection(
    _In_ int Key,
    _In_ ULONG Count,
    _Inout_ PULONG Selected)
{
    if (Key == KEY_UP && *Selected > 0)
        (*Selected)--;
    else if (Key == KEY_DOWN && *Selected + 1 < Count)
        (*Selected)++;
    else if (Key == KEY_HOME)
        *Selected = 0;
    else if (Key == KEY_END)
        *Selected = Count - 1;
}

BM_UI_ACTION
BmUiChooseEntry(
    _In_ PCWSTR Title,
    _In_reads_(Count) PCWSTR* Names,
    _In_ ULONG Count,
    _Inout_ PULONG Selected,
    _In_ LONG Timeout)
{
    WCHAR Text[BMUI_TEXT_CHARS];
    BOOLEAN Extended;
    int Key;

    Count = min(Count, BMUI_MAX_ENTRIES);
    if (*Selected >= Count)
        *Selected = 0;

    BmUiDrawFrame(Title, L"Choose an operating system to start:", L"ESC=Exit");
    BmUiPutRow(BMUI_ROW_ADVANCED, L"To choose a startup option for this choice, press F8.", BMUI_ATTR_NORMAL);

    for (;;)
    {
        BmUiDrawEntries(Names, Count, *Selected);

        if (Timeout >= 0)
        {
            RtlStringCchPrintfW(Text,
                                RTL_NUMBER_OF(Text),
                                L"Seconds until the highlighted choice will be started automatically: %ld",
                                Timeout);
            BmUiPutRow(BMUI_ROW_COUNTDOWN, Text, BMUI_ATTR_NORMAL);
        }

        BmUiPresent(0, BmUiHeight);

        if (Timeout == 0)
            return BmUiActionStart;

        Key = BmUiReadKey(&Timeout, &Extended);
        if (Key < 0)
            continue;

        if (Timeout >= 0)
        {
            Timeout = -1;
            BmUiPutRow(BMUI_ROW_COUNTDOWN, NULL, BMUI_ATTR_NORMAL);
        }

        if (Key == KEY_ENTER)
            return BmUiActionStart;

        if (Key == KEY_ESC)
            return BmUiActionExit;

        if (!Extended)
            continue;

        if (Key == KEY_F8)
            return BmUiActionAdvanced;

        BmUiMoveSelection(Key, Count, Selected);
    }
}

BOOLEAN
BmUiStartupSettings(
    _In_reads_(Count) PCSTR* Names,
    _In_ ULONG Count,
    _Out_ PULONG Choice)
{
    WCHAR Text[BMUI_MAX_ENTRIES][BMUI_TEXT_CHARS];
    PCWSTR Lines[BMUI_MAX_ENTRIES];
    BOOLEAN Extended;
    ULONG i, j;
    int Key;

    Count = min(Count, BMUI_MAX_ENTRIES);
    for (i = 0; i < Count; i++)
    {
        for (j = 0; Names[i][j] != ANSI_NULL && j < BMUI_TEXT_CHARS - 1; j++)
            Text[i][j] = (UCHAR)Names[i][j];
        Text[i][j] = UNICODE_NULL;
        Lines[i] = Text[i];
    }

    *Choice = 0;
    BmUiDrawFrame(L"Startup options", L"Choose a startup option:", L"ESC=Back");

    for (;;)
    {
        BmUiDrawEntries(Lines, Count, *Choice);
        BmUiPresent(0, BmUiHeight);

        Key = BmUiReadKey(NULL, &Extended);

        if (!Extended && Key == KEY_ENTER)
            return TRUE;

        if (!Extended && Key == KEY_ESC)
            return FALSE;

        if (Extended)
            BmUiMoveSelection(Key, Count, Choice);
    }
}
