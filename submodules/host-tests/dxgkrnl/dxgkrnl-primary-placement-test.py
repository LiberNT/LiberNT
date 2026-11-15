#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A primary allocation follows the miniport's segment preference; without one its write segments must be CPU-visible
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
source = (pathlib.Path(args.root) / 'drivers/directx/dxgkrnl/vidmm.c').read_text()


def extract(prefix, name):
    match = re.search(r'(?ms)^' + prefix + r'\n' + name + r'\(.*?^\}\n', source)
    if not match:
        raise RuntimeError(name + ' not found in vidmm.c')
    return match.group(0) + '\n'


fixture = r'''
#include <stdio.h>
typedef unsigned char BOOLEAN;
typedef unsigned int ULONG, UINT;
typedef void *PVOID;
#define TRUE 1
#define FALSE 0
#define NULL ((void *)0)
#define _In_
#define CONST const
#define FORCEINLINE static inline
typedef struct { struct { UINT Aperture : 1; UINT Agp : 1; UINT CpuVisible : 1; } Flags; BOOLEAN HostAperture; } DXGKRNL_SEGMENT, *PDXGKRNL_SEGMENT;
typedef struct { PVOID Segments; ULONG SegmentCount; } DXGKRNL_ADAPTER, *PDXGKRNL_ADAPTER;
typedef struct { union { struct { UINT PermanentSysMem : 1; UINT Cached : 1; UINT Protected : 1; UINT ExistingSysMem : 1;
                                   UINT ExistingKernelSysMem : 1; }; UINT Value; } Flags;
                 union { UINT Value; } PreferredSegment; UINT SupportedWriteSegmentSet; } DXGK_ALLOCATIONINFO;
#define ADAPTER_SEGMENTS(Adapter) ((PDXGKRNL_SEGMENT)(Adapter)->Segments)
'''

harness = r'''
static int Failures;

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
    /* The Ryzen 3500U layout: system memory, VRAM the CPU cannot reach, two apertures */
    DXGKRNL_SEGMENT Segments[4] = { { { 0, 0, 1 }, 0 }, { { 0, 0, 0 }, 0 }, { { 1, 0, 1 }, 0 }, { { 1, 0, 1 }, 0 } };
    DXGKRNL_ADAPTER Adapter = { Segments, 4 };
    DXGK_ALLOCATIONINFO Info = { { { 0 } }, { 0 }, 0 };

    Info.SupportedWriteSegmentSet = 0x3;
    Info.PreferredSegment.Value = 0x2000042;
    Check(DxgkpVidMmPrimaryPlacementValid(&Adapter, &Info), "a primary preferring VRAM the CPU cannot reach is placed");

    Info.PreferredSegment.Value = 0;
    Check(!DxgkpVidMmPrimaryPlacementValid(&Adapter, &Info), "without a preference every write segment must be CPU-visible");

    Info.SupportedWriteSegmentSet = 0x5;
    Check(DxgkpVidMmPrimaryPlacementValid(&Adapter, &Info), "CPU-visible and aperture write segments suffice");

    Info.PreferredSegment.Value = 0x2;
    Info.Flags.Cached = 1;
    Check(!DxgkpVidMmPrimaryPlacementValid(&Adapter, &Info), "a cached primary is refused");

    printf("%s: %d failure(s)\n", Failures ? "FAIL" : "PASS", Failures);
    return Failures != 0;
}
'''

program = fixture
program += extract('FORCEINLINE\nBOOLEAN', 'VidMmSegmentIsAperture')
program += extract('FORCEINLINE\nBOOLEAN', 'VidMmSegmentIsCpuVisible')
program += extract('static BOOLEAN', 'DxgkpVidMmPrimarySegmentsCpuVisible')
program += extract('static BOOLEAN', 'DxgkpVidMmPrimaryPlacementValid')
program += harness
with tempfile.TemporaryDirectory() as temp:
    path = pathlib.Path(temp) / 'test.c'
    binary = pathlib.Path(temp) / 'test'
    path.write_text(program)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wno-unused-function', '-o', str(binary), str(path)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
