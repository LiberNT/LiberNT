/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SHGetPropertyStoreFromIDList and temporary item properties
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shellutils.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <strsafe.h>

static void trace_value(PCSTR what, const PROPVARIANT *pv)
{
    PWSTR text = NULL;

    if (pv->vt != VT_EMPTY && SUCCEEDED(PropVariantToStringAlloc(*pv, &text)))
    {
        trace("%s: vt %u '%ls'\n", what, pv->vt, text);
        CoTaskMemFree(text);
    }
    else
    {
        trace("%s: vt %u\n", what, pv->vt);
    }
}

static void test_store_from_idlist(PCIDLIST_ABSOLUTE pidl)
{
    IPropertyStore *store = NULL;
    PROPVARIANT pv;
    HRESULT hr;

    hr = SHGetPropertyStoreFromIDList(pidl, GPS_DEFAULT, IID_PPV_ARG(IPropertyStore, &store));
    ok(hr == S_OK, "GPS_DEFAULT hr 0x%lx\n", hr);
    if (SUCCEEDED(hr))
    {
        PropVariantInit(&pv);
        hr = store->GetValue(PKEY_Size, &pv);
        ok(hr == S_OK, "PKEY_Size hr 0x%lx\n", hr);
        ok(pv.vt == VT_UI8 && pv.uhVal.QuadPart == 11, "PKEY_Size vt %u value %I64u\n", pv.vt, pv.uhVal.QuadPart);
        PropVariantClear(&pv);

        hr = store->GetValue(PKEY_FileName, &pv);
        ok(hr == S_OK, "PKEY_FileName hr 0x%lx\n", hr);
        ok(pv.vt == VT_LPWSTR && !lstrcmpiW(pv.pwszVal, L"libernt_props.txt"), "PKEY_FileName vt %u\n", pv.vt);
        trace_value("PKEY_FileName", &pv);
        PropVariantClear(&pv);
        store->Release();
    }

    store = (IPropertyStore *)(ULONG_PTR)0xdeadbeef;
    hr = SHGetPropertyStoreFromIDList(NULL, GPS_DEFAULT, IID_PPV_ARG(IPropertyStore, &store));
    ok(hr == E_INVALIDARG, "NULL pidl hr 0x%lx\n", hr);
    ok(store == NULL, "NULL pidl store %p\n", store);

    store = NULL;
    hr = SHGetPropertyStoreFromIDList(pidl, GPS_READWRITE, IID_PPV_ARG(IPropertyStore, &store));
    ok(hr == E_FAIL, "GPS_READWRITE hr 0x%lx\n", hr);
    if (store)
        store->Release();

    {
        IPropertyStoreCapabilities *caps = NULL;

        hr = SHGetPropertyStoreFromIDList(pidl, GPS_DEFAULT, IID_PPV_ARG(IPropertyStoreCapabilities, &caps));
        ok(hr == S_OK, "IPropertyStoreCapabilities hr 0x%lx\n", hr);
        if (caps)
        {
            hr = caps->IsPropertyWritable(PKEY_Comment);
            ok(hr == S_OK, "IsPropertyWritable(Comment) hr 0x%lx\n", hr);
            hr = caps->IsPropertyWritable(PKEY_FileName);
            ok(hr == S_OK, "IsPropertyWritable(FileName) hr 0x%lx\n", hr);
            caps->Release();
        }
    }

    store = NULL;
    hr = SHGetPropertyStoreFromIDList(pidl, GPS_DEFAULT, IID_PPV_ARG(IPropertyStore, &store));
    if (SUCCEEDED(hr))
    {
        PROPVARIANT pv;

        InitPropVariantFromString(L"x", &pv);
        hr = store->SetValue(PKEY_Comment, pv);
        ok(hr == STG_E_ACCESSDENIED, "GPS_DEFAULT SetValue hr 0x%lx\n", hr);
        PropVariantClear(&pv);
        hr = store->Commit();
        ok(hr == STG_E_ACCESSDENIED, "GPS_DEFAULT Commit hr 0x%lx\n", hr);
        store->Release();
    }
}

