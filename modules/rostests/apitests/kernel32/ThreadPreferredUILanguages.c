/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests thread preferred user interface languages and the thread UI language
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winuser.h>

#define IDS_THREAD_UI_LANGUAGE 4096

typedef BOOL (WINAPI *PFN_SETTHREADPREFERREDUILANGUAGES)(DWORD, PCZZWSTR, PULONG);
typedef BOOL (WINAPI *PFN_GETTHREADPREFERREDUILANGUAGES)(DWORD, PULONG, PZZWSTR, PULONG);
typedef BOOL (WINAPI *PFN_SETPROCESSPREFERREDUILANGUAGES)(DWORD, PCZZWSTR, PULONG);
typedef BOOL (WINAPI *PFN_GETUSERPREFERREDUILANGUAGES)(DWORD, PULONG, PZZWSTR, PULONG);
typedef LANGID (WINAPI *PFN_GETTHREADUILANGUAGE)(VOID);
typedef LANGID (WINAPI *PFN_SETTHREADUILANGUAGE)(LANGID);
typedef NTSTATUS (NTAPI *PFN_RTLSETTHREADPREFERREDUILANGUAGES)(DWORD, PCZZWSTR, PULONG);
typedef NTSTATUS (NTAPI *PFN_RTLGETTHREADPREFERREDUILANGUAGES)(DWORD, PULONG, PZZWSTR, PULONG);

static PFN_SETTHREADPREFERREDUILANGUAGES pSetThreadPreferredUILanguages;
static PFN_GETTHREADPREFERREDUILANGUAGES pGetThreadPreferredUILanguages;
static PFN_SETPROCESSPREFERREDUILANGUAGES pSetProcessPreferredUILanguages;
static PFN_GETUSERPREFERREDUILANGUAGES pGetUserPreferredUILanguages;
static PFN_GETTHREADUILANGUAGE pGetThreadUILanguage;
static PFN_SETTHREADUILANGUAGE pSetThreadUILanguage;
static PFN_RTLSETTHREADPREFERREDUILANGUAGES pRtlSetThreadPreferredUILanguages;
static PFN_RTLGETTHREADPREFERREDUILANGUAGES pRtlGetThreadPreferredUILanguages;

static const WCHAR EmptyList[] = L"\0";

static ULONG
MultiStringSize(PCZZWSTR List)
{
    PCWSTR Current = List;

    while (*Current)
        Current += wcslen(Current) + 1;
    return (ULONG)(Current - List) + 1;
}

static BOOL
MultiStringContains(PCZZWSTR List, PCWSTR Language)
{
    PCWSTR Current;

    for (Current = List; *Current; Current += wcslen(Current) + 1)
    {
        if (!_wcsicmp(Current, Language))
            return TRUE;
    }
    return FALSE;
}

static VOID
AppendUnique(PWSTR List, ULONG Capacity, PCWSTR Language, SIZE_T Length)
{
    WCHAR Name[LOCALE_NAME_MAX_LENGTH];
    ULONG Used;

    if (!Length || Length >= _countof(Name))
        return;
    memcpy(Name, Language, Length * sizeof(WCHAR));
    Name[Length] = UNICODE_NULL;
    if (MultiStringContains(List, Name))
        return;

    Used = MultiStringSize(List) - 1;
    if (Used + Length + 2 > Capacity)
        return;
    memcpy(List + Used, Name, (Length + 1) * sizeof(WCHAR));
    List[Used + Length + 1] = UNICODE_NULL;
}

static VOID
AppendList(PWSTR List, ULONG Capacity, PCZZWSTR Source, BOOL AddNeutral)
{
    PCWSTR Current;

    for (Current = Source; *Current; Current += wcslen(Current) + 1)
    {
        PCWSTR Dash = wcschr(Current, L'-');

        AppendUnique(List, Capacity, Current, wcslen(Current));
        if (AddNeutral && Dash)
            AppendUnique(List, Capacity, Current, Dash - Current);
    }
}

