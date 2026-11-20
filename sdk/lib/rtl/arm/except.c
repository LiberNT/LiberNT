/*
 * PROJECT:     LiberNT Run-Time Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM (Thumb-2) exception dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl.h>
#define NDEBUG
#include <debug.h>

/* RtlpExecuteHandlerFor* push {r3, r4, r11, lr}: r3 (the dispatcher context) lands 16 bytes below the frame. */
#define ARM_HANDLER_FRAME_DISPATCHER_OFFSET 16

EXCEPTION_DISPOSITION
NTAPI
RtlpExecuteHandlerForException(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _Inout_ PDISPATCHER_CONTEXT DispatcherContext,
    _In_ PEXCEPTION_ROUTINE ExceptionRoutine);

PRTLP_DISPATCH_EXCEPTION_HOOK RtlpDispatchExceptionHook;

/* Non-volatile state handed to language handlers: r4-r11 then d8-d15. */
#define RTLP_ARM_NONVOLATILE_WORDS (8 + 8 * 2)
VOID
NTAPI
RtlpArmCaptureNonVolatileRegisters(
    _Out_writes_(RTLP_ARM_NONVOLATILE_WORDS) PULONG NonVolatileRegisters,
    _In_ PCONTEXT Context)
{
    RtlCopyMemory(NonVolatileRegisters, &Context->R4, 8 * sizeof(ULONG));
    RtlCopyMemory(&NonVolatileRegisters[8], &Context->D[8], 8 * sizeof(ULONGLONG));
}

EXCEPTION_DISPOSITION
NTAPI
RtlpExceptionHandler(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _Inout_ PVOID DispatcherContext)
{
    PDISPATCHER_CONTEXT Nested = (PDISPATCHER_CONTEXT)DispatcherContext;
    PDISPATCHER_CONTEXT Active;

    UNREFERENCED_PARAMETER(ContextRecord);

    if (ExceptionRecord->ExceptionFlags & EXCEPTION_UNWIND)
        return ExceptionContinueSearch;

    Active = *(PDISPATCHER_CONTEXT *)((PUCHAR)EstablisherFrame - ARM_HANDLER_FRAME_DISPATCHER_OFFSET);
    Nested->EstablisherFrame = Active->EstablisherFrame;
    return ExceptionNestedException;
}

EXCEPTION_DISPOSITION
NTAPI
RtlpUnwindHandler(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _Inout_ PVOID DispatcherContext)
{
    PDISPATCHER_CONTEXT Collided = (PDISPATCHER_CONTEXT)DispatcherContext;
    PDISPATCHER_CONTEXT Active;
    ULONG TargetPc;

    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ContextRecord);

    Active = *(PDISPATCHER_CONTEXT *)((PUCHAR)EstablisherFrame - ARM_HANDLER_FRAME_DISPATCHER_OFFSET);
    TargetPc = Collided->TargetPc;
    *Collided = *Active;
    Collided->TargetPc = TargetPc;
    return ExceptionCollidedUnwind;
}

VOID
NTAPI
RtlpArmRestoreCollidedFrame(
    _Inout_ PDISPATCHER_CONTEXT DispatcherContext,
    _Out_ PCONTEXT UnwindContext,
    _Out_writes_bytes_(NonVolatileRegistersLength) PVOID NonVolatileRegisters,
    _In_ ULONG NonVolatileRegistersLength,
    _Out_ PULONG EstablisherFrame)
{
    ULONG LookupPc, UnwoundFrame;
    PVOID HandlerData;

    *UnwindContext = *DispatcherContext->ContextRecord;
    RtlCopyMemory(NonVolatileRegisters, DispatcherContext->NonVolatileRegisters, NonVolatileRegistersLength);
    LookupPc = DispatcherContext->ControlPc;
    if (DispatcherContext->ControlPcIsUnwound)
        LookupPc -= 2;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, DispatcherContext->ImageBase, LookupPc, DispatcherContext->FunctionEntry,
                     UnwindContext, &HandlerData, &UnwoundFrame, NULL);
    DispatcherContext->NonVolatileRegisters = (PBYTE)NonVolatileRegisters;
    *EstablisherFrame = DispatcherContext->EstablisherFrame;
}

/* Advance a captured context to the caller of the routine that captured it. */
VOID
NTAPI
RtlpStepContextToCaller(
    _Inout_ PCONTEXT Context)
{
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG ImageBase, EstablisherFrame;
    PVOID HandlerData;

    FunctionEntry = RtlLookupFunctionEntry(Context->Pc, &ImageBase, NULL);
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, Context->Pc, FunctionEntry, Context,
                     &HandlerData, &EstablisherFrame, NULL);
}

VOID
NTAPI
RtlGetCallersAddress(
    _Out_ PVOID *CallersAddress,
    _Out_ PVOID *CallersCaller)
{
    PVOID Callers[4];
    ULONG Number;

    Number = RtlWalkFrameChain(Callers, 4, 0);
    *CallersAddress = (Number >= 3) ? Callers[2] : NULL;
    *CallersCaller = (Number == 4) ? Callers[3] : NULL;
}

