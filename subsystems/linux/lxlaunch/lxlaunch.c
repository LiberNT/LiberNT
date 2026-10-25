/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Starts Linux programs through the lxcore driver, on pipes or on a pseudo-terminal
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <wincon.h>
#include <winioctl.h>
#include <winsvc.h>
#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>

#include "../include/lxioctl.h"
#include "../include/lxlaunch.h"

#define LX_PTY_CHUNK 4096

typedef struct _LX_PUMP
{
    HANDLE Source;
    HANDLE Target;
} LX_PUMP, *PLX_PUMP;

typedef struct _LX_TERMINAL
{
    HANDLE Pty;
    HANDLE Input;
    HANDLE Output;
} LX_TERMINAL, *PLX_TERMINAL;

static HANDLE LxChildProcess;
static BOOL LxTerminalMode;

PCHAR
LxToUtf8(
    _In_ PCWSTR String)
{
    int Length = WideCharToMultiByte(CP_UTF8, 0, String, -1, NULL, 0, NULL, NULL);
    PCHAR Buffer;

    if (Length <= 0)
        return NULL;
    Buffer = malloc(Length);
    if (Buffer != NULL)
        WideCharToMultiByte(CP_UTF8, 0, String, -1, Buffer, Length, NULL, NULL);
    return Buffer;
}

BOOL
LxStringsAdd(
    _Inout_ PLX_STRINGS Strings,
    _In_ PCSTR String)
{
    ULONG Length = (ULONG)strlen(String) + 1;
    PCHAR Buffer = realloc(Strings->Buffer, Strings->Length + Length);

    if (Buffer == NULL)
        return FALSE;
    memcpy(Buffer + Strings->Length, String, Length);
    Strings->Buffer = Buffer;
    Strings->Length += Length;
    Strings->Count++;
    return TRUE;
}

BOOL
LxEnvSet(
    _Inout_ PLX_STRINGS Env,
    _In_ PCSTR Assignment)
{
    PCSTR Equals = strchr(Assignment, '=');
    LX_STRINGS Updated = { NULL, 0, 0 };
    ULONG Offset;
    SIZE_T NameLength;

    if (Equals == NULL)
        return FALSE;
    NameLength = Equals - Assignment + 1;

    for (Offset = 0; Offset < Env->Length; Offset += (ULONG)strlen(Env->Buffer + Offset) + 1)
    {
        if (strncmp(Env->Buffer + Offset, Assignment, NameLength) != 0)
        {
            if (!LxStringsAdd(&Updated, Env->Buffer + Offset))
                return FALSE;
        }
    }
    if (!LxStringsAdd(&Updated, Assignment))
        return FALSE;

    free(Env->Buffer);
    *Env = Updated;
    return TRUE;
}

BOOL
LxIsConsole(
    _In_ DWORD StdId)
{
    DWORD Mode;

    return GetConsoleMode(GetStdHandle(StdId), &Mode);
}

static
DWORD
WINAPI
LxPumpThread(
    _In_ PVOID Parameter)
{
    PLX_PUMP Pump = Parameter;
    CHAR Buffer[4096];
    DWORD Read, Written;

    while (ReadFile(Pump->Source, Buffer, sizeof(Buffer), &Read, NULL) && (Read != 0))
    {
        if (!WriteFile(Pump->Target, Buffer, Read, &Written, NULL))
            break;
    }

    if (GetFileType(Pump->Source) == FILE_TYPE_PIPE)
        CloseHandle(Pump->Source);
    if (GetFileType(Pump->Target) == FILE_TYPE_PIPE)
        CloseHandle(Pump->Target);
    return 0;
}

static
BOOL
WINAPI
LxCtrlHandler(
    _In_ DWORD CtrlType)
{
    if ((CtrlType == CTRL_C_EVENT) || (CtrlType == CTRL_BREAK_EVENT))
    {
        if (!LxTerminalMode && (LxChildProcess != NULL))
            TerminateProcess(LxChildProcess, 128 + 2);
        return TRUE;
    }
    return FALSE;
}

