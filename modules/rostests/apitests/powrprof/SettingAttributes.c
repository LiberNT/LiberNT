/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for PowerReadSettingAttributes and PowerWriteSettingAttributes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <powrprof.h>
#include <powersetting.h>

static const GUID test_subgroup = {0x2f6c0d51, 0x8a3e, 0x4c27, {0xb1, 0x5d, 0x6e, 0x90, 0x47, 0x1a, 0xc3, 0x82}};
static const GUID test_setting = {0x9b41e7c2, 0x35d8, 0x4f0a, {0x86, 0x2e, 0x1c, 0x5f, 0xa7, 0x03, 0xd9, 0x64}};
static const GUID other_subgroup = {0x61e0c3b8, 0x4f27, 0x4a9d, {0x9c, 0x13, 0x58, 0xb2, 0x7e, 0x0f, 0xa4, 0x39}};
static const GUID other_setting = {0xc72d95a4, 0x1b6e, 0x4d08, {0xb3, 0x4f, 0x0e, 0x81, 0x26, 0xd5, 0x9a, 0x7c}};
static const GUID absent_subgroup = {0x8e15b6d3, 0x7a42, 0x4c91, {0xa8, 0x5b, 0x3d, 0x60, 0xf2, 0x1e, 0xc7, 0x94}};
static const GUID absent_setting = {0x4d8a2f17, 0xc96b, 0x4e53, {0xa0, 0x7e, 0x92, 0x3b, 0x18, 0x6c, 0xf4, 0x05}};
static const GUID no_subgroup = {0xfea3413e, 0x7e05, 0x4911, {0x9a, 0x71, 0x70, 0x03, 0x31, 0xf1, 0xc2, 0x94}};

#define SETTINGS L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerSettings"
#define TEST_SUBGROUP L"2f6c0d51-8a3e-4c27-b15d-6e90471ac382"
#define TEST_SETTING L"9b41e7c2-35d8-4f0a-862e-1c5fa703d964"
#define OTHER_SUBGROUP L"61e0c3b8-4f27-4a9d-9c13-58b27e0fa439"
#define ABSENT_SUBGROUP L"8e15b6d3-7a42-4c91-a85b-3d60f21ec794"

#define READ(subgroup, setting) PowerReadSettingAttributes(subgroup, setting)

static DWORD registry_attributes(LPCWSTR path, BOOL *present)
{
    DWORD value = 0, type = REG_NONE, size = sizeof(value);
    LSTATUS error;

    error = RegGetValueW(HKEY_LOCAL_MACHINE, path, L"Attributes", RRF_RT_ANY, &type, &value, &size);
    *present = error == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(value);
    return *present ? value : 0;
}

static void remove_test_keys(void)
{
    static const WCHAR *keys[] =
    {
        SETTINGS L"\\" TEST_SUBGROUP,
        SETTINGS L"\\" OTHER_SUBGROUP,
        SETTINGS L"\\" ABSENT_SUBGROUP,
        SETTINGS L"\\" TEST_SETTING,
    };
    UINT i;

    for (i = 0; i < ARRAYSIZE(keys); i++)
    {
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, keys[i]);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, keys[i]);
    }
}

static void test_absent(void)
{
    ok(READ(&test_subgroup, &test_setting) == 0, "absent subgroup and setting returned %lu\n", READ(&test_subgroup, &test_setting));
    ok(READ(&test_subgroup, NULL) == 0, "absent subgroup returned %lu\n", READ(&test_subgroup, NULL));
    ok(READ(NULL, &absent_setting) == 0, "absent setting returned %lu\n", READ(NULL, &absent_setting));
    ok(PowerCreateSetting(NULL, NULL, &test_setting) == ERROR_INVALID_PARAMETER, "PowerCreateSetting(NULL subgroup) succeeded\n");
    ok(PowerRemovePowerSetting(NULL, &test_setting) == ERROR_INVALID_PARAMETER, "PowerRemovePowerSetting(NULL subgroup) succeeded\n");
}

