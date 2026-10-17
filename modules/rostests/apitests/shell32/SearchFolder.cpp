/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests structured query conditions and the search folder
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shellutils.h>
#include <shobjidl.h>
#include <structuredquery.h>
#include <propkey.h>
#include <propvarutil.h>
#include <stdio.h>

static WCHAR g_Dir[MAX_PATH];

static BOOL
CreateTextFile(LPCWSTR Path, const char *Text)
{
    HANDLE File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD Written;
    BOOL Ok;

    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Ok = WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL);
    CloseHandle(File);
    return Ok;
}

static void
TestConditions(void)
{
    CComPtr<IConditionFactory2> Factory2;
    CComPtr<IConditionFactory> Factory;
    CComPtr<ICondition> Leaf, Leaf2, And, Not;
    CONDITION_TYPE Type;
    CONDITION_OPERATION Op;
    PROPVARIANT Value;
    LPWSTR Name = NULL;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_ConditionFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IConditionFactory, &Factory));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    InitPropVariantFromString(L"needle", &Value);
    hr = Factory->MakeLeaf(L"System.FileName", COP_VALUE_CONTAINS, NULL, &Value, NULL, NULL, NULL, FALSE, &Leaf);
    PropVariantClear(&Value);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        ok_hr(Leaf->GetConditionType(&Type), S_OK);
        ok_int(Type, CT_LEAF_CONDITION);
        PropVariantInit(&Value);
        hr = Leaf->GetComparisonInfo(&Name, &Op, &Value);
        ok_hr(hr, S_OK);
        ok(Name && !wcscmp(Name, L"System.FileName"), "property %ls\n", Name);
        ok_int(Op, COP_VALUE_CONTAINS);
        ok(Value.vt == VT_LPWSTR && !wcscmp(Value.pwszVal, L"needle"), "value vt %u\n", Value.vt);
        CoTaskMemFree(Name);
        PropVariantClear(&Value);
    }

    hr = Factory->QueryInterface(IID_PPV_ARG(IConditionFactory2, &Factory2));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    hr = Factory2->CreateStringLeaf(PKEY_FileName, COP_VALUE_STARTSWITH, L"need", NULL,
                                    CONDITION_CREATION_DEFAULT, IID_PPV_ARG(ICondition, &Leaf2));
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr) && Leaf)
    {
        ICondition *Subs[2] = { Leaf, Leaf2 };
        CComPtr<IEnumUnknown> Enum;
        ULONG Count = 0;
        IUnknown *Sub;

        hr = Factory2->CreateCompoundFromArray(CT_AND_CONDITION, Subs, 2, CONDITION_CREATION_DEFAULT,
                                               IID_PPV_ARG(ICondition, &And));
        ok_hr(hr, S_OK);
        if (SUCCEEDED(hr))
        {
            ok_hr(And->GetConditionType(&Type), S_OK);
            ok_int(Type, CT_AND_CONDITION);
            hr = And->GetSubConditions(IID_PPV_ARG(IEnumUnknown, &Enum));
            ok_hr(hr, S_OK);
            while (Enum && Enum->Next(1, &Sub, NULL) == S_OK)
            {
                Count++;
                Sub->Release();
            }
            ok_long(Count, 2);
        }
        hr = Factory->MakeNot(Leaf, FALSE, &Not);
        ok_hr(hr, S_OK);
        if (SUCCEEDED(hr))
        {
            ok_hr(Not->GetConditionType(&Type), S_OK);
            ok_int(Type, CT_NOT_CONDITION);
        }
    }
}

static BOOL
Contains(LPCWSTR *Found, ULONG Count, LPCWSTR Name)
{
    for (ULONG i = 0; i < Count; i++)
        if (!_wcsicmp(Found[i], Name))
            return TRUE;
    return FALSE;
}

static ULONG
Enumerate(IShellItem *Folder, LPWSTR *Names, ULONG Max)
{
    CComPtr<IEnumShellItems> Enum;
    IShellItem *Child;
    ULONG Count = 0;
    HRESULT hr;

    hr = Folder->BindToHandler(NULL, BHID_EnumItems, IID_PPV_ARG(IEnumShellItems, &Enum));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return 0;
    while (Count < Max && Enum->Next(1, &Child, NULL) == S_OK)
    {
        LPWSTR Path = NULL;
        if (SUCCEEDED(Child->GetDisplayName(SIGDN_FILESYSPATH, &Path)))
        {
            LPCWSTR Base = wcsrchr(Path, L'\\');
            Names[Count++] = _wcsdup(Base ? Base + 1 : Path);
            CoTaskMemFree(Path);
        }
        Child->Release();
    }
    return Count;
}

static void
CheckResults(LPWSTR *Names, ULONG Count, LPCWSTR What)
{
    ok(Contains((LPCWSTR *)Names, Count, L"needle_one.txt"), "%ls: top level match missing\n", What);
    ok(Contains((LPCWSTR *)Names, Count, L"needle_two.txt"), "%ls: subfolder match missing\n", What);
    ok(!Contains((LPCWSTR *)Names, Count, L"other.txt"), "%ls: non-matching file returned\n", What);
    trace("%ls: %lu results\n", What, Count);
    for (ULONG i = 0; i < Count; i++)
        free(Names[i]);
}

