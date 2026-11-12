# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     High-resolution timers hand the kernel their exact due times; other timers of a process without a resolution request fall on the default tick
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


def region(first, last):
    start = source.index('\n', source.index(first)) + 1
    return source[start:source.index(last)]


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


fixture = r'''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VOID void
#define NTAPI
#define _In_
#define _In_opt_
#define _Inout_
#define _Out_
#define _Out_opt_
#define TRUE 1
#define FALSE 0
#define KernelMode 0
#define IO_NO_INCREMENT 0
#define TIMER_MODIFY_STATE 2
#define TIMER_ALL_ACCESS 0x1F0003
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER_4 ((NTSTATUS)0xC00000F2)
#define STATUS_INVALID_PARAMETER_6 ((NTSTATUS)0xC00000F4)
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
#define PAGED_CODE()
#define _SEH2_TRY
#define _SEH2_EXCEPT(x) if (0)
#define _SEH2_END
#define _SEH2_YIELD(x) x
#define _SEH2_GetExceptionCode() 0
#define ProbeForReadLargeInteger(p) ((void)(p))
#define ProbeForWriteBoolean(p) ((void)(p))
#define ProbeForWriteHandle(p) ((void)(p))
#define RtlZeroMemory(p, n) memset((p), 0, (n))
typedef unsigned char BOOLEAN, *PBOOLEAN, UCHAR, KIRQL, KPROCESSOR_MODE;
typedef uint32_t ULONG, ACCESS_MASK;
typedef int32_t LONG, NTSTATUS, KPRIORITY;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef uintptr_t ULONG_PTR, KSPIN_LOCK, *PKSPIN_LOCK;
typedef void *PVOID, *HANDLE, **PHANDLE;
typedef union { int64_t QuadPart; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct _OBJECT_TYPE *POBJECT_TYPE;
typedef struct _OBJECT_ATTRIBUTES *POBJECT_ATTRIBUTES;
typedef enum { NotificationTimer, SynchronizationTimer } TIMER_TYPE;
typedef enum { OriginalApcEnvironment, AttachedApcEnvironment, CurrentApcEnvironment } KAPC_ENVIRONMENT;
typedef struct _KDPC *PKDPC;
typedef void (*PKDEFERRED_ROUTINE)(PKDPC, PVOID, PVOID, PVOID);
typedef struct _KDPC { PKDEFERRED_ROUTINE DeferredRoutine; PVOID DeferredContext; } KDPC;
typedef struct { BOOLEAN Inserted; LONG SignalState; } DISPATCHER_HEADER;
typedef struct { DISPATCHER_HEADER Header; LARGE_INTEGER DueTime; PKDPC Dpc; LONG Period; } KTIMER, *PKTIMER;
typedef struct _KTHREAD { int Unused; } KTHREAD, *PKTHREAD;
typedef struct { LIST_ENTRY Link; ULONGLONG DueTime; BOOLEAN Armed; } KI_HIGH_RESOLUTION_DEADLINE, *PKI_HIGH_RESOLUTION_DEADLINE;
typedef struct { ULONG SetTimerResolution; } EPROCESS, *PEPROCESS;
typedef struct _KAPC *PKAPC;
typedef void (*PKNORMAL_ROUTINE)(PVOID, PVOID, PVOID);
typedef void (*PKKERNEL_ROUTINE)(PKAPC, PKNORMAL_ROUTINE *, PVOID *, PVOID *, PVOID *);
typedef void (*PKRUNDOWN_ROUTINE)(PKAPC);
typedef PKNORMAL_ROUTINE PTIMER_APC_ROUTINE;
typedef struct _KAPC { PKTHREAD Thread; PKKERNEL_ROUTINE KernelRoutine; PKRUNDOWN_ROUTINE RundownRoutine;
                       PKNORMAL_ROUTINE NormalRoutine; PVOID NormalContext; PVOID SystemArgument1, SystemArgument2; BOOLEAN Inserted; } KAPC;
typedef struct { KTHREAD Tcb; KSPIN_LOCK ActiveTimerListLock; LIST_ENTRY ActiveTimerListHead; } ETHREAD, *PETHREAD;
typedef struct { KTIMER KeTimer; KAPC TimerApc; KDPC TimerDpc; LIST_ENTRY ActiveTimerListEntry; KSPIN_LOCK Lock; LONG Period;
                 BOOLEAN ApcAssociated, WakeTimer; LIST_ENTRY WakeTimerListEntry; ULONG SetSequence, ApcQueuedSequence;
                 BOOLEAN HighResolution; KI_HIGH_RESOLUTION_DEADLINE HighResolutionDeadline; } ETIMER, *PETIMER;
typedef struct _EX_TIMER *PEX_TIMER;
typedef void (*PEXT_CALLBACK)(PEX_TIMER, PVOID);
typedef void (*PEXT_DELETE_CALLBACK)(PVOID);
typedef void *PEXT_SET_PARAMETERS, *PEXT_CANCEL_PARAMETERS;
typedef void (*PWORKER_THREAD_ROUTINE)(PVOID);
typedef struct { PWORKER_THREAD_ROUTINE WorkerRoutine; PVOID Parameter; } WORK_QUEUE_ITEM, *PWORK_QUEUE_ITEM;
#define EX_TIMER_HIGH_RESOLUTION 0x4
#define EX_TIMER_NOTIFICATION (1UL << 31)
#define NonPagedPool 0
#define DelayedWorkQueue 0
#define PASSIVE_LEVEL 0
#define UserMode 1
static const char *Scenario = "setup";
static ETHREAD TheThread;
static ETIMER TheTimer __attribute__((aligned(16)));
static HANDLE TheHandle = (HANDLE)(ULONG_PTR)0x44;
static POBJECT_TYPE ExTimerObjectType;
static LIST_ENTRY ExpWakeList;
static KSPIN_LOCK ExpWakeListLock;
static LONG References;
static PKNORMAL_ROUTINE Queued;
static PKSPIN_LOCK CancelOnRelease;
NTSTATUS NtCancelTimer(HANDLE TimerHandle, PBOOLEAN CurrentState);
static void InitializeListHead(PLIST_ENTRY h) { h->Flink = h->Blink = h; }
static BOOLEAN IsListEmpty(PLIST_ENTRY h) { return h->Flink == h; }
static void InsertTailList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Flink = h; e->Blink = h->Blink; h->Blink->Flink = e; h->Blink = e; }
static void RemoveEntryList(PLIST_ENTRY e) { e->Blink->Flink = e->Flink; e->Flink->Blink = e->Blink; }
static KPROCESSOR_MODE PreviousMode = KernelMode;
static KPROCESSOR_MODE ExGetPreviousMode(void) { return PreviousMode; }
static PETHREAD PsGetCurrentThread(void) { return &TheThread; }
static void KeInitializeSpinLock(PKSPIN_LOCK Lock) { *Lock = 0; }
static void KeAcquireSpinLockAtDpcLevel(PKSPIN_LOCK Lock) { CHECK(*Lock == 0); *Lock = 1; }
static void KeReleaseSpinLockFromDpcLevel(PKSPIN_LOCK Lock) { CHECK(*Lock == 1); *Lock = 0; }
static void KeAcquireSpinLock(PKSPIN_LOCK Lock, KIRQL *OldIrql) { *OldIrql = 0; KeAcquireSpinLockAtDpcLevel(Lock); }
/* Whoever else wants the lock gets it the moment it is free */
static void KeReleaseSpinLock(PKSPIN_LOCK Lock, KIRQL OldIrql) {
    (void)OldIrql;
    KeReleaseSpinLockFromDpcLevel(Lock);
    if (Lock != CancelOnRelease)
        return;
    CancelOnRelease = NULL;
    CHECK(NtCancelTimer(TheHandle, NULL) == STATUS_SUCCESS);
}
static void ObReferenceObject(PVOID Object) { CHECK(Object == &TheTimer); References++; }
static void ObDereferenceObject(PVOID Object) { CHECK(Object == &TheTimer); References--; }
static void ObDereferenceObjectEx(PVOID Object, LONG Count) { CHECK(Object == &TheTimer); References -= Count; }
static NTSTATUS ObReferenceObjectByHandle(HANDLE Handle, ACCESS_MASK Access, POBJECT_TYPE Type, KPROCESSOR_MODE Mode, PVOID *Object, PVOID Info) {
    (void)Access; (void)Type; (void)Mode; (void)Info;
    CHECK(Handle == TheHandle);
    *Object = &TheTimer;
    References++;
    return STATUS_SUCCESS;
}
static NTSTATUS ObCreateObject(KPROCESSOR_MODE ProbeMode, POBJECT_TYPE Type, POBJECT_ATTRIBUTES Attributes, KPROCESSOR_MODE Mode,
                               PVOID ParseContext, ULONG Size, ULONG Paged, ULONG NonPaged, PVOID *Object) {
    (void)ProbeMode; (void)Type; (void)Attributes; (void)Mode; (void)ParseContext; (void)Paged; (void)NonPaged;
    CHECK(Size == sizeof(ETIMER));
    *Object = &TheTimer;
    References = 1;
    return STATUS_SUCCESS;
}
static NTSTATUS ObInsertObject(PVOID Object, PVOID Passed, ACCESS_MASK Access, ULONG Additional, PVOID *New, HANDLE *Handle) {
    (void)Passed; (void)Access; (void)Additional; (void)New;
    CHECK(Object == &TheTimer);
    *Handle = TheHandle;
    return STATUS_SUCCESS;
}
static EPROCESS TheProcess;
static PEPROCESS PsGetCurrentProcess(void) { return &TheProcess; }
static ULONGLONG Now;
static ULONGLONG KiQueryInterruptTimePrecise(PVOID QpcTimeStamp, BOOLEAN Unbiased) { (void)QpcTimeStamp; (void)Unbiased; return Now; }
static ULONGLONG KiRoundToDefaultTimerResolution(ULONGLONG DueTime) { return (DueTime + 156249) / 156250 * 156250; }
static void KiArmHighResolutionDeadline(PKI_HIGH_RESOLUTION_DEADLINE Deadline, ULONGLONG DueTime) { Deadline->Armed = TRUE; Deadline->DueTime = DueTime; }
static void KiDisarmHighResolutionDeadline(PKI_HIGH_RESOLUTION_DEADLINE Deadline) { Deadline->Armed = FALSE; }
static void KeFlushQueuedDpcs(void) {}
static KIRQL KeGetCurrentIrql(void) { return PASSIVE_LEVEL; }
static void ExInitializeWorkItem(PWORK_QUEUE_ITEM Item, PWORKER_THREAD_ROUTINE Routine, PVOID Parameter) { Item->WorkerRoutine = Routine; Item->Parameter = Parameter; }
static void ExQueueWorkItem(PWORK_QUEUE_ITEM Item, int Queue) { (void)Item; (void)Queue; CHECK(!"a timer nobody deleted was queued for deletion"); }
static void ExFreePoolWithTag(PVOID P, ULONG Tag) { (void)Tag; free(P); }
static void KeInitializeTimerEx(PKTIMER Timer, TIMER_TYPE Type) { (void)Type; memset(Timer, 0, sizeof(*Timer)); }
static void KeInitializeDpc(PKDPC Dpc, PKDEFERRED_ROUTINE Routine, PVOID Context) { Dpc->DeferredRoutine = Routine; Dpc->DeferredContext = Context; }
static BOOLEAN KeRemoveQueueDpc(PKDPC Dpc) { (void)Dpc; return FALSE; }
static LONG KeReadStateTimer(PKTIMER Timer) { return Timer->Header.SignalState; }
static BOOLEAN KeCancelTimer(PKTIMER Timer) {
    BOOLEAN Inserted = Timer->Header.Inserted;
    Timer->Header.Inserted = FALSE;
    return Inserted;
}
static LONG LastPeriod;
static PKDPC LastDpc;
static int Sets;
static BOOLEAN KeSetTimerEx(PKTIMER Timer, LARGE_INTEGER DueTime, LONG Period, PKDPC Dpc) {
    BOOLEAN Inserted = Timer->Header.Inserted;
    CHECK(DueTime.QuadPart < 0);
    Timer->Header.Inserted = TRUE;
    Timer->Header.SignalState = 0;
    Timer->DueTime.QuadPart = (int64_t)(Now - DueTime.QuadPart);
    Timer->Period = Period;
    Timer->Dpc = Dpc;
    LastPeriod = Period;
    LastDpc = Dpc;
    Sets++;
    return Inserted;
}
static void KeInitializeApc(PKAPC Apc, PKTHREAD Thread, KAPC_ENVIRONMENT Environment, PKKERNEL_ROUTINE KernelRoutine,
                            PKRUNDOWN_ROUTINE RundownRoutine, PKNORMAL_ROUTINE NormalRoutine, KPROCESSOR_MODE Mode, PVOID Context) {
    (void)Environment; (void)Mode;
    CHECK(!Apc->Inserted);
    Apc->Thread = Thread;
    Apc->KernelRoutine = KernelRoutine;
    Apc->RundownRoutine = RundownRoutine;
    Apc->NormalRoutine = NormalRoutine;
    Apc->NormalContext = Context;
}
static BOOLEAN KeInsertQueueApc(PKAPC Apc, PVOID Argument1, PVOID Argument2, KPRIORITY Increment) {
    (void)Argument1; (void)Argument2; (void)Increment;
    if (Apc->Inserted)
        return FALSE;
    Apc->Inserted = TRUE;
    Queued = Apc->NormalRoutine;
    return TRUE;
}
static BOOLEAN KeRemoveQueueApc(PKAPC Apc) {
    BOOLEAN Inserted = Apc->Inserted;
    Apc->Inserted = FALSE;
    return Inserted;
}
static PVOID ExAllocatePoolZero(int Pool, size_t Size, ULONG Tag) { (void)Pool; (void)Tag; return calloc(1, Size); }
'''
fixture += region('/* PRIVATE DATA', '/* PUBLIC FUNCTIONS')
for name in ['ExpCreateTimer', 'NtCreateTimer', 'NtSetTimer', 'NtCancelTimer',
             'ExpRearmHighResolutionTimer', 'ExpFinishTimerDelete', 'ExpDeleteTimerWorker', 'ExpQueueTimerDelete', 'ExpExTimerDpcRoutine', 'ExAllocateTimer', 'ExSetTimer', 'ExCancelTimer']:
    fixture += definition(name)
