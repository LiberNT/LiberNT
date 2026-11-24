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
static const GUID absent_subgroup = {0x8e15b6d3, 0x7a42, 0x4c91, {0xa8, 0x5b, 0x3d, 0x60, 0xf2, 0x1e, 0xc7, 0x94}};
static const GUID absent_setting = {0x4d8a2f17, 0xc96b, 0x4e53, {0xa0, 0x7e, 0x92, 0x3b, 0x18, 0x6c, 0xf4, 0x05}};
static const GUID no_subgroup = {0xfea3413e, 0x7e05, 0x4911, {0x9a, 0x71, 0x70, 0x03, 0x31, 0xf1, 0xc2, 0x94}};

#define SETTINGS L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerSettings"
#define TEST_SUBGROUP L"2f6c0d51-8a3e-4c27-b15d-6e90471ac382"
#define TEST_SETTING L"9b41e7c2-35d8-4f0a-862e-1c5fa703d964"
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
    ok(PowerCreateSetting(NULL, NULL, &test_setting) == ERROR_INVALID_PARAMETER, "PowerCreateSetting(NULL subgroup) succeeded\n");
    ok(PowerRemovePowerSetting(NULL, &test_setting) == ERROR_INVALID_PARAMETER, "PowerRemovePowerSetting(NULL subgroup) succeeded\n");
}

static void test_written(void)
{
    BOOL present;
    DWORD ret, value;

    ret = PowerCreateSetting(NULL, &test_subgroup, &test_setting);
    ok(ret == ERROR_SUCCESS, "PowerCreateSetting returned %lu\n", ret);
    ok(READ(&test_subgroup, &test_setting) == 0, "new setting returned %lu\n", READ(&test_subgroup, &test_setting));

    ret = PowerWriteSettingAttributes(&test_subgroup, &test_setting, POWER_ATTRIBUTE_HIDE);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(setting) returned %lu\n", ret);
    value = registry_attributes(SETTINGS L"\\" TEST_SUBGROUP L"\\" TEST_SETTING, &present);
    ok(present && value == POWER_ATTRIBUTE_HIDE, "setting key Attributes present %d value %lu\n", present, value);
    ok(READ(&test_subgroup, &test_setting) == POWER_ATTRIBUTE_HIDE, "hidden setting returned %lu\n", READ(&test_subgroup, &test_setting));
    ok(READ(&test_subgroup, NULL) == 0, "subgroup without attributes returned %lu\n", READ(&test_subgroup, NULL));

    ret = PowerWriteSettingAttributes(&test_subgroup, NULL, POWER_ATTRIBUTE_SHOW_AOAC);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(subgroup) returned %lu\n", ret);
    value = registry_attributes(SETTINGS L"\\" TEST_SUBGROUP, &present);
    ok(present && value == POWER_ATTRIBUTE_SHOW_AOAC, "subgroup key Attributes present %d value %lu\n", present, value);
    ok(READ(&test_subgroup, NULL) == POWER_ATTRIBUTE_SHOW_AOAC, "subgroup returned %lu\n", READ(&test_subgroup, NULL));
    ok(READ(&test_subgroup, &test_setting) == (POWER_ATTRIBUTE_HIDE | POWER_ATTRIBUTE_SHOW_AOAC),
       "subgroup and setting returned %lu\n", READ(&test_subgroup, &test_setting));
    ok(READ(&test_subgroup, &absent_setting) == 0, "subgroup and absent setting returned %lu\n", READ(&test_subgroup, &absent_setting));
    ok(READ(&absent_subgroup, &test_setting) == 0, "absent subgroup with the setting returned %lu\n", READ(&absent_subgroup, &test_setting));

    ret = PowerWriteSettingAttributes(&test_subgroup, NULL, 0);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(subgroup 0) returned %lu\n", ret);
    value = POWER_ATTRIBUTE_SHOW_AOAC;
    ret = RegSetKeyValueW(HKEY_LOCAL_MACHINE, SETTINGS L"\\" TEST_SUBGROUP L"\\" TEST_SETTING, L"Attributes", REG_DWORD, &value, sizeof(value));
    ok(ret == ERROR_SUCCESS, "RegSetKeyValueW returned %lu\n", ret);
    ok(READ(&test_subgroup, &test_setting) == POWER_ATTRIBUTE_SHOW_AOAC, "registry-written setting returned %lu\n",
       READ(&test_subgroup, &test_setting));

    ret = PowerWriteSettingAttributes(&test_subgroup, &test_setting, 0x10);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(0x10) returned %lu\n", ret);
    ok(READ(&test_subgroup, &test_setting) == 0x10, "undefined attribute bit returned %lu\n", READ(&test_subgroup, &test_setting));

    ret = PowerWriteSettingAttributes(NULL, &test_setting, POWER_ATTRIBUTE_SHOW_AOAC);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(NULL subgroup) returned %lu\n", ret);
    value = registry_attributes(SETTINGS L"\\" TEST_SETTING, &present);
    ok(present && value == POWER_ATTRIBUTE_SHOW_AOAC, "root setting key Attributes present %d value %lu\n", present, value);
    ok(READ(&test_subgroup, &test_setting) == 0x10, "subgroup setting returned %lu next to a root one\n", READ(&test_subgroup, &test_setting));
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
    registry_attributes(SETTINGS L"\\" ABSENT_SUBGROUP L"\\4d8a2f17-c96b-4e53-a07e-923b186cf405", &present);
    ok(!present, "removed setting key Attributes still present\n");
}

