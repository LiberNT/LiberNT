/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-mode tests for the prefix table, directory-like AVL enumeration and altitude comparison
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define TAG_TEST 'tNmK'

typedef LONG (NTAPI *PFN_RTL_COMPARE_ALTITUDES)(PCUNICODE_STRING, PCUNICODE_STRING);

static
PPREFIX_TABLE_ENTRY
FindName(
    _In_ PPREFIX_TABLE Table,
    _In_ PCSZ Name)
{
    STRING FullName;

    RtlInitString(&FullName, Name);
    return PfxFindPrefix(Table, &FullName);
}

START_TEST(RtlPrefixTableKM)
{
    PREFIX_TABLE Table;
    PREFIX_TABLE_ENTRY EntryA, EntryAB, EntryDuplicate, EntryRoot;
    STRING NameA, NameAB, NameDuplicate, NameRoot;
    CHAR DuplicateBuffer[] = "\\a";

    RtlInitString(&NameA, "\\a");
    RtlInitString(&NameAB, "\\a\\b");
    RtlInitString(&NameDuplicate, DuplicateBuffer);
    RtlInitString(&NameRoot, "\\");

    PfxInitialize(&Table);
    ok(FindName(&Table, "\\a") == NULL, "Empty table returned an entry\n");
    ok(PfxInsertPrefix(&Table, &NameA, &EntryA) == TRUE, "Insert \\a failed\n");
    ok(PfxInsertPrefix(&Table, &NameAB, &EntryAB) == TRUE, "Insert \\a\\b failed\n");
    ok(PfxInsertPrefix(&Table, &NameDuplicate, &EntryDuplicate) == FALSE, "Duplicate \\a was inserted\n");
    ok(PfxInsertPrefix(&Table, &NameRoot, &EntryRoot) == TRUE, "Insert \\ failed\n");

    ok(FindName(&Table, "\\a\\b\\c") == &EntryAB, "\\a\\b\\c did not find \\a\\b\n");
    ok(FindName(&Table, "\\a\\bc") == &EntryA, "\\a\\bc matched inside a name component\n");
    ok(FindName(&Table, "\\x") == &EntryRoot, "\\x did not find the root prefix\n");
    ok(FindName(&Table, "x") == NULL, "A relative name matched\n");

    PfxRemovePrefix(&Table, &EntryAB);
    ok(FindName(&Table, "\\a\\b") == &EntryA, "\\a\\b did not fall back to \\a\n");
    PfxRemovePrefix(&Table, &EntryA);
    PfxRemovePrefix(&Table, &EntryRoot);
    ok(FindName(&Table, "\\a") == NULL, "Emptied table returned an entry\n");
}

static
RTL_GENERIC_COMPARE_RESULTS
NTAPI
CompareKeys(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID First,
    _In_ PVOID Second)
{
    ULONG Left = *(PULONG)First, Right = *(PULONG)Second;

    UNREFERENCED_PARAMETER(Table);
    if (Left < Right)
        return GenericLessThan;
    if (Left > Right)
        return GenericGreaterThan;
    return GenericEqual;
}

static
PVOID
NTAPI
AllocateNode(
    _In_ PRTL_AVL_TABLE Table,
    _In_ CLONG Size)
{
    UNREFERENCED_PARAMETER(Table);
    return ExAllocatePoolZero(NonPagedPool, Size, TAG_TEST);
}

static
VOID
NTAPI
FreeNode(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Table);
    ExFreePoolWithTag(Buffer, TAG_TEST);
}

static
NTSTATUS
NTAPI
MatchEven(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID UserData,
    _In_ PVOID MatchData)
{
    UNREFERENCED_PARAMETER(Table);
    UNREFERENCED_PARAMETER(MatchData);
    return (*(PULONG)UserData % 20) ? STATUS_NO_MATCH : STATUS_SUCCESS;
}

static
ULONG
NextKey(
    _In_ PRTL_AVL_TABLE Table,
    _In_opt_ PRTL_AVL_MATCH_FUNCTION MatchFunction,
    _In_ ULONG NextFlag,
    _Inout_ PVOID *RestartKey,
    _Inout_ PULONG DeleteCount,
    _In_ ULONG Key)
{
    PULONG Found;

    Found = RtlEnumerateGenericTableLikeADirectory(Table, MatchFunction, NULL, NextFlag,
                                                   RestartKey, DeleteCount, &Key);
    return Found ? *Found : 0;
}

