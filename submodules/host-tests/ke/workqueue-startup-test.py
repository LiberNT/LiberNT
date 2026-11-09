# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)+# PURPOSE:     Early executive work queue regression
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


def body(path, name):
    source = (root / path).read_text()
    start = source.index('\n' + name + '(')
    start = source.index('{', start)
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start + 1:end - 1]


fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef unsigned int ULONG;
typedef int32_t LONG, *PLONG;
typedef uintptr_t ULONG_PTR;
typedef int BOOLEAN, NTSTATUS, WORK_QUEUE_TYPE;
typedef void *PVOID, *HANDLE, *PETHREAD;
typedef struct LIST_ENTRY { struct LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct { int Type; } HEADER;
typedef struct { HEADER Header; LIST_ENTRY EntryListHead; ULONG CurrentCount, MaximumCount; } KQUEUE;
typedef struct { int MakeThreadsAsNecessary, QueueDisabled; } INFO;
typedef struct { KQUEUE WorkerQueue; INFO Info; ULONG DynamicThreadCount; } EX_WORK_QUEUE, *PEX_WORK_QUEUE;
typedef struct { LIST_ENTRY List; void (*WorkerRoutine)(void *); } WORK_QUEUE_ITEM, *PWORK_QUEUE_ITEM;
typedef struct { int Initialized, Signaled; } EVENT;
#define CriticalWorkQueue 0
#define DelayedWorkQueue 1
#define HyperCriticalWorkQueue 2
#define MaximumWorkQueue 3
#define QueueObject 7
#define FALSE 0
#define TRUE 1
#define FirmwareTypeUefi 1
#define FirmwareTypeBios 0
#define EX_DELAYED_WORK_RESERVE 6
#define EX_MAX_DELAYED_WORK_THREADS 16
#define EX_CRITICAL_WORK_THREADS 5
#define EX_HYPERCRITICAL_WORK_THREADS 1
#define SynchronizationEvent 1
#define NotificationEvent 2
#define THREAD_ALL_ACCESS 0
#define KernelMode 0
#define PHASE1_INITIALIZATION_FAILED 1
#define WORKER_INVALID 2
#define ASSERT(x) assert(x)
#define DPRINT(...) ((void)0)
#define DPRINT1(...) ((void)0)
#define NT_SUCCESS(s) ((s) == 0)
#define min(a,b) ((a)<(b)?(a):(b))
#define RtlZeroMemory(p,n) memset(p,0,n)
static EX_WORK_QUEUE ExWorkerQueue[MaximumWorkQueue];
static ULONG ExpStaticWorkerCounts[MaximumWorkQueue], KeNumberProcessors = 1;
static ULONG ExpAdditionalDelayedWorkerThreads, ExpAdditionalCriticalWorkerThreads;
static ULONG ExCriticalWorkerThreads, ExDelayedWorkerThreads;
static int ExpWorkersCanSwap, ExpWorkerSwapinMutex, ExpEnvironmentLock;
static LIST_ENTRY ExpWorkerListHead, ExpFirmwareTableProviderListHead;
static int ExpFirmwareTableResource, ExpTimeRefreshLock, ExpFirmwareType, ExSuiteMask;
static EVENT ExpThreadSetManagerEvent, ExpThreadSetManagerShutdownEvent;
static PETHREAD ExpWorkerThreadBalanceManagerPtr;
static ULONG_PTR MmUserProbeAddress;
static struct { struct { int FirmwareTypeEfi; } FirmwareInformation; } Loader, *KeLoaderBlock = &Loader;
static void InitializeListHead(LIST_ENTRY *h) { h->Flink = h->Blink = h; }
static int IsListEmpty(LIST_ENTRY *h) { return h->Flink == h; }
static void KeInitializeQueue(KQUEUE *q, ULONG count) {
    q->Header.Type = QueueObject; InitializeListHead(&q->EntryListHead);
    q->CurrentCount = 0; q->MaximumCount = count ? count : KeNumberProcessors;
}
static void KeInsertQueue(KQUEUE *q, LIST_ENTRY *e) {
    LIST_ENTRY *h = &q->EntryListHead;
    assert(q->Header.Type == QueueObject);
    e->Blink = h->Blink; e->Flink = h; h->Blink->Flink = e; h->Blink = e;
}
static void KeInitializeEvent(EVENT *e, int type, int state) { (void)type; (void)state; e->Initialized = 1; }
static void KeSetEvent(EVENT *e, int a, int b) { (void)a; (void)b; assert(e->Initialized); e->Signaled = 1; }
static LONG InterlockedExchange(LONG *p, LONG value) { LONG old = *p; *p = value; return old; }
static void ExInitializeFastMutex(int *p) { (void)p; }
static void ExInitializeResourceLite(int *p) { (void)p; }
static void ExpResourceInitialization(void) {}
static void ExpInitLookasideLists(void) {}
static int ExpGetSuiteMask(void) { return 0; }
static void ExpCreateWorkerThread(int q, int dynamic) { (void)q; (void)dynamic; }
static void ExpWorkerThreadBalanceManager(void) {}
static NTSTATUS PsCreateSystemThread(HANDLE *h, int a, void *b, int c, void *d, void (*e)(void), void *f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; *h = (void *)1; return 0;
}
static void ObReferenceObjectByHandle(HANDLE h, int a, void *b, int c, void **p, void *d) {
    (void)h; (void)a; (void)b; (void)c; (void)d; *p = (void *)1;
}
static void ObCloseHandle(HANDLE h, int mode) { (void)h; (void)mode; }
static void KeBugCheckEx(int code, ...) { (void)code; assert(0); }
static BOOLEAN ExpNewThreadNecessary(WORK_QUEUE_TYPE WorkQueueType) { @NECESSARY@ }
static void ExpInitializeWorkerQueues(void) { @QUEUES@ }
static BOOLEAN ExpInitSystemPhase0(void) { @PHASE0@ }
static void ExpInitializeWorkerThreads(void) { @THREADS@ }
static void ExQueueWorkItem(PWORK_QUEUE_ITEM WorkItem, WORK_QUEUE_TYPE QueueType) { @ENQUEUE@ }
static void Worker(void *p) { (void)p; }
int main(void) {
    WORK_QUEUE_ITEM Items[MaximumWorkQueue] = {0}, Later = {0};
    ULONG i;
    assert(ExpInitSystemPhase0());
    for (i = 0; i < MaximumWorkQueue; i++) {
        Items[i].WorkerRoutine = Worker;
        ExQueueWorkItem(&Items[i], i);
        assert(ExWorkerQueue[i].WorkerQueue.EntryListHead.Flink == &Items[i].List);
    }
    KeNumberProcessors = 4;
    ExpInitializeWorkerThreads();
    for (i = 0; i < MaximumWorkQueue; i++) {
        assert(ExWorkerQueue[i].WorkerQueue.EntryListHead.Flink == &Items[i].List);
        assert(ExWorkerQueue[i].WorkerQueue.MaximumCount == 4);
    }
    Later.WorkerRoutine = Worker;
    ExQueueWorkItem(&Later, CriticalWorkQueue);
    assert(Items[CriticalWorkQueue].List.Flink == &Later.List);
    assert(ExpThreadSetManagerEvent.Signaled);
    assert(ExCriticalWorkerThreads == 5 && ExDelayedWorkerThreads == 10);
    assert(ExpWorkersCanSwap && ExpFirmwareType == FirmwareTypeBios && ExSuiteMask == 0);
    assert(ExpWorkerThreadBalanceManagerPtr == (void *)1);
    return 0;
}
'''
for marker, path, name in [
    ('NECESSARY', 'ntos/ex/work.c', 'ExpNewThreadNecessary'),
    ('PHASE0', 'ntos/ex/init.c', 'ExpInitSystemPhase0'),
    ('THREADS', 'ntos/ex/work.c', 'ExpInitializeWorkerThreads'),
    ('ENQUEUE', 'ntos/ex/work.c', 'ExQueueWorkItem'),
]:
    fixture = fixture.replace('@' + marker + '@', body(path, name))
try:
    queues = body('ntos/ex/work.c', 'ExpInitializeWorkerQueues')
except ValueError:
    queues = ''
fixture = fixture.replace('@QUEUES@', queues)

with tempfile.TemporaryDirectory(prefix='ex-workqueue-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
