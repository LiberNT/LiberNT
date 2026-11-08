# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     AMD64 interrupt connection state regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
"""Exercise the actual interrupt connection and list handoff routines."""
import argparse
import pathlib
import resource
import signal
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
s = pathlib.Path(args.source).read_text()

def body(name):
    p = s.index(name + '(')
    a = s.index('{', p)
    depth = 1
    b = a + 1
    while depth:
        depth += (s[b] == '{') - (s[b] == '}')
        b += 1
    return s[a + 1:b - 1]

fixture = r'''
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
typedef void *PVOID; typedef unsigned long ULONG; typedef unsigned char BOOLEAN; typedef int KIRQL;
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct _KINTERRUPT {
    ULONG Vector; KIRQL Irql; KIRQL SynchronizeIrql; int Number, ShareVector, Mode, Connected;
    LIST_ENTRY InterruptListEntry; void (*DispatchAddress)(void); unsigned char DispatchCode[16];
} KINTERRUPT, *PKINTERRUPT;
#define ASSERT(x) assert(x)
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define HIGH_LEVEL 31
#define PRIMARY_VECTOR_BASE 0x30
#define MAXIMUM_IDTVECTOR 255
#define DPRINT(...) ((void)0)
#define DPRINT1(...) ((void)0)
#define CONTAINING_RECORD(p,t,f) ((t *)((char *)(p)-offsetof(t,f)))
static struct { unsigned char _Op_push; unsigned char Pad[15]; } KiUnexpectedRange[256];
#define KiUnexpectedRangeEnd (KiUnexpectedRange + 256)
static PVOID Handlers[256];
static int KeNumberProcessors = 4, Enable = 1, DisableCount, Race;
static pthread_mutex_t Dispatcher = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t RaceBarrier;
static void KiInterruptDispatch(void) {}
static BOOLEAN KiConnectPassiveInterrupt(PKINTERRUPT I) { (void)I; abort(); }
static BOOLEAN KiDisconnectPassiveInterrupt(PKINTERRUPT I) { (void)I; abort(); }
static void KeSetSystemAffinityThread(uint64_t Affinity) { (void)Affinity; }
static void KeRevertToUserAffinityThread(void) {}
static KIRQL KiAcquireDispatcherLock(void) { if (Race) pthread_barrier_wait(&RaceBarrier); assert(pthread_mutex_lock(&Dispatcher)==0); return 0; }
static void KiReleaseDispatcherLock(KIRQL Old) { (void)Old; assert(pthread_mutex_unlock(&Dispatcher)==0); }
static PVOID KeQueryInterruptHandler(ULONG Vector) { return Handlers[Vector]; }
static void KeRegisterInterruptHandler(ULONG Vector, PVOID Handler) { Handlers[Vector] = Handler; }
static int HalEnableSystemInterrupt(ULONG Vector, KIRQL Irql, int Mode) { (void)Vector; (void)Irql; (void)Mode; return Enable; }
static void HalDisableSystemInterrupt(ULONG Vector, KIRQL Irql) { (void)Vector; (void)Irql; DisableCount++; }
static void InitializeListHead(PLIST_ENTRY H) { H->Flink = H->Blink = H; }
static int IsListEmpty(PLIST_ENTRY H) { return H->Flink == H; }
static void InsertTailList(PLIST_ENTRY H, PLIST_ENTRY E) { E->Blink=H->Blink; E->Flink=H; H->Blink->Flink=E; H->Blink=E; }
static void RemoveEntryList(PLIST_ENTRY E) { E->Blink->Flink=E->Flink; E->Flink->Blink=E->Blink; }
static PLIST_ENTRY RemoveTailList(PLIST_ENTRY H) { PLIST_ENTRY E=H->Blink; RemoveEntryList(E); return E; }
static BOOLEAN KeConnectInterrupt(PKINTERRUPT Interrupt) { @CONNECT@ }
static BOOLEAN KeDisconnectInterrupt(PKINTERRUPT Interrupt) { @DISCONNECT@ }
static void *RaceConnect(void *Context) { return (void *)(uintptr_t)KeConnectInterrupt(Context); }
static KINTERRUPT MakeInterrupt(int Share, int Mode) {
    KINTERRUPT I = {0}; I.Vector=0x51; I.Irql=5; I.SynchronizeIrql=5; I.Number=0; I.ShareVector=Share; I.Mode=Mode; return I;
}
int main(int argc, char **argv) {
    KINTERRUPT A=MakeInterrupt(1,0), B=MakeInterrupt(1,0), C=MakeInterrupt(1,0), D=MakeInterrupt(0,0), E=MakeInterrupt(1,1);
    (void)argv;
    Handlers[A.Vector] = &KiUnexpectedRange[A.Vector]._Op_push;
    Enable=0; assert(!KeConnectInterrupt(&A)); assert(!A.Connected && Handlers[A.Vector]==&KiUnexpectedRange[A.Vector]._Op_push);
    Enable=1; assert(KeConnectInterrupt(&A)); assert(A.Connected && Handlers[A.Vector]==A.DispatchCode);
    assert(KeConnectInterrupt(&B)); assert(KeConnectInterrupt(&C));
    assert(!KeConnectInterrupt(&D)); assert(!KeConnectInterrupt(&E));
    if (argc > 1) { B.InterruptListEntry.Blink = &B.InterruptListEntry; KeDisconnectInterrupt(&B); return 0; }
    assert(KeDisconnectInterrupt(&B)); assert(!B.Connected);
    assert(KeDisconnectInterrupt(&A)); assert(!A.Connected && Handlers[A.Vector]==C.DispatchCode);
    assert(KeDisconnectInterrupt(&C)); assert(!C.Connected && DisableCount==1);
    assert(KeDisconnectInterrupt(&C));
    {
        KINTERRUPT Head=MakeInterrupt(1,0), Shared=MakeInterrupt(1,0);
        pthread_t First, Second;
        void *FirstResult, *SecondResult;
        Handlers[Head.Vector] = &KiUnexpectedRange[Head.Vector]._Op_push;
        assert(KeConnectInterrupt(&Head));
        assert(pthread_barrier_init(&RaceBarrier,NULL,2)==0);
        Race=1;
        assert(pthread_create(&First,NULL,RaceConnect,&Shared)==0);
        assert(pthread_create(&Second,NULL,RaceConnect,&Shared)==0);
        assert(pthread_join(First,&FirstResult)==0);
        assert(pthread_join(Second,&SecondResult)==0);
        Race=0;
        assert(pthread_barrier_destroy(&RaceBarrier)==0);
        assert((uintptr_t)FirstResult==1 && (uintptr_t)SecondResult==1);
        assert(Head.InterruptListEntry.Flink==&Shared.InterruptListEntry);
        assert(Head.InterruptListEntry.Blink==&Shared.InterruptListEntry);
        assert(Shared.InterruptListEntry.Flink==&Head.InterruptListEntry);
        assert(Shared.InterruptListEntry.Blink==&Head.InterruptListEntry);
        assert(KeDisconnectInterrupt(&Shared));
        assert(KeDisconnectInterrupt(&Head));
    }
    return 0;
}
'''.replace('@CONNECT@', body('KeConnectInterrupt')).replace('@DISCONNECT@', body('KeDisconnectInterrupt'))
with tempfile.TemporaryDirectory(prefix='ke-interrupt-') as d:
    path = pathlib.Path(d)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-pthread', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
    def disable_core():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    result = subprocess.run([str(path / 'test'), 'corrupt'], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, preexec_fn=disable_core)
    if result.returncode != -signal.SIGABRT:
        raise RuntimeError(f'corrupt shared interrupt list returned {result.returncode}, expected an assertion')
