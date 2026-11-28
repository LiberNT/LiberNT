/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for virtual account logons through LogonUserExExW and
 *              the sid-name mappings behind them
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "precomp.h"
#include <winbase.h>

#ifndef LOGON32_PROVIDER_VIRTUAL
#define LOGON32_PROVIDER_VIRTUAL 4
#endif

/* The mapping operation shapes, from the Windows SDK contract (the public
 * ntsecapi.h of the Windows SDK documents these for Windows 8 and later). */
typedef enum _LSA_SID_NAME_MAPPING_OPERATION_TYPE
{
    LsaSidNameMappingOperation_Add,
    LsaSidNameMappingOperation_Remove,
    LsaSidNameMappingOperation_AddMultiple,
} LSA_SID_NAME_MAPPING_OPERATION_TYPE;

typedef enum _LSA_SID_NAME_MAPPING_OPERATION_ERROR
{
    LsaSidNameMappingOperation_Success,
    LsaSidNameMappingOperation_NonMappingError,
    LsaSidNameMappingOperation_NameCollision,
    LsaSidNameMappingOperation_SidCollision,
    LsaSidNameMappingOperation_DomainNotFound,
    LsaSidNameMappingOperation_DomainSidPrefixMismatch,
    LsaSidNameMappingOperation_MappingNotFound,
} LSA_SID_NAME_MAPPING_OPERATION_ERROR;

typedef struct _LSA_SID_NAME_MAPPING_OPERATION_ADD_INPUT
{
    UNICODE_STRING DomainName;
    UNICODE_STRING AccountName;
    PSID Sid;
    ULONG Flags;
} LSA_SID_NAME_MAPPING_OPERATION_ADD_INPUT, *PLSA_SID_NAME_MAPPING_OPERATION_ADD_INPUT;

typedef struct _LSA_SID_NAME_MAPPING_OPERATION_REMOVE_INPUT
{
    UNICODE_STRING DomainName;
    UNICODE_STRING AccountName;
} LSA_SID_NAME_MAPPING_OPERATION_REMOVE_INPUT, *PLSA_SID_NAME_MAPPING_OPERATION_REMOVE_INPUT;

typedef union _LSA_SID_NAME_MAPPING_OPERATION_INPUT
{
    LSA_SID_NAME_MAPPING_OPERATION_ADD_INPUT AddInput;
    LSA_SID_NAME_MAPPING_OPERATION_REMOVE_INPUT RemoveInput;
} LSA_SID_NAME_MAPPING_OPERATION_INPUT, *PLSA_SID_NAME_MAPPING_OPERATION_INPUT;

typedef struct _LSA_SID_NAME_MAPPING_OPERATION_GENERIC_OUTPUT
{
    LSA_SID_NAME_MAPPING_OPERATION_ERROR ErrorCode;
} LSA_SID_NAME_MAPPING_OPERATION_GENERIC_OUTPUT, *PLSA_SID_NAME_MAPPING_OPERATION_GENERIC_OUTPUT;

typedef LSA_SID_NAME_MAPPING_OPERATION_GENERIC_OUTPUT LSA_SID_NAME_MAPPING_OPERATION_ADD_OUTPUT;
typedef LSA_SID_NAME_MAPPING_OPERATION_GENERIC_OUTPUT LSA_SID_NAME_MAPPING_OPERATION_REMOVE_OUTPUT;

typedef union _LSA_SID_NAME_MAPPING_OPERATION_OUTPUT
{
    LSA_SID_NAME_MAPPING_OPERATION_ADD_OUTPUT AddOutput;
    LSA_SID_NAME_MAPPING_OPERATION_REMOVE_OUTPUT RemoveOutput;
} LSA_SID_NAME_MAPPING_OPERATION_OUTPUT, *PLSA_SID_NAME_MAPPING_OPERATION_OUTPUT;

typedef NTSTATUS (WINAPI *PLSA_MANAGE_SID_NAME_MAPPING)(
    LSA_SID_NAME_MAPPING_OPERATION_TYPE OpType,
    PLSA_SID_NAME_MAPPING_OPERATION_INPUT OpInput,
    PLSA_SID_NAME_MAPPING_OPERATION_OUTPUT *OpOutput);

typedef NTSTATUS (WINAPI *PLSA_FREE_MEMORY)(PVOID);

