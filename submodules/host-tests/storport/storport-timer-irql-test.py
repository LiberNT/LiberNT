#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Storport miniport timer DPC runs under the timer lock at the adapter interrupt IRQL
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'drivers/storage/port/storport/storport.c').read_text()

functions = ''
for name in ('PortAcquireTimerLock', 'PortReleaseTimerLock', 'PortMiniportTimerDpc'):
    match = re.search(r'(?ms)^static\n(?:\w+\n)+' + name + r'\(.*?^\}\n', source)
    if not match:
        raise RuntimeError(name + ' not found in storport.c')
    functions += match.group(0) + '\n'

fixture = r'''
#include <stdio.h>
typedef unsigned char UCHAR, BOOLEAN, KIRQL, *PKIRQL;
typedef unsigned int ULONG;
typedef void VOID, *PVOID;
typedef unsigned long long KSPIN_LOCK, *PKSPIN_LOCK;
typedef struct { int Unused; } KDPC, *PKDPC;
typedef BOOLEAN (*PHW_TIMER)(PVOID);
#define TRUE 1
#define FALSE 0
#define NULL ((void *)0)
#define NTAPI
#define _In_
#define _In_opt_
#define DISPATCH_LEVEL 2
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define max(a, b) ((a) > (b) ? (a) : (b))
static KIRQL Irql;
static int Failures, Owned, Calls;
static KIRQL CalledAt;
#define ASSERT(x) do { if (!(x)) { printf("assertion failed: %s\n", #x); Failures++; } } while (0)
static KIRQL KeGetCurrentIrql(void) { return Irql; }
static void KeRaiseIrql(KIRQL New, PKIRQL Old) { ASSERT(New >= Irql); *Old = Irql; Irql = New; }
static void KeLowerIrql(KIRQL New) { ASSERT(New <= Irql); Irql = New; }
static void KeAcquireSpinLockAtDpcLevel(PKSPIN_LOCK Lock) { ASSERT(Irql >= DISPATCH_LEVEL); ASSERT(!*Lock); *Lock = 1; Owned++; }
static void KeReleaseSpinLockFromDpcLevel(PKSPIN_LOCK Lock) { ASSERT(Irql >= DISPATCH_LEVEL); ASSERT(*Lock); *Lock = 0; Owned--; }
typedef struct { PVOID HwDeviceExtension; } MINIPORT_DEVICE_EXTENSION, *PMINIPORT_DEVICE_EXTENSION;
typedef struct { PMINIPORT_DEVICE_EXTENSION MiniportExtension; } MINIPORT;
typedef struct
{
    ULONG InterruptIrql;
    KSPIN_LOCK MiniportTimerLock;
    BOOLEAN MiniportTimerArmed;
    PHW_TIMER MiniportTimerRoutine;
    MINIPORT Miniport;
} FDO_DEVICE_EXTENSION, *PFDO_DEVICE_EXTENSION;
''' + functions + r'''
static BOOLEAN Timer(PVOID Context) { (void)Context; Calls++; CalledAt = Irql; return TRUE; }

static void Run(const char *Case, ULONG InterruptIrql)
{
    static MINIPORT_DEVICE_EXTENSION Miniport;
    FDO_DEVICE_EXTENSION Extension = { 0 };
    KDPC Dpc;
    int Before = Failures;

    Extension.InterruptIrql = InterruptIrql;
    Extension.MiniportTimerArmed = TRUE;
    Extension.MiniportTimerRoutine = Timer;
    Extension.Miniport.MiniportExtension = &Miniport;
    Irql = DISPATCH_LEVEL;
    Calls = 0;
    PortMiniportTimerDpc(&Dpc, &Extension, NULL, NULL);
    PortMiniportTimerDpc(&Dpc, &Extension, NULL, NULL);
    ASSERT(Irql == DISPATCH_LEVEL);
    ASSERT(Owned == 0);
    ASSERT(Calls == 1);
    ASSERT(CalledAt == DISPATCH_LEVEL);
    ASSERT(!Extension.MiniportTimerArmed);
    printf("%s: %s\n", Case, Failures == Before ? "ok" : "FAILED");
}

int main(void)
{
    Run("no interrupt", 0);
    Run("message interrupt at DISPATCH_LEVEL", DISPATCH_LEVEL);
    Run("line interrupt at DIRQL 7", 7);
    Run("line interrupt at DIRQL 12", 12);
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as temp:
    c_file = pathlib.Path(temp) / 'storport-timer-irql-test.c'
    exe = pathlib.Path(temp) / 'storport-timer-irql-test'
    c_file.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wno-unused-function', str(c_file), '-o', str(exe)], check=True)
    sys.exit(subprocess.run([str(exe)]).returncode)
