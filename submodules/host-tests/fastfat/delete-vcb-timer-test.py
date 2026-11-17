# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Deleting a FAT volume waits for a clean-volume DPC that is already running and leaves no timer armed
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--source', help='strucsup.c to test instead of the one under --root')
args = parser.parse_args()
source = pathlib.Path(args.source or pathlib.Path(args.root) / 'drivers/filesystems/fastfat/strucsup.c').read_text(errors='replace')

# The part of FatDeleteVcb that retires the clean-volume timer, up to the first thing it frees afterwards
match = re.search(r'(?ms)^    \(VOID\)KeCancelTimer\( &Vcb->CleanVolumeTimer \);\n.*?^    ExFreePool\( Vcb->Statistics \);\n', source)
assert match, 'the clean-volume timer teardown was not found'

fixture = r'''
#include <stdio.h>
#include <string.h>
#define VOID void
#define TRUE 1
#define FALSE 0
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); Failures++; } } while (0)
typedef unsigned char BOOLEAN;
typedef void *PVOID;
typedef struct { int Unused; } KTIMER, KDPC;
typedef struct { KTIMER CleanVolumeTimer; KDPC CleanVolumeDpc; PVOID Statistics; } VCB, *PVCB;

static int Failures, Flushes, FreedUnderDpc, Freed;
/* The DPC was taken off its queue on another processor and has not run yet; it will find dirty data */
static int DpcInFlight, TimerArmed;

static BOOLEAN KeCancelTimer(KTIMER *Timer)
{
    BOOLEAN Was = (BOOLEAN)TimerArmed;
    (void)Timer;
    TimerArmed = 0;
    return Was;
}
static BOOLEAN KeRemoveQueueDpc(KDPC *Dpc) { (void)Dpc; return FALSE; }
static void KeFlushQueuedDpcs(void)
{
    Flushes++;
    if (DpcInFlight)
    {
        /* FatCleanVolumeDpc runs to its end: there is dirty data, so it sets the timer again */
        DpcInFlight = 0;
        TimerArmed = 1;
    }
}
static void ExFreePool(PVOID Block)
{
    (void)Block;
    Freed++;
    if (DpcInFlight)
        FreedUnderDpc++;
}

static void
RetireCleanVolumeTimer(PVCB Vcb)
{
''' + match.group(0) + r'''
}

int main(void)
{
    static char Statistics[16];
    VCB Vcb;

    memset(&Vcb, 0, sizeof(Vcb));
    Vcb.Statistics = Statistics;

    DpcInFlight = 1;
    RetireCleanVolumeTimer(&Vcb);
    CHECK(Freed == 1);
    CHECK(FreedUnderDpc == 0);
    CHECK(!DpcInFlight);
    /* Nothing is left that would fire on a volume that is gone */
    CHECK(!TimerArmed);

    if (!Failures)
        printf("fastfat delete vcb: the clean-volume DPC is waited for and its timer is left unset\n");
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'vcb.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wno-unused-function', str(path / 'vcb.c'), '-o', str(path / 'vcb')], check=True)
    subprocess.run([str(path / 'vcb')], check=True)
