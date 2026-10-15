/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the shell window queries and GetWindowCompositionAttribute
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_HWND_BOOL)(HWND);
typedef HWND (WINAPI *PFN_HWND_HWND)(HWND);
typedef BOOL (WINAPI *PFN_UICONTEXT)(HANDLE, PVOID);

typedef struct _COMPOSITION_REQUEST
{
    DWORD Attribute;
    PVOID Data;
    SIZE_T DataSize;
} COMPOSITION_REQUEST;

typedef BOOL (WINAPI *PFN_GETWCA)(HWND, COMPOSITION_REQUEST *);

static PFN_GETWCA pGetWindowCompositionAttribute;

static LRESULT CALLBACK
TestWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static BOOL
QueryComposition(HWND hWnd, DWORD Attribute, PVOID Data, SIZE_T DataSize, PDWORD Error)
{
    COMPOSITION_REQUEST Request;
    BOOL Result;

    Request.Attribute = Attribute;
    Request.Data = Data;
    Request.DataSize = DataSize;
    SetLastError(0xdeadbeef);
    Result = pGetWindowCompositionAttribute(hWnd, &Request);
    *Error = GetLastError();
    return Result;
}

static VOID
TestComposition(HWND Shown, HWND Hidden, HWND Child)
{
    static const DWORD ZeroAttributes[] = { 18, 20, 23, 24, 25, 26, 27, 29, 30, 34 };
    static const DWORD InvalidAttributes[] = { 0, 2, 3, 4, 6, 7, 9, 10, 11, 13, 14, 15, 16, 17, 32, 33, 35, 36, 38, 39, 40 };
    static const DWORD SetOnly16[] = { 19, 31 };
    static const DWORD SetOnly8[] = { 22, 28, 37 };
    BYTE Data[64];
    DWORD Error, Index;
    BOOL Result;
    RECT Rect;

    memset(Data, 0xcc, sizeof(Data));
    Result = QueryComposition(Shown, 1, Data, 4, &Error);
    ok(Result && Error == 0xdeadbeef && *(DWORD *)Data == 1, "attribute 1: %d %lu %lu\n", Result, Error, *(DWORD *)Data);

    memset(Data, 0xcc, sizeof(Data));
    Result = QueryComposition(Shown, 21, Data, 4, &Error);
    ok(Result && Error == 0xdeadbeef && *(DWORD *)Data == 1, "attribute 21: %d %lu %lu\n", Result, Error, *(DWORD *)Data);

    for (Index = 0; Index < _countof(ZeroAttributes); Index++)
    {
        memset(Data, 0xcc, sizeof(Data));
        Result = QueryComposition(Shown, ZeroAttributes[Index], Data, 4, &Error);
        ok(Result && *(DWORD *)Data == 0, "attribute %lu: %d %lu %lu\n", ZeroAttributes[Index], Result, Error, *(DWORD *)Data);
        ok(*(DWORD *)(Data + 4) == 0xcccccccc, "attribute %lu wrote past 4 bytes\n", ZeroAttributes[Index]);

        Result = QueryComposition(Shown, ZeroAttributes[Index], Data, 2, &Error);
        ok(!Result && Error == ERROR_INSUFFICIENT_BUFFER, "attribute %lu size 2: %d %lu\n", ZeroAttributes[Index], Result, Error);

        memset(Data, 0xcc, sizeof(Data));
        Result = QueryComposition(Hidden, ZeroAttributes[Index], Data, 4, &Error);
        ok(Result && *(DWORD *)Data == 0, "hidden attribute %lu: %d %lu %lu\n", ZeroAttributes[Index], Result, Error, *(DWORD *)Data);
    }

    for (Index = 0; Index < _countof(InvalidAttributes); Index++)
    {
        Result = QueryComposition(Shown, InvalidAttributes[Index], Data, 4, &Error);
        ok(!Result && Error == ERROR_INVALID_PARAMETER, "attribute %lu: %d %lu\n", InvalidAttributes[Index], Result, Error);
    }

    for (Index = 0; Index < _countof(SetOnly16); Index++)
    {
        Result = QueryComposition(Shown, SetOnly16[Index], Data, 12, &Error);
        ok(!Result && Error == ERROR_INSUFFICIENT_BUFFER, "attribute %lu size 12: %d %lu\n", SetOnly16[Index], Result, Error);
        Result = QueryComposition(Shown, SetOnly16[Index], Data, 16, &Error);
        ok(!Result && Error == ERROR_INVALID_PARAMETER, "attribute %lu size 16: %d %lu\n", SetOnly16[Index], Result, Error);
    }

    for (Index = 0; Index < _countof(SetOnly8); Index++)
    {
        Result = QueryComposition(Shown, SetOnly8[Index], Data, 5, &Error);
        ok(!Result && Error == ERROR_INSUFFICIENT_BUFFER, "attribute %lu size 5: %d %lu\n", SetOnly8[Index], Result, Error);
        Result = QueryComposition(Shown, SetOnly8[Index], Data, 8, &Error);
        ok(!Result && Error == ERROR_INVALID_PARAMETER, "attribute %lu size 8: %d %lu\n", SetOnly8[Index], Result, Error);
    }

    Result = QueryComposition(Shown, 5, &Rect, 12, &Error);
    ok(!Result && Error == ERROR_INSUFFICIENT_BUFFER, "attribute 5 size 12: %d %lu\n", Result, Error);
    Result = QueryComposition(Shown, 5, &Rect, sizeof(Rect), &Error);
    ok(Result && Rect.right > Rect.left && Rect.bottom > Rect.top, "attribute 5: %d %lu\n", Result, Error);

    Result = QueryComposition(Shown, 8, &Rect, 12, &Error);
    ok(!Result && Error == ERROR_INSUFFICIENT_BUFFER, "attribute 8 size 12: %d %lu\n", Result, Error);
    Result = QueryComposition(Shown, 8, &Rect, sizeof(Rect), &Error);
    ok(Result && Rect.right > Rect.left && Rect.bottom > Rect.top, "attribute 8: %d %lu\n", Result, Error);

    Result = QueryComposition(Shown, 12, Data, 16, &Error);
    ok(!Result && Error == ERROR_INSUFFICIENT_BUFFER, "attribute 12 size 16: %d %lu\n", Result, Error);
    memset(Data, 0xcc, sizeof(Data));
    Result = QueryComposition(Shown, 12, Data, 20, &Error);
    ok(Result, "attribute 12: %d %lu\n", Result, Error);
    ok(*(DWORD *)Data == 0xcccccccc, "attribute 12 first field %08lx\n", *(DWORD *)Data);
    for (Index = 1; Index < 5; Index++)
        ok(((DWORD *)Data)[Index] == 0, "attribute 12 field %lu: %08lx\n", Index, ((DWORD *)Data)[Index]);

    Result = QueryComposition(Shown, 21, Data, 1, &Error);
    ok(!Result && Error == ERROR_INSUFFICIENT_BUFFER, "attribute 21 size 1: %d %lu\n", Result, Error);
    Result = QueryComposition(Shown, 21, Data, 64, &Error);
    ok(Result, "attribute 21 size 64: %d %lu\n", Result, Error);
    Result = QueryComposition(Shown, 21, NULL, 4, &Error);
    ok(!Result && Error == ERROR_INVALID_PARAMETER, "attribute 21 no buffer: %d %lu\n", Result, Error);
    Result = QueryComposition(NULL, 21, Data, 4, &Error);
    ok(!Result && Error == ERROR_INVALID_HANDLE, "NULL window: %d %lu\n", Result, Error);
    Result = QueryComposition((HWND)(ULONG_PTR)0x12345678, 21, Data, 4, &Error);
    ok(!Result && Error == ERROR_INVALID_HANDLE, "invalid window: %d %lu\n", Result, Error);

    memset(Data, 0xcc, sizeof(Data));
    Result = QueryComposition(Child, 21, Data, 4, &Error);
    ok(Result && *(DWORD *)Data == 1, "child attribute 21: %d %lu %lu\n", Result, Error, *(DWORD *)Data);
    memset(Data, 0xcc, sizeof(Data));
    Result = QueryComposition(Hidden, 21, Data, 4, &Error);
    ok(Result && *(DWORD *)Data == 1, "hidden attribute 21: %d %lu %lu\n", Result, Error, *(DWORD *)Data);
}

