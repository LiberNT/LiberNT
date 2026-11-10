/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared nonpaged envelope between the NX and executable pool heaps
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <mm/pool/ex/expool.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>

#define MB                  ((SIZE_T)1024 * 1024)
#define RESIDENT_BYTES      (4 * MB)
#define SHARED_BYTES        (60 * MB)
#define CONTEND_SHARED      (124 * MB)
#define CONTEND_NX_BYTES    (62 * MB)
#define CONTEND_EXEC_BYTES  (24 * MB)
#define PAGED_BYTES         (64 * MB)
#define BLOCK_BYTES         (64 * 1024)
#define HEAVY_BYTES         (36 * MB)
#define SMP_THREADS         4
#define SMP_BYTES           (10 * MB)
#define TAG_TEST            0x76457845

_Thread_local ULONG PoolHostProcessor;
_Thread_local volatile int *PoolHostLockBlocked;
ULONG KeNumberProcessors = 1;
int MiSystem;
static EPROCESS SystemProcess, TestProcess;
PEPROCESS PsInitialSystemProcess = &SystemProcess;
static PUCHAR Arena, Protection, Mapping;
static SIZE_T ArenaBytes, SharedBytes, PagedOffset, MappingBytes;
static unsigned Failures, Checks;
extern POOL_KM_LAYOUT MiPoolLayout;

#define CHECK(e) do { __atomic_add_fetch(&Checks, 1, __ATOMIC_RELAXED); if (!(e)) { \
    __atomic_add_fetch(&Failures, 1, __ATOMIC_RELAXED); \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); } } while (0)

ULONG64 KeQueryInterruptTime(VOID) { return 1; }
KIRQL KeGetCurrentIrql(VOID) { return 0; }
_Noreturn VOID KeBugCheckEx(ULONG Code, ULONG_PTR A, ULONG_PTR B, ULONG_PTR C, ULONG_PTR D)
{ fprintf(stderr, "bugcheck %x %zx %zx %zx %zx\n", Code, (size_t)A, (size_t)B, (size_t)C, (size_t)D); abort(); }
_Noreturn VOID ExRaiseStatus(NTSTATUS Status) { fprintf(stderr, "raise %x\n", Status); abort(); }
BOOLEAN MmUseSpecialPool(SIZE_T Size, ULONG Tag) { return FALSE; }
PVOID MmAllocateSpecialPool(SIZE_T Size, ULONG Tag, POOL_TYPE Type, ULONG Mode) { return NULL; }
BOOLEAN MmIsSpecialPoolAddress(PVOID Block) { return FALSE; }
VOID MmFreeSpecialPool(PVOID Block) { abort(); }
PEPROCESS PsGetCurrentProcess(VOID) { return &TestProcess; }
VOID ObReferenceObject(PEPROCESS Process) { __atomic_add_fetch(&Process->References, 1, __ATOMIC_RELAXED); }
VOID ObDereferenceObject(PEPROCESS Process) { __atomic_sub_fetch(&Process->References, 1, __ATOMIC_RELAXED); }
VOID MiWakeBalanceSetManager(VOID) { }
NTSTATUS MiWaitForMemory(NTSTATUS Status, PULONG Attempts) { return Status; }
NTSTATUS PsChargeProcessPoolQuota(PEPROCESS Process, ULONG Type, SIZE_T Size) { return STATUS_SUCCESS; }
VOID PsReturnPoolQuota(PEPROCESS Process, ULONG Type, SIZE_T Size) { }

NTSTATUS MiSystemCommitPinned(int *System, ULONG64 Base, ULONG Pages, ULONG Protect)
{
    SIZE_T Offset = (SIZE_T)(Base - (ULONG64)(ULONG_PTR)Arena);

    CHECK(Offset < ArenaBytes && Pages <= (ArenaBytes - Offset) / PAGE_SIZE);
    if (Offset >= ArenaBytes || Pages > (ArenaBytes - Offset) / PAGE_SIZE)
        return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < Pages; i++)
    {
        UCHAR Old = __atomic_exchange_n(&Protection[(Offset >> PAGE_SHIFT) + i], (UCHAR)Protect, __ATOMIC_RELAXED);
        CHECK(Old == 0);
    }
    return STATUS_SUCCESS;
}

