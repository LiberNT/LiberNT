/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Frames outside the direct map: allocation zones and the frame window
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "mmharness.h"
#include <mm/include/misys.h>

#define HM_FRAMES 2048
#define HM_DIRECT 1024

static BOOLEAN
FrameIsFilled(TEST_WORLD *World, ULONG Frame, UCHAR Value)
{
    PUCHAR Bytes = MachineFrame(&World->Machine, Frame);
    ULONG i;

    for (i = 0; i < PAGE_SIZE; i++)
    {
        if (Bytes[i] != Value)
            return FALSE;
    }
    return TRUE;
}

static void
HighMemZones(void)
{
    static TEST_WORLD World;
    static ULONG Held[HM_FRAMES];
    PMI_PFN_DATABASE Db;
    ULONG Count = 0, Frame, i, HighSeen = 0, LowSeen = 0, Cursor;
    ULONG64 HighFree;
    BOOLEAN Ordered = TRUE;

    WorldCreateZoned(&World, HM_FRAMES, HM_DIRECT, 2, 100000);
    Db = &World.System.Pfn;
    CHECK(Db->DirectFrames == HM_DIRECT && Db->Window.SlotCount != 0);
    CHECK(MiPfnDbCheck(Db) == 0);

    while ((Frame = MiPfnAllocatePage(Db, MI_ALLOCATE_NO_RECLAIM)) != MI_FRAME_INVALID)
    {
        CHECK(Frame < HM_DIRECT);
        Held[Count++] = Frame;
    }
    HighFree = MiPfnAvailablePages(Db);
    CHECK(HighFree == HM_FRAMES - HM_DIRECT);
    CHECK(MiPfnAllocatePage(Db, MI_ALLOCATE_ZEROED) == MI_FRAME_INVALID);
    CHECK(MiPfnAllocateContiguous(Db, 2, 0, HM_FRAMES - 1, 0) == MI_FRAME_INVALID);
    Cursor = HM_DIRECT;
    CHECK(MiPfnAllocatePageInRange(Db, HM_DIRECT, HM_FRAMES - 1, &Cursor) == MI_FRAME_INVALID);

    Frame = MiPfnAllocatePage(Db, MI_ALLOCATE_HIGH);
    CHECK(Frame != MI_FRAME_INVALID && Frame >= HM_DIRECT);
    CHECK(MiPfnSetCache(Db, Frame, MI_LEAF_NOCACHE) == STATUS_SUCCESS);
    CHECK((ULONG)Db->Pfn[Frame].CacheFlags == MI_LEAF_NOCACHE && World.Machine.FrameCache[Frame] == 0);
    MiPfnShareDecrement(Db, Frame, TRUE);
    CHECK(Db->Pfn[Frame].State == MiPageFree);
    CHECK(MiPfnAvailablePages(Db) == HighFree);

    while (Count != 0)
        MiPfnShareDecrement(Db, Held[--Count], TRUE);
    MiPfnDrainCaches(Db);

    while ((Frame = MiPfnAllocatePage(Db, MI_ALLOCATE_HIGH | MI_ALLOCATE_NO_RECLAIM)) != MI_FRAME_INVALID)
    {
        if (Frame >= HM_DIRECT)
        {
            HighSeen++;
            if (LowSeen != 0)
                Ordered = FALSE;
        }
        else
        {
            LowSeen++;
        }
        memset(MachineFrame(&World.Machine, Frame), 0xEE, PAGE_SIZE);
        Held[Count++] = Frame;
    }
    CHECK(Ordered && HighSeen == HM_FRAMES - HM_DIRECT && LowSeen != 0);
    CHECK(MiPfnAvailablePages(Db) == 0);
    while (Count != 0)
        MiPfnShareDecrement(Db, Held[--Count], TRUE);
    MiPfnDrainCaches(Db);

    for (i = 0; i < 3 * Db->Window.SlotCount; i++)
    {
        Frame = MiPfnAllocatePage(Db, MI_ALLOCATE_HIGH | MI_ALLOCATE_ZEROED);
        CHECK(Frame >= HM_DIRECT && Frame != MI_FRAME_INVALID);
        if (Frame == MI_FRAME_INVALID)
            break;
        CHECK(FrameIsFilled(&World, Frame, 0));
        memset(MachineFrame(&World.Machine, Frame), 0xEE, PAGE_SIZE);
        MiPfnShareDecrement(Db, Frame, TRUE);
    }
    CHECK(Db->Window.Busy == 0 && Db->Window.Maps >= 3 * Db->Window.SlotCount && Db->Window.Flushes >= 2);
    CHECK(MiPfnDbCheck(Db) == 0);
    WorldExpectClean(&World, HM_FRAMES);
    WorldDestroy(&World);
}

