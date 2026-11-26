# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Exercise the reusable HID read request at stop and resubmission boundaries
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import ctypes
import pathlib
import subprocess
import sys
import tempfile
import threading
import time

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>

#define VOID void
#define NTAPI
#define TRUE 1
#define FALSE 0
#define ASSERT assert
#define UNREFERENCED_PARAMETER(p) (void)(p)
#define Executive 0
#define KernelMode 0
#define NotificationEvent 0
#define IO_NO_INCREMENT 0
#define IRP_MJ_READ 3
#define STATUS_SUCCESS 0
#define STATUS_CANCELLED (-1)
#define STATUS_PENDING 259
#define STATUS_MORE_PROCESSING_REQUIRED (-2)
#define STATUS_CONTINUE_COMPLETION STATUS_SUCCESS
#define NT_SUCCESS(s) ((s) >= 0)
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

typedef int NTSTATUS, KSPIN_LOCK, KEVENT, IO_REMOVE_LOCK, *PBOOLEAN;
typedef unsigned ULONG;
typedef int BOOLEAN, KIRQL;
typedef void *PDEVICE_OBJECT, *PVOID, *PMDL, *PFILE_OBJECT;

typedef struct
{
    int MajorFunction;
    void *FileObject;
    struct
    {
        struct { ULONG Length, Key; struct { long long QuadPart; } ByteOffset; } Read;
    } Parameters;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;

struct Request;
typedef NTSTATUS (*PIO_COMPLETION_ROUTINE)(PDEVICE_OBJECT, struct Request *, PVOID);
typedef struct Request
{
    int Cancel, PendingReturned;
    struct { NTSTATUS Status; } IoStatus;
    void *MdlAddress, *Context;
    PIO_COMPLETION_ROUTINE Complete;
    IO_STACK_LOCATION Stack;
} IRP, *PIRP;

static int BeforeLower, AfterQueue, InParser, Gate, Cancellations, Submissions, Delivered, Waits;
static int ExpectCancelBeforeDispatch, Buffered, InlineReads, Depth, MaxDepth, Queued;
static int TestFile, OtherFile, TestMdl;
static void IoInitializeRemoveLock(IO_REMOVE_LOCK *l, ULONG tag, ULONG max, ULONG high)
{ (void)l; (void)tag; (void)max; (void)high; }
static void IoReleaseRemoveLock(IO_REMOVE_LOCK *l, PIRP i) { (void)l; (void)i; }
static void IoMarkIrpPending(PIRP i) { i->PendingReturned = TRUE; }
static void KeInitializeSpinLock(KSPIN_LOCK *l) { *l = 0; }
static void KeAcquireSpinLock(KSPIN_LOCK *l, KIRQL *old)
{
    while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE)) {}
    *old = 0;
}
static void KeReleaseSpinLock(KSPIN_LOCK *l, KIRQL old)
{ (void)old; assert(__atomic_exchange_n(l, 0, __ATOMIC_RELEASE)); }
static void KeInitializeEvent(KEVENT *e, int type, int state) { (void)type; *e = state; }
static void KeClearEvent(KEVENT *e) { __atomic_store_n(e, 0, __ATOMIC_RELEASE); }
static void KeSetEvent(KEVENT *e, int boost, int wait)
{ (void)boost; (void)wait; __atomic_store_n(e, 1, __ATOMIC_RELEASE); }
static void KeWaitForSingleObject(KEVENT *e, int reason, int mode, int alert, void *timeout)
{
    (void)reason; (void)mode; (void)alert; (void)timeout;
    __atomic_add_fetch(&Waits, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(e, __ATOMIC_ACQUIRE)) {}
}
static void IoReuseIrp(PIRP i, NTSTATUS status)
{
    assert(!__atomic_load_n(&Queued, __ATOMIC_ACQUIRE));
    memset(i, 0, sizeof(*i));
    i->IoStatus.Status = status;
}
static PIO_STACK_LOCATION IoGetNextIrpStackLocation(PIRP i) { return &i->Stack; }
static void IoSetCompletionRoutine(PIRP i, PIO_COMPLETION_ROUTINE fn, void *ctx, int s, int e, int c)
{ assert(s && e && c); i->Complete = fn; i->Context = ctx; }
static NTSTATUS IoCallDriver(PDEVICE_OBJECT device, PIRP i);
static BOOLEAN IoCancelIrp(PIRP i);

#include "hidread.h"

