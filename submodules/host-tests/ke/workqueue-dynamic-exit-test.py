# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A dynamic executive worker that goes away gives back only its own count
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
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
#include <stdlib.h>
#include <string.h>
typedef unsigned int ULONG;
typedef int32_t LONG, *PLONG, NTSTATUS, KPRIORITY;
typedef uintptr_t ULONG_PTR;
typedef unsigned char BOOLEAN, KIRQL, UCHAR;
typedef int WORK_QUEUE_TYPE;
typedef void *PVOID, *HANDLE;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct { LIST_ENTRY EntryListHead; } KQUEUE;
typedef struct { KQUEUE WorkerQueue; LONG DynamicThreadCount; ULONG WorkItemsProcessed; } EX_WORK_QUEUE, *PEX_WORK_QUEUE;
typedef void (*PWORKER_THREAD_ROUTINE)(PVOID);
typedef struct { LIST_ENTRY List; PWORKER_THREAD_ROUTINE WorkerRoutine; PVOID Parameter; } WORK_QUEUE_ITEM, *PWORK_QUEUE_ITEM;
typedef struct { UCHAR ApcStateIndex; } KTHREAD, *PKTHREAD;
typedef struct { WORK_QUEUE_TYPE QueueType; BOOLEAN Dynamic; } EXP_WORKER_THREAD_CONTEXT, *PEXP_WORKER_THREAD_CONTEXT;
typedef struct { int Unused; } EXP_WORKER_THREAD, *PEXP_WORKER_THREAD;
typedef union { long long QuadPart; } LARGE_INTEGER;
#define CriticalWorkQueue 0
#define DelayedWorkQueue 1
#define HyperCriticalWorkQueue 2
#define MaximumWorkQueue 3
#define KernelMode 0
#define PASSIVE_LEVEL 0
#define STATUS_TIMEOUT ((NTSTATUS)0x00000102)
#define STATUS_USER_APC ((NTSTATUS)0x000000C0)
#define EXP_WORKER_TAG 0
#define EXP_DEADLOCK_INTERVAL (-30 * 10 * 1000 * 1000)
#define WORKER_THREAD_RETURNED_AT_BAD_IRQL 0xE1
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define ASSERT(x) assert(x)
#define InterlockedDecrement(p) (--*(p))
static EX_WORK_QUEUE ExWorkerQueue[MaximumWorkQueue];
static ULONG ExpAdditionalCriticalWorkerThreads, ExpAdditionalDelayedWorkerThreads;
static KTHREAD CurrentThread;
static WORK_QUEUE_ITEM Item;
static int Removals, Ran, Registered;
static PKTHREAD KeGetCurrentThread(void) { return &CurrentThread; }
static KIRQL KeGetCurrentIrql(void) { return PASSIVE_LEVEL; }
static void ExFreePoolWithTag(PVOID p, ULONG tag) { (void)tag; free(p); }
static void WritePointerRelease(PVOID volatile *Destination, PVOID Value) { *Destination = Value; }
static KPRIORITY ExpWorkerPriority(WORK_QUEUE_TYPE q) { (void)q; return 12; }
static void KeSetBasePriorityThread(PKTHREAD t, KPRIORITY p) { (void)t; (void)p; }
static PEXP_WORKER_THREAD ExpRegisterWorkerThread(WORK_QUEUE_TYPE q, BOOLEAN dynamic) {
    static EXP_WORKER_THREAD Worker;
    (void)q; (void)dynamic; Registered++; return &Worker;
}
static void ExpDeregisterWorkerThread(PEXP_WORKER_THREAD w) { (void)w; Registered--; }
static PLIST_ENTRY KeRemoveQueue(KQUEUE *q, int mode, LARGE_INTEGER *timeout) {
    (void)q; (void)mode;
    assert(timeout != NULL);
    return (Removals++ == 0) ? &Item.List : (PLIST_ENTRY)(ULONG_PTR)STATUS_TIMEOUT;
}
static void KeBugCheckEx(ULONG code, ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d) {
    (void)code; (void)a; (void)b; (void)c; (void)d; assert(0);
}
static void Worker(PVOID p) { (void)p; Ran++; }
static void ExpWorkerThreadEntryPoint(PVOID Context) { @ENTRY@ }
static void RunDynamicWorker(WORK_QUEUE_TYPE QueueType) {
    PEXP_WORKER_THREAD_CONTEXT Context = malloc(sizeof(*Context));
    Context->QueueType = QueueType;
    Context->Dynamic = 1;
    Removals = 0;
    Item.WorkerRoutine = Worker;
    ExWorkerQueue[QueueType].DynamicThreadCount = 1;
    ExpWorkerThreadEntryPoint(Context);
    assert(ExWorkerQueue[QueueType].DynamicThreadCount == 0);
}
int main(void) {
    RunDynamicWorker(CriticalWorkQueue);
    RunDynamicWorker(DelayedWorkQueue);
    assert(Ran == 2 && Registered == 0);
    assert(ExpAdditionalCriticalWorkerThreads == 0);
    assert(ExpAdditionalDelayedWorkerThreads == 0);
    return 0;
}
'''
fixture = fixture.replace('@ENTRY@', body('ExpWorkerThreadEntryPoint'))

with tempfile.TemporaryDirectory(prefix='ex-workqueue-exit-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
