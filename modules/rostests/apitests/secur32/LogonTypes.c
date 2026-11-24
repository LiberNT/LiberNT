/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the groups and logon session type of Interactive and RemoteInteractive logons
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <ntsecapi.h>
#include <lm.h>
#include <stdio.h>

#define PASSWORD L"Logon-Types!2026"
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

static BOOL
AddToRemoteDesktopUsers(PCWSTR User)
{
    SID_IDENTIFIER_AUTHORITY Nt = { SECURITY_NT_AUTHORITY };
    LOCALGROUP_MEMBERS_INFO_3 Member;
    WCHAR Group[GNLEN + 1], Domain[DNLEN + 1];
    DWORD GroupLength = ARRAYSIZE(Group), DomainLength = ARRAYSIZE(Domain);
    SID_NAME_USE Use;
    NET_API_STATUS Error;
    PSID Sid;
    BOOL Found;

    if (!AllocateAndInitializeSid(&Nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_REMOTE_DESKTOP_USERS, 0, 0, 0,
                                  0, 0, 0, &Sid))
        return FALSE;
    Found = LookupAccountSidW(NULL, Sid, Group, &GroupLength, Domain, &DomainLength, &Use);
    FreeSid(Sid);
    ok(Found, "The Remote Desktop Users group has no name: %lu\n", GetLastError());
    if (!Found)
        return FALSE;
    Member.lgrmi3_domainandname = (LPWSTR)User;
    Error = NetLocalGroupAddMembers(NULL, Group, 3, (LPBYTE)&Member, 1);
    ok(Error == NERR_Success, "Adding %ls to %ls failed with %lu\n", User, Group, Error);
    return Error == NERR_Success;
}

static HANDLE
TryLogOn(HANDLE Lsa, ULONG Package, PCWSTR User, SECURITY_LOGON_TYPE Type, NTSTATUS *Result);

static HANDLE
LogOn(HANDLE Lsa, ULONG Package, PCWSTR User, SECURITY_LOGON_TYPE Type)
{
    NTSTATUS Status;
    HANDLE Token = TryLogOn(Lsa, Package, User, Type, &Status);

    ok(Status == STATUS_SUCCESS, "LsaLogonUser(%d) returned 0x%08lx\n", Type, Status);
    return Token;
}

static HANDLE
TryLogOn(HANDLE Lsa, ULONG Package, PCWSTR User, SECURITY_LOGON_TYPE Type, NTSTATUS *Result)
{
    BYTE Buffer[sizeof(MSV1_0_INTERACTIVE_LOGON) + 3 * 64 * sizeof(WCHAR)];
    PMSV1_0_INTERACTIVE_LOGON Logon = (PMSV1_0_INTERACTIVE_LOGON)Buffer;
    PWCHAR Next = (PWCHAR)(Logon + 1);
    LSA_STRING Origin = { 10, 11, "LogonTypes" };
    TOKEN_SOURCE Source = { "Test", { 0, 0 } };
    QUOTA_LIMITS Quota;
    PVOID Profile = NULL;
    ULONG ProfileLength = 0;
    LUID LogonId;
    NTSTATUS Status, SubStatus = 0;
    HANDLE Token = NULL;

    ZeroMemory(Buffer, sizeof(Buffer));
    Logon->MessageType = MsV1_0InteractiveLogon;
    Logon->LogonDomainName.Buffer = Next;
    lstrcpyW(Next, L".");
    Logon->LogonDomainName.Length = 2;
    Logon->LogonDomainName.MaximumLength = 4;
    Next += 2;
    Logon->UserName.Buffer = Next;
    lstrcpyW(Next, User);
    Logon->UserName.Length = (USHORT)(wcslen(User) * sizeof(WCHAR));
    Logon->UserName.MaximumLength = Logon->UserName.Length + sizeof(WCHAR);
    Next += wcslen(User) + 1;
    Logon->Password.Buffer = Next;
    lstrcpyW(Next, PASSWORD);
    Logon->Password.Length = (USHORT)(wcslen(PASSWORD) * sizeof(WCHAR));
    Logon->Password.MaximumLength = Logon->Password.Length + sizeof(WCHAR);
    Next += wcslen(PASSWORD) + 1;
    AllocateLocallyUniqueId(&Source.SourceIdentifier);
    Status = LsaLogonUser(Lsa, &Origin, Type, Package, Logon, (ULONG)((PBYTE)Next - Buffer), NULL, &Source, &Profile,
                          &ProfileLength, &LogonId, &Token, &Quota, &SubStatus);
    if (!NT_SUCCESS(Status))
        trace("LsaLogonUser(%d) returned 0x%08lx, substatus 0x%08lx\n", Type, Status, SubStatus);
    if (Profile)
        LsaFreeReturnBuffer(Profile);
    *Result = Status;
    return NT_SUCCESS(Status) ? Token : NULL;
}

