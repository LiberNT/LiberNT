/*
 * PROJECT:     LiberNT WHERE command
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Show the location of the files that match a pattern
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <strsafe.h>
#include <conutils.h>
#include "resource.h"

#define WHERE_DEFAULT_PATHEXT L".COM;.EXE;.BAT;.CMD"
#define WHERE_MAX_PATH 32767

typedef enum _WHERE_EXIT
{
    WhereFound = 0,
    WhereNotFound = 1,
    WhereFailed = 2
} WHERE_EXIT;

typedef struct _WHERE_LIST
{
    PWSTR *Items;
    ULONG Count;
    ULONG Capacity;
} WHERE_LIST, *PWHERE_LIST;

static BOOL Quiet;
static BOOL Quote;
static BOOL Details;
static PCWSTR RecursiveRoot;
static WHERE_LIST Patterns;
static WHERE_LIST Extensions;
static WHERE_LIST Results;

static BOOL
ListAdd(PWHERE_LIST List, PCWSTR Text, SIZE_T Length)
{
    PWSTR Copy;

    if (List->Count == List->Capacity)
    {
        ULONG Capacity = List->Capacity ? List->Capacity * 2 : 16;
        PWSTR *Items = realloc(List->Items, Capacity * sizeof(PWSTR));

        if (!Items)
            return FALSE;
        List->Items = Items;
        List->Capacity = Capacity;
    }
    Copy = malloc((Length + 1) * sizeof(WCHAR));
    if (!Copy)
        return FALSE;
    CopyMemory(Copy, Text, Length * sizeof(WCHAR));
    Copy[Length] = UNICODE_NULL;
    List->Items[List->Count++] = Copy;
    return TRUE;
}

static BOOL
ListContains(PWHERE_LIST List, PCWSTR Text)
{
    ULONG Index;

    for (Index = 0; Index < List->Count; Index++)
    {
        if (!_wcsicmp(List->Items[Index], Text))
            return TRUE;
    }
    return FALSE;
}

static VOID
ListFree(PWHERE_LIST List)
{
    ULONG Index;

    for (Index = 0; Index < List->Count; Index++)
        free(List->Items[Index]);
    free(List->Items);
    ZeroMemory(List, sizeof(*List));
}

static VOID
ReportSystemError(DWORD Error)
{
    PWSTR Message = NULL, Text, Out;
    PCWSTR In;
    DWORD Length;

    if (Quiet)
        return;
    Length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL, Error, LANG_USER_DEFAULT, (PWSTR)&Message, 0, NULL);
    ConPuts(StdErr, L"ERROR: ");
    if (!Length)
        return;
    /* The message keeps its own line ends, each expanded the way a text-mode stream writes it (CR CR LF). */
    Text = malloc((2 * Length + 1) * sizeof(WCHAR));
    if (Text)
    {
        for (In = Message, Out = Text; *In; In++)
        {
            if (In[0] == L'\r' && In[1] == L'\n')
                *Out++ = L'\r';
            *Out++ = *In;
        }
        *Out = UNICODE_NULL;
        ConPuts(StdErr, Text);
        free(Text);
    }
    LocalFree(Message);
}

static VOID
ReportError(UINT Id)
{
    if (!Quiet)
        ConResPuts(StdErr, Id);
}

