/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test deny-only and restricting SIDs of filtered tokens
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winbase.h>
#include <sddl.h>
#include <ndk/sefuncs.h>

#define LOW_GROUP_ATTRIBUTES (SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED | SE_GROUP_OWNER)
#define FOREIGN_OWNER L"S-1-5-21-1-2-3-1000"

static const GENERIC_MAPPING FileMapping =
{
    FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS
};

static PVOID
QueryToken(HANDLE Token, TOKEN_INFORMATION_CLASS Class)
{
    DWORD Length = 0;
    PVOID Buffer;

    GetTokenInformation(Token, Class, NULL, 0, &Length);
    if (!Length)
        return NULL;
    Buffer = HeapAlloc(GetProcessHeap(), 0, Length);
    if (Buffer && !GetTokenInformation(Token, Class, Buffer, Length, &Length))
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        Buffer = NULL;
    }
    return Buffer;
}

static PSID
StringSid(PCWSTR String)
{
    PSID Sid = NULL;

    ok(ConvertStringSidToSidW(String, &Sid), "ConvertStringSidToSid(%ls) failed with %lu\n", String, GetLastError());
    return Sid;
}

static DWORD
GroupAttributes(HANDLE Token, PSID Sid)
{
    PTOKEN_GROUPS Groups = QueryToken(Token, TokenGroups);
    DWORD Attributes = 0xFFFFFFFF;
    ULONG Index;

    if (!Groups)
        return Attributes;
    for (Index = 0; Index < Groups->GroupCount; Index++)
    {
        if (EqualSid(Groups->Groups[Index].Sid, Sid))
        {
            Attributes = Groups->Groups[Index].Attributes;
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, Groups);
    return Attributes;
}

static BOOL
OwnerIs(HANDLE Token, PSID Sid)
{
    PTOKEN_OWNER Owner = QueryToken(Token, TokenOwner);
    BOOL Equal = Owner && EqualSid(Owner->Owner, Sid);

    HeapFree(GetProcessHeap(), 0, Owner);
    return Equal;
}

static BOOL
CheckAccess(HANDLE Token, PCWSTR Sddl, ACCESS_MASK Desired, PACCESS_MASK Granted)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    UCHAR PrivilegeBuffer[sizeof(PRIVILEGE_SET) + 4 * sizeof(LUID_AND_ATTRIBUTES)];
    DWORD PrivilegeLength = sizeof(PrivilegeBuffer);
    HANDLE Impersonation = NULL;
    BOOL Status = FALSE;
    BOOL Result;

    *Granted = 0xDEADBEEF;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl, SDDL_REVISION_1, &Descriptor, NULL))
    {
        ok(FALSE, "Converting %ls failed with %lu\n", Sddl, GetLastError());
        return FALSE;
    }
    if (!DuplicateToken(Token, SecurityImpersonation, &Impersonation))
    {
        ok(FALSE, "DuplicateToken failed with %lu\n", GetLastError());
        LocalFree(Descriptor);
        return FALSE;
    }
    MapGenericMask(&Desired, (PGENERIC_MAPPING)&FileMapping);
    Result = AccessCheck(Descriptor, Impersonation, Desired, (PGENERIC_MAPPING)&FileMapping,
                         (PPRIVILEGE_SET)PrivilegeBuffer, &PrivilegeLength, Granted, &Status);
    ok(Result, "AccessCheck(%ls) failed with %lu\n", Sddl, GetLastError());
    CloseHandle(Impersonation);
    LocalFree(Descriptor);
    return Result && Status;
}

static BOOL
IsMember(HANDLE Token, PSID Sid)
{
    HANDLE Impersonation = NULL;
    BOOL Member = 0xDEADBEEF;

    if (!DuplicateToken(Token, SecurityImpersonation, &Impersonation))
    {
        ok(FALSE, "DuplicateToken failed with %lu\n", GetLastError());
        return Member;
    }
    ok(CheckTokenMembership(Impersonation, Sid, &Member), "CheckTokenMembership failed with %lu\n",
       GetLastError());
    CloseHandle(Impersonation);
    return Member;
}