static ULONG
MultiStringCount(PCZZWSTR List)
{
    PCWSTR Current;
    ULONG Count = 0;

    for (Current = List; *Current; Current += wcslen(Current) + 1)
        Count++;
    return Count;
}

static VOID
CheckLanguages(PCSTR Tag, DWORD Flags, PCZZWSTR Expected)
{
    WCHAR Buffer[512];
    ULONG ExpectedCount = MultiStringCount(Expected);
    ULONG ExpectedSize = ExpectedCount ? MultiStringSize(Expected) : 2;
    ULONG Count, Size;
    DWORD Error;
    BOOL Ret;

    Count = 0xcccccccc;
    Size = 0;
    SetLastError(0xdeadbeef);
    Ret = pGetThreadPreferredUILanguages(Flags, &Count, NULL, &Size);
    Error = GetLastError();
    ok(Ret, "%s: size query failed, error %lu\n", Tag, Error);
    ok(Error == 0xdeadbeef, "%s: size query error %lu\n", Tag, Error);
    ok(Size == ExpectedSize, "%s: size %lu, expected %lu\n", Tag, Size, ExpectedSize);
    if (ExpectedCount)
        ok(Count == ExpectedCount, "%s: size query count %lu, expected %lu\n", Tag, Count, ExpectedCount);
    else
        trace("%s: empty list size query count %lx\n", Tag, Count);

    memset(Buffer, 0xcc, sizeof(Buffer));
    Count = 0xcccccccc;
    Size = _countof(Buffer);
    SetLastError(0xdeadbeef);
    Ret = pGetThreadPreferredUILanguages(Flags, &Count, Buffer, &Size);
    Error = GetLastError();
    ok(Ret, "%s: query failed, error %lu\n", Tag, Error);
    ok(Error == 0xdeadbeef, "%s: query error %lu\n", Tag, Error);
    ok(Size == ExpectedSize, "%s: size %lu, expected %lu\n", Tag, Size, ExpectedSize);
    if (ExpectedCount)
        ok(Count == ExpectedCount, "%s: count %lu, expected %lu\n", Tag, Count, ExpectedCount);
    ok(Ret && Size == ExpectedSize && !memcmp(Buffer, Expected, ExpectedSize * sizeof(WCHAR)),
       "%s: unexpected list starting with %s\n", Tag, wine_dbgstr_w(Buffer));
}

static VOID
CheckFirstThreadLanguage(PCSTR Tag, PCWSTR Expected)
{
    WCHAR Buffer[128];
    ULONG Count = 0, Size = _countof(Buffer);
    BOOL Ret;

    Buffer[0] = UNICODE_NULL;
    Ret = pGetThreadPreferredUILanguages(MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, &Count, Buffer, &Size);
    ok(Ret, "%s: query failed, error %lu\n", Tag, GetLastError());
    ok(Ret && !wcscmp(Buffer, Expected), "%s: first thread language %s, expected %s\n", Tag,
       wine_dbgstr_w(Buffer), wine_dbgstr_w(Expected));
}

static BOOL
SetThreadLanguages(PCSTR Tag, DWORD Flags, PCZZWSTR Languages, ULONG ExpectedCount)
{
    ULONG Count = 0xcccccccc;
    DWORD Error;
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = pSetThreadPreferredUILanguages(Flags, Languages, &Count);
    Error = GetLastError();
    ok(Ret, "%s: set failed, error %lu\n", Tag, Error);
    ok(Error == 0xdeadbeef, "%s: set error %lu\n", Tag, Error);
    ok(Count == ExpectedCount, "%s: set %lu languages, expected %lu\n", Tag, Count, ExpectedCount);
    return Ret;
}

static VOID
CheckSetFails(PCSTR Tag, DWORD Flags, PCZZWSTR Languages, BOOL PassCount)
{
    ULONG Count = 0xcccccccc;
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = pSetThreadPreferredUILanguages(Flags, Languages, PassCount ? &Count : NULL);
    ok(!Ret, "%s: set succeeded\n", Tag);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "%s: set error %lu\n", Tag, GetLastError());
    ok(Count == 0xcccccccc, "%s: count changed to %lu\n", Tag, Count);
}

