/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User-mode CPU-set information query
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winternl.h>

BOOL
WINAPI
GetSystemCpuSetInformation(
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnedLength) PSYSTEM_CPU_SET_INFORMATION Information,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnedLength,
    _In_opt_ HANDLE Process,
    _Reserved_ ULONG Flags)
{
    NTSTATUS Status;

    *ReturnedLength = 0;
    if (Flags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Status = NtQuerySystemInformationEx(SystemCpuSetInformation, &Process, sizeof(Process), Information, BufferLength, ReturnedLength);
    if (!NT_SUCCESS(Status))
    {
        RtlSetLastWin32Error(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

static
BOOL
BasepCpuSetIdsLength(
    _In_opt_ const ULONG *CpuSetIds,
    _In_ ULONG CpuSetIdCount,
    _Out_ PULONG Length)
{
    *Length = 0;

    if ((CpuSetIds == NULL && CpuSetIdCount != 0) ||
        CpuSetIdCount > MAXDWORD / sizeof(ULONG))
    {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *Length = CpuSetIdCount * sizeof(ULONG);
    return TRUE;
}

static
BOOL
BasepFinishCpuSetQuery(
    _In_ NTSTATUS Status,
    _In_ ULONG ReturnLength,
    _Out_ PULONG RequiredIdCount)
{
    *RequiredIdCount = ReturnLength / sizeof(ULONG);

    if (!NT_SUCCESS(Status))
    {
        RtlSetLastWin32Error(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

static
BOOL
BasepFinishCpuSetChange(
    _In_ NTSTATUS Status)
{
    if (!NT_SUCCESS(Status))
    {
        RtlSetLastWin32Error(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

BOOL
WINAPI
GetProcessDefaultCpuSets(
    _In_ HANDLE Process,
    _Out_writes_to_opt_(CpuSetIdCount, *RequiredIdCount) PULONG CpuSetIds,
    _In_ ULONG CpuSetIdCount,
    _Always_(_Out_) PULONG RequiredIdCount)
{
    ULONG Length, ReturnLength = 0;
    NTSTATUS Status;

    *RequiredIdCount = 0;
    if (!BasepCpuSetIdsLength(CpuSetIds, CpuSetIdCount, &Length))
        return FALSE;

    Status = NtQueryInformationProcess(Process, ProcessDefaultCpuSetsInformation, CpuSetIds, Length, &ReturnLength);
    return BasepFinishCpuSetQuery(Status, ReturnLength, RequiredIdCount);
}

BOOL
WINAPI
SetProcessDefaultCpuSets(
    _In_ HANDLE Process,
    _In_reads_opt_(CpuSetIdCount) const ULONG *CpuSetIds,
    _In_ ULONG CpuSetIdCount)
{
    ULONG Length;

    if (!BasepCpuSetIdsLength(CpuSetIds, CpuSetIdCount, &Length))
        return FALSE;

    return BasepFinishCpuSetChange(NtSetInformationProcess(Process, ProcessDefaultCpuSetsInformation, (PVOID)CpuSetIds, Length));
}

BOOL
WINAPI
GetThreadSelectedCpuSets(
    _In_ HANDLE Thread,
    _Out_writes_to_opt_(CpuSetIdCount, *RequiredIdCount) PULONG CpuSetIds,
    _In_ ULONG CpuSetIdCount,
    _Always_(_Out_) PULONG RequiredIdCount)
{
    ULONG Length, ReturnLength = 0;
    NTSTATUS Status;

    *RequiredIdCount = 0;
    if (!BasepCpuSetIdsLength(CpuSetIds, CpuSetIdCount, &Length))
        return FALSE;

    Status = NtQueryInformationThread(Thread, ThreadSelectedCpuSets, CpuSetIds, Length, &ReturnLength);
    return BasepFinishCpuSetQuery(Status, ReturnLength, RequiredIdCount);
}

BOOL
WINAPI
SetThreadSelectedCpuSets(
    _In_ HANDLE Thread,
    _In_reads_(CpuSetIdCount) const ULONG *CpuSetIds,
    _In_ ULONG CpuSetIdCount)
{
    ULONG Length;

    if (!BasepCpuSetIdsLength(CpuSetIds, CpuSetIdCount, &Length))
        return FALSE;

    return BasepFinishCpuSetChange(NtSetInformationThread(Thread, ThreadSelectedCpuSets, (PVOID)CpuSetIds, Length));
}
