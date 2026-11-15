/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the registration of the Windows Update installer class
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "com_apitest.h"
#include <wuapi.h>

START_TEST(wuapi)
{
    IUpdateInstaller *Installer = NULL;
    CLSID Clsid;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok_hr(hr, S_OK);

    hr = CLSIDFromProgID(L"Microsoft.Update.Installer", &Clsid);
    ok_hr(hr, S_OK);
    ok(IsEqualCLSID(&Clsid, &CLSID_UpdateInstaller), "Microsoft.Update.Installer is %s\n", wine_dbgstr_guid(&Clsid));

    hr = CoCreateInstance(&CLSID_UpdateInstaller, NULL, CLSCTX_INPROC_SERVER, &IID_IUpdateInstaller, (void **)&Installer);
    ok_hr(hr, S_OK);
    ok(Installer != NULL, "No installer\n");
    if (Installer != NULL)
        IUpdateInstaller_Release(Installer);

    CoUninitialize();
}
