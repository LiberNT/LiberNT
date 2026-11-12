/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The console display state, the platform information and the display idle timeout
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef struct _DISPLAY_SETTING_WATCH
{
    KEVENT Matched;
    volatile LONG Count;
    volatile LONG Value;
    volatile LONG Expected;
} DISPLAY_SETTING_WATCH, *PDISPLAY_SETTING_WATCH;

typedef struct _ULONG_SETTING_BUFFER
{
    ULONG Version;
    GUID Guid;
    SYSTEM_POWER_CONDITION PowerCondition;
    ULONG DataLength;
    ULONG Value;
} ULONG_SETTING_BUFFER;

C_ASSERT(FIELD_OFFSET(ULONG_SETTING_BUFFER, Value) == FIELD_OFFSET(SET_POWER_SETTING_VALUE, Data));

static DISPLAY_SETTING_WATCH DisplayWatch;
static DISPLAY_SETTING_WATCH MonitorWatch;
static DISPLAY_SETTING_WATCH TimeoutWatch;

static
NTSTATUS
NTAPI
SettingCallback(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _Inout_opt_ PVOID Context)
{
    PDISPLAY_SETTING_WATCH Watch = Context;

    UNREFERENCED_PARAMETER(SettingGuid);

    if (Watch == NULL || Value == NULL || ValueLength != sizeof(ULONG))
        return STATUS_SUCCESS;

    InterlockedExchange(&Watch->Value, *(PLONG)Value);
    InterlockedIncrement(&Watch->Count);
    if (*(PLONG)Value == InterlockedCompareExchange(&Watch->Expected, 0, 0))
        KeSetEvent(&Watch->Matched, IO_NO_INCREMENT, FALSE);

    return STATUS_SUCCESS;
}

static
PVOID
Watch(
    _In_ LPCGUID Setting,
    _Out_ PDISPLAY_SETTING_WATCH Context)
{
    PVOID Handle = NULL;
    NTSTATUS Status;

    KeInitializeEvent(&Context->Matched, NotificationEvent, FALSE);
    Context->Count = 0;
    Context->Value = -1;
    Context->Expected = -1;

    Status = PoRegisterPowerSettingCallback(NULL, Setting, SettingCallback, Context, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status) ? Handle : NULL;
}

static
VOID
Unwatch(
    _In_opt_ PVOID Handle)
{
    if (Handle != NULL)
        ok_eq_hex(PoUnregisterPowerSettingCallback(Handle), STATUS_SUCCESS);
}

static
BOOLEAN
WaitForValue(
    _Inout_ PDISPLAY_SETTING_WATCH Context,
    _In_ LONG Expected,
    _In_ LONG Milliseconds)
{
    LARGE_INTEGER Timeout;

    KeClearEvent(&Context->Matched);
    InterlockedExchange(&Context->Expected, Expected);
    if (InterlockedCompareExchange(&Context->Value, 0, 0) == Expected)
        return TRUE;

    Timeout.QuadPart = -10000LL * Milliseconds;
    return KeWaitForSingleObject(&Context->Matched, Executive, KernelMode, FALSE, &Timeout) == STATUS_SUCCESS;
}

static
NTSTATUS
SetDisplayState(
    _In_reads_bytes_opt_(Length) PVOID Input,
    _In_ ULONG Length)
{
    return ZwPowerInformation(SessionDisplayState, Input, Length, NULL, 0);
}

static
NTSTATUS
SetVideoTimeout(
    _In_ SYSTEM_POWER_CONDITION Condition,
    _In_ ULONG Seconds)
{
    ULONG_SETTING_BUFFER Setting;

    RtlZeroMemory(&Setting, sizeof(Setting));
    Setting.Version = POWER_SETTING_VALUE_VERSION;
    Setting.Guid = GUID_VIDEO_POWERDOWN_TIMEOUT;
    Setting.PowerCondition = Condition;
    Setting.DataLength = sizeof(ULONG);
    Setting.Value = Seconds;
    return ZwPowerInformation(SetPowerSettingValue, &Setting, sizeof(Setting), NULL, 0);
}

