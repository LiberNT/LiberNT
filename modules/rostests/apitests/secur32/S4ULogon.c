/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for MSV1_0 S4U logons by an untrusted caller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <ntsecapi.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <stdio.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

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

static void TestLogon(HANDLE Lsa, ULONG Package, SECURITY_LOGON_TYPE Type, PCWSTR User, PCWSTR Domain,
                      NTSTATUS Expected, PSID UserSid)
{
    NTSTATUS Status, SubStatus;
    HANDLE Token;
    TOKEN_TYPE TokType = 0;
    SECURITY_IMPERSONATION_LEVEL Level = SecurityAnonymous;
    BYTE UserBuffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    PTOKEN_USER TokUser = (PTOKEN_USER)UserBuffer;
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
    ok(TokType == TokenImpersonation, "type %d user '%ls': token type %d\n", (int)Type, User, (int)TokType);
    ok(GetTokenInformation(Token, TokenImpersonationLevel, &Level, sizeof(Level), &Len),
       "TokenImpersonationLevel %lu\n", GetLastError());
    ok(Level == SecurityIdentification, "type %d user '%ls': level %d\n", (int)Type, User, (int)Level);
    ok(GetTokenInformation(Token, TokenUser, TokUser, sizeof(UserBuffer), &Len), "TokenUser %lu\n", GetLastError());
    ok(EqualSid(TokUser->User.Sid, UserSid), "type %d user '%ls': token user differs\n", (int)Type, User);
    CloseHandle(Token);
}

START_TEST(S4ULogon)
{
    HANDLE Lsa, Process;
    ULONG Package;
    LSA_STRING Name;
    WCHAR User[256], Computer[MAX_COMPUTERNAME_LENGTH + 1], Upn[300];
    BYTE UserBuffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    PTOKEN_USER Self = (PTOKEN_USER)UserBuffer;
    DWORD Len = ARRAYSIZE(User);
    NTSTATUS Status;

    ok(GetUserNameW(User, &Len), "GetUserNameW %lu\n", GetLastError());
    Len = ARRAYSIZE(Computer);
    ok(GetComputerNameW(Computer, &Len), "GetComputerNameW %lu\n", GetLastError());
    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Process), "OpenProcessToken %lu\n", GetLastError());
    ok(GetTokenInformation(Process, TokenUser, Self, sizeof(UserBuffer), &Len), "TokenUser %lu\n", GetLastError());
    CloseHandle(Process);

    Status = LsaConnectUntrusted(&Lsa);
    ok(Status == STATUS_SUCCESS, "LsaConnectUntrusted 0x%08lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    Name.Buffer = MSV1_0_PACKAGE_NAME;
    Name.Length = Name.MaximumLength = (USHORT)strlen(MSV1_0_PACKAGE_NAME);
    Status = LsaLookupAuthenticationPackage(Lsa, &Name, &Package);
    ok(Status == STATUS_SUCCESS, "LsaLookupAuthenticationPackage 0x%08lx\n", Status);
    swprintf(Upn, L"%ls@%ls", User, Computer);

    TestLogon(Lsa, Package, Network, User, L".", STATUS_SUCCESS, Self->User.Sid);
    TestLogon(Lsa, Package, Network, User, Computer, STATUS_SUCCESS, Self->User.Sid);
    TestLogon(Lsa, Package, Batch, User, L".", STATUS_SUCCESS, Self->User.Sid);
    TestLogon(Lsa, Package, Network, Upn, L"", STATUS_NOT_SUPPORTED, Self->User.Sid);
    TestLogon(Lsa, Package, Interactive, User, L".", STATUS_INVALID_PARAMETER, Self->User.Sid);
    TestLogon(Lsa, Package, Network, L"NoSuchUser9271", L".", STATUS_LOGON_FAILURE, Self->User.Sid);
    TestLogon(Lsa, Package, Network, L"", L".", STATUS_LOGON_FAILURE, Self->User.Sid);
    LsaDeregisterLogonProcess(Lsa);
}