fixture += r"""
static int Callbacks;
static void Callback(PEX_TIMER Timer, PVOID Context) { (void)Timer; (void)Context; Callbacks++; }
static void Expire(PKTIMER Timer) {
    CHECK(Timer->Header.Inserted);
    Timer->Header.Inserted = FALSE;
    Timer->Header.SignalState = 1;
}
static void RunDpc(PKDPC Dpc) { Dpc->DeferredRoutine(Dpc, Dpc->DeferredContext, NULL, NULL); }
static void DriverTimers(void) {
    PEX_TIMER Timer;
    int Before;

    Scenario = "a high-resolution driver timer keeps its period to the hundred nanoseconds";
    Now = 1000000;
    Timer = ExAllocateTimer(Callback, NULL, EX_TIMER_HIGH_RESOLUTION);
    CHECK(Timer != NULL);
    CHECK(!ExSetTimer(Timer, -55000, 55000, NULL));
    CHECK(LastPeriod == 0 && LastDpc == &Timer->Dpc);
    CHECK(Timer->ScheduledTime == 1055000 && Timer->Deadline.Armed && Timer->Deadline.DueTime == 1055000);
    Now = 1055400;
    Expire(&Timer->KeTimer);
    RunDpc(&Timer->Dpc);
    CHECK(Callbacks == 1 && Timer->ScheduledTime == 1110000 && Timer->KeTimer.Header.Inserted);
    CHECK((ULONGLONG)Timer->KeTimer.DueTime.QuadPart == 1110000 && Timer->Deadline.Armed && Timer->Deadline.DueTime == 1110000);

    Scenario = "a late expiry skips the periods that went by";
    Now = 1110000 + 3 * 55000 + 100;
    Expire(&Timer->KeTimer);
    RunDpc(&Timer->Dpc);
    CHECK(Timer->ScheduledTime == 1110000 + 4 * 55000 && Timer->Deadline.DueTime == Timer->ScheduledTime);

    Scenario = "a cancelled high-resolution timer gives its deadline back and is not set again";
    CHECK(ExCancelTimer(Timer, NULL));
    CHECK(!Timer->Deadline.Armed && !Timer->HighResolutionSet && !Timer->KeTimer.Header.Inserted);
    Before = Sets;
    RunDpc(&Timer->Dpc);
    CHECK(Sets == Before && !Timer->Deadline.Armed);

    Scenario = "a one-shot high-resolution timer gives its deadline back once it went off";
    Now = 2000000;
    CHECK(!ExSetTimer(Timer, -20000, 0, NULL));
    CHECK(Timer->Deadline.Armed && Timer->Deadline.DueTime == 2020000);
    Now = 2020100;
    Expire(&Timer->KeTimer);
    RunDpc(&Timer->Dpc);
    CHECK(!Timer->Deadline.Armed && !Timer->HighResolutionSet && !Timer->KeTimer.Header.Inserted);
    free(Timer);

    Scenario = "a default-resolution driver timer counts its period in milliseconds";
    Timer = ExAllocateTimer(NULL, NULL, 0);
    CHECK(!ExSetTimer(Timer, -55000, 55000, NULL));
    CHECK(LastPeriod == 5 && LastDpc == &Timer->Dpc && !Timer->Deadline.Armed);
    free(Timer);
}
static void NamedTimers(void) {
    LARGE_INTEGER Due = { .QuadPart = -20000 };
    HANDLE Handle;

    Scenario = "a high-resolution timer object hands its due time to the kernel";
    InitializeListHead(&TheThread.ActiveTimerListHead);
    InitializeListHead(&ExpWakeList);
    Now = 3000000;
    CHECK(ExpCreateTimer(&Handle, TIMER_ALL_ACCESS, NULL, NotificationTimer, TRUE) == STATUS_SUCCESS);
    CHECK(NtSetTimer(Handle, &Due, NULL, NULL, FALSE, 5, NULL) == STATUS_SUCCESS);
    CHECK(TheTimer.HighResolutionDeadline.Armed && TheTimer.HighResolutionDeadline.DueTime == 3020000);
    Now = 3020050;
    Expire(&TheTimer.KeTimer);
    TheTimer.KeTimer.Header.Inserted = TRUE;
    TheTimer.KeTimer.DueTime.QuadPart = 3070050;
    RunDpc(&TheTimer.TimerDpc);
    CHECK(TheTimer.HighResolutionDeadline.Armed && TheTimer.HighResolutionDeadline.DueTime == 3070050);
    CHECK(NtCancelTimer(Handle, NULL) == STATUS_SUCCESS);
    CHECK(!TheTimer.HighResolutionDeadline.Armed);

    Scenario = "a process that asked for no resolution gets its timer on the default tick";
    CHECK(ExpCreateTimer(&Handle, TIMER_ALL_ACCESS, NULL, NotificationTimer, FALSE) == STATUS_SUCCESS);
    PreviousMode = UserMode;
    Now = 1000000;
    TheProcess.SetTimerResolution = 0;
    CHECK(NtSetTimer(Handle, &Due, NULL, NULL, FALSE, 0, NULL) == STATUS_SUCCESS);
    CHECK(TheTimer.KeTimer.DueTime.QuadPart == 1093750);
    TheProcess.SetTimerResolution = 1;
    CHECK(NtSetTimer(Handle, &Due, NULL, NULL, FALSE, 0, NULL) == STATUS_SUCCESS);
    CHECK(TheTimer.KeTimer.DueTime.QuadPart == 1020000);
    TheProcess.SetTimerResolution = 0;
    TheTimer.HighResolution = TRUE;
    CHECK(NtSetTimer(Handle, &Due, NULL, NULL, FALSE, 0, NULL) == STATUS_SUCCESS);
    CHECK(TheTimer.KeTimer.DueTime.QuadPart == 1020000 && TheTimer.HighResolutionDeadline.Armed);
    CHECK(NtCancelTimer(Handle, NULL) == STATUS_SUCCESS);
    PreviousMode = KernelMode;
    CHECK(ExpCreateTimer(&Handle, TIMER_ALL_ACCESS, NULL, NotificationTimer, FALSE) == STATUS_SUCCESS);
    CHECK(NtSetTimer(Handle, &Due, NULL, NULL, FALSE, 0, NULL) == STATUS_SUCCESS);
    CHECK(TheTimer.KeTimer.DueTime.QuadPart == 1020000);
}
int main(void) {
    DriverTimers();
    NamedTimers();
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix='ex-timer-high-resolution-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-multichar',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
