/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the ACL, SID and access check functions of kernelbase
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

#ifndef BACKUP_SECURITY_INFORMATION
#define BACKUP_SECURITY_INFORMATION 0x00010000L
#endif

typedef VOID (WINAPI *FN_QuerySecurityAccessMask)(SECURITY_INFORMATION, LPDWORD);
typedef BOOL (WINAPI *FN_SetAclInformation)(PACL, LPVOID, DWORD, ACL_INFORMATION_CLASS);
typedef BOOL (WINAPI *FN_GetWindowsAccountDomainSid)(PSID, PSID, DWORD *);
typedef BOOL (WINAPI *FN_DeriveCapabilitySidsFromName)(LPCWSTR, PSID **, DWORD *, PSID **, DWORD *);
typedef BOOL (WINAPI *FN_AccessCheckByTypeResultList)(PSECURITY_DESCRIPTOR, PSID, HANDLE, DWORD, POBJECT_TYPE_LIST,
                                                     DWORD, PGENERIC_MAPPING, PPRIVILEGE_SET, LPDWORD, LPDWORD,
                                                     LPDWORD);

static FN_QuerySecurityAccessMask pQuerySecurityAccessMask;
static FN_SetAclInformation pSetAclInformation;
static FN_GetWindowsAccountDomainSid pGetWindowsAccountDomainSid;
static FN_DeriveCapabilitySidsFromName pDeriveCapabilitySidsFromName;
static FN_AccessCheckByTypeResultList pAccessCheckByTypeResultList;

static GENERIC_MAPPING FileMapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};

static
BOOL
QueryMask(
    _In_ SECURITY_INFORMATION Information,
    _Out_ DWORD *Access)
{
    BOOL Raised = FALSE;

    *Access = 0xdeadbeef;
    _SEH2_TRY
    {
        pQuerySecurityAccessMask(Information, Access);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Raised = TRUE;
    }
    _SEH2_END;

    return !Raised;
}

static
VOID
TestQuerySecurityAccessMask(VOID)
{
    static const struct
    {
        SECURITY_INFORMATION Information;
        DWORD Access;
    } Tests[] =
    {
        {0, 0},
        {OWNER_SECURITY_INFORMATION, READ_CONTROL},
        {GROUP_SECURITY_INFORMATION, READ_CONTROL},
        {DACL_SECURITY_INFORMATION, READ_CONTROL},
        {SACL_SECURITY_INFORMATION, ACCESS_SYSTEM_SECURITY},
        {LABEL_SECURITY_INFORMATION, READ_CONTROL},
        {ATTRIBUTE_SECURITY_INFORMATION, READ_CONTROL},
        {SCOPE_SECURITY_INFORMATION, READ_CONTROL},
        {BACKUP_SECURITY_INFORMATION, READ_CONTROL | ACCESS_SYSTEM_SECURITY},
        {PROTECTED_DACL_SECURITY_INFORMATION, 0},
        {PROTECTED_SACL_SECURITY_INFORMATION, 0},
        {UNPROTECTED_DACL_SECURITY_INFORMATION, 0},
        {UNPROTECTED_SACL_SECURITY_INFORMATION, 0},
        {OWNER_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION, READ_CONTROL | ACCESS_SYSTEM_SECURITY},
        {DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, READ_CONTROL},
    };
    DWORD Access;
    ULONG i;

    if (!pQuerySecurityAccessMask)
    {
        skip("QuerySecurityAccessMask not exported by kernelbase\n");
        return;
    }

    for (i = 0; i < ARRAYSIZE(Tests); i++)
    {
        if (!QueryMask(Tests[i].Information, &Access))
        {
            ok(FALSE, "QuerySecurityAccessMask(0x%lx) raised an exception\n", Tests[i].Information);
            return;
        }
        ok(Access == Tests[i].Access, "QuerySecurityAccessMask(0x%lx) returned 0x%lx, expected 0x%lx\n",
           Tests[i].Information, Access, Tests[i].Access);
    }
}

