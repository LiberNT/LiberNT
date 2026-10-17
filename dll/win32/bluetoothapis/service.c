/*
 * PROJECT:     LiberNT Bluetooth APIs
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Local service publication and installed service enumeration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winternl.h>
#include <winioctl.h>
#include <winuser.h>
#include <winreg.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <stdio.h>
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

static BOOL service_from_instance( const WCHAR *id, const WCHAR *address, GUID *guid )
{
    unsigned int d[11];
    const WCHAR *instance = wcsrchr( id, '\\' );
    int i;

    if (_wcsnicmp( id, L"BTHENUM\\{", 9 ) || !instance || !wcsstr( instance, address )) return FALSE;
    if (swscanf( id + 8, L"{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}", &d[0], &d[1], &d[2], &d[3], &d[4],
                 &d[5], &d[6], &d[7], &d[8], &d[9], &d[10] ) != 11)
        return FALSE;
    guid->Data1 = d[0];
    guid->Data2 = (USHORT)d[1];
    guid->Data3 = (USHORT)d[2];
    for (i = 0; i < 8; i++) guid->Data4[i] = (BYTE)d[3 + i];
    return TRUE;
}

DWORD WINAPI BluetoothEnumerateInstalledServices( HANDLE radio, BLUETOOTH_DEVICE_INFO *info, DWORD *count,
                                                  GUID *services )
{
    SP_DEVINFO_DATA data = { sizeof(data) };
    WCHAR id[MAX_DEVICE_ID_LEN], address[16];
    DWORD index, found = 0, capacity, i;
    HDEVINFO set;
    GUID guid;

    if (!info || info->dwSize != sizeof(*info) || !count) return ERROR_INVALID_PARAMETER;
    capacity = services ? *count : 0;
    swprintf( address, ARRAY_SIZE(address), L"%012I64X_C", info->Address.ullLong );

    set = SetupDiGetClassDevsW( NULL, L"BTHENUM", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT );
    if (set == INVALID_HANDLE_VALUE) return GetLastError();
    for (index = 0; SetupDiEnumDeviceInfo( set, index, &data ); index++)
    {
        if (!SetupDiGetDeviceInstanceIdW( set, &data, id, ARRAY_SIZE(id), NULL )) continue;
        if (!service_from_instance( id, address, &guid )) continue;
        for (i = 0; i < found && i < capacity; i++)
            if (IsEqualGUID( &services[i], &guid )) break;
        if (i < found && i < capacity) continue;
        if (found < capacity) services[found] = guid;
        found++;
    }
    SetupDiDestroyDeviceInfoList( set );

    if (services && found > capacity)
    {
        *count = capacity;
        return ERROR_MORE_DATA;
    }
    *count = found;
    return services || !found ? ERROR_SUCCESS : ERROR_MORE_DATA;
}
