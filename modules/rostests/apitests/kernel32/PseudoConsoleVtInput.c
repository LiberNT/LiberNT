/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that terminal key sequences reach a pseudo console client reading in virtual terminal input mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef HRESULT (WINAPI *PCREATE_PSEUDO_CONSOLE)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
typedef VOID (WINAPI *PCLOSE_PSEUDO_CONSOLE)(HPCON);

static HANDLE OutputPipe;
static HANDLE ReadyEvent;
static HANDLE NextEvent;
static CHAR Output[16384];
static DWORD OutputLength;

/* Collect the characters of the key-down records until Stop arrives or input stays idle. */
static DWORD
ReadKeyChars(HANDLE Input, WCHAR Stop, WCHAR *Chars, DWORD Size)
{
    DWORD Length = 0;

    while (Length < Size && WaitForSingleObject(Input, 5000) == WAIT_OBJECT_0)
    {
        INPUT_RECORD Records[32];
        DWORD Count, Index;

        if (!ReadConsoleInputW(Input, Records, ARRAYSIZE(Records), &Count))
            break;
        for (Index = 0; Index < Count && Length < Size; Index++)
        {
            KEY_EVENT_RECORD *Key = &Records[Index].Event.KeyEvent;

            if (Records[Index].EventType != KEY_EVENT || !Key->bKeyDown || !Key->uChar.UnicodeChar)
                continue;
            Chars[Length++] = Key->uChar.UnicodeChar;
            if (Key->uChar.UnicodeChar == Stop)
                return Length;
        }
        if (Stop == 0 && Length != 0)
            break;
    }
    return Length;
}

static VOID
WriteChars(HANDLE File, PCSTR Label, const WCHAR *Chars, DWORD Length)
{
    CHAR Line[512];
    DWORD Index, Written;
    size_t Used;

    StringCchPrintfA(Line, ARRAYSIZE(Line), "%s:", Label);
    for (Index = 0; Index < Length; Index++)
    {
        Used = strlen(Line);
        StringCchPrintfA(Line + Used, ARRAYSIZE(Line) - Used, " %02x", Chars[Index]);
    }
    StringCchCatA(Line, ARRAYSIZE(Line), "\n");
    WriteFile(File, Line, (DWORD)strlen(Line), &Written, NULL);
}

