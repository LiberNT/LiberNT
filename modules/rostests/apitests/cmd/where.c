/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the output and exit codes of where.exe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <strsafe.h>

#define WHERE_OUTPUT_SIZE 8192

static WCHAR WhereRoot[MAX_PATH];
static CHAR WhereRootA[MAX_PATH];

static DWORD
RunWhere(PCWSTR Arguments, PSTR Output, PSTR Errors)
{
    WCHAR Command[MAX_PATH * 4];
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, TRUE };
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    HANDLE OutRead, OutWrite, ErrRead, ErrWrite;
    DWORD Chunk, Length, ExitCode = 0xdeadbeef;

    Output[0] = Errors[0] = 0;
    StringCchPrintfW(Command, _countof(Command), L"where.exe %s", Arguments);
    if (!CreatePipe(&OutRead, &OutWrite, &Attributes, 0))
        return ExitCode;
    if (!CreatePipe(&ErrRead, &ErrWrite, &Attributes, 0))
    {
        CloseHandle(OutRead);
        CloseHandle(OutWrite);
        return ExitCode;
    }
    SetHandleInformation(OutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(ErrRead, HANDLE_FLAG_INHERIT, 0);
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    Startup.hStdOutput = OutWrite;
    Startup.hStdError = ErrWrite;
    if (!CreateProcessW(NULL, Command, NULL, NULL, TRUE, 0, NULL, WhereRoot, &Startup, &Process))
    {
        ok(FALSE, "CreateProcess(%ls) failed: %lu\n", Command, GetLastError());
        CloseHandle(OutRead);
        CloseHandle(OutWrite);
        CloseHandle(ErrRead);
        CloseHandle(ErrWrite);
        return ExitCode;
    }
    CloseHandle(OutWrite);
    CloseHandle(ErrWrite);
    for (Length = 0; Length < WHERE_OUTPUT_SIZE - 1 &&
         ReadFile(OutRead, Output + Length, WHERE_OUTPUT_SIZE - 1 - Length, &Chunk, NULL) && Chunk != 0;
         Length += Chunk);
    Output[Length] = 0;
    for (Length = 0; Length < WHERE_OUTPUT_SIZE - 1 &&
         ReadFile(ErrRead, Errors + Length, WHERE_OUTPUT_SIZE - 1 - Length, &Chunk, NULL) && Chunk != 0;
         Length += Chunk);
    Errors[Length] = 0;
    ok(WaitForSingleObject(Process.hProcess, 10000) == WAIT_OBJECT_0, "where.exe %ls did not exit\n", Arguments);
    GetExitCodeProcess(Process.hProcess, &ExitCode);
    CloseHandle(Process.hProcess);
    CloseHandle(Process.hThread);
    CloseHandle(OutRead);
    CloseHandle(ErrRead);
    return ExitCode;
}

static void
MakeFile(PCWSTR Relative)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;

    StringCchPrintfW(Path, _countof(Path), L"%s\\%s", WhereRoot, Relative);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%ls) failed: %lu\n", Path, GetLastError());
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
}

static void
MakeDirectory(PCWSTR Relative)
{
    WCHAR Path[MAX_PATH];

    StringCchPrintfW(Path, _countof(Path), L"%s\\%s", WhereRoot, Relative);
    ok(CreateDirectoryW(Path, NULL), "CreateDirectory(%ls) failed: %lu\n", Path, GetLastError());
}

static void
Remove(PCWSTR Relative, BOOL Directory)
{
    WCHAR Path[MAX_PATH];

    StringCchPrintfW(Path, _countof(Path), L"%s\\%s", WhereRoot, Relative);
    if (Directory)
        RemoveDirectoryW(Path);
    else
        DeleteFileW(Path);
}

static void
Expect(PCSTR Name, PCSTR Actual, PCSTR Lines)
{
    CHAR Expected[WHERE_OUTPUT_SIZE];
    PCSTR Line;
    PSTR Out = Expected;

    Expected[0] = 0;
    for (Line = Lines; *Line; Line += strlen(Line) + 1)
    {
        if (Line[0] == '\\' || Line[0] == '"')
            StringCchPrintfA(Out, Expected + sizeof(Expected) - Out, Line[0] == '"' ? "\"%s%s\r\n" : "%s%s\r\n",
                             WhereRootA, Line[0] == '"' ? Line + 1 : Line);
        else
            StringCchPrintfA(Out, Expected + sizeof(Expected) - Out, "%s\r\n", Line);
        Out += strlen(Out);
    }
    ok(!strcmp(Actual, Expected), "%s: output '%s', expected '%s'\n", Name, Actual, Expected);
}

