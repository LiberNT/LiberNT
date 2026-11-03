/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that exception handlers see the locals of their function
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <pseh/pseh2.h>

typedef struct _SEH_LOCALS
{
    ULONG_PTR First;
    ULONG_PTR Second;
    ULONG_PTR Third;
    ULONG HandlerRuns;
    ULONG Cleanups;
} SEH_LOCALS, *PSEH_LOCALS;

static
DECLSPEC_NOINLINE
VOID
FaultAt(
    _In_ volatile ULONG *Address)
{
    *Address = 0;
}

static
DECLSPEC_NOINLINE
NTSTATUS
ReturnFromHandler(
    _Inout_ PSEH_LOCALS Locals,
    _In_ volatile ULONG *Address)
{
    Locals->First = 1;
    Locals->Second = Locals->First + 1;
    Locals->Third = Locals->Second + 1;

    _SEH2_TRY
    {
        FaultAt(Address);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Locals->HandlerRuns++;
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Locals->Third = 0;
    return STATUS_SUCCESS;
}

static
DECLSPEC_NOINLINE
NTSTATUS
LeaveHandlerThroughGoto(
    _Inout_ PSEH_LOCALS Locals,
    _In_ volatile ULONG *Address)
{
    PSEH_LOCALS Current = Locals;
    PULONG Cleanups = &Locals->Cleanups;
    NTSTATUS Status = STATUS_SUCCESS;

    Current->First = 4;
    Current->Second = Current->First + 1;

    _SEH2_TRY
    {
        FaultAt(Address);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        goto Exit;
    }
    _SEH2_END;

    Current->Third = 0;

Exit:
    Current->Third = Current->Second + 1;
    Current->HandlerRuns++;
    (*Cleanups)++;
    return Status;
}

START_TEST(SehHandlerLocals)
{
    SEH_LOCALS Locals;
    NTSTATUS Status;

    RtlZeroMemory(&Locals, sizeof(Locals));
    Status = ReturnFromHandler(&Locals, NULL);
    ok_hex(Status, STATUS_ACCESS_VIOLATION);
    ok_size_t(Locals.First, 1);
    ok_size_t(Locals.Second, 2);
    ok_size_t(Locals.Third, 3);
    ok_int(Locals.HandlerRuns, 1);

    RtlZeroMemory(&Locals, sizeof(Locals));
    Status = LeaveHandlerThroughGoto(&Locals, NULL);
    ok_hex(Status, STATUS_ACCESS_VIOLATION);
    ok_size_t(Locals.First, 4);
    ok_size_t(Locals.Second, 5);
    ok_size_t(Locals.Third, 6);
    ok_int(Locals.HandlerRuns, 1);
    ok_int(Locals.Cleanups, 1);
}
