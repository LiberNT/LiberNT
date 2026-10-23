/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Starts a Linux program through the lxcore driver
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

typedef struct _LX_PUMP
{
    HANDLE Source;
    HANDLE Target;
} LX_PUMP, *PLX_PUMP;

typedef struct _LX_STRINGS
{
    PCHAR Buffer;
    ULONG Length;
    ULONG Count;
} LX_STRINGS, *PLX_STRINGS;

static HANDLE LxChildProcess;

static
VOID
LxUsage(VOID)
{
    fwprintf(stderr,
             L"Usage: lxrun [-t] [-n|-j] [--boot] [-r rootdir] [-C linuxdir] [-e NAME=VALUE]... program [arguments...]\n"
             L"  -t             Treat standard input and output as a terminal even when they are not a console\n"
             L"  -n             Start a new Linux instance instead of joining the running one\n"
             L"  -j             Only join the running Linux instance; fail when there is none\n"
             L"  --boot         Boot the root as a Linux instance with program (default /sbin/init) as PID 1\n"
             L"  --trace        Log failing Linux system calls of the instance to the kernel debugger\n"
             L"  --trace-all    Log every Linux system call of the instance to the kernel debugger\n"
             L"  -r rootdir     Windows directory used as the Linux root (default: current directory)\n"
             L"  -C linuxdir    Linux working directory (default: /)\n"
             L"  -e NAME=VALUE  Add or replace an environment variable\n");
}

static
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

static
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

static
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
        if (LxChildProcess != NULL)
            TerminateProcess(LxChildProcess, 128 + 2);
        return TRUE;
    }
    return FALSE;
}

