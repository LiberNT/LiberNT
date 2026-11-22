/*
 * Copyright (C) the Wine project
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

#ifndef __DINPUTD_INCLUDED__
#define __DINPUTD_INCLUDED__

#define COM_NO_WINDOWS_H
#include <objbase.h>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

DEFINE_GUID(IID_IDirectInputJoyConfig8, 0xEB0D7DFA,0x1990,0x4F27,0xB4,0xD6,0xED,0xF2,0xEE,0xC4,0xA4,0x4C);
DEFINE_GUID(IID_IDirectInputPIDDriver, 0xeec6993a,0xb3fd,0x11d2,0xa9,0x16,0x00,0xc0,0x4f,0xb9,0x86,0x38);

typedef struct IDirectInputJoyConfig8 *LPDIRECTINPUTJOYCONFIG8;


typedef BOOL (CALLBACK *LPDIJOYTYPECALLBACK)(LPCWSTR, LPVOID);

#define MAX_JOYSTRING 256
#ifndef MAX_JOYSTICKOEMVXDNAME
#define MAX_JOYSTICKOEMVXDNAME 260
#endif

#define JOY_POV_NUMDIRS         4
#define JOY_POVVAL_FORWARD      0
#define JOY_POVVAL_BACKWARD     1
#define JOY_POVVAL_LEFT         2
#define JOY_POVVAL_RIGHT        3

#define DIERR_NOMOREITEMS       MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_NO_MORE_ITEMS)

typedef struct joypos_tag
{
    DWORD dwX;
    DWORD dwY;
    DWORD dwZ;
    DWORD dwR;
    DWORD dwU;
    DWORD dwV;
} JOYPOS, *LPJOYPOS;

typedef struct joyrange_tag
{
    JOYPOS jpMin;
    JOYPOS jpMax;
    JOYPOS jpCenter;
} JOYRANGE, *LPJOYRANGE;

typedef struct joyreguservalues_tag
{
    DWORD dwTimeOut;
    JOYRANGE jrvRanges;
    JOYPOS jpDeadZone;
} JOYREGUSERVALUES, *LPJOYREGUSERVALUES;

typedef struct joyreghwsettings_tag
{
    DWORD dwFlags;
    DWORD dwNumButtons;
} JOYREGHWSETTINGS, *LPJOYHWSETTINGS;

typedef struct joyreghwvalues_tag
{
    JOYRANGE jrvHardware;
    DWORD dwPOVValues[JOY_POV_NUMDIRS];
    DWORD dwCalFlags;
} JOYREGHWVALUES, *LPJOYREGHWVALUES;

typedef struct joyreghwconfig_tag
{
    JOYREGHWSETTINGS hws;
    DWORD dwUsageSettings;
    JOYREGHWVALUES hwv;
    DWORD dwType;
    DWORD dwReserved;
} JOYREGHWCONFIG, *LPJOYREGHWCONFIG;

typedef struct DIJOYTYPEINFO_DX5
{
    DWORD dwSize;
    JOYREGHWSETTINGS hws;
    CLSID clsidConfig;
    WCHAR wszDisplayName[MAX_JOYSTRING];
    WCHAR wszCallout[MAX_JOYSTICKOEMVXDNAME];
} DIJOYTYPEINFO_DX5, *LPDIJOYTYPEINFO_DX5;
typedef const DIJOYTYPEINFO_DX5 *LPCDIJOYTYPEINFO_DX5;

typedef struct DIJOYTYPEINFO_DX6
{
    DWORD dwSize;
    JOYREGHWSETTINGS hws;
    CLSID clsidConfig;
    WCHAR wszDisplayName[MAX_JOYSTRING];
    WCHAR wszCallout[MAX_JOYSTICKOEMVXDNAME];
    WCHAR wszHardwareId[MAX_JOYSTRING];
    DWORD dwFlags1;
} DIJOYTYPEINFO_DX6, *LPDIJOYTYPEINFO_DX6;
typedef const DIJOYTYPEINFO_DX6 *LPCDIJOYTYPEINFO_DX6;

typedef struct DIJOYTYPEINFO
{
    DWORD dwSize;
    JOYREGHWSETTINGS hws;
    CLSID clsidConfig;
    WCHAR wszDisplayName[MAX_JOYSTRING];
    WCHAR wszCallout[MAX_JOYSTICKOEMVXDNAME];
    WCHAR wszHardwareId[MAX_JOYSTRING];
    DWORD dwFlags1;
    DWORD dwFlags2;
    WCHAR wszMapFile[MAX_JOYSTRING];
} DIJOYTYPEINFO, *LPDIJOYTYPEINFO;
typedef const DIJOYTYPEINFO *LPCDIJOYTYPEINFO;
#define DIJC_GUIDINSTANCE       0x00000001
#define DIJC_REGHWCONFIGTYPE    0x00000002
#define DIJC_GAIN               0x00000004
#define DIJC_CALLOUT            0x00000008
#define DIJC_WDMGAMEPORT        0x00000010

typedef struct DIJOYCONFIG_DX5
{
    DWORD dwSize;
    GUID guidInstance;
    JOYREGHWCONFIG hwc;
    DWORD dwGain;
    WCHAR wszType[MAX_JOYSTRING];
    WCHAR wszCallout[MAX_JOYSTRING];
} DIJOYCONFIG_DX5, *LPDIJOYCONFIG_DX5;
typedef const DIJOYCONFIG_DX5 *LPCDIJOYCONFIG_DX5;

typedef struct DIJOYCONFIG
{
    DWORD dwSize;
    GUID guidInstance;
    JOYREGHWCONFIG hwc;
    DWORD dwGain;
    WCHAR wszType[MAX_JOYSTRING];
    WCHAR wszCallout[MAX_JOYSTRING];
    GUID guidGameport;
} DIJOYCONFIG, *LPDIJOYCONFIG;
typedef const DIJOYCONFIG *LPCDIJOYCONFIG;

typedef struct DIJOYUSERVALUES
{
    DWORD dwSize;
    JOYREGUSERVALUES ruv;
    WCHAR wszGlobalDriver[MAX_JOYSTRING];
    WCHAR wszGameportEmulator[MAX_JOYSTRING];
} DIJOYUSERVALUES, *LPDIJOYUSERVALUES;
typedef const DIJOYUSERVALUES *LPCDIJOYUSERVALUES;


/*****************************************************************************
 * IDirectInputJoyConfig8 interface
 */
