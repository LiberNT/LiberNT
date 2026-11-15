/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a window becoming foreground takes the input language the foreground thread chose last
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <strsafe.h>

static const WCHAR s_szClass[] = L"SharedInputLanguageTest";
static const WCHAR s_szCount[] = L"SharedInputLanguageChanges";
static const WCHAR s_szLast[] = L"SharedInputLanguageLast";
static const WCHAR s_szRequests[] = L"SharedInputLanguageRequests";

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INPUTLANGCHANGE)
    {
        SetPropW(hwnd, s_szCount, (HANDLE)((ULONG_PTR)GetPropW(hwnd, s_szCount) + 1));
        SetPropW(hwnd, s_szLast, (HANDLE)lParam);
    }
    if (uMsg == WM_INPUTLANGCHANGEREQUEST)
        SetPropW(hwnd, s_szRequests, (HANDLE)((ULONG_PTR)GetPropW(hwnd, s_szRequests) + 1));
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static HWND CreateTestWindow(LPCWSTR pszTitle, int x)
{
    WNDCLASSW wc = { 0 };

    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = s_szClass;
    RegisterClassW(&wc);
    return CreateWindowExW(0, s_szClass, pszTitle, WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, 100, 200, 150, NULL, NULL,
                           NULL, NULL);
}

static void PumpFor(DWORD dwMilliseconds)
{
    DWORD dwEnd = GetTickCount() + dwMilliseconds;
    MSG msg;

    while ((LONG)(dwEnd - GetTickCount()) > 0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

static void Child(void)
{
    MSG msg;

    AllowSetForegroundWindow(ASFW_ANY);
    if (!CreateTestWindow(L"B", 400))
        ExitProcess(1);
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ExitProcess(0);
}

static BOOL MakeForeground(HWND hwnd)
{
    UINT i;

    for (i = 0; i < 20 && GetForegroundWindow() != hwnd; i++)
    {
        SetForegroundWindow(hwnd);
        PumpFor(100);
    }
    PumpFor(300);
    return GetForegroundWindow() == hwnd;
}

static BOOL IsLoaded(HKL hkl)
{
    HKL ahkl[64];
    int c = GetKeyboardLayoutList(_countof(ahkl), ahkl), i;

    for (i = 0; i < c; i++)
    {
        if (ahkl[i] == hkl)
            return TRUE;
    }
    return FALSE;
}

static void CheckFollows(HWND hwndA, HWND hwndB, HKL hkl)
{
    DWORD tidB = GetWindowThreadProcessId(hwndB, NULL);
    ULONG_PTR cBefore;

    ok(MakeForeground(hwndA), "the first window is not in the foreground\n");
    ActivateKeyboardLayout(hkl, 0);
    PumpFor(200);
    ok(GetKeyboardLayout(0) == hkl, "the first window has layout %p, expected %p\n", GetKeyboardLayout(0), hkl);
    cBefore = (ULONG_PTR)GetPropW(hwndB, s_szCount);
    ok(MakeForeground(hwndB), "the child window is not in the foreground\n");
    ok(GetKeyboardLayout(tidB) == hkl, "the child window has layout %p, expected %p\n", GetKeyboardLayout(tidB), hkl);
    ok((ULONG_PTR)GetPropW(hwndB, s_szCount) > cBefore, "the child window got no WM_INPUTLANGCHANGE\n");
    ok(GetPropW(hwndB, s_szLast) == hkl, "the child window was told %p, expected %p\n", GetPropW(hwndB, s_szLast), hkl);
}

static void SendKey(WORD wVk, DWORD dwFlags)
{
    INPUT input = { 0 };

    input.type = INPUT_KEYBOARD;
    input.ki.wVk = wVk;
    input.ki.dwFlags = dwFlags;
    SendInput(1, &input, sizeof(input));
}

static void CheckHotkey(HWND hwndA, HWND hwndB)
{
    DWORD tidB = GetWindowThreadProcessId(hwndB, NULL);
    HKL hklBefore, hklAfter;

    ok(MakeForeground(hwndA), "the first window is not in the foreground\n");
    SetPropW(hwndA, s_szRequests, NULL);
    hklBefore = GetKeyboardLayout(0);
    SendKey(VK_LMENU, 0);
    SendKey(VK_LSHIFT, 0);
    SendKey(VK_LSHIFT, KEYEVENTF_KEYUP);
    SendKey(VK_LMENU, KEYEVENTF_KEYUP);
    PumpFor(500);
    hklAfter = GetKeyboardLayout(0);
    if (hklAfter == hklBefore)
    {
        skip("Left Alt+Shift does not switch the input language here\n");
        return;
    }
    ok(GetPropW(hwndA, s_szRequests) == NULL, "the window got %Iu WM_INPUTLANGCHANGEREQUEST\n",
       (ULONG_PTR)GetPropW(hwndA, s_szRequests));
    ok(MakeForeground(hwndB), "the child window is not in the foreground\n");
    ok(GetKeyboardLayout(tidB) == hklAfter, "the child window has layout %p, expected %p\n", GetKeyboardLayout(tidB),
       hklAfter);
}

START_TEST(SharedInputLanguage)
{
    HKL hklUs = (HKL)(ULONG_PTR)0x04090409, hklJa = (HKL)(ULONG_PTR)0x04110411, hklSaved;
    BOOL fUs = IsLoaded(hklUs), fJa = IsLoaded(hklJa);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR szExe[MAX_PATH], szCmd[MAX_PATH + 32];
    HWND hwndA, hwndB = NULL;
    char **argv;
    UINT i;

    if (winetest_get_mainargs(&argv) >= 3 && !strcmp(argv[2], "child"))
    {
        Child();
        return;
    }

    LoadKeyboardLayoutW(L"00000409", KLF_NOTELLSHELL);
    LoadKeyboardLayoutW(L"00000411", KLF_NOTELLSHELL);
    if (!IsLoaded(hklUs) || !IsLoaded(hklJa))
    {
        skip("The US and Japanese layouts could not be loaded\n");
        return;
    }
    hklSaved = GetKeyboardLayout(0);

    hwndA = CreateTestWindow(L"A", 100);
    ok(hwndA != NULL, "CreateWindowExW failed with error %lu\n", GetLastError());
    GetModuleFileNameW(NULL, szExe, _countof(szExe));
    StringCchPrintfW(szCmd, _countof(szCmd), L"\"%s\" SharedInputLanguage child", szExe);
    if (hwndA && CreateProcessW(NULL, szCmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        for (i = 0; i < 50 && !hwndB; i++)
        {
            PumpFor(100);
            hwndB = FindWindowW(s_szClass, L"B");
        }
        ok(hwndB != NULL, "the child window did not appear\n");
        if (hwndB && MakeForeground(hwndA))
        {
            HKL hklFirst = GetKeyboardLayout(GetWindowThreadProcessId(hwndB, NULL)) == hklJa ? hklUs : hklJa;

            CheckFollows(hwndA, hwndB, hklFirst);
            CheckFollows(hwndA, hwndB, hklFirst == hklJa ? hklUs : hklJa);
            CheckHotkey(hwndA, hwndB);
        }
        else if (hwndB)
        {
            skip("The test window could not be brought to the foreground\n");
        }
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else
    {
        skip("The child process could not be started\n");
    }

    if (hwndA)
        DestroyWindow(hwndA);
    ActivateKeyboardLayout(hklSaved, 0);
    if (!fJa)
        UnloadKeyboardLayout(hklJa);
    if (!fUs)
        UnloadKeyboardLayout(hklUs);
}
