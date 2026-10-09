/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Caption buttons drawn in extended window frames
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <reactos/dwmframe.h>

#include "framebuttons.h"

#define DWM_FRAME_BUTTON_CACHE 24
#define DWM_FRAME_BUTTON_MAX   256

typedef BOOL (WINAPI *PFN_DRAW_CAPTION_BUTTON)(HDC, const RECT *, UINT, ULONG, BOOL, BOOL);

typedef struct _DWM_FRAME_BUTTON
{
    ULONG Button;
    ULONG State;
    ULONG Width;
    ULONG Height;
    ULONG Background;
    ULONG Flags;
    ULONG Stamp;
    ULONG *Pixels;
} DWM_FRAME_BUTTON;

static DWM_FRAME_BUTTON g_frameButtons[DWM_FRAME_BUTTON_CACHE];
static ULONG g_frameButtonStamp;
static PFN_DRAW_CAPTION_BUTTON g_drawCaptionButton;
static BOOL g_drawCaptionButtonLoaded;

static ULONG
DwmFrameRgb(COLORREF Color)
{
    return ((Color & 0xFFu) << 16) | (Color & 0xFF00u) | ((Color >> 16) & 0xFFu);
}

ULONG
DwmFrameKey(const DWM_WIN *Window)
{
    BOOL Active = (Window->LayerFlags & DWM_WINDOW_ACTIVE) != 0;

    if (Window->BackdropType >= DWM_BACKDROP_MAIN &&
        Window->BackdropType <= DWM_BACKDROP_TABBED &&
        Window->BackdropRegion != 0)
        return DwmFrameRgb(Active ? Window->BackdropColor : Window->BackdropColorization);
    return DwmFrameRgb(GetSysColor(Active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION));
}

static ULONG
DwmFrameButtonState(const DWM_WIN *Window, ULONG Button)
{
    return Window->CaptionState &
           (DWM_CAPTION_STATE_HOT(Button) | DWM_CAPTION_STATE_PRESSED(Button) |
            DWM_CAPTION_STATE_DISABLED(Button) | DWM_CAPTION_STATE_RESTORE |
            DWM_CAPTION_STATE_TOOLWINDOW);
}

static BOOL
DwmFrameRenderButton(DWM_FRAME_BUTTON *Entry)
{
    BITMAPINFO Info;
    HBITMAP Bitmap, Previous;
    ULONG *Bits = NULL;
    RECT Rect;
    HDC Dc;
    ULONG Count, Index;
    BOOL Drawn;

    if (!g_drawCaptionButtonLoaded)
    {
        HMODULE Module = LoadLibraryW(L"uxtheme.dll");

        g_drawCaptionButtonLoaded = TRUE;
        if (Module != NULL)
            g_drawCaptionButton = (PFN_DRAW_CAPTION_BUTTON)GetProcAddress(Module, "ThemeDwmDrawCaptionButton");
    }
    if (g_drawCaptionButton == NULL)
        return FALSE;

    ZeroMemory(&Info, sizeof(Info));
    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = (LONG)Entry->Width;
    Info.bmiHeader.biHeight = -(LONG)Entry->Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    Dc = CreateCompatibleDC(NULL);
    if (Dc == NULL)
        return FALSE;
    Bitmap = CreateDIBSection(Dc, &Info, DIB_RGB_COLORS, (PVOID *)&Bits, NULL, 0);
    if (Bitmap == NULL || Bits == NULL)
    {
        if (Bitmap != NULL)
            DeleteObject(Bitmap);
        DeleteDC(Dc);
        return FALSE;
    }

    Count = Entry->Width * Entry->Height;
    for (Index = 0; Index < Count; ++Index)
        Bits[Index] = Entry->Background;
    Previous = SelectObject(Dc, Bitmap);
    SetRect(&Rect, 0, 0, (LONG)Entry->Width, (LONG)Entry->Height);
    Drawn = g_drawCaptionButton(Dc, &Rect, Entry->Button, Entry->State,
                                (Entry->Flags & DWM_WINDOW_ACTIVE) != 0,
                                (Entry->Flags & DWM_WINDOW_DARK) != 0);
    GdiFlush();
    if (Drawn)
    {
        Entry->Pixels = HeapAlloc(GetProcessHeap(), 0, Count * sizeof(ULONG));
        if (Entry->Pixels != NULL)
        {
            for (Index = 0; Index < Count; ++Index)
                Entry->Pixels[Index] = Bits[Index] & 0x00FFFFFFu;
        }
    }
    SelectObject(Dc, Previous);
    DeleteObject(Bitmap);
    DeleteDC(Dc);
    return Entry->Pixels != NULL;
}

