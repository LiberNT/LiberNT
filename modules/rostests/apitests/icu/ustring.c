/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ICU string functions exported by icu.dll and icuuc.dll
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <stdint.h>

typedef int32_t (__cdecl *PFN_U_STRLEN)(const WCHAR *);

static void TestStrlen(PCSTR Dll, PFN_U_STRLEN pu_strlen)
{
    static const WCHAR Empty[] = L"";
    static const WCHAR Ascii[] = L"abc";
    static const WCHAR Pair[] = { 0xd83d, 0xde00, L'x', 0 };
    static const WCHAR Lone[] = { 0xdc00, 0xd800, 0 };
    static const WCHAR Embedded[] = { L'a', L'b', 0, L'c', L'd', 0 };

    ok(pu_strlen(Empty) == 0, "%s: u_strlen(\"\") = %d\n", Dll, pu_strlen(Empty));
    ok(pu_strlen(Ascii) == 3, "%s: u_strlen(\"abc\") = %d\n", Dll, pu_strlen(Ascii));
    ok(pu_strlen(Pair) == 3, "%s: u_strlen(pair) = %d\n", Dll, pu_strlen(Pair));
    ok(pu_strlen(Lone) == 2, "%s: u_strlen(lone) = %d\n", Dll, pu_strlen(Lone));
    ok(pu_strlen(Embedded) == 2, "%s: u_strlen(embedded) = %d\n", Dll, pu_strlen(Embedded));
}

START_TEST(ustring)
{
    HMODULE Icu, IcuUc;
    PFN_U_STRLEN IcuStrlen, IcuUcStrlen;

    Icu = LoadLibraryA("icu.dll");
    ok(Icu != NULL, "LoadLibrary(icu.dll) failed: %lu\n", GetLastError());
    IcuUc = LoadLibraryA("icuuc.dll");
    ok(IcuUc != NULL, "LoadLibrary(icuuc.dll) failed: %lu\n", GetLastError());
    if (!Icu || !IcuUc)
        return;

    IcuStrlen = (PFN_U_STRLEN)GetProcAddress(Icu, "u_strlen");
    ok(IcuStrlen != NULL, "icu.dll: no u_strlen\n");
    IcuUcStrlen = (PFN_U_STRLEN)GetProcAddress(IcuUc, "u_strlen");
    ok(IcuUcStrlen != NULL, "icuuc.dll: no u_strlen\n");
    ok(IcuUcStrlen == IcuStrlen, "icuuc.dll u_strlen %p, icu.dll u_strlen %p\n", IcuUcStrlen, IcuStrlen);

    if (IcuStrlen)
        TestStrlen("icu.dll", IcuStrlen);
    if (IcuUcStrlen)
        TestStrlen("icuuc.dll", IcuUcStrlen);

    FreeLibrary(IcuUc);
    FreeLibrary(Icu);
}
