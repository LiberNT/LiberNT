/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test FindNLSString, GetDurationFormat(Ex), GetStringScripts, VerifyScripts and GetGeoInfo
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define LCID_EN_US MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT)
#define LCID_UNKNOWN 0x2C3B
#define GEOID_RUSSIA 203
#define GEOID_SOUTHERN_EUROPE 47610
#define GEOID_UNITED_STATES 244

typedef INT (WINAPI *FN_FindNLSString)(LCID, DWORD, LPCWSTR, INT, LPCWSTR, INT, LPINT);
typedef INT (WINAPI *FN_GetDurationFormat)(LCID, DWORD, const SYSTEMTIME *, ULONGLONG, LPCWSTR, LPWSTR, INT);
typedef INT (WINAPI *FN_GetDurationFormatEx)(LPCWSTR, DWORD, const SYSTEMTIME *, ULONGLONG, LPCWSTR, LPWSTR, INT);
typedef INT (WINAPI *FN_GetStringScripts)(DWORD, LPCWSTR, INT, LPWSTR, INT);
typedef BOOL (WINAPI *FN_VerifyScripts)(DWORD, LPCWSTR, INT, LPCWSTR, INT);
typedef INT (WINAPI *FN_GetGeoInfoA)(GEOID, GEOTYPE, LPSTR, INT, LANGID);
typedef INT (WINAPI *FN_GetGeoInfoW)(GEOID, GEOTYPE, LPWSTR, INT, LANGID);

static FN_FindNLSString pFindNLSString;
static FN_GetDurationFormat pGetDurationFormat;
static FN_GetDurationFormatEx pGetDurationFormatEx;
static FN_GetStringScripts pGetStringScripts;
static FN_VerifyScripts pVerifyScripts;
static FN_GetGeoInfoA pGetGeoInfoA;
static FN_GetGeoInfoW pGetGeoInfoW;

static void
TestFindNLSStringCase(DWORD Flags, LPCWSTR Source, LPCWSTR Value, INT ExpectedIndex, INT ExpectedFound)
{
    INT Found = 0xdeadbeef;
    INT Result;

    Result = pFindNLSString(LCID_EN_US, Flags, Source, -1, Value, -1, &Found);
    ok(Result == ExpectedIndex, "FindNLSString(0x%lx, %s, %s) returned %d, expected %d\n",
       Flags, wine_dbgstr_w(Source), wine_dbgstr_w(Value), Result, ExpectedIndex);
    ok(Found == ExpectedFound, "FindNLSString(0x%lx, %s, %s) found %d, expected %d\n",
       Flags, wine_dbgstr_w(Source), wine_dbgstr_w(Value), Found, ExpectedFound);
}

