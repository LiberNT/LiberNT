# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Verifies KiSetPriorityThread serializes with KiDeferredReadyThread and KiBalanceReadyQueues so queued threads never mismatch their ready queue priority
# COPYRIGHT:   Copyright 2026 LiberNT Contributors
import argparse
import pathlib
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
import ke_timer_fixture as kt

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--seconds', type=float, default=1.0)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()

root = pathlib.Path(args.root)
src = kt.Sources(args.root)
src.text['schedule.c'] = (root / 'ntos/ke/schedule.c').read_text()

prelude = r'''
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VOID void
#define NTAPI
#define FASTCALL
#define FORCEINLINE static inline
#define IN
#define OUT
#define _In_
#define _In_opt_
#define _Inout_
#define _Out_
#define _Out_opt_
#define _Out_writes_(x)
#define _Requires_lock_held_(x)
#define _Releases_lock_(x)
#define TRUE 1
#define FALSE 0
#define NOTHING
#define PASSIVE_LEVEL 0
#define APC_LEVEL 1
#define DISPATCH_LEVEL 2
#define SYNCH_LEVEL 12
#define LOW_PRIORITY 0
#define LOW_REALTIME_PRIORITY 16
#define HIGH_PRIORITY 31
#define MAXIMUM_PRIORITY 32
#define KE_MAXIMUM_GROUPS 1
#define KE_MAXIMUM_PROCESSORS 4
#define KI_THREAD_HANDOFF_USES_RUNNING 0
#define MAXULONG 0xFFFFFFFFu
#define AdjustNone 0
#define AdjustUnwait 1
#define Ready 1
#define Running 2
#define Standby 3
#define Waiting 5
#define DeferredReady 7
#define PRIORITY_MASK(p) (1u << (p))
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define YieldProcessor() __builtin_ia32_pause()
#define FAIL(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); abort(); } while (0)
#define CHECK(x) do { if (!(x)) FAIL("line %d: %s failed", __LINE__, #x); } while (0)
#define ASSERT(x) CHECK(x)

typedef unsigned char BOOLEAN, UCHAR, KIRQL;
typedef signed char SCHAR, CHAR;
typedef unsigned short USHORT;
typedef short SHORT;
typedef int32_t LONG, *PLONG, KPRIORITY;
typedef uint32_t ULONG, *PULONG;
typedef int64_t LONGLONG, LONG_PTR;
typedef uint64_t ULONGLONG, ULONG_PTR, KSPIN_LOCK, KAFFINITY;
typedef void *PVOID;
typedef KAFFINITY KI_PROCESSOR_SET[KE_MAXIMUM_GROUPS];
typedef enum { KiBalanceQuantum = 0, KiBalanceIdle = 1 } KI_BALANCE_REASON;

typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct _SINGLE_LIST_ENTRY { struct _SINGLE_LIST_ENTRY *Next; } SINGLE_LIST_ENTRY, *PSINGLE_LIST_ENTRY;

typedef struct _KTHREAD {
    KSPIN_LOCK ThreadLock;
    UCHAR State;
    BOOLEAN ProcessReadyQueue;
    BOOLEAN Preempted;
    BOOLEAN DisableBoost;
    BOOLEAN DeferPreemption;
    BOOLEAN QueueDeferPreemption;
    SCHAR Priority;
    SCHAR BasePriority;
    SCHAR PriorityDecrement;
    SCHAR Quantum;
    SCHAR QuantumReset;
    CHAR AdjustIncrement;
    UCHAR AdjustReason;
    ULONG NextProcessor;
    ULONG IdealProcessor;
    ULONG WaitTime;
    ULONG AffinityVersion;
    ULONG PriorityFloorSummary;
    KAFFINITY Affinity[KE_MAXIMUM_GROUPS];
    LIST_ENTRY WaitListEntry;
    SINGLE_LIST_ENTRY SwapListEntry;
} KTHREAD, *PKTHREAD;

typedef struct _KPRCB {
    ULONG Number;
    KSPIN_LOCK PrcbLock;
    PKTHREAD CurrentThread;
    PKTHREAD NextThread;
    PKTHREAD IdleThread;
    ULONG ReadySummary;
    LIST_ENTRY DispatcherReadyListHead[MAXIMUM_PRIORITY];
    SINGLE_LIST_ENTRY DeferredReadyListHead;
    KAFFINITY MultiThreadProcessorSet;
    BOOLEAN DpcRoutineActive;
    BOOLEAN IdleSchedule;
} KPRCB, *PKPRCB;

static KPRCB Prcbs[KE_MAXIMUM_PROCESSORS];
static KTHREAD IdleThreads[KE_MAXIMUM_PROCESSORS];
static PKPRCB KiProcessorBlock[KE_MAXIMUM_PROCESSORS];
static ULONG KeProcessorCount = KE_MAXIMUM_PROCESSORS;
static struct { KAFFINITY Bitmap[KE_MAXIMUM_GROUPS]; } KeActiveProcessors;
static KAFFINITY KiIdleSummary[KE_MAXIMUM_GROUPS];
static KAFFINITY KiIdleSMTSummary[KE_MAXIMUM_GROUPS];
static KAFFINITY KiHeteroPerformanceSet[KE_MAXIMUM_GROUPS];
static struct { ULONG LowPart; } KeTickCount;
static BOOLEAN SmpDbgEnabled = FALSE;
static __thread PKPRCB CurrentPrcb;
static __thread KIRQL Irql = SYNCH_LEVEL;
static __thread ULONG DelayHookKind;

static void InitializeListHead(PLIST_ENTRY h) { h->Flink = h->Blink = h; }
static BOOLEAN IsListEmpty(const LIST_ENTRY *h) { return h->Flink == h; }
static void InsertTailList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Flink = h; e->Blink = h->Blink; h->Blink->Flink = e; h->Blink = e; }
static void InsertHeadList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Flink = h->Flink; e->Blink = h; h->Flink->Blink = e; h->Flink = e; }
static BOOLEAN RemoveEntryList(PLIST_ENTRY e) { PLIST_ENTRY b = e->Blink, f = e->Flink; b->Flink = f; f->Blink = b; return f == b; }
static void PushEntryList(PSINGLE_LIST_ENTRY h, PSINGLE_LIST_ENTRY e) { e->Next = h->Next; h->Next = e; }

static BOOLEAN BitScanReverse(PULONG Index, ULONG Mask)
{
    if (!Mask) return FALSE;
    *Index = 31u - (ULONG)__builtin_clz(Mask);
    return TRUE;
}

static inline LONG ReadNoFence(volatile LONG *p) { return __atomic_load_n(p, __ATOMIC_RELAXED); }
static inline LONG InterlockedExchange(volatile LONG *p, LONG v) { return __atomic_exchange_n(p, v, __ATOMIC_ACQ_REL); }
static inline PVOID InterlockedCompareExchangePointer(PVOID volatile *p, PVOID x, PVOID c)
{
    PVOID expected = c;
    __atomic_compare_exchange_n(p, &expected, x, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return expected;
}

static inline void KiAcquireFlagLock(volatile KSPIN_LOCK *Lock)
{
    while (__atomic_exchange_n((LONG *)Lock, 1, __ATOMIC_ACQUIRE) != 0)
        while (__atomic_load_n((LONG *)Lock, __ATOMIC_RELAXED) != 0) YieldProcessor();
}
static inline void KiReleaseFlagLock(volatile KSPIN_LOCK *Lock) { __atomic_store_n((LONG *)Lock, 0, __ATOMIC_RELEASE); }
static inline void KiAcquireThreadLock(PKTHREAD Thread) { KiAcquireFlagLock(&Thread->ThreadLock); }
static inline void KiReleaseThreadLock(PKTHREAD Thread) { KiReleaseFlagLock(&Thread->ThreadLock); }
static inline void KiAcquirePrcbLock(PKPRCB Prcb) { KiAcquireFlagLock(&Prcb->PrcbLock); }
static inline void KiReleasePrcbLock(PKPRCB Prcb) { KiReleaseFlagLock(&Prcb->PrcbLock); }

#define KeGetCurrentPrcb() CurrentPrcb
#define KeGetCurrentIrql() Irql
#define KiThreadAffinity(t) ((t)->Affinity)
#define KiGetThreadQuantum(t) ((t)->Quantum)
#define KiSetThreadQuantum(t, q) ((t)->Quantum = (SCHAR)(q))
#define KiApplyPriorityFloor(t, p) (p)
#define KiArchIdleProcessorNeedsWake(p) FALSE
#define KiRescheduleThread(n, c) ((void)(n), (void)(c))
#define KiCheckProcessorUnpark(t) ((void)(t))
#define KiHeteroPreferredProcessorSet(t, s) ((void)(t), (void)(s))
#define KiGetNonParkedProcessorSet(s) ((s)[0] = KeActiveProcessors.Bitmap[0])
#define KiThreadLeavingProcessor(t) ((void)(t), FALSE)
#define KiDeferReadyWhileLeavingProcessor(t) ((void)(t), FALSE)
#define SmpDbgSchedulerIpi(p, t, prio) ((void)(p), (void)(t), (void)(prio))

static inline BOOLEAN KiIsProcessorInSet(const KAFFINITY *Set, ULONG Cpu) { return (Set[0] & (1ULL << Cpu)) != 0; }
static inline BOOLEAN KiIsProcessorSetEmpty(const KAFFINITY *Set) { return Set[0] == 0; }
static inline void KiClearProcessorSet(KAFFINITY *Set) { Set[0] = 0; }
static inline void KiAddProcessorToSet(KAFFINITY *Set, ULONG Cpu) { Set[0] |= (1ULL << Cpu); }
static inline void KiCopyProcessorSet(KAFFINITY *Dst, const KAFFINITY *Src) { Dst[0] = Src[0]; }
static inline BOOLEAN KiAndProcessorSets(KAFFINITY *Dst, const KAFFINITY *A, const KAFFINITY *B) { Dst[0] = A[0] & B[0]; return Dst[0] != 0; }
static inline BOOLEAN KiDoProcessorSetsIntersect(const KAFFINITY *A, const KAFFINITY *B) { return (A[0] & B[0]) != 0; }
static inline BOOLEAN KiFindProcessorInSet(const KAFFINITY *Set, ULONG Start, PULONG Found)
{
    for (ULONG i = Start; i < KE_MAXIMUM_PROCESSORS; i++)
    {
        if (Set[0] & (1ULL << i)) { *Found = i; return TRUE; }
    }
    return FALSE;
}
#define KI_SET_WORD(n) 0
#define KI_SET_BIT(n) (1ULL << (n))
static const ULONG KiProcessorSetWords = 1;

static inline void KiInsertDeferredReadyList(PKTHREAD Thread)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    Thread->State = DeferredReady;
    PushEntryList(&Prcb->DeferredReadyListHead, &Thread->SwapListEntry);
}

static void FASTCALL KiDeferredReadyThread(PKTHREAD Thread);
static void FASTCALL KiProcessDeferredReadyList(PKPRCB Prcb);
static inline void KiCheckDeferredReadyList(PKPRCB Prcb)
{
    if (Prcb->DeferredReadyListHead.Next) KiProcessDeferredReadyList(Prcb);
}
static ULONG NTAPI KiFindIdealProcessor(const KAFFINITY *Set, ULONG Ideal);
static PKTHREAD KiStealReadyThread(PKPRCB Prcb, KPRIORITY MinPrio);
'''

