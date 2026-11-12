/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests querying TokenLinkedToken on a process token that was not split by UAC
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(TokenLinkedToken)
{
    TOKEN_ELEVATION_TYPE ElevationType;
    TOKEN_ELEVATION Elevation;
    TOKEN_LINKED_TOKEN Linked;
    HANDLE Token;
    DWORD Size, Error;
    BOOL Ret;

    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token), "OpenProcessToken failed: %lu\n", GetLastError());

    Ret = GetTokenInformation(Token, TokenElevationType, &ElevationType, sizeof(ElevationType), &Size);
    ok(Ret, "TokenElevationType failed: %lu\n", GetLastError());
    Ret = GetTokenInformation(Token, TokenElevation, &Elevation, sizeof(Elevation), &Size);
    ok(Ret, "TokenElevation failed: %lu\n", GetLastError());

    if (ElevationType != TokenElevationTypeDefault)
    {
        skip("The token was split by UAC: elevation type %d\n", ElevationType);
        CloseHandle(Token);
        return;
    }

    Linked.LinkedToken = (HANDLE)0xdeadbeef;
    SetLastError(0xdeadbeef);
    Size = 0xdeadbeef;
    Ret = GetTokenInformation(Token, TokenLinkedToken, &Linked, sizeof(Linked), &Size);
    Error = GetLastError();
    ok(!Ret, "An unsplit token has a linked token %p\n", Linked.LinkedToken);
    ok(Error == ERROR_NO_SUCH_LOGON_SESSION, "The query failed with %lu\n", Error);
    ok(Size == sizeof(Linked), "The query returned length %lu\n", Size);
    ok(Linked.LinkedToken == (HANDLE)0xdeadbeef, "The query wrote %p\n", Linked.LinkedToken);
    if (Ret)
        CloseHandle(Linked.LinkedToken);

    CloseHandle(Token);
}
