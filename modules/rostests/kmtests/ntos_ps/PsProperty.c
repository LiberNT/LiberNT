/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Thread and job property set tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

typedef PVOID (NTAPI *PFN_PS_GET_THREAD_PROPERTY)(PETHREAD, ULONG_PTR, ULONG);
typedef NTSTATUS (NTAPI *PFN_PS_SET_THREAD_PROPERTY)(PETHREAD, ULONG_PTR, PVOID);
typedef PVOID (NTAPI *PFN_PS_GET_JOB_PROPERTY)(PVOID, ULONG_PTR);
typedef NTSTATUS (NTAPI *PFN_PS_SET_JOB_PROPERTY)(PVOID, ULONG_PTR, PVOID);

#define TEST_KEY_1 ((ULONG_PTR)0x4B4D5431)
#define TEST_KEY_2 ((ULONG_PTR)0x4B4D5432)

typedef struct _TEST_PS_OBJECT
{
    HANDLE Handle;
    PVOID Object;
    LONG Base;
} TEST_PS_OBJECT, *PTEST_PS_OBJECT;

typedef struct _TEST_PS_THREAD_CONTEXT
{
    PFN_PS_SET_THREAD_PROPERTY pSet;
    PVOID Object;
    NTSTATUS Status;
} TEST_PS_THREAD_CONTEXT, *PTEST_PS_THREAD_CONTEXT;

static
PVOID
TestPsGetRoutine(
    _In_ PCWSTR Name)
{
    UNICODE_STRING RoutineName;

    RtlInitUnicodeString(&RoutineName, Name);
    return MmGetSystemRoutineAddress(&RoutineName);
}

static
LONG
TestPsDelta(
    _In_ PTEST_PS_OBJECT Object)
{
    return (LONG)OBJECT_TO_OBJECT_HEADER(Object->Object)->PointerCount - Object->Base;
}

static
BOOLEAN
TestPsCreateEvent(
    _Out_ PTEST_PS_OBJECT Object)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    RtlZeroMemory(Object, sizeof(*Object));
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateEvent(&Object->Handle, EVENT_ALL_ACCESS, &ObjectAttributes, NotificationEvent, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Status = ObReferenceObjectByHandle(Object->Handle, EVENT_ALL_ACCESS, *ExEventObjectType, KernelMode, &Object->Object, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(Object->Handle);
        Object->Handle = NULL;
        return FALSE;
    }

    Object->Base = (LONG)OBJECT_TO_OBJECT_HEADER(Object->Object)->PointerCount;
    return TRUE;
}

static
VOID
TestPsDestroyEvent(
    _Inout_ PTEST_PS_OBJECT Object)
{
    if (Object->Object)
        ObDereferenceObject(Object->Object);
    if (Object->Handle)
        ZwClose(Object->Handle);
}

static
VOID
TestPsCheckGet(
    _In_opt_ PVOID Value,
    _In_ PTEST_PS_OBJECT Expected,
    _In_ LONG ExpectedDelta)
{
    ok_eq_pointer(Value, Expected->Object);
    ok_eq_long(TestPsDelta(Expected), ExpectedDelta + 1);
    if (Value)
        ObDereferenceObject(Value);
    ok_eq_long(TestPsDelta(Expected), ExpectedDelta);
}

START_TEST(PsPropertyThread)
{
    PFN_PS_GET_THREAD_PROPERTY pGet = TestPsGetRoutine(L"PsGetThreadProperty");
    PFN_PS_SET_THREAD_PROPERTY pSet = TestPsGetRoutine(L"PsSetThreadProperty");
    PETHREAD Thread = PsGetCurrentThread();
    TEST_PS_OBJECT Event1, Event2;

    if (!pGet || !pSet)
    {
        skip(FALSE, "Thread property exports not available\n");
        return;
    }

    if (!TestPsCreateEvent(&Event1))
        return;
    if (!TestPsCreateEvent(&Event2))
    {
        TestPsDestroyEvent(&Event1);
        return;
    }

    ok_eq_pointer(pGet(Thread, TEST_KEY_1, 0), NULL);

    ok_eq_hex(pSet(Thread, TEST_KEY_1, Event1.Object), STATUS_SUCCESS);
    ok_eq_long(TestPsDelta(&Event1), 1);
    TestPsCheckGet(pGet(Thread, TEST_KEY_1, 0), &Event1, 1);
    TestPsCheckGet(pGet(Thread, TEST_KEY_1, 1), &Event1, 1);

    ok_eq_hex(pSet(Thread, TEST_KEY_2, Event2.Object), STATUS_SUCCESS);
    ok_eq_long(TestPsDelta(&Event2), 1);
    TestPsCheckGet(pGet(Thread, TEST_KEY_2, 0), &Event2, 1);

    ok_eq_hex(pSet(Thread, TEST_KEY_1, Event2.Object), STATUS_OBJECT_NAME_COLLISION);
    ok_eq_long(TestPsDelta(&Event1), 1);
    ok_eq_long(TestPsDelta(&Event2), 1);
    TestPsCheckGet(pGet(Thread, TEST_KEY_1, 0), &Event1, 1);

    ok_eq_hex(pSet(Thread, TEST_KEY_1, NULL), STATUS_SUCCESS);
    ok_eq_long(TestPsDelta(&Event1), 0);
    ok_eq_pointer(pGet(Thread, TEST_KEY_1, 0), NULL);
    ok_eq_hex(pSet(Thread, TEST_KEY_1, NULL), STATUS_SUCCESS);

    ok_eq_hex(pSet(Thread, TEST_KEY_2, NULL), STATUS_SUCCESS);
    ok_eq_long(TestPsDelta(&Event2), 0);
    ok_eq_pointer(pGet(Thread, TEST_KEY_2, 0), NULL);

    TestPsDestroyEvent(&Event2);
    TestPsDestroyEvent(&Event1);
}

