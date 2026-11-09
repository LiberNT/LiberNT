# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     AMD64 freeze and processor publication regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--repeat', type=int, default=1)
args = parser.parse_args()
root = pathlib.Path(args.root)
freeze_source = (root / 'ntos/ke/amd64/freeze.c').read_text()
init_source = (root / 'ntos/ke/amd64/krnlinit.c').read_text()


def body(source, name):
    start = source.index(name + '(')
    start = source.index('{', start)
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start + 1:end - 1]


start = init_source.index('    /* Active means it can answer an IPI:')
end = init_source.index('    /* Set the right wait IRQL */', start)
fixture = r'''
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
typedef unsigned long ULONG;
typedef unsigned long long KAFFINITY;
typedef unsigned char BOOLEAN;
typedef void *PVOID;
typedef int64_t LONG64, *PLONG64;
typedef uint64_t KSPIN_LOCK;
typedef struct { int ContextFrame; } PROCESSOR_STATE;
typedef struct { unsigned Number; KAFFINITY SetMember; volatile long IpiFrozen; PROCESSOR_STATE ProcessorState; } KPRCB, *PKPRCB;
#define VOID void
#define TRUE 1
#define FALSE 0
#define MAXIMUM_PROCESSORS 64
#define IPI_FROZEN_STATE_RUNNING 0
#define IPI_FROZEN_STATE_FROZEN 3
#define IPI_FROZEN_STATE_OWNER 4
#define IPI_FROZEN_FLAG_ACTIVE 8
#define IPI_FROZEN_STATE_TARGET_FREEZE 1
#define IPI_FROZEN_STATE_THAW 16
#define IPI_FREEZE 1
#define ASSERT(x) assert(x)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define KeMemoryBarrier() __sync_synchronize()
static KPRCB Cpus[2] = {{.Number=0,.SetMember=1},{.Number=1,.SetMember=2}};
static PKPRCB KiProcessorBlock[2] = {&Cpus[0], &Cpus[1]}, KiFreezeOwner;
static PKPRCB KiFreezeActiveSetOwner __attribute__((unused));
typedef struct { int Valid; } KI_FREEZE_WAIT;
static volatile LONG64 KiFreezeEpoch;
static ULONG KeNumberProcessors = 2;
static KAFFINITY KeActiveProcessors = 1;
static KSPIN_LOCK KiFreezeExecutionLock __attribute__((unused));
static _Thread_local unsigned CurrentCpu;
static int Waiting, Done, GateEnabled, GateReached, GateRelease, FreezeDone, Requests, ReentryEnabled, AutoThaw, GatePhase;
static void KxFreezeExecution(void);
static void KxThawExecution(void);
static PKPRCB KeGetCurrentPrcb(void) { return &Cpus[CurrentCpu]; }
static PVOID InterlockedCompareExchangePointer(PVOID volatile *p, PVOID v, PVOID old) {
    PVOID result = __sync_val_compare_and_swap(p,old,v);
    if (p == (PVOID volatile *)&KiFreezeOwner && CurrentCpu == 1 &&
        v == NULL && result == NULL && __atomic_load_n(&GateEnabled, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&GateReached, 1, __ATOMIC_RELEASE);
        if (__atomic_load_n(&ReentryEnabled, __ATOMIC_ACQUIRE)) {
            while (__atomic_load_n(&KiFreezeOwner, __ATOMIC_ACQUIRE) == NULL) sched_yield();
            KxFreezeExecution();
            KxThawExecution();
        } else {
            while (!__atomic_load_n(&GateRelease, __ATOMIC_ACQUIRE)) sched_yield();
        }
    }
    return result;
}
static PVOID InterlockedExchangePointer(PVOID volatile *p, PVOID v) {
    if (p == (PVOID volatile *)&KiFreezeActiveSetOwner && v == NULL && CurrentCpu == 1 &&
        __atomic_load_n(&GatePhase, __ATOMIC_ACQUIRE) == 2 &&
        !__atomic_load_n(&GateReached, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&GateReached, 1, __ATOMIC_RELEASE);
        while (__atomic_load_n(&KiFreezeOwner, __ATOMIC_ACQUIRE) == NULL) sched_yield();
        KxFreezeExecution();
        KxThawExecution();
    }
    return __sync_lock_test_and_set(p,v);
}
static int __attribute__((unused)) InterlockedBitTestAndSet64(PLONG64 p, int bit) {
    return (__sync_fetch_and_or(p, 1LL << bit) & (1LL << bit)) != 0;
}
static LONG64 __attribute__((unused)) InterlockedAnd64(PLONG64 p, LONG64 value) { return __sync_fetch_and_and(p, value); }
static LONG64 InterlockedIncrement64(volatile LONG64 *p) { return __sync_add_and_fetch(p, 1); }
static void KiNoteFreezeEpoch(void) { @NOTE@ }
static BOOLEAN KiHasFreezeFailure(void) { return FALSE; }
static void __attribute__((noreturn)) HalHaltSystem(void) { __builtin_trap(); }
static BOOLEAN KiFreezeFailureRequiresReturn(PKPRCB Prcb) { (void)Prcb; return FALSE; }
static BOOLEAN KiFreezeWaitExpired(KI_FREEZE_WAIT *Wait) { (void)Wait; return FALSE; }
static BOOLEAN KiFreezeWaitFailed(PKPRCB Prcb, KI_FREEZE_WAIT *Wait) { (void)Prcb; (void)Wait; return FALSE; }
static void KiFreezeIfRequested(void) {
    if (__atomic_load_n(&Cpus[CurrentCpu].IpiFrozen, __ATOMIC_ACQUIRE) == IPI_FROZEN_STATE_TARGET_FREEZE) {
        __atomic_store_n(&Cpus[CurrentCpu].IpiFrozen, IPI_FROZEN_STATE_FROZEN, __ATOMIC_RELEASE);
        while (__atomic_load_n(&Cpus[CurrentCpu].IpiFrozen, __ATOMIC_ACQUIRE) != IPI_FROZEN_STATE_THAW) sched_yield();
        __atomic_store_n(&Cpus[CurrentCpu].IpiFrozen, IPI_FROZEN_STATE_RUNNING, __ATOMIC_RELEASE);
    }
}
static void KiIpiSend(KAFFINITY targets, int type) {
    assert(targets == (CurrentCpu == 0 ? 2 : 1) && type == IPI_FREEZE);
    Requests++;
    if (CurrentCpu != 0 || __atomic_load_n(&GatePhase, __ATOMIC_ACQUIRE) != 2)
        Cpus[1 - CurrentCpu].IpiFrozen = IPI_FROZEN_STATE_FROZEN;
}
static void YieldProcessor(void) {
    if (CurrentCpu == 1) __atomic_store_n(&Waiting, 1, __ATOMIC_RELEASE);
    if (CurrentCpu == 0 && Cpus[1].IpiFrozen == IPI_FROZEN_STATE_THAW &&
        __atomic_load_n(&GatePhase, __ATOMIC_ACQUIRE) != 2)
        Cpus[1].IpiFrozen = IPI_FROZEN_STATE_RUNNING;
    if (CurrentCpu == 1 && Cpus[0].IpiFrozen == IPI_FROZEN_STATE_THAW)
        Cpus[0].IpiFrozen = IPI_FROZEN_STATE_RUNNING;
    sched_yield();
}
@HELPERS@
static KAFFINITY KiFreezeTargetSet(PKPRCB CurrentPrcb) { @TARGETS@ }
static void KxFreezeExecution(void) { @FREEZE@ }
static void KxThawExecution(void) { @THAW@ }
static void *Publish(void *ignored) {
    PKPRCB Prcb = &Cpus[1];
    (void)ignored;
    CurrentCpu = 1;
    @PUBLISH@
    __atomic_store_n(&Done, 1, __ATOMIC_RELEASE);
    return NULL;
}
static void *Freeze(void *ignored) {
    (void)ignored;
    CurrentCpu = 0;
    KxFreezeExecution();
    if (__atomic_load_n(&AutoThaw, __ATOMIC_ACQUIRE)) KxThawExecution();
    __atomic_store_n(&FreezeDone, 1, __ATOMIC_RELEASE);
    return NULL;
}
int main(void) {
    pthread_t thread, freezer;
    KxFreezeExecution();
    assert(KiFreezeOwner == &Cpus[0]);
    assert(pthread_create(&thread, NULL, Publish, NULL) == 0);
    while (!__atomic_load_n(&Waiting, __ATOMIC_ACQUIRE) && !__atomic_load_n(&Done, __ATOMIC_ACQUIRE))
        sched_yield();
    assert(!__atomic_load_n(&Done, __ATOMIC_ACQUIRE));
    assert((KeActiveProcessors & 2) == 0);
    KxThawExecution();
    assert(pthread_join(thread, NULL) == 0);
    assert(__atomic_load_n(&Done, __ATOMIC_ACQUIRE));
    assert((KeActiveProcessors & 2) != 0 && KiFreezeOwner == NULL);

    KeActiveProcessors = 1;
    Done = Waiting = GateReached = GateRelease = FreezeDone = Requests = 0;
    __atomic_store_n(&GateEnabled, 1, __ATOMIC_RELEASE);
    assert(pthread_create(&thread, NULL, Publish, NULL) == 0);
    while (!__atomic_load_n(&GateReached, __ATOMIC_ACQUIRE)) sched_yield();
    assert(pthread_create(&freezer, NULL, Freeze, NULL) == 0);
    while (__atomic_load_n(&KiFreezeOwner, __ATOMIC_ACQUIRE) == NULL) sched_yield();
    assert(!__atomic_load_n(&FreezeDone, __ATOMIC_ACQUIRE));
    __atomic_store_n(&GateRelease, 1, __ATOMIC_RELEASE);
    assert(pthread_join(thread, NULL) == 0);
    assert(pthread_join(freezer, NULL) == 0);
    assert(Requests == 1 && (KeActiveProcessors & 2) != 0);
    assert(Cpus[1].IpiFrozen == IPI_FROZEN_STATE_FROZEN);
    KxThawExecution();
    assert(Cpus[1].IpiFrozen == IPI_FROZEN_STATE_RUNNING);

    KeActiveProcessors = 1;
    KiFreezeActiveSetOwner = &Cpus[0];
    KxFreezeExecution();
    assert(KiFreezeActiveSetOwner == &Cpus[0]);
    KxThawExecution();
    assert(KiFreezeActiveSetOwner == &Cpus[0]);
    KiFreezeActiveSetOwner = NULL;

    KeActiveProcessors = 1;
    Done = Waiting = GateReached = GateRelease = FreezeDone = Requests = 0;
    __atomic_store_n(&ReentryEnabled, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&AutoThaw, 1, __ATOMIC_RELEASE);
    assert(pthread_create(&thread, NULL, Publish, NULL) == 0);
    while (!__atomic_load_n(&GateReached, __ATOMIC_ACQUIRE)) sched_yield();
    assert(pthread_create(&freezer, NULL, Freeze, NULL) == 0);
    assert(pthread_join(freezer, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(Requests == 1 && KeActiveProcessors == 3 && KiFreezeOwner == NULL);
    assert(KiFreezeActiveSetOwner == NULL);

    KeActiveProcessors = 1;
    Done = Waiting = GateReached = GateRelease = FreezeDone = Requests = 0;
    __atomic_store_n(&GateEnabled, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&GatePhase, 2, __ATOMIC_RELEASE);
    assert(pthread_create(&thread, NULL, Publish, NULL) == 0);
    while (!__atomic_load_n(&GateReached, __ATOMIC_ACQUIRE)) sched_yield();
    assert(pthread_create(&freezer, NULL, Freeze, NULL) == 0);
    assert(pthread_join(freezer, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(Requests == 2 && KeActiveProcessors == 3 && KiFreezeOwner == NULL);
    assert(KiFreezeActiveSetOwner == NULL);
    return 0;
}
'''
helpers = ''
for name, signature in [
    ('KiAcquireFreezeActiveSet', 'static BOOLEAN KiAcquireFreezeActiveSet(PKPRCB Prcb)'),
    ('KiReleaseFreezeActiveSet', 'static VOID KiReleaseFreezeActiveSet(PKPRCB Prcb, BOOLEAN Acquired)'),
    ('KiPublishActiveProcessor', 'static VOID KiPublishActiveProcessor(PKPRCB Prcb)'),
]:
    if '\n' + name + '(' in freeze_source:
        helpers += signature + ' {' + body(freeze_source, name) + '}\n'
fixture = fixture.replace('@HELPERS@', helpers)
for marker, name in [('NOTE', 'KiNoteFreezeEpoch'), ('TARGETS', 'KiFreezeTargetSet'), ('FREEZE', 'KxFreezeExecution'), ('THAW', 'KxThawExecution')]:
    fixture = fixture.replace('@' + marker + '@', body(freeze_source, name))
fixture = fixture.replace('@PUBLISH@', init_source[start:end])

with tempfile.TemporaryDirectory(prefix='ke-freeze-publication-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-pthread',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    for _ in range(args.repeat):
        subprocess.run([str(path / 'test')], check=True, timeout=10)