static
HANDLE
LxOpenDevice(
    _In_ PCWSTR Name)
{
    HANDLE Device;
    SC_HANDLE Manager, Service;
    WCHAR ImagePath[] = L"System32\\drivers\\lxcore.sys";

    Device = CreateFileW(LX_USER_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Device != INVALID_HANDLE_VALUE)
        return Device;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (Manager == NULL)
        return INVALID_HANDLE_VALUE;

    Service = OpenServiceW(Manager, LX_SERVICE_NAME, SERVICE_START);
    if (Service == NULL)
    {
        Service = CreateServiceW(Manager,
                                 LX_SERVICE_NAME,
                                 L"Linux Subsystem",
                                 SERVICE_START,
                                 SERVICE_KERNEL_DRIVER,
                                 SERVICE_DEMAND_START,
                                 SERVICE_ERROR_NORMAL,
                                 ImagePath,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL);
    }
    if (Service != NULL)
    {
        if (!StartServiceW(Service, 0, NULL) && (GetLastError() != ERROR_SERVICE_ALREADY_RUNNING))
            fwprintf(stderr, L"%s: the lxcore driver did not start (error %lu)\n", Name, GetLastError());
        CloseServiceHandle(Service);
    }
    CloseServiceHandle(Manager);

    return CreateFileW(LX_USER_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static
BOOL
LxPrepareStd(
    _In_ DWORD StdId,
    _In_ BOOL Input,
    _Out_ PHANDLE ChildHandle,
    _Out_ PHANDLE PumpThread)
{
    HANDLE Own = GetStdHandle(StdId);
    HANDLE ReadEnd, WriteEnd;
    PLX_PUMP Pump;
    DWORD Type;

    *ChildHandle = NULL;
    *PumpThread = NULL;
    if ((Own == NULL) || (Own == INVALID_HANDLE_VALUE))
        return TRUE;

    Type = GetFileType(Own);
    if ((Type == FILE_TYPE_DISK) || (Type == FILE_TYPE_PIPE))
    {
        if (!DuplicateHandle(GetCurrentProcess(), Own, GetCurrentProcess(), ChildHandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
            return FALSE;
        return TRUE;
    }

    if (!CreatePipe(&ReadEnd, &WriteEnd, NULL, 0))
        return FALSE;

    Pump = malloc(sizeof(*Pump));
    if (Pump == NULL)
    {
        CloseHandle(ReadEnd);
        CloseHandle(WriteEnd);
        return FALSE;
    }

    if (Input)
    {
        Pump->Source = Own;
        Pump->Target = WriteEnd;
        *ChildHandle = ReadEnd;
    }
    else
    {
        Pump->Source = ReadEnd;
        Pump->Target = Own;
        *ChildHandle = WriteEnd;
    }

    *PumpThread = CreateThread(NULL, 0, LxPumpThread, Pump, 0, NULL);
    if (*PumpThread == NULL)
    {
        CloseHandle(ReadEnd);
        CloseHandle(WriteEnd);
        free(Pump);
        *ChildHandle = NULL;
        return FALSE;
    }
    return TRUE;
}

static
BOOL
LxPtyTransfer(
    _In_ HANDLE Pty,
    _In_ BOOL Write,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ DWORD Length,
    _Out_ PDWORD Done)
{
    OVERLAPPED Overlapped;
    BOOL Ok;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Overlapped.hEvent == NULL)
        return FALSE;

    if (Write)
        Ok = WriteFile(Pty, Buffer, Length, Done, &Overlapped);
    else
        Ok = ReadFile(Pty, Buffer, Length, Done, &Overlapped);
    if (!Ok && (GetLastError() == ERROR_IO_PENDING))
        Ok = GetOverlappedResult(Pty, &Overlapped, Done, TRUE);

    CloseHandle(Overlapped.hEvent);
    return Ok;
}

static
BOOL
LxPtyWriteAll(
    _In_ HANDLE Pty,
    _In_reads_bytes_(Length) PCHAR Buffer,
    _In_ DWORD Length)
{
    DWORD Done;

    while (Length != 0)
    {
        if (!LxPtyTransfer(Pty, TRUE, Buffer, Length, &Done) || (Done == 0))
            return FALSE;
        Buffer += Done;
        Length -= Done;
    }
    return TRUE;
}

static
VOID
LxTerminalResize(
    _In_ PLX_TERMINAL Terminal)
{
    CONSOLE_SCREEN_BUFFER_INFO ScreenInfo;
    LX_PTY_SIZE Size;
    OVERLAPPED Overlapped;
    DWORD Returned;
    BOOL Ok;

    if (!GetConsoleScreenBufferInfo(Terminal->Output, &ScreenInfo))
        return;
    Size.Rows = (USHORT)(ScreenInfo.srWindow.Bottom - ScreenInfo.srWindow.Top + 1);
    Size.Columns = (USHORT)(ScreenInfo.srWindow.Right - ScreenInfo.srWindow.Left + 1);

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Overlapped.hEvent == NULL)
        return;
    Ok = DeviceIoControl(Terminal->Pty, IOCTL_LX_PTY_RESIZE, &Size, sizeof(Size), NULL, 0, &Returned, &Overlapped);
    if (!Ok && (GetLastError() == ERROR_IO_PENDING))
        GetOverlappedResult(Terminal->Pty, &Overlapped, &Returned, TRUE);
    CloseHandle(Overlapped.hEvent);
}

static
DWORD
WINAPI
LxTerminalConsoleInput(
    _In_ PVOID Parameter)
{
    PLX_TERMINAL Terminal = Parameter;
    INPUT_RECORD Records[64];
    WCHAR Text[256];
    CHAR Utf8[sizeof(Text) / sizeof(WCHAR) * 3];
    ULONG Length = 0, Keep;
    DWORD Count, Index;
    int Bytes;

    while (ReadConsoleInputW(Terminal->Input, Records, ARRAYSIZE(Records), &Count))
    {
        for (Index = 0; Index < Count; Index++)
        {
            KEY_EVENT_RECORD *Key = &Records[Index].Event.KeyEvent;
            WORD Repeat;

            if (Records[Index].EventType == WINDOW_BUFFER_SIZE_EVENT)
            {
                LxTerminalResize(Terminal);
                continue;
            }
            if ((Records[Index].EventType != KEY_EVENT) || !Key->bKeyDown || (Key->uChar.UnicodeChar == 0))
                continue;
            for (Repeat = max(Key->wRepeatCount, 1); (Repeat != 0) && (Length < ARRAYSIZE(Text)); Repeat--)
                Text[Length++] = Key->uChar.UnicodeChar;
        }

        if (Length == 0)
            continue;
        Keep = IS_HIGH_SURROGATE(Text[Length - 1]) ? 1 : 0;
        if (Length > Keep)
        {
            Bytes = WideCharToMultiByte(CP_UTF8, 0, Text, Length - Keep, Utf8, sizeof(Utf8), NULL, NULL);
            if ((Bytes > 0) && !LxPtyWriteAll(Terminal->Pty, Utf8, (DWORD)Bytes))
                break;
        }
        if (Keep != 0)
            Text[0] = Text[Length - 1];
        Length = Keep;
    }
    return 0;
}

static
DWORD
WINAPI
LxTerminalFileInput(
    _In_ PVOID Parameter)
{
    PLX_TERMINAL Terminal = Parameter;
    CHAR Buffer[LX_PTY_CHUNK];
    CHAR Eof = 0x04;
    DWORD Read;

    while (ReadFile(Terminal->Input, Buffer, sizeof(Buffer), &Read, NULL) && (Read != 0))
    {
        if (!LxPtyWriteAll(Terminal->Pty, Buffer, Read))
            return 0;
    }
    LxPtyWriteAll(Terminal->Pty, &Eof, 1);
    return 0;
}

static
DWORD
LxUtf8Complete(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ DWORD Length)
{
    DWORD Lead = Length, Need;

    while ((Lead != 0) && (Length - Lead < 4))
    {
        Lead--;
        if ((Buffer[Lead] & 0xC0) != 0x80)
            break;
    }
    if ((Lead == Length) || ((Buffer[Lead] & 0xC0) == 0x80))
        return Length;

    Need = (Buffer[Lead] >= 0xF0) ? 4 : (Buffer[Lead] >= 0xE0) ? 3 : (Buffer[Lead] >= 0xC0) ? 2 : 1;
    return (Length - Lead < Need) ? Lead : Length;
}

static
BOOL
LxWriteAll(
    _In_ HANDLE Handle,
    _In_reads_bytes_(Length) PCHAR Buffer,
    _In_ DWORD Length)
{
    DWORD Written;

    while (Length != 0)
    {
        if (!WriteFile(Handle, Buffer, Length, &Written, NULL) || (Written == 0))
            return FALSE;
        Buffer += Written;
        Length -= Written;
    }
    return TRUE;
}

static
DWORD
WINAPI
LxTerminalOutput(
    _In_ PVOID Parameter)
{
    PLX_TERMINAL Terminal = Parameter;
    CHAR Buffer[LX_PTY_CHUNK + 4];
    DWORD Pending = 0, Read, Complete;

    while (LxPtyTransfer(Terminal->Pty, FALSE, Buffer + Pending, LX_PTY_CHUNK, &Read) && (Read != 0))
    {
        Read += Pending;
        Complete = LxUtf8Complete((PUCHAR)Buffer, Read);
        if ((Complete != 0) && !LxWriteAll(Terminal->Output, Buffer, Complete))
            return 0;
        Pending = Read - Complete;
        MoveMemory(Buffer, Buffer + Complete, Pending);
    }
    if (Pending != 0)
        LxWriteAll(Terminal->Output, Buffer, Pending);
    return 0;
}

static
VOID
LxLaunchError(
    _In_ PLX_LAUNCH Launch,
    _In_ DWORD Error)
{
    if ((Error == ERROR_FILE_NOT_FOUND) || (Error == ERROR_PATH_NOT_FOUND))
        fwprintf(stderr, L"%s: %S: not found\n", Launch->Name, Launch->FileName);
    else if (Error == ERROR_BAD_EXE_FORMAT)
        fwprintf(stderr, L"%s: %S: not a Linux program for this machine\n", Launch->Name, Launch->FileName);
    else
        fwprintf(stderr, L"%s: %S: cannot start (error %lu)\n", Launch->Name, Launch->FileName, Error);
}

int
LxLaunch(
    _Inout_ PLX_LAUNCH Launch)
{
    UNICODE_STRING NtRoot;
    WCHAR FullRoot[MAX_PATH];
    PLX_CREATE_PROCESS_IN In;
    LX_CREATE_PROCESS_OUT Out;
    LX_TERMINAL Terminal = { NULL, NULL, NULL };
    HANDLE Device, ChildStd[3] = { NULL, NULL, NULL }, Pumps[3] = { NULL, NULL, NULL };
    HANDLE TerminalThreads[2] = { NULL, NULL };
    DWORD StdIds[3] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
    ULONG Size, Offset;
    DWORD Returned, ExitCode = 1;
    UINT OldOutputCp, OldInputCp;
    DWORD OldInputMode = 0, OldOutputMode = 0;
    BOOL ConsoleIn, ConsoleOut;
    CONSOLE_SCREEN_BUFFER_INFO ScreenInfo;
    int Index;

    if (!GetFullPathNameW(Launch->Root, MAX_PATH, FullRoot, NULL) ||
        !RtlDosPathNameToNtPathName_U(FullRoot, &NtRoot, NULL, NULL))
    {
        fwprintf(stderr, L"%s: invalid root directory '%s'\n", Launch->Name, Launch->Root);
        return 1;
    }

    Device = LxOpenDevice(Launch->Name);
    if (Device == INVALID_HANDLE_VALUE)
    {
        fwprintf(stderr, L"%s: cannot open the Linux subsystem driver (error %lu)\n", Launch->Name, GetLastError());
        return 1;
    }

    ConsoleIn = LxIsConsole(STD_INPUT_HANDLE);
    ConsoleOut = LxIsConsole(STD_OUTPUT_HANDLE);
    LxTerminalMode = Launch->Terminal;

    if (Launch->Terminal)
    {
        Terminal.Input = GetStdHandle(STD_INPUT_HANDLE);
        Terminal.Output = GetStdHandle(STD_OUTPUT_HANDLE);
        Terminal.Pty = CreateFileW(LX_USER_PTY_NAME,
                                   GENERIC_READ | GENERIC_WRITE,
                                   0,
                                   NULL,
                                   OPEN_EXISTING,
                                   FILE_FLAG_OVERLAPPED,
                                   NULL);
        if (Terminal.Pty == INVALID_HANDLE_VALUE)
        {
            fwprintf(stderr, L"%s: cannot create a pseudo-terminal (error %lu)\n", Launch->Name, GetLastError());
            CloseHandle(Device);
            return 1;
        }
    }

    OldOutputCp = GetConsoleOutputCP();
    OldInputCp = GetConsoleCP();
    if (Launch->Terminal && ConsoleIn)
    {
        GetConsoleMode(Terminal.Input, &OldInputMode);
        SetConsoleCP(CP_UTF8);
        if (!SetConsoleMode(Terminal.Input, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT))
            SetConsoleMode(Terminal.Input, ENABLE_WINDOW_INPUT);
    }
    if (Launch->Terminal && ConsoleOut)
    {
        GetConsoleMode(Terminal.Output, &OldOutputMode);
        SetConsoleMode(Terminal.Output,
                       OldOutputMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                       DISABLE_NEWLINE_AUTO_RETURN);
    }

    if (Launch->Detached)
    {
        for (Index = 0; Index < 3; Index++)
        {
            ChildStd[Index] = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          NULL, OPEN_EXISTING, 0, NULL);
            if (ChildStd[Index] == INVALID_HANDLE_VALUE)
                ChildStd[Index] = NULL;
        }
    }
    else if (!Launch->Terminal)
    {
        for (Index = 0; Index < 3; Index++)
        {
            if ((Index == 0) && (Launch->Input != NULL))
            {
                HANDLE WriteEnd;
                DWORD Written;

                if (!CreatePipe(&ChildStd[0], &WriteEnd, NULL, 0))
                    return 1;
                WriteFile(WriteEnd, Launch->Input, (DWORD)strlen(Launch->Input), &Written, NULL);
                CloseHandle(WriteEnd);
                continue;
            }
            if (!LxPrepareStd(StdIds[Index], Index == 0, &ChildStd[Index], &Pumps[Index]))
            {
                fwprintf(stderr, L"%s: cannot set up standard handles (error %lu)\n", Launch->Name, GetLastError());
                return 1;
            }
        }
    }

    Size = sizeof(*In) + NtRoot.Length + (ULONG)strlen(Launch->Cwd) + (ULONG)strlen(Launch->FileName) +
           Launch->Args.Length + Launch->Env.Length;
    In = calloc(1, Size);
    if (In == NULL)
        return 1;

    In->Size = Size;
    In->Version = LX_CREATE_PROCESS_VERSION;
    In->Flags = Launch->Flags;
    if (Launch->User)
    {
        In->Flags |= LX_CREATE_USER | (Launch->RuntimeDir ? LX_CREATE_RUNTIME_DIR : 0);
        In->Uid = Launch->Uid;
        In->Gid = Launch->Gid;
        In->GroupCount = min(Launch->GroupCount, LX_CREATE_MAX_GROUPS);
        memcpy(In->Groups, Launch->Groups, In->GroupCount * sizeof(ULONG));
    }
    if (Launch->SearchPath)
        In->Flags |= LX_CREATE_SEARCH_PATH;
    for (Index = 0; Index < 3; Index++)
        In->StdHandles[Index] = ChildStd[Index];

    Offset = sizeof(*In);
    In->RootOffset = Offset;
    In->RootLength = NtRoot.Length;
    memcpy((PUCHAR)In + Offset, NtRoot.Buffer, NtRoot.Length);
    Offset += NtRoot.Length;

    In->CwdOffset = Offset;
    In->CwdLength = (ULONG)strlen(Launch->Cwd);
    memcpy((PUCHAR)In + Offset, Launch->Cwd, In->CwdLength);
    Offset += In->CwdLength;

    In->FileOffset = Offset;
    In->FileLength = (ULONG)strlen(Launch->FileName);
    memcpy((PUCHAR)In + Offset, Launch->FileName, In->FileLength);
    Offset += In->FileLength;

    In->ArgOffset = Offset;
    In->ArgLength = Launch->Args.Length;
    In->ArgCount = Launch->Args.Count;
    memcpy((PUCHAR)In + Offset, Launch->Args.Buffer, Launch->Args.Length);
    Offset += Launch->Args.Length;

    In->EnvOffset = Offset;
    In->EnvLength = Launch->Env.Length;
    In->EnvCount = Launch->Env.Count;
    memcpy((PUCHAR)In + Offset, Launch->Env.Buffer, Launch->Env.Length);

    if (Launch->Terminal)
    {
        In->Pty = Terminal.Pty;
        In->TtyMask = 7;
        if (ConsoleOut && GetConsoleScreenBufferInfo(Terminal.Output, &ScreenInfo))
        {
            In->Rows = (USHORT)(ScreenInfo.srWindow.Bottom - ScreenInfo.srWindow.Top + 1);
            In->Columns = (USHORT)(ScreenInfo.srWindow.Right - ScreenInfo.srWindow.Left + 1);
        }
    }

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(LxCtrlHandler, TRUE);

    if (!DeviceIoControl(Device, IOCTL_LX_CREATE_PROCESS, In, Size, &Out, sizeof(Out), &Returned, NULL))
    {
        LxLaunchError(Launch, GetLastError());
        ExitCode = 127;
        for (Index = 0; Index < 3; Index++)
        {
            if (ChildStd[Index] != NULL)
                CloseHandle(ChildStd[Index]);
        }
        if (Terminal.Pty != NULL)
            CloseHandle(Terminal.Pty);
    }
    else
    {
        LxChildProcess = Out.ProcessHandle;
        for (Index = 0; Index < 3; Index++)
        {
            if (ChildStd[Index] != NULL)
                CloseHandle(ChildStd[Index]);
        }
        if (Launch->Detached)
        {
            CloseHandle(Out.ProcessHandle);
            LxChildProcess = NULL;
            CloseHandle(Device);
            free(In);
            return 0;
        }

        if (Launch->Terminal)
        {
            TerminalThreads[0] = CreateThread(NULL, 0, LxTerminalOutput, &Terminal, 0, NULL);
            if ((Terminal.Input != NULL) && (Terminal.Input != INVALID_HANDLE_VALUE))
                TerminalThreads[1] = CreateThread(NULL, 0, ConsoleIn ? LxTerminalConsoleInput : LxTerminalFileInput,
                                                  &Terminal, 0, NULL);
        }

        WaitForSingleObject(Out.ProcessHandle, INFINITE);
        GetExitCodeProcess(Out.ProcessHandle, &ExitCode);

        if (Launch->Terminal)
        {
            CloseHandle(Terminal.Pty);
            if (TerminalThreads[0] != NULL)
                WaitForSingleObject(TerminalThreads[0], 2000);
        }
        for (Index = 1; Index < 3; Index++)
        {
            if (Pumps[Index] != NULL)
                WaitForSingleObject(Pumps[Index], 2000);
        }
        CloseHandle(Out.ProcessHandle);
    }

    if (Launch->Terminal && ConsoleIn)
    {
        SetConsoleMode(Terminal.Input, OldInputMode);
        SetConsoleCP(OldInputCp);
    }
    if (Launch->Terminal && ConsoleOut)
        SetConsoleMode(Terminal.Output, OldOutputMode);
    SetConsoleOutputCP(OldOutputCp);
    CloseHandle(Device);
    free(In);
    return (int)ExitCode;
}
