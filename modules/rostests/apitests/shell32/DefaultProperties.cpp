/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SHAddDefaultPropertiesByExt, SHCreateDefaultPropertiesOp and SHSetDefaultProperties
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shellutils.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <strsafe.h>
#include <lmcons.h>

static const PROPERTYKEY key_date_taken =
    { { 0x14b81da1, 0x0135, 0x4d31, { 0x96, 0xd9, 0x6c, 0xbf, 0xc9, 0x67, 0x1a, 0x99 } }, 36867 };
static const PROPERTYKEY key_date_created =
    { { 0xf29f85e0, 0x4ff9, 0x1068, { 0xab, 0x91, 0x08, 0x00, 0x2b, 0x27, 0xb3, 0xd9 } }, 12 };

static void check_author(IPropertyStore *store, PCWSTR what, PCWSTR expected)
{
    PROPVARIANT pv;
    HRESULT hr;

    PropVariantInit(&pv);
    hr = store->GetValue(PKEY_Author, &pv);
    ok(hr == S_OK, "%ls: Author hr 0x%lx\n", what, hr);
    ok(pv.vt == (VT_VECTOR | VT_LPWSTR) && pv.calpwstr.cElems == 1 && !wcscmp(pv.calpwstr.pElems[0], expected),
       "%ls: Author vt %x count %lu\n", what, pv.vt, pv.vt == (VT_VECTOR | VT_LPWSTR) ? pv.calpwstr.cElems : 0);
    PropVariantClear(&pv);
}

static void check_now(IPropertyStore *store, const PROPERTYKEY *key, PCWSTR what, BOOL present)
{
    ULARGE_INTEGER now, value;
    FILETIME ft;
    PROPVARIANT pv;
    HRESULT hr;

    PropVariantInit(&pv);
    hr = store->GetValue(*key, &pv);
    ok(hr == S_OK, "%ls: GetValue hr 0x%lx\n", what, hr);
    if (!present)
    {
        ok(pv.vt == VT_EMPTY, "%ls: vt %u\n", what, pv.vt);
        PropVariantClear(&pv);
        return;
    }
    ok(pv.vt == VT_FILETIME, "%ls: vt %u\n", what, pv.vt);
    if (pv.vt == VT_FILETIME)
    {
        GetSystemTimeAsFileTime(&ft);
        now.LowPart = ft.dwLowDateTime;
        now.HighPart = ft.dwHighDateTime;
        value.LowPart = pv.filetime.dwLowDateTime;
        value.HighPart = pv.filetime.dwHighDateTime;
        ok(value.QuadPart <= now.QuadPart && now.QuadPart - value.QuadPart < 600000000ULL,
           "%ls: time %I64u now %I64u\n", what, value.QuadPart, now.QuadPart);
    }
    PropVariantClear(&pv);
}

static void test_add_by_ext(void)
{
    static const struct
    {
        PCWSTR ext;
        DWORD count;
        BOOL photo;
    } tests[] =
    {
        { L".txt", 2, FALSE },
        { L".jpg", 3, TRUE },
        { L".png", 3, TRUE },
        { L".mp3", 2, FALSE },
        { L".libernt-unknown", 2, FALSE },
    };
    IPropertyStore *store;
    WCHAR user[256];
    DWORD i, size, count;
    PROPVARIANT pv;
    HRESULT hr;

    size = ARRAYSIZE(user);
    GetUserNameW(user, &size);

    for (i = 0; i < ARRAYSIZE(tests); i++)
    {
        hr = PSCreateMemoryPropertyStore(IID_PPV_ARG(IPropertyStore, &store));
        ok(hr == S_OK, "PSCreateMemoryPropertyStore hr 0x%lx\n", hr);
        if (FAILED(hr))
            continue;
        hr = SHAddDefaultPropertiesByExt(tests[i].ext, store);
        ok(hr == S_OK, "%ls: hr 0x%lx\n", tests[i].ext, hr);
        count = 0;
        store->GetCount(&count);
        ok(count == tests[i].count, "%ls: count %lu\n", tests[i].ext, count);
        check_author(store, tests[i].ext, user);
        check_now(store, &key_date_created, tests[i].ext, TRUE);
        check_now(store, &key_date_taken, tests[i].ext, tests[i].photo);
        store->Release();
    }

    hr = PSCreateMemoryPropertyStore(IID_PPV_ARG(IPropertyStore, &store));
    if (SUCCEEDED(hr))
    {
        hr = SHAddDefaultPropertiesByExt(NULL, store);
        ok(hr == E_INVALIDARG, "NULL ext: hr 0x%lx\n", hr);
        hr = SHAddDefaultPropertiesByExt(L"txt", store);
        ok(hr == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION), "no dot: hr 0x%lx\n", hr);
        count = 0xdead;
        store->GetCount(&count);
        ok(count == 0, "no dot: count %lu\n", count);

        hr = SHAddDefaultPropertiesByExt(L"", store);
        ok(hr == E_INVALIDARG, "empty ext: hr 0x%lx\n", hr);

        InitPropVariantFromString(L"preset", &pv);
        store->SetValue(PKEY_Author, pv);
        PropVariantClear(&pv);
        hr = SHAddDefaultPropertiesByExt(L".txt", store);
        ok(hr == S_OK, "preset: hr 0x%lx\n", hr);
        PropVariantInit(&pv);
        store->GetValue(PKEY_Author, &pv);
        ok(pv.vt == VT_LPWSTR && !wcscmp(pv.pwszVal, L"preset"), "preset Author overwritten: vt %x\n", pv.vt);
        PropVariantClear(&pv);
        store->Release();
    }
}

static void test_op(PCWSTR path)
{
    IFileOperation *op = (IFileOperation *)(ULONG_PTR)0xdeadbeef;
    IShellItem *item = NULL;
    HRESULT hr;

    hr = SHCreateItemFromParsingName(path, NULL, IID_PPV_ARG(IShellItem, &item));
    ok(hr == S_OK, "SHCreateItemFromParsingName hr 0x%lx\n", hr);
    if (FAILED(hr))
        return;

    hr = SHCreateDefaultPropertiesOp(item, &op);
    ok(hr == E_FAIL, "SHCreateDefaultPropertiesOp hr 0x%lx\n", hr);
    ok(op == NULL, "SHCreateDefaultPropertiesOp op %p\n", op);
    if (SUCCEEDED(hr) && op)
        op->Release();

    hr = SHSetDefaultProperties(NULL, item, FOF_NO_UI, NULL);
    ok(hr == E_FAIL, "SHSetDefaultProperties hr 0x%lx\n", hr);

    item->Release();
}

START_TEST(DefaultProperties)
{
    WCHAR path[MAX_PATH];
    HANDLE file;

    CoInitialize(NULL);

    test_add_by_ext();

    GetTempPathW(MAX_PATH, path);
    StringCchCatW(path, MAX_PATH, L"libernt_defaults.txt");
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file);
        test_op(path);
        DeleteFileW(path);
    }

    CoUninitialize();
}
