# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Only the processor that holds a freeze nests it; the next owner starts from a clean state
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'ntos/ke/freezecpu.c').read_text()


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
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#define VOID void
#define TRUE 1
#define FALSE 0
#define NTAPI
#define PASSIVE_LEVEL 0
#define DISPATCH_LEVEL 2
#define HIGH_LEVEL 15
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
#define InterlockedCompareExchange(p, v, c) __sync_val_compare_and_swap((p), (c), (v))
#define InterlockedExchange(p, v) __sync_lock_test_and_set((p), (v))
typedef unsigned char BOOLEAN, KIRQL;
typedef long LONG;
typedef unsigned long ULONG;
typedef void *PKTRAP_FRAME, *PKEXCEPTION_FRAME;
typedef struct { ULONG Number; KIRQL Irql; BOOLEAN Interrupts; } KPRCB, *PKPRCB;
enum { RUNNING, FREEZING, FROZEN, THAWING };
static const char *Scenario;
static KPRCB Cpus[2] = {{0, PASSIVE_LEVEL, TRUE}, {1, DISPATCH_LEVEL, TRUE}};
static PKPRCB Current, ArchOwner;
static ULONG KiFreezeFlag, KiFreezeDepth, Freezes, Thaws, Debugs;
static PKPRCB KiFreezeHolder __attribute__((unused));
static KIRQL KiOldIrql;
static volatile LONG KiFreezeFailure;
static int Others;
static BOOLEAN TimeFrozen, NoArchOwner, DebugWhileFreezing, DebugWhileThawing, ArriveDuringThaw, ArrivedDuringThaw;
static jmp_buf Waiting;
static PKPRCB KeGetCurrentPrcb(void) { return Current; }
static BOOLEAN KeDisableInterrupts(void) { BOOLEAN Old = Current->Interrupts; Current->Interrupts = FALSE; return Old; }
static void KeRestoreInterrupts(BOOLEAN Enable) { Current->Interrupts = Enable; }
static KIRQL KeGetCurrentIrql(void) { return Current->Irql; }
static KIRQL KfRaiseIrql(KIRQL Irql) { KIRQL Old = Current->Irql; Current->Irql = Irql; return Old; }
static void KeLowerIrql(KIRQL Irql) { Current->Irql = Irql; }
static void KiFreezeInterruptTime(void) { TimeFrozen = TRUE; }
static void KiThawInterruptTime(void) { TimeFrozen = FALSE; }
static BOOLEAN KiIsBugCheckEntryOwner(PKPRCB Prcb) { (void)Prcb; return FALSE; }
__attribute__((noreturn)) static void HalHaltSystem(void) { CHECK(!"halted"); exit(1); }
static BOOLEAN KiHasFreezeFailure(void) { @HAS@ }
static BOOLEAN KeFreezeExecution(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame);
static void KeThawExecution(BOOLEAN Enable);
/* The architecture layer answers for the owner it keeps; some keep none */
__attribute__((unused)) static BOOLEAN KxIsFreezeOwner(PKPRCB Prcb) { return !NoArchOwner && ArchOwner == Prcb; }
/* A breakpoint or a failed assertion on the owner enters the debugger again, which freezes and thaws in turn */
static void DebugException(void) {
    BOOLEAN Inner = KeFreezeExecution(NULL, NULL);
    Debugs++;
    CHECK(Inner == FALSE && Current->Irql == HIGH_LEVEL && ArchOwner == Current);
    KeThawExecution(Inner);
    CHECK(Current->Irql == HIGH_LEVEL && !Current->Interrupts);
}
/*
 * The architecture layer: the owner is taken first and let go last, and the owner coming back comes
 * straight back out; anyone else waits there until the owner lets the machine go.
 */
