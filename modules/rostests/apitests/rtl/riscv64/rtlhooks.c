/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 RTL hooks for the standalone RTL tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtltests.h>

NTSTATUS
NTAPI
RtlpRiscv64ReadMemory(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ SIZE_T Length)
{
    return NtReadVirtualMemory(NtCurrentProcess(), (PVOID)Source, Destination, Length, NULL);
}

DECLSPEC_NORETURN
VOID
NTAPI
RtlpRiscv64RaiseFatal(
    _In_ NTSTATUS Status)
{
    NtTerminateProcess(NtCurrentProcess(), Status);
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

NTSTATUS
NTAPI
RtlpRiscv64UnwindUserException(
    _In_ ULONG_PTR ControlPc,
    _In_ ULONG_PTR StackLow,
    _In_ ULONG_PTR StackHigh,
    _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(ControlPc);
    UNREFERENCED_PARAMETER(StackLow);
    UNREFERENCED_PARAMETER(StackHigh);
    UNREFERENCED_PARAMETER(Context);
    return STATUS_NOT_FOUND;
}

VOID
NTAPI
RtlpRiscv64PrepareContextRestore(
    _In_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
}
