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

#undef GetLocaleInfo
START_TEST(GetLocaleInfo)
{
    Test_GetLocaleInfoW_ErrorClassification();
    Test_GetLocaleInfoEx();
}
