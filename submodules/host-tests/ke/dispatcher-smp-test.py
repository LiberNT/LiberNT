# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The kernel's own wait and signal routines, run by host threads standing for processors, lose no wake with or without the dispatcher database lock
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
import ke_timer_fixture as kt

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--bench', action='store_true', help='print operations per second for a rising number of processors')
parser.add_argument('--seconds', type=float, default=1.5)
parser.add_argument('--sanitize', action='store_true', help='build the smoke test with the thread sanitizer')
parser.add_argument('--emit', help='also write the generated source to this file')
args = parser.parse_args()
root = pathlib.Path(args.root)
src = kt.Sources(args.root)
src.text['syncobj.c'] = (root / 'ntos/ke/syncobj.c').read_text()
src.text['thrdctrl.c'] = (root / 'ntos/ke/thrdctrl.c').read_text()

ke_x = src.text['ke_x.h']
macros = '\n'.join(re.findall(r'^#define (?:KxChain\w+|KI_WAIT_\w+)\b(?:[^\n]*\\\n)*[^\n]*', ke_x, re.M))

prelude = r'''
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
#define _In_reads_(x)
#define TRUE 1
#define FALSE 0
#define NOTHING
#define PASSIVE_LEVEL 0
#define APC_LEVEL 1
#define DISPATCH_LEVEL 2
#define SYNCH_LEVEL 12
#define KernelMode 0
#define UserMode 1
#define MINLONG ((LONG)0x80000000)
#define LOW_REALTIME_PRIORITY 16
#define MAXIMUM_WAIT_OBJECTS 64
#define THREAD_WAIT_OBJECTS 3
#define TIMER_WAIT_BLOCK 3
#define KOBJECT_TYPE_MASK 0x7F
#define KOBJECT_LOCK_BIT 0x80
#define TIMER_OR_EVENT_TYPE 0x7L
#define EventNotificationObject 0
#define EventSynchronizationObject 1
#define MutantObject 2
#define SemaphoreObject 5
#define WaitBlockActive 4
#define WaitBlockInactive 5
#define Running 2
#define Waiting 5
#define DeferredReady 7
#define GateWait 8
#define AdjustUnwait 1
#define Executive 0
#define STATUS_SUCCESS 0
#define STATUS_WAIT_0 0
#define STATUS_ABANDONED 0x80
#define STATUS_USER_APC 0xC0
#define STATUS_KERNEL_APC 0x100
#define STATUS_ALERTED 0x101
#define STATUS_TIMEOUT 0x102
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_MUTANT_NOT_OWNED ((NTSTATUS)0xC0000046)
#define STATUS_SEMAPHORE_LIMIT_EXCEEDED ((NTSTATUS)0xC0000047)
#define STATUS_MUTANT_LIMIT_EXCEEDED ((NTSTATUS)0xC0000191)
#define MAXIMUM_WAIT_OBJECTS_EXCEEDED 0x0C
#define THREAD_ALERT_INCREMENT 2
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define FAIL(...) do { printf(__VA_ARGS__); printf("\n"); abort(); } while (0)
#define CHECK(x) do { if (!(x)) FAIL("line %d: %s failed", __LINE__, #x); } while (0)
/* An assertion reads lock words and thread states the way no other code does; the build without the race detector keeps them all */
#if SANITIZE
#define ASSERT(x) ((void)0)
#else
#define ASSERT(x) CHECK(x)
#endif
#define ASSERT_EVENT(x) ((void)(x))
#define ASSERT_SEMAPHORE(x) ((void)(x))
#define ASSERT_MUTANT(x) ((void)(x))
#define ASSERT_IRQL_LESS_OR_EQUAL(x) ((void)0)
#define YieldProcessor() __builtin_ia32_pause()

typedef unsigned char BOOLEAN, UCHAR, KIRQL;
typedef signed char SCHAR, CHAR, KPROCESSOR_MODE;
typedef unsigned short USHORT;
typedef short SHORT;
typedef int32_t LONG, *PLONG, NTSTATUS, KPRIORITY;
typedef uint32_t ULONG, *PULONG;
typedef int64_t LONGLONG, LONG_PTR;
typedef uint64_t ULONGLONG, ULONG_PTR, KSPIN_LOCK;
typedef void *PVOID;
typedef int KWAIT_REASON;
typedef enum { WaitAll, WaitAny, WaitNotification, WaitDequeue, WaitDpc } WAIT_TYPE;
typedef union { LONGLONG QuadPart; struct { uint32_t LowPart; int32_t HighPart; }; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
/* The kernel keeps the lock bit in the type byte; the race detector is given a word of its own to watch instead */
typedef struct _DISPATCHER_HEADER {
#if SANITIZE
    UCHAR Type, Signalling, Size, Inserted;
    volatile LONG Lock;
#else
    union { struct { UCHAR Type, Signalling, Size, Inserted; }; volatile LONG Lock; };
#endif
    LONG SignalState;
    LIST_ENTRY WaitListHead;
} DISPATCHER_HEADER, *PDISPATCHER_HEADER;
struct _KTHREAD;
typedef struct { DISPATCHER_HEADER Header; } KEVENT, *PKEVENT, KTIMER, *PKTIMER;
typedef struct { DISPATCHER_HEADER Header; LONG Limit; } KSEMAPHORE, *PKSEMAPHORE;
typedef struct { DISPATCHER_HEADER Header; LIST_ENTRY MutantListEntry; struct _KTHREAD *OwnerThread; BOOLEAN Abandoned; UCHAR ApcDisable; } KMUTANT, *PKMUTANT;
typedef struct { volatile LONG CurrentCount; } KQUEUE, *PKQUEUE;
typedef struct { int Unused; } KPRCB, *PKPRCB;
typedef struct _KWAIT_BLOCK {
    LIST_ENTRY WaitListEntry; UCHAR WaitType; volatile UCHAR BlockState; USHORT WaitKey; LONG SpareLong;
    struct _KTHREAD *Thread; PVOID Object; PVOID SparePtr;
} KWAIT_BLOCK, *PKWAIT_BLOCK;
typedef struct { LIST_ENTRY ApcListHead[2]; PVOID Process; BOOLEAN KernelApcInProgress, KernelApcPending, UserApcPending; } KAPC_STATE;
typedef struct _KTHREAD {
    KSPIN_LOCK ThreadLock;
    KSPIN_LOCK ApcQueueLock;
    volatile UCHAR State;
    struct { UCHAR State; } WaitRegister;
    LONG_PTR WaitStatus;
    PKWAIT_BLOCK WaitBlockList;
    KWAIT_BLOCK WaitBlock[THREAD_WAIT_OBJECTS + 1];
    UCHAR WaitBlockCount;
    BOOLEAN TimerActive;
    KTIMER Timer;
    KIRQL WaitIrql;
    BOOLEAN WaitNext, Alertable;
    KPROCESSOR_MODE WaitMode;
    UCHAR WaitReason;
    LIST_ENTRY WaitListEntry;
    ULONG WaitTime;
    PKQUEUE Queue;
    CHAR AdjustIncrement;
    UCHAR AdjustReason;
    LIST_ENTRY MutantListHead;
    SHORT KernelApcDisable, SpecialApcDisable;
    KAPC_STATE ApcState;
    BOOLEAN Alerted[2];
    PKPRCB WaitPrcb;
    BOOLEAN EnableStackSwap;
    SCHAR Priority;
    sem_t Park;
    ULONG Seed;
    ULONGLONG Operations;
} KTHREAD, *PKTHREAD;
typedef struct { KIRQL OldIrql; } KLOCK_QUEUE_HANDLE;

static __thread PKTHREAD CurrentThread;
static __thread KIRQL Irql;
static KPRCB Processor;
static struct { ULONG LowPart; } KeTickCount;

#define KeGetCurrentThread() CurrentThread
#define KeGetCurrentPrcb() (&Processor)
#define KeGetCurrentIrql() Irql
#define HalRequestSoftwareInterrupt(Level) ((void)(Level))
#define KiSetThreadSwapBusy(Thread) ((void)(Thread))
#define KiAddThreadToWaitList(Thread, Swappable) CHECK(!(Swappable))
#define KiIncrementQueueCurrentCount(Queue) __atomic_add_fetch(&(Queue)->CurrentCount, 1, __ATOMIC_SEQ_CST)
#define KiAcquireWaitLock(Prcb) ((void)(Prcb))
#define KiReleaseWaitLock(Prcb) ((void)(Prcb))
#define InterlockedOr(p, v) __atomic_fetch_or((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedAnd(p, v) __atomic_fetch_and((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedExchange(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)

static void InitializeListHead(PLIST_ENTRY h) { h->Flink = h->Blink = h; }
static BOOLEAN IsListEmpty(const LIST_ENTRY *h) { return h->Flink == h; }
static void InsertTailList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Flink = h; e->Blink = h->Blink; h->Blink->Flink = e; h->Blink = e; }
static void InsertHeadList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Flink = h->Flink; e->Blink = h; h->Flink->Blink = e; h->Flink = e; }
static BOOLEAN RemoveEntryList(PLIST_ENTRY e) { PLIST_ENTRY b = e->Blink, f = e->Flink; b->Flink = f; f->Blink = b; return f == b; }

static void KeLowerIrql(KIRQL New) { CHECK(New <= Irql); Irql = New; }
static void ExRaiseStatus(NTSTATUS Status) { FAIL("raised status %#x", (unsigned)Status); }
static void KeBugCheckEx(ULONG Code, ULONG_PTR A, ULONG_PTR B, ULONG_PTR C, ULONG_PTR D) { (void)A; (void)B; (void)C; (void)D; FAIL("bug check %#x", (unsigned)Code); }
static ULONGLONG KiQueryInterruptTimePrecise(PVOID Counter, BOOLEAN Unbiased) { (void)Counter; (void)Unbiased; return 0; }
static void IopSignalWaitCompletionPacket(PKWAIT_BLOCK WaitBlock) { (void)WaitBlock; FAIL("a completion packet on a wait list"); }
static BOOLEAN KxRemoveTreeTimer(PKTIMER Timer) { (void)Timer; FAIL("a timer in the table"); }

/* The old design held one lock for every processor from the dispatcher's entry to its exit or the switch; the kernel now only raises the level */
#if GLOBAL_LOCK
static pthread_spinlock_t DispatcherLock;
static __thread int DispatcherHeld;
static void KiAcquireDispatcherLockAtSynchLevel(void) { CHECK(Irql >= SYNCH_LEVEL && !DispatcherHeld); pthread_spin_lock(&DispatcherLock); DispatcherHeld = 1; }
static void KiReleaseDispatcherLockFromSynchLevel(void) { if (DispatcherHeld) { DispatcherHeld = 0; pthread_spin_unlock(&DispatcherLock); } }
#else
static void KiAcquireDispatcherLockAtSynchLevel(void) { CHECK(Irql >= SYNCH_LEVEL); }
static void KiReleaseDispatcherLockFromSynchLevel(void) { }
#endif
static KIRQL KiEnterDispatcher(void) { KIRQL Old = Irql; Irql = SYNCH_LEVEL; KiAcquireDispatcherLockAtSynchLevel(); return Old; }
static void KiExitDispatcher(KIRQL Old) { KiReleaseDispatcherLockFromSynchLevel(); KeLowerIrql(Old); }

static void KiEndThreadWait(PKTHREAD Thread);

#if SANITIZE
/* The kernel's own lock routines wait on a plain read, which is all the race detector would report; these say the same thing in atomics */
static inline void KiAcquireFlagLock(volatile KSPIN_LOCK *Lock)
{
    while (__atomic_exchange_n((LONG *)Lock, 1, __ATOMIC_ACQUIRE) != 0)
        while (__atomic_load_n((LONG *)Lock, __ATOMIC_RELAXED) != 0) YieldProcessor();
}
static inline void KiReleaseFlagLock(volatile KSPIN_LOCK *Lock) { __atomic_store_n((LONG *)Lock, 0, __ATOMIC_RELEASE); }
static inline void KiAcquireDispatcherObject(DISPATCHER_HEADER *Object)
{
    while (__atomic_fetch_or(&Object->Lock, KOBJECT_LOCK_BIT, __ATOMIC_ACQUIRE) & KOBJECT_LOCK_BIT)
        while (__atomic_load_n(&Object->Lock, __ATOMIC_RELAXED) & KOBJECT_LOCK_BIT) YieldProcessor();
}
static inline void KiReleaseDispatcherObject(DISPATCHER_HEADER *Object) { __atomic_fetch_and(&Object->Lock, ~KOBJECT_LOCK_BIT, __ATOMIC_RELEASE); }
#endif

/* A thread made ready runs at once here, where the kernel would first leave the dispatcher: the waker still holds what it held */
static void KiReadyThread(PKTHREAD Thread)
{
    CHECK(Thread->State == Waiting);
    Thread->State = DeferredReady;
    sem_post(&Thread->Park);
}

/* What the kernel's swap does around the switch: the lock is dropped, the processor is given up, and the thread finishes its own wait when it is back */
static inline void KiAcquireThreadLock(PKTHREAD Thread);
static inline void KiReleaseThreadLock(PKTHREAD Thread);

static LONG_PTR KiSwapThread(PKTHREAD Thread, PKPRCB Prcb, BOOLEAN NormalWait)
{
    KIRQL WaitIrql = Thread->WaitIrql;

    (void)Prcb;
    KiReleaseDispatcherLockFromSynchLevel();
    while (sem_wait(&Thread->Park) != 0) { }

    /* The kernel moves a ready thread on under processor locks; whoever looks under the thread lock only asks whether it waits */
    KiAcquireThreadLock(Thread);
    CHECK(Thread->State == DeferredReady);
    Thread->State = Running;
    KiReleaseThreadLock(Thread);

    if (NormalWait)
    {
        KiAcquireDispatcherLockAtSynchLevel();
        KiEndThreadWait(Thread);
        KiReleaseDispatcherLockFromSynchLevel();
    }

    KeLowerIrql(WaitIrql);
    return Thread->WaitStatus;
}

static BOOLEAN KiArmThreadWaitTimer(PKTHREAD Thread, PLARGE_INTEGER Interval, KPROCESSOR_MODE WaitMode, PLARGE_INTEGER DueTime)
{
    (void)Thread; (void)Interval; (void)WaitMode; (void)DueTime;
    FAIL("a wait with a timeout");
}
'''

