# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     HID read startup respects pending lower create failure
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
for driver, prefix, ext_type in (('kbdhid', 'KbdHid', 'KBDHID'), ('mouhid', 'MouHid', 'MOUHID')):
    source = (root / f'drivers/hid/{driver}/{driver}.c').read_text(encoding='latin-1')
    match = re.search(r'(?ms)^NTSTATUS\nNTAPI\n' + prefix + r'_Create\(.*?^\}\n', source)
    if not match:
        raise RuntimeError(prefix + '_Create not found')
    fixture = r'''
#include <assert.h>
#include <stdio.h>
#define IN
#define NTAPI
#define FALSE 0
#define TRUE 1
#define STATUS_PENDING 259
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_CONNECTED (-1)
#define NT_SUCCESS(s) ((s) >= 0)
#define NotificationEvent 0
#define Executive 0
#define KernelMode 0
#define IO_NO_INCREMENT 0
#define ASSERT assert
#define DPRINT(...)
typedef int NTSTATUS, KEVENT;
typedef struct { int DesiredAccess; } SECURITY_CONTEXT;
typedef struct { struct { struct { SECURITY_CONTEXT *SecurityContext; } Create; } Parameters; void *FileObject; } IO_STACK_LOCATION, *PIO_STACK_LOCATION;
typedef struct { struct { NTSTATUS Status; } IoStatus; IO_STACK_LOCATION Stack; int Complete; } IRP, *PIRP;
typedef struct { void *NextDeviceObject, *FileObject; KEVENT ReadCompletionEvent; } DEVICE_EXTENSION, *PDEVICE_EXTENSION;
typedef DEVICE_EXTENSION KBDHID_DEVICE_EXTENSION, *PKBDHID_DEVICE_EXTENSION, MOUHID_DEVICE_EXTENSION, *PMOUHID_DEVICE_EXTENSION;
typedef struct { void *DeviceExtension; } DEVICE_OBJECT, *PDEVICE_OBJECT;
static int Pending, LowerStatus, Started, Enabled, Waited;
static PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP i) { return &i->Stack; }
static void IoCopyCurrentIrpStackLocationToNext(PIRP i) { (void)i; }
static void KeInitializeEvent(KEVENT *e, int type, int state) { (void)e; (void)type; (void)state; }
static void KbdHid_CreateCompletion(void) {}
static void MouHid_CreateCompletion(void) {}
static void IoSetCompletionRoutine(PIRP i, void (*f)(void), void *c, int s, int e, int x) { (void)i; (void)f; (void)c; (void)s; (void)e; (void)x; }
static NTSTATUS IoCallDriver(void *device, PIRP i) { (void)device; i->IoStatus.Status=LowerStatus; return Pending ? STATUS_PENDING : LowerStatus; }
static int KeWaitForSingleObject(void *e, int reason, int mode, int alert, void *timeout) { (void)e; (void)reason; (void)mode; (void)alert; (void)timeout; ++Waited; return 0; }
static void IoCompleteRequest(PIRP i, int boost) { (void)boost; assert(!i->Complete); ++i->Complete; }
static void KeClearEvent(KEVENT *e) { (void)e; }
static void KbdHid_EnableRepeat(PDEVICE_EXTENSION e) { (void)e; ++Enabled; }
static NTSTATUS KbdHid_InitiateRead(PDEVICE_EXTENSION e) { (void)e; ++Started; return STATUS_PENDING; }
static NTSTATUS MouHid_InitiateRead(PDEVICE_EXTENSION e) { (void)e; ++Started; return STATUS_PENDING; }
''' + match.group(0) + r'''
static void run(int pending, int lower)
{
    DEVICE_EXTENSION ext = {0};
    DEVICE_OBJECT object = {&ext};
    SECURITY_CONTEXT security = {1};
    IRP irp = {0};
    NTSTATUS result;
    int file;
    irp.Stack.FileObject=&file;
    irp.Stack.Parameters.Create.SecurityContext=&security;
    Pending=pending; LowerStatus=lower; Started=Enabled=Waited=0;
    result=PREFIX_Create(&object, &irp);
    assert(irp.Complete==1 && Waited==pending);
    if (lower < 0)
    {
        if (result != lower || irp.IoStatus.Status != lower || Started || Enabled || ext.FileObject)
        {
            fprintf(stderr, "failed lower create started input: status=%d ios=%d starts=%d enabled=%d file=%d\n", result, irp.IoStatus.Status, Started, Enabled, ext.FileObject != NULL);
            assert(0);
        }
    }
    else
    {
        assert(result==STATUS_SUCCESS && irp.IoStatus.Status==STATUS_SUCCESS);
        assert(Started==1 && ext.FileObject==&file);
    }
}
int main(void)
{
    run(1, STATUS_DEVICE_NOT_CONNECTED);
    run(0, STATUS_DEVICE_NOT_CONNECTED);
    run(1, STATUS_SUCCESS);
    run(0, STATUS_SUCCESS);
    puts("HID open honors lower create completion status");
    return 0;
}
'''.replace('PREFIX', prefix)
    with tempfile.TemporaryDirectory(prefix=f'{driver}-create-') as directory:
        path = pathlib.Path(directory)
        (path / 'test.c').write_text(fixture)
        subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)