typedef BOOL (WINAPI *PLOGON_USER_EXEXW)(
    LPWSTR, LPWSTR, LPWSTR, DWORD, DWORD, PTOKEN_GROUPS,
    PHANDLE, PSID*, PVOID*, LPDWORD, PQUOTA_LIMITS);

#define TEST_DOMAIN   L"NT SERVICE"
#define TEST_ACCOUNT  L"LntVirtTestAcct"
#define TEST_ACCOUNT2 L"LntVirtTestAcct2"

static PLSA_MANAGE_SID_NAME_MAPPING pLsaManageSidNameMapping;
static PLSA_FREE_MEMORY pLsaFreeMemory;
static PLOGON_USER_EXEXW pLogonUserExExW;

/* The test service SID shape: NT SERVICE accounts hash their name into S-1-5-80-*.
 * For mapping registration any well-formed account SID serves; this one is the
 * shape a real service SID takes. */
static PSID
MakeTestSid(DWORD Rid4)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = { SECURITY_NT_AUTHORITY };
    PSID Sid = NULL;

    AllocateAndInitializeSid(&NtAuthority, 5, SECURITY_SERVICE_ID_BASE_RID,
                             0x11111111, 0x22222222, 0x33333333, Rid4,
                             0, 0, 0, &Sid);
    return Sid;
}

static void
FreeOutput(PLSA_SID_NAME_MAPPING_OPERATION_OUTPUT Output)
{
    if (Output != NULL && pLsaFreeMemory != NULL)
        pLsaFreeMemory(Output);
}

static void
RemoveMapping(PUNICODE_STRING DomainName, PUNICODE_STRING AccountName)
{
    LSA_SID_NAME_MAPPING_OPERATION_INPUT Input;
    PLSA_SID_NAME_MAPPING_OPERATION_OUTPUT Output = NULL;

    ZeroMemory(&Input, sizeof(Input));
    Input.RemoveInput.DomainName = *DomainName;
    Input.RemoveInput.AccountName = *AccountName;
    pLsaManageSidNameMapping(LsaSidNameMappingOperation_Remove, &Input, &Output);
    FreeOutput(Output);
}

static NTSTATUS
Manage(LSA_SID_NAME_MAPPING_OPERATION_TYPE Type, PLSA_SID_NAME_MAPPING_OPERATION_INPUT Input, int *Error)
{
    PLSA_SID_NAME_MAPPING_OPERATION_OUTPUT Output = NULL;
    NTSTATUS Status;

    Status = pLsaManageSidNameMapping(Type, Input, &Output);
    *Error = Output ? (int)Output->AddOutput.ErrorCode : -1;
    FreeOutput(Output);
    return Status;
}

static void
SetAddInput(PLSA_SID_NAME_MAPPING_OPERATION_INPUT Input, PUNICODE_STRING Domain, PUNICODE_STRING Account, PSID Sid)
{
    ZeroMemory(Input, sizeof(*Input));
    Input->AddInput.DomainName = *Domain;
    Input->AddInput.AccountName = *Account;
    Input->AddInput.Sid = Sid;
}

static void
SetRemoveInput(PLSA_SID_NAME_MAPPING_OPERATION_INPUT Input, PUNICODE_STRING Domain, PUNICODE_STRING Account)
{
    ZeroMemory(Input, sizeof(*Input));
    Input->RemoveInput.DomainName = *Domain;
    Input->RemoveInput.AccountName = *Account;
}

static BOOL
SetTcbPrivilege(BOOL Enable, BOOL *WasEnabled)
{
    TOKEN_PRIVILEGES New, Old;
    DWORD Len = sizeof(Old);
    HANDLE Token;
    BOOL Ret;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &Token))
        return FALSE;
    New.PrivilegeCount = 1;
    New.Privileges[0].Attributes = Enable ? SE_PRIVILEGE_ENABLED : 0;
    Ret = LookupPrivilegeValueW(NULL, SE_TCB_NAME, &New.Privileges[0].Luid) &&
          AdjustTokenPrivileges(Token, FALSE, &New, sizeof(Old), &Old, &Len) &&
          GetLastError() == ERROR_SUCCESS;
    if (WasEnabled)
        *WasEnabled = Ret && Old.PrivilegeCount == 1 && (Old.Privileges[0].Attributes & SE_PRIVILEGE_ENABLED);
    CloseHandle(Token);
    return Ret;
}

