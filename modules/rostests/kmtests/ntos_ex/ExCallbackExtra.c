/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel-Mode Test Suite extended ExCallback coverage
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static volatile LONG CallbackHits;
static PVOID ObservedArg1;
static PVOID ObservedArg2;

static
VOID
NTAPI
CallbackFunction(
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    ok_eq_pointer(CallbackContext, (PVOID)(ULONG_PTR)0xC0DE);
    ObservedArg1 = Argument1;
    ObservedArg2 = Argument2;
    InterlockedIncrement(&CallbackHits);
}

static
VOID
TestNamedCallbackSharing(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING SharedName = RTL_CONSTANT_STRING(L"\\Callback\\KmtExCallbackShared");
    UNICODE_STRING TemporaryName = RTL_CONSTANT_STRING(L"\\Callback\\KmtExCallbackTemporary");
    PCALLBACK_OBJECT First = NULL, Second = NULL;
    PVOID Registration;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &SharedName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE | OBJ_PERMANENT,
                               NULL, NULL);
    Status = ExCreateCallback(&First, &ObjectAttributes, TRUE, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = ExCreateCallback(&Second, &ObjectAttributes, FALSE, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(Second, First);
        Registration = ExRegisterCallback(First, CallbackFunction, (PVOID)(ULONG_PTR)0xC0DE);
        ok(Registration != NULL, "register on the first reference failed\n");
        CallbackHits = 0;
        ExNotifyCallback(Second, (PVOID)(ULONG_PTR)0x3333, (PVOID)(ULONG_PTR)0x4444);
        ok_eq_long(CallbackHits, 1L);
        ok_eq_pointer(ObservedArg1, (PVOID)(ULONG_PTR)0x3333);
        ok_eq_pointer(ObservedArg2, (PVOID)(ULONG_PTR)0x4444);
        if (Registration) ExUnregisterCallback(Registration);
        ObDereferenceObject(Second);
    }
    ObMakeTemporaryObject(First);
    ObDereferenceObject(First);

    First = NULL;
    Second = NULL;
    InitializeObjectAttributes(&ObjectAttributes, &TemporaryName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ExCreateCallback(&First, &ObjectAttributes, TRUE, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = ExCreateCallback(&Second, &ObjectAttributes, FALSE, TRUE);
    ok(!NT_SUCCESS(Status), "a temporary named callback object was opened by name after its handle closed: %lx\n",
       Status);
    if (NT_SUCCESS(Status))
        ObDereferenceObject(Second);
    ObDereferenceObject(First);
}

START_TEST(ExCallbackExtra)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    PCALLBACK_OBJECT CallbackObject = NULL;
    PVOID Registration1, Registration2;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, L"\\Callback\\KmtExCallbackExtra");
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_PERMANENT, NULL, NULL);

    Status = ExCreateCallback(&CallbackObject, &ObjectAttributes, TRUE, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ok(CallbackObject != NULL, "callback object NULL\n");

    CallbackHits = 0;
    Registration1 = ExRegisterCallback(CallbackObject, CallbackFunction, (PVOID)(ULONG_PTR)0xC0DE);
    ok(Registration1 != NULL, "register 1 failed\n");
    Registration2 = ExRegisterCallback(CallbackObject, CallbackFunction, (PVOID)(ULONG_PTR)0xC0DE);
    ok(Registration2 != NULL, "register 2 failed\n");

    ExNotifyCallback(CallbackObject, (PVOID)(ULONG_PTR)0x1111, (PVOID)(ULONG_PTR)0x2222);
    ok_eq_long(CallbackHits, 2L);
    ok_eq_pointer(ObservedArg1, (PVOID)(ULONG_PTR)0x1111);
    ok_eq_pointer(ObservedArg2, (PVOID)(ULONG_PTR)0x2222);

    if (Registration1) ExUnregisterCallback(Registration1);
    CallbackHits = 0;
    ExNotifyCallback(CallbackObject, NULL, NULL);
    ok_eq_long(CallbackHits, 1L);

    if (Registration2) ExUnregisterCallback(Registration2);
    CallbackHits = 0;
    ExNotifyCallback(CallbackObject, NULL, NULL);
    ok_eq_long(CallbackHits, 0L);

    ObDereferenceObject(CallbackObject);

    TestNamedCallbackSharing();
}
