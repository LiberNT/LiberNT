/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests WlanIhvControl down to the miniport and back
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <winreg.h>
#include <winuser.h>
#include <wlanapi.h>
#include <setupapi.h>

#include "vwifi.h"

static VOID
TestParameters(VOID)
{
    GUID Interface = { 0 };
    BYTE Out[4];
    DWORD Returned;
    DWORD Error;

    Error = WlanIhvControl(NULL, &Interface, wlan_ihv_control_type_driver, 0, NULL, sizeof(Out), Out, &Returned);
    ok_long(Error, ERROR_INVALID_PARAMETER);

    Error = WlanIhvControl((HANDLE)1, NULL, wlan_ihv_control_type_driver, 0, NULL, sizeof(Out), Out, &Returned);
    ok_long(Error, ERROR_INVALID_PARAMETER);

    Error = WlanIhvControl((HANDLE)1, &Interface, wlan_ihv_control_type_driver, 0, NULL, sizeof(Out), Out, NULL);
    ok_long(Error, ERROR_INVALID_PARAMETER);
}

static VOID
TestDriver(HANDLE Client, const GUID *Interface)
{
    static const BYTE Request[] = { 0x11, 0x22, 0x33, 0x44, 0x55 };
    static const BYTE Reply[] = { 0x55, 0x44, 0x33, 0x22, 0x11 };
    GUID Unknown = { 0x0badf00d, 0x1234, 0x5678, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    BYTE Out[16];
    DWORD Returned;
    DWORD Error;

    memset(Out, 0xCC, sizeof(Out));
    Returned = 0xDEADBEEF;
    Error = WlanIhvControl(Client, Interface, wlan_ihv_control_type_driver,
                           sizeof(Request), (PVOID)Request, sizeof(Out), Out, &Returned);
    ok_long(Error, ERROR_SUCCESS);
    ok_long(Returned, sizeof(Reply));
    ok(memcmp(Out, Reply, sizeof(Reply)) == 0, "The miniport's answer did not come back\n");
    ok_int(Out[sizeof(Reply)], 0xCC);

    Returned = 0xDEADBEEF;
    Error = WlanIhvControl(Client, Interface, wlan_ihv_control_type_driver,
                           sizeof(Request), (PVOID)Request, 2, Out, &Returned);
    ok_long(Error, ERROR_INSUFFICIENT_BUFFER);
    ok_long(Returned, 0);

    Returned = 0xDEADBEEF;
    Error = WlanIhvControl(Client, Interface, wlan_ihv_control_type_driver, 0, NULL, 0, NULL, &Returned);
    ok_long(Error, ERROR_SUCCESS);
    ok_long(Returned, 0);

    Returned = 0xDEADBEEF;
    Error = WlanIhvControl(Client, Interface, wlan_ihv_control_type_service,
                           sizeof(Request), (PVOID)Request, sizeof(Out), Out, &Returned);
    ok_long(Error, ERROR_NOT_SUPPORTED);
    ok_long(Returned, 0);

    Error = WlanIhvControl(Client, Interface, (WLAN_IHV_CONTROL_TYPE)2,
                           sizeof(Request), (PVOID)Request, sizeof(Out), Out, &Returned);
    ok_long(Error, ERROR_INVALID_PARAMETER);

    Error = WlanIhvControl(Client, &Unknown, wlan_ihv_control_type_driver,
                           sizeof(Request), (PVOID)Request, sizeof(Out), Out, &Returned);
    ok_long(Error, ERROR_NOT_FOUND);
}

START_TEST(WlanIhvControl)
{
    SP_DEVINFO_DATA Device;
    HDEVINFO Devices = INVALID_HANDLE_VALUE;
    DWORD NegotiatedVersion;
    HANDLE Client;
    GUID Interface;
    DWORD Error;

    TestParameters();

    Error = WlanOpenHandle(WLAN_API_VERSION_2_0, NULL, &NegotiatedVersion, &Client);
    if (Error == ERROR_SERVICE_NOT_ACTIVE)
    {
        skip("WlanSvc is not running\n");
        return;
    }
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
        return;

    if (InstallVwifi(&Devices, &Device))
    {
        if (WaitInterface(Client, Devices, &Device, &Interface))
            TestDriver(Client, &Interface);
        else
            ok(FALSE, "No wireless interface appeared for the vwifi adapter\n");
        RemoveVwifi(Devices, &Device);
    }
    else
    {
        skip("The vwifi adapter could not be installed\n");
    }

    Error = WlanCloseHandle(Client, NULL);
    ok_long(Error, ERROR_SUCCESS);
}
