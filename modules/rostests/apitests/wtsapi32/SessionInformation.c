/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for WTSQuerySessionInformation, WTSEnumerateSessions and WTSQueryUserToken
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <wtsapi32.h>
#include <tlhelp32.h>
#include <ntsecapi.h>

typedef struct _SHELL_USER
{
    BOOL Found;
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    WCHAR Name[256];
    WCHAR Domain[256];
} SHELL_USER;

static BOOL GetShellUser(DWORD SessionId, SHELL_USER *User)
{
    PROCESSENTRY32W Entry = { sizeof(Entry) };
    HANDLE Snapshot, Process, Token;
    BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    TOKEN_USER *UserInfo = (TOKEN_USER *)Buffer;
    DWORD ProcessSession, Size, NameLength, DomainLength;
    SID_NAME_USE Use;

    memset(User, 0, sizeof(*User));
    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
        return FALSE;
    if (Process32FirstW(Snapshot, &Entry))
    {
        do
        {
            if (User->Found || _wcsicmp(Entry.szExeFile, L"explorer.exe") ||
                !ProcessIdToSessionId(Entry.th32ProcessID, &ProcessSession) || ProcessSession != SessionId)
                continue;
            Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Entry.th32ProcessID);
            if (!Process)
                continue;
            if (OpenProcessToken(Process, TOKEN_QUERY, &Token))
            {
                if (GetTokenInformation(Token, TokenUser, Buffer, sizeof(Buffer), &Size))
                {
                    CopySid(sizeof(User->Sid), User->Sid, UserInfo->User.Sid);
                    NameLength = ARRAYSIZE(User->Name);
                    DomainLength = ARRAYSIZE(User->Domain);
                    User->Found = LookupAccountSidW(NULL, UserInfo->User.Sid, User->Name, &NameLength,
                                                    User->Domain, &DomainLength, &Use);
                }
                CloseHandle(Token);
            }
            CloseHandle(Process);
        } while (Process32NextW(Snapshot, &Entry));
    }
    CloseHandle(Snapshot);
    return User->Found;
}

static BOOL HasEnabledTcb(void)
{
    BYTE Buffer[4096];
    TOKEN_PRIVILEGES *Privileges = (TOKEN_PRIVILEGES *)Buffer;
    HANDLE Token;
    DWORD Size, i;
    BOOL Enabled = FALSE;
    LUID Tcb;

    if (!LookupPrivilegeValueW(NULL, L"SeTcbPrivilege", &Tcb) ||
        !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
        return FALSE;
    if (GetTokenInformation(Token, TokenPrivileges, Privileges, sizeof(Buffer), &Size))
    {
        for (i = 0; i < Privileges->PrivilegeCount; i++)
        {
            if (Privileges->Privileges[i].Luid.LowPart == Tcb.LowPart &&
                Privileges->Privileges[i].Luid.HighPart == Tcb.HighPart &&
                (Privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED))
                Enabled = TRUE;
        }
    }
    CloseHandle(Token);
    return Enabled;
}

static BOOL QueryInfo(DWORD SessionId, WTS_INFO_CLASS Class, LPWSTR *Buffer, DWORD *Bytes)
{
    *Buffer = NULL;
    *Bytes = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    return WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, SessionId, Class, Buffer, Bytes);
}

static void CheckString(DWORD SessionId, WTS_INFO_CLASS Class, PCWSTR Expected, const char *Name)
{
    LPWSTR Buffer;
    DWORD Bytes;
    BOOL Ok = QueryInfo(SessionId, Class, &Buffer, &Bytes);

    ok(Ok, "session %lu %s: error %lu\n", SessionId, Name, GetLastError());
    if (!Ok)
        return;
    ok(Bytes == (wcslen(Expected) + 1) * sizeof(WCHAR), "session %lu %s: bytes %lu\n", SessionId, Name, Bytes);
    ok(!_wcsicmp(Buffer, Expected), "session %lu %s: '%ls', expected '%ls'\n", SessionId, Name, Buffer, Expected);
    WTSFreeMemory(Buffer);
}

