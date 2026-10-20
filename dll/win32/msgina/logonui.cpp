/*
 * PROJECT:     LiberNT Logon GINA DLL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Full-screen lock, sign-in, unlock and security options screens
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "msgina.h"
#include <wingdi.h>
#include <winnls.h>
#include <objbase.h>
#include <lm.h>
#include <math.h>
extern "C" {
#include <ndk/exfuncs.h>
}
#include <shellframe.h>
#include "logonui.h"

#define LOGONUI_TIMER_ANIMATION 1
#define LOGONUI_TIMER_CLOCK     2
#define LOGONUI_TIMER_FINISH    3
#define LOGONUI_FRAME_MS        15
#define LOGONUI_CLOCK_MS        500
#define LOGONUI_CONFIRM_MS      1600
#define WM_LOGONUI_DONE         (WM_APP + 0x40)
#define LOGONUI_MAX_USERS       6
#define LOGONUI_MAX_OPTIONS     4
#define LOGONUI_TEXT            256
#define LOGONUI_ACCENT          RGB(0, 103, 192)
#define LOGONUI_ACCENT_HOT      RGB(0, 120, 215)
#define LOGONUI_PASSWORD_CHAR   0x2022
#define LOGONUI_CUE_PROP        L"LiberNTLogonCue"
#define LOGONUI_COVER_CLASS     L"LiberNTLogonCover"

enum class Page
{
    Lock,
    SignIn,
    Options,
    Password,
    Legal,
    Power,
    Emergency
};

enum class Anim
{
    None,
    Enter,
    Dismiss,
    Shake,
    Leave
};

enum class Job
{
    None,
    Logon,
    Unlock,
    Change
};

enum Hit
{
    HitNone,
    HitSubmit,
    HitPower,
    HitCancel,
    HitOk,
    HitEmergency,
    HitUser = 100,
    HitOption = 200
};

enum Option
{
    OptionLock,
    OptionSignOut,
    OptionPassword,
    OptionTaskManager
};

struct Backdrop
{
    WCHAR Path[MAX_PATH];
    int Width;
    int Height;
    HBITMAP Sharp;
    HBITMAP Soft;
    UINT32 *SharpBits;
    UINT32 *SoftBits;
};

class LogonScreen;

struct LogonJob
{
    LogonScreen *Screen;
    Job Kind;
    WCHAR User[UNLEN + 1];
    WCHAR Domain[MAX_COMPUTERNAME_LENGTH + 1];
    WCHAR Password[LOGONUI_TEXT];
    WCHAR NewPassword[LOGONUI_TEXT];
    WCHAR Hint[LOGONUI_TEXT];
    NTSTATUS Status;
    NTSTATUS SubStatus;
    INT Action;
};

struct PowerChoice
{
    UINT Text;
    DWORD Flag;
    DWORD Action;
};

static const PowerChoice PowerChoices[] =
{
    { IDS_SHUTDOWN_SHUTDOWN, WLX_SHUTDOWN_STATE_POWER_OFF, WLX_SAS_ACTION_SHUTDOWN_POWER_OFF },
    { IDS_SHUTDOWN_RESTART, WLX_SHUTDOWN_STATE_REBOOT, WLX_SAS_ACTION_SHUTDOWN_REBOOT },
    { IDS_SHUTDOWN_SLEEP, WLX_SHUTDOWN_STATE_SLEEP, WLX_SAS_ACTION_SHUTDOWN_SLEEP },
    { IDS_SHUTDOWN_HIBERNATE, WLX_SHUTDOWN_STATE_HIBERNATE, WLX_SAS_ACTION_SHUTDOWN_HIBERNATE },
    { IDS_LOGONUI_SIGN_OUT, WLX_SHUTDOWN_STATE_LOGOFF, WLX_SAS_ACTION_LOGOFF },
};

static Backdrop g_Backdrops[2];
static ULONG_PTR g_GdiplusToken;
static WNDPROC g_EditProc;
static HWND g_Cover;
static HBITMAP g_CoverBitmap;
static HWND g_LockWindow;

static HBITMAP
CreateDib(int Width, int Height, UINT32 **Bits)
{
    BITMAPINFO Info = {};

    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = Width;
    Info.bmiHeader.biHeight = -Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(NULL, &Info, DIB_RGB_COLORS, (PVOID *)Bits, NULL, 0);
}

static LRESULT CALLBACK
CoverWindowProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    switch (Message)
    {
        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            HDC Dc = BeginPaint(Window, &Paint);
            HDC Source = CreateCompatibleDC(Dc);
            HGDIOBJ Old = SelectObject(Source, g_CoverBitmap);
            RECT Client;

            GetClientRect(Window, &Client);
            BitBlt(Dc, 0, 0, Client.right, Client.bottom, Source, 0, 0, SRCCOPY);
            SelectObject(Source, Old);
            DeleteDC(Source);
            EndPaint(Window, &Paint);
            return 0;
        }

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_DESTROY:
            DeleteObject(g_CoverBitmap);
            g_CoverBitmap = NULL;
            g_Cover = NULL;
            return 0;
    }

    return DefWindowProcW(Window, Message, wParam, lParam);
}

static float
EaseOut(float t)
{
    t = 1.0f - t;
    return 1.0f - t * t * t;
}

static float
EaseInOut(float t)
{
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
}

static void
StartGdiplus()
{
    Gdiplus::GdiplusStartupInput Input;

    if (!g_GdiplusToken)
        Gdiplus::GdiplusStartup(&g_GdiplusToken, &Input, NULL);
}

static BOOL
WallpaperPath(PGINA_CONTEXT Context, BOOL User, PWSTR Path)
{
    WCHAR Value[MAX_PATH];
    DWORD Size = sizeof(Value);
    HKEY Root = NULL;
    LONG Error;

    Path[0] = UNICODE_NULL;
    if (User)
        Error = RegOpenLoggedOnHKCU(Context->UserToken, KEY_QUERY_VALUE, &Root);
    else
        Error = RegOpenKeyExW(HKEY_USERS, L".DEFAULT", 0, KEY_QUERY_VALUE, &Root);
    if (Error != ERROR_SUCCESS)
        return FALSE;

    Error = RegGetValueW(Root, L"Control Panel\\Desktop", L"Wallpaper", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL,
                         Value, &Size);
    RegCloseKey(Root);
    if (Error != ERROR_SUCCESS || !Value[0])
        return FALSE;

    return ExpandEnvironmentStringsW(Value, Path, MAX_PATH) != 0 &&
           GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
}

static void
PaintWallpaper(HDC Dc, UINT32 *Bits, int Width, int Height, PCWSTR Path)
{
    int x, y;

    for (y = 0; y < Height; y++)
    {
        UINT32 r = 10 + 14 * (Height - y) / Height, g = 24 + 34 * (Height - y) / Height, b = 44 + 60 * (Height - y) / Height;

        for (x = 0; x < Width; x++)
            Bits[y * Width + x] = (r << 16) | (g << 8) | b;
    }

    if (!Path[0])
        return;

    Gdiplus::Bitmap Image(Path, FALSE);
    UINT ImageWidth = Image.GetWidth(), ImageHeight = Image.GetHeight();

    if (Image.GetLastStatus() != Gdiplus::Ok || !ImageWidth || !ImageHeight)
        return;

    Gdiplus::REAL Scale = max((Gdiplus::REAL)Width / ImageWidth, (Gdiplus::REAL)Height / ImageHeight);
    Gdiplus::REAL SourceWidth = Width / Scale, SourceHeight = Height / Scale;
    Gdiplus::Graphics Graphics(Dc);

    Graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Graphics.DrawImage(&Image, Gdiplus::RectF(0, 0, (Gdiplus::REAL)Width, (Gdiplus::REAL)Height),
                       (ImageWidth - SourceWidth) / 2, (ImageHeight - SourceHeight) / 2, SourceWidth, SourceHeight,
                       Gdiplus::UnitPixel);
}

static void
BoxBlur(int *Channel, int *Scratch, int Width, int Height, int Radius)
{
    int x, y, i, Sum, Count;

    for (y = 0; y < Height; y++)
    {
        for (x = 0; x < Width; x++)
        {
            Sum = 0;
            Count = 0;
            for (i = -Radius; i <= Radius; i++)
            {
                if (x + i >= 0 && x + i < Width)
                {
                    Sum += Channel[y * Width + x + i];
                    Count++;
                }
            }
            Scratch[y * Width + x] = Sum / Count;
        }
    }
    for (y = 0; y < Height; y++)
    {
        for (x = 0; x < Width; x++)
        {
            Sum = 0;
            Count = 0;
            for (i = -Radius; i <= Radius; i++)
            {
                if (y + i >= 0 && y + i < Height)
                {
                    Sum += Scratch[(y + i) * Width + x];
                    Count++;
                }
            }
            Channel[y * Width + x] = Sum / Count;
        }
    }
}

static void
SoftenWallpaper(const UINT32 *Source, UINT32 *Target, int Width, int Height)
{
    const int Factor = 12;
    int SmallWidth = max(2, Width / Factor), SmallHeight = max(2, Height / Factor);
    SIZE_T Count = (SIZE_T)SmallWidth * SmallHeight;
    int *Channels = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Count * 4 * sizeof(int));
    int *Scratch, *Plane[3];
    int x, y, c, Pass;

    if (!Channels)
    {
        CopyMemory(Target, Source, (SIZE_T)Width * Height * sizeof(UINT32));
        return;
    }
    for (c = 0; c < 3; c++)
        Plane[c] = Channels + c * Count;
    Scratch = Channels + 3 * Count;

    for (y = 0; y < SmallHeight; y++)
    {
        for (x = 0; x < SmallWidth; x++)
        {
            int Sum[3] = {0, 0, 0}, Samples = 0, sx, sy;

            for (sy = y * Height / SmallHeight; sy < (y + 1) * Height / SmallHeight; sy += 2)
            {
                for (sx = x * Width / SmallWidth; sx < (x + 1) * Width / SmallWidth; sx += 2)
                {
                    UINT32 Pixel = Source[sy * Width + sx];

                    Sum[0] += (Pixel >> 16) & 0xFF;
                    Sum[1] += (Pixel >> 8) & 0xFF;
                    Sum[2] += Pixel & 0xFF;
                    Samples++;
                }
            }
            for (c = 0; c < 3; c++)
                Plane[c][y * SmallWidth + x] = Samples ? Sum[c] / Samples : 0;
        }
    }
    for (Pass = 0; Pass < 3; Pass++)
    {
        for (c = 0; c < 3; c++)
            BoxBlur(Plane[c], Scratch, SmallWidth, SmallHeight, 2);
    }

    for (y = 0; y < Height; y++)
    {
        float fy = ((float)y + 0.5f) * SmallHeight / Height - 0.5f;
        int y0 = max(0, min(SmallHeight - 1, (int)fy)), y1 = min(SmallHeight - 1, y0 + 1);
        float wy = max(0.0f, min(1.0f, fy - y0));

        for (x = 0; x < Width; x++)
        {
            float fx = ((float)x + 0.5f) * SmallWidth / Width - 0.5f;
            int x0 = max(0, min(SmallWidth - 1, (int)fx)), x1 = min(SmallWidth - 1, x0 + 1);
            float wx = max(0.0f, min(1.0f, fx - x0));
            UINT32 Pixel = 0;

            for (c = 0; c < 3; c++)
            {
                float Top = Plane[c][y0 * SmallWidth + x0] * (1 - wx) + Plane[c][y0 * SmallWidth + x1] * wx;
                float Bottom = Plane[c][y1 * SmallWidth + x0] * (1 - wx) + Plane[c][y1 * SmallWidth + x1] * wx;
                int Value = (int)((Top * (1 - wy) + Bottom * wy) * 0.58f);

                Pixel |= (UINT32)min(255, max(0, Value)) << (16 - 8 * c);
            }
            Target[y * Width + x] = Pixel;
        }
    }

    HeapFree(GetProcessHeap(), 0, Channels);
}

static void
ShadeLockScreen(UINT32 *Bits, int Width, int Height)
{
    int x, y;

    for (y = 0; y < Height; y++)
    {
        float t = (float)y / Height;
        float Shade = t < 0.5f ? 0.30f - 0.40f * t : 0.10f + 0.70f * (t - 0.5f);
        UINT32 Keep = (UINT32)((1.0f - Shade) * 256.0f);

        for (x = 0; x < Width; x++)
        {
            UINT32 Pixel = Bits[y * Width + x];

            Bits[y * Width + x] = ((((Pixel >> 16) & 0xFF) * Keep >> 8) << 16) |
                                  ((((Pixel >> 8) & 0xFF) * Keep >> 8) << 8) | ((Pixel & 0xFF) * Keep >> 8);
        }
    }
}

static Backdrop *
LoadBackdrop(PGINA_CONTEXT Context, BOOL User, int Width, int Height)
{
    Backdrop *Entry = &g_Backdrops[User ? 1 : 0];
    WCHAR Path[MAX_PATH];
    HDC Dc;
    HGDIOBJ Old;

    WallpaperPath(Context, User, Path);
    if (Entry->Sharp && Entry->Width == Width && Entry->Height == Height && !_wcsicmp(Entry->Path, Path))
        return Entry;

    if (Entry->Sharp)
        DeleteObject(Entry->Sharp);
    if (Entry->Soft)
        DeleteObject(Entry->Soft);
    ZeroMemory(Entry, sizeof(*Entry));

    Entry->Sharp = CreateDib(Width, Height, &Entry->SharpBits);
    Entry->Soft = CreateDib(Width, Height, &Entry->SoftBits);
    if (!Entry->Sharp || !Entry->Soft)
        return NULL;

    Dc = CreateCompatibleDC(NULL);
    Old = SelectObject(Dc, Entry->Sharp);
    PaintWallpaper(Dc, Entry->SharpBits, Width, Height, Path);
    SelectObject(Dc, Old);
    DeleteDC(Dc);
    GdiFlush();

    SoftenWallpaper(Entry->SharpBits, Entry->SoftBits, Width, Height);
    ShadeLockScreen(Entry->SharpBits, Width, Height);
    StringCchCopyW(Entry->Path, _countof(Entry->Path), Path);
    Entry->Width = Width;
    Entry->Height = Height;
    return Entry;
}

static BOOL
CaptureBackdrop(Backdrop *Entry, int Width, int Height)
{
    HDC ScreenDc, Dc;
    HGDIOBJ Old;

    ZeroMemory(Entry, sizeof(*Entry));
    Entry->Sharp = CreateDib(Width, Height, &Entry->SharpBits);
    Entry->Soft = CreateDib(Width, Height, &Entry->SoftBits);
    if (!Entry->Sharp || !Entry->Soft)
        return FALSE;

    ScreenDc = GetDC(NULL);
    Dc = CreateCompatibleDC(ScreenDc);
    Old = SelectObject(Dc, Entry->Sharp);
    BitBlt(Dc, 0, 0, Width, Height, ScreenDc, 0, 0, SRCCOPY);
    SelectObject(Dc, Old);
    DeleteDC(Dc);
    ReleaseDC(NULL, ScreenDc);
    GdiFlush();

    SoftenWallpaper(Entry->SharpBits, Entry->SoftBits, Width, Height);
    Entry->Width = Width;
    Entry->Height = Height;
    return TRUE;
}

static void
LoadText(UINT Id, PWSTR Buffer, int Chars)
{
    if (!LoadStringW(hDllInstance, Id, Buffer, Chars))
        Buffer[0] = UNICODE_NULL;
}

static void
FillRound(Gdiplus::Graphics &Graphics, const RECT &Rect, float Radius, BYTE Alpha, COLORREF Color)
{
    Gdiplus::GraphicsPath Path(Gdiplus::FillModeAlternate);
    Gdiplus::REAL d = Radius * 2, l = (Gdiplus::REAL)Rect.left, t = (Gdiplus::REAL)Rect.top;
    Gdiplus::REAL r = (Gdiplus::REAL)Rect.right, b = (Gdiplus::REAL)Rect.bottom;
    Gdiplus::SolidBrush Brush(Gdiplus::Color(Alpha, GetRValue(Color), GetGValue(Color), GetBValue(Color)));

    if (d <= 0)
    {
        Graphics.FillRectangle(&Brush, l, t, r - l, b - t);
        return;
    }
    Path.AddArc(l, t, d, d, 180, 90);
    Path.AddArc(r - d, t, d, d, 270, 90);
    Path.AddArc(r - d, b - d, d, d, 0, 90);
    Path.AddArc(l, b - d, d, d, 90, 90);
    Path.CloseFigure();
    Graphics.FillPath(&Brush, &Path);
}

static void
FillCircle(Gdiplus::Graphics &Graphics, const RECT &Rect, BYTE Alpha, COLORREF Color)
{
    Gdiplus::SolidBrush Brush(Gdiplus::Color(Alpha, GetRValue(Color), GetGValue(Color), GetBValue(Color)));

    Graphics.FillEllipse(&Brush, (Gdiplus::REAL)Rect.left, (Gdiplus::REAL)Rect.top,
                         (Gdiplus::REAL)(Rect.right - Rect.left), (Gdiplus::REAL)(Rect.bottom - Rect.top));
}

static void
Glyph(Gdiplus::Graphics &Graphics, PCWSTR Name, const RECT &Rect, int Size, COLORREF Color)
{
    Gdiplus::REAL x = (Rect.left + Rect.right - Size) / 2.0f, y = (Rect.top + Rect.bottom - Size) / 2.0f;

    ShellFrame::FillIcon(Graphics, Name, Gdiplus::RectF(x, y, (Gdiplus::REAL)Size, (Gdiplus::REAL)Size), Color);
}

static void
Text(HDC Dc, HFONT Font, PCWSTR String, RECT Rect, UINT Flags, COLORREF Color)
{
    HGDIOBJ Old = SelectObject(Dc, Font);

    SetBkMode(Dc, TRANSPARENT);
    SetTextColor(Dc, Color);
    DrawTextW(Dc, String, -1, &Rect, Flags | DT_NOPREFIX);
    SelectObject(Dc, Old);
}

static int
TextHeight(HDC Dc, HFONT Font, PCWSTR String, int Width, UINT Flags)
{
    RECT Rect = {0, 0, Width, 0};
    HGDIOBJ Old = SelectObject(Dc, Font);

    DrawTextW(Dc, String, -1, &Rect, Flags | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(Dc, Old);
    return Rect.bottom;
}

static LRESULT CALLBACK
CueEditProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    LRESULT Result = CallWindowProcW(g_EditProc, Window, Message, wParam, lParam);
    PCWSTR Cue;

    if (Message == WM_PAINT && GetWindowTextLengthW(Window) == 0 && (Cue = (PCWSTR)GetPropW(Window, LOGONUI_CUE_PROP)))
    {
        HDC Dc = GetDC(Window);
        RECT Rect;

        SendMessageW(Window, EM_GETRECT, 0, (LPARAM)&Rect);
        Text(Dc, (HFONT)SendMessageW(Window, WM_GETFONT, 0, 0), Cue, Rect, DT_SINGLELINE | DT_LEFT, RGB(112, 112, 112));
        ReleaseDC(Window, Dc);
    }
    else if (Message == WM_SETFOCUS || Message == WM_KILLFOCUS)
    {
        InvalidateRect(GetParent(Window), NULL, FALSE);
    }
    else if (Message == WM_NCDESTROY)
    {
        RemovePropW(Window, LOGONUI_CUE_PROP);
    }
    return Result;
}

static DWORD WINAPI
LogonJobThread(PVOID Parameter);

class LogonScreen
{
public:
    PGINA_CONTEXT Context;
    LOGONUI_MODE Mode;
    HWND Window = NULL;
    Page Current = Page::Lock;
    BOOL ExpiredFlow = FALSE;
    BOOL Dismissed = FALSE;
    BOOL Completed = FALSE;
    INT CompletedAction = WLX_SAS_ACTION_NONE;
    WCHAR ExpiredUser[UNLEN + 1] = L"";
    BOOL CadRequired = FALSE;
    int Width = 0;
    int Height = 0;
    float Scale = 1.0f;
    Backdrop *Wall = NULL;
    Backdrop Captured = {};
    DWORD PowerAllowed = 0;
    DWORD PowerAction = 0;
    int PowerRows[_countof(PowerChoices)] = {};
    HDC FrameDc = NULL;
    HDC LayerDc = NULL;
    HDC BackdropDc = NULL;
    HBITMAP FrameBitmap = NULL;
    HBITMAP LayerBitmap = NULL;
    HBITMAP BeforeBitmap = NULL;
    HGDIOBJ OldFrame = NULL;
    HGDIOBJ OldLayer = NULL;
    UINT32 *FrameBits = NULL;
    UINT32 *LayerBits = NULL;
    UINT32 *BeforeBits = NULL;
    HFONT ClockFont = NULL;
    HFONT DateFont = NULL;
    HFONT NameFont = NULL;
    HFONT BodyFont = NULL;
    HFONT SmallFont = NULL;
    HFONT OptionFont = NULL;
    HBRUSH FieldBrush = NULL;
    HWND UserEdit = NULL;
    HWND PasswordEdit = NULL;
    HWND OldEdit = NULL;
    HWND NewEdit = NULL;
    HWND ConfirmEdit = NULL;
    WCHAR Users[LOGONUI_MAX_USERS][UNLEN + 1] = {};
    BOOL NoPassword[LOGONUI_MAX_USERS] = {};
    int UserCount = 0;
    int Selected = 0;
    WCHAR Message[512] = L"";
    WCHAR Cues[5][64] = {};
    int Options[LOGONUI_MAX_OPTIONS] = {};
    int OptionCount = 0;
    int Focus = 0;
    BOOL CanPowerOff = FALSE;
    PWSTR LegalCaption = NULL;
    PWSTR LegalText = NULL;
    Anim Animation = Anim::None;
    ULONGLONG AnimationStart = 0;
    DWORD AnimationLength = 0;
    float Progress = 1.0f;
    int Hover = HitNone;
    HANDLE Worker = NULL;
    LogonJob *Pending = NULL;
    BOOL Busy = FALSE;
    ULONGLONG BusyStart = 0;
    WCHAR ClockText[64] = L"";
    BOOL CapsLock = FALSE;
    RECT Avatar = {}, Name = {}, Title = {}, Subtitle = {}, UserField = {}, PasswordField = {}, OldField = {},
         NewField = {}, ConfirmField = {}, Submit = {}, MessageRect = {}, Power = {}, Cancel = {}, Ok = {},
         LegalRect = {};
    RECT UserItems[LOGONUI_MAX_USERS + 1] = {};
    RECT OptionRows[_countof(PowerChoices)] = {};

    LogonScreen(PGINA_CONTEXT Context, LOGONUI_MODE Mode) :
        Context(Context),
        Mode(Mode)
    {
    }

    int S(int Value) const
    {
        return (int)(Value * Scale + 0.5f);
    }

    BOOL Locked() const
    {
        return Mode == LogonUiLockedNotice || Mode == LogonUiUnlock || Mode == LogonUiOptions;
    }

    BOOL OtherUser() const
    {
        return Selected >= UserCount;
    }

    BOOL Passwordless() const
    {
        return !OtherUser() && NoPassword[Selected];
    }

    PCWSTR SelectedName() const
    {
        if (Mode == LogonUiUnlock && !OtherUser())
            return Context->UserName;
        return OtherUser() ? L"" : Users[Selected];
    }

    HFONT MakeFont(PCWSTR Face, int Size)
    {
        return CreateFontW(-S(Size), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, Face);
    }

    HWND MakeEdit(int Id, UINT CueId, int CueIndex, BOOL Password)
    {
        HWND Edit = CreateWindowExW(0, L"EDIT", L"",
                                    WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | (Password ? ES_PASSWORD : 0),
                                    0, 0, 10, 10, Window, (HMENU)(INT_PTR)Id, hDllInstance, NULL);

        if (!Edit)
            return NULL;
        SendMessageW(Edit, WM_SETFONT, (WPARAM)BodyFont, FALSE);
        SendMessageW(Edit, EM_LIMITTEXT, LOGONUI_TEXT - 1, 0);
        if (Password)
            SendMessageW(Edit, EM_SETPASSWORDCHAR, LOGONUI_PASSWORD_CHAR, 0);
        LoadText(CueId, Cues[CueIndex], _countof(Cues[CueIndex]));
        SetPropW(Edit, LOGONUI_CUE_PROP, (HANDLE)Cues[CueIndex]);
        if (!g_EditProc)
            g_EditProc = (WNDPROC)GetWindowLongPtrW(Edit, GWLP_WNDPROC);
        SetWindowLongPtrW(Edit, GWLP_WNDPROC, (LONG_PTR)CueEditProc);
        return Edit;
    }

    void LoadBlankDefaultUser(PWSTR User, DWORD Count)
    {
        HKEY Key;
        DWORD Value = 0, Type, Size = 2 * sizeof(WCHAR);
        WCHAR Password[2] = L"";

        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", 0,
                          KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
            return;
        if (ReadRegDwordValue(Key, L"AutoAdminLogon", &Value) == ERROR_SUCCESS && Value &&
            RegQueryValueExW(Key, L"DefaultPassword", NULL, &Type, (PBYTE)Password, &Size) == ERROR_SUCCESS &&
            Type == REG_SZ && !Password[0])
        {
            Size = (Count - 1) * sizeof(WCHAR);
            if (RegQueryValueExW(Key, L"DefaultUserName", NULL, &Type, (PBYTE)User, &Size) != ERROR_SUCCESS ||
                Type != REG_SZ)
                User[0] = UNICODE_NULL;
        }
        RegCloseKey(Key);
    }

    void LoadUsers()
    {
        PUSER_INFO_1 Info;
        DWORD Read, Total, Resume = 0, i;
        NET_API_STATUS Status;
        int Default = -1;
        WCHAR Blank[UNLEN + 1] = L"";

        if (Mode == LogonUiUnlock)
        {
            StringCchCopyW(Users[0], _countof(Users[0]), Context->UserName);
            NoPassword[0] = IsUserPasswordBlank(Context->UserName);
            UserCount = 1;
            Selected = 0;
            return;
        }

        LoadBlankDefaultUser(Blank, _countof(Blank));
        do
        {
            Info = NULL;
            Read = 0;
            Status = NetUserEnum(NULL, 1, FILTER_NORMAL_ACCOUNT, (LPBYTE *)&Info, MAX_PREFERRED_LENGTH, &Read, &Total,
                                 &Resume);
            if (Status != NERR_Success && Status != ERROR_MORE_DATA)
                break;
            for (i = 0; Info && i < Read && UserCount < LOGONUI_MAX_USERS; i++)
            {
                if (Info[i].usri1_flags & UF_ACCOUNTDISABLE)
                    continue;
                StringCchCopyW(Users[UserCount], _countof(Users[UserCount]), Info[i].usri1_name);
                NoPassword[UserCount] = (Blank[0] && !_wcsicmp(Users[UserCount], Blank)) ||
                                        IsUserPasswordBlank(Users[UserCount]);
                if (!_wcsicmp(Users[UserCount], Context->UserName))
                    Default = UserCount;
                UserCount++;
            }
            if (Info)
                NetApiBufferFree(Info);
        } while (Status == ERROR_MORE_DATA && Read);

        if (Context->bDontDisplayLastUserName || !UserCount)
            Selected = UserCount;
        else
            Selected = Default >= 0 ? Default : 0;
    }

    void LoadOptions()
    {
        HKEY CurrentUser = NULL, Key;
        DWORD Value;
        BOOL Lock = TRUE, SignOut = TRUE, Password = TRUE, TaskManager = TRUE;

        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", 0,
                          KEY_QUERY_VALUE, &Key) == ERROR_SUCCESS)
        {
            if (ReadRegDwordValue(Key, L"DisableLockWorkstation", &Value) == ERROR_SUCCESS && Value)
                Lock = FALSE;
            RegCloseKey(Key);
        }
        if (RegOpenLoggedOnHKCU(Context->UserToken, KEY_QUERY_VALUE, &CurrentUser) == ERROR_SUCCESS)
        {
            if (RegOpenKeyExW(CurrentUser, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0,
                              KEY_QUERY_VALUE, &Key) == ERROR_SUCCESS)
            {
                if (ReadRegDwordValue(Key, L"DisableLockWorkstation", &Value) == ERROR_SUCCESS && Value)
                    Lock = FALSE;
                if (ReadRegDwordValue(Key, L"DisableChangePassword", &Value) == ERROR_SUCCESS && Value)
                    Password = FALSE;
                if (ReadRegDwordValue(Key, L"DisableTaskMgr", &Value) == ERROR_SUCCESS && Value)
                    TaskManager = FALSE;
                RegCloseKey(Key);
            }
            if (RegOpenKeyExW(CurrentUser, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", 0,
                              KEY_QUERY_VALUE, &Key) == ERROR_SUCCESS)
            {
                if (ReadRegDwordValue(Key, L"NoLogoff", &Value) == ERROR_SUCCESS && Value)
                    SignOut = FALSE;
                RegCloseKey(Key);
            }
            CanPowerOff = (GetAllowedShutdownOptions(CurrentUser, Context->UserToken) &
                           (WLX_SHUTDOWN_STATE_POWER_OFF | WLX_SHUTDOWN_STATE_REBOOT)) != 0;
            RegCloseKey(CurrentUser);
        }

        if (Lock)
            Options[OptionCount++] = OptionLock;
        if (SignOut)
            Options[OptionCount++] = OptionSignOut;
        if (Password)
            Options[OptionCount++] = OptionPassword;
        if (TaskManager)
            Options[OptionCount++] = OptionTaskManager;
    }

    void LoadLegalNotice()
    {
        HKEY Key;

        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", 0,
                          KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
            return;
        ReadRegSzValue(Key, L"LegalNoticeCaption", &LegalCaption);
        ReadRegSzValue(Key, L"LegalNoticeText", &LegalText);
        RegCloseKey(Key);
    }

    BOOL Initialize(HWND Dialog)
    {
        HDC ScreenDc;

        Window = Dialog;
        Width = GetSystemMetrics(SM_CXSCREEN);
        Height = GetSystemMetrics(SM_CYSCREEN);
        Scale = max(0.6f, min(2.0f, Height / 1080.0f));

        StartGdiplus();
        if (Mode == LogonUiPower)
            Wall = CaptureBackdrop(&Captured, Width, Height) ? &Captured : NULL;
        else
            Wall = LoadBackdrop(Context, Locked(), Width, Height);
        if (!Wall)
            return FALSE;

        ScreenDc = GetDC(NULL);
        FrameDc = CreateCompatibleDC(ScreenDc);
        LayerDc = CreateCompatibleDC(ScreenDc);
        BackdropDc = CreateCompatibleDC(ScreenDc);
        ReleaseDC(NULL, ScreenDc);
        FrameBitmap = CreateDib(Width, Height, &FrameBits);
        LayerBitmap = CreateDib(Width, Height, &LayerBits);
        BeforeBitmap = CreateDib(Width, Height, &BeforeBits);
        if (!FrameDc || !LayerDc || !BackdropDc || !FrameBitmap || !LayerBitmap)
            return FALSE;
        OldFrame = SelectObject(FrameDc, FrameBitmap);
        OldLayer = SelectObject(LayerDc, LayerBitmap);

        ClockFont = MakeFont(L"Selawik Light", 132);
        DateFont = MakeFont(L"Selawik Semilight", 32);
        NameFont = MakeFont(L"Selawik Semilight", 36);
        BodyFont = MakeFont(L"Selawik", 17);
        SmallFont = MakeFont(L"Selawik", 16);
        OptionFont = MakeFont(L"Selawik Semilight", 25);
        FieldBrush = CreateSolidBrush(RGB(255, 255, 255));

        CadRequired = !Context->bDisableCAD;
        if (Mode == LogonUiNotice || Mode == LogonUiLockedNotice)
            CanPowerOff = Context->bShutdownWithoutLogon;
        if (Mode == LogonUiSignIn || Mode == LogonUiUnlock)
        {
            LoadUsers();
            UserEdit = MakeEdit(IDC_LOGONUI_USERNAME, IDS_LOGONUI_USERNAME, 0, FALSE);
            PasswordEdit = MakeEdit(IDC_LOGONUI_PASSWORD, IDS_LOGONUI_PASSWORD, 1, TRUE);
            CanPowerOff = Context->bShutdownWithoutLogon;
            if (Mode == LogonUiSignIn)
                LoadLegalNotice();
        }
        if (Mode == LogonUiOptions)
            LoadOptions();
        if (Mode == LogonUiOptions || Mode == LogonUiSignIn)
        {
            OldEdit = MakeEdit(IDC_LOGONUI_OLDPWD, IDS_LOGONUI_OLD_PASSWORD, 2, TRUE);
            NewEdit = MakeEdit(IDC_LOGONUI_NEWPWD, IDS_LOGONUI_NEW_PASSWORD, 3, TRUE);
            ConfirmEdit = MakeEdit(IDC_LOGONUI_CONFIRMPWD, IDS_LOGONUI_CONFIRM_PASSWORD, 4, TRUE);
        }

        if (Mode == LogonUiPower)
        {
            for (int i = 0; i < (int)_countof(PowerChoices); i++)
            {
                if (!(PowerAllowed & PowerChoices[i].Flag))
                    continue;
                if (PowerChoices[i].Action == PowerAction)
                    Focus = OptionCount;
                PowerRows[OptionCount++] = i;
            }
        }

        if (Mode == LogonUiNotice || Mode == LogonUiLockedNotice)
            Current = Page::Lock;
        else if (Mode == LogonUiPower)
            Current = Page::Power;
        else if (Mode == LogonUiOptions)
            Current = Page::Options;
        else if (LegalCaption && LegalCaption[0] && LegalText && LegalText[0])
            Current = Page::Legal;
        else
            Current = Page::SignIn;

        SetWindowPos(Window, HWND_TOPMOST, 0, 0, Width, Height, SWP_NOACTIVATE);
        UpdateClock();
        CapsLock = (GetKeyState(VK_CAPITAL) & 1) != 0;
        SetTimer(Window, LOGONUI_TIMER_CLOCK, LOGONUI_CLOCK_MS, NULL);
        Layout(0);
        if ((Mode == LogonUiSignIn || Mode == LogonUiUnlock) && g_LockWindow && IsWindowVisible(g_LockWindow))
            Start(Anim::Dismiss, 340);
        else if (Current != Page::Lock)
        {
            SaveScreen();
            Start(Anim::Enter, 300);
        }
        SetWindowPos(Window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(Window);
        LogonUiRemoveCover();
        if (Current == Page::Lock)
        {
            g_LockWindow = Window;
            ShowFields(TRUE);
        }
        if (Pending)
            PostMessageW(Window, WM_LOGONUI_DONE, 0, (LPARAM)Pending);
        return TRUE;
    }

    void Destroy()
    {
        if (g_LockWindow == Window)
            g_LockWindow = NULL;
        if (Worker)
        {
            WaitForSingleObject(Worker, INFINITE);
            CloseHandle(Worker);
            Worker = NULL;
        }
        if (Pending)
        {
            SecureZeroMemory(Pending, sizeof(*Pending));
            HeapFree(GetProcessHeap(), 0, Pending);
            Pending = NULL;
        }
        if (FrameDc)
        {
            SelectObject(FrameDc, OldFrame);
            DeleteDC(FrameDc);
        }
        if (LayerDc)
        {
            SelectObject(LayerDc, OldLayer);
            DeleteDC(LayerDc);
        }
        if (BackdropDc)
            DeleteDC(BackdropDc);
        if (FrameBitmap)
            DeleteObject(FrameBitmap);
        if (LayerBitmap)
            DeleteObject(LayerBitmap);
        if (BeforeBitmap)
            DeleteObject(BeforeBitmap);
        HFONT Fonts[] = {ClockFont, DateFont, NameFont, BodyFont, SmallFont, OptionFont};
        for (HFONT Font : Fonts)
        {
            if (Font)
                DeleteObject(Font);
        }
        if (FieldBrush)
            DeleteObject(FieldBrush);
        HeapFree(GetProcessHeap(), 0, LegalCaption);
        HeapFree(GetProcessHeap(), 0, LegalText);
        if (Captured.Sharp)
            DeleteObject(Captured.Sharp);
        if (Captured.Soft)
            DeleteObject(Captured.Soft);
    }

    void UpdateClock()
    {
        WCHAR Time[32] = L"";

        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, NULL, NULL, Time, _countof(Time));
        if (wcscmp(Time, ClockText))
        {
            StringCchCopyW(ClockText, _countof(ClockText), Time);
            if (Current == Page::Lock)
                InvalidateRect(Window, NULL, FALSE);
        }
    }

    void Start(Anim Kind, DWORD Length)
    {
        Animation = Kind;
        AnimationStart = GetTickCount64();
        AnimationLength = Length;
        Progress = 0.0f;
        if (Kind == Anim::Enter || Kind == Anim::Dismiss || Kind == Anim::Leave)
            ShowFields(FALSE);
        SetTimer(Window, LOGONUI_TIMER_ANIMATION, LOGONUI_FRAME_MS, NULL);
        InvalidateRect(Window, NULL, FALSE);
    }

    void Tick()
    {
        Anim Finished = Anim::None;

        if (Animation != Anim::None)
        {
            Progress = min(1.0f, (float)(GetTickCount64() - AnimationStart) / AnimationLength);
            if (Progress >= 1.0f)
            {
                Finished = Animation;
                Animation = Anim::None;
            }
            if (Finished == Anim::Shake || Animation == Anim::Shake)
                Layout(Animation == Anim::Shake ? ShakeOffset() : 0);
        }
        if (Animation == Anim::None && !Busy)
            KillTimer(Window, LOGONUI_TIMER_ANIMATION);
        InvalidateRect(Window, NULL, FALSE);

        if (Finished == Anim::Enter || Finished == Anim::Dismiss)
            ShowFields(TRUE);
    }

    int ShakeOffset() const
    {
        float t = Progress;

        return (int)(sinf(t * 6.2831853f * 3.0f) * (1.0f - t) * S(14));
    }

    void ShowFields(BOOL Show)
    {
        HWND Edits[] = {UserEdit, PasswordEdit, OldEdit, NewEdit, ConfirmEdit};
        BOOL Visible[] = {Current == Page::SignIn && OtherUser(), Current == Page::SignIn && !Passwordless(),
                          Current == Page::Password && !ExpiredFlow, Current == Page::Password,
                          Current == Page::Password};
        HWND First = NULL;
        int i;

        if (Busy || Completed)
            Show = FALSE;
        for (i = 0; i < (int)_countof(Edits); i++)
        {
            if (!Edits[i])
                continue;
            ShowWindow(Edits[i], Show && Visible[i] ? SW_SHOWNA : SW_HIDE);
            if (Show && Visible[i] && !First)
                First = Edits[i];
        }
        if (Show)
            SetForegroundWindow(Window);
        if (Show && First)
        {
            if (Current == Page::SignIn && !OtherUser())
                First = PasswordEdit;
            if (Current == Page::SignIn && OtherUser() && GetWindowTextLengthW(UserEdit))
                First = PasswordEdit;
            SetFocus(First);
        }
        else if (Show)
        {
            SetFocus(Window);
        }
    }

    void PlaceEdit(HWND Edit, const RECT &Field, int Offset)
    {
        int EditHeight = TextHeight(FrameDc, BodyFont, L"Ag", 100, DT_SINGLELINE) + S(4);
        int Right = Field.right - S(12) - ((Edit == PasswordEdit || Edit == ConfirmEdit) ? S(42) : 0);

        if (!Edit)
            return;
        SetWindowPos(Edit, NULL, Field.left + S(12) + Offset, (Field.top + Field.bottom - EditHeight) / 2,
                     Right - Field.left - S(12), EditHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void Layout(int Offset)
    {
        int Center = Width / 2, FieldWidth = S(316), FieldHeight = S(42), y, i;

        SetRect(&Power, Width - S(24) - S(46), Height - S(24) - S(46), Width - S(24), Height - S(24));
        SetRect(&Cancel, Power.left - S(16) - S(132), Power.top + S(2), Power.left - S(16), Power.bottom - S(2));

        if (Current == Page::SignIn)
        {
            y = (int)(Height * 0.36f) - S(84);
            SetRect(&Avatar, Center - S(84), y, Center + S(84), y + S(168));
            SetRect(&Name, Center - S(400), Avatar.bottom + S(18), Center + S(400), Avatar.bottom + S(18) + S(50));
            y = Name.bottom + S(22);
            SetRectEmpty(&UserField);
            if (OtherUser())
            {
                SetRect(&UserField, Center - FieldWidth / 2, y, Center + FieldWidth / 2, y + FieldHeight);
                y += FieldHeight + S(10);
            }
            SetRect(&PasswordField, Center - FieldWidth / 2, y, Center + FieldWidth / 2, y + FieldHeight);
            OffsetRect(&UserField, Offset, 0);
            OffsetRect(&PasswordField, Offset, 0);
            SetRect(&Submit, PasswordField.right - S(42), PasswordField.top, PasswordField.right, PasswordField.bottom);
            if (Passwordless())
                SetRect(&Submit, Center - S(66) + Offset, PasswordField.top, Center + S(66) + Offset,
                        PasswordField.bottom);
            SetRect(&MessageRect, Center - S(260), PasswordField.bottom + S(16), Center + S(260),
                    PasswordField.bottom + S(16) + S(64));
            for (i = 0; i <= UserCount; i++)
            {
                int Bottom = Height - S(24) - (UserCount - i) * S(56);

                SetRect(&UserItems[i], S(24), Bottom - S(52), S(24) + S(280), Bottom);
            }
            PlaceEdit(UserEdit, UserField, 0);
            PlaceEdit(PasswordEdit, PasswordField, 0);
        }
        else if (Current == Page::Password)
        {
            y = (int)(Height * 0.26f);
            SetRect(&Title, Center - S(420), y, Center + S(420), y + S(52));
            SetRect(&Subtitle, Center - S(300), Title.bottom + S(6), Center + S(300), Title.bottom + S(6) + S(48));
            y = Subtitle.bottom + S(24);
            SetRectEmpty(&OldField);
            if (!ExpiredFlow)
            {
                SetRect(&OldField, Center - FieldWidth / 2, y, Center + FieldWidth / 2, y + FieldHeight);
                y += FieldHeight + S(10);
            }
            SetRect(&NewField, Center - FieldWidth / 2, y, Center + FieldWidth / 2, y + FieldHeight);
            y += FieldHeight + S(10);
            SetRect(&ConfirmField, Center - FieldWidth / 2, y, Center + FieldWidth / 2, y + FieldHeight);
            OffsetRect(&OldField, Offset, 0);
            OffsetRect(&NewField, Offset, 0);
            OffsetRect(&ConfirmField, Offset, 0);
            SetRect(&Submit, ConfirmField.right - S(42), ConfirmField.top, ConfirmField.right, ConfirmField.bottom);
            SetRect(&MessageRect, Center - S(260), ConfirmField.bottom + S(16), Center + S(260),
                    ConfirmField.bottom + S(16) + S(64));
            PlaceEdit(OldEdit, OldField, 0);
            PlaceEdit(NewEdit, NewField, 0);
            PlaceEdit(ConfirmEdit, ConfirmField, 0);
        }
        else if (Current == Page::Options || Current == Page::Power)
        {
            y = (int)(Height * 0.34f);
            for (i = 0; i < OptionCount; i++)
                SetRect(&OptionRows[i], Center - S(190), y + i * S(58), Center + S(190), y + i * S(58) + S(52));
        }
        else if (Current == Page::Emergency)
        {
            int TextWidth = min(S(620), Width - S(96));

            SetRect(&Title, Center - TextWidth / 2, (int)(Height * 0.3f), Center + TextWidth / 2,
                    (int)(Height * 0.3f) + S(52));
            SetRect(&LegalRect, Title.left, Title.bottom + S(14), Title.right, Title.bottom + S(14) + S(80));
            SetRect(&Ok, Center - S(140), LegalRect.bottom + S(28), Center - S(8), LegalRect.bottom + S(28) + S(42));
            SetRect(&Cancel, Center + S(8), Ok.top, Center + S(140), Ok.bottom);
        }
        else if (Current == Page::Legal)
        {
            int TextWidth = min(S(860), Width - S(96));

            SetRect(&Title, Center - TextWidth / 2, (int)(Height * 0.2f), Center + TextWidth / 2,
                    (int)(Height * 0.2f) + S(52));
            SetRect(&LegalRect, Title.left, Title.bottom + S(18), Title.right, Height - S(200));
            SetRect(&Ok, Center - S(66), Height - S(150), Center + S(66), Height - S(150) + S(42));
        }
    }

    void SaveScreen()
    {
        HDC ScreenDc;
        HGDIOBJ Old;

        if (!BeforeBitmap)
            return;
        ScreenDc = GetDC(NULL);
        Old = SelectObject(BackdropDc, BeforeBitmap);
        BitBlt(BackdropDc, 0, 0, Width, Height, ScreenDc, 0, 0, SRCCOPY);
        SelectObject(BackdropDc, Old);
        ReleaseDC(NULL, ScreenDc);
    }

    void SwitchPage(Page Next)
    {
        if (BeforeBitmap)
        {
            GdiFlush();
            CopyMemory(BeforeBits, FrameBits, (SIZE_T)Width * Height * sizeof(UINT32));
        }
        Current = Next;
        Message[0] = UNICODE_NULL;
        Hover = HitNone;
        Focus = 0;
        Layout(0);
        Start(Anim::Enter, 260);
    }

    void SetMessage(UINT Id, PCWSTR Argument)
    {
        WCHAR Format[256];

        LoadText(Id, Format, _countof(Format));
        if (Argument)
            StringCchPrintfW(Message, _countof(Message), Format, Argument);
        else
            StringCchCopyW(Message, _countof(Message), Format);
    }

    void Fail(UINT Id, PCWSTR Argument, HWND Clear)
    {
        SetMessage(Id, Argument);
        if (Clear)
            SetWindowTextW(Clear, L"");
        Start(Anim::Shake, 360);
        if (Clear)
            SetFocus(Clear);
    }

    void BeginJob(LogonJob *Request)
    {
        DWORD Length = _countof(Request->Domain);

        Request->Screen = this;
        if (!Request->Domain[0])
            GetComputerNameW(Request->Domain, &Length);
        Pending = Request;
        Busy = TRUE;
        BusyStart = GetTickCount64();
        Message[0] = UNICODE_NULL;
        ShowFields(FALSE);
        SetFocus(Window);
        Worker = CreateThread(NULL, 0, LogonJobThread, Request, 0, NULL);
        if (!Worker)
        {
            LogonJobThread(Request);
            return;
        }
        SetTimer(Window, LOGONUI_TIMER_ANIMATION, LOGONUI_FRAME_MS, NULL);
        InvalidateRect(Window, NULL, FALSE);
    }

    LogonJob *NewJob(Job Kind)
    {
        LogonJob *Request = (LogonJob *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(LogonJob));

        if (Request)
            Request->Kind = Kind;
        return Request;
    }

    void SubmitSignIn()
    {
        LogonJob *Request;

        if (Busy)
            return;
        Request = NewJob(Mode == LogonUiUnlock ? Job::Unlock : Job::Logon);
        if (!Request)
            return;
        if (OtherUser())
            GetWindowTextW(UserEdit, Request->User, _countof(Request->User));
        else
            StringCchCopyW(Request->User, _countof(Request->User), SelectedName());
        GetWindowTextW(PasswordEdit, Request->Password, _countof(Request->Password));
        if (!Request->User[0])
        {
            HeapFree(GetProcessHeap(), 0, Request);
            SetFocus(UserEdit);
            return;
        }
        PWSTR Separator = wcschr(Request->User, L'\\');
        if (Separator)
        {
            *Separator = UNICODE_NULL;
            StringCchCopyW(Request->Domain, _countof(Request->Domain), Request->User);
            MoveMemory(Request->User, Separator + 1, (wcslen(Separator + 1) + 1) * sizeof(WCHAR));
        }
        SetWindowTextW(PasswordEdit, L"");
        BeginJob(Request);
    }

    void SubmitPassword()
    {
        LogonJob *Request;
        WCHAR Confirm[LOGONUI_TEXT];

        if (Busy)
            return;
        Request = NewJob(Job::Change);
        if (!Request)
            return;
        if (ExpiredFlow)
        {
            StringCchCopyW(Request->User, _countof(Request->User), ExpiredUser);
            StringCchCopyW(Request->Domain, _countof(Request->Domain), Context->DomainName);
            StringCchCopyW(Request->Password, _countof(Request->Password), Context->Password);
        }
        else
        {
            StringCchCopyW(Request->User, _countof(Request->User), Context->UserName);
            StringCchCopyW(Request->Domain, _countof(Request->Domain), Context->DomainName);
            GetWindowTextW(OldEdit, Request->Password, _countof(Request->Password));
        }
        GetWindowTextW(NewEdit, Request->NewPassword, _countof(Request->NewPassword));
        GetWindowTextW(ConfirmEdit, Confirm, _countof(Confirm));
        if (wcscmp(Request->NewPassword, Confirm))
        {
            SecureZeroMemory(Request, sizeof(*Request));
            SecureZeroMemory(Confirm, sizeof(Confirm));
            HeapFree(GetProcessHeap(), 0, Request);
            SetWindowTextW(ConfirmEdit, L"");
            Fail(IDS_LOGONUI_MISMATCH, NULL, NewEdit);
            SetWindowTextW(NewEdit, L"");
            return;
        }
        SecureZeroMemory(Confirm, sizeof(Confirm));
        SetWindowTextW(OldEdit, L"");
        SetWindowTextW(NewEdit, L"");
        SetWindowTextW(ConfirmEdit, L"");
        BeginJob(Request);
    }

    void JobDone(LogonJob *Done)
    {
        UINT Error = 0;
        HWND Retry = PasswordEdit;

        if (Worker)
        {
            WaitForSingleObject(Worker, INFINITE);
            CloseHandle(Worker);
            Worker = NULL;
        }
        Busy = FALSE;
        Pending = NULL;

        if (Done->Kind == Job::Logon)
        {
            Context->bAutoAdminLogon = FALSE;
            if (NT_SUCCESS(Done->Status))
            {
                Finish(WLX_SAS_ACTION_LOGON, Done);
                return;
            }
            if (Done->Status == STATUS_PASSWORD_EXPIRED || Done->Status == STATUS_PASSWORD_MUST_CHANGE ||
                (Done->Status == STATUS_ACCOUNT_RESTRICTION &&
                 (Done->SubStatus == STATUS_PASSWORD_EXPIRED || Done->SubStatus == STATUS_PASSWORD_MUST_CHANGE)))
            {
                NTSTATUS Reason = Done->Status == STATUS_ACCOUNT_RESTRICTION ? Done->SubStatus : Done->Status;

                ExpiredFlow = TRUE;
                StringCchCopyW(ExpiredUser, _countof(ExpiredUser), Done->User);
                StringCchCopyW(Context->DomainName, _countof(Context->DomainName), Done->Domain);
                StringCchCopyW(Context->Password, _countof(Context->Password), Done->Password);
                SwitchPage(Page::Password);
                SetMessage(Reason == STATUS_PASSWORD_EXPIRED ? IDS_LOGONUI_PASSWORD_EXPIRED
                                                             : IDS_LOGONUI_PASSWORD_MUST_CHANGE, NULL);
                Release(Done);
                return;
            }
            if (Done->Status == STATUS_ACCOUNT_LOCKED_OUT)
            {
                Error = IDS_LOGONUI_LOCKED_OUT;
            }
            else if (Done->Status == STATUS_ACCOUNT_RESTRICTION)
            {
                switch (Done->SubStatus)
                {
                    case STATUS_ACCOUNT_DISABLED: Error = IDS_LOGONUI_DISABLED; break;
                    case STATUS_ACCOUNT_LOCKED_OUT: Error = IDS_LOGONUI_LOCKED_OUT; break;
                    case STATUS_ACCOUNT_EXPIRED: Error = IDS_LOGONUI_ACCOUNT_EXPIRED; break;
                    case STATUS_INVALID_LOGON_HOURS: Error = IDS_LOGONUI_LOGON_HOURS; break;
                    case STATUS_INVALID_WORKSTATION: Error = IDS_LOGONUI_WORKSTATION; break;
                    default: Error = IDS_LOGONUI_RESTRICTION; break;
                }
            }
            else if (Done->Status == STATUS_LOGON_FAILURE || Done->Status == STATUS_WRONG_PASSWORD ||
                     Done->Status == STATUS_NO_SUCH_USER)
            {
                Error = IDS_LOGONUI_WRONG_PASSWORD;
            }
            else
            {
                Error = IDS_LOGONUI_FAILED;
            }
        }
        else if (Done->Kind == Job::Unlock)
        {
            if (Done->Action == WLX_SAS_ACTION_UNLOCK_WKSTA)
            {
                Finish(WLX_SAS_ACTION_UNLOCK_WKSTA, Done);
                return;
            }
            if (NT_SUCCESS(Done->Status))
                Error = IDS_LOGONUI_UNLOCK_OTHER;
            else if (Done->Status == STATUS_ACCOUNT_LOCKED_OUT)
                Error = IDS_LOGONUI_LOCKED_OUT;
            else if (Done->Status == STATUS_ACCOUNT_RESTRICTION && Done->SubStatus == STATUS_ACCOUNT_DISABLED)
                Error = IDS_LOGONUI_DISABLED;
            else if (Done->Status == STATUS_ACCOUNT_RESTRICTION)
                Error = IDS_LOGONUI_RESTRICTION;
            else
                Error = IDS_LOGONUI_WRONG_PASSWORD;
        }
        else if (Done->Kind == Job::Change)
        {
            if (NT_SUCCESS(Done->Status))
            {
                if (ExpiredFlow)
                {
                    LogonJob *Again = NewJob(Job::Logon);

                    if (Again)
                    {
                        StringCchCopyW(Again->User, _countof(Again->User), Done->User);
                        StringCchCopyW(Again->Domain, _countof(Again->Domain), Done->Domain);
                        StringCchCopyW(Again->Password, _countof(Again->Password), Done->NewPassword);
                        ExpiredFlow = FALSE;
                        SecureZeroMemory(Context->Password, sizeof(Context->Password));
                        Release(Done);
                        Current = Page::SignIn;
                        Layout(0);
                        BeginJob(Again);
                        return;
                    }
                }
                Completed = TRUE;
                CompletedAction = WLX_SAS_ACTION_PWD_CHANGED;
                SetMessage(IDS_LOGONUI_CHANGED, NULL);
                Release(Done);
                SetTimer(Window, LOGONUI_TIMER_FINISH, LOGONUI_CONFIRM_MS, NULL);
                InvalidateRect(Window, NULL, FALSE);
                return;
            }
            Retry = ExpiredFlow ? NewEdit : OldEdit;
            if (Done->Status == STATUS_PASSWORD_RESTRICTION)
                Error = IDS_LOGONUI_PASSWORD_RULES;
            else if (Done->Status == STATUS_WRONG_PASSWORD || Done->Status == STATUS_LOGON_FAILURE)
                Error = IDS_LOGONUI_WRONG_PASSWORD;
            else if (Done->Status == STATUS_ACCOUNT_LOCKED_OUT)
                Error = IDS_LOGONUI_LOCKED_OUT;
            else
                Error = IDS_LOGONUI_CHANGE_FAILED;
        }

        if (Current == Page::SignIn && Passwordless())
        {
            if (Error == IDS_LOGONUI_WRONG_PASSWORD)
            {
                NoPassword[Selected] = FALSE;
                Layout(0);
            }
            else
            {
                Retry = NULL;
            }
        }
        ShowFields(TRUE);
        Fail(Error, Done->Kind == Job::Unlock ? Context->UserName : NULL, Retry);
        if (Error == IDS_LOGONUI_WRONG_PASSWORD && Done->Hint[0])
        {
            StringCchCatW(Message, _countof(Message), L"\n");
            StringCchCatW(Message, _countof(Message), Done->Hint);
        }
        Release(Done);
    }

    void Release(LogonJob *Done)
    {
        SecureZeroMemory(Done, sizeof(*Done));
        HeapFree(GetProcessHeap(), 0, Done);
    }

    void KeepFrame(BOOL Welcome)
    {
        WNDCLASSW Class = {};
        UINT32 *Bits;

        LogonUiRemoveCover();
        if (Welcome)
            Busy = TRUE;
        Animation = Anim::None;
        Layout(0);
        Render();
        GdiFlush();
        g_CoverBitmap = CreateDib(Width, Height, &Bits);
        if (!g_CoverBitmap)
            return;
        CopyMemory(Bits, FrameBits, (SIZE_T)Width * Height * sizeof(UINT32));

        Class.lpfnWndProc = CoverWindowProc;
        Class.hInstance = hDllInstance;
        Class.hCursor = LoadCursorW(NULL, IDC_ARROW);
        Class.lpszClassName = LOGONUI_COVER_CLASS;
        RegisterClassW(&Class);
        g_Cover = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, LOGONUI_COVER_CLASS, L"",
                                  WS_POPUP, 0, 0, Width, Height, NULL, NULL, hDllInstance, NULL);
        if (!g_Cover)
        {
            DeleteObject(g_CoverBitmap);
            g_CoverBitmap = NULL;
            return;
        }
        SetWindowPos(g_Cover, HWND_TOPMOST, 0, 0, Width, Height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(g_Cover);
    }

    void Finish(INT Action, LogonJob *Done)
    {
        if (Done)
            Release(Done);
        KillTimer(Window, LOGONUI_TIMER_ANIMATION);
        KillTimer(Window, LOGONUI_TIMER_CLOCK);
        KillTimer(Window, LOGONUI_TIMER_FINISH);
        if (Mode == LogonUiSignIn || Mode == LogonUiUnlock)
        {
            if (Action == WLX_SAS_ACTION_LOGON || Action == WLX_SAS_ACTION_UNLOCK_WKSTA)
                KeepFrame(TRUE);
            else if (Action == WLX_SAS_ACTION_NONE)
                KeepFrame(FALSE);
        }
        EndDialog(Window, Action);
    }

    void Dismiss()
    {
        if (Current != Page::Lock || CadRequired || Dismissed)
            return;
        Dismissed = TRUE;
        Context->pWlxFuncs->WlxSasNotify(Context->hWlx, WLX_SAS_TYPE_CTRL_ALT_DEL);
    }

    void PowerMenu()
    {
        HMENU Menu = CreatePopupMenu();
        WCHAR ShutDown[64], Restart[64];
        int Command;

        if (!Menu)
            return;
        LoadText(IDS_SHUTDOWN_SHUTDOWN, ShutDown, _countof(ShutDown));
        LoadText(IDS_SHUTDOWN_RESTART, Restart, _countof(Restart));
        AppendMenuW(Menu, MF_STRING, WLX_SAS_ACTION_SHUTDOWN_POWER_OFF, ShutDown);
        AppendMenuW(Menu, MF_STRING, WLX_SAS_ACTION_SHUTDOWN_REBOOT, Restart);
        SetForegroundWindow(Window);
        Command = TrackPopupMenuEx(Menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_BOTTOMALIGN, Power.right,
                                   Power.top - S(8), Window, NULL);
        DestroyMenu(Menu);
        if (Command)
            Finish(Command, NULL);
    }

    void Activate(int Target)
    {
        if (Busy || Completed || Animation == Anim::Dismiss)
            return;
        if (Current == Page::Lock && Target != HitPower)
        {
            Dismiss();
        }
        else if (Target == HitPower && CanPowerOff)
        {
            if (Current == Page::Options && (GetKeyState(VK_CONTROL) & 0x8000))
                SwitchPage(Page::Emergency);
            else
                PowerMenu();
        }
        else if (Target == HitEmergency && Current == Page::Emergency)
        {
            BOOLEAN Old;

            RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, TRUE, FALSE, &Old);
            NtShutdownSystem(ShutdownReboot);
            RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, Old, FALSE, &Old);
        }
        else if (Target == HitCancel && Current == Page::Emergency)
        {
            SwitchPage(Page::Options);
        }
        else if (Target == HitCancel && Current == Page::Power)
        {
            Finish(0, NULL);
        }
        else if (Target >= HitOption && Target < HitOption + OptionCount && Current == Page::Power)
        {
            PowerAction = PowerChoices[PowerRows[Target - HitOption]].Action;
            Finish((INT)PowerChoices[PowerRows[Target - HitOption]].Flag, NULL);
        }
        else if (Target == HitSubmit)
        {
            if (Current == Page::Password)
                SubmitPassword();
            else
                SubmitSignIn();
        }
        else if (Target == HitOk && Current == Page::Legal)
        {
            SwitchPage(Page::SignIn);
        }
        else if (Target == HitCancel && Current == Page::Options)
        {
            Finish(WLX_SAS_ACTION_NONE, NULL);
        }
        else if (Target >= HitUser && Target <= HitUser + UserCount && Current == Page::SignIn)
        {
            if (Selected != Target - HitUser)
            {
                Selected = Target - HitUser;
                SetWindowTextW(PasswordEdit, L"");
                SetWindowTextW(UserEdit, L"");
                SwitchPage(Page::SignIn);
            }
        }
        else if (Target >= HitOption && Target < HitOption + OptionCount && Current == Page::Options)
        {
            switch (Options[Target - HitOption])
            {
                case OptionLock: Finish(WLX_SAS_ACTION_LOCK_WKSTA, NULL); break;
                case OptionSignOut: Finish(WLX_SAS_ACTION_LOGOFF, NULL); break;
                case OptionTaskManager: Finish(WLX_SAS_ACTION_TASKLIST, NULL); break;
                case OptionPassword: SwitchPage(Page::Password); break;
            }
        }
    }

    void Back()
    {
        if (Busy || Completed)
            return;
        if (Current == Page::Lock)
            Dismiss();
        else if (Current == Page::Emergency)
            SwitchPage(Page::Options);
        else if (Current == Page::Power)
            Finish(0, NULL);
        else if (Current == Page::Password && Mode == LogonUiOptions)
            SwitchPage(Page::Options);
        else if (Current == Page::Password && ExpiredFlow)
        {
            ExpiredFlow = FALSE;
            SecureZeroMemory(Context->Password, sizeof(Context->Password));
            SwitchPage(Page::SignIn);
        }
        else
            Finish(WLX_SAS_ACTION_NONE, NULL);
    }

    int HitTest(POINT Point)
    {
        int i;

        if (Busy)
            return HitNone;
        if (Current == Page::Lock)
        {
            if (CanPowerOff && !Dismissed && Animation != Anim::Dismiss && PtInRect(&Power, Point))
                return HitPower;
            return HitNone;
        }
        if (Current == Page::Emergency)
        {
            if (PtInRect(&Ok, Point))
                return HitEmergency;
            if (PtInRect(&Cancel, Point))
                return HitCancel;
            return HitNone;
        }
        if (CanPowerOff && Current != Page::Legal && Current != Page::Power && PtInRect(&Power, Point))
            return HitPower;
        if ((Current == Page::SignIn || Current == Page::Password) && PtInRect(&Submit, Point))
            return HitSubmit;
        if ((Current == Page::Options || Current == Page::Power) && PtInRect(&Cancel, Point))
            return HitCancel;
        if (Current == Page::Legal && PtInRect(&Ok, Point))
            return HitOk;
        if (Current == Page::SignIn && UserCount)
        {
            for (i = 0; i <= UserCount; i++)
            {
                if (PtInRect(&UserItems[i], Point))
                    return HitUser + i;
            }
        }
        if (Current == Page::Options || Current == Page::Power)
        {
            for (i = 0; i < OptionCount; i++)
            {
                if (PtInRect(&OptionRows[i], Point))
                    return HitOption + i;
            }
        }
        return HitNone;
    }

    void MouseMove(POINT Point)
    {
        int Target = HitTest(Point);

        if (Target != Hover)
        {
            Hover = Target;
            if (Target >= HitOption)
                Focus = Target - HitOption;
            InvalidateRect(Window, NULL, FALSE);
        }
    }

    void Key(WPARAM Key)
    {
        if (Current == Page::Lock)
        {
            if (Key != VK_SHIFT && Key != VK_CONTROL && Key != VK_MENU && Key != VK_LWIN && Key != VK_RWIN &&
                Key != VK_CAPITAL)
                Dismiss();
        }
        else if ((Current == Page::Options || Current == Page::Power) && OptionCount)
        {
            if (Key == VK_DOWN || Key == VK_TAB)
                Focus = (Focus + 1) % OptionCount;
            else if (Key == VK_UP)
                Focus = (Focus + OptionCount - 1) % OptionCount;
            Hover = HitOption + Focus;
            InvalidateRect(Window, NULL, FALSE);
        }
    }

    void Command(WORD Id, WORD Code)
    {
        if (Code == EN_CHANGE)
        {
            HWND Edit = GetDlgItem(Window, Id);

            if (Edit)
                InvalidateRect(Edit, NULL, TRUE);
            if (Message[0] && !Busy)
            {
                Message[0] = UNICODE_NULL;
                InvalidateRect(Window, NULL, FALSE);
            }
            return;
        }
        if (Id == IDOK)
        {
            if ((Current == Page::Options || Current == Page::Power) && OptionCount)
                Activate(HitOption + Focus);
            else if (Current == Page::Emergency)
                Activate(HitEmergency);
            else if (Current == Page::Legal)
                Activate(HitOk);
            else if (Current == Page::Lock)
                Dismiss();
            else if (Current == Page::SignIn && OtherUser() && GetFocus() == UserEdit)
                SetFocus(PasswordEdit);
            else
                Activate(HitSubmit);
        }
        else if (Id == IDCANCEL)
        {
            Back();
        }
    }

    void DrawField(Gdiplus::Graphics &Graphics, HWND Edit, const RECT &Field, BOOL Arrow)
    {
        RECT Line = Field;

        FillRound(Graphics, Field, (float)S(6), 255, RGB(255, 255, 255));
        if (GetFocus() == Edit)
        {
            Line.top = Line.bottom - S(2);
            InflateRect(&Line, -S(4), 0);
            FillRound(Graphics, Line, 0, 255, LOGONUI_ACCENT);
        }
        if (Arrow)
        {
            RECT Button = {Field.right - S(38), Field.top + S(4), Field.right - S(4), Field.bottom - S(4)};

            FillRound(Graphics, Button, (float)S(4), 255, Hover == HitSubmit ? LOGONUI_ACCENT_HOT : LOGONUI_ACCENT);
            Glyph(Graphics, L"arrow_right_16_regular", Button, S(16), RGB(255, 255, 255));
        }
    }

    void DrawBusy(Gdiplus::Graphics &Graphics, HDC Dc, int Top)
    {
        float Time = (GetTickCount64() - BusyStart) / 1000.0f;
        int Center = Width / 2, Radius = S(3), i;
        WCHAR Welcome[64];
        RECT Label = {Center - S(300), Top + S(24), Center + S(300), Top + S(64)};

        for (i = 0; i < 5; i++)
        {
            float Phase = fmodf(Time * 0.8f - i * 0.11f, 1.0f);
            float Eased = Phase < 0.5f ? EaseInOut(Phase * 2.0f) * 0.5f : 0.5f + EaseInOut((Phase - 0.5f) * 2.0f) * 0.5f;
            int x = Center - S(90) + (int)(Eased * S(180));
            RECT Dot = {x - Radius, Top + S(8) - Radius, x + Radius, Top + S(8) + Radius};
            BYTE Alpha = (BYTE)(255 * min(1.0f, 4.0f * min(Phase, 1.0f - Phase) + 0.15f));

            FillCircle(Graphics, Dot, Alpha, RGB(255, 255, 255));
        }
        LoadText(IDS_LOGONUI_WELCOME, Welcome, _countof(Welcome));
        Text(Dc, NameFont, Welcome, Label, DT_CENTER | DT_SINGLELINE, RGB(255, 255, 255));
    }

    void DrawLock(HDC Dc, int Shift)
    {
        SYSTEMTIME Now;
        WCHAR Date[96] = L"", Hint[96];
        int Center = Width / 2, Top = (int)(Height * 0.17f) + Shift;
        RECT Clock = {0, Top, Width, Top + S(170)};
        RECT DateRect = {0, Clock.bottom - S(4), Width, Clock.bottom + S(46)};
        RECT HintRect = {Center - S(400), Height - S(78), Center + S(400), Height - S(46)};

        GetLocalTime(&Now);
        GetDateFormatW(LOCALE_USER_DEFAULT, 0, &Now, L"dddd, MMMM d", Date, _countof(Date));
        Text(Dc, ClockFont, ClockText, Clock, DT_CENTER | DT_SINGLELINE | DT_BOTTOM, RGB(255, 255, 255));
        Text(Dc, DateFont, Date, DateRect, DT_CENTER | DT_SINGLELINE, RGB(244, 244, 244));
        LoadText(CadRequired ? (Locked() ? IDS_LOGONUI_HINT_CAD_UNLOCK : IDS_LOGONUI_HINT_CAD_SIGNIN)
                             : (Locked() ? IDS_LOGONUI_HINT_UNLOCK : IDS_LOGONUI_HINT_SIGNIN),
                 Hint, _countof(Hint));
        Text(Dc, SmallFont, Hint, HintRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER, RGB(228, 228, 228));
    }

    void DrawPower(Gdiplus::Graphics &Graphics)
    {
        if (!CanPowerOff)
            return;
        if (Hover == HitPower)
            FillCircle(Graphics, Power, 40, RGB(255, 255, 255));
        Glyph(Graphics, L"power_20_regular", Power, S(22), RGB(255, 255, 255));
    }

    void DrawSignIn(Gdiplus::Graphics &Graphics, HDC Dc)
    {
        RECT Ring = Avatar;
        int i;

        InflateRect(&Ring, S(2), S(2));
        FillCircle(Graphics, Ring, 70, RGB(255, 255, 255));
        FillCircle(Graphics, Avatar, 46, RGB(255, 255, 255));
        Glyph(Graphics, OtherUser() && Mode == LogonUiSignIn ? L"person_add_20_regular" : L"person_20_regular",
              Avatar, S(84), RGB(255, 255, 255));

        if (OtherUser())
        {
            WCHAR Other[64];

            LoadText(IDS_LOGONUI_OTHER_USER, Other, _countof(Other));
            Text(Dc, NameFont, Other, Name, DT_CENTER | DT_SINGLELINE | DT_VCENTER, RGB(255, 255, 255));
        }
        else
        {
            Text(Dc, NameFont, SelectedName(), Name, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
                 RGB(255, 255, 255));
        }

        if (Busy)
        {
            DrawBusy(Graphics, Dc, Name.bottom + S(18));
        }
        else
        {
            if (OtherUser())
                DrawField(Graphics, UserEdit, UserField, FALSE);
            if (Passwordless())
            {
                WCHAR Label[64];

                FillRound(Graphics, Submit, (float)S(8), 255, Hover == HitSubmit ? LOGONUI_ACCENT_HOT : LOGONUI_ACCENT);
                LoadText(IDS_LOGONUI_SIGN_IN, Label, _countof(Label));
                Text(Dc, BodyFont, Label, Submit, DT_CENTER | DT_SINGLELINE | DT_VCENTER, RGB(255, 255, 255));
            }
            else
            {
                DrawField(Graphics, PasswordEdit, PasswordField, TRUE);
            }
            DrawMessage(Dc);
        }

        if (UserCount)
        {
            for (i = 0; i <= UserCount; i++)
            {
                RECT Item = UserItems[i], Badge, Label;
                WCHAR Other[64];

                if (i == Selected)
                    FillRound(Graphics, Item, (float)S(8), 46, RGB(255, 255, 255));
                else if (Hover == HitUser + i)
                    FillRound(Graphics, Item, (float)S(8), 26, RGB(255, 255, 255));
                SetRect(&Badge, Item.left + S(8), Item.top + S(8), Item.left + S(44), Item.top + S(44));
                FillCircle(Graphics, Badge, 52, RGB(255, 255, 255));
                Glyph(Graphics, i == UserCount ? L"person_add_20_regular" : L"person_20_regular", Badge, S(18),
                      RGB(255, 255, 255));
                SetRect(&Label, Badge.right + S(12), Item.top, Item.right - S(8), Item.bottom);
                if (i == UserCount)
                    LoadText(IDS_LOGONUI_OTHER_USER, Other, _countof(Other));
                Text(Dc, BodyFont, i == UserCount ? Other : Users[i], Label,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, RGB(255, 255, 255));
            }
        }
        DrawPower(Graphics);
    }

    void DrawMessage(HDC Dc)
    {
        WCHAR Caps[64];

        if (Message[0])
        {
            Text(Dc, SmallFont, Message, MessageRect, DT_CENTER | DT_WORDBREAK, RGB(255, 255, 255));
        }
        else if (CapsLock && (Current != Page::SignIn || !Passwordless()))
        {
            LoadText(IDS_LOGONUI_CAPSLOCK, Caps, _countof(Caps));
            Text(Dc, SmallFont, Caps, MessageRect, DT_CENTER | DT_SINGLELINE, RGB(236, 236, 236));
        }
    }

    void DrawPassword(Gdiplus::Graphics &Graphics, HDC Dc)
    {
        WCHAR Heading[96], Account[UNLEN + MAX_COMPUTERNAME_LENGTH + 4];

        LoadText(IDS_LOGONUI_CHANGE_PASSWORD, Heading, _countof(Heading));
        Text(Dc, NameFont, Heading, Title, DT_CENTER | DT_SINGLELINE | DT_VCENTER, RGB(255, 255, 255));
        if (!ExpiredFlow)
        {
            StringCchPrintfW(Account, _countof(Account), L"%s\\%s", Context->DomainName, Context->UserName);
            Text(Dc, BodyFont, Account, Subtitle, DT_CENTER | DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS,
                 RGB(232, 232, 232));
        }
        else
        {
            Text(Dc, BodyFont, ExpiredUser, Subtitle, DT_CENTER | DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS,
                 RGB(232, 232, 232));
        }
        if (Busy)
        {
            DrawBusy(Graphics, Dc, NewField.top);
            return;
        }
        if (Completed)
        {
            RECT Done = {MessageRect.left, NewField.top, MessageRect.right, NewField.top + S(64)};

            Text(Dc, BodyFont, Message, Done, DT_CENTER | DT_WORDBREAK, RGB(255, 255, 255));
            return;
        }
        if (!ExpiredFlow)
            DrawField(Graphics, OldEdit, OldField, FALSE);
        DrawField(Graphics, NewEdit, NewField, FALSE);
        DrawField(Graphics, ConfirmEdit, ConfirmField, TRUE);
        DrawMessage(Dc);
    }

    void DrawOptions(Gdiplus::Graphics &Graphics, HDC Dc)
    {
        static const UINT Labels[] = {IDS_LOGONUI_LOCK, IDS_LOGONUI_SIGN_OUT, IDS_LOGONUI_CHANGE_PASSWORD,
                                      IDS_LOGONUI_TASK_MANAGER};
        WCHAR Label[96];
        RECT Row;
        int i;

        for (i = 0; i < OptionCount; i++)
        {
            Row = OptionRows[i];
            if (Hover == HitOption + i || (Hover < HitOption && Focus == i))
                FillRound(Graphics, Row, (float)S(10), Hover == HitOption + i ? 40 : 28, RGB(255, 255, 255));
            LoadText(Current == Page::Power ? PowerChoices[PowerRows[i]].Text : Labels[Options[i]], Label,
                     _countof(Label));
            Row.left += S(22);
            Text(Dc, OptionFont, Label, Row, DT_LEFT | DT_SINGLELINE | DT_VCENTER, RGB(255, 255, 255));
        }
        FillRound(Graphics, Cancel, (float)S(8), Hover == HitCancel ? 64 : 38, RGB(255, 255, 255));
        LoadText(IDS_LOGONUI_CANCEL, Label, _countof(Label));
        Text(Dc, BodyFont, Label, Cancel, DT_CENTER | DT_SINGLELINE | DT_VCENTER, RGB(255, 255, 255));
        DrawPower(Graphics);
    }

    void DrawEmergency(Gdiplus::Graphics &Graphics, HDC Dc)
    {
        WCHAR Heading[96], Body[256], Label[32];

        LoadText(IDS_LOGONUI_EMERGENCY_TITLE, Heading, _countof(Heading));
        LoadText(IDS_LOGONUI_EMERGENCY_TEXT, Body, _countof(Body));
        Text(Dc, NameFont, Heading, Title, DT_CENTER | DT_SINGLELINE | DT_VCENTER, RGB(255, 255, 255));
        Text(Dc, BodyFont, Body, LegalRect, DT_CENTER | DT_WORDBREAK, RGB(244, 244, 244));
        FillRound(Graphics, Ok, (float)S(8), 255, Hover == HitEmergency ? RGB(214, 58, 48) : RGB(196, 43, 28));
        LoadText(IDS_LOGONUI_OK, Label, _countof(Label));
        Text(Dc, BodyFont, Label, Ok, DT_CENTER | DT_SINGLELINE | DT_VCENTER, RGB(255, 255, 255));
        FillRound(Graphics, Cancel, (float)S(8), Hover == HitCancel ? 64 : 38, RGB(255, 255, 255));
        LoadText(IDS_LOGONUI_CANCEL, Label, _countof(Label));
        Text(Dc, BodyFont, Label, Cancel, DT_CENTER | DT_SINGLELINE | DT_VCENTER, RGB(255, 255, 255));
    }

    void DrawLegal(Gdiplus::Graphics &Graphics, HDC Dc)
    {
        WCHAR Label[32];

        Text(Dc, NameFont, LegalCaption, Title, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
             RGB(255, 255, 255));
        Text(Dc, BodyFont, LegalText, LegalRect, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS, RGB(244, 244, 244));
        FillRound(Graphics, Ok, (float)S(8), 255, Hover == HitOk ? LOGONUI_ACCENT_HOT : LOGONUI_ACCENT);
        LoadText(IDS_LOGONUI_OK, Label, _countof(Label));
        Text(Dc, BodyFont, Label, Ok, DT_CENTER | DT_SINGLELINE | DT_VCENTER, RGB(255, 255, 255));
    }

    void DrawContent(HDC Dc, int Shift)
    {
        Gdiplus::Graphics Graphics(Dc);

        Graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Graphics.TranslateTransform(0, (Gdiplus::REAL)Shift);
        switch (Current)
        {
            case Page::Lock:
                Graphics.ResetTransform();
                DrawLock(Dc, Shift);
                DrawPower(Graphics);
                break;
            case Page::SignIn:
                ShiftRects(Shift);
                DrawSignIn(Graphics, Dc);
                ShiftRects(-Shift);
                break;
            case Page::Password:
                ShiftRects(Shift);
                DrawPassword(Graphics, Dc);
                ShiftRects(-Shift);
                break;
            case Page::Options:
            case Page::Power:
                ShiftRects(Shift);
                DrawOptions(Graphics, Dc);
                ShiftRects(-Shift);
                break;
            case Page::Emergency:
                ShiftRects(Shift);
                DrawEmergency(Graphics, Dc);
                ShiftRects(-Shift);
                break;
            case Page::Legal:
                ShiftRects(Shift);
                DrawLegal(Graphics, Dc);
                ShiftRects(-Shift);
                break;
        }
    }

    void ShiftRects(int Shift)
    {
        RECT *Rects[] = {&Avatar, &Name, &Title, &Subtitle, &UserField, &PasswordField, &OldField, &NewField,
                         &ConfirmField, &Submit, &MessageRect, &Ok, &LegalRect, &Cancel};
        int i;

        for (RECT *Rect : Rects)
            OffsetRect(Rect, 0, Shift);
        for (i = 0; i < OptionCount; i++)
            OffsetRect(&OptionRows[i], 0, Shift);
    }

    void CopyBackdrop(HDC Target, HBITMAP Source)
    {
        HGDIOBJ Old = SelectObject(BackdropDc, Source);

        BitBlt(Target, 0, 0, Width, Height, BackdropDc, 0, 0, SRCCOPY);
        SelectObject(BackdropDc, Old);
    }

    void Render()
    {
        BOOL Sharp = Current == Page::Lock;
        int Shift = 0;
        BYTE Alpha = 255;

        if (Animation == Anim::Dismiss)
        {
            int Offset = (int)(EaseInOut(Progress) * Height);

            CopyBackdrop(FrameDc, Wall->Soft);
            DrawContent(FrameDc, 0);
            CopyBackdrop(LayerDc, Wall->Sharp);
            DrawLock(LayerDc, 0);
            BitBlt(FrameDc, 0, -Offset, Width, Height, LayerDc, 0, 0, SRCCOPY);
            Gdiplus::Graphics Graphics(FrameDc);

            Graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            DrawPower(Graphics);
            return;
        }

        CopyBackdrop(FrameDc, Sharp ? Wall->Sharp : Wall->Soft);
        if (Animation == Anim::Enter)
        {
            float Eased = EaseOut(Progress);

            Shift = (int)((1.0f - Eased) * S(22));
            Alpha = (BYTE)(255 * min(1.0f, Progress * 1.6f));
        }
        if (Alpha == 255)
        {
            DrawContent(FrameDc, Shift);
            return;
        }

        BitBlt(LayerDc, 0, 0, Width, Height, FrameDc, 0, 0, SRCCOPY);
        DrawContent(LayerDc, Shift);
        if (Animation == Anim::Enter && BeforeBitmap)
        {
            GdiFlush();
            CopyMemory(FrameBits, BeforeBits, (SIZE_T)Width * Height * sizeof(UINT32));
        }
        BLENDFUNCTION Blend = {AC_SRC_OVER, 0, Alpha, 0};
        GdiAlphaBlend(FrameDc, 0, 0, Width, Height, LayerDc, 0, 0, Width, Height, Blend);
    }

    void Paint()
    {
        PAINTSTRUCT Paint;
        HDC Dc = BeginPaint(Window, &Paint);

        Render();
        GdiFlush();
        BitBlt(Dc, 0, 0, Width, Height, FrameDc, 0, 0, SRCCOPY);
        EndPaint(Window, &Paint);
    }

    void ClockTick()
    {
        BOOL Caps = (GetKeyState(VK_CAPITAL) & 1) != 0;

        UpdateClock();
        if (Caps != CapsLock)
        {
            CapsLock = Caps;
            InvalidateRect(Window, NULL, FALSE);
        }
    }
};

static VOID
RunLogon(PGINA_CONTEXT Context, LogonJob *Pending)
{
    Pending->Status = DoLoginTasks(Context, Pending->User, Pending->Domain, Pending->Password, &Pending->SubStatus);
    if (NT_SUCCESS(Pending->Status))
    {
        if (!CreateProfile(Context, Pending->User, Pending->Domain, Pending->Password))
            Pending->Status = STATUS_UNSUCCESSFUL;
        SecureZeroMemory(Context->Password, sizeof(Context->Password));
    }
    else if (Pending->Status == STATUS_LOGON_FAILURE)
    {
        GetUserPasswordHint(Pending->User, Pending->Hint, _countof(Pending->Hint));
    }
}

static DWORD WINAPI
LogonJobThread(PVOID Parameter)
{
    LogonJob *Pending = (LogonJob *)Parameter;
    PGINA_CONTEXT Context = Pending->Screen->Context;
    HWND Window = Pending->Screen->Window;

    if (Pending->Kind == Job::Logon)
    {
        RunLogon(Context, Pending);
    }
    else if (Pending->Kind == Job::Unlock)
    {
        Pending->Status = DoUnlock(Context, Pending->User, Pending->Domain, Pending->Password,
                                   &Pending->SubStatus, &Pending->Action);
        if (Pending->Status == STATUS_LOGON_FAILURE)
            GetUserPasswordHint(Pending->User, Pending->Hint, _countof(Pending->Hint));
    }
    else if (Pending->Kind == Job::Change)
    {
        Pending->Status = ConnectToLsa(Context);
        if (NT_SUCCESS(Pending->Status))
        {
            Pending->Status = MyChangePassword(Context->LsaHandle, Context->AuthenticationPackage, Pending->Domain,
                                               Pending->User, Pending->Password, Pending->NewPassword);
        }
        if (NT_SUCCESS(Pending->Status) && !_wcsicmp(Pending->User, Context->UserName))
            SecureZeroMemory(Context->Password, sizeof(Context->Password));
    }

    PostMessageW(Window, WM_LOGONUI_DONE, 0, (LPARAM)Pending);
    return 0;
}

static INT_PTR CALLBACK
LogonUiDialogProc(HWND Dialog, UINT Message, WPARAM wParam, LPARAM lParam)
{
    LogonScreen *Screen = (LogonScreen *)GetWindowLongPtrW(Dialog, DWLP_USER);

    if (Message == WM_INITDIALOG)
    {
        Screen = (LogonScreen *)lParam;
        SetWindowLongPtrW(Dialog, DWLP_USER, (LONG_PTR)Screen);
        if (!Screen->Initialize(Dialog))
            EndDialog(Dialog, -1);
        return FALSE;
    }
    if (!Screen)
        return FALSE;

    switch (Message)
    {
        case WM_ERASEBKGND:
            SetWindowLongPtrW(Dialog, DWLP_MSGRESULT, TRUE);
            return TRUE;

        case WM_PAINT:
            Screen->Paint();
            return TRUE;

        case WM_TIMER:
            if (wParam == LOGONUI_TIMER_ANIMATION)
                Screen->Tick();
            else if (wParam == LOGONUI_TIMER_CLOCK)
                Screen->ClockTick();
            else if (wParam == LOGONUI_TIMER_FINISH)
                Screen->Finish(Screen->CompletedAction, NULL);
            return TRUE;

        case WM_CTLCOLOREDIT:
            SetTextColor((HDC)wParam, RGB(24, 24, 24));
            SetBkColor((HDC)wParam, RGB(255, 255, 255));
            return (INT_PTR)Screen->FieldBrush;

        case WM_MOUSEMOVE:
        {
            POINT Point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            Screen->MouseMove(Point);
            return TRUE;
        }

        case WM_SETCURSOR:
        {
            POINT Point;

            GetCursorPos(&Point);
            ScreenToClient(Dialog, &Point);
            if (LOWORD(lParam) == HTCLIENT && (HWND)wParam == Dialog)
            {
                SetCursor(LoadCursorW(NULL, Screen->HitTest(Point) != HitNone ? IDC_HAND : IDC_ARROW));
                SetWindowLongPtrW(Dialog, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }
            return FALSE;
        }

        case WM_LBUTTONDOWN:
        {
            POINT Point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            if (Screen->Current == Page::Lock && Screen->HitTest(Point) == HitNone)
                Screen->Dismiss();
            return TRUE;
        }

        case WM_LBUTTONUP:
        {
            POINT Point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int Target = Screen->HitTest(Point);

            if (Target != HitNone)
                Screen->Activate(Target);
            return TRUE;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            Screen->Key(wParam);
            return TRUE;

        case WM_COMMAND:
            Screen->Command(LOWORD(wParam), HIWORD(wParam));
            return TRUE;

        case WM_LOGONUI_DONE:
            Screen->JobDone((LogonJob *)lParam);
            return TRUE;

        case WM_ACTIVATE:
            if (LOWORD(wParam) != WA_INACTIVE && Screen->Animation != Anim::Enter && Screen->Animation != Anim::Dismiss)
                Screen->ShowFields(TRUE);
            return FALSE;

        case WM_DESTROY:
            KillTimer(Dialog, LOGONUI_TIMER_ANIMATION);
            KillTimer(Dialog, LOGONUI_TIMER_CLOCK);
            Screen->Destroy();
            return TRUE;
    }

    return FALSE;
}

static BOOL
NoLockScreen(VOID)
{
    DWORD Value = 0, Size = sizeof(Value);

    return RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Personalization", L"NoLockScreen",
                        RRF_RT_REG_DWORD, NULL, &Value, &Size) == ERROR_SUCCESS && Value;
}

static BOOL
HasLegalNotice(VOID)
{
    PCWSTR Names[] = {L"LegalNoticeCaption", L"LegalNoticeText"};
    WCHAR Text[2];
    DWORD Size;
    LSTATUS Error;

    for (PCWSTR Name : Names)
    {
        Size = sizeof(Text);
        Error = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", Name,
                             RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, NULL, Text, &Size);
        if ((Error != ERROR_SUCCESS && Error != ERROR_MORE_DATA) || Size <= sizeof(WCHAR))
            return FALSE;
    }
    return TRUE;
}

extern "C" INT
LogonUiRun(
    _Inout_ PGINA_CONTEXT pgContext,
    _In_ LOGONUI_MODE Mode)
{
    LogonScreen *Screen;
    LogonJob *Failed = NULL;
    INT Result;

    if ((Mode == LogonUiSignIn || Mode == LogonUiUnlock) && pgContext->nPendingAction)
    {
        Result = pgContext->nPendingAction;
        pgContext->nPendingAction = 0;
        return Result;
    }

    if ((Mode == LogonUiNotice || Mode == LogonUiLockedNotice) && pgContext->bDisableCAD && NoLockScreen())
    {
        pgContext->pWlxFuncs->WlxSasNotify(pgContext->hWlx, WLX_SAS_TYPE_CTRL_ALT_DEL);
        return WLX_SAS_ACTION_NONE;
    }

    if (Mode == LogonUiSignIn && pgContext->bAutoAdminLogon && !HasLegalNotice())
    {
        Failed = (LogonJob *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(LogonJob));
        if (Failed)
        {
            DWORD Length = _countof(Failed->Domain);

            Failed->Kind = Job::Logon;
            StringCchCopyW(Failed->User, _countof(Failed->User), pgContext->UserName);
            StringCchCopyW(Failed->Domain, _countof(Failed->Domain), pgContext->DomainName);
            StringCchCopyW(Failed->Password, _countof(Failed->Password), pgContext->Password);
            if (!Failed->Domain[0])
                GetComputerNameW(Failed->Domain, &Length);
            RunLogon(pgContext, Failed);
            pgContext->bAutoAdminLogon = FALSE;
            if (NT_SUCCESS(Failed->Status))
            {
                SecureZeroMemory(Failed, sizeof(*Failed));
                HeapFree(GetProcessHeap(), 0, Failed);
                return WLX_SAS_ACTION_LOGON;
            }
        }
    }

    Screen = new LogonScreen(pgContext, Mode);
    if (!Screen)
    {
        if (Failed)
        {
            SecureZeroMemory(Failed, sizeof(*Failed));
            HeapFree(GetProcessHeap(), 0, Failed);
        }
        return WLX_SAS_ACTION_NONE;
    }
    Screen->Pending = Failed;
    Result = pgContext->pWlxFuncs->WlxDialogBoxParam(pgContext->hWlx,
                                                     pgContext->hDllInstance,
                                                     MAKEINTRESOURCEW(IDD_LOGONUI),
                                                     GetDesktopWindow(),
                                                     LogonUiDialogProc,
                                                     (LPARAM)Screen);
    delete Screen;

    if ((Mode == LogonUiNotice || Mode == LogonUiLockedNotice) &&
        (Result == WLX_SAS_ACTION_SHUTDOWN_POWER_OFF || Result == WLX_SAS_ACTION_SHUTDOWN_REBOOT))
    {
        pgContext->nPendingAction = Result;
        pgContext->pWlxFuncs->WlxSasNotify(pgContext->hWlx, WLX_SAS_TYPE_CTRL_ALT_DEL);
        return WLX_SAS_ACTION_NONE;
    }

    if (Result < WLX_SAS_ACTION_LOGON || Result > WLX_SAS_ACTION_SWITCH_CONSOLE)
        return WLX_SAS_ACTION_NONE;
    return Result;
}

extern "C" VOID
LogonUiRemoveCover(VOID)
{
    if (g_Cover)
        SendMessageW(g_Cover, WM_CLOSE, 0, 0);
}

extern "C" BOOL
LogonUiCoverShown(VOID)
{
    return g_Cover != NULL;
}

extern "C" DWORD
LogonUiPowerDialog(
    _In_opt_ HWND Parent,
    _In_ DWORD AllowedOptions,
    _Inout_ PDWORD Action)
{
    GINA_CONTEXT Context = {};
    LogonScreen *Screen;
    INT_PTR Result;

    if (!(AllowedOptions & (WLX_SHUTDOWN_STATE_POWER_OFF | WLX_SHUTDOWN_STATE_REBOOT | WLX_SHUTDOWN_STATE_SLEEP |
                            WLX_SHUTDOWN_STATE_HIBERNATE | WLX_SHUTDOWN_STATE_LOGOFF)))
        return 0;

    Context.hDllInstance = hDllInstance;
    Context.bDisableCAD = TRUE;
    Screen = new LogonScreen(&Context, LogonUiPower);
    if (!Screen)
        return 0;
    Screen->PowerAllowed = AllowedOptions;
    Screen->PowerAction = *Action;
    Result = DialogBoxParamW(hDllInstance, MAKEINTRESOURCEW(IDD_LOGONUI), Parent, LogonUiDialogProc, (LPARAM)Screen);
    if (Result > 0)
        *Action = Screen->PowerAction;
    delete Screen;
    return Result > 0 ? (DWORD)Result : 0;
}
