/*
 *  ReactOS kernel
 *  Copyright (C) 2008 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS text-mode setup
 * FILE:            base/setup/usetup/mui.c
 * PURPOSE:         Text-mode setup
 * PROGRAMMER:
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"
#include "mui.h"
#include "muilanguages.h"
#include "registry.h"

#define NDEBUG
#include <debug.h>


/* FUNCTIONS ****************************************************************/

static
ULONG
FindLanguageIndex(
    IN PCWSTR LanguageId)
{
    ULONG lngIndex = 0;

    if (LanguageId == NULL)
    {
        /* Default to en-US */
        // return 0;   // FIXME!!
        LanguageId = L"00000409";
    }

    while (MUILanguageList[lngIndex].LanguageID != NULL)
    {
        if (_wcsicmp(MUILanguageList[lngIndex].LanguageID, LanguageId) == 0)
        {
            return lngIndex;
        }

        lngIndex++;
    }

    return 0;
}

BOOLEAN
IsLanguageAvailable(
    IN PCWSTR LanguageId)
{
    ULONG lngIndex = 0;

    while (MUILanguageList[lngIndex].LanguageID != NULL)
    {
        if (_wcsicmp(MUILanguageList[lngIndex].LanguageID, LanguageId) == 0)
            return TRUE;

        lngIndex++;
    }

    return FALSE;
}


KLID
MUIDefaultKeyboardLayout(
    IN PCWSTR LanguageId)
{
    ULONG lngIndex = FindLanguageIndex(LanguageId);
    return MUILanguageList[lngIndex].MuiLayouts[0].LayoutID;
}

UINT
MUIGetOEMCodePage(
    IN PCWSTR LanguageId)
{
    ULONG lngIndex = FindLanguageIndex(LanguageId);
    return MUILanguageList[lngIndex].OEMCPage;
}

GEOID
MUIGetGeoID(
    IN PCWSTR LanguageId)
{
    ULONG lngIndex = FindLanguageIndex(LanguageId);
    return MUILanguageList[lngIndex].GeoID;
}

const MUI_LAYOUTS*
MUIGetLayoutsList(
    IN PCWSTR LanguageId)
{
    ULONG lngIndex = FindLanguageIndex(LanguageId);
    return MUILanguageList[lngIndex].MuiLayouts;
}


static
BOOLEAN
AddHotkeySettings(
    IN PCWSTR Hotkey,
    IN PCWSTR LangHotkey,
    IN PCWSTR LayoutHotkey)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    UNICODE_STRING ValueName;
    HANDLE KeyHandle;
    ULONG Disposition;
    NTSTATUS Status;

    RtlInitUnicodeString(&KeyName,
                         L".DEFAULT\\Keyboard Layout\\Toggle");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               GetRootKeyByPredefKey(HKEY_USERS, NULL),
                               NULL);

    Status =  NtCreateKey(&KeyHandle,
                          KEY_SET_VALUE,
                          &ObjectAttributes,
                          0,
                          NULL,
                          REG_OPTION_NON_VOLATILE,
                          &Disposition);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateKey() failed (Status %lx)\n", Status);
        return FALSE;
    }

    RtlInitUnicodeString(&ValueName,
                         L"Hotkey");

    Status = NtSetValueKey(KeyHandle,
                           &ValueName,
                           0,
                           REG_SZ,
                           (PVOID)Hotkey,
                           (1 + 1) * sizeof(WCHAR));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtSetValueKey() failed (Status %lx)\n", Status);
        NtClose(KeyHandle);
        return FALSE;
    }

    RtlInitUnicodeString(&ValueName,
                         L"Language Hotkey");

    Status = NtSetValueKey(KeyHandle,
                           &ValueName,
                           0,
                           REG_SZ,
                           (PVOID)LangHotkey,
                           (1 + 1) * sizeof(WCHAR));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtSetValueKey() failed (Status %lx)\n", Status);
        NtClose(KeyHandle);
        return FALSE;
    }

    RtlInitUnicodeString(&ValueName,
                         L"Layout Hotkey");

    Status = NtSetValueKey(KeyHandle,
                           &ValueName,
                           0,
                           REG_SZ,
                           (PVOID)LayoutHotkey,
                           (1 + 1) * sizeof(WCHAR));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtSetValueKey() failed (Status %lx)\n", Status);
        NtClose(KeyHandle);
        return FALSE;
    }

    NtClose(KeyHandle);
    return TRUE;
}

