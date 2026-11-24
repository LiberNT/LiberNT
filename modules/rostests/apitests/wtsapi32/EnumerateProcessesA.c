/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for WTSEnumerateProcessesA
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <winnls.h>
#include <wtsapi32.h>

static void CheckFails(DWORD Reserved, DWORD Version, BOOL PassInfo, BOOL PassCount, DWORD Error)
{
    PWTS_PROCESS_INFOA Info = NULL;
    DWORD Count = 0;
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = WTSEnumerateProcessesA(WTS_CURRENT_SERVER_HANDLE, Reserved, Version,
                                 PassInfo ? &Info : NULL, PassCount ? &Count : NULL);
    ok(!Ret, "reserved %lu version %lu info %d count %d: succeeded\n", Reserved, Version, PassInfo, PassCount);
    ok(GetLastError() == Error, "reserved %lu version %lu info %d count %d: error %lu\n",
       Reserved, Version, PassInfo, PassCount, GetLastError());
    if (PassCount)
        ok(Info == NULL, "reserved %lu version %lu: info %p\n", Reserved, Version, Info);
}

static PSID GetOwnUserSid(BYTE *Buffer, DWORD Size)
{
    HANDLE Token;
    DWORD Returned;
    BOOL Ret;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
        return NULL;
    Ret = GetTokenInformation(Token, TokenUser, Buffer, Size, &Returned);
    CloseHandle(Token);
    return Ret ? ((TOKEN_USER *)Buffer)->User.Sid : NULL;
}

START_TEST(EnumerateProcessesA)
{
    BYTE UserBuffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    PWTS_PROCESS_INFOA Info = NULL;
    PWTS_PROCESS_INFOW InfoW = NULL;
    CHAR Path[MAX_PATH], *Name;
    DWORD Count = 0, CountW = 0, Session = MAXDWORD, i;
    PSID UserSid;
    BOOL Ret, FoundSelf = FALSE, FoundSystem = FALSE, FoundSelfW = FALSE;

    CheckFails(1, 1, TRUE, TRUE, ERROR_INVALID_PARAMETER);
    CheckFails(0, 0, TRUE, TRUE, ERROR_INVALID_PARAMETER);
    CheckFails(0, 2, TRUE, TRUE, ERROR_INVALID_PARAMETER);
    CheckFails(0, 1, FALSE, TRUE, ERROR_INVALID_USER_BUFFER);
    CheckFails(0, 1, TRUE, FALSE, ERROR_INVALID_USER_BUFFER);

    UserSid = GetOwnUserSid(UserBuffer, sizeof(UserBuffer));
    ok(UserSid != NULL, "no token user, error %lu\n", GetLastError());
    ok(ProcessIdToSessionId(GetCurrentProcessId(), &Session), "ProcessIdToSessionId failed, error %lu\n", GetLastError());
    GetModuleFileNameA(NULL, Path, sizeof(Path));
    Name = strrchr(Path, '\\') ? strrchr(Path, '\\') + 1 : Path;

    SetLastError(0xdeadbeef);
    Ret = WTSEnumerateProcessesA(WTS_CURRENT_SERVER_HANDLE, 0, 1, &Info, &Count);
    ok(Ret, "WTSEnumerateProcessesA failed, error %lu\n", GetLastError());
    ok(GetLastError() == 0, "error %lu\n", GetLastError());
    if (!Ret)
        return;
    ok(Info != NULL, "no process array\n");
    ok(Count > 1, "%lu processes\n", Count);

    for (i = 0; i < Count; i++)
    {
        ok(Info[i].pProcessName != NULL, "process %lu has no name\n", Info[i].ProcessId);
        if (Info[i].pProcessName == NULL)
            continue;
        if (Info[i].pUserSid != NULL)
            ok(IsValidSid(Info[i].pUserSid), "process %lu has an invalid SID\n", Info[i].ProcessId);

        if (Info[i].ProcessId == GetCurrentProcessId())
        {
            FoundSelf = TRUE;
            ok(!lstrcmpiA(Info[i].pProcessName, Name), "own process named '%s', expected '%s'\n", Info[i].pProcessName, Name);
            ok(Info[i].SessionId == Session, "own session %lu, expected %lu\n", Info[i].SessionId, Session);
            ok(Info[i].pUserSid != NULL, "own process has no user SID\n");
            if (Info[i].pUserSid != NULL && UserSid != NULL)
                ok(EqualSid(Info[i].pUserSid, UserSid), "own process has another user SID\n");
        }
        else if (Info[i].ProcessId == 4)
        {
            FoundSystem = TRUE;
            ok(!lstrcmpA(Info[i].pProcessName, "System"), "process 4 named '%s'\n", Info[i].pProcessName);
            ok(Info[i].SessionId == 0, "process 4 in session %lu\n", Info[i].SessionId);
        }
    }
    ok(FoundSelf, "own process %lu not listed\n", GetCurrentProcessId());
    ok(FoundSystem, "process 4 not listed\n");
    WTSFreeMemory(Info);

    Ret = WTSEnumerateProcessesW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &InfoW, &CountW);
    ok(Ret, "WTSEnumerateProcessesW failed, error %lu\n", GetLastError());
    if (!Ret)
        return;
    for (i = 0; i < CountW; i++)
    {
        CHAR Converted[MAX_PATH];

        if (InfoW[i].ProcessId != GetCurrentProcessId())
            continue;
        FoundSelfW = TRUE;
        WideCharToMultiByte(CP_ACP, 0, InfoW[i].pProcessName, -1, Converted, sizeof(Converted), NULL, NULL);
        ok(!lstrcmpiA(Converted, Name), "own process named '%s' by WTSEnumerateProcessesW\n", Converted);
    }
    ok(FoundSelfW, "own process not listed by WTSEnumerateProcessesW\n");
    WTSFreeMemory(InfoW);
}
