/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the WinRT stream API set resolves to the shcore exports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

START_TEST(StreamWinRtContract)
{
    static const char *Names[] =
    {
        "CreateRandomAccessStreamOnFile",
        "CreateRandomAccessStreamOverStream",
        "CreateStreamOverRandomAccessStream"
    };
    HMODULE Contract, Host;
    unsigned int i;

    Host = LoadLibraryW(L"shcore.dll");
    ok(Host != NULL, "LoadLibrary(shcore.dll) failed: %lu\n", GetLastError());
    Contract = LoadLibraryW(L"api-ms-win-shcore-stream-winrt-l1-1-0.dll");
    ok(Contract != NULL, "LoadLibrary(api-ms-win-shcore-stream-winrt-l1-1-0.dll) failed: %lu\n", GetLastError());
    if (Host == NULL || Contract == NULL)
    {
        if (Contract != NULL)
            FreeLibrary(Contract);
        if (Host != NULL)
            FreeLibrary(Host);
        return;
    }

    ok(Contract == Host, "The API set loaded %p, shcore.dll is %p\n", Contract, Host);
    for (i = 0; i < ARRAYSIZE(Names); ++i)
    {
        FARPROC FromHost = GetProcAddress(Host, Names[i]);
        FARPROC FromContract = GetProcAddress(Contract, Names[i]);

        ok(FromHost != NULL, "shcore.dll does not export %s\n", Names[i]);
        ok(FromContract != NULL, "The API set does not export %s\n", Names[i]);
        ok(FromContract == FromHost, "%s resolves to %p through the API set and %p in shcore.dll\n", Names[i], FromContract, FromHost);
    }

    FreeLibrary(Contract);
    FreeLibrary(Host);
}
