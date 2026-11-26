# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Mixed keyboard and mouse interrupts preserve input during commands
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import re
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args=parser.parse_args()
root=pathlib.Path(args.root)/'drivers/input/i8042prt'
functions=[]
read_source=(root/'readwrite.c').read_text(encoding='latin-1')
read_name='i8042ReadDeviceData' if re.search(r'(?m)^i8042ReadDeviceData\(',read_source) else 'i8042ReadData'
for filename,names in (
    ('readwrite.c',(('i8042ReadStatus','NTSTATUS'),(read_name,'NTSTATUS'))),
    ('i8042prt.c',(('i8042PacketWrite','BOOLEAN'),('i8042PacketIsr','BOOLEAN'))),
    ('keyboard.c',(('i8042KbdQueuePacket','VOID NTAPI'),('i8042KbdCallIsrHook','BOOLEAN'),('i8042KbdInterruptService','BOOLEAN NTAPI'))),
    ('mouse.c',(('i8042MouInterruptService','BOOLEAN NTAPI'),)),
):
    source=(root/filename).read_text(encoding='latin-1')
    for name,result in names:
        match=re.search(r'(?ms)^(?:static )?'+result+r'\n'+name+r'\(.*?^\}\n',source)
        if not match: raise RuntimeError(name+' not found')
        functions.append(match.group(0))
