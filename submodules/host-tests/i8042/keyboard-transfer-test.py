# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Keyboard transfer count cannot underflow the pending queue
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
source = (pathlib.Path(args.root) / 'drivers/input/i8042prt/keyboard.c').read_text(encoding='latin-1')
matches = [re.search(r'(?ms)^static VOID NTAPI\n' + name + r'\(.*?^\}\n', source)
           for name in ('i8042KbdDpcWork', 'i8042KbdDpcRoutine')]
if not all(matches):
    raise RuntimeError('keyboard DPC routines not found')
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RtlMoveMemory memmove
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
#define I8042PRT 0
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
static int Locked;
static LONG InterlockedOr(volatile LONG *p, LONG v) { LONG old=*p; *p |= v; return old; }
static LONG InterlockedAnd(volatile LONG *p, LONG v) { LONG old=*p; *p &= v; return old; }
static void (*BeforeReleaseHook)(void), (*AfterReleaseHook)(void);
static LONG InterlockedCompareExchange(volatile LONG *p, LONG n, LONG e)
{
    LONG old;
    if (n == 0 && e == 1 && BeforeReleaseHook)
    {
        void (*hook)(void) = BeforeReleaseHook;
        BeforeReleaseHook = 0;
        hook();
    }
    old=*p;
    if (old==e) *p=n;
    if (old==e && n==0 && e==1 && AfterReleaseHook)
    {
        void (*hook)(void) = AfterReleaseHook;
        AfterReleaseHook = 0;
        hook();
    }
    return old;
}
static void i8042PacketDpc(PORT_DEVICE_EXTENSION *p) { (void)p; }
static BOOLEAN HandlePowerKeys(I8042_KEYBOARD_EXTENSION *p) { (void)p; return FALSE; }
static KIRQL KeAcquireInterruptSpinLock(void *p) { assert(p && !Locked); Locked=1; return 2; }
static void KeReleaseInterruptSpinLock(void *p, KIRQL i) { assert(p && i==2 && Locked); Locked=0; }
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
''' + '\n'.join(match.group(0) for match in matches) + r'''
static int Depth, Overreport, Calls;
static void callback(void *object, KEYBOARD_INPUT_DATA *start, KEYBOARD_INPUT_DATA *end, ULONG *count)
{
    I8042_KEYBOARD_EXTENSION *dev = object;
    assert(!Locked);
    if (Overreport)
    {
        if (Overreport == 2)
            dev->KeyboardBuffer[dev->KeysInBuffer++].MakeCode = 2;
        *count = (ULONG)(end - start) + 1;
        return;
    }
    if (Depth)
    {
        fprintf(stderr, "keyboard callbacks overlap and can deliver the same prefix twice\n");
        assert(0);
    }
    ++Calls;
    assert(end - start == 1);
    if (Calls == 1)
    {
        assert(start[0].MakeCode == 1);
        Depth = 1;
        dev->KeyboardBuffer[dev->KeysInBuffer++].MakeCode = 2;
        dev->KeyComplete = TRUE;
        i8042KbdDpcRoutine(NULL, dev, NULL, NULL);
        Depth = 0;
    }
    else
    {
        assert(Calls == 2 && start[0].MakeCode == 2);
    }
    *count = 1;
}
static void run(int overreport)
{
    PORT_DEVICE_EXTENSION port = {(void *)1};
    KEYBOARD_INPUT_DATA data[3] = {{1}, {0}, {0xabcdef}};
    I8042_KEYBOARD_EXTENSION dev = {0};
    dev.Common.PortDeviceExtension = &port;
    dev.KeyComplete = TRUE;
    dev.KeysInBuffer = 1;
    dev.KeyboardBuffer = data;
    dev.KeyboardData.ClassService = callback;
    dev.KeyboardData.ClassDeviceObject = &dev;
    Overreport = overreport;
    Calls = 0;
    i8042KbdDpcRoutine(NULL, &dev, NULL, NULL);
    if (dev.KeyboardDpcState != 0 || (!overreport && Calls != 2) || dev.KeysInBuffer != (overreport == 2 ? 2u : overreport ? 1u : 0u) || data[2].MakeCode != 0xabcdef)
    {
        fprintf(stderr, "overreport=%d count=%u guard=%x\n", overreport, dev.KeysInBuffer, data[2].MakeCode);
        assert(0);
    }
}
static int PartialCalls;
static void partial_callback(void *object, KEYBOARD_INPUT_DATA *start, KEYBOARD_INPUT_DATA *end, ULONG *count)
{
    (void)object;
    assert(!Locked);
    assert(end - start == 2);
    if (PartialCalls++ == 0)
    {
        assert(start[0].MakeCode == 1 && start[1].MakeCode == 2);
        *count = 1;
    }
    else
    {
        assert(start[0].MakeCode == 2 && start[1].MakeCode == 3);
        *count = 2;
    }
}
static void partial_transfer(void)
{
    PORT_DEVICE_EXTENSION port = {(void *)1};
    KEYBOARD_INPUT_DATA data[4] = {{1}, {2}, {0}, {0xabcdef}};
    I8042_KEYBOARD_EXTENSION dev = {0};
    dev.Common.PortDeviceExtension = &port;
    dev.KeyComplete = TRUE;
    dev.KeysInBuffer = 2;
    dev.KeyboardBuffer = data;
    dev.KeyboardData.ClassService = partial_callback;
    i8042KbdDpcRoutine(NULL, &dev, NULL, NULL);
    assert(dev.KeysInBuffer == 1 && data[0].MakeCode == 2);
    data[dev.KeysInBuffer++].MakeCode = 3;
    dev.KeyComplete = TRUE;
    i8042KbdDpcRoutine(NULL, &dev, NULL, NULL);
    assert(dev.KeysInBuffer == 0 && dev.KeyboardDpcState == 0 && PartialCalls == 2);
    assert(data[3].MakeCode == 0xabcdef);
}
static I8042_KEYBOARD_EXTENSION *HandoffDevice;
static int HandoffCalls;
static void handoff_callback(void *object, KEYBOARD_INPUT_DATA *start, KEYBOARD_INPUT_DATA *end, ULONG *transferred)
{
    (void)object;
    ++HandoffCalls;
    assert(end - start == 1 && start[0].MakeCode == (ULONG)HandoffCalls);
    *transferred = 1;
}
static void inject_handoff(void)
{
    HandoffDevice->KeyboardBuffer[HandoffDevice->KeysInBuffer++].MakeCode = 2;
    HandoffDevice->KeyComplete = TRUE;
    i8042KbdDpcRoutine(NULL, HandoffDevice, NULL, NULL);
}
static void handoff(int before)
{
    PORT_DEVICE_EXTENSION port = {(void *)1};
    KEYBOARD_INPUT_DATA data[4] = {{1}, {0}, {0}, {0xabcdef}};
    I8042_KEYBOARD_EXTENSION dev = {0};
    dev.Common.PortDeviceExtension = &port;
    dev.KeyboardBuffer = data;
    dev.KeysInBuffer = 1;
    dev.KeyComplete = TRUE;
    dev.KeyboardData.ClassService = handoff_callback;
    HandoffDevice = &dev;
    HandoffCalls = 0;
    if (before) BeforeReleaseHook = inject_handoff; else AfterReleaseHook = inject_handoff;
    i8042KbdDpcRoutine(NULL, &dev, NULL, NULL);
    assert(HandoffCalls == 2 && dev.KeysInBuffer == 0 && dev.KeyboardDpcState == 0);
    assert(!BeforeReleaseHook && !AfterReleaseHook);
    assert(data[3].MakeCode == 0xabcdef);
}
int main(void)
{
    handoff(1);
    handoff(0);
    partial_transfer();
    run(0);
    run(1);
    run(2);
    puts("keyboard transfer counts remain bounded");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='i8042-keyboard-transfer-') as directory:
    path = pathlib.Path(directory)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