#define INTERFACE IDirectInputJoyConfig8
DECLARE_INTERFACE_(IDirectInputJoyConfig8, IUnknown)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputJoyConfig8 methods ***/
    STDMETHOD(Acquire)(THIS) PURE;
    STDMETHOD(Unacquire)(THIS) PURE;
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND, DWORD) PURE;
    STDMETHOD(SendNotify)(THIS) PURE;
    STDMETHOD(EnumTypes)(THIS_ LPDIJOYTYPECALLBACK, LPVOID) PURE;
    STDMETHOD(GetTypeInfo)(THIS_ LPCWSTR, LPDIJOYTYPEINFO, DWORD) PURE;
    STDMETHOD(SetTypeInfo)(THIS_ LPCWSTR, LPCDIJOYTYPEINFO, DWORD, LPWSTR) PURE;
    STDMETHOD(DeleteType)(THIS_ LPCWSTR) PURE;
    STDMETHOD(GetConfig)(THIS_ UINT, LPDIJOYCONFIG, DWORD) PURE;
    STDMETHOD(SetConfig)(THIS_ UINT, LPCDIJOYCONFIG, DWORD) PURE;
    STDMETHOD(DeleteConfig)(THIS_ UINT) PURE;
    STDMETHOD(GetUserValues)(THIS_ LPDIJOYUSERVALUES, DWORD) PURE;
    STDMETHOD(SetUserValues)(THIS_ LPCDIJOYUSERVALUES, DWORD) PURE;
    STDMETHOD(AddNewHardware)(THIS_ HWND, REFGUID) PURE;
    STDMETHOD(OpenTypeKey)(THIS_ LPCWSTR, DWORD, PHKEY) PURE;
    STDMETHOD(OpenAppStatusKey)(THIS_ PHKEY) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define IDirectInputJoyConfig8_QueryInterface(p,a,b)      (p)->lpVtbl->QueryInterface(p,a,b)