START_TEST(ShellWindowQueries)
{
    HMODULE User32 = GetModuleHandleW(L"user32.dll");
    PFN_HWND_BOOL pIsShellManagedWindow, pIsShellFrameWindow;
    PFN_HWND_HWND pGetModernAppWindow;
    PFN_UICONTEXT pGetProcessUIContext;
    HWND Shown, Hidden, Child, Windows[3];
    WNDCLASSW WndClass = { 0 };
    DWORD Info[3], Index;
    HWND Result;
    BOOL Ret;

    pGetProcessUIContext = (PFN_UICONTEXT)GetProcAddress(User32, MAKEINTRESOURCEA(2521));
    pGetModernAppWindow = (PFN_HWND_HWND)GetProcAddress(User32, MAKEINTRESOURCEA(2569));
    pIsShellFrameWindow = (PFN_HWND_BOOL)GetProcAddress(User32, MAKEINTRESOURCEA(2573));
    pIsShellManagedWindow = (PFN_HWND_BOOL)GetProcAddress(User32, MAKEINTRESOURCEA(2574));
    pGetWindowCompositionAttribute = (PFN_GETWCA)GetProcAddress(User32, "GetWindowCompositionAttribute");
    ok(pGetProcessUIContext != NULL, "ordinal 2521 is missing\n");
    ok(pGetModernAppWindow != NULL, "ordinal 2569 is missing\n");
    ok(pIsShellFrameWindow != NULL, "ordinal 2573 is missing\n");
    ok(pIsShellManagedWindow != NULL, "ordinal 2574 is missing\n");
    ok(pGetWindowCompositionAttribute != NULL, "GetWindowCompositionAttribute is missing\n");

    WndClass.lpfnWndProc = TestWndProc;
    WndClass.hInstance = GetModuleHandleW(NULL);
    WndClass.lpszClassName = L"ShellWindowQueries";
    RegisterClassW(&WndClass);
    Shown = CreateWindowExW(0, L"ShellWindowQueries", L"shown", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, NULL, NULL, WndClass.hInstance, NULL);
    Hidden = CreateWindowExW(0, L"ShellWindowQueries", L"hidden", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, NULL, NULL, WndClass.hInstance, NULL);
    Child = CreateWindowExW(0, L"BUTTON", L"child", WS_CHILD, 0, 0, 20, 20, Shown, NULL, WndClass.hInstance, NULL);
    ok(Shown && Hidden && Child, "window creation failed\n");
    ShowWindow(Shown, SW_SHOWNOACTIVATE);
    UpdateWindow(Shown);

    Windows[0] = Shown;
    Windows[1] = Child;
    Windows[2] = GetDesktopWindow();
    for (Index = 0; Index < _countof(Windows); Index++)
    {
        if (pIsShellManagedWindow)
        {
            SetLastError(0xdeadbeef);
            Ret = pIsShellManagedWindow(Windows[Index]);
            ok(!Ret && GetLastError() == 0xdeadbeef, "IsShellManagedWindow %lu: %d %lu\n", Index, Ret, GetLastError());
        }
        if (pIsShellFrameWindow)
        {
            SetLastError(0xdeadbeef);
            Ret = pIsShellFrameWindow(Windows[Index]);
            ok(!Ret && GetLastError() == 0xdeadbeef, "IsShellFrameWindow %lu: %d %lu\n", Index, Ret, GetLastError());
        }
        if (pGetModernAppWindow)
        {
            SetLastError(0xdeadbeef);
            Result = pGetModernAppWindow(Windows[Index]);
            ok(Result == NULL && GetLastError() == ERROR_INVALID_PARAMETER, "GetModernAppWindow %lu: %p %lu\n", Index, Result, GetLastError());
        }
    }

    if (pIsShellManagedWindow)
    {
        SetLastError(0xdeadbeef);
        Ret = pIsShellManagedWindow((HWND)(ULONG_PTR)0x12345678);
        ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "IsShellManagedWindow invalid: %d %lu\n", Ret, GetLastError());
    }
    if (pIsShellFrameWindow)
    {
        SetLastError(0xdeadbeef);
        Ret = pIsShellFrameWindow((HWND)(ULONG_PTR)0x12345678);
        ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "IsShellFrameWindow invalid: %d %lu\n", Ret, GetLastError());
    }
    if (pGetModernAppWindow)
    {
        SetLastError(0xdeadbeef);
        Result = pGetModernAppWindow((HWND)(ULONG_PTR)0x12345678);
        ok(Result == NULL && GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "GetModernAppWindow invalid: %p %lu\n", Result, GetLastError());
    }

    if (pGetProcessUIContext)
    {
        memset(Info, 0xcc, sizeof(Info));
        SetLastError(0xdeadbeef);
        Ret = pGetProcessUIContext(GetCurrentProcess(), Info);
        ok(Ret && GetLastError() == 0xdeadbeef, "UI context: %d %lu\n", Ret, GetLastError());
        ok(Info[0] == 0 && Info[1] == 0 && Info[2] == 0xcccccccc, "UI context: %08lx %08lx %08lx\n", Info[0], Info[1], Info[2]);

        memset(Info, 0xcc, sizeof(Info));
        SetLastError(0xdeadbeef);
        Ret = pGetProcessUIContext(NULL, Info);
        ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "UI context NULL process: %d %lu\n", Ret, GetLastError());
        ok(Info[0] == 0xcccccccc, "UI context NULL process wrote %08lx\n", Info[0]);

        SetLastError(0xdeadbeef);
        Ret = pGetProcessUIContext(GetCurrentProcess(), NULL);
        ok(!Ret && GetLastError() == ERROR_NOACCESS, "UI context NULL buffer: %d %lu\n", Ret, GetLastError());
    }

    if (pGetWindowCompositionAttribute)
        TestComposition(Shown, Hidden, Child);

    DestroyWindow(Child);
    DestroyWindow(Shown);
    DestroyWindow(Hidden);
    UnregisterClassW(L"ShellWindowQueries", WndClass.hInstance);
}
