/*
 * Shell DDE Handling
 *
 * Copyright 2004 Robert Shearman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "shelldesktop.h"
#include <ddeml.h>
#include <strsafe.h>

WINE_DEFAULT_DEBUG_CHANNEL(shelldde);

typedef DWORD(CALLBACK * pfnCommandHandler)(PWSTR strCommand, PWSTR strPath, LPITEMIDLIST pidl, INT unkS);

struct DDECommandHandler
{
    WCHAR Command[32];
    pfnCommandHandler Handler;
};

extern DDECommandHandler HandlerList [];
extern const int HandlerListLength;

/* DDE Instance ID */
static DWORD dwDDEInst;

/* String handles */
static HSZ hszProgmanTopic;
static HSZ hszProgmanService;
static HSZ hszShell;
static HSZ hszAppProperties;
static HSZ hszFolders;
static HSZ hszGroups;

static BOOL bInitialized;

static PWSTR ProgmanDuplicate(PCWSTR Source, SIZE_T Length)
{
    PWSTR Copy = (PWSTR)CoTaskMemAlloc((Length + 1) * sizeof(WCHAR));

    if (Copy)
    {
        CopyMemory(Copy, Source, Length * sizeof(WCHAR));
        Copy[Length] = UNICODE_NULL;
    }
    return Copy;
}

static PWSTR ProgmanCombinePath(PCWSTR Directory, PCWSTR Name, PCWSTR Extension, BOOL Sanitize)
{
    SIZE_T Length = wcslen(Directory) + 1 + wcslen(Name) + (Extension ? wcslen(Extension) : 0);
    PWSTR Path = (PWSTR)CoTaskMemAlloc((Length + 1) * sizeof(WCHAR));
    PWSTR Clean = ProgmanDuplicate(Name, wcslen(Name));
    SIZE_T i;

    if (!Path || !Clean)
    {
        CoTaskMemFree(Path);
        CoTaskMemFree(Clean);
        return NULL;
    }

    for (i = 0; Sanitize && Clean[i]; i++)
    {
        if (Clean[i] < L' ' || wcschr(L"*/:<>?\\|", Clean[i]))
            Clean[i] = L'_';
    }

    PathCombineW(Path, Directory, Clean);
    if (Extension)
        StringCchCatW(Path, Length + 1, Extension);
    CoTaskMemFree(Clean);
    return Path;
}

static PWSTR ProgmanGetProgramsPath(PCWSTR Name, BOOL Sanitize)
{
    PWSTR Programs, Path;

    if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, NULL, &Programs)))
        return NULL;
    Path = ProgmanCombinePath(Programs, Name, NULL, Sanitize);
    CoTaskMemFree(Programs);
    return Path;
}

static HDDEDATA ProgmanRequestGroups(UINT uFmt)
{
    WIN32_FIND_DATAW FindData;
    PWSTR Pattern = ProgmanGetProgramsPath(L"*", FALSE);
    PWSTR Groups = ProgmanDuplicate(L"", 0), NewGroups;
    SIZE_T Length = 1;
    HDDEDATA Data = NULL;
    HANDLE hFind;
    PSTR GroupsA;
    INT LengthA;

    if (!Pattern || !Groups)
    {
        CoTaskMemFree(Pattern);
        CoTaskMemFree(Groups);
        return NULL;
    }

    hFind = FindFirstFileW(Pattern, &FindData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                !wcscmp(FindData.cFileName, L".") || !wcscmp(FindData.cFileName, L".."))
            {
                continue;
            }
            Length += wcslen(FindData.cFileName) + 2;
            NewGroups = (PWSTR)CoTaskMemRealloc(Groups, Length * sizeof(WCHAR));
            if (!NewGroups)
                break;
            Groups = NewGroups;
            StringCchCatW(Groups, Length, FindData.cFileName);
            StringCchCatW(Groups, Length, L"\r\n");
        } while (FindNextFileW(hFind, &FindData));
        FindClose(hFind);
    }

    LengthA = WideCharToMultiByte(CP_ACP, 0, Groups, -1, NULL, 0, NULL, NULL);
    GroupsA = (PSTR)CoTaskMemAlloc(LengthA);
    if (GroupsA)
    {
        WideCharToMultiByte(CP_ACP, 0, Groups, -1, GroupsA, LengthA, NULL, NULL);
        Data = DdeCreateDataHandle(dwDDEInst, (LPBYTE)GroupsA, LengthA, 0, hszGroups, uFmt, 0);
    }

    CoTaskMemFree(GroupsA);
    CoTaskMemFree(Groups);
    CoTaskMemFree(Pattern);
    return Data;
}

