/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the console command history functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winuser.h>

typedef VOID (WINAPI *PEXPUNGE_HISTORY_A)(LPCSTR);
typedef VOID (WINAPI *PEXPUNGE_HISTORY_W)(LPCWSTR);
typedef DWORD (WINAPI *PGET_HISTORY_A)(LPSTR, DWORD, LPCSTR);
typedef DWORD (WINAPI *PGET_HISTORY_W)(LPWSTR, DWORD, LPCWSTR);
typedef DWORD (WINAPI *PGET_HISTORY_LENGTH_A)(LPCSTR);
typedef DWORD (WINAPI *PGET_HISTORY_LENGTH_W)(LPCWSTR);
typedef BOOL (WINAPI *PSET_NUMBER_OF_COMMANDS_A)(DWORD, LPCSTR);
typedef BOOL (WINAPI *PSET_NUMBER_OF_COMMANDS_W)(DWORD, LPCWSTR);

static PEXPUNGE_HISTORY_A pExpungeConsoleCommandHistoryA;
static PEXPUNGE_HISTORY_W pExpungeConsoleCommandHistoryW;
static PGET_HISTORY_A pGetConsoleCommandHistoryA;
static PGET_HISTORY_W pGetConsoleCommandHistoryW;
static PGET_HISTORY_LENGTH_A pGetConsoleCommandHistoryLengthA;
static PGET_HISTORY_LENGTH_W pGetConsoleCommandHistoryLengthW;
static PSET_NUMBER_OF_COMMANDS_A pSetConsoleNumberOfCommandsA;
static PSET_NUMBER_OF_COMMANDS_W pSetConsoleNumberOfCommandsW;

static HANDLE Input;
static WCHAR ExeNameW[MAX_PATH];
static CHAR ExeNameA[MAX_PATH];

typedef struct _READ_LINE
{
    WCHAR Buffer[64];
    DWORD Count;
    BOOL Result;
} READ_LINE;

static DWORD WINAPI
ReadLineThread(PVOID Parameter)
{
    READ_LINE *Line = Parameter;

    Line->Result = ReadConsoleW(Input, Line->Buffer, ARRAYSIZE(Line->Buffer), &Line->Count, NULL);
    return 0;
}

static BOOL
EnterLine(PCWSTR Text, DWORD Mode)
{
    INPUT_RECORD Records[32];
    READ_LINE Line = { { 0 } };
    DWORD Count = 0, Written, Length = (DWORD)wcslen(Text), Index;
    HANDLE Thread;

    for (Index = 0; Index <= Length && Count + 2 <= ARRAYSIZE(Records); Index++)
    {
        WCHAR Char = Index < Length ? Text[Index] : L'\r';

        ZeroMemory(&Records[Count], 2 * sizeof(Records[0]));
        Records[Count].EventType = KEY_EVENT;
        Records[Count].Event.KeyEvent.bKeyDown = TRUE;
        Records[Count].Event.KeyEvent.wRepeatCount = 1;
        Records[Count].Event.KeyEvent.wVirtualKeyCode = Char == L'\r' ? VK_RETURN : (WORD)towupper(Char);
        Records[Count].Event.KeyEvent.uChar.UnicodeChar = Char;
        Records[Count + 1] = Records[Count];
        Records[Count + 1].Event.KeyEvent.bKeyDown = FALSE;
        Count += 2;
    }

    SetConsoleMode(Input, Mode);
    FlushConsoleInputBuffer(Input);
    if (!WriteConsoleInputW(Input, Records, Count, &Written) || Written != Count)
    {
        ok(FALSE, "WriteConsoleInputW failed with %lu\n", GetLastError());
        return FALSE;
    }
    Thread = CreateThread(NULL, 0, ReadLineThread, &Line, 0, NULL);
    if (!Thread)
    {
        ok(FALSE, "CreateThread failed with %lu\n", GetLastError());
        return FALSE;
    }
    if (WaitForSingleObject(Thread, 5000) != WAIT_OBJECT_0)
    {
        ok(FALSE, "ReadConsoleW did not return for '%S'\n", Text);
        CancelSynchronousIo(Thread);
        WaitForSingleObject(Thread, 5000);
        CloseHandle(Thread);
        return FALSE;
    }
    CloseHandle(Thread);
    ok(Line.Result, "ReadConsoleW failed for '%S'\n", Text);
    ok(Line.Count == Length + 2, "ReadConsoleW returned %lu characters for '%S'\n", Line.Count, Text);
    return Line.Result && Line.Count == Length + 2;
}