static void CheckValue(DWORD SessionId, WTS_INFO_CLASS Class, DWORD ExpectedBytes, DWORD Expected, const char *Name)
{
    LPWSTR Buffer;
    DWORD Bytes, Value = 0;
    BOOL Ok = QueryInfo(SessionId, Class, &Buffer, &Bytes);

    ok(Ok, "session %lu %s: error %lu\n", SessionId, Name, GetLastError());
    if (!Ok)
        return;
    ok(Bytes == ExpectedBytes, "session %lu %s: bytes %lu, expected %lu\n", SessionId, Name, Bytes, ExpectedBytes);
    if (Bytes <= sizeof(Value))
        memcpy(&Value, Buffer, Bytes);
    ok(Value == Expected, "session %lu %s: value %lu, expected %lu\n", SessionId, Name, Value, Expected);
    WTSFreeMemory(Buffer);
}

static void TestEnumerate(DWORD Console)
{
    PWTS_SESSION_INFOW Sessions = NULL;
    DWORD Count = 0, i;
    BOOL FoundServices = FALSE, FoundConsole = FALSE;

    ok(WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &Sessions, &Count), "enumerate error %lu\n", GetLastError());
    for (i = 0; i < Count; i++)
    {
        if (Sessions[i].SessionId == 0)
        {
            FoundServices = TRUE;
            ok(Sessions[i].State == WTSDisconnected, "session 0 state %d\n", Sessions[i].State);
            ok(!wcscmp(Sessions[i].pWinStationName, L"Services"), "session 0 name '%ls'\n", Sessions[i].pWinStationName);
        }
        else if (Sessions[i].SessionId == Console)
        {
            FoundConsole = TRUE;
            ok(Sessions[i].State == WTSActive, "console state %d\n", Sessions[i].State);
            ok(!wcscmp(Sessions[i].pWinStationName, L"Console"), "console name '%ls'\n", Sessions[i].pWinStationName);
        }
    }
    ok(FoundServices, "session 0 not enumerated\n");
    ok(FoundConsole, "console session %lu not enumerated\n", Console);
    if (Sessions)
        WTSFreeMemory(Sessions);
}

