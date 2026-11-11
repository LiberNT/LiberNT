/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Test for GetLocaleInfo(Ex)
 * PROGRAMMER:      Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include "precomp.h"

typedef
int
WINAPI
FN_GetLocaleInfoEx(
    _In_opt_ LPCWSTR lpLocaleName,
    _In_ LCTYPE  LCType,
    _Out_opt_ LPWSTR lpLCData,
    _In_ int cchData);

FN_GetLocaleInfoEx* pGetLocaleInfoEx = NULL;

static void Test_GetLocaleInfoW_ErrorClassification(void)
{
    static const struct
    {
        LCID locale;
        const WCHAR *language;
        const WCHAR *country;
        BOOL valid;
    } locales[] =
    {
        {0x0409, L"en", L"US", TRUE},
        {0x0007, L"de", L"DE", TRUE},
        {0x007f, L"iv", L"IV", TRUE},
        {0, NULL, NULL, TRUE},
        {0xffff, NULL, NULL, FALSE},
        {0xffffffff, NULL, NULL, FALSE}
    };
    static const LCTYPE types[] = {LOCALE_SISO639LANGNAME, LOCALE_SISO3166CTRYNAME, 0xffff};
    struct
    {
        BYTE before[8];
        WCHAR value[32];
        BYTE after[8];
    } output, initial;
    unsigned int i, j, repeat;
    const WCHAR *expected;
    DWORD error, expected_error;
    int ret;

    memset(&initial, 0xa5, sizeof(initial));
    for (i = 0; i < sizeof(locales) / sizeof(locales[0]); ++i)
    {
        for (j = 0; j < sizeof(types) / sizeof(types[0]); ++j)
        {
            for (repeat = 0; repeat < 2; ++repeat)
            {
                output = initial;
                SetLastError(0xdeadbeef);
                ret = GetLocaleInfoW(locales[i].locale, types[j], output.value,
                                     sizeof(output.value) / sizeof(output.value[0]));
                error = GetLastError();
                ok(!memcmp(output.before, initial.before, sizeof(output.before)),
                   "locale %lx type %lx repeat %u changed prefix\n", locales[i].locale, types[j], repeat);
                ok(!memcmp(output.after, initial.after, sizeof(output.after)),
                   "locale %lx type %lx repeat %u changed suffix\n", locales[i].locale, types[j], repeat);
                if (!locales[i].valid || j == 2)
                {
                    expected_error = locales[i].valid ? ERROR_INVALID_FLAGS : ERROR_INVALID_PARAMETER;
                    ok(ret == 0, "locale %lx type %lx repeat %u returned %d\n",
                       locales[i].locale, types[j], repeat, ret);
                    ok(error == expected_error, "locale %lx type %lx repeat %u error %lu expected %lu\n",
                       locales[i].locale, types[j], repeat, error, expected_error);
                    ok(!memcmp(output.value, initial.value, sizeof(output.value)),
                       "locale %lx type %lx repeat %u changed failed output\n", locales[i].locale, types[j], repeat);
                    continue;
                }
                expected = j ? locales[i].country : locales[i].language;
                ok(ret > 0 && ret <= (int)(sizeof(output.value) / sizeof(output.value[0])),
                   "locale %lx type %lx repeat %u returned %d\n", locales[i].locale, types[j], repeat, ret);
                ok(error == 0xdeadbeef, "locale %lx type %lx repeat %u error %lu\n",
                   locales[i].locale, types[j], repeat, error);
                if (ret <= 0 || ret > (int)(sizeof(output.value) / sizeof(output.value[0]))) continue;
                ok(output.value[ret - 1] == 0, "locale %lx type %lx repeat %u missing terminator\n",
                   locales[i].locale, types[j], repeat);
                ok(!memcmp(output.value + ret, initial.value + ret, sizeof(output.value) - ret * sizeof(WCHAR)),
                   "locale %lx type %lx repeat %u changed tail\n", locales[i].locale, types[j], repeat);
                if (expected)
                {
                    ok(ret == lstrlenW(expected) + 1, "locale %lx type %lx repeat %u count %d\n",
                       locales[i].locale, types[j], repeat, ret);
                    ok(!memcmp(output.value, expected, (lstrlenW(expected) + 1) * sizeof(WCHAR)),
                       "locale %lx type %lx repeat %u wrong text\n", locales[i].locale, types[j], repeat);
                }
            }
        }
    }
}

