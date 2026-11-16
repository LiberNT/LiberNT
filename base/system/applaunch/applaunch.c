/*
 * PROJECT:     LiberNT Application Disk Launcher
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Runs the launcher of an attached application disk at logon
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <string.h>
#include <windef.h>
#include <winbase.h>
#include <strsafe.h>
#include <wmidata.h>

#define SMBIOS_OEM_STRINGS 11
#define SMBIOS_END_OF_TABLE 127
#define APP_DRIVE_WAIT_MS (60 * 1000)
#define APP_DRIVE_POLL_MS 100

static const CHAR TestRunString[] = "libernt.applaunch";

static BOOL
IsTestRun(VOID)
{
    PMSSmBios_RawSMBiosTables Tables;
    PUCHAR Entry;
    PUCHAR End;
    PUCHAR Strings;
    PUCHAR Next;
    UINT Size;
    UINT Returned;
    BOOL Found = FALSE;

    Size = GetSystemFirmwareTable('RSMB', 0, NULL, 0);
    if (Size <= FIELD_OFFSET(MSSmBios_RawSMBiosTables, SMBiosData))
        return FALSE;

    Tables = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Tables)
        return FALSE;

    Returned = GetSystemFirmwareTable('RSMB', 0, Tables, Size);
    if (Returned <= FIELD_OFFSET(MSSmBios_RawSMBiosTables, SMBiosData) || Returned > Size ||
        Tables->Size > Returned - FIELD_OFFSET(MSSmBios_RawSMBiosTables, SMBiosData))
    {
        goto Exit;
    }

    Entry = Tables->SMBiosData;
    End = Entry + Tables->Size;
    while (!Found && End - Entry >= 4 && Entry[1] >= 4 && Entry[1] <= End - Entry)
    {
        Strings = Entry + Entry[1];
        Next = Strings;
        while (End - Next >= 2 && (Next[0] != 0 || Next[1] != 0))
            Next++;
        if (End - Next < 2)
            break;

        if (Entry[0] == SMBIOS_OEM_STRINGS)
        {
            while (Strings < Next + 1 && *Strings != 0)
            {
                if (strcmp((PCSTR)Strings, TestRunString) == 0)
                {
                    Found = TRUE;
                    break;
                }
                Strings += strlen((PCSTR)Strings) + 1;
            }
        }

        if (Entry[0] == SMBIOS_END_OF_TABLE)
            break;
        Entry = Next + 2;
    }

Exit:
    HeapFree(GetProcessHeap(), 0, Tables);
    return Found;
}

static VOID
Say(PCWSTR Format, ...)
{
    WCHAR Line[512];
    va_list Args;

    va_start(Args, Format);
    StringCchVPrintfW(Line, ARRAYSIZE(Line), Format, Args);
    va_end(Args);
    StringCchCatW(Line, ARRAYSIZE(Line), L"\n");
    OutputDebugStringW(Line);
}

static BOOL
FindAppDrive(PWSTR Drive)
{
    static const WCHAR Order[] = L"DEFGHIJKLMNOPQRSTUVWXYZC";
    WCHAR SystemDrive[MAX_PATH];
    WCHAR Path[] = L"?:\\launcher.cmd";
    DWORD Attributes;
    SIZE_T i;

    if (!GetEnvironmentVariableW(L"SystemDrive", SystemDrive, ARRAYSIZE(SystemDrive)))
        SystemDrive[0] = UNICODE_NULL;

    for (i = 0; Order[i] != UNICODE_NULL; i++)
    {
        if (SystemDrive[0] != UNICODE_NULL && SystemDrive[1] == L':' &&
            (SystemDrive[0] | 0x20) == (Order[i] | 0x20))
        {
            continue;
        }

        Path[0] = Order[i];
        Attributes = GetFileAttributesW(Path);
        if (Attributes != INVALID_FILE_ATTRIBUTES && !(Attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            Drive[0] = Order[i];
            Drive[1] = L':';
            Drive[2] = UNICODE_NULL;
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL
WaitForAppDrive(PWSTR Drive)
{
    ULONGLONG Deadline = GetTickCount64() + APP_DRIVE_WAIT_MS;

    /* Logon can come before the application disk has a drive letter */
    while (!FindAppDrive(Drive))
    {
        if (GetTickCount64() >= Deadline)
            return FALSE;
        Sleep(APP_DRIVE_POLL_MS);
    }

    return TRUE;
}

