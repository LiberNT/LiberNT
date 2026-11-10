# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Bugcheck ownership and incomplete-freeze debugger regression
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
bug = (root / 'ntos/ke/bug.c').read_text()
freeze = (root / 'ntos/ke/freeze.c').read_text()
kd = (root / 'ntos/kd64/kdapi.c').read_text()


def body(source, name):
    start = source.index('\n' + name + '(')
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[opening + 1:end - 1]


start = bug.index('    if (!KiClaimBugCheckEntry(Prcb))', bug.index('KeBugCheckWithTf('))
end = bug.index('    IsDoubleFault =', start)
prologue = bug[start:end]
trap = (root / 'ntos/kd64/kdtrap.c').read_text()
assert 'KdEnterDebugger(TrapFrame, ExceptionFrame);\n    if (KiHasFreezeFailure()) return TRUE;' in trap
assert 'KdExitDebugger(Enable);\n    if (KiHasFreezeFailure()) return TRUE;' in trap
fixture = r'''
#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <sched.h>
#define VOID void
#define TRUE 1
#define FALSE 0
#define NULLPTR ((void *)0)
typedef int BOOLEAN, LONG, KIRQL;
typedef unsigned long ULONG;
typedef uintptr_t ULONG_PTR;
typedef void *PVOID;
typedef struct { int DebuggerSavedIRQL; } KPRCB, *PKPRCB;
typedef void *PKTRAP_FRAME, *PKEXCEPTION_FRAME;
typedef struct { long long QuadPart; } LARGE_INTEGER;
static PVOID volatile KiBugCheckEntryOwner;
static volatile LONG KiFreezeFailure;
static ULONG KeBugCheckActive;
static ULONG_PTR KiBugCheckData[5];
static KPRCB Cpus[8];
static _Thread_local PKPRCB Current;
static jmp_buf Jump;
static int Disabled, SimulateFailure, FreezeCalls, PortCalls, InterruptTimeCalls;
static int KdEnteredDebugger, KdpDebuggerNestedDepth, KiFreezeFlag;
static int KdpDebuggerLock, KdpPortLocked;
static LARGE_INTEGER KdTimerStop, KdTimerStart, KdTimerDifference;
static volatile int Start, Winners;
#define InterlockedCompareExchangePointer(p,v,c) __sync_val_compare_and_swap((p),(c),(v))
#define InterlockedCompareExchange(p,v,c) __sync_val_compare_and_swap((p),(c),(v))
#define InterlockedExchange(p,v) __sync_lock_test_and_set((p),(v))
#define InterlockedIncrement(p) __sync_add_and_fetch((p),1)
static PKPRCB KeGetCurrentPrcb(void) { return Current; }
static int KeGetCurrentIrql(void) { return 0; }
static void HalHaltSystem(void) { longjmp(Jump, 1); }
static void Disable(void) { Disabled++; }
#define _disable() Disable()
static BOOLEAN KiClaimBugCheckEntry(PKPRCB Prcb) { @CLAIM@ }
static BOOLEAN KiIsBugCheckEntryOwner(PKPRCB Prcb) { @OWNER@ }
static VOID KiSetFreezeFailure(void) { @SET@ }
static BOOLEAN KiHasFreezeFailure(void) { @HAS@ }
static void CrashPrologue(ULONG BugCheckCode, ULONG_PTR BugCheckParameter1,
                          ULONG_PTR BugCheckParameter2, ULONG_PTR BugCheckParameter3,
                          ULONG_PTR BugCheckParameter4) {
    PKPRCB Prcb = KeGetCurrentPrcb();
    @PROLOGUE@
    assert(!"continued beyond crash prologue");
}
static BOOLEAN KdpDebuggerLockOwnedByCurrentThread(void) { return FALSE; }
static LARGE_INTEGER KdpQueryPerformanceCounter(PKTRAP_FRAME t) { LARGE_INTEGER r = {0}; (void)t; return r; }
static BOOLEAN KeFreezeExecution(PKTRAP_FRAME t, PKEXCEPTION_FRAME e) {
    (void)t; (void)e; FreezeCalls++;
    if (SimulateFailure) KiSetFreezeFailure();
    return TRUE;
}
static void KiFreezeInterruptTime(void) { InterruptTimeCalls++; }
static BOOLEAN KeTryToAcquireSpinLockAtDpcLevel(int *p) { (void)p; PortCalls++; return TRUE; }
static void KdSave(BOOLEAN b) { (void)b; PortCalls++; }
static void KdpDprintf(const char *f, ...) { (void)f; PortCalls++; }
static BOOLEAN KdEnterDebugger(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame) { @ENTER@ }
static void *claim(void *p) {
    Current = p;
    while (!__atomic_load_n(&Start, __ATOMIC_ACQUIRE)) sched_yield();
    if (KiClaimBugCheckEntry(Current)) __atomic_fetch_add(&Winners, 1, __ATOMIC_RELAXED);
    return NULL;
}
int main(void) {
    pthread_t threads[8];
    int i;
    for (i=0; i<8; i++) assert(pthread_create(&threads[i], NULL, claim, &Cpus[i]) == 0);
    __atomic_store_n(&Start, 1, __ATOMIC_RELEASE);
    for (i=0; i<8; i++) assert(pthread_join(threads[i], NULL) == 0);
    assert(Winners == 1);
    for (i=0; i<8; i++) assert(KiIsBugCheckEntryOwner(&Cpus[i]) == ((PVOID)&Cpus[i] == KiBugCheckEntryOwner));

    KiBugCheckEntryOwner = NULL;
    Current = &Cpus[0];
    KiSetFreezeFailure();
    if (setjmp(Jump) == 0) CrashPrologue(0x1db, 11, 22, 33, 44);
    assert(KeBugCheckActive && Disabled == 1 && KiIsBugCheckEntryOwner(Current));
    assert(KiBugCheckData[0] == 0x1db && KiBugCheckData[1] == 11 && KiBugCheckData[4] == 44);
    Current = &Cpus[1];
    if (setjmp(Jump) == 0) CrashPrologue(0x101, 1, 2, 3, 4);
    assert(KiBugCheckData[0] == 0x1db && KiBugCheckData[1] == 11);
    if (setjmp(Jump) == 0) { (void)KdEnterDebugger(NULLPTR, NULLPTR); assert(!"nonowner returned"); }

    Current = &Cpus[0];
    assert(KdEnterDebugger(NULLPTR, NULLPTR) == FALSE);
    assert(FreezeCalls == 0 && PortCalls == 0 && InterruptTimeCalls == 0);
    KiFreezeFailure = FALSE;
    SimulateFailure = TRUE;
    assert(KdEnterDebugger(NULLPTR, NULLPTR) == TRUE);
    assert(FreezeCalls == 1 && KiHasFreezeFailure());
    assert(PortCalls == 0 && InterruptTimeCalls == 0 && !KdEnteredDebugger);
    puts("bugcheck ownership and failed debugger freeze passed");
    return 0;
}
'''
for marker, source, name in [
    ('CLAIM', bug, 'KiClaimBugCheckEntry'),
    ('OWNER', bug, 'KiIsBugCheckEntryOwner'),
    ('SET', freeze, 'KiSetFreezeFailure'),
    ('HAS', freeze, 'KiHasFreezeFailure'),
    ('ENTER', kd, 'KdEnterDebugger'),
]:
    fixture = fixture.replace('@' + marker + '@', body(source, name))
fixture = fixture.replace('@PROLOGUE@', prologue)
with tempfile.TemporaryDirectory(prefix='ke-bugcheck-freeze-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-pthread',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)