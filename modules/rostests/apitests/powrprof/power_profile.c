/*
 * PROJECT:     LiberNT powrprof API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Modern processor power-scheme and telemetry parity tests
 */

#include <apitest.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <powrprof.h>
#include <powersetting.h>

static const GUID balanced_scheme = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
static const GUID power_saver_scheme = {0xa1841308, 0x3541, 0x4fab, {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};
static const GUID high_performance_scheme = {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
static const GUID processor_subgroup = {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
static const GUID processor_minimum = {0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c}};
static const GUID processor_maximum = {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}};
static const GUID processor_autonomous = {0x8baa4a8a, 0x14c6, 0x4451, {0x8e, 0x8b, 0x14, 0xbd, 0xbd, 0x19, 0x75, 0x37}};
static const GUID processor_epp = {0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63}};
static const GUID video_subgroup = {0x7516b95f, 0xf776, 0x4464, {0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99}};
static const GUID video_powerdown_timeout = {0x3c0bc021, 0xc8a8, 0x4e07, {0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e}};
static const GUID sleep_subgroup = {0x238c9fa8, 0x0aad, 0x41ed, {0x83, 0xf4, 0x97, 0xbe, 0x24, 0x2c, 0x8f, 0x20}};
static const GUID standby_timeout = {0x29f6c1db, 0x86da, 0x48c5, {0x9f, 0xdb, 0xf2, 0xb6, 0x7b, 0x1f, 0x44, 0xda}};
static const GUID hibernate_timeout = {0x9d7815a6, 0x7ee4, 0x497e, {0x88, 0x88, 0x51, 0x5a, 0x05, 0xf0, 0x23, 0x64}};
static const GUID disk_subgroup = {0x0012ee47, 0x9041, 0x4b5d, {0x9b, 0x77, 0x53, 0x5f, 0xba, 0x8b, 0x14, 0x42}};
static const GUID disk_powerdown_timeout = {0x6738e2c4, 0xe8a5, 0x4a42, {0xb1, 0x6a, 0xe0, 0x40, 0xe7, 0x69, 0x75, 0x6e}};
static const GUID unknown_guid = {0xdeadbeef, 0x1234, 0x5678, {0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78}};

typedef struct
{
    DWORD ac_minimum;
    DWORD dc_minimum;
    DWORD ac_maximum;
    DWORD dc_maximum;
    DWORD ac_autonomous;
    DWORD dc_autonomous;
    DWORD ac_epp;
    DWORD dc_epp;
} SCHEME_EXPECTED_VALUES;

static const SCHEME_EXPECTED_VALUES balanced_expected = {5, 5, 100, 100, 0, 0, 33, 50};
static const SCHEME_EXPECTED_VALUES power_saver_expected = {5, 5, 100, 100, 0, 0, 60, 60};
static const SCHEME_EXPECTED_VALUES high_performance_expected = {100, 5, 100, 100, 0, 0, 0, 0};

typedef struct
{
    const char *name;
    GUID setting;
    DWORD values[3][2];
} PROCESSOR_POLICY_EXPECTED;