static
VOID
NTAPI
TestPsThreadRoutine(
    _In_ PVOID StartContext)
{
    PTEST_PS_THREAD_CONTEXT Context = StartContext;

    Context->Status = Context->pSet(PsGetCurrentThread(), TEST_KEY_1, Context->Object);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(PsPropertyThreadExit)
{
    PFN_PS_SET_THREAD_PROPERTY pSet = TestPsGetRoutine(L"PsSetThreadProperty");
    TEST_PS_THREAD_CONTEXT Context;
    OBJECT_ATTRIBUTES ObjectAttributes;
    TEST_PS_OBJECT Event;
    HANDLE ThreadHandle;
    PVOID ThreadObject;
    NTSTATUS Status;

    if (!pSet)
    {
        skip(FALSE, "Thread property exports not available\n");
        return;
    }

    if (!TestPsCreateEvent(&Event))
        return;

    Context.pSet = pSet;
    Context.Object = Event.Object;
    Context.Status = STATUS_PENDING;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL, TestPsThreadRoutine, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        TestPsDestroyEvent(&Event);
        return;
    }

    Status = ObReferenceObjectByHandle(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode, &ThreadObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(ThreadHandle);
    if (NT_SUCCESS(Status))
    {
        KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, NULL);
        ok_eq_hex(Context.Status, STATUS_SUCCESS);
        ok_eq_long(TestPsDelta(&Event), 0);
        ObDereferenceObject(ThreadObject);
        ok_eq_long(TestPsDelta(&Event), 0);
    }

    TestPsDestroyEvent(&Event);
}

START_TEST(PsPropertyDispatch)
{
    PFN_PS_GET_THREAD_PROPERTY pGet = TestPsGetRoutine(L"PsGetThreadProperty");
    PFN_PS_SET_THREAD_PROPERTY pSet = TestPsGetRoutine(L"PsSetThreadProperty");
    PETHREAD Thread = PsGetCurrentThread();
    TEST_PS_OBJECT Event;
    NTSTATUS SetStatus, ClearStatus;
    PVOID Value;
    KIRQL OldIrql;

    if (!pGet || !pSet)
    {
        skip(FALSE, "Thread property exports not available\n");
        return;
    }

    if (!TestPsCreateEvent(&Event))
        return;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    SetStatus = pSet(Thread, TEST_KEY_1, Event.Object);
    Value = pGet(Thread, TEST_KEY_1, 0);
    ClearStatus = pSet(Thread, TEST_KEY_1, NULL);
    KeLowerIrql(OldIrql);

    ok_eq_hex(SetStatus, STATUS_SUCCESS);
    ok_eq_hex(ClearStatus, STATUS_SUCCESS);
    TestPsCheckGet(Value, &Event, 0);

    TestPsDestroyEvent(&Event);
}

START_TEST(PsPropertyJob)
{
    PFN_PS_GET_JOB_PROPERTY pGet = TestPsGetRoutine(L"PsGetJobProperty");
    PFN_PS_SET_JOB_PROPERTY pSet = TestPsGetRoutine(L"PsSetJobProperty");
    OBJECT_ATTRIBUTES ObjectAttributes;
    TEST_PS_OBJECT Event1, Event2;
    HANDLE JobHandle;
    PVOID Job;
    NTSTATUS Status;

    if (!pGet || !pSet)
    {
        skip(FALSE, "Job property exports not available\n");
        return;
    }

    if (!TestPsCreateEvent(&Event1))
        return;
    if (!TestPsCreateEvent(&Event2))
    {
        TestPsDestroyEvent(&Event1);
        return;
    }

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateJobObject(&JobHandle, JOB_OBJECT_ALL_ACCESS, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = ObReferenceObjectByHandle(JobHandle, JOB_OBJECT_ALL_ACCESS, NULL, KernelMode, &Job, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(JobHandle);
        goto Cleanup;
    }

    ok_eq_pointer(pGet(Job, TEST_KEY_1), NULL);

    ok_eq_hex(pSet(Job, TEST_KEY_1, Event1.Object), STATUS_SUCCESS);
    ok_eq_long(TestPsDelta(&Event1), 1);
    TestPsCheckGet(pGet(Job, TEST_KEY_1), &Event1, 1);

    ok_eq_hex(pSet(Job, TEST_KEY_1, Event2.Object), STATUS_OBJECT_NAME_COLLISION);
    ok_eq_long(TestPsDelta(&Event1), 1);
    ok_eq_long(TestPsDelta(&Event2), 0);
    TestPsCheckGet(pGet(Job, TEST_KEY_1), &Event1, 1);

    ok_eq_hex(pSet(Job, TEST_KEY_1, NULL), STATUS_SUCCESS);
    ok_eq_long(TestPsDelta(&Event1), 0);
    ok_eq_pointer(pGet(Job, TEST_KEY_1), NULL);

    ok_eq_hex(pSet(Job, TEST_KEY_2, Event2.Object), STATUS_SUCCESS);
    ok_eq_long(TestPsDelta(&Event2), 1);

    ObDereferenceObject(Job);
    ZwClose(JobHandle);
    ok_eq_long(TestPsDelta(&Event2), 0);

Cleanup:
    TestPsDestroyEvent(&Event2);
    TestPsDestroyEvent(&Event1);
}
