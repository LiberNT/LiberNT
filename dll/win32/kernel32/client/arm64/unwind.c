/*
 * PROJECT:     LiberNT System Libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 table-based step of the unhandled exception trace
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>

BOOL
BasepArchUnwindFrame(
    _Inout_ PCONTEXT Context,
    _Out_ PULONG_PTR ReturnAddress)
{
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 ImageBase;
    ULONG64 EstablisherFrame;
    ULONG64 PreviousSp = Context->Sp;
    ULONG64 PreviousPc = Context->Pc;
    ULONG64 StackHigh = (ULONG64)NtCurrentTeb()->NtTib.StackBase;
    PVOID HandlerData;

    FunctionEntry = RtlLookupFunctionEntry(Context->Pc, &ImageBase, NULL);
    if (FunctionEntry)
    {
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, Context->Pc, FunctionEntry, Context, &HandlerData, &EstablisherFrame, NULL);
    }
    else
    {
        Context->Pc = Context->Lr;
    }

    if (!Context->Pc || Context->Sp < PreviousSp || Context->Sp > StackHigh) return FALSE;
    if (Context->Pc == PreviousPc && Context->Sp == PreviousSp) return FALSE;
    *ReturnAddress = Context->Pc;
    return TRUE;
}
