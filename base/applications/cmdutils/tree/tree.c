/*
 * PROJECT:     LiberNT Tree Command
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Graphically display the folder structure of a drive or path
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <wincon.h>
#include <winuser.h>

#include "resource.h"

#define TREE_PATH_CHARS 32768

typedef struct _TREE_GLYPHS
{
    PCWSTR Branch;
    PCWSTR Last;
    PCWSTR Vertical;
    PCWSTR Space;
} TREE_GLYPHS;

static const TREE_GLYPHS TreeUnicodeGlyphs = { L"\x251C\x2500\x2500\x2500", L"\x2514\x2500\x2500\x2500", L"\x2502   ", L"    " };
static const TREE_GLYPHS TreeAsciiGlyphs = { L"+---", L"\\---", L"|   ", L"    " };

static HANDLE TreeOutput;
static BOOL TreeConsole;
static BOOL TreeShowFiles;
static const TREE_GLYPHS *TreeGlyphs = &TreeUnicodeGlyphs;

static VOID
TreeWriteChunk(
    _In_reads_(Length) PCWSTR Text,
    _In_ DWORD Length)
{
    DWORD Written;
    int Bytes;
    PSTR Buffer;

    if (Length == 0)
        return;

    if (TreeConsole)
    {
        WriteConsoleW(TreeOutput, Text, Length, &Written, NULL);
        return;
    }

    Bytes = WideCharToMultiByte(CP_ACP, 0, Text, (int)Length, NULL, 0, NULL, NULL);
    if (Bytes <= 0)
        return;
    Buffer = HeapAlloc(GetProcessHeap(), 0, Bytes);
    if (Buffer == NULL)
        return;
    WideCharToMultiByte(CP_ACP, 0, Text, (int)Length, Buffer, Bytes, NULL, NULL);
    WriteFile(TreeOutput, Buffer, (DWORD)Bytes, &Written, NULL);
    HeapFree(GetProcessHeap(), 0, Buffer);
}

static VOID
TreeWrite(
    _In_ PCWSTR Text)
{
    PCWSTR Start = Text;
    PCWSTR Current;

    for (Current = Text; *Current != UNICODE_NULL; Current++)
    {
        if (*Current == L'\n' && (Current == Text || Current[-1] != L'\r'))
        {
            TreeWriteChunk(Start, (DWORD)(Current - Start));
            TreeWriteChunk(L"\r\n", 2);
            Start = Current + 1;
        }
    }
    TreeWriteChunk(Start, (DWORD)(Current - Start));
}

static VOID
TreeWriteLine(
    _In_ PCWSTR Prefix,
    _In_ PCWSTR Text)
{
    TreeWriteChunk(Prefix, (DWORD)wcslen(Prefix));
    TreeWriteChunk(Text, (DWORD)wcslen(Text));
    TreeWriteChunk(L"\r\n", 2);
}

static VOID
TreeMessage(
    _In_ UINT Id,
    ...)
{
    WCHAR Format[1024];
    PWSTR Text = NULL;
    va_list Arguments;

    if (LoadStringW(GetModuleHandleW(NULL), Id, Format, _countof(Format)) == 0)
        return;

    va_start(Arguments, Id);
    FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                   Format, 0, 0, (PWSTR)&Text, 0, &Arguments);
    va_end(Arguments);

    if (Text != NULL)
    {
        TreeWrite(Text);
        LocalFree(Text);
    }
}

typedef struct _TREE_LIST
{
    PWSTR *Names;
    ULONG Count;
    ULONG Capacity;
} TREE_LIST;

static BOOL
TreeListAdd(
    _Inout_ TREE_LIST *List,
    _In_ PCWSTR Name)
{
    SIZE_T Size = (wcslen(Name) + 1) * sizeof(WCHAR);
    PWSTR Copy;

    if (List->Count == List->Capacity)
    {
        ULONG Capacity = List->Capacity ? List->Capacity * 2 : 16;
        PWSTR *Names = List->Names ?
            HeapReAlloc(GetProcessHeap(), 0, List->Names, Capacity * sizeof(PWSTR)) :
            HeapAlloc(GetProcessHeap(), 0, Capacity * sizeof(PWSTR));
        if (Names == NULL)
            return FALSE;
        List->Names = Names;
        List->Capacity = Capacity;
    }

    Copy = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Copy == NULL)
        return FALSE;
    CopyMemory(Copy, Name, Size);
    List->Names[List->Count++] = Copy;
    return TRUE;
}

static VOID
TreeListFree(
    _Inout_ TREE_LIST *List)
{
    ULONG i;

    for (i = 0; i < List->Count; i++)
        HeapFree(GetProcessHeap(), 0, List->Names[i]);
    if (List->Names != NULL)
        HeapFree(GetProcessHeap(), 0, List->Names);
    ZeroMemory(List, sizeof(*List));
}

static ULONG
TreeDirectory(
    _Inout_updates_(TREE_PATH_CHARS) PWSTR Path,
    _In_ SIZE_T PathLength,
    _Inout_updates_(TREE_PATH_CHARS) PWSTR Prefix,
    _In_ SIZE_T PrefixLength)
{
    TREE_LIST Directories = { 0 }, Files = { 0 };
    WIN32_FIND_DATAW FindData;
    HANDLE hFind;
    ULONG i;

    if (PathLength + 3 >= TREE_PATH_CHARS)
        return 0;

    Path[PathLength] = L'\\';
    Path[PathLength + 1] = L'*';
    Path[PathLength + 2] = UNICODE_NULL;
    hFind = FindFirstFileW(Path, &FindData);
    Path[PathLength] = UNICODE_NULL;
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (FindData.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
                continue;
            if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (wcscmp(FindData.cFileName, L".") != 0 && wcscmp(FindData.cFileName, L"..") != 0)
                    TreeListAdd(&Directories, FindData.cFileName);
            }
            else if (TreeShowFiles)
            {
                TreeListAdd(&Files, FindData.cFileName);
            }
        } while (FindNextFileW(hFind, &FindData));
        FindClose(hFind);
    }

    if (Files.Count != 0 && PrefixLength + 4 < TREE_PATH_CHARS)
    {
        wcscpy(Prefix + PrefixLength, Directories.Count ? TreeGlyphs->Vertical : TreeGlyphs->Space);
        for (i = 0; i < Files.Count; i++)
            TreeWriteLine(Prefix, Files.Names[i]);
        TreeWriteLine(Prefix, L"");
        Prefix[PrefixLength] = UNICODE_NULL;
    }

    for (i = 0; i < Directories.Count; i++)
    {
        BOOL Last = (i + 1 == Directories.Count);
        SIZE_T NameLength = wcslen(Directories.Names[i]);

        TreeWriteChunk(Prefix, (DWORD)PrefixLength);
        TreeWriteLine(Last ? TreeGlyphs->Last : TreeGlyphs->Branch, Directories.Names[i]);

        if (PathLength + 1 + NameLength + 3 >= TREE_PATH_CHARS || PrefixLength + 4 >= TREE_PATH_CHARS)
            continue;

        Path[PathLength] = L'\\';
        wcscpy(Path + PathLength + 1, Directories.Names[i]);
        wcscpy(Prefix + PrefixLength, Last ? TreeGlyphs->Space : TreeGlyphs->Vertical);
        TreeDirectory(Path, PathLength + 1 + NameLength, Prefix, PrefixLength + 4);
        Path[PathLength] = UNICODE_NULL;
        Prefix[PrefixLength] = UNICODE_NULL;
    }

    i = Directories.Count;
    TreeListFree(&Directories);
    TreeListFree(&Files);
    return i;
}

static PWSTR
TreeNextArgument(
    _Inout_ PWSTR *Cursor)
{
    PWSTR Source = *Cursor, Destination, Argument;
    BOOL Quoted = FALSE;

    while (*Source == L' ' || *Source == L'\t')
        Source++;
    if (*Source == UNICODE_NULL)
    {
        *Cursor = Source;
        return NULL;
    }

    Argument = Destination = Source;
    while (*Source != UNICODE_NULL && (Quoted || (*Source != L' ' && *Source != L'\t')))
    {
        if (*Source == L'"')
            Quoted = !Quoted;
        else
            *Destination++ = *Source;
        Source++;
    }
    if (*Source != UNICODE_NULL)
        Source++;
    *Destination = UNICODE_NULL;
    *Cursor = Source;
    return Argument;
}

int
wmain(
    int argc,
    WCHAR *argv[])
{
    PCWSTR Argument = NULL;
    PWSTR Path, Prefix, CommandLine, Cursor, Token;
    WCHAR Root[MAX_PATH], Label[MAX_PATH + 1], Display[MAX_PATH + 3];
    DWORD Mode, Serial = 0, Attributes, Length;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    TreeOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    TreeConsole = GetConsoleMode(TreeOutput, &Mode);

    CommandLine = HeapAlloc(GetProcessHeap(), 0, (wcslen(GetCommandLineW()) + 1) * sizeof(WCHAR));
    if (CommandLine == NULL)
        return 1;
    wcscpy(CommandLine, GetCommandLineW());
    Cursor = CommandLine;
    TreeNextArgument(&Cursor);

    while ((Token = TreeNextArgument(&Cursor)) != NULL)
    {
        if (Token[0] == L'/')
        {
            if (_wcsicmp(Token, L"/F") == 0)
            {
                TreeShowFiles = TRUE;
            }
            else if (_wcsicmp(Token, L"/A") == 0)
            {
                TreeGlyphs = &TreeAsciiGlyphs;
            }
            else if (wcscmp(Token, L"/?") == 0)
            {
                TreeMessage(IDS_USAGE);
                return 0;
            }
            else
            {
                TreeMessage(IDS_INVALID_SWITCH, Token);
                return 0;
            }
        }
        else if (Argument != NULL)
        {
            TreeMessage(IDS_TOO_MANY_PARAMETERS, Token);
            return 0;
        }
        else
        {
            Argument = Token;
        }
    }

    Path = HeapAlloc(GetProcessHeap(), 0, TREE_PATH_CHARS * sizeof(WCHAR));
    Prefix = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, TREE_PATH_CHARS * sizeof(WCHAR));
    if (Path == NULL || Prefix == NULL)
        return 1;

    Length = GetFullPathNameW(Argument ? Argument : L".", TREE_PATH_CHARS, Path, NULL);
    if (Length == 0 || Length >= TREE_PATH_CHARS)
        return 1;
    if (Length > 3 && Path[Length - 1] == L'\\')
        Path[--Length] = UNICODE_NULL;

    if (!GetVolumePathNameW(Path, Root, _countof(Root)))
    {
        Root[0] = Path[0];
        Root[1] = L':';
        Root[2] = L'\\';
        Root[3] = UNICODE_NULL;
    }
    Label[0] = UNICODE_NULL;
    GetVolumeInformationW(Root, Label, _countof(Label), &Serial, NULL, NULL, NULL, 0);

    if (Label[0] != UNICODE_NULL)
        TreeMessage(IDS_FOLDER_LISTING_VOLUME, Label);
    else
        /* FIXME: the header of a volume without a label is not verified against Windows 11 */
        TreeMessage(IDS_FOLDER_LISTING);
    /* FIXME: Windows 11 prints a varying value of unknown meaning before the serial number */
    TreeMessage(IDS_VOLUME_SERIAL, 0, HIWORD(Serial), LOWORD(Serial));

    if (Argument != NULL)
    {
        lstrcpynW(Display, Path, _countof(Display));
        CharUpperW(Display);
    }
    else
    {
        Display[0] = Path[0];
        Display[1] = L':';
        Display[2] = L'.';
        Display[3] = UNICODE_NULL;
        CharUpperW(Display);
    }
    TreeWriteLine(L"", Display);

    Attributes = GetFileAttributesW(Path);
    if (Attributes == INVALID_FILE_ATTRIBUTES || !(Attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        lstrcpynW(Display, Path + 2, _countof(Display));
        CharUpperW(Display);
        TreeMessage(IDS_INVALID_PATH, Display);
        TreeMessage(IDS_NO_SUBFOLDERS);
        return 0;
    }

    if (TreeDirectory(Path, (Path[Length - 1] == L'\\') ? Length - 1 : Length, Prefix, 0) == 0)
        TreeMessage(IDS_NO_SUBFOLDERS);

    return 0;
}
