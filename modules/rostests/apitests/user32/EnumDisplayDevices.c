/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the strings EnumDisplayDevicesW returns
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include "precomp.h"

static
VOID
CheckTail(
    _In_ PCWSTR Name,
    _In_reads_(Count) const WCHAR *String,
    _In_ SIZE_T Count)
{
    SIZE_T Length = wcsnlen(String, Count), i;

    ok(Length < Count, "%S is not terminated\n", Name);
    for (i = Length; i < Count; i++)
    {
        if (String[i] != UNICODE_NULL)
        {
            ok(0, "%S has 0x%04x at %lu after its end at %lu\n", Name, String[i], (ULONG)i, (ULONG)Length);
            return;
        }
    }
}

START_TEST(EnumDisplayDevices)
{
    DISPLAY_DEVICEW Device;
    DWORD Index;

    for (Index = 0; ; Index++)
    {
        memset(&Device, 0xcc, sizeof(Device));
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        CheckTail(L"DeviceName", Device.DeviceName, ARRAYSIZE(Device.DeviceName));
        CheckTail(L"DeviceString", Device.DeviceString, ARRAYSIZE(Device.DeviceString));
        CheckTail(L"DeviceID", Device.DeviceID, ARRAYSIZE(Device.DeviceID));
        CheckTail(L"DeviceKey", Device.DeviceKey, ARRAYSIZE(Device.DeviceKey));
    }
    if (Index == 0)
        skip("No display adapter in this session\n");
}
