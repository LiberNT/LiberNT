# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Wait timer insertion and time adjustment regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = pathlib.Path(args.source).read_text()


def function(name, result='VOID'):
    start = source.index('FORCEINLINE\n' + result + '\n' + name + '(')
    opening = source.index('{', start)
    depth = 1
    pos = opening + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


setup = function('KxSetTimerForThreadWait')
insert = function('KxInsertTimerNoRelease')
modern = 'IN PLARGE_INTEGER Interval' in insert
fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define FORCEINLINE
#define VOID void
#define IN
#define OUT
#define ASSERT(x) assert(x)
#define SYNCH_LEVEL 2
#define FALSE 0
#define TRUE 1
#define UserMode 1
typedef unsigned char UCHAR;
typedef unsigned long ULONG, *PULONG;
typedef unsigned long long ULONGLONG;
typedef int KPROCESSOR_MODE; typedef int BOOLEAN;
typedef union { long long QuadPart; struct { unsigned int LowPart; int HighPart; }; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct { int Absolute, Inserted; UCHAR Hand; } HEADER;
typedef struct { HEADER Header; unsigned long long DueTimeValue; } TIMER_STORAGE;
typedef struct { HEADER Header; union { unsigned long long QuadPart; } DueTime; } KTIMER, *PKTIMER;
typedef void *PKSPIN_LOCK_QUEUE;
typedef struct { int SetTimerResolution; } EPROCESS, *PEPROCESS;
typedef struct { struct { EPROCESS *Process; } ApcState; } KTHREAD;
static EPROCESS Process;
static KTHREAD Thread = { { &Process } };
static long long SystemTime=100, InterruptTime=100;
static int Inserts, Completions, ObjectHeld, AdjustmentHeld, BucketHeld;
static KTHREAD *KeGetCurrentThread(void) { return &Thread; }
static void KeQuerySystemTimePrecise(PLARGE_INTEGER t) { t->QuadPart=SystemTime; }
static unsigned long long KiQueryInterruptTimePrecise(void *p, int b) { (void)p;(void)b;return (unsigned long long)InterruptTime; }
static unsigned long long KiRoundToDefaultTimerResolution(unsigned long long x) { return x; }
static ULONG KiComputeTimerTableIndex(unsigned long long x) { (void)x; return 0; }
static int KeGetCurrentIrql(void) { return SYNCH_LEVEL; }
static unsigned long long KeQueryInterruptTime(void) { return (unsigned long long)InterruptTime; }
static void KiAcquireDispatcherObject(HEADER *p) { (void)p; assert(!ObjectHeld); ObjectHeld=1; }
static void KiReleaseDispatcherObject(HEADER *p) { (void)p; assert(ObjectHeld); ObjectHeld=0; }
static void KiAcquireTimerAdjustmentLock(void) { assert(ObjectHeld && !AdjustmentHeld); AdjustmentHeld=1; }
static void KiReleaseTimerAdjustmentLock(void) { assert(AdjustmentHeld); AdjustmentHeld=0; }
static PKSPIN_LOCK_QUEUE KiAcquireTimerLock(ULONG h) { (void)h; assert(!BucketHeld); BucketHeld=1; return (void *)1; }
static void KiReleaseTimerLock(PKSPIN_LOCK_QUEUE l) { assert(l && BucketHeld); BucketHeld=0; }
static int KiInsertTimerTable(PKTIMER t, ULONG h) { (void)t;(void)h;assert(BucketHeld);Inserts++;return 0; }
static void KiCompleteTimer(PKTIMER t, PKSPIN_LOCK_QUEUE l) { (void)t;(void)l;Completions++; }
'''
fixture += '\n' + setup + '\n' + insert
if modern:
    fixture += '\n' + function('KxIsThreadWaitTimerExpired', 'BOOLEAN')
if modern:
    call = 'KxInsertTimerNoRelease(&timer, hand, &interval, 0);'
else:
    call = 'KxInsertTimerNoRelease(&timer, hand);'
fixture += r'''
int main(void) {
    KTIMER timer={0}; LARGE_INTEGER interval; ULONG hand=0;
    interval.QuadPart=1000;
    KxSetTimerForThreadWait(&timer,interval,0,&hand);
    assert(timer.DueTime.QuadPart==1000);
    SystemTime=400;
    timer.Header.Inserted=1;
''' + call + r'''
    assert(Inserts==1 && timer.DueTime.QuadPart==700);
    assert(!AdjustmentHeld && !BucketHeld && !ObjectHeld);
    timer.Header.Inserted=0;
    timer.DueTime.QuadPart=123;
''' + call + r'''
    assert(Inserts==1 && timer.DueTime.QuadPart==123);
    interval.QuadPart=-50;
    SystemTime=100;
    KxSetTimerForThreadWait(&timer,interval,0,&hand);
    assert(timer.DueTime.QuadPart==150);
    SystemTime=400;
    timer.Header.Inserted=1;
''' + call + r'''
    assert(Inserts==2 && timer.DueTime.QuadPart==150);
    assert(!AdjustmentHeld && !BucketHeld && !ObjectHeld && !Completions);
'''
if modern:
    fixture += r'''
    interval.QuadPart=1000;
    SystemTime=100;
    InterruptTime=100;
    KxSetTimerForThreadWait(&timer,interval,0,&hand);
    SystemTime=400;
    InterruptTime=700;
    ObjectHeld=1;
    assert(!KxIsThreadWaitTimerExpired(&timer,&interval,0,&hand));
    assert(timer.DueTime.QuadPart==1300);
    assert(!AdjustmentHeld);
    SystemTime=1000;
    assert(KxIsThreadWaitTimerExpired(&timer,&interval,0,&hand));
    assert(!AdjustmentHeld);
    ObjectHeld=0;
'''
fixture += r'''
    puts("absolute, canceled, relative, and expiry-check cases passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'timer_insert.c'
    exe = pathlib.Path(tmp) / 'timer_insert'
    c.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Wno-unused-function',
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)