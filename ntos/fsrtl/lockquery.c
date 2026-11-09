/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Asking a lock package whether anything is locked or being locked
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
FsRtlAreThereCurrentOrInProgressFileLocks(
    _In_ PFILE_LOCK FileLock)
{
    PAGED_CODE();

    if (FileLock == NULL) return FALSE;

    if (FsRtlAreThereCurrentFileLocks(FileLock)) return TRUE;

    return (BOOLEAN)(ReadNoFence((PLONG)&FileLock->LockRequestsInProgress) !=
                     0);
}

/* EOF */
