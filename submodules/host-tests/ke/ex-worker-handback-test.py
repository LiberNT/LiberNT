# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A system worker runs the routine an item was queued with and reports it even after the routine reused the item
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'ntos/ex/exworker.c').read_text()


def definition(name):
    index = source.index('\n' + name + '(')
    start = index
    while True:
        line_start = source.rfind('\n', 0, start) + 1
        line = source[line_start:start].strip()
        if line == '' or line.endswith('*/') or line.endswith(';') or line.endswith('}') or line.startswith('#'):
            break
        start = line_start - 1
    start = start + 1 if source[start] == '\n' else start
    depth = 1
    end = source.index('{', index) + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


context = source[source.index('typedef struct _EXP_WORKER_THREAD_CONTEXT'):source.index('} EXP_WORKER_THREAD_CONTEXT, *PEXP_WORKER_THREAD_CONTEXT;')]
context += '} EXP_WORKER_THREAD_CONTEXT, *PEXP_WORKER_THREAD_CONTEXT;\n'

fixture = r'''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VOID void
#define NTAPI
#define _In_
#define TRUE 1
#define FALSE 0
#define KernelMode 0
#define PASSIVE_LEVEL 0
#define DISPATCH_LEVEL 2
#define STATUS_TIMEOUT ((NTSTATUS)0x00000102)
#define STATUS_USER_APC ((NTSTATUS)0x000000C0)
#define WORKER_THREAD_RETURNED_AT_BAD_IRQL 0xE1
#define EXP_WORKER_TAG 0x6b726f57
#define EXP_DEADLOCK_INTERVAL (-30 * 10 * 1000 * 1000)
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
typedef unsigned char BOOLEAN, UCHAR, KIRQL;
typedef uint32_t ULONG;
typedef int32_t LONG, NTSTATUS;
typedef uintptr_t ULONG_PTR;
typedef void *PVOID;
typedef union { int64_t QuadPart; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef enum { CriticalWorkQueue, DelayedWorkQueue, HyperCriticalWorkQueue, MaximumWorkQueue } WORK_QUEUE_TYPE;
typedef void (*PWORKER_THREAD_ROUTINE)(PVOID);
typedef struct { LIST_ENTRY List; PWORKER_THREAD_ROUTINE WorkerRoutine; PVOID Parameter; } WORK_QUEUE_ITEM, *PWORK_QUEUE_ITEM;
typedef struct { int Unused; } KQUEUE;
typedef struct { KQUEUE WorkerQueue; ULONG WorkItemsProcessed; LONG DynamicThreadCount; } EX_WORK_QUEUE, *PEX_WORK_QUEUE;
typedef struct { UCHAR ApcStateIndex; } KTHREAD, *PKTHREAD;
typedef struct _EXP_WORKER_THREAD *PEXP_WORKER_THREAD;
@CONTEXT@
static const char *Scenario = "setup";
static EX_WORK_QUEUE ExWorkerQueue[MaximumWorkQueue];
static KTHREAD TheThread;
static KIRQL Irql;
static WORK_QUEUE_ITEM Item;
static int Removes, Stops;
static ULONG_PTR Stop[5];
static PKTHREAD KeGetCurrentThread(void) { return &TheThread; }
static KIRQL KeGetCurrentIrql(void) { return Irql; }
static void ExFreePoolWithTag(PVOID P, ULONG Tag) { (void)Tag; free(P); }
static void KeSetBasePriorityThread(PKTHREAD Thread, LONG Priority) { (void)Thread; (void)Priority; }
static LONG ExpWorkerPriority(WORK_QUEUE_TYPE Type) { (void)Type; return 12; }
static PEXP_WORKER_THREAD ExpRegisterWorkerThread(WORK_QUEUE_TYPE Type, BOOLEAN Dynamic) { (void)Type; (void)Dynamic; return NULL; }
static void ExpDeregisterWorkerThread(PEXP_WORKER_THREAD Worker) { (void)Worker; }
static LONG InterlockedDecrement(volatile LONG *Value) { return --*Value; }
static PLIST_ENTRY KeRemoveQueue(KQUEUE *Queue, int Mode, PLARGE_INTEGER Timeout) {
    (void)Queue; (void)Mode; (void)Timeout;
    return (Removes++ == 0) ? &Item.List : (PLIST_ENTRY)(ULONG_PTR)STATUS_TIMEOUT;
}
static void KeBugCheckEx(ULONG Code, ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3, ULONG_PTR P4) {
    Stops++;
    Stop[0] = Code; Stop[1] = P1; Stop[2] = P2; Stop[3] = P3; Stop[4] = P4;
}
static PWORKER_THREAD_ROUTINE Reinitialize;
static int Ran, Wrong;
static int Argument, OtherArgument;
/* Whoever queued the item may set it up again the moment it is handed back */
static void WritePointerRelease(PVOID volatile *Destination, PVOID Value) {
    *Destination = Value;
    if (Reinitialize != NULL && Destination == (PVOID volatile *)&Item.List.Flink) {
        Item.WorkerRoutine = Reinitialize;
        Item.Parameter = &OtherArgument;
    }
}
'''
fixture = fixture.replace('@CONTEXT@', context)
fixture += definition('ExpWorkerThreadEntryPoint')
fixture += r'''
static void Other(PVOID Parameter) { (void)Parameter; Wrong++; }
static void Queued(PVOID Parameter) { CHECK(Parameter == &Argument); Ran++; }
/* A routine may reuse its item, and this one also forgets to come back at PASSIVE_LEVEL */
static void ReuseAndRaise(PVOID Parameter) {
    CHECK(Parameter == &Argument);
    Ran++;
    memset(&Item, 0xA5, sizeof(Item));
    Irql = DISPATCH_LEVEL;
}
static void Run(PWORKER_THREAD_ROUTINE Routine) {
    PVOID Context = malloc(sizeof(EXP_WORKER_THREAD_CONTEXT));
    ((PEXP_WORKER_THREAD_CONTEXT)Context)->QueueType = DelayedWorkQueue;
    ((PEXP_WORKER_THREAD_CONTEXT)Context)->Dynamic = TRUE;
    ExWorkerQueue[DelayedWorkQueue].DynamicThreadCount = 1;
    Item.List.Flink = (PLIST_ENTRY)&Item;
    Item.WorkerRoutine = Routine;
    Item.Parameter = &Argument;
    Removes = Stops = Ran = Wrong = 0;
    Irql = PASSIVE_LEVEL;
    ExpWorkerThreadEntryPoint(Context);
}
int main(void) {
    Scenario = "an item set up again as soon as it is handed back";
    Reinitialize = Other;
    Run(Queued);
    CHECK(Ran == 1 && Wrong == 0 && Stops == 0);
    Reinitialize = NULL;

    Scenario = "a routine that reused its item and returned at DISPATCH_LEVEL";
    Run(ReuseAndRaise);
    CHECK(Ran == 1 && Stops == 1);
    CHECK(Stop[0] == WORKER_THREAD_RETURNED_AT_BAD_IRQL);
    CHECK(Stop[1] == (ULONG_PTR)ReuseAndRaise && Stop[2] == DISPATCH_LEVEL);
    CHECK(Stop[3] == (ULONG_PTR)&Argument && Stop[4] == (ULONG_PTR)&Item);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='ex-worker-handback-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-multichar',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