#define ok_next(Expected, ...) \
    do \
    { \
        ULONG Next_ = NextKey(__VA_ARGS__); \
        ok(Next_ == (ULONG)(Expected), "Enumeration returned %lu, expected %lu\n", Next_, (ULONG)(Expected)); \
    } while (0)

START_TEST(RtlDirectoryEnumKM)
{
    static const ULONG Keys[] = { 30, 10, 50, 20, 40 };
    RTL_AVL_TABLE Table;
    PVOID RestartKey = NULL, Element;
    ULONG DeleteCount = 0, Index, Key;

    RtlInitializeGenericTableAvl(&Table, CompareKeys, AllocateNode, FreeNode, NULL);
    ok_next(0, &Table, NULL, FALSE, &RestartKey, &DeleteCount, 0);
    for (Index = 0; Index < RTL_NUMBER_OF(Keys); Index++)
    {
        ok(RtlInsertElementGenericTableAvl(&Table, (PVOID)&Keys[Index], sizeof(ULONG), NULL) != NULL,
           "Insert %lu failed\n", Keys[Index]);
    }

    RestartKey = NULL;
    ok_next(10, &Table, NULL, FALSE, &RestartKey, &DeleteCount, 0);
    ok_next(20, &Table, NULL, TRUE, &RestartKey, &DeleteCount, 10);
    ok_next(30, &Table, NULL, TRUE, &RestartKey, &DeleteCount, 20);

    Key = 30;
    ok(RtlDeleteElementGenericTableAvl(&Table, &Key) == TRUE, "Delete 30 failed\n");
    ok_next(40, &Table, NULL, TRUE, &RestartKey, &DeleteCount, 30);
    ok_eq_ulong(DeleteCount, Table.DeleteCount);
    ok_next(50, &Table, NULL, TRUE, &RestartKey, &DeleteCount, 40);
    ok_next(0, &Table, NULL, TRUE, &RestartKey, &DeleteCount, 50);

    RestartKey = NULL;
    ok_next(20, &Table, MatchEven, FALSE, &RestartKey, &DeleteCount, 0);
    ok_next(40, &Table, MatchEven, TRUE, &RestartKey, &DeleteCount, 20);
    ok_next(0, &Table, MatchEven, TRUE, &RestartKey, &DeleteCount, 40);

    RestartKey = NULL;
    ok_next(40, &Table, NULL, FALSE, &RestartKey, &DeleteCount, 35);

    while ((Element = RtlGetElementGenericTableAvl(&Table, 0)) != NULL)
    {
        Key = *(PULONG)Element;
        RtlDeleteElementGenericTableAvl(&Table, &Key);
    }
}

START_TEST(RtlCompareAltitudesKM)
{
    static const struct
    {
        PCWSTR First;
        PCWSTR Second;
        LONG Sign;
    } Cases[] =
    {
        { L"385100", L"385100", 0 },
        { L"03333", L"100.123456", 1 },
        { L"100.5", L"100.50", 0 },
        { L"9", L"10", -1 },
        { L"1.10", L"1.9", -1 },
    };
    PFN_RTL_COMPARE_ALTITUDES pRtlCompareAltitudes;
    UNICODE_STRING RoutineName, First, Second;
    LONG Result, Sign;
    ULONG Index;

    RtlInitUnicodeString(&RoutineName, L"RtlCompareAltitudes");
    pRtlCompareAltitudes = (PFN_RTL_COMPARE_ALTITUDES)MmGetSystemRoutineAddress(&RoutineName);
    if (!pRtlCompareAltitudes)
    {
        skip(FALSE, "RtlCompareAltitudes is not exported\n");
        return;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Cases); Index++)
    {
        RtlInitUnicodeString(&First, Cases[Index].First);
        RtlInitUnicodeString(&Second, Cases[Index].Second);
        Result = pRtlCompareAltitudes(&First, &Second);
        Sign = (Result > 0) - (Result < 0);
        ok(Sign == Cases[Index].Sign, "\"%wZ\" vs \"%wZ\" returned %ld\n", &First, &Second, Result);
    }
}