static VOID
CheckGetFails(PCSTR Tag, DWORD Flags)
{
    WCHAR Buffer[64];
    ULONG Count = 0, Size = _countof(Buffer);
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = pGetThreadPreferredUILanguages(Flags, &Count, Buffer, &Size);
    ok(!Ret, "%s: query succeeded\n", Tag);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "%s: query error %lu\n", Tag, GetLastError());
}

static VOID
TestSetAndGet(VOID)
{
    static const WCHAR Names[] = L"fr-FR\0en-US\0fr-FR\0invalid-language\0de-DE\0es-ES\0it-IT\0pt-PT\0";
    static const WCHAR NamesExpected[] = L"fr-FR\0en-US\0de-DE\0es-ES\0";
    static const WCHAR IdsExpected[] = L"040C\0" L"0409\0" L"0407\0" L"0C0A\0";
    static const WCHAR Ids[] = L"040C\0" L"0409\0" L"0400\0" L"0800\0" L"1400\0" L"0407\0";
    static const WCHAR IdsNamesExpected[] = L"fr-FR\0en-US\0de-DE\0";
    static const WCHAR Six[] = L"fr-FR\0de-DE\0es-ES\0it-IT\0pt-PT\0ja-JP\0";
    static const WCHAR SixExpected[] = L"fr-FR\0de-DE\0es-ES\0it-IT\0pt-PT\0";
    ULONG Count;
    BOOL Ret;

    CheckLanguages("initial", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, EmptyList);
    CheckLanguages("initial id", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_ID, EmptyList);

    if (!SetThreadLanguages("names", MUI_LANGUAGE_NAME, Names, 4))
        return;
    CheckLanguages("names", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, NamesExpected);
    CheckLanguages("names default", MUI_THREAD_LANGUAGES, NamesExpected);
    CheckLanguages("names id", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_ID, IdsExpected);

    SetThreadLanguages("ids", MUI_LANGUAGE_ID, Ids, 3);
    CheckLanguages("ids", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, IdsNamesExpected);

    SetThreadLanguages("six", MUI_LANGUAGE_NAME, Six, 5);
    CheckLanguages("six", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, SixExpected);

    CheckSetFails("both formats", MUI_LANGUAGE_ID | MUI_LANGUAGE_NAME, Names, TRUE);
    CheckSetFails("filter with list", MUI_CONSOLE_FILTER, Names, FALSE);
    CheckSetFails("filter with count", MUI_CONSOLE_FILTER, NULL, TRUE);
    CheckSetFails("two filters", MUI_CONSOLE_FILTER | MUI_COMPLEX_SCRIPT_FILTER, NULL, FALSE);
    CheckLanguages("after failures", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, SixExpected);

    SetLastError(0xdeadbeef);
    Ret = pSetThreadPreferredUILanguages(MUI_CONSOLE_FILTER, NULL, NULL);
    ok(Ret, "console filter failed, error %lu\n", GetLastError());
    CheckFirstThreadLanguage("console filter", L"fr-FR");
    SetLastError(0xdeadbeef);
    Ret = pSetThreadPreferredUILanguages(MUI_RESET_FILTERS, NULL, NULL);
    ok(Ret, "reset filters failed, error %lu\n", GetLastError());
    CheckLanguages("after filters", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, SixExpected);

    Count = 0xcccccccc;
    SetLastError(0xdeadbeef);
    Ret = pSetThreadPreferredUILanguages(0, NULL, &Count);
    ok(Ret, "clear failed, error %lu\n", GetLastError());
    ok(GetLastError() == 0xdeadbeef, "clear error %lu\n", GetLastError());
    ok(Count == 0xcccccccc, "clear changed the count to %lu\n", Count);
    CheckLanguages("cleared", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, EmptyList);

    SetThreadLanguages("names again", MUI_LANGUAGE_NAME, Names, 4);
    Count = 0xcccccccc;
    Ret = pSetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, EmptyList, &Count);
    ok(Ret, "empty list failed, error %lu\n", GetLastError());
    trace("empty list count %lx\n", Count);
    CheckLanguages("empty list", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, EmptyList);
}

