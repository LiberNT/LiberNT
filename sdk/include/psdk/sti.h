/*
 * Copyright (C) 2009 Damjan Jovanovic
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

#ifndef __WINE_STI_H
#define __WINE_STI_H

#include <objbase.h>
/* #include <stireg.h> */
/* #include <stierr.h> */

#pragma pack(push, 8)

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_GUID(CLSID_Sti, 0xB323F8E0L, 0x2E68, 0x11D0, 0x90, 0xEA, 0x00, 0xAA, 0x00, 0x60, 0xF8, 0x6C);

DEFINE_GUID(IID_IStillImageW, 0x641BD880, 0x2DC8, 0x11D0, 0x90, 0xEA, 0x00, 0xAA, 0x00, 0x60, 0xF8, 0x6C);

DEFINE_GUID(IID_IStillImageA, 0xA7B1F740, 0x1D7F, 0x11D1, 0xAC, 0xA9, 0x00, 0xA0, 0x24, 0x38, 0xAD, 0x48);

#define STI_VERSION_REAL         0x00000002
#define STI_VERSION_FLAG_UNICODE 0x01000000

#ifndef WINE_NO_UNICODE_MACROS
# ifdef UNICODE
#  define STI_VERSION (STI_VERSION_REAL | STI_VERSION_FLAG_UNICODE)
# else
#  define STI_VERSION (STI_VERSION_REAL)
# endif
#endif

typedef struct IStillImageA *PSTIA;
typedef struct IStillImageW *PSTIW;
DECL_WINELIB_TYPE_AW(PSTI)
typedef struct IStillImageA *LPSTILLIMAGEA;
typedef struct IStillImageW *LPSTILLIMAGEW;
DECL_WINELIB_TYPE_AW(LPSTILLIMAGE)
typedef struct IStiDeviceA *PSTIDEVICEA;
typedef struct IStiDeviceW *PSTIDEVICEW;
DECL_WINELIB_TYPE_AW(PSTIDEVICE)

HRESULT WINAPI StiCreateInstanceA(HINSTANCE hinst, DWORD dwVer, PSTIA *ppSti, LPUNKNOWN pUnkOuter);
HRESULT WINAPI StiCreateInstanceW(HINSTANCE hinst, DWORD dwVer, PSTIW *ppSti, LPUNKNOWN pUnkOuter);
#define        StiCreateInstance WINELIB_NAME_AW(StiCreateInstance)

typedef DWORD STI_DEVICE_TYPE;
typedef enum _STI_DEVICE_MJ_TYPE
{
    StiDeviceTypeDefault           = 0,
    StiDeviceTypeScanner           = 1,
    StiDeviceTypeDigitalCamera     = 2,
    StiDeviceTypeStreamingVideo    = 3
} STI_DEVICE_MJ_TYPE;

#define GET_STIDEVICE_TYPE(dwDevType) HIWORD(dwDevType)
#define GET_STIDEVICE_SUBTYPE(dwDevType) LOWORD(dwDevType)

typedef struct _STI_DEV_CAPS {
    DWORD dwGeneric;
} STI_DEV_CAPS, *PSTI_DEV_CAPS;

#define STI_MAX_INTERNAL_NAME_LENGTH 128

typedef struct _STI_DEVICE_INFORMATIONW {
    DWORD dwSize;
    STI_DEVICE_TYPE DeviceType;
    WCHAR szDeviceInternalName[STI_MAX_INTERNAL_NAME_LENGTH];
    STI_DEV_CAPS DeviceCapabilities;
    DWORD dwHardwareConfiguration;
    LPWSTR pszVendorDescription;
    LPWSTR pszDeviceDescription;
    LPWSTR pszPortName;
    LPWSTR pszPropProvider;
    LPWSTR pszLocalName;
} STI_DEVICE_INFORMATIONW, *PSTI_DEVICE_INFORMATIONW;

typedef STI_DEVICE_INFORMATIONW STI_DEVICE_INFORMATION;
typedef PSTI_DEVICE_INFORMATIONW PSTI_DEVICE_INFORMATION;

#define MAX_NOTIFICATION_DATA 64

