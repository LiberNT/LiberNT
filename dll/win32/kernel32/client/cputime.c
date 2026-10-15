/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Process and thread cycle time queries
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/process.c
 */

#include <k32.h>

BOOL
WINAPI
QueryThreadCycleTime(IN HANDLE ThreadHandle,
                     OUT PULONG64 CycleTime)
{
    THREAD_CYCLE_TIME_INFORMATION CycleInfo;
    NTSTATUS Status;

    Status = NtQueryInformationThread(ThreadHandle, ThreadCycleTime, &CycleInfo, sizeof(CycleInfo), NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    *CycleTime = CycleInfo.AccumulatedCycles;
    return TRUE;
}

BOOL
WINAPI
QueryProcessCycleTime(IN HANDLE ProcessHandle,
                      OUT PULONG64 CycleTime)
{
    PROCESS_CYCLE_TIME_INFORMATION CycleInfo;
    NTSTATUS Status;

    Status = NtQueryInformationProcess(ProcessHandle, ProcessCycleTime, &CycleInfo, sizeof(CycleInfo), NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    *CycleTime = CycleInfo.AccumulatedCycles;
    return TRUE;
}

BOOL
WINAPI
QueryIdleProcessorCycleTime(IN OUT PULONG BufferLength,
                            OUT PULONG64 ProcessorIdleCycleTime)
{
    ULONG ReturnLength;
    NTSTATUS Status;

    Status = NtQuerySystemInformation(SystemProcessorIdleCycleTimeInformation,
                                      ProcessorIdleCycleTime,
                                      *BufferLength,
                                      &ReturnLength);
    if (!*BufferLength || !Status)
        *BufferLength = ReturnLength;

    return TRUE;
}

BOOL
WINAPI
QueryIdleProcessorCycleTimeEx(IN USHORT Group,
                              IN OUT PULONG BufferLength,
                              OUT PULONG64 ProcessorIdleCycleTime)
{
    ULONG ReturnLength;
    NTSTATUS Status;

    Status = NtQuerySystemInformationEx(SystemProcessorIdleCycleTimeInformation,
                                        &Group,
                                        sizeof(Group),
                                        ProcessorIdleCycleTime,
                                        *BufferLength,
                                        &ReturnLength);
    if (!*BufferLength || !Status)
        *BufferLength = ReturnLength;

    return TRUE;
}

BOOL
WINAPI
GetProcessorSystemCycleTime(IN USHORT Group,
                            OUT PSYSTEM_PROCESSOR_CYCLE_TIME_INFORMATION Buffer,
                            IN OUT PDWORD ReturnedLength)
{
    ULONG ReturnLength = 0;
    NTSTATUS Status;

    Status = NtQuerySystemInformationEx(SystemProcessorCycleTimeInformation,
                                        &Group,
                                        sizeof(Group),
                                        Buffer,
                                        *ReturnedLength,
                                        &ReturnLength);
    if (Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_INFO_LENGTH_MISMATCH)
    {
        *ReturnedLength = ReturnLength;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    *ReturnedLength = ReturnLength;
    return TRUE;
}
