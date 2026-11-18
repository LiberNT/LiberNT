# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Two threads connecting or disconnecting one passive-level interrupt object do it once between them
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--source', help='passint.c to test instead of the one under --root')
args = parser.parse_args()
source = pathlib.Path(args.source or pathlib.Path(args.root) / 'ntos/ke/passint.c').read_text()
body = source[source.index('#define TAG_PASSIVE_INTERRUPT'):]

fixture = r'''
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define NTAPI
#define VOID void
#define _In_
#define _In_opt_
#define _Inout_
#define _Out_opt_
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define APC_LEVEL 1
#define DISPATCH_LEVEL 2
#define HIGH_LEVEL 15
#define NonPagedPool 0
#define LevelSensitive 0
#define Executive 0
#define KernelMode 0
#define IO_NO_INCREMENT 0
#define NotificationEvent 0
#define SynchronizationEvent 1
#define CriticalWorkQueue 0
#define UNREFERENCED_PARAMETER(P) ((void)(P))
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); __atomic_add_fetch(&Failures, 1, __ATOMIC_SEQ_CST); } } while (0)
#define ASSERT(x) do { if (!(x)) { printf("ASSERT(%s) at line %d\n", #x, __LINE__); __atomic_add_fetch(&Failures, 1, __ATOMIC_SEQ_CST); } } while (0)
#define CONTAINING_RECORD(Address, Type, Field) ((Type *)((char *)(Address) - offsetof(Type, Field)))
#include <stddef.h>
typedef unsigned char UCHAR, BOOLEAN, KIRQL, *PBOOLEAN;
typedef unsigned int ULONG;
typedef int LONG;
typedef void *PVOID;
typedef size_t SIZE_T;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct { int Held; } KSPIN_LOCK, EX_PUSH_LOCK;
typedef struct { int Signaled; } KEVENT;
typedef struct { int Unused; } KDPC, *PKDPC, WORK_QUEUE_ITEM, EX_RUNDOWN_REF;
typedef struct _KINTERRUPT
{
    ULONG Vector;
    KIRQL Irql;
    int Mode;
    BOOLEAN Connected;
    BOOLEAN (*ServiceRoutine)(struct _KINTERRUPT *, PVOID);
    PVOID ServiceContext;
} KINTERRUPT, *PKINTERRUPT;
typedef BOOLEAN (*PKSYNCHRONIZE_ROUTINE)(PVOID);

static int Failures, Allocations, Frees, Enables, Disables, EnableFails;
static int Arrived, Wanted;
static __thread KIRQL CurrentIrql;

/* Holds the caller until as many callers as the scenario races have arrived, or gives up waiting for them */
static void Rendezvous(void)
{
    int Tries;

    __atomic_add_fetch(&Arrived, 1, __ATOMIC_SEQ_CST);
    for (Tries = 0; Tries < 300 && __atomic_load_n(&Arrived, __ATOMIC_SEQ_CST) < Wanted; Tries++)
        usleep(1000);
}

static void Take(int *Held) { while (__atomic_exchange_n(Held, 1, __ATOMIC_SEQ_CST)) sched_yield(); }
static void Give(int *Held) { __atomic_store_n(Held, 0, __ATOMIC_SEQ_CST); }

static KIRQL KeGetCurrentIrql(void) { return CurrentIrql; }
static KIRQL KfRaiseIrql(KIRQL Irql) { KIRQL Old = CurrentIrql; CurrentIrql = Irql; return Old; }
static void KeLowerIrql(KIRQL Irql) { CurrentIrql = Irql; }
static void KeAcquireSpinLockAtDpcLevel(KSPIN_LOCK *Lock) { Take(&Lock->Held); }
static void KeReleaseSpinLockFromDpcLevel(KSPIN_LOCK *Lock) { Give(&Lock->Held); }
static void KeEnterCriticalRegion(void) { }
static void KeLeaveCriticalRegion(void) { }
static void ExAcquirePushLockExclusive(EX_PUSH_LOCK *Lock) { Take(&Lock->Held); }
static void ExReleasePushLockExclusive(EX_PUSH_LOCK *Lock) { Give(&Lock->Held); }
static void KeMemoryBarrier(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static LONG InterlockedExchange(volatile LONG *Target, LONG Value) { return __atomic_exchange_n(Target, Value, __ATOMIC_SEQ_CST); }
static LONG InterlockedCompareExchange(volatile LONG *Target, LONG Value, LONG Comparand)
{
    __atomic_compare_exchange_n(Target, &Comparand, Value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Comparand;
}

static void InsertTailList(PLIST_ENTRY Head, PLIST_ENTRY Entry)
{
    Entry->Flink = Head;
    Entry->Blink = Head->Blink;
    Head->Blink->Flink = Entry;
    Head->Blink = Entry;
}
static BOOLEAN RemoveEntryList(PLIST_ENTRY Entry)
{
    Entry->Blink->Flink = Entry->Flink;
    Entry->Flink->Blink = Entry->Blink;
    return Entry->Flink == Entry->Blink;
}

static PVOID ExAllocatePoolZero(int Pool, SIZE_T Size, ULONG Tag)
{
    (void)Pool; (void)Tag;
    Rendezvous();
    __atomic_add_fetch(&Allocations, 1, __ATOMIC_SEQ_CST);
    return calloc(1, Size);
}
/* The block is left alone so that freeing it twice shows up as a count and not as a crash */
static void ExFreePoolWithTag(PVOID Block, ULONG Tag) { (void)Block; (void)Tag; __atomic_add_fetch(&Frees, 1, __ATOMIC_SEQ_CST); }
static void KeInitializeDpc(KDPC *Dpc, PVOID Routine, PVOID Context) { (void)Dpc; (void)Routine; (void)Context; }
static void ExInitializeWorkItem(WORK_QUEUE_ITEM *Item, PVOID Routine, PVOID Context) { (void)Item; (void)Routine; (void)Context; }
static void ExQueueWorkItem(WORK_QUEUE_ITEM *Item, int Queue) { (void)Item; (void)Queue; }
static void KeInitializeEvent(KEVENT *Event, int Type, BOOLEAN State) { (void)Type; Event->Signaled = State; }
static void KeSetEvent(KEVENT *Event, int Increment, BOOLEAN Wait) { (void)Increment; (void)Wait; Give(&Event->Signaled); Event->Signaled = 1; }
static void KeWaitForSingleObject(KEVENT *Event, int Reason, int Mode, BOOLEAN Alertable, PVOID Timeout)
{
    int Expected;

    (void)Reason; (void)Mode; (void)Alertable; (void)Timeout;
    CHECK(CurrentIrql <= APC_LEVEL);
    for (;;)
    {
        Expected = 1;
        if (__atomic_compare_exchange_n(&Event->Signaled, &Expected, 0, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            return;
        sched_yield();
    }
}
static void ExInitializeRundownProtection(EX_RUNDOWN_REF *Ref) { (void)Ref; }
static BOOLEAN ExAcquireRundownProtection(EX_RUNDOWN_REF *Ref) { (void)Ref; return TRUE; }
static void ExReleaseRundownProtection(EX_RUNDOWN_REF *Ref) { (void)Ref; }
static void ExWaitForRundownProtectionRelease(EX_RUNDOWN_REF *Ref) { (void)Ref; }
static BOOLEAN KeInsertQueueDpc(KDPC *Dpc, PVOID One, PVOID Two) { (void)Dpc; (void)One; (void)Two; return TRUE; }
static BOOLEAN KeRemoveQueueDpc(KDPC *Dpc) { (void)Dpc; return FALSE; }
static void KeFlushQueuedDpcs(void) { }
static BOOLEAN HalEnableSystemInterrupt(ULONG Vector, KIRQL Irql, int Mode)
{
    (void)Vector; (void)Irql; (void)Mode;
    __atomic_add_fetch(&Enables, 1, __ATOMIC_SEQ_CST);
    return !EnableFails;
}
static void HalDisableSystemInterrupt(ULONG Vector, KIRQL Irql)
{
    (void)Vector; (void)Irql;
    Rendezvous();
    __atomic_add_fetch(&Disables, 1, __ATOMIC_SEQ_CST);
}

@BODY@

static KINTERRUPT Interrupt;
static pthread_barrier_t StartLine;
static int Results[2];

static ULONG Listed(void)
{
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    for (Entry = KiPassiveInterruptList.Flink; Entry != &KiPassiveInterruptList && Count < 16; Entry = Entry->Flink)
        Count++;
    return Count;
}

static void *Connect(void *Slot)
{
    pthread_barrier_wait(&StartLine);
    *(int *)Slot = KiConnectPassiveInterrupt(&Interrupt);
    return NULL;
}

static void *Disconnect(void *Slot)
{
    pthread_barrier_wait(&StartLine);
    *(int *)Slot = KiDisconnectPassiveInterrupt(&Interrupt);
    return NULL;
}

static void Race(void *(*Routine)(void *))
{
    pthread_t Threads[2];
    int i;

    Arrived = 0;
    Wanted = 2;
    pthread_barrier_init(&StartLine, NULL, 2);
    for (i = 0; i < 2; i++)
        pthread_create(&Threads[i], NULL, Routine, &Results[i]);
    for (i = 0; i < 2; i++)
        pthread_join(Threads[i], NULL);
    pthread_barrier_destroy(&StartLine);
}

int main(void)
{
    Interrupt.Vector = 0x51;
    Interrupt.Irql = PASSIVE_LEVEL;

    /* Both callers are told the object is connected, and it is connected once */
    Race(Connect);
    CHECK(Results[0] && Results[1]);
    CHECK(Interrupt.Connected);
    CHECK(Allocations == 1);
    CHECK(Enables == 1);
    CHECK(Listed() == 1);

    /* Both callers leave it disconnected, and it is torn down once */
    Race(Disconnect);
    CHECK(Results[0] && Results[1]);
    CHECK(!Interrupt.Connected);
    CHECK(Disables == 1);
    CHECK(Frees == 1);
    CHECK(Listed() == 0);

    /* An interrupt the HAL refuses leaves nothing behind */
    Wanted = 1;
    EnableFails = 1;
    CHECK(!KiConnectPassiveInterrupt(&Interrupt));
    CHECK(!Interrupt.Connected && Listed() == 0 && Allocations == 2 && Frees == 2);

    if (!Failures)
        printf("passive interrupt: concurrent connects and disconnects of one object happen once\n");
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'passint.c').write_text(fixture.replace('@BODY@', body))
    subprocess.run([args.cc, '-std=gnu11', '-O1', '-pthread', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-parameter',
                    str(path / 'passint.c'), '-o', str(path / 'passint')], check=True)
    subprocess.run([str(path / 'passint')], check=True, timeout=60)
