/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for AssocCreateForClasses and AssocGetDetailsOfPropKey
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shellutils.h>
#include <shobjidl.h>
#include <propkey.h>
#include <strsafe.h>

static HRESULT get_string(IQueryAssociations *qa, ASSOCSTR str, PCWSTR extra, PWSTR buffer, DWORD cch)
{
    buffer[0] = 0;
    return qa->GetString(ASSOCF_NONE, str, extra, buffer, &cch);
}

static void test_create(void)
{
    ASSOCIATIONELEMENT elements[3];
    IQueryAssociations *qa = NULL;
    WCHAR buffer[MAX_PATH], expected[MAX_PATH];
    DWORD cch;
    HKEY key;
    HRESULT hr;

    elements[0].ac = ASSOCCLASS_PROGID_STR;
    elements[0].hkClass = NULL;
    elements[0].pszClass = L"txtfile";
    elements[1].ac = ASSOCCLASS_STAR;
    elements[1].hkClass = NULL;
    elements[1].pszClass = NULL;

    hr = AssocCreateForClasses(elements, 2, IID_PPV_ARG(IQueryAssociations, &qa));
    ok(hr == S_OK, "AssocCreateForClasses hr 0x%lx\n", hr);
    if (SUCCEEDED(hr))
    {
        cch = ARRAYSIZE(expected);
        expected[0] = 0;
        AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_FRIENDLYDOCNAME, L"txtfile", NULL, expected, &cch);
        hr = get_string(qa, ASSOCSTR_FRIENDLYDOCNAME, NULL, buffer, ARRAYSIZE(buffer));
        ok(hr == S_OK, "FRIENDLYDOCNAME hr 0x%lx\n", hr);
        ok(!wcscmp(buffer, expected), "FRIENDLYDOCNAME '%ls', expected '%ls'\n", buffer, expected);

        cch = ARRAYSIZE(expected);
        expected[0] = 0;
        AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_COMMAND, L"txtfile", L"open", expected, &cch);
        hr = get_string(qa, ASSOCSTR_COMMAND, L"open", buffer, ARRAYSIZE(buffer));
        ok(hr == S_OK, "COMMAND open hr 0x%lx\n", hr);
        ok(!wcscmp(buffer, expected), "COMMAND '%ls', expected '%ls'\n", buffer, expected);

        hr = get_string(qa, ASSOCSTR_COMMAND, L"libernt-no-verb", buffer, ARRAYSIZE(buffer));
        ok(hr == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION), "missing verb hr 0x%lx\n", hr);

        key = NULL;
        hr = qa->GetKey(ASSOCF_NONE, ASSOCKEY_CLASS, NULL, &key);
        ok(hr == S_OK && key, "ASSOCKEY_CLASS hr 0x%lx\n", hr);
        if (key)
            RegCloseKey(key);

        qa->Release();
        qa = NULL;
    }

    elements[0].ac = ASSOCCLASS_PROGID_STR;
    elements[0].pszClass = L"libernt.no.such.progid";
    elements[1].ac = ASSOCCLASS_PROGID_STR;
    elements[1].pszClass = L"txtfile";
    hr = AssocCreateForClasses(elements, 2, IID_PPV_ARG(IQueryAssociations, &qa));
    ok(hr == S_OK, "missing first class hr 0x%lx\n", hr);
    if (SUCCEEDED(hr))
    {
        cch = ARRAYSIZE(expected);
        expected[0] = 0;
        AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_FRIENDLYDOCNAME, L"txtfile", NULL, expected, &cch);
        hr = get_string(qa, ASSOCSTR_FRIENDLYDOCNAME, NULL, buffer, ARRAYSIZE(buffer));
        ok(hr == S_OK && !wcscmp(buffer, expected), "fallback FRIENDLYDOCNAME hr 0x%lx '%ls'\n", hr, buffer);
        qa->Release();
        qa = NULL;
    }

    elements[0].ac = ASSOCCLASS_FOLDER;
    elements[0].pszClass = NULL;
    hr = AssocCreateForClasses(elements, 1, IID_PPV_ARG(IQueryAssociations, &qa));
    ok(hr == S_OK, "folder hr 0x%lx\n", hr);
    if (SUCCEEDED(hr))
    {
        cch = ARRAYSIZE(expected);
        expected[0] = 0;
        AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_FRIENDLYDOCNAME, L"Folder", NULL, expected, &cch);
        hr = get_string(qa, ASSOCSTR_FRIENDLYDOCNAME, NULL, buffer, ARRAYSIZE(buffer));
        ok(hr == S_OK, "folder FRIENDLYDOCNAME hr 0x%lx\n", hr);
        ok(!wcscmp(buffer, expected), "folder FRIENDLYDOCNAME '%ls', expected '%ls'\n", buffer, expected);
        qa->Release();
        qa = NULL;
    }

    elements[0].ac = ASSOCCLASS_PROGID_STR;
    elements[0].pszClass = L"libernt.no.such.progid";
    hr = AssocCreateForClasses(elements, 1, IID_PPV_ARG(IQueryAssociations, &qa));
    ok(hr == S_OK, "only missing class hr 0x%lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = get_string(qa, ASSOCSTR_FRIENDLYDOCNAME, NULL, buffer, ARRAYSIZE(buffer));
        ok(hr == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION), "only missing class FRIENDLYDOCNAME hr 0x%lx\n", hr);
        qa->Release();
        qa = NULL;
    }

    elements[0].ac = ASSOCCLASS_PROGID_KEY;
    elements[0].hkClass = NULL;
    elements[0].pszClass = NULL;
    if (!RegOpenKeyExW(HKEY_CLASSES_ROOT, L"txtfile", 0, KEY_READ, &elements[0].hkClass))
    {
        hr = AssocCreateForClasses(elements, 1, IID_PPV_ARG(IQueryAssociations, &qa));
        ok(hr == S_OK, "PROGID_KEY hr 0x%lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = get_string(qa, ASSOCSTR_FRIENDLYDOCNAME, NULL, buffer, ARRAYSIZE(buffer));
            ok(hr == S_OK, "PROGID_KEY FRIENDLYDOCNAME hr 0x%lx '%ls'\n", hr, buffer);
            qa->Release();
            qa = NULL;
        }
        RegCloseKey(elements[0].hkClass);
    }

    qa = NULL;
    hr = AssocCreateForClasses(elements, 0, IID_PPV_ARG(IQueryAssociations, &qa));
    ok(hr == S_OK, "zero classes hr 0x%lx\n", hr);
    ok(qa != NULL, "zero classes qa NULL\n");
    if (qa)
    {
        hr = get_string(qa, ASSOCSTR_FRIENDLYDOCNAME, NULL, buffer, ARRAYSIZE(buffer));
        ok(hr == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION), "zero classes FRIENDLYDOCNAME hr 0x%lx\n", hr);
        qa->Release();
        qa = NULL;
    }

    {
        IUnknown *unk = NULL;

        elements[0].ac = ASSOCCLASS_PROGID_STR;
        elements[0].hkClass = NULL;
        elements[0].pszClass = L"txtfile";
        hr = AssocCreateForClasses(elements, 1, IID_PPV_ARG(IUnknown, &unk));
        ok(hr == S_OK, "IUnknown hr 0x%lx\n", hr);
        if (unk)
            unk->Release();
    }
}

