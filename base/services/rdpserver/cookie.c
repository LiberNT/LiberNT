/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Read the user name a client puts in its X.224 Connection Request (MS-RDPBCGR 2.2.1.1)
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "broker.h"
#include <string.h>

#define COOKIE_PREFIX "Cookie: mstshash="

BOOL BrokerCookieUser(const BYTE *data, int length, WCHAR *user, DWORD count)
{
    const int prefix = (int)strlen(COOKIE_PREFIX);
    int tpkt, end, start, i, chars;
    *user = 0;
    if (length < 11 || data[0] != 3) return FALSE;
    tpkt = (data[2] << 8) | data[3];
    if (tpkt < 11 || tpkt > length || (data[5] & 0xF0) != 0xE0) return FALSE;
    end = 5 + data[4];
    if (end > tpkt) return FALSE;
    start = 11;
    if (end - start < prefix || memcmp(data + start, COOKIE_PREFIX, prefix)) return TRUE;
    start += prefix;
    for (i = start; i + 1 < end && !(data[i] == '\r' && data[i + 1] == '\n'); ++i)
        if (data[i] == '\\') start = i + 1;
    if (i + 1 >= end || i - start <= 0 || (DWORD)(i - start) >= count) return FALSE;
    chars = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, (const char *)data + start, i - start, user,
                                count - 1);
    if (!chars) return FALSE;
    user[chars] = 0;
    return TRUE;
}
