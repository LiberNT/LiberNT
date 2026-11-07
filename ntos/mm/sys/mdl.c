/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/sys/mdl.c
 * PURPOSE:     Memory descriptor list management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/misys.h>

#define MI_MDL_LOCK_ATTEMPTS 64

static
VOID
MiAssertMdl(
    _In_ PMI_MDL Mdl)
{
    UNREFERENCED_PARAMETER(Mdl);
    MI_ASSERT(Mdl->Space != NULL);
    MI_ASSERT(Mdl->ByteCount != 0 && Mdl->ByteOffset < PAGE_SIZE);
    MI_ASSERT((Mdl->StartVa & (PAGE_SIZE - 1)) == 0);
    MI_ASSERT(Mdl->PageCount == (((ULONG64)Mdl->ByteOffset + Mdl->ByteCount + PAGE_SIZE - 1) >> PAGE_SHIFT));
    MI_ASSERT((Mdl->MappedSystemVa & (PAGE_SIZE - 1)) == 0);
}

#if DBG || defined(MM_HOST_TEST)
static
VOID
MiAssertCopyMapping(
    _In_ PMI_MDL Mdl)
{
    ULONG i;

    MI_ASSERT(Mdl->Locked && Mdl->MappedSystemVa != 0);
    for (i = 0; i < Mdl->PageCount; i++)
    {
        PMI_PTE Slot = MiPtLookup(&Mdl->Space->System->SystemSpace,
                                  Mdl->MappedSystemVa + (ULONG64)i * PAGE_SIZE, NULL);
        MI_PTE Pte;

        MI_ASSERT(Slot != NULL);
        Pte = MiArchPteRead(Slot);
        MI_ASSERT(MiArchPteIsValid(Pte));
        MI_ASSERT(MiArchPteFrame(Pte) == Mdl->Frames[i]);
        MI_ASSERT((MiArchPteIsWritable(Pte) != FALSE) == (Mdl->WriteAccess != FALSE));
    }
}
#else
#define MiAssertCopyMapping(Mdl) ((void)0)
#endif

PMI_MDL
MiMdlAllocate(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG ByteCount)
{
    ULONG ByteOffset = (ULONG)(VirtualAddress & (PAGE_SIZE - 1));
    ULONG Pages = (ULONG)(((ULONG64)ByteOffset + ByteCount + PAGE_SIZE - 1) >> PAGE_SHIFT);
    PMI_MDL Mdl;

    if (ByteCount == 0)
        return NULL;

    Mdl = MI_ALLOCATE(FIELD_OFFSET(MI_MDL, Frames) + (SIZE_T)Pages * sizeof(MI_FRAME_NUMBER));
    if (Mdl == NULL)
        return NULL;

    RtlZeroMemory(Mdl, FIELD_OFFSET(MI_MDL, Frames));
    Mdl->Space = Space;
    Mdl->StartVa = VirtualAddress - ByteOffset;
    Mdl->ByteOffset = ByteOffset;
    Mdl->ByteCount = ByteCount;
    Mdl->PageCount = Pages;
    MiAssertMdl(Mdl);
    return Mdl;
}

VOID
MiMdlFree(
    _Inout_ PMI_MDL Mdl)
{
    MiAssertMdl(Mdl);
    MI_ASSERT(!Mdl->Locked && Mdl->MappedSystemVa == 0);
    MI_FREE(Mdl);
}

VOID
MiUnlockFrames(
    _Inout_ PMI_SYSTEM System,
    _In_ const MI_FRAME_NUMBER *Frames,
    _In_ ULONG PageCount,
    _In_ BOOLEAN WriteAccess)
{
    PMI_PFN_DATABASE Db = &System->Pfn;
    ULONG i;

    for (i = 0; i < PageCount; i++)
    {
        if (!MiPfnTracked(Db, Frames[i]) || Db->Pfn[Frames[i]].State == MiPageUnusable)
            continue;

        if (WriteAccess)
            MiPfnSetModified(Db, (ULONG)Frames[i]);

        MiPfnDereference(Db, (ULONG)Frames[i]);
    }
}