static VOID
TestMerge(VOID)
{
    static const WCHAR Thread[] = L"fr-FR\0de-DE\0";
    static const WCHAR Process[] = L"es-ES\0de-DE\0it-IT\0";
    static const WCHAR MergedIdsStart[] = L"040C\0" L"0407\0" L"0C0A\0" L"0410\0";
    WCHAR User[256], System[LOCALE_NAME_MAX_LENGTH + 1], Expected[512], Buffer[512];
    ULONG Count, Size;
    NTSTATUS Status;
    BOOL Ret;

    Size = _countof(User);
    Ret = pGetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &Count, User, &Size);
    ok(Ret, "user languages failed, error %lu\n", GetLastError());
    if (!Ret)
        return;
    memset(System, 0, sizeof(System));
    ok(LCIDToLocaleName(MAKELCID(GetSystemDefaultUILanguage(), SORT_DEFAULT), System, LOCALE_NAME_MAX_LENGTH, 0) != 0,
       "system default UI language name failed, error %lu\n", GetLastError());

    if (!SetThreadLanguages("merge", MUI_LANGUAGE_NAME, Thread, 2))
        return;

    memset(Expected, 0, sizeof(Expected));
    AppendList(Expected, _countof(Expected), Thread, FALSE);
    AppendList(Expected, _countof(Expected), User, FALSE);
    AppendList(Expected, _countof(Expected), System, FALSE);
    CheckLanguages("user fallback", MUI_MERGE_USER_FALLBACK | MUI_LANGUAGE_NAME, Expected);
    CheckLanguages("default merge", MUI_LANGUAGE_NAME, Expected);

    memset(Expected, 0, sizeof(Expected));
    AppendList(Expected, _countof(Expected), Thread, TRUE);
    AppendList(Expected, _countof(Expected), User, TRUE);
    AppendList(Expected, _countof(Expected), System, TRUE);
    CheckLanguages("ui fallback", MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK | MUI_LANGUAGE_NAME, Expected);

    Ret = pSetProcessPreferredUILanguages(MUI_LANGUAGE_NAME, Process, &Count);
    ok(Ret, "process languages failed, error %lu\n", GetLastError());
    if (Ret)
    {
        memset(Expected, 0, sizeof(Expected));
        AppendList(Expected, _countof(Expected), Thread, FALSE);
        AppendList(Expected, _countof(Expected), Process, FALSE);
        AppendList(Expected, _countof(Expected), User, FALSE);
        AppendList(Expected, _countof(Expected), System, FALSE);
        CheckLanguages("process merge", MUI_LANGUAGE_NAME, Expected);

        memset(Expected, 0, sizeof(Expected));
        AppendList(Expected, _countof(Expected), Thread, TRUE);
        AppendList(Expected, _countof(Expected), Process, TRUE);
        AppendList(Expected, _countof(Expected), User, TRUE);
        AppendList(Expected, _countof(Expected), System, TRUE);
        CheckLanguages("process ui fallback", MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK | MUI_LANGUAGE_NAME,
                       Expected);

        Size = _countof(Buffer);
        Ret = pGetThreadPreferredUILanguages(MUI_LANGUAGE_ID, &Count, Buffer, &Size);
        ok(Ret, "merged ids failed, error %lu\n", GetLastError());
        ok(Ret && !memcmp(Buffer, MergedIdsStart, (_countof(MergedIdsStart) - 1) * sizeof(WCHAR)),
           "merged ids start with %s\n", wine_dbgstr_w(Buffer));

        pSetProcessPreferredUILanguages(0, NULL, NULL);
    }

    CheckGetFails("thread and user fallback", MUI_THREAD_LANGUAGES | MUI_MERGE_USER_FALLBACK);
    CheckGetFails("thread and system fallback", MUI_THREAD_LANGUAGES | MUI_MERGE_SYSTEM_FALLBACK);
    CheckGetFails("system fallback alone", MUI_MERGE_SYSTEM_FALLBACK | MUI_LANGUAGE_NAME);
    CheckGetFails("both formats", MUI_LANGUAGE_ID | MUI_LANGUAGE_NAME);

    Count = 0;
    Size = 1;
    SetLastError(0xdeadbeef);
    Ret = pGetThreadPreferredUILanguages(MUI_THREAD_LANGUAGES, &Count, NULL, &Size);
    ok(!Ret, "size without buffer succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "size without buffer error %lu\n", GetLastError());

    Count = 0;
    Size = 0;
    SetLastError(0xdeadbeef);
    Ret = pGetThreadPreferredUILanguages(MUI_THREAD_LANGUAGES, &Count, Buffer, &Size);
    ok(!Ret, "buffer with size 0 succeeded\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "buffer with size 0 error %lu\n", GetLastError());
    ok(Size == _countof(Thread), "buffer with size 0 required %lu\n", Size);

    if (pRtlGetThreadPreferredUILanguages && pRtlSetThreadPreferredUILanguages)
    {
        Count = 0;
        Size = _countof(Buffer);
        Status = pRtlGetThreadPreferredUILanguages(MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, &Count, Buffer, &Size);
        ok(Status == STATUS_SUCCESS, "Rtl query status %lx\n", Status);
        ok(Count == 2 && Size == _countof(Thread) && !memcmp(Buffer, Thread, sizeof(Thread)),
           "Rtl query count %lu size %lu list %s\n", Count, Size, wine_dbgstr_w(Buffer));
        Status = pRtlSetThreadPreferredUILanguages(MUI_LANGUAGE_ID | MUI_LANGUAGE_NAME, Thread, &Count);
        ok(Status == STATUS_INVALID_PARAMETER, "Rtl set both formats status %lx\n", Status);
    }
    else
    {
        skip("ntdll thread preferred UI language functions are not available\n");
    }

    pSetThreadPreferredUILanguages(0, NULL, NULL);
}

static VOID
TestIsolationChild(VOID)
{
    CheckLanguages("other thread", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, EmptyList);
}

static VOID RunInThread(ULONG Index);

static VOID
TestIsolation(VOID)
{
    static const WCHAR German[] = L"de-DE\0";

    if (!SetThreadLanguages("isolation", MUI_LANGUAGE_NAME, German, 1))
        return;
    RunInThread(3);
    CheckLanguages("own thread", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, German);
}

static LANGID
FirstMergedLanguage(VOID)
{
    WCHAR Buffer[512];
    ULONG Count = 0, Size = _countof(Buffer);

    Buffer[0] = UNICODE_NULL;
    if (!pGetThreadPreferredUILanguages(MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK | MUI_LANGUAGE_ID,
                                        &Count, Buffer, &Size))
    {
        return 0;
    }
    return (LANGID)wcstoul(Buffer, NULL, 16);
}

static VOID
TestThreadUILanguage(VOID)
{
    static const WCHAR GermanFrench[] = L"de-DE\0fr-FR\0";
    LCID Locale = GetThreadLocale();
    LANGID LanguageId, Expected;

    Expected = FirstMergedLanguage();
    LanguageId = pGetThreadUILanguage();
    ok(Expected != 0, "no merged language\n");
    ok(LanguageId == Expected, "initial thread UI language %04x, expected %04x\n", LanguageId, Expected);

    if (!SetThreadLanguages("thread ui", MUI_LANGUAGE_NAME, GermanFrench, 2))
        return;
    LanguageId = pGetThreadUILanguage();
    ok(LanguageId == MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), "thread UI language %04x\n", LanguageId);

    SetLastError(0xdeadbeef);
    LanguageId = pSetThreadUILanguage(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH));
    ok(LanguageId == MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), "SetThreadUILanguage returned %04x, error %lu\n",
       LanguageId, GetLastError());
    LanguageId = pGetThreadUILanguage();
    ok(LanguageId == MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), "thread UI language after set %04x\n", LanguageId);
    CheckFirstThreadLanguage("set thread ui", L"fr-FR");
    ok(GetThreadLocale() == Locale, "thread locale changed from %lx to %lx\n", Locale, GetThreadLocale());

    SetLastError(0xdeadbeef);
    LanguageId = pSetThreadUILanguage(LANGIDFROMLCID(LOCALE_USER_DEFAULT));
    ok(LanguageId != LANGIDFROMLCID(LOCALE_USER_DEFAULT), "user default identifier accepted\n");
    trace("user default identifier: returned %04x, error %lu\n", LanguageId, GetLastError());
    CheckFirstThreadLanguage("after user default", L"fr-FR");

    LanguageId = pSetThreadUILanguage(0);
    ok(LanguageId != 0, "console language is 0\n");
    trace("console language %04x\n", LanguageId);
    CheckFirstThreadLanguage("after console language", L"fr-FR");
    pSetThreadPreferredUILanguages(MUI_RESET_FILTERS, NULL, NULL);

    pSetThreadPreferredUILanguages(0, NULL, NULL);
    LanguageId = pGetThreadUILanguage();
    ok(LanguageId == Expected, "thread UI language after clear %04x, expected %04x\n", LanguageId, Expected);
}

