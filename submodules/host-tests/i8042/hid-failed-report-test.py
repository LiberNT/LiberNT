# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Failed HID mouse reads must not replay the previous report
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
source = (pathlib.Path(args.root) / 'drivers/hid/mouhid/mouhid.c').read_text(encoding='latin-1')
completion = re.search(r'(?ms)^NTSTATUS\nNTAPI\nMouHid_ReadCompletion\(.*?^\}\n', source)
if not completion:
    raise RuntimeError('MouHid_ReadCompletion not found')
fixture = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define IN
#define NTAPI
#define NT_SUCCESS(status) ((status) >= 0)
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_PRIVILEGE_NOT_HELD (-1)
#define STATUS_DEVICE_NOT_CONNECTED (-2)
#define STATUS_CANCELLED (-3)
#define STATUS_IO_TIMEOUT (-4)
#define STATUS_MORE_PROCESSING_REQUIRED (-5)
#define WHEELMOUSE_HID_HARDWARE 3
#define HIDP_STATUS_SUCCESS 1
#define HidP_Input 0
#define HID_USAGE_PAGE_GENERIC 1
#define HIDP_LINK_COLLECTION_UNSPECIFIED 0
#define HID_USAGE_GENERIC_WHEEL 0x38
#define MOUSE_WHEEL 0x400
#define WHEEL_DELTA 120
#define DPRINT(...)
#define RtlZeroMemory(x, n) memset(x, 0, n)
typedef int NTSTATUS, LONG;
typedef unsigned short USHORT;
typedef void *PVOID, *PDEVICE_OBJECT;
typedef struct { struct { NTSTATUS Status; } IoStatus; } IRP, *PIRP;
typedef struct { USHORT ButtonFlags, Flags, ButtonData; LONG LastX, LastY; } MOUSE_INPUT_DATA;
typedef struct {
    int ReadReportActive, StopReadReport, ReadCompletionEvent, MouseIdentifier, FlipFlopWheel;
    void *PreparsedData, *Report;
    unsigned ReportLength;
} MOUHID_DEVICE_EXTENSION, *PMOUHID_DEVICE_EXTENSION;
static unsigned Parsed, Dispatched, Resubmitted;
static void KeSetEvent(int *e, int increment, int wait) { (void)increment; (void)wait; *e = 1; }
static void MouHid_GetButtonMove(PMOUHID_DEVICE_EXTENSION d, LONG *x, LONG *y)
{
    ++Parsed;
    *x = *(LONG *)d->Report;
    *y = 0;
}
static void MouHid_GetButtonFlags(PMOUHID_DEVICE_EXTENSION d, USHORT *flags, USHORT *mode)
{
    (void)d;
    *flags = *mode = 0;
}
static NTSTATUS HidP_GetScaledUsageValue(int a, int b, int c, int d, LONG *value, void *prep, void *report, unsigned length)
{
    (void)a; (void)b; (void)c; (void)d; (void)value; (void)prep; (void)report; (void)length;
    assert(0);
    return 0;
}
static void MouHid_DispatchInputData(PMOUHID_DEVICE_EXTENSION d, MOUSE_INPUT_DATA *data)
{
    (void)d;
    assert(data->LastX == 17);
    ++Dispatched;
}
static NTSTATUS MouHid_InitiateRead(PMOUHID_DEVICE_EXTENSION d)
{
    (void)d;
    ++Resubmitted;
    return STATUS_SUCCESS;
}
""" + completion.group(0) + r"""
static void Run(NTSTATUS status, int stopping)
{
    LONG stale = 17;
    MOUHID_DEVICE_EXTENSION dev = {0};
    IRP irp = {{status}};

    dev.Report = &stale;
    dev.ReportLength = sizeof(stale);
    dev.ReadReportActive = 1;
    dev.StopReadReport = stopping;
    Parsed = Dispatched = Resubmitted = 0;
    assert(MouHid_ReadCompletion(NULL, &irp, &dev) == STATUS_MORE_PROCESSING_REQUIRED);
    if (!NT_SUCCESS(status) || stopping)
    {
        if (Parsed || Dispatched || Resubmitted)
        {
            fprintf(stderr, "failed or stopped mouse read replayed input or submitted another read\n");
            assert(0);
        }
        assert(!dev.ReadReportActive && dev.ReadCompletionEvent);
    }
    else
    {
        assert(Parsed == 1 && Dispatched == 1 && Resubmitted == 1);
        assert(dev.ReadReportActive && !dev.ReadCompletionEvent);
    }
}
int main(void)
{
    Run(STATUS_IO_TIMEOUT, 0);
    Run(STATUS_CANCELLED, 0);
    Run(STATUS_SUCCESS, 1);
    Run(STATUS_SUCCESS, 0);
    puts("failed or stopped mouse HID reads cannot replay input");
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='mouhid-failed-report-') as directory:
    path = pathlib.Path(directory)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
