/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the callback form of the power setting and suspend/resume notifications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <powrprof.h>
#include <powersetting.h>

static const GUID acdc_power_source = {0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};
static const GUID scheme_personality = {0x245d8541, 0x3943, 0x4422, {0xb0, 0x25, 0x13, 0xa7, 0x84, 0xf6, 0x79, 0xb7}};
static const GUID processor_subgroup = {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
static const GUID processor_minimum = {0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c}};
static const GUID balanced_scheme = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
static const GUID high_performance_scheme = {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
static const GUID power_saver_scheme = {0xa1841308, 0x3541, 0x4fab, {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};
static const GUID unknown_setting = {0x6b2e1c0a, 0x8f3d, 0x4e57, {0x9a, 0x41, 0x2c, 0x7d, 0x5e, 0x10, 0x93, 0xb8}};

typedef struct _NOTIFY_RECORD
{
    HANDLE Event;
    LONG Calls;
    DWORD Thread;
    ULONG Type;
    GUID Setting;
    DWORD DataLength;
    BYTE Data[16];
} NOTIFY_RECORD;

static ULONG CALLBACK notify_callback(PVOID context, ULONG type, PVOID setting)
{
    NOTIFY_RECORD *record = context;
    POWERBROADCAST_SETTING *broadcast = setting;

    record->Thread = GetCurrentThreadId();
    record->Type = type;
    if (type == PBT_POWERSETTINGCHANGE && broadcast)
    {
        record->Setting = broadcast->PowerSetting;
        record->DataLength = broadcast->DataLength;
        memcpy(record->Data, broadcast->Data, min(broadcast->DataLength, sizeof(record->Data)));
    }
    InterlockedIncrement(&record->Calls);
    SetEvent(record->Event);
    return 0;
}

static BOOL on_ac(void)
{
    SYSTEM_POWER_STATUS status;

    return !GetSystemPowerStatus(&status) || status.ACLineStatus != 0;
}

static HPOWERNOTIFY register_setting(const GUID *setting, NOTIFY_RECORD *record, DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS *params)
{
    HPOWERNOTIFY handle = NULL;
    DWORD ret;

    memset(record, 0, sizeof(*record));
    record->Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    params->Callback = notify_callback;
    params->Context = record;
    ret = PowerSettingRegisterNotification(setting, DEVICE_NOTIFY_CALLBACK, params, &handle);
    ok(ret == ERROR_SUCCESS, "PowerSettingRegisterNotification returned %lu\n", ret);
    ok(handle != NULL, "no registration handle\n");
    return handle;
}

static void unregister_setting(HPOWERNOTIFY handle, NOTIFY_RECORD *record)
{
    DWORD ret;

    ret = PowerSettingUnregisterNotification(handle);
    ok(ret == ERROR_SUCCESS, "PowerSettingUnregisterNotification returned %lu\n", ret);
    ret = PowerSettingUnregisterNotification(handle);
    ok(ret == ERROR_INVALID_PARAMETER, "second PowerSettingUnregisterNotification returned %lu\n", ret);
    CloseHandle(record->Event);
}

static void expect_one_notification(const GUID *setting, NOTIFY_RECORD *record, DWORD length)
{
    ok(WaitForSingleObject(record->Event, 1000) == WAIT_OBJECT_0, "no notification\n");
    Sleep(100);
    ok(record->Calls == 1, "%ld notifications\n", record->Calls);
    ok(record->Thread != GetCurrentThreadId(), "the notification ran on the registering thread\n");
    ok(record->Type == PBT_POWERSETTINGCHANGE, "type 0x%lx\n", record->Type);
    ok(IsEqualGUID(&record->Setting, setting), "notification for another setting\n");
    ok(record->DataLength == length, "data length %lu\n", record->DataLength);
}

static void test_arguments(void)
{
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params = {notify_callback, NULL};
    HPOWERNOTIFY handle = NULL;
    DWORD ret;

    ret = PowerSettingRegisterNotification(&acdc_power_source, 0, &params, &handle);
    ok(ret == ERROR_INVALID_PARAMETER, "window flag returned %lu\n", ret);
    ret = PowerSettingRegisterNotification(&acdc_power_source, 0x10, &params, &handle);
    ok(ret == ERROR_INVALID_PARAMETER, "flag 0x10 returned %lu\n", ret);
    ret = PowerSettingRegisterNotification(&acdc_power_source, DEVICE_NOTIFY_CALLBACK, NULL, &handle);
    ok(ret == ERROR_INVALID_PARAMETER, "NULL recipient returned %lu\n", ret);
    ok(handle == NULL, "a refused registration returned a handle\n");
}

static void test_power_source(void)
{
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params;
    NOTIFY_RECORD record;
    HPOWERNOTIFY handle;

    handle = register_setting(&acdc_power_source, &record, &params);
    expect_one_notification(&acdc_power_source, &record, sizeof(DWORD));
    ok(*(DWORD *)record.Data == (on_ac() ? 0 : 1), "power source %lu\n", *(DWORD *)record.Data);
    unregister_setting(handle, &record);
}

static void test_personality(void)
{
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params;
    NOTIFY_RECORD record;
    HPOWERNOTIFY handle;
    GUID *active = NULL;

    handle = register_setting(&scheme_personality, &record, &params);
    expect_one_notification(&scheme_personality, &record, sizeof(GUID));
    if (PowerGetActiveScheme(NULL, &active) == ERROR_SUCCESS)
    {
        if (IsEqualGUID(active, &balanced_scheme) || IsEqualGUID(active, &high_performance_scheme) ||
            IsEqualGUID(active, &power_saver_scheme))
        {
            ok(IsEqualGUID((GUID *)record.Data, active), "personality differs from the active scheme\n");
        }
        LocalFree(active);
    }
    unregister_setting(handle, &record);
}

static void test_scheme_setting(void)
{
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params;
    NOTIFY_RECORD record;
    HPOWERNOTIFY handle;
    GUID *active = NULL;
    DWORD index = 0xdeadbeef, ret;

    handle = register_setting(&processor_minimum, &record, &params);
    expect_one_notification(&processor_minimum, &record, sizeof(DWORD));
    if (PowerGetActiveScheme(NULL, &active) == ERROR_SUCCESS)
    {
        ret = on_ac() ? PowerReadACValueIndex(NULL, active, &processor_subgroup, &processor_minimum, &index)
                      : PowerReadDCValueIndex(NULL, active, &processor_subgroup, &processor_minimum, &index);
        ok(ret == ERROR_SUCCESS, "reading the active value returned %lu\n", ret);
        ok(*(DWORD *)record.Data == index, "notified %lu, active scheme has %lu\n", *(DWORD *)record.Data, index);
        LocalFree(active);
    }
    unregister_setting(handle, &record);
}

static void test_unknown_setting(void)
{
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params;
    NOTIFY_RECORD record;
    HPOWERNOTIFY handle;

    handle = register_setting(&unknown_setting, &record, &params);
    ok(WaitForSingleObject(record.Event, 300) == WAIT_TIMEOUT, "an unknown setting was notified\n");
    unregister_setting(handle, &record);
}

static void test_two_registrations(void)
{
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params1, params2;
    NOTIFY_RECORD record1, record2;
    HPOWERNOTIFY handle1, handle2;

    handle1 = register_setting(&acdc_power_source, &record1, &params1);
    handle2 = register_setting(&acdc_power_source, &record2, &params2);
    ok(handle1 != handle2, "both registrations share a handle\n");
    expect_one_notification(&acdc_power_source, &record1, sizeof(DWORD));
    expect_one_notification(&acdc_power_source, &record2, sizeof(DWORD));
    unregister_setting(handle1, &record1);
    unregister_setting(handle2, &record2);
}

static void test_suspend_resume(void)
{
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params;
    NOTIFY_RECORD record;
    HPOWERNOTIFY handle = NULL;
    DWORD ret;

    memset(&record, 0, sizeof(record));
    record.Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    params.Callback = notify_callback;
    params.Context = &record;

    ret = PowerRegisterSuspendResumeNotification(0, &params, &handle);
    ok(ret == ERROR_INVALID_PARAMETER, "window flag returned %lu\n", ret);
    ret = PowerRegisterSuspendResumeNotification(0x10, &params, &handle);
    ok(ret == ERROR_INVALID_PARAMETER, "flag 0x10 returned %lu\n", ret);
    ret = PowerRegisterSuspendResumeNotification(DEVICE_NOTIFY_CALLBACK, &params, &handle);
    ok(ret == ERROR_SUCCESS, "PowerRegisterSuspendResumeNotification returned %lu\n", ret);
    ok(handle != NULL, "no registration handle\n");
    ok(WaitForSingleObject(record.Event, 300) == WAIT_TIMEOUT, "a suspend/resume registration was notified at once\n");
    if (ret == ERROR_SUCCESS)
    {
        ret = PowerUnregisterSuspendResumeNotification(handle);
        ok(ret == ERROR_SUCCESS, "PowerUnregisterSuspendResumeNotification returned %lu\n", ret);
        ret = PowerUnregisterSuspendResumeNotification(handle);
        ok(ret == ERROR_INVALID_PARAMETER, "second PowerUnregisterSuspendResumeNotification returned %lu\n", ret);
    }
    CloseHandle(record.Event);
}

START_TEST(PowerNotification)
{
    test_arguments();
    test_power_source();
    test_personality();
    test_scheme_setting();
    test_unknown_setting();
    test_two_registrations();
    test_suspend_resume();
}
