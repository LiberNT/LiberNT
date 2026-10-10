/*
 * PROJECT:     ReactOS Networking
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        lib/iphlpapi/dhcp_reactos.c
 * PURPOSE:     DHCP helper functions for ReactOS
 * COPYRIGHT:   Copyright 2006 Ge van Geldorp <gvg@reactos.org>
 */

#include "iphlpapi_private.h"

DWORD
getDhcpInfoForAdapter(
    PCSTR AdapterName,
    PIP_ADAPTER_INFO ptr)
{
    HKEY hKeyInterfaces = NULL, hKeyInterface = NULL;
    DWORD dwValue, dwSize, dwType;
    DWORD ret = ERROR_SUCCESS;

    ptr->DhcpEnabled = 0;
    ptr->LeaseObtained = 0;
    ptr->LeaseExpires = 0;
    strcpy(ptr->DhcpServer.IpAddress.String, "");

    ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                        L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces",
                        0,
                        KEY_READ,
                        &hKeyInterfaces);
    if (ret != ERROR_SUCCESS)
        goto done;

    ret = RegOpenKeyExA(hKeyInterfaces,
                        AdapterName,
                        0,
                        KEY_READ,
                        &hKeyInterface);
    if (ret != ERROR_SUCCESS)
        goto done;

    dwSize = sizeof(ptr->DhcpEnabled);
    ret = RegQueryValueExW(hKeyInterface,
                           L"EnableDHCP",
                           NULL,
                           &dwType,
                           (PBYTE)&ptr->DhcpEnabled,
                           &dwSize);
    if (ret != ERROR_SUCCESS)
        ptr->DhcpEnabled = 0;

    if (ptr->DhcpEnabled != 0)
    {
        dwSize = sizeof(dwValue);
        ret = RegQueryValueExW(hKeyInterface,
                               L"LeaseObtainedTime",
                               NULL,
                               &dwType,
                               (PBYTE)&dwValue,
                               &dwSize);
        if (ret == ERROR_SUCCESS)
            ptr->LeaseObtained = (time_t)dwValue;

        dwSize = sizeof(dwValue);
        ret = RegQueryValueExW(hKeyInterface,
                               L"LeaseTerminatesTime",
                               NULL,
                               &dwType,
                               (PBYTE)&dwValue,
                               &dwSize);
        if (ret == ERROR_SUCCESS)
            ptr->LeaseExpires = (time_t)dwValue;

        dwSize = sizeof(ptr->DhcpServer.IpAddress.String);
        ret = RegQueryValueExA(hKeyInterface,
                               "DhcpServer",
                               NULL,
                               &dwType,
                               (PBYTE)&ptr->DhcpServer.IpAddress.String,
                               &dwSize);
        if (ret != ERROR_SUCCESS)
            strcpy(ptr->DhcpServer.IpAddress.String, "");
    }
    ret = ERROR_SUCCESS;

done:
    if (hKeyInterface)
        RegCloseKey(hKeyInterface);

    if (hKeyInterfaces)
        RegCloseKey(hKeyInterfaces);

    return ret;
}
