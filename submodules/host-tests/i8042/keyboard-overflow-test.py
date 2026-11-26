# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A full keyboard queue does not write outside its allocation
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
functions = []
for name in ('i8042KbdQueuePacket', 'i8042KbdCallIsrHook', 'i8042KbdInterruptService'):
    match = re.search(r'(?ms)^(?:static )?(?:VOID NTAPI|BOOLEAN NTAPI|BOOLEAN)\n' + name + r'\(.*?^\}\n', source)
    if not match:
        raise RuntimeError(name + ' not found')
    functions.append(match.group(0))
fixture = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define IN
#define OUT
#define NTAPI
#define TRUE 1
#define FALSE 0
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define __analysis_assume(x) ((void)0)
#define TRACE_(...)
#define INFO_(...)
#define WARN_(...)
#define NT_SUCCESS(x) ((x) == 0)
#define KEY_E0 2
#define KEY_E1 4
#define KEY_BREAK 1
#define KEY_MAKE 0
#define I8042PRT 0
#define MANUALLY_INITIATED_CRASH 0
#define DBG_STATUS_SYSRQ 0
#define KernelMode 0
typedef void VOID, *PVOID, *PKINTERRUPT;
typedef unsigned char UCHAR, BOOLEAN, *PBOOLEAN;
typedef unsigned int ULONG;
typedef uintptr_t ULONG_PTR;
typedef int NTSTATUS;
typedef struct { unsigned short MakeCode, Flags, Reserved; } KEYBOARD_INPUT_DATA, *PKEYBOARD_INPUT_DATA;
typedef enum {Normal, GotE0, GotE1} KEYBOARD_SCAN_STATE;
typedef struct { ULONG KeyboardDataQueueSize, PollStatusIterations, CrashOnCtrlScroll; } SETTINGS;
typedef struct { SETTINGS Settings; int Packet, PacketComplete; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
typedef BOOLEAN (*ISR_HOOK)(void *, KEYBOARD_INPUT_DATA *, int *, UCHAR, UCHAR *, BOOLEAN *, KEYBOARD_SCAN_STATE *);
typedef struct { ISR_HOOK IsrRoutine; void *Context; void (*QueueKeyboardPacket)(void *); void *CallContext; } HOOK;
typedef struct { struct { PORT_DEVICE_EXTENSION *PortDeviceExtension; } Common; int KeyComplete; ULONG KeysInBuffer; int DpcKeyboard; KEYBOARD_INPUT_DATA *KeyboardBuffer; KEYBOARD_SCAN_STATE KeyboardScanState; HOOK KeyboardHook; ULONG ComboPosition; int TabPressed; } I8042_KEYBOARD_EXTENSION, *PI8042_KEYBOARD_EXTENSION;
static int i8042ReadStatus(PORT_DEVICE_EXTENSION *p, UCHAR *s) { (void)p; *s=0; return 0; }
static int i8042ReadKeyboardData(PORT_DEVICE_EXTENSION *p, UCHAR *s) { (void)p; *s=0x1e; return 0; }
static void KeStallExecutionProcessor(int x) { (void)x; }
static void KeInsertQueueDpc(int *p, void *x, void *y) { (void)p; (void)x; (void)y; }
static int i8042PacketIsr(PORT_DEVICE_EXTENSION *p, UCHAR c) { (void)p; (void)c; return 0; }
static void KeBugCheck(int x) { (void)x; assert(0); }
static void DbgBreakPointWithStatus(int x) { (void)x; assert(0); }
static void KdSystemDebugControl(int c, void *p, int a, void *q, int b, void *r, int m) { (void)c;(void)p;(void)a;(void)q;(void)b;(void)r;(void)m; assert(0); }
''' + '\n'.join(functions) + r'''
static BOOLEAN hook(void *c, KEYBOARD_INPUT_DATA *d, int *packet, UCHAR status, UCHAR *input, BOOLEAN *cont, KEYBOARD_SCAN_STATE *state)
{
    (void)c; (void)packet; (void)status; (void)input; (void)state;
    d->Reserved = 0x5555;
    *cont = TRUE;
    return TRUE;
}
int main(void)
{
    PORT_DEVICE_EXTENSION port = {{1, 1, 0}, 0, 0};
    KEYBOARD_INPUT_DATA data[2] = {{1,2,3}, {0x7777,0x7777,0x7777}};
    I8042_KEYBOARD_EXTENSION dev = {0};
    dev.Common.PortDeviceExtension = &port;
    dev.KeyboardBuffer = data;
    dev.KeysInBuffer = 1;
    dev.KeyboardHook.IsrRoutine = hook;
    dev.KeyboardHook.QueueKeyboardPacket = i8042KbdQueuePacket;
    dev.KeyboardHook.CallContext = &dev;
    assert(i8042KbdInterruptService(NULL, &dev));
    if (dev.KeysInBuffer != 1 || data[1].MakeCode != 0x7777 || data[1].Flags != 0x7777 || data[1].Reserved != 0x7777)
    {
        fprintf(stderr, "full queue modified guard: %x %x %x\n", data[1].MakeCode, data[1].Flags, data[1].Reserved);
        return 1;
    }
    dev.KeysInBuffer = 0;
    dev.KeyComplete = FALSE;
    assert(i8042KbdInterruptService(NULL, &dev));
    if (dev.KeysInBuffer != 1 || data[0].MakeCode != 0x1e || data[0].Flags != KEY_MAKE ||
        data[0].Reserved != 0 || !dev.KeyComplete || data[1].MakeCode != 0x7777)
    {
        fprintf(stderr, "queue with space did not retain the input record\n");
        return 1;
    }
    puts("keyboard queue bounds and normal input passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='i8042-overflow-') as directory:
    path = pathlib.Path(directory)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Wno-multichar',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
