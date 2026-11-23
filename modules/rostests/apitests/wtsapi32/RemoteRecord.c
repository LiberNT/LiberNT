/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test how session station names and remote connection records are returned
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>
#include <wtsapi32.h>
#include <stdio.h>
#include <malloc.h>

#define REMOTE_SESSIONS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\RemoteSessions"
#define LONG_STATION L"RDP-Tcp#1234567890123456789012"

static VOID
CheckEnumeratedNames(DWORD Console, PCWSTR Expected)
{
    PWTS_SESSION_INFOW Info = NULL;
    DWORD Count = 0, i, j;
    BOOL Found = FALSE;
    PCWSTR End;

    ok(WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &Info, &Count), "WTSEnumerateSessionsW failed: %lu\n",
       GetLastError());
    for (i = 0; i < Count; i++)
    {
        ok(Info[i].pWinStationName != NULL, "Session %lu has no station name\n", Info[i].SessionId);
        if (!Info[i].pWinStationName)
            continue;
        ok(wcslen(Info[i].pWinStationName) <= WINSTATIONNAME_LENGTH, "Session %lu has a %Iu character station name\n",
           Info[i].SessionId, wcslen(Info[i].pWinStationName));
        End = Info[i].pWinStationName + wcslen(Info[i].pWinStationName);
        for (j = 0; j < Count; j++)
        {
            if (j == i || !Info[j].pWinStationName)
                continue;
            ok(Info[j].pWinStationName > End || Info[j].pWinStationName < Info[i].pWinStationName,
               "The station name of session %lu runs into the one of session %lu\n", Info[i].SessionId,
               Info[j].SessionId);
        }
        if (Info[i].SessionId == Console && Expected)
        {
            Found = TRUE;
            ok(!wcscmp(Info[i].pWinStationName, Expected), "The console is %ls, not %ls\n", Info[i].pWinStationName,
               Expected);
        }
    }
    ok(!Expected || Found, "Session %lu was not enumerated\n", Console);
    WTSFreeMemory(Info);
    ok(HeapValidate((HANDLE)_get_heap_handle(), 0, NULL), "The heap is damaged after WTSEnumerateSessionsW\n");
}

static VOID
CheckProtocol(DWORD Console, USHORT Expected)
{
    PWSTR Buffer = NULL;
    DWORD Size = 0;

    ok(WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, Console, WTSClientProtocolType, &Buffer, &Size),
       "WTSClientProtocolType failed: %lu\n", GetLastError());
    if (Buffer)
    {
        ok(Size == sizeof(USHORT) && *(USHORT *)Buffer == Expected, "The protocol is %u (%lu bytes), not %u\n",
           *(USHORT *)Buffer, Size, Expected);
        WTSFreeMemory(Buffer);
    }
}

START_TEST(RemoteRecord)
{
    WCHAR Name[MAX_PATH];
    DWORD Console = WTSGetActiveConsoleSessionId(), Protocol = WTS_PROTOCOL_TYPE_RDP, State = WTSActive;
    HKEY Key;
    LONG Error;

    CheckEnumeratedNames(Console, NULL);
    if (!is_reactos())
    {
        skip("The remote connection record is LiberNT's own\n");
        return;
    }
    _snwprintf(Name, ARRAYSIZE(Name), L"%ls\\%lu", REMOTE_SESSIONS_KEY, Console);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Name, 0, KEY_READ, &Key) == ERROR_SUCCESS)
    {
        RegCloseKey(Key);
        skip("Session %lu is connected remotely\n", Console);
        return;
    }
    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, Name, 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &Key, NULL);
    if (Error)
    {
        skip("Cannot write %ls: %ld\n", Name, Error);
        return;
    }
    RegSetValueExW(Key, L"WinStationName", 0, REG_SZ, (BYTE *)LONG_STATION, sizeof(LONG_STATION));
    RegSetValueExW(Key, L"ProtocolType", 0, REG_DWORD, (BYTE *)&Protocol, sizeof(Protocol));
    RegSetValueExW(Key, L"ConnectState", 0, REG_DWORD, (BYTE *)&State, sizeof(State));
    CheckEnumeratedNames(Console, LONG_STATION);
    CheckProtocol(Console, WTS_PROTOCOL_TYPE_RDP);

    RegSetValueExW(Key, L"ProtocolType", 0, REG_SZ, (BYTE *)L"2", sizeof(L"2"));
    CheckProtocol(Console, WTS_PROTOCOL_TYPE_CONSOLE);
    CheckEnumeratedNames(Console, L"Console");

    RegCloseKey(Key);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, Name);
}
