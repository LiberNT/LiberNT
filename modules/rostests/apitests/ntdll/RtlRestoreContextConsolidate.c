/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unwind from a consolidation callback to the target context
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static CONTEXT TargetContext;
static ULONG64 ExpectedRip;
static ULONG64 ExpectedRsp;
static ULONG64 SeenRip;
static ULONG64 SeenRsp;
static volatile LONG CallbackCount;

static ULONG64
UnwindOneFrame(
    _Inout_ PCONTEXT Context)
{
    PRUNTIME_FUNCTION Entry;
    ULONG64 ImageBase;
    ULONG64 EstablisherFrame;
    PVOID HandlerData;

    Entry = RtlLookupFunctionEntry(Context->Rip, &ImageBase, NULL);
    if (Entry == NULL)
    {
        Context->Rip = *(PULONG64)Context->Rsp;
        Context->Rsp += sizeof(ULONG64);
        return Context->Rip;
    }
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, Context->Rip, Entry, Context,
                     &HandlerData, &EstablisherFrame, NULL);
    return Context->Rip;
}

static PVOID CALLBACK
ConsolidateCallback(
    _In_ PEXCEPTION_RECORD Record)
{
    CONTEXT Context;

    InterlockedIncrement(&CallbackCount);
    RtlCaptureContext(&Context);
    UnwindOneFrame(&Context);
    UnwindOneFrame(&Context);
    SeenRip = Context.Rip;
    SeenRsp = Context.Rsp;
    return (PVOID)Record->ExceptionInformation[2];
}

START_TEST(RtlRestoreContextConsolidate)
{
    EXCEPTION_RECORD Record;
    volatile LONG Pass = 0;

    RtlCaptureContext(&TargetContext);
    if (InterlockedIncrement(&Pass) == 1)
    {
        ExpectedRip = TargetContext.Rip;
        ExpectedRsp = TargetContext.Rsp;
        RtlZeroMemory(&Record, sizeof(Record));
        Record.ExceptionCode = STATUS_UNWIND_CONSOLIDATE;
        Record.NumberParameters = 3;
        Record.ExceptionInformation[0] = (ULONG_PTR)ConsolidateCallback;
        Record.ExceptionInformation[1] = (ULONG_PTR)&TargetContext;
        Record.ExceptionInformation[2] = TargetContext.Rip;
        RtlRestoreContext(&TargetContext, &Record);
        ok(FALSE, "RtlRestoreContext returned\n");
        return;
    }

    ok_long(CallbackCount, 1);
    ok(SeenRip == ExpectedRip, "Unwound to Rip %p, expected the target %p\n", (PVOID)SeenRip, (PVOID)ExpectedRip);
    ok(SeenRsp == ExpectedRsp, "Unwound to Rsp %p, expected the target %p\n", (PVOID)SeenRsp, (PVOID)ExpectedRsp);
}
