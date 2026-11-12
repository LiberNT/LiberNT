/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pool a file system takes without testing the result
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#define FSRTL_POOL_TAG 'tRsF'

/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
FsRtlAllocatePoolWithTag(
    _In_ POOL_TYPE PoolType,
    _In_ ULONG NumberOfBytes,
    _In_ ULONG Tag)
{
    PVOID Block = ExAllocatePoolWithTag(PoolType, NumberOfBytes, Tag);

    if (Block == NULL) ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);

    return Block;
}

PVOID
NTAPI
FsRtlAllocatePool(
    _In_ POOL_TYPE PoolType,
    _In_ ULONG NumberOfBytes)
{
    return FsRtlAllocatePoolWithTag(PoolType, NumberOfBytes, FSRTL_POOL_TAG);
}

PVOID
NTAPI
FsRtlAllocatePoolWithQuotaTag(
    _In_ POOL_TYPE PoolType,
    _In_ ULONG NumberOfBytes,
    _In_ ULONG Tag)
{
    PVOID Block = ExAllocatePoolWithQuotaTag(PoolType, NumberOfBytes, Tag);

    if (Block == NULL) ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);

    return Block;
}

PVOID
NTAPI
FsRtlAllocatePoolWithQuota(
    _In_ POOL_TYPE PoolType,
    _In_ ULONG NumberOfBytes)
{
    return FsRtlAllocatePoolWithQuotaTag(PoolType,
                                         NumberOfBytes,
                                         FSRTL_POOL_TAG);
}

/* EOF */
