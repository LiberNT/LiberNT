/*
 * PROJECT:     LiberNT Debug Output Capture
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Captures kernel debug prints and OutputDebugString output to the console and a log file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <windef.h>
#include <winbase.h>
#include <wincon.h>
#include <winioctl.h>
#include <winsvc.h>
#include <sddl.h>
#include <drivers/dbgcap.h>

#define READ_BUFFER_SIZE    (64 * 1024)
#define READ_TIMEOUT_MS     250
#define DBWIN_BUFFER_SIZE   4096
#define LINE_BUFFER_SIZE    8192

typedef struct _LINE_STATE
{
    CHAR Text[LINE_BUFFER_SIZE];
    SIZE_T Length;
    LONGLONG Time;
    ULONG ProcessId;
} LINE_STATE, *PLINE_STATE;

static CRITICAL_SECTION OutputLock;
static HANDLE StopEvent;
static HANDLE LogFile = INVALID_HANDLE_VALUE;
static BOOL Quiet;
static BOOL ClockTime;
static ULONG Sequence;
static LONGLONG StartTime;

static LONGLONG
Now(VOID)
{
    FILETIME Time;
    ULARGE_INTEGER Value;

    GetSystemTimeAsFileTime(&Time);
    Value.LowPart = Time.dwLowDateTime;
    Value.HighPart = Time.dwHighDateTime;
    return (LONGLONG)Value.QuadPart;
}

static VOID
FormatTime(
    _In_ LONGLONG Time,
    _Out_writes_(Size) PCHAR Buffer,
    _In_ SIZE_T Size)
{
    if (ClockTime)
    {
        FILETIME Utc, Local;
        SYSTEMTIME System;

        Utc.dwLowDateTime = (DWORD)Time;
        Utc.dwHighDateTime = (DWORD)(Time >> 32);
        FileTimeToLocalFileTime(&Utc, &Local);
        FileTimeToSystemTime(&Local, &System);
        _snprintf(Buffer, Size, "%02u:%02u:%02u.%03u",
                  System.wHour, System.wMinute, System.wSecond, System.wMilliseconds);
    }
    else
    {
        LONGLONG Elapsed = Time - StartTime;

        if (Elapsed < 0)
            Elapsed = 0;
        _snprintf(Buffer, Size, "%I64d.%07I64d", Elapsed / 10000000, Elapsed % 10000000);
    }
    Buffer[Size - 1] = 0;
}

static VOID
EmitLine(
    _In_ LONGLONG Time,
    _In_ BOOL Kernel,
    _In_ ULONG ProcessId,
    _In_reads_(Length) PCSTR Text,
    _In_ SIZE_T Length)
{
    CHAR TimeText[32], Prefix[48];
    DWORD Written;
    int PrefixLength;

    while (Length && (Text[Length - 1] == '\r' || Text[Length - 1] == '\n'))
        Length--;

    FormatTime(Time, TimeText, sizeof(TimeText));

    EnterCriticalSection(&OutputLock);
    Sequence++;
    if (Kernel)
        PrefixLength = _snprintf(Prefix, sizeof(Prefix), "%08lu\t%s\t", Sequence, TimeText);
    else
        PrefixLength = _snprintf(Prefix, sizeof(Prefix), "%08lu\t%s\t[%lu] ", Sequence, TimeText, ProcessId);
    if (PrefixLength < 0)
        PrefixLength = 0;

    if (!Quiet)
    {
        fwrite(Prefix, 1, PrefixLength, stdout);
        fwrite(Text, 1, Length, stdout);
        fputs("\n", stdout);
        fflush(stdout);
    }
    if (LogFile != INVALID_HANDLE_VALUE)
    {
        WriteFile(LogFile, Prefix, PrefixLength, &Written, NULL);
        WriteFile(LogFile, Text, (DWORD)Length, &Written, NULL);
        WriteFile(LogFile, "\r\n", 2, &Written, NULL);
    }
    LeaveCriticalSection(&OutputLock);
}

static VOID
FlushLine(
    _Inout_ PLINE_STATE Line,
    _In_ BOOL Kernel)
{
    if (Line->Length)
        EmitLine(Line->Time, Kernel, Line->ProcessId, Line->Text, Line->Length);
    Line->Length = 0;
}

static VOID
AppendText(
    _Inout_ PLINE_STATE Line,
    _In_ BOOL Kernel,
    _In_ LONGLONG Time,
    _In_ ULONG ProcessId,
    _In_reads_(Length) PCSTR Text,
    _In_ SIZE_T Length)
{
    SIZE_T Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Line->Length == 0)
        {
            Line->Time = Time;
            Line->ProcessId = ProcessId;
        }
        if (Text[Index] == '\n')
        {
            FlushLine(Line, Kernel);
            continue;
        }
        if (Text[Index] == '\0')
            continue;
        if (Line->Length == sizeof(Line->Text))
            FlushLine(Line, Kernel);
        Line->Text[Line->Length++] = Text[Index];
    }
}

static BOOL
StartCaptureService(
    _Out_ PBOOL Started)
{
    SC_HANDLE Manager, Service;
    BOOL Ok;

    *Started = FALSE;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!Manager)
        return FALSE;

    Service = OpenServiceW(Manager, DBGCAP_SERVICE_NAME, SERVICE_START | SERVICE_STOP);
    if (!Service && GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
    {
        Service = CreateServiceW(Manager,
                                 DBGCAP_SERVICE_NAME,
                                 L"Debug Output Capture",
                                 SERVICE_START | SERVICE_STOP,
                                 SERVICE_KERNEL_DRIVER,
                                 SERVICE_DEMAND_START,
                                 SERVICE_ERROR_NORMAL,
                                 L"System32\\drivers\\dbgcap.sys",
                                 NULL, NULL, NULL, NULL, NULL);
    }
    if (!Service)
    {
        CloseServiceHandle(Manager);
        return FALSE;
    }

    Ok = StartServiceW(Service, 0, NULL);
    if (Ok)
        *Started = TRUE;
    else if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)
        Ok = TRUE;

    CloseServiceHandle(Service);
    CloseServiceHandle(Manager);
    return Ok;
}

static VOID
StopCaptureService(VOID)
{
    SC_HANDLE Manager, Service;
    SERVICE_STATUS Status;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!Manager)
        return;
    Service = OpenServiceW(Manager, DBGCAP_SERVICE_NAME, SERVICE_STOP);
    if (Service)
    {
        ControlService(Service, SERVICE_CONTROL_STOP, &Status);
        CloseServiceHandle(Service);
    }
    CloseServiceHandle(Manager);
}

static HANDLE
OpenCaptureDevice(
    _Out_ PBOOL StartedService)
{
    HANDLE Device;

    *StartedService = FALSE;
    Device = CreateFileW(DBGCAP_WIN32_NAME, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Device != INVALID_HANDLE_VALUE)
        return Device;

    if (GetLastError() != ERROR_FILE_NOT_FOUND || !StartCaptureService(StartedService))
        return INVALID_HANDLE_VALUE;

    return CreateFileW(DBGCAP_WIN32_NAME, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static DWORD
WINAPI
KernelCaptureThread(
    _In_ LPVOID Parameter)
{
    HANDLE Device = (HANDLE)Parameter;
    DBGCAP_READ_REQUEST Request;
    static LINE_STATE Line;
    PUCHAR Buffer;
    DWORD Returned;
    ULONG Index, Offset;

    Buffer = HeapAlloc(GetProcessHeap(), 0, READ_BUFFER_SIZE);
    if (!Buffer)
        return 1;

    Request.TimeoutMs = READ_TIMEOUT_MS;
    while (WaitForSingleObject(StopEvent, 0) == WAIT_TIMEOUT)
    {
        PDBGCAP_READ_HEADER Header = (PDBGCAP_READ_HEADER)Buffer;

        memcpy(Buffer, &Request, sizeof(Request));
        if (!DeviceIoControl(Device, IOCTL_DBGCAP_READ, Buffer, sizeof(Request),
                             Buffer, READ_BUFFER_SIZE, &Returned, NULL) ||
            Returned < sizeof(*Header))
        {
            if (GetLastError() != ERROR_OPERATION_ABORTED)
                Sleep(READ_TIMEOUT_MS);
            continue;
        }

        if (Header->Lost)
        {
            CHAR Message[64];
            int Length = _snprintf(Message, sizeof(Message), "*** %lu kernel debug messages lost ***", Header->Lost);

            FlushLine(&Line, TRUE);
            EmitLine(Now(), TRUE, 0, Message, Length > 0 ? Length : 0);
        }

        if (Header->Count == 0)
        {
            FlushLine(&Line, TRUE);
            continue;
        }

        for (Index = 0, Offset = sizeof(*Header); Index < Header->Count && Offset < Returned; Index++)
        {
            PDBGCAP_RECORD Record = (PDBGCAP_RECORD)(Buffer + Offset);

            if (Record->Size < FIELD_OFFSET(DBGCAP_RECORD, Text) || Offset + Record->Size > Returned)
                break;
            AppendText(&Line, TRUE, Record->SystemTime.QuadPart, Record->ProcessId,
                       Record->Text, min(Record->Length, Record->Size - FIELD_OFFSET(DBGCAP_RECORD, Text)));
            Offset += Record->Size;
        }
    }

    FlushLine(&Line, TRUE);
    HeapFree(GetProcessHeap(), 0, Buffer);
    return 0;
}

typedef struct _DBWIN_CAPTURE
{
    HANDLE Mapping;
    HANDLE BufferReady;
    HANDLE DataReady;
    PUCHAR View;
} DBWIN_CAPTURE, *PDBWIN_CAPTURE;

static BOOL
OpenDbwin(
    _Out_ PDBWIN_CAPTURE Capture,
    _In_ BOOL Global)
{
    WCHAR Name[64] = { 0 };
    SECURITY_ATTRIBUTES Attributes, *Security = NULL;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    PCWSTR Prefix = Global ? L"Global\\" : L"";

    ZeroMemory(Capture, sizeof(*Capture));

    if (Global &&
        ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)",
                                                             SDDL_REVISION_1, &Descriptor, NULL))
    {
        Attributes.nLength = sizeof(Attributes);
        Attributes.lpSecurityDescriptor = Descriptor;
        Attributes.bInheritHandle = FALSE;
        Security = &Attributes;
    }

    _snwprintf(Name, ARRAYSIZE(Name) - 1, L"%sDBWIN_BUFFER_READY", Prefix);
    Capture->BufferReady = CreateEventW(Security, FALSE, FALSE, Name);
    if (Capture->BufferReady && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        fwprintf(stderr, L"dbglog: another debug output monitor is already running (%s)\n", Name);
        goto Fail;
    }
    _snwprintf(Name, ARRAYSIZE(Name) - 1, L"%sDBWIN_DATA_READY", Prefix);
    Capture->DataReady = CreateEventW(Security, FALSE, FALSE, Name);
    _snwprintf(Name, ARRAYSIZE(Name) - 1, L"%sDBWIN_BUFFER", Prefix);
    Capture->Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, Security, PAGE_READWRITE, 0, DBWIN_BUFFER_SIZE, Name);
    if (!Capture->BufferReady || !Capture->DataReady || !Capture->Mapping)
        goto Fail;

    Capture->View = MapViewOfFile(Capture->Mapping, FILE_MAP_READ, 0, 0, DBWIN_BUFFER_SIZE);
    if (!Capture->View)
        goto Fail;

    if (Descriptor)
        LocalFree(Descriptor);
    return TRUE;

Fail:
    if (Capture->DataReady)
        CloseHandle(Capture->DataReady);
    if (Capture->BufferReady)
        CloseHandle(Capture->BufferReady);
    if (Capture->Mapping)
        CloseHandle(Capture->Mapping);
    ZeroMemory(Capture, sizeof(*Capture));
    if (Descriptor)
        LocalFree(Descriptor);
    return FALSE;
}

static DWORD
WINAPI
Win32CaptureThread(
    _In_ LPVOID Parameter)
{
    PDBWIN_CAPTURE Capture = (PDBWIN_CAPTURE)Parameter;
    LINE_STATE *Line;
    HANDLE Waits[2];

    Line = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Line));
    if (!Line)
        return 1;

    Waits[0] = StopEvent;
    Waits[1] = Capture->DataReady;
    SetEvent(Capture->BufferReady);
    for (;;)
    {
        DWORD Result = WaitForMultipleObjects(2, Waits, FALSE, READ_TIMEOUT_MS);

        if (Result == WAIT_OBJECT_0)
            break;
        if (Result == WAIT_OBJECT_0 + 1)
        {
            ULONG ProcessId = *(DWORD *)Capture->View;
            PCSTR Text = (PCSTR)Capture->View + sizeof(DWORD);
            SIZE_T Length = strnlen(Text, DBWIN_BUFFER_SIZE - sizeof(DWORD));

            if (Line->Length && Line->ProcessId != ProcessId)
                FlushLine(Line, FALSE);
            AppendText(Line, FALSE, Now(), ProcessId, Text, Length);
            SetEvent(Capture->BufferReady);
        }
        else
        {
            FlushLine(Line, FALSE);
        }
    }

    FlushLine(Line, FALSE);
    HeapFree(GetProcessHeap(), 0, Line);
    return 0;
}

static BOOL
WINAPI
ConsoleHandler(
    _In_ DWORD Event)
{
    UNREFERENCED_PARAMETER(Event);
    SetEvent(StopEvent);
    return TRUE;
}

static VOID
Usage(VOID)
{
    fputws(L"Captures kernel debug prints (DbgPrint, DPRINT1) and OutputDebugString output.\n\n"
           L"DBGLOG [/K] [/W] [/G] [/L file [/A]] [/C] [/Q]\n\n"
           L"  /K       Capture kernel debug output (loads the DbgCap driver).\n"
           L"  /W       Capture Win32 OutputDebugString output of this session.\n"
           L"  /G       Capture Win32 output of all sessions (Global namespace).\n"
           L"  /L file  Write every line to file.\n"
           L"  /A       Append to the log file instead of replacing it.\n"
           L"  /C       Show clock time instead of seconds since capture start.\n"
           L"  /Q       Do not echo lines to the console.\n\n"
           L"Without /K, /W or /G, kernel and Win32 output are both captured.\n"
           L"Press Ctrl+C to stop.\n", stdout);
}

int
wmain(
    int argc,
    WCHAR *argv[])
{
    BOOL Kernel = FALSE, Win32 = FALSE, Global = FALSE, Append = FALSE, StartedService = FALSE;
    PCWSTR LogPath = NULL;
    HANDLE Threads[2], Device = INVALID_HANDLE_VALUE;
    DBWIN_CAPTURE Capture;
    DWORD ThreadCount = 0;
    int Index;

    for (Index = 1; Index < argc; Index++)
    {
        PCWSTR Arg = argv[Index];

        if ((Arg[0] != L'/' && Arg[0] != L'-') || !Arg[1] || Arg[2])
        {
            Usage();
            return 1;
        }
        switch (towupper(Arg[1]))
        {
            case L'K': Kernel = TRUE; break;
            case L'W': Win32 = TRUE; break;
            case L'G': Win32 = TRUE; Global = TRUE; break;
            case L'A': Append = TRUE; break;
            case L'C': ClockTime = TRUE; break;
            case L'Q': Quiet = TRUE; break;
            case L'L':
                if (++Index >= argc)
                {
                    Usage();
                    return 1;
                }
                LogPath = argv[Index];
                break;
            default:
                Usage();
                return Arg[1] == L'?' ? 0 : 1;
        }
    }
    if (!Kernel && !Win32)
        Kernel = Win32 = TRUE;

    InitializeCriticalSection(&OutputLock);
    StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    StartTime = Now();

    if (LogPath)
    {
        LogFile = CreateFileW(LogPath,
                              Append ? FILE_APPEND_DATA : GENERIC_WRITE,
                              FILE_SHARE_READ,
                              NULL,
                              Append ? OPEN_ALWAYS : CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL);
        if (LogFile == INVALID_HANDLE_VALUE)
        {
            fwprintf(stderr, L"dbglog: cannot open log file %s (error %lu)\n", LogPath, GetLastError());
            return 1;
        }
    }

    if (Kernel)
    {
        Device = OpenCaptureDevice(&StartedService);
        if (Device == INVALID_HANDLE_VALUE)
        {
            fwprintf(stderr, L"dbglog: cannot capture kernel output (error %lu); run as an administrator\n",
                     GetLastError());
        }
        else
        {
            Threads[ThreadCount] = CreateThread(NULL, 0, KernelCaptureThread, Device, 0, NULL);
            if (Threads[ThreadCount])
                ThreadCount++;
        }
    }

    if (Win32)
    {
        if (!OpenDbwin(&Capture, Global))
        {
            fwprintf(stderr, L"dbglog: cannot capture Win32 output (error %lu)\n", GetLastError());
        }
        else
        {
            Threads[ThreadCount] = CreateThread(NULL, 0, Win32CaptureThread, &Capture, 0, NULL);
            if (Threads[ThreadCount])
                ThreadCount++;
        }
    }

    if (ThreadCount == 0)
        return 1;

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    WaitForMultipleObjects(ThreadCount, Threads, TRUE, INFINITE);

    while (ThreadCount)
        CloseHandle(Threads[--ThreadCount]);
    if (Device != INVALID_HANDLE_VALUE)
        CloseHandle(Device);
    if (StartedService)
        StopCaptureService();
    if (LogFile != INVALID_HANDLE_VALUE)
        CloseHandle(LogFile);
    return 0;
}
