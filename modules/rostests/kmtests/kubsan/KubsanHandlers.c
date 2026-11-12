/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel undefined behavior sanitizer report handlers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define KUBSAN_REPORTED 0x80000000UL

typedef struct _TEST_LOCATION
{
    PCSTR FileName;
    ULONG Line;
    volatile LONG Column;
} TEST_LOCATION;

typedef struct _TEST_TYPE
{
    USHORT Kind;
    USHORT Info;
    CHAR Name[16];
} TEST_TYPE;

typedef struct _TEST_OVERFLOW_DATA
{
    TEST_LOCATION Location;
    TEST_TYPE *Type;
} TEST_OVERFLOW_DATA;

typedef struct _TEST_BOUNDS_DATA
{
    TEST_LOCATION Location;
    TEST_TYPE *ArrayType;
    TEST_TYPE *IndexType;
} TEST_BOUNDS_DATA;

VOID __cdecl __ubsan_handle_add_overflow(TEST_OVERFLOW_DATA *Data, ULONG_PTR Left, ULONG_PTR Right);
VOID __cdecl __ubsan_handle_out_of_bounds(TEST_BOUNDS_DATA *Data, ULONG_PTR Index);

static TEST_TYPE TestIntType = { 0, (5 << 1) | 1, "'int'" };
static TEST_TYPE TestArrayType = { 0xFFFF, 0, "'int[4]'" };

static VOID TestReportsOnce(VOID)
{
    TEST_OVERFLOW_DATA Data = { { "kubsan-test.c", 10, 3 }, &TestIntType };

    __ubsan_handle_add_overflow(&Data, 0x7FFFFFFF, 1);
    ok_eq_hex((ULONG)Data.Location.Column, KUBSAN_REPORTED | 3);
    ok_eq_ulong(Data.Location.Line, 10UL);

    /* A site that already reported stays quiet and keeps its mark */
    __ubsan_handle_add_overflow(&Data, 0x7FFFFFFF, 1);
    ok_eq_hex((ULONG)Data.Location.Column, KUBSAN_REPORTED | 3);
}

static VOID TestSitesAreIndependent(VOID)
{
    TEST_BOUNDS_DATA First = { { "kubsan-test.c", 20, 5 }, &TestArrayType, &TestIntType };
    TEST_BOUNDS_DATA Second = { { "kubsan-test.c", 21, 5 }, &TestArrayType, &TestIntType };

    __ubsan_handle_out_of_bounds(&First, 4);
    ok_eq_hex((ULONG)First.Location.Column, KUBSAN_REPORTED | 5);
    ok_eq_hex((ULONG)Second.Location.Column, 5UL);

    __ubsan_handle_out_of_bounds(&Second, (ULONG_PTR)-1);
    ok_eq_hex((ULONG)Second.Location.Column, KUBSAN_REPORTED | 5);
}

static VOID TestRaisedIrql(VOID)
{
    TEST_OVERFLOW_DATA Dispatch = { { "kubsan-test.c", 30, 7 }, &TestIntType };
    TEST_OVERFLOW_DATA High = { { "kubsan-test.c", 31, 7 }, &TestIntType };
    TEST_OVERFLOW_DATA After = { { "kubsan-test.c", 32, 7 }, &TestIntType };
    KIRQL OldIrql;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    __ubsan_handle_add_overflow(&Dispatch, 0x7FFFFFFF, 1);
    KeLowerIrql(OldIrql);
    ok_eq_hex((ULONG)Dispatch.Location.Column, KUBSAN_REPORTED | 7);

    /* Above DISPATCH_LEVEL the report is queued, and the next report prints it */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    __ubsan_handle_add_overflow(&High, 0x7FFFFFFF, 1);
    KeLowerIrql(OldIrql);
    ok_eq_hex((ULONG)High.Location.Column, KUBSAN_REPORTED | 7);

    __ubsan_handle_add_overflow(&After, 0x7FFFFFFF, 1);
    ok_eq_hex((ULONG)After.Location.Column, KUBSAN_REPORTED | 7);
}

START_TEST(KubsanHandlers)
{
    TestReportsOnce();
    TestSitesAreIndependent();
    TestRaisedIrql();
}
