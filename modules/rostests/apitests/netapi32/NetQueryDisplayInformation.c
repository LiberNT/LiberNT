/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for NetQueryDisplayInformation and NetGetDisplayInformationIndex
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

static PNET_DISPLAY_USER
FindUser(PNET_DISPLAY_USER Users, DWORD Count, DWORD Rid)
{
    for (DWORD i = 0; i < Count; i++)
    {
        if (Users[i].usri1_user_id == Rid)
            return &Users[i];
    }
    return NULL;
}

static VOID
TestUsers(VOID)
{
    PNET_DISPLAY_USER Users = NULL, Page = NULL;
    DWORD Count = 0, PageCount = 0, i, Next;
    NET_API_STATUS Status;

    Status = NetQueryDisplayInformation(NULL, 1, 0, 100, MAX_PREFERRED_LENGTH, &Count, (PVOID *)&Users);
    ok(Status == NERR_Success, "Level 1 returned %lu\n", Status);
    ok(Count >= 2, "Level 1 returned %lu users\n", Count);
    if (Status != NERR_Success || Users == NULL)
        return;

    PNET_DISPLAY_USER Admin = FindUser(Users, Count, DOMAIN_USER_RID_ADMIN);
    ok(Admin != NULL, "No user with RID 500\n");
    if (Admin != NULL)
    {
        ok(Admin->usri1_name != NULL && Admin->usri1_name[0] != UNICODE_NULL, "RID 500 has no name\n");
        ok(Admin->usri1_flags & UF_NORMAL_ACCOUNT, "RID 500 flags 0x%lx\n", Admin->usri1_flags);
        ok(Admin->usri1_comment != NULL, "RID 500 has no comment string\n");
        ok(Admin->usri1_full_name != NULL, "RID 500 has no full name string\n");
    }

    for (i = 1; i < Count; i++)
    {
        ok(_wcsicmp(Users[i - 1].usri1_name, Users[i].usri1_name) < 0,
           "%ls sorts after %ls\n", Users[i - 1].usri1_name, Users[i].usri1_name);
        ok(Users[i].usri1_next_index > Users[i - 1].usri1_next_index,
           "Next index %lu after %lu\n", Users[i].usri1_next_index, Users[i - 1].usri1_next_index);
    }

    Status = NetQueryDisplayInformation(NULL, 1, 0, 1, MAX_PREFERRED_LENGTH, &PageCount, (PVOID *)&Page);
    ok(Status == ERROR_MORE_DATA, "First page returned %lu\n", Status);
    ok(PageCount == 1, "First page has %lu entries\n", PageCount);
    if (Page != NULL && PageCount == 1)
    {
        ok(wcscmp(Page[0].usri1_name, Users[0].usri1_name) == 0, "First page holds %ls\n", Page[0].usri1_name);
        Next = Page[0].usri1_next_index;
        NetApiBufferFree(Page);
        Page = NULL;

        Status = NetQueryDisplayInformation(NULL, 1, Next, 1, MAX_PREFERRED_LENGTH, &PageCount, (PVOID *)&Page);
        ok(Status == NERR_Success || Status == ERROR_MORE_DATA, "Second page returned %lu\n", Status);
        ok(PageCount == 1, "Second page has %lu entries\n", PageCount);
        if (Page != NULL && PageCount == 1)
            ok(wcscmp(Page[0].usri1_name, Users[1].usri1_name) == 0, "Second page holds %ls, expected %ls\n",
               Page[0].usri1_name, Users[1].usri1_name);
    }
    if (Page != NULL)
        NetApiBufferFree(Page);
    Page = NULL;

    Status = NetQueryDisplayInformation(NULL, 1, 0x10000, 100, MAX_PREFERRED_LENGTH, &PageCount, (PVOID *)&Page);
    ok(Status == NERR_Success, "Index past the end returned %lu\n", Status);
    ok(PageCount == 0, "Index past the end returned %lu entries\n", PageCount);
    if (Page != NULL)
        NetApiBufferFree(Page);

    NetApiBufferFree(Users);
}

