/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NtInitializeRegistry flags on a running system
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(NtInitializeRegistry)
{
    NTSTATUS Status;

    Status = NtInitializeRegistry(CM_BOOT_FLAG_MAX + 1);
    ok_hex(Status, STATUS_INVALID_PARAMETER);
    Status = NtInitializeRegistry(CM_BOOT_FLAG_SMSS);
    ok_hex(Status, STATUS_ACCESS_DENIED);
    Status = NtInitializeRegistry(CM_BOOT_FLAG_SETUP);
    ok_hex(Status, STATUS_ACCESS_DENIED);
    Status = NtInitializeRegistry(CM_BOOT_FLAG_ACCEPTED + 1);
    ok_hex(Status, STATUS_INVALID_PARAMETER);
    Status = NtInitializeRegistry(CM_BOOT_FLAG_ACCEPTED + 5);
    ok_hex(Status, STATUS_INVALID_PARAMETER);
    Status = NtInitializeRegistry(CM_BOOT_FLAG_MAX);
    ok_hex(Status, STATUS_INVALID_PARAMETER);

    /* The service control manager may already have accepted this boot. */
    Status = NtInitializeRegistry(CM_BOOT_FLAG_ACCEPTED);
    ok(Status == STATUS_SUCCESS || Status == STATUS_ACCESS_DENIED, "first accept: 0x%08lx\n", Status);
    Status = NtInitializeRegistry(CM_BOOT_FLAG_ACCEPTED);
    ok_hex(Status, STATUS_ACCESS_DENIED);
    Status = NtInitializeRegistry(CM_BOOT_FLAG_ACCEPTED + 1);
    ok_hex(Status, STATUS_INVALID_PARAMETER);
}
