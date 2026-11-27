/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for RtlCreateProcessReflection
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static
VOID
CheckReflection(
    _In_ ULONG Flags)
{
    RTLP_PROCESS_REFLECTION_REFLECTION_INFORMATION Info;
    PROCESS_BASIC_INFORMATION Basic;
    UCHAR Own[512], Clone[512];
    ULONG Length, Count = 0xdead;
    NTSTATUS Status;

    RtlZeroMemory(&Info, sizeof(Info));
    Status = RtlCreateProcessReflection(NtCurrentProcess(), Flags, NULL, NULL, NULL, &Info);
    ok(Status == STATUS_SUCCESS, "Flags 0x%lx: status 0x%lx\n", Flags, Status);
    if (!NT_SUCCESS(Status)) return;

    ok(Info.ReflectionProcessHandle != NULL, "Flags 0x%lx: no process handle\n", Flags);
    ok(Info.ReflectionThreadHandle != NULL, "Flags 0x%lx: no thread handle\n", Flags);
    ok(Info.ReflectionClientId.UniqueProcess != NtCurrentTeb()->ClientId.UniqueProcess, "Flags 0x%lx: same process\n", Flags);
    ok(Info.ReflectionClientId.UniqueThread != NULL, "Flags 0x%lx: no thread id\n", Flags);

    Status = NtQueryInformationProcess(Info.ReflectionProcessHandle, ProcessBasicInformation, &Basic, sizeof(Basic), &Length);
    ok(Status == STATUS_SUCCESS, "Flags 0x%lx: basic information 0x%lx\n", Flags, Status);
    if (NT_SUCCESS(Status))
    {
        ok(Basic.UniqueProcessId == (ULONG_PTR)Info.ReflectionClientId.UniqueProcess, "Flags 0x%lx: process id\n", Flags);
        ok(Basic.InheritedFromUniqueProcessId == (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess, "Flags 0x%lx: parent\n", Flags);
        ok(Basic.PebBaseAddress == NtCurrentPeb(), "Flags 0x%lx: PEB %p, expected %p\n", Flags, Basic.PebBaseAddress, NtCurrentPeb());
    }

    RtlZeroMemory(Own, sizeof(Own));
    RtlZeroMemory(Clone, sizeof(Clone));
    if (NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessImageFileName, Own, sizeof(Own), &Length)) &&
        NT_SUCCESS(NtQueryInformationProcess(Info.ReflectionProcessHandle, ProcessImageFileName, Clone, sizeof(Clone), &Length)))
    {
        ok(RtlEqualUnicodeString((PUNICODE_STRING)Own, (PUNICODE_STRING)Clone, TRUE), "Flags 0x%lx: image %wZ, expected %wZ\n",
           Flags, (PUNICODE_STRING)Clone, (PUNICODE_STRING)Own);
    }

    if (Flags & RTL_PROCESS_REFLECTION_FLAGS_NO_SUSPEND)
    {
        ok(WaitForSingleObject(Info.ReflectionThreadHandle, 1000) == WAIT_OBJECT_0, "Flags 0x%lx: thread still running\n", Flags);
    }
    else
    {
        Status = NtSuspendThread(Info.ReflectionThreadHandle, &Count);
        ok(Status == STATUS_SUCCESS, "Flags 0x%lx: suspend 0x%lx\n", Flags, Status);
        ok(Count == 1, "Flags 0x%lx: suspend count %lu\n", Flags, Count);
    }

    NtTerminateProcess(Info.ReflectionProcessHandle, STATUS_SUCCESS);
    NtClose(Info.ReflectionThreadHandle);
    NtClose(Info.ReflectionProcessHandle);
}

START_TEST(RtlCreateProcessReflection)
{
    static const ULONG Valid[] = { 0, 2, 4, 6, 8, 0x10 };
    static const ULONG Invalid[] = { 1, 0x20, 0x40, 0x80000000 };
    RTLP_PROCESS_REFLECTION_REFLECTION_INFORMATION Info;
    HANDLE Limited = NULL;
    NTSTATUS Status;
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(Valid); i++)
        CheckReflection(Valid[i]);

    for (i = 0; i < RTL_NUMBER_OF(Invalid); i++)
    {
        RtlZeroMemory(&Info, sizeof(Info));
        Status = RtlCreateProcessReflection(NtCurrentProcess(), Invalid[i], NULL, NULL, NULL, &Info);
        ok(Status == STATUS_INVALID_PARAMETER_2, "Flags 0x%lx: status 0x%lx\n", Invalid[i], Status);
        ok(Info.ReflectionProcessHandle == NULL && Info.ReflectionThreadHandle == NULL, "Flags 0x%lx: handles returned\n", Invalid[i]);
    }

    ok(DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &Limited,
                       PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0),
       "DuplicateHandle failed with %lu\n", GetLastError());
    RtlZeroMemory(&Info, sizeof(Info));
    Status = RtlCreateProcessReflection(Limited, RTL_PROCESS_REFLECTION_FLAGS_INHERIT_HANDLES, NULL, NULL, NULL, &Info);
    ok_ntstatus(Status, STATUS_ACCESS_DENIED);
    if (NT_SUCCESS(Status)) NtTerminateProcess(Info.ReflectionProcessHandle, STATUS_SUCCESS);
    if (Limited) NtClose(Limited);
}
