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

static
void
TestMappingRoundtrip(VOID)
{
    LSA_SID_NAME_MAPPING_OPERATION_INPUT Input;
    PLSA_SID_NAME_MAPPING_OPERATION_OUTPUT Output;
    UNICODE_STRING DomainName, AccountName, AccountName2;
    NTSTATUS Status;
    PSID Sid, Sid2;

    Sid = MakeTestSid(0x44444444);
    Sid2 = MakeTestSid(0x55555555);
    ok(Sid != NULL && Sid2 != NULL, "AllocateAndInitializeSid failed\n");
    if (Sid == NULL || Sid2 == NULL)
        return;

    RtlInitUnicodeString(&DomainName, TEST_DOMAIN);
    RtlInitUnicodeString(&AccountName, TEST_ACCOUNT);
    RtlInitUnicodeString(&AccountName2, TEST_ACCOUNT2);

    /* Clean leftovers from earlier runs */
    RemoveMapping(&DomainName, &AccountName);
    RemoveMapping(&DomainName, &AccountName2);

    /* Removing an absent mapping fails and reports MappingNotFound */
    Output = NULL;
    ZeroMemory(&Input, sizeof(Input));
    Input.RemoveInput.DomainName = DomainName;
    Input.RemoveInput.AccountName = AccountName;
    Status = pLsaManageSidNameMapping(LsaSidNameMappingOperation_Remove, &Input, &Output);
    if (Status == STATUS_ACCESS_DENIED)
    {
        skip("Mapping operations need administrator rights\n");
        FreeOutput(Output);
        FreeSid(Sid);
        FreeSid(Sid2);
        return;
    }
    ok(!NT_SUCCESS(Status), "Absent remove unexpectedly succeeded (0x%08lx)\n", Status);
    ok(Output != NULL, "Absent remove produced no output\n");
    if (Output != NULL)
        ok(Output->RemoveOutput.ErrorCode == LsaSidNameMappingOperation_MappingNotFound,
           "Absent remove error %d, expected MappingNotFound\n", Output->RemoveOutput.ErrorCode);
    FreeOutput(Output);

    /* Add the mapping: succeeds and reports Success */
    Output = NULL;
    ZeroMemory(&Input, sizeof(Input));
    Input.AddInput.DomainName = DomainName;
    Input.AddInput.AccountName = AccountName;
    Input.AddInput.Sid = Sid;
    Input.AddInput.Flags = 0;
    Status = pLsaManageSidNameMapping(LsaSidNameMappingOperation_Add, &Input, &Output);
    if (Status == STATUS_ACCESS_DENIED)
    {
        skip("Mapping operations need administrator rights\n");
        FreeOutput(Output);
        FreeSid(Sid);
        FreeSid(Sid2);
        return;
    }
    ok(NT_SUCCESS(Status), "Add failed with status 0x%08lx\n", Status);
    ok(Output != NULL, "Add produced no output\n");
    if (Output != NULL)
        ok(Output->AddOutput.ErrorCode == LsaSidNameMappingOperation_Success,
           "Add error %d, expected Success\n", Output->AddOutput.ErrorCode);
    FreeOutput(Output);
    if (!NT_SUCCESS(Status))
    {
        skip("Cannot exercise the store\n");
        FreeSid(Sid);
        FreeSid(Sid2);
        return;
    }

    /* Re-adding the exact same mapping is idempotent */
    Output = NULL;
    Status = pLsaManageSidNameMapping(LsaSidNameMappingOperation_Add, &Input, &Output);
    ok(NT_SUCCESS(Status), "Idempotent re-add failed with 0x%08lx\n", Status);
    ok(Output != NULL && Output->AddOutput.ErrorCode == LsaSidNameMappingOperation_Success,
       "Re-add error %d, expected Success\n",
       Output ? (int)Output->AddOutput.ErrorCode : -1);
    FreeOutput(Output);

    /* Same name with a different SID: name collision, store unchanged */
    Output = NULL;
    Input.AddInput.Sid = Sid2;
    Status = pLsaManageSidNameMapping(LsaSidNameMappingOperation_Add, &Input, &Output);
    ok(!NT_SUCCESS(Status), "Name collision unexpectedly succeeded\n");
    ok(Output != NULL, "Name collision produced no output\n");
    if (Output != NULL)
        ok(Output->AddOutput.ErrorCode == LsaSidNameMappingOperation_NameCollision,
           "Collision error %d, expected NameCollision\n", Output->AddOutput.ErrorCode);
    FreeOutput(Output);

    /* Same SID under a different name: SID collision */
    Output = NULL;
    Input.AddInput.AccountName = AccountName2;
    Input.AddInput.Sid = Sid;
    Status = pLsaManageSidNameMapping(LsaSidNameMappingOperation_Add, &Input, &Output);
    ok(!NT_SUCCESS(Status), "SID collision unexpectedly succeeded\n");
    ok(Output != NULL, "SID collision produced no output\n");
    if (Output != NULL)
        ok(Output->AddOutput.ErrorCode == LsaSidNameMappingOperation_SidCollision,
           "Collision error %d, expected SidCollision\n", Output->AddOutput.ErrorCode);
    FreeOutput(Output);

    /* The original mapping survived the collisions: removing it succeeds */
    Output = NULL;
    ZeroMemory(&Input, sizeof(Input));
    Input.RemoveInput.DomainName = DomainName;
    Input.RemoveInput.AccountName = AccountName;
    Status = pLsaManageSidNameMapping(LsaSidNameMappingOperation_Remove, &Input, &Output);
    ok(NT_SUCCESS(Status), "Remove failed with status 0x%08lx\n", Status);
    ok(Output != NULL && Output->RemoveOutput.ErrorCode == LsaSidNameMappingOperation_Success,
       "Remove error %d, expected Success\n",
       Output ? (int)Output->RemoveOutput.ErrorCode : -1);
    FreeOutput(Output);

    /* And now it is gone */
    Output = NULL;
    Status = pLsaManageSidNameMapping(LsaSidNameMappingOperation_Remove, &Input, &Output);
    ok(!NT_SUCCESS(Status), "Second remove unexpectedly succeeded\n");
    FreeOutput(Output);

    FreeSid(Sid);
    FreeSid(Sid2);
}

static
void
TestVirtualLogon(VOID)
{
    WCHAR AccountName[] = TEST_ACCOUNT;
    WCHAR DomainName[] = TEST_DOMAIN;
    HANDLE Token = NULL;
    BOOL Result;
    DWORD Error;

    if (pLogonUserExExW == NULL)
    {
        skip("LogonUserExExW not available\n");
        return;
    }

    /* Logging on an unmapped virtual account fails */
    SetLastError(0xdeadbeef);
    Result = pLogonUserExExW(AccountName, DomainName, (LPWSTR)L"",
                             LOGON32_LOGON_SERVICE, LOGON32_PROVIDER_VIRTUAL,
                             NULL, &Token, NULL, NULL, NULL, NULL);
    Error = GetLastError();
    ok(!Result, "Unmapped virtual logon unexpectedly succeeded\n");
    trace("unmapped logon error: %lu\n", Error);
    ok(Error != 0xdeadbeef, "Error was not set\n");
    if (Token != NULL)
        CloseHandle(Token);
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

    TestVirtualLogon();
}
