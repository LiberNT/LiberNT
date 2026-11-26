# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     PS/2 input state is initialized before connection publication
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args=parser.parse_args()
root=pathlib.Path(args.root)/'drivers/input/i8042prt'
fragments=[]
for filename,connected,starts,last in (
    ('keyboard.c','KEYBOARD_CONNECTED',('KeWaitForSingleObject(&DeviceExtension->Common.PortDeviceExtension->PnpMutex', 'i8042InitializeKeyboardAttributes(DeviceExtension);', 'DeviceExtension->KeyboardHook.QueueKeyboardPacket ='), 'DeviceExtension->KeyboardHook.CallContext = DeviceExtension;'),
    ('mouse.c','MOUSE_CONNECTED',('KeWaitForSingleObject(&DeviceExtension->Common.PortDeviceExtension->PnpMutex', 'DeviceExtension->MouseState = MouseResetting;', 'DeviceExtension->MouseHook.IsrWritePort ='), 'DeviceExtension->MouseHook.CallContext = DeviceExtension;'),
):
    source=(root/filename).read_text(encoding='latin-1')
    begin=source.index('case IOCTL_INTERNAL_'+('KEYBOARD' if filename=='keyboard.c' else 'MOUSE')+'_CONNECT:')
    end=source.index('IoQueueWorkItem(WorkItem,',begin)
    body=source[begin:end]
    positions=[body.index(x) for x in starts]
    start=min(positions)
    finish=max(body.index(last)+len(last),body.index('KeReleaseMutex(&DeviceExtension->Common.PortDeviceExtension->PnpMutex, FALSE);')+len('KeReleaseMutex(&DeviceExtension->Common.PortDeviceExtension->PnpMutex, FALSE);'),body.index('IoMarkIrpPending(Irp);')+len('IoMarkIrpPending(Irp);'))
    fragments.append(body[start:finish])
fixture=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define FALSE 0
#define Executive 0
#define KernelMode 0
#define KEYBOARD_CONNECTED 1
#define MOUSE_CONNECTED 2
#define MouseResetting 1
#define ExpectingReset 2
typedef int32_t LONG, *PLONG;
typedef struct PORT PORT;
typedef struct KEYBOARD KEYBOARD;
typedef struct MOUSE MOUSE;
struct PORT { int PnpMutex; LONG Flags; KEYBOARD *KeyboardExtension; MOUSE *MouseExtension; };
struct KEYBOARD { struct { PORT *PortDeviceExtension; } Common; struct { void (*QueueKeyboardPacket)(void *); void *CallContext; } KeyboardHook; int AttributesReady; };
struct MOUSE { struct { PORT *PortDeviceExtension; } Common; struct { void (*IsrWritePort)(void *, unsigned char); void (*QueueMousePacket)(void *); void *CallContext; } MouseHook; int MouseState, MouseResetState; };
static PORT Controller;
static KEYBOARD KeyboardDevice;
static MOUSE MouseDevice;
static int MutexHeld, Published, Pending;
static void i8042KbdQueuePacket(void *p) { (void)p; }
static void i8042MouQueuePacket(void *p) { (void)p; }
static void i8042MouIsrWritePort(void *p, unsigned char v) { (void)p; (void)v; }
static void i8042InitializeKeyboardAttributes(KEYBOARD *p) { p->AttributesReady=1; }
static void KeWaitForSingleObject(void *p, int a, int b, int c, void *d) { (void)p;(void)a;(void)b;(void)c;(void)d; assert(!MutexHeld); MutexHeld=1; }
static void KeReleaseMutex(void *p, int b) { (void)p;(void)b; assert(MutexHeld); MutexHeld=0; }
static void IoMarkIrpPending(void *p) { (void)p; Pending++; }
static LONG InterlockedOr(volatile LONG *p, LONG value)
{
    LONG old=*p; assert(MutexHeld);
    if (value==KEYBOARD_CONNECTED)
    {
        if (Controller.KeyboardExtension!=&KeyboardDevice || !KeyboardDevice.AttributesReady ||
            KeyboardDevice.KeyboardHook.QueueKeyboardPacket!=i8042KbdQueuePacket ||
            KeyboardDevice.KeyboardHook.CallContext!=&KeyboardDevice)
        { fprintf(stderr,"keyboard connection published before initialization\n"); assert(0); }
    }
    else
    {
        if (Controller.MouseExtension!=&MouseDevice || MouseDevice.MouseState!=MouseResetting ||
            MouseDevice.MouseResetState!=ExpectingReset || MouseDevice.MouseHook.IsrWritePort!=i8042MouIsrWritePort ||
            MouseDevice.MouseHook.QueueMousePacket!=i8042MouQueuePacket || MouseDevice.MouseHook.CallContext!=&MouseDevice)
        { fprintf(stderr,"mouse connection published before initialization\n"); assert(0); }
    }
    *p |= value; Published++; return old;
}
static void publish_keyboard(KEYBOARD *DeviceExtension, void *Irp)
{
@KEYBOARD@
}
static void publish_mouse(MOUSE *DeviceExtension, void *Irp)
{
@MOUSE@
}
int main(void)
{
    KeyboardDevice.Common.PortDeviceExtension=&Controller;
    MouseDevice.Common.PortDeviceExtension=&Controller;
    publish_mouse(&MouseDevice,(void *)1);
    publish_keyboard(&KeyboardDevice,(void *)1);
    assert(Published==2 && Pending==2 && !MutexHeld);
    puts("PS/2 connection publication ordering passed"); return 0;
}
""".replace('@KEYBOARD@',fragments[0]).replace('@MOUSE@',fragments[1])
with tempfile.TemporaryDirectory(prefix='i8042-connect-') as directory:
    path=pathlib.Path(directory)
    (path/'test.c').write_text(fixture)
    subprocess.run([args.cc,'-std=gnu11','-O0','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