/* Windows 11 defaults for Balanced, Power saver and High performance, on AC then on DC */
static const PROCESSOR_POLICY_EXPECTED processor_policy_expected[] =
{
    {"increase threshold", {0x06cadf0e, 0x64ed, 0x448a, {0x89, 0x27, 0xce, 0x7b, 0xf9, 0x0e, 0xb3, 0x5d}}, {{60, 90}, {90, 90}, {30, 30}}},
    {"decrease threshold", {0x12a0ab44, 0xfe28, 0x4fa9, {0xb3, 0xbd, 0x4b, 0x64, 0xf4, 0x49, 0x60, 0xa6}}, {{20, 30}, {60, 60}, {10, 10}}},
    {"increase policy", {0x465e1f50, 0xb610, 0x473a, {0xab, 0x58, 0x00, 0xd1, 0x07, 0x7d, 0xc4, 0x18}}, {{3, 3}, {1, 1}, {2, 2}}},
    {"decrease policy", {0x40fbefc7, 0x2e9d, 0x4d25, {0xa1, 0x85, 0x0c, 0xfd, 0x85, 0x74, 0xba, 0xc6}}, {{0, 0}, {2, 2}, {1, 1}}},
    {"increase time", {0x984cf492, 0x3bed, 0x4488, {0xa8, 0xf9, 0x42, 0x86, 0xc9, 0x7b, 0xf5, 0xaa}}, {{1, 1}, {3, 3}, {1, 1}}},
    {"decrease time", {0xd8edeb9b, 0x95cf, 0x4f95, {0xa7, 0x3c, 0xb0, 0x61, 0x97, 0x36, 0x93, 0xc8}}, {{1, 1}, {1, 1}, {1, 1}}},
    {"time check", {0x4d2b0152, 0x7d5c, 0x498b, {0x88, 0xe2, 0x34, 0x34, 0x53, 0x92, 0xa2, 0xc5}}, {{30, 30}, {200, 200}, {15, 15}}},
    {"boost policy", {0x45bcc044, 0xd885, 0x43e2, {0x86, 0x05, 0xee, 0x0e, 0xc6, 0xe9, 0x6b, 0x59}}, {{60, 40}, {0, 0}, {100, 100}}},
    {"boost mode", {0xbe337238, 0x0d82, 0x4146, {0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7}}, {{2, 2}, {2, 2}, {2, 2}}},
    {"increase threshold 1", {0x06cadf0e, 0x64ed, 0x448a, {0x89, 0x27, 0xce, 0x7b, 0xf9, 0x0e, 0xb3, 0x5e}}, {{60, 90}, {60, 90}, {60, 90}}},
    {"decrease threshold 1", {0x12a0ab44, 0xfe28, 0x4fa9, {0xb3, 0xbd, 0x4b, 0x64, 0xf4, 0x49, 0x60, 0xa7}}, {{20, 30}, {20, 30}, {20, 30}}},
    {"increase policy 1", {0x465e1f50, 0xb610, 0x473a, {0xab, 0x58, 0x0, 0xd1, 0x7, 0x7d, 0xc4, 0x19}}, {{3, 3}, {3, 3}, {3, 3}}},
    {"decrease policy 1", {0x40fbefc7, 0x2e9d, 0x4d25, {0xa1, 0x85, 0xc, 0xfd, 0x85, 0x74, 0xba, 0xc7}}, {{0, 0}, {0, 0}, {0, 0}}},
    {"increase time 1", {0x984cf492, 0x3bed, 0x4488, {0xa8, 0xf9, 0x42, 0x86, 0xc9, 0x7b, 0xf5, 0xab}}, {{1, 1}, {3, 3}, {1, 1}}},
    {"decrease time 1", {0xd8edeb9b, 0x95cf, 0x4f95, {0xa7, 0x3c, 0xb0, 0x61, 0x97, 0x36, 0x93, 0xc9}}, {{2, 2}, {2, 2}, {2, 2}}},
    {"minimum 1", {0x893DEE8E, 0x2BEF, 0x41E0, {0x89, 0xC6, 0xB5, 0x5D, 0x09, 0x29, 0x96, 0x4D}}, {{5, 5}, {5, 5}, {100, 5}}},
    {"maximum 1", {0xBC5038F7, 0x23E0, 0x4960, {0x96, 0xDA, 0x33, 0xAB, 0xAF, 0x59, 0x35, 0xED}}, {{100, 100}, {75, 75}, {100, 100}}},
    {"epp 1", {0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x64}}, {{33, 50}, {60, 60}, {0, 0}}},
    {"frequency limit", {0x75b0ae3f, 0xbce0, 0x45a7, {0x8c, 0x89, 0xc9, 0x61, 0x1c, 0x25, 0xe1, 0x00}}, {{0, 0}, {0, 0}, {0, 0}}},
    {"frequency limit 1", {0x75b0ae3f, 0xbce0, 0x45a7, {0x8c, 0x89, 0xc9, 0x61, 0x1c, 0x25, 0xe1, 0x01}}, {{0, 0}, {0, 0}, {0, 0}}},
    {"parking min cores", {0x0cc5b647, 0xc1df, 0x4637, {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83}}, {{10, 10}, {10, 10}, {100, 100}}},
    {"parking min cores 1", {0x0cc5b647, 0xc1df, 0x4637, {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x84}}, {{0, 0}, {0, 0}, {0, 0}}},
    {"parking max cores", {0xea062031, 0x0e34, 0x4ff1, {0x9b, 0x6d, 0xeb, 0x10, 0x59, 0x33, 0x40, 0x28}}, {{100, 100}, {100, 100}, {100, 100}}},
    {"parking max cores 1", {0xea062031, 0x0e34, 0x4ff1, {0x9b, 0x6d, 0xeb, 0x10, 0x59, 0x33, 0x40, 0x29}}, {{100, 100}, {100, 100}, {100, 100}}},
    {"parking increase time", {0x2ddd5a84, 0x5a71, 0x437e, {0x91, 0x2a, 0xdb, 0x0b, 0x8c, 0x78, 0x87, 0x32}}, {{1, 1}, {1, 1}, {7, 7}}},
    {"parking decrease time", {0xdfd10d17, 0xd5eb, 0x45dd, {0x87, 0x7a, 0x9a, 0x34, 0xdd, 0xd1, 0x5c, 0x82}}, {{10, 10}, {2, 2}, {20, 20}}},
    {"parking increase policy", {0xc7be0679, 0x2817, 0x4d69, {0x9d, 0x02, 0x51, 0x9a, 0x53, 0x7e, 0xd0, 0xc6}}, {{0, 0}, {0, 0}, {0, 0}}},
    {"parking decrease policy", {0x71021b41, 0xc749, 0x4d21, {0xbe, 0x74, 0xa0, 0x0f, 0x33, 0x5d, 0x58, 0x2b}}, {{0, 0}, {0, 0}, {0, 0}}},
    {"parking concurrency", {0x2430ab6f, 0xa520, 0x44a2, {0x96, 0x01, 0xf7, 0xf2, 0x3b, 0x51, 0x34, 0xb1}}, {{97, 97}, {97, 97}, {97, 97}}},
    {"parking headroom", {0xf735a673, 0x2066, 0x4f80, {0xa0, 0xc5, 0xdd, 0xee, 0x0c, 0xf1, 0xbf, 0x5d}}, {{20, 20}, {20, 20}, {20, 20}}},
    {"parking over utilization", {0x943c8cb6, 0x6f93, 0x4227, {0xad, 0x87, 0xe9, 0xa3, 0xfe, 0xec, 0x08, 0xd1}}, {{85, 85}, {90, 90}, {60, 60}}},
};

