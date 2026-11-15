/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that drivers started at boot already see the wall-clock time
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(SystemTimeAtBoot)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP");
    SYSTEM_TIMEOFDAY_INFORMATION TimeOfDay;
    OBJECT_ATTRIBUTES Attributes;
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 256 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Basic = (PKEY_BASIC_INFORMATION)Buffer;
    ULONG Index, Length, Checked = 0;
    HANDLE Key;
    NTSTATUS Status;

    RtlZeroMemory(&TimeOfDay, sizeof(TimeOfDay));
    Status = NtQuerySystemInformation(SystemTimeOfDayInformation, &TimeOfDay, sizeof(TimeOfDay), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(TimeOfDay.BootTime.QuadPart > 126227808000000000LL, "BootTime %I64d is before 2001\n",
       TimeOfDay.BootTime.QuadPart);

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&Key, KEY_ENUMERATE_SUB_KEYS, &Attributes);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    for (Index = 0;; Index++)
    {
        RtlZeroMemory(Buffer, sizeof(Buffer));
        Status = NtEnumerateKey(Key, Index, KeyBasicInformation, Basic, sizeof(Buffer) - sizeof(WCHAR), &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
            break;
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            break;
        ok(Basic->LastWriteTime.QuadPart >= TimeOfDay.BootTime.QuadPart,
           "DEVICEMAP\\%.*ls written at %I64d, before the boot time %I64d\n",
           (int)(Basic->NameLength / sizeof(WCHAR)), Basic->Name, Basic->LastWriteTime.QuadPart,
           TimeOfDay.BootTime.QuadPart);
        Checked++;
    }
    ok(Checked != 0, "DEVICEMAP has no subkey\n");
    NtClose(Key);
}
