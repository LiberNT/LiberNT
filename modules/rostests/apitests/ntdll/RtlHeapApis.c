/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RtlEnumProcessHeaps, RtlValidateProcessHeaps and RtlQueryProcessHeapInformation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define MAX_TEST_HEAPS 64

typedef struct _HEAP_ENUM_CONTEXT
{
    ULONG Count;
    ULONG FailAt;
    PVOID Heaps[MAX_TEST_HEAPS];
} HEAP_ENUM_CONTEXT, *PHEAP_ENUM_CONTEXT;

static
NTSTATUS
NTAPI
EnumHeap(
    _In_ PVOID HeapHandle,
    _In_ PVOID Parameter)
{
    PHEAP_ENUM_CONTEXT Context = Parameter;

    if (Context->Count < MAX_TEST_HEAPS)
        Context->Heaps[Context->Count] = HeapHandle;
    Context->Count++;

    return Context->Count == Context->FailAt ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}

static
BOOLEAN
ContextHasHeap(
    _In_ PHEAP_ENUM_CONTEXT Context,
    _In_ PVOID Heap)
{
    ULONG Index;

    for (Index = 0; Index < min(Context->Count, MAX_TEST_HEAPS); Index++)
    {
        if (Context->Heaps[Index] == Heap)
            return TRUE;
    }

    return FALSE;
}

static
VOID
TestEnumProcessHeaps(VOID)
{
    HEAP_ENUM_CONTEXT Context;
    HANDLE Heaps[MAX_TEST_HEAPS];
    ULONG NumberOfHeaps;
    ULONG Index;
    PVOID Heap;

    NumberOfHeaps = RtlGetProcessHeaps(MAX_TEST_HEAPS, Heaps);
    ok(NumberOfHeaps != 0 && NumberOfHeaps <= MAX_TEST_HEAPS, "RtlGetProcessHeaps returned %lu\n", NumberOfHeaps);
    if (NumberOfHeaps == 0 || NumberOfHeaps > MAX_TEST_HEAPS)
        return;

    RtlZeroMemory(&Context, sizeof(Context));
    ok_eq_hex(RtlEnumProcessHeaps(EnumHeap, &Context), STATUS_SUCCESS);
    ok_eq_ulong(Context.Count, NumberOfHeaps);
    for (Index = 0; Index < min(Context.Count, NumberOfHeaps); Index++)
        ok(Context.Heaps[Index] == Heaps[Index], "Heap %lu: %p, expected %p\n", Index, Context.Heaps[Index], Heaps[Index]);
    ok_eq_pointer(Context.Heaps[0], RtlGetProcessHeap());

    RtlZeroMemory(&Context, sizeof(Context));
    Context.FailAt = 1;
    ok_eq_hex(RtlEnumProcessHeaps(EnumHeap, &Context), STATUS_UNSUCCESSFUL);
    ok_eq_ulong(Context.Count, 1);

    Heap = RtlCreateHeap(HEAP_GROWABLE, NULL, 0, 0, NULL, NULL);
    ok(Heap != NULL, "RtlCreateHeap failed\n");
    if (Heap == NULL)
        return;

    RtlZeroMemory(&Context, sizeof(Context));
    ok_eq_hex(RtlEnumProcessHeaps(EnumHeap, &Context), STATUS_SUCCESS);
    ok_eq_ulong(Context.Count, NumberOfHeaps + 1);
    ok(ContextHasHeap(&Context, Heap), "New heap %p was not enumerated\n", Heap);

    ok_eq_pointer(RtlDestroyHeap(Heap), NULL);

    RtlZeroMemory(&Context, sizeof(Context));
    ok_eq_hex(RtlEnumProcessHeaps(EnumHeap, &Context), STATUS_SUCCESS);
    ok_eq_ulong(Context.Count, NumberOfHeaps);
}

static
VOID
TestValidateProcessHeaps(VOID)
{
    PVOID Heap;
    PVOID Block;

    ok_eq_int(RtlValidateProcessHeaps(), TRUE);

    Heap = RtlCreateHeap(HEAP_GROWABLE, NULL, 0, 0, NULL, NULL);
    ok(Heap != NULL, "RtlCreateHeap failed\n");
    if (Heap == NULL)
        return;

    Block = RtlAllocateHeap(Heap, 0, 0x40);
    ok(Block != NULL, "RtlAllocateHeap failed\n");
    ok_eq_int(RtlValidateProcessHeaps(), TRUE);
    if (Block != NULL)
        ok_eq_int(RtlFreeHeap(Heap, 0, Block), TRUE);
    ok_eq_pointer(RtlDestroyHeap(Heap), NULL);
}

static
VOID
TestQueryProcessHeapInformation(VOID)
{
    PRTL_DEBUG_INFORMATION Buffer;
    ULONG NumberOfHeaps;
    NTSTATUS Status;

    Buffer = RtlCreateQueryDebugBuffer(0, FALSE);
    ok(Buffer != NULL, "RtlCreateQueryDebugBuffer failed\n");
    if (Buffer == NULL)
        return;

    NumberOfHeaps = RtlGetProcessHeaps(0, NULL);
    Status = RtlQueryProcessHeapInformation(Buffer);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Buffer->Heaps != NULL, "No heap information\n");
    if (NT_SUCCESS(Status) && Buffer->Heaps != NULL)
    {
        ok_eq_ulong(Buffer->Heaps->NumberOfHeaps, NumberOfHeaps);
        ok_eq_pointer(Buffer->Heaps->Heaps[0].BaseAddress, RtlGetProcessHeap());
        ok(Buffer->Heaps->Heaps[0].BytesCommitted != 0, "Process heap reports no committed bytes\n");
        ok(Buffer->Heaps->Heaps[0].BytesCommitted >= Buffer->Heaps->Heaps[0].BytesAllocated,
           "Committed 0x%I64x < allocated 0x%I64x\n", (ULONGLONG)Buffer->Heaps->Heaps[0].BytesCommitted,
           (ULONGLONG)Buffer->Heaps->Heaps[0].BytesAllocated);
    }

    ok_eq_hex(RtlDestroyQueryDebugBuffer(Buffer), STATUS_SUCCESS);
}

START_TEST(RtlHeapApis)
{
    TestEnumProcessHeaps();
    TestValidateProcessHeaps();
    TestQueryProcessHeapInformation();
}