static VOID
TestUserDenyOnly(HANDLE Process, PSID User, PCWSTR UserString)
{
    SID_AND_ATTRIBUTES Disable = { User, SE_GROUP_ENABLED };
    WCHAR Sddl[4][512];
    PTOKEN_USER RestrictedUser;
    PTOKEN_OWNER Owner;
    HANDLE Restricted;
    ACCESS_MASK Granted;
    BOOL Member;

    if (!CreateRestrictedToken(Process, 0, 1, &Disable, 0, NULL, 0, NULL, &Restricted))
    {
        ok(FALSE, "CreateRestrictedToken failed with %lu\n", GetLastError());
        return;
    }

    RestrictedUser = QueryToken(Restricted, TokenUser);
    ok(RestrictedUser != NULL, "Querying TokenUser failed with %lu\n", GetLastError());
    if (RestrictedUser)
    {
        ok(EqualSid(RestrictedUser->User.Sid, User), "The restricted token has another user\n");
        ok(RestrictedUser->User.Attributes == SE_GROUP_USE_FOR_DENY_ONLY,
           "The restricted user has attributes 0x%lx\n", RestrictedUser->User.Attributes);
        HeapFree(GetProcessHeap(), 0, RestrictedUser);
    }

    Owner = QueryToken(Process, TokenOwner);
    ok(Owner && OwnerIs(Restricted, Owner->Owner), "The restricted token has another default owner\n");
    HeapFree(GetProcessHeap(), 0, Owner);

    ok(IsMember(Process, User) == TRUE, "The process token is not a member of its user\n");
    ok(IsMember(Restricted, User) == FALSE, "The restricted token is a member of its deny-only user\n");
    Member = 0xDEADBEEF;
    SetLastError(0xDEADBEEF);
    ok(!CheckTokenMembership(Process, User, &Member), "CheckTokenMembership accepted a primary token\n");
    ok(GetLastError() == ERROR_NO_IMPERSONATION_TOKEN, "A primary token failed with %lu\n", GetLastError());
    ok(Member == FALSE, "A primary token set the result to %d\n", Member);
    if (ImpersonateLoggedOnUser(Restricted))
    {
        ok(CheckTokenMembership(NULL, User, &Member) && Member == FALSE,
           "The impersonated deny-only user is a member (%d, %lu)\n", Member, GetLastError());
        RevertToSelf();
    }
    else
    {
        ok(FALSE, "ImpersonateLoggedOnUser failed with %lu\n", GetLastError());
    }
    ok(CheckTokenMembership(NULL, User, &Member) && Member == TRUE,
       "The process is not a member of its user (%d, %lu)\n", Member, GetLastError());

    StringCchPrintfW(Sddl[0], ARRAYSIZE(Sddl[0]), L"O:" FOREIGN_OWNER L"G:" FOREIGN_OWNER L"D:(A;;FA;;;%s)",
                     UserString);
    StringCchPrintfW(Sddl[1], ARRAYSIZE(Sddl[1]),
                     L"O:" FOREIGN_OWNER L"G:" FOREIGN_OWNER L"D:(D;;FA;;;%s)(A;;FA;;;WD)", UserString);
    StringCchPrintfW(Sddl[2], ARRAYSIZE(Sddl[2]), L"O:%sG:" FOREIGN_OWNER L"D:", UserString);
    StringCchCopyW(Sddl[3], ARRAYSIZE(Sddl[3]), L"O:" FOREIGN_OWNER L"G:" FOREIGN_OWNER L"D:(A;;FA;;;WD)");

    ok(CheckAccess(Process, Sddl[0], FILE_READ_DATA, &Granted), "The user was denied by an allow ACE\n");
    ok(Granted == FILE_READ_DATA, "Granted 0x%lx\n", Granted);
    ok(!CheckAccess(Restricted, Sddl[0], FILE_READ_DATA, &Granted),
       "A deny-only user was granted by an allow ACE\n");
    ok(Granted == 0, "Granted 0x%lx\n", Granted);
    ok(!CheckAccess(Restricted, Sddl[0], MAXIMUM_ALLOWED, &Granted),
       "A deny-only user was granted MAXIMUM_ALLOWED by an allow ACE\n");
    ok(Granted == 0, "Granted 0x%lx\n", Granted);

    ok(CheckAccess(Restricted, Sddl[3], FILE_READ_DATA, &Granted), "Everyone stopped granting the restricted token\n");
    ok(Granted == FILE_READ_DATA, "Granted 0x%lx\n", Granted);
    ok(!CheckAccess(Process, Sddl[1], FILE_READ_DATA, &Granted), "A deny ACE for the user did not deny\n");
    ok(Granted == 0, "Granted 0x%lx\n", Granted);
    ok(!CheckAccess(Restricted, Sddl[1], FILE_READ_DATA, &Granted),
       "A deny ACE for a deny-only user did not deny\n");
    ok(Granted == 0, "Granted 0x%lx\n", Granted);

    ok(CheckAccess(Process, Sddl[2], READ_CONTROL | WRITE_DAC, &Granted),
       "The owner was denied READ_CONTROL and WRITE_DAC\n");
    ok(Granted == (READ_CONTROL | WRITE_DAC), "Granted 0x%lx\n", Granted);
    ok(!CheckAccess(Restricted, Sddl[2], READ_CONTROL, &Granted), "A deny-only owner was granted READ_CONTROL\n");
    ok(Granted == 0, "Granted 0x%lx\n", Granted);

    CloseHandle(Restricted);
}

