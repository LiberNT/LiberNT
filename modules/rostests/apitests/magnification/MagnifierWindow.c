/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the magnifier window and magnification state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <magnification.h>

typedef BOOL (WINAPI *FN_MagInitialize)(void);
typedef BOOL (WINAPI *FN_MagUninitialize)(void);
typedef BOOL (WINAPI *FN_MagSetWindowSource)(HWND, RECT);
typedef BOOL (WINAPI *FN_MagGetWindowSource)(HWND, RECT *);
typedef BOOL (WINAPI *FN_MagSetWindowTransform)(HWND, PMAGTRANSFORM);
typedef BOOL (WINAPI *FN_MagGetWindowTransform)(HWND, PMAGTRANSFORM);
typedef BOOL (WINAPI *FN_MagSetWindowFilterList)(HWND, DWORD, int, HWND *);
typedef int (WINAPI *FN_MagGetWindowFilterList)(HWND, DWORD *, int, HWND *);
typedef BOOL (WINAPI *FN_MagSetColorEffect)(HWND, PMAGCOLOREFFECT);
typedef BOOL (WINAPI *FN_MagGetColorEffect)(HWND, PMAGCOLOREFFECT);
typedef BOOL (WINAPI *FN_MagGetFullscreenTransform)(float *, int *, int *);
typedef BOOL (WINAPI *FN_MagSetFullscreenTransform)(float, int, int);

static FN_MagInitialize pMagInitialize;
static FN_MagUninitialize pMagUninitialize;
static FN_MagSetWindowSource pMagSetWindowSource;
static FN_MagGetWindowSource pMagGetWindowSource;
static FN_MagSetWindowTransform pMagSetWindowTransform;
static FN_MagGetWindowTransform pMagGetWindowTransform;
static FN_MagSetWindowFilterList pMagSetWindowFilterList;
static FN_MagGetWindowFilterList pMagGetWindowFilterList;
static FN_MagSetColorEffect pMagSetColorEffect;
static FN_MagGetColorEffect pMagGetColorEffect;
static FN_MagGetFullscreenTransform pMagGetFullscreenTransform;
static FN_MagSetFullscreenTransform pMagSetFullscreenTransform;

static BOOL
IsIdentityTransform(const MAGTRANSFORM *T)
{
    int i, j;

    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            if (T->v[i][j] != (i == j ? 1.0f : 0.0f))
                return FALSE;
    return TRUE;
}

static BOOL
IsIdentityEffect(const MAGCOLOREFFECT *E)
{
    int i, j;

    for (i = 0; i < 5; i++)
        for (j = 0; j < 5; j++)
            if (E->transform[i][j] != (i == j ? 1.0f : 0.0f))
                return FALSE;
    return TRUE;
}

