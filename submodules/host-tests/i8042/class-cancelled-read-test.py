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
    names = ('ClassCompleteReads', 'ClassCompletePendingRead', 'ClassCleanup', 'ClassRead',
             'ClassCancelRoutine', 'ClassCopyInput', 'ClassConsumeInput', 'ClassCallback', 'ClassStartIo')
    functions = [re.search(r'(?ms)^static (?:VOID|NTSTATUS|BOOLEAN|SIZE_T)(?: NTAPI)?\n'
                           + name + r'\([^;]*?\)\s*\{.*?^\}\n', source) for name in names]
    if not all(functions):
        raise RuntimeError(f'{driver} read routines not found')
    prototype = ''.join(f.group(0).split('\n{', 1)[0] + ';\n' for f in functions)
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
#define STATUS_DEVICE_NOT_CONNECTED (-4)
#define NT_SUCCESS(s) ((s) >= 0)
#define IO_NO_INCREMENT 0
#define IO_KEYBOARD_INCREMENT 1
#define IO_MOUSE_INCREMENT 1
#define MIN(a,b) ((a)<(b)?(a):(b))
#define RtlMoveMemory memmove
#define RtlCopyMemory memcpy
typedef int NTSTATUS, BOOLEAN, *PBOOLEAN;
typedef unsigned char KIRQL;
typedef char CCHAR;
typedef void *PFILE_OBJECT;
typedef size_t SIZE_T;
typedef unsigned long ULONG, *PULONG;
typedef char *PCHAR;
typedef struct { int value; } INPUT_DATA;
typedef INPUT_DATA KEYBOARD_INPUT_DATA, MOUSE_INPUT_DATA, *PKEYBOARD_INPUT_DATA, *PMOUSE_INPUT_DATA;
typedef struct LIST_ENTRY { struct LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
#define CONTAINING_RECORD(p,t,f) ((t *)((char *)(p)-offsetof(t,f)))
static void InitializeListHead(PLIST_ENTRY h) { h->Flink=h->Blink=h; }
static int IsListEmpty(PLIST_ENTRY h) { return h->Flink==h; }
static void InsertTailList(PLIST_ENTRY h, PLIST_ENTRY e) { e->Blink=h->Blink; e->Flink=h; h->Blink->Flink=e; h->Blink=e; }
static void RemoveEntryList(PLIST_ENTRY e) { e->Blink->Flink=e->Flink; e->Flink->Blink=e->Blink; }
static PLIST_ENTRY RemoveHeadList(PLIST_ENTRY h) { PLIST_ENTRY e=h->Flink; RemoveEntryList(e); return e; }
typedef struct { struct { struct { unsigned long Length; } Read; } Parameters; void *FileObject; } STACK;
typedef struct IRP { struct { NTSTATUS Status; SIZE_T Information; } IoStatus; int Cancel; KIRQL CancelIrql;
    STACK Stack; void *CancelRoutine; struct { struct { LIST_ENTRY ListEntry; void *DriverContext[4]; } Overlay; } Tail;
    int Completed, MarkedPending; INPUT_DATA Output[2]; } IRP, *PIRP;
typedef struct { int IsClassDO; } COMMON, *PCOMMON_DEVICE_EXTENSION;
typedef struct { SIZE_T DataQueueSize; } DRIVER_EXTENSION;
typedef struct { COMMON Common; int SpinLock; LIST_ENTRY PendingReads; BOOLEAN ReadsStopped; SIZE_T InputCount;
    INPUT_DATA *PortData; DRIVER_EXTENSION *DriverExtension; } CLASS_DEVICE_EXTENSION, *PCLASS_DEVICE_EXTENSION;
typedef struct { void *DeviceExtension; int References; } DEVICE_OBJECT, *PDEVICE_OBJECT;
static int Locked, CancelLocked;
static void ObReferenceObject(PDEVICE_OBJECT d) { ++d->References; }
static void ObDereferenceObject(PDEVICE_OBJECT d) { assert(d->References); --d->References; }
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
''' + prototype + ''.join(f.group(0) for f in functions) + r'''
static IRP NewIrp(PFILE_OBJECT file)
{
    IRP irp = {0};

    irp.Stack.FileObject = file;
    irp.Stack.Parameters.Read.Length = sizeof(INPUT_DATA);
    irp.IoStatus.Information = 42;
    return irp;
}

static void SimulateCancellation(PIRP irp)
{
    assert(!irp->Completed && irp->CancelRoutine == ClassCancelRoutine);
    irp->Cancel = TRUE;
    irp->CancelRoutine = NULL;
    irp->CancelIrql = 2;
    CancelLocked = 1;
    ClassCancelRoutine(NULL, irp);
    assert(irp->Completed == 1 && irp->IoStatus.Status == STATUS_CANCELLED);
    assert(!CancelLocked && !irp->IoStatus.Information && !irp->Tail.Overlay.DriverContext[0]);
}

int main(void)
{
    CLASS_DEVICE_EXTENSION ext = {0};
    DEVICE_OBJECT device = {&ext, 0};
    DRIVER_EXTENSION driver = {4};
    INPUT_DATA queue[4] = {0}, events[] = {{123}, {456}, {789}};
    int file1, file2;
    IRP first = NewIrp(&file1), second = NewIrp(&file2), third = NewIrp(&file1);
    IRP cancelled = NewIrp(&file2), cleanup = NewIrp(&file1);
    ULONG consumed = 0;

    ext.Common.IsClassDO = TRUE;
    ext.PortData = queue;
    ext.DriverExtension = &driver;
    InitializeListHead(&ext.PendingReads);

    assert(ClassRead(&device, &first) == STATUS_PENDING);
    assert(ClassRead(&device, &second) == STATUS_PENDING);
    assert(ClassRead(&device, &third) == STATUS_PENDING);
    assert(first.MarkedPending && second.MarkedPending && third.MarkedPending);
    assert(device.References == 3);
    cancelled.Cancel = TRUE;
    assert(ClassRead(&device, &cancelled) == STATUS_CANCELLED);
    assert(cancelled.Completed == 1 && !cancelled.IoStatus.Information);
    assert(ClassCallback(&device, events, events + 2, &consumed));
    if (first.Completed != 1 || second.Completed != 1 || third.Completed ||
        first.Output[0].value != 123 || second.Output[0].value != 456)
    {
        fprintf(stderr, "pending reads lost FIFO input or failed to complete\\n");
        return 1;
    }
    assert(consumed == 2 && !ext.InputCount && device.References == 1);
    assert(ClassCleanup(&device, &cleanup) == STATUS_SUCCESS && cleanup.Completed == 1);
    assert(third.Completed == 1 && third.IoStatus.Status == STATUS_CANCELLED && !third.IoStatus.Information);
    assert(IsListEmpty(&ext.PendingReads) && !device.References);

    first = NewIrp(&file1);
    second = NewIrp(&file2);
    third = NewIrp(&file1);
    assert(ClassRead(&device, &first) == STATUS_PENDING);
    assert(ClassRead(&device, &second) == STATUS_PENDING);
    assert(ClassRead(&device, &third) == STATUS_PENDING);
    assert(ClassCallback(&device, events, events + 1, &consumed));
    assert(first.Completed == 1 && first.IoStatus.Information == sizeof(INPUT_DATA));
    assert(first.Output[0].value == 123);
    SimulateCancellation(&second);
    assert(ClassCallback(&device, events + 2, events + 3, &consumed));
    assert(third.Completed == 1 && third.Output[0].value == 789);
    assert(IsListEmpty(&ext.PendingReads) && !device.References);

    first = NewIrp(&file1);
    second = NewIrp(&file2);
    assert(ClassRead(&device, &first) == STATUS_PENDING);
    assert(ClassRead(&device, &second) == STATUS_PENDING);
    assert(IoSetCancelRoutine(&first, NULL) == ClassCancelRoutine);
    assert(ClassCallback(&device, events, events + 2, &consumed));
    assert(!first.Completed && second.Completed == 1 && second.Output[0].value == 123);
    assert(ext.InputCount == 1 && queue[0].value == 456);
    first.Cancel = TRUE;
    first.CancelIrql = 2;
    CancelLocked = 1;
    ClassCancelRoutine(NULL, &first);
    assert(first.Completed == 1 && first.IoStatus.Status == STATUS_CANCELLED);
    assert(IsListEmpty(&ext.PendingReads) && !device.References);

    first = NewIrp(&file1);
    FillStatus = STATUS_UNSUCCESSFUL;
    assert(ClassRead(&device, &first) == STATUS_UNSUCCESSFUL);
    assert(first.Completed == 1 && !first.IoStatus.Information && ext.InputCount == 1);
    FillStatus = STATUS_SUCCESS;
    first = NewIrp(&file1);
    ClassStartIo(&device, &first);
    assert(first.Completed == 1 && first.Output[0].value == 456 && !ext.InputCount);

    first = NewIrp(&file1);
    second = NewIrp(&file2);
    third = NewIrp(&file1);
    assert(ClassRead(&device, &first) == STATUS_PENDING);
    assert(ClassRead(&device, &second) == STATUS_PENDING);
    assert(ClassRead(&device, &third) == STATUS_PENDING);
    cleanup = NewIrp(&file1);
    assert(ClassCleanup(&device, &cleanup) == STATUS_SUCCESS);
    assert(first.Completed == 1 && third.Completed == 1 && !second.Completed);
    assert(first.IoStatus.Status == STATUS_CANCELLED && third.IoStatus.Status == STATUS_CANCELLED);
    assert(device.References == 1);
    ClassCompletePendingRead(&device, NULL, STATUS_DEVICE_NOT_CONNECTED);
    assert(second.Completed == 1 && second.IoStatus.Status == STATUS_DEVICE_NOT_CONNECTED);
    assert(IsListEmpty(&ext.PendingReads) && !device.References);
    first = NewIrp(&file1);
    assert(ClassRead(&device, &first) == STATUS_DEVICE_NOT_CONNECTED);
    assert(first.Completed == 1 && !first.IoStatus.Information);
    consumed = 0;
    assert(ClassCallback(&device, events, events + 2, &consumed));
    assert(!consumed && !ext.InputCount);

    puts("class read FIFO, completion and cancellation regressions passed");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix=f'{driver}-cancel-') as directory:
        path = pathlib.Path(directory)
        (path / 'test.c').write_text(fixture)
        subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Wno-unused-parameter',
                        str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
