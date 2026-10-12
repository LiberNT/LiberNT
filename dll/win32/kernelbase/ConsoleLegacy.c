/*
 * PROJECT:     LiberNT Kernel Base
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Console entry points outside the console driver protocol
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wincon.h>
#include <wincon_undoc.h>
#include <winioctl.h>
#include <winternl.h>
#include "wine/kernelbase.h"

typedef struct _CONSOLE_FS_DEVICE_INFORMATION
{
    DEVICE_TYPE DeviceType;
    ULONG Characteristics;
} CONSOLE_FS_DEVICE_INFORMATION;

static
VOID
ConsoleSetLastNtError(
    _In_ NTSTATUS Status)
{
    SetLastError(RtlNtStatusToDosError(Status));
}

static
BOOL
ConsoleNotImplemented(VOID)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOLEAN
WINAPI
ConDllInitialize(
    _In_ ULONG Reason,
    _In_opt_ PWSTR SessionDir)
{
    WCHAR Path[MAX_PATH];
    PWSTR Name;
    DWORD Length;

    UNREFERENCED_PARAMETER(SessionDir);

    if (Reason != DLL_PROCESS_ATTACH)
        return TRUE;

    Length = GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    if (Length && Length < ARRAYSIZE(Path))
    {
        Name = wcsrchr(Path, L'\\');
        SetConsoleInputExeNameW(Name ? Name + 1 : Path);
    }
    init_console();
    return TRUE;
}

HANDLE
WINAPI
GetStdHandle(
    _In_ DWORD nStdHandle)
{
    RTL_USER_PROCESS_PARAMETERS *Parameters = NtCurrentTeb()->Peb->ProcessParameters;

    switch (nStdHandle)
    {
        case STD_INPUT_HANDLE:
            return Parameters->hStdInput;
        case STD_OUTPUT_HANDLE:
            return Parameters->hStdOutput;
        case STD_ERROR_HANDLE:
            return Parameters->hStdError;
    }
    SetLastError(ERROR_INVALID_HANDLE);
    return INVALID_HANDLE_VALUE;
}

BOOL
WINAPI
SetStdHandle(
    _In_ DWORD nStdHandle,
    _In_ HANDLE hHandle)
{
    RTL_USER_PROCESS_PARAMETERS *Parameters = NtCurrentTeb()->Peb->ProcessParameters;

    switch (nStdHandle)
    {
        case STD_INPUT_HANDLE:
            Parameters->hStdInput = hHandle;
            return TRUE;
        case STD_OUTPUT_HANDLE:
            Parameters->hStdOutput = hHandle;
            return TRUE;
        case STD_ERROR_HANDLE:
            Parameters->hStdError = hHandle;
            return TRUE;
    }
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

HANDLE
WINAPI
OpenConsoleW(
    _In_ LPCWSTR wsName,
    _In_ DWORD dwDesiredAccess,
    _In_ BOOL bInheritHandle,
    _In_ DWORD dwShareMode)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Name;
    HANDLE Handle;
    NTSTATUS Status;

    if (dwShareMode != OPEN_EXISTING)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    if (wsName && !_wcsicmp(wsName, L"CONIN$"))
        RtlInitUnicodeString(&Name, L"\\Device\\ConDrv\\CurrentIn");
    else if (wsName && !_wcsicmp(wsName, L"CONOUT$"))
        RtlInitUnicodeString(&Name, L"\\Device\\ConDrv\\CurrentOut");
    else
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    InitializeObjectAttributes(&ObjectAttributes, &Name,
                               OBJ_CASE_INSENSITIVE | (bInheritHandle ? OBJ_INHERIT : 0),
                               NULL, NULL);
    Status = NtCreateFile(&Handle, dwDesiredAccess | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                          &ObjectAttributes, &IoStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        ConsoleSetLastNtError(Status);
        return INVALID_HANDLE_VALUE;
    }
    return Handle;
}

BOOL
WINAPI
VerifyConsoleIoHandle(
    _In_ HANDLE hIoHandle)
{
    CONSOLE_FS_DEVICE_INFORMATION DeviceInfo;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    Status = NtQueryVolumeInformationFile(hIoHandle, &IoStatusBlock, &DeviceInfo,
                                          sizeof(DeviceInfo), FileFsDeviceInformation);
    return NT_SUCCESS(Status) && DeviceInfo.DeviceType == FILE_DEVICE_CONSOLE;
}

BOOL
WINAPI
CloseConsoleHandle(
    _In_ HANDLE hHandle)
{
    return CloseHandle(hHandle);
}

HANDLE
WINAPI
DuplicateConsoleHandle(
    _In_ HANDLE hSourceHandle,
    _In_ DWORD dwDesiredAccess,
    _In_ BOOL bInheritHandle,
    _In_ DWORD dwOptions)
{
    HANDLE Handle;

    if (!DuplicateHandle(GetCurrentProcess(), hSourceHandle, GetCurrentProcess(), &Handle,
                         dwDesiredAccess, bInheritHandle, dwOptions))
    {
        return INVALID_HANDLE_VALUE;
    }
    return Handle;
}

HANDLE
WINAPI
GetConsoleInputWaitHandle(VOID)
{
    return GetStdHandle(STD_INPUT_HANDLE);
}

BOOL
WINAPI
ReadConsoleInputExW(
    _In_ HANDLE hConsoleInput,
    _Out_writes_to_(nLength, *lpNumberOfEventsRead) PINPUT_RECORD lpBuffer,
    _In_ DWORD nLength,
    _Out_ LPDWORD lpNumberOfEventsRead,
    _In_ WORD wFlags)
{
    DWORD Count;

    if (wFlags & ~(CONSOLE_READ_NOREMOVE | CONSOLE_READ_NOWAIT))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (wFlags & CONSOLE_READ_NOREMOVE)
        return PeekConsoleInputW(hConsoleInput, lpBuffer, nLength, lpNumberOfEventsRead);
    if (wFlags & CONSOLE_READ_NOWAIT)
    {
        if (!GetNumberOfConsoleInputEvents(hConsoleInput, &Count))
            return FALSE;
        if (!Count)
        {
            *lpNumberOfEventsRead = 0;
            return TRUE;
        }
    }
    return ReadConsoleInputW(hConsoleInput, lpBuffer, nLength, lpNumberOfEventsRead);
}

BOOL
WINAPI
ReadConsoleInputExA(
    _In_ HANDLE hConsoleInput,
    _Out_writes_to_(nLength, *lpNumberOfEventsRead) PINPUT_RECORD lpBuffer,
    _In_ DWORD nLength,
    _Out_ LPDWORD lpNumberOfEventsRead,
    _In_ WORD wFlags)
{
    DWORD Count;

    if (wFlags & ~(CONSOLE_READ_NOREMOVE | CONSOLE_READ_NOWAIT))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (wFlags & CONSOLE_READ_NOREMOVE)
        return PeekConsoleInputA(hConsoleInput, lpBuffer, nLength, lpNumberOfEventsRead);
    if (wFlags & CONSOLE_READ_NOWAIT)
    {
        if (!GetNumberOfConsoleInputEvents(hConsoleInput, &Count))
            return FALSE;
        if (!Count)
        {
            *lpNumberOfEventsRead = 0;
            return TRUE;
        }
    }
    return ReadConsoleInputA(hConsoleInput, lpBuffer, nLength, lpNumberOfEventsRead);
}

DWORD
WINAPI
GetNumberOfConsoleFonts(VOID)
{
    return 1;
}

DWORD
WINAPI
GetConsoleFontInfo(
    _In_ HANDLE hConsoleOutput,
    _In_ BOOL bMaximumWindow,
    _In_ DWORD nFontCount,
    _Out_ PCONSOLE_FONT_INFO lpConsoleFontInfo)
{
    SetLastError(LOWORD(E_NOTIMPL));
    return FALSE;
}

BOOL
WINAPI
SetConsoleFont(
    _In_ HANDLE hConsoleOutput,
    _In_ DWORD nFont)
{
    SetLastError(LOWORD(E_NOTIMPL));
    return FALSE;
}

BOOL
WINAPI
SetConsoleIcon(
    _In_ HICON hIcon)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
SetConsoleKeyShortcuts(
    _In_ BOOL bSet,
    _In_ BYTE bReserveKeys,
    _In_ LPAPPKEY lpAppKeys,
    _In_ DWORD dwNumAppKeys)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
GetConsoleKeyboardLayoutNameA(
    _Out_ LPSTR pszLayout)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
GetConsoleKeyboardLayoutNameW(
    _Out_ LPWSTR pszLayout)
{
    return ConsoleNotImplemented();
}

HMENU
WINAPI
ConsoleMenuControl(
    _In_ HANDLE hConsoleOutput,
    _In_ DWORD dwCmdIdLow,
    _In_ DWORD dwCmdIdHigh)
{
    ConsoleNotImplemented();
    return NULL;
}

BOOL
WINAPI
SetConsoleMenuClose(
    _In_ BOOL bEnable)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
SetConsoleCursor(
    _In_ HANDLE hConsoleOutput,
    _In_ HCURSOR hCursor)
{
    return ConsoleNotImplemented();
}

INT
WINAPI
ShowConsoleCursor(
    _In_ HANDLE hConsoleOutput,
    _In_ BOOL bShow)
{
    ConsoleNotImplemented();
    return -1;
}

BOOL
WINAPI
SetConsoleMaximumWindowSize(
    _In_ HANDLE hConsoleOutput,
    _In_ COORD dwMaximumSize)
{
    return ConsoleNotImplemented();
}

DWORD
WINAPI
SetLastConsoleEventActive(VOID)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
GetConsoleSelectionInfo(
    _Out_ PCONSOLE_SELECTION_INFO lpConsoleSelectionInfo)
{
    return ConsoleNotImplemented();
}

DWORD
WINAPI
GetConsoleAliasesA(
    _Out_writes_(AliasBufferLength) LPSTR AliasBuffer,
    _In_ DWORD AliasBufferLength,
    _In_ LPCSTR ExeName)
{
    return ConsoleNotImplemented();
}

DWORD
WINAPI
GetConsoleAliasesW(
    _Out_writes_(AliasBufferLength) LPWSTR AliasBuffer,
    _In_ DWORD AliasBufferLength,
    _In_ LPCWSTR ExeName)
{
    return ConsoleNotImplemented();
}

DWORD
WINAPI
GetConsoleAliasExesA(
    _Out_writes_(ExeNameBufferLength) LPSTR ExeNameBuffer,
    _In_ DWORD ExeNameBufferLength)
{
    return ConsoleNotImplemented();
}

DWORD
WINAPI
GetConsoleAliasExesW(
    _Out_writes_(ExeNameBufferLength) LPWSTR ExeNameBuffer,
    _In_ DWORD ExeNameBufferLength)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
SetConsoleNumberOfCommandsA(
    _In_ DWORD dwNumCommands,
    _In_ LPCSTR lpExeName)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
SetConsoleNumberOfCommandsW(
    _In_ DWORD dwNumCommands,
    _In_ LPCWSTR lpExeName)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
GetConsoleHistoryInfo(
    _Out_ PCONSOLE_HISTORY_INFO lpConsoleHistoryInfo)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
SetConsoleHistoryInfo(
    _In_ PCONSOLE_HISTORY_INFO lpConsoleHistoryInfo)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
GetConsoleHardwareState(
    _In_ HANDLE hConsoleOutput,
    _Out_ PDWORD Flags,
    _Out_ PDWORD State)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
SetConsoleHardwareState(
    _In_ HANDLE hConsoleOutput,
    _In_ DWORD Flags,
    _In_ DWORD State)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
InvalidateConsoleDIBits(
    _In_ HANDLE hConsoleOutput,
    _In_ PSMALL_RECT lpRect)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
SetConsolePalette(
    _In_ HANDLE hConsoleOutput,
    _In_ HPALETTE hPalette,
    _In_ UINT dwUsage)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
WriteConsoleInputVDMA(
    _In_ HANDLE hConsoleInput,
    _In_reads_(nLength) CONST INPUT_RECORD *lpBuffer,
    _In_ DWORD nLength,
    _Out_ LPDWORD lpNumberOfEventsWritten)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
WriteConsoleInputVDMW(
    _In_ HANDLE hConsoleInput,
    _In_reads_(nLength) CONST INPUT_RECORD *lpBuffer,
    _In_ DWORD nLength,
    _Out_ LPDWORD lpNumberOfEventsWritten)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
GetConsoleNlsMode(
    _In_ HANDLE hConsole,
    _Out_ PDWORD lpdwNlsMode)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
SetConsoleNlsMode(
    _In_ HANDLE hConsole,
    _In_ DWORD fdwNlsMode)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
GetConsoleCharType(
    _In_ HANDLE hConsole,
    _In_ COORD coordCheck,
    _Out_ PDWORD pdwType)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
SetConsoleLocalEUDC(
    _In_ HANDLE hConsoleHandle,
    _In_ WORD wCodePoint,
    _In_ COORD cFontSize,
    _In_ PCHAR lpSB)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
SetConsoleCursorMode(
    _In_ HANDLE hConsoleHandle,
    _In_ BOOL Blink,
    _In_ BOOL DBEnable)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
GetConsoleCursorMode(
    _In_ HANDLE hConsoleHandle,
    _Out_ PBOOL pbBlink,
    _Out_ PBOOL pbDBEnable)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
RegisterConsoleOS2(
    _In_ BOOL fOs2Register)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
SetConsoleOS2OemFormat(
    _In_ BOOL fOs2OemFormat)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
RegisterConsoleIME(
    _In_ HWND hWndConsoleIME,
    _Out_opt_ DWORD *lpdwConsoleThreadId)
{
    return ConsoleNotImplemented();
}

BOOL
WINAPI
UnregisterConsoleIME(VOID)
{
    return ConsoleNotImplemented();
}
