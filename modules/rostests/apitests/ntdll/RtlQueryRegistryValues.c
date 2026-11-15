/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the query table processing of RtlQueryRegistryValues and RtlQueryRegistryValuesEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>

#define TEST_KEY L"\\Registry\\Machine\\Software\\RtlQueryRegistryValues_apitest"
#define TEST_SUBKEY TEST_KEY L"\\Sub"
#define ROUTINE_CONTEXT ((PVOID)0x5678)
#define ENTRY_CONTEXT ((PVOID)0x1234)

typedef NTSTATUS (NTAPI *PFN_QUERY_REGISTRY_VALUES)(ULONG, PCWSTR, PRTL_QUERY_REGISTRY_TABLE, PVOID, PVOID);

typedef struct _ROUTINE_LOG
{
    ULONG Calls;
    ULONG Type;
    ULONG Length;
    PVOID Data;
    ULONG First;
    NTSTATUS Result;
    WCHAR Trace[128];
} ROUTINE_LOG;

static PFN_QUERY_REGISTRY_VALUES Query;
static ROUTINE_LOG Log;
static ULONG Zero16[4];
static ULONG DefaultDword = 0x55;
static WCHAR DefaultString[] = L"dflt";

static
NTSTATUS
NTAPI
LogRoutine(
    _In_ PWSTR ValueName,
    _In_ ULONG ValueType,
    _In_ PVOID ValueData,
    _In_ ULONG ValueLength,
    _In_ PVOID Context,
    _In_ PVOID EntryContext)
{
    ok(Context == ROUTINE_CONTEXT, "Context %p\n", Context);
    ok(EntryContext == ENTRY_CONTEXT, "EntryContext %p\n", EntryContext);
    Log.Calls++;
    Log.Type = ValueType;
    Log.Length = ValueLength;
    Log.Data = ValueData;
    Log.First = 0;
    if (ValueData && ValueLength >= sizeof(ULONG))
        RtlCopyMemory(&Log.First, ValueData, sizeof(ULONG));
    StringCbCatW(Log.Trace, sizeof(Log.Trace), ValueName ? ValueName : L"(null)");
    StringCbCatW(Log.Trace, sizeof(Log.Trace), L":");
    if ((ValueType == REG_SZ || ValueType == REG_EXPAND_SZ) && ValueData)
        StringCbCatW(Log.Trace, sizeof(Log.Trace), ValueData);
    StringCbCatW(Log.Trace, sizeof(Log.Trace), L";");
    return Log.Result;
}

static
VOID
ResetLog(
    _In_ NTSTATUS Result)
{
    RtlZeroMemory(&Log, sizeof(Log));
    Log.Type = Log.Length = Log.First = 0xFFFFFFFF;
    Log.Result = Result;
}

static
NTSTATUS
QueryOne(
    _In_ ULONG Flags,
    _In_opt_ PCWSTR Name,
    _In_ ULONG DefaultType,
    _In_opt_ PVOID DefaultData,
    _In_ ULONG DefaultLength,
    _In_opt_ PVOID Direct)
{
    RTL_QUERY_REGISTRY_TABLE Table[2];

    RtlZeroMemory(Table, sizeof(Table));
    Table[0].QueryRoutine = Direct ? NULL : LogRoutine;
    Table[0].Flags = Flags;
    Table[0].Name = (PWSTR)Name;
    Table[0].EntryContext = Direct ? Direct : ENTRY_CONTEXT;
    Table[0].DefaultType = DefaultType;
    Table[0].DefaultData = DefaultData;
    Table[0].DefaultLength = DefaultLength;
    return Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY, Table, ROUTINE_CONTEXT, NULL);
}

