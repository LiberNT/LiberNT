/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the shell lists folders, resolves shortcuts, shows windows and fills file dialogs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include "appsmoke.h"

#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <dlgs.h>
#include <strsafe.h>

#define SHELL_WINDOW_BUDGET 20000
#define DIALOG_TICK 250
#define DIALOG_TICKS 60

typedef struct
{
    UINT Links;
    UINT Resolved;
} LINK_STATS;

typedef struct
{
    DWORD ProcessId;
    const WCHAR *Class;
    const HWND *Known;
    UINT KnownCount;
    HWND *Collected;
    UINT CollectedMax;
    UINT CollectedCount;
    HWND Found;
} WINDOW_SEARCH;

typedef struct
{
    BOOL Checked;
    UINT Ticks;
    UINT Items;
    UINT Places;
    WCHAR Folder[MAX_PATH];
} DIALOG_STATE;

static DIALOG_STATE g_Dialog;

static UINT CountItems(IShellFolder *Folder, UINT *Folders)
{
    IEnumIDList *Enum = NULL;
    LPITEMIDLIST Item;
    ULONG Attributes;
    UINT Count = 0;

    *Folders = 0;
    if (IShellFolder_EnumObjects(Folder, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN,
                                 &Enum) != S_OK || !Enum)
        return 0;

    while (IEnumIDList_Next(Enum, 1, &Item, NULL) == S_OK)
    {
        Attributes = SFGAO_FOLDER;
        if (SUCCEEDED(IShellFolder_GetAttributesOf(Folder, 1, (LPCITEMIDLIST *)&Item, &Attributes)) &&
            (Attributes & SFGAO_FOLDER))
            (*Folders)++;
        Count++;
        CoTaskMemFree(Item);
    }

    IEnumIDList_Release(Enum);
    return Count;
}

static void CheckLink(const WCHAR *File, LINK_STATS *Stats)
{
    WCHAR Raw[MAX_PATH] = L"", Target[MAX_PATH] = L"";
    IShellLinkW *Link = NULL;
    IPersistFile *Persist = NULL;
    DWORD Attributes;
    HRESULT Hr;

    Stats->Links++;
    Hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&Link);
    if (FAILED(Hr) || !Link)
        return;

    Hr = IShellLinkW_QueryInterface(Link, &IID_IPersistFile, (void **)&Persist);
    if (SUCCEEDED(Hr) && Persist)
    {
        Hr = IPersistFile_Load(Persist, File, STGM_READ);
        ok(Hr == S_OK, "Loading %ls returned %#lx\n", File, Hr);
        if (Hr == S_OK && IShellLinkW_GetPath(Link, Raw, ARRAYSIZE(Raw), NULL, SLGP_RAWPATH) == S_OK && Raw[0] &&
            ExpandEnvironmentStringsW(Raw, Target, ARRAYSIZE(Target)))
        {
            Attributes = GetFileAttributesW(Target);
            ok(Attributes != INVALID_FILE_ATTRIBUTES, "%ls points to %ls, which does not exist\n", File, Target);
            if (Attributes != INVALID_FILE_ATTRIBUTES)
                Stats->Resolved++;
        }
        IPersistFile_Release(Persist);
    }

    IShellLinkW_Release(Link);
}

