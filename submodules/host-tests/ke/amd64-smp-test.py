# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     AMD64 SMP handshake regression tests
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


def body(path, name):
    source = (root / path).read_text()
    start = source.index(name + '(')
    start = source.index('{', start)
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start + 1:end - 1]


apic = r'''
#include <assert.h>
#include <stdint.h>
typedef unsigned char UCHAR, BOOLEAN;
typedef unsigned long ULONG;
typedef int APIC_MT, APIC_TGM, APIC_DSH;
typedef union {
    uint64_t LongLong;
    struct { uint32_t Long0, Long1; };
    struct {
        uint64_t Vector:8, MessageType:3, DestinationMode:1, DeliveryStatus:1,
                 Reserved:1, Level:1, TriggerMode:1, RemoteReadStatus:2,
                 DestinationShortHand:2, Other:36, Destination:8;
    };
} APIC_INTERRUPT_COMMAND_REGISTER;
#define TRUE 1
#define FALSE 0
#define APIC_DM_Physical 0
#define APIC_MT_Fixed 0
#define HV_X64_MSR_ICR 0
#define APIC_ICR0 0
#define APIC_ICR1 1
#define APIC_ICR_POLL_LIMIT 8
#define EFLAGS_INTERRUPT_MASK 0x200
#define DPRINT1(...) ((void)0)
static int HalpApicHvMsrs, Enabled = 1, BeforeReads, AfterReads, BeforeBusy, AfterBusy, Writes, MsrWrites;
static void __writemsr(int n, uint64_t v) { (void)n; (void)v; MsrWrites++; }
static ULONG __readeflags(void) { return Enabled ? EFLAGS_INTERRUPT_MASK : 0; }
static void _disable(void) { Enabled = 0; }
static void _enable(void) { Enabled = 1; }
static ULONG ApicRead(int reg) {
    APIC_INTERRUPT_COMMAND_REGISTER s = {0};
    assert(reg == APIC_ICR0 && !Enabled);
    if (Writes == 0)
        s.DeliveryStatus = ++BeforeReads <= BeforeBusy;
    else
        s.DeliveryStatus = ++AfterReads <= AfterBusy;
    return s.Long0;
}
static void ApicWrite(int reg, ULONG value) {
    (void)value;
    assert(!Enabled && BeforeReads > BeforeBusy);
    assert((Writes == 0 && reg == APIC_ICR1) || (Writes == 1 && reg == APIC_ICR0));
    Writes++;
}
static void Send(UCHAR DestinationProcessor, UCHAR Vector, APIC_MT MessageType,
                    APIC_TGM TriggerMode, APIC_DSH DestinationShortHand, ULONG Level) {
@APIC@
}
int main(void) {
    BeforeBusy = 16;
    Send(1, 0x50, 1, 0, 0, 1);
    assert(Writes == 2 && BeforeReads > BeforeBusy && Enabled);
    BeforeReads = AfterReads = Writes = BeforeBusy = 0;
    AfterBusy = 16;
    Enabled = 0;
    Send(1, 0x50, 1, 0, 0, 1);
    assert(Writes == 2 && AfterReads > AfterBusy && !Enabled);
    HalpApicHvMsrs = 1;
    Writes = 0;
    Send(1, 0x50, APIC_MT_Fixed, 0, 0, 1);
    assert(MsrWrites == 1 && Writes == 0 && !Enabled);
    return 0;
}
'''.replace('@APIC@', body('hal/halx86/apic/apicsmp.c', 'ApicRequestGlobalInterruptLevel'))

