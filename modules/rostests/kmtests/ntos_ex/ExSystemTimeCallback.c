/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A system time change calls the routines registered on \Callback\SetSystemTime
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static KEVENT CalledEvent;
static PVOID CalledArgument1;
static PVOID CalledArgument2;
static volatile LONG CallCount;

static
VOID
NTAPI
SystemTimeChanged(
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    UNREFERENCED_PARAMETER(Context);

    CalledArgument1 = Argument1;
    CalledArgument2 = Argument2;
    InterlockedIncrement(&CallCount);
    KeSetEvent(&CalledEvent, IO_NO_INCREMENT, FALSE);
}

START_TEST(ExSystemTimeCallback)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Callback\\SetSystemTime");
    OBJECT_ATTRIBUTES Attributes;
    PCALLBACK_OBJECT Callback;
    PVOID Registration;
    LARGE_INTEGER Now;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ExCreateCallback(&Callback, &Attributes, FALSE, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    KeInitializeEvent(&CalledEvent, NotificationEvent, FALSE);
    CalledArgument1 = &CalledEvent;
    CalledArgument2 = &CalledEvent;
    CallCount = 0;

    Registration = ExRegisterCallback(Callback, SystemTimeChanged, NULL);
    ok(Registration != NULL, "ExRegisterCallback failed\n");
    if (Registration != NULL)
    {
        KeQuerySystemTime(&Now);
        Status = ZwSetSystemTime(&Now, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Timeout.QuadPart = -200LL * 10000;
        Status = KeWaitForSingleObject(&CalledEvent, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(CallCount >= 1, "The callback ran %ld times\n", CallCount);
        ok_eq_pointer(CalledArgument1, NULL);
        ok_eq_pointer(CalledArgument2, NULL);

        ExUnregisterCallback(Registration);
    }

    ObDereferenceObject(Callback);
}