fixture=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define IN
#define OUT
#define NTAPI
#define TRUE 1
#define FALSE 0
#define KBD_OBF 1
#define MOU_OBF 0x20
#define KBD_PERR 0x80
#define KBD_ACK 0xfa
#define KBD_NACK 0xfc
#define KBD_RESEND 0xfe
#define CTRL_WRITE_MOUSE 0xd4
#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL (-1)
#define STATUS_NOT_FOUND (-2)
#define STATUS_IO_TIMEOUT (-3)
#define STATUS_UNEXPECTED_IO_ERROR (-4)
#define NT_SUCCESS(x) ((x)>=0)
#define KEY_E0 2
#define KEY_E1 4
#define KEY_BREAK 1
#define KEY_MAKE 0
#define I8042PRT 0
#define MANUALLY_INITIATED_CRASH 0
#define DBG_STATUS_SYSRQ 0
#define KernelMode 0
#define ASSERT assert
#define TRACE_(...)
#define INFO_(...)
#define WARN_(...)
#define UNREFERENCED_PARAMETER(x) (void)(x)
#undef __analysis_assume
#define __analysis_assume(x) ((void)0)
typedef void VOID, *PVOID, *PKINTERRUPT;
typedef uint8_t UCHAR, *PUCHAR, BOOLEAN, *PBOOLEAN;
typedef uint32_t ULONG;
typedef uintptr_t ULONG_PTR;
typedef int NTSTATUS;
typedef struct { UCHAR *Bytes; ULONG CurrentByte, ByteCount; int State; } PACKET;
enum { Idle, SendingBytes };
typedef struct { ULONG KeyboardDataQueueSize, PollStatusIterations, CrashOnCtrlScroll, ResendIterations; } SETTINGS;
typedef struct { void *DataPort, *ControlPort; SETTINGS Settings; PACKET Packet; UCHAR PacketPort; ULONG PacketResends; BOOLEAN PacketComplete; NTSTATUS PacketResult; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
typedef struct { uint16_t UnitId, MakeCode, Flags, Reserved; ULONG ExtraInformation; } KEYBOARD_INPUT_DATA, *PKEYBOARD_INPUT_DATA;
typedef enum { Normal, GotE0, GotE1 } KEYBOARD_SCAN_STATE;
typedef BOOLEAN (*ISR_HOOK)(void *, KEYBOARD_INPUT_DATA *, PACKET *, UCHAR, UCHAR *, BOOLEAN *, KEYBOARD_SCAN_STATE *);
typedef struct { ISR_HOOK IsrRoutine; void *Context; void (*QueueKeyboardPacket)(void *); void *CallContext; } HOOK;
typedef struct { struct { PORT_DEVICE_EXTENSION *PortDeviceExtension; } Common; BOOLEAN KeyComplete; ULONG KeysInBuffer; int DpcKeyboard; KEYBOARD_INPUT_DATA *KeyboardBuffer; KEYBOARD_SCAN_STATE KeyboardScanState; HOOK KeyboardHook; ULONG ComboPosition; int TabPressed; } I8042_KEYBOARD_EXTENSION, *PI8042_KEYBOARD_EXTENSION;
typedef enum { NoChange, TimeoutStart, TimeoutCancel } TIMEOUT_STATE;
typedef enum { GenericPS2, Ps2pp } MOUSE_TYPE;
typedef struct { struct { PORT_DEVICE_EXTENSION *PortDeviceExtension; } Common; volatile ULONG MouseTimeoutState; MOUSE_TYPE MouseType; int DpcMouse; } I8042_MOUSE_EXTENSION, *PI8042_MOUSE_EXTENSION;
static struct { UCHAR value; BOOLEAN mouse; } Queue[4];
static unsigned Head, Count, DataReads, Stalls, Writes, MouseDelivered;
static UCHAR SeenMouse, SeenStatus, LastWrite;
static int ControlPort, DataPort;
static UCHAR read_port(void *port) { if (port==&ControlPort) return Count ? (UCHAR)(KBD_OBF | (Queue[Head].mouse ? MOU_OBF : 0)) : 0; assert(port==&DataPort && Count); Count--; DataReads++; return Queue[Head++].value; }
#define READ_PORT_UCHAR(p) read_port(p)
#define i8042ReadKeyboardData(p,d) i8042ReadData(p,KBD_OBF,d)
#define i8042ReadMouseData(p,d) i8042ReadData(p,MOU_OBF,d)
static BOOLEAN i8042Write(PORT_DEVICE_EXTENSION *p, void *port, UCHAR value) { (void)p; assert(port==&DataPort); Writes++; LastWrite=value; return TRUE; }
static void KeStallExecutionProcessor(int x) { (void)x; Stalls++; }
static void KeInsertQueueDpc(int *p, void *a, void *b) { (void)p; (void)a; (void)b; }
static void KeBugCheck(int x) { (void)x; assert(0); }
static void DbgBreakPointWithStatus(int x) { (void)x; assert(0); }
static void KdSystemDebugControl(int c, void *p, int a, void *q, int b, void *r, int m) { (void)c;(void)p;(void)a;(void)q;(void)b;(void)r;(void)m; assert(0); }
static void i8042MouInputTestTimeout(I8042_MOUSE_EXTENSION *p) { (void)p; }
static BOOLEAN i8042MouResetIsr(I8042_MOUSE_EXTENSION *p, UCHAR status, UCHAR value) { (void)p; SeenStatus=status; SeenMouse=value; return FALSE; }
static void i8042MouHandlePs2pp(I8042_MOUSE_EXTENSION *p, UCHAR value) { (void)p;(void)value; assert(0); }
static void i8042MouHandle(I8042_MOUSE_EXTENSION *p, UCHAR value) { (void)p; assert(value==0xfa); MouseDelivered++; }
static ULONG ReadULongAcquire(volatile ULONG *p) { return *p; }
"""+'\n'.join(functions)+r"""
int main(void)
{
    PORT_DEVICE_EXTENSION port = {0};
    I8042_KEYBOARD_EXTENSION keyboard = {0};
    I8042_MOUSE_EXTENSION mouse = {0};
    KEYBOARD_INPUT_DATA keys[4] = {{0}};
    UCHAR command[2] = {0xed,7};
    port.ControlPort=&ControlPort; port.DataPort=&DataPort;
    port.Settings.KeyboardDataQueueSize=4; port.Settings.PollStatusIterations=100;
    port.Settings.ResendIterations=2;
    port.Packet.Bytes=command; port.Packet.ByteCount=2; port.Packet.CurrentByte=1; port.Packet.State=SendingBytes;
    keyboard.Common.PortDeviceExtension=&port; keyboard.KeyboardBuffer=keys;
    keyboard.KeyboardHook.QueueKeyboardPacket=i8042KbdQueuePacket; keyboard.KeyboardHook.CallContext=&keyboard;
    mouse.Common.PortDeviceExtension=&port; mouse.MouseType=GenericPS2;
    Queue[0].value=0xfa; Queue[0].mouse=TRUE;
    Queue[1].value=0x1e; Queue[1].mouse=FALSE;
    Queue[2].value=0xfa; Queue[2].mouse=FALSE;
    Queue[3].value=0xfa; Queue[3].mouse=FALSE;
    Count=4;
    assert(!i8042KbdInterruptService(NULL,&keyboard));
    assert(Count==4 && DataReads==0 && Stalls==0 && port.Packet.CurrentByte==1);
    assert(i8042MouInterruptService(NULL,&mouse));
    assert(MouseDelivered==1 && SeenStatus==(KBD_OBF|MOU_OBF) && SeenMouse==0xfa);
    assert(port.Packet.CurrentByte==1 && !port.PacketComplete && Writes==0);
    assert(i8042KbdInterruptService(NULL,&keyboard));
    assert(keyboard.KeysInBuffer==1 && keys[0].MakeCode==0x1e && port.Packet.CurrentByte==1);
    assert(i8042KbdInterruptService(NULL,&keyboard));
    assert(port.Packet.CurrentByte==2 && Writes==1 && LastWrite==7);
    assert(i8042KbdInterruptService(NULL,&keyboard));
    assert(port.Packet.State==Idle && port.PacketComplete && port.PacketResult==STATUS_SUCCESS && Count==0);
    puts("mixed keyboard and mouse interrupts preserve input during commands"); return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='i8042-dual-isr-') as directory:
    path=pathlib.Path(directory)
    (path/'test.c').write_text(fixture)
    subprocess.run([args.cc,'-std=gnu11','-O0','-Wall','-Wextra','-Werror','-Wno-multichar','-Wno-unused-function',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
