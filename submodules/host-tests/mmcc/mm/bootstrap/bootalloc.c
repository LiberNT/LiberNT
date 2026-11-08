/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bootstrap allocator boundary and concurrency tests
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */
#include <mm/nt/mint.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define ARENA_BYTES (1024 * 1024)
#define THREADS 8
#define PER_THREAD 512
#define ALLOCATION_BYTES 64

PVOID MiKmAllocate(SIZE_T Bytes);
VOID MiKmFree(PVOID Block);
extern BOOLEAN MiPoolReady;
static uintptr_t Allocations[THREADS * PER_THREAD];

static void *AllocateThread(void *Context)
{
    size_t First = (size_t)Context * PER_THREAD;
    size_t Index;

    for (Index = 0; Index < PER_THREAD; Index++)
        Allocations[First + Index] = (uintptr_t)MiKmAllocate(ALLOCATION_BYTES);
    return NULL;
}

static int ComparePointers(const void *Left, const void *Right)
{
    uintptr_t A = *(const uintptr_t *)Left, B = *(const uintptr_t *)Right;
    return (A > B) - (A < B);
}

int main(int argc, char **argv)
{
    unsigned Index;

    if (argc != 2) return 2;
    if (!strcmp(argv[1], "oversize"))
    {
        if (MiKmAllocate(SIZE_MAX) != NULL ||
            (sizeof(SIZE_T) > 4 && MiKmAllocate((SIZE_T)UINT32_MAX + 1) != NULL)) return 1;
    }
    else if (!strcmp(argv[1], "exhaust"))
    {
        if (!MiKmAllocate(ARENA_BYTES)) return 1;
        for (Index = 0; Index < 4096; Index++)
            if (MiKmAllocate(ARENA_BYTES) != NULL) return 1;
    }
    else if (!strcmp(argv[1], "rollback"))
    {
        if (!MiKmAllocate(ARENA_BYTES - 32) || MiKmAllocate(48) != NULL || !MiKmAllocate(32)) return 1;
    }
    else if (!strcmp(argv[1], "pool"))
    {
        PVOID Boot = MiKmAllocate(64);
        PVOID Pool;
        if (Boot == NULL) return 1;
        MiPoolReady = TRUE;
        Pool = MiKmAllocate(64);
        if (Pool == NULL || Pool == Boot) return 1;
        MiKmFree(Boot);
        MiKmFree(Pool);
    }
    else if (!strcmp(argv[1], "concurrent"))
    {
        pthread_t Workers[THREADS];
        for (Index = 0; Index < THREADS; Index++)
            if (pthread_create(&Workers[Index], NULL, AllocateThread, (void *)(size_t)Index)) return 1;
        for (Index = 0; Index < THREADS; Index++)
            if (pthread_join(Workers[Index], NULL)) return 1;
        qsort(Allocations, THREADS * PER_THREAD, sizeof(Allocations[0]), ComparePointers);
        if (!Allocations[0]) return 1;
        for (Index = 1; Index < THREADS * PER_THREAD; Index++)
            if (Allocations[Index] - Allocations[Index - 1] != ALLOCATION_BYTES) return 1;
    }
    else return 2;
    puts("boot allocation boundary passed");
    return 0;
}
