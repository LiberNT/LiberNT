/*
 * PROJECT:     LiberNT Magnification API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Magnifier window class and magnification state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <magnification.h>

typedef struct _MAG_WINDOW
{
    RECT Source;
    MAGTRANSFORM Transform;
    MAGCOLOREFFECT ColorEffect;
    BOOL HasColorEffect;
    DWORD FilterMode;
    int FilterCount;
    HWND *FilterList;
    MagImageScalingCallback ScalingCallback;
} MAG_WINDOW, *PMAG_WINDOW;

static HINSTANCE MagInstance;
static CRITICAL_SECTION MagLock;
static LONG MagInitCount;
static ATOM MagClassAtom;

static VOID
MagIdentityTransform(
    _Out_ PMAGTRANSFORM Transform)
{
    ZeroMemory(Transform, sizeof(*Transform));
    Transform->v[0][0] = Transform->v[1][1] = Transform->v[2][2] = 1.0f;
}

static VOID
MagIdentityColorEffect(
    _Out_ PMAGCOLOREFFECT Effect)
{
    int i;

    ZeroMemory(Effect, sizeof(*Effect));
    for (i = 0; i < 5; i++)
        Effect->transform[i][i] = 1.0f;
}

static BOOL
MagIsIdentityColorEffect(
    _In_ const MAGCOLOREFFECT *Effect)
{
    MAGCOLOREFFECT Identity;

    MagIdentityColorEffect(&Identity);
    return memcmp(&Identity, Effect, sizeof(Identity)) == 0;
}

static PMAG_WINDOW
MagGetWindow(
    _In_ HWND hwnd)
{
    DWORD ProcessId = 0;

    if (!MagClassAtom || !IsWindow(hwnd) || GetClassWord(hwnd, GCW_ATOM) != MagClassAtom)
        return NULL;

    GetWindowThreadProcessId(hwnd, &ProcessId);
    if (ProcessId != GetCurrentProcessId())
        return NULL;

    return (PMAG_WINDOW)GetWindowLongPtrW(hwnd, 0);
}

static BYTE
MagClampChannel(
    _In_ float Value)
{
    if (Value <= 0.0f)
        return 0;
    if (Value >= 1.0f)
        return 255;
    return (BYTE)(Value * 255.0f + 0.5f);
}

static VOID
MagApplyColorEffect(
    _Inout_ PDWORD Pixels,
    _In_ SIZE_T Count,
    _In_ const MAGCOLOREFFECT *Effect,
    _In_ BOOL ApplyEffect,
    _In_ BOOL Invert)
{
    SIZE_T i;
    int Column;

    for (i = 0; i < Count; i++)
    {
        float In[5], Out[4];
        DWORD Pixel = Pixels[i];

        In[0] = (float)((Pixel >> 16) & 0xFF) / 255.0f;
        In[1] = (float)((Pixel >> 8) & 0xFF) / 255.0f;
        In[2] = (float)(Pixel & 0xFF) / 255.0f;
        In[3] = 1.0f;
        In[4] = 1.0f;

        for (Column = 0; Column < 4; Column++)
        {
            Out[Column] = ApplyEffect ? In[0] * Effect->transform[0][Column] +
                                        In[1] * Effect->transform[1][Column] +
                                        In[2] * Effect->transform[2][Column] +
                                        In[3] * Effect->transform[3][Column] +
                                        In[4] * Effect->transform[4][Column]
                                      : In[Column];
            if (Invert && Column < 3)
                Out[Column] = 1.0f - Out[Column];
        }

        Pixels[i] = ((DWORD)MagClampChannel(Out[0]) << 16) |
                    ((DWORD)MagClampChannel(Out[1]) << 8) |
                    (DWORD)MagClampChannel(Out[2]);
    }
}

static VOID
MagDrawCursor(
    _In_ HDC hdc,
    _In_ const RECT *Source,
    _In_ float ScaleX,
    _In_ float ScaleY)
{
    CURSORINFO Cursor;
    ICONINFO Icon;
    int X, Y;

    Cursor.cbSize = sizeof(Cursor);
    if (!GetCursorInfo(&Cursor) || !(Cursor.flags & CURSOR_SHOWING) || !Cursor.hCursor)
        return;
    if (!GetIconInfo(Cursor.hCursor, &Icon))
        return;

    X = (int)((Cursor.ptScreenPos.x - Source->left - (LONG)Icon.xHotspot) * ScaleX);
    Y = (int)((Cursor.ptScreenPos.y - Source->top - (LONG)Icon.yHotspot) * ScaleY);
    DrawIconEx(hdc, X, Y, Cursor.hCursor,
               (int)(GetSystemMetrics(SM_CXCURSOR) * ScaleX),
               (int)(GetSystemMetrics(SM_CYCURSOR) * ScaleY),
               0, NULL, DI_NORMAL);

    if (Icon.hbmMask)
        DeleteObject(Icon.hbmMask);
    if (Icon.hbmColor)
        DeleteObject(Icon.hbmColor);
}

static VOID
MagPaint(
    _In_ HWND hwnd,
    _In_ PMAG_WINDOW Mag)
{
    PAINTSTRUCT Paint;
    BITMAPINFO Info;
    RECT Client;
    HDC hdc, Memory, Screen;
    HBITMAP Bitmap;
    HGDIOBJ OldBitmap;
    PDWORD Bits = NULL;
    LONG Style;
    float ScaleX, ScaleY;
    int Width, Height, SourceWidth, SourceHeight;

    hdc = BeginPaint(hwnd, &Paint);
    GetClientRect(hwnd, &Client);
    Width = Client.right - Client.left;
    Height = Client.bottom - Client.top;
    if (Width <= 0 || Height <= 0)
        goto Done;

    ZeroMemory(&Info, sizeof(Info));
    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = Width;
    Info.bmiHeader.biHeight = -Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;

    Bitmap = CreateDIBSection(hdc, &Info, DIB_RGB_COLORS, (PVOID *)&Bits, NULL, 0);
    if (!Bitmap)
        goto Done;

    Memory = CreateCompatibleDC(hdc);
    if (!Memory)
    {
        DeleteObject(Bitmap);
        goto Done;
    }
    OldBitmap = SelectObject(Memory, Bitmap);

    ScaleX = Mag->Transform.v[0][0] > 0.0f ? Mag->Transform.v[0][0] : 1.0f;
    ScaleY = Mag->Transform.v[1][1] > 0.0f ? Mag->Transform.v[1][1] : 1.0f;
    SourceWidth = Mag->Source.right - Mag->Source.left;
    SourceHeight = Mag->Source.bottom - Mag->Source.top;

    if (SourceWidth > 0 && SourceHeight > 0)
    {
        Screen = GetDC(NULL);
        if (Screen)
        {
            SetStretchBltMode(Memory, COLORONCOLOR);
            StretchBlt(Memory, 0, 0, (int)(SourceWidth * ScaleX), (int)(SourceHeight * ScaleY),
                       Screen, Mag->Source.left, Mag->Source.top, SourceWidth, SourceHeight, SRCCOPY);
            ReleaseDC(NULL, Screen);
        }
    }

    GdiFlush();
    Style = GetWindowLongW(hwnd, GWL_STYLE);
    if (Mag->HasColorEffect || (Style & MS_INVERTCOLORS))
    {
        MagApplyColorEffect(Bits, (SIZE_T)Width * Height, &Mag->ColorEffect,
                            Mag->HasColorEffect, (Style & MS_INVERTCOLORS) != 0);
    }

    if (Style & MS_SHOWMAGNIFIEDCURSOR)
        MagDrawCursor(Memory, &Mag->Source, ScaleX, ScaleY);

    BitBlt(hdc, 0, 0, Width, Height, Memory, 0, 0, SRCCOPY);

    SelectObject(Memory, OldBitmap);
    DeleteDC(Memory);
    DeleteObject(Bitmap);

Done:
    EndPaint(hwnd, &Paint);
}

static LRESULT CALLBACK
MagWindowProc(
    _In_ HWND hwnd,
    _In_ UINT Message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PMAG_WINDOW Mag = (PMAG_WINDOW)GetWindowLongPtrW(hwnd, 0);

    switch (Message)
    {
        case WM_NCCREATE:
            Mag = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Mag));
            if (!Mag)
                return FALSE;
            MagIdentityTransform(&Mag->Transform);
            MagIdentityColorEffect(&Mag->ColorEffect);
            Mag->FilterMode = MW_FILTERMODE_EXCLUDE;
            SetWindowLongPtrW(hwnd, 0, (LONG_PTR)Mag);
            break;

        case WM_NCDESTROY:
            if (Mag)
            {
                SetWindowLongPtrW(hwnd, 0, 0);
                HeapFree(GetProcessHeap(), 0, Mag->FilterList);
                HeapFree(GetProcessHeap(), 0, Mag);
            }
            break;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            if (Mag)
            {
                MagPaint(hwnd, Mag);
                return 0;
            }
            break;
    }

    return DefWindowProcW(hwnd, Message, wParam, lParam);
}

BOOL WINAPI
MagInitialize(void)
{
    WNDCLASSEXW Class;
    BOOL Result = TRUE;

    EnterCriticalSection(&MagLock);
    if (MagInitCount == 0)
    {
        ZeroMemory(&Class, sizeof(Class));
        Class.cbSize = sizeof(Class);
        Class.style = CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW;
        Class.lpfnWndProc = MagWindowProc;
        Class.cbWndExtra = sizeof(LONG_PTR);
        Class.hInstance = MagInstance;
        Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        Class.lpszClassName = WC_MAGNIFIERW;
        MagClassAtom = RegisterClassExW(&Class);
        Result = MagClassAtom != 0;
    }
    if (Result)
        MagInitCount++;
    LeaveCriticalSection(&MagLock);
    return Result;
}

BOOL WINAPI
MagUninitialize(void)
{
    EnterCriticalSection(&MagLock);
    if (MagInitCount > 0 && --MagInitCount == 0)
    {
        UnregisterClassW(WC_MAGNIFIERW, MagInstance);
        MagClassAtom = 0;
    }
    LeaveCriticalSection(&MagLock);
    return TRUE;
}

BOOL WINAPI
MagSetWindowSource(
    HWND hwnd,
    RECT rect)
{
    PMAG_WINDOW Mag = MagGetWindow(hwnd);

    if (!Mag)
        return FALSE;

    Mag->Source = rect;
    InvalidateRect(hwnd, NULL, FALSE);
    return TRUE;
}

BOOL WINAPI
MagGetWindowSource(
    HWND hwnd,
    RECT *pRect)
{
    PMAG_WINDOW Mag = MagGetWindow(hwnd);

    if (!Mag)
        return FALSE;
    if (!pRect)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *pRect = Mag->Source;
    return TRUE;
}

BOOL WINAPI
MagSetWindowTransform(
    HWND hwnd,
    PMAGTRANSFORM pTransform)
{
    PMAG_WINDOW Mag = MagGetWindow(hwnd);

    if (!Mag)
        return FALSE;
    if (!pTransform)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Mag->Transform = *pTransform;
    InvalidateRect(hwnd, NULL, FALSE);
    return TRUE;
}

BOOL WINAPI
MagGetWindowTransform(
    HWND hwnd,
    PMAGTRANSFORM pTransform)
{
    PMAG_WINDOW Mag = MagGetWindow(hwnd);

    if (!Mag)
        return FALSE;
    if (!pTransform)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *pTransform = Mag->Transform;
    return TRUE;
}

BOOL WINAPI
MagSetWindowFilterList(
    HWND hwnd,
    DWORD dwFilterMode,
    int count,
    HWND *pHWND)
{
    PMAG_WINDOW Mag = MagGetWindow(hwnd);
    HWND *List = NULL;

    if (!Mag)
        return FALSE;
    if ((dwFilterMode != MW_FILTERMODE_EXCLUDE && dwFilterMode != MW_FILTERMODE_INCLUDE) ||
        count < 0 || (count > 0 && !pHWND))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (count > 0)
    {
        List = HeapAlloc(GetProcessHeap(), 0, count * sizeof(HWND));
        if (!List)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        CopyMemory(List, pHWND, count * sizeof(HWND));
    }

    HeapFree(GetProcessHeap(), 0, Mag->FilterList);
    Mag->FilterList = List;
    Mag->FilterCount = count;
    Mag->FilterMode = dwFilterMode;
    InvalidateRect(hwnd, NULL, FALSE);
    return TRUE;
}

int WINAPI
MagGetWindowFilterList(
    HWND hwnd,
    DWORD *pdwFilterMode,
    int count,
    HWND *pHWND)
{
    PMAG_WINDOW Mag = MagGetWindow(hwnd);

    if (!Mag)
        return 0;

    if (pdwFilterMode)
        *pdwFilterMode = Mag->FilterMode;
    if (count > 0 && pHWND)
        CopyMemory(pHWND, Mag->FilterList, min(count, Mag->FilterCount) * sizeof(HWND));
    return Mag->FilterCount;
}

BOOL WINAPI
MagSetImageScalingCallback(
    HWND hwnd,
    MagImageScalingCallback callback)
{
    PMAG_WINDOW Mag = MagGetWindow(hwnd);

    if (!Mag)
        return FALSE;

    Mag->ScalingCallback = callback;
    return TRUE;
}

MagImageScalingCallback WINAPI
MagGetImageScalingCallback(
    HWND hwnd)
{
    PMAG_WINDOW Mag = MagGetWindow(hwnd);

    return Mag ? Mag->ScalingCallback : NULL;
}

BOOL WINAPI
MagSetColorEffect(
    HWND hwnd,
    PMAGCOLOREFFECT pEffect)
{
    PMAG_WINDOW Mag = MagGetWindow(hwnd);

    if (!Mag)
        return FALSE;

    if (pEffect)
    {
        Mag->ColorEffect = *pEffect;
        Mag->HasColorEffect = !MagIsIdentityColorEffect(pEffect);
    }
    else
    {
        MagIdentityColorEffect(&Mag->ColorEffect);
        Mag->HasColorEffect = FALSE;
    }

    InvalidateRect(hwnd, NULL, FALSE);
    return TRUE;
}

BOOL WINAPI
MagGetColorEffect(
    HWND hwnd,
    PMAGCOLOREFFECT pEffect)
{
    PMAG_WINDOW Mag = MagGetWindow(hwnd);

    if (!Mag)
        return FALSE;
    if (!pEffect)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *pEffect = Mag->ColorEffect;
    return TRUE;
}

BOOL WINAPI
MagSetFullscreenTransform(
    float magLevel,
    int xOffset,
    int yOffset)
{
    if (magLevel == 1.0f && xOffset == 0 && yOffset == 0)
        return TRUE;

    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI
MagGetFullscreenTransform(
    float *pMagLevel,
    int *pxOffset,
    int *pyOffset)
{
    if (!pMagLevel || !pxOffset || !pyOffset)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *pMagLevel = 1.0f;
    *pxOffset = 0;
    *pyOffset = 0;
    return TRUE;
}

BOOL WINAPI
MagSetFullscreenColorEffect(
    PMAGCOLOREFFECT pEffect)
{
    if (!pEffect || MagIsIdentityColorEffect(pEffect))
        return TRUE;

    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI
MagGetFullscreenColorEffect(
    PMAGCOLOREFFECT pEffect)
{
    if (!pEffect)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    MagIdentityColorEffect(pEffect);
    return TRUE;
}

BOOL WINAPI
MagSetInputTransform(
    BOOL fEnabled,
    const LPRECT pRectSource,
    const LPRECT pRectDest)
{
    if (!fEnabled)
        return TRUE;

    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI
MagGetInputTransform(
    BOOL *pfEnabled,
    LPRECT pRectSource,
    LPRECT pRectDest)
{
    if (!pfEnabled || !pRectSource || !pRectDest)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *pfEnabled = FALSE;
    SetRectEmpty(pRectSource);
    SetRectEmpty(pRectDest);
    return TRUE;
}

BOOL WINAPI
MagShowSystemCursor(
    BOOL fShowCursor)
{
    if (fShowCursor)
        return TRUE;

    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI
DllMain(
    HINSTANCE hInstance,
    DWORD Reason,
    LPVOID Reserved)
{
    switch (Reason)
    {
        case DLL_PROCESS_ATTACH:
            MagInstance = hInstance;
            InitializeCriticalSection(&MagLock);
            DisableThreadLibraryCalls(hInstance);
            break;

        case DLL_PROCESS_DETACH:
            if (!Reserved)
                DeleteCriticalSection(&MagLock);
            break;
    }

    return TRUE;
}
