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
#undef __analysis_assume
#define __analysis_assume(x) ((void)0)
#define TRACE_(...)
#define INFO_(...)
#define WARN_(...)
#define NT_SUCCESS(x) ((x) == 0)
#define STATUS_NOT_FOUND (-2)
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
typedef struct { unsigned short UnitId, MakeCode, Flags, Reserved; ULONG ExtraInformation; } KEYBOARD_INPUT_DATA, *PKEYBOARD_INPUT_DATA;
typedef enum {Normal, GotE0, GotE1} KEYBOARD_SCAN_STATE;
typedef struct { ULONG KeyboardDataQueueSize, PollStatusIterations, CrashOnCtrlScroll; } SETTINGS;
typedef struct { SETTINGS Settings; int Packet, PacketComplete; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
typedef BOOLEAN (*ISR_HOOK)(void *, KEYBOARD_INPUT_DATA *, int *, UCHAR, UCHAR *, BOOLEAN *, KEYBOARD_SCAN_STATE *);
typedef struct { ISR_HOOK IsrRoutine; void *Context; void (*QueueKeyboardPacket)(void *); void *CallContext; } HOOK;
typedef struct { struct { PORT_DEVICE_EXTENSION *PortDeviceExtension; } Common; int KeyComplete; ULONG KeysInBuffer; int DpcKeyboard; KEYBOARD_INPUT_DATA *KeyboardBuffer; KEYBOARD_SCAN_STATE KeyboardScanState; HOOK KeyboardHook; ULONG ComboPosition; int TabPressed; } I8042_KEYBOARD_EXTENSION, *PI8042_KEYBOARD_EXTENSION;
static int WrongSource, ReadCalls, Stalls;
static UCHAR InputByte = 0x1e;
static int i8042ReadDeviceData(PORT_DEVICE_EXTENSION *p, BOOLEAN mouse, UCHAR *data, UCHAR *status) { (void)p; assert(!mouse); ReadCalls++; *status=1; *data=InputByte; return WrongSource ? STATUS_NOT_FOUND : 0; }
static void KeStallExecutionProcessor(int x) { (void)x; Stalls++; }
static unsigned DpcCalls;
static void KeInsertQueueDpc(int *p, void *x, void *y) { (void)p; (void)x; (void)y; DpcCalls++; }
static int i8042PacketIsr(PORT_DEVICE_EXTENSION *p, UCHAR c, BOOLEAN mouse) { (void)p; (void)c; assert(!mouse); return 0; }
static void KeBugCheck(int x) { (void)x; assert(0); }
static void DbgBreakPointWithStatus(int x) { (void)x; assert(0); }
static void KdSystemDebugControl(int c, void *p, int a, void *q, int b, void *r, int m) { (void)c;(void)p;(void)a;(void)q;(void)b;(void)r;(void)m; assert(0); }
''' + '\n'.join(functions) + r'''
static int HookMode;
static BOOLEAN hook(void *c, KEYBOARD_INPUT_DATA *d, int *packet, UCHAR status, UCHAR *input, BOOLEAN *cont, KEYBOARD_SCAN_STATE *state)
{
    (void)packet; (void)status; (void)input; (void)state;
    d->Reserved = 0x5555;
    if (HookMode)
        i8042KbdQueuePacket(c);
    *cont = HookMode != 1;
    return TRUE;
}
int main(void)
{
    PORT_DEVICE_EXTENSION port = {{1, 1, 0}, 0, 0};
    KEYBOARD_INPUT_DATA data[2] = {{0,1,2,3,0}, {0x7777,0x7777,0x7777,0x7777,0x77777777}};
    I8042_KEYBOARD_EXTENSION dev = {0};
    dev.Common.PortDeviceExtension = &port;
    dev.KeyboardBuffer = data;
    dev.KeysInBuffer = 1;
    dev.KeyboardHook.IsrRoutine = hook;
    dev.KeyboardHook.Context = &dev;
    dev.KeyboardHook.QueueKeyboardPacket = i8042KbdQueuePacket;
    dev.KeyboardHook.CallContext = &dev;
    assert(i8042KbdInterruptService(NULL, &dev));
    if (dev.KeysInBuffer != 1 || data[1].MakeCode != 0x7777 || data[1].Flags != 0x7777 || data[1].Reserved != 0x7777)
    {
        fprintf(stderr, "full queue modified guard: %x %x %x\n", data[1].MakeCode, data[1].Flags, data[1].Reserved);
        return 1;
    }
    for (HookMode = 1; HookMode <= 2; ++HookMode)
    {
        unsigned before = DpcCalls;
        dev.KeyComplete = FALSE;
        assert(i8042KbdInterruptService(NULL, &dev));
        assert(dev.KeysInBuffer == 1 && dev.KeyComplete && DpcCalls > before);
        assert(data[0].MakeCode == 1 && data[0].Flags == 2 && data[0].Reserved == 3);
        assert(data[1].UnitId == 0x7777 && data[1].MakeCode == 0x7777 &&
               data[1].Flags == 0x7777 && data[1].Reserved == 0x7777 &&
               data[1].ExtraInformation == 0x77777777);
    }
    HookMode = 0;
    dev.KeysInBuffer = 0;
    dev.KeyComplete = FALSE;
    assert(i8042KbdInterruptService(NULL, &dev));
    if (dev.KeysInBuffer != 1 || data[0].MakeCode != 0x1e || data[0].Flags != KEY_MAKE ||
        data[0].Reserved != 0 || !dev.KeyComplete || data[1].MakeCode != 0x7777)
    {
        fprintf(stderr, "queue with space did not retain the input record\n");
        return 1;
    }
    dev.KeysInBuffer = 0;
    for (InputByte = 0; ; InputByte = 0xff)
    {
        unsigned before = DpcCalls;
        dev.KeyComplete = FALSE;
        dev.KeyboardScanState = GotE0;
        assert(i8042KbdInterruptService(NULL, &dev));
        if (dev.KeysInBuffer != 0 || dev.KeyComplete || dev.KeyboardScanState != Normal || DpcCalls != before)
        {
            fprintf(stderr, "keyboard error byte produced an input record\n");
            return 1;
        }
        if (InputByte == 0xff) break;
    }
    InputByte = 0xaa;
    dev.KeyboardScanState = Normal;
    dev.KeyComplete = FALSE;
    {
        unsigned before = DpcCalls;
        assert(i8042KbdInterruptService(NULL, &dev));
        assert(dev.KeysInBuffer == 1 && dev.KeyComplete && DpcCalls == before + 1);
        assert(data[0].MakeCode == 0x2a && data[0].Flags == KEY_BREAK);
    }
    InputByte = 0x1e;
    dev.KeysInBuffer = 1;
    WrongSource = 1;
    ReadCalls = Stalls = 0;
    port.Settings.PollStatusIterations = 100;
    {
        unsigned before = DpcCalls;
        assert(!i8042KbdInterruptService(NULL, &dev));
        assert(ReadCalls == 1 && Stalls == 0 && DpcCalls == before && dev.KeysInBuffer == 1);
    }
    WrongSource = 0;
    port.Settings.KeyboardDataQueueSize = UINT32_MAX;
    dev.KeysInBuffer = UINT32_MAX - 1;
    i8042KbdQueuePacket(&dev);
    assert(dev.KeysInBuffer == UINT32_MAX);
    dev.KeyComplete = FALSE;
    {
        unsigned before = DpcCalls;
        i8042KbdQueuePacket(&dev);
        assert(dev.KeysInBuffer == UINT32_MAX && dev.KeyComplete && DpcCalls == before + 1);
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
