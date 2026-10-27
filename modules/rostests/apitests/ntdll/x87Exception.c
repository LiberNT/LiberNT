/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the exception records of x87 floating-point errors
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>
#include <float.h>

#define X87_DEFAULT_CONTROL_WORD 0x027F
#define X87_ALL_MASKED_CONTROL_WORD 0x037F
#define X87_EXCEPTION_FLAGS 0x003F
#define X87_STATUS_CLEAR_MASK 0x80FF

typedef struct _X87_RESULT
{
    double Value;
    USHORT StatusWord;
} X87_RESULT, *PX87_RESULT;

typedef VOID (*PX87_TRIGGER)(const USHORT *ControlWord, const double *Operand, PX87_RESULT Result);

VOID X87Reset(VOID);

#define DECLARE_X87_TRIGGER(Name) \
    VOID Name(const USHORT *ControlWord, const double *Operand, PX87_RESULT Result); \
    extern UCHAR Name##Fault[]; \
    extern UCHAR Name##Wait[]

DECLARE_X87_TRIGGER(X87InvalidOperation);
DECLARE_X87_TRIGGER(X87StackFault);
DECLARE_X87_TRIGGER(X87Denormal);
DECLARE_X87_TRIGGER(X87ZeroDivide);
DECLARE_X87_TRIGGER(X87Overflow);
DECLARE_X87_TRIGGER(X87Underflow);
DECLARE_X87_TRIGGER(X87Precision);

typedef struct _X87_TEST
{
    PCSTR Name;
    PX87_TRIGGER Trigger;
    PUCHAR Fault;
    PUCHAR Wait;
    USHORT UnmaskBit;
    double Operand;
    NTSTATUS ExpectedStatus;
    USHORT ExpectedFlags;
} X87_TEST, *PX87_TEST;

static X87_TEST X87Tests[] =
{
    { "IE", X87InvalidOperation, X87InvalidOperationFault, X87InvalidOperationWait, 0x01, -1.0, STATUS_FLOAT_INVALID_OPERATION, 0x01 },
    { "SF", X87StackFault, X87StackFaultFault, X87StackFaultWait, 0x01, 0.0, STATUS_FLOAT_STACK_CHECK, 0x41 },
    { "DE", X87Denormal, X87DenormalFault, X87DenormalWait, 0x02, DBL_MIN / 4, STATUS_FLOAT_INVALID_OPERATION, 0x02 },
    { "ZE", X87ZeroDivide, X87ZeroDivideFault, X87ZeroDivideWait, 0x04, 0.0, STATUS_FLOAT_DIVIDE_BY_ZERO, 0x04 },
    { "OE", X87Overflow, X87OverflowFault, X87OverflowWait, 0x08, DBL_MAX, STATUS_FLOAT_OVERFLOW, 0x08 },
    { "UE", X87Underflow, X87UnderflowFault, X87UnderflowWait, 0x10, DBL_MIN, STATUS_FLOAT_UNDERFLOW, 0x10 },
    { "PE", X87Precision, X87PrecisionFault, X87PrecisionWait, 0x20, 3.0, STATUS_FLOAT_INEXACT_RESULT, 0x20 },
};

typedef struct _X87_CAPTURE
{
    ULONG Count;
    ULONG ClearAt;
    ULONG MaskAt;
    ULONG GiveUpAt;
    EXCEPTION_RECORD Record;
    ULONG64 Rip;
    XMM_SAVE_AREA32 FltSave;
    ULONG ContextFlags;
} X87_CAPTURE, *PX87_CAPTURE;

static X87_CAPTURE X87Capture;

