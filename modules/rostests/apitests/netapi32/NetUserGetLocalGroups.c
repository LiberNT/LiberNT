/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that NetUserGetLocalGroups takes a user as DomainName\UserName
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
#include <stdio.h>

static DWORD
CountLocalGroups(PCWSTR User, NET_API_STATUS *Status)
{
    LPBYTE Buffer = NULL;
    DWORD Read = 0, Total = 0;

    *Status = NetUserGetLocalGroups(NULL, User, 0, 0, &Buffer, MAX_PREFERRED_LENGTH, &Read, &Total);
    if (Buffer)
        NetApiBufferFree(Buffer);
    return Read;
}

START_TEST(NetUserGetLocalGroups)
{
    WCHAR Computer[MAX_COMPUTERNAME_LENGTH + 1], Qualified[MAX_COMPUTERNAME_LENGTH + UNLEN + 2];
    DWORD Size = ARRAYSIZE(Computer), Read = 0, Total = 0, Plain, Domain;
    USER_INFO_0 *Users = NULL;
    NET_API_STATUS Status;

    Status = NetUserEnum(NULL, 0, FILTER_NORMAL_ACCOUNT, (LPBYTE *)&Users, MAX_PREFERRED_LENGTH, &Read, &Total, NULL);
    ok(Status == NERR_Success && Read != 0, "NetUserEnum failed: %lu, %lu users\n", Status, Read);
    if (Status != NERR_Success || Read == 0)
    {
        if (Users)
            NetApiBufferFree(Users);
        return;
    }

    ok(GetComputerNameW(Computer, &Size), "GetComputerNameW failed: %lu\n", GetLastError());
    _snwprintf(Qualified, ARRAYSIZE(Qualified) - 1, L"%s\\%s", Computer, Users[0].usri0_name);
    Qualified[ARRAYSIZE(Qualified) - 1] = UNICODE_NULL;

    Plain = CountLocalGroups(Users[0].usri0_name, &Status);
    ok(Status == NERR_Success, "NetUserGetLocalGroups(%ls) failed: %lu\n", Users[0].usri0_name, Status);

    /* The local account domain carries the computer's name */
    Domain = CountLocalGroups(Qualified, &Status);
    ok(Status == NERR_Success, "NetUserGetLocalGroups(%ls) failed: %lu\n", Qualified, Status);
    ok(Domain == Plain, "%ls is in %lu groups, %ls in %lu\n", Qualified, Domain, Users[0].usri0_name, Plain);

    NetApiBufferFree(Users);
}
