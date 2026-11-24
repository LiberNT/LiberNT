/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests installing and uninstalling a package through the lntget catalog window
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <strsafe.h>
#include <string.h>

#define GUI_TIMEOUT (5 * 60 * 1000)
#define ID_INSTALL 1006
#define ID_LIST 1002
#define ID_OPERATION_TITLE 1011
#define ID_INSTALLED 1015
#define ID_INSTALLED_LIST 2001
#define ID_INSTALLED_STATUS 2002
#define ID_UNINSTALL 2003

typedef struct _FIND_WINDOW
{
    DWORD ProcessId;
    PCWSTR ClassName;
    HWND Owner;
    HWND Found;
} FIND_WINDOW;

static PCWSTR
GetTool(void)
{
    static WCHAR Tool[MAX_PATH];

    if (!Tool[0] && !GetEnvironmentVariableW(L"LNTGET_TOOL", Tool, _countof(Tool)))
        wcscpy(Tool, L"lntget.exe");
    return Tool;
}

static BOOL
RunTool(PCWSTR Arguments, DWORD *ExitCode)
{
    SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
    WCHAR CommandLine[1024];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    BOOL Started;

    *ExitCode = 0xFFFFFFFF;
    StringCchPrintfW(CommandLine, _countof(CommandLine), L"\"%s\" %s", GetTool(), Arguments);
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = Startup.hStdOutput = Startup.hStdError =
        CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &Inherit, OPEN_EXISTING, 0, NULL);
    Started = CreateProcessW(NULL, CommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Process);
    if (Startup.hStdOutput != INVALID_HANDLE_VALUE)
        CloseHandle(Startup.hStdOutput);
    if (!Started)
        return FALSE;
    if (WaitForSingleObject(Process.hProcess, GUI_TIMEOUT) != WAIT_OBJECT_0)
        TerminateProcess(Process.hProcess, ERROR_TIMEOUT);
    GetExitCodeProcess(Process.hProcess, ExitCode);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return TRUE;
}