static
VOID
TestSetAclInformation(VOID)
{
    ULONG Buffer[64];
    PACL Acl = (PACL)Buffer;
    ACL_REVISION_INFORMATION Revision;
    ACL_SIZE_INFORMATION Size;
    BOOL Ret;

    if (!pSetAclInformation)
    {
        skip("SetAclInformation not exported by kernelbase\n");
        return;
    }

    ok(InitializeAcl(Acl, sizeof(Buffer), ACL_REVISION), "InitializeAcl failed: %lu\n", GetLastError());

    Revision.AclRevision = ACL_REVISION_DS;
    SetLastError(0xdeadbeef);
    Ret = pSetAclInformation(Acl, &Revision, sizeof(Revision), AclRevisionInformation);
    ok(Ret, "SetAclInformation failed: %lu\n", GetLastError());

    Revision.AclRevision = 0xdeadbeef;
    ok(GetAclInformation(Acl, &Revision, sizeof(Revision), AclRevisionInformation),
       "GetAclInformation failed: %lu\n", GetLastError());
    ok(Revision.AclRevision == ACL_REVISION_DS, "AclRevision %lu\n", Revision.AclRevision);
    ok(Acl->AclRevision == ACL_REVISION_DS, "ACL header revision %u\n", Acl->AclRevision);

    /* Only the revision can be set */
    ZeroMemory(&Size, sizeof(Size));
    SetLastError(0xdeadbeef);
    Ret = pSetAclInformation(Acl, &Size, sizeof(Size), AclSizeInformation);
    ok(!Ret, "SetAclInformation(AclSizeInformation) succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Unexpected error %lu\n", GetLastError());
    ok(Acl->AclRevision == ACL_REVISION_DS, "ACL header revision %u\n", Acl->AclRevision);
}

