/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux terminal: the login shell of the Linux root on a pseudo-terminal
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windef.h>
#include <winbase.h>
#include <wincon.h>
#include <winuser.h>
#define COBJMACROS
#include <shlobj.h>
#include <propsys.h>
#include <initguid.h>
#include <propkey.h>

#include "../include/lxioctl.h"
#include "../include/lxlaunch.h"
#include "../include/lxappid.h"
#include "lxinstall.h"

#define LX_USER_GROUPS_MAX 3

static
VOID
LxUsage(VOID)
{
    fwprintf(stderr,
             L"Usage: lxterm [-n|-j] [-y] [-u source] [-r rootdir]\n"
             L"  -n             Start a new Linux instance instead of joining the running one\n"
             L"  -j             Only join the running Linux instance; fail when there is none\n"
             L"  -y             Install the Linux system without asking when the root holds none\n"
             L"  -u source      Install from this URL or file (.tar, .tar.gz or .tar.xz) instead of Ubuntu 26.04 LTS\n"
             L"  -r rootdir     Windows directory used as the Linux root\n"
             L"                 (default: LiberNT\\Ubuntu in the local application data folder)\n");
}

static
VOID
LxPauseIfOwnConsole(VOID)
{
    DWORD Processes[2];
    WCHAR Line[8];

    if (GetConsoleProcessList(Processes, ARRAYSIZE(Processes)) != 1)
        return;
    wprintf(L"Press Enter to close this window.");
    fflush(stdout);
    fgetws(Line, ARRAYSIZE(Line), stdin);
}

static
VOID
LxSetTerminalAppId(
    _In_ PCWSTR Root)
{
    WCHAR Normalized[MAX_PATH], Value[LX_APP_ID_MAX];
    HWND Console = GetConsoleWindow();
    IPropertyStore *Store;
    PROPVARIANT Variant;
    DWORD Processes[2];

    if ((Console == NULL) || (GetConsoleProcessList(Processes, ARRAYSIZE(Processes)) != 1) ||
        !LxNormalizeRoot(Root, Normalized, ARRAYSIZE(Normalized)))
    {
        return;
    }
    _snwprintf(Value, ARRAYSIZE(Value), LX_TERMINAL_ID_FORMAT, LxRootTag(Normalized));
    Value[ARRAYSIZE(Value) - 1] = UNICODE_NULL;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(Console, &IID_IPropertyStore, (void **)&Store)))
    {
        PropVariantInit(&Variant);
        Variant.vt = VT_LPWSTR;
        Variant.pwszVal = Value;
        if (SUCCEEDED(IPropertyStore_SetValue(Store, &PKEY_AppUserModel_ID, &Variant)))
            IPropertyStore_Commit(Store);
        IPropertyStore_Release(Store);
    }
    SetConsoleTitleW(L"Linux Terminal");
}

static
BOOL
LxDefaultRoot(
    _Out_writes_(MAX_PATH) PWCHAR Root)
{
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, Root)) ||
        (wcslen(Root) + wcslen(L"\\LiberNT\\Ubuntu") >= MAX_PATH))
    {
        return FALSE;
    }
    wcscat(Root, L"\\LiberNT");
    CreateDirectoryW(Root, NULL);
    wcscat(Root, L"\\Ubuntu");
    return TRUE;
}

static
BOOL
LxRootPath(
    _In_ PCWSTR Root,
    _In_ PCWSTR Relative,
    _Out_writes_(MAX_PATH) PWCHAR Path)
{
    if (!GetFullPathNameW(Root, MAX_PATH, Path, NULL) || (wcslen(Path) + wcslen(Relative) >= MAX_PATH))
        return FALSE;
    wcscat(Path, Relative);
    return TRUE;
}

static
BOOL
LxReadAnswer(
    _Out_writes_z_(Size) PCHAR Answer,
    _In_ SIZE_T Size,
    _In_ BOOL Secret)
{
    HANDLE Input = GetStdHandle(STD_INPUT_HANDLE);
    WCHAR Line[256];
    DWORD Mode = 0, Read = 0;
    SIZE_T Length = 0;
    CHAR Byte;

    fflush(stdout);
    if (!GetConsoleMode(Input, &Mode))
    {
        while (ReadFile(Input, &Byte, 1, &Read, NULL) && (Read == 1) && (Byte != '\n'))
        {
            if ((Byte != '\r') && (Length + 1 < Size))
                Answer[Length++] = Byte;
        }
        Answer[Length] = '\0';
        return (Read == 1) || (Length != 0);
    }
    if (Secret)
        SetConsoleMode(Input, Mode & ~ENABLE_ECHO_INPUT);
    if (!ReadConsoleW(Input, Line, ARRAYSIZE(Line) - 1, &Read, NULL))
        Read = 0;
    if (Secret)
    {
        SetConsoleMode(Input, Mode);
        wprintf(L"\n");
    }
    if (Read == 0)
        return FALSE;
    Line[Read] = UNICODE_NULL;
    Line[wcscspn(Line, L"\r\n")] = UNICODE_NULL;
    return WideCharToMultiByte(CP_UTF8, 0, Line, -1, Answer, (int)Size, NULL, NULL) > 0;
}