static VOID
CheckString(PCSTR Tag, PCWSTR Expected)
{
    WCHAR Buffer[64];
    int Length;

    Buffer[0] = UNICODE_NULL;
    SetLastError(0xdeadbeef);
    Length = LoadStringW(GetModuleHandleW(NULL), IDS_THREAD_UI_LANGUAGE, Buffer, _countof(Buffer));
    ok(Length == (int)wcslen(Expected), "%s: LoadStringW returned %d, error %lu\n", Tag, Length, GetLastError());
    ok(!wcscmp(Buffer, Expected), "%s: string %s, expected %s\n", Tag, wine_dbgstr_w(Buffer), wine_dbgstr_w(Expected));
}

static VOID
TestResources(VOID)
{
    static const WCHAR German[] = L"de-DE\0";
    static const WCHAR CanadianFrench[] = L"fr-CA\0";
    static const WCHAR English[] = L"en-US\0";
    WCHAR Buffer[64];

    LoadStringW(GetModuleHandleW(NULL), IDS_THREAD_UI_LANGUAGE, Buffer, _countof(Buffer));
    trace("default thread UI string %s\n", wine_dbgstr_w(Buffer));

    if (!SetThreadLanguages("resource de-DE", MUI_LANGUAGE_NAME, German, 1))
        return;
    CheckString("de-DE", L"thread ui de-DE");

    SetThreadLanguages("resource fr-CA", MUI_LANGUAGE_NAME, CanadianFrench, 1);
    CheckString("fr-CA", L"thread ui fr");

    SetThreadLanguages("resource en-US", MUI_LANGUAGE_NAME, English, 1);
    CheckString("en-US", L"thread ui en-US");

    ok(pSetThreadUILanguage(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)) == MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN),
       "SetThreadUILanguage(de-DE) failed, error %lu\n", GetLastError());
    CheckString("SetThreadUILanguage de-DE", L"thread ui de-DE");

    pSetThreadPreferredUILanguages(0, NULL, NULL);
}

