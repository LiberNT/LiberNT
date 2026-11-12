/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing the file system runtime library up with the system
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fsrtlp.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#define FSRTL_RESOURCE_TAG 'rRsF'
#define FSRTL_LOCK_TAG 'lRsF'

PERESOURCE FsRtlPagingIoResources = NULL;
ULONG FsRtlPagingIoResourceSelector = 0;

/* PUBLIC FUNCTIONS ***********************************************************/

PERESOURCE
NTAPI
FsRtlAllocateResource(VOID)
{
    ULONG Index;

    PAGED_CODE();

    if (FsRtlPagingIoResources == NULL) return NULL;

    Index = (ULONG)InterlockedIncrement((PLONG)&FsRtlPagingIoResourceSelector);

    return &FsRtlPagingIoResources[Index % FSRTL_MAX_RESOURCES];
}

/* INIT FUNCTIONS *************************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
FsRtlInitSystem(VOID)
{
    ULONG Index;

    FsRtlPagingIoResources =
        ExAllocatePoolWithTag(NonPagedPool,
                              FSRTL_MAX_RESOURCES * sizeof(ERESOURCE),
                              FSRTL_RESOURCE_TAG);

    if (FsRtlPagingIoResources == NULL) return FALSE;

    for (Index = 0; Index < FSRTL_MAX_RESOURCES; Index++)
    {
        if (!NT_SUCCESS(ExInitializeResourceLite(
                            &FsRtlPagingIoResources[Index])))
        {
            return FALSE;
        }
    }

    ExInitializePagedLookasideList(&FsRtlFileLockLookasideList,
                                   NULL,
                                   NULL,
                                   0,
                                   sizeof(FILE_LOCK),
                                   FSRTL_LOCK_TAG,
                                   0);

    FsRtlInitializeContexts();
    FsRtlInitializeLargeMcbs();
    FsRtlInitializeTunnels();
    FsRtlInitializeWorkerThread();

    return TRUE;
}

/* EOF */
