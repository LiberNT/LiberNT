/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the ntdll scanf exports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef int (__cdecl *FN_swscanf_s)(const wchar_t *, const wchar_t *, ...);
typedef int (__cdecl *FN_sscanf_s)(const char *, const char *, ...);
typedef int (__cdecl *FN__snwscanf_s)(const wchar_t *, size_t, const wchar_t *, ...);
typedef int (__cdecl *FN_sscanf)(const char *, const char *, ...);

static void
Test_swscanf_s(FN_swscanf_s pswscanf_s)
{
    wchar_t Buffer[16], Char[2];
    int Number, Hex, Result;

    Number = Hex = -1;
    Result = pswscanf_s(L"42 abc 1f", L"%d %s %x", &Number, Buffer, (unsigned)ARRAYSIZE(Buffer), &Hex);
    ok(Result == 3, "basic: returned %d\n", Result);
    ok(Number == 42 && !wcscmp(Buffer, L"abc") && Hex == 0x1f, "basic: %d %ls %x\n", Number, Buffer, Hex);

    Char[0] = L'Z';
    Result = pswscanf_s(L"xy", L"%c", Char, 1u);
    ok(Result == 1 && Char[0] == L'x', "%%c: returned %d, %lc\n", Result, Char[0]);

    Number = -1;
    Result = pswscanf_s(L"", L"%d", &Number);
    ok(Result == 0, "empty input: returned %d\n", Result);
    ok(Number == -1, "empty input: %d\n", Number);

    Number = -1;
    Result = pswscanf_s(L"   ", L"%d", &Number);
    ok(Result == 0, "blank input: returned %d\n", Result);

    Number = -1;
    Result = pswscanf_s(L"abc", L"%d", &Number);
    ok(Result == 0 && Number == -1, "no match: returned %d, %d\n", Result, Number);

    Result = pswscanf_s(L"12345678", L"%[0-9]", Buffer, (unsigned)ARRAYSIZE(Buffer));
    ok(Result == 1 && !wcscmp(Buffer, L"12345678"), "set: returned %d, %ls\n", Result, Buffer);

    Buffer[0] = L'Z';
    Result = pswscanf_s(L"abcdef", L"%s", Buffer, 3u);
    ok(Result == 0, "small buffer: returned %d\n", Result);
    ok(Buffer[0] == UNICODE_NULL, "small buffer: first character %u\n", Buffer[0]);
}

static void
Test_sscanf_s(FN_sscanf_s psscanf_s)
{
    char Buffer[16];
    int Number, Hex, Result;

    Number = Hex = -1;
    Result = psscanf_s("42 abc 1f", "%d %s %x", &Number, Buffer, (unsigned)sizeof(Buffer), &Hex);
    ok(Result == 3, "basic: returned %d\n", Result);
    ok(Number == 42 && !strcmp(Buffer, "abc") && Hex == 0x1f, "basic: %d %s %x\n", Number, Buffer, Hex);

    Number = -1;
    Result = psscanf_s("", "%d", &Number);
    ok(Result == EOF, "empty input: returned %d\n", Result);

    Buffer[0] = 'Z';
    Result = psscanf_s("abcdef", "%s", Buffer, 3u);
    ok(Result == 0, "small buffer: returned %d\n", Result);
    ok(Buffer[0] == '\0', "small buffer: first character %u\n", (unsigned char)Buffer[0]);
}

static void
Test__snwscanf_s(FN__snwscanf_s p_snwscanf_s)
{
    int Number, Result;

    Number = -1;
    Result = p_snwscanf_s(L"12345", 3, L"%d", &Number);
    ok(Result == 1 && Number == 123, "length limit: returned %d, %d\n", Result, Number);

    Number = -1;
    Result = p_snwscanf_s(L"42", 0, L"%d", &Number);
    ok(Result == 0, "zero length: returned %d\n", Result);
    ok(Number == -1, "zero length: %d\n", Number);
}

START_TEST(ScanfSecure)
{
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    FN_swscanf_s pswscanf_s = (FN_swscanf_s)GetProcAddress(hNtdll, "swscanf_s");
    FN_sscanf_s psscanf_s = (FN_sscanf_s)GetProcAddress(hNtdll, "sscanf_s");
    FN__snwscanf_s p_snwscanf_s = (FN__snwscanf_s)GetProcAddress(hNtdll, "_snwscanf_s");
    FN_sscanf psscanf = (FN_sscanf)GetProcAddress(hNtdll, "sscanf");
    int Number, Result;

    ok(pswscanf_s != NULL, "ntdll!swscanf_s is missing\n");
    ok(psscanf_s != NULL, "ntdll!sscanf_s is missing\n");
    ok(p_snwscanf_s != NULL, "ntdll!_snwscanf_s is missing\n");
    ok(psscanf != NULL, "ntdll!sscanf is missing\n");
    ok(GetProcAddress(hNtdll, "swscanf") == NULL, "ntdll must not export swscanf\n");

    if (pswscanf_s)
        Test_swscanf_s(pswscanf_s);
    if (psscanf_s)
        Test_sscanf_s(psscanf_s);
    if (p_snwscanf_s)
        Test__snwscanf_s(p_snwscanf_s);
    if (psscanf)
    {
        Number = -1;
        Result = psscanf("", "%d", &Number);
        ok(Result == EOF, "sscanf empty input: returned %d\n", Result);
    }
}
