/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the foreground right of a process started after its launcher hid the foreground window
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define READY_EVENT L"ForegroundHideLaunchReady"
#define QUIT_EVENT L"ForegroundHideLaunchQuit"

static void PumpMessages(DWORD dwMilliseconds)
{
    DWORD dwStart = GetTickCount();
    MSG msg;

    do
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
        Sleep(20);
    } while (GetTickCount() - dwStart < dwMilliseconds);
}

static int RunBehind(DWORD dwLauncher)
{
    HANDLE hReady = OpenEventW(EVENT_MODIFY_STATE, FALSE, READY_EVENT);
    HANDLE hQuit = OpenEventW(SYNCHRONIZE, FALSE, QUIT_EVENT);
    HWND hwnd;
    MSG msg;

    if (!hReady || !hQuit)
        return 2;
    hwnd = CreateWindowExW(0, L"STATIC", L"ForegroundHideLaunch behind", WS_OVERLAPPEDWINDOW, 150, 150, 300, 200, NULL,
                           NULL, GetModuleHandleW(NULL), NULL);
    if (!hwnd)
        return 2;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);
    PumpMessages(300);
    AllowSetForegroundWindow(dwLauncher);
    SetEvent(hReady);
    while (WaitForSingleObject(hQuit, 300) == WAIT_TIMEOUT)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
    }
    DestroyWindow(hwnd);
    return 0;
}

static int RunLaunched(void)
{
    HWND hwnd;
    int ret;

    hwnd = CreateWindowExW(0, L"STATIC", L"ForegroundHideLaunch launched", WS_OVERLAPPEDWINDOW, 300, 300, 200, 100,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!hwnd)
        return 2;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    PumpMessages(300);
    ret = (GetForegroundWindow() == hwnd) ? 0 : 1;
    DestroyWindow(hwnd);
    return ret;
}

static BOOL StartChild(PCSTR pszMode, PROCESS_INFORMATION *ppi)
{
    STARTUPINFOA si = { sizeof(si) };
    char szPath[MAX_PATH], szCmdLine[MAX_PATH + 64];

    GetModuleFileNameA(NULL, szPath, _countof(szPath));
    snprintf(szCmdLine, sizeof(szCmdLine), "\"%s\" ForegroundHideLaunch %s %lu", szPath, pszMode, GetCurrentProcessId());
    return CreateProcessA(NULL, szCmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, ppi);
}

static DWORD WaitChild(PROCESS_INFORMATION *ppi)
{
    DWORD dwExitCode = 0xdeadbeef;

    while (MsgWaitForMultipleObjects(1, &ppi->hProcess, FALSE, 10000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        PumpMessages(0);
    GetExitCodeProcess(ppi->hProcess, &dwExitCode);
    CloseHandle(ppi->hProcess);
    CloseHandle(ppi->hThread);
    return dwExitCode;
}

static void InjectKey(void)
{
    INPUT input[2] = { 0 };

    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_F24;
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    ok_int(SendInput(2, input, sizeof(INPUT)), 2);
}

static void TestLaunch(BOOL bInput)
{
    PROCESS_INFORMATION piBehind = { 0 }, piLaunched = { 0 };
    HANDLE hReady, hQuit;
    HWND hwndBehind, hwndDlg, hwndFore;
    char szTitle[64];

    winetest_push_context("%s", bInput ? "input" : "no input");
    hReady = CreateEventW(NULL, TRUE, FALSE, READY_EVENT);
    hQuit = CreateEventW(NULL, TRUE, FALSE, QUIT_EVENT);
    ok(StartChild("behind", &piBehind), "CreateProcessA failed with %lu\n", GetLastError());
    ok_int(WaitForSingleObject(hReady, 10000), WAIT_OBJECT_0);
    hwndBehind = FindWindowW(L"STATIC", L"ForegroundHideLaunch behind");
    ok(hwndBehind != NULL && GetForegroundWindow() == hwndBehind, "the window behind is not in the foreground\n");

    hwndDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", L"ForegroundHideLaunch", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                              100, 100, 300, 120, NULL, NULL, NULL, NULL);
    ok(hwndDlg != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    SetForegroundWindow(hwndDlg);
    ShowWindow(hwndDlg, SW_SHOWNORMAL);
    PumpMessages(300);
    ok(GetForegroundWindow() == hwndDlg, "the dialog is not in the foreground\n");

    if (bInput)
    {
        InjectKey();
        PumpMessages(200);
    }

    ShowWindow(hwndDlg, SW_HIDE);
    PumpMessages(300);
    hwndFore = GetForegroundWindow();
    szTitle[0] = 0;
    if (hwndFore)
        GetWindowTextA(hwndFore, szTitle, sizeof(szTitle));
    ok(hwndFore == hwndBehind, "foreground after hiding '%s'\n", szTitle);

    ok(StartChild("launched", &piLaunched), "CreateProcessA failed with %lu\n", GetLastError());
    if (piLaunched.hProcess)
        ok(WaitChild(&piLaunched) == 0, "the launched process did not get the foreground\n");

    SetEvent(hQuit);
    if (piBehind.hProcess)
        WaitChild(&piBehind);
    DestroyWindow(hwndDlg);
    CloseHandle(hReady);
    CloseHandle(hQuit);
    winetest_pop_context();
}

START_TEST(ForegroundHideLaunch)
{
    char **test_argv;
    int argc = winetest_get_mainargs(&test_argv);

    if (argc >= 4 && !strcmp(test_argv[2], "behind"))
        ExitProcess(RunBehind(strtoul(test_argv[3], NULL, 10)));
    if (argc >= 3 && !strcmp(test_argv[2], "launched"))
        ExitProcess(RunLaunched());

    TestLaunch(FALSE);
    TestLaunch(TRUE);
}
