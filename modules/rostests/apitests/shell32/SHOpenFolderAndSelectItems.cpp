/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SHOpenFolderAndSelectItems
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <tlhelp32.h>

struct FOLDER_WINDOWS
{
    HWND Windows[64];
    UINT Count;
};

static ULONG
CountExplorerProcesses(VOID)
{
    PROCESSENTRY32W Entry = { sizeof(Entry) };
    HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    ULONG Count = 0;

    if (Snapshot == INVALID_HANDLE_VALUE)
        return 0;
    if (Process32FirstW(Snapshot, &Entry))
    {
        do
        {
            if (!_wcsicmp(Entry.szExeFile, L"explorer.exe"))
                Count++;
        } while (Process32NextW(Snapshot, &Entry));
    }
    CloseHandle(Snapshot);
    return Count;
}

static BOOL CALLBACK
CollectFolderWindow(HWND hwnd, LPARAM lParam)
{
    FOLDER_WINDOWS *List = reinterpret_cast<FOLDER_WINDOWS *>(lParam);
    WCHAR szClass[32];

    if (List->Count < _countof(List->Windows) && IsWindowVisible(hwnd) &&
        GetClassNameW(hwnd, szClass, _countof(szClass)) && !wcscmp(szClass, L"CabinetWClass"))
    {
        List->Windows[List->Count++] = hwnd;
    }
    return TRUE;
}

static HWND
FindNewFolderWindow(const FOLDER_WINDOWS *Before)
{
    FOLDER_WINDOWS After = { { NULL }, 0 };

    EnumWindows(CollectFolderWindow, reinterpret_cast<LPARAM>(&After));
    for (UINT i = 0; i < After.Count; i++)
    {
        UINT j;

        for (j = 0; j < Before->Count && Before->Windows[j] != After.Windows[i]; j++)
            ;
        if (j == Before->Count)
            return After.Windows[i];
    }
    return NULL;
}

static VOID
TestOpen(PCWSTR Path, UINT cidl, PCWSTR Child, DWORD ShellProcess)
{
    FOLDER_WINDOWS Before = { { NULL }, 0 };
    PIDLIST_ABSOLUTE Folder, Item = NULL;
    PCUITEMID_CHILD Items[1];
    ULONG Explorers;
    DWORD Owner = 0;
    HRESULT hr;
    HWND hwnd;

    Folder = ILCreateFromPathW(Path);
    ok(Folder != NULL, "%ls: no pidl\n", Path);
    if (!Folder)
        return;
    if (cidl)
    {
        Item = ILCreateFromPathW(Child);
        ok(Item != NULL, "%ls: no pidl\n", Child);
        if (!Item)
        {
            ILFree(Folder);
            return;
        }
        Items[0] = ILFindLastID(Item);
    }

    Explorers = CountExplorerProcesses();
    EnumWindows(CollectFolderWindow, reinterpret_cast<LPARAM>(&Before));
    hr = SHOpenFolderAndSelectItems(Folder, cidl, cidl ? Items : NULL, 0);
    ok(hr == S_OK, "%ls: hr 0x%lx\n", Path, hr);
    hwnd = FindNewFolderWindow(&Before);
    ok(hwnd != NULL, "%ls: no visible folder window when the call returned\n", Path);
    ok(CountExplorerProcesses() == Explorers, "%ls: explorer.exe count %lu, before %lu\n",
       Path, CountExplorerProcesses(), Explorers);
    if (hwnd)
    {
        GetWindowThreadProcessId(hwnd, &Owner);
        ok(Owner == ShellProcess, "%ls: window process %lu, shell process %lu\n", Path, Owner, ShellProcess);
        SendMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
    }

    if (Item)
        ILFree(Item);
    ILFree(Folder);
}

START_TEST(SHOpenFolderAndSelectItems)
{
    WCHAR WindowsDir[MAX_PATH], SystemDir[MAX_PATH];
    FOLDER_WINDOWS Before = { { NULL }, 0 };
    PIDLIST_ABSOLUTE Folder;
    DWORD ShellProcess = 0;
    HRESULT hr;
    HWND Shell;

    GetWindowsDirectoryW(WindowsDir, _countof(WindowsDir));
    GetSystemDirectoryW(SystemDir, _countof(SystemDir));

    hr = SHOpenFolderAndSelectItems(NULL, 0, NULL, 0);
    ok(hr == E_INVALIDARG, "hr 0x%lx\n", hr);

    Folder = ILCreateFromPathW(SystemDir);
    ok(Folder != NULL, "%ls: no pidl\n", SystemDir);
    if (Folder)
    {
        EnumWindows(CollectFolderWindow, reinterpret_cast<LPARAM>(&Before));
        hr = SHOpenFolderAndSelectItems(Folder, 0, NULL, 0);
        ok(hr == CO_E_NOTINITIALIZED, "without COM: hr 0x%lx\n", hr);
        ok(FindNewFolderWindow(&Before) == NULL, "without COM: a folder window was opened\n");
        ILFree(Folder);
    }

    Shell = GetShellWindow();
    if (!Shell)
    {
        skip("No shell window\n");
        return;
    }
    GetWindowThreadProcessId(Shell, &ShellProcess);

    hr = CoInitialize(NULL);
    ok(hr == S_OK, "CoInitialize: hr 0x%lx\n", hr);
    TestOpen(SystemDir, 0, NULL, ShellProcess);
    TestOpen(WindowsDir, 1, SystemDir, ShellProcess);
    CoUninitialize();
}
