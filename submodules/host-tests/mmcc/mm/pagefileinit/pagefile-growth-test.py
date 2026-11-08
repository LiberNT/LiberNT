# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Paging-file growth handoff regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
"""Exercise pending growth requests and stale event wakeups."""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
s = pathlib.Path(args.source).read_text()
def body(name):
    p=s.index(name+'('); a=s.index('{',p); d=1; b=a+1
    while d:
        d+=(s[b]=='{')-(s[b]=='}'); b+=1
    return s[a+1:b-1]
worker=body('MiExtendPagingFileWorker')
expand=body('MiExpandCommit')
fixture=r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sched.h>
#include <time.h>
typedef void VOID; typedef void *PVOID; typedef void *PKTHREAD; typedef int BOOLEAN; typedef int LONG;
typedef long long LONG64; typedef unsigned long long ULONG64; typedef int KIRQL;
typedef struct { LONG64 QuadPart; } LARGE_INTEGER;
typedef struct { ULONG64 SlotCount; } CORE;
typedef struct { CORE Core; ULONG64 MaximumSlots; } MI_NT_PAGEFILE, *PMI_NT_PAGEFILE;
typedef struct { volatile LONG64 CommittedPages, CommitLimit; } MI_SYSTEM, *PMI_SYSTEM;
typedef int WORK_QUEUE_ITEM;
#define ASSERT(x) assert(x)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define FALSE 0
#define TRUE 1
#define IO_NO_INCREMENT 0
#define CriticalWorkQueue 0
#define DelayedWorkQueue 1
#define Executive 0
#define KernelMode 0
#define APC_LEVEL 1
#define MI_PAGEFILE_GROWTH_PAGES ((64ULL * 1024 * 1024) >> 12)
#define MI_PAGEFILE_EXTEND_BACKOFF (10ULL * 1000 * 1000 * 10)
#define MI_PAGEFILE_EXTEND_WAIT (30ULL * 1000 * 1000 * 10)
#define MI_ATOMIC_READ32(p) __atomic_load_n((p),__ATOMIC_SEQ_CST)
#define MI_ATOMIC_READ64(p) __atomic_load_n((p),__ATOMIC_SEQ_CST)
static MI_SYSTEM MiSystem;
static PMI_NT_PAGEFILE MiPagingFile;
static WORK_QUEUE_ITEM MiPagingFileExtendItem, MiCommitReportItem;
static int MiPagingFileExtendEvent;
static volatile LONG MiPagingFileExtendQueued, MiCommitReportQueued;
static volatile LONG64 MiPagingFileExtendPages;
static ULONG64 MiPagingFileExtendFailTime;
static PKTHREAD MiPagingFileExtendThread;
static volatile LONG Entered, Resume, ExtendCalls, WaitCalls;
static pthread_t Worker;
static LONG64 InterlockedExchange64(volatile LONG64 *p, LONG64 v) { return __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST); }
static LONG InterlockedExchange(volatile LONG *p, LONG v) { return __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST); }
static LONG64 InterlockedCompareExchange64(volatile LONG64 *p,LONG64 v,LONG64 o) { __atomic_compare_exchange_n(p,&o,v,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return o; }
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG o) { __atomic_compare_exchange_n(p,&o,v,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return o; }
static void *InterlockedExchangePointer(void *volatile *p, void *v) { return __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST); }
static void *InterlockedCompareExchangePointer(void *volatile *p, void *v, void *o) { __atomic_compare_exchange_n(p,&o,v,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return o; }
static PKTHREAD KeGetCurrentThread(void) { return (void *)(uintptr_t)pthread_self(); }
static KIRQL KeGetCurrentIrql(void) { return 0; }
static ULONG64 KeQueryInterruptTime(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (ULONG64)t.tv_sec*10000000ULL+(ULONG64)t.tv_nsec/100; }
static void KeSetEvent(int *Event,int Priority,int Wait) { __atomic_store_n(Event,1,__ATOMIC_RELEASE); (void)Priority; (void)Wait; }
static void KeClearEvent(int *Event) { __atomic_store_n(Event,0,__ATOMIC_RELEASE); }
static void KeWaitForSingleObject(int *Event,int Reason,int Mode,int Alert,LARGE_INTEGER *Timeout) { __atomic_add_fetch(&WaitCalls,1,__ATOMIC_SEQ_CST); while (!__atomic_load_n(Event,__ATOMIC_ACQUIRE)) sched_yield(); (void)Reason;(void)Mode;(void)Alert;(void)Timeout; }
static void MiCommitReportWorker(void *Context) { (void)Context; }
static void ExInitializeWorkItem(WORK_QUEUE_ITEM *Item,void (*Routine)(void *),void *Context) { (void)Item;(void)Routine;(void)Context; }
static void MiExtendPagingFileWorker(void *Context);
static void *RunWorker(void *Context) { MiExtendPagingFileWorker(Context); return NULL; }
static void ExQueueWorkItem(WORK_QUEUE_ITEM *Item,int Queue) { (void)Item;(void)Queue; assert(pthread_create(&Worker,NULL,RunWorker,MiPagingFile)==0); }
static BOOLEAN MiExtendPagingFileTo(PMI_NT_PAGEFILE File, ULONG64 Target)
{
    LONG Calls=__atomic_add_fetch(&ExtendCalls,1,__ATOMIC_SEQ_CST);
    if (Calls==1) { __atomic_store_n(&Entered,1,__ATOMIC_RELEASE); while (!__atomic_load_n(&Resume,__ATOMIC_ACQUIRE)) sched_yield(); }
    __atomic_add_fetch(&MiSystem.CommitLimit,(LONG64)(Target-File->Core.SlotCount),__ATOMIC_SEQ_CST);
    File->Core.SlotCount=Target;
    return TRUE;
}
static void MiExtendPagingFileWorker(void *Context) { @WORKER@ }
static BOOLEAN MiExpandCommit(MI_SYSTEM *System,LONG64 Pages,LONG64 Limit,BOOLEAN Wait) { @EXPAND@ }
static void *RunWaiter(void *Unused) { (void)Unused; MiExpandCommit(&MiSystem,100000,100,TRUE); return NULL; }
int main(void)
{
    MI_NT_PAGEFILE File={{100},1000000};
    pthread_t Waiter;
    struct timespec Delay={0,30000000};
    LONG Waits;
    MiPagingFile=&File; MiSystem.CommittedPages=100; MiSystem.CommitLimit=100;
    MiExpandCommit(&MiSystem,1,100,FALSE);
    while (!__atomic_load_n(&Entered,__ATOMIC_ACQUIRE)) sched_yield();
    MiExpandCommit(&MiSystem,100000,100,FALSE);
    __atomic_store_n(&MiPagingFileExtendEvent,1,__ATOMIC_RELEASE);
    assert(pthread_create(&Waiter,NULL,RunWaiter,NULL)==0);
    while (!__atomic_load_n(&WaitCalls,__ATOMIC_ACQUIRE)) sched_yield();
    nanosleep(&Delay,NULL);
    Waits=__atomic_load_n(&WaitCalls,__ATOMIC_ACQUIRE);
    __atomic_store_n(&Resume,1,__ATOMIC_RELEASE);
    assert(pthread_join(Worker,NULL)==0);
    assert(pthread_join(Waiter,NULL)==0);
    printf("pending=%lld queued=%d extensions=%d wait_calls=%d\n",MiPagingFileExtendPages,MiPagingFileExtendQueued,ExtendCalls,Waits);
    return MiPagingFileExtendPages != 0 || MiPagingFileExtendQueued != 0 || ExtendCalls < 2 || Waits > 2;
}
'''.replace('@WORKER@',worker).replace('@EXPAND@',expand)
with tempfile.TemporaryDirectory(prefix='mm-pending-test-') as d:
 p=pathlib.Path(d); (p/'test.c').write_text(fixture)
 subprocess.run([args.cc,'-std=c11','-pthread',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=10)
