# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Early executive work queue regression
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
source = (root / 'ntos/ex/exworker.c').read_text()


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
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef unsigned int ULONG;
typedef int32_t LONG;
typedef uintptr_t ULONG_PTR, KSPIN_LOCK;
typedef unsigned char BOOLEAN, KIRQL;
typedef int NTSTATUS, WORK_QUEUE_TYPE;
typedef void *PVOID, *HANDLE;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct { int Type; } DISPATCHER_HEADER;
typedef struct { DISPATCHER_HEADER Header; LIST_ENTRY EntryListHead; ULONG CurrentCount, MaximumCount; } KQUEUE;
typedef struct { BOOLEAN MakeThreadsAsNecessary; } EX_QUEUE_INFO;
typedef struct {
    KQUEUE WorkerQueue;
    ULONG DynamicThreadCount, WorkItemsProcessed, WorkItemsProcessedLastPass, QueueDepthLastPass;
    EX_QUEUE_INFO Info;
} EX_WORK_QUEUE;
typedef struct { LIST_ENTRY List; void (*WorkerRoutine)(PVOID); PVOID Parameter; } WORK_QUEUE_ITEM, *PWORK_QUEUE_ITEM;
typedef struct { int Initialized; } KEVENT, FAST_MUTEX;
#define CriticalWorkQueue 0
#define DelayedWorkQueue 1
#define HyperCriticalWorkQueue 2
#define MaximumWorkQueue 3
#define QueueObject 7
#define FALSE 0
#define TRUE 1
#define NotificationEvent 0
#define SynchronizationEvent 1
#define THREAD_ALL_ACCESS 0
#define KernelMode 0
#define EXP_CRITICAL_WORKERS_BASE 5
#define EXP_DELAYED_WORKERS_BASE 3
#define EXP_HYPERCRITICAL_WORKERS 1
#define ASSERT(x) assert(x)
#define NT_SUCCESS(s) ((s) >= 0)
static EX_WORK_QUEUE ExWorkerQueue[MaximumWorkQueue];
static ULONG KeNumberProcessors = 1, ExCriticalWorkerThreads, ExDelayedWorkerThreads, WorkersCreated;
static LIST_ENTRY ExpWorkerListHead;
static KSPIN_LOCK ExpWorkerListLock, ExpEarlyWorkLock;
static FAST_MUTEX ExpWorkerSwapinMutex;
static KEVENT ExpThreadSetManagerEvent, ExpThreadSetManagerShutdownEvent;
static PVOID ExpWorkerThreadBalanceManagerPtr, PsThreadType;
static PLIST_ENTRY ExpEarlyWork[MaximumWorkQueue];
static BOOLEAN ExpWorkerQueuesReady;
static int LockHeld;
static void InitializeListHead(PLIST_ENTRY h) { h->Flink = h->Blink = h; }
static void KeInitializeSpinLock(KSPIN_LOCK *l) { *l = 0; }
static void KeAcquireSpinLock(KSPIN_LOCK *l, KIRQL *irql) { (void)l; assert(!LockHeld); LockHeld = 1; *irql = 0; }
static void KeReleaseSpinLock(KSPIN_LOCK *l, KIRQL irql) { (void)l; (void)irql; assert(LockHeld); LockHeld = 0; }
static BOOLEAN ReadBooleanAcquire(const BOOLEAN *p) { return *p; }
static void WriteBooleanRelease(BOOLEAN *p, BOOLEAN v) { assert(LockHeld); *p = v; }
static void ExInitializeFastMutex(FAST_MUTEX *m) { m->Initialized = 1; }
static void KeInitializeEvent(KEVENT *e, int type, int state) { (void)type; (void)state; e->Initialized = 1; }
static void KeInitializeQueue(KQUEUE *q, ULONG count) {
    q->Header.Type = QueueObject;
    InitializeListHead(&q->EntryListHead);
    q->CurrentCount = 0;
    q->MaximumCount = count ? count : KeNumberProcessors;
}
static void KeInsertQueue(KQUEUE *q, PLIST_ENTRY e) {
    PLIST_ENTRY h = &q->EntryListHead;
    assert(q->Header.Type == QueueObject);
    e->Blink = h->Blink; e->Flink = h; h->Blink->Flink = e; h->Blink = e;
}
static void ExpCreateWorkerThread(WORK_QUEUE_TYPE q, BOOLEAN dynamic) {
    (void)q; (void)dynamic;
    assert(ExpWorkerQueuesReady);
    WorkersCreated++;
}
static void ExpWorkerThreadBalanceManager(PVOID p) { (void)p; }
static NTSTATUS PsCreateSystemThread(HANDLE *h, int a, PVOID b, PVOID c, PVOID d, void (*e)(PVOID), PVOID f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; *h = (HANDLE)1; return 0;
}
static NTSTATUS ObReferenceObjectByHandle(HANDLE h, int a, PVOID b, int c, PVOID *p, PVOID d) {
    (void)h; (void)a; (void)b; (void)c; (void)d; *p = (PVOID)1; return 0;
}
static void ObCloseHandle(HANDLE h, int mode) { (void)h; (void)mode; }
static BOOLEAN ExpHoldEarlyWorkItem(PWORK_QUEUE_ITEM WorkItem, WORK_QUEUE_TYPE QueueType) { @HOLD@ }
static void ExpReleaseEarlyWork(void) { @RELEASE@ }
static void ExpInitializeWorkerThreads(void) { @THREADS@ }
static void ExQueueWorkItem(PWORK_QUEUE_ITEM WorkItem, WORK_QUEUE_TYPE QueueType) { @ENQUEUE@ }
static void Worker(PVOID p) { (void)p; }
int main(void) {
    WORK_QUEUE_ITEM Early[MaximumWorkQueue], Second, Later;
    ULONG i;
    memset(Early, 0, sizeof(Early));
    memset(&Second, 0, sizeof(Second));
    memset(&Later, 0, sizeof(Later));
    for (i = 0; i < MaximumWorkQueue; i++) {
        Early[i].WorkerRoutine = Worker;
        ExQueueWorkItem(&Early[i], i);
        assert(ExWorkerQueue[i].WorkerQueue.Header.Type != QueueObject);
    }
    Second.WorkerRoutine = Worker;
    ExQueueWorkItem(&Second, CriticalWorkQueue);
    assert(!LockHeld && !ExpWorkerQueuesReady);
    KeNumberProcessors = 4;
    ExpInitializeWorkerThreads();
    assert(ExpWorkerQueuesReady && !LockHeld && WorkersCreated != 0);
    for (i = 0; i < MaximumWorkQueue; i++) {
        assert(ExpEarlyWork[i] == NULL);
        assert(ExWorkerQueue[i].WorkerQueue.MaximumCount == 4);
        assert(ExWorkerQueue[i].WorkerQueue.EntryListHead.Flink == &Early[i].List);
    }
    assert(Early[CriticalWorkQueue].List.Flink == &Second.List);
    assert(Second.List.Flink == &ExWorkerQueue[CriticalWorkQueue].WorkerQueue.EntryListHead);
    Later.WorkerRoutine = Worker;
    ExQueueWorkItem(&Later, CriticalWorkQueue);
    assert(Second.List.Flink == &Later.List);
    assert(ExpWorkerThreadBalanceManagerPtr == (PVOID)1);
    return 0;
}
'''
for marker, name in [
    ('HOLD', 'ExpHoldEarlyWorkItem'),
    ('RELEASE', 'ExpReleaseEarlyWork'),
    ('THREADS', 'ExpInitializeWorkerThreads'),
    ('ENQUEUE', 'ExQueueWorkItem'),
]:
    fixture = fixture.replace('@' + marker + '@', body(name))

with tempfile.TemporaryDirectory(prefix='ex-workqueue-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
