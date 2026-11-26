# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Already-cancelled keyboard and mouse reads complete without disturbing pending reads
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
root = pathlib.Path(args.root)
for driver, kind in (('kbdclass', 'KEYBOARD'), ('mouclass', 'MOUSE')):
    source = (root / f'drivers/input/{driver}/{driver}.c').read_text(encoding='latin-1')
    prototype = re.search(r'(?ms)^static NTSTATUS\nHandleReadIrp\(.*?\);', source)
    read = re.search(r'(?ms)^static NTSTATUS NTAPI\nClassRead\(.*?^\}\n', source)
    handle = re.search(r'(?ms)^static NTSTATUS\nHandleReadIrp\([^;]*?\)\s*\{.*?^\}\n', source)
    if not (prototype and read and handle):
        raise RuntimeError(f'{driver} read routines not found')
    fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define VOID void
#define IN
#define OUT
#define NTAPI
#define TRUE 1
#define FALSE 0
#define ASSERT assert
#define TRACE_(...)
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define STATUS_SUCCESS 0
#define STATUS_PENDING 1
#define STATUS_CANCELLED (-1)
#define STATUS_BUFFER_TOO_SMALL (-2)
#define NT_SUCCESS(s) ((s) >= 0)
#define IO_NO_INCREMENT 0
#define IO_KEYBOARD_INCREMENT 1
#define IO_MOUSE_INCREMENT 1
#define MIN(a,b) ((a)<(b)?(a):(b))
#define RtlMoveMemory memmove
typedef int NTSTATUS, BOOLEAN, *PBOOLEAN;
typedef unsigned char KIRQL;
typedef size_t SIZE_T;
typedef struct { int value; } INPUT_DATA;
typedef INPUT_DATA KEYBOARD_INPUT_DATA, MOUSE_INPUT_DATA;
typedef struct { struct { struct { unsigned long Length; } Read; } Parameters; } STACK;
typedef struct IRP { struct { NTSTATUS Status; SIZE_T Information; } IoStatus; int Cancel; STACK Stack; void *CancelRoutine; int Completed; int MarkedPending; } IRP, *PIRP;
typedef struct { int IsClassDO; } COMMON;
typedef struct { COMMON Common; int SpinLock; PIRP PendingIrp; SIZE_T InputCount; INPUT_DATA *PortData; } CLASS_DEVICE_EXTENSION, *PCLASS_DEVICE_EXTENSION;
typedef struct { void *DeviceExtension; } DEVICE_OBJECT, *PDEVICE_OBJECT;
static int Locked, CancelLocked;
static PDEVICE_OBJECT ClassDeviceFor(PDEVICE_OBJECT p) { return p; }
static NTSTATUS ForwardIrpAndForget(PDEVICE_OBJECT p, PIRP i) { (void)p; (void)i; assert(0); return -99; }
static STACK *IoGetCurrentIrpStackLocation(PIRP i) { return &i->Stack; }
static void KeAcquireSpinLock(int *p, KIRQL *old) { (void)p; assert(!Locked); Locked=1; *old=0; }
static void KeReleaseSpinLock(int *p, KIRQL old) { (void)p; (void)old; assert(Locked); Locked=0; }
static void IoAcquireCancelSpinLock(KIRQL *old) { assert(Locked && !CancelLocked); CancelLocked=1; *old=2; }
static void IoReleaseCancelSpinLock(KIRQL old) { assert(old==2 && CancelLocked); CancelLocked=0; }
static void *IoSetCancelRoutine(PIRP i, void *p) { void *old=i->CancelRoutine; i->CancelRoutine=p; return old; }
static void IoMarkIrpPending(PIRP i) { i->MarkedPending = 1; }
static void IoCompleteRequest(PIRP i, int boost) { (void)boost; assert(!Locked && !CancelLocked && !i->Completed); i->Completed++; }
static NTSTATUS FillEntries(PIRP i, INPUT_DATA *p, SIZE_T n) { (void)i; (void)p; (void)n; return STATUS_SUCCESS; }
static void ClassCancelRoutine(void) {}
''' + prototype.group(0) + '\n' + read.group(0) + handle.group(0) + r'''
int main(void)
{
    CLASS_DEVICE_EXTENSION ext = {0};
    DEVICE_OBJECT device = {&ext};
    IRP prior = {0}, cancelled = {0}, standalone = {0}, ordinary = {0};
    NTSTATUS status;
    ext.Common.IsClassDO = TRUE;
    prior.CancelRoutine = ClassCancelRoutine;
    ext.PendingIrp = &prior;
    cancelled.Cancel = TRUE;
    cancelled.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
    cancelled.IoStatus.Status = 77;
    cancelled.IoStatus.Information = 42;
    status = ClassRead(&device, &cancelled);
    if (status != STATUS_CANCELLED || !cancelled.Completed || cancelled.IoStatus.Status != STATUS_CANCELLED ||
        cancelled.IoStatus.Information != 0 || ext.PendingIrp != &prior || prior.Completed ||
        prior.CancelRoutine != ClassCancelRoutine)
    {
        fprintf(stderr, "status=%d completed=%d ios=%d information=%zu pending-preserved=%d\n", status,
                cancelled.Completed, cancelled.IoStatus.Status, cancelled.IoStatus.Information,
                ext.PendingIrp == &prior);
        return 1;
    }
    ext.PendingIrp = NULL;
    standalone.Cancel = TRUE;
    standalone.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
    standalone.IoStatus.Information = 42;
    status = ClassRead(&device, &standalone);
    assert(status == STATUS_CANCELLED && standalone.Completed == 1);
    assert(standalone.IoStatus.Status == STATUS_CANCELLED && standalone.IoStatus.Information == 0);
    assert(ext.PendingIrp == NULL);
    ordinary.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
    status = ClassRead(&device, &ordinary);
    assert(status == STATUS_PENDING && ordinary.MarkedPending && !ordinary.Completed);
    assert(ext.PendingIrp == &ordinary && ordinary.CancelRoutine != NULL);
    puts("already-cancelled read completed without disturbing prior request");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix=f'{driver}-cancel-') as directory:
        path = pathlib.Path(directory)
        (path / 'test.c').write_text(fixture)
        subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Wno-unused-parameter',
                        str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