/* Windows 11 defaults of the display and sleep timeouts, in seconds, in the same order */
static const DWORD timeout_expected[2][3][2] =
{
    {{600, 300}, {300, 120}, {900, 600}},
    {{1800, 900}, {900, 600}, {0, 0}},
};

static void test_processor_policy_defaults(void)
{
    const GUID *schemes[3] = {&balanced_scheme, &power_saver_scheme, &high_performance_scheme};
    DWORD value, ret;
    UINT scheme, index;

    for (scheme = 0; scheme < 3; scheme++)
    {
        for (index = 0; index < ARRAYSIZE(processor_policy_expected); index++)
        {
            value = 0xdeadbeef;
            ret = PowerReadACValueIndex(NULL, schemes[scheme], &processor_subgroup, &processor_policy_expected[index].setting, &value);
            ok(ret == ERROR_SUCCESS, "scheme %u AC %s returned %lu\n", scheme, processor_policy_expected[index].name, ret);
            ok(value == processor_policy_expected[index].values[scheme][0], "scheme %u AC %s is %lu, expected %lu\n",
               scheme, processor_policy_expected[index].name, value, processor_policy_expected[index].values[scheme][0]);

            value = 0xdeadbeef;
            ret = PowerReadDCValueIndex(NULL, schemes[scheme], &processor_subgroup, &processor_policy_expected[index].setting, &value);
            ok(ret == ERROR_SUCCESS, "scheme %u DC %s returned %lu\n", scheme, processor_policy_expected[index].name, ret);
            ok(value == processor_policy_expected[index].values[scheme][1], "scheme %u DC %s is %lu, expected %lu\n",
               scheme, processor_policy_expected[index].name, value, processor_policy_expected[index].values[scheme][1]);
        }
    }
}

static void test_timeout_defaults(void)
{
    const GUID *schemes[3] = {&balanced_scheme, &power_saver_scheme, &high_performance_scheme};
    const GUID *subgroups[2] = {&video_subgroup, &sleep_subgroup};
    const GUID *settings[2] = {&video_powerdown_timeout, &standby_timeout};
    DWORD ac, dc, ret;
    UINT setting, scheme;

    for (setting = 0; setting < 2; setting++)
    {
        for (scheme = 0; scheme < 3; scheme++)
        {
            ac = dc = 0xdeadbeef;
            ret = PowerReadACValueIndex(NULL, schemes[scheme], subgroups[setting], settings[setting], &ac);
            ok(ret == ERROR_SUCCESS, "timeout %u scheme %u AC returned %lu\n", setting, scheme, ret);
            ret = PowerReadDCValueIndex(NULL, schemes[scheme], subgroups[setting], settings[setting], &dc);
            ok(ret == ERROR_SUCCESS, "timeout %u scheme %u DC returned %lu\n", setting, scheme, ret);
            ok(ac == timeout_expected[setting][scheme][0] && dc == timeout_expected[setting][scheme][1],
               "timeout %u scheme %u is %lu/%lu, expected %lu/%lu\n", setting, scheme, ac, dc,
               timeout_expected[setting][scheme][0], timeout_expected[setting][scheme][1]);
        }
    }
}

