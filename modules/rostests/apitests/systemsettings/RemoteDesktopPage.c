/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The Remote Desktop page of the Settings app exists only where the Remote Desktop server does
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>

#include <windows.h>
#include <ntstatus.h>

static BOOL ServerInstalled(void)
{
    WCHAR Path[MAX_PATH];
    DWORD Size = sizeof(Path);
    return RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\LiberNTRDPServer", L"ImagePath",
                        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, NULL, Path, &Size) == ERROR_SUCCESS;
}

static BOOL ReadDebugString(HANDLE Process, const OUTPUT_DEBUG_STRING_INFO *Info, WCHAR *Text, SIZE_T Count)
{
    SIZE_T Length = min((SIZE_T)Info->nDebugStringLength, Count - 1), Read = 0;
    if (Info->fUnicode)
    {
        if (!ReadProcessMemory(Process, Info->lpDebugStringData, Text, Length * sizeof(WCHAR), &Read))
            return FALSE;
        Text[Read / sizeof(WCHAR)] = UNICODE_NULL;
    }
    else
    {
        CHAR Ansi[512];
        Length = min(Length, sizeof(Ansi) - 1);
        if (!ReadProcessMemory(Process, Info->lpDebugStringData, Ansi, Length, &Read))
            return FALSE;
        Ansi[Read] = ANSI_NULL;
        if (!MultiByteToWideChar(CP_ACP, 0, Ansi, -1, Text, (int)Count))
            return FALSE;
    }
    return TRUE;
}

START_TEST(RemoteDesktopPage)
{
    WCHAR Path[MAX_PATH], Command[MAX_PATH + 64], Text[512];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Info;
    DEBUG_EVENT Event;
    BOOL Done = FALSE, Exited = FALSE, Page = FALSE, Toggle = FALSE;
    DWORD Start;

    GetWindowsDirectoryW(Path, _countof(Path));
    lstrcatW(Path, L"\\ImmersiveControlPanel\\systemsettings.exe");
    wsprintfW(Command, L"\"%s\" /selftest settings-dump", Path);
    if (!CreateProcessW(Path, Command, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &Startup, &Info))
    {
        skip("systemsettings.exe did not start: %lu\n", GetLastError());
        return;
    }

    Start = GetTickCount();
    while (!Exited && GetTickCount() - Start < 60000)
    {
        DWORD Status = DBG_CONTINUE;
        if (!WaitForDebugEvent(&Event, 1000))
            continue;
        if (Event.dwDebugEventCode == OUTPUT_DEBUG_STRING_EVENT && Event.dwProcessId == Info.dwProcessId &&
            ReadDebugString(Info.hProcess, &Event.u.DebugString, Text, _countof(Text)))
        {
            if (!wcsncmp(Text, L"SETDUMP: DONE", 13))
                Done = TRUE;
            else if (!wcsncmp(Text, L"SETDUMP: remotedesktop |", 24))
            {
                Page = TRUE;
                if (wcsstr(Text, L"| on") || wcsstr(Text, L"| off"))
                    Toggle = TRUE;
            }
        }
        else if (Event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
                 Event.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT)
        {
            Status = DBG_EXCEPTION_NOT_HANDLED;
        }
        else if (Event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT && Event.u.CreateProcessInfo.hFile)
        {
            CloseHandle(Event.u.CreateProcessInfo.hFile);
        }
        else if (Event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && Event.u.LoadDll.hFile)
        {
            CloseHandle(Event.u.LoadDll.hFile);
        }
        else if (Event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT && Event.dwProcessId == Info.dwProcessId)
        {
            Exited = TRUE;
        }
        ContinueDebugEvent(Event.dwProcessId, Event.dwThreadId, Status);
    }
    if (!Exited)
    {
        TerminateProcess(Info.hProcess, 1);
        DebugActiveProcessStop(Info.dwProcessId);
    }
    CloseHandle(Info.hThread);
    CloseHandle(Info.hProcess);

    if (!Done)
    {
        skip("systemsettings.exe has no settings dump\n");
        return;
    }
    if (ServerInstalled())
    {
        ok(Page, "The Remote Desktop page is missing although the server is installed\n");
        ok(Toggle, "The Remote Desktop page has no toggle\n");
    }
    else
    {
        ok(!Page, "The Remote Desktop page is listed although the server is not installed\n");
    }
}
