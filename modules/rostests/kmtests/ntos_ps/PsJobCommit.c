/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A process that leaves a job takes its commit out of the job's account
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

NTSYSAPI
NTSTATUS
NTAPI
ZwCreateProcessEx(
    _Out_ PHANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ HANDLE ParentProcess,
    _In_ ULONG Flags,
    _In_opt_ HANDLE SectionHandle,
    _In_opt_ HANDLE DebugPort,
    _In_opt_ HANDLE ExceptionPort,
    _In_ BOOLEAN InJob);

static
VOID
CheckThreadlessProcessCommit(
    _In_ HANDLE JobHandle,
    _In_ PEJOB Job)
{
    OBJECT_ATTRIBUTES Attributes;
    HANDLE ProcessHandle;
    ULONGLONG Before = Job->CurrentJobMemoryUsed;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateProcessEx(&ProcessHandle, PROCESS_ALL_ACCESS, &Attributes, ZwCurrentProcess(),
                               0, NULL, NULL, NULL, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ZwAssignProcessToJobObject(JobHandle, ProcessHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Job->CurrentJobMemoryUsed > Before, "joining charged nothing: %I64u pages\n", Job->CurrentJobMemoryUsed);

    ZwClose(ProcessHandle);
    ok_eq_ulonglong(Job->CurrentJobMemoryUsed, Before);
}

START_TEST(PsJobCommit)
{
    OBJECT_ATTRIBUTES Attributes;
    HANDLE JobHandle;
    PEJOB Job;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateJobObject(&JobHandle, JOB_OBJECT_ALL_ACCESS, &Attributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByHandle(JobHandle, JOB_OBJECT_ALL_ACCESS, PsJobType, KernelMode, (PVOID *)&Job, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckThreadlessProcessCommit(JobHandle, Job);
        ObDereferenceObject(Job);
    }

    ZwClose(JobHandle);
}
