/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the Yes and No answers of cmd.exe confirmation prompts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <stdio.h>
#include <strsafe.h>

#define DEL_FILE "confirm-del.txt"
#define COPY_SOURCE "confirm-src.txt"
#define COPY_TARGET "confirm-dst.txt"

static DWORD RunCmd(const char *Command)
{
    STARTUPINFOA Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    CHAR CommandLine[MAX_PATH];
    DWORD ExitCode = MAXDWORD;

    StringCbPrintfA(CommandLine, sizeof(CommandLine), "cmd.exe /d /c \"%s\"", Command);
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        ok(0, "CreateProcessA(%s) failed with %lu\n", CommandLine, GetLastError());
        return ExitCode;
    }
    ok(WaitForSingleObject(Process.hProcess, 10000) == WAIT_OBJECT_0, "%s did not exit\n", CommandLine);
    TerminateProcess(Process.hProcess, MAXDWORD);
    GetExitCodeProcess(Process.hProcess, &ExitCode);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return ExitCode;
}

static void WriteText(const char *Name, const char *Text)
{
    FILE *File = fopen(Name, "wb");

    ok(File != NULL, "fopen(%s) failed\n", Name);
    if (!File)
        return;
    fputs(Text, File);
    fclose(File);
}

static void CheckText(int Line, const char *Name, const char *Text)
{
    CHAR Buffer[32] = "";
    FILE *File = fopen(Name, "rb");

    ok(File != NULL, "Line %d: fopen(%s) failed\n", Line, Name);
    if (!File)
        return;
    fgets(Buffer, sizeof(Buffer), File);
    fclose(File);
    ok(!strcmp(Buffer, Text), "Line %d: %s holds '%s', expected '%s'\n", Line, Name, Buffer, Text);
}

START_TEST(confirm)
{
    WriteText(DEL_FILE, "x");
    RunCmd("echo N| del /p " DEL_FILE " >nul");
    ok(GetFileAttributesA(DEL_FILE) != INVALID_FILE_ATTRIBUTES, "DEL /P deleted the file on No\n");
    RunCmd("echo Y| del /p " DEL_FILE " >nul");
    ok(GetFileAttributesA(DEL_FILE) == INVALID_FILE_ATTRIBUTES, "DEL /P kept the file on Yes\n");
    DeleteFileA(DEL_FILE);

    WriteText(COPY_SOURCE, "new");
    WriteText(COPY_TARGET, "old");
    RunCmd("echo N| copy /-y " COPY_SOURCE " " COPY_TARGET " >nul");
    CheckText(__LINE__, COPY_TARGET, "old");
    RunCmd("echo Y| copy /-y " COPY_SOURCE " " COPY_TARGET " >nul");
    CheckText(__LINE__, COPY_TARGET, "new");
    DeleteFileA(COPY_SOURCE);
    DeleteFileA(COPY_TARGET);

    ok_long(RunCmd("echo Y| choice /n >nul"), 1);
    ok_long(RunCmd("echo N| choice /n >nul"), 2);
}
