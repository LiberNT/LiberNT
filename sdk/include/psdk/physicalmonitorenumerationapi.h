/*
 * Copyright 2014 Michael Müller for Pipelight
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

#ifndef __WINE_PHYSICALMONITORENUMERATIONAPI_H
#define __WINE_PHYSICALMONITORENUMERATIONAPI_H

#include <d3d9.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHYSICAL_MONITOR_DESCRIPTION_SIZE 128

#pragma pack(push, 1)
typedef struct _PHYSICAL_MONITOR
{
    HANDLE hPhysicalMonitor;
    WCHAR szPhysicalMonitorDescription[PHYSICAL_MONITOR_DESCRIPTION_SIZE];
} PHYSICAL_MONITOR, *LPPHYSICAL_MONITOR;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#define PhysicalMonitorEnumerationAPI_h 0

#ifdef __cplusplus
extern "C" {
#endif

_BOOL WINAPI DestroyPhysicalMonitors(DWORD dwPhysicalMonitorArraySize, LPPHYSICAL_MONITOR pPhysicalMonitorArray);
_BOOL WINAPI GetNumberOfPhysicalMonitorsFromHMONITOR(HMONITOR hMonitor, LPDWORD pdwNumberOfPhysicalMonitors);
HRESULT WINAPI GetNumberOfPhysicalMonitorsFromIDirect3DDevice9(IDirect3DDevice9 *pDirect3DDevice9, LPDWORD pdwNumberOfPhysicalMonitors);
_BOOL WINAPI GetPhysicalMonitorsFromHMONITOR(HMONITOR hMonitor, DWORD dwPhysicalMonitorArraySize, LPPHYSICAL_MONITOR pPhysicalMonitorArray);
HRESULT WINAPI GetPhysicalMonitorsFromIDirect3DDevice9(IDirect3DDevice9 *pDirect3DDevice9, DWORD dwPhysicalMonitorArraySize, LPPHYSICAL_MONITOR pPhysicalMonitorArray);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

_BOOL WINAPI DestroyPhysicalMonitor(HANDLE hMonitor);

#ifdef __cplusplus
}
#endif

typedef BOOL _BOOL;

typedef BOOL _BOOL;

#endif /* __WINE_PHYSICALMONITORENUMERATIONAPI_H */
