/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A process lets go of its primary token when it is deleted
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

START_TEST(SePrimaryTokenRelease)
{
    OBJECT_ATTRIBUTES Attributes;
    PEPROCESS Process = NULL;
    PACCESS_TOKEN Token;
    HANDLE Child = NULL;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateProcessEx(&Child, PROCESS_ALL_ACCESS, &Attributes, ZwCurrentProcess(),
                               0, NULL, NULL, NULL, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByHandle(Child, PROCESS_ALL_ACCESS, *PsProcessType, KernelMode,
                                       (PVOID *)&Process, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(Child);
    if (!NT_SUCCESS(Status)) return;

    Token = PsReferencePrimaryToken(Process);
    ok(Token != NULL, "the new process has no primary token\n");

    ObDereferenceObject(Process);

    if (Token == NULL) return;

    ok_eq_long((LONG)OBJECT_TO_OBJECT_HEADER(Token)->PointerCount, 1L);
    PsDereferencePrimaryToken(Token);
}
