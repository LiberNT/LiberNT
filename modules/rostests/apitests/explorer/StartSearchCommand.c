/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Start menu search finds a program by its command name and runs it with Enter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>

typedef struct _FOUND { HWND Window; DWORD ProcessId; } FOUND;

static BOOL CALLBACK FindWinver(HWND Window, LPARAM Param)
{
    FOUND *Found = (FOUND *)Param;
    WCHAR Image[MAX_PATH];
    DWORD ProcessId = 0, Size = _countof(Image);
    HANDLE Process;
    if (!IsWindowVisible(Window))
        return TRUE;
    GetWindowThreadProcessId(Window, &ProcessId);
    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ProcessId);
    if (!Process)
        return TRUE;
    if (QueryFullProcessImageNameW(Process, 0, Image, &Size) && !lstrcmpiW(wcsrchr(Image, L'\\') ? wcsrchr(Image, L'\\') + 1 : Image, L"winver.exe"))
    {
        Found->Window = Window;
        Found->ProcessId = ProcessId;
    }
    CloseHandle(Process);
    return Found->Window == NULL;
}

static void Key(WORD VirtualKey, BOOL Up)
{
    INPUT Input = { INPUT_KEYBOARD };
    Input.ki.wVk = VirtualKey;
    Input.ki.dwFlags = (Up ? KEYEVENTF_KEYUP : 0) | (VirtualKey == VK_LWIN ? KEYEVENTF_EXTENDEDKEY : 0);
    ok(SendInput(1, &Input, sizeof(Input)) == 1, "SendInput of key %u failed: %lu\n", VirtualKey, GetLastError());
}

static void Type(PCSTR Text)
{
    for (; *Text; Text++)
    {
        Key((WORD)(*Text & ~0x20), FALSE);
        Key((WORD)(*Text & ~0x20), TRUE);
        Sleep(60);
    }
}

START_TEST(StartSearchCommand)
{
    FOUND Found = { NULL, 0 };
    DWORD Start = GetTickCount();
    int i;

    for (i = 0; i < 100 && !FindWindowW(L"Shell_TrayWnd", NULL); i++)
        Sleep(200);
    ok(FindWindowW(L"Shell_TrayWnd", NULL) != NULL, "the taskbar is not running\n");
    EnumWindows(FindWinver, (LPARAM)&Found);
    ok(Found.Window == NULL, "winver is already running\n");
    if (Found.Window)
        return;

    Sleep(3000);
    Key(VK_LWIN, FALSE);
    Key(VK_LWIN, TRUE);
    Sleep(2500);
    Type("winver");
    Sleep(3000);
    Key(VK_RETURN, FALSE);
    Key(VK_RETURN, TRUE);

    while (!Found.Window && GetTickCount() - Start < 30000)
    {
        Sleep(250);
        EnumWindows(FindWinver, (LPARAM)&Found);
    }
    ok(Found.Window != NULL, "typing winver in the Start search and pressing Enter did not start winver.exe\n");
    if (Found.Window)
    {
        HANDLE Process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, Found.ProcessId);
        PostMessageW(Found.Window, WM_CLOSE, 0, 0);
        if (Process)
        {
            if (WaitForSingleObject(Process, 5000) != WAIT_OBJECT_0)
                TerminateProcess(Process, 0);
            CloseHandle(Process);
        }
    }
}
