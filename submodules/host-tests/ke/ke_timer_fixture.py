# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Shared host fixture for the kernel timer and system time tests
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import pathlib
import subprocess
import tempfile


class Sources:
    def __init__(self, root):
        root = pathlib.Path(root)
        self.text = {
            'ke_x.h': (root / 'ntos/include/internal/ke_x.h').read_text(),
            'ktimer.c': (root / 'ntos/ke/ktimer.c').read_text(),
            'clockint.c': (root / 'ntos/ke/clockint.c').read_text(),
            'waitobj.c': (root / 'ntos/ke/waitobj.c').read_text(),
            'dispatch.c': (root / 'ntos/ke/dispatch.c').read_text(),
        }

    def definition(self, file, name):
        """The whole definition of a function: its return type lines, signature and body."""
        source = self.text[file]
        index = source.index('\n' + name + '(')
        start = index
        while True:
            line_start = source.rfind('\n', 0, start) + 1
            line = source[line_start:start].strip()
            if line == '' or line.endswith('*/') or line.endswith(';') or line.endswith('}') or line.startswith('#') or line.startswith('//'):
                break
            start = line_start - 1
        start = start + 1 if source[start] == '\n' else start
        opening = source.index('{', index)
        depth = 1
        end = opening + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        return source[start:end] + '\n'

    def typedef(self, file, name):
        """A structure typedef, from its opening line to the names it declares."""
        source = self.text[file]
        start = source.index('typedef struct _' + name + '\n')
        end = source.index(';', source.index('} ' + name, start)) + 1
        return source[start:end] + '\n'

    def system_time_setter(self, pause=None):
        """KeSetSystemTime and the timer move it runs in its rendezvous; pause goes in before each absolute timer is put back."""
        change = self.definition('clockint.c', 'KiChangeSystemTime')
        if pause:
            needle = '        Entry = RemoveHeadList(&AbsoluteList);\n'
            assert change.count(needle) == 1
            change = change.replace(needle, '        ' + pause + '\n' + needle)
        return (self.typedef('clockint.c', 'KI_SYSTEM_TIME_CHANGE') + change +
                self.definition('clockint.c', 'KiSystemTimeChangeDpc') + self.definition('clockint.c', 'KeSetSystemTime'))


