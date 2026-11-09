# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Expired timer list cancellation and rearm regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--dpc-source')
args = parser.parse_args()
root = pathlib.Path(args.root)
dpc = pathlib.Path(args.dpc_source).read_text() if args.dpc_source else (root / 'ntos/ke/dpc.c').read_text()
header = (root / 'ntos/include/internal/ke_x.h').read_text()
timer = (root / 'ntos/ke/timerobj.c').read_text()


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    pos = opening + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


fixture = r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sched.h>
#define VOID void
#define NTAPI
#define FASTCALL
#define FORCEINLINE
#define IN
#define FALSE 0
#define TRUE 1
#define IO_NO_INCREMENT 0
#define MAX_TIMER_DPCS 32
#define DBG 0
#define CONTAINING_RECORD(p,t,f) ((t *)((char *)(p)-offsetof(t,f)))
#define ASSERT(x) assert(x)
#define DPRINT(...) ((void)0)
#define ULongToPtr(x) ((void *)(uintptr_t)(x))
#define UlongToPtr(x) ULongToPtr(x)
#define Int32x32To64(a,b) ((long long)(a)*(b))
typedef unsigned char UCHAR;
typedef int BOOLEAN, LONG;
typedef unsigned long ULONG;
typedef int KIRQL;
typedef union { long long QuadPart; struct { unsigned int LowPart; int HighPart; }; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef union { unsigned long long QuadPart; struct { unsigned int LowPart, HighPart; }; } ULARGE_INTEGER;
typedef struct E { struct E *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct KDPC { void (*DeferredRoutine)(struct KDPC *, void *, void *, void *); void *DeferredContext; } KDPC, *PKDPC;
typedef struct { int Inserted, SignalState; unsigned char Hand; LIST_ENTRY WaitListHead; pthread_mutex_t Lock; } HEADER;
typedef struct { HEADER Header; LARGE_INTEGER DueTime; LIST_ENTRY TimerListEntry; PKDPC Dpc; ULONG Period; } KTIMER, *PKTIMER;
typedef struct { LIST_ENTRY Entry; union { unsigned long long QuadPart; struct { unsigned int LowPart, HighPart; }; } Time; } KTIMER_TABLE_ENTRY, *PKTIMER_TABLE_ENTRY;
typedef pthread_mutex_t *PKSPIN_LOCK_QUEUE;
typedef struct { int unused; } KPRCB, *PKPRCB;
typedef struct { PKDPC Dpc; void (*Routine)(PKDPC,void *,void *,void *); void *Context; } DPC_QUEUE_ENTRY;
static KTIMER_TABLE_ENTRY KiTimerTableListHead[1];
static pthread_mutex_t Bucket=PTHREAD_MUTEX_INITIALIZER, Adjustment=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int AdjustmentHeld;
static int TryFailed, Detached, Resume, Signals, Rearms;
static void TestDetachProbe(void) { __atomic_store_n(&Detached,1,__ATOMIC_RELEASE); }
static KPRCB Prcb;
static void InitializeListHead(PLIST_ENTRY e) { e->Flink=e; e->Blink=e; }
static int IsListEmpty(PLIST_ENTRY e) { return e->Flink==e; }
static void InsertTailList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Blink=h->Blink; e->Flink=h; h->Blink->Flink=e; h->Blink=e; }
static int RemoveEntryList(PLIST_ENTRY e) { PLIST_ENTRY a=e->Blink,b=e->Flink; a->Flink=b; b->Blink=a; return a==b; }
static ULONG KiComputeTimerTableIndex(unsigned long long due) { (void)due; return 0; }
static PKSPIN_LOCK_QUEUE KiAcquireTimerLock(ULONG hand) { (void)hand; assert(pthread_mutex_lock(&Bucket)==0); return &Bucket; }
static void KiReleaseTimerLock(PKSPIN_LOCK_QUEUE l) { assert(pthread_mutex_unlock(l)==0); }
static void KiAcquireTimerAdjustmentLock(void) { assert(pthread_mutex_lock(&Adjustment)==0); AdjustmentHeld=1; }
static void KiReleaseTimerAdjustmentLock(void) { assert(AdjustmentHeld); AdjustmentHeld=0; assert(pthread_mutex_unlock(&Adjustment)==0); }
static BOOLEAN KiTryAcquireDispatcherObject(HEADER *h) {
    int r=pthread_mutex_trylock(&h->Lock);
    assert(r==0 || r==EBUSY);
    if (r) __atomic_store_n(&TryFailed,1,__ATOMIC_RELEASE);
    return r==0;
}
static void KiAcquireDispatcherObject(HEADER *h) { assert(pthread_mutex_lock(&h->Lock)==0); }
static void KiReleaseDispatcherObject(HEADER *h) { assert(pthread_mutex_unlock(&h->Lock)==0); }
static void YieldProcessor(void) { while (!__atomic_load_n(&Resume,__ATOMIC_ACQUIRE)) sched_yield(); }
static void KiWaitTest(void *t, int i) { (void)t;(void)i;assert(0); }
static BOOLEAN KiInsertTreeTimer(PKTIMER t, LARGE_INTEGER interval) {
    assert(interval.QuadPart<0);
    assert(!AdjustmentHeld);
    KiAcquireTimerAdjustmentLock();
    assert(pthread_mutex_lock(&Bucket)==0);
    t->Header.Inserted=1;
    InsertTailList(&KiTimerTableListHead[0].Entry,&t->TimerListEntry);
    Rearms++;
    assert(pthread_mutex_unlock(&Bucket)==0);
    KiReleaseTimerAdjustmentLock();
    return TRUE;
}
static void KeQuerySystemTime(PLARGE_INTEGER t) { t->QuadPart=1; }
static BOOLEAN KeInsertQueueDpc(PKDPC d, void *a, void *b) { (void)d;(void)a;(void)b; Signals++; return TRUE; }
static PKPRCB KeGetCurrentPrcb(void) { return &Prcb; }
static void KiRequestDispatchInterrupt(PKPRCB p) { (void)p; }
static void KiExitDispatcher(KIRQL i) { (void)i; }
static void KiBeginDpcExecution(PKPRCB p, PKDPC d) { (void)p;(void)d; }
static void KiEndDpcExecution(PKPRCB p, PKDPC d) { (void)p;(void)d; }
static void KeLowerIrql(KIRQL i) { (void)i; }
#define DISPATCH_LEVEL 2
'''
fixture += '\n' + function(header, 'FORCEINLINE\nBOOLEAN\nKxRemoveTreeTimer(')
fixture += '\n' + function(timer, 'BOOLEAN\nFASTCALL\nKiSignalTimer(')
expire_function = function(dpc, 'VOID\nFASTCALL\nKiTimerListExpire(')
needle = '        RemoveEntryList(&Timer->TimerListEntry);'
assert expire_function.count(needle) == 1
fixture += '\n' + expire_function.replace(needle, needle + '\n        TestDetachProbe();')
fixture += r'''
static void *expire(void *arg) {
    KiAcquireTimerAdjustmentLock();
    KiTimerListExpire(arg,0);
    assert(!AdjustmentHeld);
    return NULL;
}
static void init_timer(PKTIMER t, PLIST_ENTRY expired, PKDPC dpc) {
    *t=(KTIMER){0};
    assert(pthread_mutex_init(&t->Header.Lock,0)==0);
    InitializeListHead(&t->Header.WaitListHead);
    t->Header.Inserted=1;
    t->DueTime.QuadPart=1;
    t->Dpc=dpc;
    InsertTailList(expired,&t->TimerListEntry);
}
static void cancel_or_reset(int reset) {
    KTIMER t; KDPC dpc={0}; LIST_ENTRY expired; pthread_t worker;
    InitializeListHead(&expired);
    InitializeListHead(&KiTimerTableListHead[0].Entry);
    init_timer(&t,&expired,&dpc);
    TryFailed=Detached=Resume=Signals=Rearms=0;
    assert(pthread_mutex_lock(&t.Header.Lock)==0);
    assert(pthread_create(&worker,0,expire,&expired)==0);
    while (!__atomic_load_n(&TryFailed,__ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&Detached,__ATOMIC_ACQUIRE)) sched_yield();
    assert(__atomic_load_n(&TryFailed,__ATOMIC_ACQUIRE));
    assert(KxRemoveTreeTimer(&t));
    assert(IsListEmpty(&expired));
    if (reset) {
        KiAcquireTimerAdjustmentLock();
        assert(pthread_mutex_lock(&Bucket)==0);
        t.DueTime.QuadPart=100;
        t.Header.Inserted=1;
        InsertTailList(&KiTimerTableListHead[0].Entry,&t.TimerListEntry);
        assert(pthread_mutex_unlock(&Bucket)==0);
        KiReleaseTimerAdjustmentLock();
    }
    assert(pthread_mutex_unlock(&t.Header.Lock)==0);
    __atomic_store_n(&Resume,1,__ATOMIC_RELEASE);
    assert(pthread_join(worker,0)==0);
    assert(Signals==0 && Rearms==0 && t.Header.SignalState==0);
    assert(t.Header.Inserted==reset);
    assert(IsListEmpty(&KiTimerTableListHead[0].Entry)==!reset);
    assert(pthread_mutex_destroy(&t.Header.Lock)==0);
}
static void periodic(void) {
    KTIMER t; KDPC dpc={0}; LIST_ENTRY expired; pthread_t worker;
    InitializeListHead(&expired);
    InitializeListHead(&KiTimerTableListHead[0].Entry);
    init_timer(&t,&expired,&dpc);
    t.Period=10;
    TryFailed=Signals=Rearms=0; Resume=1;
    assert(pthread_create(&worker,0,expire,&expired)==0);
    assert(pthread_join(worker,0)==0);
    assert(IsListEmpty(&expired));
    assert(t.Header.SignalState==1 && t.Header.Inserted==1);
    assert(Signals==1 && Rearms==1);
    assert(KiTimerTableListHead[0].Entry.Flink==&t.TimerListEntry);
    assert(pthread_mutex_destroy(&t.Header.Lock)==0);
}
int main(void) { cancel_or_reset(0); cancel_or_reset(1); periodic(); puts("expired-list cancellation, reset, and periodic rearm passed"); }
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'timer_expired.c'
    exe = pathlib.Path(tmp) / 'timer_expired'
    c.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-pthread', '-Wall', '-Wextra',
                    '-Wno-unused-function', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=8)
