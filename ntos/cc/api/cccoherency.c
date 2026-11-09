/*
 * PROJECT:     LiberNT Cache Manager
 * FILE:        ntoskrnl/cc/api/cccoherency.c
 * PURPOSE:     Keeping cached and uncached readers of a file in agreement
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccnt.h"

/*
 * Writes out what the cache holds for a range and then lets go of it, so that
 * a reader going straight to the disk cannot find older bytes there than the
 * ones the cache was holding. Asked not to let go, it only writes out.
 */
VOID
NTAPI
CcCoherencyFlushAndPurgeCache(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer,
    _In_opt_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_opt_ ULONG Flags)
{
    CcFlushCache(SectionObjectPointer, FileOffset, Length, IoStatus);

    if (!NT_SUCCESS(IoStatus->Status)) return;
    if (Flags & CC_FLUSH_AND_PURGE_NO_PURGE) return;

    /* TODO: a range that could not be let go of is not reported back */
    CcPurgeCacheSection(SectionObjectPointer, FileOffset, Length, 0);
}

/*
 * Says whether copying that much into the cache can be done with what the
 * cache already has room for, which is what lets a caller take the fast path
 * instead of going down to the disk itself.
 */
BOOLEAN
NTAPI
CcCopyWriteWontFlush(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(FileOffset);

    /* TODO: how much of the range the cache already holds is not weighed */
    return (BOOLEAN)(Length <= CC_COPY_WRITE_WONT_FLUSH_LIMIT);
}