BOOLEAN
AddKbLayoutsToRegistry(
    _In_ const MUI_LAYOUTS* MuiLayouts)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    UNICODE_STRING ValueName;
    HANDLE KeyHandle;
    HANDLE SubKeyHandle;
    NTSTATUS Status;
    ULONG Disposition;
    ULONG uIndex;
    ULONG uCount;
    WCHAR szKeyName[48] = L".DEFAULT\\Keyboard Layout";
    WCHAR szValueName[3 + 1];
    WCHAR szSubstID[8 + 1];
    WCHAR szLayoutID[8 + 1];

    /* Open the keyboard layout key */
    RtlInitUnicodeString(&KeyName, szKeyName);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               GetRootKeyByPredefKey(HKEY_USERS, NULL),
                               NULL);

    Status =  NtCreateKey(&KeyHandle,
                          KEY_CREATE_SUB_KEY,
                          &ObjectAttributes,
                          0,
                          NULL,
                          REG_OPTION_NON_VOLATILE,
                          &Disposition);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateKey() failed (Status %lx)\n", Status);
        return FALSE;
    }

    NtClose(KeyHandle);

    KeyName.MaximumLength = sizeof(szKeyName);
    Status = RtlAppendUnicodeToString(&KeyName, L"\\Preload");
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RtlAppend() failed (%lx), string is '%wZ'\n", Status, &KeyName);
        return FALSE;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               GetRootKeyByPredefKey(HKEY_USERS, NULL),
                               NULL);

    Status = NtCreateKey(&KeyHandle,
                         KEY_SET_VALUE,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         &Disposition);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateKey() failed (Status %lx)\n", Status);
        return FALSE;
    }

    RtlInitUnicodeString(&KeyName, L".DEFAULT\\Keyboard Layout\\Substitutes");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               GetRootKeyByPredefKey(HKEY_USERS, NULL),
                               NULL);

    Status =  NtCreateKey(&SubKeyHandle,
                          KEY_SET_VALUE,
                          &ObjectAttributes,
                          0,
                          NULL,
                          REG_OPTION_NON_VOLATILE,
                          &Disposition);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtCreateKey() failed (Status %lx)\n", Status);
        goto Quit;
    }

    uCount = 0;
    for (uIndex = 0; (uIndex <= 19) && (MuiLayouts[uIndex].LangID != 0); ++uIndex)
    {
        RtlStringCchPrintfW(szValueName, _countof(szValueName), L"%u", uIndex + 1);
        RtlInitUnicodeString(&ValueName, szValueName);

        RtlStringCchPrintfW(szLayoutID, _countof(szLayoutID), L"%08lx", MuiLayouts[uIndex].LayoutID);

        if ((KLID)MuiLayouts[uIndex].LangID == MuiLayouts[uIndex].LayoutID)
        {
            /* Main keyboard layout */
            Status = NtSetValueKey(KeyHandle,
                                   &ValueName,
                                   0,
                                   REG_SZ,
                                   (PVOID)szLayoutID,
                                   (wcslen(szLayoutID)+1) * sizeof(WCHAR));
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("NtSetValueKey() failed (Status = %lx, uIndex = %u)\n", Status, uIndex);
                goto Quit;
            }
        }
        else
        {
            /* Generate a substitute keyboard layout ID */
            RtlStringCchPrintfW(szSubstID, _countof(szSubstID), L"%08lx",
                                (0xD0000000/*SUBST_MASK*/ | ((USHORT)uCount << 4) | MuiLayouts[uIndex].LangID));
            Status = NtSetValueKey(KeyHandle,
                                   &ValueName,
                                   0,
                                   REG_SZ,
                                   (PVOID)szSubstID,
                                   (wcslen(szSubstID)+1) * sizeof(WCHAR));
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("NtSetValueKey() failed (Status = %lx, uIndex = %u)\n", Status, uIndex);
                goto Quit;
            }

            /* Link the substitute layout with the original one */
            RtlInitUnicodeString(&ValueName, szSubstID);
            Status = NtSetValueKey(SubKeyHandle,
                                   &ValueName,
                                   0,
                                   REG_SZ,
                                   (PVOID)szLayoutID,
                                   (wcslen(szLayoutID)+1) * sizeof(WCHAR));
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("NtSetValueKey() failed (Status = %lx, uIndex = %u)\n", Status, uIndex);
                goto Quit;
            }

            ++uCount;
        }
    }

    AddHotkeySettings(L"1", L"1", L"2");