#define check_routine(c,n,t,l,f,d) check_routine_(__LINE__,c,n,t,l,f,d)
static
VOID
check_routine_(
    _In_ ULONG Line,
    _In_ PCSTR Case,
    _In_ ULONG Calls,
    _In_ ULONG Type,
    _In_ ULONG Length,
    _In_ ULONG First,
    _In_opt_ PVOID Data)
{
    ok_(__FILE__, Line)(Log.Calls == Calls, "case %s: %lu calls, expected %lu\n", Case, Log.Calls, Calls);
    if (Calls == 0)
        return;
    ok_(__FILE__, Line)(Log.Type == Type, "case %s: type %lu, expected %lu\n", Case, Log.Type, Type);
    ok_(__FILE__, Line)(Log.Length == Length, "case %s: length %lu, expected %lu\n", Case, Log.Length, Length);
    ok_(__FILE__, Line)(Log.First == First, "case %s: data 0x%08lx, expected 0x%08lx\n", Case, Log.First, First);
    if (Data)
        ok_(__FILE__, Line)(Log.Data == Data, "case %s: data at %p, expected the default at %p\n", Case, Log.Data, Data);
}

static
VOID
TestDefaults(VOID)
{
    ULONG Direct[4];
    NTSTATUS Status;

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND, L"Missing",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, Zero16, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    check_routine("A", 1, REG_DWORD, 0, 0, Zero16);

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", REG_DWORD, &DefaultDword, 4, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    check_routine("B", 1, REG_DWORD, 4, 0x55, &DefaultDword);

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", REG_NONE, NULL, 0, NULL);
    ok_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    check_routine("C", 0, 0, 0, 0, NULL);

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Missing",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, Zero16, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    check_routine("D", 1, REG_DWORD, 0, 0, Zero16);

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", REG_DWORD, &DefaultDword, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    check_routine("E", 1, REG_DWORD, 0, 0, &DefaultDword);

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND, L"Present",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, Zero16, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    check_routine("G", 1, REG_DWORD, 4, 7, NULL);

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Missing",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE, Zero16, 4, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    check_routine("H", 0, 0, 0, 0, NULL);

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", 0x00010000 | REG_DWORD, &DefaultDword, 4, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    check_routine("I", 1, REG_DWORD, 4, 0x55, &DefaultDword);

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", REG_SZ, DefaultString, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    check_routine("J", 1, REG_SZ, sizeof(DefaultString), 0x00660064, DefaultString);

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", REG_SZ, DefaultString, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    check_routine("K", 1, REG_SZ, sizeof(DefaultString), 0x00660064, DefaultString);

    Direct[0] = 0xAAAAAAAA;
    Direct[1] = 0xBBBBBBBB;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_TYPECHECK, L"Missing",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE, NULL, 0, Direct);
    ok_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    ok_hex(Direct[0], 0xAAAAAAAA);
    ok_hex(Direct[1], 0xBBBBBBBB);

    Direct[0] = 0xAAAAAAAA;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK, L"Missing",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, &DefaultDword, 4, Direct);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Direct[0], 0x55);
    ok_hex(Direct[1], 0xBBBBBBBB);

    Direct[0] = 0xAAAAAAAA;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_TYPECHECK, L"Missing",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, &DefaultDword, 4, Direct);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Direct[0], 0x55);

    Direct[0] = 0xAAAAAAAA;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT, L"Missing",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, &DefaultDword, 4, Direct);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Direct[0], 0x55);

    Direct[0] = 0xAAAAAAAA;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK, L"Present",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE, NULL, 0, Direct);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Direct[0], 7);
    ok_hex(Direct[1], 0xBBBBBBBB);
}

