/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that Ctrl+C written to a pseudo console reaches its client and keeps input working
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef HRESULT (WINAPI *PCREATE_PSEUDO_CONSOLE)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
typedef VOID (WINAPI *PCLOSE_PSEUDO_CONSOLE)(HPCON);

static HANDLE ChildCtrlEvent;
static HANDLE OutputPipe;
static HANDLE ReadyEvent;
static HANDLE CtrlSeenEvent;
static HANDLE StdSeenEvent;
static CHAR Output[16384];
static DWORD OutputLength;

static BOOL WINAPI
ChildCtrlHandler(DWORD Type)
{
    if (Type != CTRL_C_EVENT)
        return FALSE;
    SetEvent(ChildCtrlEvent);
    return TRUE;
}

static VOID
RunChild(VOID)
{
    HANDLE Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, 0, NULL);
    HANDLE Out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, 0, NULL);
    CHAR Line[64];
    DWORD Count, Mode = 0;

    ChildCtrlEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetConsoleCtrlHandler(ChildCtrlHandler, TRUE);
    GetConsoleMode(Input, &Mode);
    SetConsoleMode(Input, Mode | ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    WriteFile(Out, "child-ready\r\n", 13, &Count, NULL);
    if (WaitForSingleObject(ChildCtrlEvent, 5000) != WAIT_OBJECT_0)
        ExitProcess(1);
    WriteFile(Out, "child-ctrlc\r\n", 13, &Count, NULL);
    if (!ReadFile(Input, Line, sizeof(Line) - 1, &Count, NULL))
        ExitProcess(2);
    Line[Count] = '\0';
    ExitProcess(strncmp(Line, "after", 5) == 0 ? 43 : 3);
}

static VOID
RunStdChild(VOID)
{
    DWORD Count;

    ExitProcess(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "std-ready\r\n", 11, &Count, NULL) ? 44 : 4);
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
        if (strstr(Output, "child-ready"))
            SetEvent(ReadyEvent);
        if (strstr(Output, "child-ctrlc"))
            SetEvent(CtrlSeenEvent);
        if (strstr(Output, "std-ready"))
            SetEvent(StdSeenEvent);
    }
    return 0;
}

static BOOL
OpenConsole(PCREATE_PSEUDO_CONSOLE Create, HPCON *Console, HANDLE *InputWrite, HANDLE *Reader)
{
    HANDLE InputRead, OutputWrite;
    COORD Size = { 80, 25 };
    HRESULT Result;

    OutputLength = 0;
    Output[0] = '\0';
    ok(CreatePipe(&InputRead, InputWrite, NULL, 0), "CreatePipe failed with %lu\n", GetLastError());
    ok(CreatePipe(&OutputPipe, &OutputWrite, NULL, 0), "CreatePipe failed with %lu\n", GetLastError());
    Result = Create(Size, InputRead, OutputWrite, 0, Console);
    ok_hex(Result, S_OK);
    CloseHandle(InputRead);
    CloseHandle(OutputWrite);
    if (FAILED(Result))
    {
        CloseHandle(*InputWrite);
        CloseHandle(OutputPipe);
        return FALSE;
    }
    *Reader = CreateThread(NULL, 0, ReaderThread, NULL, 0, NULL);
    return TRUE;
}

static VOID
CloseConsole(PCLOSE_PSEUDO_CONSOLE Close, HPCON Console, HANDLE InputWrite, HANDLE Reader)
{
    Close(Console);
    CloseHandle(InputWrite);
    if (WaitForSingleObject(Reader, 5000) != WAIT_OBJECT_0)
        CancelSynchronousIo(Reader);
    CloseHandle(Reader);
    CloseHandle(OutputPipe);
}

