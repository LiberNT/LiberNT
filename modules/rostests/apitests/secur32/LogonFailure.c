/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the statuses of failed MSV1_0 logons
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <ntsecapi.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

static void TestPassword(HANDLE Lsa, ULONG Package, SECURITY_LOGON_TYPE Type, PCWSTR User, PCWSTR Password)
{
    BYTE Buffer[sizeof(MSV1_0_INTERACTIVE_LOGON) + 512];
    PMSV1_0_INTERACTIVE_LOGON Info = (PMSV1_0_INTERACTIVE_LOGON)Buffer;
    PWSTR Strings = (PWSTR)(Info + 1);
    LSA_STRING Origin = { 7, 7, "LogTest" };
    TOKEN_SOURCE Source = {{'L','o','g','T','e','s','t',' '}, {0, 0}};
    USHORT UserLength = (USHORT)(wcslen(User) * sizeof(WCHAR));
    USHORT PasswordLength = (USHORT)(wcslen(Password) * sizeof(WCHAR));
    PVOID Profile = NULL;
    ULONG ProfileLength = 0;
    LUID LogonId;
    QUOTA_LIMITS Quotas;
    HANDLE Token = NULL;
    NTSTATUS Status, SubStatus = 0x0BADF00D;

    ZeroMemory(Buffer, sizeof(Buffer));
    Info->MessageType = MsV1_0InteractiveLogon;
    Strings[0] = L'.';
    Info->LogonDomainName.Length = Info->LogonDomainName.MaximumLength = sizeof(WCHAR);
    Info->LogonDomainName.Buffer = Strings;
    memcpy(Strings + 1, User, UserLength);
    Info->UserName.Length = Info->UserName.MaximumLength = UserLength;
    Info->UserName.Buffer = Strings + 1;
    memcpy((PBYTE)(Strings + 1) + UserLength, Password, PasswordLength);
    Info->Password.Length = Info->Password.MaximumLength = PasswordLength;
    Info->Password.Buffer = (PWSTR)((PBYTE)(Strings + 1) + UserLength);
    Status = LsaLogonUser(Lsa, &Origin, Type, Package, Info, sizeof(Buffer), NULL, &Source,
                          &Profile, &ProfileLength, &LogonId, &Token, &Quotas, &SubStatus);
    ok(Status == STATUS_LOGON_FAILURE, "type %d user '%ls': status 0x%08lx\n", (int)Type, User, Status);
    ok(SubStatus == STATUS_SUCCESS, "type %d user '%ls': substatus 0x%08lx\n", (int)Type, User, SubStatus);
    ok(Token == NULL, "type %d user '%ls': token %p\n", (int)Type, User, Token);
    if (Profile)
        LsaFreeReturnBuffer(Profile);
    if (Token)
        CloseHandle(Token);
}

static void TestChallenge(HANDLE Lsa, ULONG Package, MSV1_0_LOGON_SUBMIT_TYPE MessageType, PCWSTR User,
                          BOOL Workstation, NTSTATUS Expected)
{
    BYTE Buffer[sizeof(MSV1_0_LM20_LOGON) + 512];
    PMSV1_0_LM20_LOGON Info = (PMSV1_0_LM20_LOGON)Buffer;
    PBYTE Data = (PBYTE)(Info + 1);
    LSA_STRING Origin = { 7, 7, "LogTest" };
    TOKEN_SOURCE Source = {{'L','o','g','T','e','s','t',' '}, {0, 0}};
    USHORT UserLength = (USHORT)(wcslen(User) * sizeof(WCHAR));
    PVOID Profile = NULL;
    ULONG ProfileLength = 0;
    LUID LogonId;
    QUOTA_LIMITS Quotas;
    HANDLE Token = NULL;
    NTSTATUS Status, SubStatus = 0x0BADF00D;

    ZeroMemory(Buffer, sizeof(Buffer));
    Info->MessageType = MessageType;
    ((PWSTR)Data)[0] = L'.';
    Info->LogonDomainName.Length = Info->LogonDomainName.MaximumLength = sizeof(WCHAR);
    Info->LogonDomainName.Buffer = (PWSTR)Data;
    memcpy(Data + 2, User, UserLength);
    Info->UserName.Length = Info->UserName.MaximumLength = UserLength;
    Info->UserName.Buffer = (PWSTR)(Data + 2);
    if (Workstation)
    {
        ((PWSTR)(Data + 200))[0] = L'w';
        Info->Workstation.Length = Info->Workstation.MaximumLength = sizeof(WCHAR);
        Info->Workstation.Buffer = (PWSTR)(Data + 200);
    }
    memset(Data + 256, 0x41, 48);
    Info->CaseSensitiveChallengeResponse.Length = Info->CaseSensitiveChallengeResponse.MaximumLength = 24;
    Info->CaseSensitiveChallengeResponse.Buffer = (PCHAR)(Data + 256);
    Info->CaseInsensitiveChallengeResponse.Length = Info->CaseInsensitiveChallengeResponse.MaximumLength = 24;
    Info->CaseInsensitiveChallengeResponse.Buffer = (PCHAR)(Data + 280);
    Status = LsaLogonUser(Lsa, &Origin, Network, Package, Info, sizeof(Buffer), NULL, &Source,
                          &Profile, &ProfileLength, &LogonId, &Token, &Quotas, &SubStatus);
    ok(Status == Expected, "message %d user '%ls' workstation %d: status 0x%08lx, expected 0x%08lx\n",
       (int)MessageType, User, Workstation, Status, Expected);
    ok(SubStatus == STATUS_SUCCESS, "message %d user '%ls' workstation %d: substatus 0x%08lx\n",
       (int)MessageType, User, Workstation, SubStatus);
    ok(Token == NULL, "message %d user '%ls': token %p\n", (int)MessageType, User, Token);
    if (Profile)
        LsaFreeReturnBuffer(Profile);
    if (Token)
        CloseHandle(Token);
}