static void trace_guid(const char *name, const GUID *guid)
{
    trace("%s=%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\n", name, guid->Data1, guid->Data2, guid->Data3, guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
}

static void query_scheme_settings(const char *name, const GUID *scheme, const SCHEME_EXPECTED_VALUES *expected)
{
    DWORD ac_min = 0xdeadbeef, dc_min = 0xdeadbeef;
    DWORD ac_max = 0xdeadbeef, dc_max = 0xdeadbeef;
    DWORD ac_autonomous = 0xdeadbeef, dc_autonomous = 0xdeadbeef;
    DWORD ac_epp = 0xdeadbeef, dc_epp = 0xdeadbeef;
    DWORD ret_ac_min, ret_dc_min, ret_ac_max, ret_dc_max, ret_ac_autonomous, ret_dc_autonomous, ret_ac_epp, ret_dc_epp;

    ret_ac_min = PowerReadACValueIndex(NULL, scheme, &processor_subgroup, &processor_minimum, &ac_min);
    ret_dc_min = PowerReadDCValueIndex(NULL, scheme, &processor_subgroup, &processor_minimum, &dc_min);
    ret_ac_max = PowerReadACValueIndex(NULL, scheme, &processor_subgroup, &processor_maximum, &ac_max);
    ret_dc_max = PowerReadDCValueIndex(NULL, scheme, &processor_subgroup, &processor_maximum, &dc_max);
    ret_ac_autonomous = PowerReadACValueIndex(NULL, scheme, &processor_subgroup, &processor_autonomous, &ac_autonomous);
    ret_dc_autonomous = PowerReadDCValueIndex(NULL, scheme, &processor_subgroup, &processor_autonomous, &dc_autonomous);
    ret_ac_epp = PowerReadACValueIndex(NULL, scheme, &processor_subgroup, &processor_epp, &ac_epp);
    ret_dc_epp = PowerReadDCValueIndex(NULL, scheme, &processor_subgroup, &processor_epp, &dc_epp);

    ok(ret_ac_min == ERROR_SUCCESS, "%s AC minimum returned %lu\n", name, ret_ac_min);
    ok(ret_dc_min == ERROR_SUCCESS, "%s DC minimum returned %lu\n", name, ret_dc_min);
    ok(ret_ac_max == ERROR_SUCCESS, "%s AC maximum returned %lu\n", name, ret_ac_max);
    ok(ret_dc_max == ERROR_SUCCESS, "%s DC maximum returned %lu\n", name, ret_dc_max);
    ok(ret_ac_autonomous == ERROR_SUCCESS, "%s AC autonomous mode returned %lu\n", name, ret_ac_autonomous);
    ok(ret_dc_autonomous == ERROR_SUCCESS, "%s DC autonomous mode returned %lu\n", name, ret_dc_autonomous);
    ok(ret_ac_epp == ERROR_SUCCESS, "%s AC EPP returned %lu\n", name, ret_ac_epp);
    ok(ret_dc_epp == ERROR_SUCCESS, "%s DC EPP returned %lu\n", name, ret_dc_epp);
    trace("%s settings ac_min=%lu dc_min=%lu ac_max=%lu dc_max=%lu ac_auto=%lu dc_auto=%lu ac_epp=%lu dc_epp=%lu\n", name, ac_min, dc_min, ac_max, dc_max, ac_autonomous, dc_autonomous, ac_epp, dc_epp);

    if (ret_ac_min == ERROR_SUCCESS && ret_ac_max == ERROR_SUCCESS)
    {
        ok(ac_min <= 100, "%s AC minimum %lu exceeds 100\n", name, ac_min);
        ok(ac_max <= 100, "%s AC maximum %lu exceeds 100\n", name, ac_max);
        ok(ac_min <= ac_max, "%s AC minimum %lu exceeds maximum %lu\n", name, ac_min, ac_max);
    }
    if (ret_dc_min == ERROR_SUCCESS && ret_dc_max == ERROR_SUCCESS)
    {
        ok(dc_min <= 100, "%s DC minimum %lu exceeds 100\n", name, dc_min);
        ok(dc_max <= 100, "%s DC maximum %lu exceeds 100\n", name, dc_max);
        ok(dc_min <= dc_max, "%s DC minimum %lu exceeds maximum %lu\n", name, dc_min, dc_max);
    }
    if (expected)
    {
        if (ret_ac_min == ERROR_SUCCESS)
            ok(ac_min == expected->ac_minimum, "%s AC minimum is %lu, expected %lu\n", name, ac_min, expected->ac_minimum);
        if (ret_dc_min == ERROR_SUCCESS)
            ok(dc_min == expected->dc_minimum, "%s DC minimum is %lu, expected %lu\n", name, dc_min, expected->dc_minimum);
        if (ret_ac_max == ERROR_SUCCESS)
            ok(ac_max == expected->ac_maximum, "%s AC maximum is %lu, expected %lu\n", name, ac_max, expected->ac_maximum);
        if (ret_dc_max == ERROR_SUCCESS)
            ok(dc_max == expected->dc_maximum, "%s DC maximum is %lu, expected %lu\n", name, dc_max, expected->dc_maximum);
        if (ret_ac_autonomous == ERROR_SUCCESS)
            ok(ac_autonomous == expected->ac_autonomous, "%s AC autonomous mode is %lu, expected %lu\n", name, ac_autonomous, expected->ac_autonomous);
        if (ret_dc_autonomous == ERROR_SUCCESS)
            ok(dc_autonomous == expected->dc_autonomous, "%s DC autonomous mode is %lu, expected %lu\n", name, dc_autonomous, expected->dc_autonomous);
        if (ret_ac_epp == ERROR_SUCCESS)
            ok(ac_epp == expected->ac_epp, "%s AC EPP is %lu, expected %lu\n", name, ac_epp, expected->ac_epp);
        if (ret_dc_epp == ERROR_SUCCESS)
            ok(dc_epp == expected->dc_epp, "%s DC EPP is %lu, expected %lu\n", name, dc_epp, expected->dc_epp);
    }
}

static void query_processor_information(const char *name)
{
    PROCESSOR_POWER_INFORMATION *info;
    SYSTEM_INFO system_info;
    NTSTATUS status;
    SIZE_T size;
    DWORD i;

    GetSystemInfo(&system_info);
    size = system_info.dwNumberOfProcessors * sizeof(*info);
    info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    ok(info != NULL, "%s processor information allocation failed\n", name);
    if (!info)
        return;

    status = CallNtPowerInformation(ProcessorInformation, NULL, 0, info, (ULONG)size);
    ok(status == STATUS_SUCCESS, "%s ProcessorInformation returned 0x%08lx\n", name, (ULONG)status);
    if (status == STATUS_SUCCESS)
    {
        for (i = 0; i < system_info.dwNumberOfProcessors; ++i)
        {
            trace("%s cpu=%lu max_mhz=%lu current_mhz=%lu limit_mhz=%lu max_idle=%lu current_idle=%lu\n", name, info[i].Number, info[i].MaxMhz, info[i].CurrentMhz, info[i].MhzLimit, info[i].MaxIdleState, info[i].CurrentIdleState);
            ok(info[i].Number == i, "%s processor %lu reported number %lu\n", name, i, info[i].Number);
            ok(info[i].MaxMhz != 0, "%s processor %lu reported zero MaxMhz\n", name, i);
            ok(info[i].MhzLimit != 0, "%s processor %lu reported zero MhzLimit\n", name, i);
            ok(info[i].MaxIdleState >= 1, "%s processor %lu reported no idle state\n", name, i);
            ok(info[i].CurrentIdleState >= 1 && info[i].CurrentIdleState <= info[i].MaxIdleState,
               "%s processor %lu reported idle state %lu of %lu\n", name, i, info[i].CurrentIdleState, info[i].MaxIdleState);
        }
    }
    HeapFree(GetProcessHeap(), 0, info);
}

static BOOL activate_and_query(const char *name, const GUID *scheme, const SCHEME_EXPECTED_VALUES *expected)
{
    GUID *active = NULL;
    DWORD ret;

    ret = PowerSetActiveScheme(NULL, scheme);
    if (ret == ERROR_FILE_NOT_FOUND || ret == ERROR_NOT_SUPPORTED)
    {
        win_skip("%s scheme is unavailable, error %lu\n", name, ret);
        return FALSE;
    }
    ok(ret == ERROR_SUCCESS, "PowerSetActiveScheme(%s) returned %lu\n", name, ret);
    if (ret != ERROR_SUCCESS)
        return FALSE;

    ret = PowerGetActiveScheme(NULL, &active);
    ok(ret == ERROR_SUCCESS, "PowerGetActiveScheme after %s returned %lu\n", name, ret);
    ok(active != NULL, "PowerGetActiveScheme after %s returned NULL\n", name);
    if (ret == ERROR_SUCCESS && active)
    {
        trace_guid(name, active);
        ok(IsEqualGUID(active, scheme), "%s did not become the active scheme\n", name);
    }
    LocalFree(active);
    query_scheme_settings(name, scheme, expected);
    query_processor_information(name);
    return TRUE;
}

static void test_argument_contract(void)
{
    GUID *active = (GUID *)0xdeadbeef;
    DWORD value = 0xdeadbeef;
    DWORD ret;

    ret = PowerSetActiveScheme(NULL, NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "PowerSetActiveScheme(NULL scheme) returned %lu\n", ret);
    ret = PowerReadACValueIndex(NULL, NULL, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_INVALID_PARAMETER, "PowerReadACValueIndex(NULL scheme) returned %lu\n", ret);
    value = 0xdeadbeef;
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, NULL, &processor_minimum, &value);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerReadACValueIndex(NULL subgroup) returned %lu\n", ret);
    value = 0xdeadbeef;
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &processor_subgroup, NULL, &value);
    ok(ret == ERROR_INVALID_PARAMETER, "PowerReadACValueIndex(NULL setting) returned %lu\n", ret);
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, NULL);
    ok(ret == ERROR_SUCCESS, "PowerReadACValueIndex(NULL output) returned %lu\n", ret);

    ret = PowerGetActiveScheme(HKEY_CURRENT_USER, &active);
    ok(ret == ERROR_SUCCESS, "PowerGetActiveScheme(non-NULL root) returned %lu\n", ret);
    ok(active != NULL, "PowerGetActiveScheme(non-NULL root) returned NULL\n");
    if (ret == ERROR_SUCCESS)
        LocalFree(active);
    ret = PowerSetActiveScheme(HKEY_CURRENT_USER, &balanced_scheme);
    ok(ret == ERROR_SUCCESS, "PowerSetActiveScheme(non-NULL root) returned %lu\n", ret);
    ret = PowerSetActiveScheme(NULL, &unknown_guid);
    ok(ret == ERROR_INVALID_PARAMETER, "PowerSetActiveScheme(unknown scheme) returned %lu\n", ret);

    ret = PowerReadACValueIndex(HKEY_CURRENT_USER, &balanced_scheme, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_SUCCESS, "PowerReadACValueIndex(non-NULL root) returned %lu\n", ret);
    ret = PowerReadACValueIndex(NULL, &unknown_guid, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerReadACValueIndex(unknown scheme) returned %lu\n", ret);
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &unknown_guid, &processor_minimum, &value);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerReadACValueIndex(unknown subgroup) returned %lu\n", ret);
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &unknown_guid, &value);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerReadACValueIndex(unknown setting) returned %lu\n", ret);
}

