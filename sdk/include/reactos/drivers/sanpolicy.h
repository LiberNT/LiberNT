/*
 * PROJECT:     LiberNT Storage Drivers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SANPOLICY=OFFLINEALL boot option shared by the storage drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

static inline
BOOLEAN
SanPolicyIsOfflineAll(
    _In_ ULONG PoolTag)
{
    UNICODE_STRING keyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control");
    UNICODE_STRING valueName = RTL_CONSTANT_STRING(L"SystemStartOptions");
    static const WCHAR option[] = L"SANPOLICY=OFFLINEALL";
    const SIZE_T optionLength = RTL_NUMBER_OF(option) - 1;
    PKEY_VALUE_PARTIAL_INFORMATION value;
    OBJECT_ATTRIBUTES attributes;
    BOOLEAN found = FALSE;
    ULONG length;
    PCWSTR text;
    HANDLE key;

    PAGED_CODE();

    InitializeObjectAttributes(&attributes, &keyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&key, KEY_QUERY_VALUE, &attributes)))
        return FALSE;

    length = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 1024 * sizeof(WCHAR);
    value = ExAllocatePoolZero(PagedPool, length + sizeof(WCHAR), PoolTag);
    if (value &&
        NT_SUCCESS(ZwQueryValueKey(key, &valueName, KeyValuePartialInformation, value, length, &length)) &&
        value->Type == REG_SZ)
    {
        for (text = (PCWSTR)value->Data; *text && !found; text++)
        {
            found = (text == (PCWSTR)value->Data || text[-1] == L' ') &&
                    _wcsnicmp(text, option, optionLength) == 0 &&
                    (text[optionLength] == UNICODE_NULL || text[optionLength] == L' ');
        }
    }

    if (value)
        ExFreePoolWithTag(value, PoolTag);
    ZwClose(key);
    return found;
}
