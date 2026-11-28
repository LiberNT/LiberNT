/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for MSV1_0 S4U logons
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <ntsecapi.h>
#include <lm.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <stdio.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define NO_TOKEN ((TOKEN_TYPE)0)

static NTSTATUS DoS4U(HANDLE Lsa, ULONG Package, SECURITY_LOGON_TYPE Type, PCWSTR User, PCWSTR Domain,
                      NTSTATUS *SubStatus, HANDLE *Token)
{
    BYTE Buffer[sizeof(MSV1_0_S4U_LOGON) + 1024];
    PMSV1_0_S4U_LOGON Info = (PMSV1_0_S4U_LOGON)Buffer;
    USHORT ULen = (USHORT)(wcslen(User) * sizeof(WCHAR)), DLen = (USHORT)(wcslen(Domain) * sizeof(WCHAR));
    LSA_STRING Origin;
    TOKEN_SOURCE Source = {{'S','4','U','T','e','s','t',' '}, {0, 0}};
    PVOID Profile = NULL;
    ULONG ProfileLen = 0;
    LUID LogonId;
    QUOTA_LIMITS Quotas;
    NTSTATUS Status;

    ZeroMemory(Buffer, sizeof(Buffer));
    Info->MessageType = MsV1_0S4ULogon;
    Info->UserPrincipalName.Length = Info->UserPrincipalName.MaximumLength = ULen;
    Info->UserPrincipalName.Buffer = (PWSTR)(Info + 1);
    memcpy(Info->UserPrincipalName.Buffer, User, ULen);
    Info->DomainName.Length = Info->DomainName.MaximumLength = DLen;
    Info->DomainName.Buffer = (PWSTR)((PBYTE)(Info + 1) + ULen);
    memcpy(Info->DomainName.Buffer, Domain, DLen);
    Origin.Buffer = "S4UTest";
    Origin.Length = Origin.MaximumLength = 7;
    AllocateLocallyUniqueId(&Source.SourceIdentifier);
    *Token = NULL;
    *SubStatus = 0x0BADF00D;
    Status = LsaLogonUser(Lsa, &Origin, Type, Package, Info, sizeof(*Info) + ULen + DLen, NULL, &Source,
                          &Profile, &ProfileLen, &LogonId, Token, &Quotas, SubStatus);
    if (Profile)
        LsaFreeReturnBuffer(Profile);
    return Status;
}

static BOOL SameName(PLSA_UNICODE_STRING Name, PCWSTR Expected)
{
    size_t Len = wcslen(Expected);

    return Name->Length == Len * sizeof(WCHAR) && !_wcsnicmp(Name->Buffer, Expected, Len);
}

static void TestLogon(HANDLE Lsa, ULONG Package, SECURITY_LOGON_TYPE Type, PCWSTR User, PCWSTR Domain,
                      NTSTATUS Expected, PSID UserSid, PCWSTR Computer, TOKEN_TYPE ExpectedType,
                      SECURITY_IMPERSONATION_LEVEL ExpectedLevel)
{
    NTSTATUS Status, SubStatus;
    HANDLE Token;
    TOKEN_TYPE TokType = 0;
    SECURITY_IMPERSONATION_LEVEL Level = SecurityAnonymous;
    BYTE UserBuffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    PTOKEN_USER TokUser = (PTOKEN_USER)UserBuffer;
    TOKEN_STATISTICS Statistics;
    PSECURITY_LOGON_SESSION_DATA Session = NULL;
    DWORD Len;

    Status = DoS4U(Lsa, Package, Type, User, Domain, &SubStatus, &Token);
    ok(Status == Expected, "type %d user '%ls' domain '%ls': status 0x%08lx, expected 0x%08lx\n",
       (int)Type, User, Domain, Status, Expected);
    ok(SubStatus == 0, "type %d user '%ls' domain '%ls': substatus 0x%08lx\n", (int)Type, User, Domain, SubStatus);
    if (!NT_SUCCESS(Status))
    {
        ok(Token == NULL, "type %d user '%ls': token %p on failure\n", (int)Type, User, Token);
        return;
    }
    ok(Token != NULL, "type %d user '%ls': no token\n", (int)Type, User);
    if (!Token)
        return;
    ok(GetTokenInformation(Token, TokenType, &TokType, sizeof(TokType), &Len), "TokenType %lu\n", GetLastError());
    ok(TokType == ExpectedType, "type %d user '%ls': token type %d, expected %d\n",
       (int)Type, User, (int)TokType, (int)ExpectedType);
    if (TokType == TokenImpersonation)
    {
        ok(GetTokenInformation(Token, TokenImpersonationLevel, &Level, sizeof(Level), &Len),
           "TokenImpersonationLevel %lu\n", GetLastError());
        ok(Level == ExpectedLevel, "type %d user '%ls': level %d, expected %d\n",
           (int)Type, User, (int)Level, (int)ExpectedLevel);
    }
    ok(GetTokenInformation(Token, TokenUser, TokUser, sizeof(UserBuffer), &Len), "TokenUser %lu\n", GetLastError());
    ok(EqualSid(TokUser->User.Sid, UserSid), "type %d user '%ls': token user differs\n", (int)Type, User);
    ok(GetTokenInformation(Token, TokenStatistics, &Statistics, sizeof(Statistics), &Len),
       "TokenStatistics %lu\n", GetLastError());
    Status = LsaGetLogonSessionData(&Statistics.AuthenticationId, &Session);
    ok(Status == STATUS_SUCCESS, "type %d user '%ls': LsaGetLogonSessionData 0x%08lx\n", (int)Type, User, Status);
    if (NT_SUCCESS(Status))
    {
        ok(Session->LogonType == (ULONG)Type, "type %d user '%ls': session logon type %lu\n",
           (int)Type, User, Session->LogonType);
        ok(SameName(&Session->UserName, User), "type %d user '%ls': session user '%.*ls'\n",
           (int)Type, User, Session->UserName.Length / (int)sizeof(WCHAR), Session->UserName.Buffer);
        ok(SameName(&Session->LogonDomain, Computer), "type %d user '%ls': session domain '%.*ls'\n",
           (int)Type, User, Session->LogonDomain.Length / (int)sizeof(WCHAR), Session->LogonDomain.Buffer);
        ok(Session->Sid != NULL && EqualSid(Session->Sid, UserSid), "type %d user '%ls': session sid differs\n",
           (int)Type, User);
        LsaFreeReturnBuffer(Session);
    }
    CloseHandle(Token);
}

