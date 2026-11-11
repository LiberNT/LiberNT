/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that exit writes the buffered tail of stdout to a pipe and to a file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_COUNT 300
#define CHILD_LIMIT_MS 10000
#define PIPE_QUOTA 65536
#define TEXT_LIMIT 32768

static int
FormatLine(char *Buffer, size_t Size, int Index)
{
    if (Index < LINE_COUNT)
        return _snprintf(Buffer, Size, "ExitFlush line %03d abcdefghijklmnopqrstuvwxyz\n", Index);
    return _snprintf(Buffer, Size, "ExitFlush end\n");
}

static void
RunChild(void)
{
    char Line[64];
    int Index;

    setvbuf(stdout, NULL, _IOFBF, 4096);
    for (Index = 0; Index <= LINE_COUNT; Index++)
    {
        FormatLine(Line, sizeof(Line), Index);
        fputs(Line, stdout);
    }
    exit(0);
}

static DWORD
BuildExpected(char *Buffer)
{
    char Line[64];
    DWORD Length = 0;
    int Index, Count, Char;

    for (Index = 0; Index <= LINE_COUNT; Index++)
    {
        Count = FormatLine(Line, sizeof(Line), Index);
        for (Char = 0; Char < Count; Char++)
        {
            if (Line[Char] == '\n')
                Buffer[Length++] = '\r';
            Buffer[Length++] = Line[Char];
        }
    }
    return Length;
}

static BOOL
StartChild(HANDLE Output, PROCESS_INFORMATION *Info)
{
    STARTUPINFOA Startup = { sizeof(Startup) };
    char Command[MAX_PATH + 64];
    char **Argv;

    winetest_get_mainargs(&Argv);
    _snprintf(Command, sizeof(Command), "\"%s\" ExitFlush child", Argv[0]);
    Command[sizeof(Command) - 1] = 0;
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdInput = NULL;
    Startup.hStdOutput = Output;
    Startup.hStdError = Output;
    return CreateProcessA(NULL, Command, NULL, NULL, TRUE, 0, NULL, NULL, &Startup, Info);
}

static BOOL
WaitChild(const char *Target, PROCESS_INFORMATION *Info)
{
    DWORD Wait, Code = 0xdeadbeef;

    Wait = WaitForSingleObject(Info->hProcess, CHILD_LIMIT_MS);
    ok(Wait == WAIT_OBJECT_0, "%s: child did not exit: %lu\n", Target, Wait);
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(Info->hProcess, 1);
    ok(GetExitCodeProcess(Info->hProcess, &Code), "%s: GetExitCodeProcess failed: %lu\n", Target, GetLastError());
    ok(Code == 0, "%s: child exit code %lu\n", Target, Code);
    CloseHandle(Info->hThread);
    CloseHandle(Info->hProcess);
    return Wait == WAIT_OBJECT_0;
}

static void
CheckText(const char *Target, const char *Received, DWORD ReceivedLength)
{
    static char Expected[TEXT_LIMIT];
    DWORD ExpectedLength = BuildExpected(Expected);

    ok(ReceivedLength == ExpectedLength, "%s: received %lu bytes, expected %lu\n",
       Target, ReceivedLength, ExpectedLength);
    ok(ReceivedLength == ExpectedLength && memcmp(Received, Expected, ExpectedLength) == 0,
       "%s: the text written before exit differs\n", Target);
}

static void
TestPipe(void)
{
    static char Received[TEXT_LIMIT];
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, TRUE };
    PROCESS_INFORMATION Info = { 0 };
    HANDLE Read, Write;
    DWORD Total = 0, Bytes;

    if (!CreatePipe(&Read, &Write, &Attributes, PIPE_QUOTA))
    {
        ok(0, "pipe: CreatePipe failed: %lu\n", GetLastError());
        return;
    }
    SetHandleInformation(Read, HANDLE_FLAG_INHERIT, 0);
    ok(StartChild(Write, &Info), "pipe: CreateProcess failed: %lu\n", GetLastError());
    CloseHandle(Write);
    if (Info.hProcess != NULL && WaitChild("pipe", &Info))
    {
        while (Total < sizeof(Received) &&
               ReadFile(Read, Received + Total, sizeof(Received) - Total, &Bytes, NULL) && Bytes != 0)
        {
            Total += Bytes;
        }
        CheckText("pipe", Received, Total);
    }
    CloseHandle(Read);
}

static void
TestFile(void)
{
    static char Received[TEXT_LIMIT];
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, TRUE };
    PROCESS_INFORMATION Info = { 0 };
    char Path[MAX_PATH];
    HANDLE File;
    DWORD Size, Bytes = 0;

    GetTempPathA(MAX_PATH - 16, Path);
    strcat(Path, "ExitFlush.txt");
    File = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &Attributes,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        ok(0, "file: CreateFile failed: %lu\n", GetLastError());
        return;
    }
    ok(StartChild(File, &Info), "file: CreateProcess failed: %lu\n", GetLastError());
    if (Info.hProcess != NULL && WaitChild("file", &Info))
    {
        Size = GetFileSize(File, NULL);
        ok(SetFilePointer(File, 0, NULL, FILE_BEGIN) == 0, "file: SetFilePointer failed: %lu\n", GetLastError());
        ok(Size <= sizeof(Received), "file: %lu bytes\n", Size);
        if (Size <= sizeof(Received))
            ok(ReadFile(File, Received, Size, &Bytes, NULL), "file: ReadFile failed: %lu\n", GetLastError());
        CheckText("file", Received, Bytes);
    }
    CloseHandle(File);
}

START_TEST(ExitFlush)
{
    char **Argv;

    if (winetest_get_mainargs(&Argv) >= 3 && strcmp(Argv[2], "child") == 0)
    {
        RunChild();
        return;
    }

    TestPipe();
    TestFile();
}
