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
match = re.search(r'(?ms)^static VOID NTAPI\ni8042KbdDpcRoutine\(.*?^\}\n', source)
if not match:
    raise RuntimeError('i8042KbdDpcRoutine not found')
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define VOID void
#define NTAPI
#define IN
#define TRUE 1
#define FALSE 0
#define ASSERT assert
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define __analysis_assume(x) ((void)0)
#define TRACE_(...)
#define INFO_(...)
#define WARN_(...) ((void)0)
#define I8042PRT 0
typedef uint32_t ULONG;
typedef uint8_t BOOLEAN, KIRQL;
typedef void *PVOID;
typedef struct { int unused; } KDPC, *PKDPC;
typedef struct { ULONG MakeCode; } KEYBOARD_INPUT_DATA, *PKEYBOARD_INPUT_DATA;
typedef struct { void *HighestDIRQLInterrupt; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
typedef void (*PSERVICE_CALLBACK_ROUTINE)(void *, KEYBOARD_INPUT_DATA *, KEYBOARD_INPUT_DATA *, ULONG *);
typedef struct { void *ClassDeviceObject; PSERVICE_CALLBACK_ROUTINE ClassService; } CONNECT_DATA;
typedef struct { struct { PORT_DEVICE_EXTENSION *PortDeviceExtension; } Common; BOOLEAN KeyComplete; ULONG KeysInBuffer; KEYBOARD_INPUT_DATA *KeyboardBuffer; CONNECT_DATA KeyboardData; } I8042_KEYBOARD_EXTENSION, *PI8042_KEYBOARD_EXTENSION;
static int Locked;
static void i8042PacketDpc(PORT_DEVICE_EXTENSION *p) { (void)p; }
static BOOLEAN HandlePowerKeys(I8042_KEYBOARD_EXTENSION *p) { (void)p; return FALSE; }
static KIRQL KeAcquireInterruptSpinLock(void *p) { assert(p && !Locked); Locked=1; return 2; }
static void KeReleaseInterruptSpinLock(void *p, KIRQL i) { assert(p && i==2 && Locked); Locked=0; }
static KIRQL KeGetCurrentIrql(void) { return 2; }
''' + match.group(0) + r'''
static int Depth, Overreport;
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
    if (!Depth)
    {
        Depth = 1;
        dev->KeyboardBuffer[dev->KeysInBuffer++].MakeCode = 2;
        dev->KeyComplete = TRUE;
        i8042KbdDpcRoutine(NULL, dev, NULL, NULL);
        Depth = 0;
        *count = 1;
    }
    else
    {
        assert(end - start == 2);
        *count = 2;
    }
}
static void run(int overreport)
{
    PORT_DEVICE_EXTENSION port = {(void *)1};
    KEYBOARD_INPUT_DATA data[3] = {{1}, {0}, {0xabcdef}};
    I8042_KEYBOARD_EXTENSION dev = {{&port}, TRUE, 1, data, {NULL, callback}};
    dev.KeyboardData.ClassDeviceObject = &dev;
    Overreport = overreport;
    i8042KbdDpcRoutine(NULL, &dev, NULL, NULL);
    if (dev.KeysInBuffer != (overreport == 2 ? 2u : overreport ? 1u : 0u) || data[2].MakeCode != 0xabcdef)
    {
        fprintf(stderr, "overreport=%d count=%u guard=%x\n", overreport, dev.KeysInBuffer, data[2].MakeCode);
        assert(0);
    }
}
int main(void)
{
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
