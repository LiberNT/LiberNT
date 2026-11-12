/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A process of the LocalSystem account holds the privileges the account is documented with, in their documented states
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

/* "LocalSystem Account", Win32 services documentation */
static const struct
{
    PCWSTR Name;
    BOOL Enabled;
} LocalSystemPrivileges[] =
{
    {L"SeAssignPrimaryTokenPrivilege", FALSE},
    {L"SeAuditPrivilege", TRUE},
    {L"SeBackupPrivilege", FALSE},
    {L"SeChangeNotifyPrivilege", TRUE},
    {L"SeCreateGlobalPrivilege", TRUE},
    {L"SeCreatePagefilePrivilege", TRUE},
    {L"SeCreatePermanentPrivilege", TRUE},
    {L"SeCreateTokenPrivilege", FALSE},
    {L"SeDebugPrivilege", TRUE},
    {L"SeImpersonatePrivilege", TRUE},
    {L"SeIncreaseBasePriorityPrivilege", TRUE},
    {L"SeIncreaseQuotaPrivilege", FALSE},
    {L"SeLoadDriverPrivilege", FALSE},
    {L"SeLockMemoryPrivilege", TRUE},
    {L"SeManageVolumePrivilege", FALSE},
    {L"SeProfileSingleProcessPrivilege", TRUE},
    {L"SeRestorePrivilege", FALSE},
    {L"SeSecurityPrivilege", FALSE},
    {L"SeShutdownPrivilege", FALSE},
    {L"SeSystemEnvironmentPrivilege", FALSE},
    {L"SeSystemtimePrivilege", FALSE},
    {L"SeTakeOwnershipPrivilege", FALSE},
    {L"SeTcbPrivilege", TRUE},
    {L"SeUndockPrivilege", FALSE},
};

static
PVOID
QueryToken(
    _In_ HANDLE Token,
    _In_ TOKEN_INFORMATION_CLASS Class)
{
    DWORD Length = 0;
    PVOID Buffer;

    GetTokenInformation(Token, Class, NULL, 0, &Length);
    if (Length == 0)
        return NULL;

    Buffer = HeapAlloc(GetProcessHeap(), 0, Length);
    if (Buffer != NULL && !GetTokenInformation(Token, Class, Buffer, Length, &Length))
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        Buffer = NULL;
    }

    return Buffer;
}

static
VOID
CheckPrivilegeStates(
    _In_ PTOKEN_PRIVILEGES Privileges)
{
    ULONG Index, Held;
    LUID Luid;

    for (Index = 0; Index < RTL_NUMBER_OF(LocalSystemPrivileges); Index++)
    {
        if (!LookupPrivilegeValueW(NULL, LocalSystemPrivileges[Index].Name, &Luid))
        {
            ok(FALSE, "LookupPrivilegeValueW(%ls) failed with %lu\n", LocalSystemPrivileges[Index].Name, GetLastError());
            continue;
        }

        for (Held = 0; Held < Privileges->PrivilegeCount; Held++)
        {
            if (Privileges->Privileges[Held].Luid.LowPart == Luid.LowPart &&
                Privileges->Privileges[Held].Luid.HighPart == Luid.HighPart)
            {
                break;
            }
        }

        if (Held == Privileges->PrivilegeCount)
        {
            ok(FALSE, "%ls is not held\n", LocalSystemPrivileges[Index].Name);
            continue;
        }

        ok(((Privileges->Privileges[Held].Attributes & SE_PRIVILEGE_ENABLED) != 0) == LocalSystemPrivileges[Index].Enabled,
           "%ls attributes 0x%lx, expected it %ls\n",
           LocalSystemPrivileges[Index].Name,
           Privileges->Privileges[Held].Attributes,
           LocalSystemPrivileges[Index].Enabled ? L"enabled" : L"disabled");
    }
}

static
VOID
CheckToken(
    _In_ HANDLE Token)
{
    PTOKEN_PRIVILEGES Privileges;
    PTOKEN_USER User;
    BOOL LocalSystem;

    User = QueryToken(Token, TokenUser);
    ok(User != NULL, "TokenUser query failed with %lu\n", GetLastError());
    if (User == NULL)
        return;

    LocalSystem = IsWellKnownSid(User->User.Sid, WinLocalSystemSid);
    HeapFree(GetProcessHeap(), 0, User);
    if (!LocalSystem)
    {
        skip("Not running as LocalSystem\n");
        return;
    }

    Privileges = QueryToken(Token, TokenPrivileges);
    ok(Privileges != NULL, "TokenPrivileges query failed with %lu\n", GetLastError());
    if (Privileges == NULL)
        return;

    CheckPrivilegeStates(Privileges);
    HeapFree(GetProcessHeap(), 0, Privileges);
}

START_TEST(LocalSystemPrivileges)
{
    HANDLE Token;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
    {
        ok(FALSE, "OpenProcessToken failed with %lu\n", GetLastError());
        return;
    }

    CheckToken(Token);
    CloseHandle(Token);
}