static VOID
RunChild(PCSTR Path)
{
    HANDLE Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, 0, NULL);
    HANDLE Out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, 0, NULL);
    HANDLE File = CreateFileA(Path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    WCHAR Chars[64];
    DWORD Count, Length;

    if (File == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    if (!SetConsoleMode(Input, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT))
        ExitProcess(2);
    WriteFile(Out, "vt-ready\r\n", 10, &Count, NULL);
    Length = ReadKeyChars(Input, L'q', Chars, ARRAYSIZE(Chars));
    WriteChars(File, "keys", Chars, Length);
    WriteFile(Out, "vt-next\r\n", 9, &Count, NULL);
    Length = ReadKeyChars(Input, 0, Chars, ARRAYSIZE(Chars));
    WriteChars(File, "escape", Chars, Length);
    CloseHandle(File);
    ExitProcess(45);
}

static DWORD WINAPI
ReaderThread(PVOID Parameter)
{
    CHAR Buffer[1024];
    DWORD Count;

    UNREFERENCED_PARAMETER(Parameter);
    while (ReadFile(OutputPipe, Buffer, sizeof(Buffer), &Count, NULL) && Count)
    {
        if (OutputLength + Count < sizeof(Output))
        {
            memcpy(Output + OutputLength, Buffer, Count);
            OutputLength += Count;
            Output[OutputLength] = '\0';
        }
        if (strstr(Output, "vt-ready"))
            SetEvent(ReadyEvent);
        if (strstr(Output, "vt-next"))
            SetEvent(NextEvent);
    }
    return 0;
}

static VOID
ReadResult(PCSTR Path, PCSTR Label, PSTR Line, DWORD Size)
{
    CHAR Text[1024], *Start, *End;
    HANDLE File;
    DWORD Count = 0;
    size_t Length = strlen(Label);

    Line[0] = '\0';
    File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return;
    ReadFile(File, Text, sizeof(Text) - 1, &Count, NULL);
    CloseHandle(File);
    Text[Count] = '\0';
    for (Start = Text; Start && *Start; Start = End ? End + 1 : NULL)
    {
        End = strchr(Start, '\n');
        if (End)
            *End = '\0';
        if (!strncmp(Start, Label, Length) && Start[Length] == ':')
        {
            StringCchCopyA(Line, Size, Start + Length + 1);
            return;
        }
    }
}

START_TEST(PseudoConsoleVtInput)
{
    STARTUPINFOEXW Startup = { { sizeof(Startup) } };
    PROCESS_INFORMATION Process;
    PCREATE_PSEUDO_CONSOLE Create;
    PCLOSE_PSEUDO_CONSOLE Close;
    HANDLE InputRead, InputWrite, OutputWrite, Reader;
    WCHAR Module[MAX_PATH], CommandLine[2 * MAX_PATH + 64];
    CHAR TempDir[MAX_PATH], Path[MAX_PATH], Line[512];
    COORD Size = { 80, 25 };
    SIZE_T AttributeSize = 0;
    DWORD Written, ExitCode = 0;
    HPCON Console;
    HRESULT Result;
    char **Arguments;

    if (winetest_get_mainargs(&Arguments) >= 4 && !strcmp(Arguments[2], "child"))
    {
        RunChild(Arguments[3]);
        return;
    }

    Create = (PCREATE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreatePseudoConsole");
    Close = (PCLOSE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ClosePseudoConsole");
    if (!Create || !Close)
    {
        skip("The pseudo console functions are not available\n");
        return;
    }

    GetTempPathA(ARRAYSIZE(TempDir), TempDir);
    GetTempFileNameA(TempDir, "vti", 0, Path);
    ReadyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    NextEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(CreatePipe(&InputRead, &InputWrite, NULL, 0), "CreatePipe failed with %lu\n", GetLastError());
    ok(CreatePipe(&OutputPipe, &OutputWrite, NULL, 0), "CreatePipe failed with %lu\n", GetLastError());
    Result = Create(Size, InputRead, OutputWrite, 0, &Console);
    ok_hex(Result, S_OK);
    CloseHandle(InputRead);
    CloseHandle(OutputWrite);
    if (FAILED(Result))
        return;
    Reader = CreateThread(NULL, 0, ReaderThread, NULL, 0, NULL);

    InitializeProcThreadAttributeList(NULL, 1, 0, &AttributeSize);
    Startup.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, AttributeSize);
    ok(Startup.lpAttributeList && InitializeProcThreadAttributeList(Startup.lpAttributeList, 1, 0, &AttributeSize),
       "attribute list failed\n");
    ok(UpdateProcThreadAttribute(Startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, Console,
                                 sizeof(Console), NULL, NULL), "UpdateProcThreadAttribute failed with %lu\n", GetLastError());
    GetModuleFileNameW(NULL, Module, MAX_PATH);
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" PseudoConsoleVtInput child \"%S\"", Module, Path);
    if (CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                       &Startup.StartupInfo, &Process))
    {
        ok(WaitForSingleObject(ReadyEvent, 5000) == WAIT_OBJECT_0, "The child did not start\n");
        /* Up, Up in application cursor mode, Delete, Ctrl+Right and Backspace as a terminal sends them. */
        ok(WriteFile(InputWrite, "\x1b[A\x1bOA\x1b[3~\x1b[1;5C\x7fq", 19, &Written, NULL),
           "WriteFile failed with %lu\n", GetLastError());
        ok(WaitForSingleObject(NextEvent, 5000) == WAIT_OBJECT_0, "The child did not read the keys\n");
        ok(WriteFile(InputWrite, "\x1b", 1, &Written, NULL), "WriteFile failed with %lu\n", GetLastError());
        if (WaitForSingleObject(Process.hProcess, 10000) != WAIT_OBJECT_0)
        {
            ok(FALSE, "The child did not exit\n");
            TerminateProcess(Process.hProcess, 99);
            WaitForSingleObject(Process.hProcess, 5000);
        }
        GetExitCodeProcess(Process.hProcess, &ExitCode);
        ok_long(ExitCode, 45);
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);

        ReadResult(Path, "keys", Line, sizeof(Line));
        ok(!strcmp(Line, " 1b 5b 41 1b 4f 41 1b 5b 33 7e 1b 5b 31 3b 35 43 7f 71"),
           "The key sequences reached the client as '%s'\n", Line);
        ReadResult(Path, "escape", Line, sizeof(Line));
        ok(!strcmp(Line, " 1b"), "Escape reached the client as '%s'\n", Line);
    }
    else
    {
        ok(FALSE, "CreateProcessW failed with %lu\n", GetLastError());
    }
    DeleteProcThreadAttributeList(Startup.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, Startup.lpAttributeList);

    Close(Console);
    CloseHandle(InputWrite);
    if (WaitForSingleObject(Reader, 5000) != WAIT_OBJECT_0)
        CancelSynchronousIo(Reader);
    CloseHandle(Reader);
    CloseHandle(OutputPipe);
    CloseHandle(ReadyEvent);
    CloseHandle(NextEvent);
    DeleteFileA(Path);
}