#define IDirectInputJoyConfig8_AddRef(p)                  (p)->lpVtbl->AddRef(p)
#define IDirectInputJoyConfig8_Release(p)                 (p)->lpVtbl->Release(p)
/*** IDirectInputJoyConfig8 methods ***/
#define IDirectInputJoyConfig8_Acquire(p)                 (p)->lpVtbl->Acquire(p)
#define IDirectInputJoyConfig8_Unacquire(p)               (p)->lpVtbl->Unacquire(p)
#define IDirectInputJoyConfig8_SetCooperativeLevel(p,a,b) (p)->lpVtbl->SetCooperativeLevel(p,a,b)
#define IDirectInputJoyConfig8_SendNotify(p)              (p)->lpVtbl->SendNotify(p)
#define IDirectInputJoyConfig8_EnumTypes(p,a,b)           (p)->lpVtbl->EnumTypes(p,a,b)
#define IDirectInputJoyConfig8_GetTypeInfo(p,a,b,c)       (p)->lpVtbl->GetTypeInfo(p,a,b,c)
#define IDirectInputJoyConfig8_SetTypeInfo(p,a,b,c,d)     (p)->lpVtbl->SetTypeInfo(p,a,b,c,d)
#define IDirectInputJoyConfig8_DeleteType(p,a)            (p)->lpVtbl->DeleteType(p,a)
#define IDirectInputJoyConfig8_GetConfig(p,a,b,c)         (p)->lpVtbl->GetConfig(p,a,b,c)
#define IDirectInputJoyConfig8_SetConfig(p,a,b,c)         (p)->lpVtbl->SetConfig(p,a,b,c)
#define IDirectInputJoyConfig8_DeleteConfig(p,a)          (p)->lpVtbl->DeleteConfig(p,a)
#define IDirectInputJoyConfig8_GetUserValues(p,a,b)       (p)->lpVtbl->GetUserValues(p,a,b)
#define IDirectInputJoyConfig8_SetUserValues(p,a,b)       (p)->lpVtbl->SetUserValues(p,a,b)
#define IDirectInputJoyConfig8_AddNewHardware(p,a,b)      (p)->lpVtbl->AddNewHardware(p,a,b)
#define IDirectInputJoyConfig8_OpenTypeKey(p,a,b,c)       (p)->lpVtbl->OpenTypeKey(p,a,b,c)
#define IDirectInputJoyConfig8_OpenAppStatusKey(p,a)      (p)->lpVtbl->OpenAppStatusKey(p,a)
#else
/*** IUnknown methods ***/
#define IDirectInputJoyConfig8_QueryInterface(p,a,b)      (p)->QueryInterface(a,b)
#define IDirectInputJoyConfig8_AddRef(p)                  (p)->AddRef()
#define IDirectInputJoyConfig8_Release(p)                 (p)->Release()
/*** IDirectInputJoyConfig8 methods ***/
#define IDirectInputJoyConfig8_Acquire(p)                 (p)->Acquire()
#define IDirectInputJoyConfig8_Unacquire(p)               (p)->Unacquire()
#define IDirectInputJoyConfig8_SetCooperativeLevel(p,a,b) (p)->SetCooperativeLevel(a,b)
#define IDirectInputJoyConfig8_SendNotify(p)              (p)->SendNotify()
#define IDirectInputJoyConfig8_EnumTypes(p,a,b)           (p)->EnumTypes(a,b)
#define IDirectInputJoyConfig8_GetTypeInfo(p,a,b,c)       (p)->GetTypeInfo(a,b,c)
#define IDirectInputJoyConfig8_SetTypeInfo(p,a,b,c,d)     (p)->SetTypeInfo(a,b,c,d)
#define IDirectInputJoyConfig8_DeleteType(p,a)            (p)->DeleteType(a)
#define IDirectInputJoyConfig8_GetConfig(p,a,b,c)         (p)->GetConfig(a,b,c)
#define IDirectInputJoyConfig8_SetConfig(p,a,b,c)         (p)->SetConfig(a,b,c)
#define IDirectInputJoyConfig8_DeleteConfig(p,a)          (p)->DeleteConfig(a)
#define IDirectInputJoyConfig8_GetUserValues(p,a,b)       (p)->GetUserValues(a,b)
#define IDirectInputJoyConfig8_SetUserValues(p,a,b)       (p)->SetUserValues(a,b)
#define IDirectInputJoyConfig8_AddNewHardware(p,a,b)      (p)->AddNewHardware(a,b)
#define IDirectInputJoyConfig8_OpenTypeKey(p,a,b,c)       (p)->OpenTypeKey(a,b,c)
#define IDirectInputJoyConfig8_OpenAppStatusKey(p,a)      (p)->OpenAppStatusKey(a)
#endif

