/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for NtdllDefWindowProc_A/W and NtdllDialogWndProc_A/W
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winuser.h>

typedef LRESULT (WINAPI *FN_WNDPROC)(HWND, UINT, WPARAM, LPARAM);

static FN_WNDPROC pNtdllDefWindowProc_A;
static FN_WNDPROC pNtdllDefWindowProc_W;
static FN_WNDPROC pNtdllDialogWndProc_A;
static FN_WNDPROC pNtdllDialogWndProc_W;

static FN_WNDPROC pDefWindowProcW;
static FN_WNDPROC pDefDlgProcA;
static FN_WNDPROC pDefDlgProcW;
static ATOM (WINAPI *pRegisterClassW)(const WNDCLASSW *);
static BOOL (WINAPI *pUnregisterClassW)(LPCWSTR, HINSTANCE);
static HWND (WINAPI *pCreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static BOOL (WINAPI *pDestroyWindow)(HWND);
static int (WINAPI *pGetWindowTextW)(HWND, LPWSTR, int);
static HWND (WINAPI *pCreateDialogIndirectParamW)(HINSTANCE, LPCDLGTEMPLATEW, HWND, DLGPROC, LPARAM);

static const WCHAR TestClassName[] = L"NtdllDefWindowProcTest";

static
INT_PTR
CALLBACK
TestDialogProc(
    _In_ HWND hDlg,
    _In_ UINT Msg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    return (Msg == WM_INITDIALOG);
}

static
VOID
TestDefWindowProc(
    _In_ HINSTANCE Instance)
{
    WNDCLASSW Class = { 0 };
    HWND Window;
    LRESULT Result;
    WCHAR TextW[32];
    CHAR TextA[32];
    int Length;

    Class.lpfnWndProc = pDefWindowProcW;
    Class.hInstance = Instance;
    Class.lpszClassName = TestClassName;
    ok(pRegisterClassW(&Class) != 0, "RegisterClassW failed with %lu\n", GetLastError());

    Window = pCreateWindowExW(0, TestClassName, L"initial", WS_POPUP, 0, 0, 10, 10, NULL, NULL, Instance, NULL);
    ok(Window != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (Window == NULL)
    {
        pUnregisterClassW(TestClassName, Instance);
        return;
    }

    Result = pNtdllDefWindowProc_W(Window, WM_SETTEXT, 0, (LPARAM)L"ntdll wide");
    ok(Result == TRUE, "NtdllDefWindowProc_W(WM_SETTEXT) returned %I64d\n", (LONGLONG)Result);
    RtlZeroMemory(TextW, sizeof(TextW));
    Length = pGetWindowTextW(Window, TextW, ARRAYSIZE(TextW));
    ok(Length == 10, "GetWindowTextW returned %d\n", Length);
    ok(wcscmp(TextW, L"ntdll wide") == 0, "Window text is '%ls'\n", TextW);

    Result = pNtdllDefWindowProc_W(Window, WM_GETTEXTLENGTH, 0, 0);
    ok(Result == 10, "NtdllDefWindowProc_W(WM_GETTEXTLENGTH) returned %I64d\n", (LONGLONG)Result);

    Result = pNtdllDefWindowProc_A(Window, WM_SETTEXT, 0, (LPARAM)"ntdll ansi");
    ok(Result == TRUE, "NtdllDefWindowProc_A(WM_SETTEXT) returned %I64d\n", (LONGLONG)Result);
    RtlZeroMemory(TextW, sizeof(TextW));
    Length = pGetWindowTextW(Window, TextW, ARRAYSIZE(TextW));
    ok(Length == 10, "GetWindowTextW returned %d\n", Length);
    ok(wcscmp(TextW, L"ntdll ansi") == 0, "Window text is '%ls'\n", TextW);

    RtlFillMemory(TextA, sizeof(TextA), 'x');
    Result = pNtdllDefWindowProc_A(Window, WM_GETTEXT, ARRAYSIZE(TextA), (LPARAM)TextA);
    ok(Result == 10, "NtdllDefWindowProc_A(WM_GETTEXT) returned %I64d\n", (LONGLONG)Result);
    ok(strcmp(TextA, "ntdll ansi") == 0, "WM_GETTEXT returned '%.31s'\n", TextA);

    RtlFillMemory(TextW, sizeof(TextW), 0xff);
    Result = pNtdllDefWindowProc_W(Window, WM_GETTEXT, ARRAYSIZE(TextW), (LPARAM)TextW);
    ok(Result == 10, "NtdllDefWindowProc_W(WM_GETTEXT) returned %I64d\n", (LONGLONG)Result);
    ok(wcscmp(TextW, L"ntdll ansi") == 0, "WM_GETTEXT returned '%.31ls'\n", TextW);

    Result = pNtdllDefWindowProc_A(Window, WM_GETTEXTLENGTH, 0, 0);
    ok(Result == pDefWindowProcW(Window, WM_GETTEXTLENGTH, 0, 0),
       "NtdllDefWindowProc_A(WM_GETTEXTLENGTH) returned %I64d\n", (LONGLONG)Result);

    ok(pDestroyWindow(Window), "DestroyWindow failed with %lu\n", GetLastError());
    ok(pUnregisterClassW(TestClassName, Instance), "UnregisterClassW failed with %lu\n", GetLastError());
}

static
VOID
TestDialogWndProc(
    _In_ HINSTANCE Instance)
{
    struct
    {
        DLGTEMPLATE Template;
        WORD Menu;
        WORD Class;
        WORD Title;
    } DECLSPEC_ALIGN(4) Dialog = { { 0 } };
    HWND hDlg;
    LRESULT Result;

    Dialog.Template.style = WS_POPUP;
    Dialog.Template.cx = 100;
    Dialog.Template.cy = 50;

    hDlg = pCreateDialogIndirectParamW(Instance, &Dialog.Template, NULL, TestDialogProc, 0);
    ok(hDlg != NULL, "CreateDialogIndirectParamW failed with %lu\n", GetLastError());
    if (hDlg == NULL)
        return;

    pNtdllDialogWndProc_W(hDlg, DM_SETDEFID, 7, 0);
    Result = pDefDlgProcW(hDlg, DM_GETDEFID, 0, 0);
    ok(LOWORD(Result) == 7, "DM_GETDEFID after NtdllDialogWndProc_W(DM_SETDEFID) returned 0x%I64x\n", (ULONGLONG)Result);
    ok(HIWORD(Result) == DC_HASDEFID, "DM_GETDEFID returned 0x%I64x\n", (ULONGLONG)Result);

    Result = pNtdllDialogWndProc_W(hDlg, DM_GETDEFID, 0, 0);
    ok(Result == MAKELONG(7, DC_HASDEFID), "NtdllDialogWndProc_W(DM_GETDEFID) returned 0x%I64x\n", (ULONGLONG)Result);

    pNtdllDialogWndProc_A(hDlg, DM_SETDEFID, 9, 0);
    Result = pDefDlgProcA(hDlg, DM_GETDEFID, 0, 0);
    ok(LOWORD(Result) == 9, "DM_GETDEFID after NtdllDialogWndProc_A(DM_SETDEFID) returned 0x%I64x\n", (ULONGLONG)Result);

    Result = pNtdllDialogWndProc_A(hDlg, DM_GETDEFID, 0, 0);
    ok(Result == MAKELONG(9, DC_HASDEFID), "NtdllDialogWndProc_A(DM_GETDEFID) returned 0x%I64x\n", (ULONGLONG)Result);

    Result = pNtdllDialogWndProc_W(hDlg, WM_SETTEXT, 0, (LPARAM)L"dialog");
    ok(Result == TRUE, "NtdllDialogWndProc_W(WM_SETTEXT) returned %I64d\n", (LONGLONG)Result);
    Result = pNtdllDialogWndProc_A(hDlg, WM_GETTEXTLENGTH, 0, 0);
    ok(Result == 6, "NtdllDialogWndProc_A(WM_GETTEXTLENGTH) returned %I64d\n", (LONGLONG)Result);

    ok(pDestroyWindow(hDlg), "DestroyWindow failed with %lu\n", GetLastError());
}

START_TEST(NtdllDefWindowProc)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    HMODULE User32;
    HINSTANCE Instance = GetModuleHandleW(NULL);

    pNtdllDefWindowProc_A = (FN_WNDPROC)GetProcAddress(Ntdll, "NtdllDefWindowProc_A");
    pNtdllDefWindowProc_W = (FN_WNDPROC)GetProcAddress(Ntdll, "NtdllDefWindowProc_W");
    pNtdllDialogWndProc_A = (FN_WNDPROC)GetProcAddress(Ntdll, "NtdllDialogWndProc_A");
    pNtdllDialogWndProc_W = (FN_WNDPROC)GetProcAddress(Ntdll, "NtdllDialogWndProc_W");
    if (!pNtdllDefWindowProc_A || !pNtdllDefWindowProc_W || !pNtdllDialogWndProc_A || !pNtdllDialogWndProc_W)
    {
        skip("NtdllDefWindowProc_A/W or NtdllDialogWndProc_A/W is missing\n");
        return;
    }

    User32 = LoadLibraryW(L"user32.dll");
    ok(User32 != NULL, "LoadLibraryW(user32) failed with %lu\n", GetLastError());
    if (User32 == NULL)
        return;

    pDefWindowProcW = (FN_WNDPROC)GetProcAddress(User32, "DefWindowProcW");
    pDefDlgProcA = (FN_WNDPROC)GetProcAddress(User32, "DefDlgProcA");
    pDefDlgProcW = (FN_WNDPROC)GetProcAddress(User32, "DefDlgProcW");
    pRegisterClassW = (PVOID)GetProcAddress(User32, "RegisterClassW");
    pUnregisterClassW = (PVOID)GetProcAddress(User32, "UnregisterClassW");
    pCreateWindowExW = (PVOID)GetProcAddress(User32, "CreateWindowExW");
    pDestroyWindow = (PVOID)GetProcAddress(User32, "DestroyWindow");
    pGetWindowTextW = (PVOID)GetProcAddress(User32, "GetWindowTextW");
    pCreateDialogIndirectParamW = (PVOID)GetProcAddress(User32, "CreateDialogIndirectParamW");
    if (!pDefWindowProcW || !pDefDlgProcA || !pDefDlgProcW || !pRegisterClassW || !pUnregisterClassW ||
        !pCreateWindowExW || !pDestroyWindow || !pGetWindowTextW || !pCreateDialogIndirectParamW)
    {
        skip("user32 exports are missing\n");
        return;
    }

    TestDefWindowProc(Instance);
    TestDialogWndProc(Instance);
}