static VOID
CheckDisabledGroups(HANDLE Token, HANDLE Filtered)
{
    PTOKEN_GROUPS Before = QueryToken(Token, TokenGroups);
    DWORD After;
    DWORD Expected;
    ULONG Index;

    if (!Before)
    {
        ok(FALSE, "Querying TokenGroups failed with %lu\n", GetLastError());
        return;
    }
    for (Index = 0; Index < Before->GroupCount; Index++)
    {
        Expected = Before->Groups[Index].Attributes;
        if (!(Expected & SE_GROUP_INTEGRITY))
            Expected = (Expected & ~LOW_GROUP_ATTRIBUTES) | SE_GROUP_USE_FOR_DENY_ONLY;
        After = GroupAttributes(Filtered, Before->Groups[Index].Sid);
        ok(After == Expected, "Group %lu: attributes 0x%lx -> 0x%lx, expected 0x%lx\n",
           Index, Before->Groups[Index].Attributes, After, Expected);
    }
    HeapFree(GetProcessHeap(), 0, Before);
}

static VOID
DisableAll(HANDLE Token)
{
    PTOKEN_GROUPS Groups = QueryToken(Token, TokenGroups);
    PTOKEN_USER User = QueryToken(Token, TokenUser);
    PSID_AND_ATTRIBUTES Disable;
    HANDLE Filtered;
    ULONG Index;

    if (!Groups || !User)
    {
        ok(FALSE, "Querying the token failed with %lu\n", GetLastError());
        HeapFree(GetProcessHeap(), 0, Groups);
        HeapFree(GetProcessHeap(), 0, User);
        return;
    }
    Disable = HeapAlloc(GetProcessHeap(), 0, (Groups->GroupCount + 1) * sizeof(*Disable));
    if (Disable)
    {
        Disable[0].Sid = User->User.Sid;
        Disable[0].Attributes = 0;
        for (Index = 0; Index < Groups->GroupCount; Index++)
        {
            Disable[Index + 1].Sid = Groups->Groups[Index].Sid;
            Disable[Index + 1].Attributes = 0;
        }
        if (CreateRestrictedToken(Token, 0, Groups->GroupCount + 1, Disable, 0, NULL, 0, NULL, &Filtered))
        {
            PTOKEN_USER FilteredUser = QueryToken(Filtered, TokenUser);

            ok(FilteredUser && FilteredUser->User.Attributes == SE_GROUP_USE_FOR_DENY_ONLY,
               "The user has attributes 0x%lx\n", FilteredUser ? FilteredUser->User.Attributes : 0xFFFFFFFF);
            ok(OwnerIs(Filtered, User->User.Sid), "The default owner is not the user\n");
            CheckDisabledGroups(Token, Filtered);
            HeapFree(GetProcessHeap(), 0, FilteredUser);
            CloseHandle(Filtered);
        }
        else
        {
            ok(FALSE, "CreateRestrictedToken failed with %lu\n", GetLastError());
        }
        HeapFree(GetProcessHeap(), 0, Disable);
    }
    HeapFree(GetProcessHeap(), 0, Groups);
    HeapFree(GetProcessHeap(), 0, User);
}

