/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_fault_smp.c
 * PURPOSE:     Concurrent page-fault host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"

#define FAULT_WORKERS 4

typedef struct _FAULT_READER
{
    TEST_WORLD *World;
    PMI_ADDRESS_SPACE Space;
    ULONG64 Base;
    ULONG64 Stride;
    ULONG Index;
    ULONG Pages;
    volatile LONG *Ready;
    volatile LONG *Go;
    volatile LONG *Done;
    volatile LONG *Guards;
} FAULT_READER;

static
void *
FaultReader(void *Argument)
{
    FAULT_READER *Reader = Argument;
    ULONG Page;

    MiHostCpu = Reader->Index;
    MachineCpu = Reader->Index;
    MI_ATOMIC_ADD32(Reader->Ready, 1);
    while (!MI_ATOMIC_READ32(Reader->Go))
        sched_yield();

    for (Page = 0; Page < Reader->Pages; Page++)
    {
        ULONG64 Va = Reader->Base + (Page * FAULT_WORKERS + Reader->Index) * Reader->Stride;
        ULONG64 Value = 0xFA000000ULL + (Page * FAULT_WORKERS + Reader->Index);
        MI_MEMORY_INFORMATION Info;
        NTSTATUS Status = MiFault(Reader->Space, Va, MiFaultWrite, TRUE);

        if (Status == STATUS_GUARD_PAGE_VIOLATION)
        {
            MI_ATOMIC_ADD32(Reader->Guards, 1);
            Status = MiFault(Reader->Space, Va, MiFaultWrite, TRUE);
        }
        CHECK(NT_SUCCESS(Status));
        Va += Reader->Index * sizeof(ULONG64);
        CHECK(NT_SUCCESS(UserWrite64(Reader->World, Reader->Index, Va, Value)));
        CHECK(UserRead64(Reader->World, Reader->Index, Va, &Status) == Value && NT_SUCCESS(Status));
        if (Page == 0)
        {
            CHECK(NT_SUCCESS(MiQueryVirtualMemory(Reader->Space, Va, &Info)));
            CHECK(Info.State == MI_MEM_COMMIT && Info.Protect == MI_PROT_READWRITE);
        }
    }

    CHECK(MiHostIrql == 0);
    MI_ATOMIC_ADD32(Reader->Done, 1);
    return NULL;
}

static
ULONG
FaultRunReaders(TEST_WORLD *World, PMI_ADDRESS_SPACE Space, ULONG64 Base, ULONG64 Stride, ULONG Pages)
{
    FAULT_READER Readers[FAULT_WORKERS];
    pthread_t Threads[FAULT_WORKERS];
    volatile LONG Ready = 0, Go = 0, Done = 0, Guards = 0;
    double Deadline;
    ULONG i;

    MI_RW_ACQUIRE_SHARED(&Space->Lock);
    for (i = 0; i < FAULT_WORKERS; i++)
        WorldAttach(World, i, Space);
    for (i = 0; i < FAULT_WORKERS; i++)
    {
        Readers[i] = (FAULT_READER){ World, Space, Base, Stride, i, Pages, &Ready, &Go, &Done, &Guards };
        MI_ASSERT(pthread_create(&Threads[i], NULL, FaultReader, &Readers[i]) == 0);
    }
    while (MI_ATOMIC_READ32(&Ready) != FAULT_WORKERS)
        sched_yield();
    MI_ATOMIC_ADD32(&Go, 1);
    Deadline = NowSeconds() + 2.0;
    while (MI_ATOMIC_READ32(&Done) != FAULT_WORKERS && NowSeconds() < Deadline)
        sched_yield();
    if (MI_ATOMIC_READ32(&Done) != FAULT_WORKERS)
        fprintf(stderr, "shared fault timeout: stride=%llu pages=%u done=%d\n",
                (unsigned long long)Stride, Pages, MI_ATOMIC_READ32(&Done));
    CHECK(MI_ATOMIC_READ32(&Done) == FAULT_WORKERS);
    MI_RW_RELEASE_SHARED(&Space->Lock);

    for (i = 0; i < FAULT_WORKERS; i++)
        MI_ASSERT(pthread_join(Threads[i], NULL) == 0);
    CHECK(MiPtCheck(Space) == 0);
    CHECK(WorldCheck(World) == 0);
    return (ULONG)Guards;
}

