# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Pool expansion ownership regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
"""Exercise expansion ownership while another caller polls its pending flag."""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
s = pathlib.Path(args.source).read_text()
p = s.index('ExpPoolAttachExpansion(')
a = s.index('{', p)
depth, b = 1, a + 1
while depth:
    depth += (s[b] == '{') - (s[b] == '}')
    b += 1
body = s[a + 1:b - 1]
fixture = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <time.h>
typedef int LONG; typedef int BOOLEAN; typedef int POOL_VA_REGION, *PPOOL_VA_REGION;
typedef int POOL_BACKING; typedef struct { void *NonPagedExpansionBase; unsigned long NonPagedExpansionBytes; } POOL_KM_LAYOUT;
#define FALSE 0
#define TRUE 1
#define EX_POOL_HEAP_NONPAGED 0
#define NT_SUCCESS(x) ((x) >= 0)
#define POOL_ASSERT(x) assert(x)
typedef struct { POOL_VA_REGION Region[1][2]; void *Heap[1]; volatile LONG ExpansionPending, ExpansionAttaching; } EX_POOL_STATE;
static EX_POOL_STATE ExpPoolState;
static volatile LONG Entered, Resume, ReaderStarted, Stop, Wrote, Continue;
static LONG InterlockedCompareExchange(volatile LONG *p, LONG n, LONG o) { __atomic_compare_exchange_n(p,&o,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return o; }
static LONG InterlockedExchange(volatile LONG *p, LONG n)
{
    LONG Old=__atomic_exchange_n(p,n,__ATOMIC_SEQ_CST);
    if (p == &ExpPoolState.ExpansionPending)
    {
        __atomic_store_n(&Wrote,1,__ATOMIC_RELAXED);
        while (!__atomic_load_n(&Continue,__ATOMIC_RELAXED)) sched_yield();
    }
    return Old;
}
static void PoolBackQueryLayout(POOL_KM_LAYOUT *L) { L->NonPagedExpansionBase=0; L->NonPagedExpansionBytes=1; }
static void PoolBackDemandBacking(POOL_BACKING *B, int Executable) { *B=0; (void)Executable; }
static int PoolVaRegionInitialize(PPOOL_VA_REGION R, void *Base, unsigned long Bytes, int Paged, POOL_BACKING *Backing)
{
    (void)R; (void)Base; (void)Bytes; (void)Paged; (void)Backing;
    __atomic_store_n(&Entered,1,__ATOMIC_RELEASE);
    while (!__atomic_load_n(&Resume,__ATOMIC_ACQUIRE)) sched_yield();
    return 0;
}
static int PoolHeapAddRegion(void *Heap, PPOOL_VA_REGION Region) { (void)Heap; (void)Region; return 0; }
static BOOLEAN ExpPoolAttachExpansion(void) { @BODY@ }
static void *Attach(void *Unused) { (void)Unused; return (void *)(uintptr_t)ExpPoolAttachExpansion(); }
static void *Poll(void *Unused)
{
    (void)Unused;
    __atomic_store_n(&ReaderStarted,1,__ATOMIC_RELEASE);
    while (!__atomic_load_n(&Wrote,__ATOMIC_RELAXED)) sched_yield();
    while (!__atomic_load_n(&Stop,__ATOMIC_ACQUIRE)) ExpPoolAttachExpansion();
    return NULL;
}
int main(void)
{
    pthread_t Worker, Reader;
    struct timespec Delay={0,30000000};
    void *Result;
    ExpPoolState.ExpansionPending=1;
    assert(pthread_create(&Worker,NULL,Attach,NULL)==0);
    while (!__atomic_load_n(&Entered,__ATOMIC_ACQUIRE)) sched_yield();
    assert(pthread_create(&Reader,NULL,Poll,NULL)==0);
    while (!__atomic_load_n(&ReaderStarted,__ATOMIC_ACQUIRE)) sched_yield();
    nanosleep(&Delay,NULL);
    __atomic_store_n(&Resume,1,__ATOMIC_RELEASE);
    while (!__atomic_load_n(&Wrote,__ATOMIC_RELAXED)) sched_yield();
    nanosleep(&Delay,NULL);
    __atomic_store_n(&Continue,1,__ATOMIC_RELAXED);
    assert(pthread_join(Worker,&Result)==0);
    nanosleep(&Delay,NULL);
    __atomic_store_n(&Stop,1,__ATOMIC_RELEASE);
    assert(pthread_join(Reader,NULL)==0);
    assert((uintptr_t)Result==1 && ExpPoolState.ExpansionPending==0 && ExpPoolState.ExpansionAttaching==0);
    return 0;
}
'''.replace('@BODY@', body)
with tempfile.TemporaryDirectory(prefix='pool-expansion-') as d:
    root = pathlib.Path(d)
    (root / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-pthread', '-fsanitize=thread', '-g', '-O1',
                    str(root / 'test.c'), '-o', str(root / 'test')], check=True)
    subprocess.run([str(root / 'test')], check=True, timeout=10)