static HANDLE
CreateTestToken(PSID User, PSID Owner)
{
    static const PCWSTR GroupSids[] =
    {
        L"S-1-1-0", L"S-1-5-5-0-12345", L"S-1-5-21-1-2-3-513", L"S-1-5-32-544"
    };
    static const DWORD GroupAttributesList[] =
    {
        SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED,
        SE_GROUP_LOGON_ID | SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED,
        SE_GROUP_RESOURCE | SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED,
        LOW_GROUP_ATTRIBUTES,
    };
    TOKEN_PRIVILEGES Enable = { 1, { { { 0, 0 }, SE_PRIVILEGE_ENABLED } } };
    struct
    {
        DWORD GroupCount;
        SID_AND_ATTRIBUTES Groups[ARRAYSIZE(GroupSids) + 1];
    } Groups;
    OBJECT_ATTRIBUTES Attributes = { sizeof(Attributes) };
    TOKEN_PRIVILEGES Privileges = { 0 };
    TOKEN_SOURCE Source = { "apitest", { 0, 0 } };
    LUID AuthenticationId = SYSTEM_LUID;
    LARGE_INTEGER Expiration;
    TOKEN_USER TokenUser;
    TOKEN_OWNER TokenOwner;
    TOKEN_PRIMARY_GROUP PrimaryGroup;
    HANDLE Process;
    HANDLE Token = NULL;
    NTSTATUS Status;
    ULONG Index;

    if (!LookupPrivilegeValueW(NULL, L"SeCreateTokenPrivilege", &Enable.Privileges[0].Luid) ||
        !OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Process))
    {
        return NULL;
    }
    AdjustTokenPrivileges(Process, FALSE, &Enable, 0, NULL, NULL);
    Index = GetLastError();
    CloseHandle(Process);
    if (Index != ERROR_SUCCESS)
        return NULL;

    Groups.GroupCount = ARRAYSIZE(Groups.Groups);
    for (Index = 0; Index < ARRAYSIZE(GroupSids); Index++)
    {
        Groups.Groups[Index].Sid = StringSid(GroupSids[Index]);
        Groups.Groups[Index].Attributes = GroupAttributesList[Index];
    }
    Groups.Groups[Index].Sid = Owner;
    Groups.Groups[Index].Attributes = LOW_GROUP_ATTRIBUTES;
    TokenUser.User.Sid = User;
    TokenUser.User.Attributes = 0;
    TokenOwner.Owner = Owner;
    PrimaryGroup.PrimaryGroup = Groups.Groups[0].Sid;
    Expiration.QuadPart = MAXLONGLONG;
    Status = NtCreateToken(&Token, TOKEN_ALL_ACCESS, &Attributes, TokenPrimary, &AuthenticationId, &Expiration,
                           &TokenUser, (PTOKEN_GROUPS)&Groups, &Privileges, &TokenOwner, &PrimaryGroup, NULL,
                           &Source);
    ok(Status == STATUS_SUCCESS, "NtCreateToken returned 0x%lx\n", Status);
    for (Index = 0; Index < ARRAYSIZE(GroupSids); Index++)
        LocalFree(Groups.Groups[Index].Sid);
    return NT_SUCCESS(Status) ? Token : NULL;
}

