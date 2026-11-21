/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests how SetNamedSecurityInfoW and SetSecurityInfo read the header of a caller's DACL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <aclapi.h>

typedef struct _ACL_CASE
{
    PCSTR Name;
    SECURITY_INFORMATION Information;
    BOOLEAN Directory;
    UCHAR Revision;
    USHORT Size;
    USHORT Count;
    INT Aces;
    DWORD Expected;
    LONG Explicit;
} ACL_CASE;

#define UNPROTECTED (DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION)
#define PROTECTED (DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION)

/* Aces: 0 none, 1 or 2 valid ACEs, -N two ACEs with the first one's size set to N - 1; Explicit -1: the DACL must be left as it was */
static const ACL_CASE Cases[] =
{
    { "zero unprotected file", UNPROTECTED, FALSE, 0, 0, 0, 0, ERROR_SUCCESS, 0 },
    { "zero unprotected directory", UNPROTECTED, TRUE, 0, 0, 0, 0, ERROR_SUCCESS, 0 },
    { "zero plain", DACL_SECURITY_INFORMATION, FALSE, 0, 0, 0, 0, ERROR_SUCCESS, 0 },
    { "zero protected", PROTECTED, FALSE, 0, 0, 0, 0, ERROR_SUCCESS, 0 },
    { "revision 2 size 0", UNPROTECTED, FALSE, 2, 0, 0, 0, ERROR_SUCCESS, 0 },
    { "revision 0 size 8", UNPROTECTED, FALSE, 0, 8, 0, 0, ERROR_SUCCESS, 0 },
    { "revision 0 count 1 size 0", UNPROTECTED, FALSE, 0, 0, 1, 0, ERROR_SUCCESS, 0 },
    { "revision 0 with an ACE", PROTECTED, FALSE, 0, 28, 1, 1, ERROR_SUCCESS, 1 },
    { "revision 1 with an ACE", PROTECTED, FALSE, 1, 28, 1, 1, ERROR_SUCCESS, 1 },
    { "revision 3 with an ACE", PROTECTED, FALSE, 3, 28, 1, 1, ERROR_SUCCESS, 1 },
    { "revision 4 with an ACE", PROTECTED, FALSE, 4, 28, 1, 1, ERROR_SUCCESS, 1 },
    { "revision 5", PROTECTED, FALSE, 5, 28, 1, 1, ERROR_INVALID_ACL, -1 },
    { "revision 255", PROTECTED, FALSE, 255, 28, 1, 1, ERROR_INVALID_ACL, -1 },
    { "size 8 count 1", PROTECTED, FALSE, 2, 8, 1, 1, ERROR_SUCCESS, 1 },
    { "count past the last ACE", PROTECTED, FALSE, 2, 28, 2, 1, ERROR_SUCCESS, 1 },
    { "size 4", PROTECTED, FALSE, 2, 4, 0, 0, ERROR_SUCCESS, 0 },
    { "count 1 of 2", PROTECTED, FALSE, 2, 52, 1, 2, ERROR_SUCCESS, 1 },
    { "size 28 holding 2", PROTECTED, FALSE, 2, 28, 2, 2, ERROR_SUCCESS, 2 },
    { "zero-sized first ACE", PROTECTED, FALSE, 2, 52, 2, -1, ERROR_SUCCESS, 0 },
    { "2-byte first ACE", PROTECTED, FALSE, 2, 52, 2, -3, ERROR_BAD_INHERITANCE_ACL, -1 },
    { "6-byte first ACE", PROTECTED, FALSE, 2, 52, 2, -7, ERROR_BAD_INHERITANCE_ACL, -1 },
    { "revision 0 unprotected with an ACE", UNPROTECTED, FALSE, 0, 28, 1, 1, ERROR_SUCCESS, 1 },
    { "revision 0 size 0 holding 2", UNPROTECTED, FALSE, 0, 0, 2, 2, ERROR_SUCCESS, 2 },
};

typedef struct _DACL_STATE
{
    SECURITY_DESCRIPTOR_CONTROL Control;
    LONG Explicit;
    LONG Inherited;
} DACL_STATE;

static BOOL ReadDacl(PCWSTR Path, DACL_STATE *State)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    PACL Dacl = NULL;
    DWORD Revision;
    DWORD Index;
    PACE_HEADER Ace;

    ZeroMemory(State, sizeof(*State));
    if (GetNamedSecurityInfoW(Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &Dacl, NULL, &Descriptor))
        return FALSE;
    GetSecurityDescriptorControl(Descriptor, &State->Control, &Revision);
    for (Index = 0; Dacl && Index < Dacl->AceCount; Index++)
    {
        if (!GetAce(Dacl, Index, (PVOID *)&Ace))
            break;
        if (Ace->AceFlags & INHERITED_ACE)
            State->Inherited++;
        else
            State->Explicit++;
    }
    LocalFree(Descriptor);
    return TRUE;
}

static void BuildAcl(PACL Acl, ULONG Size, const ACL_CASE *Case, PSID System, PSID Users)
{
    ZeroMemory(Acl, Size);
    InitializeAcl(Acl, Size, ACL_REVISION);
    if (Case->Aces)
        AddAccessAllowedAce(Acl, ACL_REVISION, FILE_GENERIC_READ, System);
    if (Case->Aces > 1 || Case->Aces < 0)
        AddAccessAllowedAce(Acl, ACL_REVISION, FILE_GENERIC_READ, Users);
    if (Case->Aces < 0)
        ((PACE_HEADER)(Acl + 1))->AceSize = (WORD)(-Case->Aces - 1);
    Acl->AclRevision = Case->Revision;
    Acl->AclSize = Case->Size;
    Acl->AceCount = Case->Count;
}

