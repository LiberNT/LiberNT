#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Paging-file initialization race regression
# COPYRIGHT:   Copyright 2026 LiberNT contributors
"""Run the actual paging-file creation initialization under forced concurrency."""
import argparse
import pathlib
import subprocess
import tempfile


def body(source, name):
    start = source.index(name + '(')
    start = source.index('{', start)
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start + 1:end - 1]


def main():
    parser = argparse.ArgumentParser()
    for option in ('source', 'init', 'cc'):
        parser.add_argument('--' + option, required=True)
    args = parser.parse_args()
    source = pathlib.Path(args.source).read_text()
    init = pathlib.Path(args.init).read_text()
    create = body(source, 'NtCreatePagingFile')
    create = create.split('PAGED_CODE();', 1)[1].split('if (PreviousMode != KernelMode && !SeSinglePrivilegeCheck', 1)[0]
    has_phase_init = 'MiPageFileCreationInitialize();' in init
    phase = body(source, 'MiPageFileCreationInitialize') if has_phase_init else ''
    fixture = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
typedef int BOOLEAN;
typedef struct { int unused; } KGUARDED_MUTEX;
#define TRUE 1
#define ASSERT(x) assert(x)
static _Atomic int MiPagingFileLockReady;
static KGUARDED_MUTEX MiPagingFileCreationLock;
static atomic_int Concurrent, InitCalls;
static pthread_barrier_t Barrier;
static void KeInitializeGuardedMutex(KGUARDED_MUTEX *Mutex)
{
    (void)Mutex;
    if (atomic_load(&Concurrent))
        pthread_barrier_wait(&Barrier);
    atomic_fetch_add(&InitCalls, 1);
}
static void Phase0Initialize(void) { @PHASE@ }
static void *CreateEntry(void *Argument) { (void)Argument; @CREATE@ return NULL; }
int main(void)
{
    pthread_t A, B;
    assert(pthread_barrier_init(&Barrier, NULL, 2) == 0);
    Phase0Initialize();
    atomic_store(&Concurrent, 1);
    assert(pthread_create(&A, NULL, CreateEntry, NULL) == 0);
    assert(pthread_create(&B, NULL, CreateEntry, NULL) == 0);
    assert(pthread_join(A, NULL) == 0);
    assert(pthread_join(B, NULL) == 0);
    printf("paging-file lock initializations: %d\n", atomic_load(&InitCalls));
    return atomic_load(&InitCalls) != 1;
}
'''.replace('@PHASE@', phase).replace('@CREATE@', create)
    with tempfile.TemporaryDirectory(prefix='mm-pagefile-init-') as directory:
        root = pathlib.Path(directory)
        test = root / 'test.c'
        test.write_text(fixture)
        exe = root / 'test'
        subprocess.run([args.cc, '-std=c11', '-pthread', str(test), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    main()
