#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A detached desktop never presents into the shared primary of a source without a VidPN path
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root) / 'drivers/directx/dxgkrnl'


def extract(path, name):
    source = (root / path).read_text()
    match = re.search(r'(?ms)^static[ \t]+\w+[ \t]*\n' + name + r'\(.*?^\}\n', source)
    if not match:
        raise RuntimeError(name + ' not found in ' + path)
    return match.group(0) + '\n'


fixture = r'''
#include <stdio.h>
#include <string.h>
typedef unsigned char UCHAR, BOOLEAN;
typedef unsigned short USHORT;
typedef unsigned int ULONG, UINT;
typedef int LONG, NTSTATUS;
typedef long long LONG64, LONGLONG;
typedef unsigned long long ULONGLONG, SIZE_T, ULONG_PTR;
typedef void VOID, *PVOID;
typedef const char *PCSTR;
typedef ULONG D3DKMT_HANDLE, D3DDDI_VIDEO_PRESENT_SOURCE_ID;
typedef PVOID HANDLE;
#define TRUE 1
#define FALSE 0
#define NULL ((void *)0)
#define _In_
#define _In_opt_
#define _Inout_
#define _In_reads_opt_(x)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#define STATUS_INVALID_BUFFER_SIZE ((NTSTATUS)0xC0000206)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184)
#define STATUS_GRAPHICS_PRESENT_OCCLUDED ((NTSTATUS)0x401E0006)
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define D3DDDI_FLIPINTERVAL_IMMEDIATE 0
#define DXGK_DIRTY_RECT_SLOTS 16
#define DXGK_PRESENT_TRACE_LOG_LIMIT 32
#define DXGK_PRESENT_TRACE_SLOW_US 20000ULL
#define DXGKRNL_TRACE(...) ((void)0)
static int Failures;
#define ASSERT(x) do { if (!(x)) { printf("assertion failed: %s\n", #x); Failures++; } } while (0)
typedef struct { LONG left, top, right, bottom; } RECT, RECTL;
typedef struct { BOOLEAN UseDodLayout; BOOLEAN IsDisplayOnlyDriver; } MINIPORT_CONTEXT;
typedef struct { MINIPORT_CONTEXT *MiniportContext; } DXGKRNL_ADAPTER, *PDXGKRNL_ADAPTER;
typedef struct
{
    PVOID ShadowFb;
    ULONG ShadowFbPitch;
    ULONG ShadowFbSize;
    ULONG CommittedWidth;
    ULONG CommittedHeight;
    BOOLEAN VidPnCommitted;
    BOOLEAN HeadlessDesktop;
    BOOLEAN RundownHeld;
    HANDLE PrimaryHandle;
} DXGKRNL_SHARED_SURFACE_SNAPSHOT, *PDXGKRNL_SHARED_SURFACE_SNAPSHOT;
typedef struct
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    PVOID pSource;
    ULONG BytesPerPixel;
    LONG Pitch;
    struct { ULONG Value; } Flags;
    ULONG NumMoves;
    PVOID pMoves;
    ULONG NumDirtyRects;
    RECT *pDirtyRect;
    PVOID pfnPresentDisplayOnlyProgress;
} DXGKARG_PRESENT_DISPLAYONLY;
enum { DxgkPresentTypeBlt, DxgkPresentTypeFlip };
typedef struct
{
    int Type;
    DXGKRNL_SHARED_SURFACE_SNAPSHOT SharedSurface;
    ULONGLONG CompositorGeneration;
    D3DKMT_HANDLE hDestination;
    PVOID DestinationAllocation;
    PVOID Device;
    PVOID Window;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    BOOLEAN DestinationIsSharedPrimary;
    BOOLEAN SourceGpuOnly;
    ULONG FlipInterval;
} DXGKRNL_PRESENT_ENTRY, *PDXGKRNL_PRESENT_ENTRY;
static DXGKRNL_SHARED_SURFACE_SNAPSHOT Current;
static int MiniportPresents, SharedPrimaryPresents, GopBlits, Leases;
static LONG g_PresentCallCount, g_ScanoutCopyCount, g_PresentShadowTraceCount;
static LONG64 g_PresentCallTotalUs, g_PresentCallMaxUs;
static LONG InterlockedIncrement(LONG *v) { return ++*v; }
static LONG64 InterlockedAdd64(LONG64 *v, LONG64 a) { return *v += a; }
static void DxgkpUpdateMaximum64(LONG64 *v, ULONGLONG n) { if ((LONG64)n > *v) *v = (LONG64)n; }
static ULONGLONG DxgkpDisplayTraceNow100ns(void) { return 0; }
static ULONGLONG DxgkpDisplayTraceElapsedUs(ULONGLONG s) { return s; }
static BOOLEAN DxgkpBootLogHoldsDisplay(void) { return FALSE; }
static BOOLEAN DxgkPostDisplayCoreFirmwareScanout(BOOLEAN DisplayOnlyDriver, BOOLEAN VidPnCommitted) { return DisplayOnlyDriver || !VidPnCommitted; }
static NTSTATUS DxgkpAcquireSharedSurfaceSnapshot(PDXGKRNL_ADAPTER a, PDXGKRNL_SHARED_SURFACE_SNAPSHOT s) { (void)a; *s = Current; return STATUS_SUCCESS; }
static void DxgkpReleaseSharedSurfaceSnapshot(PDXGKRNL_SHARED_SURFACE_SNAPSHOT s) { (void)s; }
static NTSTATUS DxgkpCallMiniportShadowPresent(PDXGKRNL_ADAPTER a, DXGKARG_PRESENT_DISPLAYONLY *p, BOOLEAN *h) { (void)a; (void)p; MiniportPresents++; *h = FALSE; return STATUS_NOT_SUPPORTED; }
static NTSTATUS DxgkpPresentDisplayOnlyToSharedPrimary(PDXGKRNL_ADAPTER a, PDXGKRNL_SHARED_SURFACE_SNAPSHOT s, DXGKARG_PRESENT_DISPLAYONLY *p) { (void)a; (void)s; (void)p; SharedPrimaryPresents++; return STATUS_SUCCESS; }
static NTSTATUS DxgkpBlitShadowToGop(PDXGKRNL_ADAPTER a, PDXGKRNL_SHARED_SURFACE_SNAPSHOT s, RECT *r) { (void)a; (void)s; (void)r; GopBlits++; return STATUS_SUCCESS; }
static void DxgkPresentAccountFrame(PDXGKRNL_ADAPTER a, ULONG id) { (void)a; (void)id; }
static NTSTATUS DxgkVidPnAcquireScanoutLease(PDXGKRNL_ADAPTER a, PVOID d, ULONG id, PVOID w, ULONGLONG g, BOOLEAN *p) { (void)a; (void)d; (void)id; (void)w; (void)g; Leases++; *p = FALSE; return STATUS_SUCCESS; }
static void DxgkVidPnReleaseScanoutLease(void) { }
static NTSTATUS DxgkVidMmReferenceAllocation(HANDLE h, PDXGKRNL_ADAPTER a, PVOID d, PVOID *o) { (void)a; (void)d; *o = h; return STATUS_SUCCESS; }
'''