static void TestUserToken(DWORD Console, const SHELL_USER *Shell)
{
    BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    TOKEN_USER *UserInfo = (TOKEN_USER *)Buffer;
    TOKEN_TYPE Type = 0;
    HANDLE Token = NULL;
    DWORD Size;
    BOOL Ok;

    SetLastError(0xdeadbeef);
    ok(!WTSQueryUserToken(Console, NULL), "NULL token pointer accepted\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL token pointer error %lu\n", GetLastError());

    if (!HasEnabledTcb())
    {
        SetLastError(0xdeadbeef);
        ok(!WTSQueryUserToken(Console, &Token), "token returned without SeTcbPrivilege\n");
        ok(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "without SeTcbPrivilege error %lu\n", GetLastError());
        return;
    }

    SetLastError(0xdeadbeef);
    ok(!WTSQueryUserToken(0, &Token), "session 0 returned a token\n");
    ok(GetLastError() == ERROR_NO_TOKEN, "session 0 error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ok = WTSQueryUserToken(Console, &Token);
    ok(Ok, "console token error %lu\n", GetLastError());
    if (!Ok)
        return;
    ok(GetTokenInformation(Token, TokenType, &Type, sizeof(Type), &Size) && Type == TokenPrimary, "token type %d\n", Type);
    if (Shell->Found && GetTokenInformation(Token, TokenUser, Buffer, sizeof(Buffer), &Size))
        ok(EqualSid(UserInfo->User.Sid, (PSID)Shell->Sid), "console token is not the shell user's token\n");
    CloseHandle(Token);
}

static void TestLogonSessions(void)
{
    PSECURITY_LOGON_SESSION_DATA Data;
    PLUID Ids = NULL;
    ULONG Count = 0, i;
    NTSTATUS Status;

    Status = LsaEnumerateLogonSessions(&Count, &Ids);
    ok(Status == 0, "LsaEnumerateLogonSessions returned %#lx\n", Status);
    if (Status != 0)
        return;
    ok(!Count || Ids != NULL, "enumerated %lu sessions with no list\n", Count);
    if (Ids)
    {
        for (i = 0; i < Count; i++)
        {
            Data = NULL;
            Status = LsaGetLogonSessionData(&Ids[i], &Data);
            ok(Status == 0 && Data != NULL, "session %lu: status %#lx, data %p\n", i, Status, Data);
            if (Status == 0 && Data)
            {
                ok(Data->LogonId.LowPart == Ids[i].LowPart && Data->LogonId.HighPart == Ids[i].HighPart,
                   "session %lu: unexpected logon ID\n", i);
                ok(Data->Size >= FIELD_OFFSET(SECURITY_LOGON_SESSION_DATA, Upn) + sizeof(Data->Upn),
                   "session %lu: data size %lu\n", i, Data->Size);
                LsaFreeReturnBuffer(Data);
            }
        }
        LsaFreeReturnBuffer(Ids);
    }
}

START_TEST(SessionInformation)
{
    SHELL_USER Shell;
    LPWSTR Buffer;
    char *BufferA;
    DWORD Console = WTSGetActiveConsoleSessionId(), Bytes;
    BOOL Ok;

    ok(Console != 0xffffffff, "no active console session\n");
    GetShellUser(Console, &Shell);
    ok(Shell.Found, "no explorer.exe in the console session %lu\n", Console);
    TestLogonSessions();

    /* A session that does not exist fails without touching the size */
    Ok = QueryInfo(Console + 100, WTSConnectState, &Buffer, &Bytes);
    ok(!Ok, "unknown session succeeded\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "unknown session error %lu\n", GetLastError());
    ok(Bytes == 0xdeadbeef, "unknown session bytes %lu\n", Bytes);

    /* Session 0 has no interactive user */
    CheckString(0, WTSUserName, L"", "WTSUserName");
    CheckString(0, WTSDomainName, L"", "WTSDomainName");
    CheckString(0, WTSWinStationName, L"Services", "WTSWinStationName");
    CheckValue(0, WTSConnectState, sizeof(DWORD), WTSDisconnected, "WTSConnectState");
    CheckValue(0, WTSSessionId, sizeof(DWORD), 0, "WTSSessionId");
    CheckValue(0, WTSClientProtocolType, sizeof(USHORT), 0, "WTSClientProtocolType");
    CheckValue(0, WTSIsRemoteSession, 1, 0, "WTSIsRemoteSession");

    /* The console session reports its own logged-on user, whoever asks */
    if (Shell.Found)
    {
        CheckString(Console, WTSUserName, Shell.Name, "WTSUserName");
        CheckString(Console, WTSDomainName, Shell.Domain, "WTSDomainName");
    }
    CheckString(Console, WTSWinStationName, L"Console", "WTSWinStationName");
    CheckValue(Console, WTSConnectState, sizeof(DWORD), WTSActive, "WTSConnectState");
    CheckValue(Console, WTSSessionId, sizeof(DWORD), Console, "WTSSessionId");
    CheckValue(Console, WTSIsRemoteSession, 1, 0, "WTSIsRemoteSession");

    BufferA = NULL;
    Bytes = 0xdeadbeef;
    Ok = WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, Console, WTSIsRemoteSession, &BufferA, &Bytes);
    ok(Ok, "ANSI WTSIsRemoteSession error %lu\n", GetLastError());
    ok(Bytes == 1, "ANSI WTSIsRemoteSession bytes %lu\n", Bytes);
    if (Ok)
        WTSFreeMemory(BufferA);
    BufferA = NULL;
    Bytes = 0xdeadbeef;
    Ok = WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, Console, WTSSessionId, &BufferA, &Bytes);
    ok(Ok, "ANSI WTSSessionId error %lu\n", GetLastError());
    ok(Bytes == sizeof(DWORD) && Ok && *(DWORD *)BufferA == Console, "ANSI WTSSessionId bytes %lu\n", Bytes);
    if (Ok)
        WTSFreeMemory(BufferA);

    TestEnumerate(Console);
    TestUserToken(Console, &Shell);
}