static void ScanLinks(const WCHAR *Directory, UINT Depth, LINK_STATS *Stats)
{
    WCHAR Path[MAX_PATH];
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    if (Depth > 4 || FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\*", Directory)))
        return;

    Find = FindFirstFileW(Path, &Data);
    if (Find == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (!lstrcmpW(Data.cFileName, L".") || !lstrcmpW(Data.cFileName, L".."))
            continue;
        if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", Directory, Data.cFileName)))
            continue;
        if (Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            ScanLinks(Path, Depth + 1, Stats);
        else if (!lstrcmpiW(PathFindExtensionW(Data.cFileName), L".lnk"))
            CheckLink(Path, Stats);
    } while (FindNextFileW(Find, &Data));

    FindClose(Find);
}

static BOOL CALLBACK SearchWindowProc(HWND Window, LPARAM Param)
{
    WINDOW_SEARCH *Search = (WINDOW_SEARCH *)Param;
    WCHAR Class[64];
    DWORD ProcessId = 0;
    UINT Index;

    if (!IsWindowVisible(Window))
        return TRUE;

    GetWindowThreadProcessId(Window, &ProcessId);
    if (Search->ProcessId && ProcessId != Search->ProcessId)
        return TRUE;

    if (Search->Class && (!GetClassNameW(Window, Class, ARRAYSIZE(Class)) || lstrcmpiW(Class, Search->Class)))
        return TRUE;

    if (Search->Collected)
    {
        if (Search->CollectedCount < Search->CollectedMax)
            Search->Collected[Search->CollectedCount++] = Window;
        return TRUE;
    }

    for (Index = 0; Index < Search->KnownCount; Index++)
    {
        if (Search->Known[Index] == Window)
            return TRUE;
    }

    Search->Found = Window;
    return FALSE;
}

static HWND WaitWindow(WINDOW_SEARCH *Search, HANDLE Process, DWORD BudgetMs)
{
    DWORD Start = GetTickCount();

    for (;;)
    {
        Search->Found = NULL;
        EnumWindows(SearchWindowProc, (LPARAM)Search);
        if (Search->Found)
            return Search->Found;
        if (GetTickCount() - Start >= BudgetMs)
            return NULL;
        if (Process && WaitForSingleObject(Process, 100) == WAIT_OBJECT_0)
            return NULL;
        if (!Process)
            Sleep(100);
    }
}

static HWND FindDesktopList(void)
{
    HWND Host = GetShellWindow(), View, Worker = NULL;

    View = Host ? FindWindowExW(Host, NULL, L"SHELLDLL_DefView", NULL) : NULL;
    while (!View && (Worker = FindWindowExW(NULL, Worker, L"WorkerW", NULL)))
        View = FindWindowExW(Worker, NULL, L"SHELLDLL_DefView", NULL);

    return View ? FindWindowExW(View, NULL, L"SysListView32", NULL) : NULL;
}

static void CheckProgramWindow(const WCHAR *Name)
{
    WINDOW_SEARCH Search = { 0 };
    WCHAR Path[MAX_PATH];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    HWND Window;

    if (!SearchPathW(NULL, Name, NULL, ARRAYSIZE(Path), Path, NULL))
    {
        skip("%ls is not installed\n", Name);
        return;
    }

    if (!CreateProcessW(Path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        if (GetLastError() == ERROR_ELEVATION_REQUIRED)
            skip("%ls needs elevation\n", Name);
        else
            ok(FALSE, "Starting %ls failed with error %lu\n", Path, GetLastError());
        return;
    }

    Search.ProcessId = Process.dwProcessId;
    Window = WaitWindow(&Search, Process.hProcess, SHELL_WINDOW_BUDGET);
    ok(Window != NULL, "%ls started but showed no window\n", Name);
    if (Window)
        PostMessageW(Window, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(Process.hProcess, 3000) != WAIT_OBJECT_0)
        TerminateProcess(Process.hProcess, 0);

    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
}

static void CheckFolderWindow(void)
{
    HWND Known[64], Window;
    WINDOW_SEARCH Search = { 0 };
    WCHAR WinDir[MAX_PATH], Command[2 * MAX_PATH + 16];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;

    GetWindowsDirectoryW(WinDir, ARRAYSIZE(WinDir));
    StringCchPrintfW(Command, ARRAYSIZE(Command), L"\"%s\\explorer.exe\" \"%s\"", WinDir, WinDir);

    Search.Class = L"CabinetWClass";
    Search.Collected = Known;
    Search.CollectedMax = ARRAYSIZE(Known);
    EnumWindows(SearchWindowProc, (LPARAM)&Search);
    Search.Collected = NULL;
    Search.Known = Known;
    Search.KnownCount = Search.CollectedCount;

    if (!CreateProcessW(NULL, Command, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        ok(FALSE, "Starting %ls failed with error %lu\n", Command, GetLastError());
        return;
    }

    Window = WaitWindow(&Search, NULL, SHELL_WINDOW_BUDGET);
    ok(Window != NULL, "Opening %ls showed no folder window\n", WinDir);
    if (Window)
        PostMessageW(Window, WM_CLOSE, 0, 0);

    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
}

static BOOL QueryDialogItems(HWND Dialog, UINT *Items)
{
    IShellBrowser *Browser = (IShellBrowser *)SendMessageW(Dialog, WM_USER + 7, 0, 0);
    IShellView *View = NULL;
    IFolderView *FolderView = NULL;
    HWND List;
    int Count = 0;
    BOOL Known = FALSE;

    if (Browser && IShellBrowser_QueryActiveShellView(Browser, &View) == S_OK && View)
    {
        if (IShellView_QueryInterface(View, &IID_IFolderView, (void **)&FolderView) == S_OK && FolderView)
        {
            Known = IFolderView_ItemCount(FolderView, SVGIO_ALLVIEW, &Count) == S_OK;
            IFolderView_Release(FolderView);
        }
        IShellView_Release(View);
    }

    if (!Known)
    {
        List = FindWindowExW(Dialog, NULL, L"SHELLDLL_DefView", NULL);
        List = List ? FindWindowExW(List, NULL, L"SysListView32", NULL) : NULL;
        if (List)
        {
            Count = (int)SendMessageW(List, LVM_GETITEMCOUNT, 0, 0);
            Known = TRUE;
        }
    }

    *Items = Count > 0 ? Count : 0;
    return Known;
}

static UINT_PTR CALLBACK DialogHook(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    HWND Dialog = GetParent(Window);
    UINT Items = 0;

    UNREFERENCED_PARAMETER(lParam);

    if (Message == WM_INITDIALOG)
    {
        SetTimer(Window, 1, DIALOG_TICK, NULL);
        return 0;
    }

    if (Message != WM_TIMER || wParam != 1 || g_Dialog.Checked)
        return 0;

    g_Dialog.Ticks++;
    if (!(QueryDialogItems(Dialog, &Items) && Items > 0) && g_Dialog.Ticks < DIALOG_TICKS)
        return 0;

    KillTimer(Window, 1);
    g_Dialog.Checked = TRUE;
    g_Dialog.Items = Items;
    g_Dialog.Places = (UINT)SendDlgItemMessageW(Dialog, cmb2, CB_GETCOUNT, 0, 0);
    SendMessageW(Dialog, CDM_GETFOLDERPATH, ARRAYSIZE(g_Dialog.Folder), (LPARAM)g_Dialog.Folder);
    PostMessageW(Dialog, WM_COMMAND, IDCANCEL, 0);
    return 0;
}

START_TEST(shell_namespace)
{
    static const struct
    {
        const CLSID *Clsid;
        const char *Name;
    } Classes[] =
    {
        { &CLSID_ShellDesktop, "CLSID_ShellDesktop" },
        { &CLSID_ShellFSFolder, "CLSID_ShellFSFolder" },
        { &CLSID_MyComputer, "CLSID_MyComputer" },
        { &CLSID_ShellLink, "CLSID_ShellLink" },
        { &CLSID_QueryAssociations, "CLSID_QueryAssociations" },
    };
    WCHAR WinDir[MAX_PATH], Path[MAX_PATH];
    IShellFolder *Desktop = NULL, *Folder;
    IUnknown *Unknown;
    LPITEMIDLIST Pidl;
    UINT Index, Count, Folders;
    HRESULT Hr;

    Hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(SUCCEEDED(Hr), "CoInitializeEx returned %#lx\n", Hr);
    if (FAILED(Hr))
        return;

    for (Index = 0; Index < ARRAYSIZE(Classes); Index++)
    {
        Unknown = NULL;
        Hr = CoCreateInstance(Classes[Index].Clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&Unknown);
        ok(Hr == S_OK, "CoCreateInstance(%s) returned %#lx\n", Classes[Index].Name, Hr);
        if (Unknown)
            IUnknown_Release(Unknown);
    }

    Hr = SHGetDesktopFolder(&Desktop);
    ok(Hr == S_OK, "SHGetDesktopFolder returned %#lx\n", Hr);
    if (FAILED(Hr) || !Desktop)
    {
        CoUninitialize();
        return;
    }

    Count = CountItems(Desktop, &Folders);
    ok(Count > 0 && Folders > 0, "The desktop folder lists %u items, %u of them folders\n", Count, Folders);

    GetWindowsDirectoryW(WinDir, ARRAYSIZE(WinDir));
    Pidl = NULL;
    Hr = IShellFolder_ParseDisplayName(Desktop, NULL, NULL, WinDir, NULL, &Pidl, NULL);
    ok(Hr == S_OK, "ParseDisplayName(%ls) returned %#lx\n", WinDir, Hr);
    if (Hr == S_OK && Pidl)
    {
        Path[0] = UNICODE_NULL;
        ok(SHGetPathFromIDListW(Pidl, Path) && !lstrcmpiW(Path, WinDir), "The parsed item names %ls\n", Path);

        Folder = NULL;
        Hr = IShellFolder_BindToObject(Desktop, Pidl, NULL, &IID_IShellFolder, (void **)&Folder);
        ok(Hr == S_OK, "BindToObject(%ls) returned %#lx\n", WinDir, Hr);
        if (Hr == S_OK && Folder)
        {
            Count = CountItems(Folder, &Folders);
            ok(Count > 0 && Folders > 0, "%ls lists %u items, %u of them folders\n", WinDir, Count, Folders);
            IShellFolder_Release(Folder);
        }
        CoTaskMemFree(Pidl);
    }

    Pidl = NULL;
    Hr = SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &Pidl);
    ok(Hr == S_OK, "SHGetSpecialFolderLocation(CSIDL_DRIVES) returned %#lx\n", Hr);
    if (Hr == S_OK && Pidl)
    {
        Folder = NULL;
        Hr = IShellFolder_BindToObject(Desktop, Pidl, NULL, &IID_IShellFolder, (void **)&Folder);
        ok(Hr == S_OK, "BindToObject(CSIDL_DRIVES) returned %#lx\n", Hr);
        if (Hr == S_OK && Folder)
        {
            Count = CountItems(Folder, &Folders);
            ok(Count > 0, "The computer folder lists no item\n");
            IShellFolder_Release(Folder);
        }
        CoTaskMemFree(Pidl);
    }

    IShellFolder_Release(Desktop);
    CoUninitialize();
}

START_TEST(shell_links)
{
    static const struct
    {
        int Id;
        const char *Name;
        BOOL Programs;
    } Folders[] =
    {
        { CSIDL_COMMON_PROGRAMS, "CSIDL_COMMON_PROGRAMS", TRUE },
        { CSIDL_PROGRAMS, "CSIDL_PROGRAMS", TRUE },
        { CSIDL_COMMON_DESKTOPDIRECTORY, "CSIDL_COMMON_DESKTOPDIRECTORY", FALSE },
        { CSIDL_DESKTOPDIRECTORY, "CSIDL_DESKTOPDIRECTORY", FALSE },
    };
    LINK_STATS Programs = { 0 }, Desktop = { 0 };
    WCHAR Path[MAX_PATH];
    DWORD Attributes;
    HRESULT Hr;
    UINT Index;

    Hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(SUCCEEDED(Hr), "CoInitializeEx returned %#lx\n", Hr);
    if (FAILED(Hr))
        return;

    for (Index = 0; Index < ARRAYSIZE(Folders); Index++)
    {
        Path[0] = UNICODE_NULL;
        Hr = SHGetFolderPathW(NULL, Folders[Index].Id, NULL, SHGFP_TYPE_CURRENT, Path);
        ok(Hr == S_OK, "SHGetFolderPathW(%s) returned %#lx\n", Folders[Index].Name, Hr);
        if (Hr != S_OK)
            continue;

        Attributes = GetFileAttributesW(Path);
        ok(Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY),
           "%s is %ls, which is not a directory\n", Folders[Index].Name, Path);
        ScanLinks(Path, 0, Folders[Index].Programs ? &Programs : &Desktop);
    }

    ok(Programs.Links > 0, "The Start menu has no program shortcut\n");
    ok(Programs.Resolved > 0, "None of the %u Start menu shortcuts points to a program\n", Programs.Links);
    trace("Start menu: %u shortcuts, %u with a program; desktop: %u shortcuts, %u with a program\n",
          Programs.Links, Programs.Resolved, Desktop.Links, Desktop.Resolved);

    CoUninitialize();
}

START_TEST(shell_windows)
{
    static const WCHAR *const Programs[] = { L"winver.exe", L"charmap.exe", L"regedit.exe" };
    DWORD_PTR Count = 0;
    HWND List;
    UINT Index;

    if (!FindWindowW(L"Shell_TrayWnd", NULL))
    {
        skip("No shell is running\n");
        return;
    }

    ok(GetShellWindow() != NULL, "The shell shows a taskbar but no desktop\n");
    List = FindDesktopList();
    ok(List != NULL, "The desktop has no icon view\n");
    if (List)
    {
        ok(SendMessageTimeoutW(List, LVM_GETITEMCOUNT, 0, 0, SMTO_ABORTIFHUNG, 5000, &Count) != 0,
           "The desktop icon view does not answer\n");
        ok(Count > 0, "The desktop shows no icon\n");
    }

    CheckFolderWindow();
    for (Index = 0; Index < ARRAYSIZE(Programs); Index++)
        CheckProgramWindow(Programs[Index]);
}

START_TEST(file_dialog)
{
    WCHAR File[MAX_PATH] = L"", WinDir[MAX_PATH];
    OPENFILENAMEW Open = { 0 };
    BOOL Chosen;
    DWORD Error;
    HRESULT Hr;

    Hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(SUCCEEDED(Hr), "CoInitializeEx returned %#lx\n", Hr);
    if (FAILED(Hr))
        return;

    GetWindowsDirectoryW(WinDir, ARRAYSIZE(WinDir));
    ZeroMemory(&g_Dialog, sizeof(g_Dialog));

    Open.lStructSize = sizeof(Open);
    Open.lpstrFilter = L"All files\0*.*\0";
    Open.lpstrFile = File;
    Open.nMaxFile = ARRAYSIZE(File);
    Open.lpstrInitialDir = WinDir;
    Open.Flags = OFN_EXPLORER | OFN_ENABLEHOOK | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    Open.lpfnHook = DialogHook;

    Chosen = GetOpenFileNameW(&Open);
    Error = CommDlgExtendedError();
    ok(!Chosen && Error == 0, "GetOpenFileNameW returned %d with error %#lx\n", Chosen, Error);
    ok(g_Dialog.Checked, "The dialog never reached its hook\n");
    if (g_Dialog.Checked)
    {
        ok(!lstrcmpiW(g_Dialog.Folder, WinDir), "The dialog shows %ls\n", g_Dialog.Folder);
        ok(g_Dialog.Items > 0, "The dialog lists no file of %ls\n", WinDir);
        ok(g_Dialog.Places > 0, "The Look in list is empty\n");
    }

    CoUninitialize();
}