static void test_write_read_contract(void)
{
    DWORD original_ac, original_dc, value;
    DWORD test_ac, test_dc;
    DWORD ret;
    BOOL restore_ac = FALSE, restore_dc = FALSE;

    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, &original_ac);
    ok(ret == ERROR_SUCCESS, "reading original balanced AC minimum returned %lu\n", ret);
    if (ret != ERROR_SUCCESS)
        return;
    ret = PowerReadDCValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, &original_dc);
    ok(ret == ERROR_SUCCESS, "reading original balanced DC minimum returned %lu\n", ret);
    if (ret != ERROR_SUCCESS)
        return;

    test_ac = original_ac == 17 ? 18 : 17;
    test_dc = original_dc == 23 ? 24 : 23;
    ret = PowerWriteACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, test_ac);
    ok(ret == ERROR_SUCCESS, "PowerWriteACValueIndex(valid) returned %lu\n", ret);
    if (ret == ERROR_SUCCESS)
        restore_ac = TRUE;
    ret = PowerWriteDCValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, test_dc);
    ok(ret == ERROR_SUCCESS, "PowerWriteDCValueIndex(valid) returned %lu\n", ret);
    if (ret == ERROR_SUCCESS)
        restore_dc = TRUE;

    value = 0xdeadbeef;
    ret = PowerReadACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_SUCCESS, "PowerReadACValueIndex(after write) returned %lu\n", ret);
    ok(value == test_ac, "PowerReadACValueIndex(after write) returned %lu, expected %lu\n", value, test_ac);
    value = 0xdeadbeef;
    ret = PowerReadDCValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_SUCCESS, "PowerReadDCValueIndex(after write) returned %lu\n", ret);
    ok(value == test_dc, "PowerReadDCValueIndex(after write) returned %lu, expected %lu\n", value, test_dc);

    ret = PowerWriteACValueIndex(HKEY_CURRENT_USER, &balanced_scheme, &processor_subgroup, &processor_minimum, 10);
    ok(ret == ERROR_SUCCESS, "PowerWriteACValueIndex(non-NULL root) returned %lu\n", ret);
    ret = PowerWriteACValueIndex(NULL, &unknown_guid, &processor_subgroup, &processor_minimum, 10);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerWriteACValueIndex(unknown scheme) returned %lu\n", ret);
    ret = PowerWriteACValueIndex(NULL, &balanced_scheme, &unknown_guid, &processor_minimum, 10);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerWriteACValueIndex(unknown subgroup) returned %lu\n", ret);
    ret = PowerWriteACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &unknown_guid, 10);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerWriteACValueIndex(unknown setting) returned %lu\n", ret);
    ret = PowerWriteACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, 101);
    ok(ret == ERROR_INVALID_DATA, "PowerWriteACValueIndex(value 101) returned %lu\n", ret);

    if (restore_ac)
        ok(PowerWriteACValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, original_ac) == ERROR_SUCCESS, "restoring balanced AC minimum failed\n");
    if (restore_dc)
        ok(PowerWriteDCValueIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_minimum, original_dc) == ERROR_SUCCESS, "restoring balanced DC minimum failed\n");
}

