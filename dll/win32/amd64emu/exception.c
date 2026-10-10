/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Deliver exceptions to the guest the way the AMD64 kernel does
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "amd64emu.h"

#define EMU_INTERRUPT_FASTFAIL 0x29
#define EMU_INTERRUPT_ASSERT   0x2C
#define EMU_INTERRUPT_DEBUG    0x2D

NTSTATUS
EmuDispatchException(
    _In_ PEMU_THREAD Thread,
    _In_ const EXCEPTION_RECORD *Record)
{
    PEMU_REGISTERS Registers = EmuCoreGetRegisters(Thread->Cpu);
    PAMD64_EXCEPTION_STACK Stack;
    AMD64_CONTEXT Context;
    NTSTATUS Status = STATUS_SUCCESS;

    Context.ContextFlags = CONTEXT_AMD64_FULL | CONTEXT_AMD64_SEGMENTS;
    EmuCoreGetContext(Thread->Cpu, &Context);
    Stack = (PVOID)((Context.Rsp - sizeof(*Stack)) & ~(ULONG64)63);

    _SEH2_TRY
    {
        RtlZeroMemory(Stack, sizeof(*Stack));
        Stack->Context = Context;
        Stack->ExceptionRecord = *Record;
        Stack->ContextEx.Legacy.Offset = -(LONG)sizeof(AMD64_CONTEXT);
        Stack->ContextEx.Legacy.Length = sizeof(AMD64_CONTEXT);
        Stack->ContextEx.XState.Offset = (LONG)((ULONG_PTR)(Stack + 1) - (ULONG_PTR)&Stack->ContextEx);
        Stack->ContextEx.XState.Length = 0;
        Stack->ContextEx.All.Offset = -(LONG)sizeof(AMD64_CONTEXT);
        Stack->ContextEx.All.Length = sizeof(AMD64_CONTEXT) + Stack->ContextEx.XState.Offset;
        Stack->MachineFrame.Rip = Context.Rip;
        Stack->MachineFrame.SegCs = Context.SegCs;
        Stack->MachineFrame.EFlags = Context.EFlags;
        Stack->MachineFrame.Rsp = Context.Rsp;
        Stack->MachineFrame.SegSs = Context.SegSs;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status)) return Status;

    Registers->Gpr[EmuRsp] = (ULONG64)Stack;
    Registers->Gpr[EmuRcx] = (ULONG64)&Stack->ExceptionRecord;
    Registers->Gpr[EmuRdx] = (ULONG64)&Stack->Context;
    Registers->Rip = EmuProcess.KiUserExceptionDispatcher;
    return STATUS_SUCCESS;
}

NTSTATUS
EmuContinue(
    _In_ PEMU_THREAD Thread,
    _In_ const AMD64_CONTEXT *Context)
{
    AMD64_CONTEXT Captured = *Context;

    if ((Captured.ContextFlags & CONTEXT_AMD64) != CONTEXT_AMD64) return STATUS_INVALID_PARAMETER;
    EmuCoreSetContext(Thread->Cpu, &Captured);
    return STATUS_SUCCESS;
}

NTSTATUS
EmuRaiseException(
    _In_ PEMU_THREAD Thread,
    _In_ const EXCEPTION_RECORD *Record,
    _In_ const AMD64_CONTEXT *Context,
    _In_ BOOLEAN FirstChance)
{
    EXCEPTION_RECORD CapturedRecord = *Record;
    AMD64_CONTEXT CapturedContext = *Context;

    if ((CapturedContext.ContextFlags & CONTEXT_AMD64) != CONTEXT_AMD64) return STATUS_INVALID_PARAMETER;

    if (!FirstChance)
    {
        DbgPrint("AMD64EMU: unhandled exception %08lx at %p\n",
                 CapturedRecord.ExceptionCode,
                 CapturedRecord.ExceptionAddress);
        NtTerminateProcess(NtCurrentProcess(), CapturedRecord.ExceptionCode);
        return STATUS_SUCCESS;
    }

    EmuCoreSetContext(Thread->Cpu, &CapturedContext);
    return EmuDispatchException(Thread, &CapturedRecord);
}

BOOLEAN
EmuSoftwareInterrupt(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG Vector,
    _Inout_ PEXCEPTION_RECORD Record)
{
    PEMU_REGISTERS Registers = EmuCoreGetRegisters(Thread->Cpu);
    ULONG Length;

    switch (Vector)
    {
        case EMU_INTERRUPT_DEBUG:
            if ((ULONG)Registers->Gpr[EmuRax] == BREAKPOINT_PRINT)
            {
                Length = (ULONG)Registers->Gpr[EmuRdx];
                if (Length > 0xFFFF) Length = 0xFFFF;
                _SEH2_TRY
                {
                    DbgPrintEx((ULONG)Registers->Gpr[EmuR8],
                               (ULONG)Registers->Gpr[EmuR9],
                               "%.*s",
                               Length,
                               (PCSTR)Registers->Gpr[EmuRcx]);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                }
                _SEH2_END;
            }
            Registers->Gpr[EmuRax] = STATUS_SUCCESS;
            Registers->Rip += 3;
            return TRUE;

        case EMU_INTERRUPT_ASSERT:
            Record->ExceptionCode = STATUS_ASSERTION_FAILURE;
            Record->NumberParameters = 0;
            return FALSE;

        case EMU_INTERRUPT_FASTFAIL:
            DbgPrint("AMD64EMU: fast fail %lu at %p\n", (ULONG)Registers->Gpr[EmuRcx], (PVOID)Registers->Rip);
            NtTerminateProcess(NtCurrentProcess(), STATUS_STACK_BUFFER_OVERRUN);
            return TRUE;
    }

    return FALSE;
}

LONG
NTAPI
EmuVectoredHandler(
    _In_ PEXCEPTION_POINTERS ExceptionInfo)
{
    PEMU_THREAD Thread = EmuCurrentThread();
    EXCEPTION_RECORD GuestRecord;
    ULONG Interrupt;
    EMU_FAULT Fault;
    NTSTATUS Status;

    if (!Thread || !Thread->Cpu) return EXCEPTION_CONTINUE_SEARCH;

    Fault = EmuCoreFault(Thread->Cpu,
                         Thread->HostDepth != 0,
                         ExceptionInfo->ExceptionRecord,
                         ExceptionInfo->ContextRecord,
                         &GuestRecord,
                         &Interrupt);
    if (Fault == EmuFaultUnrelated) return EXCEPTION_CONTINUE_SEARCH;
    if (Fault == EmuFaultResume) return EXCEPTION_CONTINUE_EXECUTION;

    Thread->HostDepth++;
    if (Interrupt == EMU_NO_INTERRUPT || !EmuSoftwareInterrupt(Thread, Interrupt, &GuestRecord))
    {
        Status = EmuDispatchException(Thread, &GuestRecord);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("AMD64EMU: exception %08lx at %p cannot be dispatched: %08lx\n",
                     GuestRecord.ExceptionCode,
                     GuestRecord.ExceptionAddress,
                     Status);
            NtTerminateProcess(NtCurrentProcess(), GuestRecord.ExceptionCode);
        }
    }
    Thread->HostDepth--;

    return EXCEPTION_CONTINUE_EXECUTION;
}
