/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WOW64 system directory query by machine type
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>

static const WCHAR SysArm32Suffix[] = L"\\SysArm32";

static
UINT
GetSysArm32DirectoryW(
    _Out_writes_to_opt_(uSize, return + 1) LPWSTR lpBuffer,
    _In_ UINT uSize)
{
    WCHAR Path[MAX_PATH];
    UINT Length;

    Length = GetSystemWindowsDirectoryW(Path, _countof(Path));
    if (!Length || Length + _countof(SysArm32Suffix) > _countof(Path))
        return 0;

    RtlCopyMemory(Path + Length, SysArm32Suffix, sizeof(SysArm32Suffix));
    Length += _countof(SysArm32Suffix) - 1;
    if (!lpBuffer || uSize <= Length)
        return Length + 1;

    RtlCopyMemory(lpBuffer, Path, (Length + 1) * sizeof(WCHAR));
    return Length;
}

static
UINT
GetSysArm32DirectoryA(
    _Out_writes_to_opt_(uSize, return + 1) LPSTR lpBuffer,
    _In_ UINT uSize)
{
    WCHAR Path[MAX_PATH];
    UINT CodePage = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    UINT Length;
    INT Needed;

    Length = GetSysArm32DirectoryW(Path, _countof(Path));
    if (!Length || Length >= _countof(Path))
        return 0;

    Needed = WideCharToMultiByte(CodePage, 0, Path, -1, NULL, 0, NULL, NULL);
    if (Needed <= 0)
        return 0;
    if (!lpBuffer || uSize < (UINT)Needed)
        return (UINT)Needed;

    return (UINT)WideCharToMultiByte(CodePage, 0, Path, -1, lpBuffer, uSize, NULL, NULL) - 1;
}

UINT
WINAPI
GetSystemWow64Directory2W(
    _Out_writes_to_opt_(uSize, return + 1) LPWSTR lpBuffer,
    _In_ UINT uSize,
    _In_ WORD ImageFileMachineType)
{
    switch (ImageFileMachineType)
    {
        case IMAGE_FILE_MACHINE_TARGET_HOST:
            return GetSystemDirectoryW(lpBuffer, uSize);

        case IMAGE_FILE_MACHINE_I386:
            return GetSystemWow64DirectoryW(lpBuffer, uSize);

        case IMAGE_FILE_MACHINE_ARMNT:
            return GetSysArm32DirectoryW(lpBuffer, uSize);
    }

    SetLastError(ERROR_BAD_ARGUMENTS);
    return 0;
}

UINT
WINAPI
GetSystemWow64Directory2A(
    _Out_writes_to_opt_(uSize, return + 1) LPSTR lpBuffer,
    _In_ UINT uSize,
    _In_ WORD ImageFileMachineType)
{
    switch (ImageFileMachineType)
    {
        case IMAGE_FILE_MACHINE_TARGET_HOST:
            return GetSystemDirectoryA(lpBuffer, uSize);

        case IMAGE_FILE_MACHINE_I386:
            return GetSystemWow64DirectoryA(lpBuffer, uSize);

        case IMAGE_FILE_MACHINE_ARMNT:
            return GetSysArm32DirectoryA(lpBuffer, uSize);
    }

    SetLastError(ERROR_BAD_ARGUMENTS);
    return 0;
}