static void
HighMemWindow(void)
{
    static TEST_WORLD World;
    PMI_PFN_DATABASE Db;
    PVOID Held[16];
    ULONG Frames[16];
    ULONG i;

    WorldCreateZoned(&World, HM_FRAMES, HM_DIRECT, 1, 100000);
    Db = &World.System.Pfn;

    for (i = 0; i < RTL_NUMBER_OF(Held); i++)
    {
        Frames[i] = MiPfnAllocatePage(Db, MI_ALLOCATE_HIGH);
        CHECK(Frames[i] != MI_FRAME_INVALID && Frames[i] >= HM_DIRECT);
        Held[i] = MiPfnMapFrame(Db, Frames[i]);
        CHECK(Held[i] != NULL);
        memset(Held[i], (int)(0x40 + i), PAGE_SIZE);
    }
    CHECK(Db->Window.Busy == RTL_NUMBER_OF(Held));
    for (i = 0; i < RTL_NUMBER_OF(Held); i++)
    {
        ULONG j;

        for (j = i + 1; j < RTL_NUMBER_OF(Held); j++)
            CHECK(Held[i] != Held[j]);
    }
    for (i = 0; i < RTL_NUMBER_OF(Held); i++)
        MiPfnUnmapFrame(Db, Held[i]);
    CHECK(Db->Window.Busy == 0 && Db->Window.Stale == RTL_NUMBER_OF(Held));
    for (i = 0; i < RTL_NUMBER_OF(Held); i++)
        CHECK(FrameIsFilled(&World, Frames[i], (UCHAR)(0x40 + i)));

    {
        ULONG Low = MiPfnAllocatePage(Db, 0);
        PVOID Direct;

        CHECK(Low != MI_FRAME_INVALID && Low < HM_DIRECT);
        Direct = MiPfnMapFrame(Db, Low);
        CHECK(Direct == MachineFrame(&World.Machine, Low));
        MiPfnUnmapFrame(Db, Direct);
        CHECK(Db->Window.Busy == 0);
        MiPfnShareDecrement(Db, Low, TRUE);
    }

    for (i = 0; i < RTL_NUMBER_OF(Held); i++)
        MiPfnShareDecrement(Db, Frames[i], TRUE);
    WorldExpectClean(&World, HM_FRAMES);
    WorldDestroy(&World);
}

typedef struct _HIGHMEM_WINDOW_WORKER
{
    PMI_PFN_DATABASE Db;
    ULONG Frame;
    ULONG Cpu;
    volatile LONG *Ready;
    volatile LONG *Go;
} HIGHMEM_WINDOW_WORKER;

static void *
HighMemWindowWorker(void *Context)
{
    HIGHMEM_WINDOW_WORKER *Worker = Context;
    ULONG i;

    MiHostCpu = Worker->Cpu;
    MachineCpu = Worker->Cpu;
    MI_ATOMIC_ADD32(Worker->Ready, 1);
    while (!MI_ATOMIC_READ32(Worker->Go))
        sched_yield();
    for (i = 0; i < 1024; i++)
    {
        PUCHAR Mapping = MiPfnMapFrame(Worker->Db, Worker->Frame);

        MI_ASSERT(Mapping != NULL);
        memset(Mapping, (int)(0x60 + Worker->Cpu), PAGE_SIZE);
        MiPfnUnmapFrame(Worker->Db, Mapping);
    }
    return NULL;
}

