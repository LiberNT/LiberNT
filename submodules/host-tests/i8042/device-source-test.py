# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Keyboard and mouse reads preserve bytes from the other source
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
source=(pathlib.Path(args.root)/'drivers/input/i8042prt/readwrite.c').read_text(encoding='latin-1')
functions=[]
for name in ('i8042ReadData','i8042ReadDeviceData'):
    match=re.search(r'(?ms)^NTSTATUS\n'+name+r'\(.*?^\}\n',source)
    if match: functions.append(match.group(0))
    elif name=='i8042ReadData': raise RuntimeError(name+' not found')
if len(functions)==1:
    read='i8042ReadData(p, mouse ? MOU_OBF : KBD_OBF, data)'
else:
    read='i8042ReadDeviceData(p, mouse, data, observed)'
fixture=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define IN
#define OUT
#define TRUE 1
#define FALSE 0
#define KBD_OBF 1
#define MOU_OBF 0x20
#define KBD_PERR 0x80
#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL (-1)
#define STATUS_NOT_FOUND (-2)
#define NT_SUCCESS(x) ((x)>=0)
#define INFO_(...)
#define I8042PRT 0
typedef uint8_t UCHAR, *PUCHAR, BOOLEAN;
typedef int NTSTATUS;
typedef struct { void *DataPort; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
static UCHAR StatusByte, DataByte;
static int DataReads;
static NTSTATUS i8042ReadStatus(PORT_DEVICE_EXTENSION *p, UCHAR *status) { (void)p; *status=StatusByte; return STATUS_SUCCESS; }
static UCHAR read_port(void *p) { (void)p; DataReads++; return DataByte; }
#define READ_PORT_UCHAR(p) read_port(p)
"""+'\n'.join(functions)+r"""
static NTSTATUS receive(PORT_DEVICE_EXTENSION *p, BOOLEAN mouse, UCHAR *data, UCHAR *observed)
{
    (void)observed;
    return @READ_CALL@;
}
int main(void)
{
    PORT_DEVICE_EXTENSION port = {(void *)1};
    UCHAR data=0x55, observed=0;
    StatusByte=KBD_OBF | MOU_OBF; DataByte=0x08; DataReads=0;
    if (receive(&port, FALSE, &data, &observed) != STATUS_NOT_FOUND || DataReads || data!=0x55) {
        fprintf(stderr, "keyboard read consumed a mouse byte\n"); return 1;
    }
    assert(NT_SUCCESS(receive(&port, TRUE, &data, &observed)) && data==0x08 && DataReads==1);
    assert(observed==StatusByte);
    StatusByte=KBD_OBF; DataByte=0x1e; DataReads=0; data=0x55;
    assert(receive(&port, TRUE, &data, &observed)==STATUS_NOT_FOUND && !DataReads && data==0x55);
    assert(NT_SUCCESS(receive(&port, FALSE, &data, &observed)) && data==0x1e && DataReads==1);
    StatusByte=MOU_OBF; DataReads=0;
    assert(!NT_SUCCESS(receive(&port, TRUE, &data, &observed)) && !DataReads);
    StatusByte=KBD_OBF | KBD_PERR; DataReads=0;
    assert(!NT_SUCCESS(receive(&port, FALSE, &data, &observed)) && DataReads==1);
    StatusByte=KBD_OBF | MOU_OBF; DataByte=0xfa; DataReads=0;
    assert(NT_SUCCESS(i8042ReadData(&port, KBD_OBF, &data)) && data==0xfa && DataReads==1);
    puts("PS/2 source classification passed"); return 0;
}
""".replace('@READ_CALL@',read)
with tempfile.TemporaryDirectory(prefix='i8042-source-') as directory:
    path=pathlib.Path(directory)
    (path/'test.c').write_text(fixture)
    subprocess.run([args.cc,'-std=gnu11','-O0','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