static VOID
TestCreatedToken(VOID)
{
    PSID User = StringSid(L"S-1-5-21-1-2-3-1001");
    PSID Owner = StringSid(L"S-1-5-21-1-2-3-1002");
    SID_AND_ATTRIBUTES Disable = { Owner, 0 };
    PTOKEN_GROUPS Before;
    HANDLE Token;
    HANDLE Filtered;
    ULONG Index;

    Token = (User && Owner) ? CreateTestToken(User, Owner) : NULL;
    if (!Token)
    {
        skip("Cannot create a token with logon and resource groups\n");
        LocalFree(User);
        LocalFree(Owner);
        return;
    }
    ok(OwnerIs(Token, Owner), "The created token has another default owner\n");

    DisableAll(Token);

    if (CreateRestrictedToken(Token, 0, 1, &Disable, 0, NULL, 0, NULL, &Filtered))
    {
        ok(OwnerIs(Filtered, User), "Disabling the owner group did not move the default owner to the user\n");
        ok(GroupAttributes(Filtered, Owner) == SE_GROUP_USE_FOR_DENY_ONLY, "The owner group has attributes 0x%lx\n",
           GroupAttributes(Filtered, Owner));
        Before = QueryToken(Token, TokenGroups);
        for (Index = 0; Before && Index < Before->GroupCount; Index++)
        {
            if (EqualSid(Before->Groups[Index].Sid, Owner))
                continue;
            ok(GroupAttributes(Filtered, Before->Groups[Index].Sid) == Before->Groups[Index].Attributes,
               "Group %lu changed to 0x%lx\n", Index, GroupAttributes(Filtered, Before->Groups[Index].Sid));
        }
        HeapFree(GetProcessHeap(), 0, Before);
        CloseHandle(Filtered);
    }
    else
    {
        ok(FALSE, "CreateRestrictedToken failed with %lu\n", GetLastError());
    }

    CloseHandle(Token);
    LocalFree(User);
    LocalFree(Owner);
}

static VOID
CheckRestrictedSids(HANDLE Token, ULONG Count, PSID First, PSID Second)
{
    PTOKEN_GROUPS Restricted = QueryToken(Token, TokenRestrictedSids);
    PSID_AND_ATTRIBUTES Entries;
    ULONG Index;

    ok(Restricted != NULL, "Querying TokenRestrictedSids failed with %lu\n", GetLastError());
    if (!Restricted)
        return;
    Entries = Restricted->Groups;
    ok(Restricted->GroupCount == Count, "%lu restricting SIDs, expected %lu\n", Restricted->GroupCount, Count);
    if (Restricted->GroupCount == Count)
    {
        if (Count > 0)
            ok(EqualSid(Entries[0].Sid, First), "Restricting SID 0 differs\n");
        if (Count > 1)
            ok(EqualSid(Entries[1].Sid, Second), "Restricting SID 1 differs\n");
    }
    for (Index = 0; Index < Restricted->GroupCount; Index++)
    {
        ok(Entries[Index].Attributes == (SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED),
           "Restricting SID %lu has attributes 0x%lx\n", Index, Entries[Index].Attributes);
    }
    HeapFree(GetProcessHeap(), 0, Restricted);
}

typedef struct _TWO_GROUPS
{
    DWORD GroupCount;
    SID_AND_ATTRIBUTES Groups[2];
} TWO_GROUPS;

static NTSTATUS
FilterRestricting(HANDLE Token, TWO_GROUPS *Restricting, PHANDLE Filtered)
{
    *Filtered = NULL;
    return NtFilterToken(Token, 0, NULL, NULL, (PTOKEN_GROUPS)Restricting, Filtered);
}

static VOID
ExpectInvalid(HANDLE Token, TWO_GROUPS *Restricting, PCSTR Case)
{
    HANDLE Filtered;
    NTSTATUS Status = FilterRestricting(Token, Restricting, &Filtered);

    ok(Status == STATUS_INVALID_PARAMETER, "%s returned 0x%lx\n", Case, Status);
    if (Filtered)
        CloseHandle(Filtered);
}