/* Returns the directory spelled as it is on disk, or NULL when it cannot be opened. */
static PWSTR
CanonicalDirectory(PCWSTR Directory)
{
    PWSTR Full, Final;
    HANDLE Handle;
    DWORD Length;

    Length = GetFullPathNameW(Directory, 0, NULL, NULL);
    if (!Length)
        return NULL;
    Full = malloc(Length * sizeof(WCHAR));
    if (!Full)
        return NULL;
    if (!GetFullPathNameW(Directory, Length, Full, NULL))
    {
        free(Full);
        return NULL;
    }

    Handle = CreateFileW(Full, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (Handle == INVALID_HANDLE_VALUE)
        return Full;
    Final = malloc(WHERE_MAX_PATH * sizeof(WCHAR));
    Length = Final ? GetFinalPathNameByHandleW(Handle, Final, WHERE_MAX_PATH, VOLUME_NAME_DOS) : 0;
    CloseHandle(Handle);
    if (!Length || Length >= WHERE_MAX_PATH)
    {
        free(Final);
        return Full;
    }
    free(Full);
    if (!wcsncmp(Final, L"\\\\?\\UNC\\", 8))
    {
        MoveMemory(Final + 1, Final + 7, (Length - 6) * sizeof(WCHAR));
        Final[0] = L'\\';
    }
    else if (!wcsncmp(Final, L"\\\\?\\", 4))
    {
        MoveMemory(Final, Final + 4, (Length - 3) * sizeof(WCHAR));
    }
    return Final;
}

static PWSTR
JoinPath(PCWSTR Directory, PCWSTR Name, PCWSTR Suffix)
{
    SIZE_T Length = wcslen(Directory);
    BOOL Separator = Length && Directory[Length - 1] != L'\\';
    SIZE_T Total = Length + Separator + wcslen(Name) + wcslen(Suffix) + 1;
    PWSTR Path;

    if (Total > WHERE_MAX_PATH)
        return NULL;
    Path = malloc(Total * sizeof(WCHAR));
    if (Path)
        StringCchPrintfW(Path, Total, Separator ? L"%s\\%s%s" : L"%s%s%s", Directory, Name, Suffix);
    return Path;
}

static BOOL
PrintMatch(PCWSTR Path, PWIN32_FIND_DATAW Data)
{
    WCHAR Date[64], Time[64];
    LARGE_INTEGER Size;
    FILETIME Local;
    SYSTEMTIME System;

    if (ListContains(&Results, Path))
        return TRUE;
    if (!ListAdd(&Results, Path, wcslen(Path)))
        return FALSE;
    if (Quiet)
        return TRUE;

    if (Details)
    {
        FileTimeToLocalFileTime(&Data->ftLastWriteTime, &Local);
        FileTimeToSystemTime(&Local, &System);
        GetDateFormatW(LOCALE_USER_DEFAULT, 0, &System, NULL, Date, _countof(Date));
        GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &System, NULL, Time, _countof(Time));
        Size.LowPart = Data->nFileSizeLow;
        Size.HighPart = Data->nFileSizeHigh;
        if (Quote)
        {
            PWSTR Quoted = JoinPath(L"\"", Path, L"\"");

            ConResPrintf(StdOut, IDS_FILE_INFO, Size.QuadPart, Date, Time, Quoted ? Quoted : Path);
            free(Quoted);
        }
        else
        {
            ConResPrintf(StdOut, IDS_FILE_INFO, Size.QuadPart, Date, Time, Path);
        }
    }
    else
    {
        ConPrintf(StdOut, Quote ? L"\"%s\"\n" : L"%s\n", Path);
    }
    return TRUE;
}