static
VOID
TestRoutineStatus(VOID)
{
    NTSTATUS Status;

    ResetLog(STATUS_INVALID_PARAMETER);
    Status = QueryOne(RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND, L"Present",
                      (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, Zero16, 0, NULL);
    ok_hex(Status, STATUS_INVALID_PARAMETER);
    check_routine("Q", 1, REG_DWORD, 4, 7, NULL);

    ResetLog(STATUS_INVALID_PARAMETER);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", REG_DWORD, &DefaultDword, 4, NULL);
    ok_hex(Status, STATUS_INVALID_PARAMETER);
    check_routine("R", 1, REG_DWORD, 4, 0x55, &DefaultDword);

    ResetLog(STATUS_BUFFER_TOO_SMALL);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Missing", REG_DWORD, &DefaultDword, 4, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    check_routine("S", 1, REG_DWORD, 4, 0x55, &DefaultDword);
}

static
VOID
TestStrings(VOID)
{
    UNICODE_STRING String;
    WCHAR Small[2];
    NTSTATUS Status;

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(0, L"Multi", REG_NONE, NULL, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Calls, 2UL);
    ok_eq_wstr(Log.Trace, L"Multi:one;Multi:two;");
    ok_eq_ulong(Log.Type, (ULONG)REG_SZ);
    ok_eq_ulong(Log.Length, (ULONG)sizeof(L"two"));

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Multi", REG_NONE, NULL, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Calls, 1UL);
    ok_eq_ulong(Log.Type, (ULONG)REG_MULTI_SZ);
    ok_eq_ulong(Log.Length, (ULONG)sizeof(L"one\0two") + sizeof(WCHAR));

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Multi2", REG_NONE, NULL, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Length, (ULONG)sizeof(L"one\0two\0") + sizeof(WCHAR));

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Sz", REG_NONE, NULL, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Length, (ULONG)sizeof(L"text"));

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"SzBare", REG_NONE, NULL, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Length, (ULONG)sizeof(L"abc") - sizeof(WCHAR));
    ok_eq_wstr(Log.Trace, L"SzBare:abc;");

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"EmptySz", REG_SZ, DefaultString, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Calls, 1UL);
    ok_eq_ulong(Log.Length, (ULONG)sizeof(DefaultString));
    ok(Log.Data == DefaultString, "Data %p\n", Log.Data);

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Empty", REG_DWORD, &DefaultDword, 4, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Calls, 1UL);
    ok_eq_ulong(Log.Length, 4UL);
    ok_hex(Log.First, 0x55);

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(0, L"Expand", REG_NONE, NULL, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Calls, 1UL);
    ok_eq_ulong(Log.Type, (ULONG)REG_SZ);
    ok_eq_wstr(Log.Trace, L"Expand:<value>;");
    ok_eq_ulong(Log.Length, (ULONG)sizeof(L"<value>"));

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, L"Expand", REG_NONE, NULL, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Type, (ULONG)REG_EXPAND_SZ);
    ok_eq_ulong(Log.Length, (ULONG)sizeof(L"<%RQRV_APITEST%>"));
    ok_eq_wstr(Log.Trace, L"Expand:<%RQRV_APITEST%>;");

    RtlInitEmptyUnicodeString(&String, NULL, 0);
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT, L"Sz", REG_NONE, NULL, 0, &String);
    ok_hex(Status, STATUS_SUCCESS);
    ok(String.Buffer != NULL, "No buffer was allocated\n");
    ok_eq_ulong((ULONG)String.Length, (ULONG)sizeof(L"text") - sizeof(WCHAR));
    ok(String.MaximumLength >= String.Length + sizeof(WCHAR), "MaximumLength %u\n", String.MaximumLength);
    if (String.Buffer)
    {
        ok(!memcmp(String.Buffer, L"text", sizeof(L"text")), "Buffer %.*ls\n", String.Length / 2, String.Buffer);
        RtlFreeUnicodeString(&String);
    }

    RtlInitEmptyUnicodeString(&String, NULL, 0);
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT, L"Missing", REG_SZ, DefaultString, 0, &String);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong((ULONG)String.Length, (ULONG)sizeof(DefaultString) - sizeof(WCHAR));
    if (String.Buffer)
    {
        ok(!memcmp(String.Buffer, DefaultString, sizeof(DefaultString)), "Buffer %.*ls\n", String.Length / 2, String.Buffer);
        RtlFreeUnicodeString(&String);
    }

    RtlInitEmptyUnicodeString(&String, NULL, 0);
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_NOEXPAND, L"Multi", REG_NONE, NULL, 0, &String);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong((ULONG)String.Length, (ULONG)sizeof(L"one\0two"));
    if (String.Buffer)
    {
        ok(!memcmp(String.Buffer, L"one\0two\0", sizeof(L"one\0two\0")), "Buffer %.*ls\n", String.Length / 2, String.Buffer);
        RtlFreeUnicodeString(&String);
    }

    RtlFillMemory(Small, sizeof(Small), 0xCC);
    RtlInitEmptyUnicodeString(&String, Small, sizeof(Small));
    String.Length = sizeof(WCHAR);
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT, L"Sz", REG_NONE, NULL, 0, &String);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong((ULONG)String.Length, (ULONG)sizeof(WCHAR));
    ok_eq_ulong((ULONG)String.MaximumLength, 4UL);
    ok(String.Buffer == Small, "Buffer %p\n", String.Buffer);
    ok_hex(Small[0], 0xCCCC);
    ok_hex(Small[1], 0xCCCC);
}