#ifndef DIAPPIDFLAG_NOSIZE
#define DIAPPIDFLAG_NOSIZE 0x00000002
#endif
#ifndef DIAPPIDFLAG_NOTIME
#define DIAPPIDFLAG_NOTIME 0x00000001
#endif
#ifndef DIDIFT_DELETE
#define DIDIFT_DELETE 0x01000000
#endif
#ifndef DIERR_BADINF
#define DIERR_BADINF 0x80040402L
#endif
#ifndef DIERR_CANCELLED
#define DIERR_CANCELLED 0x80040401L
#endif
#ifndef DIERR_DRIVERFIRST
#define DIERR_DRIVERFIRST 0x80040300L
#endif
#ifndef DIERR_DRIVERLAST
#define DIERR_DRIVERLAST 0x800403FFL
#endif
#ifndef DIERR_INVALIDCLASSINSTALLER
#define DIERR_INVALIDCLASSINSTALLER 0x80040400L
#endif
#ifndef DIJU_GAMEPORTEMULATOR
#define DIJU_GAMEPORTEMULATOR 0x00000004
#endif
#ifndef DIJU_GLOBALDRIVER
#define DIJU_GLOBALDRIVER 0x00000002
#endif
#ifndef DIJU_USERVALUES
#define DIJU_USERVALUES 0x00000001
#endif
#ifndef DIMSGWP_DX8APPSTART
#define DIMSGWP_DX8APPSTART 0x00000002
#endif
#ifndef DIMSGWP_DX8MAPPERAPPSTART
#define DIMSGWP_DX8MAPPERAPPSTART 0x00000003
#endif
#ifndef DITC_CALLOUT
#define DITC_CALLOUT 0x00000008
#endif
#ifndef DITC_CLSIDCONFIG
#define DITC_CLSIDCONFIG 0x00000002
#endif
#ifndef DITC_DISPLAYNAME
#define DITC_DISPLAYNAME 0x00000004
#endif
#ifndef DITC_FLAGS1
#define DITC_FLAGS1 0x00000020
#endif
#ifndef DITC_FLAGS2
#define DITC_FLAGS2 0x00000040
#endif
#ifndef DITC_HARDWAREID
#define DITC_HARDWAREID 0x00000010
#endif
#ifndef DITC_MAPFILE
#define DITC_MAPFILE 0x00000080
#endif
#ifndef DITC_REGHWSETTINGS
#define DITC_REGHWSETTINGS 0x00000001
#endif
#ifndef JOYTYPE_ANALOGCOMPAT
#define JOYTYPE_ANALOGCOMPAT 0x00000008l
#endif
#ifndef JOYTYPE_DEFAULTPROPSHEET
#define JOYTYPE_DEFAULTPROPSHEET 0x80000000l
#endif
#ifndef JOYTYPE_DEVICEHIDE
#define JOYTYPE_DEVICEHIDE 0x00010000l
#endif
#ifndef JOYTYPE_ENABLEINPUTREPORT
#define JOYTYPE_ENABLEINPUTREPORT 0x01000000l
#endif
#ifndef JOYTYPE_GAMEHIDE
#define JOYTYPE_GAMEHIDE 0x00080000l
#endif
#ifndef JOYTYPE_HIDEACTIVE
#define JOYTYPE_HIDEACTIVE 0x00100000l
#endif
#ifndef JOYTYPE_INFODEFAULT
#define JOYTYPE_INFODEFAULT 0x00000000l
#endif
#ifndef JOYTYPE_INFOMASK
#define JOYTYPE_INFOMASK 0x00E00000l
#endif
#ifndef JOYTYPE_INFOYRPEDALS
#define JOYTYPE_INFOYRPEDALS 0x00600000l
#endif
#ifndef JOYTYPE_INFOYYPEDALS
#define JOYTYPE_INFOYYPEDALS 0x00200000l
#endif
#ifndef JOYTYPE_INFOZISSLIDER
#define JOYTYPE_INFOZISSLIDER 0x00200000l
#endif
#ifndef JOYTYPE_INFOZISZ
#define JOYTYPE_INFOZISZ 0x00400000l
#endif
#ifndef JOYTYPE_INFOZRPEDALS
#define JOYTYPE_INFOZRPEDALS 0x00800000l
#endif
#ifndef JOYTYPE_INFOZYPEDALS
#define JOYTYPE_INFOZYPEDALS 0x00400000l
#endif
#ifndef JOYTYPE_KEYBHIDE
#define JOYTYPE_KEYBHIDE 0x00040000l
#endif
#ifndef JOYTYPE_MOUSEHIDE
#define JOYTYPE_MOUSEHIDE 0x00020000l
#endif
#ifndef JOYTYPE_NOAUTODETECTGAMEPORT
#define JOYTYPE_NOAUTODETECTGAMEPORT 0x00000002l
#endif
#ifndef JOYTYPE_NOHIDDIRECT
#define JOYTYPE_NOHIDDIRECT 0x00000004l
#endif
#ifndef JOYTYPE_ZEROGAMEENUMOEMDATA
#define JOYTYPE_ZEROGAMEENUMOEMDATA 0x00000001l
#endif
#ifndef JOY_HW_TWO_2A_2B_WITH_Y
#define JOY_HW_TWO_2A_2B_WITH_Y 12
#endif
#ifndef JOY_US_RESERVED
#define JOY_US_RESERVED 0x80000000l
#endif