static VOID
SetFilter(PCSTR Tag, DWORD Filter)
{
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = pSetThreadPreferredUILanguages(Filter, NULL, NULL);
    ok(Ret, "%s: filter 0x%lx failed, error %lu\n", Tag, Filter, GetLastError());
    ok(GetLastError() == 0xdeadbeef, "%s: filter error %lu\n", Tag, GetLastError());
}

static VOID
TestFilters(VOID)
{
    static const WCHAR Complex[] = L"ar-SA\0fr-FR\0hi-IN\0";
    static const WCHAR ComplexFiltered[] = L"fr-FR\0en\0";
    static const WCHAR ComplexFilteredIds[] = L"040C\0" L"0009\0";
    static const WCHAR Mixed[] = L"ru-RU\0fr-FR\0he-IL\0";
    static const WCHAR MixedComplexFiltered[] = L"ru-RU\0fr-FR\0en\0";
    static const WCHAR MixedConsoleFiltered[] = L"fr-FR\0en\0";
    static const WCHAR English[] = L"en-GB\0ja-JP\0";
    static const WCHAR EnglishConsoleFiltered[] = L"en-GB\0";
    static const WCHAR Maghreb[] = L"ar-MA\0th-TH\0";
    static const WCHAR MaghrebFiltered[] = L"fr\0en\0";
    static const WCHAR Arabic[] = L"ar-SA\0";
    static const WCHAR NewList[] = L"th-TH\0de-DE\0";
    static const WCHAR NewListFiltered[] = L"de-DE\0en\0";
    WCHAR User[256], Expected[512], Language[LOCALE_NAME_MAX_LENGTH];
    ULONG Count, Size;
    LANGID LanguageId;
    BOOL Ret;

    if (!SetThreadLanguages("complex", MUI_LANGUAGE_NAME, Complex, 3))
        return;
    SetFilter("complex", MUI_COMPLEX_SCRIPT_FILTER);
    CheckLanguages("complex filter", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, ComplexFiltered);
    CheckLanguages("complex filter id", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_ID, ComplexFilteredIds);
    SetFilter("complex reset", MUI_RESET_FILTERS);
    CheckLanguages("complex reset", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, Complex);

    SetThreadLanguages("maghreb", MUI_LANGUAGE_NAME, Maghreb, 2);
    SetFilter("maghreb", MUI_COMPLEX_SCRIPT_FILTER);
    CheckLanguages("maghreb filter", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, MaghrebFiltered);

    SetThreadLanguages("new list", MUI_LANGUAGE_NAME, NewList, 2);
    CheckLanguages("new list keeps filter", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, NewListFiltered);

    SetThreadLanguages("mixed", MUI_LANGUAGE_NAME, Mixed, 3);
    CheckLanguages("mixed complex filter", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, MixedComplexFiltered);
    SetFilter("mixed console", MUI_CONSOLE_FILTER);
    if (GetACP() == 1252)
    {
        CheckLanguages("mixed console filter", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, MixedConsoleFiltered);
        SetThreadLanguages("english", MUI_LANGUAGE_NAME, English, 2);
        CheckLanguages("english console filter", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, EnglishConsoleFiltered);
    }
    else
    {
        skip("console code page checks need code page 1252, have %u\n", GetACP());
    }
    SetFilter("mixed reset", MUI_RESET_FILTERS);
    CheckLanguages("mixed reset", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, (GetACP() == 1252) ? English : Mixed);

    Size = _countof(User);
    Ret = pGetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &Count, User, &Size);
    ok(Ret, "user languages failed, error %lu\n", GetLastError());
    memset(Language, 0, sizeof(Language));
    LCIDToLocaleName(MAKELCID(GetSystemDefaultUILanguage(), SORT_DEFAULT), Language, LOCALE_NAME_MAX_LENGTH, 0);
    if (Ret && GetACP() == 1252 && !wcscmp(User, L"en-US") && !User[6] && !wcscmp(Language, L"en-US"))
    {
        SetThreadLanguages("arabic", MUI_LANGUAGE_NAME, Arabic, 1);
        SetFilter("arabic console", MUI_CONSOLE_FILTER);
        CheckLanguages("arabic console merge", MUI_MERGE_USER_FALLBACK | MUI_LANGUAGE_NAME, L"en-US\0en\0");
        CheckLanguages("arabic console ui fallback", MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK | MUI_LANGUAGE_NAME,
                       L"en-US\0en\0");
        LanguageId = pGetThreadUILanguage();
        ok(LanguageId == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), "filtered thread UI language %04x\n", LanguageId);
        SetFilter("arabic reset", MUI_RESET_FILTERS);

        memset(Expected, 0, sizeof(Expected));
        AppendList(Expected, _countof(Expected), Arabic, FALSE);
        AppendList(Expected, _countof(Expected), User, FALSE);
        CheckLanguages("arabic unfiltered merge", MUI_MERGE_USER_FALLBACK | MUI_LANGUAGE_NAME, Expected);
        LanguageId = pGetThreadUILanguage();
        ok(LanguageId == MAKELANGID(LANG_ARABIC, SUBLANG_ARABIC_SAUDI_ARABIA), "unfiltered thread UI language %04x\n", LanguageId);

        LanguageId = pSetThreadUILanguage(0);
        ok(LanguageId == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), "console language %04x\n", LanguageId);
        CheckFirstThreadLanguage("console language filters the list", L"en");
        SetFilter("console language reset", MUI_RESET_FILTERS);
        CheckFirstThreadLanguage("console language keeps the list", L"ar-SA");
    }
    else
    {
        skip("merged filter checks need an en-US user and system language with code page 1252\n");
    }

    pSetThreadPreferredUILanguages(0, NULL, NULL);
    CheckLanguages("cleared", MUI_THREAD_LANGUAGES | MUI_LANGUAGE_NAME, EmptyList);
}