static WCHAR SplitTrace[256];

static
NTSTATUS
NTAPI
MixedRoutine(
    _In_ PWSTR ValueName,
    _In_ ULONG ValueType,
    _In_ PVOID ValueData,
    _In_ ULONG ValueLength,
    _In_ PVOID Context,
    _In_ PVOID EntryContext)
{
    WCHAR Item[32];

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);

    ok(EntryContext == NULL || ValueData == EntryContext, "Data %p, expected the default %p\n", ValueData, EntryContext);
    StringCbPrintfW(Item, sizeof(Item), L"%lu/%lu;", ValueType, ValueLength);
    StringCbCatW(SplitTrace, sizeof(SplitTrace), Item);
    return STATUS_SUCCESS;
}

static
VOID
TestMixedTable(VOID)
{
    static WCHAR Empty[] = L"";
    RTL_QUERY_REGISTRY_TABLE Table[7];
    ULONG Flags = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND;
    NTSTATUS Status;

    RtlZeroMemory(Table, sizeof(Table));
    Table[0] = (RTL_QUERY_REGISTRY_TABLE){ MixedRoutine, Flags, L"Missing", Zero16,
                                           (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, Zero16, 0 };
    Table[1] = (RTL_QUERY_REGISTRY_TABLE){ MixedRoutine, Flags, L"Missing", Empty,
                                           (REG_SZ << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_SZ, Empty, 0 };
    Table[2] = (RTL_QUERY_REGISTRY_TABLE){ MixedRoutine, Flags, L"Present", NULL,
                                           (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, Zero16, 0 };
    Table[3] = (RTL_QUERY_REGISTRY_TABLE){ MixedRoutine, Flags, L"Missing", DefaultString,
                                           (REG_SZ << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_SZ, DefaultString, 0 };
    Table[4] = (RTL_QUERY_REGISTRY_TABLE){ MixedRoutine, Flags, L"Sz", NULL,
                                           (REG_SZ << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_SZ, Empty, 0 };
    Table[5] = (RTL_QUERY_REGISTRY_TABLE){ MixedRoutine, Flags, L"Missing", &DefaultDword,
                                           (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD, &DefaultDword, 4 };
    SplitTrace[0] = UNICODE_NULL;
    Status = Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY, Table, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_wstr(SplitTrace, L"4/0;1/2;4/4;1/10;1/10;4/4;");
}

static
NTSTATUS
NTAPI
SplitRoutine(
    _In_ PWSTR ValueName,
    _In_ ULONG ValueType,
    _In_ PVOID ValueData,
    _In_ ULONG ValueLength,
    _In_ PVOID Context,
    _In_ PVOID EntryContext)
{
    WCHAR Item[64];

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(EntryContext);

    StringCbPrintfW(Item, sizeof(Item), L"%lu:%ls/%lu;", ValueType, ValueData ? (PCWSTR)ValueData : L"(null)", ValueLength);
    StringCbCatW(SplitTrace, sizeof(SplitTrace), Item);
    return STATUS_SUCCESS;
}

#define check_split(f,d,l,e) check_split_(__LINE__,f,d,l,e)
static
VOID
check_split_(
    _In_ ULONG Line,
    _In_ BOOLEAN Found,
    _In_ PCWSTR Data,
    _In_ ULONG Length,
    _In_ PCWSTR Expected)
{
    RTL_QUERY_REGISTRY_TABLE Table[2];
    NTSTATUS Status;

    RtlZeroMemory(Table, sizeof(Table));
    Table[0].QueryRoutine = SplitRoutine;
    Table[0].Name = Found ? L"Split" : L"Missing";
    if (Found)
    {
        RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_KEY, L"Split", REG_MULTI_SZ, (PVOID)Data, Length);
    }
    else
    {
        Table[0].DefaultType = REG_MULTI_SZ;
        Table[0].DefaultData = (PVOID)Data;
        Table[0].DefaultLength = Length;
    }
    SplitTrace[0] = UNICODE_NULL;
    Status = Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY, Table, NULL, NULL);
    ok_(__FILE__, Line)(Status == STATUS_SUCCESS, "Status 0x%lx\n", Status);
    ok_(__FILE__, Line)(!wcscmp(SplitTrace, Expected), "%s %lu bytes: \"%ls\", expected \"%ls\"\n",
                        Found ? "found" : "default", Length, SplitTrace, Expected);
}

static
VOID
TestMultiSplit(VOID)
{
    check_split(TRUE, L"\0", sizeof(L"\0"), L"1:/2;");
    check_split(TRUE, L"\0", sizeof(WCHAR), L"");
    check_split(TRUE, L"A\0B\0C", sizeof(L"A\0B\0C"), L"1:A/4;1:B/4;1:C/4;");
    check_split(TRUE, L"A\0B\0C", sizeof(L"A\0B\0C") - sizeof(WCHAR), L"1:A/4;1:B/4;");
    check_split(TRUE, L"One\0\0Three", sizeof(L"One\0\0Three"), L"1:One/8;1:/2;1:Three/12;");
    check_split(TRUE, L"One\0\0Three\0", sizeof(L"One\0\0Three\0"), L"1:One/8;1:/2;1:Three/12;");
    check_split(FALSE, L"\0", sizeof(L"\0"), L"");
    check_split(FALSE, L"A\0B\0C", sizeof(L"A\0B\0C"), L"1:A/4;1:B/4;");
    check_split(FALSE, L"A\0B\0C", sizeof(L"A\0B\0C") - sizeof(WCHAR), L"1:A/4;1:B/4;");
    check_split(FALSE, L"One\0Two", sizeof(L"One\0Two"), L"1:One/8;1:Two/8;");
    check_split(FALSE, L"One\0Two\0", sizeof(L"One\0Two\0"), L"1:One/8;1:Two/8;");
    check_split(FALSE, L"One\0\0Three", sizeof(L"One\0\0Three"), L"1:One/8;1:/2;1:Three/12;");
    check_split(FALSE, L"One\0\0Three\0", sizeof(L"One\0\0Three\0"), L"1:One/8;1:/2;1:Three/12;");
    check_split(FALSE, L"String", sizeof(L"String"), L"1:String/14;");
    check_split(FALSE, L"One\0\0Three\0", 0, L"1:One/8;");
}

static
VOID
TestDirectBinary(VOID)
{
    LONG Buffer[5];
    NTSTATUS Status;

    RtlFillMemory(Buffer, sizeof(Buffer), 0xCC);
    Buffer[0] = -8;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT, L"Bin", REG_NONE, NULL, 0, Buffer);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Buffer[0], 0x04030201);
    ok_hex(Buffer[1], 0x08070605);
    ok_hex(Buffer[2], 0xCCCCCCCC);

    RtlFillMemory(Buffer, sizeof(Buffer), 0xCC);
    Buffer[0] = 16;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT, L"Bin", REG_NONE, NULL, 0, Buffer);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Buffer[0], 8);
    ok_hex(Buffer[1], REG_BINARY);
    ok_hex(Buffer[2], 0x04030201);
    ok_hex(Buffer[3], 0x08070605);
    ok_hex(Buffer[4], 0xCCCCCCCC);

    RtlFillMemory(Buffer, sizeof(Buffer), 0xCC);
    Buffer[0] = -4;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT, L"Bin", REG_NONE, NULL, 0, Buffer);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Buffer[0], -4);
    ok_hex(Buffer[1], 0xCCCCCCCC);

    RtlFillMemory(Buffer, sizeof(Buffer), 0xCC);
    Buffer[0] = 12;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT, L"Bin", REG_NONE, NULL, 0, Buffer);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Buffer[0], 12);
    ok_hex(Buffer[1], 0xCCCCCCCC);

    Buffer[0] = 0xAAAAAAAA;
    Status = QueryOne(RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK, L"Present",
                      (REG_SZ << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE, NULL, 0, Buffer);
    ok_hex(Status, STATUS_OBJECT_TYPE_MISMATCH);
    ok_hex(Buffer[0], 0xAAAAAAAA);
}

