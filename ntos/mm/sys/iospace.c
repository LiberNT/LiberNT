/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/sys/iospace.c
 * PURPOSE:     I/O address-space mapping
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/misys.h>

static
ULONG
MiCacheTypeLeafFlags(
    _In_ MI_CACHE_TYPE CacheType)
{
    if (CacheType == MiCacheNone)
        return MI_LEAF_DEVICE | MI_LEAF_NOCACHE;

    if (CacheType == MiCacheWriteCombined)
        return MI_LEAF_WRITECOMBINE;

    return 0;
}

NTSTATUS
MiMapIoSpace(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 PhysicalAddress,
    _In_ ULONG64 Bytes,
    _In_ MI_CACHE_TYPE CacheType,
    _In_ ULONG Protection,
    _Out_ PULONG64 VirtualAddress)
{
    ULONG64 Offset = PhysicalAddress & (PAGE_SIZE - 1);
    ULONG64 Pages = (Offset + Bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;
    ULONG Flags = MiCacheTypeLeafFlags(CacheType);
    ULONG64 Base;
    ULONG64 i;

    *VirtualAddress = 0;

    if (Bytes == 0 || Pages > 0xFFFFFFFFULL ||
        (PhysicalAddress + Bytes < PhysicalAddress && PhysicalAddress + Bytes != 0))
        return STATUS_INVALID_PARAMETER;

    Base = MiReserveSystemPtes(System, (ULONG)Pages);
    if (Base == 0)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (i = 0; i < Pages; i++)
    {
        MI_FRAME_NUMBER Frame = (MI_FRAME_NUMBER)((PhysicalAddress >> PAGE_SHIFT) + i);
        NTSTATUS Status = MiSystemMapFrames(System, Base + i * PAGE_SIZE, &Frame, 1, Protection, Flags, FALSE);

        if (!NT_SUCCESS(Status))
        {
            MiSystemUnmap(System, Base, (ULONG)i, FALSE);
            MiReleaseSystemPtes(System, Base, (ULONG)Pages);
            return Status;
        }
    }

    *VirtualAddress = Base + Offset;
    return STATUS_SUCCESS;
}

VOID
MiUnmapIoSpace(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 Bytes)
{
    ULONG64 Offset = VirtualAddress & (PAGE_SIZE - 1);
    ULONG64 Pages = (Offset + Bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;
    ULONG64 Base = VirtualAddress - Offset;

    MiSystemUnmap(System, Base, (ULONG)Pages, FALSE);
    MiReleaseSystemPtes(System, Base, (ULONG)Pages);
}

static
VOID
MiIoSetAllocationHeader(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Base,
    _In_ ULONG64 Pages)
{
    PMI_PTE Header = MiPtLookup(Space, Base, NULL);

    MI_ASSERT(Header != NULL);
    MI_ASSERT(MiArchPteRead(Header) == 0);
    MI_ASSERT(Pages != 0 && Pages <= 0x7FFFFFFFULL);
    MiArchPteWrite(Header, (MI_PTE)Pages << 1);
}

NTSTATUS
MiAllocateContiguousMemory(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 Bytes,
    _In_ ULONG64 LowestAddress,
    _In_ ULONG64 HighestAddress,
    _In_ ULONG64 BoundaryBytes,
    _In_ MI_CACHE_TYPE CacheType,
    _In_ ULONG Protection,
    _Out_ PULONG64 VirtualAddress)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    ULONG64 Pages = (Bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;
    ULONG64 HighFrame = HighestAddress >> PAGE_SHIFT;
    ULONG First;
    ULONG64 Base;
    ULONG64 i;
    NTSTATUS Status;

    *VirtualAddress = 0;

    if (Bytes == 0 || Pages > 0x7FFFFFFFULL)
        return STATUS_INVALID_PARAMETER;

    if (!MiChargeCommit(Space, (LONG64)Pages))
        return STATUS_COMMITMENT_LIMIT;

    First = MiPfnAllocateContiguous(&System->Pfn, (ULONG)Pages,
                                    (ULONG)((LowestAddress + PAGE_SIZE - 1) >> PAGE_SHIFT),
                                    (HighFrame > 0xFFFFFFFEULL) ? 0xFFFFFFFEUL : (ULONG)HighFrame,
                                    (ULONG)(BoundaryBytes >> PAGE_SHIFT));
    if (First == MI_FRAME_INVALID)
    {
        MiReturnCommit(Space, (LONG64)Pages);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = MiReserveSystemPtesEx(System, (ULONG)Pages + 1, &Base);
    if (NT_SUCCESS(Status))
    {
        MiIoSetAllocationHeader(Space, Base, Pages);
        Base += PAGE_SIZE;
    }

    for (i = 0; Base != 0 && i < Pages; i++)
    {
        MI_FRAME_NUMBER Frame = First + (ULONG)i;

        if (!NT_SUCCESS(MiSystemMapFrames(System, Base + i * PAGE_SIZE, &Frame, 1, Protection,
                                          MiCacheTypeLeafFlags(CacheType) & ~MI_LEAF_DEVICE, TRUE)))
        {
            MiSystemUnmap(System, Base, (ULONG)i, FALSE);
            MiArchPteWrite(MiPtLookup(Space, Base - PAGE_SIZE, NULL), 0);
            MiReleaseSystemPtes(System, Base - PAGE_SIZE, (ULONG)Pages + 1);
            Base = 0;
        }
    }

    if (Base == 0)
    {
        for (i = 0; i < Pages; i++)
            MiPfnShareDecrement(&System->Pfn, First + (ULONG)i, TRUE);

        MiReturnCommit(Space, (LONG64)Pages);
        return NT_SUCCESS(Status) ? STATUS_INSUFFICIENT_RESOURCES : Status;
    }

    *VirtualAddress = Base;
    return STATUS_SUCCESS;
}

NTSTATUS
MiAllocateNonCachedMemory(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 Bytes,
    _Out_ PULONG64 VirtualAddress)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    ULONG64 Pages = (Bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;
    ULONG Flags = MiCacheTypeLeafFlags(MiCacheNone) & ~MI_LEAF_DEVICE;
    NTSTATUS Status;
    ULONG64 Base;
    ULONG64 i;

    *VirtualAddress = 0;

    if (Bytes == 0 || Bytes >= 0x100000000ULL)
        return STATUS_INVALID_PARAMETER;

    if (!MiChargeCommit(Space, (LONG64)Pages))
        return STATUS_COMMITMENT_LIMIT;

    Status = MiReserveSystemPtesEx(System, (ULONG)Pages + 1, &Base);
    if (!NT_SUCCESS(Status))
    {
        MiReturnCommit(Space, (LONG64)Pages);
        return Status;
    }

    MiIoSetAllocationHeader(Space, Base, Pages);
    Base += PAGE_SIZE;

    for (i = 0; i < Pages; i++)
    {
        MI_FRAME_NUMBER Frame = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);

        if (Frame == MI_FRAME_INVALID)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = MiSystemMapFrames(System, Base + i * PAGE_SIZE, &Frame, 1, MI_PROT_READWRITE, Flags, TRUE);
        if (!NT_SUCCESS(Status))
        {
            MiPfnShareDecrement(&System->Pfn, (ULONG)Frame, TRUE);
            break;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        MiSystemUnmap(System, Base, (ULONG)i, TRUE);
        MiArchPteWrite(MiPtLookup(Space, Base - PAGE_SIZE, NULL), 0);
        MiReleaseSystemPtes(System, Base - PAGE_SIZE, (ULONG)Pages + 1);
        MiReturnCommit(Space, (LONG64)Pages);
        return Status;
    }

    *VirtualAddress = Base;
    return STATUS_SUCCESS;
}

VOID
MiFreeContiguousMemory(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress)
{
    PMI_PTE Header = MiPtLookup(&System->SystemSpace, VirtualAddress - PAGE_SIZE, NULL);
    MI_PTE HeaderValue;
    ULONG64 Pages;

    MI_ASSERT((VirtualAddress & (PAGE_SIZE - 1)) == 0);
    MI_ASSERT(Header != NULL);
    HeaderValue = MiArchPteRead(Header);
    Pages = HeaderValue >> 1;
    MI_ASSERT(Pages != 0 && Pages <= 0x7FFFFFFFULL);
    MI_ASSERT(!MiArchPteIsValid(HeaderValue));

    MiSystemUnmap(System, VirtualAddress, (ULONG)Pages, TRUE);
    MiArchPteWrite(Header, 0);
    MiReleaseSystemPtes(System, VirtualAddress - PAGE_SIZE, (ULONG)Pages + 1);
    MiReturnCommit(&System->SystemSpace, (LONG64)Pages);
}
