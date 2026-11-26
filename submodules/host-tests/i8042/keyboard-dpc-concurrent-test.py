# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     PS/2 keyboard DPC ownership during overlapping execution
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
source = (pathlib.Path(args.root) / 'drivers/input/i8042prt/keyboard.c').read_text(encoding='latin-1')
functions = []
for name in ('i8042KbdDpcWork', 'i8042KbdDpcRoutine'):
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
typedef struct { ULONG MakeCode; } KEYBOARD_INPUT_DATA, *PKEYBOARD_INPUT_DATA;
typedef struct { void *HighestDIRQLInterrupt; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
typedef void (*PSERVICE_CALLBACK_ROUTINE)(void *, KEYBOARD_INPUT_DATA *, KEYBOARD_INPUT_DATA *, ULONG *);
typedef struct { void *ClassDeviceObject; PSERVICE_CALLBACK_ROUTINE ClassService; } CONNECT_DATA;
typedef struct { struct { PORT_DEVICE_EXTENSION *PortDeviceExtension; } Common; BOOLEAN KeyComplete; ULONG KeysInBuffer; KEYBOARD_INPUT_DATA *KeyboardBuffer; CONNECT_DATA KeyboardData; volatile LONG KeyboardDpcState; } I8042_KEYBOARD_EXTENSION, *PI8042_KEYBOARD_EXTENSION;
static LONG InterlockedOr(volatile LONG *p, LONG v) { return __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST); }
static LONG InterlockedAnd(volatile LONG *p, LONG v) { return __atomic_fetch_and(p, v, __ATOMIC_SEQ_CST); }
static LONG InterlockedCompareExchange(volatile LONG *p, LONG n, LONG e) { __atomic_compare_exchange_n(p, &e, n, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); return e; }
static volatile LONG Lock;
static void i8042PacketDpc(PORT_DEVICE_EXTENSION *p) { (void)p; }
static BOOLEAN HandlePowerKeys(I8042_KEYBOARD_EXTENSION *p) { (void)p; return FALSE; }
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
static PORT_DEVICE_EXTENSION Port = {(void *)1};
static KEYBOARD_INPUT_DATA Data[4];
static I8042_KEYBOARD_EXTENSION Device;
static volatile LONG Entered, Gate, Active, Overlap, DeliveredCount;
static ULONG Delivered[8];
static void callback(void *object, KEYBOARD_INPUT_DATA *start, KEYBOARD_INPUT_DATA *end, ULONG *count)
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
        Delivered[slot] = start->MakeCode;
        ++start;
    }
    if (first)
        while (!__atomic_load_n(&Gate, __ATOMIC_SEQ_CST)) {}
    *count = (ULONG)(end - Device.KeyboardBuffer);
    __atomic_fetch_sub(&Active, 1, __ATOMIC_SEQ_CST);
}
EXPORT void setup(void)
{
    memset(&Device, 0, sizeof(Device));
    memset(Data, 0, sizeof(Data));
    Device.Common.PortDeviceExtension = &Port;
    Device.KeyboardBuffer = Data;
    Device.KeyboardData.ClassService = callback;
    Device.KeyboardData.ClassDeviceObject = &Device;
    Device.KeyComplete = TRUE;
    Device.KeysInBuffer = 1;
    Data[0].MakeCode = 1;
}
EXPORT void run_dpc(void) { i8042KbdDpcRoutine(0, &Device, 0, 0); }
EXPORT int entered(void) { return __atomic_load_n(&Entered, __ATOMIC_SEQ_CST); }
EXPORT void release_callback(void) { __atomic_store_n(&Gate, 1, __ATOMIC_SEQ_CST); }
EXPORT void add_key(void)
{
    KIRQL irql = KeAcquireInterruptSpinLock(Port.HighestDIRQLInterrupt);
    assert(Device.KeysInBuffer < 3);
    Data[Device.KeysInBuffer++].MakeCode = 2;
    Device.KeyComplete = TRUE;
    KeReleaseInterruptSpinLock(Port.HighestDIRQLInterrupt, irql);
}
EXPORT int overlap(void) { return __atomic_load_n(&Overlap, __ATOMIC_SEQ_CST); }
EXPORT int delivered_count(void) { return __atomic_load_n(&DeliveredCount, __ATOMIC_SEQ_CST); }
EXPORT unsigned delivered(int n) { return Delivered[n]; }
EXPORT int remaining(void) { return (int)Device.KeysInBuffer; }
EXPORT int state(void) { return (int)Device.KeyboardDpcState; }
'''
with tempfile.TemporaryDirectory(prefix='i8042-kbd-concurrent-') as directory:
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
            lib.add_key()
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
        print('overlapping keyboard DPCs deliver each key once')
    finally:
        if sys.platform == 'win32':
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)