typedef VOID (*PTHREAD_TEST)(VOID);

static const PTHREAD_TEST ThreadTests[] =
{
    TestSetAndGet,
    TestMerge,
    TestIsolation,
    TestIsolationChild,
    TestThreadUILanguage,
    TestResources,
    TestFilters,
};

static DWORD WINAPI
ThreadTestProc(PVOID Param)
{
    ThreadTests[(ULONG_PTR)Param]();
    return 0;
}

static VOID
RunInThread(ULONG Index)
{
    HANDLE Thread;

    Thread = CreateThread(NULL, 0, ThreadTestProc, (PVOID)(ULONG_PTR)Index, 0, NULL);
    ok(Thread != NULL, "CreateThread failed, error %lu\n", GetLastError());
    if (!Thread)
        return;
    ok(WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0, "test thread %lu did not finish\n", Index);
    CloseHandle(Thread);
}

START_TEST(ThreadPreferredUILanguages)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");

    pSetThreadPreferredUILanguages = (PFN_SETTHREADPREFERREDUILANGUAGES)GetProcAddress(Kernel32, "SetThreadPreferredUILanguages");
    pGetThreadPreferredUILanguages = (PFN_GETTHREADPREFERREDUILANGUAGES)GetProcAddress(Kernel32, "GetThreadPreferredUILanguages");
    pSetProcessPreferredUILanguages = (PFN_SETPROCESSPREFERREDUILANGUAGES)GetProcAddress(Kernel32, "SetProcessPreferredUILanguages");
    pGetUserPreferredUILanguages = (PFN_GETUSERPREFERREDUILANGUAGES)GetProcAddress(Kernel32, "GetUserPreferredUILanguages");
    pGetThreadUILanguage = (PFN_GETTHREADUILANGUAGE)GetProcAddress(Kernel32, "GetThreadUILanguage");
    pSetThreadUILanguage = (PFN_SETTHREADUILANGUAGE)GetProcAddress(Kernel32, "SetThreadUILanguage");
    pRtlSetThreadPreferredUILanguages = (PFN_RTLSETTHREADPREFERREDUILANGUAGES)GetProcAddress(Ntdll, "RtlSetThreadPreferredUILanguages");
    pRtlGetThreadPreferredUILanguages = (PFN_RTLGETTHREADPREFERREDUILANGUAGES)GetProcAddress(Ntdll, "RtlGetThreadPreferredUILanguages");

    if (!pSetThreadPreferredUILanguages || !pGetThreadPreferredUILanguages || !pSetProcessPreferredUILanguages ||
        !pGetUserPreferredUILanguages || !pGetThreadUILanguage || !pSetThreadUILanguage)
    {
        skip("thread preferred UI language functions are not available\n");
        return;
    }

    RunInThread(0);
    RunInThread(1);
    RunInThread(2);
    RunInThread(4);
    RunInThread(5);
    RunInThread(6);
}
