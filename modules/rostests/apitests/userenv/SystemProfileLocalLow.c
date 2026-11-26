/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The system profile has a low integrity AppData\LocalLow
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winreg.h>
#include <aclapi.h>
#include <sddl.h>

START_TEST(SystemProfileLocalLow)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    PACL Sacl = NULL;
    PSYSTEM_MANDATORY_LABEL_ACE Ace;
    BYTE LowSid[SECURITY_MAX_SID_SIZE];
    DWORD SidLength = sizeof(LowSid), Attributes, Error;
    WCHAR Path[MAX_PATH];

    ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\config\\systemprofile\\AppData\\LocalLow", Path, MAX_PATH);
    Attributes = GetFileAttributesW(Path);
    if (Attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_ACCESS_DENIED)
    {
        skip("Reading the system profile needs administrator rights\n");
        return;
    }
    ok(Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY),
       "%ls is missing (attributes %lx, error %lu)\n", Path, Attributes, GetLastError());
    if (Attributes == INVALID_FILE_ATTRIBUTES)
        return;

    Error = GetNamedSecurityInfoW(Path, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, &Sacl, &Descriptor);
    ok(Error == ERROR_SUCCESS, "GetNamedSecurityInfoW failed %lu\n", Error);
    if (Error != ERROR_SUCCESS)
        return;
    ok(CreateWellKnownSid(WinLowLabelSid, NULL, LowSid, &SidLength), "CreateWellKnownSid failed %lu\n", GetLastError());
    ok(Sacl != NULL && Sacl->AceCount == 1, "Label ACL %p, %u entries\n", Sacl, Sacl ? Sacl->AceCount : 0);
    if (Sacl && Sacl->AceCount == 1 && GetAce(Sacl, 0, (PVOID *)&Ace))
    {
        ok(Ace->Header.AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE, "ACE type %u\n", Ace->Header.AceType);
        ok(Ace->Header.AceFlags == (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE), "ACE flags %x\n", Ace->Header.AceFlags);
        ok(Ace->Mask == SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, "ACE mask %lx\n", Ace->Mask);
        ok(EqualSid(&Ace->SidStart, LowSid), "The label is not low integrity\n");
    }
    LocalFree(Descriptor);
}
