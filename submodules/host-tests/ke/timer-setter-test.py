# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Absolute timer setter and system-time adjustment regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root)
header = (root / 'ntos/include/internal/ke_x.h').read_text()
timer = (root / 'ntos/ke/timerobj.c').read_text()
clock = (root / 'ntos/ke/clock.c').read_text()


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
#define OUT
#define OPTIONAL
#define TRUE 1
#define FALSE 0
#define DBG 1
#define DISPATCH_LEVEL 2
#define SYNCH_LEVEL 2
#define HIGH_LEVEL 15
#define TIMER_TABLE_SIZE 1
#define ASSERT(x) assert(x)
#define ASSERT_TIMER(x) ((void)(x))
#define DPRINT(...) ((void)0)
#define CONTAINING_RECORD(p,t,f) ((t *)((char *)(p)-offsetof(t,f)))
typedef int BOOLEAN, LONG, KIRQL;
typedef unsigned char UCHAR;
typedef unsigned long ULONG, *PULONG;
typedef unsigned long long ULONGLONG;
typedef union { long long QuadPart; struct { unsigned int LowPart; int HighPart; }; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct E { struct E *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct { int Inserted, Absolute, SignalState; UCHAR Hand; pthread_mutex_t Lock; } HEADER;
typedef struct { int unused; } KDPC, *PKDPC;
typedef union { unsigned long long QuadPart; struct { unsigned int LowPart, HighPart; }; } ULARGE_INTEGER;
typedef struct { HEADER Header; ULARGE_INTEGER DueTime; LIST_ENTRY TimerListEntry; PKDPC Dpc; LONG Period; } KTIMER, *PKTIMER;
typedef struct { LIST_ENTRY Entry; union { unsigned long long QuadPart; struct { unsigned int LowPart, HighPart; }; } Time; } KTIMER_TABLE_ENTRY, *PKTIMER_TABLE_ENTRY;
typedef pthread_mutex_t *PKSPIN_LOCK_QUEUE;
typedef struct { int unused; } KPRCB, *PKPRCB;
typedef struct { int unused; } TIME_FIELDS;
static KTIMER_TABLE_ENTRY KiTimerTableListHead[1];
static LARGE_INTEGER KeBootTime;
static long long KeBootTimeBias;
static int ExCmosClockIsSane;
static pthread_mutex_t Bucket=PTHREAD_MUTEX_INITIALIZER, Adjustment=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int AdjustmentHeld;
static long long SystemTime=100;
static int QueryRead, ClockStarted, ClockBlocked, ClockDone, PauseQuery, SignalCalls;
static KPRCB Prcb;
static void InitializeListHead(PLIST_ENTRY e) { e->Flink=e; e->Blink=e; }
static int IsListEmpty(PLIST_ENTRY e) { return e->Flink==e; }
static void InsertTailList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Blink=h->Blink; e->Flink=h; h->Blink->Flink=e; h->Blink=e; }
static void InsertHeadList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Flink=h->Flink; e->Blink=h; h->Flink->Blink=e; h->Flink=e; }
static int RemoveEntryList(PLIST_ENTRY e) { PLIST_ENTRY a=e->Blink,b=e->Flink; a->Flink=b; b->Blink=a; return a==b; }
static ULONG KiComputeTimerTableIndex(ULONGLONG due) { (void)due; return 0; }
static PKSPIN_LOCK_QUEUE KiAcquireTimerLock(ULONG hand) { (void)hand; assert(pthread_mutex_lock(&Bucket)==0); return &Bucket; }
static void KiReleaseTimerLock(PKSPIN_LOCK_QUEUE l) { assert(pthread_mutex_unlock(l)==0); }
static void KiAcquireTimerAdjustmentLock(void) {
    int r=pthread_mutex_trylock(&Adjustment);
    assert(r==0 || r==EBUSY);
    if (r) {
        if (__atomic_load_n(&PauseQuery,__ATOMIC_ACQUIRE)) __atomic_store_n(&ClockBlocked,1,__ATOMIC_RELEASE);
        assert(pthread_mutex_lock(&Adjustment)==0);
    }
    AdjustmentHeld=1;
}
static void KiReleaseTimerAdjustmentLock(void) { assert(AdjustmentHeld); AdjustmentHeld=0; assert(pthread_mutex_unlock(&Adjustment)==0); }
static void KiAcquireDispatcherObject(HEADER *h) { assert(pthread_mutex_lock(&h->Lock)==0); }
static void KiReleaseDispatcherObject(HEADER *h) { assert(pthread_mutex_unlock(&h->Lock)==0); }
static void KeQuerySystemTimePrecise(PLARGE_INTEGER t) {
    t->QuadPart=__atomic_load_n(&SystemTime,__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&PauseQuery,__ATOMIC_ACQUIRE)) {
        __atomic_store_n(&QueryRead,1,__ATOMIC_RELEASE);
        while (!__atomic_load_n(&ClockStarted,__ATOMIC_ACQUIRE)) sched_yield();
        while (!__atomic_load_n(&ClockBlocked,__ATOMIC_ACQUIRE) &&
               !__atomic_load_n(&ClockDone,__ATOMIC_ACQUIRE)) sched_yield();
    }
}
static void KeQuerySystemTime(PLARGE_INTEGER t) { t->QuadPart=__atomic_load_n(&SystemTime,__ATOMIC_ACQUIRE); }
static ULONGLONG KiQueryInterruptTimePrecise(void *p, int b) { (void)p;(void)b;return 100; }
static ULONGLONG KeQueryInterruptTime(void) { return 100; }
static void KeSetSystemAffinityThread(int x) { (void)x; }
static void KeRevertToUserAffinityThread(void) { }
static _Thread_local KIRQL CurrentIrql;
static KIRQL KeRaiseIrqlToSynchLevel(void) { KIRQL old=CurrentIrql; CurrentIrql=SYNCH_LEVEL; return old; }
static void KeRaiseIrql(KIRQL n, KIRQL *old) { *old=CurrentIrql; CurrentIrql=n; }
static void KeLowerIrql(KIRQL old) { CurrentIrql=old; }
static KIRQL KeGetCurrentIrql(void) { return CurrentIrql; }
static void KiPublishTime(int a, int b, PLARGE_INTEGER t, int d, int e, void *f) { (void)a;(void)b;(void)d;(void)e;(void)f;__atomic_store_n(&SystemTime,t->QuadPart,__ATOMIC_RELEASE); }
static void RtlTimeToTimeFields(PLARGE_INTEGER t, TIME_FIELDS *f) { (void)t;(void)f; }
static int HalSetRealTimeClock(TIME_FIELDS *f) { (void)f; return 1; }
static void KiTimerListExpire(PLIST_ENTRY h, KIRQL i) { (void)i; assert(IsListEmpty(h)); if (AdjustmentHeld) KiReleaseTimerAdjustmentLock(); }
static BOOLEAN KiSignalTimer(PKTIMER t) { assert(!AdjustmentHeld); t->Header.Inserted=0;t->Header.SignalState=1;SignalCalls++;return FALSE; }
static PKPRCB KeGetCurrentPrcb(void) { return &Prcb; }
static void KiRequestDispatchInterrupt(PKPRCB p) { (void)p; }
static void KiExitDispatcher(KIRQL i) { CurrentIrql=i; }
static void KiCompleteTimer(PKTIMER t, PKSPIN_LOCK_QUEUE q) { (void)t;(void)q;assert(0); }
'''
for source, signature in [
    (header, 'FORCEINLINE\nVOID\nKiRemoveEntryTimer('),
    (timer, 'BOOLEAN\nFASTCALL\nKiInsertTimerTable('),
    (header, 'FORCEINLINE\nVOID\nKxInsertTimer('),
    (header, 'FORCEINLINE\nBOOLEAN\nKiComputeDueTime('),
    (header, 'FORCEINLINE\nBOOLEAN\nKxRemoveTreeTimer('),
    (timer, 'BOOLEAN\nNTAPI\nKeCancelTimer('),
    (timer, 'BOOLEAN\nNTAPI\nKeSetTimerEx('),
    (clock, 'VOID\nNTAPI\nKeSetSystemTime('),
]:
    fixture += '\n' + function(source, signature)
fixture += r'''
static void *set_timer(void *arg) {
    LARGE_INTEGER due;
    due.QuadPart=1000;
    assert(!KeSetTimerEx(arg,due,0,NULL));
    return NULL;
}
static void *adjust_clock(void *arg) {
    LARGE_INTEGER now,old;
    (void)arg;
    while (!__atomic_load_n(&QueryRead,__ATOMIC_ACQUIRE)) sched_yield();
    __atomic_store_n(&ClockStarted,1,__ATOMIC_RELEASE);
    now.QuadPart=400;
    KeSetSystemTime(&now,&old,FALSE,NULL);
    assert(old.QuadPart==100);
    __atomic_store_n(&ClockDone,1,__ATOMIC_RELEASE);
    return NULL;
}
int main(void) {
    KTIMER timer={0}; pthread_t setter,clock_thread; LARGE_INTEGER expired;
    InitializeListHead(&KiTimerTableListHead[0].Entry);
    assert(pthread_mutex_init(&timer.Header.Lock,0)==0);
    __atomic_store_n(&PauseQuery,1,__ATOMIC_RELEASE);
    assert(pthread_create(&setter,0,set_timer,&timer)==0);
    assert(pthread_create(&clock_thread,0,adjust_clock,NULL)==0);
    assert(pthread_join(setter,0)==0);
    assert(pthread_join(clock_thread,0)==0);
    __atomic_store_n(&PauseQuery,0,__ATOMIC_RELEASE);
    assert(timer.Header.Inserted && timer.Header.Absolute);
    assert(timer.DueTime.QuadPart==700);
    assert(KeCancelTimer(&timer));
    assert(!timer.Header.Inserted && IsListEmpty(&KiTimerTableListHead[0].Entry));
    expired.QuadPart=50;
    assert(!KeSetTimerEx(&timer,expired,0,NULL));
    assert(timer.Header.SignalState==1 && !timer.Header.Inserted && SignalCalls==1);
    assert(pthread_mutex_destroy(&timer.Header.Lock)==0);
    puts("setter conversion, adjustment, cancellation, and immediate expiry passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'timer_setter.c'
    exe = pathlib.Path(tmp) / 'timer_setter'
    c.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-pthread', '-Wall', '-Wextra',
                    '-Wno-unused-function', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=8)
