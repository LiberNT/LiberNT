/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The NUL device is a bit bucket every user can open for read and write
 * COPYRIGHT:   Copyright 2026 xor2003
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <sddl.h>

static ACCESS_MASK
AllowedMask(PACL Dacl, WELL_KNOWN_SID_TYPE Type)
{
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    DWORD SidLength = sizeof(Sid);
    ACCESS_MASK Mask = 0;
    PACCESS_ALLOWED_ACE Ace;
    DWORD Index;

    if (!CreateWellKnownSid(Type, NULL, Sid, &SidLength))
        return 0;
    for (Index = 0; Index < Dacl->AceCount; Index++)
    {
        if (GetAce(Dacl, Index, (PVOID *)&Ace) && Ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
            EqualSid(&Ace->SidStart, Sid))
        {
            Mask |= Ace->Mask;
        }
    }
    return Mask;
}

START_TEST(NulDevice)
{
    BYTE AdminSid[SECURITY_MAX_SID_SIZE], Descriptor[1024];
    DWORD SidLength = sizeof(AdminSid), Needed = 0, Written = 0, Read = 0;
    SID_AND_ATTRIBUTES Disabled;
    HANDLE hNul, hToken, hRestricted, hImpersonation;
    BOOL Present = FALSE, Defaulted = FALSE;
    PACL Dacl = NULL;
    CHAR Buffer[8] = { 0 };

    hNul = CreateFileW(L"\\\\.\\NUL", READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(hNul != INVALID_HANDLE_VALUE, "CreateFileW(READ_CONTROL) failed %lu\n", GetLastError());
    if (hNul != INVALID_HANDLE_VALUE)
    {
        ok(GetKernelObjectSecurity(hNul, DACL_SECURITY_INFORMATION, (PSECURITY_DESCRIPTOR)Descriptor, sizeof(Descriptor), &Needed),
           "GetKernelObjectSecurity failed %lu\n", GetLastError());
        ok(GetSecurityDescriptorDacl((PSECURITY_DESCRIPTOR)Descriptor, &Present, &Dacl, &Defaulted) && Present && Dacl,
           "The NUL device has no DACL\n");
        if (Present && Dacl)
        {
            ok(AllowedMask(Dacl, WinWorldSid) == 0x1201bf, "Everyone mask %lx\n", AllowedMask(Dacl, WinWorldSid));
            ok(AllowedMask(Dacl, WinLocalSystemSid) == FILE_ALL_ACCESS, "SYSTEM mask %lx\n", AllowedMask(Dacl, WinLocalSystemSid));
            ok(AllowedMask(Dacl, WinBuiltinAdministratorsSid) == FILE_ALL_ACCESS, "Administrators mask %lx\n",
               AllowedMask(Dacl, WinBuiltinAdministratorsSid));
            ok(AllowedMask(Dacl, WinRestrictedCodeSid) == 0x1200a9, "Restricted mask %lx\n", AllowedMask(Dacl, WinRestrictedCodeSid));
        }
        CloseHandle(hNul);
    }

    /* Open it without the Administrators group */
    ok(CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, AdminSid, &SidLength), "CreateWellKnownSid failed\n");
    Disabled.Sid = AdminSid;
    Disabled.Attributes = 0;
    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hToken), "OpenProcessToken failed %lu\n", GetLastError());
    ok(CreateRestrictedToken(hToken, 0, 1, &Disabled, 0, NULL, 0, NULL, &hRestricted),
       "CreateRestrictedToken failed %lu\n", GetLastError());
    ok(DuplicateToken(hRestricted, SecurityImpersonation, &hImpersonation), "DuplicateToken failed %lu\n", GetLastError());
    ok(SetThreadToken(NULL, hImpersonation), "SetThreadToken failed %lu\n", GetLastError());

    hNul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(hNul != INVALID_HANDLE_VALUE, "CreateFileA(NUL, read/write) without Administrators failed %lu\n", GetLastError());
    if (hNul != INVALID_HANDLE_VALUE)
    {
        ok(WriteFile(hNul, "test", 4, &Written, NULL), "WriteFile failed %lu\n", GetLastError());
        ok(Written == 4, "Written = %lu\n", Written);
        ok(ReadFile(hNul, Buffer, sizeof(Buffer), &Read, NULL), "ReadFile failed %lu\n", GetLastError());
        ok(Read == 0, "Read = %lu\n", Read);
        CloseHandle(hNul);
    }

    RevertToSelf();
    CloseHandle(hImpersonation);
    CloseHandle(hRestricted);
    CloseHandle(hToken);
}
