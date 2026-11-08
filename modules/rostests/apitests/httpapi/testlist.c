/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list for HTTP Server API test suite
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define __ROS_LONG64__
#define STANDALONE
#include <apitest.h>

extern void func_chunked(void);
extern void func_connlimit(void);
extern void func_filechunk(void);
extern void func_httpapi(void);
extern void func_limits(void);
extern void func_properties(void);

const struct test winetest_testlist[] =
{
    { "chunked", func_chunked },
    { "connlimit", func_connlimit },
    { "filechunk", func_filechunk },
    { "httpapi", func_httpapi },
    { "limits", func_limits },
    { "properties", func_properties },
    { 0, 0 }
};