static void
HighMemWindowConcurrent(void)
{
    static TEST_WORLD World;
    HIGHMEM_WINDOW_WORKER Workers[4];
    pthread_t Threads[4];
    volatile LONG Ready = 0, Go = 0;
    PMI_PFN_DATABASE Db;
    ULONG i;

    WorldCreateZoned(&World, HM_FRAMES, HM_DIRECT, RTL_NUMBER_OF(Workers), 100000);
    Db = &World.System.Pfn;
    for (i = 0; i < RTL_NUMBER_OF(Workers); i++)
    {
        Workers[i] = (HIGHMEM_WINDOW_WORKER){ Db, MiPfnAllocatePage(Db, MI_ALLOCATE_HIGH), i, &Ready, &Go };
        MI_ASSERT(Workers[i].Frame != MI_FRAME_INVALID && Workers[i].Frame >= HM_DIRECT);
        MI_ASSERT(pthread_create(&Threads[i], NULL, HighMemWindowWorker, &Workers[i]) == 0);
    }
    while (MI_ATOMIC_READ32(&Ready) != RTL_NUMBER_OF(Workers))
        sched_yield();
    MI_ATOMIC_ADD32(&Go, 1);
    for (i = 0; i < RTL_NUMBER_OF(Workers); i++)
    {
        MI_ASSERT(pthread_join(Threads[i], NULL) == 0);
        CHECK(FrameIsFilled(&World, Workers[i].Frame, (UCHAR)(0x60 + i)));
        MiPfnShareDecrement(Db, Workers[i].Frame, TRUE);
    }
    CHECK(Db->Window.Busy == 0 && Db->Window.Maps >= 1024 * RTL_NUMBER_OF(Workers));
    CHECK(Db->Window.Flushes != 0);
    CHECK(MiPfnDbCheck(Db) == 0);
    WorldExpectClean(&World, HM_FRAMES);
    WorldDestroy(&World);
}