static BOOL
JqInstalled(void)
{
    WCHAR Path[MAX_PATH], SubKey[256], Name[256];
    DWORD Index, Length, Size;
    HKEY Uninstall;
    BOOL Found = FALSE;

    ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Microsoft\\WinGet\\Links\\jq.exe", Path, _countof(Path));
    if (GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES)
        return TRUE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ, &Uninstall) != ERROR_SUCCESS)
        return FALSE;
    for (Index = 0; !Found; Index++)
    {
        Length = _countof(SubKey);
        if (RegEnumKeyExW(Uninstall, Index, SubKey, &Length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        Size = sizeof(Name);
        Found = RegGetValueW(Uninstall, SubKey, L"DisplayName", RRF_RT_REG_SZ, NULL, Name, &Size) == ERROR_SUCCESS &&
                !wcscmp(Name, L"jq");
    }
    RegCloseKey(Uninstall);
    return Found;
}

static BOOL
ArpViewHasPrefix(HKEY Root, REGSAM View, PCWSTR Prefix)
{
    WCHAR SubKey[256], Name[512];
    DWORD Index, Length, Size;
    HKEY Uninstall;
    BOOL Found = FALSE;

    if (RegOpenKeyExW(Root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | View, &Uninstall) != ERROR_SUCCESS)
        return FALSE;
    for (Index = 0; !Found; Index++)
    {
        Length = _countof(SubKey);
        if (RegEnumKeyExW(Uninstall, Index, SubKey, &Length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        Size = sizeof(Name);
        Found = RegGetValueW(Uninstall, SubKey, L"DisplayName", RRF_RT_REG_SZ, NULL, Name, &Size) == ERROR_SUCCESS &&
                !wcsncmp(Name, Prefix, wcslen(Prefix));
    }
    RegCloseKey(Uninstall);
    return Found;
}

static BOOL
SevenZipInstalled(void)
{
    return ArpViewHasPrefix(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, L"7-Zip") ||
           ArpViewHasPrefix(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, L"7-Zip") ||
           ArpViewHasPrefix(HKEY_CURRENT_USER, 0, L"7-Zip");
}

static DWORD
WaitForChild(DWORD ParentId, PCWSTR Name, DWORD Timeout)
{
    PROCESSENTRY32W Entry;
    DWORD Start = GetTickCount(), Found = 0;
    HANDLE Snapshot;
    BOOL More;

    while (!Found && GetTickCount() - Start <= Timeout)
    {
        Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (Snapshot == INVALID_HANDLE_VALUE)
            return 0;
        Entry.dwSize = sizeof(Entry);
        for (More = Process32FirstW(Snapshot, &Entry); More && !Found; More = Process32NextW(Snapshot, &Entry))
        {
            if (Entry.th32ParentProcessID == ParentId && !_wcsicmp(Entry.szExeFile, Name))
                Found = Entry.th32ProcessID;
        }
        CloseHandle(Snapshot);
        if (!Found)
            Sleep(5);
    }
    return Found;
}

static ULONG
SuspendProcessThreads(DWORD ProcessId, BOOL Suspend)
{
    THREADENTRY32 Entry;
    HANDLE Snapshot, Thread;
    ULONG Count = 0;
    BOOL More;

    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
        return 0;
    Entry.dwSize = sizeof(Entry);
    for (More = Thread32First(Snapshot, &Entry); More; More = Thread32Next(Snapshot, &Entry))
    {
        if (Entry.th32OwnerProcessID != ProcessId)
            continue;
        Thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, Entry.th32ThreadID);
        if (!Thread)
            continue;
        if ((Suspend ? SuspendThread(Thread) : ResumeThread(Thread)) != (DWORD)-1)
            Count++;
        CloseHandle(Thread);
    }
    CloseHandle(Snapshot);
    return Count;
}

static BOOL CALLBACK
FindWindowProc(HWND Window, LPARAM Parameter)
{
    FIND_WINDOW *Find = (FIND_WINDOW *)Parameter;
    WCHAR ClassName[64];
    DWORD ProcessId = 0;

    GetWindowThreadProcessId(Window, &ProcessId);
    if (ProcessId != Find->ProcessId || !IsWindowVisible(Window) ||
        !GetClassNameW(Window, ClassName, _countof(ClassName)) || wcscmp(ClassName, Find->ClassName) ||
        (Find->Owner && GetWindow(Window, GW_OWNER) != Find->Owner))
        return TRUE;
    Find->Found = Window;
    return FALSE;
}

static HWND
WaitForWindow(HANDLE Process, PCWSTR ClassName, HWND Owner, DWORD Timeout)
{
    FIND_WINDOW Find = { GetProcessId(Process), ClassName, Owner, NULL };
    DWORD Start = GetTickCount();

    for (;;)
    {
        EnumWindows(FindWindowProc, (LPARAM)&Find);
        if (Find.Found || GetTickCount() - Start > Timeout || WaitForSingleObject(Process, 0) == WAIT_OBJECT_0)
            return Find.Found;
        Sleep(200);
    }
}

static BOOL
WaitForText(HWND Window, PCWSTR Text, PWSTR Buffer, int BufferLength, DWORD Timeout)
{
    DWORD Start = GetTickCount();

    for (;;)
    {
        Buffer[0] = L'\0';
        GetWindowTextW(Window, Buffer, BufferLength);
        if (wcsstr(Buffer, Text))
            return TRUE;
        if (GetTickCount() - Start > Timeout || !IsWindow(Window))
            return FALSE;
        Sleep(200);
    }
}

static BOOL
WaitForEnabled(HWND Window, DWORD Timeout)
{
    DWORD Start = GetTickCount();

    while (!IsWindowEnabled(Window))
    {
        if (GetTickCount() - Start > Timeout)
            return FALSE;
        Sleep(200);
    }
    return TRUE;
}

static BOOL
WaitForInstalledState(BOOL Installed, DWORD Timeout)
{
    DWORD Start = GetTickCount();

    while (JqInstalled() != Installed)
    {
        if (GetTickCount() - Start > Timeout)
            return FALSE;
        Sleep(500);
    }
    return TRUE;
}

static BOOL
ConfirmMessageBox(HANDLE Process, HWND Owner)
{
    HWND Box = WaitForWindow(Process, L"#32770", Owner, 30000);

    ok(Box != NULL, "no message box owned by %p\n", Owner);
    if (!Box)
        return FALSE;
    PostMessageW(Box, WM_COMMAND, IDOK, 0);
    return TRUE;
}

static int
SelectInstalledRow(HANDLE Process, HWND List, PCWSTR Name, BOOL Prefix)
{
    struct
    {
        LVFINDINFOW Find;
        LVITEMW Item;
        WCHAR Text[64];
    } Local, *Remote;
    int Row = -1;

    ZeroMemory(&Local, sizeof(Local));
    Remote = VirtualAllocEx(Process, NULL, sizeof(Local), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Remote != NULL, "VirtualAllocEx failed with %lu\n", GetLastError());
    if (!Remote)
        return -1;
    StringCchCopyW(Local.Text, _countof(Local.Text), Name);
    Local.Find.flags = Prefix ? LVFI_PARTIAL | LVFI_STRING : LVFI_STRING;
    Local.Find.psz = Remote->Text;
    Local.Item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    Local.Item.state = LVIS_SELECTED | LVIS_FOCUSED;
    if (WriteProcessMemory(Process, Remote, &Local, sizeof(Local), NULL))
    {
        Row = (int)SendMessageW(List, LVM_FINDITEMW, (WPARAM)-1, (LPARAM)&Remote->Find);
        if (Row >= 0)
            SendMessageW(List, LVM_SETITEMSTATE, Row, (LPARAM)&Remote->Item);
    }
    VirtualFreeEx(Process, Remote, 0, MEM_RELEASE);
    return Row;
}

static void
UninstallFromInstalledApps(HANDLE Process, HWND Catalog)
{
    HWND Installed, List, Uninstall, Status;
    WCHAR Text[256];
    int Row;

    PostMessageW(Catalog, WM_COMMAND, ID_INSTALLED, 0);
    Installed = WaitForWindow(Process, L"LntGetInstalledApps", Catalog, 30000);
    ok(Installed != NULL, "the installed apps window did not open\n");
    if (!Installed)
        return;
    ok(!IsWindowEnabled(Catalog), "the catalog window stays enabled under the installed apps window\n");
    List = GetDlgItem(Installed, ID_INSTALLED_LIST);
    Uninstall = GetDlgItem(Installed, ID_UNINSTALL);
    Status = GetDlgItem(Installed, ID_INSTALLED_STATUS);
    ok(!IsWindowEnabled(Uninstall), "Uninstall is enabled with nothing selected\n");

    Row = SelectInstalledRow(Process, List, L"jq", FALSE);
    ok(Row >= 0, "jq is not in the installed apps list\n");
    if (Row < 0)
        goto Close;
    ok(SendMessageW(List, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED) == Row, "jq is not selected\n");
    ok(WaitForEnabled(Uninstall, 5000), "Uninstall stays disabled with jq selected\n");

    PostMessageW(Uninstall, BM_CLICK, 0, 0);
    if (!ConfirmMessageBox(Process, Installed))
        goto Close;
    ok(WaitForText(Status, L"jq was uninstalled.", Text, _countof(Text), GUI_TIMEOUT), "status is '%ls'\n", Text);
    ok(WaitForInstalledState(FALSE, 20000), "jq is still installed after the window uninstalled it\n");
    ok(SelectInstalledRow(Process, List, L"jq", FALSE) < 0, "jq is still listed after the uninstall\n");

Close:
    PostMessageW(Installed, WM_CLOSE, 0, 0);
    ok(WaitForSingleObject(Process, 0) == WAIT_TIMEOUT, "lntget exited when the installed apps window closed\n");
}

START_TEST(gui)
{
    SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
    WCHAR CommandLine[1024], OutputPath[MAX_PATH], Directory[MAX_PATH], Text[256];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    HWND Catalog, Notice, List, Install, Title;
    HANDLE File;
    DWORD ExitCode, Read = 0;
    char Output[4096];

    if (wcsstr(GetTool(), L"winget"))
    {
        skip("winget has no catalog window\n");
        return;
    }
    if (JqInstalled())
    {
        skip("jq is already installed\n");
        return;
    }
    if (!RunTool(L"search --id jqlang.jq -e --source winget", &ExitCode) || ExitCode != 0)
    {
        skip("the winget source is unavailable: 0x%08lx\n", ExitCode);
        return;
    }

    GetTempPathW(_countof(Directory), Directory);
    GetTempFileNameW(Directory, L"lng", 0, OutputPath);
    File = CreateFileW(OutputPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &Inherit,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(File != INVALID_HANDLE_VALUE, "cannot create %ls: %lu\n", OutputPath, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    StringCchPrintfW(CommandLine, _countof(CommandLine), L"\"%s\" gui jqlang.jq", GetTool());
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdOutput = File;
    Startup.hStdError = File;
    if (!CreateProcessW(NULL, CommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Process))
    {
        ok(0, "CreateProcess(%ls) failed with %lu\n", CommandLine, GetLastError());
        CloseHandle(File);
        return;
    }
    CloseHandle(Process.hThread);

    Catalog = WaitForWindow(Process.hProcess, L"LntGetCatalog", NULL, 60000);
    ok(Catalog != NULL, "the catalog window did not open\n");
    if (!Catalog)
        goto Exit;
    Notice = WaitForWindow(Process.hProcess, L"#32770", Catalog, 30000);
    ok(Notice != NULL, "the compatibility notice did not open\n");
    if (Notice)
        PostMessageW(Notice, WM_COMMAND, IDOK, 0);

    ok(WaitForText(Catalog, L" packages)", Text, _countof(Text), GUI_TIMEOUT), "catalog title is '%ls'\n", Text);
    List = GetDlgItem(Catalog, ID_LIST);
    ok(SendMessageW(List, LVM_GETITEMCOUNT, 0, 0) >= 1, "the search for jqlang.jq lists nothing\n");
    ok(SendMessageW(List, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED) == 0, "the first match is not selected\n");

    Install = GetDlgItem(Catalog, ID_INSTALL);
    ok(WaitForEnabled(Install, 30000), "Install stays disabled\n");
    PostMessageW(Install, BM_CLICK, 0, 0);
    Title = GetDlgItem(Catalog, ID_OPERATION_TITLE);
    ok(WaitForText(Title, L"Installation complete", Text, _countof(Text), GUI_TIMEOUT), "operation title is '%ls'\n", Text);
    ok(WaitForInstalledState(TRUE, 5000), "jq is not installed after the window installed it\n");
    ok(IsWindowEnabled(Install), "Install stays disabled after the installation\n");

    if (JqInstalled())
        UninstallFromInstalledApps(Process.hProcess, Catalog);

    PostMessageW(Catalog, WM_CLOSE, 0, 0);

Exit:
    ok(WaitForSingleObject(Process.hProcess, 30000) == WAIT_OBJECT_0, "lntget did not exit after its window closed\n");
    TerminateProcess(Process.hProcess, ERROR_TIMEOUT);
    GetExitCodeProcess(Process.hProcess, &ExitCode);
    ok(ExitCode == 0, "lntget gui returned 0x%08lx\n", ExitCode);
    CloseHandle(Process.hProcess);

    SetFilePointer(File, 0, NULL, FILE_BEGIN);
    ReadFile(File, Output, sizeof(Output) - 1, &Read, NULL);
    Output[Read] = '\0';
    CloseHandle(File);
    ok(strstr(Output, "LNTGET_GUI_INSTALL_DONE id=jqlang.jq") != NULL, "output:\n%s\n", Output);
    ok(strstr(Output, "LNTGET_GUI_UNINSTALL_DONE id=") != NULL, "output:\n%s\n", Output);

    if (JqInstalled() && RunTool(L"uninstall --id jqlang.jq -e --source winget --silent --disable-interactivity", &ExitCode))
        trace("cleanup uninstall returned 0x%08lx\n", ExitCode);
}

START_TEST(guiuninstall)
{
    SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
    WCHAR CommandLine[1024], OutputPath[MAX_PATH], Directory[MAX_PATH];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    HWND Catalog, Notice, Installed = NULL, List, Uninstall, Box;
    HANDLE File;
    DWORD ExitCode, Read = 0, Uninstaller = 0;
    char Output[4096];
    int Row;

    if (wcsstr(GetTool(), L"winget"))
    {
        skip("winget has no catalog window\n");
        return;
    }
    if (SevenZipInstalled())
    {
        skip("7-Zip is already installed\n");
        return;
    }
    if (!RunTool(L"install --id 7zip.7zip -e --source winget --installer-type wix --silent --disable-interactivity", &ExitCode) ||
        ExitCode != 0 || !SevenZipInstalled())
    {
        skip("cannot install 7-Zip: 0x%08lx\n", ExitCode);
        return;
    }

    GetTempPathW(_countof(Directory), Directory);
    GetTempFileNameW(Directory, L"lng", 0, OutputPath);
    File = CreateFileW(OutputPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &Inherit,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(File != INVALID_HANDLE_VALUE, "cannot create %ls: %lu\n", OutputPath, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        goto Cleanup;
    StringCchPrintfW(CommandLine, _countof(CommandLine), L"\"%s\" gui", GetTool());
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdOutput = File;
    Startup.hStdError = File;
    if (!CreateProcessW(NULL, CommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Process))
    {
        ok(0, "CreateProcess(%ls) failed with %lu\n", CommandLine, GetLastError());
        CloseHandle(File);
        goto Cleanup;
    }
    CloseHandle(Process.hThread);

    Catalog = WaitForWindow(Process.hProcess, L"LntGetCatalog", NULL, 60000);
    ok(Catalog != NULL, "the catalog window did not open\n");
    if (!Catalog)
        goto Exit;
    Notice = WaitForWindow(Process.hProcess, L"#32770", Catalog, 30000);
    ok(Notice != NULL, "the compatibility notice did not open\n");
    if (Notice)
        PostMessageW(Notice, WM_COMMAND, IDOK, 0);

    PostMessageW(Catalog, WM_COMMAND, ID_INSTALLED, 0);
    Installed = WaitForWindow(Process.hProcess, L"LntGetInstalledApps", Catalog, 30000);
    ok(Installed != NULL, "the installed apps window did not open\n");
    if (!Installed)
        goto Close;
    List = GetDlgItem(Installed, ID_INSTALLED_LIST);
    Uninstall = GetDlgItem(Installed, ID_UNINSTALL);
    Row = SelectInstalledRow(Process.hProcess, List, L"7-Zip", TRUE);
    ok(Row >= 0, "7-Zip is not in the installed apps list\n");
    if (Row < 0 || !WaitForEnabled(Uninstall, 5000))
        goto Close;
    PostMessageW(Uninstall, BM_CLICK, 0, 0);
    Box = WaitForWindow(Process.hProcess, L"#32770", Installed, 30000);
    ok(Box != NULL, "no confirmation owned by the installed apps window\n");
    if (!Box)
        goto Close;
    PostMessageW(Box, WM_COMMAND, IDOK, 0);
    Uninstaller = WaitForChild(GetProcessId(Process.hProcess), L"msiexec.exe", 30000);
    ok(Uninstaller != 0, "the window started no uninstaller\n");
    if (!Uninstaller || !SuspendProcessThreads(Uninstaller, TRUE))
    {
        skip("cannot hold the uninstaller\n");
        Uninstaller = 0;
        goto Close;
    }
    PostMessageW(Installed, WM_CLOSE, 0, 0);
    Sleep(1000);
    ok(IsWindow(Installed), "the installed apps window closed while 7-Zip was uninstalling\n");
    PostMessageW(Catalog, WM_CLOSE, 0, 0);
    ok(WaitForSingleObject(Process.hProcess, 3000) == WAIT_TIMEOUT, "lntget exited while its uninstall was running\n");
    SuspendProcessThreads(Uninstaller, FALSE);
    goto Exit;

Close:
    PostMessageW(Catalog, WM_CLOSE, 0, 0);

Exit:
    ok(WaitForSingleObject(Process.hProcess, GUI_TIMEOUT) == WAIT_OBJECT_0, "lntget did not exit after its window closed\n");
    TerminateProcess(Process.hProcess, ERROR_TIMEOUT);
    GetExitCodeProcess(Process.hProcess, &ExitCode);
    ok(ExitCode == 0, "lntget gui returned 0x%08lx\n", ExitCode);
    CloseHandle(Process.hProcess);

    SetFilePointer(File, 0, NULL, FILE_BEGIN);
    ReadFile(File, Output, sizeof(Output) - 1, &Read, NULL);
    Output[Read] = '\0';
    CloseHandle(File);
    if (Installed)
    {
        ok(strstr(Output, "LNTGET_GUI_UNINSTALL_DONE id=") != NULL, "lntget exited before the uninstall finished:\n%s\n", Output);
        ok(!SevenZipInstalled(), "7-Zip is still installed after the window uninstalled it\n");
    }

Cleanup:
    if (SevenZipInstalled() && RunTool(L"uninstall --id 7zip.7zip -e --source winget --silent --disable-interactivity", &ExitCode))
        trace("cleanup uninstall returned 0x%08lx\n", ExitCode);
}
