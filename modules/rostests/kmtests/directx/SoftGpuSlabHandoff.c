/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SoftGPU contiguous slab handoff between a stopped and a starting adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <kmt_test.h>
#include "slab_handoff_core.h"

static SOFTGPU_SLAB
MakeSlab(
    _In_ ULONG_PTR Base,
    _In_ SIZE_T Size,
    _In_ ULONGLONG Physical,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    SOFTGPU_SLAB Slab;

    Slab.Base = (PVOID)Base;
    Slab.Size = Size;
    Slab.Physical.QuadPart = (LONGLONG)Physical;
    Slab.CacheType = CacheType;
    return Slab;
}

static VOID
TestParkOnlyDuringStart(VOID)
{
    BOOLEAN Result;
    SOFTGPU_SLAB_HANDOFF Handoff = { 0 };
    SOFTGPU_SLAB Slab = MakeSlab(0x10000, 0x3B5A000, 0x8000000, MmWriteCombined);
    SOFTGPU_SLAB Other = MakeSlab(0x20000, 0x1000, 0x9000000, MmWriteCombined);
    SOFTGPU_SLAB Release;

    Result = SoftGpuSlabHandoffPark(&Handoff, &Slab);
    ok_eq_bool(Result, FALSE);

    SoftGpuSlabHandoffBeginStart(&Handoff);
    Result = SoftGpuSlabHandoffPark(&Handoff, &Slab);
    ok_eq_bool(Result, TRUE);
    Result = SoftGpuSlabHandoffPark(&Handoff, &Other);
    ok_eq_bool(Result, FALSE);
    ok_eq_pointer(Handoff.Parked.Base, Slab.Base);

    Result = SoftGpuSlabHandoffEndStart(&Handoff, &Release);

    ok_eq_bool(Result, TRUE);
    ok_eq_pointer(Release.Base, Slab.Base);
    ok_eq_size(Release.Size, Slab.Size);
    ok_eq_bool(SoftGpuSlabIsEmpty(&Handoff.Parked), TRUE);
    ok_eq_long(Handoff.StartsInProgress, 0L);
}

static VOID
TestAdopt(VOID)
{
    BOOLEAN Result;
    SOFTGPU_SLAB_HANDOFF Handoff = { 0 };
    SOFTGPU_SLAB Slab = MakeSlab(0x10000, 0x3B5A000, 0x8000000, MmWriteCombined);
    SOFTGPU_SLAB Adopted, Release;

    SoftGpuSlabHandoffBeginStart(&Handoff);
    Result = SoftGpuSlabHandoffTake(&Handoff, 0x1000, 0, MmWriteCombined, &Adopted);
    ok_eq_bool(Result, FALSE);
    ok_eq_bool(SoftGpuSlabIsEmpty(&Adopted), TRUE);

    Result = SoftGpuSlabHandoffPark(&Handoff, &Slab);

    ok_eq_bool(Result, TRUE);
    Result = SoftGpuSlabHandoffTake(&Handoff, 0x3B5A000, 0, MmWriteCombined, &Adopted);
    ok_eq_bool(Result, TRUE);
    ok_eq_pointer(Adopted.Base, Slab.Base);
    ok_eq_size(Adopted.Size, Slab.Size);
    ok_eq_longlong(Adopted.Physical.QuadPart, Slab.Physical.QuadPart);
    ok_eq_bool(SoftGpuSlabIsEmpty(&Handoff.Parked), TRUE);

    Result = SoftGpuSlabHandoffEndStart(&Handoff, &Release);

    ok_eq_bool(Result, FALSE);
    ok_eq_bool(SoftGpuSlabIsEmpty(&Release), TRUE);
}

