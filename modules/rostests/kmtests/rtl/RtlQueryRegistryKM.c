/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the default value rules of RtlQueryRegistryValues and RtlQueryRegistryValuesEx in kernel mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TEST_KEY L"\\Registry\\Machine\\Software\\RtlQueryRegistryKM_kmtest"
#define ENTRY_CONTEXT ((PVOID)0x1234)

typedef NTSTATUS (NTAPI *PFN_QUERY_REGISTRY_VALUES)(ULONG, PCWSTR, PRTL_QUERY_REGISTRY_TABLE, PVOID, PVOID);

static PFN_QUERY_REGISTRY_VALUES Query;
static ULONG Calls, LastType, LastLength;
static PVOID LastData;
static NTSTATUS RoutineResult;
static ULONG Zero16[4];
static ULONG DefaultDword = 0x55;
static WCHAR DefaultString[] = L"dflt";

static
NTSTATUS
NTAPI
Routine(
    _In_ PWSTR ValueName,
    _In_ ULONG ValueType,
    _In_ PVOID ValueData,
    _In_ ULONG ValueLength,
    _In_ PVOID Context,
    _In_ PVOID EntryContext)
{
    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);

    ok(EntryContext == ENTRY_CONTEXT, "EntryContext %p\n", EntryContext);
    Calls++;
    LastType = ValueType;
    LastLength = ValueLength;
    LastData = ValueData;
    return RoutineResult;
}

static
NTSTATUS
QueryOne(
    _In_ ULONG Flags,
    _In_ PCWSTR Name,
    _In_ ULONG DefaultType,
    _In_opt_ PVOID DefaultData,
    _In_ ULONG DefaultLength,
    _In_opt_ PVOID Direct)
{
    RTL_QUERY_REGISTRY_TABLE Table[2];

    RtlZeroMemory(Table, sizeof(Table));
    Table[0].QueryRoutine = Direct ? NULL : Routine;
    Table[0].Flags = Flags;
    Table[0].Name = (PWSTR)Name;
    Table[0].EntryContext = Direct ? Direct : ENTRY_CONTEXT;
    Table[0].DefaultType = DefaultType;
    Table[0].DefaultData = DefaultData;
    Table[0].DefaultLength = DefaultLength;
    Calls = 0;
    LastType = LastLength = 0xFFFFFFFF;
    LastData = NULL;
    return Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY, Table, NULL, NULL);
}

static
VOID
TestDefaults(VOID)
{
    ULONG Direct[2];
    NTSTATUS Status;

    RoutineResult = STATUS_SUCCESS;
    Status = QueryOne(RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND, L"Missing",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, Zero16, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Calls, 1UL);
    ok_eq_ulong(LastType, (ULONG)REG_DWORD);
    ok_eq_ulong(LastLength, 0UL);
    ok_eq_pointer(LastData, Zero16);

    Status = QueryOne(RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", REG_NONE, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    ok_eq_ulong(Calls, 0UL);

    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Missing",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE, Zero16, 4, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Calls, 0UL);

    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", 0x00010000 | REG_DWORD, &DefaultDword, 4, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Calls, 1UL);
    ok_eq_ulong(LastType, (ULONG)REG_DWORD);
    ok_eq_ulong(LastLength, 4UL);

    Status = QueryOne(RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", REG_SZ, DefaultString, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Calls, 1UL);
    ok_eq_ulong(LastType, (ULONG)REG_SZ);
    ok_eq_ulong(LastLength, (ULONG)sizeof(DefaultString));

    Direct[0] = 0xAAAAAAAA;
    Direct[1] = 0xBBBBBBBB;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_TYPECHECK, L"Missing",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE, NULL, 0, Direct);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    ok_eq_hex(Direct[0], 0xAAAAAAAAUL);

    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_TYPECHECK, L"Missing",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, &DefaultDword, 4, Direct);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Direct[0], 0x55UL);
    ok_eq_hex(Direct[1], 0xBBBBBBBBUL);

    Direct[0] = 0xAAAAAAAA;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK, L"Present",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE, NULL, 0, Direct);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Direct[0], 7UL);

    RoutineResult = STATUS_INVALID_PARAMETER;
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", REG_DWORD, &DefaultDword, 4, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_ulong(Calls, 1UL);

    RoutineResult = STATUS_BUFFER_TOO_SMALL;
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Present", REG_NONE, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Calls, 1UL);
    RoutineResult = STATUS_SUCCESS;
}

START_TEST(RtlQueryRegistryKM)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(TEST_KEY);
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"Present");
    UNICODE_STRING ExName = RTL_CONSTANT_STRING(L"RtlQueryRegistryValuesEx");
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Key;
    ULONG Seven = 7;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateKey(&Key, KEY_ALL_ACCESS, &Attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = ZwSetValueKey(Key, &ValueName, 0, REG_DWORD, &Seven, sizeof(Seven));
    ok_eq_hex(Status, STATUS_SUCCESS);

    Query = RtlQueryRegistryValues;
    TestDefaults();

    Query = (PFN_QUERY_REGISTRY_VALUES)MmGetSystemRoutineAddress(&ExName);
    if (Query)
        TestDefaults();
    else
        trace("RtlQueryRegistryValuesEx is not exported\n");

    ZwDeleteKey(Key);
    ZwClose(Key);
}