static DWORD ProgmanAddItem(PCWSTR LastGroup, int Argc, PWSTR *Argv)
{
    CComPtr<IShellLinkW> Link;
    CComPtr<IPersistFile> File;
    PWSTR Target, Space = NULL, Path, Name, FileName;
    DWORD Length;
    HRESULT hr;

    if (Argc < 1 || !LastGroup)
        return DDE_FNOTPROCESSED;

    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IShellLinkW, &Link));
    if (FAILED(hr))
        return DDE_FNOTPROCESSED;

    Target = ProgmanDuplicate(Argv[0], wcslen(Argv[0]));
    if (!Target)
        return DDE_FNOTPROCESSED;
    while (!(Length = SearchPathW(NULL, Target, L".exe", 0, NULL, NULL)))
    {
        Space = wcsrchr(Target, L' ');
        if (!Space)
        {
            CoTaskMemFree(Target);
            return DDE_FNOTPROCESSED;
        }
        *Space = UNICODE_NULL;
    }
    Path = (PWSTR)CoTaskMemAlloc(Length * sizeof(WCHAR));
    if (!Path)
    {
        CoTaskMemFree(Target);
        return DDE_FNOTPROCESSED;
    }
    SearchPathW(NULL, Target, L".exe", Length, Path, NULL);
    Link->SetPath(Path);
    if (Space)
        Link->SetArguments(Argv[0] + (Space - Target) + 1);
    CoTaskMemFree(Path);
    CoTaskMemFree(Target);

    if (Argc >= 2)
        Link->SetDescription(Argv[1]);
    if (Argc >= 4)
        Link->SetIconLocation(Argv[2], wcstol(Argv[3], NULL, 10));
    if (Argc >= 7)
        Link->SetWorkingDirectory(Argv[6]);
    if (Argc >= 8)
        Link->SetHotkey((WORD)wcstol(Argv[7], NULL, 10));
    if (Argc >= 9)
    {
        if (wcstol(Argv[8], NULL, 10) == 0)
            Link->SetShowCmd(SW_SHOWMINNOACTIVE);
        else if (wcstol(Argv[8], NULL, 10) == 1)
            Link->SetShowCmd(SW_SHOWNORMAL);
    }

    hr = Link->QueryInterface(IID_PPV_ARG(IPersistFile, &File));
    if (FAILED(hr))
        return DDE_FNOTPROCESSED;

    if (Argc >= 2)
    {
        Name = ProgmanCombinePath(LastGroup, Argv[1], L".lnk", TRUE);
    }
    else
    {
        FileName = ProgmanDuplicate(PathFindFileNameW(Argv[0]), wcslen(PathFindFileNameW(Argv[0])));
        if (!FileName)
            return DDE_FNOTPROCESSED;
        *PathFindExtensionW(FileName) = UNICODE_NULL;
        Name = ProgmanCombinePath(LastGroup, FileName, L".lnk", TRUE);
        CoTaskMemFree(FileName);
    }
    if (!Name)
        return DDE_FNOTPROCESSED;

    hr = File->Save(Name, TRUE);
    CoTaskMemFree(Name);
    return FAILED(hr) ? DDE_FNOTPROCESSED : DDE_FACK;
}

