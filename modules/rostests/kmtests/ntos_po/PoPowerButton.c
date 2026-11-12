/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The power button, pressed by the host when the test asks for it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef struct _BUTTON_WATCH
{
    KEVENT Matched;
    volatile LONG Value;
    volatile LONG Expected;
} BUTTON_WATCH, *PBUTTON_WATCH;

typedef struct _POLICY_SETTING_BUFFER
{
    ULONG Version;
    GUID Guid;
    SYSTEM_POWER_CONDITION PowerCondition;
    ULONG DataLength;
    POWER_ACTION_POLICY Policy;
} POLICY_SETTING_BUFFER;

C_ASSERT(FIELD_OFFSET(POLICY_SETTING_BUFFER, Policy) == FIELD_OFFSET(SET_POWER_SETTING_VALUE, Data));

static BUTTON_WATCH DisplayWatch;
static POWER_ACTION_POLICY OriginalPolicy;
static volatile LONG OriginalPolicyFound;

static
NTSTATUS
NTAPI
DisplayCallback(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _Inout_opt_ PVOID Context)
{
    PBUTTON_WATCH Watch = Context;

    UNREFERENCED_PARAMETER(SettingGuid);

    if (Watch == NULL || Value == NULL || ValueLength != sizeof(ULONG))
        return STATUS_SUCCESS;

    InterlockedExchange(&Watch->Value, *(PLONG)Value);
    if (*(PLONG)Value == InterlockedCompareExchange(&Watch->Expected, 0, 0))
        KeSetEvent(&Watch->Matched, IO_NO_INCREMENT, FALSE);

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
PolicyCallback(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _Inout_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(SettingGuid);
    UNREFERENCED_PARAMETER(Context);

    if (Value != NULL && ValueLength == sizeof(OriginalPolicy) && InterlockedCompareExchange(&OriginalPolicyFound, 1, 0) == 0)
        RtlCopyMemory(&OriginalPolicy, Value, sizeof(OriginalPolicy));

    return STATUS_SUCCESS;
}

static
BOOLEAN
PressAndWait(
    _In_ LONG Expected)
{
    LARGE_INTEGER Timeout;

    KeClearEvent(&DisplayWatch.Matched);
    InterlockedExchange(&DisplayWatch.Expected, Expected);
    DbgPrint("PBTEST_PRESS\n");

    Timeout.QuadPart = -10000LL * 5000;
    return KeWaitForSingleObject(&DisplayWatch.Matched, Executive, KernelMode, FALSE, &Timeout) == STATUS_SUCCESS;
}

static
NTSTATUS
SetButtonPolicy(
    _In_ SYSTEM_POWER_CONDITION Condition,
    _In_ const POWER_ACTION_POLICY *Policy)
{
    POLICY_SETTING_BUFFER Setting;

    RtlZeroMemory(&Setting, sizeof(Setting));
    Setting.Version = POWER_SETTING_VALUE_VERSION;
    Setting.Guid = GUID_POWERBUTTON_ACTION;
    Setting.PowerCondition = Condition;
    Setting.DataLength = sizeof(Setting.Policy);
    Setting.Policy = *Policy;
    return ZwPowerInformation(SetPowerSettingValue, &Setting, sizeof(Setting), NULL, 0);
}

START_TEST(PoPowerButton)
{
    SYSTEM_POWER_CAPABILITIES Capabilities;
    SYSTEM_BATTERY_STATE Battery;
    SYSTEM_POWER_CONDITION Condition;
    POWER_ACTION_POLICY Policy;
    PVOID DisplayHandle = NULL;
    PVOID PolicyHandle = NULL;
    ULONG State = PowerMonitorOn;
    NTSTATUS Status;

    RtlZeroMemory(&Capabilities, sizeof(Capabilities));
    Status = ZwPowerInformation(SystemPowerCapabilities, NULL, 0, &Capabilities, sizeof(Capabilities));
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Capabilities.PowerButtonPresent, "no power button is reported\n");

    RtlZeroMemory(&Battery, sizeof(Battery));
    Status = ZwPowerInformation(SystemBatteryState, NULL, 0, &Battery, sizeof(Battery));
    ok_eq_hex(Status, STATUS_SUCCESS);
    Condition = Battery.AcOnLine ? PoAc : PoDc;

    OriginalPolicyFound = 0;
    Status = PoRegisterPowerSettingCallback(NULL, &GUID_POWERBUTTON_ACTION, PolicyCallback, NULL, &PolicyHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        PoUnregisterPowerSettingCallback(PolicyHandle);

    RtlZeroMemory(&Policy, sizeof(Policy));
    Policy.Action = PowerActionDisplayOff;
    Status = SetButtonPolicy(Condition, &Policy);
    ok_eq_hex(Status, STATUS_SUCCESS);

    KeInitializeEvent(&DisplayWatch.Matched, NotificationEvent, FALSE);
    DisplayWatch.Value = -1;
    DisplayWatch.Expected = -1;
    Status = PoRegisterPowerSettingCallback(NULL, &GUID_CONSOLE_DISPLAY_STATE, DisplayCallback, &DisplayWatch, &DisplayHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(DisplayWatch.Value, PowerMonitorOn);

    ok(PressAndWait(PowerMonitorOff), "the power button did not turn the display off, state %ld\n", DisplayWatch.Value);
    ok(PressAndWait(PowerMonitorOn), "the power button did not turn the display on, state %ld\n", DisplayWatch.Value);

    if (DisplayWatch.Value != PowerMonitorOn)
        ZwPowerInformation(SessionDisplayState, &State, sizeof(State), NULL, 0);

    if (DisplayHandle != NULL)
        PoUnregisterPowerSettingCallback(DisplayHandle);

    if (OriginalPolicyFound)
    {
        Policy = OriginalPolicy;
    }
    else
    {
        RtlZeroMemory(&Policy, sizeof(Policy));
        Policy.Action = PowerActionShutdownOff;
    }
    ok_eq_hex(SetButtonPolicy(Condition, &Policy), STATUS_SUCCESS);
}