VOID MiSystemDecommitPinned(int *System, ULONG64 Base, ULONG Pages)
{
    SIZE_T First = ((ULONG_PTR)Base - (ULONG_PTR)Arena) >> PAGE_SHIFT;

    for (ULONG i = 0; i < Pages; i++)
    {
        UCHAR Old = __atomic_exchange_n(&Protection[First + i], 0, __ATOMIC_RELAXED);
        CHECK(Old != 0);
    }
}

static BOOLEAN Setup(ULONG Processors, SIZE_T Shared)
{
    SharedBytes = Shared;
    PagedOffset = RESIDENT_BYTES + Shared;
    ArenaBytes = PagedOffset + PAGED_BYTES;
    MappingBytes = ArenaBytes + POOL_UNIT_SIZE;
    Mapping = mmap(NULL, MappingBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (Mapping == MAP_FAILED)
        return FALSE;
    Arena = (PUCHAR)(((ULONG_PTR)Mapping + POOL_UNIT_MASK) & ~(ULONG_PTR)POOL_UNIT_MASK);
    Protection = calloc(ArenaBytes / PAGE_SIZE, 1);
    if (Protection == NULL)
        return FALSE;
    memset(Protection, MI_PROT_READWRITE, RESIDENT_BYTES / PAGE_SIZE);
    KeNumberProcessors = Processors;
    MiPoolLayout.NonPagedResidentBase = Arena;
    MiPoolLayout.NonPagedResidentBytes = RESIDENT_BYTES;
    MiPoolLayout.NonPagedSharedBase = Arena + RESIDENT_BYTES;
    MiPoolLayout.NonPagedSharedBytes = SharedBytes;
    MiPoolLayout.PagedBase = Arena + PagedOffset;
    MiPoolLayout.PagedBytes = PAGED_BYTES;
    InitializePool(NonPagedPool, 0);
    InitializePool(PagedPool, 0);
    return TRUE;
}

static VOID CheckBlock(PVOID Block, SIZE_T Size, BOOLEAN Execute)
{
    ULONG HeapIndex;

    CHECK(Block != NULL);
    if (Block == NULL)
        return;
    CHECK((PUCHAR)Block >= Arena && (PUCHAR)Block + Size <= Arena + PagedOffset);
    CHECK(ExpPoolHeapFromAddress(Block, &HeapIndex) != NULL);
    CHECK(HeapIndex == (Execute ? EX_POOL_HEAP_EXECUTABLE : EX_POOL_HEAP_NONPAGED));
    for (SIZE_T i = 0; i < Size; i += PAGE_SIZE)
        CHECK(__atomic_load_n(&Protection[((PUCHAR)Block + i - Arena) >> PAGE_SHIFT], __ATOMIC_RELAXED) ==
              (Execute ? MI_PROT_EXECUTE_READWRITE : MI_PROT_READWRITE));
    memset(Block, 0xA5, Size);
}

static PVOID Allocate(BOOLEAN Execute, SIZE_T Size)
{
    return ExAllocatePoolWithTag(Execute ? NonPagedPoolExecute : NonPagedPoolNx, Size, TAG_TEST);
}

static SIZE_T Fill(BOOLEAN Execute, SIZE_T Target, PVOID *Blocks, SIZE_T Capacity, SIZE_T *Count)
{
    SIZE_T Total = 0;

    *Count = 0;
    while ((Target == 0 || Total < Target) && *Count < Capacity)
    {
        PVOID Block = Allocate(Execute, BLOCK_BYTES);

        if (Block == NULL)
            break;
        CheckBlock(Block, BLOCK_BYTES, Execute);
        Blocks[(*Count)++] = Block;
        Total += BLOCK_BYTES;
    }
    return Total;
}

static VOID Drain(PVOID *Blocks, SIZE_T Count)
{
    for (SIZE_T i = 0; i < Count; i++)
        if (Blocks[i] != NULL)
            ExFreePoolWithTag(Blocks[i], TAG_TEST);
}

static VOID CheckRegions(VOID)
{
    PPOOL_HEAP Heaps[2] = { ExpPoolState.Heap[EX_POOL_HEAP_NONPAGED], ExpPoolState.Heap[EX_POOL_HEAP_EXECUTABLE] };
    ULONG_PTR Low = (ULONG_PTR)Arena, High = (ULONG_PTR)(Arena + PagedOffset);

    for (ULONG h = 0; h < 2; h++)
    {
        ULONG Count = PoolHeapRegionCount(Heaps[h]);

        for (ULONG i = 0; i < Count; i++)
        {
            PPOOL_VA_REGION A = Heaps[h]->Region[i];

            CHECK(A->Base >= Low && A->End <= High && A->Base < A->End);
            for (ULONG g = 0; g < 2; g++)
            {
                ULONG Other = PoolHeapRegionCount(Heaps[g]);

                for (ULONG j = 0; j < Other; j++)
                {
                    PPOOL_VA_REGION B = Heaps[g]->Region[j];

                    if (A == B)
                        continue;
                    CHECK(A->End <= B->Base || B->End <= A->Base);
                }
            }
        }
    }
}

static int Heavy(BOOLEAN Execute)
{
    SIZE_T Capacity = SharedBytes / BLOCK_BYTES, Count, Again;
    PVOID *Blocks = calloc(Capacity, sizeof(PVOID));
    PVOID Other;

    CHECK(Blocks != NULL);
    if (Blocks == NULL)
        return 1;
    CHECK(Fill(Execute, HEAVY_BYTES, Blocks, Capacity, &Count) >= HEAVY_BYTES);
    Other = Allocate(!Execute, BLOCK_BYTES);
    CheckBlock(Other, BLOCK_BYTES, !Execute);
    if (Other != NULL)
        ExFreePoolWithTag(Other, TAG_TEST);
    CheckRegions();
    Drain(Blocks, Count);
    CHECK(Fill(Execute, HEAVY_BYTES, Blocks, Capacity, &Again) >= HEAVY_BYTES);
    Drain(Blocks, Again);
    free(Blocks);
    return 0;
}

static int Large(VOID)
{
    SIZE_T NxBytes = 20 * MB, ExecBytes = 12 * MB;
    PVOID Nx = Allocate(FALSE, NxBytes);
    PVOID Exec = Allocate(TRUE, ExecBytes);

    CheckBlock(Nx, NxBytes, FALSE);
    CheckBlock(Exec, ExecBytes, TRUE);
    CheckRegions();
    if (Nx != NULL)
        ExFreePoolWithTag(Nx, TAG_TEST);
    if (Exec != NULL)
        ExFreePoolWithTag(Exec, TAG_TEST);
    return 0;
}

static int Exhaust(VOID)
{
    SIZE_T Capacity = SharedBytes / BLOCK_BYTES + RESIDENT_BYTES / BLOCK_BYTES, Count;
    PVOID *Blocks = calloc(Capacity, sizeof(PVOID));
    PVOID Exec, Again;

    CHECK(Blocks != NULL);
    if (Blocks == NULL)
        return 1;
    CHECK(Fill(FALSE, 0, Blocks, Capacity, &Count) >= HEAVY_BYTES);
    CHECK(Count < Capacity);
    CHECK(Allocate(FALSE, BLOCK_BYTES) == NULL);
    Exec = Allocate(TRUE, BLOCK_BYTES);
    CheckBlock(Exec, BLOCK_BYTES, TRUE);
    if (Exec != NULL)
        ExFreePoolWithTag(Exec, TAG_TEST);
    CheckRegions();
    Drain(Blocks, Count);
    Again = Allocate(FALSE, BLOCK_BYTES);
    CheckBlock(Again, BLOCK_BYTES, FALSE);
    if (Again != NULL)
        ExFreePoolWithTag(Again, TAG_TEST);
    free(Blocks);
    return 0;
}

static int Contend(VOID)
{
    SIZE_T NxCapacity = CONTEND_NX_BYTES / BLOCK_BYTES, ExecCapacity = CONTEND_EXEC_BYTES / BLOCK_BYTES;
    SIZE_T NxCount, ExecCount;
    PVOID *Nx = calloc(NxCapacity, sizeof(PVOID));
    PVOID *Exec = calloc(ExecCapacity, sizeof(PVOID));

    CHECK(Nx != NULL && Exec != NULL);
    if (Nx == NULL || Exec == NULL)
    {
        free(Nx);
        free(Exec);
        return 1;
    }
    CHECK(Fill(FALSE, CONTEND_NX_BYTES, Nx, NxCapacity, &NxCount) >= CONTEND_NX_BYTES);
    CHECK(Fill(TRUE, CONTEND_EXEC_BYTES, Exec, ExecCapacity, &ExecCount) >= CONTEND_EXEC_BYTES);
    CheckRegions();
    Drain(Exec, ExecCount);
    Drain(Nx, NxCount);
    free(Exec);
    free(Nx);
    return 0;
}

static void *SmpWorker(void *Context)
{
    ULONG Index = (ULONG)(ULONG_PTR)Context;
    BOOLEAN Execute = (Index & 1) != 0;
    SIZE_T Capacity = SMP_BYTES / BLOCK_BYTES, Count;
    PVOID *Blocks = calloc(Capacity, sizeof(PVOID));

    PoolHostProcessor = Index;
    CHECK(Blocks != NULL);
    if (Blocks == NULL)
        return NULL;
    CHECK(Fill(Execute, SMP_BYTES, Blocks, Capacity, &Count) >= SMP_BYTES);
    return Blocks;
}

static int Smp(VOID)
{
    pthread_t Threads[SMP_THREADS];
    PVOID *Results[SMP_THREADS] = { NULL };

    for (ULONG i = 0; i < SMP_THREADS; i++)
        CHECK(pthread_create(&Threads[i], NULL, SmpWorker, (void *)(ULONG_PTR)i) == 0);
    for (ULONG i = 0; i < SMP_THREADS; i++)
        CHECK(pthread_join(Threads[i], (void **)&Results[i]) == 0);
    CheckRegions();
    for (ULONG i = 0; i < SMP_THREADS; i++)
    {
        if (Results[i] == NULL)
            continue;
        Drain(Results[i], SMP_BYTES / BLOCK_BYTES);
        free(Results[i]);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *Area = (argc > 1) ? argv[1] : "";
    int Status = 1;

    if (!Setup(strcmp(Area, "smp") == 0 ? SMP_THREADS : 1,
               strcmp(Area, "contend") == 0 ? CONTEND_SHARED : SHARED_BYTES))
        return 1;
    if (strcmp(Area, "nx") == 0)
        Status = Heavy(FALSE);
    else if (strcmp(Area, "exec") == 0)
        Status = Heavy(TRUE);
    else if (strcmp(Area, "large") == 0)
        Status = Large();
    else if (strcmp(Area, "exhaust") == 0)
        Status = Exhaust();
    else if (strcmp(Area, "contend") == 0)
        Status = Contend();
    else if (strcmp(Area, "smp") == 0)
        Status = Smp();
    else
        fprintf(stderr, "unknown area '%s'\n", Area);
    free(Protection);
    munmap(Mapping, MappingBytes);
    printf("NT pool envelope %s: %u checks, %u failures\n", Area, Checks, Failures);
    return Status != 0 || Failures != 0;
}
