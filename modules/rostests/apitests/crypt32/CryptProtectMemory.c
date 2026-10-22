/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests CryptProtectMemory and CryptUnprotectMemory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>

static void
TestRoundTrip(
    _In_ DWORD Flags)
{
    BYTE Plain[2 * CRYPTPROTECTMEMORY_BLOCK_SIZE], Data[sizeof(Plain)];
    DWORD Index;

    for (Index = 0; Index < sizeof(Plain); Index++)
        Plain[Index] = (BYTE)(Index * 7 + 3);
    memcpy(Data, Plain, sizeof(Data));

    ok(CryptProtectMemory(Data, sizeof(Data), Flags), "CryptProtectMemory(%lu) failed, error %lu\n", Flags, GetLastError());
    ok(memcmp(Data, Plain, sizeof(Data)) != 0, "CryptProtectMemory(%lu) left the data in clear text\n", Flags);
    ok(CryptUnprotectMemory(Data, sizeof(Data), Flags), "CryptUnprotectMemory(%lu) failed, error %lu\n", Flags, GetLastError());
    ok(memcmp(Data, Plain, sizeof(Data)) == 0, "CryptUnprotectMemory(%lu) did not restore the data\n", Flags);
}

START_TEST(CryptProtectMemory)
{
    BYTE Odd[CRYPTPROTECTMEMORY_BLOCK_SIZE - 1] = { 0 };

    TestRoundTrip(CRYPTPROTECTMEMORY_SAME_PROCESS);
    TestRoundTrip(CRYPTPROTECTMEMORY_CROSS_PROCESS);
    TestRoundTrip(CRYPTPROTECTMEMORY_SAME_LOGON);

    ok(!CryptProtectMemory(Odd, sizeof(Odd), CRYPTPROTECTMEMORY_SAME_PROCESS),
       "CryptProtectMemory accepted a size that is not a block multiple\n");
}