static DWORD ProgmanExecute(PCWSTR Command, int Argc, PWSTR *Argv)
{
    static PWSTR LastGroup;
    PWSTR Path, From;
    SHFILEOPSTRUCTW Operation = { 0 };
    SIZE_T Length;
    BOOL Deleted;
    int Result;

    if (!_wcsicmp(Command, L"CreateGroup") || !_wcsicmp(Command, L"ShowGroup"))
    {
        if (Argc < (_wcsicmp(Command, L"ShowGroup") ? 1 : 2))
            return DDE_FNOTPROCESSED;
        Path = ProgmanGetProgramsPath(Argv[0], TRUE);
        if (!Path)
            return DDE_FNOTPROCESSED;
        if (!_wcsicmp(Command, L"CreateGroup"))
            CreateDirectoryW(Path, NULL);
        ShellExecuteW(NULL, NULL, Path, NULL, NULL, SW_SHOWNORMAL);
        CoTaskMemFree(LastGroup);
        LastGroup = Path;
        return DDE_FACK;
    }

    if (!_wcsicmp(Command, L"DeleteGroup"))
    {
        if (Argc < 1)
            return DDE_FNOTPROCESSED;
        Path = ProgmanGetProgramsPath(Argv[0], TRUE);
        if (!Path)
            return DDE_FNOTPROCESSED;
        Length = wcslen(Path);
        From = (PWSTR)CoTaskMemAlloc((Length + 2) * sizeof(WCHAR));
        if (!From)
        {
            CoTaskMemFree(Path);
            return DDE_FNOTPROCESSED;
        }
        CopyMemory(From, Path, (Length + 1) * sizeof(WCHAR));
        From[Length + 1] = UNICODE_NULL;
        Operation.wFunc = FO_DELETE;
        Operation.pFrom = From;
        Operation.fFlags = FOF_NOCONFIRMATION;
        Result = SHFileOperationW(&Operation);
        CoTaskMemFree(From);
        CoTaskMemFree(Path);
        return (Result || Operation.fAnyOperationsAborted) ? DDE_FNOTPROCESSED : DDE_FACK;
    }

    if (!_wcsicmp(Command, L"AddItem"))
        return ProgmanAddItem(LastGroup, Argc, Argv);

    if (!_wcsicmp(Command, L"DeleteItem") || !_wcsicmp(Command, L"ReplaceItem"))
    {
        if (Argc < 1 || !LastGroup)
            return DDE_FNOTPROCESSED;
        Path = ProgmanCombinePath(LastGroup, Argv[0], L".lnk", FALSE);
        if (!Path)
            return DDE_FNOTPROCESSED;
        Deleted = DeleteFileW(Path);
        CoTaskMemFree(Path);
        return Deleted ? DDE_FACK : DDE_FNOTPROCESSED;
    }

    if (!_wcsicmp(Command, L"ExitProgman"))
        return DDE_FACK;

    FIXME("Unhandled Progman command %S\n", Command);
    return DDE_FNOTPROCESSED;
}

static void ProgmanFreeArguments(PWSTR *Argv, int Argc)
{
    int i;

    for (i = 0; i < Argc; i++)
        CoTaskMemFree(Argv[i]);
    CoTaskMemFree(Argv);
}

static PWSTR ProgmanParseArguments(PWSTR Command, PWSTR **Argv, int *Argc)
{
    PWSTR *NewArgv;
    PWSTR End;

    while (*Command != L')')
    {
        while (*Command == L' ')
            Command++;
        if (*Command == L'"')
        {
            Command++;
            End = wcschr(Command, L'"');
        }
        else
        {
            End = wcspbrk(Command, L",()[]");
            while (End && End > Command && End[-1] == L' ')
                End--;
        }
        if (!End)
            return NULL;

        NewArgv = (PWSTR *)CoTaskMemRealloc(*Argv, (*Argc + 1) * sizeof(PWSTR));
        if (!NewArgv)
            return NULL;
        *Argv = NewArgv;
        (*Argv)[*Argc] = ProgmanDuplicate(Command, End - Command);
        if (!(*Argv)[*Argc])
            return NULL;
        (*Argc)++;

        Command = End;
        if (*Command == L'"')
            Command++;
        while (*Command == L' ')
            Command++;
        if (*Command == L',')
            Command++;
        else if (*Command != L')')
            return NULL;
    }
    return Command + 1;
}

