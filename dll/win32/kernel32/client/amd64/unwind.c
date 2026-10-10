/*
 * PROJECT:     LiberNT System Libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64 table-based step of the unhandled exception trace
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
    ULONG64 PreviousRsp = Context->Rsp;
    ULONG64 StackLow = (ULONG64)NtCurrentTeb()->NtTib.StackLimit;
    ULONG64 StackHigh = (ULONG64)NtCurrentTeb()->NtTib.StackBase;
    PVOID HandlerData;

    FunctionEntry = RtlLookupFunctionEntry(Context->Rip, &ImageBase, NULL);
    if (FunctionEntry)
    {
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, Context->Rip, FunctionEntry, Context, &HandlerData, &EstablisherFrame, NULL);
    }
    else
    {
        if (Context->Rsp < StackLow || Context->Rsp > StackHigh - sizeof(ULONG64)) return FALSE;
        Context->Rip = *(PULONG64)Context->Rsp;
        Context->Rsp += sizeof(ULONG64);
    }

    if (!Context->Rip || Context->Rsp <= PreviousRsp || Context->Rsp > StackHigh) return FALSE;
    *ReturnAddress = Context->Rip;
    return TRUE;
}
