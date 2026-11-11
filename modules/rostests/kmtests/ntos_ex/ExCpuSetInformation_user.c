/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SystemCpuSetInformation through NtQuerySystemInformationEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>

START_TEST(ExCpuSetInformation)
{
    PSYSTEM_CPU_SET_INFORMATION Info;
    SYSTEM_INFO SystemInfo;
    HANDLE Process;
    ULONG Expected;
    ULONG Length;
    ULONG Index;
    NTSTATUS Status;

    GetSystemInfo(&SystemInfo);
    Expected = SystemInfo.dwNumberOfProcessors * (ULONG)sizeof(*Info);

    Process = GetCurrentProcess();
    Length = 0xdeadbeef;
    Status = NtQuerySystemInformationEx(SystemCpuSetInformation, &Process, sizeof(Process), NULL, 0, &Length);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_ulong(Length, Expected);

    Process = (HANDLE)(ULONG_PTR)0xdeadbeef;
    Length = 0xdeadbeef;
    Status = NtQuerySystemInformationEx(SystemCpuSetInformation, &Process, sizeof(Process), NULL, 0, &Length);
    ok_eq_hex(Status, STATUS_INVALID_HANDLE);
    ok_eq_ulong(Length, 0xdeadbeef);

    Process = NULL;
    Length = 0xdeadbeef;
    Status = NtQuerySystemInformationEx(SystemCpuSetInformation, NULL, sizeof(Process), NULL, 0, &Length);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_ulong(Length, 0xdeadbeef);

    Length = 0xdeadbeef;
    Status = NtQuerySystemInformationEx(SystemCpuSetInformation, &Process, sizeof(Process), NULL, 0, &Length);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_ulong(Length, Expected);

    Length = 0xdeadbeef;
    Status = NtQuerySystemInformationEx(SystemCpuSetInformation, &Process, sizeof(Process), NULL, Expected, &Length);
    ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
    ok_eq_ulong(Length, 0xdeadbeef);

    Info = HeapAlloc(GetProcessHeap(), 0, Expected);
    if (skip(Info != NULL, "no heap\n"))
        return;

    Process = GetCurrentProcess();
    Length = 0;
    Status = NtQuerySystemInformationEx(SystemCpuSetInformation, &Process, sizeof(Process), Info, Expected, &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Length, Expected);

    for (Index = 0; NT_SUCCESS(Status) && (Index < SystemInfo.dwNumberOfProcessors); Index++)
    {
        ok(Info[Index].Size == sizeof(*Info), "record %lu: size %lu\n", Index, Info[Index].Size);
        ok(Info[Index].Type == CpuSetInformation, "record %lu: type %u\n", Index, Info[Index].Type);
        ok(Info[Index].CpuSet.Id == 0x100 + Index, "record %lu: id 0x%lx\n", Index, Info[Index].CpuSet.Id);
        ok(Info[Index].CpuSet.Group == 0, "record %lu: group %u\n", Index, Info[Index].CpuSet.Group);
        ok(Info[Index].CpuSet.LogicalProcessorIndex == Index,
           "record %lu: logical processor %u\n", Index, Info[Index].CpuSet.LogicalProcessorIndex);
        ok(Info[Index].CpuSet.AllFlags == 0, "record %lu: flags 0x%x\n", Index, Info[Index].CpuSet.AllFlags);
    }

    HeapFree(GetProcessHeap(), 0, Info);
}
