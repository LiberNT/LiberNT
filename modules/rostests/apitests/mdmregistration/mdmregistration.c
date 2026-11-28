/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the device management registration query
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

typedef HRESULT (WINAPI *IS_DEVICE_REGISTERED_WITH_MANAGEMENT)(BOOL *, DWORD, PWSTR);

START_TEST(IsDeviceRegisteredWithManagement)
{
    IS_DEVICE_REGISTERED_WITH_MANAGEMENT IsRegistered;
    HMODULE Module;
    BOOL Managed = TRUE;
    HRESULT hr;

    Module = LoadLibraryW(L"mdmregistration.dll");
    ok(Module != NULL, "LoadLibraryW(mdmregistration.dll) failed, error %lu\n", GetLastError());
    if (Module == NULL)
        return;

    IsRegistered = (IS_DEVICE_REGISTERED_WITH_MANAGEMENT)GetProcAddress(Module, "IsDeviceRegisteredWithManagement");
    ok(IsRegistered != NULL, "IsDeviceRegisteredWithManagement is missing\n");
    if (IsRegistered != NULL)
    {
        hr = IsRegistered(&Managed, 0, NULL);
        ok(hr == S_OK, "IsDeviceRegisteredWithManagement returned %#lx\n", hr);
        ok(Managed == FALSE, "the device is reported as managed\n");

        hr = IsRegistered(NULL, 0, NULL);
        ok(hr == S_OK, "a NULL output returned %#lx\n", hr);
    }

    FreeLibrary(Module);
}
