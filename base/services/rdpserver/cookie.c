/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Read the user name a client puts in its X.224 Connection Request (MS-RDPBCGR 2.2.1.1)
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "broker.h"
#include <string.h>

#define COOKIE_PREFIX "Cookie: mstshash="
#define ROUTING_PREFIX "Cookie: msts=" BROKER_ROUTING_USER

static BOOL HasPrefix(const BYTE *data, int start, int end, const char *prefix)
{
    const int length = (int)strlen(prefix);
    return end - start >= length && !memcmp(data + start, prefix, length);
}

BOOL BrokerCookieUser(const BYTE *data, int length, WCHAR *user, DWORD count, BOOL *routed)
{
    UINT codepage;
    int tpkt, end, start, i, chars;
    *user = 0;
    *routed = FALSE;
    if (length < 11 || data[0] != 3) return FALSE;
    tpkt = (data[2] << 8) | data[3];
    if (tpkt < 11 || tpkt > length || (data[5] & 0xF0) != 0xE0) return FALSE;
    end = 5 + data[4];
    if (end > tpkt) return FALSE;
    start = 11;
    if (HasPrefix(data, start, end, COOKIE_PREFIX))
    {
        start += (int)strlen(COOKIE_PREFIX);
        codepage = CP_ACP;
    }
    else if (HasPrefix(data, start, end, ROUTING_PREFIX))
    {
        start += (int)strlen(ROUTING_PREFIX);
        codepage = CP_UTF8;
        *routed = TRUE;
    }
    else
        return TRUE;
    for (i = start; i + 1 < end && !(data[i] == '\r' && data[i + 1] == '\n'); ++i)
        if (data[i] == '\\') start = i + 1;
    if (i + 1 >= end || i - start <= 0 || (DWORD)(i - start) >= count) return FALSE;
    chars = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, (const char *)data + start, i - start, user,
                                count - 1);
    if (!chars) return FALSE;
    user[chars] = 0;
    return TRUE;
}

/* A client's own cookie holds no more than nine characters of its user name (MS-RDPBCGR 3.2.5.3.1) */
BOOL BrokerSameUser(const WCHAR *name, const WCHAR *cookie, BOOL routed)
{
    size_t length = wcslen(cookie);
    if (routed || length < BROKER_COOKIE_CHARS) return !lstrcmpiW(name, cookie);
    return !_wcsnicmp(name, cookie, length);
}
