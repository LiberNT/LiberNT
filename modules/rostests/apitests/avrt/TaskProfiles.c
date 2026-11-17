/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests how MMCSS reads the categories, priorities and affinities of task profiles
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <ndk/ntndk.h>
#include <avrt.h>

#define TASKS_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks"
#define NO_VALUE MAXDWORD
#define NO_CATEGORY NULL

typedef struct _PROFILE
{
    PCWSTR Name;
    PCWSTR Category;
    DWORD Priority;
    DWORD Affinity;
} PROFILE;

static const PROFILE Profiles[] =
{
    { L"AvrtTest Low",               L"Low",    4,        NO_VALUE },
    { L"AvrtTest No Category",       NO_CATEGORY, 2,      NO_VALUE },
    { L"AvrtTest Bad Category",      L"Bogus",  2,        NO_VALUE },
    { L"AvrtTest Medium 0",          L"Medium", 0,        NO_VALUE },
    { L"AvrtTest Medium 9",          L"Medium", 9,        NO_VALUE },
    { L"AvrtTest High 0",            L"High",   0,        NO_VALUE },
    { L"AvrtTest Medium Default",    L"Medium", NO_VALUE, NO_VALUE },
    { L"AvrtTest Affinity 1",        L"Medium", 2,        0x1 },
    { L"AvrtTest Affinity 3",        L"Medium", 2,        0x3 },
    { L"AvrtTest Affinity Unused",   L"Medium", 2,        0xFFFFFFFF },
    { L"AvrtTest Affinity Outside",  L"Medium", 2,        0x80000000 },
};

static HANDLE (WINAPI *pAvSetMmThreadCharacteristicsW)(LPCWSTR, LPDWORD);
static BOOL (WINAPI *pAvRevertMmThreadCharacteristics)(HANDLE);
static BOOL (WINAPI *pAvSetMmThreadPriority)(HANDLE, AVRT_PRIORITY);

static VOID
QueryThread(PLONG Priority, PLONG BasePriority, PKAFFINITY Affinity)
{
    THREAD_BASIC_INFORMATION Info;
    NTSTATUS Status;

    Status = NtQueryInformationThread(GetCurrentThread(), ThreadBasicInformation, &Info, sizeof(Info), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    *Priority = NT_SUCCESS(Status) ? Info.Priority : -1;
    *BasePriority = NT_SUCCESS(Status) ? Info.BasePriority : -1;
    *Affinity = NT_SUCCESS(Status) ? Info.AffinityMask : 0;
}

static BOOL
CreateProfiles(VOID)
{
    HKEY Tasks, Key;
    LSTATUS Error;
    ULONG i;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, TASKS_KEY, 0, KEY_CREATE_SUB_KEY, &Tasks);
    if (Error != ERROR_SUCCESS)
        return FALSE;
    for (i = 0; i < RTL_NUMBER_OF(Profiles) && Error == ERROR_SUCCESS; i++)
    {
        Error = RegCreateKeyExW(Tasks, Profiles[i].Name, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
        if (Error != ERROR_SUCCESS)
            break;
        if (Profiles[i].Category != NO_CATEGORY)
            Error = RegSetValueExW(Key, L"Scheduling Category", 0, REG_SZ, (const BYTE *)Profiles[i].Category,
                                   (DWORD)(wcslen(Profiles[i].Category) + 1) * sizeof(WCHAR));
        if (Error == ERROR_SUCCESS && Profiles[i].Priority != NO_VALUE)
            Error = RegSetValueExW(Key, L"Priority", 0, REG_DWORD, (const BYTE *)&Profiles[i].Priority, sizeof(DWORD));
        if (Error == ERROR_SUCCESS && Profiles[i].Affinity != NO_VALUE)
            Error = RegSetValueExW(Key, L"Affinity", 0, REG_DWORD, (const BYTE *)&Profiles[i].Affinity, sizeof(DWORD));
        RegCloseKey(Key);
    }
    RegCloseKey(Tasks);
    return Error == ERROR_SUCCESS;
}

static VOID
DeleteProfiles(VOID)
{
    HKEY Tasks;
    ULONG i;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TASKS_KEY, 0, DELETE | KEY_ENUMERATE_SUB_KEYS, &Tasks) != ERROR_SUCCESS)
        return;
    for (i = 0; i < RTL_NUMBER_OF(Profiles); i++)
        ok_long(RegDeleteKeyW(Tasks, Profiles[i].Name), ERROR_SUCCESS);
    RegCloseKey(Tasks);
}

static VOID
TestRejected(PCWSTR Name)
{
    HANDLE Handle;
    DWORD Index = 0;

    SetLastError(0xdeadbeef);
    Handle = pAvSetMmThreadCharacteristicsW(Name, &Index);
    ok(Handle == NULL, "%ls: registered\n", Name);
    ok_long(GetLastError(), ERROR_INVALID_TASK_NAME);
    if (Handle != NULL)
        pAvRevertMmThreadCharacteristics(Handle);
}