static void test_details(void)
{
    static const PROPERTYKEY *keys[] = { &PKEY_ItemTypeText, &PKEY_ItemType, &PKEY_Size, &PKEY_FileName, &PKEY_Author };
    static const PCSTR names[] = { "ItemTypeText", "ItemType", "Size", "FileName", "Author" };
    WCHAR path[MAX_PATH];
    PIDLIST_ABSOLUTE pidl;
    IShellFolder *folder;
    PCUITEMID_CHILD child;
    HANDLE file;
    HRESULT hr;
    DWORD i;

    GetTempPathW(MAX_PATH, path);
    StringCchCatW(path, MAX_PATH, L"libernt_details.txt");
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    CloseHandle(file);

    pidl = ILCreateFromPathW(path);
    hr = SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, &folder), &child);
    ok(hr == S_OK, "SHBindToParent hr 0x%lx\n", hr);
    if (SUCCEEDED(hr))
    {
        for (i = 0; i < ARRAYSIZE(keys); i++)
        {
            VARIANT v;
            BOOL found = 0xdead;

            VariantInit(&v);
            hr = AssocGetDetailsOfPropKey(folder, child, keys[i], &v, &found);
            trace("AssocGetDetailsOfPropKey %s: hr 0x%lx found %d vt %u '%ls'\n", names[i], hr, found, V_VT(&v),
                  V_VT(&v) == VT_BSTR ? V_BSTR(&v) : L"");
            VariantClear(&v);
        }
        folder->Release();
    }

    ILFree(pidl);
    DeleteFileW(path);
}

START_TEST(AssocCreateForClasses)
{
    CoInitialize(NULL);
    test_create();
    test_details();
    CoUninitialize();
}