static
void
TestMappingWithoutTcb(PUNICODE_STRING DomainName, PUNICODE_STRING AccountName, PSID Sid)
{
    LSA_SID_NAME_MAPPING_OPERATION_INPUT Input;
    NTSTATUS Status;
    int Error;

    SetRemoveInput(&Input, DomainName, AccountName);
    Status = Manage(LsaSidNameMappingOperation_Remove, &Input, &Error);
    ok(Status == STATUS_PRIVILEGE_NOT_HELD, "Remove without SeTcbPrivilege: status 0x%08lx\n", Status);
    ok(Error == LsaSidNameMappingOperation_NonMappingError, "Remove without SeTcbPrivilege: error %d\n", Error);

    SetAddInput(&Input, DomainName, AccountName, Sid);
    Status = Manage(LsaSidNameMappingOperation_Add, &Input, &Error);
    ok(Status == STATUS_PRIVILEGE_NOT_HELD, "Add without SeTcbPrivilege: status 0x%08lx\n", Status);
    ok(Error == LsaSidNameMappingOperation_NonMappingError, "Add without SeTcbPrivilege: error %d\n", Error);
}

static
void
TestVirtualLogon(PCWSTR Account, PCWSTR Domain, DWORD ExpectedError)
{
    WCHAR AccountName[64], DomainName[64];
    HANDLE Token = NULL;
    DWORD Error;
    BOOL Result;

    if (pLogonUserExExW == NULL)
    {
        skip("LogonUserExExW not available\n");
        return;
    }

    wcscpy(AccountName, Account);
    wcscpy(DomainName, Domain);
    SetLastError(0xdeadbeef);
    Result = pLogonUserExExW(AccountName, DomainName, (LPWSTR)L"",
                             LOGON32_LOGON_SERVICE, LOGON32_PROVIDER_VIRTUAL,
                             NULL, &Token, NULL, NULL, NULL, NULL);
    Error = GetLastError();
    ok(!Result, "Virtual logon of %ls\\%ls unexpectedly succeeded\n", Domain, Account);
    ok(Error == ExpectedError, "Virtual logon of %ls\\%ls: error %lu, expected %lu\n",
       Domain, Account, Error, ExpectedError);
    if (Token != NULL)
        CloseHandle(Token);
}