static void TestCaller(HANDLE Lsa, PCWSTR User, PCWSTR Computer, PSID Sid, BOOL Tcb)
{
    SECURITY_IMPERSONATION_LEVEL NetworkLevel = Tcb ? SecurityImpersonation : SecurityIdentification;
    TOKEN_TYPE BatchType = Tcb ? TokenPrimary : TokenImpersonation;
    WCHAR Upn[300];
    LSA_STRING Name;
    ULONG Package;
    NTSTATUS Status;

    Name.Buffer = MSV1_0_PACKAGE_NAME;
    Name.Length = Name.MaximumLength = (USHORT)strlen(MSV1_0_PACKAGE_NAME);
    Status = LsaLookupAuthenticationPackage(Lsa, &Name, &Package);
    ok(Status == STATUS_SUCCESS, "LsaLookupAuthenticationPackage 0x%08lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    _snwprintf(Upn, ARRAYSIZE(Upn) - 1, L"%ls@%ls", User, Computer);
    Upn[ARRAYSIZE(Upn) - 1] = UNICODE_NULL;

    TestLogon(Lsa, Package, Network, User, L".", STATUS_SUCCESS, Sid, Computer, TokenImpersonation, NetworkLevel);
    TestLogon(Lsa, Package, Network, User, Computer, STATUS_SUCCESS, Sid, Computer, TokenImpersonation, NetworkLevel);
    TestLogon(Lsa, Package, Batch, User, L".", STATUS_SUCCESS, Sid, Computer, BatchType, SecurityIdentification);
    TestLogon(Lsa, Package, Service, User, L".", STATUS_INVALID_PARAMETER, Sid, Computer, NO_TOKEN, 0);
    TestLogon(Lsa, Package, Interactive, User, L".", STATUS_INVALID_PARAMETER, Sid, Computer, NO_TOKEN, 0);
    TestLogon(Lsa, Package, NetworkCleartext, User, L".", STATUS_INVALID_PARAMETER, Sid, Computer, NO_TOKEN, 0);
    TestLogon(Lsa, Package, RemoteInteractive, User, L".", STATUS_INVALID_PARAMETER, Sid, Computer, NO_TOKEN, 0);
    TestLogon(Lsa, Package, Network, Upn, L"", STATUS_NOT_SUPPORTED, Sid, Computer, NO_TOKEN, 0);
    TestLogon(Lsa, Package, Network, User, L"", STATUS_NOT_SUPPORTED, Sid, Computer, NO_TOKEN, 0);
    TestLogon(Lsa, Package, Network, User, L"NOSUCHDOMAIN9", STATUS_NOT_SUPPORTED, Sid, Computer, NO_TOKEN, 0);
    TestLogon(Lsa, Package, Network, Upn, L".", STATUS_LOGON_FAILURE, Sid, Computer, NO_TOKEN, 0);
    TestLogon(Lsa, Package, Network, Upn, Computer, STATUS_LOGON_FAILURE, Sid, Computer, NO_TOKEN, 0);
    TestLogon(Lsa, Package, Network, L"NoSuchUser9271", L".", STATUS_LOGON_FAILURE, Sid, Computer, NO_TOKEN, 0);
    TestLogon(Lsa, Package, Network, L"", L".", STATUS_LOGON_FAILURE, Sid, Computer, NO_TOKEN, 0);
}

static BOOL HasTcbPrivilege(void)
{
    BYTE Buffer[4096];
    PTOKEN_PRIVILEGES Privileges = (PTOKEN_PRIVILEGES)Buffer;
    HANDLE Process;
    LUID Tcb;
    DWORD Len, i;
    BOOL Found = FALSE;

    if (!LookupPrivilegeValueW(NULL, SE_TCB_NAME, &Tcb) ||
        !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Process))
        return FALSE;
    if (GetTokenInformation(Process, TokenPrivileges, Privileges, sizeof(Buffer), &Len))
    {
        for (i = 0; i < Privileges->PrivilegeCount; i++)
        {
            if (Privileges->Privileges[i].Luid.LowPart == Tcb.LowPart &&
                Privileges->Privileges[i].Luid.HighPart == Tcb.HighPart)
                Found = TRUE;
        }
    }
    CloseHandle(Process);
    return Found;
}