static HID_READ_STATE Read;
static IRP Irp;

static void Complete(PIRP i, NTSTATUS status)
{
    i->IoStatus.Status = status;
    assert(i->Complete(NULL, i, i->Context) == STATUS_MORE_PROCESSING_REQUIRED);
}

static NTSTATUS ReportCompletion(PDEVICE_OBJECT d, PIRP i, PVOID context)
{
    int deliver = HidReadBeginCompletion(context, i->IoStatus.Status);

    (void)d;
    if (deliver)
    {
        if (InParser)
        {
            __atomic_store_n(&InParser, 2, __ATOMIC_RELEASE);
            while (!__atomic_load_n(&Gate, __ATOMIC_ACQUIRE)) {}
        }
        ++Delivered;
    }
    return HidReadEndCompletion(context, deliver);
}

static BOOLEAN IoCancelIrp(PIRP i)
{
    __atomic_add_fetch(&Cancellations, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&i->Cancel, 1, __ATOMIC_RELEASE);
    if (__atomic_exchange_n(&Queued, 0, __ATOMIC_ACQ_REL))
    {
        Complete(i, STATUS_CANCELLED);
        return TRUE;
    }
    return FALSE;
}

static NTSTATUS IoCallDriver(PDEVICE_OBJECT device, PIRP i)
{
    NTSTATUS status = STATUS_PENDING;

    assert(device == &Read && i == &Irp);
    assert(i->Stack.FileObject == &TestFile || i->Stack.FileObject == &OtherFile);
    assert(i->Stack.Parameters.Read.Length == 8 && i->MdlAddress == &TestMdl);
    ++Submissions;
    ++Depth;
    if (Depth > MaxDepth)
        MaxDepth = Depth;

    if (BeforeLower)
    {
        __atomic_store_n(&BeforeLower, 2, __ATOMIC_RELEASE);
        while (!__atomic_load_n(&Gate, __ATOMIC_ACQUIRE)) {}
    }

    if (InlineReads || Buffered)
    {
        if (InlineReads)
            --InlineReads;
        status = STATUS_SUCCESS;
        Complete(i, status);
    }
    else
    {
        assert(!__atomic_exchange_n(&Queued, 1, __ATOMIC_ACQ_REL));
        if (__atomic_load_n(&i->Cancel, __ATOMIC_ACQUIRE) &&
            __atomic_exchange_n(&Queued, 0, __ATOMIC_ACQ_REL))
        {
            status = STATUS_CANCELLED;
            Complete(i, status);
        }
        else if (ExpectCancelBeforeDispatch)
        {
            assert(0);
        }

        if (AfterQueue)
        {
            __atomic_store_n(&AfterQueue, 2, __ATOMIC_RELEASE);
            while (!__atomic_load_n(&Gate, __ATOMIC_ACQUIRE)) {}
        }
    }
    --Depth;
    return status;
}

static void Setup(int started)
{
    memset(&Read, 0, sizeof(Read));
    Gate = BeforeLower = AfterQueue = InParser = 0;
    Cancellations = Submissions = Delivered = Waits = 0;
    ExpectCancelBeforeDispatch = Buffered = InlineReads = 0;
    Depth = MaxDepth = Queued = 0;
    HidReadInitialize(&Read, &Read, ReportCompletion, &Read, 0);
    Read.Irp = &Irp;
    if (started)
        assert(!HidReadStart(&Read, &TestMdl, 8));
}

