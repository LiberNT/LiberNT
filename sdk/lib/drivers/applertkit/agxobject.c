/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Memory of the objects the Apple silicon graphics firmware reaches
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/appleadt.h>
#include <reactos/drivers/appleuat.h>
#include <reactos/drivers/appleagxinit.h>

#define AGX_OBJECT_PAGES            (APPLE_UAT_PAGE_SIZE / PAGE_SIZE)

static
ULONG64
AgxObjectChunk(
    _In_ const APPLE_AGX_OBJECT *Object,
    _In_ SIZE_T Offset)
{
    PPFN_NUMBER Frames = MmGetMdlPfnArray(Object->Mdl);

    return (ULONG64)Frames[Offset >> PAGE_SHIFT] << PAGE_SHIFT;
}

/* Every unit of the graphics address tables must be one physically contiguous, aligned run of the
 * processor's pages. */
static
BOOLEAN
AgxObjectChunksValid(
    _In_ PMDL Mdl,
    _In_ SIZE_T Bytes)
{
    PPFN_NUMBER Frames = MmGetMdlPfnArray(Mdl);
    SIZE_T Page, Pages = Bytes >> PAGE_SHIFT;

    if (MmGetMdlByteCount(Mdl) != Bytes)
        return FALSE;
    for (Page = 0; Page < Pages; ++Page)
    {
        if ((Page % AGX_OBJECT_PAGES) == 0 ? (Frames[Page] % AGX_OBJECT_PAGES) != 0
                                            : Frames[Page] != Frames[Page - 1] + 1)
        {
            return FALSE;
        }
    }
    return TRUE;
}

VOID
NTAPI
AppleAgxObjectFree(
    _Inout_ PAPPLE_AGX_OBJECT Object)
{
    if (Object->Mapping)
        MmUnmapLockedPages(Object->Mapping, Object->Mdl);
    if (Object->Mdl)
    {
        MmFreePagesFromMdl(Object->Mdl);
        ExFreePool(Object->Mdl);
    }
    RtlZeroMemory(Object, sizeof(*Object));
}

