/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the KernelBase URL functions UrlEscapeA, UrlFixupW, UrlIsA, UrlIsW and IsInternetESCEnabled
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <shlwapi.h>

#define ZONEMAP_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\ZoneMap"

typedef HRESULT (WINAPI *PFN_URLESCAPEA)(PCSTR, PSTR, DWORD *, DWORD);
typedef HRESULT (WINAPI *PFN_URLFIXUPW)(PCWSTR, PWSTR, DWORD);
typedef BOOL (WINAPI *PFN_URLISA)(PCSTR, URLIS);
typedef BOOL (WINAPI *PFN_URLISW)(PCWSTR, URLIS);
typedef BOOL (WINAPI *PFN_ISINTERNETESCENABLED)(VOID);

static PFN_URLESCAPEA pUrlEscapeA;
static PFN_URLFIXUPW pUrlFixupW;
static PFN_URLISA pUrlIsA;
static PFN_URLISW pUrlIsW;
static PFN_ISINTERNETESCENABLED pIsInternetESCEnabled;

static
VOID
TestUrlEscapeA(VOID)
{
    CHAR Buffer[4] = { 127, 0 };
    DWORD Size;
    HRESULT hr;

    if (!pUrlEscapeA)
    {
        skip("UrlEscapeA is not available\n");
        return;
    }

    Size = 1;
    hr = pUrlEscapeA("/woningplan/woonkamer basis.swf", Buffer, &Size, URL_ESCAPE_AS_UTF8);
    ok_hex(hr, E_NOTIMPL);
    ok_long(Size, 1);
    ok_int(Buffer[0], 127);

    Size = sizeof(Buffer);
    hr = pUrlEscapeA("a b", Buffer, &Size, URL_ESCAPE_SPACES_ONLY);
    ok_hex(hr, E_POINTER);
    ok_long(Size, 6);
}

static
VOID
TestUrlIs(VOID)
{
    static const struct
    {
        PCSTR Url;
        BOOL HasQuery;
    } Tests[] =
    {
        { "http://www.winehq.org/index.html?query=1", TRUE },
        { "http://www.winehq.org/?", TRUE },
        { "http://www.winehq.org/index.html", FALSE },
        { "http://www.winehq.org/index.html#anchor", FALSE },
        { "file:///c:/windows/notepad.exe", FALSE },
        { "", FALSE },
    };
    WCHAR UrlW[128];
    ULONG Index;

    if (!pUrlIsA || !pUrlIsW)
    {
        skip("UrlIsA or UrlIsW is not available\n");
        return;
    }

    ok_int(pUrlIsA(NULL, URLIS_HASQUERY), FALSE);
    ok_int(pUrlIsW(NULL, URLIS_HASQUERY), FALSE);

    for (Index = 0; Index < _countof(Tests); Index++)
    {
        MultiByteToWideChar(CP_ACP, 0, Tests[Index].Url, -1, UrlW, _countof(UrlW));
        ok(pUrlIsA(Tests[Index].Url, URLIS_HASQUERY) == Tests[Index].HasQuery,
           "UrlIsA(%s, URLIS_HASQUERY) is not %d\n", Tests[Index].Url, Tests[Index].HasQuery);
        ok(pUrlIsW(UrlW, URLIS_HASQUERY) == Tests[Index].HasQuery,
           "UrlIsW(%s, URLIS_HASQUERY) is not %d\n", Tests[Index].Url, Tests[Index].HasQuery);
    }
}

