# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     PS/2 packet admission preserves an active command and timer
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
source=(pathlib.Path(args.root)/'drivers/input/i8042prt/i8042prt.c').read_text(encoding='latin-1')
functions=[]
for name,result in (('i8042PacketWrite','BOOLEAN'),('i8042StartPacket','NTSTATUS')):
    match=re.search(r'(?ms)^(?:static )?'+result+r'\n'+name+r'\(.*?^\}\n',source)
    if not match: raise RuntimeError(name+' not found')
    functions.append(match.group(0))
fixture=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define IN
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_PENDING 1
#define STATUS_DEVICE_BUSY (-1)
#define STATUS_DEVICE_NOT_READY (-2)
#define STATUS_INTERNAL_ERROR (-3)
#define STATUS_IO_TIMEOUT (-4)
#define STATUS_ABANDONED (-5)
#define STATUS_INVALID_PARAMETER (-6)
#define NT_SUCCESS(x) ((x)>=0)
#define KEYBOARD_INITIALIZED 1
#define MOUSE_INITIALIZED 2
#define CTRL_WRITE_MOUSE 0xd4
#define I8042_PACKET_TIMEOUT_MS 500
#define I8042PRT 0
#define WARN_(...)
#define ERR_(...)
#define ASSERT assert
typedef uint8_t UCHAR, *PUCHAR, BOOLEAN, KIRQL;
typedef uint32_t ULONG;
typedef int32_t LONG, NTSTATUS;
typedef int64_t LONGLONG;
typedef void *PVOID, *PKINTERRUPT;
typedef struct { int unused; } IRP, *PIRP;
typedef struct { int armed, generation; } KTIMER;
typedef struct { int unused; } KDPC;
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
typedef enum { Unknown, Keyboard, Mouse } DEVICE_TYPE;
typedef enum { Idle, SendingBytes } STATE;
typedef struct { UCHAR *Bytes; ULONG CurrentByte, ByteCount; STATE State; } PACKET;
typedef struct { DEVICE_TYPE Type; void *Fdo; } FDO_DEVICE_EXTENSION, *PFDO_DEVICE_EXTENSION;
typedef struct { ULONG Flags; PKINTERRUPT HighestDIRQLInterrupt; int PacketTimerLock; KTIMER PacketTimer; KDPC PacketTimeoutDpc; PACKET Packet; ULONG PacketResends; BOOLEAN PacketComplete; NTSTATUS PacketResult; UCHAR PacketPort; PIRP CurrentIrp; void *CurrentIrpDevice; void *ControlPort, *DataPort; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
static int TimerHeld, IrqHeld, SetCalls, CancelCalls, WriteCalls, FailCalls, SetWithoutTimerLock, WriteOk=1;
static NTSTATUS FailedStatus;
static ULONG ReadULongAcquire(ULONG *p) { return *p; }
static NTSTATUS I8042CorePacketStatus(BOOLEAN init, PKINTERRUPT p) { return init && p ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY; }
static void KeAcquireSpinLock(int *p, KIRQL *old) { (void)p; assert(!TimerHeld && !IrqHeld); TimerHeld=1; *old=2; }
static void KeReleaseSpinLock(int *p, KIRQL old) { (void)p; assert(TimerHeld && !IrqHeld && old==2); TimerHeld=0; }
static KIRQL KeAcquireInterruptSpinLock(PKINTERRUPT p) { assert(p && !IrqHeld); IrqHeld=1; return 2; }
static void KeReleaseInterruptSpinLock(PKINTERRUPT p, KIRQL old) { assert(p && IrqHeld && old==2); IrqHeld=0; }
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
static void KeSetTimer(KTIMER *p, LARGE_INTEGER due, KDPC *dpc) { (void)dpc; assert(!IrqHeld && due.QuadPart<0); SetCalls++; if (!TimerHeld) SetWithoutTimerLock++; p->armed=1; p->generation++; }
static BOOLEAN i8042Write(PORT_DEVICE_EXTENSION *p, void *port, UCHAR value) { (void)p; (void)port; (void)value; assert(IrqHeld); WriteCalls++; return WriteOk; }
static void i8042FailPacket(void *fdo, PIRP irp, NTSTATUS status) { (void)fdo; (void)irp; assert(!TimerHeld && !IrqHeld); FailCalls++; FailedStatus=status; }
"""+'\n'.join(functions)+r"""
static void reset(void) { TimerHeld=IrqHeld=SetCalls=CancelCalls=WriteCalls=FailCalls=SetWithoutTimerLock=0; WriteOk=1; }
int main(void)
{
    PORT_DEVICE_EXTENSION p={0};
    FDO_DEVICE_EXTENSION fdo={Keyboard,(void *)2};
    IRP first={0}, second={0};
    UCHAR bytes[2]={0xed,7};
    NTSTATUS status;
    p.Flags=KEYBOARD_INITIALIZED; p.HighestDIRQLInterrupt=(void *)1;
    p.Packet.State=SendingBytes; p.CurrentIrp=&first; p.PacketTimer.armed=1; p.PacketTimer.generation=42;
    reset();
    status=i8042StartPacket(&p,&fdo,bytes,2,&second);
    if (status!=STATUS_DEVICE_BUSY || SetCalls || p.PacketTimer.generation!=42 || p.CurrentIrp!=&first) {
        fprintf(stderr,"busy packet changed the active command timer\n"); return 1;
    }
    assert(FailCalls==1 && FailedStatus==STATUS_DEVICE_BUSY && !TimerHeld && !IrqHeld);
    p.Packet.State=Idle; p.CurrentIrp=&first; reset();
    status=i8042StartPacket(&p,&fdo,bytes,2,&second);
    assert(status==STATUS_DEVICE_BUSY && !SetCalls && p.CurrentIrp==&first);
    p.CurrentIrp=NULL; reset();
    status=i8042StartPacket(&p,&fdo,NULL,0,&second);
    assert(status==STATUS_INVALID_PARAMETER && !SetCalls && !WriteCalls && FailCalls==1);
    reset();
    status=i8042StartPacket(&p,&fdo,bytes,2,&second);
    assert(status==STATUS_PENDING && SetCalls==1 && !SetWithoutTimerLock && WriteCalls==1);
    assert(p.CurrentIrp==&second && p.Packet.State==SendingBytes && p.Packet.CurrentByte==1 && p.PacketTimer.armed);
    p.Packet.State=Idle; p.CurrentIrp=NULL; p.PacketTimer.armed=0; reset(); WriteOk=0;
    status=i8042StartPacket(&p,&fdo,bytes,2,&second);
    assert(status==STATUS_IO_TIMEOUT && SetCalls==0 && CancelCalls==0 && FailCalls==1);
    assert(!p.CurrentIrp && !p.PacketTimer.armed && !TimerHeld && !IrqHeld);
    puts("PS/2 packet admission and timer ownership passed"); return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='i8042-packet-start-') as directory:
    path=pathlib.Path(directory)
    (path/'test.c').write_text(fixture)
    subprocess.run([args.cc,'-std=gnu11','-O0','-Wall','-Wextra','-Werror','-Wno-unused-function',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
