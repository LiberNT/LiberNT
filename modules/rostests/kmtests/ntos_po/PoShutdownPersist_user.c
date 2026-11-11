/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A shutdown keeps what was written to the registry just before it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/setypes.h>

#define SHUTDOWN_KEY L"SOFTWARE\\KmtShutdownPersist"
#define SHUTDOWN_VALUE L"Marker"
#define SHUTDOWN_DATA L"persisted"

/* Powers the machine off between its two boots, so it is never part of a sweep */
START_TEST(PoShutdownPersist)
{
    WCHAR Data[32];
    DWORD Type, Size = sizeof(Data);
    BOOLEAN Old;
    NTSTATUS Status;
    HKEY Key;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, SHUTDOWN_KEY, 0, KEY_ALL_ACCESS, &Key);
    if (Error == ERROR_SUCCESS)
    {
        Error = RegQueryValueExW(Key, SHUTDOWN_VALUE, NULL, &Type, (PBYTE)Data, &Size);
        ok_eq_long(Error, ERROR_SUCCESS);
        ok_eq_ulong(Type, (ULONG)REG_SZ);
        ok(Error == ERROR_SUCCESS && wcscmp(Data, SHUTDOWN_DATA) == 0,
           "the value written before the shutdown was lost\n");
        RegCloseKey(Key);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, SHUTDOWN_KEY);
        trace("PoShutdownPersist: second boot\n");
        return;
    }

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, SHUTDOWN_KEY, 0, NULL, 0, KEY_ALL_ACCESS,
                            NULL, &Key, NULL);
    ok_eq_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return;

    Error = RegSetValueExW(Key, SHUTDOWN_VALUE, 0, REG_SZ, (const BYTE *)SHUTDOWN_DATA,
                           sizeof(SHUTDOWN_DATA));
    ok_eq_long(Error, ERROR_SUCCESS);
    RegCloseKey(Key);

    Status = RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, TRUE, FALSE, &Old);
    ok_eq_hex(Status, STATUS_SUCCESS);

    trace("PoShutdownPersist: first boot, powering off\n");
    Status = NtShutdownSystem(ShutdownPowerOff);
    ok(FALSE, "NtShutdownSystem returned 0x%08lx\n", Status);
}