static VOID
TestIndex(VOID)
{
    PNET_DISPLAY_USER Page = NULL;
    DWORD Index = 0xdeadbeef, Index2 = 0xdeadbeef, Count = 0;
    NET_API_STATUS Status;

    Status = NetGetDisplayInformationIndex(NULL, 1, L"Administrator", &Index);
    ok(Status == NERR_Success, "Index of Administrator returned %lu\n", Status);
    if (Status == NERR_Success)
    {
        Status = NetQueryDisplayInformation(NULL, 1, Index, 1, MAX_PREFERRED_LENGTH, &Count, (PVOID *)&Page);
        ok(Status == NERR_Success || Status == ERROR_MORE_DATA, "Query at index %lu returned %lu\n", Index, Status);
        ok(Count == 1, "Query at index %lu returned %lu entries\n", Index, Count);
        if (Page != NULL && Count == 1)
            ok(_wcsicmp(Page[0].usri1_name, L"Administrator") == 0, "Index %lu holds %ls\n", Index, Page[0].usri1_name);
        if (Page != NULL)
            NetApiBufferFree(Page);
    }

    Status = NetGetDisplayInformationIndex(NULL, 1, L"adm", &Index2);
    ok(Status == NERR_Success, "Index of prefix adm returned %lu\n", Status);
    ok(Index2 == Index, "Prefix adm gives index %lu, Administrator %lu\n", Index2, Index);

    Status = NetGetDisplayInformationIndex(NULL, 1, L"zzzzzzzzzzzz", &Index2);
    ok(Status == ERROR_NO_MORE_ITEMS, "Prefix past every name returned %lu\n", Status);

    Status = NetGetDisplayInformationIndex(NULL, 4, L"Administrator", &Index2);
    ok(Status == ERROR_INVALID_LEVEL, "Level 4 returned %lu\n", Status);
}

static VOID
TestGroupsAndMachines(VOID)
{
    PNET_DISPLAY_GROUP Groups = NULL;
    PNET_DISPLAY_MACHINE Machines = NULL;
    DWORD Count = 0, i;
    NET_API_STATUS Status;
    BOOL FoundNone = FALSE;

    Status = NetQueryDisplayInformation(NULL, 3, 0, 100, MAX_PREFERRED_LENGTH, &Count, (PVOID *)&Groups);
    ok(Status == NERR_Success, "Level 3 returned %lu\n", Status);
    for (i = 0; Groups != NULL && i < Count; i++)
    {
        if (Groups[i].grpi3_group_id == DOMAIN_GROUP_RID_USERS)
            FoundNone = TRUE;
    }
    ok(FoundNone, "No group with RID 513 among %lu groups\n", Count);
    if (Groups != NULL)
        NetApiBufferFree(Groups);

    Count = 0xdeadbeef;
    Status = NetQueryDisplayInformation(NULL, 2, 0, 100, MAX_PREFERRED_LENGTH, &Count, (PVOID *)&Machines);
    ok(Status == NERR_Success, "Level 2 returned %lu\n", Status);
    ok(Count == 0, "Level 2 returned %lu machine accounts\n", Count);
    if (Machines != NULL)
        NetApiBufferFree(Machines);

    Status = NetQueryDisplayInformation(NULL, 0, 0, 100, MAX_PREFERRED_LENGTH, &Count, (PVOID *)&Machines);
    ok(Status == ERROR_INVALID_LEVEL, "Level 0 returned %lu\n", Status);
    Status = NetQueryDisplayInformation(NULL, 4, 0, 100, MAX_PREFERRED_LENGTH, &Count, (PVOID *)&Machines);
    ok(Status == ERROR_INVALID_LEVEL, "Level 4 returned %lu\n", Status);
}

START_TEST(NetQueryDisplayInformation)
{
    TestUsers();
    TestIndex();
    TestGroupsAndMachines();
}
