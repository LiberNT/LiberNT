/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A power setting changed by a thread outside every session
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

/* A setting of the test's own, so that changing it changes nothing else */
static const GUID TestSetting = { 0x6d3c1d2e, 0x4f0a, 0x4b8e, { 0x9b, 0x5a, 0x1c, 0x2d, 0x3e, 0x4f, 0x5a, 0x6b } };

#define TEST_SETTING_VALUE 0x5A5A0001

typedef struct _ULONG_SETTING_BUFFER
{
    ULONG Version;
    GUID Guid;
    SYSTEM_POWER_CONDITION PowerCondition;
    ULONG DataLength;
    ULONG Value;
} ULONG_SETTING_BUFFER;

C_ASSERT(FIELD_OFFSET(ULONG_SETTING_BUFFER, Value) == FIELD_OFFSET(SET_POWER_SETTING_VALUE, Data));

static SYSTEM_POWER_CONDITION Condition;
static NTSTATUS ThreadStatus;
static ULONG ThreadSessionId;

static
VOID
NTAPI
SetFromSystemThread(
    _In_ PVOID Context)
{
    ULONG_SETTING_BUFFER Setting;

    UNREFERENCED_PARAMETER(Context);

    ThreadSessionId = PsGetProcessSessionIdEx(PsGetCurrentProcess());

    RtlZeroMemory(&Setting, sizeof(Setting));
    Setting.Version = POWER_SETTING_VALUE_VERSION;
    Setting.Guid = TestSetting;
    Setting.PowerCondition = Condition;
    Setting.DataLength = sizeof(ULONG);
    Setting.Value = TEST_SETTING_VALUE;
    ThreadStatus = ZwPowerInformation(SetPowerSettingValue, &Setting, sizeof(Setting), NULL, 0);

    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(PoSettingSession)
{
    SYSTEM_BATTERY_STATE Battery;
    LARGE_INTEGER Timeout;
    PVOID Thread = NULL;
    HANDLE Handle;
    NTSTATUS Status;

    RtlZeroMemory(&Battery, sizeof(Battery));
    Status = ZwPowerInformation(SystemBatteryState, NULL, 0, &Battery, sizeof(Battery));
    ok_eq_hex(Status, STATUS_SUCCESS);
    Condition = Battery.AcOnLine ? PoAc : PoDc;

    ThreadStatus = STATUS_PENDING;
    ThreadSessionId = 0;
    Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, NULL, NULL, NULL, SetFromSystemThread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = ObReferenceObjectByHandle(Handle, SYNCHRONIZE, *PsThreadType, KernelMode, &Thread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status))
        return;

    Timeout.QuadPart = -10000LL * 5000;
    Status = KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ObDereferenceObject(Thread);

    ok_eq_ulong(ThreadSessionId, (ULONG)-1);
    ok_eq_hex(ThreadStatus, STATUS_SUCCESS);
}