static BOOL
GetDesktop(PCWSTR Variable, PWSTR Desktop, SIZE_T Size)
{
    DWORD Length;
    DWORD Attributes;

    Length = GetEnvironmentVariableW(Variable, Desktop, (DWORD)Size);
    if (!Length || Length >= Size)
        return FALSE;
    if (FAILED(StringCchCatW(Desktop, Size, L"\\Desktop")))
        return FALSE;

    Attributes = GetFileAttributesW(Desktop);
    return Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static VOID
CopyShortcuts(PCWSTR Drive)
{
    WCHAR Desktop[MAX_PATH];
    WCHAR Pattern[MAX_PATH];
    WCHAR Source[MAX_PATH];
    WCHAR Target[MAX_PATH];
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    if (!GetDesktop(L"PUBLIC", Desktop, ARRAYSIZE(Desktop)) &&
        !GetDesktop(L"USERPROFILE", Desktop, ARRAYSIZE(Desktop)))
    {
        return;
    }

    StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\shortcuts\\*", Drive);
    Find = FindFirstFileW(Pattern, &Data);
    if (Find == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (FAILED(StringCchPrintfW(Source, ARRAYSIZE(Source), L"%s\\shortcuts\\%s", Drive, Data.cFileName)) ||
            FAILED(StringCchPrintfW(Target, ARRAYSIZE(Target), L"%s\\%s", Desktop, Data.cFileName)))
        {
            continue;
        }
        if (CopyFileW(Source, Target, TRUE))
            Say(L"APPLAUNCH_SHORTCUT %s", Data.cFileName);
    } while (FindNextFileW(Find, &Data));

    FindClose(Find);
}

static DWORD
RunLauncher(PCWSTR Drive)
{
    WCHAR Shell[MAX_PATH];
    WCHAR CommandLine[2 * MAX_PATH];
    WCHAR Directory[4];
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    DWORD ExitCode;
    UINT Length;

    Length = GetSystemDirectoryW(Shell, ARRAYSIZE(Shell));
    if (!Length || Length >= ARRAYSIZE(Shell) ||
        FAILED(StringCchCatW(Shell, ARRAYSIZE(Shell), L"\\cmd.exe")) ||
        FAILED(StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine),
                                L"\"%s\" /c \"\"%s\\launcher.cmd\"\"", Shell, Drive)) ||
        FAILED(StringCchPrintfW(Directory, ARRAYSIZE(Directory), L"%s\\", Drive)))
    {
        return ERROR_BUFFER_OVERFLOW;
    }

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    if (!CreateProcessW(Shell, CommandLine, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL,
                        Directory, &StartupInfo, &ProcessInfo))
    {
        return GetLastError();
    }

    CloseHandle(ProcessInfo.hThread);
    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
    if (!GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode))
        ExitCode = GetLastError();
    CloseHandle(ProcessInfo.hProcess);
    return ExitCode;
}

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    WCHAR Drive[3];

    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    if (!IsTestRun())
        return 0;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    if (WaitForAppDrive(Drive))
    {
        Say(L"APPLAUNCH_BEGIN %s", Drive);
        CopyShortcuts(Drive);
        Say(L"APPLAUNCH_EXIT %ld", (LONG)RunLauncher(Drive));
    }
    else
    {
        Say(L"APPLAUNCH_NO_DISK");
    }

    Say(L"APPLAUNCH_DONE");
    return 0;
}
