/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stress the low-fragmentation heap from several threads and check what HeapLock holds back
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define LFH_TEST_THREADS 4
#define LFH_TEST_SLOTS 256
#define LFH_TEST_ROUNDS 20000
#define LFH_TEST_MAX_SIZE 1000

static HANDLE TestHeap;
static PVOID volatile Slots[LFH_TEST_SLOTS];
static volatile LONG Corrupted;
static volatile LONG Failed;
static volatile LONG Done;

static
UCHAR
PatternByte(
    _In_ ULONG Size,
    _In_ ULONG Offset)
{
    return (UCHAR)(Size * 31 + Offset * 7);
}

static
PVOID
AllocateBlock(
    _Inout_ PULONG Seed)
{
    ULONG Size, Offset;
    PUCHAR Block;

    *Seed = *Seed * 1103515245 + 12345;
    Size = sizeof(ULONG) + (*Seed >> 8) % (LFH_TEST_MAX_SIZE - sizeof(ULONG));
    Block = HeapAlloc(TestHeap, 0, Size);
    if (!Block)
    {
        InterlockedIncrement(&Failed);
        return NULL;
    }

    *(PULONG)Block = Size;
    for (Offset = sizeof(ULONG); Offset < Size; Offset++)
        Block[Offset] = PatternByte(Size, Offset);
    return Block;
}

static
VOID
FreeBlock(
    _In_ PUCHAR Block)
{
    ULONG Size = *(PULONG)Block, Offset;

    if (Size < sizeof(ULONG) || Size >= LFH_TEST_MAX_SIZE || HeapSize(TestHeap, 0, Block) < Size)
    {
        InterlockedIncrement(&Corrupted);
        return;
    }

    for (Offset = sizeof(ULONG); Offset < Size; Offset++)
    {
        if (Block[Offset] != PatternByte(Size, Offset))
        {
            InterlockedIncrement(&Corrupted);
            break;
        }
    }

    if (!HeapFree(TestHeap, 0, Block))
        InterlockedIncrement(&Failed);
}

static
DWORD
WINAPI
StressThread(
    _In_ PVOID Parameter)
{
    ULONG Seed = (ULONG)(ULONG_PTR)Parameter * 2654435761u + 1;
    ULONG Round, Slot;
    PVOID Block;

    for (Round = 0; Round < LFH_TEST_ROUNDS; Round++)
    {
        Seed = Seed * 1103515245 + 12345;
        Slot = (Seed >> 8) % LFH_TEST_SLOTS;
        Block = InterlockedExchangePointer((PVOID *)&Slots[Slot], NULL);
        if (Block)
        {
            FreeBlock(Block);
            continue;
        }

        Block = AllocateBlock(&Seed);
        if (Block && InterlockedCompareExchangePointer((PVOID *)&Slots[Slot], Block, NULL))
            FreeBlock(Block);
    }
    return 0;
}

static
DWORD
WINAPI
AllocateWhileLocked(
    _In_ PVOID Parameter)
{
    PVOID Block = HeapAlloc(TestHeap, 0, 24);

    InterlockedExchange(&Done, Block != NULL);
    HeapFree(TestHeap, 0, Block);
    return 0;
}

static
DWORD
WINAPI
FreeWhileLocked(
    _In_ PVOID Parameter)
{
    InterlockedExchange(&Done, HeapFree(TestHeap, 0, Parameter));
    return 0;
}

static
VOID
TestWhileLocked(
    _In_ LPTHREAD_START_ROUTINE Routine,
    _In_opt_ PVOID Parameter,
    _In_ PCSTR Operation)
{
    HANDLE Thread;

    Done = 0;
    ok(HeapLock(TestHeap), "HeapLock failed: %lu\n", GetLastError());
    Thread = CreateThread(NULL, 0, Routine, Parameter, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "%s waited for the heap lock\n", Operation);
    ok(Done == 1, "%s failed while the heap was locked\n", Operation);
    ok(HeapUnlock(TestHeap), "HeapUnlock failed: %lu\n", GetLastError());
    WaitForSingleObject(Thread, INFINITE);
    CloseHandle(Thread);
}

START_TEST(HeapLowFragmentation)
{
    ULONG Compatibility = 2, Information = 0, Index;
    HANDLE Threads[LFH_TEST_THREADS];
    PVOID Warm[64];

    TestHeap = HeapCreate(0, 0, 0);
    ok(TestHeap != NULL, "HeapCreate failed: %lu\n", GetLastError());
    if (!TestHeap) return;

    ok(HeapSetInformation(TestHeap, HeapCompatibilityInformation, &Compatibility, sizeof(Compatibility)),
       "HeapSetInformation failed: %lu\n", GetLastError());
    for (Index = 0; Index < RTL_NUMBER_OF(Warm); Index++)
        Warm[Index] = HeapAlloc(TestHeap, 0, 24);
    ok(HeapQueryInformation(TestHeap, HeapCompatibilityInformation, &Information, sizeof(Information), NULL),
       "HeapQueryInformation failed: %lu\n", GetLastError());
    ok(Information == 2, "HeapCompatibilityInformation is %lu\n", Information);

    TestWhileLocked(AllocateWhileLocked, NULL, "A low-fragmentation heap allocation");
    TestWhileLocked(FreeWhileLocked, Warm[RTL_NUMBER_OF(Warm) - 1], "A low-fragmentation heap free");
    Warm[RTL_NUMBER_OF(Warm) - 1] = NULL;

    for (Index = 0; Index < LFH_TEST_THREADS; Index++)
    {
        Threads[Index] = CreateThread(NULL, 0, StressThread, (PVOID)(ULONG_PTR)(Index + 1), 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread failed: %lu\n", GetLastError());
    }
    ok(WaitForMultipleObjects(LFH_TEST_THREADS, Threads, TRUE, 120000) == WAIT_OBJECT_0,
       "the stress threads did not finish\n");
    for (Index = 0; Index < LFH_TEST_THREADS; Index++)
        CloseHandle(Threads[Index]);

    for (Index = 0; Index < LFH_TEST_SLOTS; Index++)
    {
        if (Slots[Index])
            FreeBlock(Slots[Index]);
        Slots[Index] = NULL;
    }
    ok(Corrupted == 0, "%ld blocks were corrupted\n", Corrupted);
    ok(Failed == 0, "%ld allocations or frees failed\n", Failed);
    ok(HeapValidate(TestHeap, 0, NULL), "HeapValidate failed\n");

    for (Index = 0; Index < RTL_NUMBER_OF(Warm); Index++)
        HeapFree(TestHeap, 0, Warm[Index]);
    ok(HeapDestroy(TestHeap), "HeapDestroy failed: %lu\n", GetLastError());
}
