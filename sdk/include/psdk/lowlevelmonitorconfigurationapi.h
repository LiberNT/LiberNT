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

#ifndef __WINE_LOWLEVELMONITORCONFIGURATIONAPI_H
#define __WINE_LOWLEVELMONITORCONFIGURATIONAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct _MC_TIMING_REPORT
{
    DWORD dwHorizontalFrequencyInHZ;
    DWORD dwVerticalFrequencyInHZ;
    BYTE bTimingStatusByte;
} MC_TIMING_REPORT, *LPMC_TIMING_REPORT;
#pragma pack(pop)

typedef enum _MC_VCP_CODE_TYPE
{
    MC_MOMENTARY,
    MC_SET_PARAMETER
} MC_VCP_CODE_TYPE, *LPMC_VCP_CODE_TYPE;

#ifdef __cplusplus
}
#endif

#define LowLevelMonitorConfigurationAPI_h 0

#ifdef __cplusplus
extern "C" {
#endif

_BOOL WINAPI CapabilitiesRequestAndCapabilitiesReply(HANDLE hMonitor, LPSTR pszASCIICapabilitiesString, DWORD dwCapabilitiesStringLengthInCharacters);
_BOOL WINAPI GetCapabilitiesStringLength(HANDLE hMonitor, LPDWORD pdwCapabilitiesStringLengthInCharacters);
_BOOL WINAPI GetTimingReport(HANDLE hMonitor, LPMC_TIMING_REPORT pmtrMonitorTimingReport);
_BOOL WINAPI GetVCPFeatureAndVCPFeatureReply(HANDLE hMonitor, BYTE bVCPCode, LPMC_VCP_CODE_TYPE pvct, LPDWORD pdwCurrentValue, LPDWORD pdwMaximumValue);
_BOOL WINAPI SetVCPFeature(HANDLE hMonitor, BYTE bVCPCode, DWORD dwNewValue);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

_BOOL WINAPI SaveCurrentSettings(HANDLE hMonitor);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_LOWLEVELMONITORCONFIGURATIONAPI_H */
