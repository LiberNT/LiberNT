#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The NVMe miniport uses every namespace whose data needs no metadata buffer from the host
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
driver = pathlib.Path(args.root) / 'drivers/storage/port/stornvme'
header = (driver / 'nvme.h').read_text()
core = (driver / 'core.c').read_text()


def find(pattern, text, what):
    match = re.search(pattern, text)
    if not match:
        raise RuntimeError(what + ' not found')
    return match.group(0) + '\n'


fixture = r'''
#include <stdio.h>
#include <string.h>
typedef unsigned char UCHAR, BOOLEAN, *PUCHAR;
typedef unsigned short USHORT;
typedef unsigned int ULONG;
typedef unsigned long long ULONGLONG;
#define TRUE 1
#define FALSE 0
#define _In_
#define _Out_
'''

program = fixture
for name in ('NVME_FLBAS_FORMAT_MASK', 'NVME_DPS_PI_TYPE_MASK', 'NVME_PI_TYPE3', 'NVME_PI_SIZE'):
    program += find(r'(?m)^#define ' + name + r'\s+\S+$', header, name)
program += find(r'(?ms)^typedef struct _NVME_LBA_FORMAT\n\{.*?^\} NVME_LBA_FORMAT, \*PNVME_LBA_FORMAT;', header, 'NVME_LBA_FORMAT')
program += find(r'(?ms)^typedef struct _NVME_IDENTIFY_NAMESPACE\n\{.*?^\} NVME_IDENTIFY_NAMESPACE, \*PNVME_IDENTIFY_NAMESPACE;',
                header, 'NVME_IDENTIFY_NAMESPACE')
program += find(r'(?ms)^static\nBOOLEAN\nNvmeNamespaceFormatUsable\(.*?^\}', core, 'NvmeNamespaceFormatUsable')
program += r'''
static int Failures;

static void Expect(UCHAR Nlbaf, UCHAR Flbas, USHORT Ms, UCHAR Dps, BOOLEAN Usable, UCHAR Type, const char *What)
{
    static NVME_IDENTIFY_NAMESPACE Identify;
    UCHAR ProtectionType = 0xFF;
    BOOLEAN Result;

    memset(&Identify, 0, sizeof(Identify));
    Identify.NSZE = 1000000;
    Identify.NLBAF = Nlbaf;
    Identify.FLBAS = Flbas;
    Identify.DPS = Dps;
    Identify.LBAF[Flbas & 0xF].MS = Ms;
    Identify.LBAF[Flbas & 0xF].LBADS = 9;
    Result = NvmeNamespaceFormatUsable(&Identify, &ProtectionType);
    if (Result != Usable || ProtectionType != Type)
    {
        printf("FAIL %s: usable %u protection type %u, expected %u %u\n", What, Result, ProtectionType, Usable, Type);
        Failures++;
    }
}

int main(void)
{
    Expect(0, 0x00, 0, 0x0, TRUE, 0, "plain 512-byte format");
    Expect(1, 0x01, 0, 0x0, TRUE, 0, "second plain format");
    Expect(0, 0x00, 0, 0x1, TRUE, 0, "protection type set without metadata");
    Expect(0, 0x00, 0, 0x9, TRUE, 0, "protection type and location set without metadata");
    Expect(0, 0x10, 0, 0x0, TRUE, 0, "extended LBA flag without metadata");
    Expect(0, 0x00, 8, 0x1, TRUE, 1, "type 1 protection in 8 bytes of metadata");
    Expect(0, 0x00, 8, 0x2, TRUE, 2, "type 2 protection in 8 bytes of metadata");
    Expect(0, 0x00, 8, 0x3, TRUE, 3, "type 3 protection in 8 bytes of metadata");
    Expect(0, 0x00, 8, 0x9, TRUE, 1, "type 1 protection at the start of the metadata");
    Expect(0, 0x10, 8, 0x1, TRUE, 1, "type 1 protection in an extended LBA");
    Expect(0, 0x00, 8, 0x0, FALSE, 0, "8 bytes of metadata without protection");
    Expect(0, 0x00, 8, 0x4, FALSE, 0, "reserved protection type");
    Expect(0, 0x00, 16, 0x1, FALSE, 0, "protection inside a larger metadata area");
    Expect(0, 0x00, 64, 0x0, FALSE, 0, "plain metadata");
    Expect(0, 0x01, 0, 0x0, FALSE, 0, "format index past the format count");
    Expect(2, 0x03, 0, 0x0, FALSE, 0, "format index one past the last format");
    printf("%s: %d failure(s)\n", Failures ? "FAIL" : "PASS", Failures);
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as temp:
    source = pathlib.Path(temp) / 'test.c'
    binary = pathlib.Path(temp) / 'test'
    source.write_text(program)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wno-unused-function', '-o', str(binary), str(source)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