typedef struct
{
    const char *name;
    const GUID *subgroup;
    const GUID *setting;
} LEGACY_SETTING;

static const LEGACY_SETTING legacy_settings[] =
{
    {"video", &video_subgroup, &video_powerdown_timeout},
    {"standby", &sleep_subgroup, &standby_timeout},
    {"hibernate", &sleep_subgroup, &hibernate_timeout},
    {"disk", &disk_subgroup, &disk_powerdown_timeout},
};

static void read_legacy_settings(const GUID *scheme, DWORD values[][2])
{
    UINT i;

    for (i = 0; i < ARRAYSIZE(legacy_settings); i++)
    {
        values[i][0] = values[i][1] = 0xdeadbeef;
        PowerReadACValueIndex(NULL, scheme, legacy_settings[i].subgroup, legacy_settings[i].setting, &values[i][0]);
        PowerReadDCValueIndex(NULL, scheme, legacy_settings[i].subgroup, legacy_settings[i].setting, &values[i][1]);
    }
}

static void trace_legacy_policy(const char *name, const POWER_POLICY *policy, DWORD values[][2])
{
    UINT i;

    trace("%s legacy video=%lu/%lu idle=%lu/%lu action=%u/%u spindown=%lu/%lu dozes4=%lu/%lu\n", name,
          policy->user.VideoTimeoutAc, policy->user.VideoTimeoutDc,
          policy->user.IdleTimeoutAc, policy->user.IdleTimeoutDc,
          policy->user.IdleAc.Action, policy->user.IdleDc.Action,
          policy->user.SpindownTimeoutAc, policy->user.SpindownTimeoutDc,
          policy->mach.DozeS4TimeoutAc, policy->mach.DozeS4TimeoutDc);
    for (i = 0; i < ARRAYSIZE(legacy_settings); i++)
        trace("%s scheme %s=%lu/%lu\n", name, legacy_settings[i].name, values[i][0], values[i][1]);
}

