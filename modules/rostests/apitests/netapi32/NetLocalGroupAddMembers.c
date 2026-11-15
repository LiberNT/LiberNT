/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that NetLocalGroupAddMembers refuses a member the group already has
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <lmcons.h>
#include <lmaccess.h>
#include <lmapibuf.h>
#include <lmerr.h>
#include <sddl.h>

START_TEST(NetLocalGroupAddMembers)
{
    WCHAR Group[256], Domain[256];
    DWORD GroupSize = ARRAYSIZE(Group), DomainSize = ARRAYSIZE(Domain);
    LOCALGROUP_MEMBERS_INFO_0 Member;
    PSID GroupSid = NULL, UserSid = NULL;
    SID_NAME_USE Use;
    NET_API_STATUS Status;

    /* A builtin alias normally empty on a client, and the local system account as the member */
    ok(ConvertStringSidToSidW(L"S-1-5-32-559", &GroupSid), "ConvertStringSidToSidW failed\n");
    ok(ConvertStringSidToSidW(L"S-1-5-18", &UserSid), "ConvertStringSidToSidW failed\n");
    ok(LookupAccountSidW(NULL, GroupSid, Group, &GroupSize, Domain, &DomainSize, &Use), "No group: %lu\n", GetLastError());

    Member.lgrmi0_sid = UserSid;
    Status = NetLocalGroupAddMembers(NULL, Group, 0, (LPBYTE)&Member, 1);
    if (Status == ERROR_ACCESS_DENIED)
    {
        skip("Changing a group needs an administrator\n");
    }
    else
    {
        ok(Status == NERR_Success, "Adding returned %lu\n", Status);
        Status = NetLocalGroupAddMembers(NULL, Group, 0, (LPBYTE)&Member, 1);
        ok(Status == ERROR_MEMBER_IN_ALIAS, "Adding twice returned %lu\n", Status);
        ok(NetLocalGroupDelMembers(NULL, Group, 0, (LPBYTE)&Member, 1) == NERR_Success, "Removing failed\n");
    }

    LocalFree(GroupSid);
    LocalFree(UserSid);
}