static
void
TestMappingRoundtrip(VOID)
{
    LSA_SID_NAME_MAPPING_OPERATION_INPUT Input;
    PLSA_SID_NAME_MAPPING_OPERATION_OUTPUT Output;
    UNICODE_STRING DomainName, AccountName, AccountName2, OtherDomain;
    NTSTATUS Status;
    PSID Sid, Sid2;
    int Error;

    Sid = MakeTestSid(0x44444444);
    Sid2 = MakeTestSid(0x55555555);
    ok(Sid != NULL && Sid2 != NULL, "AllocateAndInitializeSid failed\n");
    if (Sid == NULL || Sid2 == NULL)
        return;

    RtlInitUnicodeString(&DomainName, TEST_DOMAIN);
    RtlInitUnicodeString(&AccountName, TEST_ACCOUNT);
    RtlInitUnicodeString(&AccountName2, TEST_ACCOUNT2);
    RtlInitUnicodeString(&OtherDomain, L"LntNoSuchDomain");

    if (!SetTcbPrivilege(TRUE, NULL))
    {
        trace("Caller does not hold SeTcbPrivilege\n");
        TestMappingWithoutTcb(&DomainName, &AccountName, Sid);
        TestVirtualLogon(TEST_ACCOUNT, TEST_DOMAIN, ERROR_ACCESS_DENIED);
        FreeSid(Sid);
        FreeSid(Sid2);
        return;
    }

    RemoveMapping(&DomainName, &AccountName);
    RemoveMapping(&DomainName, &AccountName2);

    SetRemoveInput(&Input, &DomainName, &AccountName);
    Status = Manage(LsaSidNameMappingOperation_Remove, &Input, &Error);
    ok(Status == STATUS_NOT_FOUND, "Absent remove: status 0x%08lx\n", Status);
    ok(Error == LsaSidNameMappingOperation_MappingNotFound, "Absent remove: error %d\n", Error);

    TestVirtualLogon(TEST_ACCOUNT, TEST_DOMAIN, ERROR_NONE_MAPPED);

    SetAddInput(&Input, &DomainName, &AccountName, Sid);
    Status = Manage(LsaSidNameMappingOperation_Add, &Input, &Error);
    ok(Status == STATUS_SUCCESS, "Add: status 0x%08lx\n", Status);
    ok(Error == LsaSidNameMappingOperation_Success, "Add: error %d\n", Error);

    Status = Manage(LsaSidNameMappingOperation_Add, &Input, &Error);
    ok(Status == STATUS_INVALID_PARAMETER, "Re-add: status 0x%08lx\n", Status);
    ok(Error == LsaSidNameMappingOperation_SidCollision, "Re-add: error %d\n", Error);

    SetAddInput(&Input, &DomainName, &AccountName, Sid2);
    Status = Manage(LsaSidNameMappingOperation_Add, &Input, &Error);
    ok(Status == STATUS_INVALID_PARAMETER, "Name collision: status 0x%08lx\n", Status);
    ok(Error == LsaSidNameMappingOperation_NameCollision, "Name collision: error %d\n", Error);

    SetAddInput(&Input, &DomainName, &AccountName2, Sid);
    Status = Manage(LsaSidNameMappingOperation_Add, &Input, &Error);
    ok(Status == STATUS_INVALID_PARAMETER, "SID collision: status 0x%08lx\n", Status);
    ok(Error == LsaSidNameMappingOperation_SidCollision, "SID collision: error %d\n", Error);

    SetAddInput(&Input, &OtherDomain, &AccountName2, Sid2);
    Status = Manage(LsaSidNameMappingOperation_Add, &Input, &Error);
    ok(Status == STATUS_INVALID_PARAMETER, "Unknown domain: status 0x%08lx\n", Status);
    ok(Error == LsaSidNameMappingOperation_DomainNotFound, "Unknown domain: error %d\n", Error);

    SetAddInput(&Input, &DomainName, &AccountName2, NULL);
    Status = Manage(LsaSidNameMappingOperation_Add, &Input, &Error);
    ok(Status == RPC_NT_NULL_REF_POINTER, "NULL SID: status 0x%08lx\n", Status);
    ok(Error == LsaSidNameMappingOperation_NonMappingError, "NULL SID: error %d\n", Error);

    Output = (PVOID)0xdeadbeef;
    Status = pLsaManageSidNameMapping((LSA_SID_NAME_MAPPING_OPERATION_TYPE)7, &Input, &Output);
    ok(Status == STATUS_INVALID_PARAMETER, "Bad operation: status 0x%08lx\n", Status);
    ok(Output == (PVOID)0xdeadbeef, "Bad operation: output %p\n", Output);

    TestVirtualLogon(TEST_ACCOUNT, TEST_DOMAIN, ERROR_ACCESS_DENIED);

    if (SetTcbPrivilege(FALSE, NULL))
    {
        TestMappingWithoutTcb(&DomainName, &AccountName2, Sid2);
        ok(SetTcbPrivilege(TRUE, NULL), "Re-enabling SeTcbPrivilege failed\n");
    }

    SetRemoveInput(&Input, &DomainName, &AccountName);
    Status = Manage(LsaSidNameMappingOperation_Remove, &Input, &Error);
    ok(Status == STATUS_SUCCESS, "Remove: status 0x%08lx\n", Status);
    ok(Error == LsaSidNameMappingOperation_Success, "Remove: error %d\n", Error);

    Status = Manage(LsaSidNameMappingOperation_Remove, &Input, &Error);
    ok(Status == STATUS_NOT_FOUND, "Second remove: status 0x%08lx\n", Status);
    ok(Error == LsaSidNameMappingOperation_MappingNotFound, "Second remove: error %d\n", Error);

    TestVirtualLogon(TEST_ACCOUNT, TEST_DOMAIN, ERROR_NONE_MAPPED);

    FreeSid(Sid);
    FreeSid(Sid2);
}

START_TEST(LogonUserVirtual)
{
    HMODULE Advapi;

    Advapi = GetModuleHandleW(L"advapi32.dll");
    if (Advapi != NULL)
    {
        pLsaManageSidNameMapping = (PLSA_MANAGE_SID_NAME_MAPPING)
            GetProcAddress(Advapi, "LsaManageSidNameMapping");
        pLsaFreeMemory = (PLSA_FREE_MEMORY)GetProcAddress(Advapi, "LsaFreeMemory");
        pLogonUserExExW = (PLOGON_USER_EXEXW)GetProcAddress(Advapi, "LogonUserExExW");
    }

    if (pLsaManageSidNameMapping == NULL)
        skip("LsaManageSidNameMapping not available\n");
    else
        TestMappingRoundtrip();
}