typedef struct DIDEVICESTATE {
  DWORD dwSize;
  DWORD dwState;
  DWORD dwLoad;
} DIDEVICESTATE, *LPDIDEVICESTATE;

typedef struct DIDRIVERVERSIONS {
  DWORD dwSize;
  DWORD dwFirmwareRevision;
  DWORD dwHardwareRevision;
  DWORD dwFFDriverVersion;
} DIDRIVERVERSIONS, *LPDIDRIVERVERSIONS;

typedef struct DIEFFECTATTRIBUTES {
  DWORD dwEffectId;
  DWORD dwEffType;
  DWORD dwStaticParams;
  DWORD dwDynamicParams;
  DWORD dwCoords;
} DIEFFECTATTRIBUTES, *LPDIEFFECTATTRIBUTES;

typedef struct DIFFDEVICEATTRIBUTES {
  DWORD dwFlags;
  DWORD dwFFSamplePeriod;
  DWORD dwFFMinTimeResolution;
} DIFFDEVICEATTRIBUTES, *LPDIFFDEVICEATTRIBUTES;

typedef struct DIFFOBJECTATTRIBUTES {
  DWORD dwFFMaxForce;
  DWORD dwFFForceResolution;
} DIFFOBJECTATTRIBUTES, *LPDIFFOBJECTATTRIBUTES;

typedef struct DIHIDFFINITINFO {
  DWORD dwSize;
  LPWSTR pwszDeviceInterface;
  GUID GuidInstance;
} DIHIDFFINITINFO, *LPDIHIDFFINITINFO;