fixture = prelude + '\n'
for file, name in [
    ('ke_x.h', 'KiAssertReadyQueue'),
    ('ke_x.h', 'KiReadyQueueInsert'),
    ('ke_x.h', 'KiReadyQueueRemove'),
    ('ke_x.h', 'KiSelectReadyThread'),
    ('schedule.c', 'KiThreadRunnableSet'),
    ('schedule.c', 'KiFirstSetProcessor'),
    ('schedule.c', 'KiThreadMayRunElsewhere'),
    ('schedule.c', 'KiUpdateAffinitySummary'),
    ('schedule.c', 'KiMarkProcessorIdle'),
    ('schedule.c', 'KiMarkProcessorBusy'),
    ('schedule.c', 'KiApplyThreadAdjustment'),
    ('schedule.c', 'KiProcessorsNotRunningDpcs'),
    ('schedule.c', 'KiSelectTargetProcessor'),
    ('schedule.c', 'KiCanStealToProcessor'),
    ('schedule.c', 'KiQueueReadyThread'),
    ('schedule.c', 'KiPlaceReadyThread'),
    ('schedule.c', 'KiDeferredReadyThread'),
    ('schedule.c', 'KiProcessDeferredReadyList'),
    ('schedule.c', 'KiSelectNextThread'),
    ('schedule.c', 'KiFindIdealProcessor'),
    ('schedule.c', 'KiStealReadyThread'),
    ('schedule.c', 'KiAcquireReadyThreadPrcb'),
    ('schedule.c', 'KiSetPriorityThread'),
    ('schedule.c', 'KiSetAffinityThread'),
    ('schedule.c', 'KiBalanceReadyQueues'),
]:
    fixture += src.definition(file, name)

