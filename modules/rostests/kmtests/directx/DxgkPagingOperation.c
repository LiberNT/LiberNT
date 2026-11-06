/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Paging operation validation
 *
 * Paging packets are how memory physically moves.  A transfer that names the
 * same segment twice, or a TLB flush of zero length, reports success while
 * leaving the GPU reading stale translations.
 */

#include <kmt_test.h>
#include "paging_core.h"

#define LEVELS 4

static VOID InitRequest(_Out_ PDXGK_PAGING_CORE_REQUEST Request, _In_ DXGK_PAGING_CORE_OP Op)
{
    RtlZeroMemory(Request, sizeof(*Request));
    Request->Operation = Op;
    Request->SystemMemoryPresent = TRUE;
}

static VOID TestOperationRange(VOID)
{
    DXGK_PAGING_CORE_REQUEST Request;

    InitRequest(&Request, DxgkPagingCoreOpNone);
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    InitRequest(&Request, DxgkPagingCoreOpMax);
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    InitRequest(&Request, (DXGK_PAGING_CORE_OP)999);
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestTransfer(VOID)
{
    DXGK_PAGING_CORE_REQUEST Request;

    InitRequest(&Request, DxgkPagingCoreOpTransfer);
    Request.TransferSize = 0x1000;
    Request.SourceSegmentId = 0;      /* system memory */
    Request.DestinationSegmentId = 1; /* VRAM */
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_SUCCESS); }

    Request.SourceSegmentId = 1;
    Request.DestinationSegmentId = 2;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* A transfer to and from the same segment is not a copy. */
    Request.SourceSegmentId = 1;
    Request.DestinationSegmentId = 1;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* System memory to system memory never involves the GPU. */
    Request.SourceSegmentId = 0;
    Request.DestinationSegmentId = 0;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* Naming system memory without any backing to move is a caller bug that
     * would otherwise dereference nothing. */
    InitRequest(&Request, DxgkPagingCoreOpTransfer);
    Request.TransferSize = 0x1000;
    Request.SourceSegmentId = 0;
    Request.DestinationSegmentId = 1;
    Request.SystemMemoryPresent = FALSE;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    InitRequest(&Request, DxgkPagingCoreOpTransfer);
    Request.TransferSize = 0;
    Request.SourceSegmentId = 0;
    Request.DestinationSegmentId = 1;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestFillAndAperture(VOID)
{
    DXGK_PAGING_CORE_REQUEST Request;

    InitRequest(&Request, DxgkPagingCoreOpFill);
    Request.TransferSize = 0x1000;
    Request.DestinationSegmentId = 1;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_SUCCESS); }
    Request.DestinationSegmentId = 0;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    Request.DestinationSegmentId = 1;
    Request.TransferSize = 0;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* Segment 0 is system memory and has no aperture to map into. */
    InitRequest(&Request, DxgkPagingCoreOpMapAperture);
    Request.DestinationSegmentId = 1;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_SUCCESS); }
    Request.DestinationSegmentId = 0;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* Mapping an aperture requires pages to map; unmapping does not. */
    InitRequest(&Request, DxgkPagingCoreOpMapAperture);
    Request.DestinationSegmentId = 1;
    Request.SystemMemoryPresent = FALSE;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    InitRequest(&Request, DxgkPagingCoreOpUnmapAperture);
    Request.DestinationSegmentId = 1;
    Request.SystemMemoryPresent = FALSE;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_SUCCESS); }

    InitRequest(&Request, DxgkPagingCoreOpDiscardContent);
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_SUCCESS); }
    InitRequest(&Request, DxgkPagingCoreOpNotifyResidency);
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_SUCCESS); }
}

static VOID TestPlacementClear(VOID)
{
    DXGK_SEGMENTFLAGS Flags;

    RtlZeroMemory(&Flags, sizeof(Flags));
    ok_eq_int(DxgkPagingCorePlacementClearOperation(Flags), DxgkPagingCoreOpFill);

    Flags.CpuVisible = 1;
    Flags.CacheCoherent = 1;
    ok_eq_int(DxgkPagingCorePlacementClearOperation(Flags), DxgkPagingCoreOpFill);

    Flags.PopulatedFromSystemMemory = 1;
    ok_eq_int(DxgkPagingCorePlacementClearOperation(Flags), DxgkPagingCoreOpFill);

    RtlZeroMemory(&Flags, sizeof(Flags));
    Flags.Aperture = 1;
    Flags.CacheCoherent = 1;
    ok_eq_int(DxgkPagingCorePlacementClearOperation(Flags), DxgkPagingCoreOpNone);

    RtlZeroMemory(&Flags, sizeof(Flags));
    Flags.Agp = 1;
    ok_eq_int(DxgkPagingCorePlacementClearOperation(Flags), DxgkPagingCoreOpNone);

    RtlZeroMemory(&Flags, sizeof(Flags));
    Flags.CpuVisible = 1;
    Flags.PitchAlignment = 1;
    ok_eq_int(DxgkPagingCorePlacementClearOperation(Flags), DxgkPagingCoreOpNone);
}