typedef struct DIOBJECTATTRIBUTES {
  DWORD dwFlags;
  WORD wUsagePage;
  WORD wUsage;
} DIOBJECTATTRIBUTES, *LPDIOBJECTATTRIBUTES;

typedef struct DIOBJECTCALIBRATION {
  LONG lMin;
  LONG lCenter;
  LONG lMax;
} DIOBJECTCALIBRATION, *LPDIOBJECTCALIBRATION;

typedef struct DIPOVCALIBRATION {
  LONG lMin[5];
  LONG lMax[5];
} DIPOVCALIBRATION, *LPDIPOVCALIBRATION;

#define DEV_STS_EFFECT_RUNNING DIEGES_PLAYING
#define DIMSGWP_NEWAPPSTART 0x00000001
#ifdef UNICODE
#define DIRECTINPUT_NOTIFICATION_MSGSTRING DIRECTINPUT_NOTIFICATION_MSGSTRINGW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DIRECTINPUT_NOTIFICATION_MSGSTRING DIRECTINPUT_NOTIFICATION_MSGSTRINGW
#else
#define DIRECTINPUT_NOTIFICATION_MSGSTRING DIRECTINPUT_NOTIFICATION_MSGSTRINGA
#endif
#endif
#endif
#define DIRECTINPUT_NOTIFICATION_MSGSTRINGA "DIRECTINPUT_NOTIFICATION_MSGSTRING"
#define DIRECTINPUT_NOTIFICATION_MSGSTRINGW L"DIRECTINPUT_NOTIFICATION_MSGSTRING"
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_KEY_LASTAPP DIRECTINPUT_REGSTR_KEY_LASTAPPW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_KEY_LASTAPP DIRECTINPUT_REGSTR_KEY_LASTAPPW
#else
#define DIRECTINPUT_REGSTR_KEY_LASTAPP DIRECTINPUT_REGSTR_KEY_LASTAPPA
#endif
#endif
#endif
#define DIRECTINPUT_REGSTR_KEY_LASTAPPA "MostRecentApplication"
#define DIRECTINPUT_REGSTR_KEY_LASTAPPW L"MostRecentApplication"
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_KEY_LASTMAPAPP DIRECTINPUT_REGSTR_KEY_LASTMAPAPPW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_KEY_LASTMAPAPP DIRECTINPUT_REGSTR_KEY_LASTMAPAPPW
#else
#define DIRECTINPUT_REGSTR_KEY_LASTMAPAPP DIRECTINPUT_REGSTR_KEY_LASTMAPAPPA
#endif
#endif
#endif
#define DIRECTINPUT_REGSTR_KEY_LASTMAPAPPA "MostRecentMapperApplication"
#define DIRECTINPUT_REGSTR_KEY_LASTMAPAPPW L"MostRecentMapperApplication"
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_APPIDFLAG DIRECTINPUT_REGSTR_VAL_APPIDFLAGW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_APPIDFLAG DIRECTINPUT_REGSTR_VAL_APPIDFLAGW
#else
#define DIRECTINPUT_REGSTR_VAL_APPIDFLAG DIRECTINPUT_REGSTR_VAL_APPIDFLAGA
#endif
#endif
#endif
#define DIRECTINPUT_REGSTR_VAL_APPIDFLAGA "AppIdFlag"
#define DIRECTINPUT_REGSTR_VAL_APPIDFLAGW L"AppIdFlag"
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_ID DIRECTINPUT_REGSTR_VAL_IDW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_ID DIRECTINPUT_REGSTR_VAL_IDW
#else
#define DIRECTINPUT_REGSTR_VAL_ID DIRECTINPUT_REGSTR_VAL_IDA
#endif
#endif
#endif
#define DIRECTINPUT_REGSTR_VAL_IDA "Id"
#define DIRECTINPUT_REGSTR_VAL_IDW L"Id"
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_LASTSTART DIRECTINPUT_REGSTR_VAL_LASTSTARTW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_LASTSTART DIRECTINPUT_REGSTR_VAL_LASTSTARTW
#else
#define DIRECTINPUT_REGSTR_VAL_LASTSTART DIRECTINPUT_REGSTR_VAL_LASTSTARTA
#endif
#endif
#endif
#define DIRECTINPUT_REGSTR_VAL_LASTSTARTA "MostRecentStart"
#define DIRECTINPUT_REGSTR_VAL_LASTSTARTW L"MostRecentStart"
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_MAPPER DIRECTINPUT_REGSTR_VAL_MAPPERW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_MAPPER DIRECTINPUT_REGSTR_VAL_MAPPERW
#else
#define DIRECTINPUT_REGSTR_VAL_MAPPER DIRECTINPUT_REGSTR_VAL_MAPPERA
#endif
#endif
#endif
#define DIRECTINPUT_REGSTR_VAL_MAPPERA "UsesMapper"
#define DIRECTINPUT_REGSTR_VAL_MAPPERW L"UsesMapper"
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_NAME DIRECTINPUT_REGSTR_VAL_NAMEW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_NAME DIRECTINPUT_REGSTR_VAL_NAMEW
#else
#define DIRECTINPUT_REGSTR_VAL_NAME DIRECTINPUT_REGSTR_VAL_NAMEA
#endif
#endif
#endif
#define DIRECTINPUT_REGSTR_VAL_NAMEA "Name"
#define DIRECTINPUT_REGSTR_VAL_NAMEW L"Name"
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_VERSION DIRECTINPUT_REGSTR_VAL_VERSIONW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DIRECTINPUT_REGSTR_VAL_VERSION DIRECTINPUT_REGSTR_VAL_VERSIONW
#else
#define DIRECTINPUT_REGSTR_VAL_VERSION DIRECTINPUT_REGSTR_VAL_VERSIONA
#endif
#endif
#endif
#define DIRECTINPUT_REGSTR_VAL_VERSIONA "Version"
#define DIRECTINPUT_REGSTR_VAL_VERSIONW L"Version"