static
HANDLE
LxOpenDevice(VOID)
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
            fwprintf(stderr, L"lxrun: the lxcore driver did not start (error %lu)\n", GetLastError());
        CloseServiceHandle(Service);
    }
    CloseServiceHandle(Manager);

    return CreateFileW(LX_USER_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static
BOOL
LxIsConsole(
    _In_ DWORD StdId)
{
    DWORD Mode;

    return GetConsoleMode(GetStdHandle(StdId), &Mode);
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

int
wmain(
    int argc,
    WCHAR *argv[])
{
    PCWSTR RootArg = L".";
    PCSTR Cwd = "/";
    LX_STRINGS Args = { NULL, 0, 0 }, Env = { NULL, 0, 0 };
    UNICODE_STRING NtRoot;
    WCHAR FullRoot[MAX_PATH];
    PLX_CREATE_PROCESS_IN In;
    LX_CREATE_PROCESS_OUT Out;
    HANDLE Device, ChildStd[3], Pumps[3];
    DWORD StdIds[3] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
    PCHAR FileName, Utf8;
    ULONG Size, Offset;
    DWORD Returned, ExitCode = 1;
    UINT OldOutputCp, OldInputCp;
    DWORD OldInputMode = 0, OldOutputMode = 0;
    BOOL ForceTty = FALSE, Tty, ConsoleIn, ConsoleOut;
    ULONG CreateFlags = 0;
    CONSOLE_SCREEN_BUFFER_INFO ScreenInfo;
    int Index;

    LxStringsAdd(&Env, "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
    LxStringsAdd(&Env, "HOME=/root");
    LxStringsAdd(&Env, "USER=root");
    LxStringsAdd(&Env, "SHELL=/bin/sh");
    LxStringsAdd(&Env, "TERM=dumb");
    LxStringsAdd(&Env, "LANG=C.UTF-8");

    for (Index = 1; Index < argc; Index++)
    {
        if (wcscmp(argv[Index], L"-t") == 0)
        {
            ForceTty = TRUE;
        }
        else if (wcscmp(argv[Index], L"-n") == 0)
        {
            CreateFlags |= LX_CREATE_NEW_INSTANCE;
        }
        else if (wcscmp(argv[Index], L"-j") == 0)
        {
            CreateFlags |= LX_CREATE_JOIN;
        }
        else if (wcscmp(argv[Index], L"--boot") == 0)
        {
            CreateFlags |= LX_CREATE_BOOT | LX_CREATE_NEW_INSTANCE;
        }
        else if (wcscmp(argv[Index], L"--trace") == 0)
        {
            CreateFlags |= LX_CREATE_TRACE;
        }
        else if (wcscmp(argv[Index], L"--trace-all") == 0)
        {
            CreateFlags |= LX_CREATE_TRACE_ALL;
        }
        else if ((wcscmp(argv[Index], L"-r") == 0) && (Index + 1 < argc))
        {
            RootArg = argv[++Index];
        }
        else if ((wcscmp(argv[Index], L"-C") == 0) && (Index + 1 < argc))
        {
            Cwd = LxToUtf8(argv[++Index]);
        }
        else if ((wcscmp(argv[Index], L"-e") == 0) && (Index + 1 < argc))
        {
            Utf8 = LxToUtf8(argv[++Index]);
            if ((Utf8 == NULL) || !LxEnvSet(&Env, Utf8))
            {
                LxUsage();
                return 1;
            }
            free(Utf8);
        }
        else if ((wcscmp(argv[Index], L"-h") == 0) || (wcscmp(argv[Index], L"/?") == 0))
        {
            LxUsage();
            return 0;
        }
        else
        {
            break;
        }
    }

    if (((Index >= argc) && !(CreateFlags & LX_CREATE_BOOT)) || (Cwd == NULL))
    {
        LxUsage();
        return 1;
    }

    if (CreateFlags & LX_CREATE_BOOT)
    {
        LxEnvSet(&Env, "container=libernt");
        LxEnvSet(&Env, "TERM=linux");
    }

    if (Index >= argc)
    {
        FileName = (PCHAR)"/sbin/init";
        if (!LxStringsAdd(&Args, FileName))
            return 1;
    }
    else
    {
        FileName = LxToUtf8(argv[Index]);
        for (; Index < argc; Index++)
        {
            Utf8 = LxToUtf8(argv[Index]);
            if ((Utf8 == NULL) || !LxStringsAdd(&Args, Utf8))
                return 1;
            free(Utf8);
        }
    }

    if (!GetFullPathNameW(RootArg, MAX_PATH, FullRoot, NULL) ||
        !RtlDosPathNameToNtPathName_U(FullRoot, &NtRoot, NULL, NULL))
    {
        fwprintf(stderr, L"lxrun: invalid root directory '%s'\n", RootArg);
        return 1;
    }

    Device = LxOpenDevice();
    if (Device == INVALID_HANDLE_VALUE)
    {
        fwprintf(stderr, L"lxrun: cannot open the Linux subsystem driver (error %lu)\n", GetLastError());
        return 1;
    }

    ConsoleIn = LxIsConsole(STD_INPUT_HANDLE);
    ConsoleOut = LxIsConsole(STD_OUTPUT_HANDLE);
    Tty = ForceTty || (ConsoleIn && ConsoleOut);

    OldOutputCp = GetConsoleOutputCP();
    OldInputCp = GetConsoleCP();
    if (Tty && ConsoleIn)
    {
        GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &OldInputMode);
        SetConsoleCP(CP_UTF8);
        if (!SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_VIRTUAL_TERMINAL_INPUT))
            SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), 0);
    }
    if (Tty && ConsoleOut)
    {
        GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &OldOutputMode);
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),
                       OldOutputMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    for (Index = 0; Index < 3; Index++)
    {
        if (Tty && (Index == 2))
        {
            ChildStd[Index] = NULL;
            Pumps[Index] = NULL;
            continue;
        }
        if (!LxPrepareStd(StdIds[Index], Index == 0, &ChildStd[Index], &Pumps[Index]))
        {
            fwprintf(stderr, L"lxrun: cannot set up standard handles (error %lu)\n", GetLastError());
            return 1;
        }
    }

    Size = sizeof(*In) + NtRoot.Length + (ULONG)strlen(Cwd) + (ULONG)strlen(FileName) + Args.Length + Env.Length;
    In = calloc(1, Size);
    if (In == NULL)
        return 1;

    In->Size = Size;
    In->Version = LX_CREATE_PROCESS_VERSION;
    In->Flags = CreateFlags;
    for (Index = 0; Index < 3; Index++)
        In->StdHandles[Index] = ChildStd[Index];

    Offset = sizeof(*In);
    In->RootOffset = Offset;
    In->RootLength = NtRoot.Length;
    memcpy((PUCHAR)In + Offset, NtRoot.Buffer, NtRoot.Length);
    Offset += NtRoot.Length;

    In->CwdOffset = Offset;
    In->CwdLength = (ULONG)strlen(Cwd);
    memcpy((PUCHAR)In + Offset, Cwd, In->CwdLength);
    Offset += In->CwdLength;

    In->FileOffset = Offset;
    In->FileLength = (ULONG)strlen(FileName);
    memcpy((PUCHAR)In + Offset, FileName, In->FileLength);
    Offset += In->FileLength;

    In->ArgOffset = Offset;
    In->ArgLength = Args.Length;
    In->ArgCount = Args.Count;
    memcpy((PUCHAR)In + Offset, Args.Buffer, Args.Length);
    Offset += Args.Length;

    In->EnvOffset = Offset;
    In->EnvLength = Env.Length;
    In->EnvCount = Env.Count;
    memcpy((PUCHAR)In + Offset, Env.Buffer, Env.Length);

    if (Tty && (ChildStd[0] != NULL) && (ChildStd[1] != NULL))
    {
        In->TtyMask = 7;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ScreenInfo))
        {
            In->Rows = (USHORT)(ScreenInfo.srWindow.Bottom - ScreenInfo.srWindow.Top + 1);
            In->Columns = (USHORT)(ScreenInfo.srWindow.Right - ScreenInfo.srWindow.Left + 1);
        }
    }

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(LxCtrlHandler, TRUE);

    if (!DeviceIoControl(Device, IOCTL_LX_CREATE_PROCESS, In, Size, &Out, sizeof(Out), &Returned, NULL))
    {
        DWORD Error = GetLastError();

        if (Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND)
            fwprintf(stderr, L"lxrun: %S: not found\n", FileName);
        else if (Error == ERROR_BAD_EXE_FORMAT)
            fwprintf(stderr, L"lxrun: %S: not a Linux program for this machine\n", FileName);
        else
            fwprintf(stderr, L"lxrun: %S: cannot start (error %lu)\n", FileName, Error);
        ExitCode = 127;
        for (Index = 0; Index < 3; Index++)
        {
            if (ChildStd[Index] != NULL)
                CloseHandle(ChildStd[Index]);
        }
    }
    else
    {
        LxChildProcess = Out.ProcessHandle;
        for (Index = 0; Index < 3; Index++)
        {
            if (ChildStd[Index] != NULL)
                CloseHandle(ChildStd[Index]);
        }

        WaitForSingleObject(Out.ProcessHandle, INFINITE);
        GetExitCodeProcess(Out.ProcessHandle, &ExitCode);
        for (Index = 1; Index < 3; Index++)
        {
            if (Pumps[Index] != NULL)
                WaitForSingleObject(Pumps[Index], 2000);
        }
        CloseHandle(Out.ProcessHandle);
    }

    if (Tty && ConsoleIn)
    {
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), OldInputMode);
        SetConsoleCP(OldInputCp);
    }
    if (Tty && ConsoleOut)
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), OldOutputMode);
    SetConsoleOutputCP(OldOutputCp);
    CloseHandle(Device);
    return (int)ExitCode;
}
