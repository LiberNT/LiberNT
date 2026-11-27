/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Write an RCDATA resource of the test executable to a file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winuser.h>

BOOL
extract_resource(
    _In_ const WCHAR *Filename,
    _In_ LPCWSTR ResourceName)
{
    HMODULE Module = GetModuleHandleW(NULL);
    HRSRC Resource;
    HANDLE File;
    PVOID Data;
    DWORD Size;
    DWORD Written = 0;
    BOOL Success;

    Resource = FindResourceW(Module, ResourceName, (LPCWSTR)RT_RCDATA);
    ok(Resource != NULL, "Resource %s not found\n", wine_dbgstr_w(ResourceName));
    if (Resource == NULL)
        return FALSE;

    Size = SizeofResource(Module, Resource);
    Data = LockResource(LoadResource(Module, Resource));
    ok(Size != 0 && Data != NULL, "Resource %s could not be loaded\n", wine_dbgstr_w(ResourceName));
    if (Size == 0 || Data == NULL)
        return FALSE;

    File = CreateFileW(Filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        skip("Cannot create %ls: %lu\n", Filename, GetLastError());
        return FALSE;
    }

    Success = WriteFile(File, Data, Size, &Written, NULL);
    ok(Success, "WriteFile failed: %lu\n", GetLastError());
    ok(Written == Size, "Wrote %lu of %lu bytes\n", Written, Size);
    CloseHandle(File);
    return Success && Written == Size;
}