static BOOL
StartChild(HPCON Console, PCWSTR Mode, BOOL NullStdHandles, PROCESS_INFORMATION *Process)
{
    STARTUPINFOEXW Startup = { { sizeof(Startup) } };
    WCHAR Module[MAX_PATH], CommandLine[MAX_PATH + 64];
    SIZE_T AttributeSize = 0;
    BOOL Created;

    if (NullStdHandles)
        Startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    InitializeProcThreadAttributeList(NULL, 1, 0, &AttributeSize);
    Startup.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, AttributeSize);
    if (!Startup.lpAttributeList)
    {
        skip("Out of memory\n");
        return FALSE;
    }
    ok(InitializeProcThreadAttributeList(Startup.lpAttributeList, 1, 0, &AttributeSize), "attribute list failed\n");
    ok(UpdateProcThreadAttribute(Startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, Console,
                                 sizeof(Console), NULL, NULL), "UpdateProcThreadAttribute failed with %lu\n", GetLastError());
    GetModuleFileNameW(NULL, Module, MAX_PATH);
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" PseudoConsoleCtrlC %s", Module, Mode);
    Created = CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                             &Startup.StartupInfo, Process);
    ok(Created, "CreateProcessW failed with %lu\n", GetLastError());
    DeleteProcThreadAttributeList(Startup.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, Startup.lpAttributeList);
    return Created;
}

static DWORD
FinishChild(PROCESS_INFORMATION *Process)
{
    DWORD ExitCode = 0;

    if (WaitForSingleObject(Process->hProcess, 5000) != WAIT_OBJECT_0)
    {
        ok(FALSE, "The child did not exit\n");
        TerminateProcess(Process->hProcess, 99);
        WaitForSingleObject(Process->hProcess, 5000);
    }
    GetExitCodeProcess(Process->hProcess, &ExitCode);
    CloseHandle(Process->hThread);
    CloseHandle(Process->hProcess);
    return ExitCode;
}

static VOID
TestCtrlC(PCREATE_PSEUDO_CONSOLE Create, PCLOSE_PSEUDO_CONSOLE Close)
{
    PROCESS_INFORMATION Process;
    HANDLE InputWrite, Reader;
    HPCON Console;
    DWORD Written;

    if (!OpenConsole(Create, &Console, &InputWrite, &Reader))
        return;
    if (StartChild(Console, L"child", FALSE, &Process))
    {
        ok(WaitForSingleObject(ReadyEvent, 5000) == WAIT_OBJECT_0, "The child did not start\n");
        ok(WriteFile(InputWrite, "\x03", 1, &Written, NULL), "WriteFile failed with %lu\n", GetLastError());
        ok(WaitForSingleObject(CtrlSeenEvent, 5000) == WAIT_OBJECT_0, "The child did not receive CTRL_C_EVENT\n");
        ok(WriteFile(InputWrite, "after\r", 6, &Written, NULL), "WriteFile failed with %lu\n", GetLastError());
        ok_long(FinishChild(&Process), 43);
    }
    CloseConsole(Close, Console, InputWrite, Reader);
}

static VOID
TestNullStdHandles(PCREATE_PSEUDO_CONSOLE Create, PCLOSE_PSEUDO_CONSOLE Close)
{
    PROCESS_INFORMATION Process;
    HANDLE InputWrite, Reader;
    HPCON Console;

    if (!OpenConsole(Create, &Console, &InputWrite, &Reader))
        return;
    if (StartChild(Console, L"stdchild", TRUE, &Process))
    {
        ok_long(FinishChild(&Process), 44);
        ok(WaitForSingleObject(StdSeenEvent, 5000) == WAIT_OBJECT_0,
           "The standard output did not reach the pseudo console\n");
    }
    CloseConsole(Close, Console, InputWrite, Reader);
}

START_TEST(PseudoConsoleCtrlC)
{
    PCREATE_PSEUDO_CONSOLE Create;
    PCLOSE_PSEUDO_CONSOLE Close;
    char **Arguments;

    if (winetest_get_mainargs(&Arguments) >= 3)
    {
        if (!strcmp(Arguments[2], "child"))
            RunChild();
        if (!strcmp(Arguments[2], "stdchild"))
            RunStdChild();
        return;
    }

    Create = (PCREATE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreatePseudoConsole");
    Close = (PCLOSE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ClosePseudoConsole");
    if (!Create || !Close)
    {
        skip("The pseudo console functions are not available\n");
        return;
    }

    ReadyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    CtrlSeenEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    StdSeenEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    TestCtrlC(Create, Close);
    TestNullStdHandles(Create, Close);
    CloseHandle(ReadyEvent);
    CloseHandle(CtrlSeenEvent);
    CloseHandle(StdSeenEvent);
}
