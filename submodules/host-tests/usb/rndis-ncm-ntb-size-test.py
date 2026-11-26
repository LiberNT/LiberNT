#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The CDC-NCM receive NTB size is never below 2048 bytes nor above the receive buffer
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
root = pathlib.Path(args.root) / 'drivers/usb/usbrndis'

source = (root / 'rndisusb.c').read_text()
function = re.search(r'(?ms)^static\nULONG\nRndisNcmRxNtbSize\(.*?^\}\n', source)
if not function:
    raise RuntimeError('RndisNcmRxNtbSize not found in rndisusb.c')

header = (root / 'usbrndis.h').read_text()
constants = ''
for name in ('NCM_NTB_MIN_IN_SIZE', 'NCM_RX_BUFFER_SIZE'):
    match = re.search(r'(?m)^#define ' + name + r'\s+(\S+)', header)
    if not match:
        raise RuntimeError(name + ' not found in usbrndis.h')
    constants += f'#define {name} {match.group(1)}\n'

fixture = constants + r'''
#include <assert.h>
#include <stdio.h>
typedef unsigned int ULONG;
#define _In_
#define ASSERT assert
''' + function.group(0) + r'''
static int Failures;

static void Expect(const char *Case, ULONG DeviceMax, ULONG Buffer, ULONG Expected)
{
    ULONG Size = RndisNcmRxNtbSize(DeviceMax, Buffer);
    if (Size != Expected)
    {
        printf("%s: device %u, buffer %u: got %u, expected %u\n", Case, DeviceMax, Buffer, Size, Expected);
        Failures++;
    }
}

int main(void)
{
    if (NCM_NTB_MIN_IN_SIZE != 2048)
    {
        printf("NCM_NTB_MIN_IN_SIZE is %u, the CDC-NCM minimum is 2048\n", (ULONG)NCM_NTB_MIN_IN_SIZE);
        Failures++;
    }
    if (NCM_RX_BUFFER_SIZE < 65535)
    {
        printf("NCM_RX_BUFFER_SIZE %u cannot hold an NTB16 of 65535 bytes\n", (ULONG)NCM_RX_BUFFER_SIZE);
        Failures++;
    }
    Expect("Pixel 6a (dwNtbInMaxSize 16384)", 16384, NCM_RX_BUFFER_SIZE, 16384);
    Expect("device maximum equal to the buffer", NCM_RX_BUFFER_SIZE, NCM_RX_BUFFER_SIZE, NCM_RX_BUFFER_SIZE);
    Expect("device maximum above the buffer", 131072, NCM_RX_BUFFER_SIZE, NCM_RX_BUFFER_SIZE);
    Expect("device maximum at the minimum", 2048, NCM_RX_BUFFER_SIZE, 2048);
    Expect("device maximum below the minimum", 1590, NCM_RX_BUFFER_SIZE, 2048);
    Expect("device maximum zero", 0, NCM_RX_BUFFER_SIZE, 2048);
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as temp:
    c_file = pathlib.Path(temp) / 'ncm_ntb_size.c'
    exe = pathlib.Path(temp) / 'ncm_ntb_size'
    c_file.write_text(fixture)
    subprocess.run([args.cc, '-std=c99', '-Wall', '-Werror', str(c_file), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.exit(result.returncode)
