/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a logon session ends once its last token is gone
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winbase.h>
#include <winuser.h>
#include <aclapi.h>
#include <lm.h>

#define TEST_USER L"LntLogoffTest"
#define TEST_PASSWORD L"Lnt!Logoff#2026q"

typedef NTSTATUS (NTAPI *PLSA_ENUMERATE_LOGON_SESSIONS)(PULONG, PLUID *);
typedef NTSTATUS (NTAPI *PLSA_FREE_RETURN_BUFFER)(PVOID);

static PLSA_ENUMERATE_LOGON_SESSIONS pLsaEnumerateLogonSessions;
static PLSA_FREE_RETURN_BUFFER pLsaFreeReturnBuffer;

static
BOOL
IsSessionListed(
    _In_ PLUID LogonId)
{
    PLUID Sessions = NULL;
    ULONG Count = 0, Index;
    BOOL Listed = FALSE;
    NTSTATUS Status;

    Status = pLsaEnumerateLogonSessions(&Count, &Sessions);
    ok(Status == STATUS_SUCCESS, "LsaEnumerateLogonSessions failed 0x%lx\n", Status);
    if (Status != STATUS_SUCCESS) return FALSE;

    for (Index = 0; Index < Count && !Listed; Index++)
    {
        Listed = (Sessions[Index].LowPart == LogonId->LowPart && Sessions[Index].HighPart == LogonId->HighPart);
    }

    pLsaFreeReturnBuffer(Sessions);
    return Listed;
}

static
PSID
GetLogonSid(
    _In_ HANDLE Token)
{
    TOKEN_GROUPS *Groups;
    PSID Sid = NULL;
    DWORD Size = 0, Index;

    GetTokenInformation(Token, TokenGroups, NULL, 0, &Size);
    Groups = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Groups == NULL) return NULL;

    if (GetTokenInformation(Token, TokenGroups, Groups, Size, &Size))
    {
        for (Index = 0; Index < Groups->GroupCount && Sid == NULL; Index++)
        {
            if ((Groups->Groups[Index].Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID) continue;

            Size = GetLengthSid(Groups->Groups[Index].Sid);
            Sid = HeapAlloc(GetProcessHeap(), 0, Size);
            if (Sid != NULL) CopySid(Size, Sid, Groups->Groups[Index].Sid);
        }
    }

    HeapFree(GetProcessHeap(), 0, Groups);
    return Sid;
}

static
BOOL
GrantWindowObject(
    _In_ HANDLE Object,
    _In_ PSID Sid,
    _Out_ PACL *OldDacl,
    _Out_ PSECURITY_DESCRIPTOR *Descriptor)
{
    EXPLICIT_ACCESSW Access;
    PACL NewDacl = NULL;
    DWORD Error;

    *OldDacl = NULL;
    *Descriptor = NULL;

    Error = GetSecurityInfo(Object, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, OldDacl, NULL, Descriptor);
    if (Error != ERROR_SUCCESS) return FALSE;

    ZeroMemory(&Access, sizeof(Access));
    Access.grfAccessPermissions = GENERIC_ALL;
    Access.grfAccessMode = GRANT_ACCESS;
    Access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    Access.Trustee.ptstrName = (LPWSTR)Sid;

    Error = SetEntriesInAclW(1, &Access, *OldDacl, &NewDacl);
    if (Error == ERROR_SUCCESS)
        Error = SetSecurityInfo(Object, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NewDacl, NULL);

    LocalFree(NewDacl);
    return Error == ERROR_SUCCESS;
}

