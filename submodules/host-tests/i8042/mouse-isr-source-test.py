# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Mouse ISR yields to the keyboard and uses the sampled status
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
source=(pathlib.Path(args.root)/'drivers/input/i8042prt/mouse.c').read_text(encoding='latin-1')
match=re.search(r'(?ms)^BOOLEAN NTAPI\ni8042MouInterruptService\(.*?^\}\n',source)
if not match: raise RuntimeError('mouse ISR not found')
fixture=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define IN
#define NTAPI
#define TRUE 1
#define FALSE 0
#define STATUS_NOT_FOUND (-2)
#define STATUS_UNSUCCESSFUL (-1)
#define STATUS_SUCCESS 0
#define NT_SUCCESS(x) ((x)>=0)
#define UNREFERENCED_PARAMETER(x) (void)(x)
#undef __analysis_assume
#define __analysis_assume(x) ((void)0)
#define TRACE_(...)
#define INFO_(...)
#define WARN_(...)
#define I8042PRT 0
typedef uint8_t UCHAR, BOOLEAN;
typedef uint32_t ULONG;
typedef int NTSTATUS;
typedef void *PVOID, *PKINTERRUPT;
typedef struct { ULONG PollStatusIterations; } SETTINGS;
typedef struct { SETTINGS Settings; int PacketComplete; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
typedef enum { NoChange, TimeoutStart, TimeoutCancel } TIMEOUT_STATE;
typedef enum { GenericPS2, Ps2pp } MOUSE_TYPE;
typedef struct { struct { PORT_DEVICE_EXTENSION *PortDeviceExtension; } Common; volatile ULONG MouseTimeoutState; MOUSE_TYPE MouseType; int DpcMouse; } I8042_MOUSE_EXTENSION, *PI8042_MOUSE_EXTENSION;
static int WrongSource, ReadCalls, Stalls, Delivered, PacketCalls, ResetCalls, EmptyReads;
static UCHAR SeenStatus, SeenByte;
static NTSTATUS i8042ReadDeviceData(PORT_DEVICE_EXTENSION *p, BOOLEAN mouse, UCHAR *data, UCHAR *status) { (void)p; assert(mouse); ReadCalls++; if (WrongSource) return STATUS_NOT_FOUND; if (EmptyReads>0) { EmptyReads--; return STATUS_UNSUCCESSFUL; } *status=0x21; *data=0x08; return STATUS_SUCCESS; }
static void KeStallExecutionProcessor(int n) { (void)n; Stalls++; }
static BOOLEAN i8042PacketIsr(PORT_DEVICE_EXTENSION *p, UCHAR b, BOOLEAN mouse) { (void)p; assert(mouse && b==0x08); PacketCalls++; return FALSE; }
static void i8042MouInputTestTimeout(I8042_MOUSE_EXTENSION *p) { (void)p; }
static BOOLEAN i8042MouResetIsr(I8042_MOUSE_EXTENSION *p, UCHAR status, UCHAR value) { (void)p; SeenStatus=status; SeenByte=value; ResetCalls++; return FALSE; }
static void i8042MouHandlePs2pp(I8042_MOUSE_EXTENSION *p, UCHAR value) { (void)p; (void)value; assert(0); }
static void i8042MouHandle(I8042_MOUSE_EXTENSION *p, UCHAR value) { (void)p; assert(value==0x08); Delivered++; }
static ULONG ReadULongAcquire(volatile ULONG *p) { return *p; }
static void KeInsertQueueDpc(int *p, void *a, void *b) { (void)p; (void)a; (void)b; }
"""+match.group(0)+r"""
int main(void)
{
    PORT_DEVICE_EXTENSION port = {{100},0};
    I8042_MOUSE_EXTENSION dev = {{&port},NoChange,GenericPS2,0};
    WrongSource=1;
    assert(!i8042MouInterruptService(NULL,&dev));
    assert(ReadCalls==1 && Stalls==0 && PacketCalls==0 && ResetCalls==0 && Delivered==0);
    WrongSource=0; ReadCalls=Stalls=0; EmptyReads=2;
    assert(i8042MouInterruptService(NULL,&dev));
    assert(ReadCalls==3 && Stalls==2 && PacketCalls==1 && ResetCalls==1 && Delivered==1);
    assert(SeenStatus==0x21 && SeenByte==0x08);
    puts("mouse ISR source and status handling passed"); return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='i8042-mouse-isr-') as directory:
    path=pathlib.Path(directory)
    (path/'test.c').write_text(fixture)
    subprocess.run([args.cc,'-std=gnu11','-O0','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