static BOOL
HasGroup(PTOKEN_GROUPS Groups, WELL_KNOWN_SID_TYPE Type)
{
    DWORD i;

    for (i = 0; i < Groups->GroupCount; i++)
    {
        if (IsWellKnownSid(Groups->Groups[i].Sid, Type) && (Groups->Groups[i].Attributes & SE_GROUP_ENABLED))
            return TRUE;
    }
    return FALSE;
}

static VOID
CheckToken(HANDLE Token, SECURITY_LOGON_TYPE Type)
{
    PSECURITY_LOGON_SESSION_DATA Session = NULL;
    TOKEN_STATISTICS Statistics;
    PTOKEN_GROUPS Groups;
    DWORD Length = 0;
    NTSTATUS Status;

    GetTokenInformation(Token, TokenGroups, NULL, 0, &Length);
    Groups = HeapAlloc(GetProcessHeap(), 0, Length);
    ok(Groups && GetTokenInformation(Token, TokenGroups, Groups, Length, &Length), "TokenGroups failed: %lu\n",
       GetLastError());
    if (Groups)
    {
        ok(HasGroup(Groups, WinInteractiveSid), "Logon type %d has no INTERACTIVE group\n", Type);
        ok(HasGroup(Groups, WinRemoteLogonIdSid) == (Type == RemoteInteractive),
           "Logon type %d %s the REMOTE INTERACTIVE LOGON group\n", Type,
           Type == RemoteInteractive ? "lacks" : "has");
        ok(!HasGroup(Groups, WinNetworkSid), "Logon type %d has the NETWORK group\n", Type);
        HeapFree(GetProcessHeap(), 0, Groups);
    }
    ok(GetTokenInformation(Token, TokenStatistics, &Statistics, sizeof(Statistics), &Length),
       "TokenStatistics failed: %lu\n", GetLastError());
    Status = LsaGetLogonSessionData(&Statistics.AuthenticationId, &Session);
    ok(Status == STATUS_SUCCESS, "LsaGetLogonSessionData returned 0x%08lx\n", Status);
    if (Session)
    {
        ok(Session->LogonType == (ULONG)Type, "The logon session has type %lu, not %d\n", Session->LogonType, Type);
        LsaFreeReturnBuffer(Session);
    }
}

