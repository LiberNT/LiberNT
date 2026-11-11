/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NtRaiseException hands the handlers the context the caller passed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TEST_EXCEPTION_CODE 0xE0004E52

#ifdef _M_AMD64

static CONTEXT RaiseContext;
static CONTEXT SeenContext;
static volatile LONG Raised;
static volatile LONG Seen;

static
LONG
CALLBACK
RaiseHandler(
    _In_ PEXCEPTION_POINTERS ExceptionInfo)
{
    if (ExceptionInfo->ExceptionRecord->ExceptionCode != TEST_EXCEPTION_CODE)
        return EXCEPTION_CONTINUE_SEARCH;

    SeenContext = *ExceptionInfo->ContextRecord;
    Seen = 1;

    /* Carry on where the context was captured, with the registers it held */
    *ExceptionInfo->ContextRecord = RaiseContext;
    return EXCEPTION_CONTINUE_EXECUTION;
}

START_TEST(NtRaiseException)
{
    EXCEPTION_RECORD Record;
    CONTEXT Context;
    PVOID Handler;
    NTSTATUS Status;

    Raised = 0;
    Seen = 0;
    Handler = RtlAddVectoredExceptionHandler(TRUE, RaiseHandler);
    ok(Handler != NULL, "RtlAddVectoredExceptionHandler failed\n");
    if (Handler == NULL)
        return;

    RtlCaptureContext(&RaiseContext);
    if (!Raised)
    {
        Raised = 1;

        Context = RaiseContext;
        Context.Rbx = 0x1111111111111111ULL;
        Context.Rsi = 0x2222222222222222ULL;
        Context.Rdi = 0x3333333333333333ULL;
        Context.R12 = 0x4444444444444444ULL;
        Context.R13 = 0x5555555555555555ULL;
        Context.R14 = 0x6666666666666666ULL;
        Context.R15 = 0x7777777777777777ULL;
        Context.Xmm6.Low = 0x8888888888888888ULL;
        Context.Xmm15.High = 0x9999999999999999ULL;

        RtlZeroMemory(&Record, sizeof(Record));
        Record.ExceptionCode = TEST_EXCEPTION_CODE;
        Record.ExceptionAddress = (PVOID)Context.Rip;

        Status = NtRaiseException(&Record, &Context, TRUE);
        ok(FALSE, "NtRaiseException returned 0x%lx\n", Status);
    }

    RtlRemoveVectoredExceptionHandler(Handler);

    ok(Seen == 1, "The handler did not run\n");
    if (Seen != 1)
        return;

    ok(SeenContext.Rbx == 0x1111111111111111ULL, "Rbx %I64x\n", SeenContext.Rbx);
    ok(SeenContext.Rsi == 0x2222222222222222ULL, "Rsi %I64x\n", SeenContext.Rsi);
    ok(SeenContext.Rdi == 0x3333333333333333ULL, "Rdi %I64x\n", SeenContext.Rdi);
    ok(SeenContext.R12 == 0x4444444444444444ULL, "R12 %I64x\n", SeenContext.R12);
    ok(SeenContext.R13 == 0x5555555555555555ULL, "R13 %I64x\n", SeenContext.R13);
    ok(SeenContext.R14 == 0x6666666666666666ULL, "R14 %I64x\n", SeenContext.R14);
    ok(SeenContext.R15 == 0x7777777777777777ULL, "R15 %I64x\n", SeenContext.R15);
    ok(SeenContext.Xmm6.Low == 0x8888888888888888ULL, "Xmm6 %I64x\n", SeenContext.Xmm6.Low);
    ok(SeenContext.Xmm15.High == 0x9999999999999999ULL, "Xmm15 %I64x\n", SeenContext.Xmm15.High);
    ok(SeenContext.Rip == RaiseContext.Rip, "Rip %I64x, expected %I64x\n", SeenContext.Rip, RaiseContext.Rip);
}

#else

START_TEST(NtRaiseException)
{
    skip("The nonvolatile register checks are written for amd64\n");
}

#endif