static void
TestSearchFolder(void)
{
    CComPtr<ISearchFolderItemFactory> Search;
    CComPtr<IConditionFactory2> Conditions;
    CComPtr<IShellItemArray> Scope;
    CComPtr<IShellItem> DirItem, Results;
    CComPtr<ICondition> Condition;
    PIDLIST_ABSOLUTE Pidl = NULL;
    LPWSTR Names[32], Display = NULL;
    ULONG Count;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_SearchFolderItemFactory, NULL, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARG(ISearchFolderItemFactory, &Search));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    hr = CoCreateInstance(CLSID_ConditionFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IConditionFactory2, &Conditions));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;

    ok_hr(Search->SetDisplayName(L"Needle results"), S_OK);
    ok_hr(SHCreateItemFromParsingName(g_Dir, NULL, IID_PPV_ARG(IShellItem, &DirItem)), S_OK);
    ok_hr(SHCreateShellItemArrayFromShellItem(DirItem, IID_PPV_ARG(IShellItemArray, &Scope)), S_OK);
    ok_hr(Search->SetScope(Scope), S_OK);
    ok_hr(Conditions->CreateStringLeaf(PKEY_FileName, COP_VALUE_STARTSWITH, L"needle", NULL,
                                       CONDITION_CREATION_DEFAULT, IID_PPV_ARG(ICondition, &Condition)), S_OK);
    ok_hr(Search->SetCondition(Condition), S_OK);

    hr = Search->GetShellItem(IID_PPV_ARG(IShellItem, &Results));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    if (SUCCEEDED(Results->GetDisplayName(SIGDN_NORMALDISPLAY, &Display)))
    {
        ok(!wcscmp(Display, L"Needle results"), "display name %ls\n", Display);
        CoTaskMemFree(Display);
    }
    Count = Enumerate(Results, Names, _countof(Names));
    CheckResults(Names, Count, L"search folder");

    hr = Search->GetIDList(&Pidl);
    ok_hr(hr, S_OK);
    ok(Pidl != NULL, "no search folder pidl\n");
    ILFree(Pidl);
}

static void
TestSearchMs(void)
{
    CComPtr<IShellItem> Results;
    WCHAR Url[MAX_PATH * 4], Escaped[MAX_PATH * 3];
    WCHAR *Out = Escaped;
    LPWSTR Names[32];
    ULONG Count;
    HRESULT hr;

    for (LPCWSTR In = g_Dir; *In; In++)
    {
        if (*In == L'\\' || *In == L':' || *In == L' ')
            Out += wsprintfW(Out, L"%%%02X", *In);
        else
            *Out++ = *In;
    }
    *Out = 0;
    wsprintfW(Url, L"search-ms:displayname=Needle&query=needle&crumb=location:%ls", Escaped);
    hr = SHCreateItemFromParsingName(Url, NULL, IID_PPV_ARG(IShellItem, &Results));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    Count = Enumerate(Results, Names, _countof(Names));
    trace("search-ms: %lu results\n", Count);
    for (ULONG i = 0; i < Count; i++)
        free(Names[i]);
    Results.Release();

    wsprintfW(Url, L"search-ms:displayname=Needle&query=System.FileName%%3A~%%3Cneedle&crumb=location:%ls", Escaped);
    if (SUCCEEDED(SHCreateItemFromParsingName(Url, NULL, IID_PPV_ARG(IShellItem, &Results))))
    {
        Count = Enumerate(Results, Names, _countof(Names));
        trace("search-ms with a file name prefix: %lu results\n", Count);
        for (ULONG i = 0; i < Count; i++)
            free(Names[i]);
    }
}

START_TEST(SearchFolder)
{
    WCHAR Temp[MAX_PATH], Path[MAX_PATH], Sub[MAX_PATH];

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetTempPathW(_countof(Temp), Temp);
    wsprintfW(g_Dir, L"%ssearchtest%lu", Temp, GetCurrentProcessId());
    CreateDirectoryW(g_Dir, NULL);
    wsprintfW(Sub, L"%s\\sub", g_Dir);
    CreateDirectoryW(Sub, NULL);
    wsprintfW(Path, L"%s\\needle_one.txt", g_Dir);
    CreateTextFile(Path, "one");
    wsprintfW(Path, L"%s\\needle_two.txt", Sub);
    CreateTextFile(Path, "two");
    wsprintfW(Path, L"%s\\other.txt", g_Dir);
    CreateTextFile(Path, "needle inside");

    TestConditions();
    TestSearchFolder();
    TestSearchMs();

    wsprintfW(Path, L"%s\\needle_one.txt", g_Dir);
    DeleteFileW(Path);
    wsprintfW(Path, L"%s\\needle_two.txt", Sub);
    DeleteFileW(Path);
    wsprintfW(Path, L"%s\\other.txt", g_Dir);
    DeleteFileW(Path);
    RemoveDirectoryW(Sub);
    RemoveDirectoryW(g_Dir);
    CoUninitialize();
}
