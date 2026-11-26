/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that applying the Background page of Display Properties does not wait for a slow window
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <commctrl.h>
#include <prsht.h>

#define SLOW_HANDLER_MS 15000

static HANDLE hEntered;
static LONG SettingChanges;

static LRESULT CALLBACK SlowProc(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (Msg == WM_SETTINGCHANGE)
    {
        InterlockedIncrement(&SettingChanges);
        SetEvent(hEntered);
        Sleep(SLOW_HANDLER_MS);
        return 0;
    }
    return DefWindowProcW(hwnd, Msg, wParam, lParam);
}

static DWORD WINAPI SlowThread(LPVOID Param)
{
    WNDCLASSW wc = { 0 };
    MSG Msg;
    HWND hwnd;

    wc.lpfnWndProc = SlowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"BackgroundApplySlow";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wc.hInstance, NULL);
    *(HWND *)Param = hwnd;
    while (GetMessageW(&Msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Msg);
        DispatchMessageW(&Msg);
    }
    return 0;
}

static DWORD gPid;
static HWND gSheet;

static BOOL CALLBACK FindSheet(HWND hwnd, LPARAM lParam)
{
    WCHAR Class[16];
    DWORD Pid;

    GetWindowThreadProcessId(hwnd, &Pid);
    if (Pid == gPid && IsWindowVisible(hwnd) && GetClassNameW(hwnd, Class, ARRAYSIZE(Class)) &&
        !wcscmp(Class, L"#32770"))
    {
        gSheet = hwnd;
        return FALSE;
    }
    return TRUE;
}

START_TEST(BackgroundApply)
{
    WCHAR Command[] = L"rundll32.exe shell32.dll,Control_RunDLL desk.cpl";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HWND hSlow = NULL;
    HANDLE hThread;
    DWORD_PTR Result;
    DWORD Start, Elapsed;
    ULONG i;

    hEntered = CreateEventW(NULL, TRUE, FALSE, NULL);
    hThread = CreateThread(NULL, 0, SlowThread, &hSlow, 0, NULL);
    ok(hThread != NULL, "CreateThread failed %lu\n", GetLastError());
    for (i = 0; i < 100 && !hSlow; i++)
        Sleep(20);
    ok(hSlow != NULL, "The slow window was not created\n");

    if (!CreateProcessW(NULL, Command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        skip("Cannot start Display Properties: %lu\n", GetLastError());
        return;
    }
    gPid = pi.dwProcessId;
    for (i = 0; i < 150 && !gSheet; i++)
    {
        Sleep(100);
        EnumWindows(FindSheet, 0);
    }
    if (!gSheet)
    {
        skip("Display Properties did not open\n");
        TerminateProcess(pi.hProcess, 0);
        goto Cleanup;
    }

    ok(SendMessageTimeoutW(gSheet, PSM_SETCURSEL, 0, 0, SMTO_NORMAL, 5000, &Result) != 0,
       "Display Properties did not select the Background page\n");

    Start = GetTickCount();
    ok(SendMessageTimeoutW(gSheet, PSM_APPLY, 0, 0, SMTO_NORMAL, 3000, &Result) != 0,
       "Applying Display Properties did not finish within 3 s while a window handled WM_SETTINGCHANGE\n");
    Elapsed = GetTickCount() - Start;
    ok(WaitForSingleObject(hEntered, 5000) == WAIT_OBJECT_0, "The slow window got no WM_SETTINGCHANGE\n");
    ok(Elapsed < SLOW_HANDLER_MS - 5000, "Applying the Background page waited %lu ms for the slow window\n", Elapsed);

    PostMessageW(gSheet, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(pi.hProcess, 20000) != WAIT_OBJECT_0)
        TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

Cleanup:
    PostMessageW(hSlow, WM_CLOSE, 0, 0);
    PostThreadMessageW(GetThreadId(hThread), WM_QUIT, 0, 0);
    WaitForSingleObject(hThread, SLOW_HANDLER_MS + 5000);
    CloseHandle(hThread);
    CloseHandle(hEntered);
}