static void test_temporary(PCIDLIST_ABSOLUTE pidl)
{
    IShellItem *item = NULL, *item2 = NULL;
    IShellItem2 *item2b = NULL;
    IPropertyStore *store = NULL;
    PROPVARIANT pv, out;
    HRESULT hr;

    hr = SHCreateItemFromIDList(pidl, IID_PPV_ARG(IShellItem, &item));
    ok(hr == S_OK, "SHCreateItemFromIDList hr 0x%lx\n", hr);
    if (FAILED(hr))
        return;

    PropVariantInit(&out);
    hr = SHGetTemporaryPropertyForItem(item, PKEY_Comment, &out);
    ok(hr == S_OK, "unset Get hr 0x%lx\n", hr);
    ok(out.vt == VT_EMPTY, "unset Get vt %u\n", out.vt);
    PropVariantClear(&out);

    InitPropVariantFromString(L"temporary value", &pv);
    hr = SHSetTemporaryPropertyForItem(item, PKEY_Comment, pv);
    ok(hr == S_OK, "Set hr 0x%lx\n", hr);

    PropVariantInit(&out);
    hr = SHGetTemporaryPropertyForItem(item, PKEY_Comment, &out);
    ok(hr == S_OK, "Get hr 0x%lx\n", hr);
    ok(out.vt == VT_LPWSTR && !wcscmp(out.pwszVal, L"temporary value"), "Get vt %u\n", out.vt);
    PropVariantClear(&out);

    PropVariantInit(&out);
    hr = SHGetTemporaryPropertyForItem(item, PKEY_Title, &out);
    ok(hr == S_OK && out.vt == VT_EMPTY, "other key hr 0x%lx vt %u\n", hr, out.vt);
    PropVariantClear(&out);

    hr = item->QueryInterface(IID_PPV_ARG(IShellItem2, &item2b));
    ok(hr == S_OK, "IShellItem2 hr 0x%lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = item2b->GetPropertyStore(GPS_TEMPORARY, IID_PPV_ARG(IPropertyStore, &store));
        ok(hr == S_OK, "GPS_TEMPORARY hr 0x%lx\n", hr);
        if (SUCCEEDED(hr))
        {
            DWORD count = 0;

            PropVariantInit(&out);
            hr = store->GetValue(PKEY_Comment, &out);
            ok(hr == S_OK && out.vt == VT_LPWSTR, "GPS_TEMPORARY value hr 0x%lx vt %u\n", hr, out.vt);
            PropVariantClear(&out);
            store->GetCount(&count);
            ok(count == 1, "GPS_TEMPORARY count %lu\n", count);
            store->Release();
            store = NULL;
        }

        hr = item2b->GetPropertyStore(GPS_DEFAULT, IID_PPV_ARG(IPropertyStore, &store));
        ok(hr == S_OK, "GPS_DEFAULT hr 0x%lx\n", hr);
        if (SUCCEEDED(hr))
        {
            PropVariantInit(&out);
            hr = store->GetValue(PKEY_Comment, &out);
            ok(hr == S_OK && out.vt == VT_EMPTY, "GPS_DEFAULT PKEY_Comment hr 0x%lx vt %u\n", hr, out.vt);
            PropVariantClear(&out);
            store->Release();
            store = NULL;
        }

        hr = item2b->GetPropertyStore((GETPROPERTYSTOREFLAGS)(GPS_TEMPORARY | GPS_READWRITE), IID_PPV_ARG(IPropertyStore, &store));
        ok(hr == S_OK, "GPS_TEMPORARY|GPS_READWRITE hr 0x%lx\n", hr);
        if (store)
        {
            store->Release();
            store = NULL;
        }
        item2b->Release();
    }

    hr = SHCreateItemFromIDList(pidl, IID_PPV_ARG(IShellItem, &item2));
    ok(hr == S_OK, "second item hr 0x%lx\n", hr);
    if (SUCCEEDED(hr))
    {
        PropVariantInit(&out);
        hr = SHGetTemporaryPropertyForItem(item2, PKEY_Comment, &out);
        ok(hr == S_OK, "second item Get hr 0x%lx\n", hr);
        ok(out.vt == VT_EMPTY, "second item vt %u\n", out.vt);
        trace_value("second item", &out);
        PropVariantClear(&out);
        item2->Release();
    }

    PropVariantInit(&out);
    hr = SHSetTemporaryPropertyForItem(item, PKEY_Comment, out);
    ok(hr == S_OK, "Set VT_EMPTY hr 0x%lx\n", hr);
    hr = SHGetTemporaryPropertyForItem(item, PKEY_Comment, &out);
    ok(hr == S_OK && out.vt == VT_EMPTY, "after VT_EMPTY hr 0x%lx vt %u\n", hr, out.vt);
    PropVariantClear(&out);

    PropVariantClear(&pv);
    item->Release();
}

START_TEST(ItemPropertyStores)
{
    WCHAR path[MAX_PATH];
    PIDLIST_ABSOLUTE pidl;
    HANDLE file;
    DWORD written;

    CoInitialize(NULL);

    GetTempPathW(MAX_PATH, path);
    StringCchCatW(path, MAX_PATH, L"libernt_props.txt");
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(file != INVALID_HANDLE_VALUE, "CreateFileW failed: %lu\n", GetLastError());
    if (file == INVALID_HANDLE_VALUE)
        return;
    WriteFile(file, "hello world", 11, &written, NULL);
    CloseHandle(file);

    pidl = ILCreateFromPathW(path);
    ok(pidl != NULL, "ILCreateFromPathW failed\n");
    if (pidl)
    {
        test_store_from_idlist(pidl);
        test_temporary(pidl);
        ILFree(pidl);
    }

    DeleteFileW(path);
    CoUninitialize();
}