static void check_legacy_policy(const char *name, const POWER_POLICY *expected, const POWER_POLICY *policy)
{
    ok(policy->user.VideoTimeoutAc == expected->user.VideoTimeoutAc && policy->user.VideoTimeoutDc == expected->user.VideoTimeoutDc,
       "%s: legacy video timeout %lu/%lu, expected %lu/%lu\n", name, policy->user.VideoTimeoutAc, policy->user.VideoTimeoutDc,
       expected->user.VideoTimeoutAc, expected->user.VideoTimeoutDc);
    ok(policy->user.IdleTimeoutAc == expected->user.IdleTimeoutAc && policy->user.IdleTimeoutDc == expected->user.IdleTimeoutDc,
       "%s: legacy idle timeout %lu/%lu, expected %lu/%lu\n", name, policy->user.IdleTimeoutAc, policy->user.IdleTimeoutDc,
       expected->user.IdleTimeoutAc, expected->user.IdleTimeoutDc);
    ok(policy->user.IdleAc.Action == expected->user.IdleAc.Action && policy->user.IdleDc.Action == expected->user.IdleDc.Action,
       "%s: legacy idle action %u/%u, expected %u/%u\n", name, policy->user.IdleAc.Action, policy->user.IdleDc.Action,
       expected->user.IdleAc.Action, expected->user.IdleDc.Action);
    ok(policy->user.SpindownTimeoutAc == expected->user.SpindownTimeoutAc && policy->user.SpindownTimeoutDc == expected->user.SpindownTimeoutDc,
       "%s: legacy spindown timeout %lu/%lu, expected %lu/%lu\n", name, policy->user.SpindownTimeoutAc, policy->user.SpindownTimeoutDc,
       expected->user.SpindownTimeoutAc, expected->user.SpindownTimeoutDc);
    ok(policy->mach.DozeS4TimeoutAc == expected->mach.DozeS4TimeoutAc && policy->mach.DozeS4TimeoutDc == expected->mach.DozeS4TimeoutDc,
       "%s: legacy hibernate delay %lu/%lu, expected %lu/%lu\n", name, policy->mach.DozeS4TimeoutAc, policy->mach.DozeS4TimeoutDc,
       expected->mach.DozeS4TimeoutAc, expected->mach.DozeS4TimeoutDc);
}

static void check_scheme_values(const char *name, DWORD expected[][2], DWORD values[][2])
{
    UINT i;

    for (i = 0; i < ARRAYSIZE(legacy_settings); i++)
        ok(values[i][0] == expected[i][0] && values[i][1] == expected[i][1], "%s: scheme %s %lu/%lu, expected %lu/%lu\n",
           name, legacy_settings[i].name, values[i][0], values[i][1], expected[i][0], expected[i][1]);
}

static void scheme_values_from_policy(const POWER_POLICY *policy, DWORD values[][2])
{
    values[0][0] = policy->user.VideoTimeoutAc;
    values[0][1] = policy->user.VideoTimeoutDc;
    values[1][0] = policy->user.IdleAc.Action == PowerActionSleep ? policy->user.IdleTimeoutAc : 0;
    values[1][1] = policy->user.IdleDc.Action == PowerActionSleep ? policy->user.IdleTimeoutDc : 0;
    values[2][0] = policy->mach.DozeS4TimeoutAc ? policy->user.IdleTimeoutAc + policy->mach.DozeS4TimeoutAc : 0;
    values[2][1] = policy->mach.DozeS4TimeoutDc ? policy->user.IdleTimeoutDc + policy->mach.DozeS4TimeoutDc : 0;
    values[3][0] = policy->user.SpindownTimeoutAc;
    values[3][1] = policy->user.SpindownTimeoutDc;
}

static void set_legacy_policy(const char *name, UINT id, const GUID *scheme, const POWER_POLICY *expected)
{
    DWORD expected_values[ARRAYSIZE(legacy_settings)][2];
    DWORD values[ARRAYSIZE(legacy_settings)][2];
    POWER_POLICY policy = *expected;
    BOOL result;

    SetLastError(0xdeadbeef);
    result = SetActivePwrScheme(id, NULL, &policy);
    ok(result, "%s: SetActivePwrScheme failed, error %lu\n", name, GetLastError());
    read_legacy_settings(scheme, values);
    memset(&policy, 0xcc, sizeof(policy));
    ok(ReadPwrScheme(id, &policy), "%s: ReadPwrScheme failed, error %lu\n", name, GetLastError());
    trace_legacy_policy(name, &policy, values);
    check_legacy_policy(name, expected, &policy);
    scheme_values_from_policy(expected, expected_values);
    check_scheme_values(name, expected_values, values);
}

static void test_disk_defaults(void)
{
    static const struct
    {
        const char *name;
        const GUID *scheme;
        DWORD ac, dc;
    } tests[] =
    {
        {"balanced", &balanced_scheme, 1200, 600},
        {"power-saver", &power_saver_scheme, 1200, 300},
        {"high-performance", &high_performance_scheme, 1200, 1200},
    };
    DWORD ac, dc, ret;
    UINT i;

    for (i = 0; i < ARRAYSIZE(tests); i++)
    {
        ac = dc = 0xdeadbeef;
        ret = PowerReadACValueIndex(NULL, tests[i].scheme, &disk_subgroup, &disk_powerdown_timeout, &ac);
        ok(ret == ERROR_SUCCESS, "%s: reading the AC disk timeout returned %lu\n", tests[i].name, ret);
        ret = PowerReadDCValueIndex(NULL, tests[i].scheme, &disk_subgroup, &disk_powerdown_timeout, &dc);
        ok(ret == ERROR_SUCCESS, "%s: reading the DC disk timeout returned %lu\n", tests[i].name, ret);
        ok(ac == tests[i].ac && dc == tests[i].dc, "%s: disk timeout %lu/%lu, expected %lu/%lu\n",
           tests[i].name, ac, dc, tests[i].ac, tests[i].dc);
    }
}

