/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RtlCheckPortableOperatingSystem and EtwCheckCoverage
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>

typedef NTSTATUS (NTAPI *PFN_RTLCHECKPORTABLEOPERATINGSYSTEM)(PBOOLEAN);
typedef BOOLEAN (NTAPI *PFN_ETWCHECKCOVERAGE)(PVOID);

START_TEST(RtlCheckPortableOperatingSystem)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PFN_RTLCHECKPORTABLEOPERATINGSYSTEM pRtlCheckPortableOperatingSystem;
    PFN_ETWCHECKCOVERAGE pEtwCheckCoverage;
    UCHAR Point[64];
    BOOLEAN IsPortable;
    NTSTATUS Status;
    BOOLEAN Result;
    ULONG Index;

    pRtlCheckPortableOperatingSystem = (PFN_RTLCHECKPORTABLEOPERATINGSYSTEM)GetProcAddress(Ntdll, "RtlCheckPortableOperatingSystem");
    pEtwCheckCoverage = (PFN_ETWCHECKCOVERAGE)GetProcAddress(Ntdll, "EtwCheckCoverage");
    ok(pRtlCheckPortableOperatingSystem != NULL, "RtlCheckPortableOperatingSystem is missing\n");
    ok(pEtwCheckCoverage != NULL, "EtwCheckCoverage is missing\n");
    ok(GetProcAddress(Ntdll, "RtlUnsubscribeWnfStateChangeNotification") != NULL,
       "RtlUnsubscribeWnfStateChangeNotification is missing\n");

    if (pRtlCheckPortableOperatingSystem)
    {
        UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control");
        UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"PortableOperatingSystem");
        OBJECT_ATTRIBUTES ObjectAttributes;
        HANDLE Key;
        ULONG Value;

        IsPortable = 0xcc;
        Status = pRtlCheckPortableOperatingSystem(&IsPortable);
        ok(Status == STATUS_NOT_FOUND, "status 0x%08lx\n", Status);
        ok(IsPortable == 0xcc, "output 0x%02x\n", IsPortable);

        InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
        Status = NtOpenKey(&Key, KEY_SET_VALUE | KEY_QUERY_VALUE, &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            skip("Control key not writable (0x%08lx)\n", Status);
        }
        else
        {
            Value = 1;
            NtSetValueKey(Key, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
            IsPortable = 0xcc;
            Status = pRtlCheckPortableOperatingSystem(&IsPortable);
            ok(Status == STATUS_SUCCESS && IsPortable == TRUE, "value 1: 0x%08lx 0x%02x\n", Status, IsPortable);

            Value = 0;
            NtSetValueKey(Key, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
            IsPortable = 0xcc;
            Status = pRtlCheckPortableOperatingSystem(&IsPortable);
            ok(Status == STATUS_SUCCESS && IsPortable == FALSE, "value 0: 0x%08lx 0x%02x\n", Status, IsPortable);

            NtSetValueKey(Key, &ValueName, 0, REG_SZ, L"1", 2 * sizeof(WCHAR));
            IsPortable = 0xcc;
            Status = pRtlCheckPortableOperatingSystem(&IsPortable);
            ok(Status == STATUS_OBJECT_TYPE_MISMATCH && IsPortable == 0xcc, "string value: 0x%08lx 0x%02x\n", Status, IsPortable);

            NtDeleteValueKey(Key, &ValueName);
            IsPortable = 0xcc;
            Status = pRtlCheckPortableOperatingSystem(&IsPortable);
            ok(Status == STATUS_NOT_FOUND && IsPortable == 0xcc, "deleted value: 0x%08lx 0x%02x\n", Status, IsPortable);
            NtClose(Key);
        }
    }

    if (pEtwCheckCoverage)
    {
        memset(Point, 0, sizeof(Point));
        Result = pEtwCheckCoverage(Point);
        ok(Result == FALSE, "coverage %d\n", Result);
        for (Index = 0; Index < sizeof(Point); Index++)
        {
            if (Point[Index])
                break;
        }
        ok(Index == sizeof(Point), "coverage point changed at %lu\n", Index);
    }
}
