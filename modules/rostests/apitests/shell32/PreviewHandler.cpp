/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests preview handler lookup and hosting
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shellutils.h>
#include <shobjidl.h>
#include <propsys.h>
#include <stdio.h>

static const WCHAR g_PreviewHandlerIid[] = L"{8895b1c6-b41f-4c1c-a562-0d564250836f}";
static WCHAR g_Txt[MAX_PATH];

static HRESULT
InitializeHandler(IUnknown *Handler, LPCWSTR Path)
{
    CComPtr<IInitializeWithItem> WithItem;
    CComPtr<IInitializeWithStream> WithStream;
    CComPtr<IInitializeWithFile> WithFile;
    CComPtr<IShellItem> Item;
    CComPtr<IStream> Stream;
    HRESULT hr;

    if (SUCCEEDED(Handler->QueryInterface(IID_PPV_ARG(IInitializeWithStream, &WithStream))))
    {
        hr = SHCreateStreamOnFileEx(Path, STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, NULL, &Stream);
        if (FAILED(hr))
            return hr;
        trace("initializing with a stream\n");
        return WithStream->Initialize(Stream, STGM_READ);
    }
    if (SUCCEEDED(Handler->QueryInterface(IID_PPV_ARG(IInitializeWithItem, &WithItem))))
    {
        hr = SHCreateItemFromParsingName(Path, NULL, IID_PPV_ARG(IShellItem, &Item));
        if (FAILED(hr))
            return hr;
        trace("initializing with an item\n");
        return WithItem->Initialize(Item, STGM_READ);
    }
    if (SUCCEEDED(Handler->QueryInterface(IID_PPV_ARG(IInitializeWithFile, &WithFile))))
    {
        trace("initializing with a file\n");
        return WithFile->Initialize(Path, STGM_READ);
    }
    return E_NOINTERFACE;
}

static BOOL CALLBACK
FindText(HWND hWnd, LPARAM Param)
{
    WCHAR Text[256], Class[64];

    GetClassNameW(hWnd, Class, _countof(Class));
    trace("preview child class %ls style %#lx exstyle %#lx\n", Class, GetWindowLongW(hWnd, GWL_STYLE),
          GetWindowLongW(hWnd, GWL_EXSTYLE));
    if (GetWindowTextW(hWnd, Text, _countof(Text)) && wcsstr(Text, L"preview content"))
    {
        *(BOOL *)Param = TRUE;
        return FALSE;
    }
    return TRUE;
}

static void
TestTextPreview(void)
{
    CComPtr<IPreviewHandler> Handler;
    WCHAR ClsidText[64];
    DWORD Length = _countof(ClsidText);
    BOOL Found = FALSE;
    CLSID Clsid;
    HWND Host;
    MSG Msg;
    RECT Rect = {0, 0, 400, 300};
    HRESULT hr;

    hr = AssocQueryStringW(ASSOCF_INIT_DEFAULTTOSTAR, ASSOCSTR_SHELLEXTENSION, L".txt", g_PreviewHandlerIid,
                           ClsidText, &Length);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    trace(".txt preview handler %ls\n", ClsidText);
    ok_hr(CLSIDFromString(ClsidText, &Clsid), S_OK);

    hr = CoCreateInstance(Clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_PPV_ARG(IPreviewHandler, &Handler));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    ok_hr(InitializeHandler(Handler, g_Txt), S_OK);

    Host = CreateWindowExW(0, L"STATIC", L"preview host", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           0, 0, 420, 340, NULL, NULL, NULL, NULL);
    ok(Host != NULL, "no host window\n");
    if (!Host)
        return;
    ok_hr(Handler->SetWindow(Host, &Rect), S_OK);
    hr = Handler->DoPreview();
    ok_hr(hr, S_OK);
    for (int i = 0; i < 50; i++)
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
        }
        if (GetWindow(Host, GW_CHILD))
            break;
        Sleep(20);
    }
    ok(GetWindow(Host, GW_CHILD) != NULL, "the preview created no window\n");
    EnumChildWindows(Host, FindText, (LPARAM)&Found);
    ok(Found, "the preview window does not show the file text\n");

    Rect.right = 300;
    ok_hr(Handler->SetRect(&Rect), S_OK);
    ok_hr(Handler->Unload(), S_OK);
    DestroyWindow(Host);
}

static void
TestNoHandler(void)
{
    WCHAR ClsidText[64];
    DWORD Length = _countof(ClsidText);
    HRESULT hr;

    hr = AssocQueryStringW(ASSOCF_INIT_DEFAULTTOSTAR, ASSOCSTR_SHELLEXTENSION, L".liberntnopreview",
                           g_PreviewHandlerIid, ClsidText, &Length);
    ok(FAILED(hr), "unknown type has a preview handler: %#lx\n", hr);
}

START_TEST(PreviewHandler)
{
    WCHAR Temp[MAX_PATH];
    HANDLE File;
    DWORD Written;
    const char Content[] = "preview content line";

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetTempPathW(_countof(Temp), Temp);
    wsprintfW(g_Txt, L"%spreview%lu.txt", Temp, GetCurrentProcessId());
    File = CreateFileW(g_Txt, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        skip("cannot create %ls\n", g_Txt);
        CoUninitialize();
        return;
    }
    WriteFile(File, Content, sizeof(Content) - 1, &Written, NULL);
    CloseHandle(File);

    TestTextPreview();
    TestNoHandler();

    DeleteFileW(g_Txt);
    CoUninitialize();
}
