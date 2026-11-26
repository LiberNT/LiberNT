/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A choice of the Settings app is applied when it is picked, not while the list is browsed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>

typedef struct _SEARCH { DWORD ProcessId; HWND Window; HWND Combo; } SEARCH;

static BOOL CALLBACK FindTop(HWND Window, LPARAM Param)
{
    SEARCH *Search = (SEARCH *)Param;
    DWORD ProcessId = 0;
    WCHAR Class[64];
    GetWindowThreadProcessId(Window, &ProcessId);
    if (ProcessId == Search->ProcessId && IsWindowVisible(Window) && GetClassNameW(Window, Class, _countof(Class)) &&
        lstrcmpW(Class, L"#32770"))
    {
        Search->Window = Window;
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK FindScale(HWND Window, LPARAM Param)
{
    SEARCH *Search = (SEARCH *)Param;
    WCHAR Class[64], Item[64];
    LRESULT Count, i;
    if (!GetClassNameW(Window, Class, _countof(Class)) || lstrcmpiW(Class, L"ComboBox"))
        return TRUE;
    Count = SendMessageW(Window, CB_GETCOUNT, 0, 0);
    for (i = 0; i < Count; i++)
    {
        if (SendMessageW(Window, CB_GETLBTEXTLEN, i, 0) < (LRESULT)_countof(Item) &&
            SendMessageW(Window, CB_GETLBTEXT, i, (LPARAM)Item) != CB_ERR && !wcsncmp(Item, L"125%", 4))
        {
            Search->Combo = Window;
            return FALSE;
        }
    }
    return TRUE;
}

static DWORD LogPixels(void)
{
    DWORD Value = 0, Size = sizeof(Value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontDPI", L"LogPixels",
                     RRF_RT_REG_DWORD, NULL, &Value, &Size) != ERROR_SUCCESS)
        return 0;
    return Value;
}

static void Key(HWND Window, WPARAM VirtualKey)
{
    SendMessageW(Window, WM_KEYDOWN, VirtualKey, 1);
    SendMessageW(Window, WM_KEYUP, VirtualKey, 0xC0000001);
    Sleep(1500);
    ok(IsWindow(Window), "the Scale choice was destroyed after key %Iu\n", VirtualKey);
}

START_TEST(SettingsChoiceCommit)
{
    WCHAR Path[MAX_PATH], Command[MAX_PATH + 32];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Info;
    SEARCH Search = { 0 };
    LRESULT Start, Browsed;
    DWORD Before;
    int i;

    GetWindowsDirectoryW(Path, _countof(Path));
    lstrcatW(Path, L"\\ImmersiveControlPanel\\systemsettings.exe");
    wsprintfW(Command, L"\"%s\" ms-settings:display", Path);
    if (!CreateProcessW(Path, Command, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Info))
    {
        skip("systemsettings.exe did not start: %lu\n", GetLastError());
        return;
    }
    Search.ProcessId = Info.dwProcessId;
    for (i = 0; i < 100 && !Search.Combo; i++)
    {
        Sleep(200);
        Search.Window = NULL;
        EnumWindows(FindTop, (LPARAM)&Search);
        if (Search.Window)
            EnumChildWindows(Search.Window, FindScale, (LPARAM)&Search);
    }
    {
        HWND Last = Search.Combo;
        DWORD Stable = GetTickCount(), Begin = GetTickCount();
        while (Search.Combo && GetTickCount() - Stable < 3000 && GetTickCount() - Begin < 60000)
        {
            Sleep(250);
            Search.Combo = NULL;
            EnumChildWindows(Search.Window, FindScale, (LPARAM)&Search);
            if (Search.Combo != Last)
            {
                Last = Search.Combo;
                Stable = GetTickCount();
            }
        }
    }
    ok(Search.Combo != NULL, "the Scale choice of the Display page was not found\n");
    if (!Search.Combo)
        goto done;
    ok(IsWindowEnabled(Search.Combo), "the Scale choice is disabled\n");

    Before = LogPixels();
    Start = SendMessageW(Search.Combo, CB_GETCURSEL, 0, 0);
    ok(Start == 0, "the Scale choice starts at %Id\n", Start);

    SendMessageW(Search.Combo, CB_SHOWDROPDOWN, TRUE, 0);
    ok(SendMessageW(Search.Combo, CB_GETDROPPEDSTATE, 0, 0), "the list did not open\n");
    Key(Search.Combo, VK_DOWN);
    Browsed = SendMessageW(Search.Combo, CB_GETCURSEL, 0, 0);
    ok(Browsed == Start + 1, "browsing moved the selection to %Id\n", Browsed);
    ok(LogPixels() == Before, "browsing the open list applied the scale: LogPixels %lu, was %lu\n", LogPixels(), Before);

    Key(Search.Combo, VK_ESCAPE);
    ok(!SendMessageW(Search.Combo, CB_GETDROPPEDSTATE, 0, 0), "Escape did not close the list\n");
    ok(SendMessageW(Search.Combo, CB_GETCURSEL, 0, 0) == Start, "cancelling left the selection at %Id\n",
       SendMessageW(Search.Combo, CB_GETCURSEL, 0, 0));
    ok(LogPixels() == Before, "cancelling applied the scale: LogPixels %lu, was %lu\n", LogPixels(), Before);

    SendMessageW(Search.Combo, CB_SHOWDROPDOWN, TRUE, 0);
    Key(Search.Combo, VK_DOWN);
    Key(Search.Combo, VK_RETURN);
    ok(LogPixels() == 120, "picking 125%% gave LogPixels %lu\n", LogPixels());

done:
    TerminateProcess(Info.hProcess, 0);
    WaitForSingleObject(Info.hProcess, 5000);
    CloseHandle(Info.hProcess);
    CloseHandle(Info.hThread);
}