static
VOID
RunChildWithToken(
    _In_ HANDLE Token)
{
    HWINSTA WindowStation = GetProcessWindowStation();
    HDESK Desktop = GetThreadDesktop(GetCurrentThreadId());
    PSECURITY_DESCRIPTOR StationDescriptor = NULL, DesktopDescriptor = NULL;
    PACL StationDacl = NULL, DesktopDacl = NULL;
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    WCHAR Image[MAX_PATH], CommandLine[MAX_PATH * 2];
    PROCESS_INFORMATION ProcessInfo;
    DWORD ExitCode = MAXDWORD;
    PSID LogonSid;

    LogonSid = GetLogonSid(Token);
    ok(LogonSid != NULL, "the logon token has no logon SID\n");
    if (LogonSid == NULL) return;

    if (GrantWindowObject(WindowStation, LogonSid, &StationDacl, &StationDescriptor) &&
        GrantWindowObject(Desktop, LogonSid, &DesktopDacl, &DesktopDescriptor))
    {
        GetModuleFileNameW(NULL, Image, ARRAYSIZE(Image));
        StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" LogonSessionEnd child", Image);

        /* A pipe keeps the security context of the last client that wrote to it, so the child writes to none */
        StartupInfo.dwFlags = STARTF_USESTDHANDLES;

        if (CreateProcessWithTokenW(Token, 0, NULL, CommandLine, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
        {
            ok(WaitForSingleObject(ProcessInfo.hProcess, 10000) == WAIT_OBJECT_0, "the child did not exit\n");
            GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
            ok(ExitCode == 0, "the child exited with 0x%lx\n", ExitCode);
            CloseHandle(ProcessInfo.hThread);
            CloseHandle(ProcessInfo.hProcess);
        }
        else
        {
            ok(0, "CreateProcessWithTokenW failed %lu\n", GetLastError());
        }
    }
    else
    {
        ok(0, "cannot grant the window station and desktop to the test user\n");
    }

    if (StationDescriptor != NULL)
    {
        SetSecurityInfo(WindowStation, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, StationDacl, NULL);
        LocalFree(StationDescriptor);
    }

    if (DesktopDescriptor != NULL)
    {
        SetSecurityInfo(Desktop, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, DesktopDacl, NULL);
        LocalFree(DesktopDescriptor);
    }

    HeapFree(GetProcessHeap(), 0, LogonSid);
}

static
BOOL
LogOn(
    _Out_ PHANDLE Token,
    _Out_ PLUID LogonId)
{
    TOKEN_STATISTICS Statistics;
    DWORD Size;

    *Token = NULL;
    ZeroMemory(LogonId, sizeof(*LogonId));

    if (!LogonUserW(TEST_USER, L".", TEST_PASSWORD, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, Token))
    {
        ok(0, "LogonUserW failed %lu\n", GetLastError());
        return FALSE;
    }

    ok(GetTokenInformation(*Token, TokenStatistics, &Statistics, sizeof(Statistics), &Size),
       "GetTokenInformation failed %lu\n", GetLastError());
    *LogonId = Statistics.AuthenticationId;
    return TRUE;
}

START_TEST(LogonSessionEnd)
{
    USER_INFO_1 UserInfo;
    NET_API_STATUS NetStatus;
    HMODULE Secur32;
    LUID First, Second;
    ULONGLONG Deadline;
    HANDLE Token;
    char **Arguments;
    BOOL Listed;
    int Count;

    Count = winetest_get_mainargs(&Arguments);
    if (Count >= 3 && !strcmp(Arguments[2], "child")) return;

    Secur32 = LoadLibraryW(L"secur32.dll");
    pLsaEnumerateLogonSessions = (PLSA_ENUMERATE_LOGON_SESSIONS)GetProcAddress(Secur32, "LsaEnumerateLogonSessions");
    pLsaFreeReturnBuffer = (PLSA_FREE_RETURN_BUFFER)GetProcAddress(Secur32, "LsaFreeReturnBuffer");
    if (!pLsaEnumerateLogonSessions || !pLsaFreeReturnBuffer)
    {
        skip("secur32 has no LsaEnumerateLogonSessions\n");
        return;
    }

    NetUserDel(NULL, TEST_USER);
    ZeroMemory(&UserInfo, sizeof(UserInfo));
    UserInfo.usri1_name = TEST_USER;
    UserInfo.usri1_password = TEST_PASSWORD;
    UserInfo.usri1_priv = USER_PRIV_USER;
    UserInfo.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    NetStatus = NetUserAdd(NULL, 1, (LPBYTE)&UserInfo, NULL);
    if (NetStatus == ERROR_ACCESS_DENIED)
    {
        skip("Creating a user needs administrator rights\n");
        return;
    }
    ok(NetStatus == NERR_Success, "NetUserAdd failed %lu\n", NetStatus);
    if (NetStatus != NERR_Success) return;

    if (LogOn(&Token, &First))
    {
        ok(IsSessionListed(&First), "the new logon session is not listed\n");
        RunChildWithToken(Token);
        CloseHandle(Token);
    }

    if (LogOn(&Token, &Second))
    {
        ok(Second.LowPart != First.LowPart || Second.HighPart != First.HighPart,
           "the second logon reused the first session\n");
        CloseHandle(Token);
    }

    /* The authority hears of the last token going and ends the session after the fact */
    Deadline = GetTickCount64() + 2000;
    do
    {
        Listed = IsSessionListed(&First);
        if (!Listed) break;
        Sleep(1);
    } while (GetTickCount64() < Deadline);
    ok(!Listed, "the first logon session outlived its last token\n");

    NetUserDel(NULL, TEST_USER);
}
