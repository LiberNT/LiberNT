/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Measures what the compositor and the system spend while the desktop is idle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <apitest.h>
#include <windows.h>
#include <ndk/umtypes.h>
#include <ndk/extypes.h>
#include <ndk/exfuncs.h>

#define IDLE_SETTLE_MS 1000
#define IDLE_WINDOW_MS 3000
#define IDLE_MAX_CPUS  64

typedef struct _IDLE_SAMPLE
{
    ULONGLONG DwmTime100ns;
    ULONGLONG DwmCycles;
    ULONGLONG BusyTime100ns;
    ULONGLONG TotalTime100ns;
    ULONGLONG Dpcs;
    ULONGLONG ContextSwitches;
    ULONGLONG Qpc;
    BOOL DwmFound;
} IDLE_SAMPLE;

static BOOL
IdleQueryDwm(_Out_ PULONGLONG Time100ns, _Out_ PULONGLONG Cycles)
{
    static const WCHAR DwmName[] = L"dwm.exe";
    ULONG Size = 0x40000;
    PVOID Buffer = NULL;
    PSYSTEM_PROCESS_INFORMATION Process;
    NTSTATUS Status;
    BOOL Found = FALSE;

    *Time100ns = 0;
    *Cycles = 0;
    for (;;)
    {
        Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
        if (Buffer == NULL)
            return FALSE;
        Status = NtQuerySystemInformation(SystemProcessInformation, Buffer, Size, NULL);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
            break;
        HeapFree(GetProcessHeap(), 0, Buffer);
        Size *= 2;
    }
    if (!NT_SUCCESS(Status))
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return FALSE;
    }

    Process = (PSYSTEM_PROCESS_INFORMATION)Buffer;
    for (;;)
    {
        if (Process->ImageName.Buffer != NULL &&
            Process->ImageName.Length == sizeof(DwmName) - sizeof(WCHAR) &&
            _wcsnicmp(Process->ImageName.Buffer, DwmName, ARRAYSIZE(DwmName) - 1) == 0)
        {
            *Time100ns += (ULONGLONG)Process->UserTime.QuadPart +
                          (ULONGLONG)Process->KernelTime.QuadPart;
            *Cycles += Process->CycleTime;
            Found = TRUE;
        }
        if (Process->NextEntryOffset == 0)
            break;
        Process = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)Process + Process->NextEntryOffset);
    }
    HeapFree(GetProcessHeap(), 0, Buffer);
    return Found;
}

static VOID
IdleSample(_Out_ IDLE_SAMPLE *Sample)
{
    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION Cpu[IDLE_MAX_CPUS];
    SYSTEM_INTERRUPT_INFORMATION Interrupts[IDLE_MAX_CPUS];
    LARGE_INTEGER Counter;
    ULONG Length, Index;

    ZeroMemory(Sample, sizeof(*Sample));
    Sample->DwmFound = IdleQueryDwm(&Sample->DwmTime100ns, &Sample->DwmCycles);

    if (NT_SUCCESS(NtQuerySystemInformation(SystemProcessorPerformanceInformation,
                                            Cpu, sizeof(Cpu), &Length)))
    {
        for (Index = 0; Index < Length / sizeof(Cpu[0]); ++Index)
        {
            ULONGLONG Total = (ULONGLONG)Cpu[Index].KernelTime.QuadPart +
                              (ULONGLONG)Cpu[Index].UserTime.QuadPart;

            Sample->TotalTime100ns += Total;
            Sample->BusyTime100ns += Total - (ULONGLONG)Cpu[Index].IdleTime.QuadPart;
        }
    }
    if (NT_SUCCESS(NtQuerySystemInformation(SystemInterruptInformation,
                                            Interrupts, sizeof(Interrupts), &Length)))
    {
        for (Index = 0; Index < Length / sizeof(Interrupts[0]); ++Index)
        {
            Sample->Dpcs += Interrupts[Index].DpcCount;
            Sample->ContextSwitches += Interrupts[Index].ContextSwitches;
        }
    }
    QueryPerformanceCounter(&Counter);
    Sample->Qpc = (ULONGLONG)Counter.QuadPart;
}

START_TEST(idle_cost)
{
    IDLE_SAMPLE Before, After;
    LARGE_INTEGER Frequency;
    ULONGLONG ElapsedUs, DwmUs, BusyPermille;

    QueryPerformanceFrequency(&Frequency);
    Sleep(IDLE_SETTLE_MS);
    IdleSample(&Before);
    if (!Before.DwmFound)
    {
        skip("dwm.exe is not running\n");
        return;
    }
    Sleep(IDLE_WINDOW_MS);
    IdleSample(&After);
    ok(After.DwmFound, "dwm.exe exited during the measurement\n");

    ElapsedUs = ((After.Qpc - Before.Qpc) * 1000000ull) / (ULONGLONG)Frequency.QuadPart;
    DwmUs = (After.DwmTime100ns - Before.DwmTime100ns) / 10;
    BusyPermille = After.TotalTime100ns > Before.TotalTime100ns ?
        ((After.BusyTime100ns - Before.BusyTime100ns) * 1000ull) /
            (After.TotalTime100ns - Before.TotalTime100ns) : 0;
    trace("idle %llu ms: dwm.exe %llu us CPU, %llu cycles, system busy %llu.%llu%%, %llu DPCs/s, %llu context switches/s\n",
          ElapsedUs / 1000, DwmUs, After.DwmCycles - Before.DwmCycles, BusyPermille / 10, BusyPermille % 10,
          ElapsedUs ? ((After.Dpcs - Before.Dpcs) * 1000000ull) / ElapsedUs : 0,
          ElapsedUs ? ((After.ContextSwitches - Before.ContextSwitches) * 1000000ull) / ElapsedUs : 0);
    ok(DwmUs * 50 <= ElapsedUs,
       "dwm.exe used %llu us of CPU in %llu us of idle desktop\n", DwmUs, ElapsedUs);
}
