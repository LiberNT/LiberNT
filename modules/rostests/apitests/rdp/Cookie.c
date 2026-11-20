/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which user name the RDP service reads from a client's X.224 Connection Request
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <string.h>
#include "broker.h"

static const BYTE NegotiationRequest[] = { 0x01, 0x00, 0x08, 0x00, 0x0b, 0x00, 0x00, 0x00 };

static int Request(BYTE *data, const char *cookie)
{
    int length = 11;
    if (cookie)
    {
        memcpy(data + length, cookie, strlen(cookie));
        length += (int)strlen(cookie);
    }
    memcpy(data + length, NegotiationRequest, sizeof(NegotiationRequest));
    length += sizeof(NegotiationRequest);
    data[0] = 3;
    data[1] = 0;
    data[2] = (BYTE)(length >> 8);
    data[3] = (BYTE)length;
    data[4] = (BYTE)(length - 5);
    data[5] = 0xE0;
    memset(data + 6, 0, 5);
    return length;
}

static void TestCookie(const char *cookie, BOOL valid, const WCHAR *expected, DWORD count)
{
    BYTE data[256];
    WCHAR user[64] = L"x";
    int length = Request(data, cookie);
    BOOL result = BrokerCookieUser(data, length, user, count);
    ok(result == valid, "%s: returned %d\n", cookie ? cookie : "no cookie", result);
    if (valid) ok(!wcscmp(user, expected), "%s: read %ls\n", cookie ? cookie : "no cookie", user);
}

START_TEST(Cookie)
{
    BYTE data[256];
    WCHAR user[64];
    int length;

    TestCookie("Cookie: mstshash=Administrator\r\n", TRUE, L"Administrator", 64);
    TestCookie("Cookie: mstshash=LIBERNT\\rdpnla\r\n", TRUE, L"rdpnla", 64);
    TestCookie(NULL, TRUE, L"", 64);
    TestCookie("Cookie: msts=3640205228.15629.0000\r\n", TRUE, L"", 64);
    TestCookie("Cookie: mstshash=\r\n", FALSE, L"", 64);
    TestCookie("Cookie: mstshash=Administrator", FALSE, L"", 64);
    TestCookie("Cookie: mstshash=Administrator\r\n", FALSE, L"", 8);

    length = Request(data, "Cookie: mstshash=Administrator\r\n");
    ok(!BrokerCookieUser(data, length - 1, user, 64), "a truncated request was read\n");
    data[0] = 0x16;
    ok(!BrokerCookieUser(data, length, user, 64), "a TLS record was read as a connection request\n");
    data[0] = 3;
    data[5] = 0xD0;
    ok(!BrokerCookieUser(data, length, user, 64), "a connection confirm was read as a request\n");
    data[5] = 0xE0;
    data[4] = (BYTE)(length - 4);
    ok(!BrokerCookieUser(data, length, user, 64), "a header longer than its packet was read\n");
}
