/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the identifiers and the installed description of the HAL's reported computer device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <initguid.h>
#include <devpkey.h>
#include <cfgmgr32.h>

static
BOOL
GetStringProperty(
    _In_ DEVINST Instance,
    _In_ const DEVPROPKEY *Key,
    _In_ DEVPROPTYPE ExpectedType,
    _Out_writes_bytes_(Size) WCHAR *Buffer,
    _In_ ULONG Size,
    _In_ PCSTR Name)
{
    DEVPROPTYPE Type = DEVPROP_TYPE_EMPTY;
    CONFIGRET Result;

    ZeroMemory(Buffer, Size);
    Result = CM_Get_DevNode_PropertyW(Instance, Key, &Type, (BYTE *)Buffer, &Size, 0);
    ok(Result == CR_SUCCESS, "%s: CM_Get_DevNode_PropertyW returned %lu\n", Name, Result);
    if (Result != CR_SUCCESS)
        return FALSE;
    ok(Type == ExpectedType, "%s: type 0x%lx\n", Name, Type);
    return Type == ExpectedType;
}

static
VOID
CheckString(
    _In_ DEVINST Instance,
    _In_ const DEVPROPKEY *Key,
    _In_ PCWSTR Expected,
    _In_ PCSTR Name)
{
    WCHAR Buffer[256];

    if (GetStringProperty(Instance, Key, DEVPROP_TYPE_STRING, Buffer, sizeof(Buffer) - sizeof(WCHAR), Name))
        ok(!wcscmp(Buffer, Expected), "%s: got '%ls', expected '%ls'\n", Name, Buffer, Expected);
}

static
VOID
CheckList(
    _In_ DEVINST Instance,
    _In_ const DEVPROPKEY *Key,
    _In_ PCWSTR Expected,
    _In_ ULONG ExpectedChars,
    _In_ PCSTR Name)
{
    WCHAR Buffer[256];

    if (GetStringProperty(Instance, Key, DEVPROP_TYPE_STRING_LIST, Buffer, sizeof(Buffer) - 2 * sizeof(WCHAR), Name))
        ok(!memcmp(Buffer, Expected, ExpectedChars * sizeof(WCHAR)), "%s: got '%ls' first\n", Name, Buffer);
}

START_TEST(HalDeviceIds)
{
    static const WCHAR CompatibleIds[] = L"DETECTEDInternal\\ACPI_HAL\0DETECTED\\ACPI_HAL\0";
    SYSTEM_INFO Info;
    DEVINST Instance;
    CONFIGRET Result;

    GetNativeSystemInfo(&Info);
    if (Info.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64)
    {
        skip("The Windows 11 values are measured on x64 only\n");
        return;
    }

    Result = CM_Locate_DevNodeW(&Instance, (DEVINSTID_W)L"ROOT\\ACPI_HAL\\0000", CM_LOCATE_DEVNODE_NORMAL);
    ok(Result == CR_SUCCESS, "CM_Locate_DevNodeW returned %lu\n", Result);
    if (Result != CR_SUCCESS)
        return;

    CheckList(Instance, &DEVPKEY_Device_HardwareIds, L"acpiapic\0", RTL_NUMBER_OF(L"acpiapic\0"), "HardwareIds");
    CheckList(Instance, &DEVPKEY_Device_CompatibleIds, CompatibleIds, RTL_NUMBER_OF(CompatibleIds), "CompatibleIds");
    CheckString(Instance, &DEVPKEY_Device_Class, L"Computer", "Class");
    CheckString(Instance, &DEVPKEY_Device_DeviceDesc, L"ACPI x64-based PC", "DeviceDesc");
    CheckString(Instance, &DEVPKEY_NAME, L"ACPI x64-based PC", "Name");
    CheckString(Instance, &DEVPKEY_Device_DriverInfPath, L"hal.inf", "DriverInfPath");
    CheckString(Instance, &DEVPKEY_Device_DriverInfSection, L"ACPI_AMD64_HAL", "DriverInfSection");
    CheckString(Instance, &DEVPKEY_Device_Manufacturer, L"(Standard computers)", "Manufacturer");
}