const ULONG *
DwmFrameButtonImage(const DWM_WIN *Window, ULONG Button, ULONG Background)
{
    DWM_FRAME_BUTTON *Entry, *Oldest = &g_frameButtons[0];
    const RECTL *Rect;
    ULONG Width, Height, State, Flags, Index;

    if (Button >= DWM_CAPTION_BUTTONS)
        return NULL;
    Rect = &Window->CaptionButtons[Button];
    if (Rect->right <= Rect->left || Rect->bottom <= Rect->top)
        return NULL;
    Width = (ULONG)(Rect->right - Rect->left);
    Height = (ULONG)(Rect->bottom - Rect->top);
    if (Width > DWM_FRAME_BUTTON_MAX || Height > DWM_FRAME_BUTTON_MAX)
        return NULL;
    State = DwmFrameButtonState(Window, Button);
    Flags = Window->LayerFlags & (DWM_WINDOW_ACTIVE | DWM_WINDOW_DARK);

    for (Index = 0; Index < DWM_FRAME_BUTTON_CACHE; ++Index)
    {
        Entry = &g_frameButtons[Index];
        if (Entry->Pixels != NULL && Entry->Button == Button && Entry->State == State &&
            Entry->Width == Width && Entry->Height == Height &&
            Entry->Background == Background && Entry->Flags == Flags)
        {
            Entry->Stamp = ++g_frameButtonStamp;
            return Entry->Pixels;
        }
        if (Entry->Pixels == NULL || Entry->Stamp < Oldest->Stamp)
            Oldest = Entry;
    }

    if (Oldest->Pixels != NULL)
        HeapFree(GetProcessHeap(), 0, Oldest->Pixels);
    ZeroMemory(Oldest, sizeof(*Oldest));
    Oldest->Button = Button;
    Oldest->State = State;
    Oldest->Width = Width;
    Oldest->Height = Height;
    Oldest->Background = Background;
    Oldest->Flags = Flags;
    Oldest->Stamp = ++g_frameButtonStamp;
    if (!DwmFrameRenderButton(Oldest))
    {
        ZeroMemory(Oldest, sizeof(*Oldest));
        return NULL;
    }
    return Oldest->Pixels;
}

VOID
DwmFrameInterior(const DWM_WIN *Window, RECTL *Interior)
{
    LONG Right = Window->ClientX + Window->ClientWidth;
    LONG Bottom = Window->ClientY + Window->ClientHeight;

    Interior->left = min(Window->ClientX + (LONG)Window->BackdropNcExtendLeft, Right);
    Interior->top = min(Window->ClientY + (LONG)Window->BackdropNcExtend, Bottom);
    Interior->right = max(Right - (LONG)Window->BackdropNcExtendRight, Interior->left);
    Interior->bottom = max(Bottom - (LONG)Window->BackdropNcExtendBottom, Interior->top);
}

ULONG
DwmFrameButtonSignature(const DWM_WIN *Window, ULONG Background)
{
    ULONG Hash = 2166136261u, Button;

    for (Button = 0; Button < DWM_CAPTION_BUTTONS; ++Button)
    {
        Hash = (Hash ^ (ULONG)Window->CaptionButtons[Button].left) * 16777619u;
        Hash = (Hash ^ (ULONG)Window->CaptionButtons[Button].top) * 16777619u;
        Hash = (Hash ^ (ULONG)Window->CaptionButtons[Button].right) * 16777619u;
        Hash = (Hash ^ (ULONG)Window->CaptionButtons[Button].bottom) * 16777619u;
    }
    Hash = (Hash ^ Window->CaptionState) * 16777619u;
    Hash = (Hash ^ (Window->LayerFlags & (DWM_WINDOW_ACTIVE | DWM_WINDOW_DARK))) * 16777619u;
    return (Hash ^ Background) * 16777619u;
}

BOOL
DwmFrameButtonBounds(const DWM_WIN *Window, RECTL *Bounds)
{
    ULONG Button;

    SetRectEmpty((RECT *)Bounds);
    if (!(Window->CaptionState & DWM_CAPTION_STATE_VALID))
        return FALSE;
    for (Button = 0; Button < DWM_CAPTION_BUTTONS; ++Button)
    {
        if (Window->CaptionButtons[Button].right > Window->CaptionButtons[Button].left &&
            Window->CaptionButtons[Button].bottom > Window->CaptionButtons[Button].top)
            UnionRect((RECT *)Bounds, (RECT *)Bounds, (const RECT *)&Window->CaptionButtons[Button]);
    }
    return !IsRectEmpty((RECT *)Bounds);
}

ULONG *
DwmFrameButtonAtlas(const DWM_WIN *Window, ULONG Background, RECTL *Bounds)
{
    ULONG Width, Height, Button, Index, Count;
    ULONG *Pixels;
    LONG Row;

    if (!DwmFrameButtonBounds(Window, Bounds))
        return NULL;
    Width = (ULONG)(Bounds->right - Bounds->left);
    Height = (ULONG)(Bounds->bottom - Bounds->top);
    if (Width > DWM_FRAME_BUTTON_MAX * DWM_CAPTION_BUTTONS || Height > DWM_FRAME_BUTTON_MAX)
        return NULL;

    Count = Width * Height;
    Pixels = HeapAlloc(GetProcessHeap(), 0, Count * sizeof(ULONG));
    if (Pixels == NULL)
        return NULL;
    for (Index = 0; Index < Count; ++Index)
        Pixels[Index] = Background | 0xFF000000u;
    for (Button = 0; Button < DWM_CAPTION_BUTTONS; ++Button)
    {
        const RECTL *Rect = &Window->CaptionButtons[Button];
        const ULONG *Image = DwmFrameButtonImage(Window, Button, Background);
        ULONG ButtonWidth;

        if (Image == NULL)
            continue;
        ButtonWidth = (ULONG)(Rect->right - Rect->left);
        for (Row = Rect->top; Row < Rect->bottom; ++Row)
        {
            ULONG *Dest = Pixels + (SIZE_T)(Row - Bounds->top) * Width + (ULONG)(Rect->left - Bounds->left);
            const ULONG *Source = Image + (SIZE_T)(Row - Rect->top) * ButtonWidth;

            for (Index = 0; Index < ButtonWidth; ++Index)
                Dest[Index] = Source[Index] | 0xFF000000u;
        }
    }
    return Pixels;
}