Quit:
    NtClose(SubKeyHandle);
    NtClose(KeyHandle);
    return NT_SUCCESS(Status);
}

BOOLEAN
AddKeyboardLayouts(
    IN PCWSTR LanguageId)
{
    ULONG lngIndex = 0;

    while (MUILanguageList[lngIndex].LanguageID != NULL)
    {
        if (_wcsicmp(MUILanguageList[lngIndex].LanguageID, LanguageId) == 0)
        {
            return AddKbLayoutsToRegistry(MUILanguageList[lngIndex].MuiLayouts);
        }

        lngIndex++;
    }

    return FALSE;
}

static
BOOLEAN
AddCodepageToRegistry(
    _In_ UINT ACPage,
    _In_ UINT OEMCPage,
    _In_ UINT MACCPage)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    HANDLE KeyHandle;
    /*
     * Buffer big enough to hold the NULL-terminated string L"4294967295",
     * corresponding to the literal 0xFFFFFFFF (MAXULONG) in decimal.
     */
    WCHAR Value[sizeof("4294967295")];

    /* Open the NLS CodePage key */
    RtlInitUnicodeString(&Name,
                         L"SYSTEM\\CurrentControlSet\\Control\\NLS\\CodePage");
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               GetRootKeyByPredefKey(HKEY_LOCAL_MACHINE, NULL),
                               NULL);
    Status = NtOpenKey(&KeyHandle,
                       KEY_WRITE,
                       &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtOpenKey() failed (Status %lx)\n", Status);
        return FALSE;
    }

    /* Set ANSI codepage */
    Status = RtlStringCchPrintfW(Value, _countof(Value), L"%lu", ACPage);
    ASSERT(NT_SUCCESS(Status));

    RtlInitUnicodeString(&Name, L"ACP");
    Status = NtSetValueKey(KeyHandle,
                           &Name,
                           0,
                           REG_SZ,
                           (PVOID)Value,
                           (wcslen(Value)+1) * sizeof(WCHAR));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtSetValueKey() failed (Status %lx)\n", Status);
        goto Quit;
    }

    /* Set OEM codepage */
    Status = RtlStringCchPrintfW(Value, _countof(Value), L"%lu", OEMCPage);
    ASSERT(NT_SUCCESS(Status));

    RtlInitUnicodeString(&Name, L"OEMCP");
    Status = NtSetValueKey(KeyHandle,
                           &Name,
                           0,
                           REG_SZ,
                           (PVOID)Value,
                           (wcslen(Value)+1) * sizeof(WCHAR));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtSetValueKey() failed (Status %lx)\n", Status);
        goto Quit;
    }

    /* Set MAC codepage */
    Status = RtlStringCchPrintfW(Value, _countof(Value), L"%lu", MACCPage);
    ASSERT(NT_SUCCESS(Status));

    RtlInitUnicodeString(&Name, L"MACCP");
    Status = NtSetValueKey(KeyHandle,
                           &Name,
                           0,
                           REG_SZ,
                           (PVOID)Value,
                           (wcslen(Value)+1) * sizeof(WCHAR));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtSetValueKey() failed (Status %lx)\n", Status);
        goto Quit;
    }

Quit:
    NtClose(KeyHandle);
    return NT_SUCCESS(Status);
}

BOOLEAN
AddCodePage(
    IN PCWSTR LanguageId)
{
    ULONG lngIndex = 0;

    while (MUILanguageList[lngIndex].LanguageID != NULL)
    {
        if (_wcsicmp(MUILanguageList[lngIndex].LanguageID, LanguageId) == 0)
        {
            return AddCodepageToRegistry(MUILanguageList[lngIndex].ACPage,
                                         MUILanguageList[lngIndex].OEMCPage,
                                         MUILanguageList[lngIndex].MACCPage);
        }

        lngIndex++;
    }

    return FALSE;
}

/* EOF */
