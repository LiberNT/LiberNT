/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for kernel32!BaseSetLastNTError
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef ULONG (WINAPI *FN_BaseSetLastNTError)(NTSTATUS);

START_TEST(BaseSetLastNTError)
{
    static const struct
    {
        NTSTATUS Status;
        ULONG Error;
    } Tests[] =
    {
        { STATUS_SUCCESS, ERROR_SUCCESS },
        { STATUS_ACCESS_DENIED, ERROR_ACCESS_DENIED },
        { STATUS_OBJECT_NAME_NOT_FOUND, ERROR_FILE_NOT_FOUND },
        { STATUS_BUFFER_OVERFLOW, ERROR_MORE_DATA },
        { STATUS_PENDING, ERROR_IO_PENDING },
        { STATUS_INVALID_HANDLE, ERROR_INVALID_HANDLE },
        { (NTSTATUS)0xE0001234, 0xE0001234 },
    };
    FN_BaseSetLastNTError pBaseSetLastNTError;
    HMODULE hKernelBase;
    ULONG Result;
    UINT i;

    pBaseSetLastNTError = (FN_BaseSetLastNTError)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "BaseSetLastNTError");
    ok(pBaseSetLastNTError != NULL, "kernel32!BaseSetLastNTError is missing\n");

    hKernelBase = GetModuleHandleW(L"kernelbase.dll");
    if (hKernelBase)
        ok(GetProcAddress(hKernelBase, "BaseSetLastNTError") == NULL, "kernelbase must not export BaseSetLastNTError\n");

    if (!pBaseSetLastNTError)
        return;

    for (i = 0; i < ARRAYSIZE(Tests); i++)
    {
        SetLastError(1234);
        Result = pBaseSetLastNTError(Tests[i].Status);
        ok(Result == Tests[i].Error, "0x%08lx: returned %lu, expected %lu\n", Tests[i].Status, Result, Tests[i].Error);
        ok(GetLastError() == Tests[i].Error, "0x%08lx: last error %lu, expected %lu\n", Tests[i].Status, GetLastError(), Tests[i].Error);
    }
}