static void Test_GetLocaleInfoEx(void)
{
    HMODULE hmodKernel32;
    int Ret;
    ULONG CodePage;

    hmodKernel32 = GetModuleHandleW(L"kernel32.dll");
    pGetLocaleInfoEx = (FN_GetLocaleInfoEx*)GetProcAddress(hmodKernel32, "GetLocaleInfoEx");
    if (pGetLocaleInfoEx == NULL)
    {
        hmodKernel32 = LoadLibraryW(L"kernel32_vista.dll");
        pGetLocaleInfoEx = (FN_GetLocaleInfoEx*)GetProcAddress(hmodKernel32, "GetLocaleInfoEx");
        if (pGetLocaleInfoEx == NULL)
        {
            skip("GetLocaleInfoEx not found in kernel32.dll\n");
            return;
        }
    }

    // Test normal usage
    Ret = pGetLocaleInfoEx(L"en-US",
                           LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                           (WCHAR*)&CodePage,
                           sizeof(DWORD) / sizeof(WCHAR));
    ok_eq_int(Ret, 2);
    ok_eq_long(CodePage, 1252ul);

    // Test with neutral locale
    Ret = pGetLocaleInfoEx(L"en",
                           LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                           NULL,
                           0);
    ok_eq_int(Ret, 2);
    ok_eq_long(CodePage, 1252ul);

    // Test with NULL locale name
    CodePage = 0xdeadbeef;
    Ret = pGetLocaleInfoEx(NULL,
                           LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                           (WCHAR *)&CodePage,
                           sizeof(DWORD) /sizeof(WCHAR));
    ok_eq_int(Ret, 2);
    ok_eq_long(CodePage, 1252ul);

    // Test with empty locale name
    CodePage = 0xdeadbeef;
    Ret = pGetLocaleInfoEx(L"",
                           LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                           (WCHAR *)&CodePage,
                           sizeof(DWORD) /sizeof(WCHAR));
    ok_eq_int(Ret, 2);
    ok_eq_long(CodePage, 1252ul);

    // Test with invalid locale name
    CodePage = 0xdeadbeef;
    Ret = pGetLocaleInfoEx(L"invalid",
                           LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                           (WCHAR *)&CodePage,
                           sizeof(DWORD) /sizeof(WCHAR));
    ok_eq_int(Ret, 0);
    ok_eq_long(GetLastError(), (ULONG)ERROR_INVALID_PARAMETER);
    ok(CodePage == 0xdeadbeef, "CodePage should not have been modified: %lx\n", CodePage);

}

typedef int (WINAPI *FN_LocaleNameFunc)(LPWSTR, int);
typedef int (WINAPI *FN_LCIDToLocaleName)(LCID, LPWSTR, int, DWORD);

