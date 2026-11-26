# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Keyboard and mouse read completion ownership regressions
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
    cancel = re.search(r'(?ms)^static VOID NTAPI\nClassCancelRoutine\([^;]*?\)\s*\{.*?^\}\n', source)
    callback = re.search(r'(?ms)^static BOOLEAN NTAPI\nClassCallback\([^;]*?\)\s*\{.*?^\}\n', source)
    start = re.search(r'(?ms)^static VOID NTAPI\nClassStartIo\([^;]*?\)\s*\{.*?^\}\n', source)
    if not (prototype and read and handle and cancel and callback and start):
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
#define DPRINT1(...)
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define STATUS_SUCCESS 0
#define STATUS_PENDING 1
#define STATUS_CANCELLED (-1)
#define STATUS_BUFFER_TOO_SMALL (-2)
#define STATUS_UNSUCCESSFUL (-3)
#define NT_SUCCESS(s) ((s) >= 0)
#define IO_NO_INCREMENT 0
#define IO_KEYBOARD_INCREMENT 1
#define IO_MOUSE_INCREMENT 1
#define MIN(a,b) ((a)<(b)?(a):(b))
#define RtlMoveMemory memmove
#define RtlCopyMemory memcpy
typedef int NTSTATUS, BOOLEAN, *PBOOLEAN;
typedef unsigned char KIRQL;
typedef size_t SIZE_T;
typedef unsigned long ULONG, *PULONG;
typedef char *PCHAR;
typedef struct { int value; } INPUT_DATA;
typedef INPUT_DATA KEYBOARD_INPUT_DATA, MOUSE_INPUT_DATA, *PKEYBOARD_INPUT_DATA, *PMOUSE_INPUT_DATA;
typedef struct { struct { struct { unsigned long Length; } Read; } Parameters; } STACK;
typedef struct IRP { struct { NTSTATUS Status; SIZE_T Information; } IoStatus; int Cancel; KIRQL CancelIrql; STACK Stack; void *CancelRoutine; int Completed; int MarkedPending; INPUT_DATA Output[2]; } IRP, *PIRP;
typedef struct { int IsClassDO; } COMMON;
typedef struct { SIZE_T DataQueueSize; } DRIVER_EXTENSION;
typedef struct { COMMON Common; int SpinLock; PIRP PendingIrp; SIZE_T InputCount; INPUT_DATA *PortData; DRIVER_EXTENSION *DriverExtension; } CLASS_DEVICE_EXTENSION, *PCLASS_DEVICE_EXTENSION;
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
static void IoCompleteRequest(PIRP i, int boost) { int lock=0; KIRQL old; (void)boost; assert(!Locked && !CancelLocked && !i->Completed); if (!Locked) { KeAcquireSpinLock(&lock, &old); KeReleaseSpinLock(&lock, old); } i->Completed++; }
static NTSTATUS FillStatus;
static NTSTATUS FillEntries(PIRP i, INPUT_DATA *p, SIZE_T n) { if (FillStatus) return FillStatus; memcpy(i->Output, p, n * sizeof(*p)); return STATUS_SUCCESS; }
static VOID NTAPI ClassCancelRoutine(IN PDEVICE_OBJECT, IN PIRP);
''' + prototype.group(0) + '\n' + read.group(0) + cancel.group(0) + handle.group(0) + callback.group(0) + start.group(0) + r'''
int main(void)
{
    CLASS_DEVICE_EXTENSION ext = {0};
    DEVICE_OBJECT device = {&ext};
    IRP prior = {0}, cancelled = {0}, ready = {0}, failed = {0}, cancelled_ready = {0}, standalone = {0}, ordinary = {0}, cancel_owned = {0}, callback_owned = {0}, callback_cancelled = {0}, callback_failed = {0};
    BOOLEAN complete_cancelled, complete_read;
    KIRQL old;
    INPUT_DATA entry = {123}, queue[2] = {{0}, {0}}, event = {456};
    DRIVER_EXTENSION driver = {2};
    ULONG consumed = 0;
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
    ext.InputCount = 1;
    ext.PortData = &entry;
    ready.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
    status = ClassRead(&device, &ready);
    assert(status == STATUS_SUCCESS && ready.Completed == 1);
    assert(ready.IoStatus.Status == STATUS_SUCCESS && ready.IoStatus.Information == sizeof(INPUT_DATA));
    assert(ext.InputCount == 0 && ext.PendingIrp == &prior && prior.CancelRoutine == ClassCancelRoutine);
    assert(ready.Output[0].value == 123);
    ext.InputCount = 1;
    failed.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
    failed.IoStatus.Information = 42;
    FillStatus = STATUS_UNSUCCESSFUL;
    status = ClassRead(&device, &failed);
    assert(status == STATUS_UNSUCCESSFUL && failed.Completed == 1 && failed.IoStatus.Information == 0);
    assert(ext.InputCount == 1 && ext.PendingIrp == &prior && prior.CancelRoutine == ClassCancelRoutine);
    FillStatus = STATUS_SUCCESS;
    cancelled_ready.Cancel = TRUE;
    cancelled_ready.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
    status = ClassRead(&device, &cancelled_ready);
    assert(status == STATUS_SUCCESS && cancelled_ready.Completed == 1 && cancelled_ready.Output[0].value == 123);
    assert(ext.InputCount == 0 && ext.PendingIrp == &prior);
    ext.PendingIrp = &cancel_owned;
    ext.InputCount = 1;
    cancel_owned.Cancel = TRUE;
    cancel_owned.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
    cancel_owned.IoStatus.Status = 77;
    cancel_owned.IoStatus.Information = 42;
    KeAcquireSpinLock(&ext.SpinLock, &old);
    status = HandleReadIrp(&device, &cancel_owned, FALSE, FALSE, &complete_cancelled, &complete_read);
    KeReleaseSpinLock(&ext.SpinLock, old);
    if (cancel_owned.Completed || ext.PendingIrp != &cancel_owned || ext.InputCount != 1 ||
        cancel_owned.IoStatus.Status != 77 || cancel_owned.IoStatus.Information != 42 ||
        complete_cancelled || complete_read)
    {
        fprintf(stderr, "callback consumed or completed a cancellation-owned read\n");
        return 1;
    }
    cancel_owned.CancelIrql = 2;
    CancelLocked = 1;
    ClassCancelRoutine(&device, &cancel_owned);
    assert(cancel_owned.Completed == 1 && cancel_owned.IoStatus.Status == STATUS_CANCELLED);
    assert(cancel_owned.IoStatus.Information == 0 && ext.PendingIrp == NULL && ext.InputCount == 1);
    callback_owned.CancelRoutine = ClassCancelRoutine;
    callback_owned.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
    ext.PendingIrp = &callback_owned;
    KeAcquireSpinLock(&ext.SpinLock, &old);
    status = HandleReadIrp(&device, &callback_owned, FALSE, FALSE, &complete_cancelled, &complete_read);
    KeReleaseSpinLock(&ext.SpinLock, old);
    assert(status == STATUS_SUCCESS && !callback_owned.Completed && complete_read && callback_owned.CancelRoutine == NULL);
    IoCompleteRequest(&callback_owned, IO_KEYBOARD_INCREMENT);
    assert(callback_owned.Output[0].value == 123 && ext.PendingIrp == NULL && ext.InputCount == 0);
    assert(!complete_cancelled && complete_read);
    ext.DriverExtension = &driver;
    ext.PortData = queue;
    ext.PendingIrp = &callback_cancelled;
    callback_cancelled.Cancel = TRUE;
    callback_cancelled.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
    assert(ClassCallback(&device, &event, &event + 1, &consumed));
    assert(consumed == 1 && ext.InputCount == 1 && queue[0].value == 456);
    assert(!callback_cancelled.Completed && ext.PendingIrp == &callback_cancelled);
    callback_cancelled.CancelIrql = 2;
    CancelLocked = 1;
    ClassCancelRoutine(&device, &callback_cancelled);
    assert(callback_cancelled.Completed == 1 && ext.PendingIrp == NULL && ext.InputCount == 1);
    callback_failed.CancelRoutine = ClassCancelRoutine;
    callback_failed.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
    callback_failed.IoStatus.Information = 42;
    ext.PendingIrp = &callback_failed;
    event.value = 789;
    FillStatus = STATUS_UNSUCCESSFUL;
    assert(ClassCallback(&device, &event, &event + 1, &consumed));
    FillStatus = STATUS_SUCCESS;
    assert(consumed == 2 && ext.InputCount == 2 && queue[0].value == 456 && queue[1].value == 789);
    assert(callback_failed.Completed == 1 && callback_failed.IoStatus.Status == STATUS_UNSUCCESSFUL);
    assert(callback_failed.IoStatus.Information == 0 && callback_failed.CancelRoutine == NULL && ext.PendingIrp == NULL);
    ext.InputCount = 0;
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
    ext.PendingIrp = NULL;
    {
        IRP start_ready = {0}, start_cancelled = {0};
        entry.value = 314;
        ext.PortData = &entry;
        ext.InputCount = 1;
        start_ready.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
        ClassStartIo(&device, &start_ready);
        assert(start_ready.Completed == 1 && start_ready.Output[0].value == 314);
        assert(start_ready.IoStatus.Status == STATUS_SUCCESS && ext.InputCount == 0);
        start_cancelled.Cancel = TRUE;
        start_cancelled.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
        start_cancelled.IoStatus.Information = 42;
        ClassStartIo(&device, &start_cancelled);
        assert(start_cancelled.Completed == 1 && start_cancelled.IoStatus.Status == STATUS_CANCELLED);
        assert(start_cancelled.IoStatus.Information == 0 && ext.PendingIrp == NULL);
    }
    puts("class read completion regressions passed");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix=f'{driver}-cancel-') as directory:
        path = pathlib.Path(directory)
        (path / 'test.c').write_text(fixture)
        subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Wno-unused-parameter',
                        str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
