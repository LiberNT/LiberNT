#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The VidPN ownership query reports a detached desktop's source as occluded, as its flips are
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
source = (pathlib.Path(args.root) / 'drivers/directx/dxgkrnl/vidpn.c').read_text()


def extract(prefix, name, required=True):
    match = re.search(r'(?ms)^' + prefix + r'\n' + name + r'\(.*?^\}\n', source)
    if not match:
        if required:
            raise RuntimeError(name + ' not found in vidpn.c')
        return ''
    return match.group(0) + '\n'


fixture = r'''
#include <stdio.h>
typedef unsigned char BOOLEAN;
typedef unsigned int ULONG, UINT;
typedef int LONG, NTSTATUS;
typedef void VOID, *PVOID;
typedef ULONG D3DKMT_HANDLE, D3DDDI_VIDEO_PRESENT_SOURCE_ID;
#define TRUE 1
#define FALSE 0
#define NULL ((void *)0)
#define NTAPI
#define CONST const
#define _In_
#define PAGED_CODE()
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_DEVICE_REMOVED ((NTSTATUS)0xC00002B6)
#define STATUS_GRAPHICS_PRESENT_OCCLUDED ((NTSTATUS)0x401E0006)
#define STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE ((NTSTATUS)0xC01E0302)
#define DXGKP_MAX_SOURCES 16
#define Executive 0
#define KernelMode 0
static int Failures, MutexDepth;
#define ASSERT(x) do { if (!(x)) { printf("assertion failed: %s\n", #x); Failures++; } } while (0)
typedef enum { DxgkAdapterStateStarted = 1, DxgkAdapterStateStopped } DXGK_ADAPTER_STATE;
typedef enum { D3DKMT_VIDPNSOURCEOWNER_UNOWNED, D3DKMT_VIDPNSOURCEOWNER_SHARED, D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE,
               D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI } D3DKMT_VIDPNSOURCEOWNER_TYPE;
typedef struct { int Mutex; DXGK_ADAPTER_STATE State; ULONG NumberOfVideoPresentSources; BOOLEAN VidPnCommitted;
                 BOOLEAN HeadlessDesktop; int VidPnMutex; } DXGKRNL_ADAPTER, *PDXGKRNL_ADAPTER;
typedef struct { PVOID OwnerDevice; D3DKMT_VIDPNSOURCEOWNER_TYPE OwnerType; } DXGKP_SOURCE_OWNER;
typedef struct { DXGKP_SOURCE_OWNER Owners[DXGKP_MAX_SOURCES]; } DXGKP_SOURCE_OWNER_ADAPTER_STATE, *PDXGKP_SOURCE_OWNER_ADAPTER_STATE;
typedef struct { D3DKMT_HANDLE hAdapter; D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; } D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP;
static DXGKRNL_ADAPTER TheAdapter;
static DXGKP_SOURCE_OWNER_ADAPTER_STATE TheOwners;
static int g_SourceOwnerMutex;
static PDXGKRNL_ADAPTER DxgkLookupAdapterByHandle(D3DKMT_HANDLE h) { return h == 1 ? &TheAdapter : NULL; }
static void DxgkDereferenceAdapter(PDXGKRNL_ADAPTER a) { (void)a; }
static void DxgkpEnsureSourceOwnerMutex(void) {}
static void ExAcquireFastMutex(int *m) { (void)m; }
static void ExReleaseFastMutex(int *m) { (void)m; }
static PDXGKP_SOURCE_OWNER_ADAPTER_STATE DxgkpFindSourceOwnerAdapterLocked(PDXGKRNL_ADAPTER a) { (void)a; return &TheOwners; }
static NTSTATUS KeWaitForSingleObject(int *m, int r, int mode, BOOLEAN a, PVOID t) { (void)m; (void)r; (void)mode; (void)a; (void)t; MutexDepth++; return 0; }
static LONG KeReleaseMutex(int *m, BOOLEAN w) { (void)m; (void)w; MutexDepth--; return 0; }
'''

harness = r'''
static void Check(int Condition, const char *What)
{
    if (!Condition)
    {
        printf("FAIL %s\n", What);
        Failures++;
    }
}

int main(void)
{
    D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP Check0 = { 1, 0 }, Check9 = { 1, 9 };

    TheAdapter.State = DxgkAdapterStateStarted;
    TheAdapter.NumberOfVideoPresentSources = 2;
    TheAdapter.VidPnCommitted = TRUE;
    Check(DxgkCheckVidPnExclusiveOwnership(&Check0) == STATUS_SUCCESS, "a source scanned out and unowned is free");

    TheAdapter.VidPnCommitted = FALSE;
    TheAdapter.HeadlessDesktop = TRUE;
    Check(DxgkCheckVidPnExclusiveOwnership(&Check0) == STATUS_GRAPHICS_PRESENT_OCCLUDED,
          "the source of a detached desktop is occluded, as its flips are");

    TheAdapter.VidPnCommitted = TRUE;
    TheAdapter.HeadlessDesktop = FALSE;
    TheOwners.Owners[0].OwnerDevice = &TheOwners;
    TheOwners.Owners[0].OwnerType = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
    Check(DxgkCheckVidPnExclusiveOwnership(&Check0) == STATUS_GRAPHICS_PRESENT_OCCLUDED, "an exclusively owned source is occluded");

    Check(DxgkCheckVidPnExclusiveOwnership(&Check9) == STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE, "a source past the adapter's is invalid");
    Check(MutexDepth == 0, "the VidPN mutex is released on every path");

    printf("%s: %d failure(s)\n", Failures ? "FAIL" : "PASS", Failures);
    return Failures != 0;
}
'''

program = fixture
program += extract('static BOOLEAN', 'DxgkpDesktopDetached', required=False)
program += extract('NTSTATUS\nNTAPI', 'DxgkCheckVidPnExclusiveOwnership')
program += harness
with tempfile.TemporaryDirectory() as temp:
    path = pathlib.Path(temp) / 'test.c'
    binary = pathlib.Path(temp) / 'test'
    path.write_text(program)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wno-unused-function', '-Wno-unused-label', '-o', str(binary), str(path)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
