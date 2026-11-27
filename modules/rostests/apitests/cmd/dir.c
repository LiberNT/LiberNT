/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the volume header, totals and no-match message of DIR against Windows 11
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <strsafe.h>
#include <stdio.h>

static char *
RunCmd(PCWSTR Cmd, PCWSTR Arguments, BOOL Stderr)
{
    SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    WCHAR Line[1024];
    HANDLE Read, Write, Null;
    static char Output[16384];
    DWORD Total = 0, Got;

    Output[0] = 0;
    if (!CreatePipe(&Read, &Write, &Inherit, 0))
        return Output;
    SetHandleInformation(Read, HANDLE_FLAG_INHERIT, 0);
    Null = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &Inherit, OPEN_EXISTING, 0, NULL);
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = NULL;
    Startup.hStdOutput = Stderr ? Null : Write;
    Startup.hStdError = Stderr ? Write : Null;
    StringCchPrintfW(Line, ARRAYSIZE(Line), L"\"%s\" /c %s", Cmd, Arguments);
    if (CreateProcessW(Cmd, Line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Process))
    {
        CloseHandle(Write);
        Write = NULL;
        while (Total < sizeof(Output) - 1 && ReadFile(Read, Output + Total, sizeof(Output) - 1 - Total, &Got, NULL) && Got)
            Total += Got;
        WaitForSingleObject(Process.hProcess, 10000);
        CloseHandle(Process.hProcess);
        CloseHandle(Process.hThread);
    }
    Output[Total] = 0;
    if (Write)
        CloseHandle(Write);
    CloseHandle(Read);
    CloseHandle(Null);
    return Output;
}

static const char *
FindLine(const char *Text, const char *Prefix)
{
    const char *Line = Text;

    while (Line && *Line)
    {
        if (!strncmp(Line, Prefix, strlen(Prefix)))
            return Line;
        Line = strchr(Line, '\n');
        if (Line)
            Line++;
    }
    return NULL;
}

static size_t
LineLength(const char *Line)
{
    return strcspn(Line, "\r\n");
}

START_TEST(dir)
{
    WCHAR Cmd[MAX_PATH], Dir[MAX_PATH], Arguments[MAX_PATH + 32];
    char Expected[MAX_PATH + 32], *Output;
    const char *Line;

    GetSystemDirectoryW(Cmd, ARRAYSIZE(Cmd));
    StringCchCatW(Cmd, ARRAYSIZE(Cmd), L"\\cmd.exe");
    GetWindowsDirectoryW(Dir, ARRAYSIZE(Dir));
    StringCchPrintfW(Dir, ARRAYSIZE(Dir), L"%c:\\Users\\Public\\dirtest919", Dir[0]);
    CreateDirectoryW(Dir, NULL);

    StringCchPrintfW(Arguments, ARRAYSIZE(Arguments), L"dir \"%s\"", Dir);
    Output = RunCmd(Cmd, Arguments, FALSE);
    ok(FindLine(Output, " Volume in drive ") != NULL, "no \" Volume in drive\" line in:\n%s\n", Output);
    ok(FindLine(Output, " Volume Serial Number is ") != NULL, "no \" Volume Serial Number is\" line\n");
    StringCchPrintfA(Expected, ARRAYSIZE(Expected), " Directory of %ls", Dir);
    ok(FindLine(Output, Expected) != NULL, "no \"%s\" line\n", Expected);
    Line = FindLine(Output, "               0 File(s)");
    ok(Line && LineLength(Line) == 45 && !strncmp(Line + 24, "              0 bytes", 21),
       "file total: \"%.*s\"\n", Line ? (int)LineLength(Line) : 0, Line ? Line : "");
    Line = FindLine(Output, "               2 Dir(s)");
    ok(Line && LineLength(Line) == 50 && !strncmp(Line + LineLength(Line) - 11, " bytes free", 11),
       "directory total: \"%.*s\"\n", Line ? (int)LineLength(Line) : 0, Line ? Line : "");

    StringCchPrintfW(Arguments, ARRAYSIZE(Arguments), L"dir \"%s\\*.xyz\"", Dir);
    Output = RunCmd(Cmd, Arguments, TRUE);
    ok(!strcmp(Output, "File Not Found\r\n"), "no match, stderr: \"%s\"\n", Output);
    Output = RunCmd(Cmd, Arguments, FALSE);
    ok(FindLine(Output, Expected) != NULL, "no match, stdout has no \"%s\" line:\n%s\n", Expected, Output);
    ok(strstr(Output, "File(s)") == NULL, "no match, stdout has totals:\n%s\n", Output);

    RemoveDirectoryW(Dir);
}