static
int
LxRunRootTool(
    _In_ PCWSTR Root,
    _In_ PCSTR FileName,
    _In_reads_(Count) PCSTR *Args,
    _In_ ULONG Count,
    _In_opt_ PCSTR Input)
{
    LX_LAUNCH Tool;
    ULONG Index;

    ZeroMemory(&Tool, sizeof(Tool));
    Tool.Name = L"lxterm";
    Tool.Root = Root;
    Tool.Cwd = "/";
    Tool.FileName = FileName;
    Tool.Input = (Input != NULL) ? Input : "";
    if (!LxEnvSet(&Tool.Env, "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"))
        return 1;
    for (Index = 0; Index < Count; Index++)
    {
        if (!LxStringsAdd(&Tool.Args, Args[Index]))
            return 1;
    }
    return LxLaunch(&Tool);
}

static
BOOL
LxValidUserName(
    _In_ PCSTR Name)
{
    SIZE_T Length = strlen(Name);

    return (Length > 0) && (Length <= 32) &&
           (((Name[0] >= 'a') && (Name[0] <= 'z')) || (Name[0] == '_')) &&
           (Name[strspn(Name, "abcdefghijklmnopqrstuvwxyz0123456789_-")] == '\0');
}

static
BOOL
LxCreateDefaultUser(
    _In_ PCWSTR Root,
    _Out_writes_z_(Size) PCHAR Name,
    _In_ SIZE_T Size)
{
    static const PCSTR Wanted[LX_USER_GROUPS_MAX] = { "adm", "sudo", "users" };
    CHAR Password[256], Again[256], Groups[64] = "", Entry[512], Config[128];
    PCSTR Args[8];
    LX_ACCOUNT Existing;
    ULONG Count = 0, Index, Gid;
    int Try;

    wprintf(L"Create the default user account of this Linux system.\n");
    wprintf(L"It does not need to match your Windows user name.\n");
    for (Try = 0;; Try++)
    {
        if (Try == 3)
            return FALSE;
        wprintf(L"New user name: ");
        if (!LxReadAnswer(Name, Size, FALSE))
            return FALSE;
        if (!LxValidUserName(Name))
        {
            wprintf(L"Use lowercase letters, digits, '_' or '-', starting with a letter.\n");
            continue;
        }
        if (LxRootAccount(Root, Name, 0, &Existing))
        {
            wprintf(L"The user %S already exists.\n", Name);
            continue;
        }
        break;
    }
    for (Try = 0;; Try++)
    {
        if (Try == 3)
            return FALSE;
        wprintf(L"New password: ");
        if (!LxReadAnswer(Password, sizeof(Password), TRUE))
            return FALSE;
        wprintf(L"Retype new password: ");
        if (!LxReadAnswer(Again, sizeof(Again), TRUE))
            return FALSE;
        if ((Password[0] != '\0') && (strcmp(Password, Again) == 0) && (strchr(Password, ':') == NULL))
            break;
        wprintf(L"The passwords do not match or are empty.\n");
    }

    for (Index = 0; Index < LX_USER_GROUPS_MAX; Index++)
    {
        if (LxRootGroupId(Root, Wanted[Index], &Gid))
        {
            if (Groups[0] != '\0')
                strcat(Groups, ",");
            strcat(Groups, Wanted[Index]);
        }
    }
    Args[Count++] = "useradd";
    Args[Count++] = "-m";
    Args[Count++] = "-s";
    Args[Count++] = LxRootHasFile(Root, L"\\usr\\bin\\bash") ? "/bin/bash" : "/bin/sh";
    if (Groups[0] != '\0')
    {
        Args[Count++] = "-G";
        Args[Count++] = Groups;
    }
    Args[Count++] = Name;
    if (LxRunRootTool(Root, "/usr/sbin/useradd", Args, Count, NULL) != 0)
    {
        wprintf(L"The user account could not be created.\n");
        return FALSE;
    }

    _snprintf(Entry, sizeof(Entry), "%s:%s\n", Name, Password);
    Entry[sizeof(Entry) - 1] = '\0';
    SecureZeroMemory(Password, sizeof(Password));
    SecureZeroMemory(Again, sizeof(Again));
    Args[0] = "chpasswd";
    Index = LxRunRootTool(Root, "/usr/sbin/chpasswd", Args, 1, Entry);
    SecureZeroMemory(Entry, sizeof(Entry));
    if (Index != 0)
        wprintf(L"The password could not be set.\n");

    _snprintf(Config, sizeof(Config), "[user]\ndefault=%s\n", Name);
    Config[sizeof(Config) - 1] = '\0';
    if (!LxAppendRootFile(Root, L"\\etc\\wsl.conf", Config))
        wprintf(L"The default user could not be saved in /etc/wsl.conf.\n");
    return TRUE;
}