static
LONG
X87Filter(
    _In_ PEXCEPTION_POINTERS ExceptionPointers)
{
    PCONTEXT Context = ExceptionPointers->ContextRecord;

    if (X87Capture.Count++ == 0)
    {
        X87Capture.Record = *ExceptionPointers->ExceptionRecord;
        X87Capture.Rip = Context->Rip;
        X87Capture.FltSave = Context->FltSave;
        X87Capture.ContextFlags = Context->ContextFlags;
    }

    if (X87Capture.Count >= X87Capture.GiveUpAt)
        return EXCEPTION_EXECUTE_HANDLER;
    if (X87Capture.Count >= X87Capture.ClearAt)
        Context->FltSave.StatusWord &= ~X87_STATUS_CLEAR_MASK;
    if (X87Capture.Count >= X87Capture.MaskAt)
        Context->FltSave.ControlWord |= X87_EXCEPTION_FLAGS;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static
BOOLEAN
X87Run(
    _In_ PX87_TEST Test,
    _In_ USHORT ControlWord,
    _In_ ULONG ClearAt,
    _In_ ULONG MaskAt,
    _Out_ PX87_RESULT Result)
{
    volatile BOOLEAN Returned = FALSE;

    RtlZeroMemory(&X87Capture, sizeof(X87Capture));
    X87Capture.ClearAt = ClearAt;
    X87Capture.MaskAt = MaskAt;
    X87Capture.GiveUpAt = 10;
    RtlZeroMemory(Result, sizeof(*Result));

    _SEH2_TRY
    {
        Test->Trigger(&ControlWord, &Test->Operand, Result);
        Returned = TRUE;
    }
    _SEH2_EXCEPT(X87Filter(_SEH2_GetExceptionInformation()))
    {
    }
    _SEH2_END;

    X87Reset();
    return Returned;
}

static
VOID
X87TestException(
    _In_ PX87_TEST Test)
{
    X87_RESULT Result;
    BOOLEAN Returned;
    ULONG i;

    Returned = X87Run(Test, X87_DEFAULT_CONTROL_WORD & ~Test->UnmaskBit, 1, 1, &Result);

    ok(X87Capture.Count == 1, "%s: Count = %lu\n", Test->Name, X87Capture.Count);
    ok(Returned, "%s: trigger did not return\n", Test->Name);
    if (X87Capture.Count == 0)
        return;

    trace("%s: Code 0x%08lx Flags 0x%lx Params %lu [0x%Ix 0x%Ix 0x%Ix] Address fault%+Id wait%+Id Rip-Address %Id\n",
          Test->Name,
          X87Capture.Record.ExceptionCode,
          X87Capture.Record.ExceptionFlags,
          X87Capture.Record.NumberParameters,
          X87Capture.Record.ExceptionInformation[0],
          X87Capture.Record.ExceptionInformation[1],
          X87Capture.Record.ExceptionInformation[2],
          (PUCHAR)X87Capture.Record.ExceptionAddress - Test->Fault,
          (PUCHAR)X87Capture.Record.ExceptionAddress - Test->Wait,
          (LONG_PTR)(X87Capture.Rip - (ULONG_PTR)X87Capture.Record.ExceptionAddress));
    trace("%s: FltSave CW 0x%04x SW 0x%04x TW 0x%02x Op 0x%04x ErrorOffset fault%+ld ContextFlags 0x%lx\n",
          Test->Name,
          X87Capture.FltSave.ControlWord,
          X87Capture.FltSave.StatusWord,
          X87Capture.FltSave.TagWord,
          X87Capture.FltSave.ErrorOpcode,
          (LONG)(X87Capture.FltSave.ErrorOffset - (ULONG)(ULONG_PTR)Test->Fault),
          X87Capture.ContextFlags);
    for (i = 3; i < X87Capture.Record.NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; i++)
    {
        trace("%s: ExceptionInformation[%lu] = 0x%Ix\n", Test->Name, i, X87Capture.Record.ExceptionInformation[i]);
    }

    ok(X87Capture.Record.ExceptionCode == Test->ExpectedStatus,
       "%s: ExceptionCode = 0x%08lx, expected 0x%08lx\n",
       Test->Name, X87Capture.Record.ExceptionCode, Test->ExpectedStatus);
    ok(X87Capture.Record.ExceptionFlags == 0, "%s: ExceptionFlags = 0x%lx\n", Test->Name, X87Capture.Record.ExceptionFlags);
    ok(X87Capture.Record.NumberParameters == 1, "%s: NumberParameters = %lu\n", Test->Name, X87Capture.Record.NumberParameters);
    ok(X87Capture.Record.ExceptionInformation[0] == 0, "%s: ExceptionInformation[0] = 0x%Ix\n",
       Test->Name, X87Capture.Record.ExceptionInformation[0]);
    ok(X87Capture.Record.ExceptionAddress == Test->Wait, "%s: ExceptionAddress = wait%+Id\n",
       Test->Name, (PUCHAR)X87Capture.Record.ExceptionAddress - Test->Wait);
    ok(X87Capture.Rip == (ULONG_PTR)Test->Wait, "%s: Rip = wait%+Id\n",
       Test->Name, (LONG_PTR)(X87Capture.Rip - (ULONG_PTR)Test->Wait));
    ok(X87Capture.FltSave.ErrorOffset == (ULONG)(ULONG_PTR)Test->Fault, "%s: ErrorOffset = fault%+ld\n",
       Test->Name, (LONG)(X87Capture.FltSave.ErrorOffset - (ULONG)(ULONG_PTR)Test->Fault));
    ok((X87Capture.FltSave.StatusWord & Test->ExpectedFlags) == Test->ExpectedFlags,
       "%s: StatusWord = 0x%04x\n", Test->Name, X87Capture.FltSave.StatusWord);
    ok(X87Capture.FltSave.ControlWord == (X87_DEFAULT_CONTROL_WORD & ~Test->UnmaskBit),
       "%s: ControlWord = 0x%04x\n", Test->Name, X87Capture.FltSave.ControlWord);
}

static
VOID
X87TestContinue(
    _In_ PX87_TEST Test)
{
    X87_RESULT Result;
    BOOLEAN Returned;

    Returned = X87Run(Test, X87_DEFAULT_CONTROL_WORD & ~Test->UnmaskBit, 3, 6, &Result);

    trace("%s: continue without clearing: Count %lu Returned %u StatusWord 0x%04x\n",
          Test->Name, X87Capture.Count, Returned, Result.StatusWord);
    ok(Returned, "%s: trigger did not return\n", Test->Name);
    ok(X87Capture.Count == 3, "%s: Count = %lu\n", Test->Name, X87Capture.Count);
}

static
VOID
X87TestMasked(
    _In_ PX87_TEST Test)
{
    X87_RESULT Result;
    BOOLEAN Returned;

    Returned = X87Run(Test, X87_ALL_MASKED_CONTROL_WORD, 1, 1, &Result);

    ok(Returned, "%s: masked trigger did not return\n", Test->Name);
    ok(X87Capture.Count == 0, "%s: masked Count = %lu, code 0x%08lx\n",
       Test->Name, X87Capture.Count, X87Capture.Record.ExceptionCode);
    ok((Result.StatusWord & Test->ExpectedFlags) == Test->ExpectedFlags,
       "%s: masked StatusWord = 0x%04x\n", Test->Name, Result.StatusWord);
}

START_TEST(x87Exception)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(X87Tests); i++)
    {
        X87TestMasked(&X87Tests[i]);
        X87TestException(&X87Tests[i]);
        X87TestContinue(&X87Tests[i]);
    }
}
