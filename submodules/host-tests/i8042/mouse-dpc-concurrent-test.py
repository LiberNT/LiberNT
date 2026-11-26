# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     PS/2 mouse DPC ownership during overlapping execution
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import ctypes
import pathlib
import re
import subprocess
import sys
import tempfile
import threading
import time

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'drivers/input/i8042prt/mouse.c').read_text(encoding='latin-1')
functions = []
for name in ('i8042MouDpcWork', 'i8042MouDpcRoutine'):
    match = re.search(r'(?ms)^static VOID NTAPI\n' + name + r'\(.*?^\}\n', source)
    if not match:
        raise RuntimeError(name + ' not found')
    functions.append(match.group(0))
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#undef __analysis_assume
#define __analysis_assume(x) ((void)0)
#define VOID void
#define NTAPI
#define IN
#define TRUE 1
#define FALSE 0
#define ASSERT assert
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define TRACE_(...)
#define INFO_(...)
#define WARN_(...)
#define RtlMoveMemory memmove
#define DISPATCH_LEVEL 2
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uint8_t BOOLEAN, KIRQL;
typedef void *PVOID;
typedef struct { int unused; } KDPC, *PKDPC;
typedef struct { LONG LastX; } MOUSE_INPUT_DATA, *PMOUSE_INPUT_DATA;
typedef struct { struct { ULONG MouseDataQueueSize; } Settings; void *HighestDIRQLInterrupt; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
typedef void (*PSERVICE_CALLBACK_ROUTINE)(void *, MOUSE_INPUT_DATA *, MOUSE_INPUT_DATA *, ULONG *);
typedef struct { void *ClassDeviceObject; PSERVICE_CALLBACK_ROUTINE ClassService; } CONNECT_DATA;
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
typedef struct { struct { PORT_DEVICE_EXTENSION *PortDeviceExtension; int MouseTimerLock, MouseTimerStopping; } Common; BOOLEAN MouseComplete; ULONG MouseInBuffer; MOUSE_INPUT_DATA *MouseBuffer; CONNECT_DATA MouseData; volatile LONG MouseDpcState; volatile ULONG MouseTimeoutState; int TimerMouseTimeout; KDPC DpcMouseTimeout; } I8042_MOUSE_EXTENSION, *PI8042_MOUSE_EXTENSION;
enum { NoChange, TimeoutStart, TimeoutCancel };
static LONG InterlockedOr(volatile LONG *p, LONG v) { return __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST); }
static LONG InterlockedAnd(volatile LONG *p, LONG v) { return __atomic_fetch_and(p, v, __ATOMIC_SEQ_CST); }
static LONG InterlockedCompareExchange(volatile LONG *p, LONG n, LONG e) { __atomic_compare_exchange_n(p, &e, n, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); return e; }
static volatile LONG Lock;
static ULONG ReadULongAcquire(volatile ULONG *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static LONG InterlockedExchange(volatile LONG *p, LONG value) { return __atomic_exchange_n(p, value, __ATOMIC_SEQ_CST); }
static void KeAcquireSpinLock(int *p, KIRQL *old) { (void)p; *old=2; }
static void KeReleaseSpinLock(int *p, KIRQL old) { (void)p; assert(old==2); }
static int KeSetTimer(int *p, LARGE_INTEGER due, KDPC *d) { (void)p; (void)due; (void)d; return 0; }
static int KeCancelTimer(int *p) { (void)p; return 0; }
static KIRQL KeAcquireInterruptSpinLock(void *p) { assert(p); while (__atomic_exchange_n(&Lock, 1, __ATOMIC_ACQUIRE)) {} return 2; }
static void KeReleaseInterruptSpinLock(void *p, KIRQL i) { assert(p && i==2); __atomic_store_n(&Lock, 0, __ATOMIC_RELEASE); }
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
static KIRQL KeGetCurrentIrql(void) { return 2; }
''' + '\n'.join(functions) + r'''
static PORT_DEVICE_EXTENSION Port = {{4}, (void *)1};
static MOUSE_INPUT_DATA Data[4];
static I8042_MOUSE_EXTENSION Device;
static volatile LONG Entered, Gate, Active, Overlap, DeliveredCount;
static ULONG Delivered[8];
static void callback(void *object, MOUSE_INPUT_DATA *start, MOUSE_INPUT_DATA *end, ULONG *count)
{
    LONG active = __atomic_fetch_add(&Active, 1, __ATOMIC_SEQ_CST);
    LONG first = __atomic_exchange_n(&Entered, 1, __ATOMIC_SEQ_CST) == 0;
    (void)object;
    if (active)
        __atomic_fetch_add(&Overlap, 1, __ATOMIC_SEQ_CST);
    while (start != end)
    {
        LONG slot = __atomic_fetch_add(&DeliveredCount, 1, __ATOMIC_SEQ_CST);
        assert(slot < 8);
        Delivered[slot] = start->LastX;
        ++start;
    }
    if (first)
        while (!__atomic_load_n(&Gate, __ATOMIC_SEQ_CST)) {}
    *count = (ULONG)(end - Device.MouseBuffer);
    __atomic_fetch_sub(&Active, 1, __ATOMIC_SEQ_CST);
}
EXPORT void setup(void)
{
    memset(&Device, 0, sizeof(Device));
    memset(Data, 0, sizeof(Data));
    Device.Common.PortDeviceExtension = &Port;
    Device.MouseBuffer = Data;
    Device.MouseData.ClassService = callback;
    Device.MouseData.ClassDeviceObject = &Device;
    Device.MouseComplete = TRUE;
    Device.MouseInBuffer = 1;
    Data[0].LastX = 1;
}
EXPORT void run_dpc(void) { i8042MouDpcRoutine(0, &Device, 0, 0); }
EXPORT int entered(void) { return __atomic_load_n(&Entered, __ATOMIC_SEQ_CST); }
EXPORT void release_callback(void) { __atomic_store_n(&Gate, 1, __ATOMIC_SEQ_CST); }
EXPORT void add_packet(void)
{
    KIRQL irql = KeAcquireInterruptSpinLock(Port.HighestDIRQLInterrupt);
    assert(Device.MouseInBuffer < 3);
    Data[Device.MouseInBuffer++].LastX = 2;
    Device.MouseComplete = TRUE;
    KeReleaseInterruptSpinLock(Port.HighestDIRQLInterrupt, irql);
}
EXPORT int overlap(void) { return __atomic_load_n(&Overlap, __ATOMIC_SEQ_CST); }
EXPORT int delivered_count(void) { return __atomic_load_n(&DeliveredCount, __ATOMIC_SEQ_CST); }
EXPORT unsigned delivered(int n) { return Delivered[n]; }
EXPORT int remaining(void) { return (int)Device.MouseInBuffer; }
EXPORT int state(void) { return (int)Device.MouseDpcState; }
'''
with tempfile.TemporaryDirectory(prefix='i8042-mouse-concurrent-') as directory:
    path = pathlib.Path(directory)
    (path / 'test.c').write_text(fixture)
    libpath = path / ('test.dll' if sys.platform == 'win32' else 'test.so')
    command = [args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror', '-shared']
    if sys.platform != 'win32':
        command.append('-fPIC')
    subprocess.run(command + [str(path / 'test.c'), '-o', str(libpath)], check=True)
    lib = ctypes.CDLL(str(libpath))
    try:
        lib.setup()
        worker = threading.Thread(target=lib.run_dpc)
        worker.start()
        other = None
        try:
            deadline = time.monotonic() + 10
            while not lib.entered():
                if time.monotonic() >= deadline:
                    raise RuntimeError('first callback did not start')
                time.sleep(0.001)
            lib.add_packet()
            other = threading.Thread(target=lib.run_dpc)
            other.start()
            other.join(10)
            if other.is_alive():
                raise RuntimeError('overlapping DPC did not hand off its work')
        finally:
            lib.release_callback()
            worker.join(10)
            if other is not None:
                other.join(10)
        if worker.is_alive() or (other is not None and other.is_alive()):
            raise RuntimeError('DPC thread did not finish')
        values = [lib.delivered(n) for n in range(lib.delivered_count())]
        if lib.overlap() or values != [1, 2] or lib.remaining() or lib.state():
            raise AssertionError(f'overlap={lib.overlap()} delivered={values} remaining={lib.remaining()} state={lib.state()}')
        print('overlapping mouse DPCs deliver each packet once')
    finally:
        if sys.platform == 'win32':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)