static void
HighMemUser(void)
{
    static TEST_WORLD World;
    MI_ADDRESS_SPACE Parent, Child;
    ULONG64 Base = USER_BASE, Size = 64 * PAGE_SIZE, Physical;
    static ULONG Held[HM_FRAMES];
    ULONG Count = 0, Frame, TableFrame, i;
    NTSTATUS Status;

    WorldCreateZoned(&World, HM_FRAMES, HM_DIRECT, 2, 100000);
    WorldAttachPageFile(&World, 1024);
    ProcessCreate(&World, &Parent);
    ProcessCreate(&World, &Child);
    WorldAttach(&World, 0, &Parent);
    WorldAttach(&World, 1, &Child);
    CHECK(NT_SUCCESS(MiAllocateVirtualMemory(&Parent, &Base, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                             MI_PROT_READWRITE)));
    for (i = 0; i < 64; i++)
    {
        CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + i * PAGE_SIZE, 0x1000 + i)));
        CHECK(MiPtTranslate(&Parent, Base + i * PAGE_SIZE, &Physical, NULL));
        CHECK((Physical >> PAGE_SHIFT) >= HM_DIRECT);
        CHECK(MiPtLookup(&Parent, Base + i * PAGE_SIZE, &TableFrame) != NULL && TableFrame < HM_DIRECT);
    }
    CHECK(Parent.RootFrame < HM_DIRECT);

    CHECK(MiTrimAddressSpace(&Parent, 64, TRUE) == 64);
    while (MiWriteModifiedPages(&World.System, 1024) != 0)
        ;
    while ((Frame = MiPfnAllocatePage(&World.System.Pfn, MI_ALLOCATE_HIGH)) != MI_FRAME_INVALID)
        Held[Count++] = Frame;
    while (Count != 0)
        MiPfnShareDecrement(&World.System.Pfn, Held[--Count], TRUE);
    CHECK(World.Paging.PageFile.SlotsInUse == 64);
    for (i = 0; i < 64; i++)
    {
        CHECK(UserRead64(&World, 0, Base + i * PAGE_SIZE, &Status) == 0x1000 + i && NT_SUCCESS(Status));
        CHECK(MiPtTranslate(&Parent, Base + i * PAGE_SIZE, &Physical, NULL));
        CHECK((Physical >> PAGE_SHIFT) >= HM_DIRECT);
    }
    CHECK(MI_ATOMIC_READ64(&Parent.PageFileFaults) == 64);

    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Parent, &Child)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 1, Base, 0xC0DE)));
    CHECK(NT_SUCCESS(UserWrite64(&World, 0, Base + PAGE_SIZE, 0xF00D)));
    CHECK(UserRead64(&World, 0, Base, &Status) == 0x1000 && NT_SUCCESS(Status));
    CHECK(UserRead64(&World, 1, Base, &Status) == 0xC0DE && NT_SUCCESS(Status));
    CHECK(UserRead64(&World, 1, Base + PAGE_SIZE, &Status) == 0x1001 && NT_SUCCESS(Status));
    CHECK(UserRead64(&World, 0, Base + PAGE_SIZE, &Status) == 0xF00D && NT_SUCCESS(Status));
    for (i = 2; i < 64; i++)
        CHECK(UserRead64(&World, 1, Base + i * PAGE_SIZE, &Status) == 0x1000 + i && NT_SUCCESS(Status));

    CHECK(World.System.Pfn.Window.Maps != 0 && World.System.Pfn.Window.Busy == 0);
    CHECK(World.Machine.DirectViolations == 0);
    WorldAttach(&World, 1, NULL);
    ProcessDestroy(&World, &Child);
    ProcessDestroy(&World, &Parent);
    WorldExpectClean(&World, HM_FRAMES);
    WorldDestroy(&World);
}

static void
HighMemCacheRuns(void)
{
    static TEST_WORLD World;
    MI_FRAME_NUMBER Frames[6];
    PMI_PFN_DATABASE Db;
    ULONG First, Single, High, Calls, i;

    WorldCreateZoned(&World, HM_FRAMES, HM_DIRECT, 1, 100000);
    Db = &World.System.Pfn;

    First = MiPfnAllocateContiguous(Db, 6, 0, HM_DIRECT - 1, 0);
    High = MiPfnAllocatePage(Db, MI_ALLOCATE_HIGH);
    CHECK(First != MI_FRAME_INVALID && High != MI_FRAME_INVALID);
    CHECK(First + 6 <= HM_DIRECT && High >= HM_DIRECT);
    Single = First + 5;
    for (i = 0; i < 4; i++)
        Frames[i] = First + i;
    Frames[4] = Single;
    Frames[5] = High;

    Calls = World.Machine.FrameCacheCalls;
    MiPfnSetCacheFrames(Db, Frames, RTL_NUMBER_OF(Frames), MI_LEAF_WRITECOMBINE);
    CHECK(World.Machine.FrameCacheCalls == Calls + 2);
    for (i = 0; i < RTL_NUMBER_OF(Frames); i++)
    {
        CHECK((ULONG)Db->Pfn[Frames[i]].CacheFlags == MI_LEAF_WRITECOMBINE);
        CHECK(World.Machine.FrameCache[Frames[i]] == (Frames[i] < HM_DIRECT ? MI_LEAF_WRITECOMBINE : 0));
    }

    MiPfnSetCacheFrames(Db, Frames, RTL_NUMBER_OF(Frames), MI_LEAF_WRITECOMBINE);
    CHECK(World.Machine.FrameCacheCalls == Calls + 2);

    CHECK(MiPfnSetCache(Db, First + 1, 0) == STATUS_SUCCESS);
    Calls = World.Machine.FrameCacheCalls;
    MiPfnSetCacheFrames(Db, Frames, RTL_NUMBER_OF(Frames), 0);
    CHECK(World.Machine.FrameCacheCalls == Calls + 3);
    for (i = 0; i < RTL_NUMBER_OF(Frames); i++)
    {
        CHECK(Db->Pfn[Frames[i]].CacheFlags == 0);
        CHECK(World.Machine.FrameCache[Frames[i]] == 0);
    }

    for (i = 0; i < RTL_NUMBER_OF(Frames); i++)
        MiPfnShareDecrement(Db, (ULONG)Frames[i], TRUE);
    MiPfnShareDecrement(Db, First + 4, TRUE);
    CHECK(MiPfnDbCheck(Db) == 0);
    WorldExpectClean(&World, HM_FRAMES);
    WorldDestroy(&World);
}

