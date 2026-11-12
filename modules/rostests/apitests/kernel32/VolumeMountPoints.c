/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test FindFirst/FindNextVolumeMountPoint and FindVolumeMountPointClose
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/iofuncs.h>

#define MOUNT_LEAF L"libernt_vmp_apitest"
#define JUNCTION_LEAF L"libernt_vmp_junction"
#define MAX_ENTRIES 64

typedef HANDLE (WINAPI *FN_FindFirstVolumeMountPointW)(LPCWSTR, LPWSTR, DWORD);
typedef HANDLE (WINAPI *FN_FindFirstVolumeMountPointA)(LPCSTR, LPSTR, DWORD);
typedef BOOL (WINAPI *FN_FindNextVolumeMountPointW)(HANDLE, LPWSTR, DWORD);
typedef BOOL (WINAPI *FN_FindNextVolumeMountPointA)(HANDLE, LPSTR, DWORD);
typedef BOOL (WINAPI *FN_FindVolumeMountPointClose)(HANDLE);

static FN_FindFirstVolumeMountPointW pFindFirstW;
static FN_FindFirstVolumeMountPointA pFindFirstA;
static FN_FindNextVolumeMountPointW pFindNextW;
static FN_FindNextVolumeMountPointA pFindNextA;
static FN_FindVolumeMountPointClose pFindClose;

static WCHAR DriveRoot[4];
static WCHAR VolumeName[MAX_PATH];
static WCHAR MountDir[MAX_PATH];
static WCHAR MountDirSlash[MAX_PATH];
static WCHAR JunctionDir[MAX_PATH];

typedef struct _SCAN_RESULT
{
    BOOL Opened;
    DWORD FirstError;
    DWORD EndError;
    ULONG Entries;
    ULONG MountHits;
    ULONG JunctionHits;
    BOOL AllRelative;
} SCAN_RESULT;

static
VOID
ClassifyName(
    _In_ PCWSTR Name,
    _Inout_ SCAN_RESULT *Result)
{
    SIZE_T Length = wcslen(Name);

    Result->Entries++;
    if (Length == 0 || Name[0] == L'\\' || Name[Length - 1] != L'\\' || wcschr(Name, L':') != NULL)
        Result->AllRelative = FALSE;
    if (_wcsicmp(Name, MOUNT_LEAF L"\\") == 0)
        Result->MountHits++;
    if (_wcsnicmp(Name, JUNCTION_LEAF, wcslen(JUNCTION_LEAF)) == 0)
        Result->JunctionHits++;
}

static
SCAN_RESULT
ScanW(
    _In_ PCWSTR Root)
{
    SCAN_RESULT Result;
    WCHAR Name[MAX_PATH];
    HANDLE Find;
    ULONG Count;
    BOOL Ok;

    RtlZeroMemory(&Result, sizeof(Result));
    Result.AllRelative = TRUE;

    SetLastError(0xdeadbeef);
    Find = pFindFirstW(Root, Name, RTL_NUMBER_OF(Name));
    if (Find == INVALID_HANDLE_VALUE || Find == NULL)
    {
        Result.FirstError = GetLastError();
        return Result;
    }

    Result.Opened = TRUE;
    ClassifyName(Name, &Result);
    for (Count = 1; Count < MAX_ENTRIES; Count++)
    {
        SetLastError(0xdeadbeef);
        Ok = pFindNextW(Find, Name, RTL_NUMBER_OF(Name));
        if (!Ok)
        {
            Result.EndError = GetLastError();
            break;
        }
        ClassifyName(Name, &Result);
    }

    Ok = pFindClose(Find);
    ok(Ok == TRUE, "FindVolumeMountPointClose failed with %lu\n", GetLastError());
    return Result;
}

