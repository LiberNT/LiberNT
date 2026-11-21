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
DECLARE_HANDLE(HPSSWALK);

#define PSS_PERF_RESOLUTION 1000000

typedef enum
{
    PSS_HANDLE_NONE                           = 0x00,
    PSS_HANDLE_HAVE_TYPE                      = 0x01,
    PSS_HANDLE_HAVE_NAME                      = 0x02,
    PSS_HANDLE_HAVE_BASIC_INFORMATION         = 0x04,
    PSS_HANDLE_HAVE_TYPE_SPECIFIC_INFORMATION = 0x08
} PSS_HANDLE_FLAGS;
DEFINE_ENUM_FLAG_OPERATORS(PSS_HANDLE_FLAGS);

typedef enum
{
    PSS_OBJECT_TYPE_UNKNOWN   = 0,
    PSS_OBJECT_TYPE_PROCESS   = 1,
    PSS_OBJECT_TYPE_THREAD    = 2,
    PSS_OBJECT_TYPE_MUTANT    = 3,
    PSS_OBJECT_TYPE_EVENT     = 4,
    PSS_OBJECT_TYPE_SECTION   = 5,
    PSS_OBJECT_TYPE_SEMAPHORE = 6
} PSS_OBJECT_TYPE;

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
    PSS_WALK_AUXILIARY_PAGES = 0,
    PSS_WALK_VA_SPACE = 1,
    PSS_WALK_HANDLES = 2,
    PSS_WALK_THREADS = 3
} PSS_WALK_INFORMATION_CLASS;

typedef enum
{
    PSS_DUPLICATE_NONE         = 0x00,
    PSS_DUPLICATE_CLOSE_SOURCE = 0x01
} PSS_DUPLICATE_FLAGS;
DEFINE_ENUM_FLAG_OPERATORS(PSS_DUPLICATE_FLAGS);

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
    DWORD AuxPagesCaptured;
} PSS_AUXILIARY_PAGES_INFORMATION;

typedef struct
{
    DWORD RegionCount;
} PSS_VA_SPACE_INFORMATION;

typedef struct
{
    DWORD HandlesCaptured;
} PSS_HANDLE_INFORMATION;

typedef struct
{
    DWORD ThreadsCaptured;
    DWORD ContextLength;
} PSS_THREAD_INFORMATION;

typedef struct
{
    HANDLE SectionHandle;
    DWORD Size;
} PSS_HANDLE_TRACE_INFORMATION;

typedef struct
{
    UINT64 TotalCycleCount;
    UINT64 TotalWallClockPeriod;
    UINT64 VaCloneCycleCount;
    UINT64 VaCloneWallClockPeriod;
    UINT64 VaSpaceCycleCount;
    UINT64 VaSpaceWallClockPeriod;
    UINT64 AuxPagesCycleCount;
    UINT64 AuxPagesWallClockPeriod;
    UINT64 HandlesCycleCount;
    UINT64 HandlesWallClockPeriod;
    UINT64 ThreadsCycleCount;
    UINT64 ThreadsWallClockPeriod;
} PSS_PERFORMANCE_COUNTERS;

typedef struct
{
    void* Address;
    MEMORY_BASIC_INFORMATION BasicInformation;
    FILETIME CaptureTime;
    void* PageContents;
    DWORD PageSize;
} PSS_AUXILIARY_PAGE_ENTRY;

typedef struct
{
    void* BaseAddress;
    void* AllocationBase;
    DWORD AllocationProtect;
    ULONG_PTR RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
    DWORD TimeDateStamp;
    DWORD SizeOfImage;
    void* ImageBase;
    DWORD CheckSum;
    WORD MappedFileNameLength;
    wchar_t const* MappedFileName;
} PSS_VA_SPACE_ENTRY;