static void test_written(void)
{
    BOOL present;
    DWORD ret, value;

    ret = PowerCreateSetting(NULL, &test_subgroup, &test_setting);
    ok(ret == ERROR_SUCCESS, "PowerCreateSetting returned %lu\n", ret);
    ret = PowerCreateSetting(NULL, &other_subgroup, &other_setting);
    ok(ret == ERROR_SUCCESS, "PowerCreateSetting(other) returned %lu\n", ret);
    ok(READ(&test_subgroup, &test_setting) == 0, "new setting returned %lu\n", READ(&test_subgroup, &test_setting));

    ret = PowerWriteSettingAttributes(&test_subgroup, &test_setting, POWER_ATTRIBUTE_HIDE);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(setting) returned %lu\n", ret);
    value = registry_attributes(SETTINGS L"\\" TEST_SUBGROUP L"\\" TEST_SETTING, &present);
    ok(present && value == POWER_ATTRIBUTE_HIDE, "setting key Attributes present %d value %lu\n", present, value);
    ok(READ(&test_subgroup, &test_setting) == POWER_ATTRIBUTE_HIDE, "hidden setting returned %lu\n", READ(&test_subgroup, &test_setting));
    ok(READ(&test_subgroup, NULL) == 0, "subgroup without attributes returned %lu\n", READ(&test_subgroup, NULL));
    ok(READ(NULL, &test_setting) == POWER_ATTRIBUTE_HIDE, "NULL subgroup returned %lu\n", READ(NULL, &test_setting));
    ok(READ(&no_subgroup, &test_setting) == POWER_ATTRIBUTE_HIDE, "NO_SUBGROUP_GUID returned %lu\n", READ(&no_subgroup, &test_setting));

    ret = PowerWriteSettingAttributes(&test_subgroup, NULL, POWER_ATTRIBUTE_SHOW_AOAC);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(subgroup) returned %lu\n", ret);
    value = registry_attributes(SETTINGS L"\\" TEST_SUBGROUP, &present);
    ok(present && value == POWER_ATTRIBUTE_SHOW_AOAC, "subgroup key Attributes present %d value %lu\n", present, value);
    ok(READ(&test_subgroup, NULL) == POWER_ATTRIBUTE_SHOW_AOAC, "subgroup returned %lu\n", READ(&test_subgroup, NULL));
    ok(READ(&test_subgroup, &test_setting) == (POWER_ATTRIBUTE_HIDE | POWER_ATTRIBUTE_SHOW_AOAC),
       "subgroup and setting returned %lu\n", READ(&test_subgroup, &test_setting));
    ok(READ(&test_subgroup, &absent_setting) == 0, "subgroup and absent setting returned %lu\n", READ(&test_subgroup, &absent_setting));
    ok(READ(NULL, &test_setting) == POWER_ATTRIBUTE_HIDE, "NULL subgroup took the subgroup attributes: %lu\n", READ(NULL, &test_setting));

    ret = PowerWriteSettingAttributes(&other_subgroup, NULL, POWER_ATTRIBUTE_HIDE);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(other subgroup) returned %lu\n", ret);
    ret = PowerWriteSettingAttributes(&test_subgroup, NULL, 0);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(subgroup 0) returned %lu\n", ret);
    ret = PowerWriteSettingAttributes(&test_subgroup, &test_setting, POWER_ATTRIBUTE_SHOW_AOAC);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(setting 2) returned %lu\n", ret);
    ok(READ(&other_subgroup, &test_setting) == (POWER_ATTRIBUTE_HIDE | POWER_ATTRIBUTE_SHOW_AOAC),
       "other subgroup with the setting returned %lu\n", READ(&other_subgroup, &test_setting));
    ok(READ(&absent_subgroup, &test_setting) == 0, "absent subgroup with the setting returned %lu\n", READ(&absent_subgroup, &test_setting));
    ok(READ(&test_subgroup, &other_setting) == 0, "setting of another subgroup returned %lu\n", READ(&test_subgroup, &other_setting));

    value = POWER_ATTRIBUTE_HIDE;
    ret = RegSetKeyValueW(HKEY_LOCAL_MACHINE, SETTINGS L"\\" TEST_SUBGROUP L"\\" TEST_SETTING, L"Attributes", REG_DWORD, &value, sizeof(value));
    ok(ret == ERROR_SUCCESS, "RegSetKeyValueW returned %lu\n", ret);
    ok(READ(&test_subgroup, &test_setting) == POWER_ATTRIBUTE_HIDE, "registry-written setting returned %lu\n", READ(&test_subgroup, &test_setting));

    ret = PowerWriteSettingAttributes(&test_subgroup, &test_setting, 0x10);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(0x10) returned %lu\n", ret);
    ok(READ(&test_subgroup, &test_setting) == 0x10, "undefined attribute bit returned %lu\n", READ(&test_subgroup, &test_setting));

    ret = PowerWriteSettingAttributes(NULL, &test_setting, POWER_ATTRIBUTE_SHOW_AOAC);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(NULL subgroup) returned %lu\n", ret);
    value = registry_attributes(SETTINGS L"\\" TEST_SETTING, &present);
    ok(present && value == POWER_ATTRIBUTE_SHOW_AOAC, "root setting key Attributes present %d value %lu\n", present, value);
    ok(READ(&test_subgroup, &test_setting) == 0x10, "subgroup setting returned %lu next to a root one\n", READ(&test_subgroup, &test_setting));
    ok(READ(&no_subgroup, &test_setting) == 0x10, "root setting returned %lu\n", READ(&no_subgroup, &test_setting));
    ret = PowerWriteSettingAttributes(&other_subgroup, &test_setting, POWER_ATTRIBUTE_HIDE);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(other subgroup setting) returned %lu\n", ret);
    ok(READ(&other_subgroup, &test_setting) == (POWER_ATTRIBUTE_HIDE | 0x10), "later subgroup holding the setting returned %lu\n",
       READ(&other_subgroup, &test_setting));
    ret = PowerRemovePowerSetting(&other_subgroup, &test_setting);
    ok(ret == ERROR_SUCCESS, "PowerRemovePowerSetting(other subgroup setting) returned %lu\n", ret);
    ret = PowerRemovePowerSetting(&no_subgroup, &test_setting);
    ok(ret == ERROR_SUCCESS, "PowerRemovePowerSetting(root setting) returned %lu\n", ret);
    registry_attributes(SETTINGS L"\\" TEST_SETTING, &present);
    ok(!present, "root setting key Attributes still present\n");

    ret = PowerWriteSettingAttributes(&absent_subgroup, &absent_setting, POWER_ATTRIBUTE_HIDE);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(absent) returned %lu\n", ret);
    ok(READ(&absent_subgroup, &absent_setting) == POWER_ATTRIBUTE_HIDE, "created setting returned %lu\n", READ(&absent_subgroup, &absent_setting));
    ok(READ(&absent_subgroup, NULL) == 0, "created subgroup returned %lu\n", READ(&absent_subgroup, NULL));
    ret = PowerRemovePowerSetting(&absent_subgroup, &absent_setting);
    ok(ret == ERROR_SUCCESS, "PowerRemovePowerSetting(created) returned %lu\n", ret);
    ok(READ(NULL, &absent_setting) == 0, "removed setting returned %lu\n", READ(NULL, &absent_setting));
}