static void test_root(void)
{
    BOOL present;
    DWORD original, ret;

    original = registry_attributes(SETTINGS, &present);
    ret = PowerWriteSettingAttributes(NULL, NULL, original ^ POWER_ATTRIBUTE_SHOW_AOAC);
    ok(ret == ERROR_SUCCESS, "PowerWriteSettingAttributes(NULL, NULL) returned %lu\n", ret);
    ok(READ(&no_subgroup, NULL) == (original ^ POWER_ATTRIBUTE_SHOW_AOAC), "NO_SUBGROUP_GUID returned %lu\n", READ(&no_subgroup, NULL));
    if (present)
        PowerWriteSettingAttributes(NULL, NULL, original);
    else
        RegDeleteKeyValueW(HKEY_LOCAL_MACHINE, SETTINGS, L"Attributes");
}

static void test_builtin(void)
{
    static const GUID subgroups[] =
    {
        {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}},
        {0x7516b95f, 0xf776, 0x4464, {0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99}},
        {0x238c9fa8, 0x0aad, 0x41ed, {0x83, 0xf4, 0x97, 0xbe, 0x24, 0x2c, 0x8f, 0x20}},
    };
    static const struct
    {
        UINT subgroup;
        GUID setting;
        DWORD attributes;
    } settings[] =
    {
        {0, {0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c}}, POWER_ATTRIBUTE_SHOW_AOAC},
        {0, {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}}, POWER_ATTRIBUTE_SHOW_AOAC},
        {0, {0x8baa4a8a, 0x14c6, 0x4451, {0x8e, 0x8b, 0x14, 0xbd, 0xbd, 0x19, 0x75, 0x37}}, POWER_ATTRIBUTE_HIDE},
        {0, {0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63}}, POWER_ATTRIBUTE_HIDE},
        {1, {0x3c0bc021, 0xc8a8, 0x4e07, {0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e}}, POWER_ATTRIBUTE_SHOW_AOAC},
        {2, {0x29f6c1db, 0x86da, 0x48c5, {0x9f, 0xdb, 0xf2, 0xb6, 0x7b, 0x1f, 0x44, 0xda}}, POWER_ATTRIBUTE_SHOW_AOAC},
        {2, {0x9d7815a6, 0x7ee4, 0x497e, {0x88, 0x88, 0x51, 0x5a, 0x05, 0xf0, 0x23, 0x64}}, 0},
    };
    UINT i;

    for (i = 0; i < ARRAYSIZE(subgroups); i++)
        ok(READ(&subgroups[i], NULL) == 0, "subgroup %u returned %lu\n", i, READ(&subgroups[i], NULL));
    for (i = 0; i < ARRAYSIZE(settings); i++)
    {
        ok(READ(&subgroups[settings[i].subgroup], &settings[i].setting) == settings[i].attributes, "setting %u returned %lu\n", i,
           READ(&subgroups[settings[i].subgroup], &settings[i].setting));
    }
}

static void test_installed(void)
{
    GUID subgroup, setting;
    DWORD size, subgroup_attributes, both;
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
            both = READ(&subgroup, &setting);
            ok((both & subgroup_attributes) == subgroup_attributes, "subgroup %lu setting %lu returned %lu without the subgroup's %lu\n",
               subgroup_index, setting_index, both, subgroup_attributes);
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
    test_builtin();
    test_installed();
}
