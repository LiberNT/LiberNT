/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A handle to a device itself reads and writes the device's security, whatever its driver dispatches
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <aclapi.h>

static
PSECURITY_DESCRIPTOR
QueryDescriptor(
    _In_ HANDLE Handle,
    _In_ SECURITY_INFORMATION Information,
    _Out_ PULONG Length)
{
    PSECURITY_DESCRIPTOR Descriptor;
    NTSTATUS Status;

    *Length = 0;
    Status = NtQuerySecurityObject(Handle, Information, NULL, 0, Length);
    ok_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok(*Length >= sizeof(SECURITY_DESCRIPTOR_RELATIVE), "Descriptor length %lu\n", *Length);
    if (Status != STATUS_BUFFER_TOO_SMALL || *Length == 0)
        return NULL;

    Descriptor = RtlAllocateHeap(RtlGetProcessHeap(), 0, *Length);
    if (!Descriptor)
        return NULL;

    Status = NtQuerySecurityObject(Handle, Information, Descriptor, *Length, Length);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Descriptor);
        return NULL;
    }

    ok(RtlValidSecurityDescriptor(Descriptor), "The device's descriptor is not valid\n");
    return Descriptor;
}

static
VOID
TestNativeDeviceOpen(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Null");
    PSECURITY_DESCRIPTOR Descriptor, After;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    BOOLEAN Present = FALSE, Defaulted;
    ULONG Length, AfterLength;
    HANDLE Handle = NULL;
    PSID Owner = NULL;
    PACL Dacl = NULL;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, READ_CONTROL | WRITE_DAC | SYNCHRONIZE, &Attributes, &Iosb,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        skip("Could not open the null device\n");
        return;
    }

    Descriptor = QueryDescriptor(Handle,
                                 OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                 &Length);
    if (Descriptor)
    {
        Status = RtlGetOwnerSecurityDescriptor(Descriptor, &Owner, &Defaulted);
        ok_hex(Status, STATUS_SUCCESS);
        ok(Owner != NULL, "The device's descriptor has no owner\n");
        Status = RtlGetDaclSecurityDescriptor(Descriptor, &Present, &Dacl, &Defaulted);
        ok_hex(Status, STATUS_SUCCESS);
        ok(Present && Dacl != NULL, "The device's descriptor has no DACL\n");
        RtlFreeHeap(RtlGetProcessHeap(), 0, Descriptor);
    }

    Descriptor = QueryDescriptor(Handle, DACL_SECURITY_INFORMATION, &Length);
    if (Descriptor)
    {
        Status = NtSetSecurityObject(Handle, DACL_SECURITY_INFORMATION, Descriptor);
        ok_hex(Status, STATUS_SUCCESS);

        After = QueryDescriptor(Handle, DACL_SECURITY_INFORMATION, &AfterLength);
        if (After)
        {
            ok(AfterLength == Length, "DACL length %lu after writing back %lu\n", AfterLength, Length);
            if (AfterLength == Length)
                ok(RtlEqualMemory(After, Descriptor, Length), "The DACL changed when written back\n");
            RtlFreeHeap(RtlGetProcessHeap(), 0, After);
        }
        RtlFreeHeap(RtlGetProcessHeap(), 0, Descriptor);
    }

    NtClose(Handle);
}

static
VOID
TestWin32DeviceName(VOID)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    PACL Dacl = NULL;
    DWORD Error;

    Error = GetNamedSecurityInfoW(L"\\\\.\\NUL", SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  NULL, NULL, &Dacl, NULL, &Descriptor);
    ok(Error == ERROR_SUCCESS, "GetNamedSecurityInfoW returned %lu\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    ok(Dacl != NULL, "The device has no DACL\n");
    if (Dacl != NULL)
    {
        Error = SetNamedSecurityInfoW((PWSTR)L"\\\\.\\NUL", SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                      NULL, NULL, Dacl, NULL);
        ok(Error == ERROR_SUCCESS, "SetNamedSecurityInfoW returned %lu\n", Error);
    }
    LocalFree(Descriptor);
}

START_TEST(NtQuerySecurityObject)
{
    TestNativeDeviceOpen();
    TestWin32DeviceName();
}