START_TEST(where)
{
    CHAR Output[WHERE_OUTPUT_SIZE], Errors[WHERE_OUTPUT_SIZE];
    WCHAR Temp[MAX_PATH], Arguments[MAX_PATH * 2], Final[MAX_PATH + 8] = L"";
    HANDLE Directory;
    DWORD ExitCode;

    GetTempPathW(_countof(Temp), Temp);
    StringCchPrintfW(WhereRoot, _countof(WhereRoot), L"%swhere-%lu", Temp, GetCurrentProcessId());
    ok(CreateDirectoryW(WhereRoot, NULL), "CreateDirectory(%ls) failed: %lu\n", WhereRoot, GetLastError());
    /* where.exe prints every directory with the case it has on disk */
    Directory = CreateFileW(WhereRoot, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(Directory != INVALID_HANDLE_VALUE, "CreateFile(%ls) failed: %lu\n", WhereRoot, GetLastError());
    if (Directory != INVALID_HANDLE_VALUE)
    {
        ok(GetFinalPathNameByHandleW(Directory, Final, _countof(Final), VOLUME_NAME_DOS) != 0,
           "GetFinalPathNameByHandle failed: %lu\n", GetLastError());
        CloseHandle(Directory);
    }
    WideCharToMultiByte(CP_ACP, 0, wcsncmp(Final, L"\\\\?\\", 4) ? Final : Final + 4, -1, WhereRootA, sizeof(WhereRootA), NULL, NULL);
    MakeDirectory(L"a");
    MakeDirectory(L"a\\b");
    MakeDirectory(L"a\\b\\leaf");
    MakeDirectory(L"empty");
    MakeFile(L"one.txt");
    MakeFile(L"a\\two.txt");
    MakeFile(L"a\\b\\leaf\\three.txt");
    MakeFile(L"a\\b\\leaf\\tool.exe");

    StringCchPrintfW(Arguments, _countof(Arguments), L"/r \"%s\" three.txt", WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 0, "recursive single match: exit %lu, errors '%s'\n", ExitCode, Errors);
    Expect("recursive single match", Output, "\\a\\b\\leaf\\three.txt\0");
    ok(Errors[0] == 0, "recursive single match: errors '%s'\n", Errors);

    StringCchPrintfW(Arguments, _countof(Arguments), L"/r \"%s\" *.txt", WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 0, "recursive wildcard: exit %lu, errors '%s'\n", ExitCode, Errors);
    Expect("recursive wildcard", Output, "\\one.txt\0\\a\\two.txt\0\\a\\b\\leaf\\three.txt\0");

    StringCchPrintfW(Arguments, _countof(Arguments), L"/r \"%s\" tool", WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 0, "recursive PATHEXT match: exit %lu, errors '%s'\n", ExitCode, Errors);
    Expect("recursive PATHEXT match", Output, "\\a\\b\\leaf\\tool.exe\0");

    StringCchPrintfW(Arguments, _countof(Arguments), L"/r \"%s\" missing.xyz", WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 1, "recursive no match: exit %lu\n", ExitCode);
    ok(Output[0] == 0, "recursive no match: output '%s'\n", Output);
    ok(!strcmp(Errors, "INFO: Could not find files for the given pattern(s).\r\n"), "recursive no match: errors '%s'\n", Errors);

    StringCchPrintfW(Arguments, _countof(Arguments), L"/q /r \"%s\" three.txt", WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 0, "quiet match: exit %lu\n", ExitCode);
    ok(Output[0] == 0 && Errors[0] == 0, "quiet match: output '%s' errors '%s'\n", Output, Errors);

    StringCchPrintfW(Arguments, _countof(Arguments), L"/q /r \"%s\" missing.xyz", WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 1, "quiet no match: exit %lu\n", ExitCode);
    ok(Output[0] == 0 && Errors[0] == 0, "quiet no match: output '%s' errors '%s'\n", Output, Errors);

    StringCchPrintfW(Arguments, _countof(Arguments), L"/f /r \"%s\" two.txt", WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 0, "quoted match: exit %lu\n", ExitCode);
    Expect("quoted match", Output, "\"\\a\\two.txt\"\0");

    StringCchPrintfW(Arguments, _countof(Arguments), L"\"%s\\a:two.txt\"", WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 0, "path pattern: exit %lu, errors '%s'\n", ExitCode, Errors);
    Expect("path pattern", Output, "\\a\\two.txt\0");

    StringCchPrintfW(Arguments, _countof(Arguments), L"\"%s:two.txt\"", WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 1, "path pattern outside the directory: exit %lu, output '%s'\n", ExitCode, Output);

    ExitCode = RunWhere(L"one.txt", Output, Errors);
    ok(ExitCode == 0, "current directory: exit %lu, errors '%s'\n", ExitCode, Errors);
    Expect("current directory", Output, "\\one.txt\0");

    StringCchPrintfW(Arguments, _countof(Arguments), L"/r \"%s\\empty\" *", WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 1, "empty directory: exit %lu, output '%s' errors '%s'\n", ExitCode, Output, Errors);

    StringCchPrintfW(Arguments, _countof(Arguments), L"/r \"%s\\nosuchdir\" one.txt", WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 2, "missing directory: exit %lu, output '%s' errors '%s'\n", ExitCode, Output, Errors);
    ok(!strcmp(Errors, "ERROR: The system cannot find the file specified.\r\r\n"), "missing directory: errors '%s'\n", Errors);

    StringCchPrintfW(Arguments, _countof(Arguments), L"/r \"%s\" /r \"%s\" one.txt", WhereRoot, WhereRoot);
    ExitCode = RunWhere(Arguments, Output, Errors);
    ok(ExitCode == 2, "repeated /r: exit %lu, errors '%s'\n", ExitCode, Errors);
    ok(!strcmp(Errors, "ERROR: Invalid syntax. '/r' option is not allowed more than '1' time(s).\r\n"
                       "Type \"WHERE /?\" for usage.\r\n"), "repeated /r: errors '%s'\n", Errors);

    Remove(L"a\\b\\leaf\\tool.exe", FALSE);
    Remove(L"a\\b\\leaf\\three.txt", FALSE);
    Remove(L"a\\two.txt", FALSE);
    Remove(L"one.txt", FALSE);
    Remove(L"empty", TRUE);
    Remove(L"a\\b\\leaf", TRUE);
    Remove(L"a\\b", TRUE);
    Remove(L"a", TRUE);
    RemoveDirectoryW(WhereRoot);
}
