/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for PowerDuplicateScheme and PowerDeleteScheme
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <powrprof.h>
#include <powersetting.h>

static const GUID balanced_scheme = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
static const GUID high_performance_scheme = {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
static const GUID processor_subgroup = {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
static const GUID processor_minimum = {0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c}};
static const GUID video_subgroup = {0x7516b95f, 0xf776, 0x4464, {0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99}};
static const GUID video_powerdown_timeout = {0x3c0bc021, 0xc8a8, 0x4e07, {0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e}};
static const GUID unknown_guid = {0xdeadbeef, 0x1234, 0x5678, {0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78}};
static const GUID fixed_guid = {0x6c1b7a3e, 0x52d0, 0x4f7e, {0x9b, 0x1a, 0x3d, 0x41, 0x0e, 0x9c, 0x27, 0x55}};

static BOOL scheme_listed(const GUID *scheme)
{
    GUID guid;
    DWORD size, ret;
    ULONG index;

    for (index = 0; index < 64; index++)
    {
        size = sizeof(guid);
        ret = PowerEnumerate(NULL, NULL, NULL, ACCESS_SCHEME, index, (UCHAR *)&guid, &size);
        if (ret != ERROR_SUCCESS)
            return FALSE;
        if (IsEqualGUID(&guid, scheme))
            return TRUE;
    }
    return FALSE;
}

static DWORD read_name(const GUID *scheme, WCHAR *name, DWORD chars)
{
    DWORD size = chars * sizeof(WCHAR);

    name[0] = UNICODE_NULL;
    return PowerReadFriendlyName(NULL, scheme, NULL, NULL, (UCHAR *)name, &size);
}

static void test_arguments(void)
{
    GUID *dest;
    DWORD ret;

    dest = NULL;
    ret = PowerDuplicateScheme(NULL, NULL, &dest);
    ok(ret == ERROR_INVALID_PARAMETER, "PowerDuplicateScheme(NULL source) returned %lu\n", ret);
    ok(dest == NULL, "PowerDuplicateScheme(NULL source) set %p\n", dest);
    ret = PowerDuplicateScheme(NULL, &balanced_scheme, NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "PowerDuplicateScheme(NULL destination) returned %lu\n", ret);
    dest = NULL;
    ret = PowerDuplicateScheme(NULL, &unknown_guid, &dest);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerDuplicateScheme(unknown source) returned %lu\n", ret);
    ok(dest == NULL, "PowerDuplicateScheme(unknown source) set %p\n", dest);

    ret = PowerDeleteScheme(NULL, NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "PowerDeleteScheme(NULL) returned %lu\n", ret);
    ret = PowerDeleteScheme(NULL, &unknown_guid);
    ok(ret == ERROR_FILE_NOT_FOUND, "PowerDeleteScheme(unknown) returned %lu\n", ret);
}

static void test_duplicate(const GUID *original)
{
    WCHAR source_name[128], name[128];
    static const WCHAR new_name[] = L"LiberNT duplicate test";
    GUID *dest = NULL, *active = NULL;
    DWORD source_ac, value, ret;

    ret = PowerReadACValueIndex(NULL, &high_performance_scheme, &processor_subgroup, &processor_minimum, &source_ac);
    ok(ret == ERROR_SUCCESS, "reading the source AC minimum returned %lu\n", ret);
    ret = read_name(&high_performance_scheme, source_name, _countof(source_name));
    ok(ret == ERROR_SUCCESS, "reading the source name returned %lu\n", ret);

    ret = PowerDuplicateScheme(NULL, &high_performance_scheme, &dest);
    ok(ret == ERROR_SUCCESS, "PowerDuplicateScheme returned %lu\n", ret);
    ok(dest != NULL, "PowerDuplicateScheme returned no GUID\n");
    if (ret != ERROR_SUCCESS || !dest)
        return;
    ok(!IsEqualGUID(dest, &high_performance_scheme), "the duplicate has the source GUID\n");
    ok(scheme_listed(dest), "the duplicate is not enumerated\n");

    ret = read_name(dest, name, _countof(name));
    ok(ret == ERROR_SUCCESS, "reading the duplicate name returned %lu\n", ret);
    ok(!wcscmp(name, source_name), "duplicate name is %ls, source %ls\n", name, source_name);

    value = 0xdeadbeef;
    ret = PowerReadACValueIndex(NULL, dest, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_SUCCESS, "reading the duplicate AC minimum returned %lu\n", ret);
    ok(value == source_ac, "duplicate AC minimum is %lu, source %lu\n", value, source_ac);
    value = 0xdeadbeef;
    ret = PowerReadDCValueIndex(NULL, dest, &video_subgroup, &video_powerdown_timeout, &value);
    ok(ret == ERROR_SUCCESS, "reading the duplicate DC video timeout returned %lu\n", ret);

    ret = PowerWriteACValueIndex(NULL, dest, &processor_subgroup, &processor_minimum, source_ac == 37 ? 38 : 37);
    ok(ret == ERROR_SUCCESS, "writing the duplicate AC minimum returned %lu\n", ret);
    value = 0xdeadbeef;
    ret = PowerReadACValueIndex(NULL, dest, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_SUCCESS && value == (source_ac == 37 ? 38u : 37u), "duplicate AC minimum after write is %lu (%lu)\n", value, ret);
    value = 0xdeadbeef;
    ret = PowerReadACValueIndex(NULL, &high_performance_scheme, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_SUCCESS && value == source_ac, "source AC minimum after the duplicate's write is %lu (%lu)\n", value, ret);

    ret = PowerWriteFriendlyName(NULL, dest, NULL, NULL, (UCHAR *)new_name, sizeof(new_name));
    ok(ret == ERROR_SUCCESS, "PowerWriteFriendlyName(duplicate) returned %lu\n", ret);
    ret = read_name(dest, name, _countof(name));
    ok(ret == ERROR_SUCCESS && !wcscmp(name, new_name), "duplicate name after write is %ls (%lu)\n", name, ret);
    ret = read_name(&high_performance_scheme, name, _countof(name));
    ok(ret == ERROR_SUCCESS && !wcscmp(name, source_name), "source name after the duplicate's write is %ls (%lu)\n", name, ret);

    ret = PowerSetActiveScheme(NULL, dest);
    ok(ret == ERROR_SUCCESS, "PowerSetActiveScheme(duplicate) returned %lu\n", ret);
    ret = PowerGetActiveScheme(NULL, &active);
    ok(ret == ERROR_SUCCESS && active && IsEqualGUID(active, dest), "the duplicate is not active (%lu)\n", ret);
    if (active)
        LocalFree(active);
    ret = PowerDeleteScheme(NULL, dest);
    ok(ret == ERROR_NOT_SUPPORTED, "PowerDeleteScheme(active duplicate) returned %lu\n", ret);
    ok(scheme_listed(dest), "the active duplicate is gone\n");

    ret = PowerSetActiveScheme(NULL, original);
    ok(ret == ERROR_SUCCESS, "restoring the original scheme returned %lu\n", ret);
    ret = PowerDeleteScheme(NULL, dest);
    ok(ret == ERROR_SUCCESS, "PowerDeleteScheme(duplicate) returned %lu\n", ret);
    ok(!scheme_listed(dest), "the deleted duplicate is still enumerated\n");
    ret = PowerReadACValueIndex(NULL, dest, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_FILE_NOT_FOUND, "reading the deleted duplicate returned %lu\n", ret);
    ret = read_name(dest, name, _countof(name));
    ok(ret == ERROR_FILE_NOT_FOUND, "reading the deleted duplicate's name returned %lu\n", ret);
    ret = PowerSetActiveScheme(NULL, dest);
    ok(ret == ERROR_INVALID_PARAMETER, "activating the deleted duplicate returned %lu\n", ret);
    ret = PowerDeleteScheme(NULL, dest);
    ok(ret == ERROR_FILE_NOT_FOUND, "deleting the duplicate again returned %lu\n", ret);
    LocalFree(dest);
}

static void test_duplicate_chain(void)
{
    GUID *first = NULL, *second = NULL;
    DWORD value, ret;

    ret = PowerDuplicateScheme(NULL, &balanced_scheme, &first);
    ok(ret == ERROR_SUCCESS && first, "duplicating balanced returned %lu\n", ret);
    if (ret != ERROR_SUCCESS || !first)
        return;
    ret = PowerWriteDCValueIndex(NULL, first, &processor_subgroup, &processor_minimum, 41);
    ok(ret == ERROR_SUCCESS, "writing the first duplicate returned %lu\n", ret);
    ret = PowerDuplicateScheme(NULL, first, &second);
    ok(ret == ERROR_SUCCESS && second, "duplicating a duplicate returned %lu\n", ret);
    if (ret == ERROR_SUCCESS && second)
    {
        value = 0xdeadbeef;
        ret = PowerReadDCValueIndex(NULL, second, &processor_subgroup, &processor_minimum, &value);
        ok(ret == ERROR_SUCCESS && value == 41, "second duplicate DC minimum is %lu (%lu)\n", value, ret);
        ok(PowerDeleteScheme(NULL, second) == ERROR_SUCCESS, "deleting the second duplicate failed\n");
        LocalFree(second);
    }
    ok(PowerDeleteScheme(NULL, first) == ERROR_SUCCESS, "deleting the first duplicate failed\n");
    LocalFree(first);
}

static void test_caller_guid(void)
{
    GUID target = fixed_guid;
    GUID *dest = &target;
    DWORD ret;

    PowerDeleteScheme(NULL, &fixed_guid);
    ret = PowerDuplicateScheme(NULL, &balanced_scheme, &dest);
    ok(ret == ERROR_SUCCESS, "PowerDuplicateScheme(caller GUID) returned %lu\n", ret);
    ok(dest == &target, "PowerDuplicateScheme(caller GUID) replaced the pointer with %p\n", dest);
    ok(IsEqualGUID(&target, &fixed_guid), "PowerDuplicateScheme(caller GUID) changed the GUID\n");
    ok(scheme_listed(&fixed_guid), "the caller GUID scheme is not enumerated\n");

    ret = PowerDuplicateScheme(NULL, &balanced_scheme, &dest);
    ok(ret == ERROR_ALREADY_EXISTS, "PowerDuplicateScheme(existing caller GUID) returned %lu\n", ret);
    target = balanced_scheme;
    ret = PowerDuplicateScheme(NULL, &high_performance_scheme, &dest);
    ok(ret == ERROR_ALREADY_EXISTS, "PowerDuplicateScheme(built-in caller GUID) returned %lu\n", ret);

    ret = PowerDeleteScheme(NULL, &fixed_guid);
    ok(ret == ERROR_SUCCESS, "PowerDeleteScheme(caller GUID) returned %lu\n", ret);
    ok(!scheme_listed(&fixed_guid), "the caller GUID scheme is still enumerated\n");
}

static void test_delete_builtin(const GUID *original)
{
    GUID target = IsEqualGUID(original, &high_performance_scheme) ? balanced_scheme : high_performance_scheme;
    WCHAR source_name[128], name[128];
    GUID *backup = NULL, *dest = &target;
    DWORD source_ac, value, ret;

    ret = PowerReadACValueIndex(NULL, &target, &processor_subgroup, &processor_minimum, &source_ac);
    ok(ret == ERROR_SUCCESS, "reading the built-in AC minimum returned %lu\n", ret);
    ret = read_name(&target, source_name, _countof(source_name));
    ok(ret == ERROR_SUCCESS, "reading the built-in name returned %lu\n", ret);
    ret = PowerDuplicateScheme(NULL, &target, &backup);
    ok(ret == ERROR_SUCCESS && backup, "duplicating the built-in returned %lu\n", ret);
    if (ret != ERROR_SUCCESS || !backup)
        return;

    ret = PowerDeleteScheme(NULL, &target);
    ok(ret == ERROR_SUCCESS, "PowerDeleteScheme(inactive built-in) returned %lu\n", ret);
    ok(!scheme_listed(&target), "the deleted built-in is still enumerated\n");
    ret = PowerReadACValueIndex(NULL, &target, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_FILE_NOT_FOUND, "reading the deleted built-in returned %lu\n", ret);
    ret = read_name(&target, name, _countof(name));
    ok(ret == ERROR_FILE_NOT_FOUND, "reading the deleted built-in's name returned %lu\n", ret);
    ret = PowerSetActiveScheme(NULL, &target);
    ok(ret == ERROR_INVALID_PARAMETER, "activating the deleted built-in returned %lu\n", ret);
    ret = PowerDeleteScheme(NULL, &target);
    ok(ret == ERROR_FILE_NOT_FOUND, "deleting the built-in again returned %lu\n", ret);
    value = 0xdeadbeef;
    ret = PowerReadACValueIndex(NULL, backup, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_SUCCESS && value == source_ac, "the duplicate's AC minimum after deleting its source is %lu (%lu)\n", value, ret);

    ret = PowerDuplicateScheme(NULL, backup, &dest);
    ok(ret == ERROR_SUCCESS, "duplicating onto the deleted built-in returned %lu\n", ret);
    ok(dest == &target, "duplicating onto the deleted built-in replaced the pointer with %p\n", dest);
    ok(scheme_listed(&target), "the restored built-in is not enumerated\n");
    value = 0xdeadbeef;
    ret = PowerReadACValueIndex(NULL, &target, &processor_subgroup, &processor_minimum, &value);
    ok(ret == ERROR_SUCCESS && value == source_ac, "the restored built-in AC minimum is %lu (%lu)\n", value, ret);
    ret = read_name(&target, name, _countof(name));
    ok(ret == ERROR_SUCCESS && !wcscmp(name, source_name), "the restored built-in name is %ls (%lu)\n", name, ret);
    ok(PowerDeleteScheme(NULL, backup) == ERROR_SUCCESS, "deleting the built-in's duplicate failed\n");
    LocalFree(backup);
}

START_TEST(PowerScheme)
{
    GUID *original = NULL;
    DWORD ret;

    ret = PowerGetActiveScheme(NULL, &original);
    ok(ret == ERROR_SUCCESS && original, "PowerGetActiveScheme returned %lu\n", ret);
    if (ret != ERROR_SUCCESS || !original)
        return;

    test_arguments();
    test_duplicate(original);
    test_duplicate_chain();
    test_caller_guid();
    test_delete_builtin(original);

    ret = PowerSetActiveScheme(NULL, original);
    ok(ret == ERROR_SUCCESS, "restoring the original scheme returned %lu\n", ret);
    LocalFree(original);
}