static void test_legacy_policy_contract(void)
{
    DWORD original_values[ARRAYSIZE(legacy_settings)][2];
    DWORD values[ARRAYSIZE(legacy_settings)][2];
    POWER_POLICY original, policy;
    GUID *active = NULL;
    UINT id, i;
    DWORD ret;

    ret = PowerGetActiveScheme(NULL, &active);
    ok(ret == ERROR_SUCCESS, "PowerGetActiveScheme returned %lu\n", ret);
    if (ret != ERROR_SUCCESS)
        return;
    if (!GetActivePwrScheme(&id) || !ReadPwrScheme(id, &original))
    {
        skip("no legacy policy for the active scheme\n");
        LocalFree(active);
        return;
    }
    trace("legacy id=%u\n", id);
    read_legacy_settings(active, original_values);
    trace_legacy_policy("before", &original, original_values);

    policy = original;
    policy.user.VideoTimeoutAc = 120;
    policy.user.VideoTimeoutDc = 180;
    policy.user.IdleTimeoutAc = 1500;
    policy.user.IdleTimeoutDc = 1260;
    policy.user.IdleAc.Action = PowerActionSleep;
    policy.user.IdleDc.Action = PowerActionSleep;
    policy.user.SpindownTimeoutAc = 1140;
    policy.user.SpindownTimeoutDc = 960;
    policy.mach.DozeS4TimeoutAc = 3600;
    policy.mach.DozeS4TimeoutDc = 2700;
    set_legacy_policy("all", id, active, &policy);

    policy = original;
    policy.user.VideoTimeoutAc = original.user.VideoTimeoutAc == 420 ? 480 : 420;
    set_legacy_policy("video", id, active, &policy);

    policy = original;
    policy.user.IdleTimeoutAc = 1500;
    policy.user.IdleAc.Action = PowerActionNone;
    set_legacy_policy("idle-none", id, active, &policy);

    ok(SetActivePwrScheme(id, NULL, &original), "restoring the legacy policy failed, error %lu\n", GetLastError());
    for (i = 0; i < ARRAYSIZE(legacy_settings); i++)
    {
        PowerWriteACValueIndex(NULL, active, legacy_settings[i].subgroup, legacy_settings[i].setting, original_values[i][0]);
        PowerWriteDCValueIndex(NULL, active, legacy_settings[i].subgroup, legacy_settings[i].setting, original_values[i][1]);
    }
    ret = PowerSetActiveScheme(NULL, active);
    ok(ret == ERROR_SUCCESS, "PowerSetActiveScheme returned %lu\n", ret);
    read_legacy_settings(active, values);
    memset(&policy, 0xcc, sizeof(policy));
    ok(ReadPwrScheme(id, &policy), "ReadPwrScheme failed, error %lu\n", GetLastError());
    trace_legacy_policy("restored", &policy, values);
    check_legacy_policy("restored", &original, &policy);
    check_scheme_values("restored", original_values, values);
    LocalFree(active);
}

static void test_default_index(void)
{
    DWORD ac = 0xdeadbeef, dc = 0xdeadbeef;
    DWORD ret;

    ret = PowerReadACDefaultIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_epp, NULL);
    ok(ret == ERROR_SUCCESS, "AC default without output returned %lu\n", ret);
    ret = PowerReadDCDefaultIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_epp, NULL);
    ok(ret == ERROR_SUCCESS, "DC default without output returned %lu\n", ret);
    ret = PowerReadDCDefaultIndex(NULL, NULL, &processor_subgroup, &processor_epp, &dc);
    ok(ret == ERROR_INVALID_PARAMETER, "DC default without personality returned %lu\n", ret);

    ret = PowerReadACDefaultIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_epp, &ac);
    ok(ret == ERROR_SUCCESS, "AC default returned %lu\n", ret);
    ok(ac == balanced_expected.ac_epp, "AC default EPP is %lu, expected %lu\n", ac, balanced_expected.ac_epp);
    ret = PowerReadDCDefaultIndex(NULL, &balanced_scheme, &processor_subgroup, &processor_epp, &dc);
    ok(ret == ERROR_SUCCESS, "DC default returned %lu\n", ret);
    ok(dc == balanced_expected.dc_epp, "DC default EPP is %lu, expected %lu\n", dc, balanced_expected.dc_epp);
}

START_TEST(power_profile)
{
    GUID *original = NULL;
    DWORD ret;

    test_default_index();

    ret = PowerGetActiveScheme(NULL, &original);
    ok(ret == ERROR_SUCCESS, "PowerGetActiveScheme returned %lu\n", ret);
    ok(original != NULL, "PowerGetActiveScheme returned NULL\n");
    if (ret != ERROR_SUCCESS || !original)
        return;

    trace_guid("original", original);
    test_argument_contract();
    test_write_read_contract();
    test_disk_defaults();
    test_legacy_policy_contract();
    test_processor_policy_defaults();
    test_timeout_defaults();
    query_scheme_settings("original", original, NULL);
    query_processor_information("original-prime");
    query_processor_information("original");

    activate_and_query("balanced", &balanced_scheme, &balanced_expected);
    activate_and_query("power-saver", &power_saver_scheme, &power_saver_expected);
    activate_and_query("high-performance", &high_performance_scheme, &high_performance_expected);

    ret = PowerSetActiveScheme(NULL, original);
    ok(ret == ERROR_SUCCESS, "restoring the original power scheme returned %lu\n", ret);
    LocalFree(original);
}
