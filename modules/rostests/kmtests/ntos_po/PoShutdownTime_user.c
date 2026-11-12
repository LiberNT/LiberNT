/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A clean shutdown records its time in the registry
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/setypes.h>

#define MARKER_KEY L"SOFTWARE\\KmtShutdownTime"
#define MARKER_VALUE L"Before"

static
VOID
CheckShutdownTime(
    _In_ HKEY Marker)
{
    SYSTEM_TIMEOFDAY_INFORMATION TimeOfDay;
    LARGE_INTEGER Before, Recorded;
    UCHAR Data[16];
    DWORD Type, Size;
    NTSTATUS Status;
    HKEY Key;
    LONG Error;

    Size = sizeof(Before);
    Error = RegQueryValueExW(Marker, MARKER_VALUE, NULL, &Type, (PBYTE)&Before, &Size);
    ok_eq_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Control\\Windows",
                          0,
                          KEY_QUERY_VALUE,
                          &Key);
    ok_eq_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return;

    Size = sizeof(Data);
    Error = RegQueryValueExW(Key, L"ShutdownTime", NULL, &Type, Data, &Size);
    RegCloseKey(Key);
    ok_eq_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return;

    ok_eq_ulong(Type, (ULONG)REG_BINARY);
    ok_eq_ulong(Size, (ULONG)sizeof(Recorded));
    if (Size != sizeof(Recorded)) return;
    RtlCopyMemory(&Recorded, Data, sizeof(Recorded));

    Status = NtQuerySystemInformation(SystemTimeOfDayInformation, &TimeOfDay, sizeof(TimeOfDay), NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ok(Recorded.QuadPart >= Before.QuadPart,
       "the shutdown time %I64x is earlier than the first boot's %I64x\n",
       Recorded.QuadPart, Before.QuadPart);
    ok(Recorded.QuadPart <= TimeOfDay.BootTime.QuadPart,
       "the shutdown time %I64x is later than this boot's start %I64x\n",
       Recorded.QuadPart, TimeOfDay.BootTime.QuadPart);
}

/* Shuts the machine down between its two boots, so it is never part of a sweep */
START_TEST(PoShutdownTime)
{
    LARGE_INTEGER Before;
    BOOLEAN Old;
    NTSTATUS Status;
    HKEY Key;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, MARKER_KEY, 0, KEY_ALL_ACCESS, &Key);
    if (Error == ERROR_SUCCESS)
    {
        CheckShutdownTime(Key);
        RegCloseKey(Key);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, MARKER_KEY);
        trace("PoShutdownTime: second boot\n");
        return;
    }

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, MARKER_KEY, 0, NULL, 0, KEY_ALL_ACCESS,
                            NULL, &Key, NULL);
    ok_eq_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return;

    GetSystemTimeAsFileTime((LPFILETIME)&Before);
    Error = RegSetValueExW(Key, MARKER_VALUE, 0, REG_BINARY, (const BYTE *)&Before, sizeof(Before));
    ok_eq_long(Error, ERROR_SUCCESS);
    RegCloseKey(Key);

    Status = RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, TRUE, FALSE, &Old);
    ok_eq_hex(Status, STATUS_SUCCESS);

    trace("PoShutdownTime: first boot, shutting down without powering off\n");
    Status = NtShutdownSystem(ShutdownNoReboot);
    ok(FALSE, "NtShutdownSystem returned 0x%08lx\n", Status);
}
