/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests GetUILanguageInfo
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_GETUILANGUAGEINFO)(DWORD, PCZZWSTR, PZZWSTR, PDWORD, PDWORD);

static PFN_GETUILANGUAGEINFO pGetUILanguageInfo;

#define NAME_NOT_FOUND_ERROR 0

static VOID
CheckFails(PCSTR Tag, DWORD Flags, PCZZWSTR Languages, PZZWSTR Buffer, DWORD Size, DWORD ExpectedError)
{
    DWORD Attributes = 0xcccccccc;
    DWORD Error;
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = pGetUILanguageInfo(Flags, Languages, Buffer, &Size, &Attributes);
    Error = GetLastError();
    ok(!Ret, "%s: succeeded\n", Tag);
    if (ExpectedError != NAME_NOT_FOUND_ERROR)
    {
        ok(Error == ExpectedError, "%s: error %lu, expected %lu\n", Tag, Error, ExpectedError);
        return;
    }

    ok(Error != ERROR_SUCCESS && Error != 0xdeadbeef && Error != ERROR_INVALID_PARAMETER &&
       Error != ERROR_INSUFFICIENT_BUFFER, "%s: error %lu is not a not-found error\n", Tag, Error);
    trace("%s: not-found error %lu\n", Tag, Error);
}

static VOID
CheckInstalled(PCSTR Tag, DWORD Flags, PCZZWSTR Language)
{
    static const DWORD Kinds = MUI_FULL_LANGUAGE | MUI_PARTIAL_LANGUAGE | MUI_LIP_LANGUAGE;
    WCHAR Buffer[256];
    DWORD Attributes, Size, Required;
    BOOL Ret;

    Attributes = 0;
    Size = 0;
    SetLastError(0xdeadbeef);
    Ret = pGetUILanguageInfo(Flags, Language, NULL, &Size, &Attributes);
    ok(Ret, "%s: size query failed, error %lu\n", Tag, GetLastError());
    ok(Size >= 2, "%s: size %lu\n", Tag, Size);
    ok(Attributes & MUI_LANGUAGE_INSTALLED, "%s: attributes %lx\n", Tag, Attributes);
    ok(Attributes & MUI_FULL_LANGUAGE, "%s: attributes %lx\n", Tag, Attributes);
    ok((Attributes & Kinds) == MUI_FULL_LANGUAGE, "%s: attributes %lx\n", Tag, Attributes);
    trace("%s: attributes %lx size %lu error %lu\n", Tag, Attributes, Size, GetLastError());
    if (!Ret || Size < 2 || Size > _countof(Buffer))
        return;
    Required = Size;

    memset(Buffer, 0xcc, sizeof(Buffer));
    Attributes = 0;
    SetLastError(0xdeadbeef);
    Ret = pGetUILanguageInfo(Flags, Language, Buffer, &Size, &Attributes);
    ok(Ret, "%s: query failed, error %lu\n", Tag, GetLastError());
    ok(Size == Required, "%s: size %lu, expected %lu\n", Tag, Size, Required);
    ok(Ret && !Buffer[Size - 1] && !Buffer[Size - 2], "%s: list is not double-null terminated\n", Tag);
    ok(Attributes & MUI_LANGUAGE_INSTALLED, "%s: attributes %lx\n", Tag, Attributes);
    trace("%s: fallback list %s\n", Tag, wine_dbgstr_w(Buffer));

    Size = 1;
    Attributes = 0;
    SetLastError(0xdeadbeef);
    Ret = pGetUILanguageInfo(Flags, Language, Buffer, &Size, &Attributes);
    ok(!Ret, "%s: one-character buffer succeeded\n", Tag);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "%s: one-character buffer error %lu\n", Tag, GetLastError());
    ok(Size == Required, "%s: one-character buffer size %lu, expected %lu\n", Tag, Size, Required);
}

START_TEST(GetUILanguageInfo)
{
    WCHAR Name[LOCALE_NAME_MAX_LENGTH + 1], Id[6], Pair[2 * LOCALE_NAME_MAX_LENGTH + 2];
    WCHAR Buffer[16];
    LANGID LanguageId;
    SIZE_T Length;

    pGetUILanguageInfo = (PFN_GETUILANGUAGEINFO)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetUILanguageInfo");
    if (!pGetUILanguageInfo)
    {
        skip("GetUILanguageInfo is not available\n");
        return;
    }

    LanguageId = GetSystemDefaultUILanguage();
    memset(Name, 0, sizeof(Name));
    if (!LCIDToLocaleName(MAKELCID(LanguageId, SORT_DEFAULT), Name, LOCALE_NAME_MAX_LENGTH, 0))
    {
        skip("no name for the system default UI language %04x\n", LanguageId);
        return;
    }
    memset(Id, 0, sizeof(Id));
    StringCchPrintfW(Id, 5, L"%04X", LanguageId);
    trace("system default UI language %s\n", wine_dbgstr_w(Name));

    CheckInstalled("name", MUI_LANGUAGE_NAME, Name);
    CheckInstalled("default format", 0, Name);
    CheckInstalled("id", MUI_LANGUAGE_ID, Id);

    CheckFails("null list", MUI_LANGUAGE_NAME, NULL, NULL, 0, ERROR_INVALID_PARAMETER);
    CheckFails("empty list", MUI_LANGUAGE_NAME, L"\0", NULL, 0, ERROR_INVALID_PARAMETER);
    CheckFails("both formats", MUI_LANGUAGE_ID | MUI_LANGUAGE_NAME, Name, NULL, 0, ERROR_INVALID_PARAMETER);
    CheckFails("other flag", MUI_LANGUAGE_NAME | MUI_MERGE_USER_FALLBACK, Name, NULL, 0, ERROR_INVALID_PARAMETER);
    CheckFails("size without buffer", MUI_LANGUAGE_NAME, Name, NULL, 10, ERROR_INVALID_PARAMETER);
    CheckFails("custom unspecified", MUI_LANGUAGE_ID, L"1000\0", Buffer, _countof(Buffer), ERROR_INVALID_PARAMETER);
    CheckFails("short id", MUI_LANGUAGE_ID, L"409\0", Buffer, _countof(Buffer), ERROR_INVALID_PARAMETER);
    CheckFails("long id", MUI_LANGUAGE_ID, L"04090\0", Buffer, _countof(Buffer), ERROR_INVALID_PARAMETER);
    CheckFails("non-hex id", MUI_LANGUAGE_ID, L"04G9\0", Buffer, _countof(Buffer), ERROR_INVALID_PARAMETER);

    CheckFails("not installed", MUI_LANGUAGE_NAME, L"am-ET\0", Buffer, _countof(Buffer), NAME_NOT_FOUND_ERROR);
    CheckFails("not installed id", MUI_LANGUAGE_ID, L"045E\0", Buffer, _countof(Buffer), NAME_NOT_FOUND_ERROR);
    CheckFails("unknown name", MUI_LANGUAGE_NAME, L"xx-YY\0", Buffer, _countof(Buffer), NAME_NOT_FOUND_ERROR);

    memset(Pair, 0, sizeof(Pair));
    Length = wcslen(Name);
    memcpy(Pair, Name, Length * sizeof(WCHAR));
    memcpy(Pair + Length + 1, L"am-ET", sizeof(L"am-ET"));
    CheckFails("fallback of a full language", MUI_LANGUAGE_NAME, Pair, Buffer, _countof(Buffer), NAME_NOT_FOUND_ERROR);
}