static void CreateObject(PCWSTR Path, BOOLEAN Directory)
{
    if (Directory)
        ok(CreateDirectoryW(Path, NULL), "CreateDirectoryW(%ls) failed with %lu\n", Path, GetLastError());
    else
    {
        HANDLE File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%ls) failed with %lu\n", Path, GetLastError());
        CloseHandle(File);
    }
}

static void RemoveObject(PCWSTR Path, BOOLEAN Directory)
{
    if (Directory)
        RemoveDirectoryW(Path);
    else
        DeleteFileW(Path);
}

START_TEST(SetSecurityInfoAcl)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    PSID System = NULL, Users = NULL;
    WCHAR Base[MAX_PATH], Path[MAX_PATH];
    union
    {
        ACL Acl;
        BYTE Bytes[256];
    } Buffer;
    DACL_STATE Before, After;
    HANDLE File;
    ULONG Index;
    DWORD Error;

    AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &System);
    AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &Users);
    GetTempPathW(ARRAYSIZE(Base), Base);
    wcscat(Base, L"SetSecurityInfoAcl");
    CreateDirectoryW(Base, NULL);
    if (!ReadDacl(Base, &Before) || !Before.Inherited)
    {
        skip("%ls has no inherited DACL\n", Base);
        RemoveDirectoryW(Base);
        FreeSid(System);
        FreeSid(Users);
        return;
    }

    for (Index = 0; Index < ARRAYSIZE(Cases); Index++)
    {
        const ACL_CASE *Case = &Cases[Index];

        swprintf(Path, ARRAYSIZE(Path), L"%ls\\case%lu", Base, Index);
        CreateObject(Path, Case->Directory);
        ok(ReadDacl(Path, &Before), "%s: reading the DACL failed\n", Case->Name);
        BuildAcl(&Buffer.Acl, sizeof(Buffer), Case, System, Users);
        Error = SetNamedSecurityInfoW(Path, SE_FILE_OBJECT, Case->Information, NULL, NULL, &Buffer.Acl, NULL);
        ok(Error == Case->Expected, "%s: SetNamedSecurityInfoW returned %lu, expected %lu\n", Case->Name, Error, Case->Expected);
        ok(ReadDacl(Path, &After), "%s: reading the DACL back failed\n", Case->Name);
        if (Case->Explicit < 0)
        {
            ok(After.Explicit == Before.Explicit, "%s: %ld explicit ACEs, expected %ld\n", Case->Name, After.Explicit, Before.Explicit);
            ok(After.Inherited == Before.Inherited, "%s: %ld inherited ACEs, expected %ld\n", Case->Name, After.Inherited, Before.Inherited);
            ok(!(After.Control & SE_DACL_PROTECTED), "%s: the DACL became protected\n", Case->Name);
        }
        else if (Case->Information & PROTECTED_DACL_SECURITY_INFORMATION)
        {
            ok(After.Explicit == Case->Explicit, "%s: %ld explicit ACEs, expected %ld\n", Case->Name, After.Explicit, Case->Explicit);
            ok(After.Inherited == 0, "%s: %ld inherited ACEs, expected none\n", Case->Name, After.Inherited);
            ok(After.Control & SE_DACL_PROTECTED, "%s: the DACL is not protected, control %04x\n", Case->Name, After.Control);
        }
        else
        {
            ok(After.Explicit == Case->Explicit, "%s: %ld explicit ACEs, expected %ld\n", Case->Name, After.Explicit, Case->Explicit);
            ok(After.Inherited == Before.Inherited, "%s: %ld inherited ACEs, expected %ld\n", Case->Name, After.Inherited, Before.Inherited);
            ok(After.Control & SE_DACL_AUTO_INHERITED, "%s: the DACL is not auto-inherited, control %04x\n", Case->Name, After.Control);
            ok(!(After.Control & SE_DACL_PROTECTED), "%s: the DACL became protected\n", Case->Name);
        }
        RemoveObject(Path, Case->Directory);
    }

    swprintf(Path, ARRAYSIZE(Path), L"%ls\\handle", Base);
    CreateObject(Path, FALSE);
    ok(ReadDacl(Path, &Before), "handle: reading the DACL failed\n");
    File = CreateFileW(Path, READ_CONTROL | WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    ZeroMemory(&Buffer, sizeof(Buffer));
    Error = SetSecurityInfo(File, SE_FILE_OBJECT, UNPROTECTED, NULL, NULL, &Buffer.Acl, NULL);
    ok(Error == ERROR_SUCCESS, "SetSecurityInfo with a zero-filled DACL returned %lu\n", Error);
    CloseHandle(File);
    ok(ReadDacl(Path, &After), "handle: reading the DACL back failed\n");
    ok(After.Explicit == 0, "handle: %ld explicit ACEs\n", After.Explicit);
    ok(After.Inherited == Before.Inherited, "handle: %ld inherited ACEs, expected %ld\n", After.Inherited, Before.Inherited);
    DeleteFileW(Path);

    RemoveDirectoryW(Base);
    FreeSid(System);
    FreeSid(Users);
}