fixture += r'''
static KTHREAD Targets[8];
static volatile LONG StopWorkers, StartWorkers;
static volatile ULONGLONG Placements, PriorityChanges, Balances;

static void InitSystem(void)
{
    KeActiveProcessors.Bitmap[0] = (1ULL << KE_MAXIMUM_PROCESSORS) - 1;
    KiIdleSummary[0] = 0;
    KiIdleSMTSummary[0] = 0;
    KiHeteroPerformanceSet[0] = KeActiveProcessors.Bitmap[0];

    for (ULONG i = 0; i < KE_MAXIMUM_PROCESSORS; i++)
    {
        memset(&Prcbs[i], 0, sizeof(Prcbs[i]));
        memset(&IdleThreads[i], 0, sizeof(IdleThreads[i]));
        Prcbs[i].Number = i;
        Prcbs[i].IdleThread = &IdleThreads[i];
        Prcbs[i].CurrentThread = &IdleThreads[i];
        IdleThreads[i].State = Running;
        IdleThreads[i].Priority = 0;
        IdleThreads[i].NextProcessor = i;
        for (ULONG p = 0; p < MAXIMUM_PRIORITY; p++)
            InitializeListHead(&Prcbs[i].DispatcherReadyListHead[p]);
        KiProcessorBlock[i] = &Prcbs[i];
    }

    for (ULONG i = 0; i < 8; i++)
    {
        memset(&Targets[i], 0, sizeof(Targets[i]));
        Targets[i].State = Waiting;
        Targets[i].BasePriority = 8;
        Targets[i].Priority = 8;
        Targets[i].Quantum = 6;
        Targets[i].QuantumReset = 6;
        Targets[i].IdealProcessor = i % KE_MAXIMUM_PROCESSORS;
        Targets[i].NextProcessor = i % KE_MAXIMUM_PROCESSORS;
        Targets[i].Affinity[0] = KeActiveProcessors.Bitmap[0];
    }
}

static void *ReadyAndDrainWorker(void *Arg)
{
    ULONG Cpu = (ULONG)(uintptr_t)Arg;
    CurrentPrcb = &Prcbs[Cpu];
    Irql = SYNCH_LEVEL;
    ULONG Seed = 1001 + Cpu * 313;

    while (!__atomic_load_n(&StartWorkers, __ATOMIC_ACQUIRE))
        sched_yield();

    while (!__atomic_load_n(&StopWorkers, __ATOMIC_ACQUIRE))
    {
        Seed = Seed * 1103515245u + 12345u;
        PKTHREAD T = &Targets[(Seed >> 16) & 7];

        KiAcquireThreadLock(T);
        if (T->State == Waiting)
        {
            T->AdjustReason = AdjustUnwait;
            T->AdjustIncrement = (CHAR)((Seed >> 8) & 3);
            KiInsertDeferredReadyList(T);
        }
        KiReleaseThreadLock(T);

        KiCheckDeferredReadyList(CurrentPrcb);

        KiAcquirePrcbLock(CurrentPrcb);
        PKTHREAD Picked = CurrentPrcb->NextThread;
        if (Picked != NULL)
        {
            CurrentPrcb->NextThread = NULL;
            Picked->State = Running;
            Picked->NextProcessor = CurrentPrcb->Number;
        }
        else
        {
            Picked = KiSelectReadyThread(0, CurrentPrcb);
            if (Picked != NULL)
            {
                Picked->State = Running;
                Picked->NextProcessor = CurrentPrcb->Number;
            }
        }
        KiReleasePrcbLock(CurrentPrcb);
        KiCheckDeferredReadyList(CurrentPrcb);

        if (Picked != NULL)
        {
            KiAcquireThreadLock(Picked);
            Picked->State = Waiting;
            KiReleaseThreadLock(Picked);
            __atomic_add_fetch(&Placements, 1, __ATOMIC_RELAXED);
        }
    }
    return NULL;
}

static void *PriorityWorker(void *Arg)
{
    ULONG Cpu = (ULONG)(uintptr_t)Arg;
    CurrentPrcb = &Prcbs[Cpu];
    Irql = SYNCH_LEVEL;
    ULONG Seed = 7777 + Cpu * 997;

    while (!__atomic_load_n(&StartWorkers, __ATOMIC_ACQUIRE))
        sched_yield();

    while (!__atomic_load_n(&StopWorkers, __ATOMIC_ACQUIRE))
    {
        Seed = Seed * 1103515245u + 12345u;
        PKTHREAD T = &Targets[(Seed >> 16) & 7];
        KPRIORITY NewPrio = 4 + (KPRIORITY)((Seed >> 8) % 10);

        KiAcquireThreadLock(T);
        if ((Seed & 7) == 0)
        {
            KAFFINITY NewAffinity[KE_MAXIMUM_GROUPS];
            static const KAFFINITY Masks[3] = { 0x3, 0xC, 0xF };
            NewAffinity[0] = Masks[(Seed >> 12) % 3];
            KiSetAffinityThread(T, NewAffinity);
        }
        else
        {
            T->BasePriority = (SCHAR)NewPrio;
            T->PriorityDecrement = 0;
            KiSetPriorityThread(T, NewPrio);
        }
        KiReleaseThreadLock(T);

        KiCheckDeferredReadyList(CurrentPrcb);
        __atomic_add_fetch(&PriorityChanges, 1, __ATOMIC_RELAXED);
    }
    return NULL;
}

static void *BalanceWorker(void *Arg)
{
    ULONG Cpu = (ULONG)(uintptr_t)Arg;
    CurrentPrcb = &Prcbs[Cpu];
    Irql = SYNCH_LEVEL;

    while (!__atomic_load_n(&StartWorkers, __ATOMIC_ACQUIRE))
        sched_yield();

    while (!__atomic_load_n(&StopWorkers, __ATOMIC_ACQUIRE))
    {
        if (KiBalanceReadyQueues(CurrentPrcb, KiBalanceQuantum))
            __atomic_add_fetch(&Balances, 1, __ATOMIC_RELAXED);
        KiCheckDeferredReadyList(CurrentPrcb);
        KiAcquirePrcbLock(CurrentPrcb);
        PKTHREAD Picked = CurrentPrcb->NextThread;
        if (Picked != NULL)
        {
            CurrentPrcb->NextThread = NULL;
            Picked->State = Running;
            Picked->NextProcessor = CurrentPrcb->Number;
        }
        else
        {
            Picked = KiSelectReadyThread(0, CurrentPrcb);
            if (Picked != NULL)
            {
                Picked->State = Running;
                Picked->NextProcessor = CurrentPrcb->Number;
            }
        }
        KiReleasePrcbLock(CurrentPrcb);
        KiCheckDeferredReadyList(CurrentPrcb);
        if (Picked != NULL)
        {
            KiAcquireThreadLock(Picked);
            Picked->State = Waiting;
            KiReleaseThreadLock(Picked);
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    double Seconds = argc > 1 ? atof(argv[1]) : 1.0;
    pthread_t Threads[4];

    InitSystem();

    CHECK(pthread_create(&Threads[0], NULL, ReadyAndDrainWorker, (void *)(uintptr_t)0) == 0);
    CHECK(pthread_create(&Threads[1], NULL, ReadyAndDrainWorker, (void *)(uintptr_t)1) == 0);
    CHECK(pthread_create(&Threads[2], NULL, PriorityWorker, (void *)(uintptr_t)2) == 0);
    CHECK(pthread_create(&Threads[3], NULL, BalanceWorker, (void *)(uintptr_t)3) == 0);

    __atomic_store_n(&StartWorkers, 1, __ATOMIC_RELEASE);
    usleep((useconds_t)(Seconds * 1000000.0));
    __atomic_store_n(&StopWorkers, 1, __ATOMIC_RELEASE);

    for (ULONG i = 0; i < 4; i++)
        CHECK(pthread_join(Threads[i], NULL) == 0);

    printf("ready-priority-race: %llu placements, %llu priority changes, %llu balances, 0 mismatches\n",
           (unsigned long long)Placements,
           (unsigned long long)PriorityChanges,
           (unsigned long long)Balances);
    return 0;
}
'''

flags = [
    '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
    '-Wno-sign-compare', '-pthread',
]
if args.sanitize:
    flags += ['-fsanitize=thread', '-g']

with tempfile.TemporaryDirectory(prefix='ke-ready-priority-race-') as tmp:
    path = pathlib.Path(tmp)
    c_file = path / 'test.c'
    bin_file = path / 'test'
    c_file.write_text(fixture)
    subprocess.run([args.cc] + flags + [str(c_file), '-o', str(bin_file)], check=True)
    result = subprocess.run([str(bin_file), str(args.seconds)], capture_output=True, text=True, timeout=60)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='', file=sys.stderr)
    if result.returncode != 0:
        raise SystemExit(result.returncode)