static VOID
CheckHistoryW(PCWSTR Expected, DWORD ExpectedBytes, PCSTR What)
{
    WCHAR Buffer[64];
    DWORD Result;

    SetLastError(0xdeadbeef);
    Result = pGetConsoleCommandHistoryLengthW(ExeNameW);
    ok(Result == ExpectedBytes, "%s: length %lu, expected %lu\n", What, Result, ExpectedBytes);
    ok(GetLastError() == 0xdeadbeef, "%s: last error %lu\n", What, GetLastError());

    memset(Buffer, 0x55, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Result = pGetConsoleCommandHistoryW(Buffer, sizeof(Buffer), ExeNameW);
    ok(Result == ExpectedBytes, "%s: GetConsoleCommandHistoryW returned %lu, expected %lu\n", What, Result,
       ExpectedBytes);
    ok(GetLastError() == 0xdeadbeef, "%s: last error %lu\n", What, GetLastError());
    ok(Result != ExpectedBytes || !memcmp(Buffer, Expected, ExpectedBytes), "%s: wrong history contents\n", What);
}

static VOID
TestUnicode(VOID)
{
    WCHAR Upper[MAX_PATH], Buffer[64];
    DWORD Result, Mode = ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT;
    BOOL Ret;

    pExpungeConsoleCommandHistoryW(ExeNameW);
    CheckHistoryW(L"", 0, "after expunge");

    if (!EnterLine(L"abc", Mode) || !EnterLine(L"de", Mode) || !EnterLine(L"abc", Mode) || !EnterLine(L"abc", Mode))
        return;
    CheckHistoryW(L"abc\0de\0abc", 22, "three lines");

    StringCchCopyW(Upper, ARRAYSIZE(Upper), ExeNameW);
    CharUpperW(Upper);
    SetLastError(0xdeadbeef);
    ok_long(pGetConsoleCommandHistoryLengthW(Upper), 22);
    ok_long(GetLastError(), 0xdeadbeef);

    memset(Buffer, 0x55, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Result = pGetConsoleCommandHistoryW(Buffer, 21, ExeNameW);
    ok_long(Result, 0);
    ok_long(GetLastError(), ERROR_BUFFER_OVERFLOW);
    ok(Buffer[0] == 0x5555, "Buffer was written: %x\n", Buffer[0]);
    SetLastError(0xdeadbeef);
    ok_long(pGetConsoleCommandHistoryW(Buffer, 22, ExeNameW), 22);
    ok_long(GetLastError(), 0xdeadbeef);

    SetLastError(0xdeadbeef);
    ok_long(pGetConsoleCommandHistoryLengthW(L"p12nohistory.exe"), 0);
    ok_long(GetLastError(), 0xdeadbeef);
    SetLastError(0xdeadbeef);
    ok_long(pGetConsoleCommandHistoryW(Buffer, sizeof(Buffer), L"p12nohistory.exe"), 0);
    ok_long(GetLastError(), 0xdeadbeef);

    if (!EnterLine(L"noecho", ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT))
        return;
    CheckHistoryW(L"abc\0de\0abc", 22, "line read without echo");

    SetLastError(0xdeadbeef);
    Ret = pSetConsoleNumberOfCommandsW(2, ExeNameW);
    ok(Ret, "SetConsoleNumberOfCommandsW failed with %lu\n", GetLastError());
    ok_long(GetLastError(), 0xdeadbeef);
    CheckHistoryW(L"abc\0de", 14, "two commands kept");
    if (!EnterLine(L"xyz", Mode))
        return;
    CheckHistoryW(L"de\0xyz", 14, "oldest command dropped");

    SetLastError(0xdeadbeef);
    ok(pSetConsoleNumberOfCommandsW(5, L"p12nohistory.exe"), "SetConsoleNumberOfCommandsW failed with %lu\n",
       GetLastError());
    ok_long(GetLastError(), 0xdeadbeef);
    ok(pSetConsoleNumberOfCommandsW(5, ExeNameW), "SetConsoleNumberOfCommandsW failed with %lu\n", GetLastError());
    if (!EnterLine(L"de", Mode))
        return;
    CheckHistoryW(L"de\0xyz\0de", 20, "duplicate kept without HistoryNoDup");

    SetLastError(0xdeadbeef);
    pExpungeConsoleCommandHistoryW(Upper);
    ok_long(GetLastError(), 0xdeadbeef);
    CheckHistoryW(L"", 0, "expunged through another case");
    if (!EnterLine(L"q", Mode))
        return;
    CheckHistoryW(L"q", 4, "size kept by expunge");

    SetLastError(0xdeadbeef);
    Result = pGetConsoleCommandHistoryLengthW(NULL);
    printf("length-null-exe: ret=%lu gle=%lu\n", Result, GetLastError());
    SetLastError(0xdeadbeef);
    Result = pGetConsoleCommandHistoryW(Buffer, sizeof(Buffer), NULL);
    printf("history-null-exe: ret=%lu gle=%lu\n", Result, GetLastError());
    SetLastError(0xdeadbeef);
    Ret = pSetConsoleNumberOfCommandsW(5, NULL);
    printf("commands-null-exe: ret=%d gle=%lu\n", Ret, GetLastError());
    SetLastError(0xdeadbeef);
    Result = pGetConsoleCommandHistoryW(NULL, 0, ExeNameW);
    printf("history-null-buffer: ret=%lu gle=%lu\n", Result, GetLastError());
}

static VOID
TestAnsi(VOID)
{
    CHAR Buffer[64];
    DWORD Result, Mode = ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT;

    pExpungeConsoleCommandHistoryA(ExeNameA);
    SetLastError(0xdeadbeef);
    ok_long(pGetConsoleCommandHistoryLengthA(ExeNameA), 0);
    ok_long(GetLastError(), 0xdeadbeef);

    if (!EnterLine(L"one", Mode) || !EnterLine(L"two", Mode))
        return;
    SetLastError(0xdeadbeef);
    ok_long(pGetConsoleCommandHistoryLengthA(ExeNameA), 8);
    ok_long(GetLastError(), 0xdeadbeef);
    ok_long(pGetConsoleCommandHistoryLengthW(ExeNameW), 16);

    memset(Buffer, 0x55, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    Result = pGetConsoleCommandHistoryA(Buffer, sizeof(Buffer), ExeNameA);
    ok_long(Result, 8);
    ok_long(GetLastError(), 0xdeadbeef);
    ok(Result == 8 && !memcmp(Buffer, "one\0two", 8), "Wrong ANSI history\n");

    memset(Buffer, 0x55, sizeof(Buffer));
    SetLastError(0xdeadbeef);
    ok_long(pGetConsoleCommandHistoryA(Buffer, 7, ExeNameA), 0);
    ok_long(GetLastError(), ERROR_BUFFER_OVERFLOW);
    ok(Buffer[0] == 0x55, "Buffer was written: %x\n", Buffer[0]);

    SetLastError(0xdeadbeef);
    ok(pSetConsoleNumberOfCommandsA(1, ExeNameA), "SetConsoleNumberOfCommandsA failed with %lu\n", GetLastError());
    ok_long(GetLastError(), 0xdeadbeef);
    ok_long(pGetConsoleCommandHistoryLengthA(ExeNameA), 4);
    ok(pSetConsoleNumberOfCommandsA(50, ExeNameA), "SetConsoleNumberOfCommandsA failed with %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    pExpungeConsoleCommandHistoryA(ExeNameA);
    ok_long(GetLastError(), 0xdeadbeef);
    ok_long(pGetConsoleCommandHistoryLengthA(ExeNameA), 0);
    SetLastError(0xdeadbeef);
    ok_long(pGetConsoleCommandHistoryA(Buffer, sizeof(Buffer), ExeNameA), 0);
    ok_long(GetLastError(), 0xdeadbeef);
}

START_TEST(ConsoleCommandHistory)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    WCHAR Path[MAX_PATH];
    PWSTR Name;
    DWORD Mode = 0;

    pExpungeConsoleCommandHistoryA = (PEXPUNGE_HISTORY_A)GetProcAddress(Kernel32, "ExpungeConsoleCommandHistoryA");
    pExpungeConsoleCommandHistoryW = (PEXPUNGE_HISTORY_W)GetProcAddress(Kernel32, "ExpungeConsoleCommandHistoryW");
    pGetConsoleCommandHistoryA = (PGET_HISTORY_A)GetProcAddress(Kernel32, "GetConsoleCommandHistoryA");
    pGetConsoleCommandHistoryW = (PGET_HISTORY_W)GetProcAddress(Kernel32, "GetConsoleCommandHistoryW");
    pGetConsoleCommandHistoryLengthA = (PGET_HISTORY_LENGTH_A)GetProcAddress(Kernel32, "GetConsoleCommandHistoryLengthA");
    pGetConsoleCommandHistoryLengthW = (PGET_HISTORY_LENGTH_W)GetProcAddress(Kernel32, "GetConsoleCommandHistoryLengthW");
    pSetConsoleNumberOfCommandsA = (PSET_NUMBER_OF_COMMANDS_A)GetProcAddress(Kernel32, "SetConsoleNumberOfCommandsA");
    pSetConsoleNumberOfCommandsW = (PSET_NUMBER_OF_COMMANDS_W)GetProcAddress(Kernel32, "SetConsoleNumberOfCommandsW");
    if (!pExpungeConsoleCommandHistoryA || !pExpungeConsoleCommandHistoryW || !pGetConsoleCommandHistoryA ||
        !pGetConsoleCommandHistoryW || !pGetConsoleCommandHistoryLengthA || !pGetConsoleCommandHistoryLengthW ||
        !pSetConsoleNumberOfCommandsA || !pSetConsoleNumberOfCommandsW)
    {
        skip("The console command history functions are not exported\n");
        return;
    }

    GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    Name = wcsrchr(Path, L'\\');
    StringCchCopyW(ExeNameW, ARRAYSIZE(ExeNameW), Name ? Name + 1 : Path);
    WideCharToMultiByte(CP_ACP, 0, ExeNameW, -1, ExeNameA, sizeof(ExeNameA), NULL, NULL);

    FreeConsole();
    if (!AllocConsole())
    {
        skip("AllocConsole failed with %lu\n", GetLastError());
        return;
    }
    Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
    ok(Input != INVALID_HANDLE_VALUE, "Opening CONIN$ failed with %lu\n", GetLastError());
    if (Input != INVALID_HANDLE_VALUE)
    {
        GetConsoleMode(Input, &Mode);
        TestUnicode();
        TestAnsi();
        SetConsoleMode(Input, Mode);
        FlushConsoleInputBuffer(Input);
        CloseHandle(Input);
    }
    FreeConsole();
}