sequence = r'''
#include <assert.h>
#include <stdint.h>
typedef unsigned char UCHAR;
typedef unsigned long ULONG;
typedef union { uint64_t QuadPart; struct { uint32_t LowPart; int32_t HighPart; }; } PHYSICAL_ADDRESS;
typedef struct { UCHAR LapicId; } PROCESSOR_IDENTITY;
static PROCESSOR_IDENTITY Identities[2] = {{0}, {0x37}}, *HalpProcessorIdentity = Identities;
enum { APIC_MT_INIT = 5, APIC_MT_Startup = 6, APIC_TGM_Level = 1,
       APIC_TGM_Edge = 0, APIC_DSH_Destination = 0 };
#define ASSERT(x) assert(x)
static unsigned Step;
static void ApicRequestGlobalInterruptLevel(UCHAR id, UCHAR vector, int type, int trigger, int shorthand, ULONG level) {
    assert(id == 0x37 && vector == 0 && type == APIC_MT_INIT && trigger == APIC_TGM_Level && shorthand == 0);
    assert((Step == 0 && level == 1) || (Step == 2 && level == 0)); Step++;
}
static void ApicRequestGlobalInterrupt(UCHAR id, UCHAR vector, int type, int trigger, int shorthand) {
    assert(id == 0x37 && vector == 0x8a && type == APIC_MT_Startup && trigger == APIC_TGM_Edge && shorthand == 0);
    assert(Step == 4 || Step == 6); Step++;
}
static void KeStallExecutionProcessor(ULONG us) {
    assert((Step == 1 && us == 10000) || ((Step == 3 || Step == 5) && us == 200)); Step++;
}
static void Start(ULONG NTProcessorNumber, PHYSICAL_ADDRESS StartupLoc) { @SEQUENCE@ }
int main(void) { PHYSICAL_ADDRESS address = {.QuadPart = 0x8a000}; Start(1, address); assert(Step == 7); return 0; }
'''.replace('@SEQUENCE@', body('hal/halx86/apic/apicsmp.c', 'ApicStartApplicationProcessor'))

