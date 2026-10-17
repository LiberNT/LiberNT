/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests recent documents and application document lists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <shellutils.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <stdio.h>

static const WCHAR g_AppId[] = L"LiberNT.ApiTest.RecentDocs";
static WCHAR g_File[MAX_PATH];
static WCHAR g_Name[MAX_PATH];

static BOOL
WaitForFile(LPCWSTR Path, DWORD Milliseconds)
{
    DWORD Start = GetTickCount();

    do
    {
        if (GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES)
            return TRUE;
        Sleep(50);
    } while (GetTickCount() - Start < Milliseconds);
    return FALSE;
}

static BOOL
ListContains(IObjectArray *List, LPCWSTR Path)
{
    UINT Count = 0;

    if (FAILED(List->GetCount(&Count)))
        return FALSE;
    for (UINT i = 0; i < Count; i++)
    {
        CComPtr<IShellItem> Item;
        CComPtr<IShellLinkW> Link;
        WCHAR Target[MAX_PATH];
        LPWSTR ItemPath = NULL;
        BOOL Match = FALSE;

        if (SUCCEEDED(List->GetAt(i, IID_PPV_ARG(IShellItem, &Item))) &&
            SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &ItemPath)))
        {
            Match = !_wcsicmp(ItemPath, Path);
            CoTaskMemFree(ItemPath);
        }
        else if (SUCCEEDED(List->GetAt(i, IID_PPV_ARG(IShellLinkW, &Link))) &&
                 SUCCEEDED(Link->GetPath(Target, _countof(Target), NULL, 0)))
        {
            Match = !_wcsicmp(Target, Path);
        }
        if (Match)
            return TRUE;
    }
    return FALSE;
}

static void
TestRecentFolder(void)
{
    CComPtr<IShellLinkW> Link;
    CComPtr<IPersistFile> Persist;
    CComPtr<IShellItem> Item;
    WCHAR LinkPath[MAX_PATH], Target[MAX_PATH];
    LPWSTR Recent = NULL, Display = NULL;
    HRESULT hr;

    hr = SHGetKnownFolderPath(FOLDERID_Recent, 0, NULL, &Recent);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    hr = SHCreateItemFromParsingName(g_File, NULL, IID_PPV_ARG(IShellItem, &Item));
    ok_hr(hr, S_OK);
    if (FAILED(hr) || FAILED(Item->GetDisplayName(SIGDN_NORMALDISPLAY, &Display)))
    {
        CoTaskMemFree(Recent);
        return;
    }
    wsprintfW(LinkPath, L"%s\\%s.lnk", Recent, Display);
    trace("expected recent link %ls\n", LinkPath);
    CoTaskMemFree(Display);
    CoTaskMemFree(Recent);
    DeleteFileW(LinkPath);

    SHAddToRecentDocs(SHARD_PATHW, g_File);
    ok(WaitForFile(LinkPath, 5000), "no recent link %ls\n", LinkPath);

    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IShellLinkW, &Link));
    if (SUCCEEDED(hr) && SUCCEEDED(Link->QueryInterface(IID_PPV_ARG(IPersistFile, &Persist))) &&
        SUCCEEDED(Persist->Load(LinkPath, STGM_READ)))
    {
        ok_hr(Link->GetPath(Target, _countof(Target), NULL, 0), S_OK);
        ok(!_wcsicmp(Target, g_File), "link target %ls\n", Target);
    }
    else
    {
        ok(FALSE, "cannot load recent link: %#lx\n", hr);
    }
    DeleteFileW(LinkPath);
}

static void
TestDocumentLists(void)
{
    CComPtr<IApplicationDocumentLists> Lists;
    CComPtr<IApplicationDestinations> Destinations;
    CComPtr<IObjectArray> Recent;
    CComPtr<IShellItem> Item;
    SHARDAPPIDINFO Info;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_ApplicationDocumentLists, NULL, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARG(IApplicationDocumentLists, &Lists));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    ok_hr(Lists->SetAppID(g_AppId), S_OK);

    hr = SHCreateItemFromParsingName(g_File, NULL, IID_PPV_ARG(IShellItem, &Item));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    Info.psi = Item;
    Info.pszAppID = g_AppId;
    SHAddToRecentDocs(SHARD_APPIDINFO, &Info);

    Sleep(1000);
    hr = Lists->GetList(ADLT_RECENT, 0, IID_PPV_ARG(IObjectArray, &Recent));
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        ok(!ListContains(Recent, g_File), "the list of an app not registered for .txt holds the document\n");
    Recent.Release();

    hr = Lists->GetList(ADLT_FREQUENT, 0, IID_PPV_ARG(IObjectArray, &Recent));
    ok_hr(hr, S_OK);
    Recent.Release();

    hr = CoCreateInstance(CLSID_ApplicationDestinations, NULL, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARG(IApplicationDestinations, &Destinations));
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    ok_hr(Destinations->SetAppID(g_AppId), S_OK);
    hr = Destinations->RemoveDestination(Item);
    ok_hr(hr, HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
    hr = Lists->GetList(ADLT_RECENT, 0, IID_PPV_ARG(IObjectArray, &Recent));
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
        ok(!ListContains(Recent, g_File), "removed document still listed\n");
    ok_hr(Destinations->RemoveAllDestinations(), S_OK);
}

static const WCHAR g_Advanced[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";

START_TEST(RecentDocs)
{
    WCHAR Temp[MAX_PATH];
    HANDLE File;
    DWORD Track = 0, OldTrack = 0, Size = sizeof(OldTrack);
    BOOL HadTrack;

    HadTrack = SHGetValueW(HKEY_CURRENT_USER, g_Advanced, L"Start_TrackDocs", NULL, &OldTrack, &Size) == ERROR_SUCCESS;
    trace("Start_TrackDocs %s %lu, NoRecentDocsHistory %lu\n", HadTrack ? "set" : "unset", OldTrack,
          SHRestricted(REST_NORECENTDOCSHISTORY));
    Track = 1;
    SHSetValueW(HKEY_CURRENT_USER, g_Advanced, L"Start_TrackDocs", REG_DWORD, &Track, sizeof(Track));
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok_hr(SetCurrentProcessExplicitAppUserModelID(g_AppId), S_OK);
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, Temp)))
        GetTempPathW(_countof(Temp), Temp);
    wsprintfW(g_Name, L"recentdoc%lu.txt", GetCurrentProcessId());
    wsprintfW(g_File, L"%s\\%s", Temp, g_Name);
    File = CreateFileW(g_File, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        skip("cannot create %ls\n", g_File);
        CoUninitialize();
        return;
    }
    CloseHandle(File);

    TestRecentFolder();
    TestDocumentLists();

    DeleteFileW(g_File);
    CoUninitialize();
    if (HadTrack)
        SHSetValueW(HKEY_CURRENT_USER, g_Advanced, L"Start_TrackDocs", REG_DWORD, &OldTrack, sizeof(OldTrack));
    else
        SHDeleteValueW(HKEY_CURRENT_USER, g_Advanced, L"Start_TrackDocs");
}
