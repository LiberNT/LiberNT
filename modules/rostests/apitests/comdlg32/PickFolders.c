/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests how IFileOpenDialog with FOS_PICKFOLDERS accepts a typed folder name
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <strsafe.h>

typedef struct
{
    IFileDialogEvents IFileDialogEvents_iface;
    LONG ref;
    LPCWSTR name;
    UINT changes;
    LPWSTR navigated;
    UINT_PTR watchdog;
} EVENTS;

static HWND g_dialog;
static BOOL g_extra_window;

static EVENTS *impl_from_events(IFileDialogEvents *iface)
{
    return CONTAINING_RECORD(iface, EVENTS, IFileDialogEvents_iface);
}

static HRESULT WINAPI events_QueryInterface(IFileDialogEvents *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IFileDialogEvents))
    {
        *ppv = iface;
        IFileDialogEvents_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI events_AddRef(IFileDialogEvents *iface)
{
    return InterlockedIncrement(&impl_from_events(iface)->ref);
}

static ULONG WINAPI events_Release(IFileDialogEvents *iface)
{
    return InterlockedDecrement(&impl_from_events(iface)->ref);
}

static HRESULT WINAPI events_OnFileOk(IFileDialogEvents *iface, IFileDialog *pfd)
{
    return S_OK;
}

static HRESULT WINAPI events_OnFolderChanging(IFileDialogEvents *iface, IFileDialog *pfd, IShellItem *psi)
{
    return S_OK;
}

static BOOL CALLBACK close_extra_window(HWND hwnd, LPARAM lParam)
{
    if (hwnd != g_dialog && IsWindowVisible(hwnd))
    {
        g_extra_window = TRUE;
        PostMessageW(hwnd, WM_COMMAND, IDOK, 0);
    }
    return TRUE;
}

static VOID CALLBACK watchdog_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    EnumThreadWindows(GetCurrentThreadId(), close_extra_window, 0);
    if (g_dialog)
        PostMessageW(g_dialog, WM_COMMAND, IDCANCEL, 0);
}

static HRESULT WINAPI events_OnFolderChange(IFileDialogEvents *iface, IFileDialog *pfd)
{
    EVENTS *This = impl_from_events(iface);
    IOleWindow *pow;
    IShellItem *psi;
    HRESULT hr;

    if (++This->changes > 1)
    {
        if (This->changes == 2 && g_dialog)
        {
            hr = IFileDialog_GetFolder(pfd, &psi);
            ok_hex(hr, S_OK);
            if (SUCCEEDED(hr))
            {
                hr = IShellItem_GetDisplayName(psi, SIGDN_FILESYSPATH, &This->navigated);
                ok_hex(hr, S_OK);
                IShellItem_Release(psi);
            }
            ok_int(PostMessageW(g_dialog, WM_COMMAND, IDOK, 0), TRUE);
        }
        return S_OK;
    }

    hr = IFileDialog_QueryInterface(pfd, &IID_IOleWindow, (void **)&pow);
    ok_hex(hr, S_OK);
    if (FAILED(hr))
        return S_OK;
    hr = IOleWindow_GetWindow(pow, &g_dialog);
    ok_hex(hr, S_OK);
    IOleWindow_Release(pow);

    hr = IFileDialog_SetFileName(pfd, This->name);
    ok_hex(hr, S_OK);

    ok_int(PostMessageW(g_dialog, WM_COMMAND, IDOK, 0), TRUE);
    This->watchdog = SetTimer(NULL, 0, 3000, watchdog_proc);
    ok(This->watchdog != 0, "SetTimer failed: %lu\n", GetLastError());
    return S_OK;
}

static HRESULT WINAPI events_OnSelectionChange(IFileDialogEvents *iface, IFileDialog *pfd)
{
    return S_OK;
}

static HRESULT WINAPI events_OnShareViolation(IFileDialogEvents *iface, IFileDialog *pfd, IShellItem *psi,
                                              FDE_SHAREVIOLATION_RESPONSE *pResponse)
{
    return S_OK;
}

static HRESULT WINAPI events_OnTypeChange(IFileDialogEvents *iface, IFileDialog *pfd)
{
    return S_OK;
}

static HRESULT WINAPI events_OnOverwrite(IFileDialogEvents *iface, IFileDialog *pfd, IShellItem *psi,
                                         FDE_OVERWRITE_RESPONSE *pResponse)
{
    return S_OK;
}