typedef struct _STINOTIFY {
    DWORD dwSize;
    GUID guidNotificationCode;
    BYTE abNotificationData[MAX_NOTIFICATION_DATA];
} STINOTIFY,*LPSTINOTIFY;

#define INTERFACE IStillImageW
DECLARE_INTERFACE_(IStillImageW, IUnknown)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IStillImageW methods ***/
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion) PURE;
    STDMETHOD(GetDeviceList)(THIS_ DWORD dwType, DWORD dwFlags, DWORD *pdwItemsReturned, LPVOID *ppBuffer) PURE;
    STDMETHOD(GetDeviceInfo)(THIS_ LPWSTR pwszDeviceName, LPVOID *ppBuffer) PURE;
    STDMETHOD(CreateDevice)(THIS_ LPWSTR pwszDeviceName, DWORD dwMode, PSTIDEVICEW *pDevice, LPUNKNOWN pUnkOuter) PURE;
    STDMETHOD(GetDeviceValue)(THIS_ LPWSTR pwszDeviceName, LPWSTR pValueName, LPDWORD pType, LPBYTE pData, LPDWORD cbData) PURE;
    STDMETHOD(SetDeviceValue)(THIS_ LPWSTR pwszDeviceName, LPWSTR pValueName, DWORD type, LPBYTE pData, DWORD cbData) PURE;
    STDMETHOD(GetSTILaunchInformation)(THIS_ LPWSTR pwszDeviceName, DWORD *pdwEventCode, LPWSTR pwszEventName) PURE;
    STDMETHOD(RegisterLaunchApplication)(THIS_ LPWSTR pwszAppName, LPWSTR pwszCommandLine) PURE;
    STDMETHOD(UnregisterLaunchApplication)(THIS_ LPWSTR pwszAppName) PURE;
    STDMETHOD(EnableHwNotifications)(THIS_ LPCWSTR pwszDeviceName, BOOL bNewState) PURE;
    STDMETHOD(GetHwNotificationState)(THIS_ LPCWSTR pwszDeviceName, BOOL *pbCurrentState) PURE;
    STDMETHOD(RefreshDeviceBus)(THIS_ LPCWSTR pwszDeviceName) PURE;
    STDMETHOD(LaunchApplicationForDevice)(THIS_ LPWSTR pwszDeviceName, LPWSTR pwszAppName, LPSTINOTIFY pStiNotify);
    STDMETHOD(SetupDeviceParameters)(THIS_ PSTI_DEVICE_INFORMATIONW pDevInfo);
    STDMETHOD(WriteToErrorLog)(THIS_ DWORD dwMessageType, LPCWSTR pszMessage) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define IStillImage_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IStillImage_AddRef(p)             (p)->lpVtbl->AddRef(p)