static void Test_ConsoleFallbackName(void)
{
    static const struct
    {
        const WCHAR *locale;
        const WCHAR *fallback;
    } tests[] =
    {
        { L"ar-SA", L"en" },
        { L"hi-IN", L"en" },
        { L"he-IL", L"en" },
        { L"th-TH", L"en" },
        { L"fa-IR", L"en" },
        { L"ar-MA", L"fr" },
        { L"en-US", L"en-US" },
        { L"fr-FR", L"fr-FR" },
        { L"de-DE", L"de-DE" },
        { L"ja-JP", L"ja-JP" },
        { L"ru-RU", L"ru-RU" },
    };
    WCHAR buffer[86];
    char bufferA[86];
    unsigned int i;
    HMODULE hmodVista;
    FN_GetLocaleInfoEx *pVistaGetLocaleInfoEx;
    int ret;

    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
        memset(buffer, 0xcc, sizeof(buffer));
        SetLastError(0xdeadbeef);
        ret = pGetLocaleInfoEx(tests[i].locale, LOCALE_SCONSOLEFALLBACKNAME, buffer, sizeof(buffer) / sizeof(buffer[0]));
        ok(ret == lstrlenW(tests[i].fallback) + 1, "%S: console fallback returned %d, error %lu\n",
           tests[i].locale, ret, GetLastError());
        ok(ret && !lstrcmpW(buffer, tests[i].fallback), "%S: console fallback %S, expected %S\n",
           tests[i].locale, ret ? buffer : L"(none)", tests[i].fallback);
    }

    SetLastError(0xdeadbeef);
    ret = pGetLocaleInfoEx(L"ar-SA", LOCALE_SCONSOLEFALLBACKNAME, NULL, 0);
    ok(ret == 3, "ar-SA console fallback size query returned %d, error %lu\n", ret, GetLastError());
    ok(GetLastError() == 0xdeadbeef, "ar-SA console fallback size query changed the error to %lu\n", GetLastError());

    memset(buffer, 0xcc, sizeof(buffer));
    SetLastError(0xdeadbeef);
    ret = pGetLocaleInfoEx(L"ar-SA", LOCALE_SCONSOLEFALLBACKNAME, buffer, 2);
    ok(ret == 0, "ar-SA console fallback with 2 characters returned %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "ar-SA console fallback with 2 characters error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = pGetLocaleInfoEx(L"ar-SA", LOCALE_SCONSOLEFALLBACKNAME, NULL, 4);
    ok(ret == 0, "ar-SA console fallback into NULL returned %d\n", ret);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "ar-SA console fallback into NULL error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = pGetLocaleInfoEx(L"ar-SA", LOCALE_SCONSOLEFALLBACKNAME, buffer, -1);
    ok(ret == 0, "ar-SA console fallback with -1 characters returned %d\n", ret);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "ar-SA console fallback with -1 characters error %lu\n", GetLastError());

    memset(buffer, 0xcc, sizeof(buffer));
    ret = GetLocaleInfoW(MAKELCID(MAKELANGID(LANG_ARABIC, SUBLANG_ARABIC_SAUDI_ARABIA), SORT_DEFAULT),
                         LOCALE_SCONSOLEFALLBACKNAME, buffer, sizeof(buffer) / sizeof(buffer[0]));
    ok(ret == 3 && !lstrcmpW(buffer, L"en"), "GetLocaleInfoW 0401 console fallback returned %d %S\n",
       ret, ret ? buffer : L"(none)");

    memset(bufferA, 0xcc, sizeof(bufferA));
    ret = GetLocaleInfoA(MAKELCID(MAKELANGID(LANG_ARABIC, SUBLANG_ARABIC_SAUDI_ARABIA), SORT_DEFAULT),
                         LOCALE_SCONSOLEFALLBACKNAME, bufferA, sizeof(bufferA));
    ok(ret == 3 && !lstrcmpA(bufferA, "en"), "GetLocaleInfoA 0401 console fallback returned %d %s\n",
       ret, ret ? bufferA : "(none)");

    memset(buffer, 0xcc, sizeof(buffer));
    ret = GetLocaleInfoW(MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT),
                         LOCALE_SCONSOLEFALLBACKNAME, buffer, sizeof(buffer) / sizeof(buffer[0]));
    ok(ret == 6 && !lstrcmpW(buffer, L"en-US"), "GetLocaleInfoW 0409 console fallback returned %d %S\n",
       ret, ret ? buffer : L"(none)");

    hmodVista = LoadLibraryW(L"kernel32_vista.dll");
    if (!hmodVista)
    {
        skip("kernel32_vista.dll not present\n");
        return;
    }
    pVistaGetLocaleInfoEx = (FN_GetLocaleInfoEx*)GetProcAddress(hmodVista, "GetLocaleInfoEx");
    ok(pVistaGetLocaleInfoEx != NULL, "kernel32_vista.dll has no GetLocaleInfoEx\n");
    if (pVistaGetLocaleInfoEx)
    {
        memset(buffer, 0xcc, sizeof(buffer));
        ret = pVistaGetLocaleInfoEx(L"ar-SA", LOCALE_SCONSOLEFALLBACKNAME, buffer, sizeof(buffer) / sizeof(buffer[0]));
        ok(ret == 3 && !lstrcmpW(buffer, L"en"), "kernel32_vista GetLocaleInfoEx console fallback returned %d %S\n",
           ret, ret ? buffer : L"(none)");
    }
    FreeLibrary(hmodVista);
}

