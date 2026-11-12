/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Every well-known privilege value has its name and its own display name, and the name leads back to the value
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

/* The SE_*_NAME strings of winnt.h, indexed from SE_MIN_WELL_KNOWN_PRIVILEGE (2), with their user right names in English (Privilege Constants) */
static const struct
{
    PCWSTR Name;
    PCWSTR DisplayName;
} WellKnownNames[] =
{
    {L"SeCreateTokenPrivilege", L"Create a token object"},
    {L"SeAssignPrimaryTokenPrivilege", L"Replace a process level token"},
    {L"SeLockMemoryPrivilege", L"Lock pages in memory"},
    {L"SeIncreaseQuotaPrivilege", L"Adjust memory quotas for a process"},
    {L"SeMachineAccountPrivilege", L"Add workstations to domain"},
    {L"SeTcbPrivilege", L"Act as part of the operating system"},
    {L"SeSecurityPrivilege", L"Manage auditing and security log"},
    {L"SeTakeOwnershipPrivilege", L"Take ownership of files or other objects"},
    {L"SeLoadDriverPrivilege", L"Load and unload device drivers"},
    {L"SeSystemProfilePrivilege", L"Profile system performance"},
    {L"SeSystemtimePrivilege", L"Change the system time"},
    {L"SeProfileSingleProcessPrivilege", L"Profile single process"},
    {L"SeIncreaseBasePriorityPrivilege", L"Increase scheduling priority"},
    {L"SeCreatePagefilePrivilege", L"Create a pagefile"},
    {L"SeCreatePermanentPrivilege", L"Create permanent shared objects"},
    {L"SeBackupPrivilege", L"Back up files and directories"},
    {L"SeRestorePrivilege", L"Restore files and directories"},
    {L"SeShutdownPrivilege", L"Shut down the system"},
    {L"SeDebugPrivilege", L"Debug programs"},
    {L"SeAuditPrivilege", L"Generate security audits"},
    {L"SeSystemEnvironmentPrivilege", L"Modify firmware environment values"},
    {L"SeChangeNotifyPrivilege", L"Bypass traverse checking"},
    {L"SeRemoteShutdownPrivilege", L"Force shutdown from a remote system"},
    {L"SeUndockPrivilege", L"Remove computer from docking station"},
    {L"SeSyncAgentPrivilege", L"Synchronize directory service data"},
    {L"SeEnableDelegationPrivilege", L"Enable computer and user accounts to be trusted for delegation"},
    {L"SeManageVolumePrivilege", L"Perform volume maintenance tasks"},
    {L"SeImpersonatePrivilege", L"Impersonate a client after authentication"},
    {L"SeCreateGlobalPrivilege", L"Create global objects"},
    {L"SeTrustedCredManAccessPrivilege", L"Access Credential Manager as a trusted caller"},
    {L"SeRelabelPrivilege", L"Modify an object label"},
    {L"SeIncreaseWorkingSetPrivilege", L"Increase a process working set"},
    {L"SeTimeZonePrivilege", L"Change the time zone"},
    {L"SeCreateSymbolicLinkPrivilege", L"Create symbolic links"},
    {L"SeDelegateSessionUserImpersonatePrivilege", L"Obtain an impersonation token for another user in the same session"},
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
        ok(wcscmp(Name, WellKnownNames[Index].Name) == 0, "privilege %lu is named %ls, expected %ls\n",
           Luid.LowPart, Name, WellKnownNames[Index].Name);
        ok(Length == wcslen(WellKnownNames[Index].Name), "privilege %lu name length %lu\n", Luid.LowPart, Length);

        ok(LookupPrivilegeValueW(NULL, WellKnownNames[Index].Name, &Back), "LookupPrivilegeValueW(%ls) failed with %lu\n",
           WellKnownNames[Index].Name, GetLastError());
        ok(Back.LowPart == Luid.LowPart && Back.HighPart == 0, "%ls has value %lu.%lu, expected %lu\n",
           WellKnownNames[Index].Name, Back.HighPart, Back.LowPart, Luid.LowPart);

        Length = RTL_NUMBER_OF(DisplayName);
        if (!LookupPrivilegeDisplayNameW(NULL, WellKnownNames[Index].Name, DisplayName, &Length, &LanguageId))
        {
            ok(FALSE, "LookupPrivilegeDisplayNameW(%ls) failed with %lu\n", WellKnownNames[Index].Name, GetLastError());
            continue;
        }
        ok(Length == wcslen(DisplayName), "%ls display name length %lu\n", WellKnownNames[Index].Name, Length);
        if (PRIMARYLANGID(LanguageId) == LANG_ENGLISH)
        {
            ok(wcscmp(DisplayName, WellKnownNames[Index].DisplayName) == 0, "%ls is shown as \"%ls\", expected \"%ls\"\n",
               WellKnownNames[Index].Name, DisplayName, WellKnownNames[Index].DisplayName);
        }
    }
}
