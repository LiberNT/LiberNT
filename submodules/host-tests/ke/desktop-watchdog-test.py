# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Exercise the resident desktop watchdog with a deterministic interrupt clock
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True, type=pathlib.Path)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = args.root.resolve()
shim = r"""
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#define ASSERT assert
#define NTAPI
#define VOID void
#define TRUE 1
#define FALSE 0
#define APC_LEVEL 1
#define HIGH_LEVEL 31
#define MAXULONG UINT32_MAX
#define NonPagedPoolNx 0
#define LIBERNT_DESKTOP_WATCHDOG_TIMEOUT 0x07010001
typedef void *PVOID;
typedef uint8_t BOOLEAN, KIRQL;
typedef uint32_t ULONG;
typedef uintptr_t ULONG_PTR;
typedef uint64_t ULONGLONG;
typedef int KSPIN_LOCK;
typedef struct _KEVENT { struct { int SignalState; } Header; int refs; } KEVENT, *PKEVENT;
typedef struct _IO_STATUS_BLOCK { int Status; ULONG_PTR Information; } IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;
#define ReadAcquire(p) __atomic_load_n(p, __ATOMIC_ACQUIRE)
#define NT_SUCCESS(status) ((status) >= 0)
typedef struct list { struct list *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
#define CONTAINING_RECORD(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
static void InsertTailList(PLIST_ENTRY head, PLIST_ENTRY entry) {
    entry->Blink = head->Blink;
    entry->Flink = head;
    head->Blink->Flink = entry;
    head->Blink = entry;
}
static void RemoveEntryList(PLIST_ENTRY entry) {
    entry->Blink->Flink = entry->Flink;
    entry->Flink->Blink = entry->Blink;
}
static ULONGLONG now;
static KIRQL irql;
static ULONG session = 1;
static ULONG reports, reason;
static int allocation_allowed = 1;
static ULONGLONG KiDesktopInterruptTime(void) { return now; }
static KIRQL KeGetCurrentIrql(void) { return irql; }
static KIRQL KfRaiseIrql(KIRQL target) {
    KIRQL previous = irql;
    irql = target;
    return previous;
}
static void KeAcquireSpinLockAtDpcLevel(int *lock) {
    assert(irql == HIGH_LEVEL && !*lock);
    *lock = 1;
}
static int KeTryToAcquireSpinLockAtDpcLevel(int *lock) {
    if (*lock) return 0;
    *lock = 1;
    return 1;
}
static void KeReleaseSpinLockFromDpcLevel(int *lock) { assert(*lock); *lock = 0; }
static void KeReleaseSpinLock(int *lock, KIRQL previous) {
    KeReleaseSpinLockFromDpcLevel(lock);
    irql = previous;
}
static ULONG PsGetCurrentProcessSessionId(void) { return session; }
static void ObReferenceObject(void *object) { ++((PKEVENT)object)->refs; }
static void ObDereferenceObject(void *object) { --((PKEVENT)object)->refs; }
static int KeReadStateEvent(PKEVENT event) { return event->Header.SignalState; }
#define ExAllocatePoolZero(pool, size, tag) (allocation_allowed ? calloc(1, size) : NULL)
#define ExFreePoolWithTag(ptr, tag) free(ptr)
#define DbgPrint(...) ((void)0)
static ULONG stop_session;
static void KeBugCheckEx(ULONG code, ULONG kind, ULONG sid, ULONG_PTR elapsed, ULONG limit) {
    assert(code == LIBERNT_DESKTOP_WATCHDOG_TIMEOUT && limit == 10000 && elapsed >= limit);
    reason = kind;
    stop_session = sid;
    ++reports;
}
"""
cases = r"""
#include "desktopwatch.c"
KEVENT PopShutdownEvent;
static void tick(ULONGLONG advance, ULONG expected) {
    now += advance;
    reports = 0;
    irql = HIGH_LEVEL;
    KiCheckDesktopWatchdogs(now);
    irql = 0;
    assert(reports == !!expected);
    if (expected) assert(reason == expected && stop_session == session);
}
#include "output.inc"
int main(void) {
    const ULONGLONG ten = 100000000;
    KEVENT device = {0};
    IO_STATUS_BLOCK io = {0};
    PLNT_DESKTOP_WATCH input = LntCreateDesktopWatch(LntWatchInput, &device, &io);
    PLNT_DESKTOP_WATCH dwm = LntCreateDesktopWatch(LntWatchCompositor, NULL, NULL);
    PLNT_DESKTOP_WATCH output = LntCreateDesktopWatch(LntWatchFramebuffer, NULL, NULL);
    assert(input && dwm && output && device.refs == 1);
    check_direct_presentation(output, ten);
    allocation_allowed = 0;
    tick(ten * 2, 0);
    LntUpdateDesktopWatch(input, LntWatchObserveRead);
    tick(ten * 2, 0);
    device.Header.SignalState = 1;
    tick(ten * 2, 0);
    io.Status = -1;
    io.Information = 8;
    tick(ten * 2, 0);
    io.Status = 0;
    tick(1, 0);
    tick(ten - 1, 0);
    KiDesktopWatchLock = 1;
    tick(1, 0);
    KiDesktopWatchLock = 0;
    tick(0, LntWatchInput);
    LntUpdateDesktopWatch(input, LntWatchComplete);
    tick(ten * 2, 0);
    LntUpdateDesktopWatch(dwm, LntWatchAttach);
    LntUpdateDesktopWatch(output, LntWatchArm);
    tick(ten * 2, 0);
    LntUpdateDesktopWatch(dwm, LntWatchArm);
    tick(ten / 2, 0);
    LntUpdateDesktopWatch(dwm, LntWatchArm);
    LntUpdateDesktopWatch(dwm, LntWatchPull);
    LntUpdateDesktopWatch(dwm, LntWatchDirtyFrame);
    tick(ten / 2 - 1, 0);
    LntUpdateDesktopWatch(dwm, LntWatchPull);
    LntUpdateDesktopWatch(dwm, LntWatchEmptyFrame);
    tick(1, LntWatchCompositor);
    LntUpdateDesktopWatch(dwm, LntWatchPresented);
    tick(ten, 0);
    LntUpdateDesktopWatch(dwm, LntWatchArm);
    LntUpdateDesktopWatch(dwm, LntWatchPull);
    LntUpdateDesktopWatch(dwm, LntWatchArm);
    LntUpdateDesktopWatch(dwm, LntWatchEmptyFrame);
    tick(ten, LntWatchCompositor);
    LntUpdateDesktopWatch(dwm, LntWatchOccluded);
    LntUpdateDesktopWatch(dwm, LntWatchArm);
    tick(ten * 2, 0);
    LntUpdateDesktopWatch(dwm, LntWatchPull);
    LntUpdateDesktopWatch(dwm, LntWatchDirtyFrame);
    tick(ten, LntWatchCompositor);
    LntUpdateDesktopWatch(dwm, LntWatchPresented);
    LntUpdateDesktopWatch(dwm, LntWatchDetach);
    tick(ten - 1, 0);
    LntUpdateDesktopWatch(output, LntWatchProgress);
    tick(ten - 1, 0);
    LntUpdateDesktopWatch(output, LntWatchArm);
    tick(1, LntWatchFramebuffer);
    LntUpdateDesktopWatch(output, LntWatchSuspendOutput);
    tick(ten * 2, 0);
    LntUpdateDesktopWatch(output, LntWatchResumeOutput);
    tick(ten - 1, 0);
    tick(1, LntWatchFramebuffer);
    PopShutdownEvent.Header.SignalState = 1;
    tick(ten, 0);
    LntUpdateDesktopWatch(output, LntWatchComplete);
    PopShutdownEvent.Header.SignalState = 0;
    LntUpdateDesktopWatch(dwm, LntWatchAttach);
    session = 2;
    allocation_allowed = 1;
    PLNT_DESKTOP_WATCH other = LntCreateDesktopWatch(LntWatchFramebuffer, NULL, NULL);
    allocation_allowed = 0;
    LntUpdateDesktopWatch(other, LntWatchArm);
    tick(ten, LntWatchFramebuffer);
    LntDestroyDesktopWatch(other);
    session = 1;
    LntDestroyDesktopWatch(dwm);
    LntUpdateDesktopWatch(output, LntWatchBeginDraw);
    tick(ten - 1, 0);
    LntUpdateDesktopWatch(output, LntWatchBeginDraw);
    tick(1, LntWatchFramebuffer);
    LntUpdateDesktopWatch(output, LntWatchEndDraw);
    tick(ten - 1, 0);
    LntUpdateDesktopWatch(output, LntWatchEndDraw);
    tick(ten, 0);
    LntDestroyDesktopWatch(output);
    LntDestroyDesktopWatch(input);
    tick(ten, 0);
    assert(!device.refs && KiDesktopWatches.Flink == &KiDesktopWatches);
    assert(!LntCreateDesktopWatch(LntWatchInput, &device, &io) && !device.refs);
    return 0;
}
"""


