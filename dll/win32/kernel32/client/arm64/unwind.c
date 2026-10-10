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
#ifdef __arm64ec__
    PARM64EC_NT_CONTEXT Registers = (PARM64EC_NT_CONTEXT)Context;
#else
    PCONTEXT Registers = Context;
#endif
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 ImageBase;
    ULONG64 EstablisherFrame;
    ULONG64 PreviousSp = Registers->Sp;
    ULONG64 PreviousPc = Registers->Pc;
    ULONG64 StackHigh = (ULONG64)NtCurrentTeb()->NtTib.StackBase;
    PVOID HandlerData;

    FunctionEntry = RtlLookupFunctionEntry(Registers->Pc, &ImageBase, NULL);
    if (FunctionEntry)
    {
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, Registers->Pc, FunctionEntry, Context, &HandlerData, &EstablisherFrame, NULL);
    }
    else
    {
        Registers->Pc = Registers->Lr;
    }

    if (!Registers->Pc || Registers->Sp < PreviousSp || Registers->Sp > StackHigh) return FALSE;
    if (Registers->Pc == PreviousPc && Registers->Sp == PreviousSp) return FALSE;
    *ReturnAddress = Registers->Pc;
    return TRUE;
}
