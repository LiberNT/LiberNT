# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A stale mouse reset timeout cannot undo a completed reset
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'drivers/input/i8042prt/mouse.c').read_text(encoding='latin-1')
match = re.search(r'(?ms)^static VOID NTAPI\ni8042DpcRoutineMouseTimeout\(.*?^\}\n', source)
if not match:
    raise RuntimeError('mouse timeout DPC not found')
fixture = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define VOID void
#define NTAPI
#define IN
#define DISPATCH_LEVEL 2
#define MOUSE_PRESENT 1
#define I8042PRT 0
#define ASSERT assert
#define WARN_(...)
#define UNREFERENCED_PARAMETER(x) (void)(x)
#undef __analysis_assume
#define __analysis_assume(x) ((void)0)
typedef int32_t LONG, *PLONG;
typedef uint32_t ULONG;
typedef unsigned char KIRQL, BOOLEAN;
typedef void *PVOID, *PKINTERRUPT;
typedef struct { int unused; } KDPC, *PKDPC;
typedef enum { MouseIdle, MouseResetting } MOUSE_STATE;
enum { NoChange, TimeoutStart, TimeoutCancel };
typedef struct { void *HighestDIRQLInterrupt; LONG Flags; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
typedef struct { struct { PORT_DEVICE_EXTENSION *PortDeviceExtension; int MouseTimerLock, MouseTimerStopping; } Common; int MouseResetState; MOUSE_STATE MouseState; ULONG MouseTimeoutState; int TimerMouseTimeout; } I8042_MOUSE_EXTENSION, *PI8042_MOUSE_EXTENSION;
static ULONG ReadULongAcquire(volatile ULONG *p) { return *p; }
static int KeReadStateTimer(int *p) { return *p; }
static void KeAcquireSpinLock(int *p, KIRQL *old) { (void)p; *old=2; }
static void KeReleaseSpinLock(int *p, KIRQL old) { (void)p; assert(old==2); }
static int Locked;
static KIRQL KeGetCurrentIrql(void) { return 2; }
static KIRQL KeAcquireInterruptSpinLock(void *p) { assert(p && !Locked); Locked=1; return 2; }
static void KeReleaseInterruptSpinLock(void *p, KIRQL old) { assert(p && Locked && old==2); Locked=0; }
typedef struct { void *Interrupt; KIRQL TableIrql, InterruptIrql; } I8042_INTERRUPT_LOCK;
static BOOLEAN i8042AcquireInterrupt(PORT_DEVICE_EXTENSION *port, I8042_INTERRUPT_LOCK *lock)
{
    lock->Interrupt = port->HighestDIRQLInterrupt;
    if (!lock->Interrupt)
        return 0;
    lock->TableIrql = 2;
    lock->InterruptIrql = KeAcquireInterruptSpinLock(lock->Interrupt);
    return 1;
}
static void i8042ReleaseInterrupt(PORT_DEVICE_EXTENSION *port, I8042_INTERRUPT_LOCK *lock)
{
    (void)port;
    KeReleaseInterruptSpinLock(lock->Interrupt, lock->InterruptIrql);
}
static LONG InterlockedAnd(volatile LONG *p, LONG value) { LONG old=*p; *p &= value; return old; }
""" + match.group(0) + r"""
int main(void)
{
    PORT_DEVICE_EXTENSION port = {(void *)1, MOUSE_PRESENT};
    I8042_MOUSE_EXTENSION dev = {{&port, 0, 0}, 0, MouseIdle, NoChange, 1};
    i8042DpcRoutineMouseTimeout(NULL, &dev, NULL, NULL);
    if (!(port.Flags & MOUSE_PRESENT)) { fprintf(stderr, "stale timeout marked an initialized mouse absent\n"); return 1; }
    dev.MouseState = MouseResetting;
    i8042DpcRoutineMouseTimeout(NULL, &dev, NULL, NULL);
    assert(!(port.Flags & MOUSE_PRESENT));
    puts("mouse reset timeout state passed");
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='i8042-mouse-timeout-') as directory:
    path = pathlib.Path(directory)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