static VOID
TestRestrictedSource(HANDLE Restricted, PSID User, PSID Everyone, PSID Authenticated)
{
    TWO_GROUPS Second = { 2, { { Everyone, 0 }, { Authenticated, 0 } } };
    TWO_GROUPS Disjoint = { 1, { { Authenticated, 0 } } };
    TWO_GROUPS Empty = { 0 };
    HANDLE Filtered;
    NTSTATUS Status;

    ok(IsMember(Restricted, User) == TRUE, "A restricting user is not a member\n");
    ok(IsMember(Restricted, Everyone) == TRUE, "A restricting group is not a member\n");
    ok(IsMember(Restricted, Authenticated) == FALSE, "A group outside the restricting SIDs is a member\n");

    Status = FilterRestricting(Restricted, &Second, &Filtered);
    ok(Status == STATUS_SUCCESS, "Restricting a restricted token returned 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        CheckRestrictedSids(Filtered, 1, Everyone, NULL);
        CloseHandle(Filtered);
    }
    ExpectInvalid(Restricted, NULL, "Filtering a restricted token without restricting SIDs");
    ExpectInvalid(Restricted, &Empty, "Filtering a restricted token with no restricting SIDs");
    ExpectInvalid(Restricted, &Disjoint, "A disjoint restricting set");
}

static VOID
TestRestrictingSids(HANDLE Process, PSID User)
{
    PSID Everyone = StringSid(L"S-1-1-0");
    PSID Authenticated = StringSid(L"S-1-5-11");
    TWO_GROUPS First = { 2, { { Everyone, 0 }, { User, 0 } } };
    TWO_GROUPS Attributed = { 1, { { Authenticated, SE_GROUP_ENABLED } } };
    TWO_GROUPS Foreign = { 1, { { StringSid(L"S-1-5-21-1-2-3-4"), 0 } } };
    HANDLE Restricted;
    NTSTATUS Status;

    Status = FilterRestricting(Process, NULL, &Restricted);
    ok(Status == STATUS_SUCCESS, "Filtering without arguments returned 0x%lx\n", Status);
    if (Restricted)
        CloseHandle(Restricted);
    ExpectInvalid(Process, &Attributed, "A restricting SID with attributes");

    Status = FilterRestricting(Process, &First, &Restricted);
    ok(Status == STATUS_SUCCESS, "Restricting the process token returned 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        CheckRestrictedSids(Restricted, 2, Everyone, User);
        TestRestrictedSource(Restricted, User, Everyone, Authenticated);
        CloseHandle(Restricted);
    }

    Status = FilterRestricting(Process, &Foreign, &Restricted);
    ok(Status == STATUS_SUCCESS, "Restricting to a foreign SID returned 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        ok(IsMember(Restricted, Foreign.Groups[0].Sid) == FALSE, "A foreign restricting SID is a member\n");
        ok(IsMember(Restricted, Everyone) == FALSE, "Everyone is a member outside the restricting SIDs\n");
        CloseHandle(Restricted);
    }

    LocalFree(Foreign.Groups[0].Sid);
    LocalFree(Everyone);
    LocalFree(Authenticated);
}

START_TEST(RestrictedTokenUser)
{
    PTOKEN_USER User;
    PWSTR UserString = NULL;
    HANDLE Process;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &Process))
    {
        skip("OpenProcessToken failed with %lu\n", GetLastError());
        return;
    }
    User = QueryToken(Process, TokenUser);
    ok(User != NULL, "Querying TokenUser failed with %lu\n", GetLastError());
    if (User && ConvertSidToStringSidW(User->User.Sid, &UserString))
    {
        ok(User->User.Attributes == 0, "The user has attributes 0x%lx\n", User->User.Attributes);
        TestUserDenyOnly(Process, User->User.Sid, UserString);
        TestRestrictingSids(Process, User->User.Sid);
        LocalFree(UserString);
    }
    DisableAll(Process);
    TestCreatedToken();
    HeapFree(GetProcessHeap(), 0, User);
    CloseHandle(Process);
}