static BOOL FindLocalUser(PWSTR User, DWORD UserSize, PSID Sid, DWORD SidSize, PCWSTR Computer, PSID Self)
{
    PUSER_INFO_1 Users = NULL;
    DWORD Read = 0, Total = 0, i, Len, SelfSidSize = SidSize;
    WCHAR Domain[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD DomainLen = ARRAYSIZE(Domain);
    SID_NAME_USE Use;
    BOOL Found = FALSE;

    Len = UserSize;
    if (GetUserNameW(User, &Len) &&
        LookupAccountNameW(NULL, User, Sid, &SelfSidSize, Domain, &DomainLen, &Use) &&
        Use == SidTypeUser && !_wcsicmp(Domain, Computer) && EqualSid(Sid, Self))
        return TRUE;

    if (NetUserEnum(NULL, 1, FILTER_NORMAL_ACCOUNT, (PBYTE *)&Users, MAX_PREFERRED_LENGTH, &Read, &Total, NULL) != NERR_Success)
        return FALSE;
    for (i = 0; i < Read && !Found; i++)
    {
        DWORD ThisSidSize = SidSize;

        DomainLen = ARRAYSIZE(Domain);
        if ((Users[i].usri1_flags & UF_ACCOUNTDISABLE) || wcslen(Users[i].usri1_name) >= UserSize)
            continue;
        if (!LookupAccountNameW(NULL, Users[i].usri1_name, Sid, &ThisSidSize, Domain, &DomainLen, &Use) ||
            Use != SidTypeUser)
            continue;
        wcscpy(User, Users[i].usri1_name);
        Found = TRUE;
    }
    NetApiBufferFree(Users);
    return Found;
}

START_TEST(S4ULogon)
{
    HANDLE Lsa, Process;
    WCHAR User[256], Computer[MAX_COMPUTERNAME_LENGTH + 1];
    BYTE SelfBuffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE], Sid[SECURITY_MAX_SID_SIZE];
    PTOKEN_USER Self = (PTOKEN_USER)SelfBuffer;
    LSA_STRING ProcessName;
    LSA_OPERATIONAL_MODE Mode;
    DWORD Len = ARRAYSIZE(Computer);
    BOOL Tcb;
    NTSTATUS Status;

    ok(GetComputerNameW(Computer, &Len), "GetComputerNameW %lu\n", GetLastError());
    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Process), "OpenProcessToken %lu\n", GetLastError());
    ok(GetTokenInformation(Process, TokenUser, Self, sizeof(SelfBuffer), &Len), "TokenUser %lu\n", GetLastError());
    CloseHandle(Process);
    if (!FindLocalUser(User, ARRAYSIZE(User), (PSID)Sid, sizeof(Sid), Computer, Self->User.Sid))
    {
        skip("No enabled local user account\n");
        return;
    }
    Tcb = HasTcbPrivilege();
    trace("S4U target '%ls', caller %s SeTcbPrivilege\n", User, Tcb ? "with" : "without");

    Status = LsaConnectUntrusted(&Lsa);
    ok(Status == STATUS_SUCCESS, "LsaConnectUntrusted 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        TestCaller(Lsa, User, Computer, (PSID)Sid, Tcb);
        LsaDeregisterLogonProcess(Lsa);
    }

    ProcessName.Buffer = "S4UTest";
    ProcessName.Length = ProcessName.MaximumLength = 7;
    Status = LsaRegisterLogonProcess(&ProcessName, &Lsa, &Mode);
    if (!Tcb)
    {
        ok(Status == STATUS_PORT_CONNECTION_REFUSED, "LsaRegisterLogonProcess 0x%08lx\n", Status);
        if (NT_SUCCESS(Status))
            LsaDeregisterLogonProcess(Lsa);
        return;
    }
    ok(Status == STATUS_SUCCESS, "LsaRegisterLogonProcess 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        TestCaller(Lsa, User, Computer, (PSID)Sid, Tcb);
        LsaDeregisterLogonProcess(Lsa);
    }
}
