/*
 * PROJECT:     LiberNT Setup API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AddProperty directive of a class install section
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "setupapi_private.h"

#include <devpropdef.h>

#define FLG_ADDPROPERTY_NOCLOBBER     0x00000001
#define FLG_ADDPROPERTY_OVERWRITEONLY 0x00000002
#define FLG_ADDPROPERTY_APPEND        0x00000004
#define FLG_ADDPROPERTY_OR            0x00000008
#define FLG_ADDPROPERTY_AND           0x00000010

static
BOOL
SETUP_ReadPropertyValue(
    _In_ PINFCONTEXT Line,
    _In_ DEVPROPTYPE Type,
    _Outptr_result_bytebuffer_(*Size) PBYTE *Data,
    _Out_ PDWORD Size)
{
    DWORD Length = 0;
    INT Value;

    *Data = NULL;
    *Size = 0;
    switch (Type)
    {
        case DEVPROP_TYPE_BOOLEAN:
            if (!SetupGetIntField(Line, 5, &Value))
                return FALSE;
            *Data = HeapAlloc(GetProcessHeap(), 0, sizeof(DEVPROP_BOOLEAN));
            if (!*Data)
                return FALSE;
            **Data = Value ? DEVPROP_TRUE : DEVPROP_FALSE;
            *Size = sizeof(DEVPROP_BOOLEAN);
            return TRUE;

        case DEVPROP_TYPE_UINT32:
            if (!SetupGetIntField(Line, 5, &Value))
                return FALSE;
            *Data = HeapAlloc(GetProcessHeap(), 0, sizeof(ULONG));
            if (!*Data)
                return FALSE;
            *(PULONG)*Data = (ULONG)Value;
            *Size = sizeof(ULONG);
            return TRUE;

        case DEVPROP_TYPE_STRING:
            if (!SetupGetStringFieldW(Line, 5, NULL, 0, &Length) || !Length)
                return FALSE;
            *Data = HeapAlloc(GetProcessHeap(), 0, Length * sizeof(WCHAR));
            if (!*Data || !SetupGetStringFieldW(Line, 5, (PWSTR)*Data, Length, NULL))
                break;
            *Size = Length * sizeof(WCHAR);
            return TRUE;

        case DEVPROP_TYPE_STRING_LIST:
            if (!SetupGetMultiSzFieldW(Line, 5, NULL, 0, &Length) || !Length)
                return FALSE;
            *Data = HeapAlloc(GetProcessHeap(), 0, Length * sizeof(WCHAR));
            if (!*Data || !SetupGetMultiSzFieldW(Line, 5, (PWSTR)*Data, Length, NULL))
                break;
            *Size = Length * sizeof(WCHAR);
            return TRUE;

        case DEVPROP_TYPE_BINARY:
            if (!SetupGetBinaryField(Line, 5, NULL, 0, &Length))
                return FALSE;
            *Data = HeapAlloc(GetProcessHeap(), 0, Length ? Length : 1);
            if (!*Data || (Length && !SetupGetBinaryField(Line, 5, *Data, Length, NULL)))
                break;
            *Size = Length;
            return TRUE;

        default:
            WARN("AddProperty type 0x%lx is not supported\n", Type);
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
    }

    HeapFree(GetProcessHeap(), 0, *Data);
    *Data = NULL;
    return FALSE;
}

static
BOOL
SETUP_MergePropertyValue(
    _In_ HKEY Property,
    _In_ DEVPROPTYPE Type,
    _In_ DWORD Flags,
    _Inout_ PBYTE *Data,
    _Inout_ PDWORD Size)
{
    DWORD OldType, OldSize = 0;
    PBYTE Old, Merged;
    PCWSTR String;
    SIZE_T Used;

    if (RegQueryValueExW(Property, NULL, NULL, &OldType, NULL, &OldSize) || OldType != (0xffff0000 | Type))
        return TRUE;
    Old = HeapAlloc(GetProcessHeap(), 0, OldSize + sizeof(WCHAR) * 2);
    if (!Old)
        return FALSE;
    if (RegQueryValueExW(Property, NULL, NULL, NULL, Old, &OldSize))
    {
        HeapFree(GetProcessHeap(), 0, Old);
        return TRUE;
    }

    if (Type == DEVPROP_TYPE_UINT32 && OldSize == sizeof(ULONG) && (Flags & (FLG_ADDPROPERTY_OR | FLG_ADDPROPERTY_AND)))
    {
        if (Flags & FLG_ADDPROPERTY_OR)
            *(PULONG)*Data |= *(PULONG)Old;
        else
            *(PULONG)*Data &= *(PULONG)Old;
    }
    else if (Type == DEVPROP_TYPE_STRING_LIST && (Flags & FLG_ADDPROPERTY_APPEND))
    {
        Merged = HeapAlloc(GetProcessHeap(), 0, OldSize + *Size + sizeof(WCHAR));
        if (!Merged)
        {
            HeapFree(GetProcessHeap(), 0, Old);
            return FALSE;
        }
        for (Used = 0; Used + sizeof(WCHAR) <= OldSize && *(PCWSTR)(Old + Used); Used += (wcslen((PCWSTR)(Old + Used)) + 1) * sizeof(WCHAR))
            ;
        memcpy(Merged, Old, Used);
        for (String = (PCWSTR)*Data; *String; String += wcslen(String) + 1)
        {
            PCWSTR Existing;
            BOOL Present = FALSE;

            for (Existing = (PCWSTR)Merged; (PBYTE)Existing < Merged + Used && *Existing; Existing += wcslen(Existing) + 1)
            {
                if (!_wcsicmp(Existing, String))
                {
                    Present = TRUE;
                    break;
                }
            }
            if (!Present)
            {
                memcpy(Merged + Used, String, (wcslen(String) + 1) * sizeof(WCHAR));
                Used += (wcslen(String) + 1) * sizeof(WCHAR);
            }
        }
        *(PWSTR)(Merged + Used) = UNICODE_NULL;
        HeapFree(GetProcessHeap(), 0, *Data);
        *Data = Merged;
        *Size = (DWORD)(Used + sizeof(WCHAR));
    }

    HeapFree(GetProcessHeap(), 0, Old);
    return TRUE;
}

static
BOOL
SETUP_AddClassProperty(
    _In_ HKEY ClassKey,
    _In_ PINFCONTEXT Line)
{
    WCHAR Field[MAX_GUID_STRING_LEN + 1], Path[MAX_GUID_STRING_LEN + 8];
    HKEY Properties, Property;
    DEVPROPTYPE Type;
    DWORD Flags = 0, Size, Disposition;
    PBYTE Data;
    INT Pid, IntType, IntFlags;
    LSTATUS Error;
    GUID Guid;

    if (!SetupGetStringFieldW(Line, 1, Field, ARRAY_SIZE(Field), NULL) || Field[0] != L'{')
    {
        WARN("AddProperty by name is not supported for a setup class\n");
        return TRUE;
    }
    if (Field[MAX_GUID_STRING_LEN - 2] != L'}')
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    Field[MAX_GUID_STRING_LEN - 2] = UNICODE_NULL;
    if (UuidFromStringW(Field + 1, &Guid) != RPC_S_OK)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (!SetupGetIntField(Line, 2, &Pid) || Pid < 2 || !SetupGetIntField(Line, 3, &IntType))
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (SetupGetIntField(Line, 4, &IntFlags))
        Flags = (DWORD)IntFlags;
    Type = (DEVPROPTYPE)IntType;

    if (!SETUP_ReadPropertyValue(Line, Type, &Data, &Size))
        return FALSE;

    swprintf(Path, ARRAY_SIZE(Path), L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\\%04X",
             Guid.Data1, Guid.Data2, Guid.Data3, Guid.Data4[0], Guid.Data4[1], Guid.Data4[2],
             Guid.Data4[3], Guid.Data4[4], Guid.Data4[5], Guid.Data4[6], Guid.Data4[7], Pid);

    Error = RegCreateKeyExW(ClassKey, L"Properties", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &Properties, NULL);
    if (!Error)
    {
        BOOL Existed = FALSE;

        Error = RegOpenKeyExW(Properties, Path, 0, KEY_READ | KEY_WRITE, &Property);
        if (!Error)
            Existed = !RegQueryValueExW(Property, NULL, NULL, NULL, NULL, NULL);
        else if (Error == ERROR_FILE_NOT_FOUND && !(Flags & FLG_ADDPROPERTY_OVERWRITEONLY))
            Error = RegCreateKeyExW(Properties, Path, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &Property, &Disposition);
        else if (Error == ERROR_FILE_NOT_FOUND)
        {
            Error = ERROR_SUCCESS;
            Property = NULL;
        }

        if (!Error && Property)
        {
            if (Existed && (Flags & FLG_ADDPROPERTY_NOCLOBBER))
                Error = ERROR_SUCCESS;
            else if (!Existed && (Flags & FLG_ADDPROPERTY_OVERWRITEONLY))
                Error = ERROR_SUCCESS;
            else if (Existed && !SETUP_MergePropertyValue(Property, Type, Flags, &Data, &Size))
                Error = ERROR_NOT_ENOUGH_MEMORY;
            else
                Error = RegSetValueExW(Property, NULL, 0, 0xffff0000 | (0xffff & Type), Data, Size);
            RegCloseKey(Property);
        }
        RegCloseKey(Properties);
    }

    HeapFree(GetProcessHeap(), 0, Data);
    if (Error)
    {
        SetLastError(Error);
        return FALSE;
    }
    return TRUE;
}

BOOL
SETUP_InstallClassProperties(
    _In_ HINF InfHandle,
    _In_ PCWSTR SectionName)
{
    INFCONTEXT Directive, Line;
    WCHAR Section[MAX_PATH], GuidString[MAX_GUID_STRING_LEN + 1];
    HKEY ClassKey;
    DWORD Index;
    BOOL Result = TRUE;
    GUID ClassGuid;

    if (!SetupFindFirstLineW(InfHandle, SectionName, L"AddProperty", &Directive))
        return TRUE;

    if (!SetupFindFirstLineW(InfHandle, L"Version", L"ClassGuid", &Line) ||
        !SetupGetStringFieldW(&Line, 1, GuidString, ARRAY_SIZE(GuidString), NULL) ||
        GuidString[0] != L'{' || GuidString[MAX_GUID_STRING_LEN - 2] != L'}')
    {
        SetLastError(ERROR_INVALID_CLASS);
        return FALSE;
    }
    GuidString[MAX_GUID_STRING_LEN - 2] = UNICODE_NULL;
    if (UuidFromStringW(GuidString + 1, &ClassGuid) != RPC_S_OK)
    {
        SetLastError(ERROR_INVALID_CLASS);
        return FALSE;
    }

    ClassKey = SetupDiOpenClassRegKeyExW(&ClassGuid, KEY_READ | KEY_WRITE, DIOCR_INSTALLER, NULL, NULL);
    if (ClassKey == INVALID_HANDLE_VALUE)
        return FALSE;

    do
    {
        for (Index = 1; Result && SetupGetStringFieldW(&Directive, Index, Section, ARRAY_SIZE(Section), NULL); Index++)
        {
            if (!SetupFindFirstLineW(InfHandle, Section, NULL, &Line))
                continue;
            do
            {
                Result = SETUP_AddClassProperty(ClassKey, &Line);
            } while (Result && SetupFindNextLine(&Line, &Line));
        }
    } while (Result && SetupFindNextMatchLineW(&Directive, L"AddProperty", &Directive));

    RegCloseKey(ClassKey);
    return Result;
}
