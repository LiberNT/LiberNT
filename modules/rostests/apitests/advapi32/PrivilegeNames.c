/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Every well-known privilege value has its name, and the name leads back to the value
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

/* The SE_*_NAME strings of winnt.h, indexed from SE_MIN_WELL_KNOWN_PRIVILEGE (2) */
static const PCWSTR WellKnownNames[] =
{
    L"SeCreateTokenPrivilege",
    L"SeAssignPrimaryTokenPrivilege",
    L"SeLockMemoryPrivilege",
    L"SeIncreaseQuotaPrivilege",
    L"SeMachineAccountPrivilege",
    L"SeTcbPrivilege",
    L"SeSecurityPrivilege",
    L"SeTakeOwnershipPrivilege",
    L"SeLoadDriverPrivilege",
    L"SeSystemProfilePrivilege",
    L"SeSystemtimePrivilege",
    L"SeProfileSingleProcessPrivilege",
    L"SeIncreaseBasePriorityPrivilege",
    L"SeCreatePagefilePrivilege",
    L"SeCreatePermanentPrivilege",
    L"SeBackupPrivilege",
    L"SeRestorePrivilege",
    L"SeShutdownPrivilege",
    L"SeDebugPrivilege",
    L"SeAuditPrivilege",
    L"SeSystemEnvironmentPrivilege",
    L"SeChangeNotifyPrivilege",
    L"SeRemoteShutdownPrivilege",
    L"SeUndockPrivilege",
    L"SeSyncAgentPrivilege",
    L"SeEnableDelegationPrivilege",
    L"SeManageVolumePrivilege",
    L"SeImpersonatePrivilege",
    L"SeCreateGlobalPrivilege",
    L"SeTrustedCredManAccessPrivilege",
    L"SeRelabelPrivilege",
    L"SeIncreaseWorkingSetPrivilege",
    L"SeTimeZonePrivilege",
    L"SeCreateSymbolicLinkPrivilege",
    L"SeDelegateSessionUserImpersonatePrivilege",
};

START_TEST(PrivilegeNames)
{
    WCHAR Name[64], DisplayName[256];
    DWORD Length, LanguageId;
    ULONG Index;
    LUID Luid, Back;

    for (Index = 0; Index < RTL_NUMBER_OF(WellKnownNames); Index++)
    {
        Luid.LowPart = 2 + Index;
        Luid.HighPart = 0;

        Length = RTL_NUMBER_OF(Name);
        if (!LookupPrivilegeNameW(NULL, &Luid, Name, &Length))
        {
            ok(FALSE, "LookupPrivilegeNameW(%lu) failed with %lu\n", Luid.LowPart, GetLastError());
            continue;
        }
        ok(wcscmp(Name, WellKnownNames[Index]) == 0, "privilege %lu is named %ls, expected %ls\n",
           Luid.LowPart, Name, WellKnownNames[Index]);
        ok(Length == wcslen(WellKnownNames[Index]), "privilege %lu name length %lu\n", Luid.LowPart, Length);

        ok(LookupPrivilegeValueW(NULL, WellKnownNames[Index], &Back), "LookupPrivilegeValueW(%ls) failed with %lu\n",
           WellKnownNames[Index], GetLastError());
        ok(Back.LowPart == Luid.LowPart && Back.HighPart == 0, "%ls has value %lu.%lu, expected %lu\n",
           WellKnownNames[Index], Back.HighPart, Back.LowPart, Luid.LowPart);

        Length = RTL_NUMBER_OF(DisplayName);
        ok(LookupPrivilegeDisplayNameW(NULL, WellKnownNames[Index], DisplayName, &Length, &LanguageId) && Length != 0,
           "LookupPrivilegeDisplayNameW(%ls) failed with %lu\n", WellKnownNames[Index], GetLastError());
    }
}
