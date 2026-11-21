/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the status NtSetSecurityObject and a create return for each malformed descriptor part
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static UCHAR BadSid[16] = { 0, 1, 0, 0, 0, 0, 0, 5, 18, 0, 0, 0 };
static UCHAR SystemSid[12] = { 1, 1, 0, 0, 0, 0, 0, 5, 18, 0, 0, 0 };
static WCHAR Base[MAX_PATH];

static HANDLE OpenCase(PCWSTR Name, ACCESS_MASK Access)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;

    swprintf(Path, ARRAYSIZE(Path), L"%ls\\%ls", Base, Name);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%ls) failed with %lu\n", Path, GetLastError());
    CloseHandle(File);
    File = CreateFileW(Path, Access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "reopening %ls failed with %lu\n", Path, GetLastError());
    return File;
}

static void DeleteCase(PCWSTR Name)
{
    WCHAR Path[MAX_PATH];

    swprintf(Path, ARRAYSIZE(Path), L"%ls\\%ls", Base, Name);
    DeleteFileW(Path);
}

static NTSTATUS SetParts(HANDLE File, SECURITY_INFORMATION Information, PSID Owner, PSID Group, PACL Dacl, PACL Sacl)
{
    SECURITY_DESCRIPTOR Descriptor;

    InitializeSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    Descriptor.Owner = Owner;
    Descriptor.Group = Group;
    if (Dacl)
    {
        Descriptor.Dacl = Dacl;
        Descriptor.Control |= SE_DACL_PRESENT;
    }
    if (Sacl)
    {
        Descriptor.Sacl = Sacl;
        Descriptor.Control |= SE_SACL_PRESENT;
    }
    return NtSetSecurityObject(File, Information, &Descriptor);
}

static void TestSet(PCWSTR Name, SECURITY_INFORMATION Information, PSID Owner, PSID Group, PACL Dacl, PACL Sacl, NTSTATUS Expected)
{
    HANDLE File = OpenCase(Name, READ_CONTROL | WRITE_DAC | WRITE_OWNER);
    NTSTATUS Status = SetParts(File, Information, Owner, Group, Dacl, Sacl);

    ok(Status == Expected, "%ls: NtSetSecurityObject(%08lx) returned 0x%08lx, expected 0x%08lx\n", Name, Information, Status, Expected);
    CloseHandle(File);
    DeleteCase(Name);
}

static void TestCreate(PCWSTR Name, PSID Owner, PACL Dacl, BOOL Directory, DWORD Expected)
{
    SECURITY_DESCRIPTOR Descriptor;
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), &Descriptor, FALSE };
    WCHAR Path[MAX_PATH];
    HANDLE File;
    BOOL Created;

    swprintf(Path, ARRAYSIZE(Path), L"%ls\\%ls", Base, Name);
    InitializeSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    Descriptor.Owner = Owner;
    if (Dacl)
    {
        Descriptor.Dacl = Dacl;
        Descriptor.Control |= SE_DACL_PRESENT;
    }
    if (Directory)
    {
        Created = CreateDirectoryW(Path, &Attributes);
        if (Created)
            RemoveDirectoryW(Path);
    }
    else
    {
        File = CreateFileW(Path, GENERIC_WRITE, 0, &Attributes, CREATE_ALWAYS, 0, NULL);
        Created = File != INVALID_HANDLE_VALUE;
        if (Created)
        {
            CloseHandle(File);
            DeleteFileW(Path);
        }
    }
    ok(!Created, "%ls: the create succeeded\n", Name);
    ok(GetLastError() == Expected, "%ls: the create failed with %lu, expected %lu\n", Name, GetLastError(), Expected);
}

