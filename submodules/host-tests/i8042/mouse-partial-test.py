# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     PS/2 mouse assembly survives delivery of an earlier packet
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
for name in ('i8042MouQueuePacket', 'i8042MouHandleButtons', 'i8042MouHandle', 'i8042MouDpcWork', 'i8042MouDpcRoutine'):
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
typedef void *PVOID;
typedef uint8_t UCHAR, BOOLEAN, KIRQL;
typedef int8_t CHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
typedef struct { int unused; } KDPC, *PKDPC;
typedef struct { USHORT UnitId, Flags; union { ULONG Buttons; struct { USHORT ButtonFlags, ButtonData; }; }; ULONG RawButtons; LONG LastX, LastY; ULONG ExtraInformation; } MOUSE_INPUT_DATA, *PMOUSE_INPUT_DATA;
typedef enum { MouseIdle, XMovement, YMovement, ZMovement } MOUSE_STATE;
typedef enum { GenericPS2, Intellimouse, IntellimouseExplorer, Ps2pp } MOUSE_TYPE;
typedef enum { NoChange, TimeoutStart, TimeoutCancel } TIMEOUT_STATE;
typedef struct { ULONG MouseDataQueueSize; uint64_t MouseSynchIn100ns; } SETTINGS;
typedef struct { SETTINGS Settings; void *HighestDIRQLInterrupt; int Packet; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
typedef void (*PSERVICE_CALLBACK_ROUTINE)(void *, MOUSE_INPUT_DATA *, MOUSE_INPUT_DATA *, ULONG *);
typedef struct { void *ClassDeviceObject; PSERVICE_CALLBACK_ROUTINE ClassService; } CONNECT_DATA;
typedef struct { void (*QueueMousePacket)(void *); void *CallContext; } HOOK;
typedef struct { struct { PORT_DEVICE_EXTENSION *PortDeviceExtension; int MouseTimerLock, MouseTimerStopping; } Common; MOUSE_INPUT_DATA *MouseBuffer; ULONG MouseInBuffer; MOUSE_STATE MouseState; MOUSE_TYPE MouseType; USHORT MouseButtonState; HOOK MouseHook; KDPC DpcMouse, DpcMouseTimeout; int TimerMouseTimeout; volatile ULONG MouseTimeoutState; int MouseTimeoutActive; int MouseComplete; CONNECT_DATA MouseData; volatile LONG MouseDpcState; } I8042_MOUSE_EXTENSION, *PI8042_MOUSE_EXTENSION;
static ULONG ReadULongAcquire(volatile ULONG *p) { return *p; }
static LONG InterlockedExchange(volatile LONG *p, LONG n) { LONG old=*p; *p=n; return old; }
static LONG InterlockedOr(volatile LONG *p, LONG value) { LONG old=*p; *p |= value; return old; }
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
static LONG InterlockedAnd(volatile LONG *p, LONG value) { LONG old=*p; *p &= value; return old; }
static void KeAcquireSpinLock(int *p, KIRQL *old) { (void)p; *old=2; }
static void KeReleaseSpinLock(int *p, KIRQL old) { (void)p; assert(old==2); }
static int LockHeld;
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
static int KeCancelTimer(int *p) { (void)p; return 1; }
static void KeSetTimer(int *p, LARGE_INTEGER t, KDPC *d) { (void)p; (void)t; (void)d; }
''' + '\n'.join(functions) + r'''
static int AppendComplete;
static ULONG TransferCount;
static ULONG InitialCount;
static int ChangeCount;
static void callback(void *p, MOUSE_INPUT_DATA *start, MOUSE_INPUT_DATA *end, ULONG *count)
{
    I8042_MOUSE_EXTENSION *dev = p;
    assert(!LockHeld && (ULONG)(end - start) == InitialCount);
    if (AppendComplete)
    {
        i8042MouHandle(dev, 0x08);
        i8042MouHandle(dev, 0x03);
        i8042MouHandle(dev, 0x04);
    }
    i8042MouHandle(dev, 0x19);
    dev->MouseBuffer[dev->MouseInBuffer].ExtraInformation = 0x1234;
    if (ChangeCount)
        dev->MouseInBuffer = 0;
    *count = TransferCount;
}
static void run(int append, ULONG initial, ULONG transfer)
{
    PORT_DEVICE_EXTENSION port = {{6, 10}, (void *)1, 0};
    MOUSE_INPUT_DATA data[7] = {{0}};
    I8042_MOUSE_EXTENSION dev = {0};
    ULONG index = initial - transfer + (append ? 1 : 0);
    data[0].LastX = 10;
    data[6].ExtraInformation = 0xcafebabe;
    if (initial == 2)
        data[1].LastX = 20;
    dev.Common.PortDeviceExtension = &port;
    dev.MouseBuffer = data;
    dev.MouseInBuffer = initial;
    dev.MouseComplete = TRUE;
    dev.MouseState = MouseIdle;
    dev.MouseType = GenericPS2;
    dev.MouseHook.QueueMousePacket = i8042MouQueuePacket;
    dev.MouseHook.CallContext = &dev;
    dev.MouseData.ClassService = callback;
    dev.MouseData.ClassDeviceObject = &dev;
    AppendComplete = append;
    InitialCount = initial;
    TransferCount = transfer;
    ChangeCount = 0;
    i8042MouDpcRoutine(NULL, &dev, NULL, NULL);
    assert(dev.MouseInBuffer == index && dev.MouseState == XMovement);
    if (initial == 2 && transfer == 1)
        assert(data[0].LastX == 20);
    if (initial == 2 && transfer == 0)
        assert(data[0].LastX == 10 && data[1].LastX == 20);
    if (append)
        assert(data[index - 1].LastX == 3 && data[index - 1].LastY == -4);
    i8042MouHandle(&dev, 0xfe);
    i8042MouHandle(&dev, 0x02);
    if (dev.MouseInBuffer != index + 1 || data[index].LastX != -2 || data[index].LastY != -2 ||
        !(data[index].ButtonFlags & MOUSE_LEFT_BUTTON_DOWN) || data[index].ExtraInformation != 0x1234)
    {
        fprintf(stderr, "append=%d initial=%u transferred=%u count=%u x=%d y=%d buttons=%x\n",
                append, initial, transfer, dev.MouseInBuffer,
                data[index].LastX, data[index].LastY, data[index].ButtonFlags);
        assert(0);
    }
    i8042MouHandle(&dev, 0x08);
    i8042MouHandle(&dev, 0x00);
    i8042MouHandle(&dev, 0x00);
    assert(dev.MouseInBuffer == index + 2 && (data[index + 1].ButtonFlags & 2));
    assert(data[6].ExtraInformation == 0xcafebabe);
}
static void invalid_count(void)
{
    PORT_DEVICE_EXTENSION port = {{2, 10}, (void *)1, 0};
    MOUSE_INPUT_DATA data[2] = {{0}};
    I8042_MOUSE_EXTENSION dev = {0};
    dev.Common.PortDeviceExtension = &port;
    dev.MouseBuffer = data;
    dev.MouseInBuffer = 1;
    dev.MouseComplete = TRUE;
    dev.MouseHook.QueueMousePacket = i8042MouQueuePacket;
    dev.MouseHook.CallContext = &dev;
    dev.MouseData.ClassService = callback;
    dev.MouseData.ClassDeviceObject = &dev;
    AppendComplete = 0;
    InitialCount = 1;
    TransferCount = 1;
    ChangeCount = 1;
    i8042MouDpcRoutine(NULL, &dev, NULL, NULL);
    assert(dev.MouseInBuffer == 0);
}
static void overreported_count(void)
{
    PORT_DEVICE_EXTENSION port = {{4, 10}, (void *)1, 0};
    MOUSE_INPUT_DATA data[5] = {{0}};
    I8042_MOUSE_EXTENSION dev = {0};
    data[0].LastX = 10;
    data[4].ExtraInformation = 0xcafebabe;
    dev.Common.PortDeviceExtension = &port;
    dev.MouseBuffer = data;
    dev.MouseInBuffer = 1;
    dev.MouseComplete = TRUE;
    dev.MouseHook.QueueMousePacket = i8042MouQueuePacket;
    dev.MouseHook.CallContext = &dev;
    dev.MouseData.ClassService = callback;
    dev.MouseData.ClassDeviceObject = &dev;
    AppendComplete = 1;
    InitialCount = 1;
    TransferCount = 2;
    ChangeCount = 0;
    i8042MouDpcRoutine(NULL, &dev, NULL, NULL);
    assert(dev.MouseInBuffer == 2 && data[0].LastX == 10 && data[1].LastX == 3);
    assert(data[2].ExtraInformation == 0x1234 && data[4].ExtraInformation == 0xcafebabe);
}
static int CallbackDepth, CallbackCalls;
static void overlap_callback(void *object, MOUSE_INPUT_DATA *start, MOUSE_INPUT_DATA *end, ULONG *count)
{
    I8042_MOUSE_EXTENSION *dev = object;
    if (CallbackDepth)
    {
        fprintf(stderr, "mouse callbacks overlap and can deliver the same prefix twice\n");
        assert(0);
    }
    assert(!LockHeld && end - start == 1);
    ++CallbackCalls;
    if (CallbackCalls == 1)
    {
        assert(start[0].LastX == 10);
        dev->MouseBuffer[dev->MouseInBuffer].LastX = 20;
        i8042MouQueuePacket(dev);
        CallbackDepth = 1;
        i8042MouDpcRoutine(NULL, dev, NULL, NULL);
        CallbackDepth = 0;
    }
    else
    {
        assert(CallbackCalls == 2 && start[0].LastX == 20);
    }
    *count = 1;
}
static void overlap_delivery(void)
{
    PORT_DEVICE_EXTENSION port = {{4, 10}, (void *)1, 0};
    MOUSE_INPUT_DATA data[5] = {{0}};
    I8042_MOUSE_EXTENSION dev = {0};
    dev.Common.PortDeviceExtension = &port;
    dev.MouseBuffer = data;
    dev.MouseInBuffer = 1;
    dev.MouseComplete = TRUE;
    dev.MouseData.ClassService = overlap_callback;
    dev.MouseData.ClassDeviceObject = &dev;
    data[0].LastX = 10;
    i8042MouDpcRoutine(NULL, &dev, NULL, NULL);
    assert(CallbackCalls == 2 && dev.MouseInBuffer == 0);
}
static I8042_MOUSE_EXTENSION *HandoffDevice;
static int HandoffCalls;
static void handoff_callback(void *object, MOUSE_INPUT_DATA *start, MOUSE_INPUT_DATA *end, ULONG *transferred)
{
    (void)object;
    ++HandoffCalls;
    assert(end - start == 1 && start[0].LastX == (LONG)HandoffCalls);
    *transferred = 1;
}
static void inject_handoff(void)
{
    HandoffDevice->MouseBuffer[HandoffDevice->MouseInBuffer++].LastX = 2;
    HandoffDevice->MouseComplete = TRUE;
    i8042MouDpcRoutine(NULL, HandoffDevice, NULL, NULL);
}
static void handoff(int before)
{
    PORT_DEVICE_EXTENSION port = {{4, 10}, (void *)1, 0};
    MOUSE_INPUT_DATA data[5] = {{0}};
    data[0].LastX = 1;
    data[4].ExtraInformation = 0xabcdef;
    I8042_MOUSE_EXTENSION dev = {0};
    dev.Common.PortDeviceExtension = &port;
    dev.MouseBuffer = data;
    dev.MouseInBuffer = 1;
    dev.MouseComplete = TRUE;
    dev.MouseData.ClassService = handoff_callback;
    HandoffDevice = &dev;
    HandoffCalls = 0;
    if (before) BeforeReleaseHook = inject_handoff; else AfterReleaseHook = inject_handoff;
    i8042MouDpcRoutine(NULL, &dev, NULL, NULL);
    assert(HandoffCalls == 2 && dev.MouseInBuffer == 0 && dev.MouseDpcState == 0);
    assert(!BeforeReleaseHook && !AfterReleaseHook);
    assert(data[4].ExtraInformation == 0xabcdef);
}
int main(void)
{
    handoff(1);
    handoff(0);
    overlap_delivery();
    run(0, 1, 1);
    run(1, 1, 1);
    run(1, 2, 2);
    run(0, 2, 1);
    run(1, 2, 1);
    run(0, 2, 0);
    invalid_count();
    overreported_count();
    puts("mouse partial packets and queue transfer bounds passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='i8042-mouse-partial-') as directory:
    path = pathlib.Path(directory)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror', '-Wno-implicit-fallthrough',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
