/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Transacted file operations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>

#define NDEBUG
#include <debug.h>

static
HANDLE
BasepEnterTransaction(
    _In_opt_ HANDLE hTransaction)
{
    HANDLE Previous = RtlGetCurrentTransaction();

    RtlSetCurrentTransaction(hTransaction);
    return Previous;
}

static
VOID
BasepLeaveTransaction(
    _In_opt_ HANDLE Previous)
{
    RtlSetCurrentTransaction(Previous);
}

BOOL
WINAPI
CopyFileTransactedW(
    _In_ LPCWSTR lpExistingFileName,
    _In_ LPCWSTR lpNewFileName,
    _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
    _In_opt_ LPVOID lpData,
    _In_opt_ LPBOOL pbCancel,
    _In_ DWORD dwCopyFlags,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = CopyFileExW(lpExistingFileName, lpNewFileName, lpProgressRoutine,
                         lpData, pbCancel, dwCopyFlags);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
CopyFileTransactedA(
    _In_ LPCSTR lpExistingFileName,
    _In_ LPCSTR lpNewFileName,
    _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
    _In_opt_ LPVOID lpData,
    _In_opt_ LPBOOL pbCancel,
    _In_ DWORD dwCopyFlags,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = CopyFileExA(lpExistingFileName, lpNewFileName, lpProgressRoutine,
                         lpData, pbCancel, dwCopyFlags);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
CreateDirectoryTransactedW(
    _In_opt_ LPCWSTR lpTemplateDirectory,
    _In_ LPCWSTR lpNewDirectory,
    _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    if (lpTemplateDirectory != NULL)
    {
        Result = CreateDirectoryExW(lpTemplateDirectory, lpNewDirectory, lpSecurityAttributes);
    }
    else
    {
        Result = CreateDirectoryW(lpNewDirectory, lpSecurityAttributes);
    }
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
CreateDirectoryTransactedA(
    _In_opt_ LPCSTR lpTemplateDirectory,
    _In_ LPCSTR lpNewDirectory,
    _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    if (lpTemplateDirectory != NULL)
    {
        Result = CreateDirectoryExA(lpTemplateDirectory, lpNewDirectory, lpSecurityAttributes);
    }
    else
    {
        Result = CreateDirectoryA(lpNewDirectory, lpSecurityAttributes);
    }
    BasepLeaveTransaction(Previous);
    return Result;
}

HANDLE
WINAPI
CreateFileTransactedW(
    _In_ LPCWSTR lpFileName,
    _In_ DWORD dwDesiredAccess,
    _In_ DWORD dwShareMode,
    _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    _In_ DWORD dwCreationDisposition,
    _In_ DWORD dwFlagsAndAttributes,
    _In_opt_ HANDLE hTemplateFile,
    _In_ HANDLE hTransaction,
    _In_opt_ PUSHORT pusMiniVersion,
    _Reserved_ PVOID lpExtendedParameter)
{
    HANDLE Previous;
    HANDLE Result;

    if (lpExtendedParameter != NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    /* TODO: hand a miniversion other than the default view to the file system */
    UNREFERENCED_PARAMETER(pusMiniVersion);

    Previous = BasepEnterTransaction(hTransaction);
    Result = CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                         dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    BasepLeaveTransaction(Previous);
    return Result;
}

HANDLE
WINAPI
CreateFileTransactedA(
    _In_ LPCSTR lpFileName,
    _In_ DWORD dwDesiredAccess,
    _In_ DWORD dwShareMode,
    _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    _In_ DWORD dwCreationDisposition,
    _In_ DWORD dwFlagsAndAttributes,
    _In_opt_ HANDLE hTemplateFile,
    _In_ HANDLE hTransaction,
    _In_opt_ PUSHORT pusMiniVersion,
    _Reserved_ PVOID lpExtendedParameter)
{
    HANDLE Previous;
    HANDLE Result;

    if (lpExtendedParameter != NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    /* TODO: hand a miniversion other than the default view to the file system */
    UNREFERENCED_PARAMETER(pusMiniVersion);

    Previous = BasepEnterTransaction(hTransaction);
    Result = CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                         dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
CreateHardLinkTransactedW(
    _In_ LPCWSTR lpFileName,
    _In_ LPCWSTR lpExistingFileName,
    _Reserved_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = CreateHardLinkW(lpFileName, lpExistingFileName, lpSecurityAttributes);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
CreateHardLinkTransactedA(
    _In_ LPCSTR lpFileName,
    _In_ LPCSTR lpExistingFileName,
    _Reserved_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = CreateHardLinkA(lpFileName, lpExistingFileName, lpSecurityAttributes);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOLEAN
WINAPI
CreateSymbolicLinkTransactedW(
    _In_ LPCWSTR lpSymlinkFileName,
    _In_ LPCWSTR lpTargetFileName,
    _In_ DWORD dwFlags,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOLEAN Result;

    Result = CreateSymbolicLinkW(lpSymlinkFileName, lpTargetFileName, dwFlags);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOLEAN
WINAPI
CreateSymbolicLinkTransactedA(
    _In_ LPCSTR lpSymlinkFileName,
    _In_ LPCSTR lpTargetFileName,
    _In_ DWORD dwFlags,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOLEAN Result;

    Result = CreateSymbolicLinkA(lpSymlinkFileName, lpTargetFileName, dwFlags);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
DeleteFileTransactedW(
    _In_ LPCWSTR lpFileName,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = DeleteFileW(lpFileName);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
DeleteFileTransactedA(
    _In_ LPCSTR lpFileName,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = DeleteFileA(lpFileName);
    BasepLeaveTransaction(Previous);
    return Result;
}

HANDLE
WINAPI
FindFirstFileNameTransactedW(
    _In_ LPCWSTR lpFileName,
    _In_ DWORD dwFlags,
    _Inout_ LPDWORD StringLength,
    _Out_writes_(*StringLength) PWSTR LinkName,
    _In_opt_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    HANDLE Result;

    Result = FindFirstFileNameW(lpFileName, dwFlags, StringLength, LinkName);
    BasepLeaveTransaction(Previous);
    return Result;
}

HANDLE
WINAPI
FindFirstFileTransactedW(
    _In_ LPCWSTR lpFileName,
    _In_ FINDEX_INFO_LEVELS fInfoLevelId,
    _Out_ LPVOID lpFindFileData,
    _In_ FINDEX_SEARCH_OPS fSearchOp,
    _Reserved_ LPVOID lpSearchFilter,
    _In_ DWORD dwAdditionalFlags,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    HANDLE Result;

    Result = FindFirstFileExW(lpFileName, fInfoLevelId, lpFindFileData, fSearchOp,
                              lpSearchFilter, dwAdditionalFlags);
    BasepLeaveTransaction(Previous);
    return Result;
}

HANDLE
WINAPI
FindFirstFileTransactedA(
    _In_ LPCSTR lpFileName,
    _In_ FINDEX_INFO_LEVELS fInfoLevelId,
    _Out_ LPVOID lpFindFileData,
    _In_ FINDEX_SEARCH_OPS fSearchOp,
    _Reserved_ LPVOID lpSearchFilter,
    _In_ DWORD dwAdditionalFlags,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    HANDLE Result;

    Result = FindFirstFileExA(lpFileName, fInfoLevelId, lpFindFileData, fSearchOp,
                              lpSearchFilter, dwAdditionalFlags);
    BasepLeaveTransaction(Previous);
    return Result;
}

HANDLE
WINAPI
FindFirstStreamTransactedW(
    _In_ LPCWSTR lpFileName,
    _In_ STREAM_INFO_LEVELS InfoLevel,
    _Out_ LPVOID lpFindStreamData,
    _Reserved_ DWORD dwFlags,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    HANDLE Result;

    Result = FindFirstStreamW(lpFileName, InfoLevel, lpFindStreamData, dwFlags);
    BasepLeaveTransaction(Previous);
    return Result;
}

DWORD
WINAPI
GetCompressedFileSizeTransactedW(
    _In_ LPCWSTR lpFileName,
    _Out_opt_ LPDWORD lpFileSizeHigh,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    DWORD Result;

    Result = GetCompressedFileSizeW(lpFileName, lpFileSizeHigh);
    BasepLeaveTransaction(Previous);
    return Result;
}

DWORD
WINAPI
GetCompressedFileSizeTransactedA(
    _In_ LPCSTR lpFileName,
    _Out_opt_ LPDWORD lpFileSizeHigh,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    DWORD Result;

    Result = GetCompressedFileSizeA(lpFileName, lpFileSizeHigh);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
GetFileAttributesTransactedW(
    _In_ LPCWSTR lpFileName,
    _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
    _Out_ LPVOID lpFileInformation,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = GetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
GetFileAttributesTransactedA(
    _In_ LPCSTR lpFileName,
    _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
    _Out_ LPVOID lpFileInformation,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = GetFileAttributesExA(lpFileName, fInfoLevelId, lpFileInformation);
    BasepLeaveTransaction(Previous);
    return Result;
}

DWORD
WINAPI
GetFullPathNameTransactedW(
    _In_ LPCWSTR lpFileName,
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength, return + 1) LPWSTR lpBuffer,
    _Outptr_opt_ LPWSTR *lpFilePart,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    DWORD Result;

    Result = GetFullPathNameW(lpFileName, nBufferLength, lpBuffer, lpFilePart);
    BasepLeaveTransaction(Previous);
    return Result;
}

DWORD
WINAPI
GetFullPathNameTransactedA(
    _In_ LPCSTR lpFileName,
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength, return + 1) LPSTR lpBuffer,
    _Outptr_opt_ LPSTR *lpFilePart,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    DWORD Result;

    Result = GetFullPathNameA(lpFileName, nBufferLength, lpBuffer, lpFilePart);
    BasepLeaveTransaction(Previous);
    return Result;
}

DWORD
WINAPI
GetLongPathNameTransactedW(
    _In_ LPCWSTR lpszShortPath,
    _Out_writes_to_opt_(cchBuffer, return + 1) LPWSTR lpszLongPath,
    _In_ DWORD cchBuffer,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    DWORD Result;

    Result = GetLongPathNameW(lpszShortPath, lpszLongPath, cchBuffer);
    BasepLeaveTransaction(Previous);
    return Result;
}

DWORD
WINAPI
GetLongPathNameTransactedA(
    _In_ LPCSTR lpszShortPath,
    _Out_writes_to_opt_(cchBuffer, return + 1) LPSTR lpszLongPath,
    _In_ DWORD cchBuffer,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    DWORD Result;

    Result = GetLongPathNameA(lpszShortPath, lpszLongPath, cchBuffer);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
MoveFileTransactedW(
    _In_ LPCWSTR lpExistingFileName,
    _In_opt_ LPCWSTR lpNewFileName,
    _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
    _In_opt_ LPVOID lpData,
    _In_ DWORD dwFlags,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous;
    BOOL Result;

    if (dwFlags & MOVEFILE_FAIL_IF_NOT_TRACKABLE)
    {
        SetLastError(ERROR_NO_LINK_TRACKING_IN_TRANSACTION);
        return FALSE;
    }

    Previous = BasepEnterTransaction(hTransaction);
    Result = MoveFileWithProgressW(lpExistingFileName, lpNewFileName, lpProgressRoutine,
                                   lpData, dwFlags);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
MoveFileTransactedA(
    _In_ LPCSTR lpExistingFileName,
    _In_opt_ LPCSTR lpNewFileName,
    _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
    _In_opt_ LPVOID lpData,
    _In_ DWORD dwFlags,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous;
    BOOL Result;

    if (dwFlags & MOVEFILE_FAIL_IF_NOT_TRACKABLE)
    {
        SetLastError(ERROR_NO_LINK_TRACKING_IN_TRANSACTION);
        return FALSE;
    }

    Previous = BasepEnterTransaction(hTransaction);
    Result = MoveFileWithProgressA(lpExistingFileName, lpNewFileName, lpProgressRoutine,
                                   lpData, dwFlags);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
RemoveDirectoryTransactedW(
    _In_ LPCWSTR lpPathName,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = RemoveDirectoryW(lpPathName);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
RemoveDirectoryTransactedA(
    _In_ LPCSTR lpPathName,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = RemoveDirectoryA(lpPathName);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
SetFileAttributesTransactedW(
    _In_ LPCWSTR lpFileName,
    _In_ DWORD dwFileAttributes,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = SetFileAttributesW(lpFileName, dwFileAttributes);
    BasepLeaveTransaction(Previous);
    return Result;
}

BOOL
WINAPI
SetFileAttributesTransactedA(
    _In_ LPCSTR lpFileName,
    _In_ DWORD dwFileAttributes,
    _In_ HANDLE hTransaction)
{
    HANDLE Previous = BasepEnterTransaction(hTransaction);
    BOOL Result;

    Result = SetFileAttributesA(lpFileName, dwFileAttributes);
    BasepLeaveTransaction(Previous);
    return Result;
}
