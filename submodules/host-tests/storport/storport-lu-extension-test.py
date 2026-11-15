#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Storport gives each logical unit its miniport storage area through StorPortGetLogicalUnit
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
port = pathlib.Path(args.root) / 'drivers/storage/port/storport'


def extract(path, name):
    source = (port / path).read_text()
    match = re.search(r'(?ms)^(?:\w+\n)+' + name + r'\(\n.*?^\}\n', source)
    if not match:
        raise RuntimeError(name + ' not found in ' + path)
    return match.group(0) + '\n'


entry = re.search(r'(?ms)^typedef struct _PORT_LU_ENTRY\n\{.*?^\} PORT_LU_ENTRY, \*PPORT_LU_ENTRY;\n',
                  (port / 'precomp.h').read_text())
if not entry:
    raise RuntimeError('PORT_LU_ENTRY not found in precomp.h')

fixture = r'''
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char UCHAR, BOOLEAN, KIRQL;
typedef unsigned int ULONG;
typedef int LONG, NTSTATUS;
typedef void VOID, *PVOID;
#define TRUE 1
#define FALSE 0
#define NTAPI
#define STORPORT_API
#define _In_
#define _Out_
#define PASSIVE_LEVEL 0
#define NonPagedPoolNx 0
#define TAG_LU_EXTENSION 0
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define DPRINT(...) ((void)0)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define CONTAINING_RECORD(a, t, f) ((t *)((char *)(a) - offsetof(t, f)))
static int Failures, Allocations;
#define ASSERT(x) do { if (!(x)) { printf("assertion failed: %s\n", #x); Failures++; } } while (0)
static KIRQL KeGetCurrentIrql(void) { return PASSIVE_LEVEL; }
static PVOID ExAllocatePoolZero(int Type, size_t Size, ULONG Tag) { (void)Type; (void)Tag; Allocations++; return calloc(1, Size); }
static void ExFreePoolWithTag(PVOID p, ULONG Tag) { (void)Tag; Allocations--; free(p); }
static LONG ReadAcquire(volatile LONG *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static PVOID ReadPointerAcquire(PVOID volatile *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static LONG InterlockedExchange(volatile LONG *p, LONG v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
static PVOID InterlockedExchangePointer(PVOID volatile *p, PVOID v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
'''

types = r'''
typedef struct { ULONG SpecificLuExtensionSize; } PORT_CONFIGURATION_INFORMATION;
typedef struct _MINIPORT { struct _FDO_DEVICE_EXTENSION *DeviceExtension; PORT_CONFIGURATION_INFORMATION PortConfig; } MINIPORT;
typedef struct _FDO_DEVICE_EXTENSION { MINIPORT Miniport; ULONG PdoCount; PPORT_LU_ENTRY volatile LuEntries; } FDO_DEVICE_EXTENSION, *PFDO_DEVICE_EXTENSION;
typedef struct _MINIPORT_DEVICE_EXTENSION { struct _MINIPORT *Miniport; UCHAR HwDeviceExtension[0]; } MINIPORT_DEVICE_EXTENSION, *PMINIPORT_DEVICE_EXTENSION;
PVOID PortFindLuExtension(PFDO_DEVICE_EXTENSION, ULONG, ULONG, ULONG);
'''

harness = r'''
static struct { MINIPORT_DEVICE_EXTENSION Header; UCHAR Hw[32]; } Miniport;

static void Check(int Condition, const char *What)
{
    if (!Condition)
    {
        printf("FAIL %s\n", What);
        Failures++;
    }
}

static void Release(PPORT_LU_ENTRY Entry)
{
    if (Entry != NULL)
        Check(InterlockedExchange(&Entry->Present, 0) == 1, "released unit was present");
}

int main(void)
{
    FDO_DEVICE_EXTENSION Fdo;
    PVOID Hw = Miniport.Header.HwDeviceExtension;
    PPORT_LU_ENTRY First = NULL, Second = NULL, Again = NULL;
    UCHAR *Area;
    size_t Index;

    memset(&Fdo, 0, sizeof(Fdo));
    Fdo.Miniport.DeviceExtension = &Fdo;
    Miniport.Header.Miniport = &Fdo.Miniport;

    Check(PortAcquireLuEntry(&Fdo, 0, 1, 0, &First) == STATUS_SUCCESS && First == NULL, "no area without a size");
    Check(StorPortGetLogicalUnit(Hw, 0, 1, 0) == NULL, "no area without a size is reported");

    Fdo.Miniport.PortConfig.SpecificLuExtensionSize = 64;
    Check(StorPortGetLogicalUnit(Hw, 0, 1, 0) == NULL, "unknown unit has no area");
    Check(PortAcquireLuEntry(&Fdo, 0, 1, 0, &First) == STATUS_SUCCESS && First != NULL, "first unit gets an entry");
    Check(PortAcquireLuEntry(&Fdo, 0, 2, 0, &Second) == STATUS_SUCCESS && Second != NULL, "second unit gets an entry");
    Area = StorPortGetLogicalUnit(Hw, 0, 1, 0);
    Check(Area != NULL && ((size_t)Area % sizeof(void *)) == 0, "present unit has an aligned area");
    for (Index = 0; Area != NULL && Index < 64; Index++)
        Check(Area[Index] == 0, "new area is zeroed");
    if (Area != NULL)
        memset(Area, 0xA5, 64);
    Check(StorPortGetLogicalUnit(Hw, 0, 2, 0) != NULL && StorPortGetLogicalUnit(Hw, 0, 2, 0) != Area, "units have separate areas");
    Check(StorPortGetLogicalUnit(Hw, 0, 1, 1) == NULL && StorPortGetLogicalUnit(Hw, 1, 1, 0) == NULL, "address must match");

    Release(First);
    Check(StorPortGetLogicalUnit(Hw, 0, 1, 0) == NULL, "removed unit has no area");
    Check(PortAcquireLuEntry(&Fdo, 0, 1, 0, &Again) == STATUS_SUCCESS && Again == First, "returning unit reuses its entry");
    Check(StorPortGetLogicalUnit(Hw, 0, 1, 0) == Area, "returning unit keeps its area address");
    for (Index = 0; Area != NULL && Index < 64; Index++)
        Check(Area[Index] == 0, "returning unit's area is zeroed");

    Release(Again);
    Release(Second);
    PortFreeLuEntries(&Fdo);
    Check(Allocations == 0, "teardown frees every entry");
    Check(StorPortGetLogicalUnit(Hw, 0, 2, 0) == NULL, "no area after teardown");
    printf("%s: %d failure(s)\n", Failures ? "FAIL" : "PASS", Failures);
    return Failures != 0;
}
'''

program = fixture + entry.group(0) + types
program += extract('pdo.c', 'PortAcquireLuEntry')
program += extract('pdo.c', 'PortFindLuExtension')
program += extract('pdo.c', 'PortFreeLuEntries')
program += extract('storport.c', 'StorPortGetLogicalUnit')
program += harness
with tempfile.TemporaryDirectory() as temp:
    source = pathlib.Path(temp) / 'test.c'
    binary = pathlib.Path(temp) / 'test'
    source.write_text(program)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wno-unused-function', '-o', str(binary), str(source)],
                   check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