static
NTSTATUS
MiLockPagesWorker(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 StartVa,
    _In_ ULONG PageCount,
    _In_ BOOLEAN UserMode,
    _In_ BOOLEAN WriteAccess,
    _In_ BOOLEAN ConsumeGuard,
    _Out_ PMI_FRAME_NUMBER Frames,
    _Out_opt_ PULONG CacheFlags)
{
    PMI_PFN_DATABASE Db = &Space->System->Pfn;
    ULONG i;

    StartVa &= ~((ULONG64)PAGE_SIZE - 1);

    if (PageCount == 0 || StartVa < Space->LowestVa || StartVa > Space->HighestVa ||
        ((ULONG64)PageCount << PAGE_SHIFT) - 1 > Space->HighestVa - StartVa)
    {
        return STATUS_ACCESS_VIOLATION;
    }

    for (i = 0; i < PageCount; i++)
    {
        ULONG64 Va = StartVa + (ULONG64)i * PAGE_SIZE;
        NTSTATUS Status = STATUS_WORKING_SET_QUOTA;
        ULONG Attempt;

        for (Attempt = 0; Attempt < MI_MDL_LOCK_ATTEMPTS; Attempt++)
        {
            MI_PTE Pte;
            ULONG64 Physical;

            MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

            if (MiPtTranslate(Space, Va, &Physical, &Pte) && (!UserMode || MiArchPteIsUser(Pte)) &&
                (!WriteAccess || (MiArchPteIsWritable(Pte) && MiArchPteIsDirty(Pte))))
            {
                ULONG64 Frame = Physical >> PAGE_SHIFT;

                if (MiPfnTracked(Db, Frame) && Db->Pfn[Frame].State != MiPageUnusable)
                {
                    KIRQL OldIrql = MiPfnLock(Db, (ULONG)Frame);

                    if (!(MI_PFN_FLAGS(&Db->Pfn[Frame]) & MI_PFN_FLAG_PAGE_TABLE))
                        MI_ATOMIC_WRITE32(&Db->Pfn[Frame].CacheFlags,
                                          MiArchPteLeafFlags(Pte) & MI_LEAF_CACHE_MASK);
                    MiPfnReferenceLocked(Db, (ULONG)Frame);
                    MiPfnUnlock(Db, (ULONG)Frame, OldIrql);
                }

                Frames[i] = (MI_FRAME_NUMBER)Frame;
                if (CacheFlags != NULL)
                    CacheFlags[i] = MiArchPteLeafFlags(Pte) & MI_LEAF_CACHE_MASK;
                MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
                Status = STATUS_SUCCESS;
                break;
            }

            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);

            Status = ConsumeGuard
                ? MiFaultWithWriteAllowance(Space, Va, WriteAccess ? MiFaultWrite : MiFaultRead, UserMode, TRUE)
                : MiFaultForCopy(Space, Va, UserMode, WriteAccess);
            if (!NT_SUCCESS(Status))
                break;

            Status = STATUS_WORKING_SET_QUOTA;
        }

        if (!NT_SUCCESS(Status))
        {
            MiUnlockFrames(Space->System, Frames, i, WriteAccess);
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
MiLockPages(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 StartVa,
    _In_ ULONG PageCount,
    _In_ BOOLEAN UserMode,
    _In_ BOOLEAN WriteAccess,
    _Out_ PMI_FRAME_NUMBER Frames)
{
    return MiLockPagesWorker(Space, StartVa, PageCount, UserMode, WriteAccess, TRUE, Frames, NULL);
}

NTSTATUS
MiLockSelectedPages(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ const ULONG64 *Addresses,
    _In_ ULONG PageCount,
    _In_ BOOLEAN UserMode,
    _In_ BOOLEAN WriteAccess,
    _Out_ PMI_FRAME_NUMBER Frames)
{
    ULONG i;

    if (Addresses == NULL || Frames == NULL || PageCount == 0)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < PageCount; i++)
    {
        PMI_ADDRESS_SPACE Target = Space;
        ULONG64 Va = Addresses[i];
        NTSTATUS Status;

        if (!UserMode && Va >= Space->System->SystemSpace.LowestVa)
            Target = &Space->System->SystemSpace;
        Status = (Va & (PAGE_SIZE - 1)) ? STATUS_INVALID_PARAMETER
            : MiLockPages(Target, Va, 1, UserMode, WriteAccess, &Frames[i]);
        if (!NT_SUCCESS(Status))
        {
            MiUnlockFrames(Space->System, Frames, i, WriteAccess);
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
MiMapFrames(
    _Inout_ PMI_SYSTEM System,
    _In_ const MI_FRAME_NUMBER *Frames,
    _In_ ULONG PageCount,
    _In_ MI_CACHE_TYPE CacheType,
    _In_ ULONG Protection,
    _Out_ PULONG64 Base)
{
    ULONG Flags = (CacheType == MiCacheNone) ? MI_LEAF_NOCACHE
                                             : ((CacheType == MiCacheWriteCombined) ? MI_LEAF_WRITECOMBINE : 0);
    NTSTATUS Status;

    Status = MiReserveSystemPtesEx(System, PageCount, Base);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = MiSystemMapFrames(System, *Base, Frames, PageCount, Protection, Flags | MI_LEAF_PFN_CACHE, FALSE);
    if (!NT_SUCCESS(Status))
    {
        MiReleaseSystemPtes(System, *Base, PageCount);
        *Base = 0;
    }

    return Status;
}

VOID
MiUnmapFrames(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 Base,
    _In_ ULONG PageCount)
{
    MiSystemUnmap(System, Base, PageCount, FALSE);
    MiReleaseSystemPtes(System, Base, PageCount);
}

NTSTATUS
MiProbeAndLockPages(
    _Inout_ PMI_MDL Mdl,
    _In_ BOOLEAN UserMode,
    _In_ BOOLEAN WriteAccess)
{
    NTSTATUS Status;

    MiAssertMdl(Mdl);
    if (Mdl->Locked)
        return STATUS_INVALID_PARAMETER;

    Mdl->WriteAccess = WriteAccess;
    Status = MiLockPages(Mdl->Space, Mdl->StartVa, Mdl->PageCount, UserMode, WriteAccess, Mdl->Frames);
    if (NT_SUCCESS(Status))
        Mdl->Locked = TRUE;

    MI_ASSERT(Mdl->Locked == (BOOLEAN)NT_SUCCESS(Status));
    return Status;
}

NTSTATUS
MiProbeAndLockPagesForCopy(
    _Inout_ PMI_MDL Mdl,
    _In_ BOOLEAN UserMode,
    _In_ BOOLEAN WriteAccess)
{
    NTSTATUS Status;

    MiAssertMdl(Mdl);
    if (Mdl->Locked)
        return STATUS_INVALID_PARAMETER;

    Mdl->WriteAccess = WriteAccess;
    Status = MiLockPagesWorker(Mdl->Space, Mdl->StartVa, Mdl->PageCount, UserMode, WriteAccess, FALSE,
                                Mdl->Frames, NULL);
    if (NT_SUCCESS(Status))
        Mdl->Locked = TRUE;

    MI_ASSERT(Mdl->Locked == (BOOLEAN)NT_SUCCESS(Status));
    return Status;
}

NTSTATUS
MiMapPagesForCopy(
    _Inout_ PMI_MDL Mdl,
    _In_ BOOLEAN UserMode,
    _In_ BOOLEAN WriteAccess)
{
    PMI_SYSTEM System = Mdl->Space->System;
    PULONG CacheFlags;
    NTSTATUS Status;
    ULONG Page;

    MiAssertMdl(Mdl);
    if (Mdl->Locked || Mdl->MappedSystemVa != 0)
        return STATUS_INVALID_PARAMETER;

    CacheFlags = MI_ALLOCATE((SIZE_T)Mdl->PageCount * sizeof(*CacheFlags));
    if (CacheFlags == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Mdl->WriteAccess = WriteAccess;
    Status = MiLockPagesWorker(Mdl->Space, Mdl->StartVa, Mdl->PageCount, UserMode, WriteAccess, FALSE,
                                Mdl->Frames, CacheFlags);
    if (NT_SUCCESS(Status))
    {
        Mdl->Locked = TRUE;
        Mdl->MappedSystemVa = MiReserveSystemPtes(System, Mdl->PageCount);
        if (Mdl->MappedSystemVa == 0)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            for (Page = 0; Page < Mdl->PageCount; Page++)
            {
                Status = MiSystemMapFrames(System, Mdl->MappedSystemVa + (ULONG64)Page * PAGE_SIZE,
                                            &Mdl->Frames[Page], 1,
                                            WriteAccess ? MI_PROT_READWRITE : MI_PROT_READONLY,
                                            CacheFlags[Page], FALSE);
                if (!NT_SUCCESS(Status))
                    break;
            }
        }
        if (!NT_SUCCESS(Status))
            MiUnlockPages(Mdl);
    }
    MI_FREE(CacheFlags);
    if (NT_SUCCESS(Status))
        MiAssertCopyMapping(Mdl);
    else
        MI_ASSERT(!Mdl->Locked && Mdl->MappedSystemVa == 0);
    return Status;
}

VOID
MiUnlockPages(
    _Inout_ PMI_MDL Mdl)
{
    MiAssertMdl(Mdl);
    MI_ASSERT(Mdl->Locked);

    if (Mdl->MappedSystemVa != 0)
        MiUnmapLockedPages(Mdl);
    MI_ASSERT(Mdl->MappedSystemVa == 0);

    MiUnlockFrames(Mdl->Space->System, Mdl->Frames, Mdl->PageCount, Mdl->WriteAccess);
    Mdl->Locked = FALSE;
    MiAssertMdl(Mdl);
    MI_ASSERT(Mdl->MappedSystemVa == 0);
}

NTSTATUS
MiBuildMdlForSystemRange(
    _Inout_ PMI_MDL Mdl)
{
    ULONG i;

    MiAssertMdl(Mdl);
    for (i = 0; i < Mdl->PageCount; i++)
    {
        ULONG64 Physical = MiGetPhysicalAddress(Mdl->Space, Mdl->StartVa + (ULONG64)i * PAGE_SIZE);

        if (Physical == 0)
            return STATUS_ACCESS_VIOLATION;

        Mdl->Frames[i] = (MI_FRAME_NUMBER)(Physical >> PAGE_SHIFT);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
MiMapLockedPages(
    _Inout_ PMI_MDL Mdl,
    _In_ MI_CACHE_TYPE CacheType,
    _Out_ PULONG64 SystemVa)
{
    NTSTATUS Status;

    MiAssertMdl(Mdl);
    *SystemVa = 0;

    if (Mdl->MappedSystemVa == 0)
    {
        Status = MiMapFrames(Mdl->Space->System, Mdl->Frames, Mdl->PageCount, CacheType, MI_PROT_READWRITE,
                             &Mdl->MappedSystemVa);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    MiAssertMdl(Mdl);
    MI_ASSERT(Mdl->MappedSystemVa != 0);
    *SystemVa = Mdl->MappedSystemVa + Mdl->ByteOffset;
    return STATUS_SUCCESS;
}

VOID
MiUnmapLockedPages(
    _Inout_ PMI_MDL Mdl)
{
    MiAssertMdl(Mdl);
    if (Mdl->MappedSystemVa == 0)
        return;

    MiUnmapFrames(Mdl->Space->System, Mdl->MappedSystemVa, Mdl->PageCount);
    Mdl->MappedSystemVa = 0;
}

VOID
MiUnmapLockedPagesLocal(
    _Inout_ PMI_MDL Mdl)
{
    MiAssertMdl(Mdl);
    if (Mdl->MappedSystemVa == 0)
        return;

    MiSystemUnmapLocal(Mdl->Space->System, Mdl->MappedSystemVa, Mdl->PageCount);
    MiReleaseSystemPtes(Mdl->Space->System, Mdl->MappedSystemVa, Mdl->PageCount);
    Mdl->MappedSystemVa = 0;
}
