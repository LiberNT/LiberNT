# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     PS/2 mouse reset timer generation and delivery races
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
functions = []
for name in ('i8042StopMouseTimer', 'i8042MouDpcWork', 'i8042MouDpcRoutine', 'i8042DpcRoutineMouseTimeout'):
    match = re.search(r'(?ms)^(?:static )?(?:VOID NTAPI|VOID|BOOLEAN)\n' + name + r'\(.*?^\}\n', source)
    if not match:
        raise RuntimeError(name + ' not found')
    functions.append(match.group(0))
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define VOID void
#define NTAPI
#define IN
#define TRUE 1
#define FALSE 0
#define ASSERT assert
#define UNREFERENCED_PARAMETER(x) (void)(x)
#undef __analysis_assume
#define __analysis_assume(x) ((void)0)
#define TRACE_(...)
#define INFO_(...)
#define WARN_(...) ((void)0)
#define ERR_(...)
#define I8042PRT 0
#define MOUSE_PRESENT 1
#define DISPATCH_LEVEL 2
#define RtlMoveMemory memmove
#define MOUSE_MOVE_RELATIVE 0
#define MOUSE_LEFT_BUTTON_DOWN 1
#define MOUSE_RIGHT_BUTTON_DOWN 4
#define MOUSE_MIDDLE_BUTTON_DOWN 16
#define MOUSE_BUTTON_4_DOWN 64
#define MOUSE_BUTTON_5_DOWN 256
#define MOUSE_WHEEL 0x400
#define WHEEL_DELTA 120
typedef void *PVOID, *PKINTERRUPT;
typedef uint8_t UCHAR, BOOLEAN, KIRQL;
typedef int8_t CHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG, *PULONG;
typedef int32_t LONG, *PLONG;
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
typedef struct { int unused; } KDPC, *PKDPC;
typedef struct { int queued, signaled; } KTIMER;
typedef struct { USHORT UnitId, Flags; union { ULONG Buttons; struct { USHORT ButtonFlags, ButtonData; }; }; ULONG RawButtons; LONG LastX, LastY; ULONG ExtraInformation; } MOUSE_INPUT_DATA, *PMOUSE_INPUT_DATA;
typedef enum { MouseIdle, MouseResetting, XMovement, YMovement, ZMovement } MOUSE_STATE;
typedef enum { GenericPS2, Intellimouse, IntellimouseExplorer, Ps2pp } MOUSE_TYPE;
typedef enum { NoChange, TimeoutStart, TimeoutCancel } TIMEOUT_STATE;
typedef struct { ULONG MouseDataQueueSize; uint64_t MouseSynchIn100ns; } SETTINGS;
typedef struct { SETTINGS Settings; void *HighestDIRQLInterrupt; int Packet; LONG Flags; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
typedef void (*PSERVICE_CALLBACK_ROUTINE)(void *, MOUSE_INPUT_DATA *, MOUSE_INPUT_DATA *, ULONG *);
typedef struct { void *ClassDeviceObject; PSERVICE_CALLBACK_ROUTINE ClassService; } CONNECT_DATA;
typedef struct { void (*QueueMousePacket)(void *); void *CallContext; } HOOK;
typedef struct { struct { PORT_DEVICE_EXTENSION *PortDeviceExtension; int MouseTimerLock, MouseTimerStopping; } Common; MOUSE_INPUT_DATA *MouseBuffer; ULONG MouseInBuffer; MOUSE_STATE MouseState; MOUSE_TYPE MouseType; USHORT MouseButtonState; HOOK MouseHook; KDPC DpcMouse, DpcMouseTimeout; KTIMER TimerMouseTimeout; volatile ULONG MouseTimeoutState; int MouseTimeoutActive; int MouseComplete; int MouseResetState; CONNECT_DATA MouseData; volatile LONG MouseDpcState; } I8042_MOUSE_EXTENSION, *PI8042_MOUSE_EXTENSION;
static int LockHeld, TimerHeld, InjectCancel, CancelCalls;
static I8042_MOUSE_EXTENSION *InjectedDevice;
static void KeAcquireSpinLock(int *p, KIRQL *old) { (void)p; assert(!TimerHeld); TimerHeld=1; *old=2; }
static void KeReleaseSpinLock(int *p, KIRQL old) { (void)p; assert(TimerHeld && old==2); TimerHeld=0; }
static ULONG ReadULongAcquire(volatile ULONG *p) { return *p; }
static void WriteULongRelease(volatile ULONG *p, ULONG value) { *p=value; }
static LONG InterlockedAnd(volatile LONG *p, LONG value) { LONG old=*p; *p &= value; return old; }
static LONG InterlockedExchange(volatile LONG *p, LONG value) { LONG old=*p; *p=value; return old; }
static LONG InterlockedOr(volatile LONG *p, LONG value) { LONG old=*p; *p |= value; return old; }
static LONG InterlockedCompareExchange(volatile LONG *p, LONG n, LONG e) { LONG old=*p; if (old==e) *p=n; return old; }
static KIRQL KeGetCurrentIrql(void) { return 2; }
static KIRQL KeAcquireInterruptSpinLock(void *p) { assert(p && !LockHeld); LockHeld = 1; return 2; }
static void KeReleaseInterruptSpinLock(void *p, KIRQL i) { assert(p && i == 2 && LockHeld); LockHeld = 0; }
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
static void KeInsertQueueDpc(KDPC *p, void *a, void *b) { (void)p; (void)a; (void)b; }
static int KeCancelTimer(KTIMER *p) { int was=p->queued; assert(TimerHeld && !LockHeld); CancelCalls++; p->queued=0; return was; }
static void KeSetTimer(KTIMER *p, LARGE_INTEGER t, KDPC *d) { (void)t; (void)d; p->queued=1; p->signaled=0; if (InjectCancel) { KIRQL old; InjectCancel=0; old=KeAcquireInterruptSpinLock(InjectedDevice->Common.PortDeviceExtension->HighestDIRQLInterrupt); InjectedDevice->MouseState=MouseIdle; InjectedDevice->MouseTimeoutState=TimeoutCancel; KeReleaseInterruptSpinLock(InjectedDevice->Common.PortDeviceExtension->HighestDIRQLInterrupt, old); } }
static int KeReadStateTimer(KTIMER *p) { assert(TimerHeld && !LockHeld); return p->signaled; }
''' + '\n'.join(functions) + r'''
static int Delivered;
static void callback(void *p, MOUSE_INPUT_DATA *start, MOUSE_INPUT_DATA *end, ULONG *count)
{
    (void)p; assert(!LockHeld && end-start==1); Delivered++; *count=1;
}
int main(void)
{
    PORT_DEVICE_EXTENSION port = {0};
    MOUSE_INPUT_DATA data[3] = {{0}};
    I8042_MOUSE_EXTENSION dev = {0};
    port.Settings.MouseDataQueueSize=3;
    port.HighestDIRQLInterrupt=(void *)1;
    port.Flags=MOUSE_PRESENT;
    dev.Common.PortDeviceExtension=&port;
    dev.MouseBuffer=data;
    dev.MouseState=MouseResetting;
    dev.MouseTimeoutState=TimeoutStart;
    dev.MouseTimeoutActive=1;
    dev.TimerMouseTimeout.signaled=1;
    i8042DpcRoutineMouseTimeout(NULL, &dev, NULL, NULL);
    assert(port.Flags & MOUSE_PRESENT);
    i8042MouDpcRoutine(NULL, &dev, NULL, NULL);
    if (!dev.TimerMouseTimeout.queued || dev.TimerMouseTimeout.signaled) {
        fprintf(stderr, "expired timer was not rearmed for the new reset\n"); return 1;
    }
    i8042DpcRoutineMouseTimeout(NULL, &dev, NULL, NULL);
    assert(port.Flags & MOUSE_PRESENT);
    dev.TimerMouseTimeout.queued=0;
    dev.TimerMouseTimeout.signaled=1;
    i8042DpcRoutineMouseTimeout(NULL, &dev, NULL, NULL);
    assert(!(port.Flags & MOUSE_PRESENT));
    port.Flags=MOUSE_PRESENT;
    dev.MouseState=MouseResetting;
    dev.MouseTimeoutState=TimeoutStart;
    dev.MouseInBuffer=1;
    dev.MouseComplete=TRUE;
    dev.MouseData.ClassService=callback;
    i8042MouDpcRoutine(NULL, &dev, NULL, NULL);
    if (Delivered != 1 || dev.MouseInBuffer != 0) {
        fprintf(stderr, "starting reset timer stranded an already complete mouse packet\n"); return 1;
    }
    Delivered=0;
    dev.MouseState=MouseResetting;
    dev.MouseTimeoutState=TimeoutStart;
    dev.TimerMouseTimeout.signaled=1;
    dev.MouseInBuffer=1;
    dev.MouseComplete=TRUE;
    InjectedDevice=&dev;
    InjectCancel=1;
    i8042MouDpcRoutine(NULL, &dev, NULL, NULL);
    assert(dev.MouseTimeoutState==TimeoutCancel);
    i8042DpcRoutineMouseTimeout(NULL, &dev, NULL, NULL);
    assert(port.Flags & MOUSE_PRESENT);
    i8042MouDpcRoutine(NULL, &dev, NULL, NULL);
    assert(dev.MouseTimeoutState==NoChange && !dev.TimerMouseTimeout.queued);
    assert(Delivered==1 && dev.MouseInBuffer==0);
    dev.TimerMouseTimeout.queued=1;
    i8042StopMouseTimer(&dev, TRUE);
    assert(dev.Common.MouseTimerStopping && !dev.TimerMouseTimeout.queued);
    dev.TimerMouseTimeout.signaled=1;
    i8042DpcRoutineMouseTimeout(NULL, &dev, NULL, NULL);
    assert(port.Flags & MOUSE_PRESENT);
    dev.MouseState=MouseResetting;
    dev.MouseTimeoutState=TimeoutStart;
    i8042MouDpcRoutine(NULL, &dev, NULL, NULL);
    assert(!dev.TimerMouseTimeout.queued);
    { int calls=CancelCalls; i8042StopMouseTimer(&dev, FALSE); assert(CancelCalls==calls); }
    puts("mouse reset timer rearm and cancellation races passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='i8042-mouse-partial-') as directory:
    path = pathlib.Path(directory)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-implicit-fallthrough',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
