/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ResizePseudoConsole
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef HRESULT (WINAPI *PCREATE_PSEUDO_CONSOLE)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
typedef VOID (WINAPI *PCLOSE_PSEUDO_CONSOLE)(HPCON);
typedef HRESULT (WINAPI *PRESIZE_PSEUDO_CONSOLE)(HPCON, COORD);

#define RESIZE_PHASES 4

typedef struct _RESIZE_SHARED
{
    SHORT Cols[RESIZE_PHASES];
    SHORT Rows[RESIZE_PHASES];
    BOOL Ok[RESIZE_PHASES];
    CONSOLE_SCREEN_BUFFER_INFO Info[RESIZE_PHASES];
} RESIZE_SHARED;

static HANDLE OutputPipe;

static VOID
GetSharedNames(DWORD ParentId, PWSTR Mapping, PWSTR Go, PWSTR Done, SIZE_T Size)
{
    StringCchPrintfW(Mapping, Size, L"Local\\ResizePseudoConsoleMap%lu", ParentId);
    StringCchPrintfW(Go, Size, L"Local\\ResizePseudoConsoleGo%lu", ParentId);
    StringCchPrintfW(Done, Size, L"Local\\ResizePseudoConsoleDone%lu", ParentId);
}

static VOID
RunChild(DWORD ParentId)
{
    WCHAR MappingName[64], GoName[64], DoneName[64];
    CONSOLE_SCREEN_BUFFER_INFO *Info;
    RESIZE_SHARED *Shared;
    HANDLE Mapping, Go, Done, Output;
    ULONG Phase, Tries;

    GetSharedNames(ParentId, MappingName, GoName, DoneName, ARRAYSIZE(MappingName));
    Mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MappingName);
    Shared = Mapping ? MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*Shared)) : NULL;
    Go = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, GoName);
    Done = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, DoneName);
    Output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    if (!Shared || !Go || !Done || Output == INVALID_HANDLE_VALUE)
        ExitProcess(1);

    for (Phase = 0; Phase < RESIZE_PHASES; Phase++)
    {
        if (WaitForSingleObject(Go, 10000) != WAIT_OBJECT_0)
            ExitProcess(2);
        Info = &Shared->Info[Phase];
        for (Tries = 0; Tries < 100; Tries++)
        {
            Shared->Ok[Phase] = GetConsoleScreenBufferInfo(Output, Info);
            if (!Shared->Ok[Phase] ||
                (Info->dwSize.X == Shared->Cols[Phase] &&
                 Info->srWindow.Bottom - Info->srWindow.Top + 1 == Shared->Rows[Phase]))
            {
                break;
            }
            Sleep(20);
        }
        SetEvent(Done);
    }
    ExitProcess(0);
}

static DWORD WINAPI
ReaderThread(PVOID Parameter)
{
    CHAR Buffer[1024];
    DWORD Count;

    UNREFERENCED_PARAMETER(Parameter);
    while (ReadFile(OutputPipe, Buffer, sizeof(Buffer), &Count, NULL) && Count)
        ;
    return 0;
}

static BOOL
StartChild(HPCON Console, PROCESS_INFORMATION *Process)
{
    STARTUPINFOEXW Startup = { { sizeof(Startup) } };
    WCHAR Module[MAX_PATH], CommandLine[MAX_PATH + 64];
    SIZE_T AttributeSize = 0;
    BOOL Created;

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
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" ResizePseudoConsole child %lu", Module,
                     GetCurrentProcessId());
    Created = CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                             &Startup.StartupInfo, Process);
    ok(Created, "CreateProcessW failed with %lu\n", GetLastError());
    DeleteProcThreadAttributeList(Startup.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, Startup.lpAttributeList);
    return Created;
}