freeze = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef unsigned long ULONG;
typedef unsigned long long KAFFINITY;
typedef int64_t LONG64;
typedef uint64_t ULONG64;
typedef unsigned char BOOLEAN;
typedef void *PVOID;
typedef struct { int ContextFrame; } PROCESSOR_STATE;
typedef struct { unsigned Number; KAFFINITY SetMember; volatile long IpiFrozen; PROCESSOR_STATE ProcessorState; } KPRCB, *PKPRCB;
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
#define KI_FREEZE_WAIT_SPINS 8
#define ASSERT(x) assert(x)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define KeMemoryBarrier() __sync_synchronize()
#define DPRINT1(...) ((void)0)
static KPRCB Cpus[2] = {{.Number=0,.SetMember=1},{.Number=1,.SetMember=2}};
static PKPRCB KiProcessorBlock[2] = {&Cpus[0], &Cpus[1]}, KiFreezeOwner;
static PKPRCB KiFreezeActiveSetOwner;
typedef struct { int Valid; } KI_FREEZE_WAIT;
static volatile LONG64 KiFreezeEpoch;
static ULONG KeNumberProcessors = 2;
static int Yields;
static KAFFINITY KeActiveProcessors = 3;
static PKPRCB KeGetCurrentPrcb(void) { return &Cpus[0]; }
static PVOID InterlockedCompareExchangePointer(PVOID volatile *p, PVOID v, PVOID old) {
    return __sync_val_compare_and_swap(p,old,v);
}
static PVOID InterlockedExchangePointer(PVOID volatile *p, PVOID v) { return __sync_lock_test_and_set(p,v); }
static LONG64 InterlockedIncrement64(volatile LONG64 *p) { return __sync_add_and_fetch(p, 1); }
static LONG64 InterlockedCompareExchange64(volatile LONG64 *p, LONG64 v, LONG64 old) { return __sync_val_compare_and_swap(p, old, v); }
static void KiNoteFreezeEpoch(void) { @NOTE@ }
static ULONG64 KiQueryFreezeEpoch(void) { @EPOCH@ }
static BOOLEAN KiIsFreezeActive(void) { @ACTIVE@ }
static BOOLEAN KiFreezeFailureRequiresReturn(PKPRCB Prcb) { (void)Prcb; return FALSE; }
static BOOLEAN KiFreezeWaitExpired(KI_FREEZE_WAIT *Wait) { (void)Wait; return FALSE; }
static BOOLEAN KiFreezeWaitFailed(PKPRCB Prcb, KI_FREEZE_WAIT *Wait) { (void)Prcb; (void)Wait; return FALSE; }
static void KiFreezeIfRequested(void) { assert(0); }
static void KiIpiSend(KAFFINITY targets, int type) {
    assert(targets == 2 && type == IPI_FREEZE);
    Cpus[1].IpiFrozen = IPI_FROZEN_STATE_SAVING;
}
static void YieldProcessor(void) {
    if (++Yields == 16) {
        if (Cpus[1].IpiFrozen == IPI_FROZEN_STATE_SAVING)
            Cpus[1].IpiFrozen = IPI_FROZEN_STATE_FROZEN;
        else if (Cpus[1].IpiFrozen == IPI_FROZEN_STATE_THAW)
            Cpus[1].IpiFrozen = IPI_FROZEN_STATE_RUNNING;
    }
}
static BOOLEAN KiAcquireFreezeActiveSet(PKPRCB Prcb) { @ACQUIRE@ }
static VOID KiReleaseFreezeActiveSet(PKPRCB Prcb, BOOLEAN Acquired) { @RELEASE@ }
static KAFFINITY KiFreezeTargetSet(PKPRCB CurrentPrcb) { @TARGETS@ }
static void KxFreezeExecution(void) { @FREEZE@ }
static void KxThawExecution(void) { @THAW@ }
int main(void) {
    assert(KiQueryFreezeEpoch() == 0 && !KiIsFreezeActive());
    KxFreezeExecution();
    assert(KiQueryFreezeEpoch() == 1 && KiIsFreezeActive());
    assert(Cpus[1].IpiFrozen == IPI_FROZEN_STATE_FROZEN && Yields >= 16);
    Yields = 0;
    KxThawExecution();
    assert(Cpus[1].IpiFrozen == IPI_FROZEN_STATE_RUNNING && Yields >= 16);
    assert(KiFreezeOwner == 0 && !KiIsFreezeActive());
    assert(KiQueryFreezeEpoch() == 1);
    KeNumberProcessors = 1;
    KeActiveProcessors = 1;
    Yields = 0;
    KxFreezeExecution();
    KxThawExecution();
    assert(Yields == 0 && KiFreezeOwner == 0 && KiQueryFreezeEpoch() == 2);
    return 0;
}
'''
for marker, name in [('NOTE', 'KiNoteFreezeEpoch'), ('EPOCH', 'KiQueryFreezeEpoch'), ('ACTIVE', 'KiIsFreezeActive'), ('ACQUIRE', 'KiAcquireFreezeActiveSet'), ('RELEASE', 'KiReleaseFreezeActiveSet'), ('TARGETS', 'KiFreezeTargetSet'), ('FREEZE', 'KxFreezeExecution'), ('THAW', 'KxThawExecution')]:
    freeze = freeze.replace('@' + marker + '@', body('ntos/ke/amd64/freeze.c', name))

startup = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char UINT8, BOOLEAN;
typedef unsigned long ULONG, ULONG_PTR;
typedef unsigned long long ULONG64, ULONGLONG;
typedef void *PVOID;
typedef struct { unsigned short Limit; PVOID Base; } KDESCRIPTOR;
typedef struct { KDESCRIPTOR Gdtr, Idtr; ULONG64 Cr0, Cr3, Cr4; unsigned short Tr; } SPECIAL;
typedef struct { unsigned short SegCs, SegDs, SegEs, SegSs, SegFs, SegGs; ULONG64 Rsp, Rip, Rcx, EFlags; } CONTEXT;
typedef struct { SPECIAL SpecialRegisters; CONTEXT ContextFrame; } KPROCESSOR_STATE, *PKPROCESSOR_STATE;
typedef struct { KPROCESSOR_STATE ProcessorState; ULONG64 SetMember; } KPRCB, *PKPRCB;
typedef struct { KPRCB Prcb; } KIPCR, *PKIPCR;
typedef struct { char data[16]; } KIDTENTRY64, KGDTENTRY64, KTSS64, ETHREAD;
typedef KGDTENTRY64 *PKGDTENTRY64; typedef KIDTENTRY64 *PKIDTENTRY64;
typedef KTSS64 *PKTSS64; typedef void *PKTHREAD;
typedef struct { ULONG_PTR Prcb, Thread, KernelStack; } LOADER;
#define PAGE_SIZE 4096
#define DOUBLE_FAULT_STACK_SIZE 4096
#define DECLSPEC_ALIGN(n) __attribute__((aligned(n)))
#define AP_START_WAIT_TIME (5 * 10000000ULL)
#define EFLAGS_INTERRUPT_MASK 0x200
#define CLOCK_WATCHDOG_TIMEOUT 0x101
#define NonPagedPool 0
#define TAG_KERNEL 0
#define KGDT64_R0_CODE 1
#define KGDT64_R0_DATA 2
#define KGDT64_SYS_TSS 3
#define FALSE 0
#define TRUE 1
#define ASSERT(x) assert(x)
#define DPRINT1(...) ((void)0)
#define ALIGN_UP_BY(v,a) (((v)+((a)-1)) & ~((ULONG_PTR)((a)-1)))
#define ALIGN_DOWN_BY(v,a) ((v) & ~((ULONG64)((a)-1)))
#define ALIGN_DOWN_POINTER_BY(v,a) ((PVOID)ALIGN_DOWN_BY((ULONG_PTR)(v),(a)))
#define RtlCopyMemory(d,s,n) memcpy(d,s,n)
#define RtlZeroMemory(d,n) memset(d,0,n)
#define min(a,b) ((a)<(b)?(a):(b))
static ULONG KeMaximumProcessors = 2, KeNumprocSpecified, KeBootprocSpecified;
static LOADER Loader, *KeLoaderBlock = &Loader;
static KPRCB *KiProcessorBlock[2];
static ULONG64 KeActiveProcessors;
static int Mode, BugcheckCode;
static ULONGLONG Clock;
static jmp_buf FailStop;
static char Table[16];
static void *ExAllocatePoolZero(int pool, size_t size, int tag) { (void)pool; (void)tag; return calloc(1,size); }
static void ExFreePoolWithTag(void *p, int tag) { (void)tag; free(p); }
static void *MmCreateKernelStack(int a, int b) { (void)a; (void)b; return malloc(8192); }
static void MmDeleteKernelStack(void *p, int a) { (void)a; free(p); }
static void descriptor(unsigned short *p) { KDESCRIPTOR *d = (KDESCRIPTOR *)p; d->Limit = 0; d->Base = Table; }
#define __sgdt(p) descriptor(p)
#define __sidt(p) descriptor(p)
static ULONG64 __readcr0(void) { return 0; }
static ULONG64 __readcr3(void) { return 0; }
static ULONG64 __readcr4(void) { return 0; }
static ULONG64 __readeflags(void) { return 0; }
static void KiSystemStartup(void) {}
static void KiInitializeProcessorBootStructures(ULONG n, PKIPCR p, PKGDTENTRY64 g,
    PKIDTENTRY64 i, PKTSS64 t, PKTHREAD th, PVOID ks, PVOID ds, PVOID df, PVOID nm) {
    (void)g; (void)i; (void)t; (void)th; (void)ks; (void)ds; (void)df; (void)nm;
    p->Prcb.SetMember = 1ULL << n;
}
typedef struct _APINFO {
    DECLSPEC_ALIGN(PAGE_SIZE) KIDTENTRY64 Idt[256];
    DECLSPEC_ALIGN(PAGE_SIZE) KGDTENTRY64 Gdt[128];
    DECLSPEC_ALIGN(16) UINT8 DoubleFaultStack[DOUBLE_FAULT_STACK_SIZE];
    DECLSPEC_ALIGN(16) UINT8 NmiStack[DOUBLE_FAULT_STACK_SIZE];
    KIPCR Pcr; ETHREAD Thread; KTSS64 Tss;
} APINFO, *PAPINFO;
static BOOLEAN HalStartNextProcessor(LOADER *l, PKPROCESSOR_STATE state) {
    (void)state;
    if (Mode != 1) {
        if (Mode != 3) KiProcessorBlock[1] = (KPRCB *)l->Prcb;
        l->Prcb = 0;
        if (Mode == 0) KeActiveProcessors |= 2;
    }
    return TRUE;
}
static ULONGLONG KeQueryInterruptTime(void) { Clock += 10000000; return Clock; }
static void KeMemoryBarrier(void) {}
static void YieldProcessor(void) {}
__attribute__((noreturn,unused)) static void KeBugCheckEx(ULONG code, ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d) {
    (void)a; (void)b; (void)c; (void)d; BugcheckCode = (int)code; longjmp(FailStop, 1);
}
static void KeStartAllProcessors(void) { @STARTUP@ }
int main(void) {
    Mode = 0; KeActiveProcessors = 1;
    KeStartAllProcessors();
    assert(KeActiveProcessors == 3);
    for (Mode = 1; Mode <= 3; Mode++) {
        memset(&Loader, 0, sizeof(Loader));
        memset(KiProcessorBlock, 0, sizeof(KiProcessorBlock));
        KeActiveProcessors = 1; Clock = 0; BugcheckCode = 0;
        if (setjmp(FailStop) == 0) { KeStartAllProcessors(); assert(!"startup returned after timeout"); }
        assert(BugcheckCode == CLOCK_WATCHDOG_TIMEOUT);
    }
    return 0;
}
'''.replace('@STARTUP@', body('ntos/ke/amd64/mproc.c', 'KeStartAllProcessors'))

with tempfile.TemporaryDirectory(prefix='ke-amd64-smp-') as tmp:
    path = pathlib.Path(tmp)
    for name, source in [('apic', apic), ('sequence', sequence), ('freeze', freeze), ('startup', startup)]:
        (path / (name + '.c')).write_text(source)
        subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        str(path / (name + '.c')), '-o', str(path / name)], check=True)
        subprocess.run([str(path / name)], check=True, timeout=10)
