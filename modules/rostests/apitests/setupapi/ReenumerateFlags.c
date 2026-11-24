/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CM_Reenumerate_DevNode flag validation and asynchronous requests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <cfgmgr32.h>

START_TEST(ReenumerateFlags)
{
    static const ULONG Flags[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0x10, 0x80000000 };
    DEVINST Root;
    CONFIGRET Result;
    ULONG Index;

    Result = CM_Locate_DevNodeW(&Root, NULL, CM_LOCATE_DEVNODE_NORMAL);
    ok(Result == CR_SUCCESS, "CM_Locate_DevNodeW returned 0x%lx\n", Result);
    if (Result != CR_SUCCESS)
        return;

    for (Index = 0; Index < ARRAYSIZE(Flags); Index++)
    {
        Result = CM_Reenumerate_DevNode(Root, Flags[Index]);
        trace("flags 0x%lx -> 0x%lx\n", Flags[Index], Result);
        ok(Result == ((Flags[Index] & ~CM_REENUMERATE_BITS) ? CR_INVALID_FLAG : CR_SUCCESS),
           "flags 0x%lx returned 0x%lx\n", Flags[Index], Result);
    }

    Result = CM_Reenumerate_DevNode(0, CM_REENUMERATE_ASYNCHRONOUS);
    ok(Result == CR_INVALID_DEVNODE, "devnode 0 returned 0x%lx\n", Result);
}