typedef struct IDirectInputEffectDriverVtbl IDirectInputEffectDriverVtbl;

struct IDirectInputEffectDriver
{
    struct IDirectInputEffectDriverVtbl *lpVtbl;
};

typedef struct IDirectInputJoyConfig8Vtbl IDirectInputJoyConfig8Vtbl;

typedef struct IDirectInputJoyConfigVtbl IDirectInputJoyConfigVtbl;

struct IDirectInputJoyConfig
{
    struct IDirectInputJoyConfigVtbl *lpVtbl;
};

typedef const DIDRIVERVERSIONS *LPCDIDRIVERVERSIONS;

typedef const DIEFFECTATTRIBUTES *LPCDIEFFECTATTRIBUTES;

typedef const DIFFDEVICEATTRIBUTES *LPCDIFFDEVICEATTRIBUTES;

typedef const DIFFOBJECTATTRIBUTES *LPCDIFFOBJECTATTRIBUTES;

typedef const DIOBJECTATTRIBUTES *LPCDIOBJECTATTRIBUTES;

typedef const DIOBJECTCALIBRATION *LPCDIOBJECTCALIBRATION;

typedef const DIPOVCALIBRATION *LPCDIPOVCALIBRATION;

typedef struct IDirectInputEffectDriver *LPDIRECTINPUTEFFECTDRIVER;

typedef struct IDirectInputJoyConfig *LPDIRECTINPUTJOYCONFIG;