static
VOID
TestTable(VOID)
{
    RTL_QUERY_REGISTRY_TABLE Table[4];
    ULONG Direct;
    HANDLE Key;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOEXPAND, NULL, REG_NONE, NULL, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Calls, 8UL);
    ok_eq_wstr(Log.Trace, L"Present:;Doomed:;Multi:;Multi2:;SzBare:abc;Expand:<%RQRV_APITEST%>;Sz:text;Bin:;");

    ResetLog(STATUS_SUCCESS);
    Status = QueryOne(RTL_QUERY_REGISTRY_NOVALUE, NULL, REG_NONE, NULL, 0, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Calls, 1UL);
    ok_eq_ulong(Log.Type, (ULONG)REG_NONE);
    ok_eq_ulong(Log.Length, 0UL);
    ok(Log.Data == NULL, "Data %p\n", Log.Data);

    RtlZeroMemory(Table, sizeof(Table));
    Table[0].Flags = RTL_QUERY_REGISTRY_SUBKEY;
    Table[0].Name = L"Sub";
    Table[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Table[1].Name = L"Inner";
    Table[1].EntryContext = &Direct;
    Direct = 0;
    Status = Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY, Table, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Direct, 9);

    RtlZeroMemory(Table, sizeof(Table));
    Table[0].Flags = RTL_QUERY_REGISTRY_SUBKEY;
    Table[0].Name = L"Sub";
    Table[1].Flags = RTL_QUERY_REGISTRY_TOPKEY | RTL_QUERY_REGISTRY_DIRECT;
    Table[1].Name = L"Present";
    Table[1].EntryContext = &Direct;
    Direct = 0;
    Status = Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY, Table, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Direct, 7);

    RtlZeroMemory(Table, sizeof(Table));
    Table[0].Flags = RTL_QUERY_REGISTRY_SUBKEY | RTL_QUERY_REGISTRY_REQUIRED;
    Table[0].Name = L"NoSuchKey";
    Status = Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY, Table, NULL, NULL);
    ok_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);

    ResetLog(STATUS_SUCCESS);
    Status = Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY L"\\NoSuchKey", Table, NULL, NULL);
    ok_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    Status = Query(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL, TEST_KEY L"\\NoSuchKey", Table, NULL, NULL);
    ok_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);

    RtlZeroMemory(Table, sizeof(Table));
    Table[0].QueryRoutine = LogRoutine;
    Table[0].Flags = RTL_QUERY_REGISTRY_SUBKEY;
    Table[0].Name = L"Sub";
    Table[0].EntryContext = ENTRY_CONTEXT;
    ResetLog(STATUS_SUCCESS);
    Status = Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY, Table, ROUTINE_CONTEXT, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Log.Calls, 1UL);
    ok_eq_wstr(Log.Trace, L"Inner:;");

    RtlZeroMemory(Table, sizeof(Table));
    Table[0].Flags = RTL_QUERY_REGISTRY_SUBKEY;
    Table[0].Name = L"NoSuchKey";
    Table[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
    Table[1].Name = L"Inner";
    Table[1].EntryContext = &Direct;
    Table[1].DefaultType = REG_DWORD;
    Table[1].DefaultData = &DefaultDword;
    Table[1].DefaultLength = sizeof(DefaultDword);
    Direct = 0;
    Status = Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY, Table, NULL, NULL);
    ok_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    ok_hex(Direct, 0);

    RtlInitUnicodeString(&Name, TEST_KEY);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&Key, KEY_READ, &Attributes);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(Table, sizeof(Table));
        Table[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
        Table[0].Name = L"Present";
        Table[0].EntryContext = &Direct;
        Direct = 0;
        Status = Query(RTL_REGISTRY_HANDLE, (PCWSTR)Key, Table, NULL, NULL);
        ok_hex(Status, STATUS_SUCCESS);
        ok_hex(Direct, 7);
        NtClose(Key);
    }

    RtlZeroMemory(Table, sizeof(Table));
    Table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_DELETE;
    Table[0].Name = L"Doomed";
    Table[0].EntryContext = &Direct;
    Direct = 0;
    Status = Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY, Table, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Direct, 3);
    Direct = 0xAAAAAAAA;
    Status = Query(RTL_REGISTRY_ABSOLUTE, TEST_KEY, Table, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(Direct, 0xAAAAAAAA);
}