harness = r'''
static ULONG Pixels[64 * 64];

static void Present(BOOLEAN Committed, BOOLEAN Headless, NTSTATUS Expected, int ExpectedPresents)
{
    MINIPORT_CONTEXT Miniport = { FALSE };
    DXGKRNL_ADAPTER Adapter = { &Miniport };
    RECTL Rect = { 0, 0, 64, 64 };
    NTSTATUS Status;

    memset(&Current, 0, sizeof(Current));
    Current.ShadowFb = Pixels;
    Current.ShadowFbPitch = 64 * 4;
    Current.ShadowFbSize = sizeof(Pixels);
    Current.CommittedWidth = 64;
    Current.CommittedHeight = 64;
    Current.VidPnCommitted = Committed;
    Current.HeadlessDesktop = Headless;
    MiniportPresents = SharedPrimaryPresents = GopBlits = 0;
    Status = DxgkpPresentSourceRects(&Adapter, NULL, 0, 0, &Rect, 1, "test");
    if (Status != Expected || MiniportPresents != ExpectedPresents || SharedPrimaryPresents != ExpectedPresents)
    {
        printf("FAIL present committed=%d headless=%d: status 0x%08x miniport %d shared-primary %d gop %d\n",
               Committed, Headless, (unsigned)Status, MiniportPresents, SharedPrimaryPresents, GopBlits);
        Failures++;
    }
}

static void Flip(BOOLEAN Headless, ULONGLONG Generation, NTSTATUS Expected)
{
    MINIPORT_CONTEXT Miniport = { FALSE };
    DXGKRNL_ADAPTER Adapter = { &Miniport };
    DXGKRNL_PRESENT_ENTRY Entry;
    NTSTATUS Status;

    memset(&Entry, 0, sizeof(Entry));
    Entry.Type = DxgkPresentTypeFlip;
    Entry.SharedSurface.VidPnCommitted = !Headless;
    Entry.SharedSurface.HeadlessDesktop = Headless;
    Entry.SharedSurface.RundownHeld = TRUE;
    Entry.SharedSurface.PrimaryHandle = (HANDLE)0x1234;
    Entry.CompositorGeneration = Generation;
    Status = DxgkpPrepareDetachedDesktopPresent(&Adapter, &Entry);
    if (Status != Expected || Entry.Type != DxgkPresentTypeFlip || Entry.DestinationIsSharedPrimary)
    {
        printf("FAIL flip headless=%d generation=%llu: status 0x%08x type %d shared-primary %d\n",
               Headless, Generation, (unsigned)Status, Entry.Type, Entry.DestinationIsSharedPrimary);
        Failures++;
    }
}

int main(void)
{
    Present(TRUE, FALSE, STATUS_SUCCESS, 1);
    Present(FALSE, TRUE, STATUS_SUCCESS, 0);
    Present(FALSE, FALSE, STATUS_UNSUCCESSFUL, 0);
    Flip(FALSE, 1, STATUS_SUCCESS);
    Flip(TRUE, 0, STATUS_GRAPHICS_PRESENT_OCCLUDED);
    Flip(TRUE, 1, STATUS_GRAPHICS_PRESENT_OCCLUDED);
    printf("%s: %d failure(s)\n", Failures ? "FAIL" : "PASS", Failures);
    return Failures != 0;
}
'''

program = fixture
program += extract('display.c', 'DxgkpPresentSourceRects')
program += extract('present.c', 'DxgkpPrepareDetachedDesktopPresent')
program += harness
with tempfile.TemporaryDirectory() as temp:
    source = pathlib.Path(temp) / 'test.c'
    binary = pathlib.Path(temp) / 'test'
    source.write_text(program)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wno-unused-function', '-Wno-unused-variable',
                    '-Wno-unused-but-set-variable', '-o', str(binary), str(source)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
