/*
 * PROJECT:     ReactOS netapi32.dll API Tests
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Test list
 * COPYRIGHT:   Copyright 2017 Colin Finck (colin@reactos.org)
 */

#define STANDALONE
#include <apitest.h>

extern void func_DsRoleGetPrimaryDomainInformation(void);
extern void func_NetLocalGroupAddMembers(void);
extern void func_NetUserEnum(void);
extern void func_NetUserGetLocalGroups(void);

const struct test winetest_testlist[] =
{
    { "DsRoleGetPrimaryDomainInformation", func_DsRoleGetPrimaryDomainInformation },
    { "NetLocalGroupAddMembers", func_NetLocalGroupAddMembers },
    { "NetUserEnum", func_NetUserEnum },
    { "NetUserGetLocalGroups", func_NetUserGetLocalGroups },

    { 0, 0 }
};