fixture = prelude + macros + '\n'
if not args.sanitize:
    for name in ('KiAcquireDispatcherObject', 'KiReleaseDispatcherObject', 'KiAcquireFlagLock', 'KiReleaseFlagLock'):
        fixture += src.definition('ke_x.h', name)
for file, name in [('ke_x.h', 'KiAcquireThreadLock'), ('ke_x.h', 'KiReleaseThreadLock'),
                   ('ke_x.h', 'KiSatisfyMutantWait'), ('ke_x.h', 'KiSatisfyNonMutantWait'), ('ke_x.h', 'KiSatisfyObjectWait'),
                   ('ke_x.h', 'KiRecalculateDueTime'), ('ke_x.h', 'KiCheckThreadStackSwap'), ('ke_x.h', 'KiCheckAlertability'),
                   ('ke_x.h', 'KiIsKernelApcDeliverable'), ('ke_x.h', 'KxIsThreadWaitInterrupted'),
                   ('ke_x.h', 'KiBeginThreadWait'), ('ke_x.h', 'KxTryBeginThreadWait'), ('ke_x.h', 'KxExitDispatcherForRetry'),
                   ('ke_x.h', 'KxCommitThreadWait'),
                   ('dispatch.c', 'KiThreadWaitBlock'), ('dispatch.c', 'KiThreadWaitBlockCount'), ('dispatch.c', 'KiWaitBlockObject'), ('dispatch.c', 'KiUnlinkOneWaitBlock'),
                   ('dispatch.c', 'KiUnlinkWaitBlocks'), ('dispatch.c', 'KiEndThreadWait'), ('dispatch.c', 'KiUnwaitThread'),
                   ('dispatch.c', 'KiLockCommittedWaiter'), ('dispatch.c', 'KiUnlinkWaitBlock'), ('dispatch.c', 'KiWaitTest'),
                   ('ke_x.h', 'KxUnwaitThread'),
                   ('waitobj.c', 'KiIsObjectSignalled'), ('waitobj.c', 'KiSatisfyOwnWait'), ('waitobj.c', 'KiIsMutantAtLimit'),
                   ('waitobj.c', 'KiLockWaitObjects'), ('waitobj.c', 'KiUnlockWaitObjects'), ('waitobj.c', 'KiPublishWaitBlock'),
                   ('waitobj.c', 'KeWaitForSingleObject'), ('waitobj.c', 'KeWaitForMultipleObjects'),
                   ('syncobj.c', 'KiCompleteSignalling'), ('syncobj.c', 'KiSignalWaiters'), ('syncobj.c', 'KeSetEvent'),
                   ('syncobj.c', 'KePulseEvent'), ('syncobj.c', 'KeResetEvent'), ('syncobj.c', 'KeReleaseSemaphore'),
                   ('syncobj.c', 'KeReleaseMutant')]:
    fixture += src.definition(file, name)