static void
HighMemCacheFailure(void)
{
    static TEST_WORLD World;
    MI_FRAME_NUMBER Frames[70];
    PMI_PFN_DATABASE Db;
    ULONG First, i;
    ULONG64 Va;
    NTSTATUS Status;

    WorldCreateZoned(&World, HM_FRAMES, HM_DIRECT, 1, 100000);
    Db = &World.System.Pfn;
    WorldAttach(&World, 0, NULL);
    MI_ASSERT(NT_SUCCESS(MiSystemPtesInitialize(&World.System, 256, 1)));
    First = MiPfnAllocateContiguous(Db, RTL_NUMBER_OF(Frames), 0, HM_DIRECT - 1, 0);
    MI_ASSERT(First != MI_FRAME_INVALID);
    for (i = 0; i < RTL_NUMBER_OF(Frames); i++)
        Frames[i] = First + i;
    World.Machine.FrameCacheFailureFramePlusOne = First + 67;
    Status = MiPfnSetCacheFrames(Db, Frames, RTL_NUMBER_OF(Frames), MI_LEAF_NOCACHE);
    CHECK(Status == STATUS_NOT_SUPPORTED);
    for (i = 0; i < RTL_NUMBER_OF(Frames); i++)
    {
        CHECK((ULONG)Db->Pfn[Frames[i]].CacheFlags == World.Machine.FrameCache[Frames[i]]);
        CHECK(World.Machine.FrameCache[Frames[i]] == (i < 66 ? MI_LEAF_NOCACHE : 0));
    }
    World.Machine.FrameCacheFailureFramePlusOne = 0;
    MiPfnSetCacheFrames(Db, Frames, RTL_NUMBER_OF(Frames), 0);

    Va = MiReserveSystemPtes(&World.System, 4);
    MI_ASSERT(Va != 0);
    World.Machine.FrameCacheFailureFramePlusOne = First + 3;
    Status = MiSystemMapFrames(&World.System, Va, Frames, 4, MI_PROT_READWRITE, MI_LEAF_NOCACHE, TRUE);
    CHECK(Status == STATUS_NOT_SUPPORTED);
    for (i = 0; i < 4; i++)
    {
        PMI_PTE Slot = MiPtLookup(&World.System.SystemSpace, Va + (ULONG64)i * PAGE_SIZE, NULL);

        CHECK(Slot == NULL || !MiArchPteIsValid(MiArchPteRead(Slot)));
        CHECK((ULONG)Db->Pfn[Frames[i]].CacheFlags == World.Machine.FrameCache[Frames[i]]);
    }
    if (NT_SUCCESS(Status))
        MiSystemUnmap(&World.System, Va, 4, FALSE);
    World.Machine.FrameCacheFailureFramePlusOne = 0;
    MiPfnSetCacheFrames(Db, Frames, RTL_NUMBER_OF(Frames), 0);
    MiReleaseSystemPtes(&World.System, Va, 4);
    CHECK(NT_SUCCESS(MiPfnSetCache(Db, First + 1, MI_LEAF_NOCACHE)));
    World.Machine.FrameCacheFailureFramePlusOne = First + 2;
    MiPfnShareDecrement(Db, First + 1, TRUE);
    CHECK(Db->Pfn[First + 1].State == MiPageBad);
    CHECK(Db->Pfn[First + 1].CacheFlags == MI_LEAF_NOCACHE);
    CHECK(World.Machine.FrameCache[First + 1] == MI_LEAF_NOCACHE);
    World.Machine.FrameCacheFailureFramePlusOne = 0;
    CHECK(NT_SUCCESS(MiPfnSetCache(Db, First + 2, MI_LEAF_NOCACHE)));
    MiPfnShareDecrement(Db, First + 2, FALSE);
    CHECK(Db->Pfn[First + 2].State == MiPageStandby);
    World.Machine.FrameCacheFailureFramePlusOne = First + 3;
    MiPfnPurgeStandby(Db);
    CHECK(Db->Pfn[First + 2].State == MiPageBad);
    CHECK(Db->Pfn[First + 2].CacheFlags == MI_LEAF_NOCACHE);
    World.Machine.FrameCacheFailureFramePlusOne = 0;
    CHECK(NT_SUCCESS(MiPfnSetCache(Db, First + 3, MI_LEAF_NOCACHE)));
    MiPfnShareDecrement(Db, First + 3, FALSE);
    CHECK(Db->Pfn[First + 3].State == MiPageStandby);
    World.Machine.FrameCacheFailureFramePlusOne = First + 4;
    CHECK(MiPfnAllocateContiguous(Db, 1, First + 3, First + 3, 0) == MI_FRAME_INVALID);
    CHECK(Db->Pfn[First + 3].State == MiPageBad);
    CHECK(Db->Pfn[First + 3].CacheFlags == MI_LEAF_NOCACHE);
    World.Machine.FrameCacheFailureFramePlusOne = 0;
    for (i = 0; i < RTL_NUMBER_OF(Frames); i++)
        if (i < 1 || i > 3)
            MiPfnShareDecrement(Db, (ULONG)Frames[i], TRUE);
    MiSystemPtesUninitialize(&World.System);
    CHECK(MiPfnListCount(Db, MiPageBad) == 3);
    CHECK(MiPfnDbCheck(Db) == 0);
    WorldExpectClean(&World, HM_FRAMES - 3);
    WorldDestroy(&World);
}

