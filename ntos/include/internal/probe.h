/*
 * COPYRIGHT:   Copyright 2022 George Bișoc <george.bisoc@reactos.org>
 */

#pragma once

#include <reactos/probe.h>

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



    }

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




    }

    return Status;
}
