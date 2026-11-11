# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     AMD64 application processor start: active once it can take an IPI, never during a freeze, within a bound
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root)
init_source = (root / 'ntos/ke/amd64/krnlinit.c').read_text()
freeze_source = (root / 'ntos/ke/amd64/freeze.c').read_text()
mproc_source = (root / 'ntos/ke/amd64/mproc.c').read_text()


def body(source, name):
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned char BOOLEAN, KIRQL;
typedef unsigned long ULONG;
typedef unsigned long long KAFFINITY;
typedef uint64_t ULONG_PTR, KSPIN_LOCK;
typedef int64_t LONG64, *PLONG64;
typedef void *PVOID;
typedef struct { KAFFINITY ProcessorMask; } KNODE, *PKNODE;
typedef struct { ULONG Number; KAFFINITY SetMember; PKNODE ParentNode; void *CurrentThread, *NextThread, *IdleThread; int ProcessorState; } KPRCB, *PKPRCB;
typedef struct { int Unused; } KPROCESS, *PKPROCESS;
typedef struct { struct { PKPROCESS Process; } ApcState; long Priority; KIRQL WaitIrql; } KTHREAD, *PKTHREAD;
typedef struct { ULONG_PTR KernelStack, Prcb, Thread; } LOADER_PARAMETER_BLOCK, *PLOADER_PARAMETER_BLOCK;
#define VOID void
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define APC_LEVEL 1
#define DISPATCH_LEVEL 2
#define HIGH_LEVEL 15
#define ASSERT(x) CHECK(x)
#define KeMemoryBarrier() __sync_synchronize()
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
static const char *Scenario;
static KNODE Node0, *KeNodeBlock[1] = {&Node0};
static KPRCB Cpus[2];
static KPROCESS Process;
static KTHREAD Thread1;
static LOADER_PARAMETER_BLOCK Loader, *KeLoaderBlock = &Loader;
static PKPRCB KiFreezeOwner, KiFreezeActiveSetOwner __attribute__((unused));
static KAFFINITY KeActiveProcessors, KiIdleSummary;
static KSPIN_LOCK KiFreezeExecutionLock __attribute__((unused));
static KIRQL Irql;
static BOOLEAN InterruptsEnabled;
static unsigned Yields, ThawAfter;
static jmp_buf Idle;
#define AP_ACTIVE() ((KeActiveProcessors & Cpus[1].SetMember) != 0)
/* An active processor is sent IPIs: it has to be able to take them, and a frozen machine gets no new ones */
static void CheckActive(void) {
    if (!AP_ACTIVE())
        return;
    CHECK(InterruptsEnabled && Irql <= DISPATCH_LEVEL);
    CHECK(KiFreezeOwner == NULL);
}
static PKPRCB KeGetCurrentPrcb(void) { return &Cpus[1]; }
static PKTHREAD KeGetCurrentThread(void) { return &Thread1; }
static void PoInitializePrcb(PKPRCB Prcb) { (void)Prcb; }
static void KiSaveProcessorControlState(int *State) { (void)State; }
static void KiGetCacheInformation(void) {}
static void KiInitSpinLocks(PKPRCB Prcb, ULONG Number) { (void)Prcb; (void)Number; }
static void ExInitPoolLookasidePointers(void) {}
static void KiInitializeKernel(PKPROCESS p, PKTHREAD t, PVOID s, PKPRCB r, PLOADER_PARAMETER_BLOCK l) { (void)p; (void)t; (void)s; (void)r; (void)l; CHECK(0); }
static void KiInitializeHandBuiltThread(PKTHREAD t, PKPROCESS p, PVOID s) { (void)t; (void)p; (void)s; }
static void KiCalculateCpuFrequency(PKPRCB Prcb) { (void)Prcb; }
static KIRQL KfRaiseIrql(KIRQL New) { KIRQL Old = Irql; CheckActive(); Irql = New; return Old; }
static void KeLowerIrql(KIRQL New) { CheckActive(); Irql = New; }
static void KeSetPriorityThread(PKTHREAD t, long p) { (void)t; (void)p; }
static void KiAcquirePrcbLock(PKPRCB Prcb) { (void)Prcb; }
static void KiReleasePrcbLock(PKPRCB Prcb) { (void)Prcb; }
static void _enable(void) { CheckActive(); InterruptsEnabled = TRUE; }
__attribute__((unused)) static BOOLEAN InterlockedBitTestAndSet64(PLONG64 Base, LONG64 Bit) { return (__sync_fetch_and_or(Base, 1LL << Bit) >> Bit) & 1; }
__attribute__((unused)) static LONG64 InterlockedAnd64(PLONG64 Base, LONG64 Mask) { CheckActive(); return __sync_fetch_and_and(Base, Mask); }
__attribute__((unused)) static PVOID InterlockedCompareExchangePointer(PVOID volatile *p, PVOID v, PVOID old) { return __sync_val_compare_and_swap(p, old, v); }
__attribute__((unused)) static PVOID InterlockedExchangePointer(PVOID volatile *p, PVOID v) { CheckActive(); return __sync_lock_test_and_set(p, v); }
/* The freeze in progress ends after a few spins */
__attribute__((unused)) static void YieldProcessor(void) {
    CheckActive();
    if (++Yields > 100000) { printf("%s: the processor never became active\n", Scenario); exit(1); }
    if (Yields == ThawAfter) KiFreezeOwner = NULL;
}
__attribute__((noreturn)) static void KiIdleLoop(void) { CheckActive(); longjmp(Idle, 1); }
/* The watchdog never fires here: these scenarios do not wait */
typedef struct { int Valid; } KI_FREEZE_WAIT;
__attribute__((unused)) static BOOLEAN KiHasFreezeFailure(void) { return FALSE; }
__attribute__((unused)) static BOOLEAN KiFreezeFailureRequiresReturn(PKPRCB Prcb) { (void)Prcb; return FALSE; }
__attribute__((unused)) static BOOLEAN KiFreezeWaitExpired(KI_FREEZE_WAIT *Wait) { (void)Wait; return FALSE; }
__attribute__((unused)) static BOOLEAN KiFreezeWaitFailed(PKPRCB Prcb, KI_FREEZE_WAIT *Wait) { (void)Prcb; (void)Wait; return FALSE; }
__attribute__((unused)) static void KiNoteFreezeEpoch(void) {}
__attribute__((unused)) static void HalHaltSystem(void) { CHECK(!"a processor halted"); }
@PUBLISH@
static void KiSystemStartupBootStack(void) { @STARTUP@ }
static void Run(const char *Name, PKPRCB FreezeOwner, unsigned Thaw) {
    Scenario = Name;
    Cpus[0].SetMember = 1;
    Cpus[1].Number = 1;
    Cpus[1].SetMember = 2;
    Thread1.ApcState.Process = &Process;
    Loader.Thread = (ULONG_PTR)&Thread1;
    Loader.Prcb = (ULONG_PTR)&Cpus[1];
    KeActiveProcessors = 1;
    KiFreezeOwner = FreezeOwner;
    ThawAfter = Thaw;
    Yields = 0;
    Irql = HIGH_LEVEL;
    InterruptsEnabled = FALSE;
    if (setjmp(Idle) == 0)
        KiSystemStartupBootStack();
    CHECK(AP_ACTIVE() && Loader.Prcb == 0 && InterruptsEnabled && Irql == DISPATCH_LEVEL);
}
int main(void) {
    Run("no freeze", NULL, 0);
    Run("freeze in progress", &Cpus[0], 5);
    CHECK(Yields >= 5);
    return 0;
}
'''

publish = ''
for name, prototype in [('KiAcquireFreezeActiveSet', 'static BOOLEAN KiAcquireFreezeActiveSet(PKPRCB Prcb)'),
                        ('KiReleaseFreezeActiveSet', 'static void KiReleaseFreezeActiveSet(PKPRCB Prcb, BOOLEAN Acquired)'),
                        ('KiPublishActiveProcessor', 'static void KiPublishActiveProcessor(PKPRCB Prcb)')]:
    text = body(freeze_source, name)
    if text is not None:
        publish += prototype + ' {' + text + '}\n'
fixture = fixture.replace('@PUBLISH@', publish)
fixture = fixture.replace('@STARTUP@', body(init_source, 'KiSystemStartupBootStack'))

startup = r'''
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char UINT8, BOOLEAN;
typedef unsigned long ULONG;
typedef uint64_t ULONG64, ULONG_PTR, ULONGLONG, KAFFINITY;
typedef void *PVOID;
typedef struct { unsigned short Pad[3]; unsigned short Limit; PVOID Base; } KDESCRIPTOR;
typedef struct { KDESCRIPTOR Gdtr, Idtr; ULONG64 Cr0, Cr3, Cr4; unsigned short Tr; } KSPECIAL_REGISTERS;
typedef struct { unsigned short SegCs, SegDs, SegEs, SegSs, SegFs, SegGs; ULONG64 Rsp, Rip, Rcx, EFlags; } CONTEXT;
typedef struct { KSPECIAL_REGISTERS SpecialRegisters; CONTEXT ContextFrame; } KPROCESSOR_STATE, *PKPROCESSOR_STATE;
typedef struct { ULONG Number; KAFFINITY SetMember; KPROCESSOR_STATE ProcessorState; } KPRCB, *PKPRCB;
typedef struct { KPRCB Prcb; } KIPCR, *PKIPCR;
typedef struct { char Data[16]; } KIDTENTRY64, KGDTENTRY64, KTSS64, ETHREAD;
typedef KGDTENTRY64 *PKGDTENTRY64;
typedef KIDTENTRY64 *PKIDTENTRY64;
typedef KTSS64 *PKTSS64;
typedef void *PKTHREAD;
typedef struct { ULONG_PTR KernelStack, Prcb, Thread; } LOADER_PARAMETER_BLOCK;
#define VOID void
#define TRUE 1
#define FALSE 0
#define PAGE_SIZE 4096
#define DOUBLE_FAULT_STACK_SIZE 4096
#define MAXIMUM_PROCESSORS 64
#define DECLSPEC_ALIGN(n) __attribute__((aligned(n)))
@DEFINES@
#define EFLAGS_INTERRUPT_MASK 0x200
#define CLOCK_WATCHDOG_TIMEOUT 0x101
#define NonPagedPool 0
#define TAG_KERNEL 0
#define KGDT64_R0_CODE 0x10
#define KGDT64_R0_DATA 0x18
#define KGDT64_SYS_TSS 0x40
#define DPRINT1(...) ((void)0)
#define ASSERT(x) CHECK(x)
#define CHECK(x) do { if (!(x)) { printf("startup:%d: %s failed\n", __LINE__, #x); exit(1); } } while (0)
#define AFFINITY_MASK(n) ((KAFFINITY)1 << (n))
#define ALIGN_UP_BY(v, a) (((ULONG_PTR)(v) + ((a) - 1)) & ~((ULONG_PTR)(a) - 1))
#define ALIGN_DOWN_BY(v, a) ((ULONG_PTR)(v) & ~((ULONG_PTR)(a) - 1))
#define ALIGN_DOWN_POINTER_BY(v, a) ((PVOID)ALIGN_DOWN_BY((v), (a)))
#define RtlCopyMemory(d, s, n) memcpy((d), (s), (n))
#define RtlZeroMemory(d, n) memset((d), 0, (n))
#define KeMemoryBarrier() __sync_synchronize()
#define min(a, b) ((a) < (b) ? (a) : (b))
typedef struct _APINFO
{
    DECLSPEC_ALIGN(PAGE_SIZE) KIDTENTRY64 Idt[256];
    DECLSPEC_ALIGN(PAGE_SIZE) KGDTENTRY64 Gdt[128];
    DECLSPEC_ALIGN(16) UINT8 DoubleFaultStack[DOUBLE_FAULT_STACK_SIZE];
    DECLSPEC_ALIGN(16) UINT8 NmiStack[DOUBLE_FAULT_STACK_SIZE];
    KIPCR Pcr;
    ETHREAD Thread;
    KTSS64 Tss;
} APINFO, *PAPINFO;
static ULONG KeMaximumProcessors = 3, KeNumprocSpecified, KeBootprocSpecified, KeMaximumIncrement = 156250;
static LOADER_PARAMETER_BLOCK Loader, *KeLoaderBlock = &Loader;
static PKPRCB KiProcessorBlock[MAXIMUM_PROCESSORS];
static KAFFINITY KeActiveProcessors, Started;
static ULONGLONG Clock;
static ULONG Mode, StopCode;
static ULONG_PTR StopParameter[4];
static unsigned Yields;
static jmp_buf Stopped;
static char Table[16];
static PVOID ExAllocatePoolZero(int Pool, size_t Size, ULONG Tag) {
    PVOID p; (void)Pool; (void)Tag;
    if (posix_memalign(&p, PAGE_SIZE, Size)) return NULL;
    memset(p, 0, Size); return p;
}
static void ExFreePoolWithTag(PVOID p, ULONG Tag) { (void)Tag; free(p); }
static PVOID MmCreateKernelStack(BOOLEAN Large, UINT8 Node) { (void)Large; (void)Node; return (char *)malloc(8192) + 8192; }
static void MmDeleteKernelStack(PVOID Stack, BOOLEAN Large) { (void)Large; free((char *)Stack - 8192); }
static void Descriptor(unsigned short *Limit) { KDESCRIPTOR *d = (KDESCRIPTOR *)((char *)Limit - 6); d->Limit = sizeof(Table) - 1; d->Base = Table; }
#define __sgdt(p) Descriptor(p)
#define __sidt(p) Descriptor(p)
static ULONG64 __readcr0(void) { return 0; }
static ULONG64 __readcr3(void) { return 0; }
static ULONG64 __readcr4(void) { return 0; }
static ULONG64 __readeflags(void) { return 0; }
static void KiSystemStartup(void) {}
static void KiInitializeProcessorBootStructures(ULONG Number, PKIPCR Pcr, PKGDTENTRY64 g, PKIDTENTRY64 i, PKTSS64 t,
                                                PKTHREAD th, PVOID ks, PVOID ds, PVOID df, PVOID nm) {
    (void)g; (void)i; (void)t; (void)th; (void)ks; (void)ds; (void)df; (void)nm;
    Pcr->Prcb.Number = Number; Pcr->Prcb.SetMember = AFFINITY_MASK(Number);
}
/*
 * Mode 0: the processor takes the loader block at once and becomes active a few spins later.
 * Mode 1: it never takes the loader block. Mode 2: it takes it and never becomes active.
 */
