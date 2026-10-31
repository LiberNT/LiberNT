/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RtlCheckPortableOperatingSystem and RtlSetPortableOperatingSystem
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>

static UNICODE_STRING ControlKey = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control");
static UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"PortableOperatingSystem");

static HANDLE OpenControlKey(ACCESS_MASK Access)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Key = NULL;

    InitializeObjectAttributes(&ObjectAttributes, &ControlKey, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(NtOpenKey(&Key, Access, &ObjectAttributes)))
        return NULL;
    return Key;
}

static NTSTATUS ReadRawValue(PULONG Type, PULONG Data)
{
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 16];
    PKEY_VALUE_PARTIAL_INFORMATION Info = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    HANDLE Key = OpenControlKey(KEY_QUERY_VALUE);
    ULONG Length;
    NTSTATUS Status;

    if (Key == NULL)
        return STATUS_UNSUCCESSFUL;
    Status = NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Buffer, sizeof(Buffer), &Length);
    NtClose(Key);
    if (NT_SUCCESS(Status))
    {
        *Type = Info->Type;
        *Data = Info->DataLength >= sizeof(ULONG) ? *(PULONG)Info->Data : 0;
    }
    return Status;
}

static NTSTATUS WriteRawValue(ULONG Type, PVOID Data, ULONG Length)
{
    HANDLE Key = OpenControlKey(KEY_SET_VALUE);
    NTSTATUS Status;

    if (Key == NULL)
        return STATUS_ACCESS_DENIED;
    Status = NtSetValueKey(Key, &ValueName, 0, Type, Data, Length);
    NtClose(Key);
    return Status;
}

static VOID DeleteRawValue(VOID)
{
    HANDLE Key = OpenControlKey(KEY_SET_VALUE);

    if (Key != NULL)
    {
        NtDeleteValueKey(Key, &ValueName);
        NtClose(Key);
    }
}

START_TEST(RtlPortableOperatingSystem)
{
    BOOLEAN Portable;
    ULONG OriginalType = 0, OriginalData = 0, Type = 0, Data = 0;
    BOOLEAN HadValue;
    NTSTATUS Status;

    HadValue = NT_SUCCESS(ReadRawValue(&OriginalType, &OriginalData));
    if (!HadValue)
    {
        Portable = 0xCC;
        Status = RtlCheckPortableOperatingSystem(&Portable);
        ok_hex(Status, STATUS_NOT_FOUND);
        ok_int(Portable, 0xCC);
    }

    Status = RtlSetPortableOperatingSystem(TRUE);
    if (Status == STATUS_ACCESS_DENIED)
    {
        skip("Writing the Control key needs administrator rights\n");
        return;
    }
    ok_hex(Status, STATUS_SUCCESS);
    ok_hex(ReadRawValue(&Type, &Data), STATUS_SUCCESS);
    ok_long(Type, REG_DWORD);
    ok_long(Data, 1);
    Portable = 0xCC;
    ok_hex(RtlCheckPortableOperatingSystem(&Portable), STATUS_SUCCESS);
    ok_int(Portable, TRUE);

    ok_hex(RtlSetPortableOperatingSystem(FALSE), STATUS_SUCCESS);
    ok_hex(ReadRawValue(&Type, &Data), STATUS_SUCCESS);
    ok_long(Data, 0);
    Portable = 0xCC;
    ok_hex(RtlCheckPortableOperatingSystem(&Portable), STATUS_SUCCESS);
    ok_int(Portable, FALSE);

    ok_hex(RtlSetPortableOperatingSystem(2), STATUS_SUCCESS);
    ok_hex(ReadRawValue(&Type, &Data), STATUS_SUCCESS);
    ok_long(Data, 1);

    Data = 7;
    ok_hex(WriteRawValue(REG_SZ, L"1", sizeof(L"1")), STATUS_SUCCESS);
    Portable = 0xCC;
    ok_hex(RtlCheckPortableOperatingSystem(&Portable), STATUS_OBJECT_TYPE_MISMATCH);
    ok_int(Portable, 0xCC);

    ok_hex(WriteRawValue(REG_DWORD, &Data, sizeof(Data)), STATUS_SUCCESS);
    Portable = 0xCC;
    ok_hex(RtlCheckPortableOperatingSystem(&Portable), STATUS_SUCCESS);
    ok_int(Portable, TRUE);

    if (HadValue)
        WriteRawValue(OriginalType, &OriginalData, sizeof(OriginalData));
    else
        DeleteRawValue();
}

START_TEST(PortableWorkspaceHibernate)
{
    UNICODE_STRING PolicyKey = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Policies\\Microsoft\\PortableOperatingSystem");
    UNICODE_STRING HibernateName = RTL_CONSTANT_STRING(L"Hibernate");
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Info = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    SYSTEM_POWER_CAPABILITIES Capabilities;
    OBJECT_ATTRIBUTES ObjectAttributes;
    BOOLEAN Portable = FALSE;
    ULONG Allow = 0, Length;
    HANDLE Key;
    NTSTATUS Status;

    RtlZeroMemory(&Capabilities, sizeof(Capabilities));
    Status = NtPowerInformation(SystemPowerCapabilities, NULL, 0, &Capabilities, sizeof(Capabilities));
    ok_hex(Status, STATUS_SUCCESS);
    trace("SystemS4 %u HiberFilePresent %u\n", Capabilities.SystemS4, Capabilities.HiberFilePresent);

    Status = RtlCheckPortableOperatingSystem(&Portable);
    if (!NT_SUCCESS(Status) || !Portable)
    {
        skip("Not started from a portable workspace (0x%lx, %u)\n", Status, Portable);
        return;
    }

    InitializeObjectAttributes(&ObjectAttributes, &PolicyKey, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (NT_SUCCESS(NtOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes)))
    {
        if (NT_SUCCESS(NtQueryValueKey(Key, &HibernateName, KeyValuePartialInformation, Buffer, sizeof(Buffer), &Length)) &&
            Info->Type == REG_DWORD)
        {
            Allow = *(PULONG)Info->Data;
        }
        NtClose(Key);
    }
    if (Allow == 1)
    {
        skip("The portable workspace policy allows hibernation\n");
        return;
    }

    ok(!Capabilities.HiberFilePresent, "Hibernation is enabled on a portable workspace\n");
}
