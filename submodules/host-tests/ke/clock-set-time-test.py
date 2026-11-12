# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A system time set on one processor survives the clock tick and the sleep resume on another
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'ntos/ke/clockint.c').read_text()


def body(name):
    start = source.index('\n' + name + '(')
    start = source.index('{', start)
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start + 1:end - 1]


fixture = r'''
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define VOID void
#define TRUE 1
#define FALSE 0
#define NTAPI
#define FASTCALL
#define APC_LEVEL 1
#define SYNCH_LEVEL 12
#define CLOCK_LEVEL 13
#define IPI_LEVEL 14
#define TIMER_TABLE_SIZE 4
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define FIELD_OFFSET(t, f) offsetof(t, f)
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define CHECK(x) do { if (!(x)) { printf("%d: %s failed\n", __LINE__, #x); exit(1); } } while (0)
#define ASSERT_IRQL_LESS_OR_EQUAL(x) ((void)0)
#define ASSERT(x) CHECK(x)
typedef unsigned char BOOLEAN, UCHAR, KIRQL;
typedef unsigned long ULONG;
typedef uint64_t ULONGLONG, KSPIN_LOCK, *PKSPIN_LOCK;
typedef int64_t LONGLONG;
typedef union { LONGLONG QuadPart; struct { uint32_t LowPart; int32_t HighPart; }; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct { uint32_t LowPart; int32_t High1Time; int32_t High2Time; } KSYSTEM_TIME;
typedef struct { KSYSTEM_TIME InterruptTime, SystemTime, TickCount; ULONG TickCountLowDeprecated; } KUSER_SHARED_DATA, *PKUSER_SHARED_DATA;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef union { ULONGLONG QuadPart; struct { uint32_t LowPart; uint32_t HighPart; }; } ULARGE_INTEGER;
typedef struct { struct { BOOLEAN Absolute, Inserted; UCHAR Hand; } Header; LIST_ENTRY TimerListEntry; ULARGE_INTEGER DueTime; } KTIMER, *PKTIMER;
typedef struct { LIST_ENTRY Entry; } KTIMER_TABLE_ENTRY;
typedef void *PKSPIN_LOCK_QUEUE;
static volatile KSYSTEM_TIME KiSystemTimeValue, KiInterruptTimeValue, KeTickCount;
static KSPIN_LOCK KiSystemTimeLock __attribute__((unused));
static KUSER_SHARED_DATA Shared, *MmWriteableSharedUserData = &Shared;
static KTIMER_TABLE_ENTRY KiTimerTableListHead[TIMER_TABLE_SIZE];
static LARGE_INTEGER KeBootTime;
static ULONGLONG KeBootTimeBias, KiInterruptTimeBias;
static BOOLEAN KiInterruptTimeFrozen, KiTimeAdjustmentEnabled;
static ULONG KeTimeAdjustment, KeMaximumIncrement = 156250;
static ULONGLONG KiTimeAdjustmentRemainder;
static __thread int OnClockOwner;
static volatile int Interleave, SetStarted, SetDone, LowHolder;
static LARGE_INTEGER NewTime = {.QuadPart = 0x01DC000000000000LL}, *SetTime = &NewTime;
static LARGE_INTEGER KiReadSystemTime(volatile KSYSTEM_TIME *Time) { LARGE_INTEGER r; r.QuadPart = *(volatile LONGLONG *)Time; return r; }
static void KiWriteSystemTime(volatile KSYSTEM_TIME *Time, LARGE_INTEGER Value) { Time->High2Time = Value.HighPart; __atomic_store_n((LONGLONG *)Time, Value.QuadPart, __ATOMIC_RELEASE); }
static ULONGLONG KiGetInterruptTime(void) { return (ULONGLONG)KiReadSystemTime(&KiInterruptTimeValue).QuadPart; }
static void KiWriteInterruptTime(LARGE_INTEGER Interrupt) { KiWriteSystemTime(&KiInterruptTimeValue, Interrupt); }
static void KiPublishSharedTime(ULONG Offset, LARGE_INTEGER Value) { (void)Offset; (void)Value; }
static void KiPublishSystemTimeWithBaseline(LARGE_INTEGER Interrupt, LARGE_INTEGER System) { (void)Interrupt; (void)System; }
static ULONG KiTakeElapsedTicks(volatile long *Offset, ULONG Increment) { (void)Offset; (void)Increment; return 0; }
static long KiTickOffset;
static volatile ULONG KiTickPhase;
static long long NowNs(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
/* The clock owner reads the time; another processor sets it before the owner writes its tick back */
static ULONGLONG KiGetSystemTime(void) {
    ULONGLONG Value = (ULONGLONG)KiReadSystemTime(&KiSystemTimeValue).QuadPart;
    if (OnClockOwner && Interleave) {
        long long Start = NowNs();
        Interleave = 0;
        __atomic_store_n(&SetStarted, 1, __ATOMIC_RELEASE);
        while (!__atomic_load_n(&SetDone, __ATOMIC_ACQUIRE) && NowNs() - Start < 50000000LL)
            sched_yield();
    }
    return Value;
}
static __thread KIRQL Irql;
__attribute__((unused)) static KIRQL KfRaiseIrql(KIRQL New) { KIRQL Old = Irql; CHECK(New >= Old); Irql = New; return Old; }
__attribute__((unused)) static KIRQL KeGetCurrentIrql(void) { return Irql; }
__attribute__((unused)) static void KeLowerIrql(KIRQL New) { Irql = New; }
/* The sleep IPI takes the lock, so a holder below IPI_LEVEL could be interrupted by it on its own processor */
__attribute__((unused)) static void KxAcquireSpinLock(PKSPIN_LOCK Lock) {
    if (Irql < IPI_LEVEL) LowHolder = 1;
    while (__atomic_exchange_n(Lock, 1, __ATOMIC_ACQUIRE)) sched_yield();
}
__attribute__((unused)) static void KxReleaseSpinLock(PKSPIN_LOCK Lock) { __atomic_store_n(Lock, 0, __ATOMIC_RELEASE); }
static KIRQL KiAcquireDispatcherLock(void) { return KfRaiseIrql(SYNCH_LEVEL); }
static void KiReleaseDispatcherLock(KIRQL Old) { KeLowerIrql(Old); }
static PKSPIN_LOCK_QUEUE KiAcquireTimerLock(ULONG Hand) { (void)Hand; return NULL; }
static void KiReleaseTimerLock(PKSPIN_LOCK_QUEUE Queue) { (void)Queue; }
static void InitializeListHead(PLIST_ENTRY Head) { Head->Flink = Head->Blink = Head; }
static BOOLEAN IsListEmpty(PLIST_ENTRY Head) { return Head->Flink == Head; }
static PLIST_ENTRY RemoveHeadList(PLIST_ENTRY Head) { PLIST_ENTRY e = Head->Flink; Head->Flink = e->Flink; e->Flink->Blink = Head; return e; }
static void InsertTailList(PLIST_ENTRY Head, PLIST_ENTRY e) { e->Flink = Head; e->Blink = Head->Blink; Head->Blink->Flink = e; Head->Blink = e; }
static void KiRemoveEntryTimer(PKTIMER Timer) { (void)Timer; CHECK(0); }
static ULONG KiComputeTimerTableIndex(ULONGLONG Due) { return (ULONG)(Due % TIMER_TABLE_SIZE); }
static void KiAcquireDispatcherObject(void *Header) { (void)Header; }
static void KxInsertTimer(PKTIMER Timer, ULONG Hand) { (void)Timer; (void)Hand; CHECK(0); }
static ULONGLONG KiAdjustedIncrement(ULONG Increment) { @ADJUSTED@ }
ULONGLONG KiUpdateSharedTime(ULONG Increment, BOOLEAN UpdateSystem) { @TICK@ }
void KeSetSystemTime(PLARGE_INTEGER NewSystemTime, PLARGE_INTEGER OldSystemTime, BOOLEAN FixInterruptTime, PLARGE_INTEGER HalTime) { @SET@ }
void KiResumeClockTime(ULONGLONG SleepTime) { @RESUME@ }
static LARGE_INTEGER Previous;
static void *Setter(void *Unused) {
    (void)Unused;
    while (!__atomic_load_n(&SetStarted, __ATOMIC_ACQUIRE))
        sched_yield();
    KeSetSystemTime(SetTime, &Previous, FALSE, NULL);
    __atomic_store_n(&SetDone, 1, __ATOMIC_RELEASE);
    return NULL;
}
int main(void) {
    pthread_t Thread;
    LARGE_INTEGER Start = {.QuadPart = 0x01D0000000000000LL};
    ULONG i;
    for (i = 0; i < TIMER_TABLE_SIZE; i++)
        InitializeListHead(&KiTimerTableListHead[i].Entry);
    KiWriteSystemTime(&KiSystemTimeValue, Start);
    KeBootTime.QuadPart = Start.QuadPart - 1000;
    LARGE_INTEGER Resumed = {.QuadPart = 0x01DD000000000000LL};
    int Failed = 0;
    OnClockOwner = 1;
    Irql = CLOCK_LEVEL;
    Interleave = 1;
    CHECK(pthread_create(&Thread, NULL, Setter, NULL) == 0);
    (void)KiUpdateSharedTime(156250, TRUE);
    CHECK(pthread_join(Thread, NULL) == 0);
    CHECK(Irql == CLOCK_LEVEL);
    if (KiReadSystemTime(&KiSystemTimeValue).QuadPart != NewTime.QuadPart)
    {
        printf("the clock tick put the old time back over the time that was set\n");
        Failed = 1;
    }
    else
    {
        /* The time set and the boot time moved by the same amount, measured from the tick */
        CHECK(Previous.QuadPart == Start.QuadPart + 156250);
        CHECK(KeBootTime.QuadPart == NewTime.QuadPart - 156250 - 1000);
    }

    /* Processor 0 resumes the clock in the sleep IPI while another processor, already released, sets the time */
    KiWriteSystemTime(&KiSystemTimeValue, Start);
    KiInterruptTimeFrozen = TRUE;
    SetTime = &Resumed;
    SetStarted = SetDone = 0;
    Irql = IPI_LEVEL;
    Interleave = 1;
    CHECK(pthread_create(&Thread, NULL, Setter, NULL) == 0);
    KiResumeClockTime(30000000);
    CHECK(pthread_join(Thread, NULL) == 0);
    CHECK(!KiInterruptTimeFrozen && KiInterruptTimeBias == 30000000);
    if (KiReadSystemTime(&KiSystemTimeValue).QuadPart != Resumed.QuadPart)
    {
        printf("the sleep resume put the old time back over the time that was set\n");
        Failed = 1;
    }
    else
    {
        CHECK(Previous.QuadPart == Start.QuadPart + 30000000);
    }

    if (LowHolder)
    {
        printf("the system time lock was taken below IPI_LEVEL\n");
        Failed = 1;
    }
    return Failed;
}
'''
fixture = fixture.replace('@TICK@', body('KiUpdateSharedTime')).replace('@SET@', body('KeSetSystemTime'))
fixture = fixture.replace('@RESUME@', body('KiResumeClockTime')).replace('@ADJUSTED@', body('KiAdjustedIncrement'))
with tempfile.TemporaryDirectory(prefix='ke-clock-set-time-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-missing-field-initializers', '-pthread', str(path / 'test.c'), '-o', str(path / 'test')],
                   check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