static
VOID
TestGetWindowsAccountDomainSid(VOID)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    ULONG Domain[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    ULONG Expected[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    PSID Account;
    DWORD Size;
    BOOL Ret;

    if (!pGetWindowsAccountDomainSid)
    {
        skip("GetWindowsAccountDomainSid not exported by kernelbase\n");
        return;
    }

    ok(AllocateAndInitializeSid(&NtAuthority, 5, SECURITY_NT_NON_UNIQUE, 0x11111111, 0x22222222, 0x33333333,
                                DOMAIN_USER_RID_ADMIN, 0, 0, 0, &Account),
       "AllocateAndInitializeSid failed: %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pGetWindowsAccountDomainSid(NULL, NULL, NULL);
    ok(!Ret, "GetWindowsAccountDomainSid succeeded\n");
    ok(GetLastError() == ERROR_INVALID_SID, "Unexpected error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pGetWindowsAccountDomainSid(Account, NULL, NULL);
    ok(!Ret, "GetWindowsAccountDomainSid succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Unexpected error %lu\n", GetLastError());

    Size = sizeof(Domain);
    SetLastError(0xdeadbeef);
    Ret = pGetWindowsAccountDomainSid(Account, NULL, &Size);
    ok(!Ret, "GetWindowsAccountDomainSid succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Unexpected error %lu\n", GetLastError());
    ok(Size == GetSidLengthRequired(4), "Size %lu\n", Size);

    SetLastError(0xdeadbeef);
    Ret = pGetWindowsAccountDomainSid(Account, Domain, NULL);
    ok(!Ret, "GetWindowsAccountDomainSid succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Unexpected error %lu\n", GetLastError());

    Size = 1;
    SetLastError(0xdeadbeef);
    Ret = pGetWindowsAccountDomainSid(Account, Domain, &Size);
    ok(!Ret, "GetWindowsAccountDomainSid succeeded\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Unexpected error %lu\n", GetLastError());
    ok(Size == GetSidLengthRequired(4), "Size %lu\n", Size);

    Size = sizeof(Domain);
    Ret = pGetWindowsAccountDomainSid(Account, Domain, &Size);
    ok(Ret, "GetWindowsAccountDomainSid failed: %lu\n", GetLastError());
    ok(Size == GetSidLengthRequired(4), "Size %lu\n", Size);

    ok(InitializeSid(Expected, &NtAuthority, 4), "InitializeSid failed: %lu\n", GetLastError());
    *GetSidSubAuthority(Expected, 0) = SECURITY_NT_NON_UNIQUE;
    *GetSidSubAuthority(Expected, 1) = 0x11111111;
    *GetSidSubAuthority(Expected, 2) = 0x22222222;
    *GetSidSubAuthority(Expected, 3) = 0x33333333;
    ok(Ret && EqualSid(Domain, Expected), "Unexpected domain SID\n");

    FreeSid(Account);
}

static
VOID
TestDeriveCapabilitySidsFromName(VOID)
{
    static const ULONG Hash[8] =
    {
        0xddd798eb, 0x367bd9d0, 0x1c9e610a, 0x0c43dc7e, 0xe91d8625, 0x395e7cf8, 0xe6e7c3d2, 0x2661e620
    };
    static const SID_IDENTIFIER_AUTHORITY AppAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};
    static const SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    PSID *GroupSids, *CapabilitySids;
    DWORD GroupCount, CapabilityCount;
    PSID Sid;
    BOOL Ret;

    if (!pDeriveCapabilitySidsFromName)
    {
        skip("DeriveCapabilitySidsFromName not exported by kernelbase\n");
        return;
    }

    GroupSids = CapabilitySids = NULL;
    GroupCount = CapabilityCount = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Ret = pDeriveCapabilitySidsFromName(L"__AB", &GroupSids, &GroupCount, &CapabilitySids, &CapabilityCount);
    ok(Ret, "DeriveCapabilitySidsFromName failed: %lu\n", GetLastError());
    ok(GetLastError() == 0xdeadbeef, "Last error changed to %lu\n", GetLastError());
    if (!Ret)
        return;

    ok(GroupCount == 1, "Group SID count %lu\n", GroupCount);
    ok(CapabilityCount == 1, "Capability SID count %lu\n", CapabilityCount);

    Sid = GroupSids[0];
    ok(IsValidSid(Sid), "Invalid group SID\n");
    ok(!memcmp(GetSidIdentifierAuthority(Sid), &NtAuthority, sizeof(NtAuthority)), "Group SID authority mismatch\n");
    ok(*GetSidSubAuthorityCount(Sid) == 9, "Group SID has %u sub-authorities\n", *GetSidSubAuthorityCount(Sid));
    ok(*GetSidSubAuthority(Sid, 0) == SECURITY_BUILTIN_DOMAIN_RID, "Group SID RID0 %lu\n", *GetSidSubAuthority(Sid, 0));
    ok(!memcmp(GetSidSubAuthority(Sid, 1), Hash, sizeof(Hash)), "Group SID hash mismatch\n");

    Sid = CapabilitySids[0];
    ok(IsValidSid(Sid), "Invalid capability SID\n");
    ok(!memcmp(GetSidIdentifierAuthority(Sid), &AppAuthority, sizeof(AppAuthority)),
       "Capability SID authority mismatch\n");
    ok(*GetSidSubAuthorityCount(Sid) == 10, "Capability SID has %u sub-authorities\n", *GetSidSubAuthorityCount(Sid));
    ok(*GetSidSubAuthority(Sid, 0) == SECURITY_CAPABILITY_BASE_RID, "Capability SID RID0 %lu\n",
       *GetSidSubAuthority(Sid, 0));
    ok(*GetSidSubAuthority(Sid, 1) == SECURITY_CAPABILITY_APP_RID, "Capability SID RID1 %lu\n",
       *GetSidSubAuthority(Sid, 1));
    ok(!memcmp(GetSidSubAuthority(Sid, 2), Hash, sizeof(Hash)), "Capability SID hash mismatch\n");

    /* The caller frees the SIDs and the arrays with LocalFree */
    ok(LocalFree(GroupSids[0]) == NULL, "LocalFree failed: %lu\n", GetLastError());
    ok(LocalFree(GroupSids) == NULL, "LocalFree failed: %lu\n", GetLastError());
    ok(LocalFree(CapabilitySids[0]) == NULL, "LocalFree failed: %lu\n", GetLastError());
    ok(LocalFree(CapabilitySids) == NULL, "LocalFree failed: %lu\n", GetLastError());
}

typedef struct _CHECK_SD
{
    SECURITY_DESCRIPTOR Sd;
    ULONG Guests[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    ULONG Acl[64];
} CHECK_SD;

static
VOID
BuildCheckSd(
    _Out_ CHECK_SD *Check,
    _In_ BOOL WithOwner,
    _In_ BOOL WithDacl)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
    ULONG World[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    PACL Acl = (PACL)Check->Acl;

    ok(InitializeSid(Check->Guests, &NtAuthority, 2), "InitializeSid failed: %lu\n", GetLastError());
    *GetSidSubAuthority(Check->Guests, 0) = SECURITY_BUILTIN_DOMAIN_RID;
    *GetSidSubAuthority(Check->Guests, 1) = DOMAIN_ALIAS_RID_GUESTS;
    ok(InitializeSid(World, &WorldAuthority, 1), "InitializeSid failed: %lu\n", GetLastError());
    *GetSidSubAuthority(World, 0) = SECURITY_WORLD_RID;

    ok(InitializeAcl(Acl, sizeof(Check->Acl), ACL_REVISION), "InitializeAcl failed: %lu\n", GetLastError());
    ok(AddAccessAllowedAce(Acl, ACL_REVISION, FILE_GENERIC_READ, World),
       "AddAccessAllowedAce failed: %lu\n", GetLastError());
    ok(InitializeSecurityDescriptor(&Check->Sd, SECURITY_DESCRIPTOR_REVISION),
       "InitializeSecurityDescriptor failed: %lu\n", GetLastError());
    ok(SetSecurityDescriptorDacl(&Check->Sd, TRUE, WithDacl ? Acl : NULL, FALSE),
       "SetSecurityDescriptorDacl failed: %lu\n", GetLastError());
    if (WithOwner)
    {
        ok(SetSecurityDescriptorOwner(&Check->Sd, Check->Guests, FALSE),
           "SetSecurityDescriptorOwner failed: %lu\n", GetLastError());
        ok(SetSecurityDescriptorGroup(&Check->Sd, Check->Guests, FALSE),
           "SetSecurityDescriptorGroup failed: %lu\n", GetLastError());
    }
}

static
BOOL
CheckResultList(
    _In_ PSECURITY_DESCRIPTOR Sd,
    _In_ HANDLE Token,
    _In_ DWORD Desired,
    _In_ POBJECT_TYPE_LIST Types,
    _Out_writes_(2) DWORD *Granted,
    _Out_writes_(2) DWORD *Status)
{
    PRIVILEGE_SET Privileges;
    DWORD PrivilegesLength = sizeof(Privileges);

    Granted[0] = Granted[1] = 0xdeadbeef;
    Status[0] = Status[1] = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    return pAccessCheckByTypeResultList(Sd, NULL, Token, Desired, Types, 2, &FileMapping, &Privileges,
                                        &PrivilegesLength, Granted, Status);
}

static
VOID
TestAccessCheckByTypeResultList(VOID)
{
    static GUID ObjectGuid = {0x6d1e7a4c, 0x5b0f, 0x4c8e, {0x9a, 0x51, 0x3f, 0x2e, 0x10, 0x7b, 0x44, 0x01}};
    static GUID PropertyGuid = {0x6d1e7a4c, 0x5b0f, 0x4c8e, {0x9a, 0x51, 0x3f, 0x2e, 0x10, 0x7b, 0x44, 0x02}};
    OBJECT_TYPE_LIST Types[2], Unordered[2];
    CHECK_SD Check, NoOwner, NullDacl;
    HANDLE Token, NoQueryToken;
    DWORD Granted[2], Status[2];
    BOOL Ret;

    if (!pAccessCheckByTypeResultList)
    {
        skip("AccessCheckByTypeResultList not exported by kernelbase\n");
        return;
    }

    BuildCheckSd(&Check, TRUE, TRUE);
    BuildCheckSd(&NoOwner, FALSE, TRUE);
    BuildCheckSd(&NullDacl, TRUE, FALSE);

    Types[0].Level = ACCESS_OBJECT_GUID;
    Types[0].Sbz = 0;
    Types[0].ObjectType = &ObjectGuid;
    Types[1].Level = ACCESS_PROPERTY_SET_GUID;
    Types[1].Sbz = 0;
    Types[1].ObjectType = &PropertyGuid;
    Unordered[0] = Types[1];
    Unordered[1] = Types[0];

    ok(ImpersonateSelf(SecurityImpersonation), "ImpersonateSelf failed: %lu\n", GetLastError());
    ok(OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &Token), "OpenThreadToken failed: %lu\n", GetLastError());
    ok(OpenThreadToken(GetCurrentThread(), TOKEN_IMPERSONATE, TRUE, &NoQueryToken),
       "OpenThreadToken failed: %lu\n", GetLastError());
    ok(RevertToSelf(), "RevertToSelf failed: %lu\n", GetLastError());

    Ret = CheckResultList(&Check.Sd, Token, FILE_READ_DATA, Types, Granted, Status);
    ok(Ret, "AccessCheckByTypeResultList failed: %lu\n", GetLastError());
    ok(Granted[0] == FILE_READ_DATA && Granted[1] == FILE_READ_DATA,
       "Granted 0x%lx 0x%lx\n", Granted[0], Granted[1]);
    ok(Status[0] == 0 && Status[1] == 0, "Status %lu %lu\n", Status[0], Status[1]);

    Ret = CheckResultList(&Check.Sd, Token, FILE_WRITE_DATA, Types, Granted, Status);
    ok(Ret, "AccessCheckByTypeResultList failed: %lu\n", GetLastError());
    ok(Granted[0] == 0 && Granted[1] == 0, "Granted 0x%lx 0x%lx\n", Granted[0], Granted[1]);
    ok(Status[0] == ERROR_ACCESS_DENIED && Status[1] == ERROR_ACCESS_DENIED,
       "Status %lu %lu\n", Status[0], Status[1]);

    /* A NULL DACL grants the requested access to every element */
    Ret = CheckResultList(&NullDacl.Sd, Token, FILE_WRITE_DATA, Types, Granted, Status);
    ok(Ret, "AccessCheckByTypeResultList failed: %lu\n", GetLastError());
    ok(Granted[0] == FILE_WRITE_DATA && Granted[1] == FILE_WRITE_DATA,
       "Granted 0x%lx 0x%lx\n", Granted[0], Granted[1]);
    ok(Status[0] == 0 && Status[1] == 0, "Status %lu %lu\n", Status[0], Status[1]);

    /* Failures leave both arrays as they were */
    Ret = CheckResultList(&NoOwner.Sd, Token, FILE_READ_DATA, Types, Granted, Status);
    ok(!Ret, "AccessCheckByTypeResultList succeeded without owner\n");
    ok(GetLastError() == ERROR_INVALID_SECURITY_DESCR, "Unexpected error %lu\n", GetLastError());
    ok(Granted[0] == 0xdeadbeef && Granted[1] == 0xdeadbeef, "Granted 0x%lx 0x%lx\n", Granted[0], Granted[1]);
    ok(Status[0] == 0xdeadbeef && Status[1] == 0xdeadbeef, "Status 0x%lx 0x%lx\n", Status[0], Status[1]);

    Ret = CheckResultList(&Check.Sd, Token, FILE_READ_DATA, Unordered, Granted, Status);
    ok(!Ret, "AccessCheckByTypeResultList succeeded with an unordered list\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Unexpected error %lu\n", GetLastError());
    ok(Granted[0] == 0xdeadbeef && Granted[1] == 0xdeadbeef, "Granted 0x%lx 0x%lx\n", Granted[0], Granted[1]);
    ok(Status[0] == 0xdeadbeef && Status[1] == 0xdeadbeef, "Status 0x%lx 0x%lx\n", Status[0], Status[1]);

    Ret = CheckResultList(&Check.Sd, NoQueryToken, FILE_READ_DATA, Types, Granted, Status);
    ok(!Ret, "AccessCheckByTypeResultList succeeded without TOKEN_QUERY\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "Unexpected error %lu\n", GetLastError());
    ok(Granted[0] == 0xdeadbeef && Granted[1] == 0xdeadbeef, "Granted 0x%lx 0x%lx\n", Granted[0], Granted[1]);
    ok(Status[0] == 0xdeadbeef && Status[1] == 0xdeadbeef, "Status 0x%lx 0x%lx\n", Status[0], Status[1]);

    CloseHandle(NoQueryToken);
    CloseHandle(Token);
}

START_TEST(SecurityBaseApis)
{
    HMODULE KernelBase = LoadLibraryW(L"kernelbase.dll");

    ok(KernelBase != NULL, "LoadLibraryW(kernelbase) failed: %lu\n", GetLastError());
    if (!KernelBase)
        return;

    pQuerySecurityAccessMask = (FN_QuerySecurityAccessMask)GetProcAddress(KernelBase, "QuerySecurityAccessMask");
    pSetAclInformation = (FN_SetAclInformation)GetProcAddress(KernelBase, "SetAclInformation");
    pGetWindowsAccountDomainSid = (FN_GetWindowsAccountDomainSid)GetProcAddress(KernelBase, "GetWindowsAccountDomainSid");
    pDeriveCapabilitySidsFromName = (FN_DeriveCapabilitySidsFromName)GetProcAddress(KernelBase, "DeriveCapabilitySidsFromName");
    pAccessCheckByTypeResultList = (FN_AccessCheckByTypeResultList)GetProcAddress(KernelBase, "AccessCheckByTypeResultList");

    TestQuerySecurityAccessMask();
    TestSetAclInformation();
    TestGetWindowsAccountDomainSid();
    TestDeriveCapabilitySidsFromName();
    TestAccessCheckByTypeResultList();
}
