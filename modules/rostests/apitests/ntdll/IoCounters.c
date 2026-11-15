/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that file reads and writes are counted for the system and the process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define IO_COUNTERS_CALLS 8
#define IO_COUNTERS_CHUNK 512

static
BOOLEAN
QueryCounters(
    _Out_ PSYSTEM_PERFORMANCE_INFORMATION System,
    _Out_ PIO_COUNTERS Process)
{
    NTSTATUS Status;

    Status = NtQuerySystemInformation(SystemPerformanceInformation, System, sizeof(*System), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessIoCounters, Process, sizeof(*Process), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status);
}

START_TEST(IoCounters)
{
    SYSTEM_PERFORMANCE_INFORMATION SystemBefore, SystemAfter;
    IO_COUNTERS ProcessBefore, ProcessAfter;
    UCHAR Buffer[IO_COUNTERS_CHUNK];
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    ULONG Index;
    DWORD Done;
    HANDLE File;

    ok(GetTempPathW(RTL_NUMBER_OF(Directory), Directory) != 0, "GetTempPathW failed with %lu\n", GetLastError());
    ok(GetTempFileNameW(Directory, L"ioc", 0, Path) != 0, "GetTempFileNameW failed with %lu\n", GetLastError());

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    RtlFillMemory(Buffer, sizeof(Buffer), 0x5A);

    if (!QueryCounters(&SystemBefore, &ProcessBefore))
    {
        CloseHandle(File);
        return;
    }

    for (Index = 0; Index < IO_COUNTERS_CALLS; Index++)
    {
        ok(WriteFile(File, Buffer, sizeof(Buffer), &Done, NULL) && Done == sizeof(Buffer),
           "WriteFile %lu failed with %lu\n", Index, GetLastError());
    }

    SetFilePointer(File, 0, NULL, FILE_BEGIN);
    for (Index = 0; Index < IO_COUNTERS_CALLS; Index++)
    {
        ok(ReadFile(File, Buffer, sizeof(Buffer), &Done, NULL) && Done == sizeof(Buffer),
           "ReadFile %lu failed with %lu\n", Index, GetLastError());
    }

    if (QueryCounters(&SystemAfter, &ProcessAfter))
    {
        ok(SystemAfter.IoReadOperationCount - SystemBefore.IoReadOperationCount >= IO_COUNTERS_CALLS,
           "System reads went from %lu to %lu\n", SystemBefore.IoReadOperationCount, SystemAfter.IoReadOperationCount);
        ok(SystemAfter.IoWriteOperationCount - SystemBefore.IoWriteOperationCount >= IO_COUNTERS_CALLS,
           "System writes went from %lu to %lu\n", SystemBefore.IoWriteOperationCount, SystemAfter.IoWriteOperationCount);
        ok(SystemAfter.IoReadTransferCount.QuadPart - SystemBefore.IoReadTransferCount.QuadPart >= IO_COUNTERS_CALLS * IO_COUNTERS_CHUNK,
           "System read bytes went from %I64d to %I64d\n", SystemBefore.IoReadTransferCount.QuadPart, SystemAfter.IoReadTransferCount.QuadPart);
        ok(SystemAfter.IoWriteTransferCount.QuadPart - SystemBefore.IoWriteTransferCount.QuadPart >= IO_COUNTERS_CALLS * IO_COUNTERS_CHUNK,
           "System written bytes went from %I64d to %I64d\n", SystemBefore.IoWriteTransferCount.QuadPart, SystemAfter.IoWriteTransferCount.QuadPart);

        ok(ProcessAfter.ReadOperationCount - ProcessBefore.ReadOperationCount >= IO_COUNTERS_CALLS,
           "Process reads went from %I64u to %I64u\n", ProcessBefore.ReadOperationCount, ProcessAfter.ReadOperationCount);
        ok(ProcessAfter.WriteOperationCount - ProcessBefore.WriteOperationCount >= IO_COUNTERS_CALLS,
           "Process writes went from %I64u to %I64u\n", ProcessBefore.WriteOperationCount, ProcessAfter.WriteOperationCount);
        ok(ProcessAfter.ReadTransferCount - ProcessBefore.ReadTransferCount >= IO_COUNTERS_CALLS * IO_COUNTERS_CHUNK,
           "Process read bytes went from %I64u to %I64u\n", ProcessBefore.ReadTransferCount, ProcessAfter.ReadTransferCount);
        ok(ProcessAfter.WriteTransferCount - ProcessBefore.WriteTransferCount >= IO_COUNTERS_CALLS * IO_COUNTERS_CHUNK,
           "Process written bytes went from %I64u to %I64u\n", ProcessBefore.WriteTransferCount, ProcessAfter.WriteTransferCount);
    }

    CloseHandle(File);
}
