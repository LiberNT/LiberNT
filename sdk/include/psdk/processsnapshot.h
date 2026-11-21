/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Public process snapshot capture and query declarations
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#pragma once

#include <windef.h>
#include <winbase.h>

DECLARE_HANDLE(HPSS);

typedef enum
{
    PSS_CAPTURE_NONE                                = 0x00000000,
    PSS_CAPTURE_VA_CLONE                            = 0x00000001,
    PSS_CAPTURE_RESERVED_00000002                   = 0x00000002,
    PSS_CAPTURE_HANDLES                             = 0x00000004,
    PSS_CAPTURE_HANDLE_NAME_INFORMATION             = 0x00000008,
    PSS_CAPTURE_HANDLE_BASIC_INFORMATION            = 0x00000010,
    PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION    = 0x00000020,
    PSS_CAPTURE_HANDLE_TRACE                        = 0x00000040,
    PSS_CAPTURE_THREADS                             = 0x00000080,
    PSS_CAPTURE_THREAD_CONTEXT                      = 0x00000100,
    PSS_CAPTURE_THREAD_CONTEXT_EXTENDED             = 0x00000200,
    PSS_CAPTURE_RESERVED_00000400                   = 0x00000400,
    PSS_CAPTURE_VA_SPACE                            = 0x00000800,
    PSS_CAPTURE_VA_SPACE_SECTION_INFORMATION        = 0x00001000,
    PSS_CAPTURE_IPT_TRACE                           = 0x00002000,
    PSS_CAPTURE_RESERVED_00004000                   = 0x00004000,

    PSS_CREATE_BREAKAWAY_OPTIONAL                   = 0x04000000,
    PSS_CREATE_BREAKAWAY                            = 0x08000000,
    PSS_CREATE_FORCE_BREAKAWAY                      = 0x10000000,
    PSS_CREATE_USE_VM_ALLOCATIONS                   = 0x20000000,
    PSS_CREATE_MEASURE_PERFORMANCE                  = 0x40000000,
    PSS_CREATE_RELEASE_SECTION                      = 0x80000000
} PSS_CAPTURE_FLAGS;
DEFINE_ENUM_FLAG_OPERATORS(PSS_CAPTURE_FLAGS);

typedef enum
{
    PSS_QUERY_PROCESS_INFORMATION = 0,
    PSS_QUERY_VA_CLONE_INFORMATION = 1,
    PSS_QUERY_AUXILIARY_PAGES_INFORMATION = 2,
    PSS_QUERY_VA_SPACE_INFORMATION = 3,
    PSS_QUERY_HANDLE_INFORMATION = 4,
    PSS_QUERY_THREAD_INFORMATION = 5,
    PSS_QUERY_HANDLE_TRACE_INFORMATION = 6,
    PSS_QUERY_PERFORMANCE_COUNTERS = 7
} PSS_QUERY_INFORMATION_CLASS;

typedef enum
{
    PSS_PROCESS_FLAGS_NONE        = 0x00000000,
    PSS_PROCESS_FLAGS_PROTECTED   = 0x00000001,
    PSS_PROCESS_FLAGS_WOW64       = 0x00000002,
    PSS_PROCESS_FLAGS_RESERVED_03 = 0x00000004,
    PSS_PROCESS_FLAGS_RESERVED_04 = 0x00000008,
    PSS_PROCESS_FLAGS_FROZEN      = 0x00000010
} PSS_PROCESS_FLAGS;
DEFINE_ENUM_FLAG_OPERATORS(PSS_PROCESS_FLAGS);

typedef struct
{
    DWORD ExitStatus;
    void* PebBaseAddress;
    ULONG_PTR AffinityMask;
    LONG BasePriority;
    DWORD ProcessId;
    DWORD ParentProcessId;
    PSS_PROCESS_FLAGS Flags;
    FILETIME CreateTime;
    FILETIME ExitTime;
    FILETIME KernelTime;
    FILETIME UserTime;
    DWORD PriorityClass;
    ULONG_PTR PeakVirtualSize;
    ULONG_PTR VirtualSize;
    DWORD PageFaultCount;
    ULONG_PTR PeakWorkingSetSize;
    ULONG_PTR WorkingSetSize;
    ULONG_PTR QuotaPeakPagedPoolUsage;
    ULONG_PTR QuotaPagedPoolUsage;
    ULONG_PTR QuotaPeakNonPagedPoolUsage;
    ULONG_PTR QuotaNonPagedPoolUsage;
    ULONG_PTR PagefileUsage;
    ULONG_PTR PeakPagefileUsage;
    ULONG_PTR PrivateUsage;
    DWORD ExecuteFlags;
    WCHAR ImageFileName[MAX_PATH];
} PSS_PROCESS_INFORMATION;

typedef struct
{
    HANDLE VaCloneHandle;
} PSS_VA_CLONE_INFORMATION;

typedef struct
{
    DWORD ThreadsCaptured;
    DWORD ContextLength;
} PSS_THREAD_INFORMATION;

#ifdef __cplusplus
extern "C" {
#endif

DWORD WINAPI PssCaptureSnapshot(_In_ HANDLE ProcessHandle, _In_ PSS_CAPTURE_FLAGS CaptureFlags,
                               _In_opt_ DWORD ThreadContextFlags, _Out_ HPSS *SnapshotHandle);
DWORD WINAPI PssFreeSnapshot(_In_ HANDLE ProcessHandle, _In_ HPSS SnapshotHandle);
DWORD WINAPI PssQuerySnapshot(_In_ HPSS SnapshotHandle, _In_ PSS_QUERY_INFORMATION_CLASS InformationClass,
                             _Out_writes_bytes_(BufferLength) void *Buffer, _In_ DWORD BufferLength);

#ifdef __cplusplus
}
#endif

#ifndef PSS_PERF_RESOLUTION
#define PSS_PERF_RESOLUTION 1000000
#endif