static BOOLEAN HalStartNextProcessor(LOADER_PARAMETER_BLOCK *Block, PKPROCESSOR_STATE State) {
    PKPRCB Prcb = (PKPRCB)Block->Prcb; (void)State;
    Started |= Prcb->SetMember;
    if (Mode == 1) return TRUE;
    KiProcessorBlock[Prcb->Number] = Prcb;
    Block->Prcb = 0;
    return TRUE;
}
static void YieldProcessor(void) {
    if (++Yields > 100000) { printf("startup: mode %lu: the wait for the started processors never ended\n", Mode); exit(1); }
    if (Yields == 3 && Mode == 0) KeActiveProcessors |= Started;
}
/* A second of interrupt time goes by at each reading */
__attribute__((unused)) static ULONGLONG KeQueryInterruptTime(void) { Clock += 10000000; return Clock; }
__attribute__((noreturn, unused)) static void KeBugCheckEx(ULONG Code, ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d) {
    StopCode = Code; StopParameter[0] = a; StopParameter[1] = b; StopParameter[2] = c; StopParameter[3] = d;
    longjmp(Stopped, 1);
}
static void KeStartAllProcessors(void) { @MPROC@ }
static void Run(ULONG NewMode) {
    Mode = NewMode; Yields = 0; Clock = 0; Started = 0; StopCode = 0;
    KeActiveProcessors = 1;
    memset(&Loader, 0, sizeof(Loader));
    memset(KiProcessorBlock, 0, sizeof(KiProcessorBlock));
    if (setjmp(Stopped) == 0)
        KeStartAllProcessors();
}
int main(void) {
    Run(0);
    CHECK(StopCode == 0 && Started == 6 && KeActiveProcessors == 7);
    /* A processor that does not report in within 5 s is named in a CLOCK_WATCHDOG_TIMEOUT */
    Run(1);
    CHECK(StopCode == CLOCK_WATCHDOG_TIMEOUT && Started == 2);
    CHECK(StopParameter[0] == 50000000 / KeMaximumIncrement && StopParameter[1] == 0);
    CHECK(StopParameter[2] == Loader.Prcb && StopParameter[2] != 0 && StopParameter[3] == 1);
    Run(2);
    CHECK(StopCode == CLOCK_WATCHDOG_TIMEOUT && Started == 6);
    CHECK(StopParameter[0] == 50000000 / KeMaximumIncrement && StopParameter[1] == 0);
    CHECK(StopParameter[2] == (ULONG_PTR)KiProcessorBlock[1] && StopParameter[2] != 0 && StopParameter[3] == 1);
    return 0;
}
'''.replace('@MPROC@', body(mproc_source, 'KeStartAllProcessors'))
startup = startup.replace('@DEFINES@', '\n'.join(re.findall(r'^#define AP_START_WAIT_TIME .*$', mproc_source, re.M)))

with tempfile.TemporaryDirectory(prefix='ke-ap-start-') as tmp:
    path = pathlib.Path(tmp)
    for name, text in [('publish', fixture), ('startup', startup)]:
        (path / (name + '.c')).write_text(text)
        subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(path / (name + '.c')),
                        '-o', str(path / name)], check=True)
        subprocess.run([str(path / name)], check=True, timeout=10)
