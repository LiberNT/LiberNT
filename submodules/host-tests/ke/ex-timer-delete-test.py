# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     ExDeleteTimer keeps the timer object until no expiry or callback can use it
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'ntos/ex/extimer.c').read_text()

typedef = source[source.index('typedef struct _EX_TIMER'):source.index('} EX_TIMER;') + len('} EX_TIMER;')]
routines = source[source.index('/* TIMERS A DRIVER KEEPS TO ITSELF'):source.index('/* EOF */')]

fixture = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VOID void
#define NTAPI
#define _In_
#define _In_opt_
#define _Inout_
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define APC_LEVEL 1
#define DISPATCH_LEVEL 2
#define NonPagedPool 0
#define DelayedWorkQueue 0
#define EX_TIMER_HIGH_RESOLUTION 0x4
#define EX_TIMER_NOTIFICATION (1UL << 31)
#define EXP_TIMER_TAG 0x656d6954
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
typedef unsigned char BOOLEAN, KIRQL;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef uintptr_t KSPIN_LOCK, *PKSPIN_LOCK;
typedef void *PVOID;
typedef union { int64_t QuadPart; } LARGE_INTEGER;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY;
typedef enum { NotificationTimer, SynchronizationTimer } TIMER_TYPE;
typedef struct _KDPC *PKDPC;
typedef void (*PKDEFERRED_ROUTINE)(PKDPC, PVOID, PVOID, PVOID);
typedef struct _KDPC { PKDEFERRED_ROUTINE DeferredRoutine; PVOID DeferredContext; } KDPC;
typedef struct { BOOLEAN Inserted; } DISPATCHER_HEADER;
typedef struct { DISPATCHER_HEADER Header; LARGE_INTEGER DueTime; PKDPC Dpc; LONG Period; } KTIMER, *PKTIMER;
typedef struct { LIST_ENTRY Link; ULONGLONG DueTime; BOOLEAN Armed; } KI_HIGH_RESOLUTION_DEADLINE, *PKI_HIGH_RESOLUTION_DEADLINE;
typedef void (*PWORKER_THREAD_ROUTINE)(PVOID);
typedef struct { PWORKER_THREAD_ROUTINE WorkerRoutine; PVOID Parameter; } WORK_QUEUE_ITEM, *PWORK_QUEUE_ITEM;
typedef struct _EX_TIMER *PEX_TIMER;
typedef void (*PEXT_CALLBACK)(PEX_TIMER, PVOID);
typedef void (*PEXT_DELETE_CALLBACK)(PVOID);
typedef void *PEXT_SET_PARAMETERS, *PEXT_CANCEL_PARAMETERS;
typedef struct { ULONG Version; ULONG Reserved; PEXT_DELETE_CALLBACK DeleteCallback; PVOID DeleteContext; } EXT_DELETE_PARAMETERS, *PEXT_DELETE_PARAMETERS;
static const char *Scenario = "setup";
static KIRQL Irql;
static BOOLEAN InTimerRoutine;
static int Frees, Flushes, DeleteCalls, DeleteCallsBeforeFree;
static void *Live;
static PWORK_QUEUE_ITEM Pending[8];
static int PendingCount;
/* A routine waiting on a processor's queue, and one the clock holds the table's lock for and has not queued yet */
static PKDPC Queued;
static PKDPC HandingOut;
static BOOLEAN DeadlineArmed(PVOID Timer);
static KIRQL KeGetCurrentIrql(void) { return Irql; }
static void KeInitializeSpinLock(PKSPIN_LOCK Lock) { *Lock = 0; }
static void KeAcquireSpinLockAtDpcLevel(PKSPIN_LOCK Lock) { CHECK(*Lock == 0); *Lock = 1; }
static void KeReleaseSpinLockFromDpcLevel(PKSPIN_LOCK Lock) { CHECK(*Lock == 1); *Lock = 0; }
static void KeAcquireSpinLock(PKSPIN_LOCK Lock, KIRQL *OldIrql) { *OldIrql = Irql; KeAcquireSpinLockAtDpcLevel(Lock); }
static void KeReleaseSpinLock(PKSPIN_LOCK Lock, KIRQL OldIrql) { (void)OldIrql; KeReleaseSpinLockFromDpcLevel(Lock); }
static PVOID ExAllocatePoolZero(int Pool, size_t Size, ULONG Tag) { (void)Pool; (void)Tag; Live = calloc(1, Size); return Live; }
static void ExFreePoolWithTag(PVOID P, ULONG Tag) {
    (void)Tag;
    CHECK(P == Live);
    CHECK(!InTimerRoutine);
    CHECK(!((PKTIMER)P)->Header.Inserted);
    CHECK(!DeadlineArmed(P));
    CHECK(Queued == NULL && HandingOut == NULL);
    Frees++;
    memset(P, 0xA5, 64);
    free(P);
    Live = NULL;
}
static ULONGLONG Now;
static ULONGLONG KiQueryInterruptTimePrecise(PVOID Qpc, BOOLEAN Unbiased) { (void)Qpc; (void)Unbiased; return Now; }
static void KiArmHighResolutionDeadline(PKI_HIGH_RESOLUTION_DEADLINE D, ULONGLONG Due) { D->Armed = TRUE; D->DueTime = Due; }
static void KiDisarmHighResolutionDeadline(PKI_HIGH_RESOLUTION_DEADLINE D) { D->Armed = FALSE; }
static void KeInitializeTimerEx(PKTIMER Timer, TIMER_TYPE Type) { (void)Type; memset(Timer, 0, sizeof(*Timer)); }
static void KeInitializeDpc(PKDPC Dpc, PKDEFERRED_ROUTINE Routine, PVOID Context) { Dpc->DeferredRoutine = Routine; Dpc->DeferredContext = Context; }
static void RunRoutine(PKDPC Dpc) {
    KIRQL Before = Irql;
    CHECK(Live != NULL);
    Irql = DISPATCH_LEVEL;
    InTimerRoutine = TRUE;
    Dpc->DeferredRoutine(Dpc, Dpc->DeferredContext, NULL, NULL);
    InTimerRoutine = FALSE;
    Irql = Before;
}
/* Whoever takes the timer table's lock waits for the clock to finish handing an expiry out */
static void TakeTableLock(void) { if (HandingOut != NULL) { CHECK(Queued == NULL); Queued = HandingOut; HandingOut = NULL; } }
static BOOLEAN KeSetTimerEx(PKTIMER Timer, LARGE_INTEGER Due, LONG Period, PKDPC Dpc) {
    BOOLEAN Was = Timer->Header.Inserted;
    TakeTableLock();
    Timer->Header.Inserted = TRUE;
    Timer->DueTime.QuadPart = (int64_t)Now - Due.QuadPart;
    Timer->Period = Period;
    Timer->Dpc = Dpc;
    return Was;
}
static BOOLEAN KeCancelTimer(PKTIMER Timer) {
    BOOLEAN Was = Timer->Header.Inserted;
    TakeTableLock();
    Timer->Header.Inserted = FALSE;
    Timer->Period = 0;
    return Was;
}
static void KeFlushQueuedDpcs(void) {
    PKDPC Dpc = Queued;
    CHECK(Irql <= APC_LEVEL);
    CHECK(!InTimerRoutine);
    Flushes++;
    Queued = NULL;
    if (Dpc != NULL)
        RunRoutine(Dpc);
}
static void ExInitializeWorkItem(PWORK_QUEUE_ITEM Item, PWORKER_THREAD_ROUTINE Routine, PVOID Parameter) { Item->WorkerRoutine = Routine; Item->Parameter = Parameter; }
static void ExQueueWorkItem(PWORK_QUEUE_ITEM Item, int Queue) { (void)Queue; CHECK(PendingCount < 8); Pending[PendingCount++] = Item; }
@TYPEDEF@
@ROUTINES@
static BOOLEAN DeadlineArmed(PVOID Timer) { return ((PEX_TIMER)Timer)->Deadline.Armed; }
static void RunWorkers(void) {
    while (PendingCount) {
        PWORK_QUEUE_ITEM Item = Pending[--PendingCount];
        PWORKER_THREAD_ROUTINE Routine = Item->WorkerRoutine;
        PVOID Parameter = Item->Parameter;
        Irql = 0;
        Routine(Parameter);
    }
}
/* The clock takes the timer off the table, puts a periodic one back on, and runs its routine */
static void Expire(PEX_TIMER Timer) {
    Timer->KeTimer.Header.Inserted = (Timer->KeTimer.Period != 0);
    RunRoutine(Timer->KeTimer.Dpc);
}
static int Calls;
static PEX_TIMER DeleteFromCallback;
static BOOLEAN DeleteCancels;
static void Callback(PEX_TIMER Timer, PVOID Context) {
    (void)Context;
    CHECK(Timer == Live);
    Calls++;
    if (DeleteFromCallback == Timer) {
        CHECK(ExDeleteTimer(Timer, DeleteCancels, FALSE, NULL) == FALSE);
        CHECK(ExSetTimer(Timer, -10000, 0, NULL) == FALSE);
        CHECK(ExCancelTimer(Timer, NULL) == FALSE);
        CHECK(ExDeleteTimer(Timer, TRUE, FALSE, NULL) == FALSE);
    }
}
static void Deleted(PVOID Context) { CHECK(Context == &DeleteCalls); DeleteCalls++; if (Live != NULL) DeleteCallsBeforeFree++; }
static void Reset(const char *Name) {
    Scenario = Name;
    Frees = Flushes = DeleteCalls = DeleteCallsBeforeFree = Calls = 0;
    DeleteFromCallback = NULL;
    DeleteCancels = TRUE;
    PendingCount = 0;
    Queued = HandingOut = NULL;
    Irql = 0;
}
int main(void) {
    EXT_DELETE_PARAMETERS Params = { 0, 0, Deleted, &DeleteCalls };
    PEX_TIMER Timer;

    Reset("one-shot set, deleted without cancel");
    Timer = ExAllocateTimer(Callback, NULL, 0);
    CHECK(ExSetTimer(Timer, -100000, 0, NULL) == FALSE);
    CHECK(ExDeleteTimer(Timer, FALSE, FALSE, &Params) == FALSE);
    CHECK(Frees == 0 && PendingCount == 0);
    Expire(Timer);
    CHECK(Calls == 1 && Frees == 0);
    RunWorkers();
    CHECK(Frees == 1 && DeleteCalls == 1 && DeleteCallsBeforeFree == 0 && Flushes == 1);

    Reset("deleted from its own callback");
    Timer = ExAllocateTimer(Callback, NULL, 0);
    DeleteFromCallback = Timer;
    CHECK(ExSetTimer(Timer, -100000, 0, NULL) == FALSE);
    Expire(Timer);
    CHECK(Calls == 1 && Frees == 0 && PendingCount == 1);
    RunWorkers();
    CHECK(Frees == 1 && Flushes == 1);

    Reset("periodic, deleted without cancel");
    Timer = ExAllocateTimer(Callback, NULL, 0);
    CHECK(ExSetTimer(Timer, -100000, 100000, NULL) == FALSE);
    CHECK(ExDeleteTimer(Timer, FALSE, FALSE, &Params) == FALSE);
    Expire(Timer);
    CHECK(Calls == 1 && !Timer->KeTimer.Header.Inserted && PendingCount == 1);
    RunRoutine(&Timer->Dpc);
    CHECK(Calls == 1 && Frees == 0);
    RunWorkers();
    CHECK(Frees == 1 && DeleteCalls == 1);

    Reset("cancelled and waited for");
    Timer = ExAllocateTimer(Callback, NULL, 0);
    CHECK(ExSetTimer(Timer, -100000, 0, NULL) == FALSE);
    CHECK(ExDeleteTimer(Timer, TRUE, TRUE, &Params) == TRUE);
    CHECK(Frees == 1 && DeleteCalls == 1 && Flushes == 1 && PendingCount == 0);

    Reset("not set, deleted without waiting");
    Timer = ExAllocateTimer(NULL, NULL, 0);
    CHECK(ExDeleteTimer(Timer, FALSE, FALSE, &Params) == FALSE);
    CHECK(Frees == 0 && PendingCount == 1);
    RunWorkers();
    CHECK(Frees == 1 && DeleteCalls == 1);

    Reset("high resolution, cancelled without waiting");
    Timer = ExAllocateTimer(Callback, NULL, EX_TIMER_HIGH_RESOLUTION);
    CHECK(ExSetTimer(Timer, -100000, 50000, NULL) == FALSE);
    CHECK(Timer->Deadline.Armed);
    CHECK(ExDeleteTimer(Timer, TRUE, FALSE, NULL) == TRUE);
    CHECK(!Timer->Deadline.Armed && Frees == 0);
    RunWorkers();
    CHECK(Frees == 1);

    Reset("high resolution, deleted without cancel between its expiry and its routine");
    Timer = ExAllocateTimer(Callback, NULL, EX_TIMER_HIGH_RESOLUTION);
    CHECK(ExSetTimer(Timer, -100000, 0, NULL) == FALSE);
    Timer->KeTimer.Header.Inserted = FALSE;
    HandingOut = &Timer->Dpc;
    CHECK(ExDeleteTimer(Timer, FALSE, FALSE, &Params) == FALSE);
    CHECK(Frees == 0 && Calls == 0);
    RunWorkers();
    CHECK(Calls == 1 && Frees == 1 && DeleteCalls == 1);

    Reset("periodic, caught off the table while the clock puts it back");
    Timer = ExAllocateTimer(Callback, NULL, 0);
    CHECK(ExSetTimer(Timer, -100000, 100000, NULL) == FALSE);
    Timer->KeTimer.Header.Inserted = FALSE;
    CHECK(ExDeleteTimer(Timer, FALSE, FALSE, &Params) == FALSE);
    Timer->KeTimer.Header.Inserted = TRUE;
    RunRoutine(&Timer->Dpc);
    CHECK(Calls == 1 && !Timer->KeTimer.Header.Inserted && PendingCount == 1);
    RunWorkers();
    CHECK(Frees == 1 && DeleteCalls == 1);

    Reset("periodic, deleted without cancel from its own callback, goes off once more");
    Timer = ExAllocateTimer(Callback, NULL, 0);
    DeleteFromCallback = Timer;
    DeleteCancels = FALSE;
    CHECK(ExSetTimer(Timer, -100000, 100000, NULL) == FALSE);
    Expire(Timer);
    CHECK(Calls == 1 && Timer->KeTimer.Header.Inserted && PendingCount == 0);
    Expire(Timer);
    CHECK(Calls == 2 && !Timer->KeTimer.Header.Inserted && PendingCount == 1);
    RunWorkers();
    CHECK(Frees == 1);

    Reset("one-shot set again before the routine of its earlier expiry ran");
    Timer = ExAllocateTimer(Callback, NULL, 0);
    CHECK(ExSetTimer(Timer, -100000, 0, NULL) == FALSE);
    Timer->KeTimer.Header.Inserted = FALSE;
    CHECK(ExSetTimer(Timer, -100000, 0, NULL) == FALSE);
    CHECK(ExDeleteTimer(Timer, FALSE, FALSE, &Params) == FALSE);
    RunRoutine(&Timer->Dpc);
    CHECK(Calls == 1 && Timer->KeTimer.Header.Inserted && PendingCount == 0);
    Expire(Timer);
    CHECK(Calls == 2 && PendingCount == 1);
    RunWorkers();
    CHECK(Frees == 1 && DeleteCalls == 1);

    Reset("one-shot expiry still being handed out when it is deleted");
    Timer = ExAllocateTimer(Callback, NULL, 0);
    CHECK(ExSetTimer(Timer, -100000, 0, NULL) == FALSE);
    Timer->KeTimer.Header.Inserted = FALSE;
    HandingOut = &Timer->Dpc;
    CHECK(ExDeleteTimer(Timer, FALSE, FALSE, &Params) == FALSE);
    RunWorkers();
    CHECK(Calls == 1 && Frees == 1 && DeleteCalls == 1 && DeleteCallsBeforeFree == 0);
    return 0;
}
'''
fixture = fixture.replace('@TYPEDEF@', typedef).replace('@ROUTINES@', routines)
with tempfile.TemporaryDirectory(prefix='ex-timer-delete-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-unused-function', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