static VOID
CheckMismatch(
    _In_ SIZE_T Size,
    _In_ ULONGLONG HighestAddress,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    BOOLEAN Result;
    SOFTGPU_SLAB_HANDOFF Handoff = { 0 };
    SOFTGPU_SLAB Slab = MakeSlab(0x10000, 0x200000, 0x8000000, MmWriteCombined);
    SOFTGPU_SLAB Adopted, Release;

    SoftGpuSlabHandoffBeginStart(&Handoff);
    Result = SoftGpuSlabHandoffPark(&Handoff, &Slab);
    ok_eq_bool(Result, TRUE);
    Result = SoftGpuSlabHandoffTake(&Handoff, Size, HighestAddress, CacheType, &Adopted);
    ok_eq_bool(Result, FALSE);
    ok_eq_bool(SoftGpuSlabIsEmpty(&Adopted), TRUE);
    ok_eq_pointer(Handoff.Parked.Base, Slab.Base);
    Result = SoftGpuSlabHandoffEndStart(&Handoff, &Release);
    ok_eq_bool(Result, TRUE);
    ok_eq_pointer(Release.Base, Slab.Base);
    ok_eq_bool(SoftGpuSlabIsEmpty(&Handoff.Parked), TRUE);
}

static VOID
TestFit(VOID)
{
    SOFTGPU_SLAB Slab = MakeSlab(0x10000, 0x200000, 0x8000000, MmWriteCombined);

    ok_eq_bool(SoftGpuSlabFits(&Slab, 0x200000, 0, MmWriteCombined), TRUE);
    ok_eq_bool(SoftGpuSlabFits(&Slab, 0x1000, 0, MmWriteCombined), TRUE);
    ok_eq_bool(SoftGpuSlabFits(&Slab, 0x200000, 0x81FFFFF, MmWriteCombined), TRUE);
    ok_eq_bool(SoftGpuSlabFits(&Slab, 0x200000, 0x81FFFFE, MmWriteCombined), FALSE);
    ok_eq_bool(SoftGpuSlabFits(&Slab, 0x200000, 0x7FFFFFF, MmWriteCombined), FALSE);

    CheckMismatch(0x201000, 0, MmWriteCombined);
    CheckMismatch(0x200000, 0, MmNonCached);
    CheckMismatch(0x200000, 0x81FFFFE, MmWriteCombined);
}

static VOID
TestNestedStarts(VOID)
{
    BOOLEAN Result;
    SOFTGPU_SLAB_HANDOFF Handoff = { 0 };
    SOFTGPU_SLAB Slab = MakeSlab(0x10000, 0x200000, 0x8000000, MmWriteCombined);
    SOFTGPU_SLAB Release;

    SoftGpuSlabHandoffBeginStart(&Handoff);
    SoftGpuSlabHandoffBeginStart(&Handoff);
    Result = SoftGpuSlabHandoffPark(&Handoff, &Slab);
    ok_eq_bool(Result, TRUE);
    Result = SoftGpuSlabHandoffEndStart(&Handoff, &Release);
    ok_eq_bool(Result, FALSE);
    ok_eq_bool(SoftGpuSlabIsEmpty(&Release), TRUE);
    ok_eq_pointer(Handoff.Parked.Base, Slab.Base);
    Result = SoftGpuSlabHandoffEndStart(&Handoff, &Release);
    ok_eq_bool(Result, TRUE);
    ok_eq_pointer(Release.Base, Slab.Base);
    ok_eq_long(Handoff.StartsInProgress, 0L);
}

static VOID
TestRetryAfterMismatch(VOID)
{
    BOOLEAN Result;
    SOFTGPU_SLAB_HANDOFF Handoff = { 0 };
    SOFTGPU_SLAB Slab = MakeSlab(0x10000, 0x200000, 0x8000000, MmWriteCombined);
    SOFTGPU_SLAB Adopted, Release;

    SoftGpuSlabHandoffBeginStart(&Handoff);
    Result = SoftGpuSlabHandoffPark(&Handoff, &Slab);
    ok_eq_bool(Result, TRUE);
    Result = SoftGpuSlabHandoffTake(&Handoff, 0x300000, 0, MmWriteCombined, &Adopted);
    ok_eq_bool(Result, FALSE);
    ok_eq_pointer(Handoff.Parked.Base, Slab.Base);
    Result = SoftGpuSlabHandoffTake(&Handoff, 0x200000, 0, MmWriteCombined, &Adopted);
    ok_eq_bool(Result, TRUE);
    ok_eq_pointer(Adopted.Base, Slab.Base);
    Result = SoftGpuSlabHandoffEndStart(&Handoff, &Release);
    ok_eq_bool(Result, FALSE);
}

START_TEST(SoftGpuSlabHandoff)
{
    TestParkOnlyDuringStart();
    TestAdopt();
    TestFit();
    TestRetryAfterMismatch();
    TestNestedStarts();
}