static void test_root(void)
{
    BOOL present;
    DWORD original, ret;

    original = registry_attributes(SETTINGS, &present);
    ret = PowerWriteSettingAttributes(NULL, NULL, original ^ POWER_ATTRIBUTE_SHOW_AOAC);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(NULL, NULL) returned %lu\n", ret);
    ok(READ(&no_subgroup, NULL) == (original ^ POWER_ATTRIBUTE_SHOW_AOAC), "NO_SUBGROUP_GUID returned %lu\n", READ(&no_subgroup, NULL));
    ok(READ(NULL, &other_setting) == 0, "NULL subgroup took the root attributes: %lu\n", READ(NULL, &other_setting));
    if (present)
        PowerWriteSettingAttributes(NULL, NULL, original);
    else
        RegDeleteKeyValueW(HKEY_LOCAL_MACHINE, SETTINGS, L"Attributes");
}

static void test_installed(void)
{
    GUID subgroup, setting;
    DWORD size, subgroup_attributes, setting_attributes, both;
    ULONG subgroup_index, setting_index;
    UINT checked = 0, hidden = 0;

    for (subgroup_index = 0; subgroup_index < 64; subgroup_index++)
    {
        size = sizeof(subgroup);
        if (PowerEnumerate(NULL, NULL, NULL, ACCESS_SUBGROUP, subgroup_index, (UCHAR *)&subgroup, &size) != ERROR_SUCCESS)
            break;
        subgroup_attributes = READ(&subgroup, NULL);
        for (setting_index = 0; setting_index < 256; setting_index++)
        {
            size = sizeof(setting);
            if (PowerEnumerate(NULL, NULL, &subgroup, ACCESS_INDIVIDUAL_SETTING, setting_index, (UCHAR *)&setting, &size) != ERROR_SUCCESS)
                break;
            setting_attributes = READ(NULL, &setting);
            both = READ(&subgroup, &setting);
            ok(both == (subgroup_attributes | setting_attributes), "subgroup %lu setting %lu returned %lu, expected %lu | %lu\n",
               subgroup_index, setting_index, both, subgroup_attributes, setting_attributes);
            if (both & POWER_ATTRIBUTE_HIDE)
                hidden++;
            checked++;
        }
    }
    ok(checked != 0, "no installed setting enumerated\n");
    trace("%u installed settings checked, %u hidden\n", checked, hidden);
}

START_TEST(SettingAttributes)
{
    remove_test_keys();
    test_absent();
    test_written();
    test_root();
    remove_test_keys();
    test_installed();
}