static DWORD ProgmanParseCommand(PWSTR Command)
{
    DWORD Result = DDE_FACK;
    PWSTR Opcode, End, *Argv;
    int Argc;

    while (*Command == L' ')
        Command++;
    if (*Command != L'[')
        return DDE_FNOTPROCESSED;

    while (*Command == L'[')
    {
        Argv = NULL;
        Argc = 0;
        Command++;
        while (*Command == L' ')
            Command++;
        End = wcspbrk(Command, L" ,()[]\"");
        if (!End)
            return DDE_FNOTPROCESSED;
        Opcode = ProgmanDuplicate(Command, End - Command);
        if (!Opcode)
            return DDE_FNOTPROCESSED;

        Command = End;
        while (*Command == L' ')
            Command++;
        if (*Command == L'(')
        {
            Command = ProgmanParseArguments(Command + 1, &Argv, &Argc);
            while (Command && *Command == L' ')
                Command++;
        }
        if (!Command || *Command != L']')
        {
            CoTaskMemFree(Opcode);
            ProgmanFreeArguments(Argv, Argc);
            return DDE_FNOTPROCESSED;
        }
        Command++;
        while (*Command == L' ')
            Command++;

        Result = ProgmanExecute(Opcode, Argc, Argv);
        CoTaskMemFree(Opcode);
        ProgmanFreeArguments(Argv, Argc);
        if (Result == DDE_FNOTPROCESSED)
            break;
    }
    return Result;
}

static DWORD ProgmanOnExecute(HDDEDATA hdata)
{
    DWORD Length = DdeGetData(hdata, NULL, 0, 0);
    PWSTR Command;
    DWORD Result;

    if (Length < sizeof(WCHAR))
        return DDE_FNOTPROCESSED;
    Command = (PWSTR)CoTaskMemAlloc(Length + sizeof(WCHAR));
    if (!Command)
        return DDE_FNOTPROCESSED;
    DdeGetData(hdata, (LPBYTE)Command, Length, 0);
    Command[Length / sizeof(WCHAR)] = UNICODE_NULL;
    Result = ProgmanParseCommand(Command);
    CoTaskMemFree(Command);
    return Result;
}

static BOOL Dde_OnConnect(HSZ hszTopic, HSZ hszService)
{
    WCHAR szTopic[MAX_PATH];
    WCHAR szService[MAX_PATH];

    DdeQueryStringW(dwDDEInst, hszTopic, szTopic, _countof(szTopic), CP_WINUNICODE);
    DdeQueryStringW(dwDDEInst, hszService, szService, _countof(szService), CP_WINUNICODE);

    TRACE("Dde_OnConnect: topic=%S, service=%S\n", szTopic, szService);

    return TRUE;
}

static void Dde_OnConnectConfirm(HCONV hconv, HSZ hszTopic, HSZ hszService)
{
    WCHAR szTopic[MAX_PATH];
    WCHAR szService[MAX_PATH];

    DdeQueryStringW(dwDDEInst, hszTopic, szTopic, _countof(szTopic), CP_WINUNICODE);
    DdeQueryStringW(dwDDEInst, hszService, szService, _countof(szService), CP_WINUNICODE);

    TRACE("Dde_OnConnectConfirm: hconv=%p, topic=%S, service=%S\n", hconv, szTopic, szService);
}

static BOOL Dde_OnWildConnect(HSZ hszTopic, HSZ hszService)
{
    WCHAR szTopic[MAX_PATH];
    WCHAR szService[MAX_PATH];

    DdeQueryStringW(dwDDEInst, hszTopic, szTopic, _countof(szTopic), CP_WINUNICODE);
    DdeQueryStringW(dwDDEInst, hszService, szService, _countof(szService), CP_WINUNICODE);

    TRACE("Dde_OnWildConnect: topic=%S, service=%S\n", szTopic, szService);

    return FALSE;
}

