/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing the system to a stop
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Tells the parts of the executive that keep something of their own that the
 * system is going down, in the order in which they may still be of use to each
 * other: the ones that write go before the ones they write through.
 */
VOID
NTAPI
ExShutdownSystem(VOID)
{
    EtwpShutdownLoggers();
}

NTSTATUS
NTAPI
NtShutdownSystem(
    _In_ SHUTDOWN_ACTION Action)
{
    POWER_ACTION PowerAction;

    PAGED_CODE();

    if ((Action != ShutdownNoReboot) &&
        (Action != ShutdownReboot) &&
        (Action != ShutdownPowerOff))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!SeSinglePrivilegeCheck(SeShutdownPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (Action == ShutdownReboot)
    {
        PowerAction = PowerActionShutdownReset;
    }
    else if (Action == ShutdownPowerOff)
    {
        PowerAction = PowerActionShutdownOff;
    }
    else
    {
        PowerAction = PowerActionShutdown;
    }

    return NtSetSystemPowerState(PowerAction,
                                 PowerSystemSleeping3,
                                 POWER_ACTION_OVERRIDE_APPS);
}

/* EOF */
