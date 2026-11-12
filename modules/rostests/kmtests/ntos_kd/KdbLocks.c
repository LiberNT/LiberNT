/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops in the debugger holding a resource, for its view of held resources
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static ERESOURCE HeldResource;

START_TEST(KdbLocks)
{
    NTSTATUS Status;
    BOOLEAN Acquired;

    Status = ExInitializeResourceLite(&HeldResource);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    KeEnterCriticalRegion();
    Acquired = ExAcquireResourceExclusiveLite(&HeldResource, TRUE);
    ok_bool_true(Acquired, "ExAcquireResourceExclusiveLite");

    /* The debugger's start-up file lists the held resources here */
    DbgPrint("KdbLocks: resource %p held by thread %p\n", &HeldResource, KeGetCurrentThread());
    DbgBreakPoint();

    if (Acquired)
        ExReleaseResourceLite(&HeldResource);
    KeLeaveCriticalRegion();

    Status = ExDeleteResourceLite(&HeldResource);
    ok_eq_hex(Status, STATUS_SUCCESS);
}
