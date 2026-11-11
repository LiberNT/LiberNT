/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for HeapSummary
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *FN_HeapSummary)(HANDLE, DWORD, LPHEAP_SUMMARY);

START_TEST(HeapSummary)
{
    FN_HeapSummary pHeapSummary;
    HEAP_SUMMARY Summary;
    HANDLE Heap;
    PVOID Freed;
    BOOL Result;

    pHeapSummary = (FN_HeapSummary)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "HeapSummary");
    ok(pHeapSummary != NULL, "HeapSummary is missing\n");
    if (pHeapSummary == NULL)
        return;

    Heap = HeapCreate(0, 0, 0);
    ok(Heap != NULL, "HeapCreate failed with %lu\n", GetLastError());
    if (Heap == NULL)
        return;

    ok(HeapAlloc(Heap, 0, 0x100) != NULL, "HeapAlloc(0x100) failed\n");
    ok(HeapAlloc(Heap, 0, 0x200) != NULL, "HeapAlloc(0x200) failed\n");
    Freed = HeapAlloc(Heap, 0, 0x300);
    ok(Freed != NULL, "HeapAlloc(0x300) failed\n");
    ok(HeapAlloc(Heap, 0, 0x60000) != NULL, "HeapAlloc(0x60000) failed\n");
    ok_eq_bool(HeapFree(Heap, 0, Freed), TRUE);

    RtlZeroMemory(&Summary, sizeof(Summary));
    SetLastError(0xdeadbeef);
    Result = pHeapSummary(Heap, 0, &Summary);
    ok_eq_bool(Result, FALSE);
    ok_eq_ulong(GetLastError(), ERROR_INVALID_PARAMETER);

    Summary.cb = sizeof(Summary) + 1;
    SetLastError(0xdeadbeef);
    Result = pHeapSummary(Heap, 0, &Summary);
    ok_eq_bool(Result, FALSE);
    ok_eq_ulong(GetLastError(), ERROR_INVALID_PARAMETER);

    Summary.cb = sizeof(Summary);
    SetLastError(0xdeadbeef);
    Result = pHeapSummary(Heap, 0, &Summary);
    ok_eq_bool(Result, TRUE);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);
    ok_eq_ulong(Summary.cb, sizeof(Summary));
    ok(Summary.cbAllocated == 0x100 + 0x200 + 0x60000, "cbAllocated 0x%I64x\n", (ULONGLONG)Summary.cbAllocated);
    ok(Summary.cbCommitted >= Summary.cbAllocated, "cbCommitted 0x%I64x < cbAllocated 0x%I64x\n",
       (ULONGLONG)Summary.cbCommitted, (ULONGLONG)Summary.cbAllocated);
    ok(Summary.cbReserved >= Summary.cbCommitted, "cbReserved 0x%I64x < cbCommitted 0x%I64x\n",
       (ULONGLONG)Summary.cbReserved, (ULONGLONG)Summary.cbCommitted);

    ok_eq_bool(HeapDestroy(Heap), TRUE);

    RtlZeroMemory(&Summary, sizeof(Summary));
    Summary.cb = sizeof(Summary);
    Result = pHeapSummary(GetProcessHeap(), 0, &Summary);
    ok_eq_bool(Result, TRUE);
    ok(Summary.cbAllocated != 0, "Process heap reports no allocated bytes\n");
    ok(Summary.cbCommitted >= Summary.cbAllocated, "cbCommitted 0x%I64x < cbAllocated 0x%I64x\n",
       (ULONGLONG)Summary.cbCommitted, (ULONGLONG)Summary.cbAllocated);
    ok(Summary.cbReserved >= Summary.cbCommitted, "cbReserved 0x%I64x < cbCommitted 0x%I64x\n",
       (ULONGLONG)Summary.cbReserved, (ULONGLONG)Summary.cbCommitted);
}
