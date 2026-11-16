# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A stopped machine reads PCI configuration through the HAL's non-waiting path, never the locked one
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
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

source = (root / 'ntos/kd64/kdbus.c').read_text()
functions = source[source.index('NTSTATUS\nNTAPI\nKdpSysReadBusData('):source.index('/* EOF */')]
ntstatus = (root / 'sdk/include/psdk/ntstatus.h').read_text()
statuses = '\n'.join(re.search(r'^#define %s\s+.*$' % name, ntstatus, re.M).group(0)
                     for name in ('STATUS_SUCCESS', 'STATUS_UNSUCCESSFUL', 'STATUS_DEVICE_BUSY'))
iotypes = (root / 'sdk/include/xdk/iotypes.h').read_text()
bus_enum = re.search(r'typedef enum _BUS_DATA_TYPE \{.*?\} BUS_DATA_TYPE, \*PBUS_DATA_TYPE;', iotypes, re.S).group(0)

fixture = r'''
#include <stdio.h>
typedef int NTSTATUS;
typedef unsigned int ULONG, *PULONG;
typedef unsigned char BOOLEAN;
typedef void *PVOID;
#define NTAPI
#define MAXULONG 0xFFFFFFFFu
#define _In_
#define _Out_
#define _Out_writes_bytes_(x)
#define _In_reads_bytes_(x)
@STATUSES@
@BUSENUM@
static BOOLEAN KdEnteredDebugger;
static ULONG HelperResult, HalResult, HelperCalls, HalGetCalls, HalSetCalls;
static ULONG __attribute__((unused)) HelperResultFor(void) { HelperCalls++; return HelperResult; }
#define HalpKdReadPciConfig(Bus, Slot, Buffer, Offset, Length) HelperResultFor()
static ULONG HalGetBusDataByOffset(BUS_DATA_TYPE Type, ULONG Bus, ULONG Slot, PVOID Buffer, ULONG Offset, ULONG Length)
{ (void)Type; (void)Bus; (void)Slot; (void)Buffer; (void)Offset; (void)Length; HalGetCalls++; return HalResult; }
static ULONG HalSetBusDataByOffset(BUS_DATA_TYPE Type, ULONG Bus, ULONG Slot, PVOID Buffer, ULONG Offset, ULONG Length)
{ (void)Type; (void)Bus; (void)Slot; (void)Buffer; (void)Offset; (void)Length; HalSetCalls++; return HalResult; }
@FUNCTIONS@
static int Failures;
static void Read(const char *Name, BOOLEAN Entered, BUS_DATA_TYPE Type, ULONG Helper, ULONG Hal,
                 NTSTATUS Status, ULONG Length, ULONG HelperCount, ULONG HalCount)
{
    unsigned char Buffer[256];
    ULONG Actual = 0x55;
    NTSTATUS Result;
    KdEnteredDebugger = Entered; HelperResult = Helper; HalResult = Hal; HelperCalls = HalGetCalls = 0;
    Result = KdpSysReadBusData(Type, 0, 3, 0, Buffer, sizeof(Buffer), &Actual);
    if (Result != Status || Actual != Length || HelperCalls != HelperCount || HalGetCalls != HalCount)
    {
        printf("FAIL %s: status %#x length %u helper %u hal %u\n", Name, (unsigned)Result, Actual, HelperCalls, HalGetCalls);
        Failures++;
    }
}
int main(void)
{
    ULONG Actual = 0;
    unsigned char Buffer[4];

    Read("stopped, PCI lock held", 1, PCIConfiguration, MAXULONG, 64, STATUS_DEVICE_BUSY, 0, 1, 0);
    Read("stopped, PCI read", 1, PCIConfiguration, 64, 0, STATUS_SUCCESS, 64, 1, 0);
    Read("stopped, PCI slot empty", 1, PCIConfiguration, 0, 64, STATUS_UNSUCCESSFUL, 0, 1, 0);
    Read("stopped, CMOS", 1, Cmos, MAXULONG, 16, STATUS_SUCCESS, 16, 0, 1);
    Read("running, PCI", 0, PCIConfiguration, MAXULONG, 32, STATUS_SUCCESS, 32, 0, 1);
    Read("running, PCI slot empty", 0, PCIConfiguration, 64, 0, STATUS_UNSUCCESSFUL, 0, 0, 1);

    HalResult = 4; HalSetCalls = 0;
    if (KdpSysWriteBusData(PCIConfiguration, 0, 3, 0, Buffer, sizeof(Buffer), &Actual) != STATUS_SUCCESS || Actual != 4 || HalSetCalls != 1)
    {
        printf("FAIL write: length %u\n", Actual);
        Failures++;
    }
    HalResult = 0;
    if (KdpSysWriteBusData(PCIConfiguration, 0, 3, 0, Buffer, sizeof(Buffer), &Actual) != STATUS_UNSUCCESSFUL || Actual != 0)
    {
        printf("FAIL failed write: length %u\n", Actual);
        Failures++;
    }

    if (!Failures)
        printf("kd-bus-data: a stopped machine never takes the PCI configuration lock\n");
    return Failures != 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'kdbus.c').write_text(fixture.replace('@STATUSES@', statuses).replace('@BUSENUM@', bus_enum)
                                  .replace('@FUNCTIONS@', functions))
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(path / 'kdbus.c'), '-o', str(path / 'kdbus')], check=True)
    subprocess.run([str(path / 'kdbus')], check=True)