static void KxFreezeExecution(void) {
    CHECK(Current->Irql == HIGH_LEVEL && !Current->Interrupts);
    if (NoArchOwner) {
        Freezes++;
        return;
    }
    if (ArchOwner == Current)
        return;
    if (ArchOwner != NULL)
        longjmp(Waiting, 1);
    ArchOwner = Current;
    Freezes++;
    CHECK(Others == RUNNING);
    Others = FREEZING;
    if (DebugWhileFreezing) {
        DebugWhileFreezing = FALSE;
        DebugException();
    }
    CHECK(ArchOwner == Current && Others == FREEZING);
    Others = FROZEN;
}
static void KxThawExecution(void) {
    CHECK(Current->Irql == HIGH_LEVEL && !Current->Interrupts);
    if (NoArchOwner) {
        Thaws++;
        return;
    }
    CHECK(ArchOwner == Current && Others == FROZEN);
    Others = THAWING;
    if (DebugWhileThawing) {
        DebugWhileThawing = FALSE;
        DebugException();
    }
    CHECK(ArchOwner == Current && Others == THAWING);
    Others = RUNNING;
    ArchOwner = NULL;
    Thaws++;
    /* Another processor that was waiting takes the machine as soon as it is let go */
    if (ArriveDuringThaw) {
        PKPRCB Self = Current;
        ArriveDuringThaw = FALSE;
        Current = &Cpus[1];
        Cpus[1].Irql = DISPATCH_LEVEL; Cpus[1].Interrupts = TRUE;
        (void)KeFreezeExecution(NULL, NULL);
        ArrivedDuringThaw = TRUE;
        Current = Self;
    }
}
static BOOLEAN KeFreezeExecution(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame) { @FREEZE@ }
static void KeThawExecution(BOOLEAN Enable) { @THAW@ }
int main(void) {
    BOOLEAN Enable, Inner;

    Scenario = "nested on the processor that holds the machine";
    Current = &Cpus[0];
    Enable = KeFreezeExecution(NULL, NULL);
    Inner = KeFreezeExecution(NULL, NULL);
    CHECK(Enable == TRUE && Inner == FALSE && Freezes == 1 && TimeFrozen && Current->Irql == HIGH_LEVEL);
    KeThawExecution(Inner);
    CHECK(Thaws == 0 && TimeFrozen && ArchOwner == &Cpus[0]);
    KeThawExecution(Enable);
    CHECK(Thaws == 1 && !TimeFrozen && ArchOwner == NULL && Current->Irql == PASSIVE_LEVEL && Current->Interrupts);

    Scenario = "a second processor while the first holds the machine";
    Freezes = Thaws = 0;
    Current = &Cpus[0];
    Enable = KeFreezeExecution(NULL, NULL);
    Current = &Cpus[1];
    if (setjmp(Waiting) == 0) {
        (void)KeFreezeExecution(NULL, NULL);
        CHECK(!"the second processor took the freeze as its own");
    }
    CHECK(Cpus[1].Irql == HIGH_LEVEL && !Cpus[1].Interrupts);
    Cpus[1].Irql = DISPATCH_LEVEL; Cpus[1].Interrupts = TRUE;
    Current = &Cpus[0];
    KeThawExecution(Enable);
    CHECK(Thaws == 1 && ArchOwner == NULL && !TimeFrozen);
    CHECK(Current->Irql == PASSIVE_LEVEL && Current->Interrupts);

    Scenario = "the next owner arriving as the machine is let go";
    Freezes = Thaws = 0;
    Current = &Cpus[0];
    Enable = KeFreezeExecution(NULL, NULL);
    ArriveDuringThaw = TRUE;
    KeThawExecution(Enable);
    CHECK(ArrivedDuringThaw && Thaws == 1 && Freezes == 2);
    CHECK(Cpus[0].Irql == PASSIVE_LEVEL && Cpus[0].Interrupts);
    CHECK(ArchOwner == &Cpus[1] && TimeFrozen && KiFreezeFlag != 0);
    Current = &Cpus[1];
    KeThawExecution(TRUE);
    CHECK(Thaws == 2 && ArchOwner == NULL && !TimeFrozen && KiFreezeFlag == 0);
    CHECK(Cpus[1].Irql == DISPATCH_LEVEL && Cpus[1].Interrupts);

    Scenario = "a debug exception on the owner while it stops the others";
    Freezes = Thaws = 0;
    Current = &Cpus[0];
    DebugWhileFreezing = TRUE;
    Enable = KeFreezeExecution(NULL, NULL);
    CHECK(Debugs == 1 && Freezes == 1 && Thaws == 0 && ArchOwner == &Cpus[0] && Others == FROZEN);
    CHECK(Enable == TRUE && TimeFrozen && KiFreezeFlag != 0 && Current->Irql == HIGH_LEVEL && !Current->Interrupts);
    KeThawExecution(Enable);
    CHECK(Thaws == 1 && ArchOwner == NULL && Others == RUNNING && !TimeFrozen && KiFreezeFlag == 0);
    CHECK(Current->Irql == PASSIVE_LEVEL && Current->Interrupts);

    Scenario = "a debug exception on the owner while it lets the others go";
    Freezes = Thaws = Debugs = 0;
    Current = &Cpus[0];
    Enable = KeFreezeExecution(NULL, NULL);
    DebugWhileThawing = TRUE;
    KeThawExecution(Enable);
    CHECK(Debugs == 1 && Freezes == 1 && Thaws == 1 && ArchOwner == NULL && Others == RUNNING);
    CHECK(!TimeFrozen && KiFreezeFlag == 0 && Current->Irql == PASSIVE_LEVEL && Current->Interrupts);

    Scenario = "nested where the architecture layer keeps no owner";
    Freezes = Thaws = 0;
    NoArchOwner = TRUE;
    Current = &Cpus[0];
    Enable = KeFreezeExecution(NULL, NULL);
    Inner = KeFreezeExecution(NULL, NULL);
    CHECK(Enable == TRUE && Inner == FALSE && Freezes == 1 && TimeFrozen && Current->Irql == HIGH_LEVEL);
    KeThawExecution(Inner);
    CHECK(Thaws == 0 && TimeFrozen && KiFreezeFlag != 0);
    KeThawExecution(Enable);
    CHECK(Thaws == 1 && !TimeFrozen && KiFreezeFlag == 0 && Current->Irql == PASSIVE_LEVEL && Current->Interrupts);
    return 0;
}
'''
for marker, name in [('HAS', 'KiHasFreezeFailure'), ('FREEZE', 'KeFreezeExecution'), ('THAW', 'KeThawExecution')]:
    fixture = fixture.replace('@' + marker + '@', body(name))
with tempfile.TemporaryDirectory(prefix='ke-freeze-owner-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