typedef struct
{
    HANDLE Handle;
    PSS_HANDLE_FLAGS Flags;
    PSS_OBJECT_TYPE ObjectType;
    FILETIME CaptureTime;
    DWORD Attributes;
    DWORD GrantedAccess;
    DWORD HandleCount;
    DWORD PointerCount;
    DWORD PagedPoolCharge;
    DWORD NonPagedPoolCharge;
    FILETIME CreationTime;
    WORD TypeNameLength;
    wchar_t const* TypeName;
    WORD ObjectNameLength;
    wchar_t const* ObjectName;
    union
    {
        struct
        {
            DWORD ExitStatus;
            void* PebBaseAddress;
            ULONG_PTR AffinityMask;
            LONG BasePriority;
            DWORD ProcessId;
            DWORD ParentProcessId;
            DWORD Flags;
        } Process;
        struct
        {
            DWORD ExitStatus;
            void* TebBaseAddress;
            DWORD ProcessId;
            DWORD ThreadId;
            ULONG_PTR AffinityMask;
            int Priority;
            int BasePriority;
            void* Win32StartAddress;
        } Thread;
        struct
        {
            LONG CurrentCount;
            BOOL Abandoned;
            DWORD OwnerProcessId;
            DWORD OwnerThreadId;
        } Mutant;
        struct
        {
            BOOL ManualReset;
            BOOL Signaled;
        } Event;
        struct
        {
            void* BaseAddress;
            DWORD AllocationAttributes;
            LARGE_INTEGER MaximumSize;
        } Section;
        struct
        {
            LONG CurrentCount;
            LONG MaximumCount;
        } Semaphore;
    } TypeSpecificInformation;
} PSS_HANDLE_ENTRY;

typedef enum
{
    PSS_THREAD_FLAGS_NONE       = 0x0000,
    PSS_THREAD_FLAGS_TERMINATED = 0x0001
} PSS_THREAD_FLAGS;
DEFINE_ENUM_FLAG_OPERATORS(PSS_THREAD_FLAGS);

typedef struct
{
    DWORD ExitStatus;
    void* TebBaseAddress;
    DWORD ProcessId;
    DWORD ThreadId;
    ULONG_PTR AffinityMask;
    int Priority;
    int BasePriority;
    void* LastSyscallFirstArgument;
    WORD LastSyscallNumber;
    FILETIME CreateTime;
    FILETIME ExitTime;
    FILETIME KernelTime;
    FILETIME UserTime;
    void* Win32StartAddress;
    FILETIME CaptureTime;
    PSS_THREAD_FLAGS Flags;
    WORD SuspendCount;
    WORD SizeOfContextRecord;
    PCONTEXT ContextRecord;
} PSS_THREAD_ENTRY;

typedef struct
{
    void* Context;
    void* (WINAPI *AllocRoutine)(_In_ void* Context, _In_ DWORD Size);
    void (WINAPI *FreeRoutine)(_In_ void* Context, _In_opt_ void* Address);
} PSS_ALLOCATOR;

#ifdef __cplusplus
extern "C" {
#endif

DWORD WINAPI PssCaptureSnapshot(_In_ HANDLE ProcessHandle, _In_ PSS_CAPTURE_FLAGS CaptureFlags,
                               _In_opt_ DWORD ThreadContextFlags, _Out_ HPSS *SnapshotHandle);
DWORD WINAPI PssFreeSnapshot(_In_ HANDLE ProcessHandle, _In_ HPSS SnapshotHandle);
DWORD WINAPI PssQuerySnapshot(_In_ HPSS SnapshotHandle, _In_ PSS_QUERY_INFORMATION_CLASS InformationClass,
                             _Out_writes_bytes_(BufferLength) void *Buffer, _In_ DWORD BufferLength);
DWORD WINAPI PssWalkSnapshot(_In_ HPSS SnapshotHandle, _In_ PSS_WALK_INFORMATION_CLASS InformationClass,
                            _In_ HPSSWALK WalkMarkerHandle, _Out_writes_opt_(BufferLength) void *Buffer,
                            _In_ DWORD BufferLength);
DWORD WINAPI PssDuplicateSnapshot(_In_ HANDLE SourceProcessHandle, _In_ HPSS SnapshotHandle,
                                 _In_ HANDLE TargetProcessHandle, _Out_ HPSS *TargetSnapshotHandle,
                                 _In_opt_ PSS_DUPLICATE_FLAGS Flags);
DWORD WINAPI PssWalkMarkerCreate(_In_opt_ PSS_ALLOCATOR const *Allocator, _Out_ HPSSWALK *WalkMarkerHandle);
DWORD WINAPI PssWalkMarkerFree(_In_ HPSSWALK WalkMarkerHandle);
DWORD WINAPI PssWalkMarkerGetPosition(_In_ HPSSWALK WalkMarkerHandle, _Out_ ULONG_PTR *Position);
DWORD WINAPI PssWalkMarkerSetPosition(_In_ HPSSWALK WalkMarkerHandle, _In_ ULONG_PTR Position);
DWORD WINAPI PssWalkMarkerSeekToBeginning(_In_ HPSSWALK WalkMarkerHandle);

#ifdef __cplusplus
}
#endif

#ifndef PSS_PERF_RESOLUTION
#define PSS_PERF_RESOLUTION 1000000
#endif