BOOLEAN
NTAPI
RtlDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT ContextRecord)
{
    DISPATCHER_CONTEXT DispatcherContext;
    ULONG NonVolatileRegisters[RTLP_ARM_NONVOLATILE_WORDS];
    PEXCEPTION_ROUTINE ExceptionRoutine;
    EXCEPTION_DISPOSITION Disposition;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG ImageBase, EstablisherFrame, NestedFrame, PreviousSp, ControlPc, LookupPc;
    ULONG_PTR StackLow, StackHigh;
    CONTEXT UnwindContext;
    PVOID HandlerData;
    BOOLEAN ControlPcIsUnwound, Handled;

    if (RtlpDispatchExceptionHook && RtlpDispatchExceptionHook(ExceptionRecord, ContextRecord, &Handled))
        return Handled;

    if (RtlCallVectoredExceptionHandlers(ExceptionRecord, ContextRecord))
    {
        RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
        return TRUE;
    }

    UnwindContext = *ContextRecord;
    if (ExceptionRecord->ExceptionFlags & EXCEPTION_SOFTWARE_ORIGINATE)
        UnwindContext.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;

    NestedFrame = 0;
    RtlpGetStackLimits(&StackLow, &StackHigh);

    for (;;)
    {
        ImageBase = 0;
        ControlPc = UnwindContext.Pc & ~1UL;
        ControlPcIsUnwound = !!(UnwindContext.ContextFlags & CONTEXT_UNWOUND_TO_CALL);
        LookupPc = ControlPcIsUnwound ? (ControlPc - 2) : ControlPc;
        FunctionEntry = RtlLookupFunctionEntry(LookupPc, &ImageBase, NULL);
        if (!FunctionEntry)
        {
            if (!UnwindContext.Lr || ((UnwindContext.Lr & ~1UL) == ControlPc))
                break;
            UnwindContext.Pc = UnwindContext.Lr;
            UnwindContext.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
            continue;
        }

        EstablisherFrame = 0;
        PreviousSp = UnwindContext.Sp;
        RtlpArmCaptureNonVolatileRegisters(NonVolatileRegisters, &UnwindContext);
        ExceptionRoutine = RtlVirtualUnwind(UNW_FLAG_EHANDLER, ImageBase, LookupPc, FunctionEntry,
                                            &UnwindContext, &HandlerData, &EstablisherFrame, NULL);

        if ((EstablisherFrame < StackLow) || (EstablisherFrame > StackHigh) || (EstablisherFrame & 3) ||
            ((UnwindContext.Sp == PreviousSp) && ((UnwindContext.Pc & ~1UL) == ControlPc)))
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            break;
        }

        if (ExceptionRoutine)
        {
            RtlZeroMemory(&DispatcherContext, sizeof(DispatcherContext));
            DispatcherContext.ControlPc = ControlPc;
            DispatcherContext.ImageBase = ImageBase;
            DispatcherContext.FunctionEntry = FunctionEntry;
            DispatcherContext.EstablisherFrame = EstablisherFrame;
            DispatcherContext.ContextRecord = &UnwindContext;
            DispatcherContext.LanguageHandler = ExceptionRoutine;
            DispatcherContext.HandlerData = HandlerData;
            DispatcherContext.ScopeIndex = 0;
            DispatcherContext.ControlPcIsUnwound = ControlPcIsUnwound;
            DispatcherContext.NonVolatileRegisters = (PBYTE)NonVolatileRegisters;

            do
            {
                Disposition = RtlpExecuteHandlerForException(ExceptionRecord, (PVOID)(ULONG_PTR)EstablisherFrame,
                                                             ContextRecord, &DispatcherContext,
                                                             DispatcherContext.LanguageHandler);
                if (EstablisherFrame == NestedFrame)
                {
                    ExceptionRecord->ExceptionFlags &= ~EXCEPTION_NESTED_CALL;
                    NestedFrame = 0;
                }

                if (Disposition == ExceptionContinueExecution)
                {
                    if (ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
                        RtlRaiseStatus(STATUS_NONCONTINUABLE_EXCEPTION);
                    RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
                    return TRUE;
                }

                if (Disposition == ExceptionNestedException)
                {
                    ExceptionRecord->ExceptionFlags |= EXCEPTION_NESTED_CALL;
                    if (DispatcherContext.EstablisherFrame > NestedFrame)
                        NestedFrame = DispatcherContext.EstablisherFrame;
                }
                else if (Disposition == ExceptionCollidedUnwind)
                {
                    RtlpArmRestoreCollidedFrame(&DispatcherContext, &UnwindContext, NonVolatileRegisters,
                                                sizeof(NonVolatileRegisters), &EstablisherFrame);
                    DispatcherContext.ContextRecord = &UnwindContext;
                }
            }
            while (Disposition == ExceptionCollidedUnwind);

            if ((Disposition != ExceptionContinueSearch) && (Disposition != ExceptionNestedException))
                break;
        }

        if (!UnwindContext.Pc)
            break;
    }

    RtlCallVectoredContinueHandlers(ExceptionRecord, ContextRecord);
    return FALSE;
}

VOID
NTAPI
RtlInitializeContext(
    _In_ HANDLE ProcessHandle,
    _Out_ PCONTEXT ThreadContext,
    _In_opt_ PVOID ThreadStartParam,
    _In_ PTHREAD_START_ROUTINE ThreadStartAddress,
    _In_ PINITIAL_TEB InitialStack)
{
    UNREFERENCED_PARAMETER(ProcessHandle);

    RtlZeroMemory(ThreadContext, sizeof(*ThreadContext));
    ThreadContext->ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    ThreadContext->Pc = (ULONG)(ULONG_PTR)ThreadStartAddress & ~1UL;
    ThreadContext->Cpsr = 0x10 | 0x20;
    ThreadContext->Sp = (ULONG)ALIGN_DOWN_BY((ULONG_PTR)InitialStack, 8);
    ThreadContext->R0 = (ULONG)(ULONG_PTR)ThreadStartParam;
    ThreadContext->Lr = (ULONG)(ULONG_PTR)RtlExitUserThread;
}
