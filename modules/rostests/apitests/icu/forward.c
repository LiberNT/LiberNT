/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     icuuc.dll and icuin.dll exports forwarded to icu.dll
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <stdint.h>

typedef int32_t (__cdecl *PFN_U_STRTOUPPER)(WCHAR *, int32_t, const WCHAR *, int32_t, const char *, int *);
typedef void *(__cdecl *PFN_UCOL_OPEN)(const char *, int *);
typedef void (__cdecl *PFN_UCOL_CLOSE)(void *);
typedef int (__cdecl *PFN_UCOL_STRCOLL)(void *, const WCHAR *, int32_t, const WCHAR *, int32_t);

static const char *const UcNames[] =
{
    "u_foldCase", "u_getIntPropertyValue", "u_strToLower", "u_strToUpper",
    "ubrk_close", "ubrk_first", "ubrk_getRuleStatus", "ubrk_next", "ubrk_open", "ubrk_setUText",
    "ucasemap_close", "ucasemap_open", "ucasemap_utf8FoldCase",
    "utext_char32At", "utext_close", "utext_extract", "utext_nativeLength", "utext_openUChars", "utext_openUTF8",
    "u_strlen", "ucnv_open", "ucnv_close",
};

static const char *const InNames[] =
{
    "ucol_close", "ucol_open", "ucol_strcoll",
    "uregex_clone", "uregex_close", "uregex_end", "uregex_findNext", "uregex_matches", "uregex_open",
    "uregex_openUText", "uregex_replaceAllUText", "uregex_setText", "uregex_start",
};

static ULONG
CheckForwarders(PCSTR DllName, HMODULE Dll, HMODULE Icu, const char *const *Names, ULONG Count)
{
    ULONG Index, Resolved = 0;

    for (Index = 0; Index < Count; Index++)
    {
        FARPROC Target = GetProcAddress(Icu, Names[Index]);
        FARPROC Forward = GetProcAddress(Dll, Names[Index]);

        if (!Target)
        {
            ok(Forward == NULL, "%s!%s resolves to %p without an icu.dll export\n", DllName, Names[Index], Forward);
            continue;
        }
        ok(Forward == Target, "%s!%s is %p, icu.dll!%s is %p\n", DllName, Names[Index], Forward, Names[Index], Target);
        Resolved++;
    }
    return Resolved;
}

START_TEST(forward)
{
    HMODULE Icu, IcuUc, IcuIn;
    ULONG Resolved;

    Icu = LoadLibraryA("icu.dll");
    ok(Icu != NULL, "LoadLibrary(icu.dll) failed: %lu\n", GetLastError());
    IcuUc = LoadLibraryA("icuuc.dll");
    ok(IcuUc != NULL, "LoadLibrary(icuuc.dll) failed: %lu\n", GetLastError());
    IcuIn = LoadLibraryA("icuin.dll");
    ok(IcuIn != NULL, "LoadLibrary(icuin.dll) failed: %lu\n", GetLastError());
    if (!Icu || !IcuUc || !IcuIn)
        return;

    Resolved = CheckForwarders("icuuc.dll", IcuUc, Icu, UcNames, ARRAYSIZE(UcNames));
    trace("icuuc.dll: %lu of %u names resolved\n", Resolved, (unsigned)ARRAYSIZE(UcNames));
    Resolved = CheckForwarders("icuin.dll", IcuIn, Icu, InNames, ARRAYSIZE(InNames));
    trace("icuin.dll: %lu of %u names resolved\n", Resolved, (unsigned)ARRAYSIZE(InNames));

    if (!GetProcAddress(Icu, "ubrk_open"))
    {
        skip("icu.dll is the stub without the full ICU\n");
        return;
    }

    {
        static const WCHAR Source[] = L"abc";
        WCHAR Upper[8] = { 0 };
        int Status = 0;
        PFN_U_STRTOUPPER pu_strToUpper = (PFN_U_STRTOUPPER)GetProcAddress(IcuUc, "u_strToUpper");
        int32_t Length = pu_strToUpper(Upper, ARRAYSIZE(Upper), Source, -1, "", &Status);

        ok(Status <= 0, "u_strToUpper status %d\n", Status);
        ok(Length == 3 && !wcscmp(Upper, L"ABC"), "u_strToUpper returned %d, %ls\n", Length, Upper);
    }

    {
        int Status = 0;
        PFN_UCOL_OPEN pucol_open = (PFN_UCOL_OPEN)GetProcAddress(IcuIn, "ucol_open");
        PFN_UCOL_CLOSE pucol_close = (PFN_UCOL_CLOSE)GetProcAddress(IcuIn, "ucol_close");
        PFN_UCOL_STRCOLL pucol_strcoll = (PFN_UCOL_STRCOLL)GetProcAddress(IcuIn, "ucol_strcoll");
        void *Collator = pucol_open("en", &Status);

        ok(Collator != NULL && Status <= 0, "ucol_open returned %p, status %d\n", Collator, Status);
        if (Collator)
        {
            ok(pucol_strcoll(Collator, L"a", -1, L"b", -1) < 0, "ucol_strcoll(a, b) is not less\n");
            ok(pucol_strcoll(Collator, L"b", -1, L"b", -1) == 0, "ucol_strcoll(b, b) is not equal\n");
            pucol_close(Collator);
        }
    }
}
