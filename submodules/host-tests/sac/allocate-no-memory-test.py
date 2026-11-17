# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The console's allocator reports a failed allocation instead of clearing memory it did not get
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--source', help='memory.c to test instead of the one under --root')
args = parser.parse_args()
source = pathlib.Path(args.source or pathlib.Path(args.root) / 'drivers/sac/driver/memory.c').read_text()

match = re.search(r'(?ms)^PVOID\nNTAPI\nMyAllocatePool\(.*?^\}\n', source)
assert match, 'MyAllocatePool not found'

fixture = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#define NTAPI
#define IN
#define NonPagedPool 0
#define SAC_DBG(Level, ...)
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); Failures++; } } while (0)
typedef void *PVOID;
typedef char *PCHAR;
typedef unsigned int ULONG;
typedef size_t SIZE_T;

static int Failures, FailAllocation;

static PVOID ExAllocatePoolWithTag(int Type, SIZE_T Size, ULONG Tag)
{
    char *p;
    (void)Type; (void)Tag;
    if (FailAllocation)
        return NULL;
    p = malloc(Size);
    memset(p, 0xA5, Size);
    return p;
}

''' + match.group(0) + r'''
static sigjmp_buf Crashed;
static void OnFault(int Signal) { (void)Signal; siglongjmp(Crashed, 1); }

int main(void)
{
    static const char Zero[64];
    volatile int Faulted = 0;
    PVOID Block = (PVOID)1;

    signal(SIGSEGV, OnFault);

    /* Its callers test for NULL: that is what a failed allocation has to give them */
    FailAllocation = 1;
    if (!sigsetjmp(Crashed, 1))
        Block = MyAllocatePool(64, 'tseT', __FILE__, __LINE__);
    else
        Faulted = 1;
    CHECK(!Faulted);
    CHECK(Block == NULL);

    /* A block that was obtained is handed over cleared */
    FailAllocation = 0;
    Block = MyAllocatePool(64, 'tseT', __FILE__, __LINE__);
    CHECK(Block != NULL && memcmp(Block, Zero, 64) == 0);

    if (!Failures)
        printf("sac allocator: a block is returned cleared, a failed allocation as NULL\n");
    return Failures != 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'memory.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wno-unused-function', '-Wno-multichar', '-Wno-nonnull',
                    str(path / 'memory.c'), '-o', str(path / 'memory')], check=True)
    subprocess.run([str(path / 'memory')], check=True)
