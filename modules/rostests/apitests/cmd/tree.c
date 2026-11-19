/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the redirected output of tree.com
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <strsafe.h>

#define TREE_OUTPUT_SIZE 8192

static WCHAR TreeRoot[MAX_PATH];

static BOOL
RunTree(PCWSTR Arguments, PSTR Output, DWORD *Length)
{
    WCHAR System[MAX_PATH], Command[MAX_PATH * 3];
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, TRUE };
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    HANDLE Read, Write;
    DWORD Chunk, ExitCode = 0xdeadbeef;
    BOOL Created;

    *Length = 0;
    GetSystemDirectoryW(System, _countof(System));
    StringCchPrintfW(Command, _countof(Command), L"\"%s\\tree.com\" %s", System, Arguments);
    if (!CreatePipe(&Read, &Write, &Attributes, 0))
        return FALSE;
    SetHandleInformation(Read, HANDLE_FLAG_INHERIT, 0);

    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdOutput = Write;
    Startup.hStdError = Write;
    Created = CreateProcessW(NULL, Command, NULL, NULL, TRUE, 0, NULL, NULL, &Startup, &Process);
    CloseHandle(Write);
    if (!Created)
    {
        CloseHandle(Read);
        return FALSE;
    }

    while (*Length < TREE_OUTPUT_SIZE - 1 &&
           ReadFile(Read, Output + *Length, TREE_OUTPUT_SIZE - 1 - *Length, &Chunk, NULL) && Chunk != 0)
    {
        *Length += Chunk;
    }
    Output[*Length] = ANSI_NULL;
    CloseHandle(Read);

    ok(WaitForSingleObject(Process.hProcess, 10000) == WAIT_OBJECT_0, "tree.com %ls did not exit\n", Arguments);
    GetExitCodeProcess(Process.hProcess, &ExitCode);
    ok(ExitCode == 0, "tree.com %ls exited with %lu\n", Arguments, ExitCode);
    CloseHandle(Process.hProcess);
    CloseHandle(Process.hThread);
    return TRUE;
}

static VOID
ExpectBody(PCWSTR Arguments, BOOL Header, PCWSTR Path, PCWSTR Expected)
{
    CHAR Output[TREE_OUTPUT_SIZE], Wanted[TREE_OUTPUT_SIZE];
    WCHAR PathLine[MAX_PATH + 4];
    DWORD Length;
    PSTR Body = Output;
    int i;

    if (!RunTree(Arguments, Output, &Length))
    {
        ok(FALSE, "Cannot run tree.com %ls (%lu)\n", Arguments, GetLastError());
        return;
    }

    if (Header)
    {
        for (i = 0; i < 2 && Body != NULL; i++)
        {
            Body = strstr(Body, "\r\n");
            if (Body != NULL)
                Body += 2;
        }
        ok(Body != NULL, "tree.com %ls printed no header: %s\n", Arguments, Output);
        if (Body == NULL)
            return;

        StringCchPrintfW(PathLine, _countof(PathLine), L"%s\r\n", Path);
        CharUpperW(PathLine);
        WideCharToMultiByte(CP_ACP, 0, PathLine, -1, Wanted, sizeof(Wanted), NULL, NULL);
        ok(strncmp(Body, Wanted, strlen(Wanted)) == 0, "tree.com %ls path line: %s\n", Arguments, Body);
        if (strncmp(Body, Wanted, strlen(Wanted)) != 0)
            return;
        Body += strlen(Wanted);
    }

    WideCharToMultiByte(CP_ACP, 0, Expected, -1, Wanted, sizeof(Wanted), NULL, NULL);
    ok(strcmp(Body, Wanted) == 0, "tree.com %ls printed:\n%s\nexpected:\n%s\n", Arguments, Body, Wanted);
}

static BOOL
MakeDirectory(PCWSTR Relative, DWORD Attributes)
{
    WCHAR Path[MAX_PATH];

    StringCchPrintfW(Path, _countof(Path), L"%s\\%s", TreeRoot, Relative);
    if (!CreateDirectoryW(Path, NULL))
        return FALSE;
    return Attributes == 0 || SetFileAttributesW(Path, Attributes);
}

static BOOL
MakeFile(PCWSTR Relative, DWORD Attributes)
{
    WCHAR Path[MAX_PATH];
    HANDLE hFile;

    StringCchPrintfW(Path, _countof(Path), L"%s\\%s", TreeRoot, Relative);
    hFile = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_NEW, Attributes ? Attributes : FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    CloseHandle(hFile);
    return TRUE;
}

static VOID
RemoveTree(PCWSTR Path)
{
    WCHAR Pattern[MAX_PATH], Child[MAX_PATH];
    WIN32_FIND_DATAW FindData;
    HANDLE hFind;

    StringCchPrintfW(Pattern, _countof(Pattern), L"%s\\*", Path);
    hFind = FindFirstFileW(Pattern, &FindData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!wcscmp(FindData.cFileName, L".") || !wcscmp(FindData.cFileName, L".."))
                continue;
            StringCchPrintfW(Child, _countof(Child), L"%s\\%s", Path, FindData.cFileName);
            SetFileAttributesW(Child, FILE_ATTRIBUTE_NORMAL);
            if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveTree(Child);
            else
                DeleteFileW(Child);
        } while (FindNextFileW(hFind, &FindData));
        FindClose(hFind);
    }
    RemoveDirectoryW(Path);
}

