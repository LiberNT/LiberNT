/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that NtFlushInstructionCache publishes patched code, its status codes and its cost
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define PATCH_ROUNDS 256
#define TIMED_FLUSHES 4096

typedef ULONG (*PCODE_FUNCTION)(VOID);

static
SIZE_T
EmitReturnConstant(
    _Out_writes_bytes_(16) PUCHAR Code,
    _In_ USHORT Value)
{
#if defined(_M_AMD64) || defined(_M_IX86)
    Code[0] = 0xB8;
    *(ULONG UNALIGNED *)&Code[1] = Value;
    Code[5] = 0xC3;
    return 6;
#elif defined(_M_ARM64)
    ((PULONG)Code)[0] = 0x52800000 | ((ULONG)Value << 5);
    ((PULONG)Code)[1] = 0xD65F03C0;
    return 8;
#else
    UNREFERENCED_PARAMETER(Code);
    UNREFERENCED_PARAMETER(Value);
    return 0;
#endif
}

static
VOID
TestPatchedCode(
    _In_ PUCHAR Code)
{
    PCODE_FUNCTION Function = (PCODE_FUNCTION)Code;
    NTSTATUS Status;
    ULONG Index;
    ULONG Mismatches = 0;
    SIZE_T Length;

    for (Index = 0; Index < PATCH_ROUNDS; Index++)
    {
        Length = EmitReturnConstant(Code, (USHORT)(Index + 1));
        if (Length == 0)
        {
            skip("No code emitter for this architecture\n");
            return;
        }

        Status = NtFlushInstructionCache(NtCurrentProcess(), Code, Length);
        if (!NT_SUCCESS(Status))
        {
            ok_ntstatus(Status, STATUS_SUCCESS);
            return;
        }

        if (Function() != Index + 1)
            Mismatches++;
    }

    ok(Mismatches == 0, "%lu of %u patched functions returned the old value\n", Mismatches, PATCH_ROUNDS);
}

static
VOID
TestParameters(
    _In_ PUCHAR Code)
{
    NTSTATUS Status;

    Status = NtFlushInstructionCache(NtCurrentProcess(), NULL, 0);
    ok_ntstatus(Status, STATUS_SUCCESS);

    Status = NtFlushInstructionCache(NtCurrentProcess(), Code, 0);
    ok_ntstatus(Status, STATUS_SUCCESS);

    Status = NtFlushInstructionCache(NULL, Code, 16);
    ok_ntstatus(Status, STATUS_INVALID_HANDLE);

    Status = NtFlushInstructionCache(NtCurrentProcess(), (PVOID)(~(ULONG_PTR)0xFFFF), 16);
    ok_ntstatus(Status, STATUS_ACCESS_VIOLATION);

    Status = NtFlushInstructionCache(NtCurrentProcess(), Code, ~(SIZE_T)0);
    ok_ntstatus(Status, STATUS_ACCESS_VIOLATION);
}

static
VOID
TestCost(
    _In_ PUCHAR Code)
{
    LARGE_INTEGER Frequency, Start, End;
    ULONG Index;
    ULONG Failures = 0;
    ULONGLONG Nanoseconds;

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);
    for (Index = 0; Index < TIMED_FLUSHES; Index++)
    {
        if (!NT_SUCCESS(NtFlushInstructionCache(NtCurrentProcess(), Code, 16)))
            Failures++;
    }
    QueryPerformanceCounter(&End);

    ok(Failures == 0, "%lu flushes failed\n", Failures);

    Nanoseconds = (ULONGLONG)(End.QuadPart - Start.QuadPart) * 1000000000ULL / (ULONGLONG)Frequency.QuadPart;
    trace("%u flushes of 16 bytes took %I64u ns, %I64u ns each\n",
          TIMED_FLUSHES, Nanoseconds, Nanoseconds / TIMED_FLUSHES);
}

START_TEST(NtFlushInstructionCache)
{
    PUCHAR Code;

    Code = VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    ok(Code != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (Code == NULL)
        return;

    TestPatchedCode(Code);
    TestParameters(Code);
    TestCost(Code);

    VirtualFree(Code, 0, MEM_RELEASE);
}