static void Test_PersianCalendarType(void)
{
    WCHAR buffer[16];
    DWORD value;
    int ret;

    memset(buffer, 0xcc, sizeof(buffer));
    SetLastError(0xdeadbeef);
    ret = pGetLocaleInfoEx(L"fa-IR", LOCALE_ICALENDARTYPE | LOCALE_NOUSEROVERRIDE, buffer, sizeof(buffer) / sizeof(buffer[0]));
    ok(ret == 3 && !lstrcmpW(buffer, L"22"), "fa-IR calendar type returned %d %S, error %lu\n",
       ret, ret ? buffer : L"(none)", GetLastError());

    value = 0xdeadbeef;
    ret = pGetLocaleInfoEx(L"fa-IR", LOCALE_ICALENDARTYPE | LOCALE_NOUSEROVERRIDE | LOCALE_RETURN_NUMBER,
                           (WCHAR *)&value, sizeof(value) / sizeof(WCHAR));
    ok(ret == 2, "fa-IR calendar type number returned %d\n", ret);
    ok(value == CAL_PERSIAN, "fa-IR calendar type number %lu, expected %u\n", value, CAL_PERSIAN);

    value = 0xdeadbeef;
    ret = GetLocaleInfoW(MAKELCID(MAKELANGID(LANG_PERSIAN, SUBLANG_PERSIAN_IRAN), SORT_DEFAULT),
                         LOCALE_ICALENDARTYPE | LOCALE_NOUSEROVERRIDE | LOCALE_RETURN_NUMBER,
                         (WCHAR *)&value, sizeof(value) / sizeof(WCHAR));
    ok(ret == 2 && value == CAL_PERSIAN, "GetLocaleInfoW 0429 calendar type returned %d %lu\n", ret, value);

    value = 0xdeadbeef;
    ret = GetLocaleInfoA(MAKELCID(MAKELANGID(LANG_PERSIAN, SUBLANG_PERSIAN_IRAN), SORT_DEFAULT),
                         LOCALE_ICALENDARTYPE | LOCALE_NOUSEROVERRIDE | LOCALE_RETURN_NUMBER,
                         (char *)&value, sizeof(value));
    ok(ret == sizeof(value) && value == CAL_PERSIAN, "GetLocaleInfoA 0429 calendar type returned %d %lu\n", ret, value);
}

static void Test_InvalidLCType(void)
{
    static const LCTYPE types[] = { 0x0000, 0x005f, 0x01b0, 0x0e70, 0x1000 };
    WCHAR buffer[64];
    unsigned int i;
    int ret;

    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++)
    {
        SetLastError(0xdeadbeef);
        ret = GetLocaleInfoW(MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT), types[i],
                             buffer, sizeof(buffer) / sizeof(buffer[0]));
        ok(ret == 0, "GetLocaleInfoW type %04lx returned %d\n", types[i], ret);
        ok(GetLastError() == ERROR_INVALID_FLAGS, "GetLocaleInfoW type %04lx error %lu\n", types[i], GetLastError());

        SetLastError(0xdeadbeef);
        ret = pGetLocaleInfoEx(L"en-US", types[i], buffer, sizeof(buffer) / sizeof(buffer[0]));
        ok(ret == 0, "GetLocaleInfoEx type %04lx returned %d\n", types[i], ret);
        ok(GetLastError() == ERROR_INVALID_FLAGS, "GetLocaleInfoEx type %04lx error %lu\n", types[i], GetLastError());
    }
}

