#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The paging process gets its address space before the paging engine can run a buffer
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
directory = pathlib.Path(args.root) / 'drivers/directx/dxgkrnl'
adapter = (directory / 'adapter.c').read_text()
paging = (directory / 'paging.c').read_text()
core = (directory / 'gpuva_core.c').read_text()
header = (directory / 'gpuva_core.h').read_text()
failures = 0


def check(condition, what):
    global failures
    if not condition:
        print('FAIL ' + what)
        failures += 1


def extract(source, prefix, name):
    match = re.search(r'(?ms)^' + prefix + r'\n' + name + r'\(.*?^\}\n', source)
    if not match:
        raise RuntimeError(name + ' not found')
    return match.group(0) + '\n'


start = extract(adapter, 'NTSTATUS', 'DxgkAdapterStart')
complete = extract(adapter, 'static VOID', 'DxgkpCompleteAdapterStart')
bringup = 'DxgkGpuVaInitializePageTablePool(Adapter)'
gate = 'InterlockedExchange(&Adapter->SubmitDmaStopping, 0);'
check(start.count(bringup) == 1, 'adapter start brings the paging process address space up once')
check(start.count(gate) == 1, 'adapter start opens tracked submission once')
check(bringup not in complete, 'the address space is not left to the end of adapter start')
if start.count(bringup) == 1 and start.count(gate) == 1:
    position = start.index(bringup)
    check(start.index('DxgkCreatePagingSystemContext(Adapter)') < position, 'the paging context exists first')
    check(start.index(gate) < position, 'the bring-up runs once the scheduler takes packets')
    between = start[start.index(gate):position]
    check(re.fullmatch(r'[^;]*;\s*InterlockedExchange\(&Adapter->VidSchStopping, 0\);\s*\(VOID\)', between) is not None,
          'nothing else runs between opening the scheduler and the bring-up')
    check(position < start.index('DxgkPresentResume(Adapter)'), 'presents resume after the bring-up')
    check(position < start.index('DxgkDisplayRegister(Adapter)'), 'the display registers after the bring-up')

submits = [match.start() for match in re.finditer(r'VidSchSubmitCommandTracked\(', paging)]
check(len(submits) == 3, 'three paging submit sites')
for position in submits:
    line = paging.rfind('\n', 0, paging.rfind('\n', 0, position)) + 1
    check('ASSERT(!Adapter->GpuMmuCapsValid || DxgkGpuVaCorePagingBufferReachable(' in paging[line:position],
          'a paging submit asserts its buffer is reachable')

fixture = r'''
#include <stdio.h>
typedef unsigned char BOOLEAN;
typedef unsigned int ULONG;
#define TRUE 1
#define FALSE 0
#define _In_
'''
fixture += ''.join(line + '\n' for line in header.splitlines() if re.match(r'#define DXGK_GPUVA_CORE_UPDATE_\w+\s+\d+$', line))

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
    Check(DxgkGpuVaCorePagingBufferReachable(DXGK_GPUVA_CORE_UPDATE_CPU_VIRTUAL, FALSE, FALSE), "CPU-written tables place no demand on the paging buffer");
    Check(DxgkGpuVaCorePagingBufferReachable(DXGK_GPUVA_CORE_UPDATE_GPU_VIRTUAL, FALSE, FALSE), "GPU_VIRTUAL tables can still be written with the CPU");
    Check(!DxgkGpuVaCorePagingBufferReachable(DXGK_GPUVA_CORE_UPDATE_GPU_PHYSICAL, FALSE, FALSE), "a GPU_PHYSICAL adapter cannot run a paging buffer without an address in the paging process");
    Check(DxgkGpuVaCorePagingBufferReachable(DXGK_GPUVA_CORE_UPDATE_GPU_PHYSICAL, TRUE, FALSE), "the paging process's own initialization is completed with the CPU");
    Check(DxgkGpuVaCorePagingBufferReachable(DXGK_GPUVA_CORE_UPDATE_GPU_PHYSICAL, FALSE, TRUE), "a buffer mapped in the paging process runs there");
    return Failures != 0;
}
'''

program = fixture + extract(core, 'BOOLEAN', 'DxgkGpuVaCorePagingBufferReachable') + harness
with tempfile.TemporaryDirectory() as temp:
    path = pathlib.Path(temp) / 'test.c'
    binary = pathlib.Path(temp) / 'test'
    path.write_text(program)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wno-unused-function', '-o', str(binary), str(path)], check=True)
    failures += subprocess.run([str(binary)]).returncode
print('%s: %d failure(s)' % ('FAIL' if failures else 'PASS', failures))
sys.exit(failures != 0)