static HDDEDATA Dde_OnRequest(UINT uFmt, HCONV hconv, HSZ hszTopic, HSZ hszItem)
{
    WCHAR szTopic[MAX_PATH];
    WCHAR szItem[MAX_PATH];

    DdeQueryStringW(dwDDEInst, hszTopic, szTopic, _countof(szTopic), CP_WINUNICODE);
    DdeQueryStringW(dwDDEInst, hszItem, szItem, _countof(szItem), CP_WINUNICODE);

    TRACE("Dde_OnRequest: uFmt=%d, hconv=%p, topic=%S, item=%S\n", hconv, szTopic, szItem);

    if (hszTopic == hszProgmanTopic && hszItem == hszGroups && uFmt == CF_TEXT)
        return ProgmanRequestGroups(uFmt);

    if (hszTopic == hszProgmanTopic && hszItem == hszProgmanService && uFmt == CF_TEXT)
    {
        static BYTE EmptyGroups[] = "\r\n";
        return DdeCreateDataHandle(dwDDEInst, EmptyGroups, sizeof(EmptyGroups), 0, hszProgmanService, uFmt, 0);
    }

    return NULL;
}

static LPITEMIDLIST _ILReadFromSharedMemory(PCWSTR strField)
{
    LPITEMIDLIST ret = NULL;

    // Ensure it really is an IDLIST-formatted parameter
    // Format for IDLIST params: ":pid:shared"
    if (*strField != L':')
        return NULL;

    HANDLE hData = UlongToHandle(StrToIntW(strField + 1));
    PWSTR strSecond = StrChrW(strField + 1, L':');

    if (strSecond)
    {
        int pid = StrToIntW(strSecond + 1);
        void* pvShared = SHLockShared(hData, pid);
        if (pvShared)
        {
            ret = ILClone((LPCITEMIDLIST) pvShared);
            SHUnlockShared(pvShared);
            SHFreeShared(hData, pid);
        }
    }
    return ret;
}

static DWORD Dde_OnExecute(HCONV hconv, HSZ hszTopic, HDDEDATA hdata)
{
    WCHAR szTopic[MAX_PATH];
    WCHAR szCommand[MAX_PATH];
    WCHAR *pszCommand;

    if (hszTopic == hszProgmanTopic)
        return ProgmanOnExecute(hdata);

    DdeQueryStringW(dwDDEInst, hszTopic, szTopic, _countof(szTopic), CP_WINUNICODE);

    pszCommand = (WCHAR*) DdeAccessData(hdata, NULL);
    if (!pszCommand)
        return DDE_FNOTPROCESSED;

    StringCchCopyW(szCommand, _countof(szCommand), pszCommand);

    DdeUnaccessData(hdata);

    TRACE("Dde_OnExecute: hconv=%p, topic=%S, command=%S\n", hconv, szTopic, pszCommand);

    /*
    [ViewFolder("%l", %I, %S)]    -- Open a folder in standard mode
    [ExploreFolder("%l", %I, %S)] -- Open a folder in "explore" mode (file tree is shown to the left by default)
    [FindFolder("%l", %I)]        -- Open a folder in "find" mode (search panel is shown to the left by default)
    [ShellFile("%1","%1",%S)]     -- Execute the contents of the specified .SCF file

    Approximate grammar (Upper names define rules, <lower> names define terminals, single-quotes are literals):

        Rules
            Command = ('[' Function ']') | Function
            Function = <identifier> '(' Parameters ')'
            Parameters = (<quoted-string> (',' <idlist> (',' <number>)?)?)?

        Terminals
            <identifier> =  [a-zA-Z]+
            <quoted-string> = \"([^\"]|\\.)\"
            <idlist> = \:[0-9]+\:[0-9]+
            <number> = [0-9]+
    */

    WCHAR Command[MAX_PATH] = L"";
    WCHAR Path[MAX_PATH] = L"";
    LPITEMIDLIST IdList = NULL;
    INT UnknownParameter = 0;

    // Simplified parsing (assumes the command will not be TOO broken):

    PWSTR cmd = szCommand;
    //    1. if starts with [, skip first char
    if (*cmd == L'[')
        cmd++;

    if (*cmd == L']')
    {
        ERR("Empty command. Nothing to run.\n");
        return DDE_FNOTPROCESSED;
    }

    // Read until first (, and take text before ( as command name
    {
        PWSTR cmdEnd = StrChrW(cmd, L'(');

        if (!cmdEnd)
        {
            ERR("Could not find '('. Invalid command.\n");
            return DDE_FNOTPROCESSED;
        }

        *cmdEnd = 0;

        StringCchCopy(Command, _countof(Command), cmd);

        cmd = cmdEnd + 1;
    }

    // Read first param after (, expecting quoted string
    if (*cmd != L')')
    {
        // Copy unescaped string
        PWSTR dst = Path;
        BOOL isQuote = FALSE;

        PWSTR arg = cmd;

        while (*arg && (isQuote || *arg != L','))
        {
            if (*arg == L'"')
            {
                isQuote = !isQuote;
                if (isQuote && arg != cmd) // do not copy the " at the beginning of the string
                {
                    *(dst++) = L'"';
                }
            }
            else
            {
                *(dst++) = *arg;
            }

            arg++;
        }

        cmd = arg + 1;

        while (*cmd == L' ')
            cmd++;
    }

    // Read second param, expecting an idlist in shared memory
    if (*cmd != L')')
    {
        if (*cmd != ':')
        {
            ERR("Expected ':'. Invalid command.\n");
            return DDE_FNOTPROCESSED;
        }

        PWSTR idlistEnd = StrChrW(cmd, L',');

        if (!idlistEnd)
            idlistEnd = StrChrW(cmd, L')');

        if (!idlistEnd)
        {
            ERR("Expected ',' or ')'. Invalid command.\n");
            return DDE_FNOTPROCESSED;
        }

        IdList = _ILReadFromSharedMemory(cmd);

        cmd = idlistEnd + 1;
    }

    // Read third param, expecting an integer
    if (*cmd != L')')
    {
        UnknownParameter = StrToIntW(cmd);
    }

    TRACE("Parse end: cmd=%S, S=%d, pidl=%p, path=%S\n", Command, UnknownParameter, IdList, Path);

    // Find handler in list
    for (int i = 0; i < HandlerListLength; i++)
    {
        DDECommandHandler & handler = HandlerList[i];
        if (StrCmpW(handler.Command, Command) == 0)
        {
            return handler.Handler(Command, Path, IdList, UnknownParameter);
        }
    }

    // No handler found
    ERR("Unknown command %S\n", Command);
    return DDE_FNOTPROCESSED;
}