#define IStillImage_Release(p)            (p)->lpVtbl->Release(p)
/*** IStillImage methods ***/
#define IStillImage_Initialize(p,a,b)                   (p)->lpVtbl->Initialize(p,a,b)
#define IStillImage_GetDeviceList(p,a,b,c,d)            (p)->lpVtbl->GetDeviceList(p,a,b,c,d)
#define IStillImage_GetDeviceInfo(p,a,b)                (p)->lpVtbl->GetDeviceInfo(p,a,b)
#define IStillImage_CreateDevice(p,a,b,c,d)             (p)->lpVtbl->CreateDevice(p,a,b,c,d)
#define IStillImage_GetDeviceValue(p,a,b,c,d,e)         (p)->lpVtbl->GetDeviceValue(p,a,b,c,d,e)
#define IStillImage_SetDeviceValue(p,a,b,c,d,e)         (p)->lpVtbl->SetDeviceValue(p,a,b,c,d,e)
#define IStillImage_GetSTILaunchInformation(p,a,b,c)    (p)->lpVtbl->GetSTILaunchInformation(p,a,b,c)
#define IStillImage_RegisterLaunchApplication(p,a,b)    (p)->lpVtbl->RegisterLaunchApplication(p,a,b)
#define IStillImage_UnregisterLaunchApplication(p,a)    (p)->lpVtbl->UnregisterLaunchApplication(p,a)
#define IStillImage_EnableHwNotifications(p,a,b)        (p)->lpVtbl->EnableHwNotifications(p,a,b)
#define IStillImage_GetHwNotificationState(p,a,b)       (p)->lpVtbl->GetHwNotificationState(p,a,b)
#define IStillImage_RefreshDeviceBus(p,a)               (p)->lpVtbl->RefreshDeviceBus(p,a)
#define IStillImage_LaunchApplicationForDevice(p,a,b,c) (p)->lpVtbl->LaunchApplicationForDevice(p,a,b,c)
#define IStillImage_SetupDeviceParameters(p,a)          (p)->lpVtbl->SetupDeviceParameters(p,a)
#define IStillImage_WriteToErrorLog(p,a,b)              (p)->lpVtbl->WriteToErrorLog(p,a,b)
#else
/*** IUnknown methods ***/
#define IStillImage_QueryInterface(p,a,b) (p)->QueryInterface(a,b)
#define IStillImage_AddRef(p)             (p)->AddRef()
#define IStillImage_Release(p)            (p)->Release()
/*** IStillImage methods ***/
#define IStillImage_Initialize(p,a,b)                   (p)->Initialize(a,b)
#define IStillImage_GetDeviceList(p,a,b,c,d)            (p)->GetDeviceList(a,b,c,d)
#define IStillImage_GetDeviceInfo(p,a,b)                (p)->GetDeviceInfo(a,b)
#define IStillImage_CreateDevice(p,a,b,c,d)             (p)->CreateDevice(a,b,c,d)
#define IStillImage_GetDeviceValue(p,a,b,c,d,e)         (p)->GetDeviceValue(a,b,c,d,e)
#define IStillImage_SetDeviceValue(p,a,b,c,d,e)         (p)->SetDeviceValue(a,b,c,d,e)
#define IStillImage_GetSTILaunchInformation(p,a,b,c)    (p)->GetSTILaunchInformation(a,b,c)
#define IStillImage_RegisterLaunchApplication(p,a,b)    (p)->RegisterLaunchApplication(a,b)
#define IStillImage_UnregisterLaunchApplication(p,a)    (p)->UnregisterLaunchApplication(a)
#define IStillImage_EnableHwNotifications(p,a,b)        (p)->EnableHwNotifications(a,b)
#define IStillImage_GetHwNotificationState(p,a,b)       (p)->GetHwNotificationState(a,b)
#define IStillImage_RefreshDeviceBus(p,a)               (p)->RefreshDeviceBus(a)
#define IStillImage_LaunchApplicationForDevice(p,a,b,c) (p)->LaunchApplicationForDevice(a,b,c)
#define IStillImage_SetupDeviceParameters(p,a)          (p)->SetupDeviceParameters(a)
#define IStillImage_WriteToErrorLog(p,a,b)              (p)->WriteToErrorLog(a,b)
#endif

#ifdef __cplusplus
}
#endif

#pragma pack(pop)