NTSTATUS
NTAPI
AppleAgxObjectCreate(
    _Inout_ PAPPLE_AGX_HEAP Heap,
    _In_ const APPLE_AGX_REQUEST *Request,
    _Out_ PAPPLE_AGX_OBJECT Object)
{
    APPLE_UAT_PROTECTION Protection;
    PHYSICAL_ADDRESS Low, High, Skip;
    ULONG64 Page, Offset;
    SIZE_T Bytes, At;
    NTSTATUS Status;

    RtlZeroMemory(Object, sizeof(*Object));
    Status = AppleAgxObjectPlace(Heap, Request, &Page, &Bytes);
    if (!NT_SUCCESS(Status))
        return Status;
    Offset = Request->At ? (Request->At & (APPLE_UAT_PAGE_SIZE - 1)) : Request->Offset;
    Low.QuadPart = 0;
    High.QuadPart = (LONGLONG)((1ULL << APPLE_UAT_OUTPUT_BITS) - 1);
    Skip.QuadPart = APPLE_UAT_PAGE_SIZE;
    Object->Mdl = MmAllocatePagesForMdlEx(Low, High, Skip, Bytes, MmWriteCombined,
                                          MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED);
    if (!Object->Mdl || !AgxObjectChunksValid(Object->Mdl, Bytes))
    {
        AppleAgxObjectFree(Object);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Object->Page = Page;
    Object->Bytes = Bytes;
    Object->Address = Page ? Page + Offset : 0;
    Object->Class = Request->Class;
    Object->Leaf = Request->Leaf;
    Object->Lower = Request->Lower;
    if (Request->Client)
    {
        Object->ClientPage = Request->Client;
        Object->Client = Request->Client + Offset;
    }
    if (Request->Cpu)
    {
        Object->Mapping = MmMapLockedPagesSpecifyCache(Object->Mdl, KernelMode, MmWriteCombined, NULL, FALSE,
                                                       NormalPagePriority | MdlMappingNoExecute);
        if (!Object->Mapping)
        {
            AppleAgxObjectFree(Object);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Object->Mapping, Bytes);
        Object->Virtual = (PUCHAR)Object->Mapping + Offset;
    }
    KeMemoryBarrier();
    if (Request->Hidden)
        return STATUS_SUCCESS;
    for (At = 0; At < Bytes; At += APPLE_UAT_PAGE_SIZE)
    {
        AppleAgxClassProtection(At < Request->CachedBytes ? APPLE_AGX_CLASS_FIRMWARE_CACHED : Request->Class,
                                &Protection);
        Status = AppleUatAdoptedMap(Heap->Adopted, Page + At, AgxObjectChunk(Object, At), APPLE_UAT_PAGE_SIZE,
                                    &Protection);
        if (!NT_SUCCESS(Status))
            break;
    }
    if (NT_SUCCESS(Status) && Request->Lower)
        Status = AppleAgxObjectAlias(Object, &Heap->Adopted->Lower, Object->ClientPage);
    if (!NT_SUCCESS(Status))
    {
        if (At)
            AppleUatUnmap(&Heap->Adopted->Host, Page, At);
        AppleAgxObjectFree(Object);
        return Status;
    }
    Heap->Pages += (ULONG)(Bytes / APPLE_UAT_PAGE_SIZE);
    return STATUS_SUCCESS;
}

/* A client address space reaches the same pages at the object's client address. */
NTSTATUS
NTAPI
AppleAgxObjectAlias(
    _In_ const APPLE_AGX_OBJECT *Object,
    _Inout_ PAPPLE_UAT Uat,
    _In_ ULONG64 Address)
{
    APPLE_UAT_PROTECTION Protection;
    SIZE_T At;
    NTSTATUS Status;

    if (!Object->Mdl || !Object->Leaf || !Address || (Address & (APPLE_UAT_PAGE_SIZE - 1)))
        return STATUS_INVALID_PARAMETER;
    AppleAgxLeafProtection(Object->Leaf, &Protection);
    for (At = 0; At < Object->Bytes; At += APPLE_UAT_PAGE_SIZE)
    {
        Status = AppleUatMap(Uat, Address + At, AgxObjectChunk(Object, At), APPLE_UAT_PAGE_SIZE, &Protection);
        if (!NT_SUCCESS(Status))
        {
            if (At)
                AppleUatUnmap(Uat, Address, At);
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleAgxObjectZero(
    _In_ const APPLE_AGX_OBJECT *Object,
    _In_ SIZE_T Offset,
    _In_ SIZE_T Length,
    _In_ UCHAR Fill)
{
    SIZE_T Start = (SIZE_T)(Object->Address - Object->Page) + Offset;

    ASSERT(Object->Mapping);
    ASSERT(Start <= Object->Bytes && Length <= Object->Bytes - Start);
    RtlFillMemory((PUCHAR)Object->Mapping + Start, Length, Fill);
    KeMemoryBarrier();
}

/* The objects of the initial data sit at the addresses this firmware build was linked to look for
 * them at, each one contiguous for the processor however many pages it spans. The timestamp arena
 * is only named: the clients' timestamp buffers are mapped into it when they are bound. */
NTSTATUS
NTAPI
AppleAgxInitCreate(
    _Out_ PAPPLE_AGX_INIT Init,
    _In_ PAPPLE_UAT_ADOPTED Adopted,
    _In_z_ PCSTR Owner)
{
    APPLE_AGX_HEAP Heap;
    APPLE_AGX_REQUEST Request;
    APPLE_AGX_LAYOUT Layout;
    ULONG Object;
    NTSTATUS Status;

    RtlZeroMemory(Init, sizeof(*Init));
    RtlZeroMemory(&Heap, sizeof(Heap));
    Init->Adopted = Adopted;
    Init->Owner = Owner;
    Heap.Adopted = Adopted;
    Heap.Owner = Owner;
    for (Object = 0; Object < APPLE_AGX_OBJECTS; ++Object)
    {
        if (!AppleAgxLayout(Object, &Layout) || !Layout.Size)
        {
            AppleAgxInitRelease(Init);
            return STATUS_INVALID_PARAMETER;
        }
        if (Object == APPLE_AGX_OBJECT_TIMESTAMPS)
            continue;
        RtlZeroMemory(&Request, sizeof(Request));
        Request.At = Layout.Address;
        Request.Size = Layout.Size;
        Request.Class = Layout.Class;
        Request.Cpu = TRUE;
        if (Object == APPLE_AGX_OBJECT_HWDATA)
        {
            Request.CachedBytes = (ULONG)(((Layout.Address & (APPLE_UAT_PAGE_SIZE - 1)) + APPLE_AGX_HWDATA_CACHED +
                                           APPLE_UAT_PAGE_SIZE - 1) & ~(APPLE_UAT_PAGE_SIZE - 1));
        }
        Status = AppleAgxObjectCreate(&Heap, &Request, &Init->Object[Object]);
        Init->Pages = Init->Allocations = Heap.Pages;
        if (!NT_SUCCESS(Status))
        {
            AppleAgxInitRelease(Init);
            return Status;
        }
        Init->Virtual[Object] = Init->Object[Object].Virtual;
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleAgxInitRelease(
    _Inout_ PAPPLE_AGX_INIT Init)
{
    ULONG Object;

    for (Object = 0; Object < APPLE_AGX_OBJECTS; ++Object)
        AppleAgxObjectFree(&Init->Object[Object]);
    RtlZeroMemory(Init, sizeof(*Init));
}