static
VOID
TestUrlFixup(VOID)
{
    static const struct
    {
        PCWSTR Url;
        HRESULT Result;
        PCWSTR Fixed;
    } Tests[] =
    {
        { L"http://www.microsoft.com", S_OK, L"http://www.microsoft.com" },
        { L"http:www.microsoft.com", S_OK, L"http://www.microsoft.com" },
        { L"http:someone@example.com", S_OK, L"http://someone@example.com" },
        { L"abc:def", S_FALSE, NULL },
        { L"someone@example.com", S_FALSE, NULL },
    };
    WCHAR Buffer[64];
    ULONG Index;
    HRESULT hr;

    if (!pUrlFixupW)
    {
        skip("UrlFixupW is not available\n");
        return;
    }

    for (Index = 0; Index < _countof(Tests); Index++)
    {
        wcscpy(Buffer, L"unchanged");
        hr = pUrlFixupW(Tests[Index].Url, Buffer, _countof(Buffer));
        ok(hr == Tests[Index].Result, "UrlFixupW(%ls) returned 0x%08lx\n", Tests[Index].Url, hr);
        if (hr == S_OK && Tests[Index].Fixed)
            ok(!wcscmp(Buffer, Tests[Index].Fixed), "UrlFixupW(%ls) gave %ls\n", Tests[Index].Url, Buffer);
    }

    wcscpy(Buffer, L"http:www.microsoft.com");
    hr = pUrlFixupW(Buffer, Buffer, _countof(Buffer));
    ok_hex(hr, S_OK);
    ok_wstr(Buffer, L"http://www.microsoft.com");

    hr = pUrlFixupW(L"http:www.microsoft.com", Buffer, 8);
    ok(FAILED(hr), "UrlFixupW into a short buffer returned 0x%08lx\n", hr);
}

static
VOID
TestIsInternetESCEnabled(VOID)
{
    DWORD Saved, Size, Type, One = 1, Zero = 0;
    BOOL HadValue;
    HKEY Key;
    LONG Error;

    if (!pIsInternetESCEnabled)
    {
        skip("IsInternetESCEnabled is not available\n");
        return;
    }

    Error = RegCreateKeyExW(HKEY_CURRENT_USER, ZONEMAP_KEY, 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &Key, NULL);
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
        return;

    Size = sizeof(Saved);
    HadValue = RegQueryValueExW(Key, L"IEHarden", NULL, &Type, (BYTE *)&Saved, &Size) == ERROR_SUCCESS &&
               Type == REG_DWORD && Size == sizeof(Saved);
    trace("IEHarden %s (%lu), IsInternetESCEnabled %d\n", HadValue ? "set" : "absent", HadValue ? Saved : 0,
          pIsInternetESCEnabled());

    RegDeleteValueW(Key, L"IEHarden");
    SetLastError(0x33333333);
    ok_int(pIsInternetESCEnabled(), FALSE);
    ok_long(GetLastError(), 0x33333333);

    ok_long(RegSetValueExW(Key, L"IEHarden", 0, REG_DWORD, (const BYTE *)&One, sizeof(One)), ERROR_SUCCESS);
    ok_int(pIsInternetESCEnabled(), TRUE);

    ok_long(RegSetValueExW(Key, L"IEHarden", 0, REG_DWORD, (const BYTE *)&Zero, sizeof(Zero)), ERROR_SUCCESS);
    ok_int(pIsInternetESCEnabled(), FALSE);

    if (HadValue)
        RegSetValueExW(Key, L"IEHarden", 0, REG_DWORD, (const BYTE *)&Saved, sizeof(Saved));
    else
        RegDeleteValueW(Key, L"IEHarden");
    RegCloseKey(Key);
}

START_TEST(KernelBaseUrl)
{
    HMODULE KernelBase;

    KernelBase = GetModuleHandleW(L"kernelbase.dll");
    ok(KernelBase != NULL, "kernelbase.dll is not loaded\n");
    if (!KernelBase)
        return;

    pUrlEscapeA = (PFN_URLESCAPEA)GetProcAddress(KernelBase, "UrlEscapeA");
    pUrlFixupW = (PFN_URLFIXUPW)GetProcAddress(KernelBase, "UrlFixupW");
    pUrlIsA = (PFN_URLISA)GetProcAddress(KernelBase, "UrlIsA");
    pUrlIsW = (PFN_URLISW)GetProcAddress(KernelBase, "UrlIsW");
    pIsInternetESCEnabled = (PFN_ISINTERNETESCENABLED)GetProcAddress(KernelBase, "IsInternetESCEnabled");

    TestUrlEscapeA();
    TestUrlIs();
    TestUrlFixup();
    TestIsInternetESCEnabled();
}
