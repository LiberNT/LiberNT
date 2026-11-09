/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel mode tests for logon session manipulation API
 * COPYRIGHT:   Copyright 2021 George Bișoc <george.bisoc@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ntifs.h>

#define IMAGINARY_LOGON {0x001, 0}

static
VOID
LogonMarkTermination(VOID)
{
    NTSTATUS Status;
    LUID SystemLogonID = SYSTEM_LUID;
    LUID NonExistentLogonID = IMAGINARY_LOGON;

    /* Mark the system authentication logon for termination */
    Status = SeMarkLogonSessionForTerminationNotification(&SystemLogonID);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* We give a non existent logon */
    Status = SeMarkLogonSessionForTerminationNotification(&NonExistentLogonID);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
}

static
NTSTATUS
NTAPI
LogonSessionTerminated(
    _In_ PLUID LogonId)
{
    UNREFERENCED_PARAMETER(LogonId);
    return STATUS_SUCCESS;
}

static
VOID
LogonTerminationCallbacks(VOID)
{
    NTSTATUS Status;

    Status = SeRegisterLogonSessionTerminatedRoutine(NULL);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = SeUnregisterLogonSessionTerminatedRoutine(NULL);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = SeRegisterLogonSessionTerminatedRoutine(LogonSessionTerminated);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* Each registration stands on its own and needs its own removal */
    Status = SeRegisterLogonSessionTerminatedRoutine(LogonSessionTerminated);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = SeUnregisterLogonSessionTerminatedRoutine(LogonSessionTerminated);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = SeUnregisterLogonSessionTerminatedRoutine(LogonSessionTerminated);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = SeUnregisterLogonSessionTerminatedRoutine(LogonSessionTerminated);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_INSUFFICIENT_RESOURCES);
}

static
VOID
LogonMarkAnonymous(VOID)
{
    NTSTATUS Status;
    LUID AnonymousLogonID = ANONYMOUS_LOGON_LUID;

    /* The anonymous logon is there before anyone could log on */
    Status = SeMarkLogonSessionForTerminationNotification(&AnonymousLogonID);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

START_TEST(SeLogonSession)
{
    LogonMarkTermination();
    LogonMarkAnonymous();
    LogonTerminationCallbacks();
}