START_TEST(tree)
{
    WCHAR Temp[MAX_PATH], Arguments[MAX_PATH * 2], Path[MAX_PATH];
    BOOL Ready;

    GetTempPathW(_countof(Temp), Temp);
    StringCchPrintfW(Path, _countof(Path), L"%streetest%lu", Temp, GetCurrentProcessId());
    GetFullPathNameW(Path, _countof(TreeRoot), TreeRoot, NULL);
    RemoveTree(TreeRoot);

    Ready = CreateDirectoryW(TreeRoot, NULL) &&
            MakeDirectory(L"a", 0) && MakeDirectory(L"a\\a1", 0) && MakeDirectory(L"a\\a2", 0) &&
            MakeDirectory(L"b", 0) && MakeDirectory(L"c", 0) && MakeDirectory(L"c\\c1", 0) &&
            MakeDirectory(L"c\\c1\\c11", 0) && MakeDirectory(L"h", FILE_ATTRIBUTE_HIDDEN) &&
            MakeDirectory(L"s", FILE_ATTRIBUTE_SYSTEM) && MakeFile(L"f1.txt", 0) &&
            MakeFile(L"hid.txt", FILE_ATTRIBUTE_HIDDEN) && MakeFile(L"sys.txt", FILE_ATTRIBUTE_SYSTEM) &&
            MakeFile(L"a\\a.txt", 0) && MakeFile(L"c\\c1\\c11\\deep.txt", 0);
    ok(Ready, "Cannot build the test folders under %ls (%lu)\n", TreeRoot, GetLastError());
    if (!Ready)
    {
        RemoveTree(TreeRoot);
        return;
    }

    StringCchPrintfW(Arguments, _countof(Arguments), L"\"%s\"", TreeRoot);
    ExpectBody(Arguments, TRUE, TreeRoot,
               L"\x251C\x2500\x2500\x2500" L"a\r\n"
               L"\x2502   \x251C\x2500\x2500\x2500" L"a1\r\n"
               L"\x2502   \x2514\x2500\x2500\x2500" L"a2\r\n"
               L"\x251C\x2500\x2500\x2500" L"b\r\n"
               L"\x2514\x2500\x2500\x2500" L"c\r\n"
               L"    \x2514\x2500\x2500\x2500" L"c1\r\n"
               L"        \x2514\x2500\x2500\x2500" L"c11\r\n");

    StringCchPrintfW(Arguments, _countof(Arguments), L"\"%s\" /f", TreeRoot);
    ExpectBody(Arguments, TRUE, TreeRoot,
               L"\x2502   f1.txt\r\n"
               L"\x2502   \r\n"
               L"\x251C\x2500\x2500\x2500" L"a\r\n"
               L"\x2502   \x2502   a.txt\r\n"
               L"\x2502   \x2502   \r\n"
               L"\x2502   \x251C\x2500\x2500\x2500" L"a1\r\n"
               L"\x2502   \x2514\x2500\x2500\x2500" L"a2\r\n"
               L"\x251C\x2500\x2500\x2500" L"b\r\n"
               L"\x2514\x2500\x2500\x2500" L"c\r\n"
               L"    \x2514\x2500\x2500\x2500" L"c1\r\n"
               L"        \x2514\x2500\x2500\x2500" L"c11\r\n"
               L"                deep.txt\r\n"
               L"                \r\n");

    StringCchPrintfW(Arguments, _countof(Arguments), L"/A /F \"%s\\\"", TreeRoot);
    ExpectBody(Arguments, TRUE, TreeRoot,
               L"|   f1.txt\r\n"
               L"|   \r\n"
               L"+---a\r\n"
               L"|   |   a.txt\r\n"
               L"|   |   \r\n"
               L"|   +---a1\r\n"
               L"|   \\---a2\r\n"
               L"+---b\r\n"
               L"\\---c\r\n"
               L"    \\---c1\r\n"
               L"        \\---c11\r\n"
               L"                deep.txt\r\n"
               L"                \r\n");

    StringCchPrintfW(Path, _countof(Path), L"%s\\a\\a1", TreeRoot);
    StringCchPrintfW(Arguments, _countof(Arguments), L"\"%s\"", Path);
    ExpectBody(Arguments, TRUE, Path, L"No subfolders exist \r\n\r\n");

    StringCchPrintfW(Path, _countof(Path), L"%s\\nope", TreeRoot);
    StringCchPrintfW(Arguments, _countof(Arguments), L"\"%s\"", Path);
    {
        WCHAR Expected[MAX_PATH * 2], Upper[MAX_PATH];

        StringCchCopyW(Upper, _countof(Upper), Path + 2);
        CharUpperW(Upper);
        StringCchPrintfW(Expected, _countof(Expected), L"Invalid path - %s\r\nNo subfolders exist \r\n\r\n", Upper);
        ExpectBody(Arguments, TRUE, Path, Expected);
    }

    StringCchPrintfW(Arguments, _countof(Arguments), L"\"%s\" /Q", TreeRoot);
    ExpectBody(Arguments, FALSE, NULL, L"Invalid switch - /Q\r\n");

    ExpectBody(L"x y", FALSE, NULL, L"Too many parameters - y\r\n");

    ExpectBody(L"/?", FALSE, NULL,
               L"Graphically displays the folder structure of a drive or path.\r\n"
               L"\r\n"
               L"TREE [drive:][path] [/F] [/A]\r\n"
               L"\r\n"
               L"   /F   Display the names of the files in each folder.\r\n"
               L"   /A   Use ASCII instead of extended characters.\r\n"
               L"\r\n");

    RemoveTree(TreeRoot);
}