#define IDirectInputEffectDriver_AddRef(p) (p) -> lpVtbl -> AddRef(p)
#define IDirectInputEffectDriver_DestroyEffect(p, a, b) (p) -> lpVtbl -> DestroyEffect(p, a, b)
#define IDirectInputEffectDriver_DeviceID(p, a, b, c, d, e) (p) -> lpVtbl -> DeviceID(p, a, b, c, d, e)
#define IDirectInputEffectDriver_DownloadEffect(p, a, b, c, d, e) (p) -> lpVtbl -> DownloadEffect(p, a, b, c, d, e)
#define IDirectInputEffectDriver_Escape(p, a, b, c) (p) -> lpVtbl -> Escape(p, a, b, c)
#define IDirectInputEffectDriver_GetEffectStatus(p, a, b, c) (p) -> lpVtbl -> GetEffectStatus(p, a, b, c)
#define IDirectInputEffectDriver_GetForceFeedbackState(p, a, b) (p) -> lpVtbl -> GetForceFeedbackState(p, a, b)
#define IDirectInputEffectDriver_GetVersions(p, a) (p) -> lpVtbl -> GetVersions(p, a)
#define IDirectInputEffectDriver_QueryInterface(p, a, b) (p) -> lpVtbl -> QueryInterface(p, a, b)
#define IDirectInputEffectDriver_Release(p) (p) -> lpVtbl -> Release(p)
#define IDirectInputEffectDriver_SendForceFeedbackCommand(p, a, b) (p) -> lpVtbl -> SendForceFeedbackCommand(p, a, b)
#define IDirectInputEffectDriver_SetGain(p, a, b) (p) -> lpVtbl -> SetGain(p, a, b)
#define IDirectInputEffectDriver_StartEffect(p, a, b, c, d) (p) -> lpVtbl -> StartEffect(p, a, b, c, d)
#define IDirectInputEffectDriver_StopEffect(p, a, b) (p) -> lpVtbl -> StopEffect(p, a, b)
#define IDirectInputJoyConfig_Acquire(p) (p) -> lpVtbl -> Acquire(p)
#define IDirectInputJoyConfig_AddNewHardware(p, a, b) (p) -> lpVtbl -> AddNewHardware(p, a, b)
#define IDirectInputJoyConfig_AddRef(p) (p) -> lpVtbl -> AddRef(p)
#define IDirectInputJoyConfig_DeleteConfig(p, a) (p) -> lpVtbl -> DeleteConfig(p, a)
#define IDirectInputJoyConfig_DeleteType(p, a) (p) -> lpVtbl -> DeleteType(p, a)
#define IDirectInputJoyConfig_EnumTypes(p, a, b) (p) -> lpVtbl -> EnumTypes(p, a, b)
#define IDirectInputJoyConfig_GetConfig(p, a, b, c) (p) -> lpVtbl -> GetConfig(p, a, b, c)
#define IDirectInputJoyConfig_GetTypeInfo(p, a, b, c) (p) -> lpVtbl -> GetTypeInfo(p, a, b, c)
#define IDirectInputJoyConfig_GetUserValues(p, a, b) (p) -> lpVtbl -> GetUserValues(p, a, b)
#define IDirectInputJoyConfig_OpenConfigKey(p, a, b, c) (p) -> lpVtbl -> OpenConfigKey(p, a, b, c)
#define IDirectInputJoyConfig_OpenTypeKey(p, a, b, c) (p) -> lpVtbl -> OpenTypeKey(p, a, b, c)
#define IDirectInputJoyConfig_QueryInterface(p, a, b) (p) -> lpVtbl -> QueryInterface(p, a, b)
#define IDirectInputJoyConfig_Release(p) (p) -> lpVtbl -> Release(p)
#define IDirectInputJoyConfig_SendNotify(p) (p) -> lpVtbl -> SendNotify(p)
#define IDirectInputJoyConfig_SetConfig(p, a, b, c) (p) -> lpVtbl -> SetConfig(p, a, b, c)
#define IDirectInputJoyConfig_SetCooperativeLevel(p, a, b) (p) -> lpVtbl -> SetCooperativeLevel(p, a, b)
#define IDirectInputJoyConfig_SetTypeInfo(p, a, b, c) (p) -> lpVtbl -> SetTypeInfo(p, a, b, c)
#define IDirectInputJoyConfig_SetUserValues(p, a, b) (p) -> lpVtbl -> SetUserValues(p, a, b)
#define IDirectInputJoyConfig_Unacquire(p) (p) -> lpVtbl -> Unacquire(p)

#endif /* __DINPUTD_INCLUDED__ */