static IFileDialogEventsVtbl events_vtbl =
{
    events_QueryInterface,
    events_AddRef,
    events_Release,
    events_OnFileOk,
    events_OnFolderChanging,
    events_OnFolderChange,
    events_OnSelectionChange,
    events_OnShareViolation,
    events_OnTypeChange,
    events_OnOverwrite,
};

static void test_pick_folder(LPCWSTR parent, LPCWSTR name, LPCWSTR expected)
{
    IFileOpenDialog *pfod;
    IShellItem *psi_parent, *psi;
    EVENTS events = { { &events_vtbl }, 1, name, 0, NULL, 0 };
    FILEOPENDIALOGOPTIONS fos;
    SFGAOF attrs;
    LPWSTR path;
    DWORD cookie;
    HRESULT hr;

    g_dialog = NULL;
    g_extra_window = FALSE;

    hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IFileOpenDialog, (void **)&pfod);
    ok_hex(hr, S_OK);
    if (FAILED(hr))
        return;

    hr = IFileOpenDialog_GetOptions(pfod, &fos);
    ok_hex(hr, S_OK);
    hr = IFileOpenDialog_SetOptions(pfod, fos | FOS_PICKFOLDERS);
    ok_hex(hr, S_OK);

    hr = SHCreateItemFromParsingName(parent, NULL, &IID_IShellItem, (void **)&psi_parent);
    ok_hex(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        hr = IFileOpenDialog_SetFolder(pfod, psi_parent);
        ok_hex(hr, S_OK);
        IShellItem_Release(psi_parent);
    }

    hr = IFileOpenDialog_Advise(pfod, &events.IFileDialogEvents_iface, &cookie);
    ok_hex(hr, S_OK);

    hr = IFileOpenDialog_Show(pfod, NULL);
    ok_hex(hr, S_OK);
    ok_int(g_extra_window, FALSE);
    todo_ros ok_int(events.changes, 2);
    if (events.navigated)
    {
        ok(!lstrcmpiW(events.navigated, expected), "Navigated to %ls, expected %ls\n", events.navigated, expected);
        CoTaskMemFree(events.navigated);
    }

    if (events.watchdog)
        KillTimer(NULL, events.watchdog);

    if (hr == S_OK)
    {
        hr = IFileOpenDialog_GetResult(pfod, &psi);
        ok_hex(hr, S_OK);
        if (SUCCEEDED(hr))
        {
            hr = IShellItem_GetDisplayName(psi, SIGDN_FILESYSPATH, &path);
            ok_hex(hr, S_OK);
            if (SUCCEEDED(hr))
            {
                ok(!lstrcmpiW(path, expected), "Got %ls, expected %ls\n", path, expected);
                CoTaskMemFree(path);
            }

            attrs = 0;
            hr = IShellItem_GetAttributes(psi, SFGAO_FOLDER | SFGAO_FILESYSTEM, &attrs);
            ok_hex(hr, S_OK);
            ok_hex(attrs, SFGAO_FOLDER | SFGAO_FILESYSTEM);
            IShellItem_Release(psi);
        }
    }

    hr = IFileOpenDialog_Unadvise(pfod, cookie);
    ok_hex(hr, S_OK);
    IFileOpenDialog_Release(pfod);
    ok_long(events.ref, 1);
}

START_TEST(PickFolders)
{
    WCHAR temp[MAX_PATH], parent[MAX_PATH], folder[MAX_PATH];
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok_hex(hr, S_OK);

    ok(GetTempPathW(_countof(temp), temp) != 0, "GetTempPathW failed: %lu\n", GetLastError());
    ok(GetLongPathNameW(temp, parent, _countof(parent)) != 0, "GetLongPathNameW failed: %lu\n", GetLastError());
    PathRemoveBackslashW(parent);
    StringCchPrintfW(folder, _countof(folder), L"%s\\PickFolders_apitest", parent);
    RemoveDirectoryW(folder);
    ok_int(CreateDirectoryW(folder, NULL), TRUE);

    test_pick_folder(parent, folder, folder);
    test_pick_folder(parent, L"PickFolders_apitest", folder);

    ok_int(RemoveDirectoryW(folder), TRUE);
    CoUninitialize();
}
