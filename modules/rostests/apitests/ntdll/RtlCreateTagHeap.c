/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RtlCreateTagHeap
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(RtlCreateTagHeap)
{
    WCHAR Names[] = L"INIT\0SECOND\0";
    WCHAR Single[] = L"ONLY";
    RTL_HEAP_TAG_INFO Info;
    HANDLE Heap;
    ULONG Tag, Tag2;
    PWSTR Name;
    PVOID Block;

    Tag = RtlCreateTagHeap(RtlGetProcessHeap(), 0, L"PROBE!", Names);
    ok_long(Tag, 0);
    Tag2 = RtlCreateTagHeap(RtlGetProcessHeap(), 0, L"PROBE!", Single);
    ok_long(Tag2, 0);
    Tag2 = RtlCreateTagHeap(RtlGetProcessHeap(), 0, NULL, Single);
    ok_long(Tag2, 0);

    RtlZeroMemory(&Info, sizeof(Info));
    Name = RtlQueryTagHeap(RtlGetProcessHeap(), 0, (USHORT)Tag, FALSE, &Info);
    ok_ptr(Name, NULL);
    ok_long(Info.NumberOfAllocations, 0);

    Heap = RtlCreateHeap(HEAP_GROWABLE, NULL, 0, 0, NULL, NULL);
    ok(Heap != NULL, "RtlCreateHeap failed\n");
    if (!Heap)
        return;
    Tag = RtlCreateTagHeap(Heap, 0, L"PRIV!", Names);
    ok_long(Tag, 0);
    Block = RtlAllocateHeap(Heap, Tag << 18, 64);
    ok(Block != NULL, "Tagged allocation failed\n");
    RtlZeroMemory(&Info, sizeof(Info));
    Name = RtlQueryTagHeap(Heap, 0, (USHORT)Tag, FALSE, &Info);
    ok_ptr(Name, NULL);
    ok_long(Info.NumberOfAllocations, 0);
    if (Block)
        RtlFreeHeap(Heap, 0, Block);
    Tag = RtlCreateTagHeap(Heap, 0, L"PRIV!", L"");
    ok_long(Tag, 0);
    RtlDestroyHeap(Heap);

    Heap = RtlCreateHeap(HEAP_GROWABLE | HEAP_CREATE_ENABLE_TRACING, NULL, 0, 0, NULL, NULL);
    if (Heap)
    {
        Tag = RtlCreateTagHeap(Heap, 0, L"TRACE!", Names);
        ok_long(Tag, 0);
        RtlDestroyHeap(Heap);
    }
}
