# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     AMD64 freeze and thaw watchdog regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
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
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
typedef unsigned long ULONG;
typedef uint64_t ULONG64, KAFFINITY;
typedef int64_t LONG64;
typedef unsigned char BOOLEAN;
typedef void *PVOID;
typedef void *PKTRAP_FRAME, *PKEXCEPTION_FRAME;
typedef long *PLONG;
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
typedef struct { int ContextFrame; } PROCESSOR_STATE;
typedef struct { ULONG Number; KAFFINITY SetMember; volatile long IpiFrozen; PROCESSOR_STATE ProcessorState; } KPRCB, *PKPRCB;
typedef struct { ULONG64 Start, Now, Frequency, Epoch; BOOLEAN Valid; } KI_FREEZE_WAIT;
typedef int KCONTINUE_STATUS;
#define ContinueSuccess 1
#define VOID void
#define TRUE 1
#define FALSE 0
#define MAXIMUM_PROCESSORS 64
#define IPI_FROZEN_STATE_RUNNING 0
#define IPI_FROZEN_STATE_TARGET_FREEZE 1
#define IPI_FROZEN_STATE_SAVING 2
#define IPI_FROZEN_STATE_FROZEN 3
#define IPI_FROZEN_STATE_OWNER 4
#define IPI_FROZEN_FLAG_ACTIVE 8
#define IPI_FROZEN_STATE_THAW 16
#define IPI_FREEZE 1
#define IPI_WATCHDOG_TIMEOUT 0x1db
#define ASSERT(x) assert(x)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define KeMemoryBarrier() __sync_synchronize()
static KPRCB Cpus[2];
static PKPRCB KiProcessorBlock[2] = {&Cpus[0], &Cpus[1]}, KiFreezeOwner, KiFreezeActiveSetOwner, KdpPortOwnerPrcb;
static volatile LONG64 KiFreezeEpoch;
static ULONG KeNumberProcessors = 2;
static KAFFINITY KeActiveProcessors = 3;
static BOOLEAN KiRelaxedTiming, KdEnteredDebugger, Failure, CrashOwner;
static unsigned Mode, Yields, ExitReason;
static LONG64 Clock;
static jmp_buf Exit;
static PKPRCB KeGetCurrentPrcb(void) { return &Cpus[0]; }
static PVOID InterlockedCompareExchangePointer(PVOID volatile *p, PVOID v, PVOID old) { return __sync_val_compare_and_swap(p,old,v); }
static PVOID InterlockedExchangePointer(PVOID volatile *p, PVOID v) { return __sync_lock_test_and_set(p,v); }
static long InterlockedCompareExchange(PLONG p, long v, long old) { return __sync_val_compare_and_swap(p,old,v); }
static long InterlockedExchange(PLONG p, long v) { return __sync_lock_test_and_set(p,v); }
static LONG64 InterlockedIncrement64(volatile LONG64 *p) { return __sync_add_and_fetch(p, 1); }
static LONG64 InterlockedCompareExchange64(volatile LONG64 *p, LONG64 v, LONG64 old) { return __sync_val_compare_and_swap(p, old, v); }
static BOOLEAN KiHasFreezeFailure(void) { return Failure; }
static void KiSetFreezeFailure(void) { Failure = TRUE; }
static BOOLEAN KiIsBugCheckEntryOwner(PKPRCB p) { assert(p == &Cpus[0]); return CrashOwner; }
static LARGE_INTEGER KeQueryPerformanceCounter(LARGE_INTEGER *freq) {
    LARGE_INTEGER result; freq->QuadPart = 1; result.QuadPart = Clock++;
    if (result.QuadPart == 10) {
        if (Mode == 6) Cpus[1].IpiFrozen = IPI_FROZEN_STATE_FROZEN;
        if (Mode == 7) Cpus[1].IpiFrozen = IPI_FROZEN_STATE_RUNNING;
        if (Mode == 8) KiFreezeActiveSetOwner = NULL;
        if (Mode == 9) KiFreezeOwner = NULL;
    }
    return result;
}
__attribute__((noreturn)) static void HalHaltSystem(void) { ExitReason = 2; longjmp(Exit,1); }
__attribute__((noreturn)) static void KeBugCheckEx(ULONG code, ULONG64 a, ULONG64 b, ULONG64 c, ULONG64 d) {
    assert(code == IPI_WATCHDOG_TIMEOUT && a == 1 && b - c >= 10 && d == 0);
    assert(Failure); ExitReason = 1; longjmp(Exit,1);
}
static BOOLEAN KiFreezeFailureRequiresReturn(PKPRCB Prcb);
static void YieldProcessor(void);
static KCONTINUE_STATUS KdReportProcessorChange(void) { return ContinueSuccess; }
static void MiAmd64ProcessTlbRequest(ULONG source) { (void)source; }
static void KiFreezeWaitForThaw(PKPRCB CurrentPrcb) { @WAIT@ }
static void KiSaveProcessorState(PKTRAP_FRAME t, PKEXCEPTION_FRAME e) { (void)t; (void)e; assert(0); }
static void KiRestoreProcessorState(PKTRAP_FRAME t, PKEXCEPTION_FRAME e) { (void)t; (void)e; assert(0); }
static BOOLEAN KiProcessorFreezeHandler(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame) { @HANDLER@ }
static void KiFreezeIfRequested(void) { @DEFERRED@ assert(0); }
static void KiIpiSend(KAFFINITY targets, int type) { assert(targets == 2 && type == IPI_FREEZE); Cpus[1].IpiFrozen = Mode == 9 ? IPI_FROZEN_STATE_FROZEN : IPI_FROZEN_STATE_SAVING; }
static void YieldProcessor(void) {
    Yields++;
    assert(Yields < 1000);
    if (Mode == 4 && Yields == 25) Cpus[1].IpiFrozen = IPI_FROZEN_STATE_FROZEN;
    if (Mode == 5 && Yields == 3) Failure = TRUE;
}
static void KiNoteFreezeEpoch(void) { @NOTE@ }
static ULONG64 KiQueryFreezeEpoch(void) { @EPOCH@ }
static BOOLEAN KiFreezeFailureRequiresReturn(PKPRCB Prcb) { @FAILURE@ }
static BOOLEAN KiFreezeWaitExpired(KI_FREEZE_WAIT *Wait) { @EXPIRED@ }
static BOOLEAN KiFreezeWaitFailed(PKPRCB Prcb, KI_FREEZE_WAIT *Wait) { @FAILED@ }
static BOOLEAN KiAcquireFreezeActiveSet(PKPRCB Prcb) { @ACQUIRE@ }
static VOID KiReleaseFreezeActiveSet(PKPRCB Prcb, BOOLEAN Acquired) { @RELEASE@ }
static KAFFINITY KiFreezeTargetSet(PKPRCB CurrentPrcb) { @TARGETS@ }
static void KxFreezeExecution(void) { @FREEZE@ }
static void KxThawExecution(void) { @THAW@ }
static void Reset(void) {
    memset(Cpus,0,sizeof(Cpus)); Cpus[0].SetMember=1; Cpus[1].SetMember=2; Cpus[1].Number=1;
    KiFreezeOwner=KiFreezeActiveSetOwner=NULL; KiFreezeEpoch=0; Clock=0; Yields=ExitReason=0;
    KiRelaxedTiming=KdEnteredDebugger=Failure=CrashOwner=FALSE; Mode=0;
}
int main(void) {
    Reset();
    if (setjmp(Exit)==0) { KxFreezeExecution(); assert(!"freeze returned"); }
    assert(ExitReason==1 && KiFreezeOwner==&Cpus[0] && Cpus[1].IpiFrozen==IPI_FROZEN_STATE_SAVING);
    Reset(); CrashOwner=TRUE;
    KxFreezeExecution();
    assert(Failure && KiFreezeOwner==&Cpus[0] && Cpus[1].IpiFrozen==IPI_FROZEN_STATE_SAVING);
    Reset(); KiFreezeOwner=&Cpus[0]; Cpus[0].IpiFrozen=IPI_FROZEN_STATE_OWNER|IPI_FROZEN_FLAG_ACTIVE; Cpus[1].IpiFrozen=IPI_FROZEN_STATE_FROZEN;
    if (setjmp(Exit)==0) { KxThawExecution(); assert(!"thaw returned"); }
    assert(ExitReason==1 && KiFreezeOwner==&Cpus[0] && Cpus[1].IpiFrozen==IPI_FROZEN_STATE_THAW);
    Reset(); KiFreezeOwner=&Cpus[1];
    if (setjmp(Exit)==0) { KxFreezeExecution(); assert(!"owner wait returned"); }
    assert(ExitReason==1 && KiFreezeOwner==&Cpus[1]);
    Reset(); KiFreezeActiveSetOwner=&Cpus[1];
    if (setjmp(Exit)==0) { KxFreezeExecution(); assert(!"active-set wait returned"); }
    assert(ExitReason==1 && KiFreezeActiveSetOwner==&Cpus[1]);
    Reset(); Mode=4; KiRelaxedTiming=TRUE; KxFreezeExecution();
    assert(!Failure && Yields==25 && Cpus[1].IpiFrozen==IPI_FROZEN_STATE_FROZEN);
    Reset(); Mode=5; CrashOwner=TRUE; KxFreezeExecution();
    assert(Failure && Yields==3 && KiFreezeOwner==&Cpus[0]);
    Reset(); Mode=5;
    if (setjmp(Exit)==0) { KxFreezeExecution(); assert(!"failed freeze returned"); }
    assert(ExitReason==2 && KiFreezeOwner==&Cpus[0]);
    Reset(); Failure=CrashOwner=TRUE; Cpus[0].IpiFrozen=IPI_FROZEN_STATE_FROZEN;
    KiFreezeWaitForThaw(&Cpus[0]);
    assert(Cpus[0].IpiFrozen==IPI_FROZEN_STATE_FROZEN);
    Reset(); Failure=TRUE; Cpus[0].IpiFrozen=IPI_FROZEN_STATE_FROZEN;
    if (setjmp(Exit)==0) { KiFreezeWaitForThaw(&Cpus[0]); assert(!"failed frozen CPU returned"); }
    assert(ExitReason==2);
    Reset(); CrashOwner=TRUE; Cpus[0].IpiFrozen=IPI_FROZEN_STATE_TARGET_FREEZE;
    assert(KiProcessorFreezeHandler((void *)1, (void *)1));
    KiFreezeIfRequested();
    assert(Cpus[0].IpiFrozen==IPI_FROZEN_STATE_TARGET_FREEZE);
    Reset(); Mode=6; KxFreezeExecution();
    assert(!Failure && Cpus[1].IpiFrozen==IPI_FROZEN_STATE_FROZEN);
    Reset(); Mode=7; KiFreezeOwner=&Cpus[0]; Cpus[0].IpiFrozen=IPI_FROZEN_STATE_OWNER|IPI_FROZEN_FLAG_ACTIVE; Cpus[1].IpiFrozen=IPI_FROZEN_STATE_FROZEN;
    KxThawExecution(); assert(!Failure && KiFreezeOwner==NULL);
    Reset(); Mode=8; KiFreezeActiveSetOwner=&Cpus[1]; KiRelaxedTiming=FALSE; Cpus[1].SetMember=0; KeActiveProcessors=1;
    KxFreezeExecution(); assert(!Failure && KiFreezeOwner==&Cpus[0]); KeActiveProcessors=3;
    Reset(); Mode=9; KiFreezeOwner=&Cpus[1]; KxFreezeExecution(); assert(!Failure && KiFreezeOwner==&Cpus[0]);
    return 0;
}
'''
deferred = body('KiFreezeIfRequested').split('    Enable = KeDisableInterrupts();')[0].replace('    BOOLEAN Enable;\n', '')
fixture = fixture.replace('@DEFERRED@', deferred)
for marker, name in [('HANDLER','KiProcessorFreezeHandler'), ('WAIT','KiFreezeWaitForThaw'), ('NOTE','KiNoteFreezeEpoch'), ('EPOCH','KiQueryFreezeEpoch'), ('FAILURE','KiFreezeFailureRequiresReturn'), ('EXPIRED','KiFreezeWaitExpired'), ('FAILED','KiFreezeWaitFailed'), ('ACQUIRE','KiAcquireFreezeActiveSet'), ('RELEASE','KiReleaseFreezeActiveSet'), ('TARGETS','KiFreezeTargetSet'), ('FREEZE','KxFreezeExecution'), ('THAW','KxThawExecution')]:
    fixture = fixture.replace('@' + marker + '@', body(name))
with tempfile.TemporaryDirectory(prefix='ke-freeze-timeout-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
