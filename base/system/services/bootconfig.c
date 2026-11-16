/*
 * PROJECT:     LiberNT Service Control Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot configuration status reported through NotifyBootConfigStatus
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "services.h"
#include <ndk/cmfuncs.h>

#define NDEBUG
#include <debug.h>

static LONG ScmBootAccepted;
static BOOL ScmLastKnownGoodEnabled;

/* Last Known Good is off unless enabled when the service control manager starts. */
VOID
ScmReadBootConfigPolicy(VOID)
{
    DWORD Enabled = 0;
    DWORD Size = sizeof(Enabled);
    DWORD Type;
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Configuration Manager\\LastKnownGood",
                      0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
    {
        return;
    }
    if (RegQueryValueExW(Key, L"Enabled", NULL, &Type, (LPBYTE)&Enabled, &Size) == ERROR_SUCCESS &&
        Type == REG_DWORD && Enabled == 1)
    {
        ScmLastKnownGoodEnabled = TRUE;
    }
    RegCloseKey(Key);
}

/* The kernel keeps the Last Known Good control set; without the feature there is nothing to save. */
DWORD
ScmAcceptBoot(VOID)
{
    NTSTATUS Status;

    if (!ScmLastKnownGoodEnabled)
        return ERROR_SUCCESS;

    /* TODO: wait for the auto-start services to finish before accepting the boot */
    if (InterlockedCompareExchange(&ScmBootAccepted, 1, 0) != 0)
        return ERROR_BOOT_ALREADY_ACCEPTED;

    Status = NtInitializeRegistry(CM_BOOT_FLAG_ACCEPTED);
    if (Status == STATUS_ACCESS_DENIED)
        return ERROR_BOOT_ALREADY_ACCEPTED;
    if (!NT_SUCCESS(Status))
        DPRINT1("SERVICES: accepting the boot failed (Status 0x%08lx)\n", Status);
    return RtlNtStatusToDosError(Status);
}

DWORD
ScmRunLastKnownGood(VOID)
{
    if (InterlockedCompareExchange(&ScmBootAccepted, 0, 0) != 0)
        return ERROR_BOOT_ALREADY_ACCEPTED;

    /* TODO: restart the system on the Last Known Good control set */
    return ERROR_SUCCESS;
}