static VOID
CheckPhase(RESIZE_SHARED *Shared, HANDLE Go, HANDLE Done, ULONG Phase, SHORT Cols, SHORT Rows)
{
    CONSOLE_SCREEN_BUFFER_INFO *Info = &Shared->Info[Phase];

    Shared->Cols[Phase] = Cols;
    Shared->Rows[Phase] = Rows;
    SetEvent(Go);
    if (WaitForSingleObject(Done, 10000) != WAIT_OBJECT_0)
    {
        ok(FALSE, "Phase %lu: the child did not answer\n", Phase);
        return;
    }
    ok(Shared->Ok[Phase], "Phase %lu: GetConsoleScreenBufferInfo failed in the child\n", Phase);
    ok(Info->dwSize.X == Cols && Info->dwSize.Y == Rows, "Phase %lu: buffer %dx%d, expected %dx%d\n", Phase,
       Info->dwSize.X, Info->dwSize.Y, Cols, Rows);
    ok(Info->srWindow.Left == 0 && Info->srWindow.Top == 0 &&
       Info->srWindow.Right == Cols - 1 && Info->srWindow.Bottom == Rows - 1,
       "Phase %lu: window (%d,%d)-(%d,%d), expected (0,0)-(%d,%d)\n", Phase, Info->srWindow.Left,
       Info->srWindow.Top, Info->srWindow.Right, Info->srWindow.Bottom, Cols - 1, Rows - 1);
}

static VOID
TestResize(
    _In_ PCREATE_PSEUDO_CONSOLE Create,
    _In_ PCLOSE_PSEUDO_CONSOLE Close,
    _In_ PRESIZE_PSEUDO_CONSOLE Resize)
{
    WCHAR MappingName[64], GoName[64], DoneName[64];
    HANDLE InputRead, InputWrite, OutputWrite, Mapping, Go, Done, Reader;
    PROCESS_INFORMATION Process;
    RESIZE_SHARED *Shared;
    COORD Size = { 80, 25 };
    DWORD ExitCode = 0;
    HPCON Console;
    HRESULT Result;

    GetSharedNames(GetCurrentProcessId(), MappingName, GoName, DoneName, ARRAYSIZE(MappingName));
    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*Shared), MappingName);
    Shared = Mapping ? MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*Shared)) : NULL;
    Go = CreateEventW(NULL, FALSE, FALSE, GoName);
    Done = CreateEventW(NULL, FALSE, FALSE, DoneName);
    if (!Shared || !Go || !Done)
    {
        skip("Could not create the shared objects: %lu\n", GetLastError());
        return;
    }
    ok(CreatePipe(&InputRead, &InputWrite, NULL, 0), "CreatePipe failed with %lu\n", GetLastError());
    ok(CreatePipe(&OutputPipe, &OutputWrite, NULL, 0), "CreatePipe failed with %lu\n", GetLastError());
    Result = Create(Size, InputRead, OutputWrite, 0, &Console);
    ok_hex(Result, S_OK);
    CloseHandle(InputRead);
    CloseHandle(OutputWrite);
    if (FAILED(Result))
    {
        CloseHandle(InputWrite);
        CloseHandle(OutputPipe);
        return;
    }
    Reader = CreateThread(NULL, 0, ReaderThread, NULL, 0, NULL);

    Size.X = -1;
    Size.Y = 30;
    ok_hex(Resize(Console, Size), E_INVALIDARG);
    Size.X = 100;
    Size.Y = -1;
    ok_hex(Resize(Console, Size), E_INVALIDARG);

    if (StartChild(Console, &Process))
    {
        CheckPhase(Shared, Go, Done, 0, 80, 25);
        Size.X = 100;
        Size.Y = 30;
        ok_hex(Resize(Console, Size), S_OK);
        CheckPhase(Shared, Go, Done, 1, 100, 30);
        Size.X = 60;
        Size.Y = 20;
        ok_hex(Resize(Console, Size), S_OK);
        CheckPhase(Shared, Go, Done, 2, 60, 20);
        Size.X = 0;
        Size.Y = 0;
        ok_hex(Resize(Console, Size), S_OK);
        CheckPhase(Shared, Go, Done, 3, 60, 20);

        if (WaitForSingleObject(Process.hProcess, 5000) != WAIT_OBJECT_0)
        {
            ok(FALSE, "The child did not exit\n");
            TerminateProcess(Process.hProcess, 99);
            WaitForSingleObject(Process.hProcess, 5000);
        }
        GetExitCodeProcess(Process.hProcess, &ExitCode);
        ok_long(ExitCode, 0);
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
    }

    Close(Console);
    CloseHandle(InputWrite);
    if (Reader)
    {
        if (WaitForSingleObject(Reader, 5000) != WAIT_OBJECT_0)
            CancelSynchronousIo(Reader);
        CloseHandle(Reader);
    }
    CloseHandle(OutputPipe);
    UnmapViewOfFile(Shared);
    CloseHandle(Mapping);
    CloseHandle(Go);
    CloseHandle(Done);
}

