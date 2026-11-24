/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the session's desktop compositor is started again after it exits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <tlhelp32.h>

#define DWM_EXIT_WAIT_MS 2000
#define DWM_RESTART_WAIT_MS 3000

static DWORD
FindCompositor(_In_ DWORD SessionId, _In_ DWORD ExcludedId)
{
    PROCESSENTRY32W Entry;
    HANDLE Snapshot;
    DWORD Found = 0;
    DWORD ProcessSession;
    BOOL More;

    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
        return 0;
    Entry.dwSize = sizeof(Entry);
    for (More = Process32FirstW(Snapshot, &Entry); More && Found == 0; More = Process32NextW(Snapshot, &Entry))
    {
        if (_wcsicmp(Entry.szExeFile, L"dwm.exe") == 0 &&
            Entry.th32ProcessID != ExcludedId &&
            ProcessIdToSessionId(Entry.th32ProcessID, &ProcessSession) &&
            ProcessSession == SessionId)
        {
            Found = Entry.th32ProcessID;
        }
    }
    CloseHandle(Snapshot);
    return Found;
}

START_TEST(dwm_restart)
{
    DWORD SessionId;
    DWORD Old;
    DWORD New = 0;
    DWORD Start;
    HANDLE Process;

    ok(ProcessIdToSessionId(GetCurrentProcessId(), &SessionId), "ProcessIdToSessionId failed with %lu\n", GetLastError());
    Old = FindCompositor(SessionId, 0);
    if (Old == 0)
    {
        skip("no dwm.exe in session %lu\n", SessionId);
        return;
    }
    Process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, Old);
    if (Process == NULL)
    {
        skip("dwm.exe %lu cannot be terminated (error %lu)\n", Old, GetLastError());
        return;
    }
    ok(TerminateProcess(Process, 1), "TerminateProcess failed with %lu\n", GetLastError());
    ok(WaitForSingleObject(Process, DWM_EXIT_WAIT_MS) == WAIT_OBJECT_0, "dwm.exe %lu did not exit\n", Old);
    CloseHandle(Process);

    Start = GetTickCount();
    New = FindCompositor(SessionId, Old);
    while (New == 0 && GetTickCount() - Start < DWM_RESTART_WAIT_MS)
    {
        Sleep(50);
        New = FindCompositor(SessionId, Old);
    }
    ok(New != 0, "no dwm.exe in session %lu %lu ms after %lu exited\n", SessionId, GetTickCount() - Start, Old);
    if (New != 0)
        trace("dwm.exe %lu replaced %lu after %lu ms\n", New, Old, GetTickCount() - Start);
}