static VOID
CheckNetworkToken(HANDLE Token, SECURITY_LOGON_TYPE Type)
{
    PSECURITY_LOGON_SESSION_DATA Session = NULL;
    TOKEN_STATISTICS Statistics;
    TOKEN_TYPE Kind = 0;
    PTOKEN_GROUPS Groups;
    DWORD Length = 0;
    NTSTATUS Status;

    ok(GetTokenInformation(Token, TokenType, &Kind, sizeof(Kind), &Length), "TokenType failed: %lu\n",
       GetLastError());
    ok(Kind == TokenImpersonation, "Logon type %d gave token type %d\n", Type, Kind);
    GetTokenInformation(Token, TokenGroups, NULL, 0, &Length);
    Groups = HeapAlloc(GetProcessHeap(), 0, Length);
    ok(Groups && GetTokenInformation(Token, TokenGroups, Groups, Length, &Length), "TokenGroups failed: %lu\n",
       GetLastError());
    if (Groups)
    {
        ok(HasGroup(Groups, WinNetworkSid), "Logon type %d has no NETWORK group\n", Type);
        ok(!HasGroup(Groups, WinInteractiveSid), "Logon type %d has the INTERACTIVE group\n", Type);
        HeapFree(GetProcessHeap(), 0, Groups);
    }
    ok(GetTokenInformation(Token, TokenStatistics, &Statistics, sizeof(Statistics), &Length),
       "TokenStatistics failed: %lu\n", GetLastError());
    Status = LsaGetLogonSessionData(&Statistics.AuthenticationId, &Session);
    ok(Status == STATUS_SUCCESS, "LsaGetLogonSessionData returned 0x%08lx\n", Status);
    if (Session)
    {
        ok(Session->LogonType == (ULONG)Type, "The logon session has type %lu, not %d\n", Session->LogonType, Type);
        LsaFreeReturnBuffer(Session);
    }
}

START_TEST(LogonTypes)
{
    static WCHAR Password[] = PASSWORD;
    LSA_STRING PackageName = { sizeof(MSV1_0_PACKAGE_NAME) - 1, sizeof(MSV1_0_PACKAGE_NAME), MSV1_0_PACKAGE_NAME };
    USER_INFO_1 Info = { 0 };
    WCHAR User[16];
    HANDLE Lsa = NULL, Token;
    ULONG Package = 0;
    NET_API_STATUS Error;
    NTSTATUS Status;

    _snwprintf(User, ARRAYSIZE(User), L"lt%06lx", GetTickCount() & 0xffffff);
    Info.usri1_name = User;
    Info.usri1_password = Password;
    Info.usri1_priv = USER_PRIV_USER;
    Info.usri1_flags = UF_SCRIPT | UF_DONT_EXPIRE_PASSWD;
    Error = NetUserAdd(NULL, 1, (LPBYTE)&Info, NULL);
    if (Error != NERR_Success)
    {
        skip("NetUserAdd failed with %lu\n", Error);
        return;
    }
    Status = LsaConnectUntrusted(&Lsa);
    ok(Status == STATUS_SUCCESS, "LsaConnectUntrusted returned 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Status = LsaLookupAuthenticationPackage(Lsa, &PackageName, &Package);
        ok(Status == STATUS_SUCCESS, "LsaLookupAuthenticationPackage returned 0x%08lx\n", Status);
        Token = LogOn(Lsa, Package, User, Interactive);
        if (Token)
        {
            CheckToken(Token, Interactive);
            CloseHandle(Token);
        }
        Token = LogOn(Lsa, Package, User, Network);
        if (Token)
        {
            CheckNetworkToken(Token, Network);
            CloseHandle(Token);
        }
        Token = LogOn(Lsa, Package, User, NetworkCleartext);
        if (Token)
        {
            CheckNetworkToken(Token, NetworkCleartext);
            CloseHandle(Token);
        }
        Token = TryLogOn(Lsa, Package, User, RemoteInteractive, &Status);
        ok(Status == STATUS_LOGON_TYPE_NOT_GRANTED,
           "A user outside Remote Desktop Users got a RemoteInteractive logon: 0x%08lx\n", Status);
        if (Token)
            CloseHandle(Token);
        if (AddToRemoteDesktopUsers(User))
        {
            Token = LogOn(Lsa, Package, User, RemoteInteractive);
            if (Token)
            {
                CheckToken(Token, RemoteInteractive);
                CloseHandle(Token);
            }
        }
        LsaDeregisterLogonProcess(Lsa);
    }
    NetUserDel(NULL, User);
}
