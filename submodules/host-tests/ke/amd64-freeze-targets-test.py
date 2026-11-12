# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     AMD64 freeze marks, signals, waits for and thaws the same processors
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'ntos/ke/amd64/freeze.c').read_text()


def body(name):
    start = source.find('\n' + name + '(')
    if start < 0:
        return None
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
#include <string.h>
typedef unsigned long ULONG;
typedef unsigned long long KAFFINITY;
typedef unsigned char BOOLEAN;
typedef void *PVOID;
typedef struct { int ContextFrame; } PROCESSOR_STATE;
typedef struct { ULONG Number; KAFFINITY SetMember; volatile long IpiFrozen; PROCESSOR_STATE ProcessorState; } KPRCB, *PKPRCB;
#define VOID void
#define TRUE 1
#define FALSE 0
#define MAXIMUM_PROCESSORS 64
#define IPI_FREEZE 4
#define IPI_FROZEN_STATE_RUNNING 0
#define IPI_FROZEN_STATE_FROZEN 2
#define IPI_FROZEN_STATE_THAW 3
#define IPI_FROZEN_STATE_OWNER 4
#define IPI_FROZEN_STATE_TARGET_FREEZE 5
#define IPI_FROZEN_FLAG_ACTIVE 0x20
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define KeMemoryBarrier() __sync_synchronize()
static const char *Scenario;
static KPRCB Cpus[3];
static PKPRCB KiProcessorBlock[MAXIMUM_PROCESSORS], KiFreezeOwner;
static PKPRCB KiFreezeActiveSetOwner __attribute__((unused));
static ULONG KeNumberProcessors;
static KAFFINITY KeActiveProcessors, Signalled;
static unsigned Yields;
static jmp_buf Stuck;
static PKPRCB KeGetCurrentPrcb(void) { return &Cpus[0]; }
static PVOID InterlockedCompareExchangePointer(PVOID volatile *p, PVOID v, PVOID old) { return __sync_val_compare_and_swap(p, old, v); }
static PVOID InterlockedExchangePointer(PVOID volatile *p, PVOID v) { return __sync_lock_test_and_set(p, v); }
static void KiFreezeIfRequested(void) { CHECK(!"the owner was taken"); }
/* An active processor answers the NMI at once */
static void KiIpiSend(KAFFINITY Targets, ULONG Request) {
    ULONG i;
    CHECK(Request == IPI_FREEZE && (Signalled & Targets) == 0);
    Signalled |= Targets;
    for (i = 0; i < 3; i++)
        if ((Targets & Cpus[i].SetMember) && (KeActiveProcessors & Cpus[i].SetMember) &&
            Cpus[i].IpiFrozen == IPI_FROZEN_STATE_TARGET_FREEZE)
            Cpus[i].IpiFrozen = IPI_FROZEN_STATE_FROZEN;
}
/* A thawed processor goes back to running; one that was never told anything does nothing */
static void YieldProcessor(void) {
    ULONG i;
    if (++Yields > 100000)
        longjmp(Stuck, 1);
    for (i = 0; i < 3; i++)
        if (Cpus[i].IpiFrozen == IPI_FROZEN_STATE_THAW)
            Cpus[i].IpiFrozen = IPI_FROZEN_STATE_RUNNING;
}
/* The watchdog never fires here: these scenarios do not wait */
typedef struct { int Valid; } KI_FREEZE_WAIT;
__attribute__((unused)) static BOOLEAN KiHasFreezeFailure(void) { return FALSE; }
__attribute__((unused)) static BOOLEAN KiFreezeFailureRequiresReturn(PKPRCB Prcb) { (void)Prcb; return FALSE; }
__attribute__((unused)) static BOOLEAN KiIsBugCheckEntryOwner(PKPRCB Prcb) { (void)Prcb; return FALSE; }
__attribute__((unused)) static void KiSetFreezeFailure(void) { CHECK(!"a freeze failed"); }
__attribute__((unused)) static BOOLEAN KiFreezeWaitExpired(KI_FREEZE_WAIT *Wait) { (void)Wait; return FALSE; }
__attribute__((unused)) static BOOLEAN KiFreezeWaitFailed(PKPRCB Prcb, KI_FREEZE_WAIT *Wait) { (void)Prcb; (void)Wait; return FALSE; }
__attribute__((unused)) static void KiNoteFreezeEpoch(void) {}
__attribute__((unused)) static void HalHaltSystem(void) { CHECK(!"a processor halted"); }
@TARGETS@
static void KxFreezeExecution(void) { @FREEZE@ }
static void KxThawExecution(void) { @THAW@ }
static void Reset(const char *Name, KAFFINITY Active, int Published) {
    ULONG i;
    Scenario = Name;
    memset(Cpus, 0, sizeof(Cpus));
    memset(KiProcessorBlock, 0, sizeof(KiProcessorBlock));
    for (i = 0; i < 3; i++) {
        Cpus[i].Number = i;
        Cpus[i].SetMember = 1ULL << i;
        if (i < (ULONG)Published)
            KiProcessorBlock[i] = &Cpus[i];
    }
    KeNumberProcessors = 3;
    KeActiveProcessors = Active;
    KiFreezeOwner = NULL;
    Signalled = 0;
    Yields = 0;
}
static void FreezeAndThaw(KAFFINITY Expected) {
    ULONG i;
    if (setjmp(Stuck)) {
        printf("%s: the freeze waited for a processor it never signalled\n", Scenario);
        exit(1);
    }
    KxFreezeExecution();
    CHECK(KiFreezeOwner == &Cpus[0] && Signalled == Expected);
    for (i = 1; i < 3; i++)
        CHECK(Cpus[i].IpiFrozen == ((Expected & Cpus[i].SetMember) ? IPI_FROZEN_STATE_FROZEN : IPI_FROZEN_STATE_RUNNING));
    if (setjmp(Stuck)) {
        printf("%s: the thaw waited for a processor that was never frozen\n", Scenario);
        exit(1);
    }
    KxThawExecution();
    CHECK(KiFreezeOwner == NULL);
    for (i = 0; i < 3; i++)
        CHECK(Cpus[i].IpiFrozen == IPI_FROZEN_STATE_RUNNING);
}
int main(void) {
    Reset("all active", 7, 3);
    FreezeAndThaw(6);
    Reset("counted, not active yet", 3, 3);
    FreezeAndThaw(2);
    Reset("counted, not published yet", 3, 2);
    FreezeAndThaw(2);
    return 0;
}
'''

helpers = ''
for name, prototype in [('KiAcquireFreezeActiveSet', 'static BOOLEAN KiAcquireFreezeActiveSet(PKPRCB Prcb)'),
                        ('KiReleaseFreezeActiveSet', 'static void KiReleaseFreezeActiveSet(PKPRCB Prcb, BOOLEAN Acquired)'),
                        ('KiFreezeTargetSet', 'static KAFFINITY KiFreezeTargetSet(PKPRCB CurrentPrcb)')]:
    text = body(name)
    if text is not None:
        helpers += prototype + ' {' + text + '}\n'
fixture = fixture.replace('@TARGETS@', helpers)
fixture = fixture.replace('@FREEZE@', body('KxFreezeExecution'))
fixture = fixture.replace('@THAW@', body('KxThawExecution'))
with tempfile.TemporaryDirectory(prefix='ke-freeze-targets-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