static
void
FaultSharedPte(ULONG Mode)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    PMI_SEGMENT Segment = NULL;
    ULONG64 Base = USER_BASE, Size = PAGE_SIZE;
    ULONG i;

    WorldCreate(&World, 256, FAULT_WORKERS, 100000);
    ProcessCreate(&World, &Space);
    if (Mode == 4)
        WorldAttachPageFile(&World, 256);

    if (Mode == 5)
    {
        CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentPageFileBacked, PAGE_SIZE,
                                         MI_PROT_READWRITE, NULL, NULL, NULL, 0, &Segment)));
        CHECK(NT_SUCCESS(MiMapView(&Space, Segment, &Base, 0, &Size, MI_PROT_WRITECOPY, 0)));
    }
    else
    {
        CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                                 MI_PROT_READWRITE | ((Mode == 1) ? MI_PROT_GUARD : 0))));
    }

    if (Mode >= 2 && Mode <= 4)
        CHECK(NT_SUCCESS(MiFault(&Space, Base, Mode == 4 ? MiFaultWrite : MiFaultRead, TRUE)));
    if (Mode == 3 || Mode == 4)
        CHECK(MiTrimAddressSpace(&Space, 1, TRUE) == 1);
    if (Mode == 4)
    {
        ULONG Held[256];
        ULONG Count = 0;
        ULONG Frame;

        CHECK(MiWriteModifiedPages(&World.System, 1) == 1);
        while ((Frame = MiPfnAllocatePage(&World.System.Pfn, MI_ALLOCATE_NO_RECLAIM | TEST_ANY_FRAME)) != MI_FRAME_INVALID)
        {
            MI_ASSERT(Count < RTL_NUMBER_OF(Held));
            Held[Count++] = Frame;
        }
        Frame = MiPfnAllocatePage(&World.System.Pfn, TEST_ANY_FRAME);
        CHECK(Frame != MI_FRAME_INVALID);
        if (Frame != MI_FRAME_INVALID)
            Held[Count++] = Frame;
        CHECK(MiSoftKind(MiArchPteRead(MiPtLookup(&Space, Base, NULL))) == MiSoftPageFile);
        for (i = 0; i < Count; i++)
            MiPfnFreePage(&World.System.Pfn, Held[i]);
    }

    CHECK(FaultRunReaders(&World, &Space, Base, 0, 1) == (ULONG)(Mode == 1));
    CHECK(Space.PrivatePages == 1 && Space.ResidentPages == 1);
    for (i = 0; i < FAULT_WORKERS; i++)
    {
        NTSTATUS Status;

        CHECK(UserRead64(&World, 0, Base + i * sizeof(ULONG64), &Status) == 0xFA000000ULL + i);
        CHECK(NT_SUCCESS(Status));
    }
    if (Mode == 5)
    {
        ULONG64 Alias = 0;
        NTSTATUS Status;

        CHECK(Space.CopyOnWriteFaults == 1);
        CHECK(NT_SUCCESS(MiMapView(&Space, Segment, &Alias, 0, &Size, MI_PROT_READONLY, 0)));
        CHECK(UserRead64(&World, 0, Alias, &Status) == 0 && NT_SUCCESS(Status));
    }
    if (Mode == 4)
        CHECK(Space.PageFileFaults == 1);

    for (i = 0; i < FAULT_WORKERS; i++)
        WorldAttach(&World, i, NULL);
    ProcessDestroy(&World, &Space);
    if (Segment != NULL)
        MiSegmentDereference(Segment);
    WorldExpectClean(&World, 256);
    WorldDestroy(&World);
}

static
void
FaultParallelTables(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    ULONG64 Base = USER_BASE;
    ULONG64 Span;
    ULONG64 Size;
    ULONG i;

    WorldCreate(&World, 2048, FAULT_WORKERS, 1000000);
    ProcessCreate(&World, &Space);
    Span = 1ULL << World.System.Arch->Level[1].Shift;
    Size = Span * FAULT_WORKERS * 64;
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Base, &Size,
                                             MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    CHECK(FaultRunReaders(&World, &Space, Base, Span, 64) == 0);
    CHECK(Space.PrivatePages == FAULT_WORKERS * 64 && Space.ResidentPages == FAULT_WORKERS * 64);
    for (i = 0; i < FAULT_WORKERS; i++)
        WorldAttach(&World, i, NULL);
    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 2048);
    WorldDestroy(&World);
}

#define PROTECT_PAGES 16
#define PROTECT_ROUNDS 1500

typedef struct _PROTECT_WORKER
{
    TEST_WORLD *World;
    PMI_ADDRESS_SPACE Space;
    ULONG64 Own;
    ULONG64 Common;
    ULONG Index;
    ULONG64 LastValue;
    volatile LONG *Ready;
    volatile LONG *Go;
} PROTECT_WORKER;

