/*
 * PROJECT:     LiberNT Bluetooth APIs
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Local service publication (BluetoothSetLocalServiceInfo)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winternl.h>
#include <winioctl.h>
#include <bthsdpdef.h>
#include <bluetoothapis.h>
#include <reactos/drivers/bthlocal.h>

#ifndef STATUS_PRIVILEGE_NOT_HELD
#define STATUS_PRIVILEGE_NOT_HELD ((DWORD)0xC0000061)
#endif

static BOOL has_load_driver_privilege( void )
{
    PRIVILEGE_SET set = { 1, PRIVILEGE_SET_ALL_NECESSARY };
    HANDLE token;
    BOOL held = FALSE;

    if (!LookupPrivilegeValueW( NULL, L"SeLoadDriverPrivilege", &set.Privilege[0].Luid )) return FALSE;
    if (!OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &token ) &&
        !OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ))
        return FALSE;
    if (!PrivilegeCheck( token, &set, &held )) held = FALSE;
    CloseHandle( token );
    return held;
}

DWORD WINAPI BluetoothSetLocalServiceInfo( HANDLE radio, const GUID *class_guid, ULONG instance,
                                           const BLUETOOTH_LOCAL_SERVICE_INFO *info )
{
    BLUETOOTH_FIND_RADIO_PARAMS params = { sizeof(params) };
    BTHMINI_LOCAL_SERVICE request;
    HBLUETOOTH_RADIO_FIND find;
    HANDLE own = NULL;
    DWORD returned, ret = ERROR_SUCCESS;

    if (!class_guid || !info) return ERROR_INVALID_PARAMETER;
    if (!has_load_driver_privilege()) return STATUS_PRIVILEGE_NOT_HELD;
    if (!radio)
    {
        find = BluetoothFindFirstRadio( &params, &own );
        if (!find) return ERROR_BAD_UNIT;
        BluetoothFindRadioClose( find );
        radio = own;
    }
    memset( &request, 0, sizeof(request) );
    request.ServiceClass = *class_guid;
    request.Instance = instance;
    request.Enabled = info->Enabled != FALSE;
    request.Address = info->btAddr.ullLong;
    lstrcpynW( request.Name, info->szName, ARRAY_SIZE(request.Name) );
    lstrcpynW( request.DeviceString, info->szDeviceString, ARRAY_SIZE(request.DeviceString) );
    if (!DeviceIoControl( radio, IOCTL_BTHMINI_SET_LOCAL_SERVICE, &request, sizeof(request), NULL, 0, &returned, NULL ))
    {
        ret = GetLastError();
        if (ret == ERROR_PRIVILEGE_NOT_HELD) ret = STATUS_PRIVILEGE_NOT_HELD;
        else if (ret == ERROR_INVALID_HANDLE || ret == ERROR_INVALID_FUNCTION) ret = ERROR_NOT_FOUND;
    }
    if (own) CloseHandle( own );
    return ret;
}
