/*
 * SetupAPI stubs
 *
 * Copyright 2000 James Hatheway
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "setupapi_private.h"

/***********************************************************************
 *		pSetupRegistryDelnode(SETUPAPI.@)
 */
BOOL WINAPI pSetupRegistryDelnode(DWORD x, DWORD y)
{
    FIXME("%08x %08x: stub\n", x, y);
    return FALSE;
}

/***********************************************************************
 *      SetupPromptReboot(SETUPAPI.@)
 */
INT WINAPI SetupPromptReboot( HSPFILEQ file_queue, HWND owner, BOOL scan_only )
{
    FIXME("%p, %p, %d\n", file_queue, owner, scan_only);
    return 0;
}

/***********************************************************************
 *              SetupLogFileW  (SETUPAPI.@)
 */
BOOL WINAPI SetupLogFileW(HSPFILELOG file_log, PCWSTR section, PCWSTR source, PCWSTR target,
                          DWORD checksum, PCWSTR tag, PCWSTR description, PCWSTR other, DWORD flags)
{
    FIXME("(%p, %s, '%s', '%s', %lu, %s, %s, %s, %lu): stub\n", file_log,
          debugstr_w(section), debugstr_w(source), debugstr_w(target), checksum,
          debugstr_w(tag), debugstr_w(description), debugstr_w(other), flags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/***********************************************************************
 *              SetupLogFileA  (SETUPAPI.@)
 */
BOOL WINAPI SetupLogFileA(HSPFILELOG file_log, PCSTR section, PCSTR source, PCSTR target,
                          DWORD checksum, PCSTR tag, PCSTR description, PCSTR other, DWORD flags)
{
    FIXME("(%p, %p, '%s', '%s', %lu, %p, %p, %p, %lu): stub\n", file_log,
          section, source, target, checksum, tag, description, other, flags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/***********************************************************************
 *              SetupDiDrawMiniIcon  (SETUPAPI.@)
 */
INT WINAPI SetupDiDrawMiniIcon(HDC hdc, RECT rect, INT index, DWORD flags)
{
    FIXME("(%p, %s, %d, %lx) stub\n", hdc, wine_dbgstr_rect(&rect), index, flags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

/***********************************************************************
 *              SetupDiGetClassBitmapIndex  (SETUPAPI.@)
 */
BOOL WINAPI SetupDiGetClassBitmapIndex(const GUID *class, PINT index)
{
    FIXME("(%s, %p) stub\n", debugstr_guid(class), index);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/***********************************************************************
 *      SetupDiUnremoveDevice(SETUPAPI.@)
 */
BOOL WINAPI
SetupDiUnremoveDevice(
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData)
{
    FIXME ("Stub %p %p\n", DeviceInfoSet, DeviceInfoData);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


WINSETUPAPI BOOL WINAPI SetupDiGetDeviceInterfaceAlias(IN HDEVINFO  DeviceInfoSet, IN PSP_DEVICE_INTERFACE_DATA  DeviceInterfaceData, IN CONST GUID *AliasInterfaceClassGuid, OUT PSP_DEVICE_INTERFACE_DATA  AliasDeviceInterfaceData)
{
    FIXME("%p %p %p %p %p stub\n", DeviceInfoSet, DeviceInterfaceData, AliasInterfaceClassGuid, AliasDeviceInterfaceData);
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

/***********************************************************************
 *      SetupVerifyInfFileA(SETUPAPI.@)
 */
BOOL WINAPI
SetupVerifyInfFileA(
    IN PCSTR InfName,
    IN PSP_ALTPLATFORM_INFO AltPlatformInfo,
    OUT PSP_INF_SIGNER_INFO_A InfFileName)
{
    FIXME ("Stub %s %p %p\n", debugstr_a(InfName), AltPlatformInfo, InfFileName);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/***********************************************************************
 *      SetupVerifyInfFileW(SETUPAPI.@)
 */
BOOL WINAPI
SetupVerifyInfFileW(
    IN PCWSTR InfName,
    IN PSP_ALTPLATFORM_INFO AltPlatformInfo,
    OUT PSP_INF_SIGNER_INFO_W InfFileName)
{
    FIXME ("Stub %s %p %p\n", debugstr_w(InfName), AltPlatformInfo, InfFileName);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI
SetupDiSetDriverInstallParamsA(
    _In_ HDEVINFO DeviceInfoSet,
    _In_opt_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ PSP_DRVINFO_DATA_A DriverInfoData,
    _In_ PSP_DRVINSTALL_PARAMS DriverInstallParams)
{
    FIXME("Stub %p %p %p %p\n", DeviceInfoSet, DeviceInfoData, DriverInfoData, DriverInstallParams);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI
SetupDiSetDriverInstallParamsW(
    _In_ HDEVINFO DeviceInfoSet,
    _In_opt_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ PSP_DRVINFO_DATA_W DriverInfoData,
    _In_ PSP_DRVINSTALL_PARAMS DriverInstallParams)
{
    FIXME("Stub %p %p %p %p\n", DeviceInfoSet, DeviceInfoData, DriverInfoData, DriverInstallParams);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
