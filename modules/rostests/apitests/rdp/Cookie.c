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

static void TestCookie(const char *cookie, BOOL valid, const WCHAR *expected, DWORD count, BOOL routed)
{
    BYTE data[256];
    WCHAR user[64] = L"x";
    BOOL token = 2;
    int length = Request(data, cookie);
    BOOL result = BrokerCookieUser(data, length, user, count, &token);
    ok(result == valid, "%s: returned %d\n", cookie ? cookie : "no cookie", result);
    if (valid) ok(!wcscmp(user, expected), "%s: read %ls\n", cookie ? cookie : "no cookie", user);
    if (valid) ok(token == routed, "%s: routed %d\n", cookie ? cookie : "no cookie", token);
}

START_TEST(Cookie)
{
    BYTE data[256];
    WCHAR user[64];
    BOOL routed;
    int length;

    TestCookie("Cookie: mstshash=Administrator\r\n", TRUE, L"Administrator", 64, FALSE);
    TestCookie("Cookie: mstshash=LIBERNT\\rdpnla\r\n", TRUE, L"rdpnla", 64, FALSE);
    TestCookie("Cookie: mstshash=Administr\r\n", TRUE, L"Administr", 64, FALSE);
    TestCookie(NULL, TRUE, L"", 64, FALSE);
    TestCookie("Cookie: msts=3640205228.15629.0000\r\n", TRUE, L"", 64, FALSE);
    TestCookie("Cookie: msts=user=Administrator\r\n", TRUE, L"Administrator", 64, TRUE);
    TestCookie("Cookie: msts=user=\xe3\x83\xa6\xe3\x83\xbc\xe3\x82\xb6\r\n", TRUE, L"\x30e6\x30fc\x30b6", 64, TRUE);
    TestCookie("Cookie: msts=user=\r\n", FALSE, L"", 64, TRUE);
    TestCookie("Cookie: msts=user=\xe3\x83\r\n", FALSE, L"", 64, TRUE);
    TestCookie("Cookie: mstshash=\r\n", FALSE, L"", 64, FALSE);
    TestCookie("Cookie: mstshash=Administrator", FALSE, L"", 64, FALSE);
    TestCookie("Cookie: mstshash=Administrator\r\n", FALSE, L"", 8, FALSE);

    length = Request(data, "Cookie: mstshash=Administrator\r\n");
    ok(!BrokerCookieUser(data, length - 1, user, 64, &routed), "a truncated request was read\n");
    data[0] = 0x16;
    ok(!BrokerCookieUser(data, length, user, 64, &routed), "a TLS record was read as a connection request\n");
    data[0] = 3;
    data[5] = 0xD0;
    ok(!BrokerCookieUser(data, length, user, 64, &routed), "a connection confirm was read as a request\n");
    data[5] = 0xE0;
    data[4] = (BYTE)(length - 4);
    ok(!BrokerCookieUser(data, length, user, 64, &routed), "a header longer than its packet was read\n");

    ok(BrokerSameUser(L"Administrator", L"Administr", FALSE), "a nine-character cookie did not match its user\n");
    ok(BrokerSameUser(L"administrator", L"ADMINISTR", FALSE), "a cookie matched only with the same case\n");
    ok(!BrokerSameUser(L"Administrator", L"Administr", TRUE), "a routing token matched only the start of a name\n");
    ok(BrokerSameUser(L"rdpnla", L"RDPNLA", FALSE), "a short cookie did not match its user\n");
    ok(!BrokerSameUser(L"rdpnla2", L"rdpnla", FALSE), "a short cookie matched only the start of a name\n");
    ok(!BrokerSameUser(L"Admin", L"Administr", FALSE), "a nine-character cookie matched a shorter name\n");
}
