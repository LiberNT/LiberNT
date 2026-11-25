/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     License values a display driver stack reads, against the APIs that report the same facts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

static
NTSTATUS
QueryDword(
    _In_ PCWSTR Name,
    _Out_ PULONG Value)
{
    UNICODE_STRING ValueName;
    ULONG Type = 0xA5A5A5A5, Result = 0;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    *Value = 0xA5A5A5A5;
    Status = NtQueryLicenseValue(&ValueName, &Type, Value, sizeof(*Value), &Result);
    if (NT_SUCCESS(Status))
    {
        ok_long(Type, 4);
        ok_long(Result, sizeof(ULONG));
    }
    return Status;
}

START_TEST(NtQueryLicenseValue)
{
    ULONG Value, Family = 0, Product = 0;
    NTSTATUS Status;

    RtlGetDeviceFamilyInfoEnum(NULL, &Family, NULL);
    Status = QueryDword(L"Kernel-OneCore-DeviceFamilyID", &Value);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(Value == Family, "device family license %lu, RtlGetDeviceFamilyInfoEnum %lu\n", Value, Family);

    ok(RtlGetProductInfo(10, 0, 0, 0, &Product), "RtlGetProductInfo failed\n");
    Status = QueryDword(L"Kernel-ProductInfo", &Value);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(Value == Product, "product license 0x%lx, RtlGetProductInfo 0x%lx\n", Value, Product);

    Status = QueryDword(L"Microsoft-Windows-Core-AllowMultiMon", &Value);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok_long(Value, 1);
}
