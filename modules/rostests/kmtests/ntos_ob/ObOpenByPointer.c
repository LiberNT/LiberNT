/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel-Mode Test Suite ObOpenObjectByPointer API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestOpenProcessByPointer(VOID)
{
    PEPROCESS Process = PsGetCurrentProcess();
    HANDLE Handle = NULL;
    PEPROCESS Referenced = NULL;
    NTSTATUS Status;

    Status = ObOpenObjectByPointer(Process, OBJ_KERNEL_HANDLE, NULL, PROCESS_ALL_ACCESS, *PsProcessType, KernelMode, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ok(Handle != NULL, "handle NULL\n");

    Status = ObReferenceObjectByHandle(Handle, PROCESS_ALL_ACCESS, *PsProcessType, KernelMode, (PVOID *)&Referenced, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(Referenced, Process);
        ObDereferenceObject(Referenced);
    }

    Status = ObCloseHandle(Handle, KernelMode);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestOpenThreadByPointer(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();
    HANDLE Handle = NULL;
    PVOID Referenced = NULL;
    NTSTATUS Status;

    Status = ObOpenObjectByPointer(Thread, OBJ_KERNEL_HANDLE, NULL, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByHandle(Handle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &Referenced, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(Referenced, (PVOID)Thread);
        ObDereferenceObject(Referenced);
    }

    Status = ObCloseHandle(Handle, KernelMode);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestTypeMismatch(VOID)
{
    PEPROCESS Process = PsGetCurrentProcess();
    HANDLE Handle = NULL;
    PVOID Referenced = NULL;
    NTSTATUS Status;

    Status = ObOpenObjectByPointer(Process, OBJ_KERNEL_HANDLE, NULL, PROCESS_ALL_ACCESS, *PsProcessType, KernelMode, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByHandle(Handle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &Referenced, NULL);
    ok_eq_hex(Status, STATUS_OBJECT_TYPE_MISMATCH);
    ok_eq_pointer(Referenced, NULL);

    ObCloseHandle(Handle, KernelMode);
}

static
VOID
TestQueryName(VOID)
{
    UNICODE_STRING DirName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Handle = NULL;
    PVOID Directory = NULL;
    NTSTATUS Status;
    UCHAR Buffer[512];
    POBJECT_NAME_INFORMATION NameInfo = (POBJECT_NAME_INFORMATION)Buffer;
    ULONG ReturnLength;
    UNICODE_STRING Expected;

    RtlInitUnicodeString(&DirName, L"\\KernelObjects");
    InitializeObjectAttributes(&ObjectAttributes, &DirName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenDirectoryObject(&Handle, DIRECTORY_QUERY, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByHandle(Handle, DIRECTORY_QUERY, NULL, KernelMode, &Directory, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ObQueryNameString(Directory, NameInfo, sizeof(Buffer), &ReturnLength);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            RtlInitUnicodeString(&Expected, L"\\KernelObjects");
            ok(RtlEqualUnicodeString(&NameInfo->Name, &Expected, TRUE), "name %wZ\n", &NameInfo->Name);
        }
        ObDereferenceObject(Directory);
    }

    ZwClose(Handle);
}

static
NTSTATUS
OpenForNoAccess(
    _In_ PVOID Object,
    _In_ POBJECT_TYPE Type,
    _In_ KPROCESSOR_MODE Mode)
{
    HANDLE Handle = NULL;
    NTSTATUS Status;

    Status = ObOpenObjectByPointer(Object, Mode == KernelMode ? OBJ_KERNEL_HANDLE : 0, NULL, 0, Type, Mode, &Handle);
    if (NT_SUCCESS(Status))
    {
        ok(Handle != NULL, "handle NULL\n");
        ObCloseHandle(Handle, Mode);
    }
    return Status;
}

static
VOID
TestOpenForNoAccess(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE EventHandle = NULL;
    PKEVENT Event = NULL;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateEvent(&EventHandle, EVENT_ALL_ACCESS, &ObjectAttributes, NotificationEvent, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByHandle(EventHandle, 0, *ExEventObjectType, KernelMode, (PVOID *)&Event, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = OpenForNoAccess(Event, *ExEventObjectType, UserMode);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = OpenForNoAccess(Event, *ExEventObjectType, KernelMode);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ObDereferenceObject(Event);
    }
    ZwClose(EventHandle);

    Status = OpenForNoAccess(PsGetCurrentProcess(), *PsProcessType, UserMode);
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);
    Status = OpenForNoAccess(PsGetCurrentProcess(), *PsProcessType, KernelMode);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

START_TEST(ObOpenByPointer)
{
    TestOpenProcessByPointer();
    TestOpenThreadByPointer();
    TestTypeMismatch();
    TestQueryName();
    TestOpenForNoAccess();
}
