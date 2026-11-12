/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Return to user mode at a non-canonical instruction pointer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/kefuncs.h>
#include <ndk/psfuncs.h>

#define NON_CANONICAL_RIP 0x0000800000000000ULL
#define RETURN_FAULT_EXIT_CODE 0x5A5A0001
#define RETURN_FAULT_R12 0x1212343456567878ULL

typedef struct _RETURN_FAULT
{
    DWORD ThreadId;
    LONG Hits;
    NTSTATUS Code;
    ULONG Parameters;
    ULONG_PTR Information[2];
    PVOID Address;
    DWORD64 Rip;
    DWORD64 R12;
    WORD SegCs;
    WORD ThreadSegCs;
} RETURN_FAULT;

static RETURN_FAULT Fault;

static
DECLSPEC_NORETURN
VOID
NTAPI
ReturnFaultExit(
    _In_ ULONG_PTR Code)
{
    ExitThread((DWORD)Code);
}

static
LONG
NTAPI
ReturnFaultHandler(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    PEXCEPTION_RECORD Record = Pointers->ExceptionRecord;
    PCONTEXT Context = Pointers->ContextRecord;

    if (GetCurrentThreadId() != Fault.ThreadId)
        return EXCEPTION_CONTINUE_SEARCH;

    Fault.Hits++;
    Fault.Code = Record->ExceptionCode;
    Fault.Parameters = Record->NumberParameters;
    Fault.Information[0] = Record->ExceptionInformation[0];
    Fault.Information[1] = Record->ExceptionInformation[1];
    Fault.Address = Record->ExceptionAddress;
    Fault.Rip = Context->Rip;
    Fault.R12 = Context->R12;
    Fault.SegCs = Context->SegCs;

    Context->Rsp = (Context->Rsp & ~(DWORD64)15) - 0x88;
    Context->Rip = (DWORD64)ReturnFaultExit;
    Context->Rcx = RETURN_FAULT_EXIT_CODE;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static
DWORD
WINAPI
SetContextReturnThread(
    _In_ PVOID Parameter)
{
    CONTEXT Context;

    UNREFERENCED_PARAMETER(Parameter);

    RtlCaptureContext(&Context);
    Fault.ThreadSegCs = Context.SegCs;
    Context.ContextFlags = CONTEXT_CONTROL;
    Context.Rip = NON_CANONICAL_RIP;
    Context.Rsp -= 0x100;
    NtSetContextThread(NtCurrentThread(), &Context);
    return 0;
}

static
DWORD
WINAPI
ContinueReturnThread(
    _In_ PVOID Parameter)
{
    CONTEXT Context;

    UNREFERENCED_PARAMETER(Parameter);

    RtlCaptureContext(&Context);
    Fault.ThreadSegCs = Context.SegCs;
    Context.ContextFlags = CONTEXT_FULL;
    Context.Rip = NON_CANONICAL_RIP;
    Context.Rsp -= 0x100;
    Context.R12 = RETURN_FAULT_R12;
    NtContinue(&Context, FALSE);
    return 0;
}

static
VOID
TestReturnFault(
    _In_ LPTHREAD_START_ROUTINE Routine,
    _In_ BOOLEAN FullContext)
{
    HANDLE Thread;
    DWORD Wait, ExitCode = 0;

    RtlZeroMemory(&Fault, sizeof(Fault));
    Thread = CreateThread(NULL, 0x10000, Routine, NULL, CREATE_SUSPENDED, &Fault.ThreadId);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!Thread)
        return;

    ResumeThread(Thread);
    Wait = WaitForSingleObject(Thread, 10000);
    ok_eq_ulong(Wait, WAIT_OBJECT_0);
    if (Wait != WAIT_OBJECT_0)
    {
        TerminateThread(Thread, 0);
        CloseHandle(Thread);
        return;
    }

    ok(GetExitCodeThread(Thread, &ExitCode), "GetExitCodeThread failed with %lu\n", GetLastError());
    ok_eq_hex(ExitCode, RETURN_FAULT_EXIT_CODE);
    ok_eq_long(Fault.Hits, 1);
    ok_eq_hex(Fault.Code, STATUS_ACCESS_VIOLATION);
    ok_eq_ulong(Fault.Parameters, 2);
    ok_eq_ulongptr(Fault.Information[0], 0);
    ok_eq_ulongptr(Fault.Information[1], (ULONG_PTR)-1);
    ok_eq_pointer(Fault.Address, (PVOID)NON_CANONICAL_RIP);
    ok_eq_hex64(Fault.Rip, NON_CANONICAL_RIP);
    ok_eq_uint(Fault.SegCs, Fault.ThreadSegCs);
    if (FullContext)
        ok_eq_hex64(Fault.R12, RETURN_FAULT_R12);

    CloseHandle(Thread);
}

START_TEST(KeNonCanonicalReturn)
{
    PVOID Handler;

    Handler = RtlAddVectoredExceptionHandler(1, ReturnFaultHandler);
    ok(Handler != NULL, "RtlAddVectoredExceptionHandler failed\n");
    if (!Handler)
        return;

    TestReturnFault(SetContextReturnThread, FALSE);
    TestReturnFault(ContinueReturnThread, TRUE);

    RtlRemoveVectoredExceptionHandler(Handler);
}