static VOID TestPageTableAndTlb(VOID)
{
    DXGK_PAGING_CORE_REQUEST Request;

    InitRequest(&Request, DxgkPagingCoreOpUpdatePageTable);
    Request.PageTableLevel = 0;
    Request.GpuVirtualAddress = 0x1000;
    Request.SizeInBytes = 0x1000;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_SUCCESS); }
    Request.PageTableLevel = LEVELS - 1;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* A level the miniport did not declare indexes past the level array. */
    Request.PageTableLevel = LEVELS;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    InitRequest(&Request, DxgkPagingCoreOpUpdatePageTable);
    Request.SizeInBytes = 0;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    InitRequest(&Request, DxgkPagingCoreOpUpdatePageTable);
    Request.GpuVirtualAddress = MAXULONGLONG - 0xFF;
    Request.SizeInBytes = 0x1000;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INTEGER_OVERFLOW); }

    /*
     * A zero-length flush is the dangerous one: it reports success while
     * leaving every stale translation in place.
     */
    InitRequest(&Request, DxgkPagingCoreOpFlushTlb);
    Request.GpuVirtualAddress = 0x1000;
    Request.SizeInBytes = 0;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    Request.SizeInBytes = 0x1000;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_SUCCESS); }
    Request.GpuVirtualAddress = MAXULONGLONG;
    { NTSTATUS Observed = DxgkPagingCoreValidate(&Request, LEVELS); ok_eq_hex(Observed, STATUS_INTEGER_OVERFLOW); }
}

static VOID TestContextResourceHints(VOID)
{
    PUCHAR Object;
    PVOID Mapping;
    PMDL Mdl;
    PHYSICAL_ADDRESS Low, High, Skip;
    ULONG Class;
    PVOID Resource;

    Object = ExAllocatePoolWithTag(NonPagedPool, 0x800, 'tHxD');
    if (skip(Object != NULL, "No pool\n"))
        return;
    RtlZeroMemory(Object, 0x800);
    *(PULONG)(Object + 0x10) = 7;
    *(PVOID *)(Object + 0x758) = Object;
    ok_eq_bool(DxgkPagingCoreReadContextResourceHints(Object, &Class, &Resource), TRUE);
    ok_eq_ulong(Class, 7UL);
    ok_eq_pointer(Resource, Object);
    ExFreePoolWithTag(Object, 'tHxD');

    Class = 1;
    Resource = &Class;
    ok_eq_bool(DxgkPagingCoreReadContextResourceHints(NULL, &Class, &Resource), FALSE);
    ok_eq_ulong(Class, 0UL);
    ok_eq_pointer(Resource, NULL);

    /* An object that ends before +0x758, followed by an unmapped page. */
    Mapping = MmAllocateMappingAddress(2 * PAGE_SIZE, 'tHxD');
    if (skip(Mapping != NULL, "No reserved mapping\n"))
        return;
    Low.QuadPart = 0;
    High.QuadPart = MAXLONGLONG;
    Skip.QuadPart = 0;
    Mdl = MmAllocatePagesForMdl(Low, High, Skip, PAGE_SIZE);
    if (!skip(Mdl != NULL && MmGetMdlByteCount(Mdl) == PAGE_SIZE, "No page\n"))
    {
        PUCHAR Page = MmMapLockedPagesWithReservedMapping(Mapping, 'tHxD', Mdl, MmCached);

        ok(Page != NULL, "MmMapLockedPagesWithReservedMapping failed\n");
        if (Page != NULL)
        {
            Object = Page + PAGE_SIZE - 0x100;
            RtlZeroMemory(Object, 0x100);
            *(PULONG)(Object + 0x10) = 7;
            ok_eq_bool(MmIsAddressValid(Page + PAGE_SIZE), FALSE);
            Class = 1;
            Resource = &Class;
            ok_eq_bool(DxgkPagingCoreReadContextResourceHints(Object, &Class, &Resource), FALSE);
            ok_eq_ulong(Class, 0UL);
            ok_eq_pointer(Resource, NULL);
            MmUnmapReservedMapping(Mapping, 'tHxD', Mdl);
        }
    }
    if (Mdl != NULL)
    {
        MmFreePagesFromMdl(Mdl);
        ExFreePool(Mdl);
    }
    MmFreeMappingAddress(Mapping, 'tHxD');
}

START_TEST(DxgkPagingOperation)
{
    TestOperationRange();
    TestTransfer();
    TestFillAndAperture();
    TestPlacementClear();
    TestPageTableAndTlb();
    TestContextResourceHints();
}

/* EOF */