static
VOID
DeleteTestKey(
    _In_ PCWSTR Path)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Key;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (NT_SUCCESS(NtOpenKey(&Key, DELETE, &Attributes)))
    {
        NtDeleteKey(Key);
        NtClose(Key);
    }
}

static
BOOLEAN
CreateTestKey(VOID)
{
    static const BYTE Binary[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    static const WCHAR Multi[] = L"one\0two";
    static const WCHAR Multi2[] = L"one\0two\0";
    static const WCHAR SzBare[] = { L'a', L'b', L'c' };
    static const WCHAR Expand[] = L"<%RQRV_APITEST%>";
    static const WCHAR Sz[] = L"text";
    ULONG Value;

    DeleteTestKey(TEST_SUBKEY);
    DeleteTestKey(TEST_KEY);
    if (!NT_SUCCESS(RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, TEST_KEY)) ||
        !NT_SUCCESS(RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, TEST_SUBKEY)))
    {
        return FALSE;
    }

    Value = 7;
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_KEY, L"Present", REG_DWORD, &Value, sizeof(Value));
    Value = 3;
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_KEY, L"Doomed", REG_DWORD, &Value, sizeof(Value));
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_KEY, L"Multi", REG_MULTI_SZ, (PVOID)Multi, sizeof(Multi));
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_KEY, L"Multi2", REG_MULTI_SZ, (PVOID)Multi2, sizeof(Multi2));
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_KEY, L"SzBare", REG_SZ, (PVOID)SzBare, sizeof(SzBare));
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_KEY, L"Expand", REG_EXPAND_SZ, (PVOID)Expand, sizeof(Expand));
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_KEY, L"Sz", REG_SZ, (PVOID)Sz, sizeof(Sz));
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_KEY, L"Bin", REG_BINARY, (PVOID)Binary, sizeof(Binary));
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_KEY, L"Empty", REG_DWORD, &Value, 0);
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_KEY, L"EmptySz", REG_SZ, (PVOID)Sz, 0);
    Value = 9;
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, TEST_SUBKEY, L"Inner", REG_DWORD, &Value, sizeof(Value));
    return TRUE;
}

