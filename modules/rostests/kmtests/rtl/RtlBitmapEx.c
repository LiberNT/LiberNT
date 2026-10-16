/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     64-bit RTL_BITMAP_EX routine tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef struct _TEST_RTL_BITMAP_EX
{
    ULONG64 SizeOfBitMap;
    PULONG64 Buffer;
} TEST_RTL_BITMAP_EX, *PTEST_RTL_BITMAP_EX;

typedef BOOLEAN (NTAPI *PFN_RTL_TEST_BIT_EX)(PTEST_RTL_BITMAP_EX, ULONG64);
typedef ULONG64 (NTAPI *PFN_RTL_FIND_CLEAR_BITS_AND_SET_EX)(PTEST_RTL_BITMAP_EX, ULONG64, ULONG64);

static
PVOID
TestGetRoutine(
    _In_ PCWSTR Name)
{
    UNICODE_STRING RoutineName;

    RtlInitUnicodeString(&RoutineName, Name);
    return MmGetSystemRoutineAddress(&RoutineName);
}

START_TEST(RtlBitmapEx)
{
    PFN_RTL_TEST_BIT_EX pTestBit = TestGetRoutine(L"RtlTestBitEx");
    PFN_RTL_FIND_CLEAR_BITS_AND_SET_EX pFindAndSet = TestGetRoutine(L"RtlFindClearBitsAndSetEx");
    TEST_RTL_BITMAP_EX Bitmap;
    ULONG64 Buffer[4];
    ULONG64 Index;

    if (!pTestBit || !pFindAndSet)
    {
        skip(FALSE, "RTL_BITMAP_EX exports not available\n");
        return;
    }

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Bitmap.SizeOfBitMap = 256;
    Bitmap.Buffer = Buffer;

    Buffer[1] = 1ULL << 5;
    Buffer[3] = 1ULL << 63;
    ok_eq_uint(pTestBit(&Bitmap, 69), TRUE);
    ok_eq_uint(pTestBit(&Bitmap, 68), FALSE);
    ok_eq_uint(pTestBit(&Bitmap, 70), FALSE);
    ok_eq_uint(pTestBit(&Bitmap, 255), TRUE);
    ok_eq_uint(pTestBit(&Bitmap, 0), FALSE);

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Buffer[0] = 0x3FF;
    Index = pFindAndSet(&Bitmap, 4, 0);
    ok_eq_ulonglong(Index, 10ULL);
    ok_eq_ulonglong(Buffer[0], 0x3FFFULL);

    Index = pFindAndSet(&Bitmap, 2, 100);
    ok_eq_ulonglong(Index, 100ULL);
    ok_eq_ulonglong(Buffer[1], 3ULL << 36);

    Index = pFindAndSet(&Bitmap, 70, 0);
    ok_eq_ulonglong(Index, 14ULL);
    ok_eq_ulonglong(Buffer[0], 0xFFFFFFFFFFFFFFFFULL);
    ok_eq_ulonglong(Buffer[1], (3ULL << 36) | 0xFFFFFULL);
    ok_eq_ulonglong(Buffer[2], 0ULL);
    ok_eq_ulonglong(Buffer[3], 0ULL);

    Index = pFindAndSet(&Bitmap, 300, 0);
    ok_eq_ulonglong(Index, 0xFFFFFFFFFFFFFFFFULL);
}
