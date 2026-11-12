/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Whether the programs a volume runs at startup have finished
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* TODO: nothing published says how the session manager reports that it has finished, so a file system is told the checks are done and may use the volume */
BOOLEAN FsRtlpVolumeStartupApplicationsComplete = TRUE;

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
FsRtlAreVolumeStartupApplicationsComplete(VOID)
{
    PAGED_CODE();

    return FsRtlpVolumeStartupApplicationsComplete;
}

/* EOF */