#ifndef STIEDFL_ALLDEVICES
#define STIEDFL_ALLDEVICES 0x00000000
#endif
#ifndef STIEDFL_ATTACHEDONLY
#define STIEDFL_ATTACHEDONLY 0x00000001
#endif
#ifndef STI_DEVICE_CREATE_BOTH
#define STI_DEVICE_CREATE_BOTH 0x00000003
#endif
#ifndef STI_DEVICE_CREATE_DATA
#define STI_DEVICE_CREATE_DATA 0x00000002
#endif
#ifndef STI_DEVICE_CREATE_MASK
#define STI_DEVICE_CREATE_MASK 0x0000FFFF
#endif
#ifndef STI_DEVICE_CREATE_STATUS
#define STI_DEVICE_CREATE_STATUS 0x00000001
#endif
#ifndef STI_DEVSTATUS_EVENTS_STATE
#define STI_DEVSTATUS_EVENTS_STATE 0x0002
#endif
#ifndef STI_DEVSTATUS_ONLINE_STATE
#define STI_DEVSTATUS_ONLINE_STATE 0x0001
#endif
#ifndef STI_DIAGCODE_HWPRESENCE
#define STI_DIAGCODE_HWPRESENCE 0x00000001
#endif
#ifndef STI_EVENTHANDLING_ENABLED
#define STI_EVENTHANDLING_ENABLED 0x00000001
#endif
#ifndef STI_EVENTHANDLING_PENDING
#define STI_EVENTHANDLING_PENDING 0x00000004
#endif
#ifndef STI_EVENTHANDLING_POLLING
#define STI_EVENTHANDLING_POLLING 0x00000002
#endif
#ifndef STI_GENCAP_AUTO_PORTSELECT
#define STI_GENCAP_AUTO_PORTSELECT 0x00000008
#endif
#ifndef STI_GENCAP_GENERATE_ARRIVALEVENT
#define STI_GENCAP_GENERATE_ARRIVALEVENT 0x00000004
#endif
#ifndef STI_GENCAP_NOTIFICATIONS
#define STI_GENCAP_NOTIFICATIONS 0x00000001
#endif
#ifndef STI_GENCAP_POLLING_NEEDED
#define STI_GENCAP_POLLING_NEEDED 0x00000002
#endif
#ifndef STI_GENCAP_SUBSET
#define STI_GENCAP_SUBSET 0x00000020
#endif
#ifndef STI_GENCAP_WIA
#define STI_GENCAP_WIA 0x00000010
#endif
#ifndef STI_HW_CONFIG_PARALLEL
#define STI_HW_CONFIG_PARALLEL 0x0010
#endif
#ifndef STI_HW_CONFIG_SCSI
#define STI_HW_CONFIG_SCSI 0x0002
#endif
#ifndef STI_HW_CONFIG_SERIAL
#define STI_HW_CONFIG_SERIAL 0x0008
#endif
#ifndef STI_HW_CONFIG_UNKNOWN
#define STI_HW_CONFIG_UNKNOWN 0x0001
#endif
#ifndef STI_HW_CONFIG_USB
#define STI_HW_CONFIG_USB 0x0004
#endif
#ifndef STI_ONLINESTATE_BUSY
#define STI_ONLINESTATE_BUSY 0x00000100
#endif
#ifndef STI_ONLINESTATE_ERROR
#define STI_ONLINESTATE_ERROR 0x00000004
#endif
#ifndef STI_ONLINESTATE_INITIALIZING
#define STI_ONLINESTATE_INITIALIZING 0x00000400
#endif
#ifndef STI_ONLINESTATE_IO_ACTIVE
#define STI_ONLINESTATE_IO_ACTIVE 0x00000080
#endif
#ifndef STI_ONLINESTATE_OFFLINE
#define STI_ONLINESTATE_OFFLINE 0x00000040
#endif
#ifndef STI_ONLINESTATE_OPERATIONAL
#define STI_ONLINESTATE_OPERATIONAL 0x00000001
#endif
#ifndef STI_ONLINESTATE_PAPER_JAM
#define STI_ONLINESTATE_PAPER_JAM 0x00000010
#endif
#ifndef STI_ONLINESTATE_PAPER_PROBLEM
#define STI_ONLINESTATE_PAPER_PROBLEM 0x00000020
#endif
#ifndef STI_ONLINESTATE_PAUSED
#define STI_ONLINESTATE_PAUSED 0x00000008
#endif
#ifndef STI_ONLINESTATE_PENDING
#define STI_ONLINESTATE_PENDING 0x00000002
#endif
#ifndef STI_ONLINESTATE_POWER_SAVE
#define STI_ONLINESTATE_POWER_SAVE 0x00002000
#endif
#ifndef STI_ONLINESTATE_TRANSFERRING
#define STI_ONLINESTATE_TRANSFERRING 0x00000200
#endif
#ifndef STI_ONLINESTATE_USER_INTERVENTION
#define STI_ONLINESTATE_USER_INTERVENTION 0x00001000
#endif
#ifndef STI_ONLINESTATE_WARMING_UP
#define STI_ONLINESTATE_WARMING_UP 0x00000800
#endif
#ifndef STI_RAW_RESERVED
#define STI_RAW_RESERVED 0x1000
#endif
#ifndef STI_SUBSCRIBE_FLAG_EVENT
#define STI_SUBSCRIBE_FLAG_EVENT 0x0002
#endif
#ifndef STI_SUBSCRIBE_FLAG_WINDOW
#define STI_SUBSCRIBE_FLAG_WINDOW 0x0001
#endif
#ifndef STI_TRACE_ERROR
#define STI_TRACE_ERROR 0x00000004
#endif
#ifndef STI_TRACE_INFORMATION
#define STI_TRACE_INFORMATION 0x00000001
#endif
#ifndef STI_TRACE_WARNING
#define STI_TRACE_WARNING 0x00000002
#endif
#ifndef STI_UNICODE
#define STI_UNICODE 1
#endif
#ifndef STI_VERSION_FLAG_MASK
#define STI_VERSION_FLAG_MASK 0xff000000
#endif
#ifndef STI_VERSION_MIN_ALLOWED
#define STI_VERSION_MIN_ALLOWED 0x00000002
#endif
#ifndef WIA_INCOMPAT_XP
#define WIA_INCOMPAT_XP 0x00000001
#endif