static
SCAN_RESULT
ScanA(
    _In_ PCSTR Root)
{
    SCAN_RESULT Result;
    CHAR NameA[MAX_PATH];
    WCHAR NameW[MAX_PATH];
    HANDLE Find;
    ULONG Count;
    BOOL Ok;

    RtlZeroMemory(&Result, sizeof(Result));
    Result.AllRelative = TRUE;

    SetLastError(0xdeadbeef);
    Find = pFindFirstA(Root, NameA, sizeof(NameA));
    if (Find == INVALID_HANDLE_VALUE || Find == NULL)
    {
        Result.FirstError = GetLastError();
        return Result;
    }

    Result.Opened = TRUE;
    MultiByteToWideChar(CP_ACP, 0, NameA, -1, NameW, RTL_NUMBER_OF(NameW));
    ClassifyName(NameW, &Result);
    for (Count = 1; Count < MAX_ENTRIES; Count++)
    {
        SetLastError(0xdeadbeef);
        Ok = pFindNextA(Find, NameA, sizeof(NameA));
        if (!Ok)
        {
            Result.EndError = GetLastError();
            break;
        }
        MultiByteToWideChar(CP_ACP, 0, NameA, -1, NameW, RTL_NUMBER_OF(NameW));
        ClassifyName(NameW, &Result);
    }

    Ok = pFindClose(Find);
    ok(Ok == TRUE, "FindVolumeMountPointClose failed with %lu\n", GetLastError());
    return Result;
}

static
BOOL
CreateJunction(
    _In_ PCWSTR Directory,
    _In_ PCWSTR NtTarget)
{
    UCHAR Buffer[FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer) + 2 * MAX_PATH * sizeof(WCHAR)];
    PREPARSE_DATA_BUFFER Reparse = (PREPARSE_DATA_BUFFER)Buffer;
    USHORT TargetLength = (USHORT)(wcslen(NtTarget) * sizeof(WCHAR));
    DWORD Returned;
    HANDLE Handle;
    BOOL Ok;

    if (!CreateDirectoryW(Directory, NULL))
        return FALSE;

    Handle = CreateFileW(Directory, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                         FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (Handle == INVALID_HANDLE_VALUE)
        return FALSE;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Reparse->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    Reparse->MountPointReparseBuffer.SubstituteNameOffset = 0;
    Reparse->MountPointReparseBuffer.SubstituteNameLength = TargetLength;
    Reparse->MountPointReparseBuffer.PrintNameOffset = TargetLength + sizeof(WCHAR);
    Reparse->MountPointReparseBuffer.PrintNameLength = 0;
    RtlCopyMemory(Reparse->MountPointReparseBuffer.PathBuffer, NtTarget, TargetLength);
    Reparse->ReparseDataLength = (USHORT)(FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer) -
                                          FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer) +
                                          TargetLength + 2 * sizeof(WCHAR));
    Ok = DeviceIoControl(Handle, FSCTL_SET_REPARSE_POINT, Reparse,
                         FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer) + Reparse->ReparseDataLength,
                         NULL, 0, &Returned, NULL);
    CloseHandle(Handle);
    return Ok;
}