START_TEST(NtSetSecurityObjectStatus)
{
    ACL Zero;
    union
    {
        ACL Acl;
        UCHAR Bytes[64];
    } BadAce;
    HANDLE File;
    NTSTATUS Status;

    GetTempPathW(ARRAYSIZE(Base), Base);
    wcscat(Base, L"NtSetSecurityObjectStatus");
    CreateDirectoryW(Base, NULL);

    RtlZeroMemory(&Zero, sizeof(Zero));
    InitializeAcl(&BadAce.Acl, sizeof(BadAce), ACL_REVISION);
    AddAccessAllowedAce(&BadAce.Acl, ACL_REVISION, GENERIC_READ, (PSID)SystemSid);
    ((PACE_HEADER)(&BadAce.Acl + 1))->AceSize = 4;

    TestSet(L"dacl-zero", DACL_SECURITY_INFORMATION, NULL, NULL, &Zero, NULL, STATUS_INVALID_ACL);
    TestSet(L"dacl-bad-ace", DACL_SECURITY_INFORMATION, NULL, NULL, &BadAce.Acl, NULL, STATUS_INVALID_ACL);
    TestSet(L"owner-bad", OWNER_SECURITY_INFORMATION, (PSID)BadSid, NULL, NULL, NULL, STATUS_INVALID_SID);
    TestSet(L"group-bad", GROUP_SECURITY_INFORMATION, NULL, (PSID)BadSid, NULL, NULL, STATUS_INVALID_SID);
    TestSet(L"owner-dacl-zero", OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, (PSID)SystemSid, NULL, &Zero, NULL, STATUS_INVALID_ACL);
    TestSet(L"dacl-zero-unasked", OWNER_SECURITY_INFORMATION, (PSID)SystemSid, NULL, &Zero, NULL, STATUS_INVALID_ACL);
    TestSet(L"label-zero", LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, &Zero, STATUS_INVALID_ACL);
    TestSet(L"owner-bad-dacl-zero", OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, (PSID)BadSid, NULL, &Zero, NULL, STATUS_INVALID_ACL);
    TestSet(L"group-bad-dacl-zero", GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, (PSID)BadSid, &Zero, NULL, STATUS_INVALID_ACL);
    TestSet(L"owner-group-bad", OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, (PSID)BadSid, (PSID)BadSid, NULL, NULL, STATUS_INVALID_SID);
    TestSet(L"label-zero-owner-bad", LABEL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION, (PSID)BadSid, NULL, NULL, &Zero, STATUS_INVALID_ACL);
    TestSet(L"owner-bad-unasked", DACL_SECURITY_INFORMATION, (PSID)BadSid, NULL, NULL, NULL, STATUS_INVALID_SID);

    /* The handle is checked for the access the information needs before the descriptor is read */
    TestSet(L"sacl-zero", SACL_SECURITY_INFORMATION, NULL, NULL, NULL, &Zero, STATUS_ACCESS_DENIED);
    Status = SetParts((HANDLE)(ULONG_PTR)0x7FFC, DACL_SECURITY_INFORMATION, NULL, NULL, &Zero, NULL);
    ok(Status == STATUS_INVALID_HANDLE, "bad handle with a zero DACL returned 0x%08lx\n", Status);
    Status = NtSetSecurityObject((HANDLE)(ULONG_PTR)0x7FFC, DACL_SECURITY_INFORMATION, NULL);
    ok(Status == STATUS_ACCESS_VIOLATION, "bad handle with no descriptor returned 0x%08lx\n", Status);
    File = OpenCase(L"no-descriptor", READ_CONTROL | WRITE_DAC);
    Status = NtSetSecurityObject(File, DACL_SECURITY_INFORMATION, NULL);
    ok(Status == STATUS_ACCESS_VIOLATION, "no descriptor returned 0x%08lx\n", Status);
    CloseHandle(File);
    DeleteCase(L"no-descriptor");
    File = OpenCase(L"read-only", READ_CONTROL);
    Status = SetParts(File, DACL_SECURITY_INFORMATION, NULL, NULL, &Zero, NULL);
    ok(Status == STATUS_ACCESS_DENIED, "a handle without WRITE_DAC with a zero DACL returned 0x%08lx\n", Status);
    CloseHandle(File);
    DeleteCase(L"read-only");

    TestCreate(L"create-dacl-zero", NULL, &Zero, FALSE, ERROR_INVALID_ACL);
    TestCreate(L"directory-dacl-zero", NULL, &Zero, TRUE, ERROR_INVALID_ACL);
    TestCreate(L"create-owner-bad", (PSID)BadSid, NULL, FALSE, ERROR_INVALID_SID);

    RemoveDirectoryW(Base);
}