static void Dde_OnDisconnect(HCONV hconv)
{
    TRACE("Dde_OnDisconnect: hconv=%p\n", hconv);
}

static HDDEDATA CALLBACK DdeCallback(
    UINT uType,
    UINT uFmt,
    HCONV hconv,
    HSZ hsz1,
    HSZ hsz2,
    HDDEDATA hdata,
    ULONG_PTR dwData1,
    ULONG_PTR dwData2)
{
    switch (uType)
    {
    case XTYP_CONNECT:
        return (HDDEDATA) (DWORD_PTR) Dde_OnConnect(hsz1, hsz2);
    case XTYP_CONNECT_CONFIRM:
        Dde_OnConnectConfirm(hconv, hsz1, hsz2);
        return NULL;
    case XTYP_WILDCONNECT:
        return (HDDEDATA) (DWORD_PTR) Dde_OnWildConnect(hsz1, hsz2);
    case XTYP_REQUEST:
        return Dde_OnRequest(uFmt, hconv, hsz1, hsz2);
    case XTYP_EXECUTE:
        return (HDDEDATA) (DWORD_PTR) Dde_OnExecute(hconv, hsz1, hdata);
    case XTYP_DISCONNECT:
        Dde_OnDisconnect(hconv);
        return NULL;
    case XTYP_REGISTER:
        return NULL;
    default:
        WARN("DdeCallback: unknown uType=%d\n", uType);
        return NULL;
    }
}
/*************************************************************************
 * ShellDDEInit (SHELL32.@)
 *
 * Registers the Shell DDE services with the system so that applications
 * can use them.
 *
 * PARAMS
 *  bInit [I] TRUE to initialize the services, FALSE to uninitialize.
 *
 * RETURNS
 *  Nothing.
 */
