/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Telling an exception a file system may pass on from one it may not
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
FsRtlIsNtstatusExpected(
    _In_ NTSTATUS Ntstatus)
{
    switch (Ntstatus)
    {
        case STATUS_DATATYPE_MISALIGNMENT:
        case STATUS_ACCESS_VIOLATION:
        case STATUS_ILLEGAL_INSTRUCTION:
        case STATUS_INSTRUCTION_MISALIGNMENT:
            return FALSE;

        default:
            return TRUE;
    }
}

NTSTATUS
NTAPI
FsRtlNormalizeNtstatus(
    _In_ NTSTATUS Exception,
    _In_ NTSTATUS GenericException)
{
    return FsRtlIsNtstatusExpected(Exception) ? Exception : GenericException;
}

BOOLEAN
NTAPI
FsRtlIsTotalDeviceFailure(
    _In_ NTSTATUS Status)
{
    if (!NT_ERROR(Status)) return FALSE;

    if ((Status == STATUS_CRC_ERROR) || (Status == STATUS_DEVICE_DATA_ERROR))
    {
        return FALSE;
    }

    return TRUE;
}

/* EOF */