fixture += r'''
/* The thread's APC lock is its own; an alert takes it and then the dispatcher, as the kernel does */
#define KiAcquireApcLockRaiseToSynch(Thread, Handle) do { (Handle)->OldIrql = Irql; Irql = SYNCH_LEVEL; KiAcquireFlagLock(&(Thread)->ApcQueueLock); } while (0)
#define KiReleaseApcLockFromSynchLevel(Handle) KiReleaseFlagLock(&Thread->ApcQueueLock)
'''
fixture += src.definition('thrdctrl.c', 'KeAlertThread')

fixture += r'''
#define MAX_PROCESSORS 64
#define SEMAPHORE_COUNT 3

static KTHREAD Threads[MAX_PROCESSORS];
static ULONG ThreadCount;
static KMUTANT Mutex;
static KSEMAPHORE Semaphore;
static KEVENT AutoEvent, ManualEvent, Baton[MAX_PROCESSORS];
static volatile LONG InMutex, SemaphoreHeld, StopWorkers, StartWorkers;
static volatile LONGLONG Progress, Alerts;

/* One object set per processor, on its own cache lines, for the run that measures the path and not the contention on an object */
static struct { KEVENT Event; KSEMAPHORE Semaphore; KMUTANT Mutant; char Pad[64]; } __attribute__((aligned(128))) Own[MAX_PROCESSORS];

static void InitHeader(PDISPATCHER_HEADER Header, UCHAR Type, LONG State)
{
    Header->Lock = 0;
    Header->Type = Type;
    Header->SignalState = State;
    InitializeListHead(&Header->WaitListHead);
}

static void InitMutant(PKMUTANT Mutant)
{
    InitHeader(&Mutant->Header, MutantObject, 1);
    Mutant->OwnerThread = NULL;
    Mutant->Abandoned = FALSE;
    Mutant->ApcDisable = 0;
}

static ULONG Random(PKTHREAD Thread) { Thread->Seed = Thread->Seed * 1103515245u + 12345u; return Thread->Seed >> 8; }

static void EnterMutex(void) { if (__atomic_add_fetch(&InMutex, 1, __ATOMIC_SEQ_CST) != 1) FAIL("two threads own the mutant"); }
static void LeaveMutex(void) { __atomic_sub_fetch(&InMutex, 1, __ATOMIC_SEQ_CST); }
static void EnterSemaphore(void) { if (__atomic_add_fetch(&SemaphoreHeld, 1, __ATOMIC_SEQ_CST) > SEMAPHORE_COUNT) FAIL("more holders than the semaphore counts"); }
static void LeaveSemaphore(void) { __atomic_sub_fetch(&SemaphoreHeld, 1, __ATOMIC_SEQ_CST); }

static void Begin(PKTHREAD Thread)
{
    CurrentThread = Thread;
    Irql = PASSIVE_LEVEL;
    while (!__atomic_load_n(&StartWorkers, __ATOMIC_ACQUIRE))
        sched_yield();
}

/* Every kind of wait and signal on objects all the processors share */
static void *StressWorker(void *Parameter)
{
    PKTHREAD Thread = Parameter;
    ULONG Self = (ULONG)(Thread - Threads);
    PVOID Objects[3];
    NTSTATUS Status;

    Begin(Thread);
    while (!__atomic_load_n(&StopWorkers, __ATOMIC_ACQUIRE))
    {
        switch (Random(Thread) % 7)
        {
            case 0:
                Status = KeWaitForSingleObject(&Mutex, Executive, KernelMode, FALSE, NULL);
                if (Status != STATUS_SUCCESS) FAIL("mutant wait answered %#x", (unsigned)Status);
                EnterMutex();
                if (Random(Thread) % 4 == 0)
                {
                    Status = KeWaitForSingleObject(&Mutex, Executive, KernelMode, FALSE, NULL);
                    if (Status != STATUS_SUCCESS) FAIL("recursive mutant wait answered %#x", (unsigned)Status);
                    KeReleaseMutant(&Mutex, 1, FALSE, FALSE);
                }
                LeaveMutex();
                KeReleaseMutant(&Mutex, 1, FALSE, FALSE);
                break;

            case 1:
                Status = KeWaitForSingleObject(&Semaphore, Executive, KernelMode, FALSE, NULL);
                if (Status != STATUS_SUCCESS) FAIL("semaphore wait answered %#x", (unsigned)Status);
                EnterSemaphore();
                LeaveSemaphore();
                KeReleaseSemaphore(&Semaphore, 0, 1, FALSE);
                break;

            case 2:
                Objects[0] = &Mutex;
                Objects[1] = &Semaphore;
                Status = KeWaitForMultipleObjects(2, Objects, WaitAll, Executive, KernelMode, FALSE, NULL, NULL);
                if (Status != STATUS_SUCCESS) FAIL("wait for all answered %#x", (unsigned)Status);
                EnterMutex();
                EnterSemaphore();
                LeaveSemaphore();
                LeaveMutex();
                KeReleaseSemaphore(&Semaphore, 0, 1, FALSE);
                KeReleaseMutant(&Mutex, 1, FALSE, FALSE);
                break;

            case 3:
                Objects[0] = &Baton[Self];
                Objects[1] = &AutoEvent;
                Objects[2] = &ManualEvent;
                KeSetEvent(&Baton[(Self + 1) % ThreadCount], 0, FALSE);
                KeSetEvent(&AutoEvent, 0, FALSE);
                Status = KeWaitForMultipleObjects(3, Objects, WaitAny, Executive, KernelMode, TRUE, NULL, NULL);
                if (Status == STATUS_ALERTED)
                    __atomic_add_fetch(&Alerts, 1, __ATOMIC_RELAXED);
                else if (Status < STATUS_WAIT_0 || Status > STATUS_WAIT_0 + 2)
                    FAIL("wait for any answered %#x", (unsigned)Status);
                break;

            case 4:
                if (Random(Thread) & 1)
                    KeSetEvent(&ManualEvent, 0, FALSE);
                else
                    KeResetEvent(&ManualEvent);
                break;

            case 5:
                KePulseEvent(&AutoEvent, 0, FALSE);
                KeSetEvent(&Baton[Random(Thread) % ThreadCount], 0, FALSE);
                break;

            default:
                KeAlertThread(&Threads[Random(Thread) % ThreadCount], KernelMode);
                break;
        }

        Thread->Operations++;
        __atomic_add_fetch(&Progress, 1, __ATOMIC_RELAXED);
    }

    return NULL;
}

/* Signals and waits that never block, each processor on objects of its own: what is left to contend for is the dispatcher itself */
static void *PrivateWorker(void *Parameter)
{
    PKTHREAD Thread = Parameter;
    ULONG Self = (ULONG)(Thread - Threads);

    Begin(Thread);
    while (!__atomic_load_n(&StopWorkers, __ATOMIC_ACQUIRE))
    {
        KeSetEvent(&Own[Self].Event, 0, FALSE);
        CHECK(KeWaitForSingleObject(&Own[Self].Event, Executive, KernelMode, FALSE, NULL) == STATUS_SUCCESS);
        KeReleaseSemaphore(&Own[Self].Semaphore, 0, 1, FALSE);
        CHECK(KeWaitForSingleObject(&Own[Self].Semaphore, Executive, KernelMode, FALSE, NULL) == STATUS_SUCCESS);
        CHECK(KeWaitForSingleObject(&Own[Self].Mutant, Executive, KernelMode, FALSE, NULL) == STATUS_SUCCESS);
        KeReleaseMutant(&Own[Self].Mutant, 1, FALSE, FALSE);
        Thread->Operations += 6;
    }

    return NULL;
}

/* Two processors hand an event back and forth, so every operation is a wake and a block */
static void *PairWorker(void *Parameter)
{
    PKTHREAD Thread = Parameter;
    ULONG Self = (ULONG)(Thread - Threads);
    ULONG Peer = Self ^ 1;

    Begin(Thread);
    while (!__atomic_load_n(&StopWorkers, __ATOMIC_ACQUIRE))
    {
        KeSetEvent(&Baton[Peer], 0, FALSE);
        CHECK(KeWaitForSingleObject(&Baton[Self], Executive, KernelMode, FALSE, NULL) == STATUS_SUCCESS);
        Thread->Operations += 2;
    }

    KeSetEvent(&Baton[Peer], 0, FALSE);
    return NULL;
}

static void Setup(ULONG Count)
{
    ULONG Index;

    ThreadCount = Count;
    StopWorkers = 0;
    StartWorkers = 0;
    Progress = 0;
    Alerts = 0;
    InitMutant(&Mutex);
    InitHeader(&Semaphore.Header, SemaphoreObject, SEMAPHORE_COUNT);
    Semaphore.Limit = SEMAPHORE_COUNT;
    InitHeader(&AutoEvent.Header, EventSynchronizationObject, 0);
    InitHeader(&ManualEvent.Header, EventNotificationObject, 0);

    for (Index = 0; Index < MAX_PROCESSORS; Index++)
    {
        PKTHREAD Thread = &Threads[Index];

        memset(Thread, 0, sizeof(*Thread));
        InitializeListHead(&Thread->MutantListHead);
        InitializeListHead(&Thread->ApcState.ApcListHead[KernelMode]);
        InitializeListHead(&Thread->ApcState.ApcListHead[UserMode]);
        InitHeader(&Thread->Timer.Header, 8, 0);
        Thread->WaitRegister.State = KI_WAIT_ABORTED;
        Thread->State = Running;
        Thread->Seed = 1000 + Index * 7919;
        sem_init(&Thread->Park, 0, 0);

        InitHeader(&Baton[Index].Header, EventSynchronizationObject, 0);
        InitHeader(&Own[Index].Event.Header, EventSynchronizationObject, 0);
        InitHeader(&Own[Index].Semaphore.Header, SemaphoreObject, 0);
        Own[Index].Semaphore.Limit = 1;
        InitMutant(&Own[Index].Mutant);
    }
}

static double Run(void *(*Worker)(void *), ULONG Count, double Seconds, BOOLEAN Watch)
{
    pthread_t Ids[MAX_PROCESSORS];
    struct timespec Start, End;
    ULONGLONG Total = 0;
    LONGLONG Last = 0, Now;
    ULONG Index, Tick;
    PKTHREAD Self = &Threads[MAX_PROCESSORS - 1];

    Setup(Count);
    CurrentThread = Self;
    Irql = PASSIVE_LEVEL;

    for (Index = 0; Index < Count; Index++)
        CHECK(pthread_create(&Ids[Index], NULL, Worker, &Threads[Index]) == 0);

    clock_gettime(CLOCK_MONOTONIC, &Start);
    __atomic_store_n(&StartWorkers, 1, __ATOMIC_RELEASE);

    for (Tick = 0; Tick < (ULONG)(Seconds * 20); Tick++)
    {
        usleep(50000);
        if (!Watch)
            continue;

        Now = __atomic_load_n(&Progress, __ATOMIC_RELAXED);
        if (Now == Last)
        {
            /* Every worker can be parked on its baton at once; one that still does not move after a kick has lost a wake */
            for (Index = 0; Index < Count; Index++)
                KeSetEvent(&Baton[Index], 0, FALSE);
            usleep(300000);
            if (__atomic_load_n(&Progress, __ATOMIC_RELAXED) == Now)
                FAIL("no progress with %u processors: a wake was lost or a lock is stuck", (unsigned)Count);
        }
        Last = Now;
    }

    __atomic_store_n(&StopWorkers, 1, __ATOMIC_RELEASE);
    clock_gettime(CLOCK_MONOTONIC, &End);

    if (Watch)
    {
        ULONG Round;

        for (Round = 0; Round < 4000; Round++)
        {
            for (Index = 0; Index < Count; Index++)
            {
                KeSetEvent(&Baton[Index], 0, FALSE);
                KeAlertThread(&Threads[Index], KernelMode);
            }
            KeSetEvent(&ManualEvent, 0, FALSE);
            usleep(500);
        }
    }

    for (Index = 0; Index < Count; Index++)
    {
        CHECK(pthread_join(Ids[Index], NULL) == 0);
        Total += Threads[Index].Operations;
    }

    if (Watch)
    {
        if (Semaphore.Header.SignalState != SEMAPHORE_COUNT) FAIL("the semaphore ended at %d of %d", Semaphore.Header.SignalState, SEMAPHORE_COUNT);
        if (Mutex.Header.SignalState != 1 || Mutex.OwnerThread != NULL) FAIL("the mutant ended owned");
        if (!IsListEmpty(&Mutex.Header.WaitListHead) || !IsListEmpty(&Semaphore.Header.WaitListHead) ||
            !IsListEmpty(&AutoEvent.Header.WaitListHead) || !IsListEmpty(&ManualEvent.Header.WaitListHead))
        {
            FAIL("a wait block was left on an object");
        }
        for (Index = 0; Index < Count; Index++)
        {
            if (!IsListEmpty(&Baton[Index].Header.WaitListHead)) FAIL("a wait block was left on a baton");
            if (!IsListEmpty(&Threads[Index].MutantListHead)) FAIL("a thread ended holding a mutant");
        }
    }

    return (double)Total / ((End.tv_sec - Start.tv_sec) + (End.tv_nsec - Start.tv_nsec) / 1e9);
}

int main(int argc, char **argv)
{
    double Seconds = argc > 2 ? atof(argv[2]) : 1.5;
    ULONG Count;

#if GLOBAL_LOCK
    pthread_spin_init(&DispatcherLock, PTHREAD_PROCESS_PRIVATE);
#endif

    if (argc > 1 && !strcmp(argv[1], "stress"))
    {
        Count = argc > 3 ? (ULONG)atoi(argv[3]) : 12;
        printf("%.0f\n", Run(StressWorker, Count, Seconds, TRUE));
        return 0;
    }

    Count = argc > 3 ? (ULONG)atoi(argv[3]) : 1;
    printf("%.0f\n", Run(!strcmp(argv[1], "pair") ? PairWorker : PrivateWorker, Count, Seconds, FALSE));
    return 0;
}
'''

