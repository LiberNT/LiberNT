/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the debug registers that the context of a kernel-mode trap reports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static volatile ULONG DivideResult;
static ULONG TrapContextFlags;
static ULONG64 TrapDebugRegisters[6];

static
DECLSPEC_NOINLINE
VOID
FillStackBelow(VOID)
{
    volatile UCHAR Buffer[2048];
    ULONG Index;

    for (Index = 0; Index < sizeof(Buffer); Index++)
        Buffer[Index] = 0xA5;
}

static
LONG
SaveDebugRegisters(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    PCONTEXT Context = Pointers->ContextRecord;

    TrapContextFlags = Context->ContextFlags;
    TrapDebugRegisters[0] = Context->Dr0;
    TrapDebugRegisters[1] = Context->Dr1;
    TrapDebugRegisters[2] = Context->Dr2;
    TrapDebugRegisters[3] = Context->Dr3;
    TrapDebugRegisters[4] = Context->Dr6;
    TrapDebugRegisters[5] = Context->Dr7;

    return EXCEPTION_EXECUTE_HANDLER;
}

START_TEST(KeTrapDebugRegisters)
{
    volatile ULONG Dividend = 1;
    volatile ULONG Zero = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    /* The trap frame is built where this left its pattern */
    FillStackBelow();

    _SEH2_TRY
    {
        DivideResult = Dividend / Zero;
    }
    _SEH2_EXCEPT(SaveDebugRegisters(_SEH2_GetExceptionInformation()))
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ok_eq_hex(Status, STATUS_INTEGER_DIVIDE_BY_ZERO);

    if ((TrapContextFlags & CONTEXT_DEBUG_REGISTERS) != CONTEXT_DEBUG_REGISTERS)
        return;

    for (Index = 0; Index < RTL_NUMBER_OF(TrapDebugRegisters); Index++)
    {
        ok(TrapDebugRegisters[Index] == 0,
           "Debug register slot %lu is 0x%I64x\n", Index, TrapDebugRegisters[Index]);
    }
}
