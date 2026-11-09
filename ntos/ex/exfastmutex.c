/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The fast mutex, as callers outside the kernel reach it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

#undef ExAcquireFastMutex
VOID
FASTCALL
ExAcquireFastMutex(
    _Inout_ PFAST_MUTEX FastMutex)
{
    _ExAcquireFastMutex(FastMutex);
}

#undef ExReleaseFastMutex
VOID
FASTCALL
ExReleaseFastMutex(
    _Inout_ PFAST_MUTEX FastMutex)
{
    _ExReleaseFastMutex(FastMutex);
}

#undef ExTryToAcquireFastMutex
BOOLEAN
FASTCALL
ExTryToAcquireFastMutex(
    _Inout_ PFAST_MUTEX FastMutex)
{
    return _ExTryToAcquireFastMutex(FastMutex);
}

#undef ExAcquireFastMutexUnsafe
VOID
FASTCALL
ExAcquireFastMutexUnsafe(
    _Inout_ PFAST_MUTEX FastMutex)
{
    _ExAcquireFastMutexUnsafe(FastMutex);
}

#undef ExReleaseFastMutexUnsafe
VOID
FASTCALL
ExReleaseFastMutexUnsafe(
    _Inout_ PFAST_MUTEX FastMutex)
{
    _ExReleaseFastMutexUnsafe(FastMutex);
}

#undef ExEnterCriticalRegionAndAcquireFastMutexUnsafe
VOID
FASTCALL
ExEnterCriticalRegionAndAcquireFastMutexUnsafe(
    _Inout_ PFAST_MUTEX FastMutex)
{
    _ExEnterCriticalRegionAndAcquireFastMutexUnsafe(FastMutex);
}

#undef ExReleaseFastMutexUnsafeAndLeaveCriticalRegion
VOID
FASTCALL
ExReleaseFastMutexUnsafeAndLeaveCriticalRegion(
    _Inout_ PFAST_MUTEX FastMutex)
{
    _ExReleaseFastMutexUnsafeAndLeaveCriticalRegion(FastMutex);
}

/* EOF */
