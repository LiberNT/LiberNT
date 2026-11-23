/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test how the test controller's credentials and key are compared and kept from other users
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <aclapi.h>
#include "secret.h"

static BOOL
UsersMayRead(const char *Path)
{
    SID_IDENTIFIER_AUTHORITY Nt = { SECURITY_NT_AUTHORITY };
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    TRUSTEE_A Trustee;
    ACCESS_MASK Rights = 0;
    PSID Users = NULL;
    PACL Dacl = NULL;
    BOOL May;

    if (GetNamedSecurityInfoA((char *)Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &Dacl, NULL,
                              &Descriptor) != ERROR_SUCCESS ||
        !AllocateAndInitializeSid(&Nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0,
                                  &Users))
    {
        LocalFree(Descriptor);
        return TRUE;
    }
    BuildTrusteeWithSidA(&Trustee, Users);
    May = !Dacl || GetEffectiveRightsFromAclA(Dacl, &Trustee, &Rights) != ERROR_SUCCESS || (Rights & FILE_READ_DATA);
    FreeSid(Users);
    LocalFree(Descriptor);
    return May;
}

static VOID
TestProtect(VOID)
{
    char Directory[MAX_PATH], Path[MAX_PATH];
    HANDLE File;

    GetSystemDirectoryA(Directory, sizeof(Directory));
    lstrcatA(Directory, "\\rdpserver");
    if (!GetTempFileNameA(Directory, "rds", 0, Path))
    {
        skip("Cannot create a file in %s: %lu\n", Directory, GetLastError());
        return;
    }
    ok(UsersMayRead(Path), "A new file in %s cannot be read by Users; the test checks nothing\n", Directory);
    ok(ProtectSecretFile(Path), "ProtectSecretFile failed: %lu\n", GetLastError());
    ok(!UsersMayRead(Path), "Users may still read the protected file\n");
    File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "An administrator cannot read the protected file: %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
    ok(DeleteFileA(Path), "Cannot delete %s: %lu\n", Path, GetLastError());
    ok(!ProtectSecretFile(Path), "ProtectSecretFile succeeded on a missing file\n");
}

static VOID
TestEqual(VOID)
{
    ok(SecretEqual("correct horse battery", "correct horse battery"), "The same secret differs\n");
    ok(!SecretEqual("correct horse batterz", "correct horse battery"), "A different last byte matched\n");
    ok(!SecretEqual("correct horse", "correct horse battery"), "A prefix matched\n");
    ok(!SecretEqual("correct horse battery staple", "correct horse battery"), "A longer secret matched\n");
    ok(!SecretEqual("", "correct horse battery"), "An empty secret matched\n");
    ok(SecretEqual("", ""), "Two empty secrets differ\n");
}

START_TEST(ControllerSecret)
{
    TestEqual();
    TestProtect();
}