static
void *
ProtectWorker(void *Argument)
{
    PROTECT_WORKER *Worker = Argument;
    ULONG Previous = MI_PROT_READWRITE;
    ULONG Round;

    MiHostCpu = Worker->Index;
    MachineCpu = Worker->Index;
    MI_ATOMIC_ADD32(Worker->Ready, 1);
    while (!MI_ATOMIC_READ32(Worker->Go))
        sched_yield();

    for (Round = 0; Round < PROTECT_ROUNDS; Round++)
    {
        ULONG Protection = (Round & 1) ? MI_PROT_READONLY : MI_PROT_READWRITE;
        ULONG64 Va = Worker->Own, Size = PROTECT_PAGES * PAGE_SIZE;
        ULONG64 Common = Worker->Common, CommonSize = PROTECT_PAGES * PAGE_SIZE;
        ULONG64 Page = Worker->Own + (Round % PROTECT_PAGES) * PAGE_SIZE + Worker->Index * sizeof(ULONG64);
        ULONG Old = 0;
        NTSTATUS Status;

        CHECK(NT_SUCCESS(MiProtectVirtualMemory(Worker->Space, &Va, &Size, Protection, &Old)));
        CHECK(Old == Previous && Va == Worker->Own && Size == PROTECT_PAGES * PAGE_SIZE);
        Previous = Protection;

        if (Protection == MI_PROT_READWRITE)
        {
            ULONG64 Value = 0x9E000000ULL + Worker->Index * 0x100000ULL + Round;

            CHECK(NT_SUCCESS(MiFault(Worker->Space, Page, MiFaultWrite, TRUE)));
            CHECK(NT_SUCCESS(UserWrite64(Worker->World, Worker->Index, Page, Value)));
            CHECK(UserRead64(Worker->World, Worker->Index, Page, &Status) == Value && NT_SUCCESS(Status));
            Worker->LastValue = Value;
        }
        else
        {
            CHECK(MiFault(Worker->Space, Page, MiFaultWrite, TRUE) == STATUS_ACCESS_VIOLATION);
            CHECK(NT_SUCCESS(MiFault(Worker->Space, Page, MiFaultRead, TRUE)));
        }

        {
            static const ULONG Cycle[3] = { MI_PROT_READWRITE, MI_PROT_NOACCESS, MI_PROT_READONLY };
            ULONG64 Target = Worker->Common + ((Round * 7 + Worker->Index) % PROTECT_PAGES) * PAGE_SIZE;

            CHECK(NT_SUCCESS(MiProtectVirtualMemory(Worker->Space, &Common, &CommonSize,
                                                    Cycle[(Round + Worker->Index) % 3], &Old)));
            CHECK(Old == MI_PROT_READONLY || Old == MI_PROT_READWRITE || Old == MI_PROT_NOACCESS);
            Status = MiFault(Worker->Space, Target, (Round & 2) ? MiFaultWrite : MiFaultRead, TRUE);
            CHECK(NT_SUCCESS(Status) || Status == STATUS_ACCESS_VIOLATION);
        }

        if ((Round & 63) == 0)
        {
            MI_MEMORY_INFORMATION Info;

            CHECK(NT_SUCCESS(MiQueryVirtualMemory(Worker->Space, Worker->Own, &Info)));
            CHECK(Info.State == MI_MEM_COMMIT && Info.Protect == Protection);
        }
    }

    CHECK(MiHostIrql == 0);
    return NULL;
}

static
void
FaultProtectConcurrent(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Space;
    PROTECT_WORKER Workers[FAULT_WORKERS];
    pthread_t Threads[FAULT_WORKERS];
    volatile LONG Ready = 0, Go = 0;
    ULONG64 Base = USER_BASE, Size = (FAULT_WORKERS + 1) * PROTECT_PAGES * PAGE_SIZE;
    ULONG64 Va;
    NTSTATUS Status;
    ULONG i;

    WorldCreate(&World, 1024, FAULT_WORKERS, 1000000);
    ProcessCreate(&World, &Space);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Space, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE)));
    for (Va = Base; Va < Base + Size; Va += PAGE_SIZE)
        CHECK(NT_SUCCESS(MiFault(&Space, Va, MiFaultWrite, TRUE)));

    for (i = 0; i < FAULT_WORKERS; i++)
        WorldAttach(&World, i, &Space);
    for (i = 0; i < FAULT_WORKERS; i++)
    {
        Workers[i] = (PROTECT_WORKER){ &World, &Space, Base + (i + 1) * PROTECT_PAGES * PAGE_SIZE, Base, i, 0,
                                       &Ready, &Go };
        MI_ASSERT(pthread_create(&Threads[i], NULL, ProtectWorker, &Workers[i]) == 0);
    }
    while (MI_ATOMIC_READ32(&Ready) != FAULT_WORKERS)
        sched_yield();
    MI_ATOMIC_ADD32(&Go, 1);
    for (i = 0; i < FAULT_WORKERS; i++)
        MI_ASSERT(pthread_join(Threads[i], NULL) == 0);

    for (i = 0; i < FAULT_WORKERS; i++)
    {
        ULONG64 Page = Workers[i].Own + ((PROTECT_ROUNDS - 2) % PROTECT_PAGES) * PAGE_SIZE + i * sizeof(ULONG64);

        CHECK(UserRead64(&World, 0, Page, &Status) == Workers[i].LastValue && NT_SUCCESS(Status));
    }
    CHECK(MiPtCheck(&Space) == 0);
    CHECK(WorldCheck(&World) == 0);
    for (i = 0; i < FAULT_WORKERS; i++)
        WorldAttach(&World, i, NULL);
    ProcessDestroy(&World, &Space);
    WorldExpectClean(&World, 1024);
    WorldDestroy(&World);
}

void
TestFaultSmp(void)
{
    ULONG Mode;

    for (Mode = 0; Mode < 6; Mode++)
        FaultSharedPte(Mode);
    FaultParallelTables();
    FaultProtectConcurrent();
}