static void Test_DefaultLocaleNames(void)
{
    HMODULE hmodKernel32 = GetModuleHandleW(L"kernel32.dll");
    FN_LocaleNameFunc pGetUserDefaultLocaleName = (FN_LocaleNameFunc)GetProcAddress(hmodKernel32, "GetUserDefaultLocaleName");
    FN_LocaleNameFunc pGetSystemDefaultLocaleName = (FN_LocaleNameFunc)GetProcAddress(hmodKernel32, "GetSystemDefaultLocaleName");
    FN_LCIDToLocaleName pLCIDToLocaleName = (FN_LCIDToLocaleName)GetProcAddress(hmodKernel32, "LCIDToLocaleName");
    WCHAR name[86], expected[86];
    int ret, expected_ret;

    if (!pGetUserDefaultLocaleName || !pGetSystemDefaultLocaleName || !pLCIDToLocaleName)
    {
        skip("default locale name functions not found in kernel32.dll\n");
        return;
    }

    expected_ret = pLCIDToLocaleName(GetUserDefaultLCID(), expected, sizeof(expected) / sizeof(expected[0]), 0);
    ok(expected_ret > 1, "LCIDToLocaleName(user default) returned %d\n", expected_ret);
    memset(name, 0xcc, sizeof(name));
    SetLastError(0xdeadbeef);
    ret = pGetUserDefaultLocaleName(name, sizeof(name) / sizeof(name[0]));
    ok(ret == expected_ret, "GetUserDefaultLocaleName returned %d, expected %d\n", ret, expected_ret);
    ok(ret == lstrlenW(name) + 1, "GetUserDefaultLocaleName returned %d for %S\n", ret, name);
    ok(!lstrcmpW(name, expected), "GetUserDefaultLocaleName %S, expected %S\n", name, expected);
    ok(GetLastError() == 0xdeadbeef, "GetUserDefaultLocaleName changed the error to %lu\n", GetLastError());

    memset(name, 0xcc, sizeof(name));
    SetLastError(0xdeadbeef);
    ret = pGetUserDefaultLocaleName(name, 1);
    ok(ret == 0, "GetUserDefaultLocaleName with 1 character returned %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "GetUserDefaultLocaleName with 1 character error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = pGetUserDefaultLocaleName(NULL, sizeof(name) / sizeof(name[0]));
    ok(ret == 0, "GetUserDefaultLocaleName(NULL) returned %d\n", ret);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "GetUserDefaultLocaleName(NULL) error %lu\n", GetLastError());

    expected_ret = pLCIDToLocaleName(GetSystemDefaultLCID(), expected, sizeof(expected) / sizeof(expected[0]), 0);
    ok(expected_ret > 1, "LCIDToLocaleName(system default) returned %d\n", expected_ret);
    memset(name, 0xcc, sizeof(name));
    SetLastError(0xdeadbeef);
    ret = pGetSystemDefaultLocaleName(name, sizeof(name) / sizeof(name[0]));
    ok(ret == expected_ret, "GetSystemDefaultLocaleName returned %d, expected %d\n", ret, expected_ret);
    ok(ret == lstrlenW(name) + 1, "GetSystemDefaultLocaleName returned %d for %S\n", ret, name);
    ok(!lstrcmpW(name, expected), "GetSystemDefaultLocaleName %S, expected %S\n", name, expected);

    memset(name, 0xcc, sizeof(name));
    SetLastError(0xdeadbeef);
    ret = pGetSystemDefaultLocaleName(name, 1);
    ok(ret == 0, "GetSystemDefaultLocaleName with 1 character returned %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "GetSystemDefaultLocaleName with 1 character error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = pGetSystemDefaultLocaleName(NULL, sizeof(name) / sizeof(name[0]));
    ok(ret == 0, "GetSystemDefaultLocaleName(NULL) returned %d\n", ret);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "GetSystemDefaultLocaleName(NULL) error %lu\n", GetLastError());
}

static void Test_SetLocaleInfoVisible(void)
{
    WCHAR saved[16], value[16];
    char valueA[16];
    int ret;
    BOOL set;

    ret = GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SLIST, saved, sizeof(saved) / sizeof(saved[0]));
    ok(ret > 0, "GetLocaleInfoW(LOCALE_SLIST) returned %d, error %lu\n", ret, GetLastError());
    if (ret <= 0)
        return;

    set = SetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SLIST, "|");
    ok(set, "SetLocaleInfoA(LOCALE_SLIST) failed, error %lu\n", GetLastError());
    if (set)
    {
        ret = GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SLIST, value, sizeof(value) / sizeof(value[0]));
        ok(ret == 2 && !lstrcmpW(value, L"|"), "GetLocaleInfoW after SetLocaleInfoA returned %d %S\n",
           ret, ret ? value : L"(none)");
        ret = GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SLIST, valueA, sizeof(valueA));
        ok(ret == 2 && !lstrcmpA(valueA, "|"), "GetLocaleInfoA after SetLocaleInfoA returned %d %s\n",
           ret, ret ? valueA : "(none)");
        ret = pGetLocaleInfoEx(NULL, LOCALE_SLIST, value, sizeof(value) / sizeof(value[0]));
        ok(ret == 2 && !lstrcmpW(value, L"|"), "GetLocaleInfoEx after SetLocaleInfoA returned %d %S\n",
           ret, ret ? value : L"(none)");
    }

    set = SetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SLIST, saved);
    ok(set, "SetLocaleInfoW(LOCALE_SLIST) restore failed, error %lu\n", GetLastError());
    ret = GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SLIST, value, sizeof(value) / sizeof(value[0]));
    ok(ret == lstrlenW(saved) + 1 && !lstrcmpW(value, saved), "GetLocaleInfoW after restore returned %d %S\n",
       ret, ret ? value : L"(none)");
}

#undef GetLocaleInfo
START_TEST(GetLocaleInfo)
{
    Test_GetLocaleInfoW_ErrorClassification();
    Test_GetLocaleInfoEx();
    if (!pGetLocaleInfoEx)
        return;
    Test_ConsoleFallbackName();
    Test_PersianCalendarType();
    Test_InvalidLCType();
    Test_DefaultLocaleNames();
    Test_SetLocaleInfoVisible();
}