static
VOID
LxStartWindowHost(
    _In_ PCWSTR Root)
{
    WCHAR Normalized[MAX_PATH], Program[MAX_PATH], Marker[MAX_PATH], CommandLine[2 * MAX_PATH + 16];
    STARTUPINFOW Startup;
    PROCESS_INFORMATION Process;
    PWCHAR Slash;

    if (!LxNormalizeRoot(Root, Normalized, ARRAYSIZE(Normalized)) ||
        !LxRootPath(Normalized, L"\\usr\\local\\lib\\libernt\\lxwayland\\lxwayland", Marker) ||
        (GetFileAttributesW(Marker) == INVALID_FILE_ATTRIBUTES) ||
        !GetModuleFileNameW(NULL, Program, ARRAYSIZE(Program)))
    {
        return;
    }
    Slash = wcsrchr(Program, L'\\');
    if ((Slash == NULL) || ((Slash - Program) + 16 >= ARRAYSIZE(Program)))
        return;
    wcscpy(Slash + 1, L"lxwinhost.exe");
    _snwprintf(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" -r \"%s\"", Program, Normalized);
    CommandLine[ARRAYSIZE(CommandLine) - 1] = UNICODE_NULL;
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    if (CreateProcessW(Program, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
    }
}

int
wmain(
    int argc,
    WCHAR *argv[])
{
    LX_ACCOUNT Account;
    static WCHAR DefaultRoot[MAX_PATH], FullRoot[MAX_PATH];
    CHAR UserName[64];
    BOOL Installed;
    PCWSTR Url = NULL;
    BOOL AssumeYes = FALSE;
    LX_LAUNCH Launch;
    PCSTR Base;
    PCHAR Argv0;
    int Index;

    ZeroMemory(&Launch, sizeof(Launch));
    Launch.Name = L"lxterm";
    Launch.Root = NULL;
    Launch.Terminal = TRUE;

    for (Index = 1; Index < argc; Index++)
    {
        if (wcscmp(argv[Index], L"-n") == 0)
        {
            Launch.Flags |= LX_CREATE_NEW_INSTANCE;
        }
        else if (wcscmp(argv[Index], L"-j") == 0)
        {
            Launch.Flags |= LX_CREATE_JOIN;
        }
        else if (wcscmp(argv[Index], L"-y") == 0)
        {
            AssumeYes = TRUE;
        }
        else if ((wcscmp(argv[Index], L"-u") == 0) && (Index + 1 < argc))
        {
            Url = argv[++Index];
        }
        else if ((wcscmp(argv[Index], L"-r") == 0) && (Index + 1 < argc))
        {
            Launch.Root = argv[++Index];
        }
        else if ((wcscmp(argv[Index], L"-h") == 0) || (wcscmp(argv[Index], L"/?") == 0))
        {
            LxUsage();
            return 0;
        }
        else
        {
            LxUsage();
            return 1;
        }
    }

    if (Launch.Root == NULL)
    {
        if (!LxDefaultRoot(DefaultRoot))
        {
            fwprintf(stderr, L"lxterm: cannot find the local application data folder; pass -r rootdir\n");
            LxPauseIfOwnConsole();
            return 1;
        }
        Launch.Root = DefaultRoot;
    }
    LxSetTerminalAppId(Launch.Root);
    Installed = !LxRootInstalled(Launch.Root);
    if (Installed && (LxInstallRoot(Launch.Root, Url, AssumeYes) != 0))
    {
        LxPauseIfOwnConsole();
        return 1;
    }
    if (GetFullPathNameW(Launch.Root, MAX_PATH, FullRoot, NULL))
        LxKeepResolvConf(FullRoot);
    LxStartWindowHost(Launch.Root);

    if (!LxEnvSet(&Launch.Env, "TERM=xterm-256color") ||
        !LxEnvSet(&Launch.Env, "COLORTERM=truecolor"))
    {
        return 1;
    }

    if (!LxRootDefaultUser(Launch.Root, UserName, sizeof(UserName)) &&
        (!Installed || !LxCreateDefaultUser(Launch.Root, UserName, sizeof(UserName))))
    {
        UserName[0] = '\0';
    }
    if (((UserName[0] == '\0') || !LxRootAccount(Launch.Root, UserName, 0, &Account)) &&
        !LxRootAccount(Launch.Root, NULL, 0, &Account))
    {
        ZeroMemory(&Account, sizeof(Account));
        strcpy(Account.Name, "root");
        strcpy(Account.Home, "/root");
        strcpy(Account.Shell, "/bin/sh");
    }
    if (!LxSetSession(&Launch, Launch.Root, &Account))
        return 1;

    Base = strrchr(Account.Shell, '/');
    Base = (Base != NULL) ? Base + 1 : Account.Shell;
    Argv0 = malloc(strlen(Base) + 2);
    if (Argv0 == NULL)
        return 1;
    Argv0[0] = '-';
    strcpy(Argv0 + 1, Base);
    Launch.FileName = Account.Shell;
    if (!LxStringsAdd(&Launch.Args, Argv0))
        return 1;

    return LxLaunch(&Launch);
}
