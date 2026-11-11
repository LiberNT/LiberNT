/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Secure memory cache callbacks on memory secured by a driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#include "kernel32_test.h"

#define SECURED_SIZE 0x10000

static PVOID SecuredBase;
static HANDLE SecuredHandle;
static LONG CallbackCalls;
static PVOID CallbackAddress;

static
HANDLE
SecureRange(
    _In_ PVOID Address,
    _In_ SIZE_T Size,
    _In_ ULONG ProbeMode)
{
    KMT_SECURE_MEMORY Request;
    DWORD Length = sizeof(Request);
    DWORD Error;

    Request.Address = Address;
    Request.Size = Size;
    Request.ProbeMode = ProbeMode;
    Request.SecureHandle = NULL;
    Error = KmtSendBufferToDriver(IOCTL_SECURE_MEMORY, &Request, sizeof(Request), &Length);
    ok(Error == ERROR_SUCCESS, "IOCTL_SECURE_MEMORY failed with %lu\n", Error);
    return (Error == ERROR_SUCCESS) ? Request.SecureHandle : NULL;
}

static
VOID
UnsecureRange(
    _In_ HANDLE SecureHandle)
{
    KMT_SECURE_MEMORY Request;
    DWORD Length = 0;
    DWORD Error;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.SecureHandle = SecureHandle;
    Error = KmtSendBufferToDriver(IOCTL_UNSECURE_MEMORY, &Request, sizeof(Request), &Length);
    ok(Error == ERROR_SUCCESS, "IOCTL_UNSECURE_MEMORY failed with %lu\n", Error);
}

static
BOOLEAN
NTAPI
UnsecureCallback(
    _In_ PVOID Addr,
    _In_ SIZE_T Range)
{
    UNREFERENCED_PARAMETER(Range);

    CallbackCalls++;
    CallbackAddress = Addr;
    if (SecuredHandle == NULL || Addr != SecuredBase)
        return FALSE;

    UnsecureRange(SecuredHandle);
    SecuredHandle = NULL;
    return TRUE;
}

START_TEST(SecureMemoryCache)
{
    DWORD OldProtect;
    DWORD Error;
    BOOL Result;

    Error = KmtLoadAndOpenDriver(L"kernel32", FALSE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
        return;

    SecuredBase = VirtualAlloc(NULL, SECURED_SIZE, MEM_COMMIT, PAGE_READWRITE);
    ok(SecuredBase != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (SecuredBase != NULL)
    {
        SecuredHandle = SecureRange(SecuredBase, SECURED_SIZE, PAGE_READWRITE);
        if (SecuredHandle != NULL)
        {
            Result = VirtualProtect(SecuredBase, SECURED_SIZE, PAGE_NOACCESS, &OldProtect);
            ok(Result == FALSE, "VirtualProtect on secured memory succeeded without a callback\n");
            Result = VirtualFree(SecuredBase, 0, MEM_RELEASE);
            ok(Result == FALSE, "VirtualFree on secured memory succeeded without a callback\n");

            Result = AddSecureMemoryCacheCallback(UnsecureCallback);
            ok_eq_int(Result, TRUE);

            Result = VirtualProtect(SecuredBase, SECURED_SIZE, PAGE_NOACCESS, &OldProtect);
            ok(Result == TRUE, "VirtualProtect failed with %lu\n", GetLastError());
            ok_eq_long(CallbackCalls, 1);
            ok_eq_pointer(CallbackAddress, SecuredBase);
            ok_eq_pointer(SecuredHandle, NULL);
            Result = VirtualProtect(SecuredBase, SECURED_SIZE, PAGE_READWRITE, &OldProtect);
            ok_eq_int(Result, TRUE);

            SecuredHandle = SecureRange(SecuredBase, SECURED_SIZE, PAGE_READWRITE);
            Result = VirtualFree(SecuredBase, 0, MEM_RELEASE);
            ok(Result == TRUE, "VirtualFree failed with %lu\n", GetLastError());
            ok_eq_long(CallbackCalls, 2);
            ok_eq_pointer(SecuredHandle, NULL);
            if (Result)
                SecuredBase = NULL;

            Result = RemoveSecureMemoryCacheCallback(UnsecureCallback);
            ok_eq_int(Result, TRUE);
            if (SecuredHandle != NULL)
                UnsecureRange(SecuredHandle);
        }
        if (SecuredBase != NULL)
            VirtualFree(SecuredBase, 0, MEM_RELEASE);
    }

    KmtCloseDriver();
    KmtUnloadDriver();
}