static VOID
CheckFunction(
    _In_ PRESIZE_PSEUDO_CONSOLE Function,
    _In_ PCSTR Provider)
{
    COORD Size;
    HRESULT Result;

    Size.X = 80;
    Size.Y = 25;
    Result = Function(NULL, Size);
    ok_hex(Result, E_INVALIDARG);

    Size.X = -1;
    Result = Function(NULL, Size);
    ok_hex(Result, E_INVALIDARG);

    Size.X = 80;
    Size.Y = -1;
    Result = Function(NULL, Size);
    ok_hex(Result, E_INVALIDARG);

    trace("%s pseudo-console resize validation complete\n", Provider);
}

START_TEST(ResizePseudoConsole)
{
    PCREATE_PSEUDO_CONSOLE Kernel32Create;
    PCLOSE_PSEUDO_CONSOLE Kernel32Close;
    PRESIZE_PSEUDO_CONSOLE Kernel32Function;
    PCREATE_PSEUDO_CONSOLE KernelBaseCreate;
    PCLOSE_PSEUDO_CONSOLE KernelBaseClose;
    PRESIZE_PSEUDO_CONSOLE KernelBaseFunction;
    HMODULE Kernel32;
    HMODULE KernelBase;
    DWORD Error;
    char **Arguments;

    if (winetest_get_mainargs(&Arguments) >= 4 && !strcmp(Arguments[2], "child"))
    {
        RunChild(strtoul(Arguments[3], NULL, 10));
        return;
    }

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    Kernel32Create = (PCREATE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(Kernel32, "CreatePseudoConsole");
    ok(Kernel32Create != NULL, "CreatePseudoConsole is not exported by kernel32\n");
    Kernel32Close = (PCLOSE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(Kernel32, "ClosePseudoConsole");
    ok(Kernel32Close != NULL, "ClosePseudoConsole is not exported by kernel32\n");
    Kernel32Function = (PRESIZE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(Kernel32, "ResizePseudoConsole");
    ok(Kernel32Function != NULL, "ResizePseudoConsole is not exported by kernel32\n");
    if (Kernel32Function)
        CheckFunction(Kernel32Function, "kernel32");
    if (Kernel32Create && Kernel32Close && Kernel32Function)
        TestResize(Kernel32Create, Kernel32Close, Kernel32Function);

    KernelBase = LoadLibraryW(L"kernelbase.dll");
    Error = GetLastError();
    ok(KernelBase != NULL, "Failed to load kernelbase.dll: %lu\n", Error);
    KernelBaseFunction = NULL;
    if (KernelBase)
    {
        KernelBaseCreate = (PCREATE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(KernelBase, "CreatePseudoConsole");
        ok(KernelBaseCreate != NULL, "CreatePseudoConsole is not exported by kernelbase\n");
        KernelBaseClose = (PCLOSE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(KernelBase, "ClosePseudoConsole");
        ok(KernelBaseClose != NULL, "ClosePseudoConsole is not exported by kernelbase\n");
        KernelBaseFunction = (PRESIZE_PSEUDO_CONSOLE)(PVOID)GetProcAddress(KernelBase, "ResizePseudoConsole");
    }
    ok(KernelBaseFunction != NULL, "ResizePseudoConsole is not exported by kernelbase\n");
    if (KernelBaseFunction)
        CheckFunction(KernelBaseFunction, "kernelbase");

    if (KernelBase)
        FreeLibrary(KernelBase);
}