static void
TestMagnifier(HWND Host, HWND Mag)
{
    RECT Source = { 10, 20, 110, 70 }, Out;
    MAGTRANSFORM Transform, OutTransform;
    MAGCOLOREFFECT Effect, OutEffect;
    HWND Filter[2] = { NULL, NULL };
    DWORD Mode = 0xdeadbeef;
    float Level = -1.0f;
    int X = -1, Y = -1, Count;
    BOOL Ret;

    SetRect(&Out, -1, -1, -1, -1);
    Ret = pMagGetWindowSource(Mag, &Out);
    ok(Ret == TRUE, "MagGetWindowSource returned %d\n", Ret);
    ok(IsRectEmpty(&Out) && Out.left == 0 && Out.top == 0, "default source is %ld %ld %ld %ld\n",
       Out.left, Out.top, Out.right, Out.bottom);

    ZeroMemory(&OutTransform, sizeof(OutTransform));
    Ret = pMagGetWindowTransform(Mag, &OutTransform);
    ok(Ret == TRUE, "MagGetWindowTransform returned %d\n", Ret);
    ok(IsIdentityTransform(&OutTransform), "the default transform is not the identity\n");

    Ret = pMagSetWindowSource(Mag, Source);
    ok(Ret == TRUE, "MagSetWindowSource returned %d\n", Ret);
    SetRect(&Out, -1, -1, -1, -1);
    Ret = pMagGetWindowSource(Mag, &Out);
    ok(Ret == TRUE && EqualRect(&Out, &Source), "source is %ld %ld %ld %ld\n",
       Out.left, Out.top, Out.right, Out.bottom);

    ZeroMemory(&Transform, sizeof(Transform));
    Transform.v[0][0] = 2.0f;
    Transform.v[1][1] = 2.0f;
    Transform.v[2][2] = 1.0f;
    Ret = pMagSetWindowTransform(Mag, &Transform);
    ok(Ret == TRUE, "MagSetWindowTransform returned %d\n", Ret);
    ZeroMemory(&OutTransform, sizeof(OutTransform));
    Ret = pMagGetWindowTransform(Mag, &OutTransform);
    ok(Ret == TRUE && !memcmp(&Transform, &OutTransform, sizeof(Transform)), "the transform was not kept\n");

    Count = pMagGetWindowFilterList(Mag, &Mode, 2, Filter);
    ok(Count == 0, "default filter count %d\n", Count);
    ok(Mode == MW_FILTERMODE_EXCLUDE, "default filter mode %lu\n", Mode);
    Filter[0] = Host;
    Ret = pMagSetWindowFilterList(Mag, MW_FILTERMODE_EXCLUDE, 1, Filter);
    ok(Ret == TRUE, "MagSetWindowFilterList returned %d\n", Ret);
    Filter[0] = NULL;
    Mode = 0xdeadbeef;
    Count = pMagGetWindowFilterList(Mag, &Mode, 2, Filter);
    ok(Count == 1 && Mode == MW_FILTERMODE_EXCLUDE && Filter[0] == Host,
       "filter count %d mode %lu window %p\n", Count, Mode, Filter[0]);

    ZeroMemory(&OutEffect, sizeof(OutEffect));
    Ret = pMagGetColorEffect(Mag, &OutEffect);
    ok(Ret == TRUE && IsIdentityEffect(&OutEffect), "the default color effect is not the identity\n");
    ZeroMemory(&Effect, sizeof(Effect));
    Effect.transform[0][0] = Effect.transform[0][1] = Effect.transform[0][2] = 0.3f;
    Effect.transform[1][0] = Effect.transform[1][1] = Effect.transform[1][2] = 0.6f;
    Effect.transform[2][0] = Effect.transform[2][1] = Effect.transform[2][2] = 0.1f;
    Effect.transform[3][3] = 1.0f;
    Effect.transform[4][4] = 1.0f;
    Ret = pMagSetColorEffect(Mag, &Effect);
    ok(Ret == TRUE, "MagSetColorEffect returned %d\n", Ret);
    ZeroMemory(&OutEffect, sizeof(OutEffect));
    Ret = pMagGetColorEffect(Mag, &OutEffect);
    ok(Ret == TRUE && !memcmp(&Effect, &OutEffect, sizeof(Effect)), "the color effect was not kept\n");

    UpdateWindow(Mag);

    SetLastError(0xdeadbeef);
    Ret = pMagSetWindowSource(Host, Source);
    ok(Ret == FALSE, "MagSetWindowSource on a plain window returned %d\n", Ret);
    ok(GetLastError() == 0xdeadbeef, "last error %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Ret = pMagGetWindowSource(Host, &Out);
    ok(Ret == FALSE, "MagGetWindowSource on a plain window returned %d\n", Ret);
    ok(GetLastError() == 0xdeadbeef, "last error %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Ret = pMagSetWindowTransform(Host, &Transform);
    ok(Ret == FALSE, "MagSetWindowTransform on a plain window returned %d\n", Ret);
    ok(GetLastError() == 0xdeadbeef, "last error %lu\n", GetLastError());
    Count = pMagGetWindowFilterList(Host, &Mode, 2, Filter);
    ok(Count == 0, "MagGetWindowFilterList on a plain window returned %d\n", Count);

    Ret = pMagGetFullscreenTransform(&Level, &X, &Y);
    ok(Ret == TRUE && Level == 1.0f && X == 0 && Y == 0, "full screen transform %d %d %d %d\n",
       Ret, (int)(Level * 1000), X, Y);
    Ret = pMagSetFullscreenTransform(1.0f, 0, 0);
    ok(Ret == TRUE, "MagSetFullscreenTransform(1.0, 0, 0) returned %d\n", Ret);
}

START_TEST(MagnifierWindow)
{
    HMODULE hMag = LoadLibraryW(L"magnification.dll");
    WNDCLASSW Class = { 0 };
    HWND Host, Mag;
    BOOL Ret;

    ok(hMag != NULL, "magnification.dll is missing: %lu\n", GetLastError());
    if (!hMag)
        return;

    pMagInitialize = (FN_MagInitialize)GetProcAddress(hMag, "MagInitialize");
    pMagUninitialize = (FN_MagUninitialize)GetProcAddress(hMag, "MagUninitialize");
    pMagSetWindowSource = (FN_MagSetWindowSource)GetProcAddress(hMag, "MagSetWindowSource");
    pMagGetWindowSource = (FN_MagGetWindowSource)GetProcAddress(hMag, "MagGetWindowSource");
    pMagSetWindowTransform = (FN_MagSetWindowTransform)GetProcAddress(hMag, "MagSetWindowTransform");
    pMagGetWindowTransform = (FN_MagGetWindowTransform)GetProcAddress(hMag, "MagGetWindowTransform");
    pMagSetWindowFilterList = (FN_MagSetWindowFilterList)GetProcAddress(hMag, "MagSetWindowFilterList");
    pMagGetWindowFilterList = (FN_MagGetWindowFilterList)GetProcAddress(hMag, "MagGetWindowFilterList");
    pMagSetColorEffect = (FN_MagSetColorEffect)GetProcAddress(hMag, "MagSetColorEffect");
    pMagGetColorEffect = (FN_MagGetColorEffect)GetProcAddress(hMag, "MagGetColorEffect");
    pMagGetFullscreenTransform = (FN_MagGetFullscreenTransform)GetProcAddress(hMag, "MagGetFullscreenTransform");
    pMagSetFullscreenTransform = (FN_MagSetFullscreenTransform)GetProcAddress(hMag, "MagSetFullscreenTransform");

    Ret = pMagSetWindowSource(GetDesktopWindow(), (RECT){ 0, 0, 1, 1 });
    ok(Ret == FALSE, "MagSetWindowSource before MagInitialize returned %d\n", Ret);

    Ret = pMagInitialize();
    ok(Ret == TRUE, "MagInitialize returned %d\n", Ret);
    Ret = pMagInitialize();
    ok(Ret == TRUE, "a second MagInitialize returned %d\n", Ret);

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"MagnifierTestHost";
    RegisterClassW(&Class);
    Host = CreateWindowExW(0, L"MagnifierTestHost", L"Host", WS_OVERLAPPEDWINDOW,
                           0, 0, 300, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Host != NULL, "host window creation failed: %lu\n", GetLastError());
    Mag = CreateWindowExW(0, WC_MAGNIFIERW, L"MagnifierWindow", WS_CHILD | WS_VISIBLE | MS_SHOWMAGNIFIEDCURSOR,
                          0, 0, 200, 100, Host, NULL, GetModuleHandleW(NULL), NULL);
    ok(Mag != NULL, "magnifier window creation failed: %lu\n", GetLastError());

    if (Host && Mag)
        TestMagnifier(Host, Mag);

    if (Mag)
        DestroyWindow(Mag);
    if (Host)
        DestroyWindow(Host);

    Ret = pMagUninitialize();
    ok(Ret == TRUE, "MagUninitialize returned %d\n", Ret);
    Ret = pMagUninitialize();
    ok(Ret == TRUE, "a second MagUninitialize returned %d\n", Ret);
}
