/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A power action can be queued, and a shutdown that may ask the applications goes through the session
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/pofuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/setypes.h>

/* Ends with a shutdown request that the session carries out, so it is never part of a sweep */
START_TEST(PoInitiatePowerAction)
{
    BOOLEAN Old, Dummy;
    NTSTATUS Status;

    Status = RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, FALSE, FALSE, &Old);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = NtInitiatePowerAction(PowerActionWarmEject, PowerSystemSleeping1, 0, TRUE);
    ok_eq_hex(Status, STATUS_PRIVILEGE_NOT_HELD);

    Status = RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, TRUE, FALSE, &Dummy);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = NtInitiatePowerAction(PowerActionWarmEject, PowerSystemSleeping1, 0, FALSE);
    if (!skip(!NT_SUCCESS(Status), "A warm eject is carried out on this machine\n"))
    {
        Status = NtInitiatePowerAction(PowerActionWarmEject, PowerSystemSleeping1, 0, TRUE);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    trace("PoInitiatePowerAction: asking the session for a shutdown\n");
    Status = NtInitiatePowerAction(PowerActionShutdownOff,
                                   PowerSystemShutdown,
                                   POWER_ACTION_QUERY_ALLOWED,
                                   FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
}
