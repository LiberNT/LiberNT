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
#include <shlobj.h>

#include "../include/lxioctl.h"
#include "../include/lxlaunch.h"
#include "lxinstall.h"

#define LX_PASSWD_MAX (1024 * 1024)

typedef struct _LX_ACCOUNT
{
    CHAR Name[64];
    CHAR Home[MAX_PATH];
    CHAR Shell[MAX_PATH];
} LX_ACCOUNT, *PLX_ACCOUNT;

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
VOID
LxCopyField(
    _Out_writes_z_(Size) PCHAR Target,
    _In_ SIZE_T Size,
    _In_ PCSTR Field)
{
    if (strlen(Field) < Size)
        strcpy(Target, Field);
}

static
VOID
LxLookupAccount(
    _In_ PCWSTR Root,
    _In_ PCSTR Uid,
    _Inout_ PLX_ACCOUNT Account)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;
    PCHAR Data, Line, Next;
    DWORD Size, Read;

    if (!GetFullPathNameW(Root, MAX_PATH, Path, NULL) ||
        (wcslen(Path) + wcslen(L"\\etc\\passwd") >= MAX_PATH))
    {
        return;
    }
    wcscat(Path, L"\\etc\\passwd");

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return;
    Size = GetFileSize(File, NULL);
    Data = ((Size != INVALID_FILE_SIZE) && (Size < LX_PASSWD_MAX)) ? malloc(Size + 1) : NULL;
    if ((Data == NULL) || !ReadFile(File, Data, Size, &Read, NULL))
    {
        free(Data);
        CloseHandle(File);
        return;
    }
    CloseHandle(File);
    Data[Read] = '\0';

    for (Line = Data; Line != NULL; Line = Next)
    {
        PCHAR Fields[7];
        int Count = 0;
        PCHAR Cursor;

        Next = strchr(Line, '\n');
        if (Next != NULL)
            *Next++ = '\0';

        for (Cursor = Line; Count < 7; Count++)
        {
            Fields[Count] = Cursor;
            Cursor = strchr(Cursor, ':');
            if (Cursor == NULL)
            {
                Count++;
                break;
            }
            *Cursor++ = '\0';
        }
        if ((Count < 7) || (strcmp(Fields[2], Uid) != 0))
            continue;

        Cursor = Fields[6] + strcspn(Fields[6], "\r");
        *Cursor = '\0';
        LxCopyField(Account->Name, sizeof(Account->Name), Fields[0]);
        if (Fields[5][0] == '/')
            LxCopyField(Account->Home, sizeof(Account->Home), Fields[5]);
        if (Fields[6][0] == '/')
            LxCopyField(Account->Shell, sizeof(Account->Shell), Fields[6]);
        break;
    }
    free(Data);
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
LxDefaultUser(
    _In_ PCWSTR Root,
    _Out_writes_z_(Size) PCHAR Name,
    _In_ SIZE_T Size)
{
    WCHAR Path[MAX_PATH];
    CHAR Data[4096];
    PCHAR Line, Next;
    BOOL InUser = FALSE;
    DWORD Read = 0;
    HANDLE File;

    Name[0] = '\0';
    if (!LxRootPath(Root, L"\\etc\\wsl.conf", Path))
        return FALSE;
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!ReadFile(File, Data, sizeof(Data) - 1, &Read, NULL))
        Read = 0;
    CloseHandle(File);
    Data[Read] = '\0';

    for (Line = Data; Line != NULL; Line = Next)
    {
        PCHAR Value;

        Next = strchr(Line, '\n');
        if (Next != NULL)
            *Next++ = '\0';
        Line[strcspn(Line, "\r")] = '\0';
        Line += strspn(Line, " \t");
        if (Line[0] == '[')
        {
            InUser = (strncmp(Line, "[user]", 6) == 0);
            continue;
        }
        if (!InUser || (strncmp(Line, "default", 7) != 0))
            continue;
        Value = Line + 7 + strspn(Line + 7, " \t");
        if (*Value != '=')
            continue;
        Value++;
        Value += strspn(Value, " \t");
        Value[strcspn(Value, " \t#")] = '\0';
        if ((Value[0] == '\0') || (strlen(Value) >= Size) ||
            (Value[strspn(Value, "abcdefghijklmnopqrstuvwxyz0123456789_-")] != '\0'))
        {
            return FALSE;
        }
        strcpy(Name, Value);
        return TRUE;
    }
    return FALSE;
}

