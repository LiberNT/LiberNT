/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the thread UI language, not the thread locale, selects language-neutral resources
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winuser.h>

#define IDS_RESOURCE_UI_LANGUAGE 4700

static void
CheckString(PCWSTR Expected, PCSTR Tag)
{
    WCHAR Buffer[16];
    int Length;

    Buffer[0] = UNICODE_NULL;
    Length = LoadStringW(GetModuleHandleW(NULL), IDS_RESOURCE_UI_LANGUAGE, Buffer, _countof(Buffer));
    ok(Length == 2 && !wcscmp(Buffer, Expected), "%s: LoadStringW gave %d %s, expected %s\n", Tag, Length,
       wine_dbgstr_w(Buffer), wine_dbgstr_w(Expected));
}

static void
CheckFind(LANGID Expected, PCSTR Tag)
{
    HMODULE Module = GetModuleHandleW(NULL);
    LPCWSTR Name = MAKEINTRESOURCEW(IDS_RESOURCE_UI_LANGUAGE / 16 + 1);
    HRSRC Neutral = FindResourceExW(Module, (LPCWSTR)RT_STRING, Name, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
    HRSRC Explicit = FindResourceExW(Module, (LPCWSTR)RT_STRING, Name, Expected);

    ok(Neutral != NULL && Neutral == Explicit, "%s: FindResourceExW(LANG_NEUTRAL) gave %p, the %04x resource is %p\n", Tag,
       Neutral, Expected, Explicit);
}

START_TEST(ResourceUILanguage)
{
    LANGID UiLanguage = GetUserDefaultUILanguage();
    LANGID ThreadUiLanguage = GetThreadUILanguage();
    LANGID Other, Result;
    LCID ThreadLocale = GetThreadLocale();
    LCID OtherLocale;
    PCWSTR Base, OtherString;
    LANGID BaseId;

    if (PRIMARYLANGID(UiLanguage) == LANG_JAPANESE)
    {
        Base = L"ja";
        BaseId = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
        Other = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
        OtherString = L"en";
    }
    else if (PRIMARYLANGID(UiLanguage) == LANG_ENGLISH)
    {
        Base = L"en";
        BaseId = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
        Other = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
        OtherString = L"ja";
    }
    else
    {
        skip("UI language %04x is neither English nor Japanese\n", UiLanguage);
        return;
    }
    trace("UI language %04x, thread UI language %04x, thread locale %08lx\n", UiLanguage, ThreadUiLanguage, ThreadLocale);
    ok(ThreadUiLanguage == UiLanguage, "thread UI language %04x, user UI language %04x\n", ThreadUiLanguage, UiLanguage);

    CheckString(Base, "default");
    CheckFind(BaseId, "default");

    OtherLocale = MAKELCID(Other, SORT_DEFAULT);
    ok(SetThreadLocale(OtherLocale), "SetThreadLocale failed, error %lu\n", GetLastError());
    ok(GetThreadLocale() == OtherLocale, "thread locale %08lx\n", GetThreadLocale());
    ok(GetThreadUILanguage() == ThreadUiLanguage, "SetThreadLocale changed the thread UI language to %04x\n", GetThreadUILanguage());
    CheckString(Base, "thread locale");
    CheckFind(BaseId, "thread locale");
    ok(SetThreadLocale(ThreadLocale), "restoring the thread locale failed, error %lu\n", GetLastError());

    Result = SetThreadUILanguage(Other);
    ok(Result == Other, "SetThreadUILanguage(%04x) returned %04x\n", Other, Result);
    ok(GetThreadUILanguage() == Other, "thread UI language %04x after SetThreadUILanguage(%04x)\n", GetThreadUILanguage(), Other);
    ok(GetThreadLocale() == ThreadLocale, "SetThreadUILanguage changed the thread locale to %08lx\n", GetThreadLocale());
    ok(GetUserDefaultUILanguage() == UiLanguage, "SetThreadUILanguage changed the user UI language to %04x\n", GetUserDefaultUILanguage());
    CheckString(OtherString, "thread UI language");
    CheckFind(Other, "thread UI language");

    Result = SetThreadUILanguage(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN));
    ok(Result == MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), "SetThreadUILanguage(0407) returned %04x\n", Result);
    CheckString(L"de", "thread UI language 0407");

    Result = SetThreadUILanguage(ThreadUiLanguage);
    ok(Result == ThreadUiLanguage, "restoring the thread UI language returned %04x\n", Result);
    ok(GetThreadUILanguage() == ThreadUiLanguage, "thread UI language %04x after restoring\n", GetThreadUILanguage());
    CheckString(Base, "restored");
}