typedef DWORD STI_RAW_CONTROL_CODE;

typedef struct _ERROR_INFOW {
  DWORD dwSize;
  DWORD dwGenericError;
  DWORD dwVendorError;
  WCHAR szExtendedErrorText[255];
} STI_ERROR_INFOW, *PSTI_ERROR_INFOW;

typedef struct _STI_DEVICE_STATUS {
  DWORD dwSize;
  DWORD StatusMask;
  DWORD dwOnlineState;
  DWORD dwHardwareStatusCode;
  DWORD dwEventHandlingState;
  DWORD dwPollingInterval;
} STI_DEVICE_STATUS, *PSTI_DEVICE_STATUS;

typedef struct _STI_WIA_DEVICE_INFORMATIONW {
  DWORD dwSize;
  STI_DEVICE_TYPE DeviceType;
  WCHAR szDeviceInternalName[128];
  STI_DEV_CAPS DeviceCapabilitiesA;
  DWORD dwHardwareConfiguration;
  LPWSTR pszVendorDescription;
  LPWSTR pszDeviceDescription;
  LPWSTR pszPortName;
  LPWSTR pszPropProvider;
  LPWSTR pszLocalName;
  LPWSTR pszUiDll;
  LPWSTR pszServer;
} STI_WIA_DEVICE_INFORMATIONW, *PSTI_WIA_DEVICE_INFORMATIONW;

typedef struct _STISUBSCRIBE {
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwFilter;
  HWND hWndNotify;
  HANDLE hEvent;
  UINT uiNotificationMessage;
} STISUBSCRIBE, *LPSTISUBSCRIBE;

#define _STICOM_
#define DLLEXP __declspec(dllexport)
#if (!defined(MIDL_PASS)) && (defined(UNICODE) || defined(STI_UNICODE))
#ifdef UNICODE
#define IID_IStillImage IID_IStillImageW
#else
#define IID_IStillImage IID_IStillImageA
#endif
#endif
#ifdef UNICODE
#define IStillImage IStillImageW
#else
#if (!defined(MIDL_PASS)) && (defined(UNICODE) || defined(STI_UNICODE))
#ifdef UNICODE
#define IStillImage IStillImageW
#else
#define IStillImage IStillImageA
#endif
#endif
#endif
#if (!defined(MIDL_PASS)) && (defined(UNICODE) || defined(STI_UNICODE))
#define IStillImageVtbl IStillImageWVtbl
#endif
#define STI_ADD_DEVICE_BROADCAST_ACTION "Arrival"
#define STI_ADD_DEVICE_BROADCAST_STRING "STI\\" STI_ADD_DEVICE_BROADCAST_ACTION "\\%s"
#define STI_GENCAP_COMMON_MASK ((DWORD)0xFF)
#define STI_REMOVE_DEVICE_BROADCAST_ACTION "Removal"
#define STI_REMOVE_DEVICE_BROADCAST_STRING "STI\\" STI_REMOVE_DEVICE_BROADCAST_ACTION "\\%s"
#define STI_VERSION_3 0x1000003

#if (!defined(MIDL_PASS))
typedef struct IStiDeviceVtbl IStiDeviceVtbl;