static
BOOL
LxEnvPrintf(
    _Inout_ PLX_STRINGS Env,
    _In_ PCSTR Name,
    _In_ PCSTR Value)
{
    SIZE_T Length = strlen(Name) + strlen(Value) + 2;
    PCHAR Assignment = malloc(Length);
    BOOL Result;

    if (Assignment == NULL)
        return FALSE;
    _snprintf(Assignment, Length, "%s=%s", Name, Value);
    Result = LxEnvSet(Env, Assignment);
    free(Assignment);
    return Result;
}

int
wmain(
    int argc,
    WCHAR *argv[])
{
    LX_ACCOUNT Account = { "root", "/root", "/bin/sh" };
    static WCHAR DefaultRoot[MAX_PATH], FullRoot[MAX_PATH];
    WCHAR SetupPath[MAX_PATH];
    CHAR UserName[64];
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
    if (!LxRootInstalled(Launch.Root) && (LxInstallRoot(Launch.Root, Url, AssumeYes) != 0))
    {
        LxPauseIfOwnConsole();
        return 1;
    }
    if (GetFullPathNameW(Launch.Root, MAX_PATH, FullRoot, NULL))
        LxKeepResolvConf(FullRoot);

    if (!LxEnvSet(&Launch.Env, "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin") ||
        !LxEnvSet(&Launch.Env, "TERM=xterm-256color") ||
        !LxEnvSet(&Launch.Env, "COLORTERM=truecolor") ||
        !LxEnvSet(&Launch.Env, "LANG=C.UTF-8"))
    {
        return 1;
    }

    if (LxDefaultUser(Launch.Root, UserName, sizeof(UserName)))
    {
        Launch.FileName = "/usr/sbin/runuser";
        Launch.Cwd = "/";
        if (!LxStringsAdd(&Launch.Args, "runuser") || !LxStringsAdd(&Launch.Args, "-l") ||
            !LxStringsAdd(&Launch.Args, "-w") || !LxStringsAdd(&Launch.Args, "COLORTERM") ||
            !LxStringsAdd(&Launch.Args, UserName))
        {
            return 1;
        }
        return LxLaunch(&Launch);
    }

    LxLookupAccount(Launch.Root, "0", &Account);

    Base = strrchr(Account.Shell, '/');
    Base = (Base != NULL) ? Base + 1 : Account.Shell;
    Argv0 = malloc(strlen(Base) + 2);
    if (Argv0 == NULL)
        return 1;
    Argv0[0] = '-';
    strcpy(Argv0 + 1, Base);

    Launch.FileName = Account.Shell;
    Launch.Cwd = Account.Home;
    if (!LxEnvPrintf(&Launch.Env, "HOME", Account.Home) ||
        !LxEnvPrintf(&Launch.Env, "USER", Account.Name) ||
        !LxEnvPrintf(&Launch.Env, "LOGNAME", Account.Name) ||
        !LxEnvPrintf(&Launch.Env, "SHELL", Account.Shell))
    {
        return 1;
    }
    if (LxRootPath(Launch.Root, L"\\usr\\local\\lib\\libernt\\user-setup", SetupPath) &&
        (GetFileAttributesW(SetupPath) != INVALID_FILE_ATTRIBUTES))
    {
        Launch.FileName = "/bin/sh";
        return (LxStringsAdd(&Launch.Args, "sh") && LxStringsAdd(&Launch.Args, "/usr/local/lib/libernt/user-setup"))
               ? LxLaunch(&Launch) : 1;
    }
    if (!LxStringsAdd(&Launch.Args, Argv0))
        return 1;

    return LxLaunch(&Launch);
}