/* Prints the files of Directory that match Pattern, as given and with every PATHEXT extension. */
static BOOL
SearchDirectory(PCWSTR Directory, PCWSTR Pattern)
{
    WIN32_FIND_DATAW Data;
    ULONG Index;
    HANDLE Find;
    PWSTR Query, Path;
    BOOL Success = TRUE;

    for (Index = 0; Success && Index < Extensions.Count; Index++)
    {
        Query = JoinPath(Directory, Pattern, Extensions.Items[Index]);
        if (!Query)
            continue;
        Find = FindFirstFileExW(Query, FindExInfoBasic, &Data, FindExSearchNameMatch, NULL, 0);
        free(Query);
        if (Find == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            if (Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            Path = JoinPath(Directory, Data.cFileName, L"");
            if (!Path)
                continue;
            Success = PrintMatch(Path, &Data);
            free(Path);
        } while (Success && FindNextFileW(Find, &Data));
        FindClose(Find);
    }
    return Success;
}

static BOOL
SearchTree(PCWSTR Directory, PCWSTR Pattern)
{
    WIN32_FIND_DATAW Data;
    HANDLE Find;
    PWSTR Query, Child;
    BOOL Success;

    Success = SearchDirectory(Directory, Pattern);
    Query = JoinPath(Directory, L"*", L"");
    if (!Success || !Query)
        return Success;
    Find = FindFirstFileExW(Query, FindExInfoBasic, &Data, FindExSearchNameMatch, NULL, 0);
    free(Query);
    if (Find == INVALID_HANDLE_VALUE)
        return TRUE;
    do
    {
        if (!(Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            !wcscmp(Data.cFileName, L".") || !wcscmp(Data.cFileName, L".."))
        {
            continue;
        }
        Child = JoinPath(Directory, Data.cFileName, L"");
        if (!Child)
            continue;
        Success = SearchTree(Child, Pattern);
        free(Child);
    } while (Success && FindNextFileW(Find, &Data));
    FindClose(Find);
    return Success;
}

/* Searches every directory of a ';' separated list, after the current directory when asked. */
static BOOL
SearchDirectoryList(PCWSTR List, PCWSTR Pattern, BOOL CurrentFirst)
{
    PWSTR Copy, Next, Entry, Directory;
    SIZE_T Length;
    BOOL Success = TRUE;

    if (CurrentFirst)
    {
        Directory = CanonicalDirectory(L".");
        if (!Directory)
            return FALSE;
        Success = SearchDirectory(Directory, Pattern);
        free(Directory);
    }

    Copy = _wcsdup(List);
    if (!Copy)
        return FALSE;
    for (Entry = Copy; Success && Entry; Entry = Next)
    {
        Next = wcschr(Entry, L';');
        if (Next)
            *Next++ = UNICODE_NULL;
        if (*Entry == L'"')
        {
            Entry++;
            Length = wcslen(Entry);
            if (Length && Entry[Length - 1] == L'"')
                Entry[Length - 1] = UNICODE_NULL;
        }
        if (!*Entry)
            continue;
        Directory = CanonicalDirectory(Entry);
        if (!Directory)
            continue;
        Success = SearchDirectory(Directory, Pattern);
        free(Directory);
    }
    free(Copy);
    return Success;
}

static BOOL
SearchVariable(PCWSTR Name, PCWSTR Pattern, BOOL CurrentFirst)
{
    DWORD Length = GetEnvironmentVariableW(Name, NULL, 0);
    PWSTR Value;
    BOOL Success;

    if (!Length)
    {
        if (!Quiet)
            ConResPrintf(StdErr, IDS_BAD_ENVVAR, Name);
        return CurrentFirst ? SearchDirectoryList(L"", Pattern, TRUE) : TRUE;
    }
    Value = malloc(Length * sizeof(WCHAR));
    if (!Value)
        return FALSE;
    if (!GetEnvironmentVariableW(Name, Value, Length))
        Value[0] = UNICODE_NULL;
    Success = SearchDirectoryList(Value, Pattern, CurrentFirst);
    free(Value);
    return Success;
}

static BOOL
LoadExtensions(VOID)
{
    DWORD Length = GetEnvironmentVariableW(L"PATHEXT", NULL, 0);
    PWSTR Value, Entry, Next;
    BOOL Success = TRUE;

    if (!ListAdd(&Extensions, L"", 0))
        return FALSE;
    Value = Length ? malloc(Length * sizeof(WCHAR)) : _wcsdup(WHERE_DEFAULT_PATHEXT);
    if (!Value)
        return FALSE;
    if (Length && !GetEnvironmentVariableW(L"PATHEXT", Value, Length))
        Value[0] = UNICODE_NULL;
    for (Entry = Value; Success && Entry; Entry = Next)
    {
        Next = wcschr(Entry, L';');
        if (Next)
            *Next++ = UNICODE_NULL;
        if (*Entry)
            Success = ListAdd(&Extensions, Entry, wcslen(Entry));
    }
    free(Value);
    return Success;
}

static BOOL
SetFlag(BOOL *Flag, PCWSTR Option)
{
    if (*Flag)
    {
        ConResPrintf(StdErr, IDS_TOO_MANY, Option, 1);
        ConResPuts(StdErr, IDS_TYPE_HELP);
        return FALSE;
    }
    *Flag = TRUE;
    return TRUE;
}

static BOOL
ParseArguments(INT argc, WCHAR **argv, BOOL *Help)
{
    BOOL Recursive = FALSE;
    INT Index;

    for (Index = 1; Index < argc; Index++)
    {
        PCWSTR Argument = argv[Index];

        if ((Argument[0] != L'/' && Argument[0] != L'-') || !Argument[1] || Argument[2])
        {
            if (Argument[0] == L'/' || Argument[0] == L'-')
            {
                ConResPrintf(StdErr, IDS_BAD_ARG, Argument);
                ConResPuts(StdErr, IDS_TYPE_HELP);
                return FALSE;
            }
            if (!ListAdd(&Patterns, Argument, wcslen(Argument)))
            {
                ConResPuts(StdErr, IDS_OUTOFMEMORY);
                return FALSE;
            }
            continue;
        }

        switch (towupper(Argument[1]))
        {
            case L'?':
                if (!SetFlag(Help, Argument))
                    return FALSE;
                break;
            case L'Q':
                if (!SetFlag(&Quiet, Argument))
                    return FALSE;
                break;
            case L'F':
                if (!SetFlag(&Quote, Argument))
                    return FALSE;
                break;
            case L'T':
                if (!SetFlag(&Details, Argument))
                    return FALSE;
                break;
            case L'R':
                if (!SetFlag(&Recursive, Argument))
                    return FALSE;
                if (Index + 1 >= argc)
                {
                    ConResPrintf(StdErr, IDS_WANT_VALUE, Argument);
                    ConResPuts(StdErr, IDS_TYPE_HELP);
                    return FALSE;
                }
                RecursiveRoot = argv[++Index];
                break;
            default:
                ConResPrintf(StdErr, IDS_BAD_ARG, Argument);
                ConResPuts(StdErr, IDS_TYPE_HELP);
                return FALSE;
        }
    }
    return TRUE;
}

static BOOL
SearchPattern(PWSTR Pattern, BOOL *Failed)
{
    PWSTR Separator = wcsrchr(Pattern, L':');

    if (Separator && Separator != Pattern + 1)
    {
        *Separator++ = UNICODE_NULL;
        if (RecursiveRoot)
        {
            ReportError(Pattern[0] == L'$' ? IDS_ENVPAT_WITH_R : IDS_PATHPAT_WITH_R);
            *Failed = TRUE;
            return TRUE;
        }
        if (!*Separator || wcschr(Separator, L'\\'))
        {
            ReportError(IDS_BAD_PATHPAT);
            *Failed = TRUE;
            return TRUE;
        }
        if (Pattern[0] == L'$')
            return SearchVariable(Pattern + 1, Separator, FALSE);
        return SearchDirectoryList(Pattern, Separator, FALSE);
    }

    if (RecursiveRoot)
    {
        PWSTR Root = CanonicalDirectory(RecursiveRoot);
        BOOL Success;

        if (!Root)
            return FALSE;
        Success = SearchTree(Root, Pattern);
        free(Root);
        return Success;
    }
    return SearchVariable(L"PATH", Pattern, TRUE);
}

static BOOL
CheckRecursiveRoot(VOID)
{
    DWORD Attributes;

    if (wcschr(RecursiveRoot, L';'))
    {
        ReportError(IDS_BAD_NAME);
        return FALSE;
    }
    Attributes = GetFileAttributesW(RecursiveRoot);
    if (Attributes == INVALID_FILE_ATTRIBUTES)
    {
        ReportSystemError(GetLastError());
        return FALSE;
    }
    if (!(Attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        ReportError(IDS_BAD_DIR);
        return FALSE;
    }
    return TRUE;
}

static WHERE_EXIT
RunWhere(INT argc, WCHAR **argv)
{
    typedef BOOL (WINAPI *PFN_DISABLE_REDIRECTION)(PVOID *);
    PFN_DISABLE_REDIRECTION DisableRedirection;
    BOOL Help = FALSE, Failed = FALSE;
    PVOID Redirection;
    ULONG Index;

    if (!ParseArguments(argc, argv, &Help))
        return WhereFailed;
    if (Help || !Patterns.Count)
    {
        ConResPuts(StdOut, IDS_USAGE);
        return Help ? WhereFound : WhereFailed;
    }
    if (RecursiveRoot && !CheckRecursiveRoot())
        return WhereFailed;

    DisableRedirection = (PFN_DISABLE_REDIRECTION)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                 "Wow64DisableWow64FsRedirection");
    if (DisableRedirection)
        DisableRedirection(&Redirection);

    if (!LoadExtensions())
    {
        ReportError(IDS_OUTOFMEMORY);
        return WhereFailed;
    }
    for (Index = 0; Index < Patterns.Count; Index++)
    {
        if (!SearchPattern(Patterns.Items[Index], &Failed))
        {
            ReportError(IDS_OUTOFMEMORY);
            return WhereFailed;
        }
    }

    if (Failed)
        return WhereFailed;
    if (Results.Count)
        return WhereFound;
    ReportError(IDS_NOT_FOUND);
    return WhereNotFound;
}

INT
wmain(INT argc, WCHAR **argv)
{
    WHERE_EXIT Exit;

    ConInitStdStreams();
    Exit = RunWhere(argc, argv);
    ListFree(&Results);
    ListFree(&Patterns);
    ListFree(&Extensions);
    return Exit;
}