static void
TestFindNLSStringBody(void)
{
    static const WCHAR Accent1[] = { 0x1e69, 'o', 'u', 'r', 'c', 'e', 0 };
    static const WCHAR Accent2[] = { 's', 0x0323, 0x0307, 'o', 'u', 'r', 'c', 'e', 0 };
    INT Found;
    INT Result;

    TestFindNLSStringCase(FIND_FROMSTART, L"SimpleSimple", L"Simple", 0, 6);
    TestFindNLSStringCase(FIND_FROMEND, L"SimpleSimple", L"Simp", 6, 4);
    TestFindNLSStringCase(FIND_STARTSWITH, L"SimpleSimple", L"Simp", 0, 4);
    TestFindNLSStringCase(FIND_ENDSWITH, L"SimpleSimple", L"Simple", 6, 6);
    TestFindNLSStringCase(FIND_FROMSTART, Accent1, Accent2, 0, 6);
    TestFindNLSStringCase(FIND_FROMSTART, L"x-oss-security", L"-oss", 1, 4);
    TestFindNLSStringCase(FIND_FROMEND, L"x-oss-oss2", L"-oss", 5, 4);

    Found = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Result = pFindNLSString(LCID_EN_US, FIND_ENDSWITH, L"SimpleSimple", -1, L"Simp", -1, &Found);
    ok(Result == -1, "Not found: returned %d\n", Result);
    ok(Found == 0xdeadbeef, "Not found: found %d\n", Found);
    ok(GetLastError() == ERROR_SUCCESS, "Not found: error %lu\n", GetLastError());

    Found = 0xdeadbeef;
    Result = pFindNLSString(LOCALE_USER_DEFAULT, FIND_FROMSTART, L"SimpleSimple", 12, L"Simple", 6, &Found);
    ok(Result == 0, "LOCALE_USER_DEFAULT: returned %d\n", Result);
    ok(Found == 6, "LOCALE_USER_DEFAULT: found %d\n", Found);

    Result = pFindNLSString(LCID_EN_US, FIND_FROMSTART, L"SimpleSimple", -1, L"Simple", -1, NULL);
    ok(Result == 0, "NULL pcchFound: returned %d\n", Result);

    SetLastError(0xdeadbeef);
    Result = pFindNLSString(LCID_UNKNOWN, FIND_FROMSTART, L"foo", 3, L"foo", 3, NULL);
    ok(Result == -1, "Unknown LCID: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Unknown LCID: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pFindNLSString(LCID_EN_US, FIND_FROMSTART, NULL, 3, L"foo", 3, NULL);
    ok(Result == -1, "NULL source: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL source: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pFindNLSString(LCID_EN_US, FIND_FROMSTART, L"foo", -5, L"foo", 3, NULL);
    ok(Result == -1, "Source length -5: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Source length -5: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pFindNLSString(LCID_EN_US, FIND_FROMSTART, L"foo", 0, L"foo", 3, NULL);
    ok(Result == -1, "Source length 0: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Source length 0: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pFindNLSString(LCID_EN_US, FIND_FROMSTART, L"foo", 3, NULL, 3, NULL);
    ok(Result == -1, "NULL value: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL value: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pFindNLSString(LCID_EN_US, FIND_FROMSTART, L"foo", 3, L"foo", -5, NULL);
    ok(Result == -1, "Value length -5: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Value length -5: error %lu\n", GetLastError());
}

static void
TestDurationCase(const SYSTEMTIME *Duration, ULONGLONG Ticks, LPCWSTR Format, LPCWSTR Expected)
{
    WCHAR Buffer[64];
    INT Result;

    Buffer[0] = UNICODE_NULL;
    Result = pGetDurationFormatEx(L"en-US", 0, Duration, Ticks, Format, Buffer, ARRAYSIZE(Buffer));
    ok(Result == (INT)wcslen(Expected) + 1, "%s returned %d, expected %d\n",
       wine_dbgstr_w(Format), Result, (INT)wcslen(Expected) + 1);
    ok(!wcscmp(Buffer, Expected), "%s gave %s, expected %s\n",
       wine_dbgstr_w(Format), wine_dbgstr_w(Buffer), wine_dbgstr_w(Expected));

    Result = pGetDurationFormatEx(L"en-US", 0, Duration, Ticks, Format, NULL, 0);
    ok(Result == (INT)wcslen(Expected) + 1, "%s size query returned %d\n", wine_dbgstr_w(Format), Result);
}

static void
TestGetDurationFormatBody(void)
{
    static const ULONGLONG Seconds345 = 3450000000ULL;
    static const ULONGLONG Fraction = 51234567ULL;
    SYSTEMTIME Days = { 2024, 7, 3, 14, 2, 45, 12, 247 };
    SYSTEMTIME Minutes = { 0, 0, 0, 0, 0, 5, 45, 0 };
    WCHAR Buffer[64], Buffer2[64];
    INT Result, Result2;

    TestDurationCase(&Days, 0, L"d:hh:mm:ss", L"14:02:45:12");
    TestDurationCase(&Days, 0, L"hh:mm:ss:ff", L"338:45:12:24");
    TestDurationCase(&Days, 0, L"hh:mm:ss:fff", L"338:45:12:247");
    TestDurationCase(&Days, 0, L"h' h 'mm' m 'ss' s'", L"338 h 45 m 12 s");

    TestDurationCase(NULL, Seconds345, L"hh:mm:ss", L"00:05:45");
    TestDurationCase(NULL, Seconds345, L"h:mm:ss", L"0:05:45");
    TestDurationCase(NULL, Seconds345, L"mm:ss", L"05:45");
    TestDurationCase(NULL, Seconds345, L"m:ss", L"5:45");
    TestDurationCase(NULL, Seconds345, L"mm' m 'ss' s'", L"05 m 45 s");
    TestDurationCase(NULL, Seconds345, L"ss", L"345");
    TestDurationCase(NULL, Seconds345, L"ss' seconds'", L"345 seconds");
    TestDurationCase(NULL, 3100000000ULL, L"m:ss", L"5:10");

    TestDurationCase(NULL, Fraction, L"s.fff", L"5.123");
    TestDurationCase(NULL, Fraction, L"s.ffffff", L"5.123456");
    TestDurationCase(NULL, Fraction, L"s.fffffffff", L"5.123456700");
    TestDurationCase(NULL, Fraction, L"fff 'ms'", L"5123 ms");
    TestDurationCase(NULL, Fraction, L"ffffff 'microseconds'", L"5123456 microseconds");
    TestDurationCase(NULL, Fraction, L"fffffffff 'ns'", L"5123456700 ns");

    TestDurationCase(NULL, 0, L"h:mm:ss", L"0:00:00");
    TestDurationCase(&Minutes, Fraction, L"hh:mm:ss", L"00:05:45");

    SetLastError(0xdeadbeef);
    Result = pGetDurationFormatEx(L"en-US", 0, NULL, Seconds345, L"hh:mm:ss", Buffer, 5);
    ok(Result == 0, "Short buffer: returned %d\n", Result);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Short buffer: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pGetDurationFormatEx(L"en-US", 0, NULL, Seconds345, L"hh:mm:ss", NULL, ARRAYSIZE(Buffer));
    ok(Result == 0, "NULL buffer: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL buffer: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pGetDurationFormatEx(L"en-US", LOCALE_NOUSEROVERRIDE, NULL, Seconds345, L"hh:mm:ss",
                                  Buffer, ARRAYSIZE(Buffer));
    ok(Result == 0, "Flags with a format: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Flags with a format: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pGetDurationFormatEx(L"xx-INVALID", 0, NULL, Seconds345, L"hh:mm:ss", Buffer, ARRAYSIZE(Buffer));
    ok(Result == 0, "Invalid locale: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Invalid locale: error %lu\n", GetLastError());

    Result = pGetDurationFormatEx(L"en-US", 0, NULL, Seconds345, NULL, Buffer, ARRAYSIZE(Buffer));
    ok(Result > 1, "Locale format: returned %d\n", Result);
    Result2 = pGetDurationFormatEx(L"en-US", LOCALE_NOUSEROVERRIDE, NULL, Seconds345, NULL,
                                   Buffer2, ARRAYSIZE(Buffer2));
    ok(Result2 == Result && !wcscmp(Buffer, Buffer2), "LOCALE_NOUSEROVERRIDE gave %d %s, expected %d %s\n",
       Result2, wine_dbgstr_w(Buffer2), Result, wine_dbgstr_w(Buffer));

    Result2 = pGetDurationFormat(LCID_EN_US, 0, NULL, Seconds345, NULL, Buffer2, ARRAYSIZE(Buffer2));
    ok(Result2 == Result && !wcscmp(Buffer, Buffer2), "GetDurationFormat locale format gave %d %s, expected %d %s\n",
       Result2, wine_dbgstr_w(Buffer2), Result, wine_dbgstr_w(Buffer));

    Buffer[0] = UNICODE_NULL;
    Result = pGetDurationFormat(LCID_EN_US, 0, &Days, 0, L"d:hh:mm:ss", Buffer, ARRAYSIZE(Buffer));
    ok(Result == 12 && !wcscmp(Buffer, L"14:02:45:12"), "GetDurationFormat gave %d %s\n",
       Result, wine_dbgstr_w(Buffer));

    Result = pGetDurationFormat(LOCALE_INVARIANT, 0, NULL, Seconds345, L"m:ss", NULL, 0);
    ok(Result == 5, "GetDurationFormat(LOCALE_INVARIANT) size query returned %d\n", Result);

    SetLastError(0xdeadbeef);
    Result = pGetDurationFormat(LCID_UNKNOWN, 0, NULL, Seconds345, L"m:ss", Buffer, ARRAYSIZE(Buffer));
    ok(Result == 0, "GetDurationFormat unknown LCID: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "GetDurationFormat unknown LCID: error %lu\n", GetLastError());
}

static void
TestScriptsCase(DWORD Flags, LPCWSTR String, INT Length, LPCWSTR Expected)
{
    WCHAR Buffer[64];
    INT Result;

    Buffer[0] = 0xffff;
    Result = pGetStringScripts(Flags, String, Length, Buffer, ARRAYSIZE(Buffer));
    ok(Result == (INT)wcslen(Expected) + 1, "GetStringScripts(0x%lx, %s) returned %d, expected %d\n",
       Flags, wine_dbgstr_w(String), Result, (INT)wcslen(Expected) + 1);
    ok(!wcscmp(Buffer, Expected), "GetStringScripts(0x%lx, %s) gave %s, expected %s\n",
       Flags, wine_dbgstr_w(String), wine_dbgstr_w(Buffer), wine_dbgstr_w(Expected));

    Result = pGetStringScripts(Flags, String, Length, NULL, 0);
    ok(Result == (INT)wcslen(Expected) + 1, "GetStringScripts(0x%lx, %s) size query returned %d\n",
       Flags, wine_dbgstr_w(String), Result);
}

static void
TestGetStringScriptsBody(void)
{
    static const WCHAR Mixed[] =
        L"This string has \x0421\x0443\x0433\x0456\x05c0\x05c0\x0456\x0441, Hebrew, and GR\x0395\x0395\x039a "
        L"\x5341 Chinese \x5de5\x53e3 and PUA \xf8d9 characters.  Depending on font it may look like Latin";
    static const WCHAR Russian[] = L"\x0440\x0443\x0441\x0441\x043a\x0438\x0439 (Russian)";
    WCHAR Buffer[64];
    INT Result;

    TestScriptsCase(0, L"Microsoft.com", -1, L"Latn;");
    TestScriptsCase(GSS_ALLOW_INHERITED_COMMON, L"Microsoft.com", -1, L"Latn;Zyyy;");
    TestScriptsCase(GSS_ALLOW_INHERITED_COMMON, L"Ni\x00f1o", -1, L"Latn;");
    TestScriptsCase(GSS_ALLOW_INHERITED_COMMON, L"Nin\x0303o", -1, L"Latn;Qaii;");
    TestScriptsCase(0, L"Sp\x043e\x043e" L"f", -1, L"Cyrl;Latn;");
    TestScriptsCase(0, L"\xf000", -1, L"Zzzz;");
    TestScriptsCase(GSS_ALLOW_INHERITED_COMMON, L"\xf000", -1, L"Zzzz;");
    TestScriptsCase(0, Mixed, -1, L"Cyrl;Grek;Hani;Hebr;Latn;Zzzz;");
    TestScriptsCase(0, Russian, -1, L"Cyrl;Latn;");
    TestScriptsCase(0, L"123 .,", -1, L"");
    TestScriptsCase(0, L"abc\x043e", 3, L"Latn;");
    TestScriptsCase(0, L"\xd801\xdc00", -1, L"Dsrt;");

    Buffer[0] = 0xffff;
    Result = pGetStringScripts(0, L"abc", 0, Buffer, ARRAYSIZE(Buffer));
    ok(Result == 1, "Empty string: returned %d\n", Result);
    ok(Buffer[0] == UNICODE_NULL, "Empty string: gave 0x%04x\n", Buffer[0]);

    SetLastError(0xdeadbeef);
    Result = pGetStringScripts(GSS_ALLOW_INHERITED_COMMON, L"Microsoft.com", -1, Buffer, 5);
    ok(Result == 0, "Short buffer: returned %d\n", Result);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Short buffer: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pGetStringScripts(2, L"Microsoft.com", -1, Buffer, ARRAYSIZE(Buffer));
    ok(Result == 0, "Invalid flags: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_FLAGS, "Invalid flags: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pGetStringScripts(0, NULL, 3, Buffer, ARRAYSIZE(Buffer));
    ok(Result == 0, "NULL string: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL string: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pGetStringScripts(0, L"abc", -2, Buffer, ARRAYSIZE(Buffer));
    ok(Result == 0, "String length -2: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "String length -2: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pGetStringScripts(0, L"abc", -1, NULL, ARRAYSIZE(Buffer));
    ok(Result == 0, "NULL buffer: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL buffer: error %lu\n", GetLastError());
}

static void
TestVerifyScriptsCase(DWORD Flags, LPCWSTR LocaleScripts, LPCWSTR TestScripts, BOOL ExpectedResult, DWORD ExpectedError)
{
    BOOL Result;

    SetLastError(0xdeadbeef);
    Result = pVerifyScripts(Flags, LocaleScripts, -1, TestScripts, -1);
    ok(Result == ExpectedResult, "VerifyScripts(0x%lx, %s, %s) returned %d, expected %d\n",
       Flags, wine_dbgstr_w(LocaleScripts), wine_dbgstr_w(TestScripts), Result, ExpectedResult);
    ok(GetLastError() == ExpectedError, "VerifyScripts(0x%lx, %s, %s) error %lu, expected %lu\n",
       Flags, wine_dbgstr_w(LocaleScripts), wine_dbgstr_w(TestScripts), GetLastError(), ExpectedError);
}

static void
TestVerifyScriptsBody(void)
{
    static const WCHAR Japanese[] = L"Hani;Hira;Kana;";
    WCHAR Scripts[64];
    BOOL Result;

    TestVerifyScriptsCase(0, Japanese, L"Hani;", TRUE, 0xdeadbeef);
    TestVerifyScriptsCase(VS_ALLOW_LATIN, Japanese, L"Hani;", TRUE, 0xdeadbeef);
    TestVerifyScriptsCase(0, Japanese, L"Hani;Latn;", FALSE, ERROR_SUCCESS);
    TestVerifyScriptsCase(VS_ALLOW_LATIN, Japanese, L"Hani;Latn;", TRUE, 0xdeadbeef);
    TestVerifyScriptsCase(0, Japanese, L"Cyrl;", FALSE, ERROR_SUCCESS);
    TestVerifyScriptsCase(VS_ALLOW_LATIN, Japanese, L"Cyrl;", FALSE, ERROR_SUCCESS);
    TestVerifyScriptsCase(0, L"Hani;", Japanese, FALSE, ERROR_SUCCESS);
    TestVerifyScriptsCase(VS_ALLOW_LATIN, L"Hani;", Japanese, FALSE, ERROR_SUCCESS);
    TestVerifyScriptsCase(0, Japanese, L"Cyrl", FALSE, ERROR_INVALID_PARAMETER);
    TestVerifyScriptsCase(VS_ALLOW_LATIN, Japanese, L"Cyrl", FALSE, ERROR_INVALID_PARAMETER);
    TestVerifyScriptsCase(0, Japanese, L"", TRUE, 0xdeadbeef);
    TestVerifyScriptsCase(VS_ALLOW_LATIN, Japanese, L"", TRUE, 0xdeadbeef);
    TestVerifyScriptsCase(2, Japanese, L"Hani;", FALSE, ERROR_INVALID_FLAGS);

    SetLastError(0xdeadbeef);
    Result = pVerifyScripts(0, Japanese, -1, L"Hani;", 0);
    ok(!Result, "Test length 0: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Test length 0: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pVerifyScripts(0, Japanese, 0, L"Hani;", -1);
    ok(!Result, "Locale length 0: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Locale length 0: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pVerifyScripts(0, NULL, -1, L"Hani;", -1);
    ok(!Result, "NULL locale list: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL locale list: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pVerifyScripts(0, Japanese, -1, NULL, -1);
    ok(!Result, "NULL test list: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL test list: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pVerifyScripts(0, Japanese, 5, L"Hira;", -1);
    ok(!Result, "Locale length 5: returned %d\n", Result);
    ok(GetLastError() == ERROR_SUCCESS, "Locale length 5: error %lu\n", GetLastError());

    Result = pGetStringScripts(0, L"\x0440\x0443\x0441\x0441\x043a\x0438\x0439 (Russian)", -1, Scripts, ARRAYSIZE(Scripts));
    ok(Result == 11, "GetStringScripts returned %d\n", Result);
    SetLastError(0xdeadbeef);
    Result = pVerifyScripts(0, L"Cyrl;", -1, Scripts, -1);
    ok(!Result && GetLastError() == ERROR_SUCCESS, "Cyrl; against %s: %d, error %lu\n",
       wine_dbgstr_w(Scripts), Result, GetLastError());
    Result = pVerifyScripts(VS_ALLOW_LATIN, L"Cyrl;", -1, Scripts, -1);
    ok(Result, "Cyrl; with VS_ALLOW_LATIN against %s: %d\n", wine_dbgstr_w(Scripts), Result);
}

static void
TestGeoInfoA(GEOID Geo, GEOTYPE Type, LANGID Lang, LPCSTR Expected)
{
    CHAR Buffer[32];
    INT Result;

    Buffer[0] = 0;
    Result = pGetGeoInfoA(Geo, Type, Buffer, sizeof(Buffer), Lang);
    ok(Result == (INT)strlen(Expected) + 1, "GetGeoInfoA(%ld, %ld, 0x%x) returned %d, expected %d\n",
       Geo, Type, Lang, Result, (INT)strlen(Expected) + 1);
    ok(!strcmp(Buffer, Expected), "GetGeoInfoA(%ld, %ld, 0x%x) gave %s, expected %s\n",
       Geo, Type, Lang, Buffer, Expected);
}

static void
TestGeoInfoW(GEOID Geo, GEOTYPE Type, LANGID Lang, LPCWSTR Expected)
{
    WCHAR Buffer[32];
    INT Result;

    Buffer[0] = 0;
    Result = pGetGeoInfoW(Geo, Type, Buffer, ARRAYSIZE(Buffer), Lang);
    ok(Result == (INT)wcslen(Expected) + 1, "GetGeoInfoW(%ld, %ld, 0x%x) returned %d, expected %d\n",
       Geo, Type, Lang, Result, (INT)wcslen(Expected) + 1);
    ok(!wcscmp(Buffer, Expected), "GetGeoInfoW(%ld, %ld, 0x%x) gave %s, expected %s\n",
       Geo, Type, Lang, wine_dbgstr_w(Buffer), wine_dbgstr_w(Expected));
}

static void
TestGeoInfoAMatchesW(GEOID Geo, GEOTYPE Type)
{
    CHAR BufferA[32], ExpectedA[32];
    WCHAR BufferW[32];
    INT ResultA, ResultW;

    BufferA[0] = 0;
    BufferW[0] = 0;
    ResultW = pGetGeoInfoW(Geo, Type, BufferW, ARRAYSIZE(BufferW), 0);
    ResultA = pGetGeoInfoA(Geo, Type, BufferA, sizeof(BufferA), 0);
    ok(ResultW > 1, "GetGeoInfoW(%ld, %ld) returned %d\n", Geo, Type, ResultW);
    WideCharToMultiByte(CP_ACP, 0, BufferW, -1, ExpectedA, sizeof(ExpectedA), NULL, NULL);
    ok(ResultA == (INT)strlen(ExpectedA) + 1 && !strcmp(BufferA, ExpectedA),
       "GetGeoInfoA(%ld, %ld) gave %d %s, expected %s\n", Geo, Type, ResultA, BufferA, ExpectedA);
}

static void
TestGetGeoInfoBody(void)
{
    CHAR Expected[32], Iso639[16], BufferA[32];
    WCHAR ExpectedW[32];
    INT Result;
    UINT Index;

    TestGeoInfoA(GEOID_RUSSIA, GEO_RFC1766, 0x143b, "smj-ru");
    TestGeoInfoW(GEOID_RUSSIA, GEO_RFC1766, 0x143b, L"smj-ru");
    TestGeoInfoA(GEOID_RUSSIA, GEO_LCID, 0x143b, "0000143B");
    TestGeoInfoW(GEOID_RUSSIA, GEO_LCID, 0x143b, L"0000143B");
    TestGeoInfoA(GEOID_UNITED_STATES, GEO_RFC1766, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK), "en-us");
    TestGeoInfoW(GEOID_UNITED_STATES, GEO_RFC1766, MAKELANGID(LANG_CHINESE, SUBLANG_NEUTRAL), L"zh-us");

    GetLocaleInfoA(GetUserDefaultLangID(), LOCALE_SISO639LANGNAME, Iso639, sizeof(Iso639));
    StringCchPrintfA(Expected, ARRAYSIZE(Expected), "%s-ru", Iso639);
    for (Index = 0; Expected[Index]; Index++)
    {
        if (Expected[Index] >= 'A' && Expected[Index] <= 'Z')
            Expected[Index] += 'a' - 'A';
    }
    TestGeoInfoA(GEOID_RUSSIA, GEO_RFC1766, 0, Expected);
    StringCchPrintfA(Expected, ARRAYSIZE(Expected), "%08X", GetUserDefaultLangID());
    TestGeoInfoA(GEOID_RUSSIA, GEO_LCID, 0, Expected);
    MultiByteToWideChar(CP_ACP, 0, Expected, -1, ExpectedW, ARRAYSIZE(ExpectedW));
    TestGeoInfoW(GEOID_RUSSIA, GEO_LCID, 0, ExpectedW);

    SetLastError(0xdeadbeef);
    Result = pGetGeoInfoA(GEOID_RUSSIA, GEO_RFC1766, BufferA, sizeof(BufferA), LCID_UNKNOWN);
    ok(Result == 0, "GEO_RFC1766 unknown language: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "GEO_RFC1766 unknown language: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pGetGeoInfoW(GEOID_RUSSIA, GEO_LCID, ExpectedW, ARRAYSIZE(ExpectedW), LCID_UNKNOWN);
    ok(Result == 0, "GEO_LCID unknown language: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "GEO_LCID unknown language: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pGetGeoInfoA(GEOID_RUSSIA, GEO_TIMEZONES, BufferA, sizeof(BufferA), 0);
    ok(Result == 0, "GEO_TIMEZONES: returned %d\n", Result);
    ok(GetLastError() == 0xdeadbeef, "GEO_TIMEZONES: error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = pGetGeoInfoW(GEOID_RUSSIA, GEO_OFFICIALLANGUAGES, ExpectedW, ARRAYSIZE(ExpectedW), 0);
    ok(Result == 0, "GEO_OFFICIALLANGUAGES: returned %d\n", Result);
    ok(GetLastError() == 0xdeadbeef, "GEO_OFFICIALLANGUAGES: error %lu\n", GetLastError());

    TestGeoInfoA(GEOID_RUSSIA, GEO_NAME, 0, "RU");
    TestGeoInfoA(GEOID_SOUTHERN_EUROPE, GEO_NAME, 0, "039");
    TestGeoInfoW(GEOID_SOUTHERN_EUROPE, GEO_NAME, 0, L"039");
    TestGeoInfoA(GEOID_RUSSIA, GEO_ID, 0, "203");
    TestGeoInfoA(GEOID_SOUTHERN_EUROPE, GEO_ID, 0, "47610");
    TestGeoInfoA(GEOID_RUSSIA, GEO_PARENT, 0, "47609");
    TestGeoInfoA(GEOID_RUSSIA, GEO_ISO_UN_NUMBER, 0, "643");
    TestGeoInfoA(GEOID_UNITED_STATES, GEO_DIALINGCODE, 0, "1");
    TestGeoInfoA(GEOID_UNITED_STATES, GEO_CURRENCYCODE, 0, "USD");
    TestGeoInfoA(GEOID_UNITED_STATES, GEO_CURRENCYSYMBOL, 0, "$");
    TestGeoInfoAMatchesW(GEOID_RUSSIA, GEO_LATITUDE);
    TestGeoInfoAMatchesW(GEOID_RUSSIA, GEO_LONGITUDE);

    if (GetUserDefaultLangID() == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US))
    {
        GetLocaleInfoA(MAKELANGID(LANG_RUSSIAN, SUBLANG_DEFAULT), LOCALE_SENGCOUNTRY, Expected, sizeof(Expected));
        TestGeoInfoA(GEOID_RUSSIA, GEO_FRIENDLYNAME, 0, Expected);
    }
    else
    {
        skip("User language is not en-US, localized geo names not tested\n");
    }

    SetLastError(0xdeadbeef);
    Result = pGetGeoInfoA(GEOID_RUSSIA, GEO_ID + 1, NULL, 0, 0);
    ok(Result == 0, "Invalid type: returned %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_FLAGS, "Invalid type: error %lu\n", GetLastError());
}

START_TEST(NlsStringApis)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");

    pFindNLSString = (FN_FindNLSString)GetProcAddress(Kernel32, "FindNLSString");
    pGetDurationFormat = (FN_GetDurationFormat)GetProcAddress(Kernel32, "GetDurationFormat");
    pGetDurationFormatEx = (FN_GetDurationFormatEx)GetProcAddress(Kernel32, "GetDurationFormatEx");
    pGetStringScripts = (FN_GetStringScripts)GetProcAddress(Kernel32, "GetStringScripts");
    pVerifyScripts = (FN_VerifyScripts)GetProcAddress(Kernel32, "VerifyScripts");
    pGetGeoInfoA = (FN_GetGeoInfoA)GetProcAddress(Kernel32, "GetGeoInfoA");
    pGetGeoInfoW = (FN_GetGeoInfoW)GetProcAddress(Kernel32, "GetGeoInfoW");

    ok(pFindNLSString != NULL, "FindNLSString is not exported\n");
    ok(pGetDurationFormat != NULL, "GetDurationFormat is not exported\n");
    ok(pGetDurationFormatEx != NULL, "GetDurationFormatEx is not exported\n");
    ok(pGetStringScripts != NULL, "GetStringScripts is not exported\n");
    ok(pVerifyScripts != NULL, "VerifyScripts is not exported\n");
    ok(pGetGeoInfoA != NULL && pGetGeoInfoW != NULL, "GetGeoInfoA/W are not exported\n");

    if (pFindNLSString)
    {
        StartSeh()
            TestFindNLSStringBody();
        EndSeh(STATUS_SUCCESS);
    }
    if (pGetDurationFormat && pGetDurationFormatEx)
    {
        StartSeh()
            TestGetDurationFormatBody();
        EndSeh(STATUS_SUCCESS);
    }
    if (pGetStringScripts)
    {
        StartSeh()
            TestGetStringScriptsBody();
        EndSeh(STATUS_SUCCESS);
    }
    if (pVerifyScripts && pGetStringScripts)
    {
        StartSeh()
            TestVerifyScriptsBody();
        EndSeh(STATUS_SUCCESS);
    }
    if (pGetGeoInfoA && pGetGeoInfoW)
    {
        StartSeh()
            TestGetGeoInfoBody();
        EndSeh(STATUS_SUCCESS);
    }
}
