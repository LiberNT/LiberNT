/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Implementation of longjmp for RISC-V 64
 */

#include <setjmp.h>
#include <windef.h>

__declspec(noreturn)
void __longjmp_noframe(const _JUMP_BUFFER* Buffer, int Value);

static
ULONG64
FindSetjmpFrame(
    _In_ const _JUMP_BUFFER *Buffer)
{
    CONTEXT Context;
    PRUNTIME_FUNCTION Target, Function;
    ULONG64 TargetImage, ImageBase, ControlPc, EstablisherFrame;
    PVOID HandlerData;
    BOOLEAN Unwound;
    ULONG Frames;

    Target = RtlLookupFunctionEntry(Buffer->Ra, &TargetImage, NULL);
    if (Target == NULL)
        return 0;

    RtlCaptureContext(&Context);
    for (Frames = 0; Frames < 256 && Context.Pc != 0 && Context.Sp <= Buffer->Sp; Frames++)
    {
        Unwound = (Context.ContextFlags & CONTEXT_UNWOUND_TO_CALL) != 0;
        ControlPc = Unwound ? Context.Pc - 2 : Context.Pc;
        Function = RtlLookupFunctionEntry(ControlPc, &ImageBase, NULL);
        if (Function == NULL)
            return 0;

        if (Context.Sp == Buffer->Sp && ImageBase == TargetImage &&
            Function->BeginAddress == Target->BeginAddress)
        {
            RtlVirtualUnwind(UNW_FLAG_UHANDLER, ImageBase, ControlPc, Function, &Context, &HandlerData,
                             &EstablisherFrame, NULL);
            return EstablisherFrame;
        }

        RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, ControlPc, Function, &Context, &HandlerData,
                         &EstablisherFrame, NULL);
    }

    return 0;
}

__declspec(noreturn)
void __cdecl
longjmp(
    _In_reads_(_JBLEN) jmp_buf Buffer,
    _In_ int Value)
{
    const _JUMP_BUFFER *JumpBuffer = (const _JUMP_BUFFER*)Buffer;
    ULONG64 Frame;

    Value = Value ? Value : 1;

    Frame = FindSetjmpFrame(JumpBuffer);
    if (Frame != 0)
    {
        EXCEPTION_RECORD ExceptionRecord = { 0 };
        ExceptionRecord.ExceptionCode = STATUS_LONGJUMP;
        ExceptionRecord.NumberParameters = 1;
        ExceptionRecord.ExceptionInformation[0] = (ULONG_PTR)JumpBuffer;

        RtlUnwind((PVOID)(ULONG_PTR)Frame, (PVOID)(ULONG_PTR)JumpBuffer->Ra, &ExceptionRecord,
                  (PVOID)(ULONG_PTR)Value);
    }

    __longjmp_noframe(JumpBuffer, Value);
    __builtin_unreachable();
}
