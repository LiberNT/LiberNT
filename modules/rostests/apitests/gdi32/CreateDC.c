/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Display names accepted by CreateDCW
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

static VOID
CreateDcExpect(
    _In_opt_ PCWSTR Driver,
    _In_opt_ PCWSTR Device,
    _In_ BOOL Expected)
{
    HDC hDC = CreateDCW(Driver, Device, NULL, NULL);

    ok((hDC != NULL) == Expected, "CreateDCW('%ls', '%ls'): %p\n", Driver ? Driver : L"(null)", Device ? Device : L"(null)", hDC);
    if (hDC != NULL)
        DeleteDC(hDC);
}

START_TEST(CreateDC)
{
    DISPLAY_DEVICEW Device;
    WCHAR Lower[ARRAYSIZE(Device.DeviceName)];
    UINT Index;

    CreateDcExpect(L"DISPLAY", NULL, TRUE);
    CreateDcExpect(L"display", NULL, TRUE);
    CreateDcExpect(NULL, L"DISPLAY", FALSE);

    ZeroMemory(&Device, sizeof(Device));
    Device.cb = sizeof(Device);
    if (!EnumDisplayDevicesW(NULL, 0, &Device, 0))
    {
        skip("no display device\n");
        return;
    }
    for (Index = 0; Index < ARRAYSIZE(Lower); ++Index)
    {
        Lower[Index] = Device.DeviceName[Index];
        if (Lower[Index] >= L'A' && Lower[Index] <= L'Z')
            Lower[Index] += L'a' - L'A';
    }
    CreateDcExpect(NULL, Device.DeviceName, TRUE);
    CreateDcExpect(Device.DeviceName, NULL, TRUE);
    CreateDcExpect(NULL, Lower, TRUE);
    CreateDcExpect(Lower, NULL, TRUE);
}