EXTERN_C void WINAPI ShellDDEInit(BOOL bInit)
{
    TRACE("ShellDDEInit bInit = %s\n", bInit ? "TRUE" : "FALSE");

    if (bInit && !bInitialized)
    {
        DdeInitializeW(&dwDDEInst, DdeCallback, CBF_FAIL_ADVISES | CBF_FAIL_POKES, 0);

        hszProgmanTopic = DdeCreateStringHandleW(dwDDEInst, L"Progman", CP_WINUNICODE);
        hszProgmanService = DdeCreateStringHandleW(dwDDEInst, L"Progman", CP_WINUNICODE);
        hszShell = DdeCreateStringHandleW(dwDDEInst, L"Shell", CP_WINUNICODE);
        hszAppProperties = DdeCreateStringHandleW(dwDDEInst, L"AppProperties", CP_WINUNICODE);
        hszFolders = DdeCreateStringHandleW(dwDDEInst, L"Folders", CP_WINUNICODE);
        hszGroups = DdeCreateStringHandleW(dwDDEInst, L"Groups", CP_WINUNICODE);

        if (hszProgmanTopic && hszProgmanService &&
            hszShell && hszAppProperties && hszFolders && hszGroups &&
            DdeNameService(dwDDEInst, hszFolders, 0, DNS_REGISTER) &&
            DdeNameService(dwDDEInst, hszProgmanService, 0, DNS_REGISTER) &&
            DdeNameService(dwDDEInst, hszShell, 0, DNS_REGISTER))
        {
            bInitialized = TRUE;
        }
    }
    else if (!bInit && bInitialized)
    {
        /* unregister all services */
        DdeNameService(dwDDEInst, 0, 0, DNS_UNREGISTER);

        if (hszGroups)
            DdeFreeStringHandle(dwDDEInst, hszGroups);
        if (hszFolders)
            DdeFreeStringHandle(dwDDEInst, hszFolders);
        if (hszAppProperties)
            DdeFreeStringHandle(dwDDEInst, hszAppProperties);
        if (hszShell)
            DdeFreeStringHandle(dwDDEInst, hszShell);
        if (hszProgmanService)
            DdeFreeStringHandle(dwDDEInst, hszProgmanService);
        if (hszProgmanTopic)
            DdeFreeStringHandle(dwDDEInst, hszProgmanTopic);

        DdeUninitialize(dwDDEInst);

        bInitialized = FALSE;
    }
}

static DWORD CALLBACK DDE_OnViewFolder(PWSTR strCommand, PWSTR strPath, LPITEMIDLIST pidl, INT unkS)
{
    if (!pidl)
        pidl = ILCreateFromPathW(strPath);

    if (!pidl)
        return DDE_FNOTPROCESSED;

    if (FAILED(SHOpenNewFrame(pidl, NULL, 0, 0)))
        return DDE_FNOTPROCESSED;

    return DDE_FACK;
}

static DWORD CALLBACK DDW_OnExploreFolder(PWSTR strCommand, PWSTR strPath, LPITEMIDLIST pidl, INT unkS)
{
    if (!pidl)
        pidl = ILCreateFromPathW(strPath);

    if (!pidl)
        return DDE_FNOTPROCESSED;

    if (FAILED(SHOpenNewFrame(pidl, NULL, 0, SH_EXPLORER_CMDLINE_FLAG_E)))
        return DDE_FNOTPROCESSED;

    return DDE_FACK;
}

static DWORD CALLBACK DDE_OnFindFolder(PWSTR strCommand, PWSTR strPath, LPITEMIDLIST pidl, INT unkS)
{
    UNIMPLEMENTED;
    return DDE_FNOTPROCESSED;
}

static DWORD CALLBACK DDE_OnShellFile(PWSTR strCommand, PWSTR strPath, LPITEMIDLIST pidl, INT unkS)
{
    UNIMPLEMENTED;
    return DDE_FNOTPROCESSED;
}

DDECommandHandler HandlerList [] = {

        { L"ViewFolder", DDE_OnViewFolder },
        { L"ExploreFolder", DDW_OnExploreFolder },
        { L"FindFolder", DDE_OnFindFolder },
        { L"ShellFile", DDE_OnShellFile }
};

const int HandlerListLength = _countof(HandlerList);