def extract_function(path, name):
    source = path.read_text()
    start = source.index('\n' + name + '(') + 1
    end = source.index('\n}', start) + 2
    return source[start:end]


def output_fixture():
    gdi = root / 'subsystems/win/win32k/gdi/ntgdi/dclife.c'
    cdd = root / 'subsystems/win/win32k/drivers/displays/cdd/present.c'
    producers = '\n'.join([
        'static BOOL', extract_function(gdi, 'DC_bWatchDesktopDraw'),
        'static VOID', extract_function(cdd, 'RcddAccumulateDirtyRect'),
        'static BOOL', extract_function(cdd, 'RcddNotifyDirtyRects'),
        'static BOOL', extract_function(cdd, 'RcddPublishPending'),
    ])
    return r'''
typedef int BOOL, LONG;
typedef struct { LONG left, top, right, bottom; } RECTL;
typedef struct { ULONG flFlags; } PDEV;
typedef struct { ULONG dctype, fs; PDEV *ppdev; } DC, *PDC;
enum { DCTYPE_DIRECT, DC_REDIRECTION = 2, PDEV_DISPLAY = 4 };
static BOOL desktopLockHeld;
#define UserIsEntered() (desktopLockHeld)
typedef struct {
    PLNT_DESKTOP_WATCH Watch;
    PVOID ScreenPtr, hDriver;
    RECTL PendingRects[2], SentRect;
    ULONG PendingRectCount, PendingSeq, DrawSeq, SentSeq, ScreenDelta, ScreenHeight;
    LONG PresentQueuedCount, PresentCompletedCount, PresentSynchronousCount, PresentFailedCount;
} CDD, *PRCDD_PDEV;
#define InterlockedIncrement(p) (++*(p))
static void RcddAccumulateRect(RECTL *rects, ULONG *count, const RECTL *r) { rects[(*count)++] = *r; }
static void RcddRectUnion(RECTL *a, const RECTL *b) { (void)a; (void)b; }
typedef unsigned char UCHAR;
typedef size_t SIZE_T;
typedef struct {
    ULONG StructSize, Flags, RectCount, SourcePitch, SourceSize;
    ULONG_PTR Source;
    RECTL Rects[1];
} DXGK_PRESENT_DIRTY_RECTS_INPUT, *PDXGK_PRESENT_DIRTY_RECTS_INPUT;
#include <string.h>
#define DXGK_PRESENT_DIRTY_MAX_RECTS 2
#define IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT 1
#define STATUS_GRAPHICS_PRESENT_OCCLUDED 2
#define STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE 3
#define FIELD_OFFSET(t, f) offsetof(t, f)
#define RtlZeroMemory(p,n) memset(p,0,n)
#define RtlCopyMemory(d,s,n) memcpy(d,s,n)
static ULONG outputError;
static ULONG EngDeviceIoControl(PVOID d, ULONG code, PVOID in, ULONG size, PVOID out, ULONG len, ULONG *ret) {
    (void)d; (void)code; (void)in; (void)size; (void)out; (void)len; (void)ret;
    if (!outputError) tick(100000000, LntWatchFramebuffer);
    return outputError;
}
''' + producers + r'''
static void check_direct_presentation(PLNT_DESKTOP_WATCH watch, ULONGLONG ten) {
    PDEV display = {PDEV_DISPLAY};
    DC dc = {DCTYPE_DIRECT, 0, &display};
    CDD c = {.Watch = watch, .ScreenPtr = &display};
    const RECTL rect = {0, 0, 10, 10};
    desktopLockHeld = FALSE;
    assert(!DC_bWatchDesktopDraw(&dc));
    RcddAccumulateDirtyRect(&c, &rect);
    tick(ten * 2, 0);
    desktopLockHeld = TRUE;
    assert(DC_bWatchDesktopDraw(&dc));
    outputError = STATUS_GRAPHICS_PRESENT_OCCLUDED;
    assert(!RcddPublishPending(&c, 1));
    tick(ten * 2, 0);
    outputError = STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
    assert(!RcddPublishPending(&c, 1));
    tick(ten * 2, 0);
    outputError = 0;
    assert(RcddPublishPending(&c, 1));
    assert(c.PendingRectCount == 0 && c.PresentCompletedCount == 1);
    tick(ten * 2, 0);
}
'''


with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory)
    (path / 'ntoskrnl.h').write_text(shim)
    (path / 'output.inc').write_text(output_fixture())
    (path / 'test.c').write_text(cases)
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(path),
                    '-I', str(root / 'sdk/include'), '-I', str(root / 'ntos/ke'),
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
