/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A thread may delete a resource it still holds, as file systems do when a volume goes away
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
VOID
DeleteHeldExclusive(VOID)
{
    ERESOURCE Resource;
    NTSTATUS Status;

    Status = ExInitializeResourceLite(&Resource);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    KeEnterCriticalRegion();
    ok_bool_true(ExAcquireResourceExclusiveLite(&Resource, TRUE), "ExAcquireResourceExclusiveLite returned");
    ok_bool_true(ExAcquireResourceExclusiveLite(&Resource, TRUE), "recursive ExAcquireResourceExclusiveLite returned");
    ok_eq_ulong(ExIsResourceAcquiredExclusiveLite(&Resource), (ULONG)TRUE);

    Status = ExDeleteResourceLite(&Resource);
    ok_eq_hex(Status, STATUS_SUCCESS);
    KeLeaveCriticalRegion();
}

static
VOID
DeleteHeldShared(VOID)
{
    ERESOURCE Resource;
    NTSTATUS Status;

    Status = ExInitializeResourceLite(&Resource);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    KeEnterCriticalRegion();
    ok_bool_true(ExAcquireResourceSharedLite(&Resource, TRUE), "ExAcquireResourceSharedLite returned");
    ok_eq_ulong(ExIsResourceAcquiredSharedLite(&Resource), 1UL);

    Status = ExDeleteResourceLite(&Resource);
    ok_eq_hex(Status, STATUS_SUCCESS);
    KeLeaveCriticalRegion();
}

START_TEST(ExResourceDeleteHeld)
{
    DeleteHeldExclusive();
    DeleteHeldShared();
}
