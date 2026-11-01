/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SHAssocEnumHandlers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shobjidl.h>

static BOOL contains_nocase(PCWSTR haystack, PCWSTR needle)
{
    SIZE_T n = wcslen(needle);

    for (; *haystack; haystack++)
        if (!_wcsnicmp(haystack, needle, n))
            return TRUE;
    return FALSE;
}

static ULONG enum_handlers(PCWSTR ext, ASSOC_FILTER filter, BOOL *found_notepad, ULONG *recommended)
{
    IEnumAssocHandlers *handlers = NULL;
    IAssocHandler *handler;
    ULONG fetched, count = 0;
    HRESULT hr;

    *found_notepad = FALSE;
    *recommended = 0;
    hr = SHAssocEnumHandlers(ext, filter, &handlers);
    ok(hr == S_OK, "%ls filter %d: hr 0x%lx\n", ext, filter, hr);
    if (FAILED(hr))
        return 0;

    while (handlers->Next(1, &handler, &fetched) == S_OK && fetched == 1)
    {
        PWSTR name = NULL, ui = NULL, icon = NULL;
        int index = -1;
        HRESULT hr_name, hr_ui, hr_icon, hr_rec;

        hr_name = handler->GetName(&name);
        hr_ui = handler->GetUIName(&ui);
        hr_icon = handler->GetIconLocation(&icon, &index);
        hr_rec = handler->IsRecommended();
        trace("%ls filter %d: name 0x%lx '%ls' ui 0x%lx '%ls' icon 0x%lx '%ls',%d recommended 0x%lx\n",
              ext, filter, hr_name, name, hr_ui, ui, hr_icon, icon, index, hr_rec);
        ok(hr_name == S_OK && name && *name, "GetName hr 0x%lx\n", hr_name);
        ok(hr_ui == S_OK && ui && *ui, "GetUIName hr 0x%lx\n", hr_ui);
        if (name && contains_nocase(name, L"notepad"))
            *found_notepad = TRUE;
        if (hr_rec == S_OK)
            (*recommended)++;
        CoTaskMemFree(name);
        CoTaskMemFree(ui);
        CoTaskMemFree(icon);
        handler->Release();
        count++;
    }

    hr = handlers->Next(1, &handler, &fetched);
    ok(hr == S_FALSE && fetched == 0, "end hr 0x%lx fetched %lu\n", hr, fetched);
    handlers->Release();
    return count;
}

static void dump_key(HKEY root, PCWSTR path, int depth)
{
    WCHAR name[256], data[512];
    DWORD index, cch, size, type;
    HKEY key;

    if (RegOpenKeyExW(root, path, 0, KEY_READ, &key))
        return;
    for (index = 0; ; index++)
    {
        cch = ARRAYSIZE(name);
        size = sizeof(data) - sizeof(WCHAR);
        if (RegEnumValueW(key, index, name, &cch, NULL, &type, (BYTE *)data, &size))
            break;
        data[size / sizeof(WCHAR)] = 0;
        trace("reg %ls [%ls] type %lu '%ls'\n", path, cch ? name : L"(default)", type,
              (type == REG_SZ || type == REG_EXPAND_SZ) ? data : L"");
    }
    for (index = 0; depth > 0; index++)
    {
        WCHAR sub[512];

        cch = ARRAYSIZE(name);
        if (RegEnumKeyExW(key, index, name, &cch, NULL, NULL, NULL, NULL))
            break;
        wsprintfW(sub, L"%s\\%s", path, name);
        trace("reg key %ls\n", sub);
        dump_key(root, sub, depth - 1);
    }
    RegCloseKey(key);
}

START_TEST(SHAssocEnumHandlers)
{
    IEnumAssocHandlers *handlers;
    ULONG count, recommended;
    BOOL notepad;
    HRESULT hr;

    CoInitialize(NULL);

    count = enum_handlers(L".txt", ASSOC_FILTER_NONE, &notepad, &recommended);
    ok(count >= 1, ".txt handlers %lu\n", count);
    ok(notepad, ".txt has no notepad handler\n");

    count = enum_handlers(L".txt", ASSOC_FILTER_RECOMMENDED, &notepad, &recommended);
    ok(count >= 1, ".txt recommended handlers %lu\n", count);
    ok(recommended == count, ".txt recommended %lu of %lu\n", recommended, count);

    count = enum_handlers(L".libernt-unknown-ext", ASSOC_FILTER_NONE, &notepad, &recommended);
    ok(count >= 1, "unknown extension handlers %lu\n", count);
    ok(recommended == 0, "unknown extension recommended %lu\n", recommended);

    handlers = (IEnumAssocHandlers *)(ULONG_PTR)0xdeadbeef;
    hr = SHAssocEnumHandlers(L"txt", ASSOC_FILTER_NONE, &handlers);
    ok(hr == S_OK, "no dot: hr 0x%lx\n", hr);
    if (SUCCEEDED(hr) && handlers)
        handlers->Release();
    count = enum_handlers(L"txt", ASSOC_FILTER_NONE, &notepad, &recommended);
    trace("no dot: %lu handlers, %lu recommended\n", count, recommended);

    handlers = (IEnumAssocHandlers *)(ULONG_PTR)0xdeadbeef;
    hr = SHAssocEnumHandlers(L"", ASSOC_FILTER_NONE, &handlers);
    ok(hr == S_OK, "empty: hr 0x%lx\n", hr);
    if (SUCCEEDED(hr) && handlers)
        handlers->Release();
    count = enum_handlers(L"", ASSOC_FILTER_NONE, &notepad, &recommended);
    trace("empty: %lu handlers, %lu recommended\n", count, recommended);

    handlers = (IEnumAssocHandlers *)(ULONG_PTR)0xdeadbeef;
    hr = SHAssocEnumHandlers(NULL, ASSOC_FILTER_NONE, &handlers);
    ok(hr == E_OUTOFMEMORY, "NULL: hr 0x%lx\n", hr);

    dump_key(HKEY_CLASSES_ROOT, L"*\\OpenWithList", 1);
    dump_key(HKEY_CLASSES_ROOT, L"*\\OpenWithProgids", 0);
    dump_key(HKEY_CLASSES_ROOT, L".txt", 2);
    dump_key(HKEY_CLASSES_ROOT, L"SystemFileAssociations\\.txt", 2);
    dump_key(HKEY_CLASSES_ROOT, L"SystemFileAssociations\\text", 2);
    dump_key(HKEY_CLASSES_ROOT, L"txtfile\\shell", 2);
    dump_key(HKEY_CLASSES_ROOT, L"Applications\\notepad.exe", 2);
    dump_key(HKEY_CLASSES_ROOT, L"Applications\\mspaint.exe", 2);
    dump_key(HKEY_CLASSES_ROOT, L"Applications\\wmplayer.exe", 2);
    dump_key(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.txt", 2);

    CoUninitialize();
}
