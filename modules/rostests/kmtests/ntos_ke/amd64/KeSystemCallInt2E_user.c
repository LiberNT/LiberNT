/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Software interrupt 0x2E from user mode without the int 0x2E system call mechanism
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#ifndef SYSTEM_CALL_SYSCALL
#define SYSTEM_CALL_SYSCALL 0
#endif

typedef struct _INT2E_FAULT
{
    DWORD ThreadId;
    PVOID Address;
    LONG Hits;
    NTSTATUS Code;
    ULONG Parameters;
    ULONG_PTR Information[2];
    PVOID ExceptionAddress;
} INT2E_FAULT;

static INT2E_FAULT Int2eFault;

static
LONG
NTAPI
Int2eHandler(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    PEXCEPTION_RECORD Record = Pointers->ExceptionRecord;

    if (GetCurrentThreadId() != Int2eFault.ThreadId ||
        (PVOID)Pointers->ContextRecord->Rip != Int2eFault.Address)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    Int2eFault.Hits++;
    Int2eFault.Code = Record->ExceptionCode;
    Int2eFault.Parameters = Record->NumberParameters;
    Int2eFault.Information[0] = Record->ExceptionInformation[0];
    Int2eFault.Information[1] = Record->ExceptionInformation[1];
    Int2eFault.ExceptionAddress = Record->ExceptionAddress;

    Pointers->ContextRecord->Rip += 2;
    return EXCEPTION_CONTINUE_EXECUTION;
}

START_TEST(KeSystemCallInt2E)
{
    PVOID Handler;

    ok_eq_ulong(SharedUserData->SystemCall, SYSTEM_CALL_SYSCALL);
    if (SharedUserData->SystemCall != SYSTEM_CALL_SYSCALL)
        return;

    RtlZeroMemory(&Int2eFault, sizeof(Int2eFault));
    Int2eFault.ThreadId = GetCurrentThreadId();
    Handler = RtlAddVectoredExceptionHandler(1, Int2eHandler);
    ok(Handler != NULL, "RtlAddVectoredExceptionHandler failed\n");
    if (!Handler)
        return;

    __asm__ __volatile__("leaq 1f(%%rip), %%rdx\n\t"
                         "movq %%rdx, %0\n\t"
                         "movl $0xFFFFFFFF, %%eax\n\t"
                         "1: int $0x2e\n\t"
                         : "=m"(Int2eFault.Address)
                         :
                         : "rax", "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");

    RtlRemoveVectoredExceptionHandler(Handler);

    ok_eq_long(Int2eFault.Hits, 1);
    ok_eq_hex(Int2eFault.Code, STATUS_ACCESS_VIOLATION);
    ok_eq_ulong(Int2eFault.Parameters, 2);
    ok_eq_ulongptr(Int2eFault.Information[0], 0);
    ok_eq_ulongptr(Int2eFault.Information[1], (ULONG_PTR)-1);
    ok_eq_pointer(Int2eFault.ExceptionAddress, Int2eFault.Address);
}