struct IStiDevice
{
    struct IStiDeviceVtbl *lpVtbl;
};

typedef struct IStillImageWVtbl IStillImageWVtbl;

typedef struct IStiDevice *LPSTILLIMAGEDEVICE;
#endif

#if (defined(UNICODE) || defined(STI_UNICODE))
typedef PSTI_WIA_DEVICE_INFORMATIONW PSTI_WIA_DEVICE_INFORMATION;

typedef STI_ERROR_INFOW STI_ERROR_INFO;
#endif

struct _STI_DIAG
{
    DWORD dwSize;
    DWORD dwBasicDiagCode;
    DWORD dwVendorDiagCode;
    DWORD dwStatusMask;
    STI_ERROR_INFO sErrorInfo;
};

typedef struct _STI_DIAG *LPSTI_DIAG;

typedef LPSTI_DIAG LPDIAG;

typedef STI_ERROR_INFO *PSTI_ERROR_INFO;

typedef struct _STI_DIAG STI_DIAG;

typedef STI_DIAG DIAG;

#if (defined(UNICODE) || defined(STI_UNICODE))
typedef STI_WIA_DEVICE_INFORMATIONW STI_WIA_DEVICE_INFORMATION;
#endif

#define GET_STIDCOMMON_CAPS(dwGenericCaps) LOWORD(dwGenericCaps)
#define GET_STIVENDOR_CAPS(dwGenericCaps) HIWORD(dwGenericCaps)
#define GET_STIVER_MAJOR(dwVersion) (HIWORD(dwVersion) & ~STI_VERSION_FLAG_MASK)
#define GET_STIVER_MINOR(dwVersion) LOWORD(dwVersion)
#if (!defined(MIDL_PASS))
#define IStiDevice_AddRef(p) (p) -> lpVtbl -> AddRef(p)
#define IStiDevice_DeviceReset(p) (p) -> lpVtbl -> DeviceReset(p)
#define IStiDevice_Diagnostic(p, a) (p) -> lpVtbl -> Diagnostic(p, a)
#define IStiDevice_Escape(p, a, b, c, d, e, f) (p) -> lpVtbl -> Escape(p, a, b, c, d, e, f)
#define IStiDevice_GetCapabilities(p, a) (p) -> lpVtbl -> GetCapabilities(p, a)
#define IStiDevice_GetLastError(p, a) (p) -> lpVtbl -> GetLastError(p, a)
#define IStiDevice_GetLastErrorInfo(p, a) (p) -> lpVtbl -> GetLastErrorInfo(p, a)
#define IStiDevice_GetNotificationData(p, a) (p) -> lpVtbl -> GetNotificationData(p, a)
#define IStiDevice_GetStatus(p, a) (p) -> lpVtbl -> GetStatus(p, a)
#define IStiDevice_Initialize(p, a, b, c, d) (p) -> lpVtbl -> Initialize(p, a, b, c, d)
#define IStiDevice_LockDevice(p, a) (p) -> lpVtbl -> LockDevice(p, a)
#define IStiDevice_QueryInterface(p, a, b) (p) -> lpVtbl -> QueryInterface(p, a, b)
#define IStiDevice_RawReadCommand(p, a, b, c) (p) -> lpVtbl -> RawReadCommand(p, a, b, c)
#define IStiDevice_RawReadData(p, a, b, c) (p) -> lpVtbl -> RawReadData(p, a, b, c)
#define IStiDevice_RawWriteCommand(p, a, b, c) (p) -> lpVtbl -> RawWriteCommand(p, a, b, c)
#define IStiDevice_RawWriteData(p, a, b, c) (p) -> lpVtbl -> RawWriteData(p, a, b, c)
#define IStiDevice_Release(p) (p) -> lpVtbl -> Release(p)
#define IStiDevice_Subscribe(p, a) (p) -> lpVtbl -> Subscribe(p, a)
#define IStiDevice_UnLockDevice(p) (p) -> lpVtbl -> UnLockDevice(p)
#define IStiDevice_UnSubscribe(p) (p) -> lpVtbl -> UnSubscribe(p)
#endif

#endif /* __WINE_STI_H */