static
VOID
TestParameters(VOID)
{
    WCHAR Name[MAX_PATH];
    WCHAR NoSlash[MAX_PATH];
    HANDLE Find;
    BOOL Ok;

    StringCchCopyW(NoSlash, RTL_NUMBER_OF(NoSlash), VolumeName);
    NoSlash[wcslen(NoSlash) - 1] = UNICODE_NULL;
    SetLastError(0xdeadbeef);
    Find = pFindFirstW(NoSlash, Name, RTL_NUMBER_OF(Name));
    ok(Find == INVALID_HANDLE_VALUE, "FindFirstVolumeMountPointW without a trailing backslash returned %p\n", Find);
    ok(GetLastError() == ERROR_INVALID_NAME, "Last error %lu\n", GetLastError());
    if (Find != INVALID_HANDLE_VALUE && Find != NULL)
        pFindClose(Find);

    SetLastError(0xdeadbeef);
    Ok = pFindNextW(INVALID_HANDLE_VALUE, Name, RTL_NUMBER_OF(Name));
    ok(Ok == FALSE, "FindNextVolumeMountPointW(INVALID_HANDLE_VALUE) succeeded\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "Last error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ok = pFindClose(INVALID_HANDLE_VALUE);
    ok(Ok == FALSE, "FindVolumeMountPointClose(INVALID_HANDLE_VALUE) succeeded\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "Last error %lu\n", GetLastError());
}

static
VOID
TestSmallBuffer(VOID)
{
    WCHAR Name[MAX_PATH];
    WCHAR First[MAX_PATH];
    HANDLE Find;
    BOOL Ok;

    SetLastError(0xdeadbeef);
    Find = pFindFirstW(VolumeName, Name, 1);
    ok(Find == INVALID_HANDLE_VALUE, "FindFirstVolumeMountPointW with a 1-character buffer returned %p\n", Find);
    ok(GetLastError() == ERROR_FILENAME_EXCED_RANGE, "Last error %lu\n", GetLastError());
    if (Find != INVALID_HANDLE_VALUE && Find != NULL)
        pFindClose(Find);

    Find = pFindFirstW(VolumeName, First, RTL_NUMBER_OF(First));
    ok(Find != INVALID_HANDLE_VALUE, "FindFirstVolumeMountPointW failed with %lu\n", GetLastError());
    if (Find == INVALID_HANDLE_VALUE || Find == NULL)
        return;

    SetLastError(0xdeadbeef);
    Ok = pFindNextW(Find, Name, 1);
    if (!Ok && GetLastError() == ERROR_FILENAME_EXCED_RANGE)
    {
        WCHAR Retry[MAX_PATH];

        Ok = pFindNextW(Find, Retry, RTL_NUMBER_OF(Retry));
        ok(Ok == TRUE, "FindNextVolumeMountPointW after a short buffer failed with %lu\n", GetLastError());
        if (Ok)
            ok(_wcsicmp(Retry, First) != 0, "The retried entry repeats the first one ('%S')\n", Retry);
    }
    else
    {
        ok(Ok == FALSE && GetLastError() == ERROR_NO_MORE_FILES,
           "FindNextVolumeMountPointW with a 1-character buffer returned %d, error %lu\n", Ok, GetLastError());
    }

    pFindClose(Find);
}

START_TEST(VolumeMountPoints)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    WCHAR WindowsDir[MAX_PATH];
    CHAR VolumeNameA[MAX_PATH];
    WCHAR JunctionTarget[MAX_PATH];
    SCAN_RESULT Scan;
    BOOL Ok;

    pFindFirstW = (FN_FindFirstVolumeMountPointW)GetProcAddress(Kernel32, "FindFirstVolumeMountPointW");
    pFindFirstA = (FN_FindFirstVolumeMountPointA)GetProcAddress(Kernel32, "FindFirstVolumeMountPointA");
    pFindNextW = (FN_FindNextVolumeMountPointW)GetProcAddress(Kernel32, "FindNextVolumeMountPointW");
    pFindNextA = (FN_FindNextVolumeMountPointA)GetProcAddress(Kernel32, "FindNextVolumeMountPointA");
    pFindClose = (FN_FindVolumeMountPointClose)GetProcAddress(Kernel32, "FindVolumeMountPointClose");
    if (!pFindFirstW || !pFindFirstA || !pFindNextW || !pFindNextA || !pFindClose)
    {
        skip("Volume mount point enumeration is not exported\n");
        return;
    }

    GetWindowsDirectoryW(WindowsDir, RTL_NUMBER_OF(WindowsDir));
    DriveRoot[0] = WindowsDir[0];
    DriveRoot[1] = L':';
    DriveRoot[2] = L'\\';
    DriveRoot[3] = UNICODE_NULL;
    Ok = GetVolumeNameForVolumeMountPointW(DriveRoot, VolumeName, RTL_NUMBER_OF(VolumeName));
    ok(Ok == TRUE, "GetVolumeNameForVolumeMountPointW(%S) failed with %lu\n", DriveRoot, GetLastError());
    if (!Ok)
        return;

    StringCchPrintfW(MountDir, RTL_NUMBER_OF(MountDir), L"%s%s", DriveRoot, MOUNT_LEAF);
    StringCchPrintfW(MountDirSlash, RTL_NUMBER_OF(MountDirSlash), L"%s\\", MountDir);
    StringCchPrintfW(JunctionDir, RTL_NUMBER_OF(JunctionDir), L"%s%s", DriveRoot, JUNCTION_LEAF);
    StringCchPrintfW(JunctionTarget, RTL_NUMBER_OF(JunctionTarget), L"\\??\\%s", WindowsDir);
    WideCharToMultiByte(CP_ACP, 0, VolumeName, -1, VolumeNameA, sizeof(VolumeNameA), NULL, NULL);

    DeleteVolumeMountPointW(MountDirSlash);
    RemoveDirectoryW(MountDir);
    RemoveDirectoryW(JunctionDir);

    TestParameters();

    Ok = CreateDirectoryW(MountDir, NULL);
    ok(Ok == TRUE, "CreateDirectoryW(%S) failed with %lu\n", MountDir, GetLastError());
    Ok = SetVolumeMountPointW(MountDirSlash, VolumeName);
    ok(Ok == TRUE, "SetVolumeMountPointW(%S, %S) failed with %lu\n", MountDirSlash, VolumeName, GetLastError());
    if (!Ok)
    {
        RemoveDirectoryW(MountDir);
        return;
    }

    Ok = CreateJunction(JunctionDir, JunctionTarget);
    ok(Ok == TRUE, "Creating the junction %S failed with %lu\n", JunctionDir, GetLastError());

    Scan = ScanW(VolumeName);
    ok(Scan.Opened, "FindFirstVolumeMountPointW(%S) failed with %lu\n", VolumeName, Scan.FirstError);
    ok(Scan.MountHits == 1, "Mounted folder found %lu times in %lu entries\n", Scan.MountHits, Scan.Entries);
    ok(Scan.JunctionHits == 0, "Directory junction reported as a mounted folder %lu times\n", Scan.JunctionHits);
    ok(Scan.AllRelative, "A returned name is not a volume-relative path with a trailing backslash\n");
    ok(Scan.EndError == ERROR_NO_MORE_FILES, "Enumeration ended with %lu\n", Scan.EndError);

    Scan = ScanW(DriveRoot);
    ok(Scan.Opened, "FindFirstVolumeMountPointW(%S) failed with %lu\n", DriveRoot, Scan.FirstError);
    ok(Scan.MountHits == 1, "Mounted folder found %lu times through the drive root\n", Scan.MountHits);
    ok(Scan.JunctionHits == 0, "Directory junction reported through the drive root\n");

    Scan = ScanA(VolumeNameA);
    ok(Scan.Opened, "FindFirstVolumeMountPointA(%s) failed with %lu\n", VolumeNameA, Scan.FirstError);
    ok(Scan.MountHits == 1, "Mounted folder found %lu times through the ANSI functions\n", Scan.MountHits);
    ok(Scan.JunctionHits == 0, "Directory junction reported through the ANSI functions\n");
    ok(Scan.AllRelative, "An ANSI name is not a volume-relative path with a trailing backslash\n");
    ok(Scan.EndError == ERROR_NO_MORE_FILES, "ANSI enumeration ended with %lu\n", Scan.EndError);

    TestSmallBuffer();

    Ok = DeleteVolumeMountPointW(MountDirSlash);
    ok(Ok == TRUE, "DeleteVolumeMountPointW(%S) failed with %lu\n", MountDirSlash, GetLastError());

    Scan = ScanW(VolumeName);
    ok(Scan.Opened || Scan.FirstError == ERROR_NO_MORE_FILES,
       "FindFirstVolumeMountPointW after the delete failed with %lu\n", Scan.FirstError);
    ok(Scan.MountHits == 0, "Deleted mounted folder still found %lu times\n", Scan.MountHits);
    ok(Scan.JunctionHits == 0, "Directory junction reported after the delete\n");

    RemoveDirectoryW(MountDir);
    RemoveDirectoryW(JunctionDir);
}
