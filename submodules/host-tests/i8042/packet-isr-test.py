# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     PS/2 command replies do not consume unrelated input
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
source = (pathlib.Path(args.root) / 'drivers/input/i8042prt/i8042prt.c').read_text(encoding='latin-1')
functions=[]
for name in ('i8042PacketWrite', 'i8042PacketIsr'):
    match = re.search(r'(?ms)^(?:static )?BOOLEAN\n' + name + r'\(.*?^\}\n', source)
    if not match:
        raise RuntimeError(name + ' not found')
    functions.append(match.group(0))
source_argument = ', mouse' if re.search(r'i8042PacketIsr\([^)]*\bMouseInput\b', functions[-1], re.S) else ''
fixture = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define IN
#define TRUE 1
#define FALSE 0
#define KBD_ACK 0xfa
#define KBD_NACK 0xfc
#define KBD_RESEND 0xfe
#define CTRL_WRITE_MOUSE 0xd4
#define STATUS_SUCCESS 0
#define STATUS_IO_TIMEOUT (-1)
#define STATUS_UNEXPECTED_IO_ERROR (-2)
#define I8042PRT 0
#define WARN_(...)
typedef uint8_t UCHAR, BOOLEAN;
typedef uint32_t ULONG;
typedef int NTSTATUS;
typedef struct { UCHAR *Bytes; ULONG CurrentByte, ByteCount; int State; } PACKET;
enum { Idle, SendingBytes };
typedef struct { ULONG ResendIterations; } SETTINGS;
typedef struct { void *ControlPort, *DataPort; UCHAR PacketPort; PACKET Packet; ULONG PacketResends; BOOLEAN PacketComplete; NTSTATUS PacketResult; SETTINGS Settings; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
static int Writes;
static UCHAR WriteValues[8];
static BOOLEAN i8042Write(PORT_DEVICE_EXTENSION *p, void *port, UCHAR value) { (void)p; (void)port; assert(Writes<8); WriteValues[Writes++]=value; return TRUE; }
""" + '\n'.join(functions) + r"""
static BOOLEAN receive(PORT_DEVICE_EXTENSION *p, UCHAR data, BOOLEAN mouse)
{
    (void)mouse;
    return i8042PacketIsr(p, data@SOURCE_ARGUMENT@);
}
static void keyboard_source(void)
{
    UCHAR bytes[2] = {0xed, 7};
    PORT_DEVICE_EXTENSION p = {0};
    p.Packet.Bytes=bytes; p.Packet.ByteCount=2; p.Packet.CurrentByte=1;
    p.Packet.State=SendingBytes; p.Settings.ResendIterations=2; Writes=0;
    if (receive(&p, 0x08, TRUE) || p.Packet.CurrentByte != 1 || Writes || p.PacketComplete) {
        fprintf(stderr, "mouse input changed an active keyboard command\n"); assert(0);
    }
    assert(!receive(&p, KBD_ACK, TRUE) && p.Packet.CurrentByte==1 && !Writes);
    assert(!receive(&p, KBD_RESEND, TRUE) && p.Packet.CurrentByte==1 && !p.PacketResends && !Writes);
    assert(!receive(&p, KBD_NACK, TRUE) && p.Packet.State==SendingBytes && !p.PacketComplete);
    assert(!receive(&p, 0x1e, FALSE) && p.Packet.CurrentByte==1 && !Writes);
    assert(receive(&p, KBD_ACK, FALSE) && p.Packet.CurrentByte==2 && Writes==1 && WriteValues[0]==7);
    assert(receive(&p, KBD_ACK, FALSE) && p.Packet.State==Idle && p.PacketComplete && p.PacketResult==STATUS_SUCCESS);
}
static void resend_and_failure(void)
{
    UCHAR bytes[1] = {0xf3};
    PORT_DEVICE_EXTENSION p = {0};
    p.Packet.Bytes=bytes; p.Packet.ByteCount=1; p.Packet.CurrentByte=1;
    p.Packet.State=SendingBytes; p.Settings.ResendIterations=1; Writes=0;
    assert(receive(&p, KBD_RESEND, FALSE));
    assert(Writes==1 && WriteValues[0]==0xf3 && p.Packet.CurrentByte==1 && p.PacketResends==1);
    assert(receive(&p, KBD_RESEND, FALSE));
    assert(p.Packet.State==Idle && p.PacketComplete && p.PacketResult==STATUS_IO_TIMEOUT && Writes==1);
}
static void second_byte_and_nack(void)
{
    UCHAR bytes[2] = {0xed, 7};
    PORT_DEVICE_EXTENSION p = {0};
    p.Packet.Bytes=bytes; p.Packet.ByteCount=2; p.Packet.CurrentByte=1;
    p.Packet.State=SendingBytes; p.Settings.ResendIterations=2; Writes=0;
    assert(receive(&p, KBD_ACK, FALSE) && p.Packet.CurrentByte==2 && Writes==1);
    assert(receive(&p, KBD_RESEND, FALSE) && p.Packet.CurrentByte==2 && Writes==2);
    assert(WriteValues[1]==7 && p.PacketResends==1);
    assert(receive(&p, KBD_NACK, FALSE) && p.Packet.State==Idle && p.PacketComplete);
    assert(p.PacketResult==STATUS_UNEXPECTED_IO_ERROR && p.PacketResends==0);
}
static void invalid_packet_state(void)
{
    UCHAR bytes[1] = {0xf3};
    PORT_DEVICE_EXTENSION p = {0};
    p.Packet.Bytes=bytes; p.Packet.ByteCount=1; p.Packet.State=SendingBytes;
    p.Settings.ResendIterations=2; Writes=0;
    assert(receive(&p, KBD_RESEND, FALSE));
    if (!p.PacketComplete || p.PacketResult != STATUS_UNEXPECTED_IO_ERROR || Writes) {
        fprintf(stderr, "invalid packet index accepted as success\n"); assert(0);
    }
    p.Packet.State=SendingBytes; p.PacketComplete=FALSE; p.Packet.CurrentByte=2;
    assert(receive(&p, KBD_ACK, FALSE));
    assert(p.PacketComplete && p.PacketResult==STATUS_UNEXPECTED_IO_ERROR && !Writes);
    p.Packet.State=SendingBytes; p.PacketComplete=FALSE; p.Packet.CurrentByte=0;
    p.Packet.ByteCount=0; p.Packet.Bytes=NULL;
    assert(receive(&p, KBD_ACK, FALSE));
    assert(p.PacketComplete && p.PacketResult==STATUS_UNEXPECTED_IO_ERROR && !Writes);
}
static void mouse_source(void)
{
    UCHAR bytes[2] = {0xf3, 100};
    PORT_DEVICE_EXTENSION p = {0};
    p.PacketPort=CTRL_WRITE_MOUSE; p.Packet.Bytes=bytes; p.Packet.ByteCount=2;
    p.Packet.CurrentByte=1; p.Packet.State=SendingBytes; Writes=0;
    assert(!receive(&p, KBD_ACK, FALSE) && p.Packet.CurrentByte==1 && !Writes);
    assert(receive(&p, KBD_ACK, TRUE) && p.Packet.CurrentByte==2 && Writes==2);
    assert(WriteValues[0]==CTRL_WRITE_MOUSE && WriteValues[1]==100);
}
int main(void) { keyboard_source(); resend_and_failure(); second_byte_and_nack(); invalid_packet_state(); mouse_source(); puts("PS/2 command source and response filtering passed"); return 0; }
""".replace('@SOURCE_ARGUMENT@', source_argument)
with tempfile.TemporaryDirectory(prefix='i8042-packet-isr-') as directory:
    path=pathlib.Path(directory)
    (path/'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror',
                    str(path/'test.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
