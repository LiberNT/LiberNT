#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Exercise pending receive interrupts across masking and rearming on the 82540EM
# COPYRIGHT:   Copyright 2026 The LiberNT contributors

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
driver = args.root.resolve() / 'drivers/network/dd/e1000e'
source = (driver / 'interrupt_ndis6.c').read_text()
functions = []
for name in ('E1000DisableInterrupts', 'E1000EnableInterrupts'):
    match = re.search(r'^VOID\n' + name + r'\(.*?^\}', source, re.M | re.S)
    if not match:
        raise ValueError(f'Cannot locate {name}')
    functions.append(match.group())

fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef uint8_t UCHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG;
typedef uint64_t UINT64;
#define C_ASSERT(expr) _Static_assert(expr, #expr)
#include "e1000hw.h"

#define VOID void
#define _In_
enum { E1000InterruptModeLegacy, E1000InterruptModeMsi, E1000InterruptModeMsix };
#define E1000_FLAG_INTERRUPT_ENABLED 0x40
#define InterlockedAnd(p, v) (*(p) &= (v))
#define InterlockedOr(p, v) (*(p) |= (v))

typedef struct {
    void *IoBase;
    ULONG InterruptMask, Flags, InterruptMode, IsPCIe, RxQueueCount;
    struct { ULONG VectorCount; } MsixInfo;
} ADAPTER, *PE1000_ADAPTER;

static ULONG Cause, Mask;

static ULONG ReadReg(ULONG Reg)
{
    if (Reg == E1000_REG_STATUS)
        return 0;
    assert(Reg == E1000_REG_ICR);
    ULONG Pending = Cause;
    Cause = 0;
    return Pending;
}

static void WriteReg(ULONG Reg, ULONG Value)
{
    if (Reg == E1000_REG_IMC)
        Mask &= ~Value;
    else {
        assert(Reg == E1000_REG_IMS);
        Mask |= Value;
    }
}

#define E1000_READ_REG(a, r) ReadReg(r)
#define E1000_WRITE_REG(a, r, v) WriteReg(r, v)
''' + '\n'.join(functions) + r'''

static int Check(const char *Name, ULONG BeforeMask, ULONG WhileMasked)
{
    ADAPTER Adapter = {0};
    ULONG Expected = BeforeMask | WhileMasked;
    Adapter.IoBase = &Adapter;
    Cause = BeforeMask;
    Mask = E1000_IMS_RXT0;
    E1000DisableInterrupts(&Adapter);
    assert(Mask == 0);
    Cause |= WhileMasked;
    E1000EnableInterrupts(&Adapter);
    ULONG Asserted = Cause & Mask;
    ULONG Acknowledged = ReadReg(E1000_REG_ICR);
    if (!(Mask & E1000_IMS_RXT0) || Asserted != Expected ||
        Acknowledged != Expected || (Cause & Mask) != 0) {
        fprintf(stderr, "%s: expected %#x, asserted %#x, acknowledged %#x\n",
                Name, Expected, Asserted, Acknowledged);
        return 1;
    }
    return 0;
}

int main(void)
{
    int Failures = Check("no work", 0, 0);
    Failures += Check("receive before mask", E1000_IMS_RXT0, 0);
    Failures += Check("receive after drain", 0, E1000_IMS_RXT0);
    return Failures != 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp)
    test = path / 'interrupt-test.c'
    binary = path / 'interrupt-test'
    test.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-I', str(driver),
                    str(test), '-o', str(binary)], check=True)
    raise SystemExit(subprocess.run([str(binary)]).returncode)
