/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Memory API set functions exported only by KernelBase
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winternl.h>

static
BOOL
IsExecuteProtection(
    _In_ ULONG Protection)
{
    return (Protection & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                          PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

PVOID
WINAPI
VirtualAllocFromApp(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T Size,
    _In_ ULONG AllocationType,
    _In_ ULONG Protection)
{
    if (IsExecuteProtection(Protection))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    return VirtualAlloc(BaseAddress, Size, AllocationType, Protection);
}

PVOID
WINAPI
VirtualAlloc2FromApp(
    _In_opt_ HANDLE Process,
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T Size,
    _In_ ULONG AllocationType,
    _In_ ULONG PageProtection,
    _Inout_updates_opt_(ParameterCount) MEM_EXTENDED_PARAMETER *ExtendedParameters,
    _In_ ULONG ParameterCount)
{
    if (IsExecuteProtection(PageProtection))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    return VirtualAlloc2(Process,
                         BaseAddress,
                         Size,
                         AllocationType,
                         PageProtection,
                         ExtendedParameters,
                         ParameterCount);
}

HANDLE
WINAPI
CreateFileMappingFromApp(
    _In_ HANDLE hFile,
    _In_opt_ PSECURITY_ATTRIBUTES SecurityAttributes,
    _In_ ULONG PageProtection,
    _In_ ULONG64 MaximumSize,
    _In_opt_ PCWSTR Name)
{
    return CreateFileMappingW(hFile,
                              SecurityAttributes,
                              PageProtection,
                              (DWORD)(MaximumSize >> 32),
                              (DWORD)MaximumSize,
                              Name);
}

HANDLE
WINAPI
OpenFileMappingFromApp(
    _In_ ULONG DesiredAccess,
    _In_ BOOL InheritHandle,
    _In_ PCWSTR Name)
{
    return OpenFileMappingW(DesiredAccess, InheritHandle, Name);
}

PVOID
WINAPI
MapViewOfFileFromApp(
    _In_ HANDLE hFileMappingObject,
    _In_ ULONG DesiredAccess,
    _In_ ULONG64 FileOffset,
    _In_ SIZE_T NumberOfBytesToMap)
{
    return MapViewOfFile(hFileMappingObject,
                         DesiredAccess,
                         (DWORD)(FileOffset >> 32),
                         (DWORD)FileOffset,
                         NumberOfBytesToMap);
}

BOOL
WINAPI
QueryVirtualMemoryInformation(
    _In_ HANDLE Process,
    _In_ const VOID *VirtualAddress,
    _In_ WIN32_MEMORY_INFORMATION_CLASS MemoryInformationClass,
    _Out_writes_bytes_(MemoryInformationSize) PVOID MemoryInformation,
    _In_ SIZE_T MemoryInformationSize,
    _Out_opt_ PSIZE_T ReturnSize)
{
    NTSTATUS Status;

    if (MemoryInformationClass != MemoryRegionInfo)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Status = NtQueryVirtualMemory(Process,
                                  (PVOID)VirtualAddress,
                                  MemoryRegionInformation,
                                  MemoryInformation,
                                  MemoryInformationSize,
                                  ReturnSize);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

#ifdef _WIN64

PVOID
WINAPI
MapViewOfFile3(
    _In_ HANDLE FileMapping,
    _In_opt_ HANDLE Process,
    _In_opt_ PVOID BaseAddress,
    _In_ ULONG64 Offset,
    _In_ SIZE_T ViewSize,
    _In_ ULONG AllocationType,
    _In_ ULONG PageProtection,
    _Inout_updates_opt_(ParameterCount) MEM_EXTENDED_PARAMETER *ExtendedParameters,
    _In_ ULONG ParameterCount)
{
    LARGE_INTEGER SectionOffset;
    NTSTATUS Status;

    if (!Process)
        Process = GetCurrentProcess();

    SectionOffset.QuadPart = Offset;
    Status = NtMapViewOfSectionEx(FileMapping,
                                  Process,
                                  &BaseAddress,
                                  &SectionOffset,
                                  &ViewSize,
                                  AllocationType,
                                  PageProtection,
                                  ExtendedParameters,
                                  ParameterCount);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return NULL;
    }

    return BaseAddress;
}

BOOL
WINAPI
UnmapViewOfFile2(
    _In_ HANDLE Process,
    _In_ PVOID BaseAddress,
    _In_ ULONG UnmapFlags)
{
    NTSTATUS Status;

    Status = NtUnmapViewOfSectionEx(Process, BaseAddress, UnmapFlags);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

#endif
