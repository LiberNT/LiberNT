/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power requests a driver creates, and sets and clears at DISPATCH_LEVEL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
EXECUTION_STATE
QueryState(VOID)
{
    EXECUTION_STATE State = 0;
    NTSTATUS Status;

    Status = ZwPowerInformation(SystemExecutionState, NULL, 0, &State, sizeof(State));
    ok_eq_hex(Status, STATUS_SUCCESS);
    return State;
}

static
NTSTATUS
ChangeAtDispatch(
    _In_ PVOID Request,
    _In_ POWER_REQUEST_TYPE Type,
    _In_ BOOLEAN Set)
{
    NTSTATUS Status;
    KIRQL OldIrql;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Status = Set ? PoSetPowerRequest(Request, Type) : PoClearPowerRequest(Request, Type);
    KeLowerIrql(OldIrql);

    return Status;
}

static
VOID
CheckRequests(VOID)
{
    COUNTED_REASON_CONTEXT Context;
    PVOID Request = (PVOID)(ULONG_PTR)1;
    BOOLEAN Held;
    NTSTATUS Status;

    Status = PoCreatePowerRequest(&Request, NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_pointer(Request, NULL);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Version = POWER_REQUEST_CONTEXT_VERSION;
    Context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    RtlInitUnicodeString(&Context.SimpleString, L"LiberNT kernel power request test");

    Status = PoCreatePowerRequest(&Request, KmtDriverObject->DeviceObject, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Held = (QueryState() & ES_SYSTEM_REQUIRED) != 0;
    if (Held) trace("Another component keeps the system required; its state is not checked\n");

    ok_eq_hex(ChangeAtDispatch(Request, PowerRequestSystemRequired, TRUE), STATUS_SUCCESS);
    ok_eq_hex(ChangeAtDispatch(Request, PowerRequestSystemRequired, TRUE), STATUS_SUCCESS);
    ok_eq_hex(ChangeAtDispatch(Request, PowerRequestDisplayRequired, TRUE), STATUS_NOT_SUPPORTED);
    ok_eq_hex(ChangeAtDispatch(Request, PowerRequestDisplayRequired, FALSE), STATUS_NOT_SUPPORTED);
    if (!Held) ok((QueryState() & ES_SYSTEM_REQUIRED) != 0, "a set request does not keep the system required\n");

    ok_eq_hex(ChangeAtDispatch(Request, PowerRequestSystemRequired, FALSE), STATUS_SUCCESS);
    if (!Held) ok((QueryState() & ES_SYSTEM_REQUIRED) != 0, "the request was let go while still set once\n");

    ok_eq_hex(ChangeAtDispatch(Request, PowerRequestSystemRequired, FALSE), STATUS_SUCCESS);
    ok_eq_hex(ChangeAtDispatch(Request, PowerRequestSystemRequired, FALSE), STATUS_NOT_SUPPORTED);
    if (!Held) ok((QueryState() & ES_SYSTEM_REQUIRED) == 0, "a cleared request still keeps the system required\n");

    ok_eq_hex(ChangeAtDispatch(Request, PowerRequestSystemRequired, TRUE), STATUS_SUCCESS);
    PoDeletePowerRequest(Request);
    if (!Held) ok((QueryState() & ES_SYSTEM_REQUIRED) == 0, "a deleted request still keeps the system required\n");
}

START_TEST(PoKernelPowerRequest)
{
    _SEH2_TRY
    {
        CheckRequests();
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ok(FALSE, "the power request routines raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
}
