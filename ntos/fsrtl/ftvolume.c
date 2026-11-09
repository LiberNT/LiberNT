/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a file system tells a fault tolerant volume about itself
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PROTOTYPES *****************************************************************/

NTSTATUS
NTAPI
FsRtlSyncVolumes(
    _In_ ULONG Unused1,
    _In_ ULONG Unused2,
    _In_ ULONG Unused3);

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlBalanceReads(
    _In_ PDEVICE_OBJECT TargetDevice)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(TargetDevice);

    /* TODO: no published request tells a mirror to balance its reads, and no mirror driver exists here; every device answers as a device that is not a mirror */
    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS
NTAPI
FsRtlSyncVolumes(
    _In_ ULONG Unused1,
    _In_ ULONG Unused2,
    _In_ ULONG Unused3)
{
    UNREFERENCED_PARAMETER(Unused1);
    UNREFERENCED_PARAMETER(Unused2);
    UNREFERENCED_PARAMETER(Unused3);

    return STATUS_SUCCESS;
}

/* EOF */
