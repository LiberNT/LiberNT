/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list for the DHCP client API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_ClientIdentifier(void);
extern void func_DhcpAcquireParameters(void);

const struct test winetest_testlist[] =
{
    { "ClientIdentifier", func_ClientIdentifier },
    { "DhcpAcquireParameters", func_DhcpAcquireParameters },
    { 0, 0 }
};
