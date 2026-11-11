/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RtlEnumerateGenericTableLikeADirectory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef PVOID (NTAPI *FN_RtlEnumerateGenericTableLikeADirectory)(PRTL_AVL_TABLE, PRTL_AVL_MATCH_FUNCTION, PVOID,
                                                                  ULONG, PVOID *, PULONG, PVOID);

static FN_RtlEnumerateGenericTableLikeADirectory pRtlEnumerateGenericTableLikeADirectory;

typedef struct _MATCH_DATA
{
    PRTL_AVL_TABLE Table;
    ULONG Divisor;
    ULONG StopAt;
    ULONG Calls;
    ULONG MaxKey;
    BOOLEAN WrongTable;
} MATCH_DATA, *PMATCH_DATA;

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
    return RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Size);
}

static
VOID
NTAPI
FreeNode(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Table);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
}

static
NTSTATUS
NTAPI
MatchKeys(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID UserData,
    _In_ PVOID MatchData)
{
    PMATCH_DATA Match = MatchData;
    ULONG Key = *(PULONG)UserData;

    Match->Calls++;
    if (Key > Match->MaxKey)
        Match->MaxKey = Key;
    if (Table != Match->Table)
        Match->WrongTable = TRUE;
    if (Match->StopAt != 0 && Key >= Match->StopAt)
        return STATUS_NO_MORE_MATCHES;
    if (Key % Match->Divisor)
        return STATUS_NO_MATCH;
    return STATUS_SUCCESS;
}

static
ULONG
NextKey(
    _In_ PRTL_AVL_TABLE Table,
    _In_opt_ PRTL_AVL_MATCH_FUNCTION MatchFunction,
    _In_opt_ PVOID MatchData,
    _In_ ULONG NextFlag,
    _Inout_ PVOID *RestartKey,
    _Inout_ PULONG DeleteCount,
    _In_ ULONG Key)
{
    PULONG Found;

    Found = pRtlEnumerateGenericTableLikeADirectory(Table, MatchFunction, MatchData, NextFlag,
                                                    RestartKey, DeleteCount, &Key);
    return Found ? *Found : 0;
}

#define ok_next(Expected, ...) \
    do \
    { \
        ULONG Next_ = NextKey(__VA_ARGS__); \
        ok(Next_ == (ULONG)(Expected), "Enumeration returned %lu, expected %lu\n", Next_, (ULONG)(Expected)); \
    } while (0)

static
VOID
InsertKeys(
    _In_ PRTL_AVL_TABLE Table)
{
    static const ULONG Keys[] = { 30, 10, 50, 20, 40 };
    ULONG Index;
    BOOLEAN NewElement;

    for (Index = 0; Index < RTL_NUMBER_OF(Keys); Index++)
    {
        NewElement = FALSE;
        ok(RtlInsertElementGenericTableAvl(Table, (PVOID)&Keys[Index], sizeof(ULONG), &NewElement) != NULL,
           "Insert %lu failed\n", Keys[Index]);
        ok(NewElement == TRUE, "Key %lu was not new\n", Keys[Index]);
    }
}

static
VOID
TestEnumeration(
    _In_ PRTL_AVL_TABLE Table)
{
    PVOID RestartKey;
    ULONG DeleteCount, Key;

    RestartKey = NULL;
    DeleteCount = 0;
    ok_next(10UL, Table, NULL, NULL, FALSE, &RestartKey, &DeleteCount, 0);
    ok(RestartKey != NULL, "RestartKey was not set\n");
    ok_next(20UL, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 10);
    ok_next(30UL, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 20);
    ok_next(40UL, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 30);
    ok_next(50UL, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 40);
    ok_next(0UL, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 50);
    ok_eq_ulong(DeleteCount, Table->DeleteCount);

    RestartKey = NULL;
    DeleteCount = 0;
    ok_next(10UL, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 0);

    for (Key = 10; Key <= 50; Key += 10)
    {
        RestartKey = NULL;
        ok_next(Key, Table, NULL, NULL, FALSE, &RestartKey, &DeleteCount, Key);
        RestartKey = NULL;
        ok_next(Key < 50 ? Key + 10 : 0, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, Key);
    }

    RestartKey = NULL;
    ok_next(40UL, Table, NULL, NULL, FALSE, &RestartKey, &DeleteCount, 35);
    RestartKey = NULL;
    ok_next(40UL, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 35);
    RestartKey = NULL;
    ok_next(0UL, Table, NULL, NULL, FALSE, &RestartKey, &DeleteCount, 55);
    RestartKey = NULL;
    ok_next(0UL, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 55);
}

