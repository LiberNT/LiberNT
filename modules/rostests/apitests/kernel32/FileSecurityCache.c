/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A file's security descriptor is read back as it was last written
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <aclapi.h>

static BOOL
ReadDacl(PCWSTR Path, PACL *Dacl, PSECURITY_DESCRIPTOR *Descriptor)
{
    DWORD Error;

    *Dacl = NULL;
    *Descriptor = NULL;
    Error = GetNamedSecurityInfoW((LPWSTR)Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  NULL, NULL, Dacl, NULL, Descriptor);
    return Error == ERROR_SUCCESS;
}

START_TEST(FileSecurityCache)
{
    WCHAR Directory[MAX_PATH];
    WCHAR Path[MAX_PATH];
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    PACL Dacl = NULL;
    ACL_SIZE_INFORMATION Before = { 0 }, After = { 0 }, Again = { 0 };
    EXPLICIT_ACCESSW Access;
    PACL Extended = NULL;
    HANDLE File;
    DWORD Error;
    ULONG Round;

    if (!GetTempPathW(MAX_PATH, Directory))
    {
        skip("No temporary directory: %lu\n", GetLastError());
        return;
    }
    if (!GetTempFileNameW(Directory, L"sec", 0, Path))
    {
        skip("No temporary file: %lu\n", GetLastError());
        return;
    }

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    CloseHandle(File);

    /* Reading twice must not change what is stored. */
    for (Round = 0; Round < 2; Round++)
    {
        if (!ReadDacl(Path, &Dacl, &Descriptor))
        {
            skip("The volume does not carry per-file security\n");
            DeleteFileW(Path);
            return;
        }
        ok(Dacl != NULL, "No DACL on round %lu\n", Round);
        if (Dacl != NULL)
            ok(GetAclInformation(Dacl, &Before, sizeof(Before), AclSizeInformation),
               "GetAclInformation failed with %lu\n", GetLastError());
        LocalFree(Descriptor);
        Descriptor = NULL;
    }

    /* Grant everyone read access, which adds one entry to the list. */
    RtlZeroMemory(&Access, sizeof(Access));
    Access.grfAccessPermissions = GENERIC_READ;
    Access.grfAccessMode = GRANT_ACCESS;
    Access.grfInheritance = NO_INHERITANCE;
    Access.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    Access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    Access.Trustee.ptstrName = (LPWSTR)L"EVERYONE";

    if (!ReadDacl(Path, &Dacl, &Descriptor))
    {
        DeleteFileW(Path);
        return;
    }
    Error = SetEntriesInAclW(1, &Access, Dacl, &Extended);
    LocalFree(Descriptor);
    Descriptor = NULL;
    if (Error != ERROR_SUCCESS || Extended == NULL)
    {
        skip("SetEntriesInAclW failed with %lu\n", Error);
        DeleteFileW(Path);
        return;
    }

    Error = SetNamedSecurityInfoW(Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  NULL, NULL, Extended, NULL);
    ok(Error == ERROR_SUCCESS, "SetNamedSecurityInfoW failed with %lu\n", Error);
    LocalFree(Extended);

    if (Error == ERROR_SUCCESS)
    {
        /* The write must be visible to the next reader, and to the one after it. */
        if (ReadDacl(Path, &Dacl, &Descriptor))
        {
            ok(GetAclInformation(Dacl, &After, sizeof(After), AclSizeInformation),
               "GetAclInformation failed with %lu\n", GetLastError());
            ok(After.AceCount == Before.AceCount + 1,
               "The descriptor still has %lu entries, expected %lu\n",
               After.AceCount, Before.AceCount + 1);
            LocalFree(Descriptor);
            Descriptor = NULL;
        }

        if (ReadDacl(Path, &Dacl, &Descriptor))
        {
            ok(GetAclInformation(Dacl, &Again, sizeof(Again), AclSizeInformation),
               "GetAclInformation failed with %lu\n", GetLastError());
            ok(Again.AceCount == After.AceCount,
               "A second read returned %lu entries, expected %lu\n",
               Again.AceCount, After.AceCount);
            LocalFree(Descriptor);
        }
    }

    DeleteFileW(Path);
}