static
VOID
TestPlatformInformation(VOID)
{
    SYSTEM_POWER_CAPABILITIES Capabilities;
    POWER_PLATFORM_INFORMATION Information;
    ULONG Input = 0;
    NTSTATUS Status;

    RtlZeroMemory(&Capabilities, sizeof(Capabilities));
    Status = ZwPowerInformation(SystemPowerCapabilities, NULL, 0, &Capabilities, sizeof(Capabilities));
    ok_eq_hex(Status, STATUS_SUCCESS);

    Information.AoAc = 0xCC;
    Status = ZwPowerInformation(PlatformInformation, NULL, 0, &Information, sizeof(Information));
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_uint(Information.AoAc, Capabilities.AoAc);

    Status = ZwPowerInformation(PlatformInformation, &Input, sizeof(Input), &Information, sizeof(Information));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = ZwPowerInformation(PlatformInformation, NULL, 0, NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = ZwPowerInformation(PlatformInformation, NULL, 0, &Information, 0);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
}

static
VOID
TestDisplayStateRequests(VOID)
{
    ULONG Large[2] = { PowerMonitorOn, 0 };
    ULONG State;
    NTSTATUS Status;

    Status = SetDisplayState(NULL, sizeof(ULONG));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    State = PowerMonitorOn;
    Status = SetDisplayState(&State, sizeof(USHORT));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = SetDisplayState(Large, sizeof(Large));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    State = PowerMonitorDim + 1;
    Status = SetDisplayState(&State, sizeof(State));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    State = PowerMonitorDim;
    Status = SetDisplayState(&State, sizeof(State));
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(WaitForValue(&DisplayWatch, PowerMonitorDim, 2000), "the display was not dimmed, state %ld\n", DisplayWatch.Value);

    State = PowerMonitorOn;
    Status = SetDisplayState(&State, sizeof(State));
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(WaitForValue(&DisplayWatch, PowerMonitorOn, 2000), "the display did not come back on, state %ld\n", DisplayWatch.Value);
}

START_TEST(PoDisplayStateOff)
{
    SYSTEM_BATTERY_STATE Battery;
    SYSTEM_POWER_CONDITION Condition;
    PVOID DisplayHandle;
    PVOID MonitorHandle;
    PVOID TimeoutHandle;
    ULONG Original;
    NTSTATUS Status;

    TestPlatformInformation();

    RtlZeroMemory(&Battery, sizeof(Battery));
    Status = ZwPowerInformation(SystemBatteryState, NULL, 0, &Battery, sizeof(Battery));
    ok_eq_hex(Status, STATUS_SUCCESS);
    Condition = Battery.AcOnLine ? PoAc : PoDc;

    DisplayHandle = Watch(&GUID_CONSOLE_DISPLAY_STATE, &DisplayWatch);
    MonitorHandle = Watch(&GUID_MONITOR_POWER_ON, &MonitorWatch);
    ok_eq_long(DisplayWatch.Count, 1);
    ok_eq_long(DisplayWatch.Value, PowerMonitorOn);
    ok_eq_long(MonitorWatch.Count, 1);
    ok_eq_long(MonitorWatch.Value, 1);

    TestDisplayStateRequests();

    TimeoutHandle = Watch(&GUID_VIDEO_POWERDOWN_TIMEOUT, &TimeoutWatch);
    Original = (TimeoutWatch.Count != 0) ? (ULONG)TimeoutWatch.Value : 0;
    Unwatch(TimeoutHandle);

    Status = SetVideoTimeout(Condition, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(WaitForValue(&DisplayWatch, PowerMonitorOff, 250), "the display did not idle out, state %ld\n", DisplayWatch.Value);
        ok(WaitForValue(&MonitorWatch, 0, 250), "the monitor was not reported off, value %ld\n", MonitorWatch.Value);
        ok_eq_hex(SetVideoTimeout(Condition, Original), STATUS_SUCCESS);
    }

    Unwatch(MonitorHandle);
    Unwatch(DisplayHandle);
}

START_TEST(PoDisplayStateOn)
{
    PVOID DisplayHandle;
    ULONG State = PowerMonitorOn;
    BOOLEAN On;

    DisplayHandle = Watch(&GUID_CONSOLE_DISPLAY_STATE, &DisplayWatch);
    ok_eq_long(DisplayWatch.Count, 1);

    On = WaitForValue(&DisplayWatch, PowerMonitorOn, 2000);
    ok(On, "a display requirement did not turn the display on, state %ld\n", DisplayWatch.Value);
    if (!On)
        SetDisplayState(&State, sizeof(State));

    Unwatch(DisplayHandle);
}

START_TEST(PoSetSystemState)
{
    PVOID DisplayHandle;
    ULONG State;
    EXECUTION_STATE Before;
    EXECUTION_STATE After;
    NTSTATUS Status;

    DisplayHandle = Watch(&GUID_CONSOLE_DISPLAY_STATE, &DisplayWatch);
    ok_eq_long(DisplayWatch.Value, PowerMonitorOn);

    State = PowerMonitorOff;
    Status = SetDisplayState(&State, sizeof(State));
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(WaitForValue(&DisplayWatch, PowerMonitorOff, 2000), "the display did not turn off, state %ld\n", DisplayWatch.Value);

    Before = 0;
    Status = ZwPowerInformation(SystemExecutionState, NULL, 0, &Before, sizeof(Before));
    ok_eq_hex(Status, STATUS_SUCCESS);

    PoSetSystemState(ES_DISPLAY_REQUIRED);
    ok(WaitForValue(&DisplayWatch, PowerMonitorOn, 2000), "a display requirement did not turn the display on, state %ld\n", DisplayWatch.Value);

    After = 0;
    Status = ZwPowerInformation(SystemExecutionState, NULL, 0, &After, sizeof(After));
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Before & ES_DISPLAY_REQUIRED)
        skip(FALSE, "the display is already required by someone else\n");
    else
        ok(!(After & ES_DISPLAY_REQUIRED), "a passing display requirement was kept, state 0x%lx\n", After);

    State = PowerMonitorOn;
    SetDisplayState(&State, sizeof(State));
    ok(WaitForValue(&DisplayWatch, PowerMonitorOn, 2000), "the display did not come back on, state %ld\n", DisplayWatch.Value);
    Unwatch(DisplayHandle);
}
