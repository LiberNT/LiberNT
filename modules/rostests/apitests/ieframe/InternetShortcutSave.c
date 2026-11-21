/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests saving an Internet shortcut while its property storage is still open
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <windows.h>
#include <intshcut.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>

static void ReadText(PCWSTR Path, PSTR Buffer, DWORD Size)
{
    HANDLE File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD Read = 0;

    Buffer[0] = 0;
    if (File == INVALID_HANDLE_VALUE)
        return;
    ReadFile(File, Buffer, Size - 1, &Read, NULL);
    Buffer[Read] = 0;
    CloseHandle(File);
}

START_TEST(InternetShortcutSave)
{
    IUniformResourceLocatorW *Url = NULL;
    IPropertySetStorage *SetStorage = NULL;
    IPropertyStorage *Storage = NULL;
    IPersistFile *PersistFile = NULL;
    PROPSPEC Specs[2] = { { PRSPEC_PROPID, { PID_IS_ICONINDEX } }, { PRSPEC_PROPID, { PID_IS_ICONFILE } } };
    PROPVARIANT Values[2];
    WCHAR Path[MAX_PATH];
    CHAR Text[512];
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetTempPathW(ARRAYSIZE(Path), Path);
    wcscat(Path, L"ieframe save test.url");
    DeleteFileW(Path);

    hr = CoCreateInstance(&CLSID_InternetShortcut, NULL, CLSCTX_ALL, &IID_IUniformResourceLocatorW, (void **)&Url);
    ok(hr == S_OK, "CoCreateInstance returned 0x%08lx\n", hr);
    if (FAILED(hr))
    {
        CoUninitialize();
        return;
    }
    hr = Url->lpVtbl->SetURL(Url, L"https://nodejs.org/", 0);
    ok(hr == S_OK, "SetURL returned 0x%08lx\n", hr);

    /* WiX util's internet shortcut action keeps the storage open until after IPersistFile::Save */
    hr = Url->lpVtbl->QueryInterface(Url, &IID_IPropertySetStorage, (void **)&SetStorage);
    ok(hr == S_OK, "QueryInterface(IPropertySetStorage) returned 0x%08lx\n", hr);
    if (SetStorage)
    {
        hr = IPropertySetStorage_Open(SetStorage, &FMTID_Intshcut, STGM_WRITE, &Storage);
        ok(hr == S_OK, "Open(FMTID_Intshcut, STGM_WRITE) returned 0x%08lx\n", hr);
    }
    if (Storage)
    {
        Values[0].vt = VT_I4;
        Values[0].lVal = 3;
        Values[1].vt = VT_LPWSTR;
        Values[1].pwszVal = L"C:\\Windows\\system32\\shell32.dll";
        hr = IPropertyStorage_WriteMultiple(Storage, 2, Specs, Values, 0);
        ok(hr == S_OK, "WriteMultiple returned 0x%08lx\n", hr);
        hr = IPropertyStorage_Commit(Storage, STGC_DEFAULT);
        ok(hr == S_OK, "Commit returned 0x%08lx\n", hr);
    }

    hr = Url->lpVtbl->QueryInterface(Url, &IID_IPersistFile, (void **)&PersistFile);
    ok(hr == S_OK, "QueryInterface(IPersistFile) returned 0x%08lx\n", hr);
    if (PersistFile)
    {
        hr = IPersistFile_Save(PersistFile, Path, TRUE);
        ok(hr == S_OK, "Save with the property storage open returned 0x%08lx\n", hr);
        IPersistFile_Release(PersistFile);
    }

    ReadText(Path, Text, sizeof(Text));
    ok(strstr(Text, "[InternetShortcut]") != NULL, "missing section in '%s'\n", Text);
    ok(strstr(Text, "URL=https://nodejs.org/") != NULL, "missing URL in '%s'\n", Text);
    ok(StrStrIA(Text, "IconFile=C:\\Windows\\system32\\shell32.dll") != NULL, "missing icon file in '%s'\n", Text);
    ok(StrStrIA(Text, "IconIndex=3") != NULL, "missing icon index in '%s'\n", Text);

    if (Storage)
        IPropertyStorage_Release(Storage);
    if (SetStorage)
        IPropertySetStorage_Release(SetStorage);
    Url->lpVtbl->Release(Url);
    DeleteFileW(Path);
    CoUninitialize();
}
