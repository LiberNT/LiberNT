# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A generic-call barrier synchronized twice in a row releases every processor once per call
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import os
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--source', help='dpcobj.c to test instead of the one under --root')
args = parser.parse_args()
source = pathlib.Path(args.source or pathlib.Path(args.root) / 'ntos/ke/dpcobj.c').read_text()

match = re.search(r'(?ms)^BOOLEAN\nNTAPI\nKeSignalCallDpcSynchronize\(.*?^\}\n', source)
assert match, 'KeSignalCallDpcSynchronize not found'
defines = ''.join(re.findall(r'(?m)^#define KI_REVERSE_BARRIER_\w+.*\n', source))

fixture = r'''
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define NTAPI
#define _In_
#define TRUE 1
#define FALSE 0
#define CHECK(x) do { if (!(x)) { printf("%d: %s failed\n", __LINE__, #x); exit(1); } } while (0)
#define ASSERT(x) CHECK(x)
#define YieldProcessor() sched_yield()
#define KeMemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define InterlockedDecrement(p) __atomic_sub_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedExchange(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
typedef unsigned char BOOLEAN;
typedef unsigned int ULONG;
typedef int LONG, *PLONG;
typedef void *PVOID;
typedef struct { ULONG Barrier; ULONG TotalProcessors; } DEFERRED_REVERSE_BARRIER, *PDEFERRED_REVERSE_BARRIER;
''' + defines + match.group(0) + r'''
#define ROUNDS 20000
#define CALLS (ROUNDS * 2)
static DEFERRED_REVERSE_BARRIER Barrier;
static volatile int Winners[CALLS];
static volatile int Arrived[CALLS];
static ULONG Processors;
/* Each round is what a generic call that elects one processor does: synchronize, let the winner work, synchronize again */
static void *Processor(void *Unused) {
    int Call;
    (void)Unused;
    for (Call = 0; Call < CALLS; Call++) {
        __atomic_add_fetch(&Arrived[Call], 1, __ATOMIC_SEQ_CST);
        if (KeSignalCallDpcSynchronize(&Barrier))
            __atomic_add_fetch(&Winners[Call], 1, __ATOMIC_SEQ_CST);
        /* Nobody is let through a call before everybody has reached it */
        CHECK(__atomic_load_n(&Arrived[Call], __ATOMIC_SEQ_CST) == (int)Processors);
    }
    return NULL;
}
int main(int argc, char **argv) {
    pthread_t Threads[64];
    ULONG i;
    Processors = (ULONG)atoi(argv[1]);
    (void)argc;
    Barrier.Barrier = Processors;
    Barrier.TotalProcessors = Processors;
    for (i = 0; i < Processors; i++)
        CHECK(pthread_create(&Threads[i], NULL, Processor, NULL) == 0);
    for (i = 0; i < Processors; i++)
        CHECK(pthread_join(Threads[i], NULL) == 0);
    for (i = 0; i < CALLS; i++)
        CHECK(Winners[i] == 1);
    printf("dpc-barrier: %u processors, %d calls, one winner each\n", Processors, CALLS);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='ke-dpc-barrier-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    for count in sorted({2, 3, min(8, max(2, os.cpu_count() or 2))}):
        subprocess.run([str(path / 'test'), str(count)], check=True, timeout=60)