static
VOID
RunAll(
    _In_ PCSTR Routine)
{
    trace("%s\n", Routine);
    if (!CreateTestKey())
    {
        skip("Cannot create %ls\n", TEST_KEY);
        return;
    }
    TestDefaults();
    TestRoutineStatus();
    TestMixedTable();
    TestStrings();
    TestDirectBinary();
    TestTable();
    TestMultiSplit();
    DeleteTestKey(TEST_SUBKEY);
    DeleteTestKey(TEST_KEY);
}

START_TEST(RtlQueryRegistryValues)
{
    UNICODE_STRING Variable = RTL_CONSTANT_STRING(L"RQRV_APITEST");
    UNICODE_STRING Value = RTL_CONSTANT_STRING(L"value");
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");

    RtlSetEnvironmentVariable(NULL, &Variable, &Value);

    Query = (PFN_QUERY_REGISTRY_VALUES)GetProcAddress(Ntdll, "RtlQueryRegistryValues");
    RunAll("RtlQueryRegistryValues");

    Query = (PFN_QUERY_REGISTRY_VALUES)GetProcAddress(Ntdll, "RtlQueryRegistryValuesEx");
    if (Query)
        RunAll("RtlQueryRegistryValuesEx");
    else
        skip("RtlQueryRegistryValuesEx is not exported\n");

    RtlSetEnvironmentVariable(NULL, &Variable, NULL);
}
