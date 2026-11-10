# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     System-time adjustment and timer cancellation regression
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
clock = (root / 'ntos/ke/clock.c').read_text()
header = (root / 'ntos/include/internal/ke_x.h').read_text()


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    pos = opening + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


clock_function = function(clock, 'VOID\nNTAPI\nKeSetSystemTime(')
needle = '        RemoveEntryList(&Timer->TimerListEntry);'
assert clock_function.count(needle) == 1
clock_function = clock_function.replace(needle, '        TestPause();\n' + needle)
remove_function = function(header, 'FORCEINLINE\nBOOLEAN\nKxRemoveTreeTimer(')
fixture = r'''
#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#define VOID void
#define NTAPI
#define FASTCALL
#define FORCEINLINE
#define IN
#define OUT
#define OPTIONAL
#define FALSE 0
#define TRUE 1
#define DISPATCH_LEVEL 2
#define HIGH_LEVEL 15
#define TIMER_TABLE_SIZE 1
#define ASSERT(x) assert(x)
#define CONTAINING_RECORD(p,t,f) ((t *)((char *)(p)-offsetof(t,f)))
typedef int BOOLEAN;
typedef unsigned char UCHAR, KIRQL;
typedef unsigned long ULONG;
typedef long long LONGLONG; typedef unsigned long long ULONGLONG;
typedef union { long long QuadPart; struct { unsigned int LowPart; int HighPart; }; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct E { struct E *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct { int Inserted, Absolute; UCHAR Hand; } HEADER;
typedef struct { HEADER Header; union { unsigned long long QuadPart; } DueTime; LIST_ENTRY TimerListEntry; } KTIMER, *PKTIMER;
typedef struct { LIST_ENTRY Entry; union { unsigned long long QuadPart; struct { unsigned int LowPart, HighPart; }; } Time; } KTIMER_TABLE_ENTRY, *PKTIMER_TABLE_ENTRY;
typedef pthread_mutex_t *PKSPIN_LOCK_QUEUE;
typedef struct { int unused; } TIME_FIELDS;
static KTIMER_TABLE_ENTRY KiTimerTableListHead[1];
static LARGE_INTEGER KeBootTime;
static LONGLONG KeBootTimeBias;
int ExCmosClockIsSane;
static pthread_mutex_t Bucket=PTHREAD_MUTEX_INITIALIZER, Adjustment=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int AdjustmentHeld;
static int Paused, Attempted, Done;
static KTIMER Timer;
static void InitializeListHead(PLIST_ENTRY e) { e->Flink=e; e->Blink=e; }
static int IsListEmpty(PLIST_ENTRY e) { return e->Flink==e; }
static void InsertTailList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Blink=h->Blink; e->Flink=h; h->Blink->Flink=e; h->Blink=e; }
static int RemoveEntryList(PLIST_ENTRY e) { PLIST_ENTRY a=e->Blink,b=e->Flink; a->Flink=b; b->Blink=a; return a==b; }
static ULONG KiComputeTimerTableIndex(unsigned long long x) { (void)x; return 0; }
static PKSPIN_LOCK_QUEUE KiAcquireTimerLock(ULONG h) { (void)h; assert(pthread_mutex_lock(&Bucket)==0); return &Bucket; }
static void KiReleaseTimerLock(PKSPIN_LOCK_QUEUE l) { assert(pthread_mutex_unlock(l)==0); }
static void KiAcquireTimerAdjustmentLock(void) { assert(pthread_mutex_lock(&Adjustment)==0); AdjustmentHeld=1; }
static void KiReleaseTimerAdjustmentLock(void) { AdjustmentHeld=0; assert(pthread_mutex_unlock(&Adjustment)==0); }
static void KiRemoveEntryTimer(PKTIMER t) { RemoveEntryList(&t->TimerListEntry); }
static int KiInsertTimerTable(PKTIMER t, ULONG h) { (void)h; InsertTailList(&KiTimerTableListHead[0].Entry,&t->TimerListEntry); return 0; }
static void KeSetSystemAffinityThread(int x) { (void)x; }
static void KeRevertToUserAffinityThread(void) { }
static KIRQL KeRaiseIrqlToSynchLevel(void) { return 0; }
static void KeRaiseIrql(KIRQL n, KIRQL *old) { (void)n; *old=2; }
static void KeLowerIrql(KIRQL old) { (void)old; }
static KIRQL KeGetCurrentIrql(void) { return 0; }
static void KeQuerySystemTime(PLARGE_INTEGER t) { t->QuadPart=100; }
static void KiPublishTime(int a, int b, PLARGE_INTEGER c, int d, int e, void *f) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; }
static void RtlTimeToTimeFields(PLARGE_INTEGER t, TIME_FIELDS *f) { (void)t;(void)f; }
static int HalSetRealTimeClock(TIME_FIELDS *f) { (void)f; return 1; }
static void KiTimerListExpire(PLIST_ENTRY h, KIRQL i) { (void)i; assert(IsListEmpty(h)); if (AdjustmentHeld) KiReleaseTimerAdjustmentLock(); }
static void TestPause(void) {
    struct timespec delay={0,1000000}; int i;
    __atomic_store_n(&Paused,1,__ATOMIC_RELEASE);
    while (!__atomic_load_n(&Attempted,__ATOMIC_ACQUIRE)) { }
    for (i=0;i<100 && !__atomic_load_n(&Done,__ATOMIC_ACQUIRE);i++) nanosleep(&delay,0);
}
'''
fixture += '\n' + remove_function + '\n' + clock_function
fixture += r'''
static void *cancel(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&Paused,__ATOMIC_ACQUIRE)) { }
    __atomic_store_n(&Attempted,1,__ATOMIC_RELEASE);
    assert(KxRemoveTreeTimer(&Timer));
    __atomic_store_n(&Done,1,__ATOMIC_RELEASE);
    return NULL;
}
int main(void) {
    pthread_t worker; LARGE_INTEGER old, now;
    InitializeListHead(&KiTimerTableListHead[0].Entry);
    Timer.Header.Inserted=Timer.Header.Absolute=1;
    Timer.DueTime.QuadPart=1000;
    InsertTailList(&KiTimerTableListHead[0].Entry,&Timer.TimerListEntry);
    assert(pthread_create(&worker,0,cancel,0)==0);
    now.QuadPart=200;
    KeSetSystemTime(&now,&old,0,0);
    assert(pthread_join(worker,0)==0);
    assert(__atomic_load_n(&Done,__ATOMIC_ACQUIRE));
    assert(!Timer.Header.Inserted);
    assert(IsListEmpty(&KiTimerTableListHead[0].Entry));
    puts("clock adjustment and cancellation serialized");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'time_adjust.c'
    exe = pathlib.Path(tmp) / 'time_adjust'
    c.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-pthread', '-Wall', '-Wextra',
                    '-Wno-unused-function', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=5)