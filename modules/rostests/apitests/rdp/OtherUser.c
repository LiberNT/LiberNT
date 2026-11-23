/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that one user's Remote Desktop connection cannot take over another user's session
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <lm.h>
#include "rdpclient.h"

#define FIRST_USER  "rdpowner"
#define SECOND_USER "rdpother"

START_TEST(OtherUser)
{
    RDP_TEST_SETUP Setup;
    RDP_CLIENT Owner, Other;
    DWORD Wait;

    if (!RdpTestStart(&Setup))
        return;
    if (RdpTestAddUser(L"" FIRST_USER) && RdpTestAddUser(L"" SECOND_USER))
    {
        RdpClientStart(&Owner, FIRST_USER, NULL);
        ok(RdpClientWaitConnected(&Owner), "%s could not connect: 0x%08x\n", FIRST_USER, Owner.Error);
        if (Owner.Alive)
        {
            Sleep(3000);
            RdpClientStart(&Other, SECOND_USER, FIRST_USER);
            RdpClientWaitConnected(&Other);
            for (Wait = 0; Wait < 50 && !Owner.Disconnected; Wait++)
                Sleep(100);
            ok(!Owner.Disconnected, "%s was disconnected by %s: 0x%08x\n", FIRST_USER, SECOND_USER, Owner.Error);
            ok(Owner.Alive, "%s is no longer connected\n", FIRST_USER);
            RdpClientStop(&Other);
        }
        RdpClientStop(&Owner);
    }
    NetUserDel(NULL, L"" FIRST_USER);
    NetUserDel(NULL, L"" SECOND_USER);
    RdpTestEnd(&Setup);
}
