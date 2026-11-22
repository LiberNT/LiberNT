/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IoIs32bitProcess(NULL) answers for the process that sends the request
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "IoIs32bitProcess.h"

#define PROBE_DEVICE_PATH L"\\\\.\\Global\\GLOBALROOT\\Device\\" IO_IS32BIT_PROBE_DEVICE

static
DWORD
ProbeFromThisProcess(
    _Out_ PIO_IS32BIT_PROBE_RESULT Result)
{
    HANDLE Device;
    DWORD Returned = 0;
    DWORD Error = ERROR_SUCCESS;

    ZeroMemory(Result, sizeof(*Result));
    Device = CreateFileW(PROBE_DEVICE_PATH, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Device == INVALID_HANDLE_VALUE)
        return GetLastError();

    if (!DeviceIoControl(Device, IOCTL_IO_IS32BIT_PROBE, NULL, 0, Result, sizeof(*Result), &Returned, NULL))
        Error = GetLastError();
    else if (Returned != sizeof(*Result))
        Error = ERROR_INVALID_DATA;

    CloseHandle(Device);
    return Error;
}

static
BOOL
ParseHelperOutput(
    _In_z_ PCSTR Output,
    _Out_ PIO_IS32BIT_PROBE_RESULT Result)
{
    PUCHAR Bytes = (PUCHAR)Result;
    PCSTR Data = strstr(Output, "data=");
    SIZE_T i;

    ZeroMemory(Result, sizeof(*Result));
    if (Data == NULL)
        return FALSE;
    Data += 5;

    for (i = 0; i < sizeof(*Result); i++)
    {
        unsigned int Byte;

        if (sscanf(Data + 2 * i, "%2x", &Byte) != 1)
            return FALSE;
        Bytes[i] = (UCHAR)Byte;
    }
    return TRUE;
}

static
DWORD
ProbeFromWow64Process(
    _In_z_ PCWSTR Helper,
    _Out_ PIO_IS32BIT_PROBE_RESULT Result)
{
    SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    WCHAR CommandLine[MAX_PATH * 2];
    CHAR Output[512];
    HANDLE ReadPipe, WritePipe;
    DWORD Total = 0, Read, ExitCode = ERROR_GEN_FAILURE;

    ZeroMemory(Result, sizeof(*Result));
    if (!CreatePipe(&ReadPipe, &WritePipe, &Inherit, 0))
        return GetLastError();
    SetHandleInformation(ReadPipe, HANDLE_FLAG_INHERIT, 0);

    StringCbPrintfW(CommandLine, sizeof(CommandLine), L"\"%s\" %s %lx %lu",
                    Helper, PROBE_DEVICE_PATH, (ULONG)IOCTL_IO_IS32BIT_PROBE, (ULONG)sizeof(*Result));
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdOutput = WritePipe;
    StartupInfo.hStdError = WritePipe;
    StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    if (!CreateProcessW(Helper, CommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        ExitCode = GetLastError();
        CloseHandle(WritePipe);
        CloseHandle(ReadPipe);
        return ExitCode;
    }
    CloseHandle(WritePipe);

    while (Total < sizeof(Output) - 1 &&
           ReadFile(ReadPipe, Output + Total, sizeof(Output) - 1 - Total, &Read, NULL) && Read != 0)
    {
        Total += Read;
    }
    Output[Total] = ANSI_NULL;
    CloseHandle(ReadPipe);

    WaitForSingleObject(ProcessInfo.hProcess, 10000);
    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);

    trace("helper: %s", Output);
    if (ExitCode != ERROR_SUCCESS)
        return ExitCode;
    return ParseHelperOutput(Output, Result) ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}

static
VOID
CheckCallers(VOID)
{
    IO_IS32BIT_PROBE_RESULT Result;
    WCHAR Helper[MAX_PATH];
    DWORD Error;

    Error = ProbeFromThisProcess(&Result);
    if (skip(Error != ERROR_NOT_SUPPORTED, "IoIs32bitProcess is not exported on this architecture\n"))
        return;
    ok_eq_int(Error, ERROR_SUCCESS);
    ok_eq_ulong(Result.ProcessIsWow64, (ULONG)(sizeof(PVOID) == sizeof(ULONG)));
    ok_eq_ulong(Result.NullForm, Result.ProcessIsWow64);
    ok_eq_ulong(Result.IrpForm, Result.ProcessIsWow64);
    ok_eq_ulong(Result.KernelIrpForm, 0UL);

    if (!GetSystemWow64DirectoryW(Helper, ARRAYSIZE(Helper)))
        Helper[0] = UNICODE_NULL;
    StringCbCatW(Helper, sizeof(Helper), L"\\kmtwow64.exe");
    if (skip(sizeof(PVOID) != sizeof(ULONG) && GetFileAttributesW(Helper) != INVALID_FILE_ATTRIBUTES,
             "no 32-bit helper %ls\n", Helper))
    {
        return;
    }

    Error = ProbeFromWow64Process(Helper, &Result);
    ok_eq_int(Error, ERROR_SUCCESS);
    ok_eq_ulong(Result.ProcessIsWow64, 1UL);
    ok_eq_ulong(Result.NullForm, 1UL);
    ok_eq_ulong(Result.IrpForm, 1UL);
    ok_eq_ulong(Result.KernelIrpForm, 0UL);
}

START_TEST(IoIs32bitProcess)
{
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"IoIs32bitProcess", FALSE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
        return;

    CheckCallers();

    KmtCloseDriver();
    KmtUnloadDriver();
}
