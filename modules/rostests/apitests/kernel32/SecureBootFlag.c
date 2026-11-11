/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the shared user page reports the firmware's Secure Boot state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <ndk/setypes.h>

#ifndef SHARED_GLOBAL_FLAGS_SECURE_BOOT_ENABLED
#define SHARED_GLOBAL_FLAGS_SECURE_BOOT_ENABLED (1UL << 7)
#endif

#define EFI_GLOBAL_VARIABLE_GUID_STRING L"{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}"

START_TEST(SecureBootFlag)
{
    UCHAR Value[2];
    DWORD Length, Error;
    BOOLEAN WasEnabled, Dummy, FirmwareEnabled, SharedEnabled;
    NTSTATUS Status;

    Status = RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, &WasEnabled);
    if (!NT_SUCCESS(Status))
    {
        skip("SeSystemEnvironmentPrivilege cannot be enabled (0x%08lx)\n", Status);
        return;
    }

    Value[0] = Value[1] = 0xCC;
    SetLastError(0xdeadbeef);
    Length = GetFirmwareEnvironmentVariableW(L"SecureBoot",
                                             EFI_GLOBAL_VARIABLE_GUID_STRING,
                                             Value,
                                             sizeof(Value));
    Error = GetLastError();
    trace("SecureBoot variable: length %lu, value %u, error %lu\n", Length, Value[0], Error);

    if (Length == 0)
    {
        ok(Error == ERROR_INVALID_FUNCTION || Error == ERROR_ENVVAR_NOT_FOUND,
           "GetFirmwareEnvironmentVariableW(SecureBoot) failed with %lu\n", Error);
    }
    else
    {
        ok_long(Length, 1);
    }

    FirmwareEnabled = (Length == 1) && (Value[0] == 1);
    SharedEnabled = (SharedUserData->SharedDataFlags & SHARED_GLOBAL_FLAGS_SECURE_BOOT_ENABLED) != 0;
    ok_int(SharedEnabled, FirmwareEnabled);

    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, WasEnabled, FALSE, &Dummy);
}
