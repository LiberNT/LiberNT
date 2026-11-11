/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A driver registering for GUID_LOCK_CONSOLE_ON_WAKE is told the console is locked on wake
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static LONG CallbackCount;
static ULONG LastLength;
static ULONG LastValue;

static
NTSTATUS
NTAPI
LockConsoleSettingChanged(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _Inout_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    ok(IsEqualGUID(SettingGuid, &GUID_LOCK_CONSOLE_ON_WAKE), "The callback was told another setting\n");
    LastLength = ValueLength;
    if ((Value != NULL) && (ValueLength == sizeof(ULONG)))
        LastValue = *(PULONG)Value;

    InterlockedIncrement(&CallbackCount);
    return STATUS_SUCCESS;
}

START_TEST(PoLockConsoleOnWake)
{
    PVOID Handle = NULL;
    NTSTATUS Status;

    CallbackCount = 0;
    LastLength = 0;
    LastValue = MAXULONG;

    Status = PoRegisterPowerSettingCallback(NULL, &GUID_LOCK_CONSOLE_ON_WAKE, LockConsoleSettingChanged, NULL, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || (Handle == NULL))
        return;

    ok_eq_long(CallbackCount, 1);
    ok_eq_ulong(LastLength, sizeof(ULONG));
    ok_eq_ulong(LastValue, 1UL);

    Status = PoUnregisterPowerSettingCallback(Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
}
