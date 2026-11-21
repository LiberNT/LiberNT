/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Validation of information class requests against their class table
 * COPYRIGHT:   Copyright 2022 George Bișoc <george.bisoc@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <libernt/probe.h>

ROS_PROBE_UNUSED
static
__inline
NTSTATUS
IcifCheckClass(
    _In_ ULONG Class,
    _In_ const INFORMATION_CLASS_INFO *ClassList,
    _In_ ULONG ClassListEntries,
    _In_ BOOLEAN Set,
    _In_ ULONG BufferLength)
{
    const INFORMATION_CLASS_INFO *Info;
    USHORT Required;

    if (Class >= ClassListEntries)
        return STATUS_INVALID_INFO_CLASS;

    Info = &ClassList[Class];
    if (!(Info->Flags & (Set ? ICIF_SET : ICIF_QUERY)))
        return STATUS_INVALID_INFO_CLASS;

    Required = Set ? Info->RequiredSizeSET : Info->RequiredSizeQUERY;
    if (Required != 0 && BufferLength != Required &&
        !(Info->Flags & (Set ? ICIF_SET_SIZE_VARIABLE : ICIF_QUERY_SIZE_VARIABLE)))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    return STATUS_SUCCESS;
}

ROS_PROBE_UNUSED
static
__inline
NTSTATUS
DefaultSetInfoBufferCheck(
    _In_ ULONG Class,
    _In_ const INFORMATION_CLASS_INFO *ClassList,
    _In_ ULONG ClassListEntries,
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;

    Status = IcifCheckClass(Class, ClassList, ClassListEntries, TRUE, BufferLength);
    if (!NT_SUCCESS(Status) || PreviousMode == KernelMode)
        return Status;

    if (Buffer == NULL && BufferLength != 0)
        return STATUS_ACCESS_VIOLATION;

    _SEH2_TRY
    {
        ProbeForRead(Buffer, BufferLength, ClassList[Class].AlignmentSET);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

ROS_PROBE_UNUSED
static
__inline
NTSTATUS
DefaultQueryInfoBufferCheck(
    _In_ ULONG Class,
    _In_ const INFORMATION_CLASS_INFO *ClassList,
    _In_ ULONG ClassListEntries,
    _In_ ULONG Flags,
    _In_opt_ PVOID Buffer,
    _In_ ULONG BufferLength,
    _In_opt_ PULONG ReturnLength,
    _In_opt_ PULONG_PTR ReturnLengthPtr,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;

    Status = IcifCheckClass(Class, ClassList, ClassListEntries, FALSE, BufferLength);
    if (!NT_SUCCESS(Status) || PreviousMode == KernelMode)
        return Status;

    /* A zero length probes nothing, so a class with an optional buffer still passes */
    _SEH2_TRY
    {
        if (Flags & ICIF_PROBE_READ)
            ProbeForRead(Buffer, BufferLength, ClassList[Class].AlignmentQUERY);
        else
            ProbeForWrite(Buffer, BufferLength, ClassList[Class].AlignmentQUERY);

        if ((Flags & ICIF_FORCE_RETURN_LENGTH_PROBE) || ReturnLength != NULL)
            ProbeForWriteUlong(ReturnLength);

        if (ReturnLengthPtr != NULL)
            ProbeForWrite(ReturnLengthPtr, sizeof(ULONG_PTR), sizeof(ULONG_PTR));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}
