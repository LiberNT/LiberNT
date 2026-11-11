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
crash = (root / 'ntos/ke/crash.c').read_text()
freeze = (root / 'ntos/ke/freezecpu.c').read_text()
enable = (root / 'ntos/kd64/kdenable.c').read_text()
report = (root / 'ntos/kd64/kdexception.c').read_text()


def body(source, name, fallback=None):
    start = source.find('\n' + name + '(')
    if start < 0:
        if fallback is None:
            raise ValueError(name)
        return fallback
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[opening + 1:end - 1]


fixture = r'''
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define VOID void
#define TRUE 1
#define FALSE 0
#define NTAPI
#define UserMode 1
#define HIGH_LEVEL 15
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
typedef unsigned char BOOLEAN, KIRQL, KPROCESSOR_MODE;
typedef long LONG;
typedef unsigned long ULONG;
typedef void *PVOID;
typedef struct { long long QuadPart; } LARGE_INTEGER;
typedef struct { int Unused; } KTHREAD, *PKTHREAD;
typedef struct { PKTHREAD CurrentThread; } KPRCB, *PKPRCB;
typedef void *PKTRAP_FRAME, *PKEXCEPTION_FRAME, *PEXCEPTION_RECORD, *PCONTEXT;
typedef struct { int Unused; } KDPC;
#define InterlockedCompareExchangePointer(p, v, c) __sync_val_compare_and_swap((p), (c), (v))
#define InterlockedCompareExchange(p, v, c) __sync_val_compare_and_swap((p), (c), (v))
#define InterlockedExchange(p, v) __sync_lock_test_and_set((p), (v))
static const char *Scenario;
static KTHREAD Threads[2];
static KPRCB Cpus[2] = {{&Threads[0]}, {&Threads[1]}};
static PKPRCB Current;
static PKTHREAD KeBugCheckOwner;
static volatile LONG KiFreezeFailure;
static ULONG KiFreezeDepth, KiFreezeFlag;
static KIRQL KiOldIrql;
static BOOLEAN KdEnteredDebugger, KdDebuggerEnabled = TRUE, KdIgnoreUmExceptions, KdAutoEnableOnEvent;
static BOOLEAN KdpReportedFirstChance;
static KPROCESSOR_MODE KdpReportedMode;
static PVOID KdpTimeSlipEvent = &KdpTimeSlipEvent;
static KDPC KdpTimeSlipDpc;
static LARGE_INTEGER KdTimerStart, KdTimerStop, KdTimerDifference;
static int Halts, Freezes, Thaws, PortLocks, PortUnlocks, Reports, TimeSlips, SimulateFailure;
static jmp_buf Halted;
__attribute__((unused)) static PKPRCB KeGetCurrentPrcb(void) { return Current; }
static LARGE_INTEGER KeQueryPerformanceCounter(LARGE_INTEGER *Frequency) { LARGE_INTEGER r = {0}; (void)Frequency; return r; }
__attribute__((noreturn, unused)) static void HalHaltSystem(void) { Halts++; longjmp(Halted, 1); }
static void KiSetFreezeFailure(void);
static BOOLEAN KiHasFreezeFailure(void);
static BOOLEAN KiIsBugCheckEntryOwner(PKPRCB Prcb);
static BOOLEAN KeFreezeExecution(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame) {
    (void)TrapFrame; (void)ExceptionFrame; Freezes++; KiFreezeDepth = 1;
    if (SimulateFailure) KiSetFreezeFailure();
    return TRUE;
}
static void KxThawExecution(void) { Thaws++; }
static void KeRestoreInterrupts(BOOLEAN Enable) { (void)Enable; }
static void KeLowerIrql(KIRQL Irql) { (void)Irql; }
static void KiThawInterruptTime(void) {}
static void KdpPortLock(void) { PortLocks++; }
static void KdpPortUnlock(void) { PortUnlocks++; }
static BOOLEAN KdEnableDebuggerWithLock(BOOLEAN NeedLock) { (void)NeedLock; return TRUE; }
static BOOLEAN KdpReportExceptionStateChange(PEXCEPTION_RECORD r, PCONTEXT c, BOOLEAN s) { (void)r; (void)c; (void)s; Reports++; return FALSE; }
static void KdSetOwedBreakpoints(void) {}
static BOOLEAN KeInsertQueueDpc(KDPC *Dpc, PVOID a, PVOID b) { (void)Dpc; (void)a; (void)b; TimeSlips++; return TRUE; }
#define NT_SUCCESS(x) (x)
static void KiSetFreezeFailure(void) { @SET@ }
static BOOLEAN KiHasFreezeFailure(void) { @HAS@ }
static BOOLEAN KiIsBugCheckEntryOwner(PKPRCB Prcb) { @OWNER@ }
static void KeThawExecution(BOOLEAN Enable) { @THAW@ }
static BOOLEAN KdEnterDebugger(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame) { @ENTER@ }
static void KdExitDebugger(BOOLEAN Enable) { @EXIT@ }
static BOOLEAN KdpReport(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame, PEXCEPTION_RECORD ExceptionRecord,
                         PCONTEXT ContextRecord, KPROCESSOR_MODE PreviousMode, BOOLEAN SecondChanceException) { @REPORT@ }
static void Reset(const char *Name, int Cpu, int Failed) {
    Scenario = Name; Current = &Cpus[Cpu];
    KiFreezeFailure = Failed; KeBugCheckOwner = &Threads[0];
    KiFreezeDepth = KiFreezeFlag = 0; KdEnteredDebugger = FALSE;
    Halts = Freezes = Thaws = PortLocks = PortUnlocks = Reports = TimeSlips = SimulateFailure = 0;
}
int main(void) {
    Reset("owner", 0, FALSE);
    CHECK(KiIsBugCheckEntryOwner(&Cpus[0]) && !KiIsBugCheckEntryOwner(&Cpus[1]));
    KeBugCheckOwner = NULL;
    CHECK(!KiIsBugCheckEntryOwner(&Cpus[0]) && !KiIsBugCheckEntryOwner(&Cpus[1]));
    KiSetFreezeFailure();
    CHECK(KiHasFreezeFailure());

    /* Once a freeze has failed, a processor that does not own the stop halts where it is */
    Reset("debugger entry on another processor", 1, TRUE);
    if (setjmp(Halted) == 0) { (void)KdEnterDebugger(NULL, NULL); CHECK(!"entered the debugger"); }
    CHECK(Halts == 1 && Freezes == 0 && PortLocks == 0);
    Reset("debugger exit on another processor", 1, TRUE);
    if (setjmp(Halted) == 0) { KdExitDebugger(TRUE); CHECK(!"left the debugger"); }
    CHECK(Halts == 1 && Thaws == 0 && PortUnlocks == 0);
    Reset("thaw on another processor", 1, TRUE);
    if (setjmp(Halted) == 0) { KeThawExecution(TRUE); CHECK(!"thawed"); }
    CHECK(Halts == 1 && Thaws == 0);

    /* The owner of the stop goes on, without the debugger and without letting anything go */
    Reset("debugger entry on the owner", 0, TRUE);
    CHECK(KdEnterDebugger(NULL, NULL) == FALSE);
    CHECK(Halts == 0 && Freezes == 0 && PortLocks == 0 && !KdEnteredDebugger);
    KdExitDebugger(FALSE);
    CHECK(Thaws == 0 && PortUnlocks == 0);
    Reset("thaw on the owner", 0, TRUE);
    KeThawExecution(TRUE);
    CHECK(Halts == 0 && Thaws == 0);
    Reset("report on the owner", 0, TRUE);
    CHECK(KdpReport(NULL, NULL, NULL, NULL, 0, FALSE) == TRUE);
    CHECK(Reports == 0 && Freezes == 0 && PortLocks == 0 && TimeSlips == 0);

    /* A freeze that fails on the way in leaves the port alone */
    Reset("freeze failing on entry", 0, FALSE);
    SimulateFailure = TRUE;
    CHECK(KdEnterDebugger(NULL, NULL) == TRUE);
    CHECK(Freezes == 1 && KiHasFreezeFailure() && PortLocks == 0 && !KdEnteredDebugger);
    Reset("report with a freeze failing on entry", 0, FALSE);
    SimulateFailure = TRUE;
    CHECK(KdpReport(NULL, NULL, NULL, NULL, 0, FALSE) == TRUE);
    CHECK(Freezes == 1 && Reports == 0 && PortLocks == 0 && TimeSlips == 0);

    /* Without a failure nothing changes */
    Reset("ordinary report", 0, FALSE);
    CHECK(KdpReport(NULL, NULL, NULL, NULL, 0, FALSE) == FALSE);
    CHECK(Freezes == 1 && Reports == 1 && PortLocks == 1 && PortUnlocks == 1 && Thaws == 1 && TimeSlips == 1);
    return 0;
}
'''
for marker, source, name, fallback in [
    ('SET', freeze, 'KiSetFreezeFailure', 'InterlockedExchange(&KiFreezeFailure, TRUE);'),
    ('HAS', freeze, 'KiHasFreezeFailure', 'return KiFreezeFailure != FALSE;'),
    ('OWNER', crash, 'KiIsBugCheckEntryOwner', 'return KeBugCheckOwner != NULL && KeBugCheckOwner == Prcb->CurrentThread;'),
    ('THAW', freeze, 'KeThawExecution', None),
    ('ENTER', enable, 'KdEnterDebugger', None),
    ('EXIT', enable, 'KdExitDebugger', None),
    ('REPORT', report, 'KdpReport', None),
]:
    fixture = fixture.replace('@' + marker + '@', body(source, name, fallback))
with tempfile.TemporaryDirectory(prefix='ke-bugcheck-freeze-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
