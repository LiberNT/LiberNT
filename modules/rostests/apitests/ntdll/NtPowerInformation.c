/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test NtPowerInformation(ProcessorInformation): status codes and per-processor frequencies
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define BUSY_MILLISECONDS 100

typedef struct _PROCESSOR_POWER_INFO
{
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
} PROCESSOR_POWER_INFO, *PPROCESSOR_POWER_INFO;

static
VOID
SpinOnProcessor(
    _In_ ULONG Processor)
{
    DWORD_PTR Old;
    ULONGLONG End;
    volatile ULONG Sink = 0;

    Old = SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << Processor);
    End = GetTickCount64() + BUSY_MILLISECONDS;
    while (GetTickCount64() < End)
        Sink++;
    SetThreadAffinityMask(GetCurrentThread(), Old);
}

START_TEST(NtPowerInformation)
{
    PPROCESSOR_POWER_INFO Info;
    SYSTEM_INFO SystemInfo;
    ULONG Size, Index;
    NTSTATUS Status;
    ULONG Input = 0;

    GetSystemInfo(&SystemInfo);
    Size = SystemInfo.dwNumberOfProcessors * sizeof(*Info);
    Info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    ok(Info != NULL, "HeapAlloc failed\n");
    if (Info == NULL)
        return;

    Status = NtPowerInformation(ProcessorInformation, NULL, 0, Info, Size - 1);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);

    Status = NtPowerInformation(ProcessorInformation, &Input, sizeof(Input), Info, Size);
    ok_ntstatus(Status, STATUS_INVALID_PARAMETER);

    Status = NtPowerInformation(ProcessorInformation, NULL, 0, Info, Size);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < SystemInfo.dwNumberOfProcessors; Index++)
        {
            ok(Info[Index].Number == Index, "Processor %lu reported number %lu\n", Index, Info[Index].Number);
            ok(Info[Index].MaxMhz != 0, "Processor %lu reported no MaxMhz\n", Index);
            ok(Info[Index].CurrentMhz != 0, "Processor %lu reported no CurrentMhz\n", Index);
            ok(Info[Index].MhzLimit != 0 && Info[Index].MhzLimit <= Info[Index].MaxMhz,
               "Processor %lu reported MhzLimit %lu for MaxMhz %lu\n", Index, Info[Index].MhzLimit, Info[Index].MaxMhz);
            trace("idle cpu %lu max %lu current %lu limit %lu idle states %lu/%lu\n", Index, Info[Index].MaxMhz,
                  Info[Index].CurrentMhz, Info[Index].MhzLimit, Info[Index].CurrentIdleState, Info[Index].MaxIdleState);
        }
    }

    for (Index = 0; Index < SystemInfo.dwNumberOfProcessors; Index++)
        SpinOnProcessor(Index);

    Status = NtPowerInformation(ProcessorInformation, NULL, 0, Info, Size);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < SystemInfo.dwNumberOfProcessors; Index++)
        {
            trace("busy cpu %lu max %lu current %lu limit %lu\n", Index, Info[Index].MaxMhz,
                  Info[Index].CurrentMhz, Info[Index].MhzLimit);
        }
    }

    HeapFree(GetProcessHeap(), 0, Info);
}
