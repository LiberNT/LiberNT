/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Keep the test controller's credentials and key away from other users
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "secret.h"
#include <aclapi.h>
#include <sddl.h>
#include <string.h>

BOOL ProtectSecretFile(const char *path)
{
    PSECURITY_DESCRIPTOR descriptor = NULL;
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = NULL;
    DWORD error;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1,
                                                              &descriptor, NULL))
        return FALSE;
    if (!GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) || !present)
    {
        LocalFree(descriptor);
        return FALSE;
    }
    error = SetNamedSecurityInfoA((char *)path, SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, dacl,
                                  NULL);
    LocalFree(descriptor);
    SetLastError(error);
    return error == ERROR_SUCCESS;
}

BOOL SecretEqual(const char *given, const char *expected)
{
    size_t givenLength = strlen(given), expectedLength = strlen(expected), i;
    unsigned char difference = givenLength != expectedLength;
    for (i = 0; i < expectedLength; ++i)
        difference |= (unsigned char)(expected[i] ^ (i < givenLength ? given[i] : 0));
    return difference == 0;
}
