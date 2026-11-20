/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM (Thumb-2) C language exception handler and longjmp unwind
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>

LONG __cdecl __C_ExecuteHandler(PVOID Argument, PVOID EstablisherFrame, PVOID Handler, PULONG NonVolatileRegisters);

/*
 * The scope table (SCOPE_TABLE_ARM) lists guarded ranges as image RVAs; a zero
 * JumpTarget marks a __finally, otherwise HandlerAddress is the filter or
 * EXCEPTION_EXECUTE_HANDLER and JumpTarget the __except block.
 */
EXCEPTION_DISPOSITION
__cdecl
__C_specific_handler(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _Inout_ PDISPATCHER_CONTEXT DispatcherContext)
{
    PSCOPE_TABLE_ARM ScopeTable = (PSCOPE_TABLE_ARM)DispatcherContext->HandlerData;
    ULONG_PTR ImageBase = DispatcherContext->ImageBase;
    ULONG_PTR ControlPc = DispatcherContext->ControlPc;
    ULONG_PTR TargetPc = DispatcherContext->TargetPc;
    EXCEPTION_POINTERS Pointers;
    ULONG Index;
    LONG Filter;

    UNREFERENCED_PARAMETER(ContextRecord);

    if (DispatcherContext->ControlPcIsUnwound)
        ControlPc -= 2;

    for (Index = DispatcherContext->ScopeIndex; Index < ScopeTable->Count; Index++)
    {
        if ((ControlPc < ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress) ||
            (ControlPc >= ImageBase + ScopeTable->ScopeRecord[Index].EndAddress))
        {
            continue;
        }

        if (ExceptionRecord->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
        {
            if (ScopeTable->ScopeRecord[Index].JumpTarget)
                continue;
            if ((ExceptionRecord->ExceptionFlags & EXCEPTION_TARGET_UNWIND) &&
                (TargetPc >= ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress) &&
                (TargetPc < ImageBase + ScopeTable->ScopeRecord[Index].EndAddress))
            {
                break;
            }
            DispatcherContext->ScopeIndex = Index + 1;
            __C_ExecuteHandler((PVOID)TRUE, EstablisherFrame,
                               (PVOID)(ImageBase + ScopeTable->ScopeRecord[Index].HandlerAddress),
                               (PULONG)DispatcherContext->NonVolatileRegisters);
            continue;
        }

        if (!ScopeTable->ScopeRecord[Index].JumpTarget)
            continue;

        if (ScopeTable->ScopeRecord[Index].HandlerAddress != EXCEPTION_EXECUTE_HANDLER)
        {
            Pointers.ExceptionRecord = ExceptionRecord;
            Pointers.ContextRecord = DispatcherContext->ContextRecord;
            Filter = __C_ExecuteHandler(&Pointers, EstablisherFrame,
                                        (PVOID)(ImageBase + ScopeTable->ScopeRecord[Index].HandlerAddress),
                                        (PULONG)DispatcherContext->NonVolatileRegisters);
            if (Filter < 0)
                return ExceptionContinueExecution;
            if (Filter == 0)
                continue;
        }

        RtlUnwindEx(EstablisherFrame,
                    (PVOID)(ImageBase + ScopeTable->ScopeRecord[Index].JumpTarget),
                    ExceptionRecord,
                    (PVOID)(ULONG_PTR)ExceptionRecord->ExceptionCode,
                    DispatcherContext->ContextRecord,
                    DispatcherContext->HistoryTable);
    }

    return ExceptionContinueSearch;
}

/* longjmp on ARM unwinds the frames between the caller and the setjmp frame. */
VOID
__cdecl
__jump_unwind(
    _In_ PVOID TargetFrame,
    _In_ PVOID TargetPc)
{
    RtlUnwind(TargetFrame, TargetPc, NULL, NULL);
}