EXPORT void setup(int mode)
{
    Setup(1);
    BeforeLower = mode == 1 || mode == 4;
    AfterQueue = mode == 2;
    InParser = mode == 3;
    ExpectCancelBeforeDispatch = mode == 1;
    Buffered = mode == 4;
}
EXPORT void open_read(void)
{
    assert(HidReadOpen(&Read, &TestFile));
    HidReadSubmit(&Read);
}
EXPORT void complete_read(void)
{
    assert(__atomic_exchange_n(&Queued, 0, __ATOMIC_ACQ_REL));
    Complete(&Irp, STATUS_SUCCESS);
}
EXPORT void stop_read(void)
{
    assert(HidReadBeginStop(&Read, NULL, FALSE));
    assert(!HidReadFinishStop(&Read));
}
EXPORT int at_gate(void)
{
    return __atomic_load_n(&BeforeLower, __ATOMIC_ACQUIRE) == 2 ||
           __atomic_load_n(&AfterQueue, __ATOMIC_ACQUIRE) == 2 ||
           __atomic_load_n(&InParser, __ATOMIC_ACQUIRE) == 2;
}
EXPORT int waiting(void) { return __atomic_load_n(&Waits, __ATOMIC_ACQUIRE); }
EXPORT void release_gate(void) { __atomic_store_n(&Gate, 1, __ATOMIC_RELEASE); }
EXPORT void verify(int mode)
{
    assert(Read.Idle && !Read.Active && !Read.Submitting && !Read.InFlight && !Read.Resubmit);
    assert(!Read.Started && Read.File == &TestFile && !Read.Stops && !Queued);
    assert(Submissions == 1 && Cancellations == (mode != 3) && Delivered == (mode == 3));
}
EXPORT void verify_inline_and_reopen(void)
{
    Setup(0);
    assert(!HidReadOpen(&Read, &TestFile));
    assert(Read.File == &TestFile && !Submissions && !HidReadIsStarted(&Read));
    InlineReads = 8192;
    assert(HidReadStart(&Read, &TestMdl, 8));
    HidReadSubmit(&Read);
    assert(Delivered == 8192 && MaxDepth == 1 && Submissions == 8193 && Queued);
    assert(HidReadIsStarted(&Read));
    assert(!HidReadBeginStop(&Read, &OtherFile, FALSE));
    assert(HidReadBeginStop(&Read, &TestFile, FALSE));
    assert(!HidReadOpen(&Read, &OtherFile));
    assert(Cancellations == 1 && Submissions == 8193 && !Queued);
    assert(HidReadFinishStop(&Read));
    HidReadSubmit(&Read);
    assert(Read.File == &OtherFile && Irp.Stack.FileObject == &OtherFile && !Irp.Cancel && Queued);

    assert(HidReadBeginStop(&Read, NULL, FALSE));
    assert(HidReadBeginStop(&Read, &OtherFile, FALSE));
    assert(!HidReadOpen(&Read, &TestFile));
    assert(!HidReadFinishStop(&Read));
    assert(!HidReadFinishStop(&Read));
    assert(Read.File == &TestFile && Read.Idle && !Queued);
    assert(HidReadStart(&Read, &TestMdl, 8));
    HidReadSubmit(&Read);
    assert(Irp.Stack.FileObject == &TestFile && !Irp.Cancel && Queued);
    assert(HidReadBeginStop(&Read, NULL, TRUE));
    assert(!HidReadCanOpen(&Read) && !HidReadFinishStop(&Read));
    assert(!HidReadOpen(&Read, &OtherFile) && Read.Idle && !Queued);
}
'''


def until(predicate, detail):
    deadline = time.monotonic() + 5
    while not predicate():
        if time.monotonic() >= deadline:
            raise RuntimeError(detail)
        time.sleep(.001)


with tempfile.TemporaryDirectory(prefix='hid-read-stop-') as directory:
    path = pathlib.Path(directory)
    source = path / 'test.c'
    source.write_text(fixture)
    binary = path / ('test.dll' if sys.platform == 'win32' else 'test.so')
    command = [args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-function', '-shared', '-I', str(pathlib.Path(args.root) / 'drivers/hid')]
    if sys.platform != 'win32':
        command.append('-fPIC')
    subprocess.run(command + [str(source), '-o', str(binary)], check=True)
    test = ctypes.CDLL(str(binary))
    try:
        for mode in (1, 2, 3, 4):
            test.setup(mode)
            if mode == 3:
                test.open_read()
            reader = threading.Thread(target=test.complete_read if mode == 3 else test.open_read, daemon=True)
            stopper = threading.Thread(target=test.stop_read, daemon=True)
            reader.start()
            until(test.at_gate, 'read did not reach its race boundary')
            stopper.start()
            try:
                until(test.waiting, 'stop could not reach the read drain')
                assert stopper.is_alive(), 'stop returned before report processing or submission drained'
            finally:
                test.release_gate()
            for thread in (reader, stopper):
                thread.join(5)
                assert not thread.is_alive(), 'a stop or read remained blocked after the handoff'
            test.verify(mode)
        test.verify_inline_and_reopen()
        print('HID read resubmission, cancellation, and restart passed')
    finally:
        if sys.platform == 'win32' and not any(t.is_alive() for t in threading.enumerate()
                                               if t is not threading.main_thread()):
            import _ctypes
            _ctypes.FreeLibrary(test._handle)