static void
HighMemCacheFailureOnReclaim(void)
{
    static TEST_WORLD World;
    static ULONG Held[HM_DIRECT];
    PMI_PFN_DATABASE Db;
    ULONG Count = 0, Frame, i;

    WorldCreateZoned(&World, HM_FRAMES, HM_DIRECT, 1, 100000);
    Db = &World.System.Pfn;
    while ((Frame = MiPfnAllocatePage(Db, MI_ALLOCATE_NO_RECLAIM)) != MI_FRAME_INVALID)
    {
        MI_ASSERT(Count < RTL_NUMBER_OF(Held));
        Held[Count++] = Frame;
    }
    MI_ASSERT(Count > 1);
    Frame = Held[0];
    CHECK(NT_SUCCESS(MiPfnSetCache(Db, Frame, MI_LEAF_NOCACHE)));
    MiPfnShareDecrement(Db, Frame, FALSE);
    MiPfnShareDecrement(Db, Held[1], FALSE);
    CHECK(Db->Pfn[Frame].State == MiPageStandby);
    World.Machine.FrameCacheFailureFramePlusOne = Frame + 1;
    CHECK(MiPfnAllocatePage(Db, 0) == Held[1]);
    CHECK(Db->Pfn[Frame].State == MiPageBad);
    CHECK(Db->Pfn[Frame].CacheFlags == MI_LEAF_NOCACHE);
    World.Machine.FrameCacheFailureFramePlusOne = 0;
    for (i = 1; i < Count; i++)
        MiPfnShareDecrement(Db, Held[i], TRUE);
    CHECK(MiPfnListCount(Db, MiPageBad) == 1);
    CHECK(MiPfnDbCheck(Db) == 0);
    WorldExpectClean(&World, HM_FRAMES - 1);
    WorldDestroy(&World);
}

