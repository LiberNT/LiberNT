/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the DPC overrides under Session Manager\Kernel are what the system runs with
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>

static const struct
{
    PCWSTR Name;
    ULONG Offset;
} DpcValues[] =
{
    { L"DpcQueueDepth", FIELD_OFFSET(SYSTEM_DPC_BEHAVIOR_INFORMATION, DpcQueueDepth) },
    { L"MinimumDpcRate", FIELD_OFFSET(SYSTEM_DPC_BEHAVIOR_INFORMATION, MinimumDpcRate) },
    { L"AdjustDpcThreshold", FIELD_OFFSET(SYSTEM_DPC_BEHAVIOR_INFORMATION, AdjustDpcThreshold) },
    { L"IdealDpcRate", FIELD_OFFSET(SYSTEM_DPC_BEHAVIOR_INFORMATION, IdealDpcRate) },
};

START_TEST(DpcBehaviorRegistry)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Session Manager\\Kernel");
    SYSTEM_DPC_BEHAVIOR_INFORMATION Behavior;
    SYSTEM_TIMEOFDAY_INFORMATION TimeOfDay;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    KEY_BASIC_INFORMATION KeyInfo;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ValueName;
    ULONG Length, Index, Checked = 0;
    NTSTATUS Status;
    HANDLE Key;

    InitializeObjectAttributes(&Attributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&Key, KEY_READ, &Attributes);
    if (!NT_SUCCESS(Status))
    {
        skip("Session Manager\\Kernel cannot be opened (0x%lx)\n", Status);
        return;
    }

    Status = NtQueryKey(Key, KeyBasicInformation, &KeyInfo, sizeof(KeyInfo), &Length);
    ok(Status == STATUS_SUCCESS || Status == STATUS_BUFFER_OVERFLOW, "NtQueryKey returned 0x%lx\n", Status);

    Status = NtQuerySystemInformation(SystemTimeOfDayInformation, &TimeOfDay, sizeof(TimeOfDay), NULL);
    ok_hex(Status, STATUS_SUCCESS);

    /* What the kernel holds was read at boot, so a key written since says nothing about it */
    if (KeyInfo.LastWriteTime.QuadPart >= TimeOfDay.BootTime.QuadPart)
    {
        skip("The key was written after this boot\n");
        NtClose(Key);
        return;
    }

    Status = NtQuerySystemInformation(SystemDpcBehaviorInformation, &Behavior, sizeof(Behavior), &Length);
    ok_hex(Status, STATUS_SUCCESS);

    for (Index = 0; Index < ARRAYSIZE(DpcValues) && NT_SUCCESS(Status); Index++)
    {
        RtlInitUnicodeString(&ValueName, DpcValues[Index].Name);
        if (!NT_SUCCESS(NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Buffer, sizeof(Buffer), &Length)))
            continue;
        if (Value->Type != REG_DWORD || Value->DataLength != sizeof(ULONG)) continue;

        ok(*(PULONG)((PUCHAR)&Behavior + DpcValues[Index].Offset) == *(PULONG)Value->Data,
           "%ls is %lu in the registry but the system runs with %lu\n", DpcValues[Index].Name,
           *(PULONG)Value->Data, *(PULONG)((PUCHAR)&Behavior + DpcValues[Index].Offset));
        Checked++;
    }

    NtClose(Key);

    if (Checked == 0) skip("No DPC override in the registry\n");
}