static BOOL HasTcbPrivilege(void)
{
    BYTE Buffer[4096];
    PTOKEN_PRIVILEGES Privileges = (PTOKEN_PRIVILEGES)Buffer;
    HANDLE Process;
    LUID Tcb;
    DWORD Length, i;
    BOOL Found = FALSE;

    if (!LookupPrivilegeValueW(NULL, SE_TCB_NAME, &Tcb) ||
        !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Process))
        return FALSE;
    if (GetTokenInformation(Process, TokenPrivileges, Privileges, sizeof(Buffer), &Length))
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

START_TEST(LogonFailure)
{
    static const MSV1_0_LOGON_SUBMIT_TYPE ChallengeTypes[] = { MsV1_0Lm20Logon, MsV1_0NetworkLogon };
    WCHAR User[256];
    DWORD Length = ARRAYSIZE(User);
    HANDLE Lsa;
    ULONG Package;
    LSA_STRING Name;
    NTSTATUS Status, Expected;
    BOOL Tcb;
    UINT i;

    if (!GetUserNameW(User, &Length) || !_wcsicmp(User, L"SYSTEM"))
        wcscpy(User, L"Administrator");
    Tcb = HasTcbPrivilege();
    trace("caller %s SeTcbPrivilege\n", Tcb ? "with" : "without");

    Status = LsaConnectUntrusted(&Lsa);
    ok(Status == STATUS_SUCCESS, "LsaConnectUntrusted 0x%08lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    Name.Buffer = MSV1_0_PACKAGE_NAME;
    Name.Length = Name.MaximumLength = (USHORT)strlen(MSV1_0_PACKAGE_NAME);
    Status = LsaLookupAuthenticationPackage(Lsa, &Name, &Package);
    ok(Status == STATUS_SUCCESS, "LsaLookupAuthenticationPackage 0x%08lx\n", Status);

    TestPassword(Lsa, Package, Interactive, L"NoSuchUser9271", L"x");
    TestPassword(Lsa, Package, Interactive, User, L"WrongPassword9271");
    TestPassword(Lsa, Package, Network, L"NoSuchUser9271", L"x");
    TestPassword(Lsa, Package, Network, User, L"WrongPassword9271");
    TestPassword(Lsa, Package, Batch, L"NoSuchUser9271", L"x");

    Expected = Tcb ? STATUS_LOGON_FAILURE : STATUS_ACCESS_DENIED;
    for (i = 0; i < ARRAYSIZE(ChallengeTypes); i++)
    {
        TestChallenge(Lsa, Package, ChallengeTypes[i], L"NoSuchUser9271", FALSE, STATUS_INVALID_PARAMETER);
        TestChallenge(Lsa, Package, ChallengeTypes[i], User, FALSE, STATUS_INVALID_PARAMETER);
        TestChallenge(Lsa, Package, ChallengeTypes[i], L"NoSuchUser9271", TRUE, Expected);
        TestChallenge(Lsa, Package, ChallengeTypes[i], User, TRUE, Expected);
    }

    LsaDeregisterLogonProcess(Lsa);
}
