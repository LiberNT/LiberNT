# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The kernel mixer sets a pin's data format only for a pin it has and a caller it can trust
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--source', help='pin.c to test instead of the one under --root')
args = parser.parse_args()
source = pathlib.Path(args.source or pathlib.Path(args.root) / 'drivers/wdm/audio/filters/kmixer/pin.c').read_text()

match = re.search(r'(?ms)^NTSTATUS\nNTAPI\nPin_fnDeviceIoControl\(.*?^\}\n', source)
assert match, 'Pin_fnDeviceIoControl not found'

fixture = r'''
#include <stdio.h>
#include <string.h>
#define NTAPI
#define DPRINT1(...)
#define ASSERT(x) ((void)0)
#define KernelMode 0
#define UserMode 1
#define IO_NO_INCREMENT 0
#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define KSPROPERTY_CONNECTION_DATAFORMAT 4
#define KSPROPERTY_TYPE_SET 2
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); Failures++; } } while (0)
typedef int NTSTATUS;
typedef unsigned int ULONG;
typedef unsigned short USHORT;
typedef void *PVOID;
typedef struct { ULONG a, b, c, d; } GUID;
typedef struct { GUID Set; ULONG Id; ULONG Flags; } KSPROPERTY;
typedef struct { KSPROPERTY Property; ULONG PinId; ULONG Reserved; } KSP_PIN, *PKSP_PIN;
typedef struct { struct { USHORT nChannels, wBitsPerSample; ULONG nSamplesPerSec; } WaveFormatEx; } KSDATAFORMAT_WAVEFORMATEX, *PKSDATAFORMAT_WAVEFORMATEX;
typedef struct { PVOID FsContext2; } FILE_OBJECT, *PFILE_OBJECT;
typedef struct
{
    struct { struct { ULONG InputBufferLength, OutputBufferLength; PVOID Type3InputBuffer; } DeviceIoControl; } Parameters;
    PFILE_OBJECT FileObject;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
typedef struct _IRP
{
    char RequestorMode;
    PVOID UserBuffer;
    struct { NTSTATUS Status; ULONG Information; } IoStatus;
    IO_STACK_LOCATION Stack;
} IRP, *PIRP;
typedef PVOID PDEVICE_OBJECT;

static int Failures, Completions;
static const GUID KSPROPSETID_Connection = { 1, 2, 3, 4 };

static PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP Irp) { return &Irp->Stack; }
static void IoCompleteRequest(PIRP Irp, int Boost) { (void)Irp; (void)Boost; Completions++; }
static int IsEqualGUIDAligned(const GUID *a, const GUID *b) { return memcmp(a, b, sizeof(*a)) == 0; }

''' + match.group(0) + r'''
int main(void)
{
    /* The pin's two formats, with whatever follows them in the pool block */
    static KSDATAFORMAT_WAVEFORMATEX Memory[4];
    KSDATAFORMAT_WAVEFORMATEX Wanted = { { 2, 16, 48000 } }, Untouched;
    FILE_OBJECT File = { Memory };
    KSP_PIN Request;
    IRP Irp;

    memset(Memory, 0x5A, sizeof(Memory));
    Untouched = Memory[2];

    memset(&Request, 0, sizeof(Request));
    Request.Property.Set = KSPROPSETID_Connection;
    Request.Property.Id = KSPROPERTY_CONNECTION_DATAFORMAT;
    Request.Property.Flags = KSPROPERTY_TYPE_SET;

    memset(&Irp, 0, sizeof(Irp));
    Irp.RequestorMode = KernelMode;
    Irp.UserBuffer = &Wanted;
    Irp.Stack.FileObject = &File;
    Irp.Stack.Parameters.DeviceIoControl.InputBufferLength = sizeof(Request);
    Irp.Stack.Parameters.DeviceIoControl.OutputBufferLength = sizeof(Wanted);
    Irp.Stack.Parameters.DeviceIoControl.Type3InputBuffer = &Request;

    /* A pin the mixer does not have is refused, and nothing past its two formats is written */
    Request.PinId = 2;
    CHECK(Pin_fnDeviceIoControl(NULL, &Irp) != STATUS_SUCCESS);
    CHECK(memcmp(&Memory[2], &Untouched, sizeof(Untouched)) == 0);
    Request.PinId = 0x40000000;
    CHECK(Pin_fnDeviceIoControl(NULL, &Irp) != STATUS_SUCCESS);

    /* A caller in user mode names its own addresses for the request and the format: it is refused */
    Request.PinId = 1;
    Irp.RequestorMode = UserMode;
    CHECK(Pin_fnDeviceIoControl(NULL, &Irp) != STATUS_SUCCESS);
    CHECK(Memory[1].WaveFormatEx.nSamplesPerSec != 48000);

    /* The stream class driver's own request for one of the two pins goes through */
    Irp.RequestorMode = KernelMode;
    CHECK(Pin_fnDeviceIoControl(NULL, &Irp) == STATUS_SUCCESS);
    CHECK(Memory[1].WaveFormatEx.nSamplesPerSec == 48000 && Memory[1].WaveFormatEx.nChannels == 2);
    CHECK(Completions == 4);

    if (!Failures)
        printf("kmixer pin format: set for pin 0 or 1 at a kernel caller's request, refused otherwise\n");
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'pin.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wno-unused-function', str(path / 'pin.c'), '-o', str(path / 'pin')], check=True)
    subprocess.run([str(path / 'pin')], check=True)