static
VOID
TestMatchFunction(
    _In_ PRTL_AVL_TABLE Table)
{
    MATCH_DATA Match = { Table, 20, 0, 0, 0, FALSE };
    PVOID RestartKey = NULL;
    ULONG DeleteCount = 0;

    ok_next(20UL, Table, MatchKeys, &Match, FALSE, &RestartKey, &DeleteCount, 0);
    ok_next(40UL, Table, MatchKeys, &Match, TRUE, &RestartKey, &DeleteCount, 20);
    ok_next(0UL, Table, MatchKeys, &Match, TRUE, &RestartKey, &DeleteCount, 40);
    ok(Match.Calls != 0, "Match function was not called\n");
    ok(Match.WrongTable == FALSE, "Match function received another table\n");

    Match.StopAt = 40;
    Match.MaxKey = 0;
    RestartKey = NULL;
    ok_next(20UL, Table, MatchKeys, &Match, FALSE, &RestartKey, &DeleteCount, 0);
    ok_next(0UL, Table, MatchKeys, &Match, TRUE, &RestartKey, &DeleteCount, 20);
    ok(Match.MaxKey == 40, "Enumeration went on to key %lu after STATUS_NO_MORE_MATCHES\n", Match.MaxKey);
}

static
VOID
TestDeleteBetweenCalls(
    _In_ PRTL_AVL_TABLE Table)
{
    PVOID RestartKey = NULL;
    ULONG DeleteCount = 0, Key = 20;

    ok_next(10UL, Table, NULL, NULL, FALSE, &RestartKey, &DeleteCount, 10);
    ok_next(20UL, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 10);
    ok_eq_ulong(DeleteCount, Table->DeleteCount);

    ok(RtlDeleteElementGenericTableAvl(Table, &Key) == TRUE, "Delete 20 failed\n");
    ok(DeleteCount != Table->DeleteCount, "Delete did not change the table delete count\n");

    ok_next(30UL, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 20);
    ok_eq_ulong(DeleteCount, Table->DeleteCount);
    ok_next(40UL, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 30);

    Key = 40;
    ok(RtlDeleteElementGenericTableAvl(Table, &Key) == TRUE, "Delete 40 failed\n");
    ok_next(50, Table, NULL, NULL, FALSE, &RestartKey, &DeleteCount, 40);
    ok_eq_ulong(DeleteCount, Table->DeleteCount);
    ok_next(0, Table, NULL, NULL, TRUE, &RestartKey, &DeleteCount, 50);
}

START_TEST(RtlEnumerateGenericTableLikeADirectory)
{
    RTL_AVL_TABLE Table;
    PVOID RestartKey = NULL;
    ULONG DeleteCount = 0, Key;
    PVOID Element;

    pRtlEnumerateGenericTableLikeADirectory = (FN_RtlEnumerateGenericTableLikeADirectory)
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlEnumerateGenericTableLikeADirectory");
    if (!pRtlEnumerateGenericTableLikeADirectory)
    {
        skip("RtlEnumerateGenericTableLikeADirectory is not exported\n");
        return;
    }

    RtlInitializeGenericTableAvl(&Table, CompareKeys, AllocateNode, FreeNode, NULL);
    ok_next(0UL, &Table, NULL, NULL, FALSE, &RestartKey, &DeleteCount, 0);

    InsertKeys(&Table);
    TestEnumeration(&Table);
    TestMatchFunction(&Table);
    TestDeleteBetweenCalls(&Table);

    while ((Element = RtlGetElementGenericTableAvl(&Table, 0)) != NULL)
    {
        Key = *(PULONG)Element;
        RtlDeleteElementGenericTableAvl(&Table, &Key);
    }
    RestartKey = NULL;
    ok_next(0UL, &Table, NULL, NULL, FALSE, &RestartKey, &DeleteCount, 0);
}
