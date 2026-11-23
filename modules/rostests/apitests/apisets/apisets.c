/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Validate the apiset lookup in ApiSetResolveToHost
 * COPYRIGHT:   Copyright 2024 Mark Jansen <mark.jansen@reactos.org>
 */

#include <ndk/umtypes.h>
#include <ndk/rtlfuncs.h>
#include <apitest.h>
#include "apisetsp.h"

static void
resolve_single(PCUNICODE_STRING Apiset)
{
    UNICODE_STRING Tmp = {0};
    NTSTATUS Status = RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE, Apiset, &Tmp);
    ok_ntstatus(Status, STATUS_SUCCESS);

    BOOLEAN Resolved = FALSE;
    UNICODE_STRING Result = {0};

    Status = ApiSetResolveToHost(~0u, &Tmp, &Resolved, &Result);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_eq_bool(Resolved, TRUE);
    ok(Result.Buffer != NULL, "Got NULL\n");
}

static void
resolve_fail_single(PCUNICODE_STRING Apiset)
{
    UNICODE_STRING Tmp = {0};
    NTSTATUS Status = RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE, Apiset, &Tmp);
    ok_ntstatus(Status, STATUS_SUCCESS);

    BOOLEAN Resolved = FALSE;
    UNICODE_STRING Result = {0, 0, (PWSTR)0xbadbeef};

    Status = ApiSetResolveToHost(~0u, &Tmp, &Resolved, &Result);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_eq_bool(Resolved, FALSE);
    ok_ptr(Result.Buffer, (PWSTR)0xbadbeef);
}

static void
test_single(PCUNICODE_STRING Apiset)
{
    winetest_push_context("%S", Apiset->Buffer);
    resolve_single(Apiset);
    winetest_pop_context();
}

static void
fail_single(PCUNICODE_STRING Apiset)
{
    winetest_push_context("%S", Apiset->Buffer);
    resolve_fail_single(Apiset);
    winetest_pop_context();
}


static void
test_contract_host(PCUNICODE_STRING Contract, PCUNICODE_STRING ExpectedHost)
{
    UNICODE_STRING Host = {0};
    BOOLEAN Resolved = FALSE;
    NTSTATUS Status;

    Status = ApiSetResolveToHost(APISET_WIN10, Contract, &Resolved, &Host);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Resolved, "%S did not resolve\n", Contract->Buffer);
    ok(Resolved && RtlEqualUnicodeString(&Host, ExpectedHost, TRUE),
       "%S did not resolve to %S\n", Contract->Buffer, ExpectedHost->Buffer);
}

static void
test_contract_presence(PCUNICODE_STRING Contract)
{
    BOOLEAN InSchema = FALSE, Present = FALSE;
    NTSTATUS Status;

    Status = ApiSetQuerySchemaPresence(APISET_WIN10, Contract, &InSchema, &Present);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(InSchema && Present, "%S is not present in the API set schema\n", Contract->Buffer);
}

START_TEST(apisets)
{
    // Ensure we can find some manually selected ones (both with and without extension):
    UNICODE_STRING Console1 = RTL_CONSTANT_STRING(L"api-MS-Win-Core-Console-L1-1-0.dll");
    UNICODE_STRING Console2 = RTL_CONSTANT_STRING(L"api-MS-Win-Core-Console-L1-1-0");
    UNICODE_STRING Handle1 = RTL_CONSTANT_STRING(L"api-MS-Win-Core-Handle-L1-1-0.dll");
    UNICODE_STRING Handle2 = RTL_CONSTANT_STRING(L"api-MS-Win-Core-Handle-L1-1-0");
    UNICODE_STRING Heap1 = RTL_CONSTANT_STRING(L"api-MS-Win-Core-Heap-L1-1-0.dll");
    UNICODE_STRING Heap2 = RTL_CONSTANT_STRING(L"api-MS-Win-Core-Heap-L1-1-0");
    UNICODE_STRING StringAnsi = RTL_CONSTANT_STRING(L"api-ms-win-core-stringansi-l1-1-0.dll");
    UNICODE_STRING NamedPipe = RTL_CONSTANT_STRING(L"api-ms-win-core-namedpipe-l1-1-0.dll");
    UNICODE_STRING ServiceLegacy = RTL_CONSTANT_STRING(L"api-ms-win-service-legacy-l1-1-0.dll");
    UNICODE_STRING KernelBase = RTL_CONSTANT_STRING(L"kernelbase.dll");
    UNICODE_STRING Advapi32 = RTL_CONSTANT_STRING(L"advapi32.dll");
    UNICODE_STRING Shcore = RTL_CONSTANT_STRING(L"shcore.dll");
    UNICODE_STRING MemoryNewest = RTL_CONSTANT_STRING(L"api-ms-win-core-memory-l1-1-9");
    UNICODE_STRING SysinfoNewest = RTL_CONSTANT_STRING(L"api-ms-win-core-sysinfo-l1-2-8");
    UNICODE_STRING KernelContracts[] = {
        RTL_CONSTANT_STRING(L"api-ms-win-core-memory-l1-1-0.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-memory-l1-1-1.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-memory-l1-1-2.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-memory-l1-1-7.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-sysinfo-l1-2-0.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-sysinfo-l1-2-1.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-sysinfo-l1-2-6.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-wow64-l1-1-0.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-wow64-l1-1-3.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-security-base-l1-1-0.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-security-base-l1-1-1.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-memory-l1-1-4.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-memory-l1-1-9.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-sysinfo-l1-2-3.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-sysinfo-l1-2-8.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-core-wow64-l1-1-1.dll"),
    };
    UNICODE_STRING ShcoreContracts[] = {
        RTL_CONSTANT_STRING(L"api-ms-win-shcore-registry-l1-1-0.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-shcore-registry-l1-1-1.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-shcore-sysinfo-l1-1-0.dll"),
        RTL_CONSTANT_STRING(L"api-ms-win-shcore-unicodeansi-l1-1-0.dll"),
    };

    test_contract_host(&StringAnsi, &KernelBase);
    test_contract_host(&NamedPipe, &KernelBase);
    test_contract_host(&ServiceLegacy, &Advapi32);
    test_contract_presence(&MemoryNewest);
    test_contract_presence(&SysinfoNewest);
    for (DWORD n = 0; n < RTL_NUMBER_OF(KernelContracts); n++)
        test_contract_host(&KernelContracts[n], &KernelBase);
    for (DWORD n = 0; n < RTL_NUMBER_OF(ShcoreContracts); n++)
        test_contract_host(&ShcoreContracts[n], &Shcore);

    test_single(&Console1);
    test_single(&Console2);
    test_single(&Handle1);
    test_single(&Handle2);
    test_single(&Heap1);
    test_single(&Heap2);

    // Walk the entire table, to ensure we can find each entry
    for (DWORD n = 0; n < g_ApisetsCount; ++n)
    {
        PCUNICODE_STRING Apiset = &g_Apisets[n].Name;
        winetest_push_context("%ld: %S", n, Apiset->Buffer);
        resolve_single(Apiset);
        winetest_pop_context();
    }

    UNICODE_STRING Fail1 = RTL_CONSTANT_STRING(L"");
    UNICODE_STRING Fail2 = RTL_CONSTANT_STRING(L"api-");
    UNICODE_STRING Fail3 = RTL_CONSTANT_STRING(L"ext");

    fail_single(&Fail1);
    fail_single(&Fail2);
    fail_single(&Fail3);
}