typedef struct _HIGHMEM_CACHE_WORKER
{
    PMI_PFN_DATABASE Db;
    ULONG Frame;
    ULONG Cpu;
    ULONG Requested;
    ULONG Actual;
    NTSTATUS Status;
    volatile LONG *Ready;
    volatile LONG *Go;
} HIGHMEM_CACHE_WORKER;

static void *
HighMemCacheWorker(void *Context)
{
    HIGHMEM_CACHE_WORKER *Worker = Context;

    MiHostCpu = Worker->Cpu;
    MachineCpu = Worker->Cpu;
    MI_ATOMIC_ADD32(Worker->Ready, 1);
    while (!MI_ATOMIC_READ32(Worker->Go))
        sched_yield();
    Worker->Status = MiPfnMappingFlags(Worker->Db, Worker->Frame, Worker->Requested, &Worker->Actual);
    return NULL;
}

static void
HighMemConcurrentFirstMapping(void)
{
    static TEST_WORLD World;
    HIGHMEM_CACHE_WORKER Workers[4];
    pthread_t Threads[4];
    PMI_PFN_DATABASE Db;
    ULONG Round, Index;

    WorldCreateZoned(&World, HM_FRAMES, HM_DIRECT, RTL_NUMBER_OF(Workers), 100000);
    Db = &World.System.Pfn;
    for (Round = 0; Round < 32; Round++)
    {
        volatile LONG Ready = 0, Go = 0;
        ULONG Frame = MiPfnAllocatePage(Db, 0);
        ULONG Calls = World.Machine.FrameCacheCalls;

        MI_ASSERT(Frame != MI_FRAME_INVALID);
        Db->Pfn[Frame].CacheFlags = MI_PFN_CACHE_UNASSIGNED;
        for (Index = 0; Index < RTL_NUMBER_OF(Workers); Index++)
        {
            Workers[Index] = (HIGHMEM_CACHE_WORKER){ Db, Frame, Index,
                (Index & 1) ? MI_LEAF_NOCACHE : MI_LEAF_WRITECOMBINE, 0, 0, &Ready, &Go };
            MI_ASSERT(pthread_create(&Threads[Index], NULL, HighMemCacheWorker, &Workers[Index]) == 0);
        }
        while (MI_ATOMIC_READ32(&Ready) != RTL_NUMBER_OF(Workers))
            sched_yield();
        MI_ATOMIC_ADD32(&Go, 1);
        for (Index = 0; Index < RTL_NUMBER_OF(Workers); Index++)
            MI_ASSERT(pthread_join(Threads[Index], NULL) == 0);
        for (Index = 0; Index < RTL_NUMBER_OF(Workers); Index++)
        {
            CHECK(NT_SUCCESS(Workers[Index].Status));
            CHECK(Workers[Index].Actual == (ULONG)MI_ATOMIC_READ32(&Db->Pfn[Frame].CacheFlags));
        }
        CHECK((ULONG)MI_ATOMIC_READ32(&Db->Pfn[Frame].CacheFlags) == World.Machine.FrameCache[Frame]);
        CHECK(World.Machine.FrameCacheCalls == Calls + 1);
        MiPfnShareDecrement(Db, Frame, TRUE);
    }
    CHECK(MiPfnDbCheck(Db) == 0);
    WorldExpectClean(&World, HM_FRAMES);
    WorldDestroy(&World);
}

void
TestHighMem(void)
{
    HighMemZones();
    HighMemWindow();
    HighMemWindowConcurrent();
    HighMemUser();
    HighMemCacheRuns();
    HighMemCacheFailure();
    HighMemCacheFailureOnReclaim();
    HighMemConcurrentFirstMapping();
}