static VOID
TestLevels(PCWSTR Name, LONG Registered, const LONG *Levels)
{
    LONG Priority, Base, BeforePriority, BeforeBase;
    KAFFINITY Affinity, BeforeAffinity;
    HANDLE Handle;
    DWORD Index = 0;
    INT Level;

    ok(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL), "priority not set: %lu\n", GetLastError());
    QueryThread(&BeforePriority, &BeforeBase, &BeforeAffinity);
    Handle = pAvSetMmThreadCharacteristicsW(Name, &Index);
    ok(Handle != NULL, "%ls: not registered: %lu\n", Name, GetLastError());
    if (Handle == NULL)
        return;

    QueryThread(&Priority, &Base, &Affinity);
    if (Registered < 0)
    {
        ok(Priority == BeforePriority, "%ls: priority %ld, %ld before\n", Name, Priority, BeforePriority);
        ok(Base == BeforeBase, "%ls: base priority %ld, %ld before\n", Name, Base, BeforeBase);
        ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_BELOW_NORMAL);
    }
    else
    {
        ok(Priority == Registered, "%ls: priority %ld, expected %ld\n", Name, Priority, Registered);
    }

    for (Level = AVRT_PRIORITY_VERYLOW; Level <= AVRT_PRIORITY_CRITICAL; Level++)
    {
        ok_int(pAvSetMmThreadPriority(Handle, (AVRT_PRIORITY)Level), TRUE);
        QueryThread(&Priority, &Base, &Affinity);
        ok(Priority == Levels[Level - AVRT_PRIORITY_VERYLOW], "%ls: level %d: priority %ld, expected %ld\n",
           Name, Level, Priority, Levels[Level - AVRT_PRIORITY_VERYLOW]);
    }

    ok_int(pAvRevertMmThreadCharacteristics(Handle), TRUE);
    QueryThread(&Priority, &Base, &Affinity);
    ok(Base == BeforeBase, "%ls: base priority %ld after revert, %ld before\n", Name, Base, BeforeBase);
    ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_BELOW_NORMAL);
    ok(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL), "priority not reset: %lu\n", GetLastError());
}

static VOID
TestAffinity(PCWSTR Name, KAFFINITY Expected)
{
    LONG Priority, Base;
    KAFFINITY Affinity, Before;
    HANDLE Handle;
    DWORD Index = 0;

    QueryThread(&Priority, &Base, &Before);
    Handle = pAvSetMmThreadCharacteristicsW(Name, &Index);
    ok(Handle != NULL, "%ls: not registered: %lu\n", Name, GetLastError());
    if (Handle == NULL)
        return;
    QueryThread(&Priority, &Base, &Affinity);
    ok(Affinity == Expected, "%ls: affinity %Ix, expected %Ix\n", Name, (size_t)Affinity, (size_t)Expected);
    ok(Priority == 17, "%ls: priority %ld\n", Name, Priority);
    ok_int(pAvRevertMmThreadCharacteristics(Handle), TRUE);
    QueryThread(&Priority, &Base, &Affinity);
    ok(Affinity == Before, "%ls: affinity %Ix after revert, %Ix before\n", Name, (size_t)Affinity, (size_t)Before);
}

static KAFFINITY
ProfileAffinity(DWORD Affinity, KAFFINITY Process)
{
    return ((Affinity & ~Process) == 0) ? Affinity : Process;
}

START_TEST(TaskProfiles)
{
    static const LONG LowLevels[] = { 6, 7, 8, 9, 10 };
    static const LONG DefaultLevels[] = { 16, 16, 16, 17, 18 };
    HMODULE Avrt = LoadLibraryW(L"avrt.dll");
    DWORD_PTR Process, System;
    HANDLE Handle;
    DWORD Index = 0;

    ok(Avrt != NULL, "avrt.dll not loaded: %lu\n", GetLastError());
    if (!Avrt)
        return;
    pAvSetMmThreadCharacteristicsW = (PVOID)GetProcAddress(Avrt, "AvSetMmThreadCharacteristicsW");
    pAvRevertMmThreadCharacteristics = (PVOID)GetProcAddress(Avrt, "AvRevertMmThreadCharacteristics");
    pAvSetMmThreadPriority = (PVOID)GetProcAddress(Avrt, "AvSetMmThreadPriority");
    if (!pAvSetMmThreadCharacteristicsW || !pAvRevertMmThreadCharacteristics || !pAvSetMmThreadPriority)
    {
        ok(0, "avrt export missing\n");
        return;
    }

    if (!CreateProfiles())
    {
        skip("The task profiles can't be written: %lu\n", GetLastError());
        return;
    }
    Handle = pAvSetMmThreadCharacteristicsW(Profiles[0].Name, &Index);
    if (Handle == NULL && GetLastError() == ERROR_INVALID_TASK_NAME)
    {
        skip("The task profiles were read before this test wrote its own; run it again after a restart\n");
        return;
    }
    ok(Handle != NULL, "not registered: %lu\n", GetLastError());
    if (Handle != NULL)
        pAvRevertMmThreadCharacteristics(Handle);

    TestLevels(L"AvrtTest Low", -1, LowLevels);
    TestLevels(L"AvrtTest No Category", -1, LowLevels);
    TestLevels(L"AvrtTest Medium Default", 16, DefaultLevels);
    TestRejected(L"AvrtTest Bad Category");
    TestRejected(L"AvrtTest Medium 0");
    TestRejected(L"AvrtTest Medium 9");
    TestRejected(L"AvrtTest High 0");

    ok(GetProcessAffinityMask(GetCurrentProcess(), &Process, &System), "no process affinity: %lu\n", GetLastError());
    TestAffinity(L"AvrtTest Affinity 1", 0x1);
    TestAffinity(L"AvrtTest Affinity 3", ProfileAffinity(0x3, Process));
    TestAffinity(L"AvrtTest Affinity Unused", Process);
    TestAffinity(L"AvrtTest Affinity Outside", ProfileAffinity(0x80000000, Process));
    ok(SetProcessAffinityMask(GetCurrentProcess(), 0x1), "process affinity not set: %lu\n", GetLastError());
    TestAffinity(L"AvrtTest Affinity 3", 0x1);
    ok(SetProcessAffinityMask(GetCurrentProcess(), Process), "process affinity not restored: %lu\n", GetLastError());

    DeleteProfiles();
}