# The timer world every test builds on: one table bucket, object locks that are
# real mutexes, a rendezvous no processor above DISPATCH_LEVEL can be inside,
# and a clock the test moves by hand.
PRELUDE = r'''
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define VOID void
#define NTAPI
#define FASTCALL
#define FORCEINLINE static inline
#define IN
#define OUT
#define _In_
#define _In_opt_
#define _Out_
#define _Inout_
#define TRUE 1
#define FALSE 0
#define DBG 1
#define NOTHING
#define PASSIVE_LEVEL 0
#define APC_LEVEL 1
#define DISPATCH_LEVEL 2
#define SYNCH_LEVEL 12
#define CLOCK_LEVEL 13
#define IPI_LEVEL 14
#define TIMER_TABLE_SIZE 1
#define TIMER_WAIT_BLOCK 3
#define KernelMode 0
#define UserMode 1
#define STATUS_TIMEOUT 0x102
#define WaitAny 1
#define KI_TIMER_BOOST 1
#define KI_HUNDRED_NS_PER_MS 10000
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define FIELD_OFFSET(t, f) offsetof(t, f)
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define UlongToPtr(u) ((PVOID)(ULONG_PTR)(u))
#define YieldProcessor() sched_yield()
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
#define ASSERT_TIMER(x) ((void)(x))
#define ASSERT_IRQL_LESS_OR_EQUAL(x) ((void)0)
#define max(a, b) ((a) > (b) ? (a) : (b))
#define Int32x32To64(a, b) ((LONGLONG)(int32_t)(a) * (LONGLONG)(int32_t)(b))
typedef unsigned char BOOLEAN, *PBOOLEAN, UCHAR, KIRQL, KPROCESSOR_MODE;
typedef unsigned short USHORT;
typedef unsigned long ULONG, *PULONG;
typedef long LONG;
typedef uint64_t ULONGLONG, *PULONGLONG, ULONG_PTR, KSPIN_LOCK, *PKSPIN_LOCK;
typedef int64_t LONGLONG, LONG_PTR;
typedef void *PVOID;
typedef union { LONGLONG QuadPart; struct { uint32_t LowPart; int32_t HighPart; }; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef union { ULONGLONG QuadPart; struct { uint32_t LowPart; uint32_t HighPart; }; } ULARGE_INTEGER;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct { uint32_t LowPart; int32_t High1Time; int32_t High2Time; } KSYSTEM_TIME;
typedef struct { KSYSTEM_TIME SystemTime; } KUSER_SHARED_DATA;
typedef struct { BOOLEAN Absolute, Inserted; UCHAR Hand, EncodedTolerableDelay; LONG SignalState; LIST_ENTRY WaitListHead; pthread_mutex_t Lock; } DISPATCHER_HEADER, *PDISPATCHER_HEADER;
typedef struct _KDPC { int Type; ULONG Number; void (*DeferredRoutine)(struct _KDPC *, PVOID, PVOID, PVOID); PVOID DeferredContext; ULONG ProcessorHistory; } KDPC, *PKDPC;
typedef struct { DISPATCHER_HEADER Header; ULARGE_INTEGER DueTime; LIST_ENTRY TimerListEntry; PKDPC Dpc; LONG Period; } KTIMER, *PKTIMER;
typedef struct { LIST_ENTRY Entry; ULARGE_INTEGER Time; } KTIMER_TABLE_ENTRY, *PKTIMER_TABLE_ENTRY;
typedef pthread_mutex_t *PKSPIN_LOCK_QUEUE;
static const char *Scenario = "setup";
static KTIMER_TABLE_ENTRY KiTimerTableListHead[TIMER_TABLE_SIZE];
static pthread_mutex_t Bucket = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t Rendezvous = PTHREAD_RWLOCK_INITIALIZER;
static __thread KIRQL Irql;
static __thread int ObjectLocks, BucketWithObject, InRendezvous;
static volatile int DispatcherContended;
static volatile KSYSTEM_TIME KiSystemTimeValue;
static KSPIN_LOCK KiSystemTimeLock __attribute__((unused));
static LARGE_INTEGER KeBootTime;
static ULONGLONG KeBootTimeBias;
static ULONGLONG InterruptTimeNow = 100;
static ULONG KeMaximumIncrement __attribute__((unused)) = 100000, KeTimeIncrement __attribute__((unused)) = 100000, KiTickPhase __attribute__((unused));
static void InitializeListHead(PLIST_ENTRY h) { h->Flink = h->Blink = h; }
static BOOLEAN IsListEmpty(PLIST_ENTRY h) { return h->Flink == h; }
static void InsertTailList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Flink = h; e->Blink = h->Blink; h->Blink->Flink = e; h->Blink = e; }
static void InsertHeadList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Flink = h->Flink; e->Blink = h; h->Flink->Blink = e; h->Flink = e; }
static BOOLEAN RemoveEntryList(PLIST_ENTRY e) { PLIST_ENTRY b = e->Blink, f = e->Flink; b->Flink = f; f->Blink = b; return f == b; }
static PLIST_ENTRY RemoveHeadList(PLIST_ENTRY h) { PLIST_ENTRY e = h->Flink; RemoveEntryList(e); return e; }
static KIRQL KeGetCurrentIrql(void) { return Irql; }
/* A DPC cannot run on a processor at DISPATCH_LEVEL or above, so a rendezvous of every processor's DPC waits for each of them to come back below it */
static KIRQL KfRaiseIrql(KIRQL New) {
    KIRQL Old = Irql;
    CHECK(New >= Old);
    if (Old < DISPATCH_LEVEL && New >= DISPATCH_LEVEL && !InRendezvous)
        CHECK(pthread_rwlock_rdlock(&Rendezvous) == 0);
    Irql = New;
    return Old;
}
static void KeLowerIrql(KIRQL New) {
    CHECK(New <= Irql);
    if (Irql >= DISPATCH_LEVEL && New < DISPATCH_LEVEL && !InRendezvous)
        CHECK(pthread_rwlock_unlock(&Rendezvous) == 0);
    Irql = New;
}
__attribute__((unused)) static KIRQL KiEnterDispatcher(void) { return KfRaiseIrql(SYNCH_LEVEL); }
__attribute__((unused)) static void KiExitDispatcher(KIRQL Old) { KeLowerIrql(Old); }
static void KiAcquireDispatcherObject(PDISPATCHER_HEADER h) { CHECK(pthread_mutex_lock(&h->Lock) == 0); ObjectLocks++; }
__attribute__((unused)) static BOOLEAN KiTryAcquireDispatcherObject(PDISPATCHER_HEADER h) { if (pthread_mutex_trylock(&h->Lock) != 0) return FALSE; ObjectLocks++; return TRUE; }
static void KiReleaseDispatcherObject(PDISPATCHER_HEADER h) { CHECK(ObjectLocks > 0); ObjectLocks--; CHECK(pthread_mutex_unlock(&h->Lock) == 0); }
__attribute__((unused)) static BOOLEAN KiIsDispatcherObjectLocked(PDISPATCHER_HEADER h) { (void)h; return ObjectLocks > 0; }
static void (*BucketHook)(void);
/* Whether an object lock came first is kept for the insertion checks */
static PKSPIN_LOCK_QUEUE KiAcquireTimerLock(ULONG Hand) {
    CHECK(Hand < TIMER_TABLE_SIZE && Irql >= DISPATCH_LEVEL);
    BucketWithObject = ObjectLocks > 0;
    if (BucketHook) BucketHook();
    CHECK(pthread_mutex_lock(&Bucket) == 0);
    return &Bucket;
}
static void KiReleaseTimerLock(PKSPIN_LOCK_QUEUE Queue) { CHECK(pthread_mutex_unlock(Queue) == 0); }
/* The architecture hook a timer insertion calls; the x86 one does nothing */
#define KiClockTimerInserted(DueTime) ((void)(DueTime))
static ULONG KiComputeTimerTableIndex(ULONGLONG DueTime) { (void)DueTime; return 0; }
__attribute__((unused)) static void KxAcquireSpinLock(PKSPIN_LOCK Lock) { while (__atomic_exchange_n(Lock, 1, __ATOMIC_ACQUIRE)) sched_yield(); }
__attribute__((unused)) static void KxReleaseSpinLock(PKSPIN_LOCK Lock) { __atomic_store_n(Lock, 0, __ATOMIC_RELEASE); }
static LARGE_INTEGER ReadTime(volatile KSYSTEM_TIME *Time) { LARGE_INTEGER r; r.QuadPart = __atomic_load_n((volatile LONGLONG *)Time, __ATOMIC_ACQUIRE); return r; }
__attribute__((unused)) static void KiWriteSystemTime(volatile KSYSTEM_TIME *Time, LARGE_INTEGER Value) { Time->High2Time = Value.HighPart; __atomic_store_n((volatile LONGLONG *)Time, Value.QuadPart, __ATOMIC_RELEASE); }
__attribute__((unused)) static ULONGLONG KiGetSystemTime(void) { return (ULONGLONG)ReadTime(&KiSystemTimeValue).QuadPart; }
__attribute__((unused)) static void KiPublishSharedTime(ULONG Offset, LARGE_INTEGER Value) { (void)Offset; (void)Value; }
__attribute__((unused)) static ULONGLONG KiQueryInterruptTimePrecise(PVOID Counter, BOOLEAN Unbiased) { (void)Counter; (void)Unbiased; return InterruptTimeNow; }
static void SetClock(LONGLONG SystemTime, ULONGLONG InterruptTime) {
    LARGE_INTEGER t; t.QuadPart = SystemTime;
    KiWriteSystemTime(&KiSystemTimeValue, t);
    InterruptTimeNow = InterruptTime;
}
static void InitTable(void) { ULONG i; for (i = 0; i < TIMER_TABLE_SIZE; i++) { InitializeListHead(&KiTimerTableListHead[i].Entry); KiTimerTableListHead[i].Time.QuadPart = ~0ULL; } }
static void InitTimer(PKTIMER Timer) {
    *Timer = (KTIMER){0};
    CHECK(pthread_mutex_init(&Timer->Header.Lock, NULL) == 0);
    InitializeListHead(&Timer->Header.WaitListHead);
}
typedef struct { ULONG Barrier; ULONG TotalProcessors; } DEFERRED_REVERSE_BARRIER;
/* One processor stands for all of them: it runs the routine once everybody else is below DISPATCH_LEVEL */
__attribute__((unused)) static void KeGenericCallDpc(void (*Routine)(PKDPC, PVOID, PVOID, PVOID), PVOID Context) {
    volatile LONG Count = 1;
    DEFERRED_REVERSE_BARRIER Barrier = {1, 1};
    KIRQL Old = Irql;
    CHECK(Irql <= APC_LEVEL);
    if (pthread_rwlock_trywrlock(&Rendezvous) != 0) {
        __atomic_store_n(&DispatcherContended, 1, __ATOMIC_RELEASE);
        CHECK(pthread_rwlock_wrlock(&Rendezvous) == 0);
    }
    InRendezvous = 1;
    Irql = DISPATCH_LEVEL;
    Routine(NULL, Context, (PVOID)&Count, &Barrier);
    CHECK(Irql == DISPATCH_LEVEL && Count == 0);
    Irql = Old;
    InRendezvous = 0;
    CHECK(pthread_rwlock_unlock(&Rendezvous) == 0);
}
__attribute__((unused)) static BOOLEAN KeSignalCallDpcSynchronize(PVOID Barrier) { (void)Barrier; return TRUE; }
__attribute__((unused)) static void KeSignalCallDpcDone(PVOID Count) { __atomic_sub_fetch((volatile LONG *)Count, 1, __ATOMIC_SEQ_CST); }
static long long NowNs(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
/* Waits up to 50 ms for a flag another thread sets: long enough to see it run, short enough to keep the test fast */
static int AwaitFlag(volatile int *Flag) {
    long long Start = NowNs();
    while (!__atomic_load_n(Flag, __ATOMIC_ACQUIRE) && NowNs() - Start < 50000000LL)
        sched_yield();
    return __atomic_load_n(Flag, __ATOMIC_ACQUIRE);
}
'''


def run(name, fixture, cc):
    with tempfile.TemporaryDirectory(prefix='ke-' + name + '-') as tmp:
        path = pathlib.Path(tmp)
        (path / 'test.c').write_text(fixture)
        subprocess.run([cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                        '-Wno-unused-function', '-Wno-unused-variable', '-Wno-sign-compare', '-Wno-missing-field-initializers',
                        '-pthread', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True, timeout=10)
