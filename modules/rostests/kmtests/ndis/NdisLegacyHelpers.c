/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS list, read-write lock and periodic timer tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndis.h>

#undef NdisInterlockedPushEntryList
#undef NdisInterlockedPopEntryList

__declspec(dllimport) PSINGLE_LIST_ENTRY NTAPI NdisInterlockedPushEntryList(PSINGLE_LIST_ENTRY, PSINGLE_LIST_ENTRY, PNDIS_SPIN_LOCK);
__declspec(dllimport) PSINGLE_LIST_ENTRY NTAPI NdisInterlockedPopEntryList(PSINGLE_LIST_ENTRY, PNDIS_SPIN_LOCK);
__declspec(dllimport) VOID NTAPI NdisDprAcquireReadWriteLock(PNDIS_RW_LOCK, BOOLEAN, PLOCK_STATE);
__declspec(dllimport) VOID NTAPI NdisDprReleaseReadWriteLock(PNDIS_RW_LOCK, PLOCK_STATE);

static volatile LONG TestTimerCount;

static
VOID
NTAPI
TestTimerFunction(
    PVOID SystemSpecific1,
    PVOID FunctionContext,
    PVOID SystemSpecific2,
    PVOID SystemSpecific3)
{
    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(FunctionContext);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);
    InterlockedIncrement(&TestTimerCount);
}

static
VOID
TestDelay(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

START_TEST(NdisLegacyHelpers)
{
    SINGLE_LIST_ENTRY Head, First, Second;
    PSINGLE_LIST_ENTRY Entry;
    NDIS_SPIN_LOCK SpinLock;
    NDIS_RW_LOCK RwLock;
    LOCK_STATE LockState;
    NDIS_TIMER Timer;
    BOOLEAN Cancelled;
    LONG Count;
    KIRQL OldIrql;

    NdisAllocateSpinLock(&SpinLock);
    Head.Next = NULL;
    Entry = NdisInterlockedPushEntryList(&Head, &First, &SpinLock);
    ok_eq_pointer(Entry, NULL);
    Entry = NdisInterlockedPushEntryList(&Head, &Second, &SpinLock);
    ok_eq_pointer(Entry, &First);
    ok_eq_pointer(Head.Next, &Second);
    Entry = NdisInterlockedPopEntryList(&Head, &SpinLock);
    ok_eq_pointer(Entry, &Second);
    Entry = NdisInterlockedPopEntryList(&Head, &SpinLock);
    ok_eq_pointer(Entry, &First);
    Entry = NdisInterlockedPopEntryList(&Head, &SpinLock);
    ok_eq_pointer(Entry, NULL);
    NdisFreeSpinLock(&SpinLock);

    NdisInitializeReadWriteLock(&RwLock);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    NdisDprAcquireReadWriteLock(&RwLock, FALSE, &LockState);
    ok_irql(DISPATCH_LEVEL);
    NdisDprReleaseReadWriteLock(&RwLock, &LockState);
    ok_irql(DISPATCH_LEVEL);
    NdisDprAcquireReadWriteLock(&RwLock, TRUE, &LockState);
    ok_irql(DISPATCH_LEVEL);
    NdisDprReleaseReadWriteLock(&RwLock, &LockState);
    ok_irql(DISPATCH_LEVEL);
    KeLowerIrql(OldIrql);

    TestTimerCount = 0;
    NdisInitializeTimer(&Timer, TestTimerFunction, NULL);
    NdisSetPeriodicTimer(&Timer, 20);
    TestDelay(250);
    NdisCancelTimer(&Timer, &Cancelled);
    ok(Cancelled, "periodic timer was not pending\n");
    Count = TestTimerCount;
    ok(Count >= 3, "periodic timer fired %ld times\n", Count);
    TestDelay(100);
    ok_eq_long(TestTimerCount, Count);
}
