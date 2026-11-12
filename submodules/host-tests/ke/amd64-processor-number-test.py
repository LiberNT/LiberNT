# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     AMD64 processor numbering: the PRCB is published before it is counted
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'ntos/ke/amd64/kiinit.c').read_text()


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
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned char BOOLEAN, KIRQL;
typedef signed char CCHAR;
typedef unsigned long ULONG;
typedef uint64_t ULONG64, ULONG_PTR, KAFFINITY, KSPIN_LOCK;
typedef int64_t LONG64, *PLONG64;
typedef void *PVOID;
typedef struct { ULONG Number; KAFFINITY SetMember; ULONG FeatureBits, FeatureBitsHigh; } KPRCB, *PKPRCB;
typedef struct { void *GdtBase, *IdtBase, *TssBase; KPRCB Prcb; } KIPCR, *PKIPCR;
typedef struct { struct { void *Process; } ApcState; } KTHREAD, *PKTHREAD;
typedef struct { ULONG_PTR KernelStack, Prcb, Thread, Process; union { struct { void *CommonDataArea; } I386; } u; } LOADER_PARAMETER_BLOCK, *PLOADER_PARAMETER_BLOCK;
typedef struct { ULONG Eax, Ebx, Ecx, Edx; } CPU_INFO;
#define VOID void
#define TRUE 1
#define FALSE 0
#define MAXIMUM_PROCESSORS 64
#define HIGH_LEVEL 15
#define X86_FEATURE_MCE 0x80
#define CR4_MCE 0x40
#define DBG_STATUS_CONTROL_C 1
#define DPRINT1(...) ((void)0)
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
#define ALIGN_DOWN_BY(v, a) ((ULONG64)(v) & ~((ULONG64)(a) - 1))
#define KeMemoryBarrier() __sync_synchronize()
#define CHECK(x) do { if (!(x)) { printf("%s:%d: %s failed\n", Scenario, __LINE__, #x); exit(1); } } while (0)
static const char *Scenario;
static KIPCR P0, P1;
static KTHREAD Thread0, Thread1;
static LOADER_PARAMETER_BLOCK Loader;
static PLOADER_PARAMETER_BLOCK KeLoaderBlock;
static PKPRCB KiProcessorBlock[MAXIMUM_PROCESSORS];
static CCHAR KeNumberProcessors;
static KAFFINITY KeActiveProcessors;
static KSPIN_LOCK KiFreezeExecutionLock;
static ULONG64 BootCycles, KeFeatureBits, Cr4;
static ULONG KeXStateLength = 64;
static void *FrLdrDbgPrint;
static jmp_buf Started;
/* Whatever runs while the processor is being set up may walk [0, KeNumberProcessors) */
static void CheckPublished(void) {
    CCHAR i;
    for (i = 0; i < KeNumberProcessors; i++)
        CHECK(KiProcessorBlock[(int)i] != NULL);
}
static ULONG64 __rdtsc(void) { return 1; }
static ULONG64 __readcr4(void) { return Cr4; }
static void __writecr4(ULONG64 Value) { Cr4 = Value; }
static void KiInitializeP0BootStructures(PLOADER_PARAMETER_BLOCK LoaderBlock) {
    CheckPublished();
    LoaderBlock->Prcb = (ULONG_PTR)&P0.Prcb;
    LoaderBlock->Thread = (ULONG_PTR)&Thread0;
}
static void KiInitializeCpu(PKIPCR Pcr) { (void)Pcr; CheckPublished(); }
static void KiInitializeMxCsrMask(void) {}
static void KiInitModuleList(PLOADER_PARAMETER_BLOCK LoaderBlock) { (void)LoaderBlock; }
static void KeInitExceptions(void) {}
static void MiInitializeKernelVaLayout(PLOADER_PARAMETER_BLOCK LoaderBlock) { (void)LoaderBlock; }
static void KiCpuId(CPU_INFO *Info, ULONG Leaf) { (void)Leaf; Info->Edx = 0; }
static BOOLEAN InterlockedBitTestAndSet64(PLONG64 Base, LONG64 Bit) { return (__sync_fetch_and_or(Base, 1LL << Bit) >> Bit) & 1; }
static LONG64 InterlockedAnd64(PLONG64 Base, LONG64 Mask) { return __sync_fetch_and_and(Base, Mask); }
static void HalInitializeProcessor(ULONG Cpu, PLOADER_PARAMETER_BLOCK LoaderBlock) { (void)Cpu; (void)LoaderBlock; CheckPublished(); }
static BOOLEAN KdInitSystem(ULONG Phase, PLOADER_PARAMETER_BLOCK LoaderBlock) { (void)Phase; (void)LoaderBlock; return TRUE; }
static BOOLEAN KdPollBreakIn(void) { return FALSE; }
static void DbgBreakPointWithStatus(ULONG Status) { (void)Status; }
static KIRQL KfRaiseIrql(KIRQL Irql) { return Irql; }
static void KiInitializeKernelMachineDependent(PKPRCB Prcb, PLOADER_PARAMETER_BLOCK LoaderBlock) { (void)Prcb; (void)LoaderBlock; }
static void KiInitializeXStateConfiguration(ULONG Cpu) { (void)Cpu; }
__attribute__((noreturn)) static void KiSwitchToBootStack(ULONG64 Stack) { (void)Stack; CheckPublished(); longjmp(Started, 1); }
static void KiSystemStartup(PLOADER_PARAMETER_BLOCK LoaderBlock) { @STARTUP@ }
int main(void) {
    Scenario = "boot processor";
    Loader.KernelStack = 0x10000;
    if (setjmp(Started) == 0)
        KiSystemStartup(&Loader);
    CHECK(KeNumberProcessors == 1 && KiProcessorBlock[0] == &P0.Prcb && KeLoaderBlock == &Loader);

    Scenario = "application processor";
    P1.Prcb.Number = 1;
    P1.Prcb.SetMember = 2;
    Loader.Prcb = (ULONG_PTR)&P1.Prcb;
    Loader.Thread = (ULONG_PTR)&Thread1;
    if (setjmp(Started) == 0)
        KiSystemStartup(&Loader);
    CHECK(KeNumberProcessors == 2 && KiProcessorBlock[1] == &P1.Prcb && KiProcessorBlock[0] == &P0.Prcb);
    return 0;
}
'''.replace('@STARTUP@', body('KiSystemStartup'))

with tempfile.TemporaryDirectory(prefix='ke-processor-number-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-but-set-variable',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