flags = ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function',
         '-Wno-unused-variable', '-Wno-sign-compare', '-Wno-missing-field-initializers', '-pthread']

if args.emit:
    pathlib.Path(args.emit).write_text(fixture)

with tempfile.TemporaryDirectory(prefix='ke-dispatcher-smp-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    binaries = {}
    for mode in (1, 0):
        binaries[mode] = str(path / f'test{mode}')
        extra = ['-fsanitize=thread', '-g', '-DSANITIZE=1'] if args.sanitize else ['-DSANITIZE=0']
        subprocess.run([args.cc] + flags + extra + [f'-DGLOBAL_LOCK={mode}', str(path / 'test.c'), '-o', binaries[mode]], check=True)

    def run(mode, kind, count, seconds):
        result = subprocess.run([binaries[mode], kind, str(seconds), str(count)], capture_output=True, text=True, timeout=120 + seconds * 4)
        if result.returncode != 0:
            print(result.stdout + result.stderr)
            raise SystemExit(f'{kind} with {count} processors, dispatcher lock {"held" if mode else "absent"}: failed')
        return float(result.stdout.strip().splitlines()[-1])

    for mode in (1, 0):
        for count in (2, 12):
            rate = run(mode, 'stress', count, args.seconds)
            print(f'dispatcher-smp: stress, {count} processors, dispatcher lock {"held" if mode else "absent"}: {rate:,.0f} operations per second, no wake lost')

    if args.bench:
        import os
        host = os.cpu_count() or 1
        for kind, title in (('private', 'signals and waits that do not block, objects private to each processor'),
                            ('pair', 'pairs of processors waking each other')):
            print(f'\n{title} (operations per second; this host has {host} processors)')
            print(f'{"processors":>10} {"lock held":>14} {"lock absent":>14} {"ratio":>7}')
            for count in (1, 2, 4, 8, 16, 24, 32, 48, 64):
                if kind == 'pair' and count % 2:
                    continue
                held = run(1, kind, count, args.seconds)
                absent = run(0, kind, count, args.seconds)
                print(f'{count:>10} {held:>14,.0f} {absent:>14,.0f} {absent / held:>6.1f}x')
