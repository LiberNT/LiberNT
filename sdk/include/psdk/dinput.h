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

#ifndef __DINPUT_INCLUDED__
#define __DINPUT_INCLUDED__

#define COM_NO_WINDOWS_H
#include <objbase.h>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION	0x0800
#endif

/* Classes */
DEFINE_GUID(CLSID_DirectInput,		0x25E609E0,0xB259,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(CLSID_DirectInputDevice,	0x25E609E1,0xB259,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);

DEFINE_GUID(CLSID_DirectInput8,		0x25E609E4,0xB259,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(CLSID_DirectInputDevice8,	0x25E609E5,0xB259,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);

/* Interfaces */
DEFINE_GUID(IID_IDirectInputA,		0x89521360,0xAA8A,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(IID_IDirectInputW,		0x89521361,0xAA8A,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(IID_IDirectInput2A,		0x5944E662,0xAA8A,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(IID_IDirectInput2W,		0x5944E663,0xAA8A,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(IID_IDirectInput7A,		0x9A4CB684,0x236D,0x11D3,0x8E,0x9D,0x00,0xC0,0x4F,0x68,0x44,0xAE);
DEFINE_GUID(IID_IDirectInput7W,		0x9A4CB685,0x236D,0x11D3,0x8E,0x9D,0x00,0xC0,0x4F,0x68,0x44,0xAE);
DEFINE_GUID(IID_IDirectInput8A,		0xBF798030,0x483A,0x4DA2,0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00);
DEFINE_GUID(IID_IDirectInput8W,		0xBF798031,0x483A,0x4DA2,0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00);
DEFINE_GUID(IID_IDirectInputDeviceA,	0x5944E680,0xC92E,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(IID_IDirectInputDeviceW,	0x5944E681,0xC92E,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(IID_IDirectInputDevice2A,	0x5944E682,0xC92E,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(IID_IDirectInputDevice2W,	0x5944E683,0xC92E,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(IID_IDirectInputDevice7A,	0x57D7C6BC,0x2356,0x11D3,0x8E,0x9D,0x00,0xC0,0x4F,0x68,0x44,0xAE);
DEFINE_GUID(IID_IDirectInputDevice7W,	0x57D7C6BD,0x2356,0x11D3,0x8E,0x9D,0x00,0xC0,0x4F,0x68,0x44,0xAE);
DEFINE_GUID(IID_IDirectInputDevice8A,	0x54D41080,0xDC15,0x4833,0xA4,0x1B,0x74,0x8F,0x73,0xA3,0x81,0x79);
DEFINE_GUID(IID_IDirectInputDevice8W,	0x54D41081,0xDC15,0x4833,0xA4,0x1B,0x74,0x8F,0x73,0xA3,0x81,0x79);
DEFINE_GUID(IID_IDirectInputEffect,	0xE7E1F7C0,0x88D2,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);

/* Predefined object types */
DEFINE_GUID(GUID_XAxis,	0xA36D02E0,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_YAxis,	0xA36D02E1,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_ZAxis,	0xA36D02E2,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_RxAxis,0xA36D02F4,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_RyAxis,0xA36D02F5,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_RzAxis,0xA36D02E3,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_Slider,0xA36D02E4,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_Button,0xA36D02F0,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_Key,	0x55728220,0xD33C,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_POV,	0xA36D02F2,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_Unknown,0xA36D02F3,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);

/* Predefined product GUIDs */
DEFINE_GUID(GUID_SysMouse,	0x6F1D2B60,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_SysKeyboard,	0x6F1D2B61,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_Joystick,	0x6F1D2B70,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_SysMouseEm,	0x6F1D2B80,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_SysMouseEm2,	0x6F1D2B81,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_SysKeyboardEm,	0x6F1D2B82,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_SysKeyboardEm2,0x6F1D2B83,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);

/* predefined forcefeedback effects */
DEFINE_GUID(GUID_ConstantForce,	0x13541C20,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);
DEFINE_GUID(GUID_RampForce,	0x13541C21,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);
DEFINE_GUID(GUID_Square,	0x13541C22,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);
DEFINE_GUID(GUID_Sine,		0x13541C23,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);
DEFINE_GUID(GUID_Triangle,	0x13541C24,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);
DEFINE_GUID(GUID_SawtoothUp,	0x13541C25,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);
DEFINE_GUID(GUID_SawtoothDown,	0x13541C26,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);
DEFINE_GUID(GUID_Spring,	0x13541C27,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);
DEFINE_GUID(GUID_Damper,	0x13541C28,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);
DEFINE_GUID(GUID_Inertia,	0x13541C29,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);
DEFINE_GUID(GUID_Friction,	0x13541C2A,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);
DEFINE_GUID(GUID_CustomForce,	0x13541C2B,0x8E33,0x11D0,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35);

typedef struct IDirectInputA *LPDIRECTINPUTA;
typedef struct IDirectInputW *LPDIRECTINPUTW;
typedef struct IDirectInput2A *LPDIRECTINPUT2A;
typedef struct IDirectInput2W *LPDIRECTINPUT2W;
typedef struct IDirectInput7A *LPDIRECTINPUT7A;
typedef struct IDirectInput7W *LPDIRECTINPUT7W;
typedef struct IDirectInput8A *LPDIRECTINPUT8A;
typedef struct IDirectInput8W *LPDIRECTINPUT8W;
typedef struct IDirectInputDeviceA *LPDIRECTINPUTDEVICEA;
typedef struct IDirectInputDeviceW *LPDIRECTINPUTDEVICEW;
typedef struct IDirectInputDevice2A *LPDIRECTINPUTDEVICE2A;
typedef struct IDirectInputDevice2W *LPDIRECTINPUTDEVICE2W;
typedef struct IDirectInputDevice7A *LPDIRECTINPUTDEVICE7A;
typedef struct IDirectInputDevice7W *LPDIRECTINPUTDEVICE7W;
typedef struct IDirectInputDevice8A *LPDIRECTINPUTDEVICE8A;
typedef struct IDirectInputDevice8W *LPDIRECTINPUTDEVICE8W;
typedef struct IDirectInputEffect *LPDIRECTINPUTEFFECT;
typedef struct SysKeyboardA *LPSYSKEYBOARDA;
typedef struct SysMouseA *LPSYSMOUSEA;

#define IID_IDirectInput WINELIB_NAME_AW(IID_IDirectInput)
DECL_WINELIB_TYPE_AW(LPDIRECTINPUT)
#define IID_IDirectInput2 WINELIB_NAME_AW(IID_IDirectInput2)
DECL_WINELIB_TYPE_AW(LPDIRECTINPUT2)
#define IID_IDirectInput7 WINELIB_NAME_AW(IID_IDirectInput7)
DECL_WINELIB_TYPE_AW(LPDIRECTINPUT7)
#define IID_IDirectInput8 WINELIB_NAME_AW(IID_IDirectInput8)
DECL_WINELIB_TYPE_AW(LPDIRECTINPUT8)
#define IID_IDirectInputDevice WINELIB_NAME_AW(IID_IDirectInputDevice)
DECL_WINELIB_TYPE_AW(LPDIRECTINPUTDEVICE)
#define IID_IDirectInputDevice2 WINELIB_NAME_AW(IID_IDirectInputDevice2)
DECL_WINELIB_TYPE_AW(LPDIRECTINPUTDEVICE2)
#define IID_IDirectInputDevice7 WINELIB_NAME_AW(IID_IDirectInputDevice7)
DECL_WINELIB_TYPE_AW(LPDIRECTINPUTDEVICE7)
#define IID_IDirectInputDevice8 WINELIB_NAME_AW(IID_IDirectInputDevice8)
DECL_WINELIB_TYPE_AW(LPDIRECTINPUTDEVICE8)

#define DI_OK                           S_OK
#define DI_NOTATTACHED                  S_FALSE
#define DI_BUFFEROVERFLOW               S_FALSE
#define DI_PROPNOEFFECT                 S_FALSE
#define DI_NOEFFECT                     S_FALSE
#define DI_POLLEDDEVICE                 ((HRESULT)0x00000002)
#define DI_DOWNLOADSKIPPED              ((HRESULT)0x00000003)
#define DI_EFFECTRESTARTED              ((HRESULT)0x00000004)
#define DI_TRUNCATED                    ((HRESULT)0x00000008)
#define DI_SETTINGSNOTSAVED             ((HRESULT)0x0000000B)
#define DI_TRUNCATEDANDRESTARTED        ((HRESULT)0x0000000C)
#define DI_WRITEPROTECT                 ((HRESULT)0x00000013)

#define DIERR_OLDDIRECTINPUTVERSION     \
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_OLD_WIN_VERSION)
#define DIERR_BETADIRECTINPUTVERSION    \
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_RMODE_APP)
#define DIERR_BADDRIVERVER              \
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_BAD_DRIVER_LEVEL)
#define DIERR_DEVICENOTREG              REGDB_E_CLASSNOTREG
#define DIERR_NOTFOUND                  \
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_FILE_NOT_FOUND)
#define DIERR_OBJECTNOTFOUND            \
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_FILE_NOT_FOUND)
#define DIERR_INVALIDPARAM              E_INVALIDARG
#define DIERR_NOINTERFACE               E_NOINTERFACE
#define DIERR_GENERIC                   E_FAIL
#define DIERR_OUTOFMEMORY               E_OUTOFMEMORY
#define DIERR_UNSUPPORTED               E_NOTIMPL
#define DIERR_NOTINITIALIZED            \
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_NOT_READY)
#define DIERR_ALREADYINITIALIZED        \
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_ALREADY_INITIALIZED)
#define DIERR_NOAGGREGATION             CLASS_E_NOAGGREGATION
#define DIERR_OTHERAPPHASPRIO           E_ACCESSDENIED
#define DIERR_INPUTLOST                 \
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_READ_FAULT)
#define DIERR_ACQUIRED                  \
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_BUSY)
#define DIERR_NOTACQUIRED               \
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, ERROR_INVALID_ACCESS)
#define DIERR_READONLY                  E_ACCESSDENIED
#define DIERR_HANDLEEXISTS              E_ACCESSDENIED
#ifndef E_PENDING
#define E_PENDING                       __MSABI_LONG(0x8000000A)
#endif
#define DIERR_INSUFFICIENTPRIVS         __MSABI_LONG(0x80040200)
#define DIERR_DEVICEFULL                __MSABI_LONG(0x80040201)
#define DIERR_MOREDATA                  __MSABI_LONG(0x80040202)
#define DIERR_NOTDOWNLOADED             __MSABI_LONG(0x80040203)
#define DIERR_HASEFFECTS                __MSABI_LONG(0x80040204)
#define DIERR_NOTEXCLUSIVEACQUIRED      __MSABI_LONG(0x80040205)
#define DIERR_INCOMPLETEEFFECT          __MSABI_LONG(0x80040206)
#define DIERR_NOTBUFFERED               __MSABI_LONG(0x80040207)
#define DIERR_EFFECTPLAYING             __MSABI_LONG(0x80040208)
#define DIERR_UNPLUGGED                 __MSABI_LONG(0x80040209)
#define DIERR_REPORTFULL                __MSABI_LONG(0x8004020A)
#define DIERR_MAPFILEFAIL               __MSABI_LONG(0x8004020B)

#define DIENUM_STOP                     0
#define DIENUM_CONTINUE                 1

#define DIEDFL_ALLDEVICES               0x00000000
#define DIEDFL_ATTACHEDONLY             0x00000001
#define DIEDFL_FORCEFEEDBACK            0x00000100
#define DIEDFL_INCLUDEALIASES           0x00010000
#define DIEDFL_INCLUDEPHANTOMS          0x00020000
#define DIEDFL_INCLUDEHIDDEN		0x00040000

#define DIDEVTYPE_DEVICE                1
#define DIDEVTYPE_MOUSE                 2
#define DIDEVTYPE_KEYBOARD              3
#define DIDEVTYPE_JOYSTICK              4
#define DIDEVTYPE_HID                   0x00010000

#define DI8DEVCLASS_ALL             0
#define DI8DEVCLASS_DEVICE          1
#define DI8DEVCLASS_POINTER         2
#define DI8DEVCLASS_KEYBOARD        3
#define DI8DEVCLASS_GAMECTRL        4

#define DI8DEVTYPE_DEVICE           0x11
#define DI8DEVTYPE_MOUSE            0x12
#define DI8DEVTYPE_KEYBOARD         0x13
#define DI8DEVTYPE_JOYSTICK         0x14
#define DI8DEVTYPE_GAMEPAD          0x15
#define DI8DEVTYPE_DRIVING          0x16
#define DI8DEVTYPE_FLIGHT           0x17
#define DI8DEVTYPE_1STPERSON        0x18
#define DI8DEVTYPE_DEVICECTRL       0x19
#define DI8DEVTYPE_SCREENPOINTER    0x1A
#define DI8DEVTYPE_REMOTE           0x1B
#define DI8DEVTYPE_SUPPLEMENTAL     0x1C

#define DIDEVTYPEMOUSE_UNKNOWN          1
#define DIDEVTYPEMOUSE_TRADITIONAL      2
#define DIDEVTYPEMOUSE_FINGERSTICK      3
#define DIDEVTYPEMOUSE_TOUCHPAD         4
#define DIDEVTYPEMOUSE_TRACKBALL        5

#define DIDEVTYPEKEYBOARD_UNKNOWN       0
#define DIDEVTYPEKEYBOARD_PCXT          1
#define DIDEVTYPEKEYBOARD_OLIVETTI      2
#define DIDEVTYPEKEYBOARD_PCAT          3
#define DIDEVTYPEKEYBOARD_PCENH         4
#define DIDEVTYPEKEYBOARD_NOKIA1050     5
#define DIDEVTYPEKEYBOARD_NOKIA9140     6
#define DIDEVTYPEKEYBOARD_NEC98         7
#define DIDEVTYPEKEYBOARD_NEC98LAPTOP   8
#define DIDEVTYPEKEYBOARD_NEC98106      9
#define DIDEVTYPEKEYBOARD_JAPAN106     10
#define DIDEVTYPEKEYBOARD_JAPANAX      11
#define DIDEVTYPEKEYBOARD_J3100        12

#define DIDEVTYPEJOYSTICK_UNKNOWN       1
#define DIDEVTYPEJOYSTICK_TRADITIONAL   2
#define DIDEVTYPEJOYSTICK_FLIGHTSTICK   3
#define DIDEVTYPEJOYSTICK_GAMEPAD       4
#define DIDEVTYPEJOYSTICK_RUDDER        5
#define DIDEVTYPEJOYSTICK_WHEEL         6
#define DIDEVTYPEJOYSTICK_HEADTRACKER   7

#define DI8DEVTYPEMOUSE_UNKNOWN                     1
#define DI8DEVTYPEMOUSE_TRADITIONAL                 2
#define DI8DEVTYPEMOUSE_FINGERSTICK                 3
#define DI8DEVTYPEMOUSE_TOUCHPAD                    4
#define DI8DEVTYPEMOUSE_TRACKBALL                   5
#define DI8DEVTYPEMOUSE_ABSOLUTE                    6

#define DI8DEVTYPEKEYBOARD_UNKNOWN                  0
#define DI8DEVTYPEKEYBOARD_PCXT                     1
#define DI8DEVTYPEKEYBOARD_OLIVETTI                 2
#define DI8DEVTYPEKEYBOARD_PCAT                     3
#define DI8DEVTYPEKEYBOARD_PCENH                    4
#define DI8DEVTYPEKEYBOARD_NOKIA1050                5
#define DI8DEVTYPEKEYBOARD_NOKIA9140                6
#define DI8DEVTYPEKEYBOARD_NEC98                    7
#define DI8DEVTYPEKEYBOARD_NEC98LAPTOP              8
#define DI8DEVTYPEKEYBOARD_NEC98106                 9
#define DI8DEVTYPEKEYBOARD_JAPAN106                10
#define DI8DEVTYPEKEYBOARD_JAPANAX                 11
#define DI8DEVTYPEKEYBOARD_J3100                   12

#define DI8DEVTYPE_LIMITEDGAMESUBTYPE               1

#define DI8DEVTYPEJOYSTICK_LIMITED                  DI8DEVTYPE_LIMITEDGAMESUBTYPE
#define DI8DEVTYPEJOYSTICK_STANDARD                 2

#define DI8DEVTYPEGAMEPAD_LIMITED                   DI8DEVTYPE_LIMITEDGAMESUBTYPE
#define DI8DEVTYPEGAMEPAD_STANDARD                  2
#define DI8DEVTYPEGAMEPAD_TILT                      3

#define DI8DEVTYPEDRIVING_LIMITED                   DI8DEVTYPE_LIMITEDGAMESUBTYPE
#define DI8DEVTYPEDRIVING_COMBINEDPEDALS            2
#define DI8DEVTYPEDRIVING_DUALPEDALS                3
#define DI8DEVTYPEDRIVING_THREEPEDALS               4
#define DI8DEVTYPEDRIVING_HANDHELD                  5

#define DI8DEVTYPEFLIGHT_LIMITED                    DI8DEVTYPE_LIMITEDGAMESUBTYPE
#define DI8DEVTYPEFLIGHT_STICK                      2
#define DI8DEVTYPEFLIGHT_YOKE                       3
#define DI8DEVTYPEFLIGHT_RC                         4

#define DI8DEVTYPE1STPERSON_LIMITED                 DI8DEVTYPE_LIMITEDGAMESUBTYPE
#define DI8DEVTYPE1STPERSON_UNKNOWN                 2
#define DI8DEVTYPE1STPERSON_SIXDOF                  3
#define DI8DEVTYPE1STPERSON_SHOOTER                 4

#define DI8DEVTYPESCREENPTR_UNKNOWN                 2
#define DI8DEVTYPESCREENPTR_LIGHTGUN                3
#define DI8DEVTYPESCREENPTR_LIGHTPEN                4
#define DI8DEVTYPESCREENPTR_TOUCH                   5

#define DI8DEVTYPEREMOTE_UNKNOWN                    2

#define DI8DEVTYPEDEVICECTRL_UNKNOWN                2
#define DI8DEVTYPEDEVICECTRL_COMMSSELECTION         3
#define DI8DEVTYPEDEVICECTRL_COMMSSELECTION_HARDWIRED 4

#define DI8DEVTYPESUPPLEMENTAL_UNKNOWN              2
#define DI8DEVTYPESUPPLEMENTAL_2NDHANDCONTROLLER    3
#define DI8DEVTYPESUPPLEMENTAL_HEADTRACKER          4
#define DI8DEVTYPESUPPLEMENTAL_HANDTRACKER          5
#define DI8DEVTYPESUPPLEMENTAL_SHIFTSTICKGATE       6
#define DI8DEVTYPESUPPLEMENTAL_SHIFTER              7
#define DI8DEVTYPESUPPLEMENTAL_THROTTLE             8
#define DI8DEVTYPESUPPLEMENTAL_SPLITTHROTTLE        9
#define DI8DEVTYPESUPPLEMENTAL_COMBINEDPEDALS      10
#define DI8DEVTYPESUPPLEMENTAL_DUALPEDALS          11
#define DI8DEVTYPESUPPLEMENTAL_THREEPEDALS         12
#define DI8DEVTYPESUPPLEMENTAL_RUDDERPEDALS        13

#define GET_DIDEVICE_TYPE(dwDevType)     LOBYTE(dwDevType)
#define GET_DIDEVICE_SUBTYPE(dwDevType)  HIBYTE(dwDevType)

typedef struct DIDEVICEOBJECTINSTANCE_DX3A {
    DWORD   dwSize;
    GUID    guidType;
    DWORD   dwOfs;
    DWORD   dwType;
    DWORD   dwFlags;
    CHAR    tszName[MAX_PATH];
} DIDEVICEOBJECTINSTANCE_DX3A, *LPDIDEVICEOBJECTINSTANCE_DX3A;
typedef const DIDEVICEOBJECTINSTANCE_DX3A *LPCDIDEVICEOBJECTINSTANCE_DX3A;
typedef struct DIDEVICEOBJECTINSTANCE_DX3W {
    DWORD   dwSize;
    GUID    guidType;
    DWORD   dwOfs;
    DWORD   dwType;
    DWORD   dwFlags;
    WCHAR   tszName[MAX_PATH];
} DIDEVICEOBJECTINSTANCE_DX3W, *LPDIDEVICEOBJECTINSTANCE_DX3W;
typedef const DIDEVICEOBJECTINSTANCE_DX3W *LPCDIDEVICEOBJECTINSTANCE_DX3W;

DECL_WINELIB_TYPE_AW(DIDEVICEOBJECTINSTANCE_DX3)
DECL_WINELIB_TYPE_AW(LPDIDEVICEOBJECTINSTANCE_DX3)
DECL_WINELIB_TYPE_AW(LPCDIDEVICEOBJECTINSTANCE_DX3)

typedef struct DIDEVICEOBJECTINSTANCEA {
    DWORD	dwSize;
    GUID	guidType;
    DWORD	dwOfs;
    DWORD	dwType;
    DWORD	dwFlags;
    CHAR	tszName[MAX_PATH];
    DWORD	dwFFMaxForce;
    DWORD	dwFFForceResolution;
    WORD	wCollectionNumber;
    WORD	wDesignatorIndex;
    WORD	wUsagePage;
    WORD	wUsage;
    DWORD	dwDimension;
    WORD	wExponent;
    WORD	wReportId;
} DIDEVICEOBJECTINSTANCEA, *LPDIDEVICEOBJECTINSTANCEA;
typedef const DIDEVICEOBJECTINSTANCEA *LPCDIDEVICEOBJECTINSTANCEA;

typedef struct DIDEVICEOBJECTINSTANCEW {
    DWORD	dwSize;
    GUID	guidType;
    DWORD	dwOfs;
    DWORD	dwType;
    DWORD	dwFlags;
    WCHAR	tszName[MAX_PATH];
    DWORD	dwFFMaxForce;
    DWORD	dwFFForceResolution;
    WORD	wCollectionNumber;
    WORD	wDesignatorIndex;
    WORD	wUsagePage;
    WORD	wUsage;
    DWORD	dwDimension;
    WORD	wExponent;
    WORD	wReportId;
} DIDEVICEOBJECTINSTANCEW, *LPDIDEVICEOBJECTINSTANCEW;
typedef const DIDEVICEOBJECTINSTANCEW *LPCDIDEVICEOBJECTINSTANCEW;

DECL_WINELIB_TYPE_AW(DIDEVICEOBJECTINSTANCE)
DECL_WINELIB_TYPE_AW(LPDIDEVICEOBJECTINSTANCE)
DECL_WINELIB_TYPE_AW(LPCDIDEVICEOBJECTINSTANCE)

typedef struct DIDEVICEINSTANCE_DX3A {
    DWORD   dwSize;
    GUID    guidInstance;
    GUID    guidProduct;
    DWORD   dwDevType;
    CHAR    tszInstanceName[MAX_PATH];
    CHAR    tszProductName[MAX_PATH];
} DIDEVICEINSTANCE_DX3A, *LPDIDEVICEINSTANCE_DX3A;
typedef const DIDEVICEINSTANCE_DX3A *LPCDIDEVICEINSTANCE_DX3A;
typedef struct DIDEVICEINSTANCE_DX3W {
    DWORD   dwSize;
    GUID    guidInstance;
    GUID    guidProduct;
    DWORD   dwDevType;
    WCHAR   tszInstanceName[MAX_PATH];
    WCHAR   tszProductName[MAX_PATH];
} DIDEVICEINSTANCE_DX3W, *LPDIDEVICEINSTANCE_DX3W;
typedef const DIDEVICEINSTANCE_DX3W *LPCDIDEVICEINSTANCE_DX3W;

DECL_WINELIB_TYPE_AW(DIDEVICEINSTANCE_DX3)
DECL_WINELIB_TYPE_AW(LPDIDEVICEINSTANCE_DX3)
DECL_WINELIB_TYPE_AW(LPCDIDEVICEINSTANCE_DX3)

typedef struct DIDEVICEINSTANCEA {
    DWORD	dwSize;
    GUID	guidInstance;
    GUID	guidProduct;
    DWORD	dwDevType;
    CHAR	tszInstanceName[MAX_PATH];
    CHAR	tszProductName[MAX_PATH];
    GUID	guidFFDriver;
    WORD	wUsagePage;
    WORD	wUsage;
} DIDEVICEINSTANCEA, *LPDIDEVICEINSTANCEA;
typedef const DIDEVICEINSTANCEA *LPCDIDEVICEINSTANCEA;

typedef struct DIDEVICEINSTANCEW {
    DWORD	dwSize;
    GUID	guidInstance;
    GUID	guidProduct;
    DWORD	dwDevType;
    WCHAR	tszInstanceName[MAX_PATH];
    WCHAR	tszProductName[MAX_PATH];
    GUID	guidFFDriver;
    WORD	wUsagePage;
    WORD	wUsage;
} DIDEVICEINSTANCEW, *LPDIDEVICEINSTANCEW;
typedef const DIDEVICEINSTANCEW *LPCDIDEVICEINSTANCEW;

DECL_WINELIB_TYPE_AW(DIDEVICEINSTANCE)
DECL_WINELIB_TYPE_AW(LPDIDEVICEINSTANCE)
DECL_WINELIB_TYPE_AW(LPCDIDEVICEINSTANCE)

typedef BOOL (CALLBACK *LPDIENUMDEVICESCALLBACKA)(LPCDIDEVICEINSTANCEA,LPVOID);
typedef BOOL (CALLBACK *LPDIENUMDEVICESCALLBACKW)(LPCDIDEVICEINSTANCEW,LPVOID);
DECL_WINELIB_TYPE_AW(LPDIENUMDEVICESCALLBACK)

#define DIEDBS_MAPPEDPRI1		0x00000001
#define DIEDBS_MAPPEDPRI2		0x00000002
#define DIEDBS_RECENTDEVICE		0x00000010
#define DIEDBS_NEWDEVICE		0x00000020

#define DIEDBSFL_ATTACHEDONLY		0x00000000
#define DIEDBSFL_THISUSER		0x00000010
#define DIEDBSFL_FORCEFEEDBACK		DIEDFL_FORCEFEEDBACK
#define DIEDBSFL_AVAILABLEDEVICES	0x00001000
#define DIEDBSFL_MULTIMICEKEYBOARDS	0x00002000
#define DIEDBSFL_NONGAMINGDEVICES	0x00004000
#define DIEDBSFL_VALID			0x00007110

typedef BOOL (CALLBACK *LPDIENUMDEVICESBYSEMANTICSCBA)(LPCDIDEVICEINSTANCEA,LPDIRECTINPUTDEVICE8A,DWORD,DWORD,LPVOID);
typedef BOOL (CALLBACK *LPDIENUMDEVICESBYSEMANTICSCBW)(LPCDIDEVICEINSTANCEW,LPDIRECTINPUTDEVICE8W,DWORD,DWORD,LPVOID);
DECL_WINELIB_TYPE_AW(LPDIENUMDEVICESBYSEMANTICSCB)

typedef BOOL (CALLBACK *LPDICONFIGUREDEVICESCALLBACK)(LPUNKNOWN,LPVOID);

typedef BOOL (CALLBACK *LPDIENUMDEVICEOBJECTSCALLBACKA)(LPCDIDEVICEOBJECTINSTANCEA,LPVOID);
typedef BOOL (CALLBACK *LPDIENUMDEVICEOBJECTSCALLBACKW)(LPCDIDEVICEOBJECTINSTANCEW,LPVOID);
DECL_WINELIB_TYPE_AW(LPDIENUMDEVICEOBJECTSCALLBACK)

typedef BOOL (CALLBACK *LPDIENUMCREATEDEFFECTOBJECTSCALLBACK)(LPDIRECTINPUTEFFECT, LPVOID);

#define DIK_ESCAPE          0x01
#define DIK_1               0x02
#define DIK_2               0x03
#define DIK_3               0x04
#define DIK_4               0x05
#define DIK_5               0x06
#define DIK_6               0x07
#define DIK_7               0x08
#define DIK_8               0x09
#define DIK_9               0x0A
#define DIK_0               0x0B
#define DIK_MINUS           0x0C    /* - on main keyboard */
#define DIK_EQUALS          0x0D
#define DIK_BACK            0x0E    /* backspace */
#define DIK_TAB             0x0F
#define DIK_Q               0x10
#define DIK_W               0x11
#define DIK_E               0x12
#define DIK_R               0x13
#define DIK_T               0x14
#define DIK_Y               0x15
#define DIK_U               0x16
#define DIK_I               0x17
#define DIK_O               0x18
#define DIK_P               0x19
#define DIK_LBRACKET        0x1A
#define DIK_RBRACKET        0x1B
#define DIK_RETURN          0x1C    /* Enter on main keyboard */
#define DIK_LCONTROL        0x1D
#define DIK_A               0x1E
#define DIK_S               0x1F
#define DIK_D               0x20
#define DIK_F               0x21
#define DIK_G               0x22
#define DIK_H               0x23
#define DIK_J               0x24
#define DIK_K               0x25
#define DIK_L               0x26
#define DIK_SEMICOLON       0x27
#define DIK_APOSTROPHE      0x28
#define DIK_GRAVE           0x29    /* accent grave */
#define DIK_LSHIFT          0x2A
#define DIK_BACKSLASH       0x2B
#define DIK_Z               0x2C
#define DIK_X               0x2D
#define DIK_C               0x2E
#define DIK_V               0x2F
#define DIK_B               0x30
#define DIK_N               0x31
#define DIK_M               0x32
#define DIK_COMMA           0x33
#define DIK_PERIOD          0x34    /* . on main keyboard */
#define DIK_SLASH           0x35    /* / on main keyboard */
#define DIK_RSHIFT          0x36
#define DIK_MULTIPLY        0x37    /* * on numeric keypad */
#define DIK_LMENU           0x38    /* left Alt */
#define DIK_SPACE           0x39
#define DIK_CAPITAL         0x3A
#define DIK_F1              0x3B
#define DIK_F2              0x3C
#define DIK_F3              0x3D
#define DIK_F4              0x3E
#define DIK_F5              0x3F
#define DIK_F6              0x40
#define DIK_F7              0x41
#define DIK_F8              0x42
#define DIK_F9              0x43
#define DIK_F10             0x44
#define DIK_NUMLOCK         0x45
#define DIK_SCROLL          0x46    /* Scroll Lock */
#define DIK_NUMPAD7         0x47
#define DIK_NUMPAD8         0x48
#define DIK_NUMPAD9         0x49
#define DIK_SUBTRACT        0x4A    /* - on numeric keypad */
#define DIK_NUMPAD4         0x4B
#define DIK_NUMPAD5         0x4C
#define DIK_NUMPAD6         0x4D
#define DIK_ADD             0x4E    /* + on numeric keypad */
#define DIK_NUMPAD1         0x4F
#define DIK_NUMPAD2         0x50
#define DIK_NUMPAD3         0x51
#define DIK_NUMPAD0         0x52
#define DIK_DECIMAL         0x53    /* . on numeric keypad */
#define DIK_F11             0x57
#define DIK_F12             0x58
#define DIK_F13             0x64    /*                     (NEC PC98) */
#define DIK_F14             0x65    /*                     (NEC PC98) */
#define DIK_F15             0x66    /*                     (NEC PC98) */
#define DIK_KANA            0x70    /* (Japanese keyboard)            */
#define DIK_CONVERT         0x79    /* (Japanese keyboard)            */
#define DIK_NOCONVERT       0x7B    /* (Japanese keyboard)            */
#define DIK_YEN             0x7D    /* (Japanese keyboard)            */
#define DIK_NUMPADEQUALS    0x8D    /* = on numeric keypad (NEC PC98) */
#define DIK_CIRCUMFLEX      0x90    /* (Japanese keyboard)            */
#define DIK_AT              0x91    /*                     (NEC PC98) */
#define DIK_COLON           0x92    /*                     (NEC PC98) */
#define DIK_UNDERLINE       0x93    /*                     (NEC PC98) */
#define DIK_KANJI           0x94    /* (Japanese keyboard)            */
#define DIK_STOP            0x95    /*                     (NEC PC98) */
#define DIK_AX              0x96    /*                     (Japan AX) */
#define DIK_UNLABELED       0x97    /*                        (J3100) */
#define DIK_NUMPADENTER     0x9C    /* Enter on numeric keypad */
#define DIK_RCONTROL        0x9D
#define DIK_NUMPADCOMMA     0xB3    /* , on numeric keypad (NEC PC98) */
#define DIK_DIVIDE          0xB5    /* / on numeric keypad */
#define DIK_SYSRQ           0xB7
#define DIK_RMENU           0xB8    /* right Alt */
#define DIK_PAUSE           0xC5    /* Pause */
#define DIK_HOME            0xC7    /* Home on arrow keypad */
#define DIK_UP              0xC8    /* UpArrow on arrow keypad */
#define DIK_PRIOR           0xC9    /* PgUp on arrow keypad */
#define DIK_LEFT            0xCB    /* LeftArrow on arrow keypad */
#define DIK_RIGHT           0xCD    /* RightArrow on arrow keypad */
#define DIK_END             0xCF    /* End on arrow keypad */
#define DIK_DOWN            0xD0    /* DownArrow on arrow keypad */
#define DIK_NEXT            0xD1    /* PgDn on arrow keypad */
#define DIK_INSERT          0xD2    /* Insert on arrow keypad */
#define DIK_DELETE          0xD3    /* Delete on arrow keypad */
#define DIK_LWIN            0xDB    /* Left Windows key */
#define DIK_RWIN            0xDC    /* Right Windows key */
#define DIK_APPS            0xDD    /* AppMenu key */
#define DIK_POWER           0xDE
#define DIK_SLEEP           0xDF
#define DIK_BACKSPACE       DIK_BACK            /* backspace */
#define DIK_NUMPADSTAR      DIK_MULTIPLY        /* * on numeric keypad */
#define DIK_LALT            DIK_LMENU           /* left Alt */
#define DIK_CAPSLOCK        DIK_CAPITAL         /* CapsLock */
#define DIK_NUMPADMINUS     DIK_SUBTRACT        /* - on numeric keypad */
#define DIK_NUMPADPLUS      DIK_ADD             /* + on numeric keypad */
#define DIK_NUMPADPERIOD    DIK_DECIMAL         /* . on numeric keypad */
#define DIK_NUMPADSLASH     DIK_DIVIDE          /* / on numeric keypad */
#define DIK_RALT            DIK_RMENU           /* right Alt */
#define DIK_UPARROW         DIK_UP              /* UpArrow on arrow keypad */
#define DIK_PGUP            DIK_PRIOR           /* PgUp on arrow keypad */
#define DIK_LEFTARROW       DIK_LEFT            /* LeftArrow on arrow keypad */
#define DIK_RIGHTARROW      DIK_RIGHT           /* RightArrow on arrow keypad */
#define DIK_DOWNARROW       DIK_DOWN            /* DownArrow on arrow keypad */
#define DIK_PGDN            DIK_NEXT            /* PgDn on arrow keypad */

/* New DirectInput8 style keyboard constants */

#define DIKEYBOARD_ESCAPE               (DIK_ESCAPE | 0x81000400)
#define DIKEYBOARD_1                    (DIK_1 | 0x81000400)
#define DIKEYBOARD_2                    (DIK_2 | 0x81000400)
#define DIKEYBOARD_3                    (DIK_3 | 0x81000400)
#define DIKEYBOARD_4                    (DIK_4 | 0x81000400)
#define DIKEYBOARD_5                    (DIK_5 | 0x81000400)
#define DIKEYBOARD_6                    (DIK_6 | 0x81000400)
#define DIKEYBOARD_7                    (DIK_7 | 0x81000400)
#define DIKEYBOARD_8                    (DIK_8 | 0x81000400)
#define DIKEYBOARD_9                    (DIK_9 | 0x81000400)
#define DIKEYBOARD_0                    (DIK_0 | 0x81000400)
#define DIKEYBOARD_MINUS                (DIK_MINUS | 0x81000400)
#define DIKEYBOARD_EQUALS               (DIK_EQUALS | 0x81000400)
#define DIKEYBOARD_BACK                 (DIK_BACK | 0x81000400)
#define DIKEYBOARD_TAB                  (DIK_TAB | 0x81000400)
#define DIKEYBOARD_Q                    (DIK_Q | 0x81000400)
#define DIKEYBOARD_W                    (DIK_W | 0x81000400)
#define DIKEYBOARD_E                    (DIK_E | 0x81000400)
#define DIKEYBOARD_R                    (DIK_R | 0x81000400)
#define DIKEYBOARD_T                    (DIK_T | 0x81000400)
#define DIKEYBOARD_Y                    (DIK_Y | 0x81000400)
#define DIKEYBOARD_U                    (DIK_U | 0x81000400)
#define DIKEYBOARD_I                    (DIK_I | 0x81000400)
#define DIKEYBOARD_O                    (DIK_O | 0x81000400)
#define DIKEYBOARD_P                    (DIK_P | 0x81000400)
#define DIKEYBOARD_LBRACKET             (DIK_LBRACKET | 0x81000400)
#define DIKEYBOARD_RBRACKET             (DIK_RBRACKET | 0x81000400)
#define DIKEYBOARD_RETURN               (DIK_RETURN | 0x81000400)
#define DIKEYBOARD_LCONTROL             (DIK_LCONTROL | 0x81000400)
#define DIKEYBOARD_A                    (DIK_A | 0x81000400)
#define DIKEYBOARD_S                    (DIK_S | 0x81000400)
#define DIKEYBOARD_D                    (DIK_D | 0x81000400)
#define DIKEYBOARD_F                    (DIK_F | 0x81000400)
#define DIKEYBOARD_G                    (DIK_G | 0x81000400)
#define DIKEYBOARD_H                    (DIK_H | 0x81000400)
#define DIKEYBOARD_J                    (DIK_J | 0x81000400)
#define DIKEYBOARD_K                    (DIK_K | 0x81000400)
#define DIKEYBOARD_L                    (DIK_L | 0x81000400)
#define DIKEYBOARD_SEMICOLON            (DIK_SEMICOLON | 0x81000400)
#define DIKEYBOARD_APOSTROPHE           (DIK_APOSTROPHE | 0x81000400)
#define DIKEYBOARD_GRAVE                (DIK_GRAVE | 0x81000400)
#define DIKEYBOARD_LSHIFT               (DIK_LSHIFT | 0x81000400)
#define DIKEYBOARD_BACKSLASH            (DIK_BACKSLASH | 0x81000400)
#define DIKEYBOARD_Z                    (DIK_Z | 0x81000400)
#define DIKEYBOARD_X                    (DIK_X | 0x81000400)
#define DIKEYBOARD_C                    (DIK_C | 0x81000400)
#define DIKEYBOARD_V                    (DIK_V | 0x81000400)
#define DIKEYBOARD_B                    (DIK_B | 0x81000400)
#define DIKEYBOARD_N                    (DIK_N | 0x81000400)
#define DIKEYBOARD_M                    (DIK_M | 0x81000400)
#define DIKEYBOARD_COMMA                (DIK_COMMA | 0x81000400)
#define DIKEYBOARD_PERIOD               (DIK_PERIOD | 0x81000400)
#define DIKEYBOARD_SLASH                (DIK_SLASH | 0x81000400)
#define DIKEYBOARD_RSHIFT               (DIK_RSHIFT | 0x81000400)
#define DIKEYBOARD_MULTIPLY             (DIK_MULTIPLY | 0x81000400)
#define DIKEYBOARD_LMENU                (DIK_LMENU | 0x81000400)
#define DIKEYBOARD_SPACE                (DIK_SPACE | 0x81000400)
#define DIKEYBOARD_CAPITAL              (DIK_CAPITAL | 0x81000400)
#define DIKEYBOARD_F1                   (DIK_F1 | 0x81000400)
#define DIKEYBOARD_F2                   (DIK_F2 | 0x81000400)
#define DIKEYBOARD_F3                   (DIK_F3 | 0x81000400)
#define DIKEYBOARD_F4                   (DIK_F4 | 0x81000400)
#define DIKEYBOARD_F5                   (DIK_F5 | 0x81000400)
#define DIKEYBOARD_F6                   (DIK_F6 | 0x81000400)
#define DIKEYBOARD_F7                   (DIK_F7 | 0x81000400)
#define DIKEYBOARD_F8                   (DIK_F8 | 0x81000400)
#define DIKEYBOARD_F9                   (DIK_F9 | 0x81000400)
#define DIKEYBOARD_F10                  (DIK_F10 | 0x81000400)
#define DIKEYBOARD_NUMLOCK              (DIK_NUMLOCK | 0x81000400)
#define DIKEYBOARD_SCROLL               (DIK_SCROLL | 0x81000400)
#define DIKEYBOARD_NUMPAD7              (DIK_NUMPAD7 | 0x81000400)
#define DIKEYBOARD_NUMPAD8              (DIK_NUMPAD8 | 0x81000400)
#define DIKEYBOARD_NUMPAD9              (DIK_NUMPAD9 | 0x81000400)
#define DIKEYBOARD_SUBTRACT             (DIK_SUBTRACT | 0x81000400)
#define DIKEYBOARD_NUMPAD4              (DIK_NUMPAD4 | 0x81000400)
#define DIKEYBOARD_NUMPAD5              (DIK_NUMPAD5 | 0x81000400)
#define DIKEYBOARD_NUMPAD6              (DIK_NUMPAD6 | 0x81000400)
#define DIKEYBOARD_ADD                  (DIK_ADD | 0x81000400)
#define DIKEYBOARD_NUMPAD1              (DIK_NUMPAD1 | 0x81000400)
#define DIKEYBOARD_NUMPAD2              (DIK_NUMPAD2 | 0x81000400)
#define DIKEYBOARD_NUMPAD3              (DIK_NUMPAD3 | 0x81000400)
#define DIKEYBOARD_NUMPAD0              (DIK_NUMPAD0 | 0x81000400)
#define DIKEYBOARD_DECIMAL              (DIK_DECIMAL | 0x81000400)
#define DIKEYBOARD_F11                  (DIK_F11 | 0x81000400)
#define DIKEYBOARD_F12                  (DIK_F12 | 0x81000400)
#define DIKEYBOARD_F13                  (DIK_F13 | 0x81000400)
#define DIKEYBOARD_F14                  (DIK_F14 | 0x81000400)
#define DIKEYBOARD_F15                  (DIK_F15 | 0x81000400)
#define DIKEYBOARD_KANA                 (DIK_KANA | 0x81000400)
#define DIKEYBOARD_CONVERT              (DIK_CONVERT | 0x81000400)
#define DIKEYBOARD_NOCONVERT            (DIK_NOCONVERT | 0x81000400)
#define DIKEYBOARD_YEN                  (DIK_YEN | 0x81000400)
#define DIKEYBOARD_NUMPADEQUALS         (DIK_NUMPADEQUALS | 0x81000400)
#define DIKEYBOARD_CIRCUMFLEX           (DIK_CIRCUMFLEX | 0x81000400)
#define DIKEYBOARD_AT                   (DIK_AT | 0x81000400)
#define DIKEYBOARD_COLON                (DIK_COLON | 0x81000400)
#define DIKEYBOARD_UNDERLINE            (DIK_UNDERLINE | 0x81000400)
#define DIKEYBOARD_KANJI                (DIK_KANJI | 0x81000400)
#define DIKEYBOARD_STOP                 (DIK_STOP | 0x81000400)
#define DIKEYBOARD_AX                   (DIK_AX | 0x81000400)
#define DIKEYBOARD_UNLABELED            (DIK_UNLABELED | 0x81000400)
#define DIKEYBOARD_NUMPADENTER          (DIK_NUMPADENTER | 0x81000400)
#define DIKEYBOARD_RCONTROL             (DIK_RCONTROL | 0x81000400)
#define DIKEYBOARD_NUMPADCOMMA          (DIK_NUMPADCOMMA | 0x81000400)
#define DIKEYBOARD_DIVIDE               (DIK_DIVIDE | 0x81000400)
#define DIKEYBOARD_SYSRQ                (DIK_SYSRQ | 0x81000400)
#define DIKEYBOARD_RMENU                (DIK_RMENU | 0x81000400)
#define DIKEYBOARD_PAUSE                (DIK_PAUSE | 0x81000400)
#define DIKEYBOARD_HOME                 (DIK_HOME | 0x81000400)
#define DIKEYBOARD_UP                   (DIK_UP | 0x81000400)
#define DIKEYBOARD_PRIOR                (DIK_PRIOR | 0x81000400)
#define DIKEYBOARD_LEFT                 (DIK_LEFT | 0x81000400)
#define DIKEYBOARD_RIGHT                (DIK_RIGHT | 0x81000400)
#define DIKEYBOARD_END                  (DIK_END | 0x81000400)
#define DIKEYBOARD_DOWN                 (DIK_DOWN | 0x81000400)
#define DIKEYBOARD_NEXT                 (DIK_NEXT | 0x81000400)
#define DIKEYBOARD_INSERT               (DIK_INSERT | 0x81000400)
#define DIKEYBOARD_DELETE               (DIK_DELETE | 0x81000400)
#define DIKEYBOARD_LWIN                 (DIK_LWIN | 0x81000400)
#define DIKEYBOARD_RWIN                 (DIK_RWIN | 0x81000400)
#define DIKEYBOARD_APPS                 (DIK_APPS | 0x81000400)
#define DIKEYBOARD_POWER                (DIK_POWER | 0x81000400)
#define DIKEYBOARD_SLEEP                (DIK_SLEEP | 0x81000400)
#define DIKEYBOARD_BACKSPACE            (DIK_BACKSPACE | 0x81000400)
#define DIKEYBOARD_NUMPADSTAR           (DIK_NUMPADSTAR | 0x81000400)
#define DIKEYBOARD_LALT                 (DIK_LALT | 0x81000400)
#define DIKEYBOARD_CAPSLOCK             (DIK_CAPSLOCK | 0x81000400)
#define DIKEYBOARD_NUMPADMINUS          (DIK_NUMPADMINUS | 0x81000400)
#define DIKEYBOARD_NUMPADPLUS           (DIK_NUMPADPLUS | 0x81000400)
#define DIKEYBOARD_NUMPADPERIOD         (DIK_NUMPADPERIOD | 0x81000400)
#define DIKEYBOARD_NUMPADSLASH          (DIK_NUMPADSLASH | 0x81000400)
#define DIKEYBOARD_RALT                 (DIK_RALT | 0x81000400)
#define DIKEYBOARD_UPARROW              (DIK_UPARROW | 0x81000400)
#define DIKEYBOARD_PGUP                 (DIK_PGUP | 0x81000400)
#define DIKEYBOARD_LEFTARROW            (DIK_LEFTARROW | 0x81000400)
#define DIKEYBOARD_RIGHTARROW           (DIK_RIGHTARROW | 0x81000400)
#define DIKEYBOARD_DOWNARROW            (DIK_DOWNARROW | 0x81000400)
#define DIKEYBOARD_PGDN                 (DIK_PGDN | 0x81000400)

#define DIDFT_ALL		0x00000000
#define DIDFT_RELAXIS		0x00000001
#define DIDFT_ABSAXIS		0x00000002
#define DIDFT_AXIS		0x00000003
#define DIDFT_PSHBUTTON		0x00000004
#define DIDFT_TGLBUTTON		0x00000008
#define DIDFT_BUTTON		0x0000000C
#define DIDFT_POV		0x00000010
#define DIDFT_COLLECTION	0x00000040
#define DIDFT_NODATA		0x00000080
#define DIDFT_ANYINSTANCE	0x00FFFF00
#define DIDFT_INSTANCEMASK	DIDFT_ANYINSTANCE
#define DIDFT_MAKEINSTANCE(n)	((WORD)(n) << 8)
#define DIDFT_GETTYPE(n)	LOBYTE(n)
#define DIDFT_GETINSTANCE(n)	LOWORD((n) >> 8)
#define DIDFT_FFACTUATOR	0x01000000
#define DIDFT_FFEFFECTTRIGGER	0x02000000
#define DIDFT_OUTPUT		0x10000000
#define DIDFT_VENDORDEFINED	0x04000000
#define DIDFT_ALIAS		0x08000000
#ifndef DIDFT_OPTIONAL
#define DIDFT_OPTIONAL		0x80000000
#endif
#define DIDFT_ENUMCOLLECTION(n)	((WORD)(n) << 8)
#define DIDFT_NOCOLLECTION	0x00FFFF00
#define DIDF_ABSAXIS		0x00000001
#define DIDF_RELAXIS		0x00000002

#define DIGDD_PEEK		0x00000001

#define DISEQUENCE_COMPARE(dwSq1,cmp,dwSq2) ((int)((dwSq1) - (dwSq2)) cmp 0)

typedef struct DIDEVICEOBJECTDATA_DX3 {
    DWORD	dwOfs;
    DWORD	dwData;
    DWORD	dwTimeStamp;
    DWORD	dwSequence;
} DIDEVICEOBJECTDATA_DX3,*LPDIDEVICEOBJECTDATA_DX3;
typedef const DIDEVICEOBJECTDATA_DX3 *LPCDIDEVICEOBJECTDATA_DX3;

typedef struct DIDEVICEOBJECTDATA {
    DWORD	dwOfs;
    DWORD	dwData;
    DWORD	dwTimeStamp;
    DWORD	dwSequence;
    UINT_PTR	uAppData;
} DIDEVICEOBJECTDATA, *LPDIDEVICEOBJECTDATA;
typedef const DIDEVICEOBJECTDATA *LPCDIDEVICEOBJECTDATA;

typedef struct _DIOBJECTDATAFORMAT {
    const GUID *pguid;
    DWORD	dwOfs;
    DWORD	dwType;
    DWORD	dwFlags;
} DIOBJECTDATAFORMAT, *LPDIOBJECTDATAFORMAT;
typedef const DIOBJECTDATAFORMAT *LPCDIOBJECTDATAFORMAT;

typedef struct _DIDATAFORMAT {
    DWORD			dwSize;
    DWORD			dwObjSize;
    DWORD			dwFlags;
    DWORD			dwDataSize;
    DWORD			dwNumObjs;
    LPDIOBJECTDATAFORMAT	rgodf;
} DIDATAFORMAT, *LPDIDATAFORMAT;
typedef const DIDATAFORMAT *LPCDIDATAFORMAT;

#define DIDOI_FFACTUATOR	0x00000001
#define DIDOI_FFEFFECTTRIGGER	0x00000002
#define DIDOI_POLLED		0x00008000
#define DIDOI_ASPECTPOSITION	0x00000100
#define DIDOI_ASPECTVELOCITY	0x00000200
#define DIDOI_ASPECTACCEL	0x00000300
#define DIDOI_ASPECTFORCE	0x00000400
#define DIDOI_ASPECTMASK	0x00000F00
#define DIDOI_GUIDISUSAGE	0x00010000

typedef struct DIPROPHEADER {
    DWORD	dwSize;
    DWORD	dwHeaderSize;
    DWORD	dwObj;
    DWORD	dwHow;
} DIPROPHEADER,*LPDIPROPHEADER;
typedef const DIPROPHEADER *LPCDIPROPHEADER;

#define DIPH_DEVICE	0
#define DIPH_BYOFFSET	1
#define DIPH_BYID	2
#define DIPH_BYUSAGE	3

#define DIMAKEUSAGEDWORD(UsagePage, Usage) (DWORD)MAKELONG(Usage, UsagePage)

typedef struct DIPROPDWORD {
	DIPROPHEADER	diph;
	DWORD		dwData;
} DIPROPDWORD, *LPDIPROPDWORD;
typedef const DIPROPDWORD *LPCDIPROPDWORD;

typedef struct DIPROPRANGE {
	DIPROPHEADER	diph;
	LONG		lMin;
	LONG		lMax;
} DIPROPRANGE, *LPDIPROPRANGE;
typedef const DIPROPRANGE *LPCDIPROPRANGE;

#define DIPROPRANGE_NOMIN	((LONG)0x80000000)
#define DIPROPRANGE_NOMAX	((LONG)0x7FFFFFFF)

typedef struct DIPROPCAL {
	DIPROPHEADER diph;
	LONG	lMin;
	LONG	lCenter;
	LONG	lMax;
} DIPROPCAL, *LPDIPROPCAL;
typedef const DIPROPCAL *LPCDIPROPCAL;

typedef struct DIPROPGUIDANDPATH {
	DIPROPHEADER diph;
	GUID    guidClass;
	WCHAR   wszPath[MAX_PATH];
} DIPROPGUIDANDPATH, *LPDIPROPGUIDANDPATH;
typedef const DIPROPGUIDANDPATH *LPCDIPROPGUIDANDPATH;

typedef struct DIPROPSTRING {
        DIPROPHEADER diph;
        WCHAR        wsz[MAX_PATH];
} DIPROPSTRING, *LPDIPROPSTRING;
typedef const DIPROPSTRING *LPCDIPROPSTRING;

typedef struct DIPROPPOINTER {
        DIPROPHEADER diph;
        UINT_PTR uData;
} DIPROPPOINTER, *LPDIPROPPOINTER;
typedef const DIPROPPOINTER *LPCDIPROPPOINTER;

/* special property GUIDs */
#ifdef __cplusplus
#define MAKEDIPROP(prop)	(*(const GUID *)(prop))
#else
#define MAKEDIPROP(prop)	((REFGUID)(prop))
#endif
#define DIPROP_BUFFERSIZE	MAKEDIPROP(1)
#define DIPROP_AXISMODE		MAKEDIPROP(2)

#define DIPROPAXISMODE_ABS	0
#define DIPROPAXISMODE_REL	1

#define DIPROP_GRANULARITY	MAKEDIPROP(3)
#define DIPROP_RANGE		MAKEDIPROP(4)
#define DIPROP_DEADZONE		MAKEDIPROP(5)
#define DIPROP_SATURATION	MAKEDIPROP(6)
#define DIPROP_FFGAIN		MAKEDIPROP(7)
#define DIPROP_FFLOAD		MAKEDIPROP(8)
#define DIPROP_AUTOCENTER	MAKEDIPROP(9)

#define DIPROPAUTOCENTER_OFF	0
#define DIPROPAUTOCENTER_ON	1

#define DIPROP_CALIBRATIONMODE	MAKEDIPROP(10)

#define DIPROPCALIBRATIONMODE_COOKED	0
#define DIPROPCALIBRATIONMODE_RAW	1

#define DIPROP_CALIBRATION	MAKEDIPROP(11)
#define DIPROP_GUIDANDPATH	MAKEDIPROP(12)

#define DIPROP_INSTANCENAME     MAKEDIPROP(13)
#define DIPROP_PRODUCTNAME      MAKEDIPROP(14)
#define DIPROP_JOYSTICKID       MAKEDIPROP(15)

#define DIPROP_PHYSICALRANGE    MAKEDIPROP(18)
#define DIPROP_LOGICALRANGE     MAKEDIPROP(19)

#define DIPROP_KEYNAME     MAKEDIPROP(20)
#define DIPROP_CPOINTS     MAKEDIPROP(21)
#define DIPROP_APPDATA     MAKEDIPROP(22)
#define DIPROP_SCANCODE    MAKEDIPROP(23)
#define DIPROP_VIDPID      MAKEDIPROP(24)
#define DIPROP_USERNAME    MAKEDIPROP(25)
#define DIPROP_TYPENAME    MAKEDIPROP(26)

typedef struct DIDEVCAPS_DX3 {
    DWORD	dwSize;
    DWORD	dwFlags;
    DWORD	dwDevType;
    DWORD	dwAxes;
    DWORD	dwButtons;
    DWORD	dwPOVs;
} DIDEVCAPS_DX3, *LPDIDEVCAPS_DX3;

typedef struct DIDEVCAPS {
    DWORD	dwSize;
    DWORD	dwFlags;
    DWORD	dwDevType;
    DWORD	dwAxes;
    DWORD	dwButtons;
    DWORD	dwPOVs;
    DWORD	dwFFSamplePeriod;
    DWORD	dwFFMinTimeResolution;
    DWORD	dwFirmwareRevision;
    DWORD	dwHardwareRevision;
    DWORD	dwFFDriverVersion;
} DIDEVCAPS,*LPDIDEVCAPS;

#define DIDC_ATTACHED		0x00000001
#define DIDC_POLLEDDEVICE	0x00000002
#define DIDC_EMULATED		0x00000004
#define DIDC_POLLEDDATAFORMAT	0x00000008
#define DIDC_FORCEFEEDBACK	0x00000100
#define DIDC_FFATTACK		0x00000200
#define DIDC_FFFADE		0x00000400
#define DIDC_SATURATION		0x00000800
#define DIDC_POSNEGCOEFFICIENTS	0x00001000
#define DIDC_POSNEGSATURATION	0x00002000
#define DIDC_DEADBAND		0x00004000
#define DIDC_STARTDELAY		0x00008000
#define DIDC_ALIAS		0x00010000
#define DIDC_PHANTOM		0x00020000
#define DIDC_HIDDEN		0x00040000


/* SetCooperativeLevel dwFlags */
#define DISCL_EXCLUSIVE		0x00000001
#define DISCL_NONEXCLUSIVE	0x00000002
#define DISCL_FOREGROUND	0x00000004
#define DISCL_BACKGROUND	0x00000008
#define DISCL_NOWINKEY          0x00000010

/* Device FF flags */
#define DISFFC_RESET            0x00000001
#define DISFFC_STOPALL          0x00000002
#define DISFFC_PAUSE            0x00000004
#define DISFFC_CONTINUE         0x00000008
#define DISFFC_SETACTUATORSON   0x00000010
#define DISFFC_SETACTUATORSOFF  0x00000020

#define DIGFFS_EMPTY            0x00000001
#define DIGFFS_STOPPED          0x00000002
#define DIGFFS_PAUSED           0x00000004
#define DIGFFS_ACTUATORSON      0x00000010
#define DIGFFS_ACTUATORSOFF     0x00000020
#define DIGFFS_POWERON          0x00000040
#define DIGFFS_POWEROFF         0x00000080
#define DIGFFS_SAFETYSWITCHON   0x00000100
#define DIGFFS_SAFETYSWITCHOFF  0x00000200
#define DIGFFS_USERFFSWITCHON   0x00000400
#define DIGFFS_USERFFSWITCHOFF  0x00000800
#define DIGFFS_DEVICELOST       0x80000000

/* Effect flags */
#define DIEFT_ALL		0x00000000

#define DIEFT_CONSTANTFORCE	0x00000001
#define DIEFT_RAMPFORCE		0x00000002
#define DIEFT_PERIODIC		0x00000003
#define DIEFT_CONDITION		0x00000004
#define DIEFT_CUSTOMFORCE	0x00000005
#define DIEFT_HARDWARE		0x000000FF
#define DIEFT_FFATTACK		0x00000200
#define DIEFT_FFFADE		0x00000400
#define DIEFT_SATURATION	0x00000800
#define DIEFT_POSNEGCOEFFICIENTS 0x00001000
#define DIEFT_POSNEGSATURATION	0x00002000
#define DIEFT_DEADBAND		0x00004000
#define DIEFT_STARTDELAY	0x00008000
#define DIEFT_GETTYPE(n)	LOBYTE(n)

#define DIEFF_OBJECTIDS         0x00000001
#define DIEFF_OBJECTOFFSETS     0x00000002
#define DIEFF_CARTESIAN         0x00000010
#define DIEFF_POLAR             0x00000020
#define DIEFF_SPHERICAL         0x00000040

#define DIEP_DURATION           0x00000001
#define DIEP_SAMPLEPERIOD       0x00000002
#define DIEP_GAIN               0x00000004
#define DIEP_TRIGGERBUTTON      0x00000008
#define DIEP_TRIGGERREPEATINTERVAL 0x00000010
#define DIEP_AXES               0x00000020
#define DIEP_DIRECTION          0x00000040
#define DIEP_ENVELOPE           0x00000080
#define DIEP_TYPESPECIFICPARAMS 0x00000100
#define DIEP_STARTDELAY         0x00000200
#define DIEP_ALLPARAMS_DX5      0x000001FF
#define DIEP_ALLPARAMS          0x000003FF
#define DIEP_START              0x20000000
#define DIEP_NORESTART          0x40000000
#define DIEP_NODOWNLOAD         0x80000000
#define DIEB_NOTRIGGER          0xFFFFFFFF

#define DIES_SOLO               0x00000001
#define DIES_NODOWNLOAD         0x80000000

#define DIEGES_PLAYING          0x00000001
#define DIEGES_EMULATED         0x00000002

#define DI_DEGREES		100
#define DI_FFNOMINALMAX		10000
#define DI_SECONDS		1000000

typedef struct DICONSTANTFORCE {
	LONG			lMagnitude;
} DICONSTANTFORCE, *LPDICONSTANTFORCE;
typedef const DICONSTANTFORCE *LPCDICONSTANTFORCE;

typedef struct DIRAMPFORCE {
	LONG			lStart;
	LONG			lEnd;
} DIRAMPFORCE, *LPDIRAMPFORCE;
typedef const DIRAMPFORCE *LPCDIRAMPFORCE;

typedef struct DIPERIODIC {
	DWORD			dwMagnitude;
	LONG			lOffset;
	DWORD			dwPhase;
	DWORD			dwPeriod;
} DIPERIODIC, *LPDIPERIODIC;
typedef const DIPERIODIC *LPCDIPERIODIC;

typedef struct DICONDITION {
	LONG			lOffset;
	LONG			lPositiveCoefficient;
	LONG			lNegativeCoefficient;
	DWORD			dwPositiveSaturation;
	DWORD			dwNegativeSaturation;
	LONG			lDeadBand;
} DICONDITION, *LPDICONDITION;
typedef const DICONDITION *LPCDICONDITION;

typedef struct DICUSTOMFORCE {
	DWORD			cChannels;
	DWORD			dwSamplePeriod;
	DWORD			cSamples;
	LPLONG			rglForceData;
} DICUSTOMFORCE, *LPDICUSTOMFORCE;
typedef const DICUSTOMFORCE *LPCDICUSTOMFORCE;

typedef struct DIENVELOPE {
	DWORD			dwSize;
	DWORD			dwAttackLevel;
	DWORD			dwAttackTime;
	DWORD			dwFadeLevel;
	DWORD			dwFadeTime;
} DIENVELOPE, *LPDIENVELOPE;
typedef const DIENVELOPE *LPCDIENVELOPE;

typedef struct DIEFFECT_DX5 {
	DWORD			dwSize;
	DWORD			dwFlags;
	DWORD			dwDuration;
	DWORD			dwSamplePeriod;
	DWORD			dwGain;
	DWORD			dwTriggerButton;
	DWORD			dwTriggerRepeatInterval;
	DWORD			cAxes;
	LPDWORD			rgdwAxes;
	LPLONG			rglDirection;
	LPDIENVELOPE		lpEnvelope;
	DWORD			cbTypeSpecificParams;
	LPVOID			lpvTypeSpecificParams;
} DIEFFECT_DX5, *LPDIEFFECT_DX5;
typedef const DIEFFECT_DX5 *LPCDIEFFECT_DX5;

typedef struct DIEFFECT {
	DWORD			dwSize;
	DWORD			dwFlags;
	DWORD			dwDuration;
	DWORD			dwSamplePeriod;
	DWORD			dwGain;
	DWORD			dwTriggerButton;
	DWORD			dwTriggerRepeatInterval;
	DWORD			cAxes;
	LPDWORD			rgdwAxes;
	LPLONG			rglDirection;
	LPDIENVELOPE		lpEnvelope;
	DWORD			cbTypeSpecificParams;
	LPVOID			lpvTypeSpecificParams;
	DWORD			dwStartDelay;
} DIEFFECT, *LPDIEFFECT;
typedef const DIEFFECT *LPCDIEFFECT;
typedef DIEFFECT DIEFFECT_DX6;
typedef LPDIEFFECT LPDIEFFECT_DX6;

typedef struct DIEFFECTINFOA {
	DWORD			dwSize;
	GUID			guid;
	DWORD			dwEffType;
	DWORD			dwStaticParams;
	DWORD			dwDynamicParams;
	CHAR			tszName[MAX_PATH];
} DIEFFECTINFOA, *LPDIEFFECTINFOA;
typedef const DIEFFECTINFOA *LPCDIEFFECTINFOA;

typedef struct DIEFFECTINFOW {
	DWORD			dwSize;
	GUID			guid;
	DWORD			dwEffType;
	DWORD			dwStaticParams;
	DWORD			dwDynamicParams;
	WCHAR			tszName[MAX_PATH];
} DIEFFECTINFOW, *LPDIEFFECTINFOW;
typedef const DIEFFECTINFOW *LPCDIEFFECTINFOW;

DECL_WINELIB_TYPE_AW(DIEFFECTINFO)
DECL_WINELIB_TYPE_AW(LPDIEFFECTINFO)
DECL_WINELIB_TYPE_AW(LPCDIEFFECTINFO)

typedef BOOL (CALLBACK *LPDIENUMEFFECTSCALLBACKA)(LPCDIEFFECTINFOA, LPVOID);
typedef BOOL (CALLBACK *LPDIENUMEFFECTSCALLBACKW)(LPCDIEFFECTINFOW, LPVOID);

typedef struct DIEFFESCAPE {
	DWORD	dwSize;
	DWORD	dwCommand;
	LPVOID	lpvInBuffer;
	DWORD	cbInBuffer;
	LPVOID	lpvOutBuffer;
	DWORD	cbOutBuffer;
} DIEFFESCAPE, *LPDIEFFESCAPE;

typedef struct DIJOYSTATE {
	LONG	lX;
	LONG	lY;
	LONG	lZ;
	LONG	lRx;
	LONG	lRy;
	LONG	lRz;
	LONG	rglSlider[2];
	DWORD	rgdwPOV[4];
	BYTE	rgbButtons[32];
} DIJOYSTATE, *LPDIJOYSTATE;

typedef struct DIJOYSTATE2 {
	LONG	lX;
	LONG	lY;
	LONG	lZ;
	LONG	lRx;
	LONG	lRy;
	LONG	lRz;
	LONG	rglSlider[2];
	DWORD	rgdwPOV[4];
	BYTE	rgbButtons[128];
	LONG	lVX;		/* 'v' as in velocity */
	LONG	lVY;
	LONG	lVZ;
	LONG	lVRx;
	LONG	lVRy;
	LONG	lVRz;
	LONG	rglVSlider[2];
	LONG	lAX;		/* 'a' as in acceleration */
	LONG	lAY;
	LONG	lAZ;
	LONG	lARx;
	LONG	lARy;
	LONG	lARz;
	LONG	rglASlider[2];
	LONG	lFX;		/* 'f' as in force */
	LONG	lFY;
	LONG	lFZ;
	LONG	lFRx;		/* 'fr' as in rotational force aka torque */
	LONG	lFRy;
	LONG	lFRz;
	LONG	rglFSlider[2];
} DIJOYSTATE2, *LPDIJOYSTATE2;

#define DIJOFS_X		FIELD_OFFSET(DIJOYSTATE, lX)
#define DIJOFS_Y		FIELD_OFFSET(DIJOYSTATE, lY)
#define DIJOFS_Z		FIELD_OFFSET(DIJOYSTATE, lZ)
#define DIJOFS_RX		FIELD_OFFSET(DIJOYSTATE, lRx)
#define DIJOFS_RY		FIELD_OFFSET(DIJOYSTATE, lRy)
#define DIJOFS_RZ		FIELD_OFFSET(DIJOYSTATE, lRz)
#define DIJOFS_SLIDER(n)	(FIELD_OFFSET(DIJOYSTATE, rglSlider) + \
                                                        (n) * sizeof(LONG))
#define DIJOFS_POV(n)		(FIELD_OFFSET(DIJOYSTATE, rgdwPOV) + \
                                                        (n) * sizeof(DWORD))
#define DIJOFS_BUTTON(n)	(FIELD_OFFSET(DIJOYSTATE, rgbButtons) + (n))
#define DIJOFS_BUTTON0		DIJOFS_BUTTON(0)
#define DIJOFS_BUTTON1		DIJOFS_BUTTON(1)
#define DIJOFS_BUTTON2		DIJOFS_BUTTON(2)
#define DIJOFS_BUTTON3		DIJOFS_BUTTON(3)
#define DIJOFS_BUTTON4		DIJOFS_BUTTON(4)
#define DIJOFS_BUTTON5		DIJOFS_BUTTON(5)
#define DIJOFS_BUTTON6		DIJOFS_BUTTON(6)
#define DIJOFS_BUTTON7		DIJOFS_BUTTON(7)
#define DIJOFS_BUTTON8		DIJOFS_BUTTON(8)
#define DIJOFS_BUTTON9		DIJOFS_BUTTON(9)
#define DIJOFS_BUTTON10		DIJOFS_BUTTON(10)
#define DIJOFS_BUTTON11		DIJOFS_BUTTON(11)
#define DIJOFS_BUTTON12		DIJOFS_BUTTON(12)
#define DIJOFS_BUTTON13		DIJOFS_BUTTON(13)
#define DIJOFS_BUTTON14		DIJOFS_BUTTON(14)
#define DIJOFS_BUTTON15		DIJOFS_BUTTON(15)
#define DIJOFS_BUTTON16		DIJOFS_BUTTON(16)
#define DIJOFS_BUTTON17		DIJOFS_BUTTON(17)
#define DIJOFS_BUTTON18		DIJOFS_BUTTON(18)
#define DIJOFS_BUTTON19		DIJOFS_BUTTON(19)
#define DIJOFS_BUTTON20		DIJOFS_BUTTON(20)
#define DIJOFS_BUTTON21		DIJOFS_BUTTON(21)
#define DIJOFS_BUTTON22		DIJOFS_BUTTON(22)
#define DIJOFS_BUTTON23		DIJOFS_BUTTON(23)
#define DIJOFS_BUTTON24		DIJOFS_BUTTON(24)
#define DIJOFS_BUTTON25		DIJOFS_BUTTON(25)
#define DIJOFS_BUTTON26		DIJOFS_BUTTON(26)
#define DIJOFS_BUTTON27		DIJOFS_BUTTON(27)
#define DIJOFS_BUTTON28		DIJOFS_BUTTON(28)
#define DIJOFS_BUTTON29		DIJOFS_BUTTON(29)
#define DIJOFS_BUTTON30		DIJOFS_BUTTON(30)
#define DIJOFS_BUTTON31		DIJOFS_BUTTON(31)

/* DInput 7 structures, types */
typedef struct DIFILEEFFECT {
  DWORD       dwSize;
  GUID        GuidEffect;
  LPCDIEFFECT lpDiEffect;
  CHAR        szFriendlyName[MAX_PATH];
} DIFILEEFFECT, *LPDIFILEEFFECT;

typedef const DIFILEEFFECT *LPCDIFILEEFFECT;
typedef BOOL (CALLBACK *LPDIENUMEFFECTSINFILECALLBACK)(LPCDIFILEEFFECT , LPVOID);

/* DInput 8 structures and types */
typedef struct _DIACTIONA {
	UINT_PTR	uAppData;
	DWORD		dwSemantic;
	DWORD		dwFlags;
	union {
		LPCSTR	lptszActionName;
		UINT	uResIdString;
	} DUMMYUNIONNAME;
	GUID		guidInstance;
	DWORD		dwObjID;
	DWORD		dwHow;
} DIACTIONA, *LPDIACTIONA;
typedef const DIACTIONA *LPCDIACTIONA;

typedef struct _DIACTIONW {
	UINT_PTR	uAppData;
	DWORD		dwSemantic;
	DWORD		dwFlags;
	union {
		LPCWSTR	lptszActionName;
		UINT	uResIdString;
	} DUMMYUNIONNAME;
	GUID		guidInstance;
	DWORD		dwObjID;
	DWORD		dwHow;
} DIACTIONW, *LPDIACTIONW;
typedef const DIACTIONW *LPCDIACTIONW;

DECL_WINELIB_TYPE_AW(DIACTION)
DECL_WINELIB_TYPE_AW(LPDIACTION)
DECL_WINELIB_TYPE_AW(LPCDIACTION)

#define DIA_FORCEFEEDBACK	0x00000001
#define DIA_APPMAPPED		0x00000002
#define DIA_APPNOMAP		0x00000004
#define DIA_NORANGE		0x00000008
#define DIA_APPFIXED		0x00000010

#define DIAH_UNMAPPED		0x00000000
#define DIAH_USERCONFIG		0x00000001
#define DIAH_APPREQUESTED	0x00000002
#define DIAH_HWAPP		0x00000004
#define DIAH_HWDEFAULT		0x00000008
#define DIAH_DEFAULT		0x00000020
#define DIAH_ERROR		0x80000000

typedef struct _DIACTIONFORMATA {
	DWORD		dwSize;
	DWORD		dwActionSize;
	DWORD		dwDataSize;
	DWORD		dwNumActions;
	LPDIACTIONA	rgoAction;
	GUID		guidActionMap;
	DWORD		dwGenre;
	DWORD		dwBufferSize;
	LONG		lAxisMin;
	LONG		lAxisMax;
	HINSTANCE	hInstString;
	FILETIME	ftTimeStamp;
	DWORD		dwCRC;
	CHAR		tszActionMap[MAX_PATH];
} DIACTIONFORMATA, *LPDIACTIONFORMATA;
typedef const DIACTIONFORMATA *LPCDIACTIONFORMATA;

typedef struct _DIACTIONFORMATW {
	DWORD		dwSize;
	DWORD		dwActionSize;
	DWORD		dwDataSize;
	DWORD		dwNumActions;
	LPDIACTIONW	rgoAction;
	GUID		guidActionMap;
	DWORD		dwGenre;
	DWORD		dwBufferSize;
	LONG		lAxisMin;
	LONG		lAxisMax;
	HINSTANCE	hInstString;
	FILETIME	ftTimeStamp;
	DWORD		dwCRC;
	WCHAR		tszActionMap[MAX_PATH];
} DIACTIONFORMATW, *LPDIACTIONFORMATW;
typedef const DIACTIONFORMATW *LPCDIACTIONFORMATW;

DECL_WINELIB_TYPE_AW(DIACTIONFORMAT)
DECL_WINELIB_TYPE_AW(LPDIACTIONFORMAT)
DECL_WINELIB_TYPE_AW(LPCDIACTIONFORMAT)

#define DIAFTS_NEWDEVICELOW	0xFFFFFFFF
#define DIAFTS_NEWDEVICEHIGH	0xFFFFFFFF
#define DIAFTS_UNUSEDDEVICELOW	0x00000000
#define DIAFTS_UNUSEDDEVICEHIGH	0x00000000

#define DIDBAM_DEFAULT		0x00000000
#define DIDBAM_PRESERVE		0x00000001
#define DIDBAM_INITIALIZE	0x00000002
#define DIDBAM_HWDEFAULTS	0x00000004

#define DIDSAM_DEFAULT		0x00000000
#define DIDSAM_NOUSER		0x00000001
#define DIDSAM_FORCESAVE	0x00000002

#define DICD_DEFAULT		0x00000000
#define DICD_EDIT		0x00000001

#ifndef D3DCOLOR_DEFINED
typedef DWORD D3DCOLOR;
#define D3DCOLOR_DEFINED
#endif

typedef struct _DICOLORSET {
	DWORD		dwSize;
	D3DCOLOR	cTextFore;
	D3DCOLOR	cTextHighlight;
	D3DCOLOR	cCalloutLine;
	D3DCOLOR	cCalloutHighlight;
	D3DCOLOR	cBorder;
	D3DCOLOR	cControlFill;
	D3DCOLOR	cHighlightFill;
	D3DCOLOR	cAreaFill;
} DICOLORSET, *LPDICOLORSET;
typedef const DICOLORSET *LPCDICOLORSET;

typedef struct _DICONFIGUREDEVICESPARAMSA {
	DWORD			dwSize;
	DWORD			dwcUsers;
	LPSTR			lptszUserNames;
	DWORD			dwcFormats;
	LPDIACTIONFORMATA	lprgFormats;
	HWND			hwnd;
	DICOLORSET		dics;
	LPUNKNOWN		lpUnkDDSTarget;
} DICONFIGUREDEVICESPARAMSA, *LPDICONFIGUREDEVICESPARAMSA;
typedef const DICONFIGUREDEVICESPARAMSA *LPCDICONFIGUREDEVICESPARAMSA;

typedef struct _DICONFIGUREDEVICESPARAMSW {
	DWORD			dwSize;
	DWORD			dwcUsers;
	LPWSTR			lptszUserNames;
	DWORD			dwcFormats;
	LPDIACTIONFORMATW	lprgFormats;
	HWND			hwnd;
	DICOLORSET		dics;
	LPUNKNOWN		lpUnkDDSTarget;
} DICONFIGUREDEVICESPARAMSW, *LPDICONFIGUREDEVICESPARAMSW;
typedef const DICONFIGUREDEVICESPARAMSW *LPCDICONFIGUREDEVICESPARAMSW;

DECL_WINELIB_TYPE_AW(DICONFIGUREDEVICESPARAMS)
DECL_WINELIB_TYPE_AW(LPDICONFIGUREDEVICESPARAMS)
DECL_WINELIB_TYPE_AW(LPCDICONFIGUREDEVICESPARAMS)

#define DIDIFT_CONFIGURATION	0x00000001
#define DIDIFT_OVERLAY		0x00000002

#define DIDAL_CENTERED		0x00000000
#define DIDAL_LEFTALIGNED	0x00000001
#define DIDAL_RIGHTALIGNED	0x00000002
#define DIDAL_MIDDLE		0x00000000
#define DIDAL_TOPALIGNED	0x00000004
#define DIDAL_BOTTOMALIGNED	0x00000008

typedef struct _DIDEVICEIMAGEINFOA {
	CHAR	tszImagePath[MAX_PATH];
	DWORD	dwFlags;
	DWORD	dwViewID;
	RECT	rcOverlay;
	DWORD	dwObjID;
	DWORD	dwcValidPts;
	POINT	rgptCalloutLine[5];
	RECT	rcCalloutRect;
	DWORD	dwTextAlign;
} DIDEVICEIMAGEINFOA, *LPDIDEVICEIMAGEINFOA;
typedef const DIDEVICEIMAGEINFOA *LPCDIDEVICEIMAGEINFOA;

typedef struct _DIDEVICEIMAGEINFOW {
	WCHAR	tszImagePath[MAX_PATH];
	DWORD	dwFlags;
	DWORD	dwViewID;
	RECT	rcOverlay;
	DWORD	dwObjID;
	DWORD	dwcValidPts;
	POINT	rgptCalloutLine[5];
	RECT	rcCalloutRect;
	DWORD	dwTextAlign;
} DIDEVICEIMAGEINFOW, *LPDIDEVICEIMAGEINFOW;
typedef const DIDEVICEIMAGEINFOW *LPCDIDEVICEIMAGEINFOW;

DECL_WINELIB_TYPE_AW(DIDEVICEIMAGEINFO)
DECL_WINELIB_TYPE_AW(LPDIDEVICEIMAGEINFO)
DECL_WINELIB_TYPE_AW(LPCDIDEVICEIMAGEINFO)

typedef struct _DIDEVICEIMAGEINFOHEADERA {
	DWORD	dwSize;
	DWORD	dwSizeImageInfo;
	DWORD	dwcViews;
	DWORD	dwcButtons;
	DWORD	dwcAxes;
	DWORD	dwcPOVs;
	DWORD	dwBufferSize;
	DWORD	dwBufferUsed;
	LPDIDEVICEIMAGEINFOA	lprgImageInfoArray;
} DIDEVICEIMAGEINFOHEADERA, *LPDIDEVICEIMAGEINFOHEADERA;
typedef const DIDEVICEIMAGEINFOHEADERA *LPCDIDEVICEIMAGEINFOHEADERA;

typedef struct _DIDEVICEIMAGEINFOHEADERW {
	DWORD	dwSize;
	DWORD	dwSizeImageInfo;
	DWORD	dwcViews;
	DWORD	dwcButtons;
	DWORD	dwcAxes;
	DWORD	dwcPOVs;
	DWORD	dwBufferSize;
	DWORD	dwBufferUsed;
	LPDIDEVICEIMAGEINFOW	lprgImageInfoArray;
} DIDEVICEIMAGEINFOHEADERW, *LPDIDEVICEIMAGEINFOHEADERW;
typedef const DIDEVICEIMAGEINFOHEADERW *LPCDIDEVICEIMAGEINFOHEADERW;

DECL_WINELIB_TYPE_AW(DIDEVICEIMAGEINFOHEADER)
DECL_WINELIB_TYPE_AW(LPDIDEVICEIMAGEINFOHEADER)
DECL_WINELIB_TYPE_AW(LPCDIDEVICEIMAGEINFOHEADER)


/*****************************************************************************
 * IDirectInputEffect interface
 */
#define INTERFACE IDirectInputEffect
DECLARE_INTERFACE_(IDirectInputEffect,IUnknown)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputEffect methods ***/
    STDMETHOD(Initialize)(THIS_ HINSTANCE, DWORD, REFGUID) PURE;
    STDMETHOD(GetEffectGuid)(THIS_ LPGUID) PURE;
    STDMETHOD(GetParameters)(THIS_ LPDIEFFECT, DWORD) PURE;
    STDMETHOD(SetParameters)(THIS_ LPCDIEFFECT, DWORD) PURE;
    STDMETHOD(Start)(THIS_ DWORD, DWORD) PURE;
    STDMETHOD(Stop)(THIS) PURE;
    STDMETHOD(GetEffectStatus)(THIS_ LPDWORD) PURE;
    STDMETHOD(Download)(THIS) PURE;
    STDMETHOD(Unload)(THIS) PURE;
    STDMETHOD(Escape)(THIS_ LPDIEFFESCAPE) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define IDirectInputEffect_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IDirectInputEffect_AddRef(p)             (p)->lpVtbl->AddRef(p)
#define IDirectInputEffect_Release(p)            (p)->lpVtbl->Release(p)
/*** IDirectInputEffect methods ***/
#define IDirectInputEffect_Initialize(p,a,b,c)    (p)->lpVtbl->Initialize(p,a,b,c)
#define IDirectInputEffect_GetEffectGuid(p,a)     (p)->lpVtbl->GetEffectGuid(p,a)
#define IDirectInputEffect_GetParameters(p,a,b)   (p)->lpVtbl->GetParameters(p,a,b)
#define IDirectInputEffect_SetParameters(p,a,b)   (p)->lpVtbl->SetParameters(p,a,b)
#define IDirectInputEffect_Start(p,a,b)           (p)->lpVtbl->Start(p,a,b)
#define IDirectInputEffect_Stop(p)                (p)->lpVtbl->Stop(p)
#define IDirectInputEffect_GetEffectStatus(p,a)   (p)->lpVtbl->GetEffectStatus(p,a)
#define IDirectInputEffect_Download(p)            (p)->lpVtbl->Download(p)
#define IDirectInputEffect_Unload(p)              (p)->lpVtbl->Unload(p)
#define IDirectInputEffect_Escape(p,a)            (p)->lpVtbl->Escape(p,a)
#else
/*** IUnknown methods ***/
#define IDirectInputEffect_QueryInterface(p,a,b) (p)->QueryInterface(a,b)
#define IDirectInputEffect_AddRef(p)             (p)->AddRef()
#define IDirectInputEffect_Release(p)            (p)->Release()
/*** IDirectInputEffect methods ***/
#define IDirectInputEffect_Initialize(p,a,b,c)    (p)->Initialize(a,b,c)
#define IDirectInputEffect_GetEffectGuid(p,a)     (p)->GetEffectGuid(a)
#define IDirectInputEffect_GetParameters(p,a,b)   (p)->GetParameters(a,b)
#define IDirectInputEffect_SetParameters(p,a,b)   (p)->SetParameters(a,b)
#define IDirectInputEffect_Start(p,a,b)           (p)->Start(a,b)
#define IDirectInputEffect_Stop(p)                (p)->Stop()
#define IDirectInputEffect_GetEffectStatus(p,a)   (p)->GetEffectStatus(a)
#define IDirectInputEffect_Download(p)            (p)->Download()
#define IDirectInputEffect_Unload(p)              (p)->Unload()
#define IDirectInputEffect_Escape(p,a)            (p)->Escape(a)
#endif


/*****************************************************************************
 * IDirectInputDeviceA interface
 */
#define INTERFACE IDirectInputDeviceA
DECLARE_INTERFACE_(IDirectInputDeviceA,IUnknown)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputDeviceA methods ***/
    STDMETHOD(GetCapabilities)(THIS_ LPDIDEVCAPS lpDIDevCaps) PURE;
    STDMETHOD(EnumObjects)(THIS_ LPDIENUMDEVICEOBJECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetProperty)(THIS_ REFGUID rguidProp, LPDIPROPHEADER pdiph) PURE;
    STDMETHOD(SetProperty)(THIS_ REFGUID rguidProp, LPCDIPROPHEADER pdiph) PURE;
    STDMETHOD(Acquire)(THIS) PURE;
    STDMETHOD(Unacquire)(THIS) PURE;
    STDMETHOD(GetDeviceState)(THIS_ DWORD cbData, LPVOID lpvData) PURE;
    STDMETHOD(GetDeviceData)(THIS_ DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) PURE;
    STDMETHOD(SetDataFormat)(THIS_ LPCDIDATAFORMAT lpdf) PURE;
    STDMETHOD(SetEventNotification)(THIS_ HANDLE hEvent) PURE;
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND hwnd, DWORD dwFlags) PURE;
    STDMETHOD(GetObjectInfo)(THIS_ LPDIDEVICEOBJECTINSTANCEA pdidoi, DWORD dwObj, DWORD dwHow) PURE;
    STDMETHOD(GetDeviceInfo)(THIS_ LPDIDEVICEINSTANCEA pdidi) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) PURE;
};
#undef INTERFACE

/*****************************************************************************
 * IDirectInputDeviceW interface
 */
#define INTERFACE IDirectInputDeviceW
DECLARE_INTERFACE_(IDirectInputDeviceW,IUnknown)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputDeviceW methods ***/
    STDMETHOD(GetCapabilities)(THIS_ LPDIDEVCAPS lpDIDevCaps) PURE;
    STDMETHOD(EnumObjects)(THIS_ LPDIENUMDEVICEOBJECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetProperty)(THIS_ REFGUID rguidProp, LPDIPROPHEADER pdiph) PURE;
    STDMETHOD(SetProperty)(THIS_ REFGUID rguidProp, LPCDIPROPHEADER pdiph) PURE;
    STDMETHOD(Acquire)(THIS) PURE;
    STDMETHOD(Unacquire)(THIS) PURE;
    STDMETHOD(GetDeviceState)(THIS_ DWORD cbData, LPVOID lpvData) PURE;
    STDMETHOD(GetDeviceData)(THIS_ DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) PURE;
    STDMETHOD(SetDataFormat)(THIS_ LPCDIDATAFORMAT lpdf) PURE;
    STDMETHOD(SetEventNotification)(THIS_ HANDLE hEvent) PURE;
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND hwnd, DWORD dwFlags) PURE;
    STDMETHOD(GetObjectInfo)(THIS_ LPDIDEVICEOBJECTINSTANCEW pdidoi, DWORD dwObj, DWORD dwHow) PURE;
    STDMETHOD(GetDeviceInfo)(THIS_ LPDIDEVICEINSTANCEW pdidi) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define IDirectInputDevice_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IDirectInputDevice_AddRef(p)             (p)->lpVtbl->AddRef(p)
#define IDirectInputDevice_Release(p)            (p)->lpVtbl->Release(p)
/*** IDirectInputDevice methods ***/
#define IDirectInputDevice_GetCapabilities(p,a)       (p)->lpVtbl->GetCapabilities(p,a)
#define IDirectInputDevice_EnumObjects(p,a,b,c)       (p)->lpVtbl->EnumObjects(p,a,b,c)
#define IDirectInputDevice_GetProperty(p,a,b)         (p)->lpVtbl->GetProperty(p,a,b)
#define IDirectInputDevice_SetProperty(p,a,b)         (p)->lpVtbl->SetProperty(p,a,b)
#define IDirectInputDevice_Acquire(p)                 (p)->lpVtbl->Acquire(p)
#define IDirectInputDevice_Unacquire(p)               (p)->lpVtbl->Unacquire(p)
#define IDirectInputDevice_GetDeviceState(p,a,b)      (p)->lpVtbl->GetDeviceState(p,a,b)
#define IDirectInputDevice_GetDeviceData(p,a,b,c,d)   (p)->lpVtbl->GetDeviceData(p,a,b,c,d)
#define IDirectInputDevice_SetDataFormat(p,a)         (p)->lpVtbl->SetDataFormat(p,a)
#define IDirectInputDevice_SetEventNotification(p,a)  (p)->lpVtbl->SetEventNotification(p,a)
#define IDirectInputDevice_SetCooperativeLevel(p,a,b) (p)->lpVtbl->SetCooperativeLevel(p,a,b)
#define IDirectInputDevice_GetObjectInfo(p,a,b,c)     (p)->lpVtbl->GetObjectInfo(p,a,b,c)
#define IDirectInputDevice_GetDeviceInfo(p,a)         (p)->lpVtbl->GetDeviceInfo(p,a)
#define IDirectInputDevice_RunControlPanel(p,a,b)     (p)->lpVtbl->RunControlPanel(p,a,b)
#define IDirectInputDevice_Initialize(p,a,b,c)        (p)->lpVtbl->Initialize(p,a,b,c)
#else
/*** IUnknown methods ***/
#define IDirectInputDevice_QueryInterface(p,a,b) (p)->QueryInterface(a,b)
#define IDirectInputDevice_AddRef(p)             (p)->AddRef()
#define IDirectInputDevice_Release(p)            (p)->Release()
/*** IDirectInputDevice methods ***/
#define IDirectInputDevice_GetCapabilities(p,a)       (p)->GetCapabilities(a)
#define IDirectInputDevice_EnumObjects(p,a,b,c)       (p)->EnumObjects(a,b,c)
#define IDirectInputDevice_GetProperty(p,a,b)         (p)->GetProperty(a,b)
#define IDirectInputDevice_SetProperty(p,a,b)         (p)->SetProperty(a,b)
#define IDirectInputDevice_Acquire(p)                 (p)->Acquire()
#define IDirectInputDevice_Unacquire(p)               (p)->Unacquire()
#define IDirectInputDevice_GetDeviceState(p,a,b)      (p)->GetDeviceState(a,b)
#define IDirectInputDevice_GetDeviceData(p,a,b,c,d)   (p)->GetDeviceData(a,b,c,d)
#define IDirectInputDevice_SetDataFormat(p,a)         (p)->SetDataFormat(a)
#define IDirectInputDevice_SetEventNotification(p,a)  (p)->SetEventNotification(a)
#define IDirectInputDevice_SetCooperativeLevel(p,a,b) (p)->SetCooperativeLevel(a,b)
#define IDirectInputDevice_GetObjectInfo(p,a,b,c)     (p)->GetObjectInfo(a,b,c)
#define IDirectInputDevice_GetDeviceInfo(p,a)         (p)->GetDeviceInfo(a)
#define IDirectInputDevice_RunControlPanel(p,a,b)     (p)->RunControlPanel(a,b)
#define IDirectInputDevice_Initialize(p,a,b,c)        (p)->Initialize(a,b,c)
#endif


/*****************************************************************************
 * IDirectInputDevice2A interface
 */
#define INTERFACE IDirectInputDevice2A
DECLARE_INTERFACE_(IDirectInputDevice2A,IDirectInputDeviceA)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputDeviceA methods ***/
    STDMETHOD(GetCapabilities)(THIS_ LPDIDEVCAPS lpDIDevCaps) PURE;
    STDMETHOD(EnumObjects)(THIS_ LPDIENUMDEVICEOBJECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetProperty)(THIS_ REFGUID rguidProp, LPDIPROPHEADER pdiph) PURE;
    STDMETHOD(SetProperty)(THIS_ REFGUID rguidProp, LPCDIPROPHEADER pdiph) PURE;
    STDMETHOD(Acquire)(THIS) PURE;
    STDMETHOD(Unacquire)(THIS) PURE;
    STDMETHOD(GetDeviceState)(THIS_ DWORD cbData, LPVOID lpvData) PURE;
    STDMETHOD(GetDeviceData)(THIS_ DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) PURE;
    STDMETHOD(SetDataFormat)(THIS_ LPCDIDATAFORMAT lpdf) PURE;
    STDMETHOD(SetEventNotification)(THIS_ HANDLE hEvent) PURE;
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND hwnd, DWORD dwFlags) PURE;
    STDMETHOD(GetObjectInfo)(THIS_ LPDIDEVICEOBJECTINSTANCEA pdidoi, DWORD dwObj, DWORD dwHow) PURE;
    STDMETHOD(GetDeviceInfo)(THIS_ LPDIDEVICEINSTANCEA pdidi) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) PURE;
    /*** IDirectInputDevice2A methods ***/
    STDMETHOD(CreateEffect)(THIS_ REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT *ppdeff, LPUNKNOWN punkOuter) PURE;
    STDMETHOD(EnumEffects)(THIS_ LPDIENUMEFFECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwEffType) PURE;
    STDMETHOD(GetEffectInfo)(THIS_ LPDIEFFECTINFOA pdei, REFGUID rguid) PURE;
    STDMETHOD(GetForceFeedbackState)(THIS_ LPDWORD pdwOut) PURE;
    STDMETHOD(SendForceFeedbackCommand)(THIS_ DWORD dwFlags) PURE;
    STDMETHOD(EnumCreatedEffectObjects)(THIS_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl) PURE;
    STDMETHOD(Escape)(THIS_ LPDIEFFESCAPE pesc) PURE;
    STDMETHOD(Poll)(THIS) PURE;
    STDMETHOD(SendDeviceData)(THIS_ DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl) PURE;
};
#undef INTERFACE

/*****************************************************************************
 * IDirectInputDevice2W interface
 */
#define INTERFACE IDirectInputDevice2W
DECLARE_INTERFACE_(IDirectInputDevice2W,IDirectInputDeviceW)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputDeviceW methods ***/
    STDMETHOD(GetCapabilities)(THIS_ LPDIDEVCAPS lpDIDevCaps) PURE;
    STDMETHOD(EnumObjects)(THIS_ LPDIENUMDEVICEOBJECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetProperty)(THIS_ REFGUID rguidProp, LPDIPROPHEADER pdiph) PURE;
    STDMETHOD(SetProperty)(THIS_ REFGUID rguidProp, LPCDIPROPHEADER pdiph) PURE;
    STDMETHOD(Acquire)(THIS) PURE;
    STDMETHOD(Unacquire)(THIS) PURE;
    STDMETHOD(GetDeviceState)(THIS_ DWORD cbData, LPVOID lpvData) PURE;
    STDMETHOD(GetDeviceData)(THIS_ DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) PURE;
    STDMETHOD(SetDataFormat)(THIS_ LPCDIDATAFORMAT lpdf) PURE;
    STDMETHOD(SetEventNotification)(THIS_ HANDLE hEvent) PURE;
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND hwnd, DWORD dwFlags) PURE;
    STDMETHOD(GetObjectInfo)(THIS_ LPDIDEVICEOBJECTINSTANCEW pdidoi, DWORD dwObj, DWORD dwHow) PURE;
    STDMETHOD(GetDeviceInfo)(THIS_ LPDIDEVICEINSTANCEW pdidi) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) PURE;
    /*** IDirectInputDevice2W methods ***/
    STDMETHOD(CreateEffect)(THIS_ REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT *ppdeff, LPUNKNOWN punkOuter) PURE;
    STDMETHOD(EnumEffects)(THIS_ LPDIENUMEFFECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwEffType) PURE;
    STDMETHOD(GetEffectInfo)(THIS_ LPDIEFFECTINFOW pdei, REFGUID rguid) PURE;
    STDMETHOD(GetForceFeedbackState)(THIS_ LPDWORD pdwOut) PURE;
    STDMETHOD(SendForceFeedbackCommand)(THIS_ DWORD dwFlags) PURE;
    STDMETHOD(EnumCreatedEffectObjects)(THIS_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl) PURE;
    STDMETHOD(Escape)(THIS_ LPDIEFFESCAPE pesc) PURE;
    STDMETHOD(Poll)(THIS) PURE;
    STDMETHOD(SendDeviceData)(THIS_ DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define IDirectInputDevice2_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IDirectInputDevice2_AddRef(p)             (p)->lpVtbl->AddRef(p)
#define IDirectInputDevice2_Release(p)            (p)->lpVtbl->Release(p)
/*** IDirectInputDevice methods ***/
#define IDirectInputDevice2_GetCapabilities(p,a)       (p)->lpVtbl->GetCapabilities(p,a)
#define IDirectInputDevice2_EnumObjects(p,a,b,c)       (p)->lpVtbl->EnumObjects(p,a,b,c)
#define IDirectInputDevice2_GetProperty(p,a,b)         (p)->lpVtbl->GetProperty(p,a,b)
#define IDirectInputDevice2_SetProperty(p,a,b)         (p)->lpVtbl->SetProperty(p,a,b)
#define IDirectInputDevice2_Acquire(p)                 (p)->lpVtbl->Acquire(p)
#define IDirectInputDevice2_Unacquire(p)               (p)->lpVtbl->Unacquire(p)
#define IDirectInputDevice2_GetDeviceState(p,a,b)      (p)->lpVtbl->GetDeviceState(p,a,b)
#define IDirectInputDevice2_GetDeviceData(p,a,b,c,d)   (p)->lpVtbl->GetDeviceData(p,a,b,c,d)
#define IDirectInputDevice2_SetDataFormat(p,a)         (p)->lpVtbl->SetDataFormat(p,a)
#define IDirectInputDevice2_SetEventNotification(p,a)  (p)->lpVtbl->SetEventNotification(p,a)
#define IDirectInputDevice2_SetCooperativeLevel(p,a,b) (p)->lpVtbl->SetCooperativeLevel(p,a,b)
#define IDirectInputDevice2_GetObjectInfo(p,a,b,c)     (p)->lpVtbl->GetObjectInfo(p,a,b,c)
#define IDirectInputDevice2_GetDeviceInfo(p,a)         (p)->lpVtbl->GetDeviceInfo(p,a)
#define IDirectInputDevice2_RunControlPanel(p,a,b)     (p)->lpVtbl->RunControlPanel(p,a,b)
#define IDirectInputDevice2_Initialize(p,a,b,c)        (p)->lpVtbl->Initialize(p,a,b,c)
/*** IDirectInputDevice2 methods ***/
#define IDirectInputDevice2_CreateEffect(p,a,b,c,d)           (p)->lpVtbl->CreateEffect(p,a,b,c,d)
#define IDirectInputDevice2_EnumEffects(p,a,b,c)              (p)->lpVtbl->EnumEffects(p,a,b,c)
#define IDirectInputDevice2_GetEffectInfo(p,a,b)              (p)->lpVtbl->GetEffectInfo(p,a,b)
#define IDirectInputDevice2_GetForceFeedbackState(p,a)        (p)->lpVtbl->GetForceFeedbackState(p,a)
#define IDirectInputDevice2_SendForceFeedbackCommand(p,a)     (p)->lpVtbl->SendForceFeedbackCommand(p,a)
#define IDirectInputDevice2_EnumCreatedEffectObjects(p,a,b,c) (p)->lpVtbl->EnumCreatedEffectObjects(p,a,b,c)
#define IDirectInputDevice2_Escape(p,a)                       (p)->lpVtbl->Escape(p,a)
#define IDirectInputDevice2_Poll(p)                           (p)->lpVtbl->Poll(p)
#define IDirectInputDevice2_SendDeviceData(p,a,b,c,d)         (p)->lpVtbl->SendDeviceData(p,a,b,c,d)
#else
/*** IUnknown methods ***/
#define IDirectInputDevice2_QueryInterface(p,a,b) (p)->QueryInterface(a,b)
#define IDirectInputDevice2_AddRef(p)             (p)->AddRef()
#define IDirectInputDevice2_Release(p)            (p)->Release()
/*** IDirectInputDevice methods ***/
#define IDirectInputDevice2_GetCapabilities(p,a)       (p)->GetCapabilities(a)
#define IDirectInputDevice2_EnumObjects(p,a,b,c)       (p)->EnumObjects(a,b,c)
#define IDirectInputDevice2_GetProperty(p,a,b)         (p)->GetProperty(a,b)
#define IDirectInputDevice2_SetProperty(p,a,b)         (p)->SetProperty(a,b)
#define IDirectInputDevice2_Acquire(p)                 (p)->Acquire()
#define IDirectInputDevice2_Unacquire(p)               (p)->Unacquire()
#define IDirectInputDevice2_GetDeviceState(p,a,b)      (p)->GetDeviceState(a,b)
#define IDirectInputDevice2_GetDeviceData(p,a,b,c,d)   (p)->GetDeviceData(a,b,c,d)
#define IDirectInputDevice2_SetDataFormat(p,a)         (p)->SetDataFormat(a)
#define IDirectInputDevice2_SetEventNotification(p,a)  (p)->SetEventNotification(a)
#define IDirectInputDevice2_SetCooperativeLevel(p,a,b) (p)->SetCooperativeLevel(a,b)
#define IDirectInputDevice2_GetObjectInfo(p,a,b,c)     (p)->GetObjectInfo(a,b,c)
#define IDirectInputDevice2_GetDeviceInfo(p,a)         (p)->GetDeviceInfo(a)
#define IDirectInputDevice2_RunControlPanel(p,a,b)     (p)->RunControlPanel(a,b)
#define IDirectInputDevice2_Initialize(p,a,b,c)        (p)->Initialize(a,b,c)
/*** IDirectInputDevice2 methods ***/
#define IDirectInputDevice2_CreateEffect(p,a,b,c,d)           (p)->CreateEffect(a,b,c,d)
#define IDirectInputDevice2_EnumEffects(p,a,b,c)              (p)->EnumEffects(a,b,c)
#define IDirectInputDevice2_GetEffectInfo(p,a,b)              (p)->GetEffectInfo(a,b)
#define IDirectInputDevice2_GetForceFeedbackState(p,a)        (p)->GetForceFeedbackState(a)
#define IDirectInputDevice2_SendForceFeedbackCommand(p,a)     (p)->SendForceFeedbackCommand(a)
#define IDirectInputDevice2_EnumCreatedEffectObjects(p,a,b,c) (p)->EnumCreatedEffectObjects(a,b,c)
#define IDirectInputDevice2_Escape(p,a)                       (p)->Escape(a)
#define IDirectInputDevice2_Poll(p)                           (p)->Poll()
#define IDirectInputDevice2_SendDeviceData(p,a,b,c,d)         (p)->SendDeviceData(a,b,c,d)
#endif

/*****************************************************************************
 * IDirectInputDevice7A interface
 */
#define INTERFACE IDirectInputDevice7A
DECLARE_INTERFACE_(IDirectInputDevice7A,IDirectInputDevice2A)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputDeviceA methods ***/
    STDMETHOD(GetCapabilities)(THIS_ LPDIDEVCAPS lpDIDevCaps) PURE;
    STDMETHOD(EnumObjects)(THIS_ LPDIENUMDEVICEOBJECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetProperty)(THIS_ REFGUID rguidProp, LPDIPROPHEADER pdiph) PURE;
    STDMETHOD(SetProperty)(THIS_ REFGUID rguidProp, LPCDIPROPHEADER pdiph) PURE;
    STDMETHOD(Acquire)(THIS) PURE;
    STDMETHOD(Unacquire)(THIS) PURE;
    STDMETHOD(GetDeviceState)(THIS_ DWORD cbData, LPVOID lpvData) PURE;
    STDMETHOD(GetDeviceData)(THIS_ DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) PURE;
    STDMETHOD(SetDataFormat)(THIS_ LPCDIDATAFORMAT lpdf) PURE;
    STDMETHOD(SetEventNotification)(THIS_ HANDLE hEvent) PURE;
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND hwnd, DWORD dwFlags) PURE;
    STDMETHOD(GetObjectInfo)(THIS_ LPDIDEVICEOBJECTINSTANCEA pdidoi, DWORD dwObj, DWORD dwHow) PURE;
    STDMETHOD(GetDeviceInfo)(THIS_ LPDIDEVICEINSTANCEA pdidi) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) PURE;
    /*** IDirectInputDevice2A methods ***/
    STDMETHOD(CreateEffect)(THIS_ REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT *ppdeff, LPUNKNOWN punkOuter) PURE;
    STDMETHOD(EnumEffects)(THIS_ LPDIENUMEFFECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwEffType) PURE;
    STDMETHOD(GetEffectInfo)(THIS_ LPDIEFFECTINFOA pdei, REFGUID rguid) PURE;
    STDMETHOD(GetForceFeedbackState)(THIS_ LPDWORD pdwOut) PURE;
    STDMETHOD(SendForceFeedbackCommand)(THIS_ DWORD dwFlags) PURE;
    STDMETHOD(EnumCreatedEffectObjects)(THIS_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl) PURE;
    STDMETHOD(Escape)(THIS_ LPDIEFFESCAPE pesc) PURE;
    STDMETHOD(Poll)(THIS) PURE;
    STDMETHOD(SendDeviceData)(THIS_ DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl) PURE;
    /*** IDirectInputDevice7A methods ***/
    STDMETHOD(EnumEffectsInFile)(THIS_ LPCSTR lpszFileName,LPDIENUMEFFECTSINFILECALLBACK pec,LPVOID pvRef,DWORD dwFlags) PURE;
    STDMETHOD(WriteEffectToFile)(THIS_ LPCSTR lpszFileName,DWORD dwEntries,LPDIFILEEFFECT rgDiFileEft,DWORD dwFlags) PURE;
};
#undef INTERFACE

/*****************************************************************************
 * IDirectInputDevice7W interface
 */
#define INTERFACE IDirectInputDevice7W
DECLARE_INTERFACE_(IDirectInputDevice7W,IDirectInputDevice2W)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputDeviceW methods ***/
    STDMETHOD(GetCapabilities)(THIS_ LPDIDEVCAPS lpDIDevCaps) PURE;
    STDMETHOD(EnumObjects)(THIS_ LPDIENUMDEVICEOBJECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetProperty)(THIS_ REFGUID rguidProp, LPDIPROPHEADER pdiph) PURE;
    STDMETHOD(SetProperty)(THIS_ REFGUID rguidProp, LPCDIPROPHEADER pdiph) PURE;
    STDMETHOD(Acquire)(THIS) PURE;
    STDMETHOD(Unacquire)(THIS) PURE;
    STDMETHOD(GetDeviceState)(THIS_ DWORD cbData, LPVOID lpvData) PURE;
    STDMETHOD(GetDeviceData)(THIS_ DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) PURE;
    STDMETHOD(SetDataFormat)(THIS_ LPCDIDATAFORMAT lpdf) PURE;
    STDMETHOD(SetEventNotification)(THIS_ HANDLE hEvent) PURE;
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND hwnd, DWORD dwFlags) PURE;
    STDMETHOD(GetObjectInfo)(THIS_ LPDIDEVICEOBJECTINSTANCEW pdidoi, DWORD dwObj, DWORD dwHow) PURE;
    STDMETHOD(GetDeviceInfo)(THIS_ LPDIDEVICEINSTANCEW pdidi) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) PURE;
    /*** IDirectInputDevice2W methods ***/
    STDMETHOD(CreateEffect)(THIS_ REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT *ppdeff, LPUNKNOWN punkOuter) PURE;
    STDMETHOD(EnumEffects)(THIS_ LPDIENUMEFFECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwEffType) PURE;
    STDMETHOD(GetEffectInfo)(THIS_ LPDIEFFECTINFOW pdei, REFGUID rguid) PURE;
    STDMETHOD(GetForceFeedbackState)(THIS_ LPDWORD pdwOut) PURE;
    STDMETHOD(SendForceFeedbackCommand)(THIS_ DWORD dwFlags) PURE;
    STDMETHOD(EnumCreatedEffectObjects)(THIS_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl) PURE;
    STDMETHOD(Escape)(THIS_ LPDIEFFESCAPE pesc) PURE;
    STDMETHOD(Poll)(THIS) PURE;
    STDMETHOD(SendDeviceData)(THIS_ DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl) PURE;
    /*** IDirectInputDevice7W methods ***/
    STDMETHOD(EnumEffectsInFile)(THIS_ LPCWSTR lpszFileName,LPDIENUMEFFECTSINFILECALLBACK pec,LPVOID pvRef,DWORD dwFlags) PURE;
    STDMETHOD(WriteEffectToFile)(THIS_ LPCWSTR lpszFileName,DWORD dwEntries,LPDIFILEEFFECT rgDiFileEft,DWORD dwFlags) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define IDirectInputDevice7_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IDirectInputDevice7_AddRef(p)             (p)->lpVtbl->AddRef(p)
#define IDirectInputDevice7_Release(p)            (p)->lpVtbl->Release(p)
/*** IDirectInputDevice methods ***/
#define IDirectInputDevice7_GetCapabilities(p,a)       (p)->lpVtbl->GetCapabilities(p,a)
#define IDirectInputDevice7_EnumObjects(p,a,b,c)       (p)->lpVtbl->EnumObjects(p,a,b,c)
#define IDirectInputDevice7_GetProperty(p,a,b)         (p)->lpVtbl->GetProperty(p,a,b)
#define IDirectInputDevice7_SetProperty(p,a,b)         (p)->lpVtbl->SetProperty(p,a,b)
#define IDirectInputDevice7_Acquire(p)                 (p)->lpVtbl->Acquire(p)
#define IDirectInputDevice7_Unacquire(p)               (p)->lpVtbl->Unacquire(p)
#define IDirectInputDevice7_GetDeviceState(p,a,b)      (p)->lpVtbl->GetDeviceState(p,a,b)
#define IDirectInputDevice7_GetDeviceData(p,a,b,c,d)   (p)->lpVtbl->GetDeviceData(p,a,b,c,d)
#define IDirectInputDevice7_SetDataFormat(p,a)         (p)->lpVtbl->SetDataFormat(p,a)
#define IDirectInputDevice7_SetEventNotification(p,a)  (p)->lpVtbl->SetEventNotification(p,a)
#define IDirectInputDevice7_SetCooperativeLevel(p,a,b) (p)->lpVtbl->SetCooperativeLevel(p,a,b)
#define IDirectInputDevice7_GetObjectInfo(p,a,b,c)     (p)->lpVtbl->GetObjectInfo(p,a,b,c)
#define IDirectInputDevice7_GetDeviceInfo(p,a)         (p)->lpVtbl->GetDeviceInfo(p,a)
#define IDirectInputDevice7_RunControlPanel(p,a,b)     (p)->lpVtbl->RunControlPanel(p,a,b)
#define IDirectInputDevice7_Initialize(p,a,b,c)        (p)->lpVtbl->Initialize(p,a,b,c)
/*** IDirectInputDevice2 methods ***/
#define IDirectInputDevice7_CreateEffect(p,a,b,c,d)           (p)->lpVtbl->CreateEffect(p,a,b,c,d)
#define IDirectInputDevice7_EnumEffects(p,a,b,c)              (p)->lpVtbl->EnumEffects(p,a,b,c)
#define IDirectInputDevice7_GetEffectInfo(p,a,b)              (p)->lpVtbl->GetEffectInfo(p,a,b)
#define IDirectInputDevice7_GetForceFeedbackState(p,a)        (p)->lpVtbl->GetForceFeedbackState(p,a)
#define IDirectInputDevice7_SendForceFeedbackCommand(p,a)     (p)->lpVtbl->SendForceFeedbackCommand(p,a)
#define IDirectInputDevice7_EnumCreatedEffectObjects(p,a,b,c) (p)->lpVtbl->EnumCreatedEffectObjects(p,a,b,c)
#define IDirectInputDevice7_Escape(p,a)                       (p)->lpVtbl->Escape(p,a)
#define IDirectInputDevice7_Poll(p)                           (p)->lpVtbl->Poll(p)
#define IDirectInputDevice7_SendDeviceData(p,a,b,c,d)         (p)->lpVtbl->SendDeviceData(p,a,b,c,d)
/*** IDirectInputDevice7 methods ***/
#define IDirectInputDevice7_EnumEffectsInFile(p,a,b,c,d) (p)->lpVtbl->EnumEffectsInFile(p,a,b,c,d)
#define IDirectInputDevice7_WriteEffectToFile(p,a,b,c,d) (p)->lpVtbl->WriteEffectToFile(p,a,b,c,d)
#else
/*** IUnknown methods ***/
#define IDirectInputDevice7_QueryInterface(p,a,b) (p)->QueryInterface(a,b)
#define IDirectInputDevice7_AddRef(p)             (p)->AddRef()
#define IDirectInputDevice7_Release(p)            (p)->Release()
/*** IDirectInputDevice methods ***/
#define IDirectInputDevice7_GetCapabilities(p,a)       (p)->GetCapabilities(a)
#define IDirectInputDevice7_EnumObjects(p,a,b,c)       (p)->EnumObjects(a,b,c)
#define IDirectInputDevice7_GetProperty(p,a,b)         (p)->GetProperty(a,b)
#define IDirectInputDevice7_SetProperty(p,a,b)         (p)->SetProperty(a,b)
#define IDirectInputDevice7_Acquire(p)                 (p)->Acquire()
#define IDirectInputDevice7_Unacquire(p)               (p)->Unacquire()
#define IDirectInputDevice7_GetDeviceState(p,a,b)      (p)->GetDeviceState(a,b)
#define IDirectInputDevice7_GetDeviceData(p,a,b,c,d)   (p)->GetDeviceData(a,b,c,d)
#define IDirectInputDevice7_SetDataFormat(p,a)         (p)->SetDataFormat(a)
#define IDirectInputDevice7_SetEventNotification(p,a)  (p)->SetEventNotification(a)
#define IDirectInputDevice7_SetCooperativeLevel(p,a,b) (p)->SetCooperativeLevel(a,b)
#define IDirectInputDevice7_GetObjectInfo(p,a,b,c)     (p)->GetObjectInfo(a,b,c)
#define IDirectInputDevice7_GetDeviceInfo(p,a)         (p)->GetDeviceInfo(a)
#define IDirectInputDevice7_RunControlPanel(p,a,b)     (p)->RunControlPanel(a,b)
#define IDirectInputDevice7_Initialize(p,a,b,c)        (p)->Initialize(a,b,c)
/*** IDirectInputDevice2 methods ***/
#define IDirectInputDevice7_CreateEffect(p,a,b,c,d)           (p)->CreateEffect(a,b,c,d)
#define IDirectInputDevice7_EnumEffects(p,a,b,c)              (p)->EnumEffects(a,b,c)
#define IDirectInputDevice7_GetEffectInfo(p,a,b)              (p)->GetEffectInfo(a,b)
#define IDirectInputDevice7_GetForceFeedbackState(p,a)        (p)->GetForceFeedbackState(a)
#define IDirectInputDevice7_SendForceFeedbackCommand(p,a)     (p)->SendForceFeedbackCommand(a)
#define IDirectInputDevice7_EnumCreatedEffectObjects(p,a,b,c) (p)->EnumCreatedEffectObjects(a,b,c)
#define IDirectInputDevice7_Escape(p,a)                       (p)->Escape(a)
#define IDirectInputDevice7_Poll(p)                           (p)->Poll()
#define IDirectInputDevice7_SendDeviceData(p,a,b,c,d)         (p)->SendDeviceData(a,b,c,d)
/*** IDirectInputDevice7 methods ***/
#define IDirectInputDevice7_EnumEffectsInFile(p,a,b,c,d) (p)->EnumEffectsInFile(a,b,c,d)
#define IDirectInputDevice7_WriteEffectToFile(p,a,b,c,d) (p)->WriteEffectToFile(a,b,c,d)
#endif


/*****************************************************************************
 * IDirectInputDevice8A interface
 */
#define INTERFACE IDirectInputDevice8A
DECLARE_INTERFACE_(IDirectInputDevice8A,IDirectInputDevice7A)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputDeviceA methods ***/
    STDMETHOD(GetCapabilities)(THIS_ LPDIDEVCAPS lpDIDevCaps) PURE;
    STDMETHOD(EnumObjects)(THIS_ LPDIENUMDEVICEOBJECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetProperty)(THIS_ REFGUID rguidProp, LPDIPROPHEADER pdiph) PURE;
    STDMETHOD(SetProperty)(THIS_ REFGUID rguidProp, LPCDIPROPHEADER pdiph) PURE;
    STDMETHOD(Acquire)(THIS) PURE;
    STDMETHOD(Unacquire)(THIS) PURE;
    STDMETHOD(GetDeviceState)(THIS_ DWORD cbData, LPVOID lpvData) PURE;
    STDMETHOD(GetDeviceData)(THIS_ DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) PURE;
    STDMETHOD(SetDataFormat)(THIS_ LPCDIDATAFORMAT lpdf) PURE;
    STDMETHOD(SetEventNotification)(THIS_ HANDLE hEvent) PURE;
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND hwnd, DWORD dwFlags) PURE;
    STDMETHOD(GetObjectInfo)(THIS_ LPDIDEVICEOBJECTINSTANCEA pdidoi, DWORD dwObj, DWORD dwHow) PURE;
    STDMETHOD(GetDeviceInfo)(THIS_ LPDIDEVICEINSTANCEA pdidi) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) PURE;
    /*** IDirectInputDevice2A methods ***/
    STDMETHOD(CreateEffect)(THIS_ REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT *ppdeff, LPUNKNOWN punkOuter) PURE;
    STDMETHOD(EnumEffects)(THIS_ LPDIENUMEFFECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwEffType) PURE;
    STDMETHOD(GetEffectInfo)(THIS_ LPDIEFFECTINFOA pdei, REFGUID rguid) PURE;
    STDMETHOD(GetForceFeedbackState)(THIS_ LPDWORD pdwOut) PURE;
    STDMETHOD(SendForceFeedbackCommand)(THIS_ DWORD dwFlags) PURE;
    STDMETHOD(EnumCreatedEffectObjects)(THIS_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl) PURE;
    STDMETHOD(Escape)(THIS_ LPDIEFFESCAPE pesc) PURE;
    STDMETHOD(Poll)(THIS) PURE;
    STDMETHOD(SendDeviceData)(THIS_ DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl) PURE;
    /*** IDirectInputDevice7A methods ***/
    STDMETHOD(EnumEffectsInFile)(THIS_ LPCSTR lpszFileName,LPDIENUMEFFECTSINFILECALLBACK pec,LPVOID pvRef,DWORD dwFlags) PURE;
    STDMETHOD(WriteEffectToFile)(THIS_ LPCSTR lpszFileName,DWORD dwEntries,LPDIFILEEFFECT rgDiFileEft,DWORD dwFlags) PURE;
    /*** IDirectInputDevice8A methods ***/
    STDMETHOD(BuildActionMap)(THIS_ LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags) PURE;
    STDMETHOD(SetActionMap)(THIS_ LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags) PURE;
    STDMETHOD(GetImageInfo)(THIS_ LPDIDEVICEIMAGEINFOHEADERA lpdiDevImageInfoHeader) PURE;
};
#undef INTERFACE

/*****************************************************************************
 * IDirectInputDevice8W interface
 */
#define INTERFACE IDirectInputDevice8W
DECLARE_INTERFACE_(IDirectInputDevice8W,IDirectInputDevice7W)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputDeviceW methods ***/
    STDMETHOD(GetCapabilities)(THIS_ LPDIDEVCAPS lpDIDevCaps) PURE;
    STDMETHOD(EnumObjects)(THIS_ LPDIENUMDEVICEOBJECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetProperty)(THIS_ REFGUID rguidProp, LPDIPROPHEADER pdiph) PURE;
    STDMETHOD(SetProperty)(THIS_ REFGUID rguidProp, LPCDIPROPHEADER pdiph) PURE;
    STDMETHOD(Acquire)(THIS) PURE;
    STDMETHOD(Unacquire)(THIS) PURE;
    STDMETHOD(GetDeviceState)(THIS_ DWORD cbData, LPVOID lpvData) PURE;
    STDMETHOD(GetDeviceData)(THIS_ DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) PURE;
    STDMETHOD(SetDataFormat)(THIS_ LPCDIDATAFORMAT lpdf) PURE;
    STDMETHOD(SetEventNotification)(THIS_ HANDLE hEvent) PURE;
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND hwnd, DWORD dwFlags) PURE;
    STDMETHOD(GetObjectInfo)(THIS_ LPDIDEVICEOBJECTINSTANCEW pdidoi, DWORD dwObj, DWORD dwHow) PURE;
    STDMETHOD(GetDeviceInfo)(THIS_ LPDIDEVICEINSTANCEW pdidi) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) PURE;
    /*** IDirectInputDevice2W methods ***/
    STDMETHOD(CreateEffect)(THIS_ REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT *ppdeff, LPUNKNOWN punkOuter) PURE;
    STDMETHOD(EnumEffects)(THIS_ LPDIENUMEFFECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwEffType) PURE;
    STDMETHOD(GetEffectInfo)(THIS_ LPDIEFFECTINFOW pdei, REFGUID rguid) PURE;
    STDMETHOD(GetForceFeedbackState)(THIS_ LPDWORD pdwOut) PURE;
    STDMETHOD(SendForceFeedbackCommand)(THIS_ DWORD dwFlags) PURE;
    STDMETHOD(EnumCreatedEffectObjects)(THIS_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl) PURE;
    STDMETHOD(Escape)(THIS_ LPDIEFFESCAPE pesc) PURE;
    STDMETHOD(Poll)(THIS) PURE;
    STDMETHOD(SendDeviceData)(THIS_ DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl) PURE;
    /*** IDirectInputDevice7W methods ***/
    STDMETHOD(EnumEffectsInFile)(THIS_ LPCWSTR lpszFileName,LPDIENUMEFFECTSINFILECALLBACK pec,LPVOID pvRef,DWORD dwFlags) PURE;
    STDMETHOD(WriteEffectToFile)(THIS_ LPCWSTR lpszFileName,DWORD dwEntries,LPDIFILEEFFECT rgDiFileEft,DWORD dwFlags) PURE;
    /*** IDirectInputDevice8W methods ***/
    STDMETHOD(BuildActionMap)(THIS_ LPDIACTIONFORMATW lpdiaf, LPCWSTR lpszUserName, DWORD dwFlags) PURE;
    STDMETHOD(SetActionMap)(THIS_ LPDIACTIONFORMATW lpdiaf, LPCWSTR lpszUserName, DWORD dwFlags) PURE;
    STDMETHOD(GetImageInfo)(THIS_ LPDIDEVICEIMAGEINFOHEADERW lpdiDevImageInfoHeader) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define IDirectInputDevice8_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IDirectInputDevice8_AddRef(p)             (p)->lpVtbl->AddRef(p)
#define IDirectInputDevice8_Release(p)            (p)->lpVtbl->Release(p)
/*** IDirectInputDevice methods ***/
#define IDirectInputDevice8_GetCapabilities(p,a)       (p)->lpVtbl->GetCapabilities(p,a)
#define IDirectInputDevice8_EnumObjects(p,a,b,c)       (p)->lpVtbl->EnumObjects(p,a,b,c)
#define IDirectInputDevice8_GetProperty(p,a,b)         (p)->lpVtbl->GetProperty(p,a,b)
#define IDirectInputDevice8_SetProperty(p,a,b)         (p)->lpVtbl->SetProperty(p,a,b)
#define IDirectInputDevice8_Acquire(p)                 (p)->lpVtbl->Acquire(p)
#define IDirectInputDevice8_Unacquire(p)               (p)->lpVtbl->Unacquire(p)
#define IDirectInputDevice8_GetDeviceState(p,a,b)      (p)->lpVtbl->GetDeviceState(p,a,b)
#define IDirectInputDevice8_GetDeviceData(p,a,b,c,d)   (p)->lpVtbl->GetDeviceData(p,a,b,c,d)
#define IDirectInputDevice8_SetDataFormat(p,a)         (p)->lpVtbl->SetDataFormat(p,a)
#define IDirectInputDevice8_SetEventNotification(p,a)  (p)->lpVtbl->SetEventNotification(p,a)
#define IDirectInputDevice8_SetCooperativeLevel(p,a,b) (p)->lpVtbl->SetCooperativeLevel(p,a,b)
#define IDirectInputDevice8_GetObjectInfo(p,a,b,c)     (p)->lpVtbl->GetObjectInfo(p,a,b,c)
#define IDirectInputDevice8_GetDeviceInfo(p,a)         (p)->lpVtbl->GetDeviceInfo(p,a)
#define IDirectInputDevice8_RunControlPanel(p,a,b)     (p)->lpVtbl->RunControlPanel(p,a,b)
#define IDirectInputDevice8_Initialize(p,a,b,c)        (p)->lpVtbl->Initialize(p,a,b,c)
/*** IDirectInputDevice2 methods ***/
#define IDirectInputDevice8_CreateEffect(p,a,b,c,d)           (p)->lpVtbl->CreateEffect(p,a,b,c,d)
#define IDirectInputDevice8_EnumEffects(p,a,b,c)              (p)->lpVtbl->EnumEffects(p,a,b,c)
#define IDirectInputDevice8_GetEffectInfo(p,a,b)              (p)->lpVtbl->GetEffectInfo(p,a,b)
#define IDirectInputDevice8_GetForceFeedbackState(p,a)        (p)->lpVtbl->GetForceFeedbackState(p,a)
#define IDirectInputDevice8_SendForceFeedbackCommand(p,a)     (p)->lpVtbl->SendForceFeedbackCommand(p,a)
#define IDirectInputDevice8_EnumCreatedEffectObjects(p,a,b,c) (p)->lpVtbl->EnumCreatedEffectObjects(p,a,b,c)
#define IDirectInputDevice8_Escape(p,a)                       (p)->lpVtbl->Escape(p,a)
#define IDirectInputDevice8_Poll(p)                           (p)->lpVtbl->Poll(p)
#define IDirectInputDevice8_SendDeviceData(p,a,b,c,d)         (p)->lpVtbl->SendDeviceData(p,a,b,c,d)
/*** IDirectInputDevice7 methods ***/
#define IDirectInputDevice8_EnumEffectsInFile(p,a,b,c,d) (p)->lpVtbl->EnumEffectsInFile(p,a,b,c,d)
#define IDirectInputDevice8_WriteEffectToFile(p,a,b,c,d) (p)->lpVtbl->WriteEffectToFile(p,a,b,c,d)
/*** IDirectInputDevice8 methods ***/
#define IDirectInputDevice8_BuildActionMap(p,a,b,c) (p)->lpVtbl->BuildActionMap(p,a,b,c)
#define IDirectInputDevice8_SetActionMap(p,a,b,c)   (p)->lpVtbl->SetActionMap(p,a,b,c)
#define IDirectInputDevice8_GetImageInfo(p,a)       (p)->lpVtbl->GetImageInfo(p,a)
#else
/*** IUnknown methods ***/
#define IDirectInputDevice8_QueryInterface(p,a,b) (p)->QueryInterface(a,b)
#define IDirectInputDevice8_AddRef(p)             (p)->AddRef()
#define IDirectInputDevice8_Release(p)            (p)->Release()
/*** IDirectInputDevice methods ***/
#define IDirectInputDevice8_GetCapabilities(p,a)       (p)->GetCapabilities(a)
#define IDirectInputDevice8_EnumObjects(p,a,b,c)       (p)->EnumObjects(a,b,c)
#define IDirectInputDevice8_GetProperty(p,a,b)         (p)->GetProperty(a,b)
#define IDirectInputDevice8_SetProperty(p,a,b)         (p)->SetProperty(a,b)
#define IDirectInputDevice8_Acquire(p)                 (p)->Acquire()
#define IDirectInputDevice8_Unacquire(p)               (p)->Unacquire()
#define IDirectInputDevice8_GetDeviceState(p,a,b)      (p)->GetDeviceState(a,b)
#define IDirectInputDevice8_GetDeviceData(p,a,b,c,d)   (p)->GetDeviceData(a,b,c,d)
#define IDirectInputDevice8_SetDataFormat(p,a)         (p)->SetDataFormat(a)
#define IDirectInputDevice8_SetEventNotification(p,a)  (p)->SetEventNotification(a)
#define IDirectInputDevice8_SetCooperativeLevel(p,a,b) (p)->SetCooperativeLevel(a,b)
#define IDirectInputDevice8_GetObjectInfo(p,a,b,c)     (p)->GetObjectInfo(a,b,c)
#define IDirectInputDevice8_GetDeviceInfo(p,a)         (p)->GetDeviceInfo(a)
#define IDirectInputDevice8_RunControlPanel(p,a,b)     (p)->RunControlPanel(a,b)
#define IDirectInputDevice8_Initialize(p,a,b,c)        (p)->Initialize(a,b,c)
/*** IDirectInputDevice2 methods ***/
#define IDirectInputDevice8_CreateEffect(p,a,b,c,d)           (p)->CreateEffect(a,b,c,d)
#define IDirectInputDevice8_EnumEffects(p,a,b,c)              (p)->EnumEffects(a,b,c)
#define IDirectInputDevice8_GetEffectInfo(p,a,b)              (p)->GetEffectInfo(a,b)
#define IDirectInputDevice8_GetForceFeedbackState(p,a)        (p)->GetForceFeedbackState(a)
#define IDirectInputDevice8_SendForceFeedbackCommand(p,a)     (p)->SendForceFeedbackCommand(a)
#define IDirectInputDevice8_EnumCreatedEffectObjects(p,a,b,c) (p)->EnumCreatedEffectObjects(a,b,c)
#define IDirectInputDevice8_Escape(p,a)                       (p)->Escape(a)
#define IDirectInputDevice8_Poll(p)                           (p)->Poll()
#define IDirectInputDevice8_SendDeviceData(p,a,b,c,d)         (p)->SendDeviceData(a,b,c,d)
/*** IDirectInputDevice7 methods ***/
#define IDirectInputDevice8_EnumEffectsInFile(p,a,b,c,d) (p)->EnumEffectsInFile(a,b,c,d)
#define IDirectInputDevice8_WriteEffectToFile(p,a,b,c,d) (p)->WriteEffectToFile(a,b,c,d)
/*** IDirectInputDevice8 methods ***/
#define IDirectInputDevice8_BuildActionMap(p,a,b,c) (p)->BuildActionMap(a,b,c)
#define IDirectInputDevice8_SetActionMap(p,a,b,c)   (p)->SetActionMap(a,b,c)
#define IDirectInputDevice8_GetImageInfo(p,a)       (p)->GetImageInfo(a)
#endif

/* "Standard" Mouse report... */
typedef struct DIMOUSESTATE {
  LONG lX;
  LONG lY;
  LONG lZ;
  BYTE rgbButtons[4];
} DIMOUSESTATE;

/* "Standard" Mouse report for DInput 7... */
typedef struct DIMOUSESTATE2 {
  LONG lX;
  LONG lY;
  LONG lZ;
  BYTE rgbButtons[8];
} DIMOUSESTATE2;

#define DIMOFS_X        FIELD_OFFSET(DIMOUSESTATE, lX)
#define DIMOFS_Y        FIELD_OFFSET(DIMOUSESTATE, lY)
#define DIMOFS_Z        FIELD_OFFSET(DIMOUSESTATE, lZ)
#define DIMOFS_BUTTON0 (FIELD_OFFSET(DIMOUSESTATE, rgbButtons) + 0)
#define DIMOFS_BUTTON1 (FIELD_OFFSET(DIMOUSESTATE, rgbButtons) + 1)
#define DIMOFS_BUTTON2 (FIELD_OFFSET(DIMOUSESTATE, rgbButtons) + 2)
#define DIMOFS_BUTTON3 (FIELD_OFFSET(DIMOUSESTATE, rgbButtons) + 3)
#define DIMOFS_BUTTON4 (FIELD_OFFSET(DIMOUSESTATE2, rgbButtons) + 4)
#define DIMOFS_BUTTON5 (FIELD_OFFSET(DIMOUSESTATE2, rgbButtons) + 5)
#define DIMOFS_BUTTON6 (FIELD_OFFSET(DIMOUSESTATE2, rgbButtons) + 6)
#define DIMOFS_BUTTON7 (FIELD_OFFSET(DIMOUSESTATE2, rgbButtons) + 7)

/* New DirectInput8 mouse definitions */

#define DIMOUSE_XAXISAB            (0x82000200 | DIMOFS_X)
#define DIMOUSE_YAXISAB            (0x82000200 | DIMOFS_Y)
#define DIMOUSE_XAXIS              (0x82000300 | DIMOFS_X)
#define DIMOUSE_YAXIS              (0x82000300 | DIMOFS_Y)
#define DIMOUSE_WHEEL              (0x82000300 | DIMOFS_Z)
#define DIMOUSE_BUTTON0            (0x82000400 | DIMOFS_BUTTON0)
#define DIMOUSE_BUTTON1            (0x82000400 | DIMOFS_BUTTON1)
#define DIMOUSE_BUTTON2            (0x82000400 | DIMOFS_BUTTON2)
#define DIMOUSE_BUTTON3            (0x82000400 | DIMOFS_BUTTON3)
#define DIMOUSE_BUTTON4            (0x82000400 | DIMOFS_BUTTON4)
#define DIMOUSE_BUTTON5            (0x82000400 | DIMOFS_BUTTON5)
#define DIMOUSE_BUTTON6            (0x82000400 | DIMOFS_BUTTON6)
#define DIMOUSE_BUTTON7            (0x82000400 | DIMOFS_BUTTON7)

#ifdef __cplusplus
extern "C" {
#endif
extern const DIDATAFORMAT c_dfDIMouse;
extern const DIDATAFORMAT c_dfDIMouse2; /* DX 7 */
extern const DIDATAFORMAT c_dfDIKeyboard;
extern const DIDATAFORMAT c_dfDIJoystick;
extern const DIDATAFORMAT c_dfDIJoystick2;
#ifdef __cplusplus
};
#endif

#define DIVIRTUAL_DRIVING_RACE     0x01000000
#define DIAXIS_DRIVINGR_STEER      0x01008a01
#define DIAXIS_DRIVINGR_ACCELERATE 0x01039202

#define DIVOICE_CHANNEL1           0x83000401

#define DIAXIS_ANY_X_1             0xFF00C201
#define DIAXIS_ANY_X_2             0xFF00C202
#define DIAXIS_ANY_Y_1             0xFF014201
#define DIAXIS_ANY_Y_2             0xFF014202
#define DIAXIS_ANY_Z_1             0xFF01C201
#define DIAXIS_ANY_Z_2             0xFF01C202
#define DIAXIS_ANY_R_1             0xFF024201
#define DIAXIS_ANY_R_2             0xFF024202
#define DIAXIS_ANY_U_1             0xFF02C201
#define DIAXIS_ANY_U_2             0xFF02C202
#define DIAXIS_ANY_V_1             0xFF034201
#define DIAXIS_ANY_V_2             0xFF034202
#define DIAXIS_ANY_A_1             0xFF03C201
#define DIAXIS_ANY_A_2             0xFF03C202
#define DIAXIS_ANY_B_1             0xFF044201
#define DIAXIS_ANY_B_2             0xFF044202
#define DIAXIS_ANY_C_1             0xFF04C201
#define DIAXIS_ANY_C_2             0xFF04C202
#define DIAXIS_ANY_S_1             0xFF054201
#define DIAXIS_ANY_S_2             0xFF054202
#define DIAXIS_ANY_1               0xFF004201
#define DIAXIS_ANY_2               0xFF004202
#define DIAXIS_ANY_3               0xFF004203
#define DIAXIS_ANY_4               0xFF004204
#define DIPOV_ANY_1                0xFF004601
#define DIPOV_ANY_2                0xFF004602
#define DIPOV_ANY_3                0xFF004603
#define DIPOV_ANY_4                0xFF004604
#define DIBUTTON_ANY(instance)     (0xFF004400 | (instance))

#define DIVIRTUAL_FLYING_HELICOPTER        0x06000000
#define DIBUTTON_FLYINGH_MENU              0x060004fd
#define DIBUTTON_FLYINGH_FIRE              0x06001401
#define DIBUTTON_FLYINGH_WEAPONS           0x06001402
#define DIBUTTON_FLYINGH_TARGET            0x06001403
#define DIBUTTON_FLYINGH_DEVICE            0x060044fe
#define DIBUTTON_FLYINGH_PAUSE             0x060044fc
#define DIHATSWITCH_FLYINGH_GLANCE         0x06004601
#define DIBUTTON_FLYINGH_FIRESECONDARY     0x06004c07
#define DIBUTTON_FLYINGH_COUNTER           0x06005404
#define DIBUTTON_FLYINGH_VIEW              0x06006405
#define DIBUTTON_FLYINGH_GEAR              0x06006406
#define DIAXIS_FLYINGH_BANK                0x06008a01
#define DIAXIS_FLYINGH_PITCH               0x06010a02
#define DIAXIS_FLYINGH_COLLECTIVE          0x06018a03
#define DIAXIS_FLYINGH_TORQUE              0x06025a04
#define DIAXIS_FLYINGH_THROTTLE            0x0603da05
#define DIBUTTON_FLYINGH_FASTER_LINK       0x0603dce0
#define DIBUTTON_FLYINGH_SLOWER_LINK       0x0603dce8
#define DIBUTTON_FLYINGH_GLANCE_LEFT_LINK  0x0607c4e4
#define DIBUTTON_FLYINGH_GLANCE_RIGHT_LINK 0x0607c4ec
#define DIBUTTON_FLYINGH_GLANCE_UP_LINK    0x0607c4e0
#define DIBUTTON_FLYINGH_GLANCE_DOWN_LINK  0x0607c4e8

#define DIVIRTUAL_SPACESIM                  0x07000000
#define DIBUTTON_SPACESIM_FIRE              0x07000401
#define DIBUTTON_SPACESIM_WEAPONS           0x07000402
#define DIBUTTON_SPACESIM_TARGET            0x07000403
#define DIBUTTON_SPACESIM_MENU              0x070004fd
#define DIBUTTON_SPACESIM_VIEW              0x07004404
#define DIBUTTON_SPACESIM_DISPLAY           0x07004405
#define DIBUTTON_SPACESIM_RAISE             0x07004406
#define DIBUTTON_SPACESIM_LOWER             0x07004407
#define DIBUTTON_SPACESIM_GEAR              0x07004408
#define DIBUTTON_SPACESIM_FIRESECONDARY     0x07004409
#define DIBUTTON_SPACESIM_PAUSE             0x070044fc
#define DIBUTTON_SPACESIM_DEVICE            0x070044fe
#define DIHATSWITCH_SPACESIM_GLANCE         0x07004601
#define DIBUTTON_SPACESIM_LEFT_LINK         0x0700c4e4
#define DIBUTTON_SPACESIM_RIGHT_LINK        0x0700c4ec
#define DIAXIS_SPACESIM_LATERAL             0x07008201
#define DIAXIS_SPACESIM_MOVE                0x07010202
#define DIBUTTON_SPACESIM_FORWARD_LINK      0x070144e0
#define DIBUTTON_SPACESIM_BACKWARD_LINK     0x070144e8
#define DIAXIS_SPACESIM_CLIMB               0x0701c204
#define DIAXIS_SPACESIM_ROTATE              0x07024205
#define DIBUTTON_SPACESIM_TURN_LEFT_LINK    0x070244e4
#define DIBUTTON_SPACESIM_TURN_RIGHT_LINK   0x070244ec
#define DIAXIS_SPACESIM_THROTTLE            0x07038203
#define DIBUTTON_SPACESIM_FASTER_LINK       0x0703c4e0
#define DIBUTTON_SPACESIM_SLOWER_LINK       0x0703c4e8
#define DIBUTTON_SPACESIM_GLANCE_UP_LINK    0x0707c4e0
#define DIBUTTON_SPACESIM_GLANCE_LEFT_LINK  0x0707c4e4
#define DIBUTTON_SPACESIM_GLANCE_DOWN_LINK  0x0707c4e8
#define DIBUTTON_SPACESIM_GLANCE_RIGHT_LINK 0x0707c4ec

/*****************************************************************************
 * IDirectInputA interface
 */
#define INTERFACE IDirectInputA
DECLARE_INTERFACE_(IDirectInputA,IUnknown)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputA methods ***/
    STDMETHOD(CreateDevice)(THIS_ REFGUID rguid, LPDIRECTINPUTDEVICEA *lplpDirectInputDevice, LPUNKNOWN pUnkOuter) PURE;
    STDMETHOD(EnumDevices)(THIS_ DWORD dwDevType, LPDIENUMDEVICESCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetDeviceStatus)(THIS_ REFGUID rguidInstance) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion) PURE;
};
#undef INTERFACE

/*****************************************************************************
 * IDirectInputW interface
 */
#define INTERFACE IDirectInputW
DECLARE_INTERFACE_(IDirectInputW,IUnknown)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputW methods ***/
    STDMETHOD(CreateDevice)(THIS_ REFGUID rguid, LPDIRECTINPUTDEVICEW *lplpDirectInputDevice, LPUNKNOWN pUnkOuter) PURE;
    STDMETHOD(EnumDevices)(THIS_ DWORD dwDevType, LPDIENUMDEVICESCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetDeviceStatus)(THIS_ REFGUID rguidInstance) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define IDirectInput_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IDirectInput_AddRef(p)             (p)->lpVtbl->AddRef(p)
#define IDirectInput_Release(p)            (p)->lpVtbl->Release(p)
/*** IDirectInput methods ***/
#define IDirectInput_CreateDevice(p,a,b,c)  (p)->lpVtbl->CreateDevice(p,a,b,c)
#define IDirectInput_EnumDevices(p,a,b,c,d) (p)->lpVtbl->EnumDevices(p,a,b,c,d)
#define IDirectInput_GetDeviceStatus(p,a)   (p)->lpVtbl->GetDeviceStatus(p,a)
#define IDirectInput_RunControlPanel(p,a,b) (p)->lpVtbl->RunControlPanel(p,a,b)
#define IDirectInput_Initialize(p,a,b)      (p)->lpVtbl->Initialize(p,a,b)
#else
/*** IUnknown methods ***/
#define IDirectInput_QueryInterface(p,a,b) (p)->QueryInterface(a,b)
#define IDirectInput_AddRef(p)             (p)->AddRef()
#define IDirectInput_Release(p)            (p)->Release()
/*** IDirectInput methods ***/
#define IDirectInput_CreateDevice(p,a,b,c)  (p)->CreateDevice(a,b,c)
#define IDirectInput_EnumDevices(p,a,b,c,d) (p)->EnumDevices(a,b,c,d)
#define IDirectInput_GetDeviceStatus(p,a)   (p)->GetDeviceStatus(a)
#define IDirectInput_RunControlPanel(p,a,b) (p)->RunControlPanel(a,b)
#define IDirectInput_Initialize(p,a,b)      (p)->Initialize(a,b)
#endif

/*****************************************************************************
 * IDirectInput2A interface
 */
#define INTERFACE IDirectInput2A
DECLARE_INTERFACE_(IDirectInput2A,IDirectInputA)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputA methods ***/
    STDMETHOD(CreateDevice)(THIS_ REFGUID rguid, LPDIRECTINPUTDEVICEA *lplpDirectInputDevice, LPUNKNOWN pUnkOuter) PURE;
    STDMETHOD(EnumDevices)(THIS_ DWORD dwDevType, LPDIENUMDEVICESCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetDeviceStatus)(THIS_ REFGUID rguidInstance) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion) PURE;
    /*** IDirectInput2A methods ***/
    STDMETHOD(FindDevice)(THIS_ REFGUID rguid, LPCSTR pszName, LPGUID pguidInstance) PURE;
};
#undef INTERFACE

/*****************************************************************************
 * IDirectInput2W interface
 */
#define INTERFACE IDirectInput2W
DECLARE_INTERFACE_(IDirectInput2W,IDirectInputW)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputW methods ***/
    STDMETHOD(CreateDevice)(THIS_ REFGUID rguid, LPDIRECTINPUTDEVICEW *lplpDirectInputDevice, LPUNKNOWN pUnkOuter) PURE;
    STDMETHOD(EnumDevices)(THIS_ DWORD dwDevType, LPDIENUMDEVICESCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetDeviceStatus)(THIS_ REFGUID rguidInstance) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion) PURE;
    /*** IDirectInput2W methods ***/
    STDMETHOD(FindDevice)(THIS_ REFGUID rguid, LPCWSTR pszName, LPGUID pguidInstance) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define IDirectInput2_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IDirectInput2_AddRef(p)             (p)->lpVtbl->AddRef(p)
#define IDirectInput2_Release(p)            (p)->lpVtbl->Release(p)
/*** IDirectInput methods ***/
#define IDirectInput2_CreateDevice(p,a,b,c)  (p)->lpVtbl->CreateDevice(p,a,b,c)
#define IDirectInput2_EnumDevices(p,a,b,c,d) (p)->lpVtbl->EnumDevices(p,a,b,c,d)
#define IDirectInput2_GetDeviceStatus(p,a)   (p)->lpVtbl->GetDeviceStatus(p,a)
#define IDirectInput2_RunControlPanel(p,a,b) (p)->lpVtbl->RunControlPanel(p,a,b)
#define IDirectInput2_Initialize(p,a,b)      (p)->lpVtbl->Initialize(p,a,b)
/*** IDirectInput2 methods ***/
#define IDirectInput2_FindDevice(p,a,b,c)    (p)->lpVtbl->FindDevice(p,a,b,c)
#else
/*** IUnknown methods ***/
#define IDirectInput2_QueryInterface(p,a,b) (p)->QueryInterface(a,b)
#define IDirectInput2_AddRef(p)             (p)->AddRef()
#define IDirectInput2_Release(p)            (p)->Release()
/*** IDirectInput methods ***/
#define IDirectInput2_CreateDevice(p,a,b,c)  (p)->CreateDevice(a,b,c)
#define IDirectInput2_EnumDevices(p,a,b,c,d) (p)->EnumDevices(a,b,c,d)
#define IDirectInput2_GetDeviceStatus(p,a)   (p)->GetDeviceStatus(a)
#define IDirectInput2_RunControlPanel(p,a,b) (p)->RunControlPanel(a,b)
#define IDirectInput2_Initialize(p,a,b)      (p)->Initialize(a,b)
/*** IDirectInput2 methods ***/
#define IDirectInput2_FindDevice(p,a,b,c)    (p)->FindDevice(a,b,c)
#endif

/*****************************************************************************
 * IDirectInput7A interface
 */
#define INTERFACE IDirectInput7A
DECLARE_INTERFACE_(IDirectInput7A,IDirectInput2A)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputA methods ***/
    STDMETHOD(CreateDevice)(THIS_ REFGUID rguid, LPDIRECTINPUTDEVICEA *lplpDirectInputDevice, LPUNKNOWN pUnkOuter) PURE;
    STDMETHOD(EnumDevices)(THIS_ DWORD dwDevType, LPDIENUMDEVICESCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetDeviceStatus)(THIS_ REFGUID rguidInstance) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion) PURE;
    /*** IDirectInput2A methods ***/
    STDMETHOD(FindDevice)(THIS_ REFGUID rguid, LPCSTR pszName, LPGUID pguidInstance) PURE;
    /*** IDirectInput7A methods ***/
    STDMETHOD(CreateDeviceEx)(THIS_ REFGUID rguid, REFIID riid, LPVOID *pvOut, LPUNKNOWN lpUnknownOuter) PURE;
};
#undef INTERFACE

/*****************************************************************************
 * IDirectInput7W interface
 */
#define INTERFACE IDirectInput7W
DECLARE_INTERFACE_(IDirectInput7W,IDirectInput2W)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInputW methods ***/
    STDMETHOD(CreateDevice)(THIS_ REFGUID rguid, LPDIRECTINPUTDEVICEW *lplpDirectInputDevice, LPUNKNOWN pUnkOuter) PURE;
    STDMETHOD(EnumDevices)(THIS_ DWORD dwDevType, LPDIENUMDEVICESCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetDeviceStatus)(THIS_ REFGUID rguidInstance) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion) PURE;
    /*** IDirectInput2W methods ***/
    STDMETHOD(FindDevice)(THIS_ REFGUID rguid, LPCWSTR pszName, LPGUID pguidInstance) PURE;
    /*** IDirectInput7W methods ***/
    STDMETHOD(CreateDeviceEx)(THIS_ REFGUID rguid, REFIID riid, LPVOID *pvOut, LPUNKNOWN lpUnknownOuter) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define IDirectInput7_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IDirectInput7_AddRef(p)             (p)->lpVtbl->AddRef(p)
#define IDirectInput7_Release(p)            (p)->lpVtbl->Release(p)
/*** IDirectInput methods ***/
#define IDirectInput7_CreateDevice(p,a,b,c)  (p)->lpVtbl->CreateDevice(p,a,b,c)
#define IDirectInput7_EnumDevices(p,a,b,c,d) (p)->lpVtbl->EnumDevices(p,a,b,c,d)
#define IDirectInput7_GetDeviceStatus(p,a)   (p)->lpVtbl->GetDeviceStatus(p,a)
#define IDirectInput7_RunControlPanel(p,a,b) (p)->lpVtbl->RunControlPanel(p,a,b)
#define IDirectInput7_Initialize(p,a,b)      (p)->lpVtbl->Initialize(p,a,b)
/*** IDirectInput2 methods ***/
#define IDirectInput7_FindDevice(p,a,b,c)    (p)->lpVtbl->FindDevice(p,a,b,c)
/*** IDirectInput7 methods ***/
#define IDirectInput7_CreateDeviceEx(p,a,b,c,d) (p)->lpVtbl->CreateDeviceEx(p,a,b,c,d)
#else
/*** IUnknown methods ***/
#define IDirectInput7_QueryInterface(p,a,b) (p)->QueryInterface(a,b)
#define IDirectInput7_AddRef(p)             (p)->AddRef()
#define IDirectInput7_Release(p)            (p)->Release()
/*** IDirectInput methods ***/
#define IDirectInput7_CreateDevice(p,a,b,c)  (p)->CreateDevice(a,b,c)
#define IDirectInput7_EnumDevices(p,a,b,c,d) (p)->EnumDevices(a,b,c,d)
#define IDirectInput7_GetDeviceStatus(p,a)   (p)->GetDeviceStatus(a)
#define IDirectInput7_RunControlPanel(p,a,b) (p)->RunControlPanel(a,b)
#define IDirectInput7_Initialize(p,a,b)      (p)->Initialize(a,b)
/*** IDirectInput2 methods ***/
#define IDirectInput7_FindDevice(p,a,b,c)    (p)->FindDevice(a,b,c)
/*** IDirectInput7 methods ***/
#define IDirectInput7_CreateDeviceEx(p,a,b,c,d) (p)->CreateDeviceEx(a,b,c,d)
#endif


/*****************************************************************************
 * IDirectInput8A interface
 */
#define INTERFACE IDirectInput8A
DECLARE_INTERFACE_(IDirectInput8A,IUnknown)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInput8A methods ***/
    STDMETHOD(CreateDevice)(THIS_ REFGUID rguid, LPDIRECTINPUTDEVICE8A *lplpDirectInputDevice, LPUNKNOWN pUnkOuter) PURE;
    STDMETHOD(EnumDevices)(THIS_ DWORD dwDevType, LPDIENUMDEVICESCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetDeviceStatus)(THIS_ REFGUID rguidInstance) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion) PURE;
    STDMETHOD(FindDevice)(THIS_ REFGUID rguid, LPCSTR pszName, LPGUID pguidInstance) PURE;
    STDMETHOD(EnumDevicesBySemantics)(THIS_ LPCSTR ptszUserName, LPDIACTIONFORMATA lpdiActionFormat, LPDIENUMDEVICESBYSEMANTICSCBA lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(ConfigureDevices)(THIS_ LPDICONFIGUREDEVICESCALLBACK lpdiCallback, LPDICONFIGUREDEVICESPARAMSA lpdiCDParams, DWORD dwFlags, LPVOID pvRefData) PURE;
};
#undef INTERFACE

/*****************************************************************************
 * IDirectInput8W interface
 */
#define INTERFACE IDirectInput8W
DECLARE_INTERFACE_(IDirectInput8W,IUnknown)
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    /*** IDirectInput8W methods ***/
    STDMETHOD(CreateDevice)(THIS_ REFGUID rguid, LPDIRECTINPUTDEVICE8W *lplpDirectInputDevice, LPUNKNOWN pUnkOuter) PURE;
    STDMETHOD(EnumDevices)(THIS_ DWORD dwDevType, LPDIENUMDEVICESCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(GetDeviceStatus)(THIS_ REFGUID rguidInstance) PURE;
    STDMETHOD(RunControlPanel)(THIS_ HWND hwndOwner, DWORD dwFlags) PURE;
    STDMETHOD(Initialize)(THIS_ HINSTANCE hinst, DWORD dwVersion) PURE;
    STDMETHOD(FindDevice)(THIS_ REFGUID rguid, LPCWSTR pszName, LPGUID pguidInstance) PURE;
    STDMETHOD(EnumDevicesBySemantics)(THIS_ LPCWSTR ptszUserName, LPDIACTIONFORMATW lpdiActionFormat, LPDIENUMDEVICESBYSEMANTICSCBW lpCallback, LPVOID pvRef, DWORD dwFlags) PURE;
    STDMETHOD(ConfigureDevices)(THIS_ LPDICONFIGUREDEVICESCALLBACK lpdiCallback, LPDICONFIGUREDEVICESPARAMSW lpdiCDParams, DWORD dwFlags, LPVOID pvRefData) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define IDirectInput8_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IDirectInput8_AddRef(p)             (p)->lpVtbl->AddRef(p)
#define IDirectInput8_Release(p)            (p)->lpVtbl->Release(p)
/*** IDirectInput8 methods ***/
#define IDirectInput8_CreateDevice(p,a,b,c)       (p)->lpVtbl->CreateDevice(p,a,b,c)
#define IDirectInput8_EnumDevices(p,a,b,c,d)      (p)->lpVtbl->EnumDevices(p,a,b,c,d)
#define IDirectInput8_GetDeviceStatus(p,a)        (p)->lpVtbl->GetDeviceStatus(p,a)
#define IDirectInput8_RunControlPanel(p,a,b)      (p)->lpVtbl->RunControlPanel(p,a,b)
#define IDirectInput8_Initialize(p,a,b)           (p)->lpVtbl->Initialize(p,a,b)
#define IDirectInput8_FindDevice(p,a,b,c)         (p)->lpVtbl->FindDevice(p,a,b,c)
#define IDirectInput8_EnumDevicesBySemantics(p,a,b,c,d,e) (p)->lpVtbl->EnumDevicesBySemantics(p,a,b,c,d,e)
#define IDirectInput8_ConfigureDevices(p,a,b,c,d) (p)->lpVtbl->ConfigureDevices(p,a,b,c,d)
#else
/*** IUnknown methods ***/
#define IDirectInput8_QueryInterface(p,a,b) (p)->QueryInterface(a,b)
#define IDirectInput8_AddRef(p)             (p)->AddRef()
#define IDirectInput8_Release(p)            (p)->Release()
/*** IDirectInput8 methods ***/
#define IDirectInput8_CreateDevice(p,a,b,c)       (p)->CreateDevice(a,b,c)
#define IDirectInput8_EnumDevices(p,a,b,c,d)      (p)->EnumDevices(a,b,c,d)
#define IDirectInput8_GetDeviceStatus(p,a)        (p)->GetDeviceStatus(a)
#define IDirectInput8_RunControlPanel(p,a,b)      (p)->RunControlPanel(a,b)
#define IDirectInput8_Initialize(p,a,b)           (p)->Initialize(a,b)
#define IDirectInput8_FindDevice(p,a,b,c)         (p)->FindDevice(a,b,c)
#define IDirectInput8_EnumDevicesBySemantics(p,a,b,c,d,e) (p)->EnumDevicesBySemantics(a,b,c,d,e)
#define IDirectInput8_ConfigureDevices(p,a,b,c,d) (p)->ConfigureDevices(a,b,c,d)
#endif


/* Export functions */

#ifdef __cplusplus
extern "C" {
#endif

HRESULT WINAPI DirectInput8Create(HINSTANCE,DWORD,REFIID,LPVOID *,LPUNKNOWN);

HRESULT WINAPI DirectInputCreateA(HINSTANCE,DWORD,LPDIRECTINPUTA *,LPUNKNOWN);
HRESULT WINAPI DirectInputCreateW(HINSTANCE,DWORD,LPDIRECTINPUTW *,LPUNKNOWN);
#define DirectInputCreate WINELIB_NAME_AW(DirectInputCreate)

HRESULT WINAPI DirectInputCreateEx(HINSTANCE,DWORD,REFIID,LPVOID *,LPUNKNOWN);

#ifdef __cplusplus
};
#endif

#ifndef DIAXIS_2DCONTROL_INOUT
#define DIAXIS_2DCONTROL_INOUT 0x23018203
#endif
#ifndef DIAXIS_2DCONTROL_LATERAL
#define DIAXIS_2DCONTROL_LATERAL 0x23008201
#endif
#ifndef DIAXIS_2DCONTROL_MOVE
#define DIAXIS_2DCONTROL_MOVE 0x23010202
#endif
#ifndef DIAXIS_2DCONTROL_ROTATEZ
#define DIAXIS_2DCONTROL_ROTATEZ 0x23024204
#endif
#ifndef DIAXIS_3DCONTROL_INOUT
#define DIAXIS_3DCONTROL_INOUT 0x24018203
#endif
#ifndef DIAXIS_3DCONTROL_LATERAL
#define DIAXIS_3DCONTROL_LATERAL 0x24008201
#endif
#ifndef DIAXIS_3DCONTROL_MOVE
#define DIAXIS_3DCONTROL_MOVE 0x24010202
#endif
#ifndef DIAXIS_3DCONTROL_ROTATEX
#define DIAXIS_3DCONTROL_ROTATEX 0x24034204
#endif
#ifndef DIAXIS_3DCONTROL_ROTATEY
#define DIAXIS_3DCONTROL_ROTATEY 0x2402C205
#endif
#ifndef DIAXIS_3DCONTROL_ROTATEZ
#define DIAXIS_3DCONTROL_ROTATEZ 0x24024206
#endif
#ifndef DIAXIS_ARCADEP_LATERAL
#define DIAXIS_ARCADEP_LATERAL 0x22008201
#endif
#ifndef DIAXIS_ARCADEP_MOVE
#define DIAXIS_ARCADEP_MOVE 0x22010202
#endif
#ifndef DIAXIS_ARCADES_LATERAL
#define DIAXIS_ARCADES_LATERAL 0x21008201
#endif
#ifndef DIAXIS_ARCADES_MOVE
#define DIAXIS_ARCADES_MOVE 0x21010202
#endif
#ifndef DIAXIS_BASEBALLB_LATERAL
#define DIAXIS_BASEBALLB_LATERAL 0x0F008201
#endif
#ifndef DIAXIS_BASEBALLB_MOVE
#define DIAXIS_BASEBALLB_MOVE 0x0F010202
#endif
#ifndef DIAXIS_BASEBALLF_LATERAL
#define DIAXIS_BASEBALLF_LATERAL 0x11008201
#endif
#ifndef DIAXIS_BASEBALLF_MOVE
#define DIAXIS_BASEBALLF_MOVE 0x11010202
#endif
#ifndef DIAXIS_BASEBALLP_LATERAL
#define DIAXIS_BASEBALLP_LATERAL 0x10008201
#endif
#ifndef DIAXIS_BASEBALLP_MOVE
#define DIAXIS_BASEBALLP_MOVE 0x10010202
#endif
#ifndef DIAXIS_BBALLD_LATERAL
#define DIAXIS_BBALLD_LATERAL 0x13008201
#endif
#ifndef DIAXIS_BBALLD_MOVE
#define DIAXIS_BBALLD_MOVE 0x13010202
#endif
#ifndef DIAXIS_BBALLO_LATERAL
#define DIAXIS_BBALLO_LATERAL 0x12008201
#endif
#ifndef DIAXIS_BBALLO_MOVE
#define DIAXIS_BBALLO_MOVE 0x12010202
#endif
#ifndef DIAXIS_BIKINGM_BRAKE
#define DIAXIS_BIKINGM_BRAKE 0x1C044203
#endif
#ifndef DIAXIS_BIKINGM_PEDAL
#define DIAXIS_BIKINGM_PEDAL 0x1C010202
#endif
#ifndef DIAXIS_BIKINGM_TURN
#define DIAXIS_BIKINGM_TURN 0x1C008201
#endif
#ifndef DIAXIS_BROWSER_LATERAL
#define DIAXIS_BROWSER_LATERAL 0x28008201
#endif
#ifndef DIAXIS_BROWSER_MOVE
#define DIAXIS_BROWSER_MOVE 0x28010202
#endif
#ifndef DIAXIS_BROWSER_VIEW
#define DIAXIS_BROWSER_VIEW 0x28018203
#endif
#ifndef DIAXIS_CADF_INOUT
#define DIAXIS_CADF_INOUT 0x25018203
#endif
#ifndef DIAXIS_CADF_LATERAL
#define DIAXIS_CADF_LATERAL 0x25008201
#endif
#ifndef DIAXIS_CADF_MOVE
#define DIAXIS_CADF_MOVE 0x25010202
#endif
#ifndef DIAXIS_CADF_ROTATEX
#define DIAXIS_CADF_ROTATEX 0x25034204
#endif
#ifndef DIAXIS_CADF_ROTATEY
#define DIAXIS_CADF_ROTATEY 0x2502C205
#endif
#ifndef DIAXIS_CADF_ROTATEZ
#define DIAXIS_CADF_ROTATEZ 0x25024206
#endif
#ifndef DIAXIS_CADM_INOUT
#define DIAXIS_CADM_INOUT 0x26018203
#endif
#ifndef DIAXIS_CADM_LATERAL
#define DIAXIS_CADM_LATERAL 0x26008201
#endif
#ifndef DIAXIS_CADM_MOVE
#define DIAXIS_CADM_MOVE 0x26010202
#endif
#ifndef DIAXIS_CADM_ROTATEX
#define DIAXIS_CADM_ROTATEX 0x26034204
#endif
#ifndef DIAXIS_CADM_ROTATEY
#define DIAXIS_CADM_ROTATEY 0x2602C205
#endif
#ifndef DIAXIS_CADM_ROTATEZ
#define DIAXIS_CADM_ROTATEZ 0x26024206
#endif
#ifndef DIAXIS_DRIVINGC_ACCELERATE
#define DIAXIS_DRIVINGC_ACCELERATE 0x02039202
#endif
#ifndef DIAXIS_DRIVINGC_ACCEL_AND_BRAKE
#define DIAXIS_DRIVINGC_ACCEL_AND_BRAKE 0x02014A04
#endif
#ifndef DIAXIS_DRIVINGC_BRAKE
#define DIAXIS_DRIVINGC_BRAKE 0x02041203
#endif
#ifndef DIAXIS_DRIVINGC_STEER
#define DIAXIS_DRIVINGC_STEER 0x02008A01
#endif
#ifndef DIAXIS_DRIVINGR_ACCEL_AND_BRAKE
#define DIAXIS_DRIVINGR_ACCEL_AND_BRAKE 0x01014A04
#endif
#ifndef DIAXIS_DRIVINGR_BRAKE
#define DIAXIS_DRIVINGR_BRAKE 0x01041203
#endif
#ifndef DIAXIS_DRIVINGT_ACCELERATE
#define DIAXIS_DRIVINGT_ACCELERATE 0x03039203
#endif
#ifndef DIAXIS_DRIVINGT_ACCEL_AND_BRAKE
#define DIAXIS_DRIVINGT_ACCEL_AND_BRAKE 0x03014A06
#endif
#ifndef DIAXIS_DRIVINGT_BARREL
#define DIAXIS_DRIVINGT_BARREL 0x03010202
#endif
#ifndef DIAXIS_DRIVINGT_BRAKE
#define DIAXIS_DRIVINGT_BRAKE 0x03045205
#endif
#ifndef DIAXIS_DRIVINGT_ROTATE
#define DIAXIS_DRIVINGT_ROTATE 0x03020204
#endif
#ifndef DIAXIS_DRIVINGT_STEER
#define DIAXIS_DRIVINGT_STEER 0x03008A01
#endif
#ifndef DIAXIS_FIGHTINGH_LATERAL
#define DIAXIS_FIGHTINGH_LATERAL 0x08008201
#endif
#ifndef DIAXIS_FIGHTINGH_MOVE
#define DIAXIS_FIGHTINGH_MOVE 0x08010202
#endif
#ifndef DIAXIS_FIGHTINGH_ROTATE
#define DIAXIS_FIGHTINGH_ROTATE 0x08024203
#endif
#ifndef DIAXIS_FISHING_LATERAL
#define DIAXIS_FISHING_LATERAL 0x0E008201
#endif
#ifndef DIAXIS_FISHING_MOVE
#define DIAXIS_FISHING_MOVE 0x0E010202
#endif
#ifndef DIAXIS_FISHING_ROTATE
#define DIAXIS_FISHING_ROTATE 0x0E024203
#endif
#ifndef DIAXIS_FLYINGC_BANK
#define DIAXIS_FLYINGC_BANK 0x04008A01
#endif
#ifndef DIAXIS_FLYINGC_BRAKE
#define DIAXIS_FLYINGC_BRAKE 0x04046A04
#endif
#ifndef DIAXIS_FLYINGC_FLAPS
#define DIAXIS_FLYINGC_FLAPS 0x04055A06
#endif
#ifndef DIAXIS_FLYINGC_PITCH
#define DIAXIS_FLYINGC_PITCH 0x04010A02
#endif
#ifndef DIAXIS_FLYINGC_RUDDER
#define DIAXIS_FLYINGC_RUDDER 0x04025205
#endif
#ifndef DIAXIS_FLYINGC_THROTTLE
#define DIAXIS_FLYINGC_THROTTLE 0x04039203
#endif
#ifndef DIAXIS_FLYINGM_BANK
#define DIAXIS_FLYINGM_BANK 0x05008A01
#endif
#ifndef DIAXIS_FLYINGM_BRAKE
#define DIAXIS_FLYINGM_BRAKE 0x05046205
#endif
#ifndef DIAXIS_FLYINGM_FLAPS
#define DIAXIS_FLYINGM_FLAPS 0x05055206
#endif
#ifndef DIAXIS_FLYINGM_PITCH
#define DIAXIS_FLYINGM_PITCH 0x05010A02
#endif
#ifndef DIAXIS_FLYINGM_RUDDER
#define DIAXIS_FLYINGM_RUDDER 0x05024A04
#endif
#ifndef DIAXIS_FLYINGM_THROTTLE
#define DIAXIS_FLYINGM_THROTTLE 0x05039203
#endif
#ifndef DIAXIS_FOOTBALLD_LATERAL
#define DIAXIS_FOOTBALLD_LATERAL 0x17008201
#endif
#ifndef DIAXIS_FOOTBALLD_MOVE
#define DIAXIS_FOOTBALLD_MOVE 0x17010202
#endif
#ifndef DIAXIS_FOOTBALLO_LATERAL
#define DIAXIS_FOOTBALLO_LATERAL 0x16008201
#endif
#ifndef DIAXIS_FOOTBALLO_MOVE
#define DIAXIS_FOOTBALLO_MOVE 0x16010202
#endif
#ifndef DIAXIS_FOOTBALLQ_LATERAL
#define DIAXIS_FOOTBALLQ_LATERAL 0x15008201
#endif
#ifndef DIAXIS_FOOTBALLQ_MOVE
#define DIAXIS_FOOTBALLQ_MOVE 0x15010202
#endif
#ifndef DIAXIS_FPS_LOOKUPDOWN
#define DIAXIS_FPS_LOOKUPDOWN 0x09018203
#endif
#ifndef DIAXIS_FPS_MOVE
#define DIAXIS_FPS_MOVE 0x09010202
#endif
#ifndef DIAXIS_FPS_ROTATE
#define DIAXIS_FPS_ROTATE 0x09008201
#endif
#ifndef DIAXIS_FPS_SIDESTEP
#define DIAXIS_FPS_SIDESTEP 0x09024204
#endif
#ifndef DIAXIS_GOLF_LATERAL
#define DIAXIS_GOLF_LATERAL 0x18008201
#endif
#ifndef DIAXIS_GOLF_MOVE
#define DIAXIS_GOLF_MOVE 0x18010202
#endif
#ifndef DIAXIS_HOCKEYD_LATERAL
#define DIAXIS_HOCKEYD_LATERAL 0x1A008201
#endif
#ifndef DIAXIS_HOCKEYD_MOVE
#define DIAXIS_HOCKEYD_MOVE 0x1A010202
#endif
#ifndef DIAXIS_HOCKEYG_LATERAL
#define DIAXIS_HOCKEYG_LATERAL 0x1B008201
#endif
#ifndef DIAXIS_HOCKEYG_MOVE
#define DIAXIS_HOCKEYG_MOVE 0x1B010202
#endif
#ifndef DIAXIS_HOCKEYO_LATERAL
#define DIAXIS_HOCKEYO_LATERAL 0x19008201
#endif
#ifndef DIAXIS_HOCKEYO_MOVE
#define DIAXIS_HOCKEYO_MOVE 0x19010202
#endif
#ifndef DIAXIS_HUNTING_LATERAL
#define DIAXIS_HUNTING_LATERAL 0x0D008201
#endif
#ifndef DIAXIS_HUNTING_MOVE
#define DIAXIS_HUNTING_MOVE 0x0D010202
#endif
#ifndef DIAXIS_HUNTING_ROTATE
#define DIAXIS_HUNTING_ROTATE 0x0D024203
#endif
#ifndef DIAXIS_MECHA_ROTATE
#define DIAXIS_MECHA_ROTATE 0x29020203
#endif
#ifndef DIAXIS_MECHA_STEER
#define DIAXIS_MECHA_STEER 0x29008201
#endif
#ifndef DIAXIS_MECHA_THROTTLE
#define DIAXIS_MECHA_THROTTLE 0x29038204
#endif
#ifndef DIAXIS_MECHA_TORSO
#define DIAXIS_MECHA_TORSO 0x29010202
#endif
#ifndef DIAXIS_RACQUET_LATERAL
#define DIAXIS_RACQUET_LATERAL 0x20008201
#endif
#ifndef DIAXIS_RACQUET_MOVE
#define DIAXIS_RACQUET_MOVE 0x20010202
#endif
#ifndef DIAXIS_REMOTE_SLIDER
#define DIAXIS_REMOTE_SLIDER 0x27050201
#endif
#ifndef DIAXIS_REMOTE_SLIDER2
#define DIAXIS_REMOTE_SLIDER2 0x27054202
#endif
#ifndef DIAXIS_SKIING_SPEED
#define DIAXIS_SKIING_SPEED 0x1D010202
#endif
#ifndef DIAXIS_SKIING_TURN
#define DIAXIS_SKIING_TURN 0x1D008201
#endif
#ifndef DIAXIS_SOCCERD_LATERAL
#define DIAXIS_SOCCERD_LATERAL 0x1F008201
#endif
#ifndef DIAXIS_SOCCERD_MOVE
#define DIAXIS_SOCCERD_MOVE 0x1F010202
#endif
#ifndef DIAXIS_SOCCERO_BEND
#define DIAXIS_SOCCERO_BEND 0x1E018203
#endif
#ifndef DIAXIS_SOCCERO_LATERAL
#define DIAXIS_SOCCERO_LATERAL 0x1E008201
#endif
#ifndef DIAXIS_SOCCERO_MOVE
#define DIAXIS_SOCCERO_MOVE 0x1E010202
#endif
#ifndef DIAXIS_STRATEGYR_LATERAL
#define DIAXIS_STRATEGYR_LATERAL 0x0B008201
#endif
#ifndef DIAXIS_STRATEGYR_MOVE
#define DIAXIS_STRATEGYR_MOVE 0x0B010202
#endif
#ifndef DIAXIS_STRATEGYR_ROTATE
#define DIAXIS_STRATEGYR_ROTATE 0x0B024203
#endif
#ifndef DIAXIS_STRATEGYT_LATERAL
#define DIAXIS_STRATEGYT_LATERAL 0x0C008201
#endif
#ifndef DIAXIS_STRATEGYT_MOVE
#define DIAXIS_STRATEGYT_MOVE 0x0C010202
#endif
#ifndef DIAXIS_TPS_MOVE
#define DIAXIS_TPS_MOVE 0x0A010202
#endif
#ifndef DIAXIS_TPS_STEP
#define DIAXIS_TPS_STEP 0x0A00C203
#endif
#ifndef DIAXIS_TPS_TURN
#define DIAXIS_TPS_TURN 0x0A020201
#endif
#ifndef DIBUTTON_2DCONTROL_DEVICE
#define DIBUTTON_2DCONTROL_DEVICE 0x230044FE
#endif
#ifndef DIBUTTON_2DCONTROL_DISPLAY
#define DIBUTTON_2DCONTROL_DISPLAY 0x23004405
#endif
#ifndef DIBUTTON_2DCONTROL_MENU
#define DIBUTTON_2DCONTROL_MENU 0x230004FD
#endif
#ifndef DIBUTTON_2DCONTROL_PAUSE
#define DIBUTTON_2DCONTROL_PAUSE 0x230044FC
#endif
#ifndef DIBUTTON_2DCONTROL_SELECT
#define DIBUTTON_2DCONTROL_SELECT 0x23000401
#endif
#ifndef DIBUTTON_2DCONTROL_SPECIAL
#define DIBUTTON_2DCONTROL_SPECIAL 0x23000403
#endif
#ifndef DIBUTTON_2DCONTROL_SPECIAL1
#define DIBUTTON_2DCONTROL_SPECIAL1 0x23000402
#endif
#ifndef DIBUTTON_2DCONTROL_SPECIAL2
#define DIBUTTON_2DCONTROL_SPECIAL2 0x23000404
#endif
#ifndef DIBUTTON_3DCONTROL_DEVICE
#define DIBUTTON_3DCONTROL_DEVICE 0x240044FE
#endif
#ifndef DIBUTTON_3DCONTROL_DISPLAY
#define DIBUTTON_3DCONTROL_DISPLAY 0x24004405
#endif
#ifndef DIBUTTON_3DCONTROL_MENU
#define DIBUTTON_3DCONTROL_MENU 0x240004FD
#endif
#ifndef DIBUTTON_3DCONTROL_PAUSE
#define DIBUTTON_3DCONTROL_PAUSE 0x240044FC
#endif
#ifndef DIBUTTON_3DCONTROL_SELECT
#define DIBUTTON_3DCONTROL_SELECT 0x24000401
#endif
#ifndef DIBUTTON_3DCONTROL_SPECIAL
#define DIBUTTON_3DCONTROL_SPECIAL 0x24000403
#endif
#ifndef DIBUTTON_3DCONTROL_SPECIAL1
#define DIBUTTON_3DCONTROL_SPECIAL1 0x24000402
#endif
#ifndef DIBUTTON_3DCONTROL_SPECIAL2
#define DIBUTTON_3DCONTROL_SPECIAL2 0x24000404
#endif
#ifndef DIBUTTON_ARCADEP_BACK_LINK
#define DIBUTTON_ARCADEP_BACK_LINK 0x220144E8
#endif
#ifndef DIBUTTON_ARCADEP_CROUCH
#define DIBUTTON_ARCADEP_CROUCH 0x22000403
#endif
#ifndef DIBUTTON_ARCADEP_DEVICE
#define DIBUTTON_ARCADEP_DEVICE 0x220044FE
#endif
#ifndef DIBUTTON_ARCADEP_FIRE
#define DIBUTTON_ARCADEP_FIRE 0x22000402
#endif
#ifndef DIBUTTON_ARCADEP_FIRESECONDARY
#define DIBUTTON_ARCADEP_FIRESECONDARY 0x22004406
#endif
#ifndef DIBUTTON_ARCADEP_FORWARD_LINK
#define DIBUTTON_ARCADEP_FORWARD_LINK 0x220144E0
#endif
#ifndef DIBUTTON_ARCADEP_JUMP
#define DIBUTTON_ARCADEP_JUMP 0x22000401
#endif
#ifndef DIBUTTON_ARCADEP_LEFT_LINK
#define DIBUTTON_ARCADEP_LEFT_LINK 0x2200C4E4
#endif
#ifndef DIBUTTON_ARCADEP_MENU
#define DIBUTTON_ARCADEP_MENU 0x220004FD
#endif
#ifndef DIBUTTON_ARCADEP_PAUSE
#define DIBUTTON_ARCADEP_PAUSE 0x220044FC
#endif
#ifndef DIBUTTON_ARCADEP_RIGHT_LINK
#define DIBUTTON_ARCADEP_RIGHT_LINK 0x2200C4EC
#endif
#ifndef DIBUTTON_ARCADEP_SELECT
#define DIBUTTON_ARCADEP_SELECT 0x22000405
#endif
#ifndef DIBUTTON_ARCADEP_SPECIAL
#define DIBUTTON_ARCADEP_SPECIAL 0x22000404
#endif
#ifndef DIBUTTON_ARCADEP_VIEW_DOWN_LINK
#define DIBUTTON_ARCADEP_VIEW_DOWN_LINK 0x2207C4E8
#endif
#ifndef DIBUTTON_ARCADEP_VIEW_LEFT_LINK
#define DIBUTTON_ARCADEP_VIEW_LEFT_LINK 0x2207C4E4
#endif
#ifndef DIBUTTON_ARCADEP_VIEW_RIGHT_LINK
#define DIBUTTON_ARCADEP_VIEW_RIGHT_LINK 0x2207C4EC
#endif
#ifndef DIBUTTON_ARCADEP_VIEW_UP_LINK
#define DIBUTTON_ARCADEP_VIEW_UP_LINK 0x2207C4E0
#endif
#ifndef DIBUTTON_ARCADES_ATTACK
#define DIBUTTON_ARCADES_ATTACK 0x21000403
#endif
#ifndef DIBUTTON_ARCADES_BACK_LINK
#define DIBUTTON_ARCADES_BACK_LINK 0x210144E8
#endif
#ifndef DIBUTTON_ARCADES_CARRY
#define DIBUTTON_ARCADES_CARRY 0x21000402
#endif
#ifndef DIBUTTON_ARCADES_DEVICE
#define DIBUTTON_ARCADES_DEVICE 0x210044FE
#endif
#ifndef DIBUTTON_ARCADES_FORWARD_LINK
#define DIBUTTON_ARCADES_FORWARD_LINK 0x210144E0
#endif
#ifndef DIBUTTON_ARCADES_LEFT_LINK
#define DIBUTTON_ARCADES_LEFT_LINK 0x2100C4E4
#endif
#ifndef DIBUTTON_ARCADES_MENU
#define DIBUTTON_ARCADES_MENU 0x210004FD
#endif
#ifndef DIBUTTON_ARCADES_PAUSE
#define DIBUTTON_ARCADES_PAUSE 0x210044FC
#endif
#ifndef DIBUTTON_ARCADES_RIGHT_LINK
#define DIBUTTON_ARCADES_RIGHT_LINK 0x2100C4EC
#endif
#ifndef DIBUTTON_ARCADES_SELECT
#define DIBUTTON_ARCADES_SELECT 0x21000405
#endif
#ifndef DIBUTTON_ARCADES_SPECIAL
#define DIBUTTON_ARCADES_SPECIAL 0x21000404
#endif
#ifndef DIBUTTON_ARCADES_THROW
#define DIBUTTON_ARCADES_THROW 0x21000401
#endif
#ifndef DIBUTTON_ARCADES_VIEW_DOWN_LINK
#define DIBUTTON_ARCADES_VIEW_DOWN_LINK 0x2107C4E8
#endif
#ifndef DIBUTTON_ARCADES_VIEW_LEFT_LINK
#define DIBUTTON_ARCADES_VIEW_LEFT_LINK 0x2107C4E4
#endif
#ifndef DIBUTTON_ARCADES_VIEW_RIGHT_LINK
#define DIBUTTON_ARCADES_VIEW_RIGHT_LINK 0x2107C4EC
#endif
#ifndef DIBUTTON_ARCADES_VIEW_UP_LINK
#define DIBUTTON_ARCADES_VIEW_UP_LINK 0x2107C4E0
#endif
#ifndef DIBUTTON_BASEBALLB_BACK_LINK
#define DIBUTTON_BASEBALLB_BACK_LINK 0x0F0144E8
#endif
#ifndef DIBUTTON_BASEBALLB_BOX
#define DIBUTTON_BASEBALLB_BOX 0x0F00440A
#endif
#ifndef DIBUTTON_BASEBALLB_BUNT
#define DIBUTTON_BASEBALLB_BUNT 0x0F000404
#endif
#ifndef DIBUTTON_BASEBALLB_BURST
#define DIBUTTON_BASEBALLB_BURST 0x0F000406
#endif
#ifndef DIBUTTON_BASEBALLB_CONTACT
#define DIBUTTON_BASEBALLB_CONTACT 0x0F000408
#endif
#ifndef DIBUTTON_BASEBALLB_DEVICE
#define DIBUTTON_BASEBALLB_DEVICE 0x0F0044FE
#endif
#ifndef DIBUTTON_BASEBALLB_FORWARD_LINK
#define DIBUTTON_BASEBALLB_FORWARD_LINK 0x0F0144E0
#endif
#ifndef DIBUTTON_BASEBALLB_LEFT_LINK
#define DIBUTTON_BASEBALLB_LEFT_LINK 0x0F00C4E4
#endif
#ifndef DIBUTTON_BASEBALLB_MENU
#define DIBUTTON_BASEBALLB_MENU 0x0F0004FD
#endif
#ifndef DIBUTTON_BASEBALLB_NORMAL
#define DIBUTTON_BASEBALLB_NORMAL 0x0F000402
#endif
#ifndef DIBUTTON_BASEBALLB_NOSTEAL
#define DIBUTTON_BASEBALLB_NOSTEAL 0x0F004409
#endif
#ifndef DIBUTTON_BASEBALLB_PAUSE
#define DIBUTTON_BASEBALLB_PAUSE 0x0F0044FC
#endif
#ifndef DIBUTTON_BASEBALLB_POWER
#define DIBUTTON_BASEBALLB_POWER 0x0F000403
#endif
#ifndef DIBUTTON_BASEBALLB_RIGHT_LINK
#define DIBUTTON_BASEBALLB_RIGHT_LINK 0x0F00C4EC
#endif
#ifndef DIBUTTON_BASEBALLB_SELECT
#define DIBUTTON_BASEBALLB_SELECT 0x0F000401
#endif
#ifndef DIBUTTON_BASEBALLB_SLIDE
#define DIBUTTON_BASEBALLB_SLIDE 0x0F000407
#endif
#ifndef DIBUTTON_BASEBALLB_STEAL
#define DIBUTTON_BASEBALLB_STEAL 0x0F000405
#endif
#ifndef DIBUTTON_BASEBALLF_AIM_LEFT_LINK
#define DIBUTTON_BASEBALLF_AIM_LEFT_LINK 0x1100C4E4
#endif
#ifndef DIBUTTON_BASEBALLF_AIM_RIGHT_LINK
#define DIBUTTON_BASEBALLF_AIM_RIGHT_LINK 0x1100C4EC
#endif
#ifndef DIBUTTON_BASEBALLF_BACK_LINK
#define DIBUTTON_BASEBALLF_BACK_LINK 0x110144E8
#endif
#ifndef DIBUTTON_BASEBALLF_BURST
#define DIBUTTON_BASEBALLF_BURST 0x11000404
#endif
#ifndef DIBUTTON_BASEBALLF_DEVICE
#define DIBUTTON_BASEBALLF_DEVICE 0x110044FE
#endif
#ifndef DIBUTTON_BASEBALLF_DIVE
#define DIBUTTON_BASEBALLF_DIVE 0x11000406
#endif
#ifndef DIBUTTON_BASEBALLF_FORWARD_LINK
#define DIBUTTON_BASEBALLF_FORWARD_LINK 0x110144E0
#endif
#ifndef DIBUTTON_BASEBALLF_JUMP
#define DIBUTTON_BASEBALLF_JUMP 0x11000405
#endif
#ifndef DIBUTTON_BASEBALLF_MENU
#define DIBUTTON_BASEBALLF_MENU 0x110004FD
#endif
#ifndef DIBUTTON_BASEBALLF_NEAREST
#define DIBUTTON_BASEBALLF_NEAREST 0x11000401
#endif
#ifndef DIBUTTON_BASEBALLF_PAUSE
#define DIBUTTON_BASEBALLF_PAUSE 0x110044FC
#endif
#ifndef DIBUTTON_BASEBALLF_SHIFTIN
#define DIBUTTON_BASEBALLF_SHIFTIN 0x11004407
#endif
#ifndef DIBUTTON_BASEBALLF_SHIFTOUT
#define DIBUTTON_BASEBALLF_SHIFTOUT 0x11004408
#endif
#ifndef DIBUTTON_BASEBALLF_THROW1
#define DIBUTTON_BASEBALLF_THROW1 0x11000402
#endif
#ifndef DIBUTTON_BASEBALLF_THROW2
#define DIBUTTON_BASEBALLF_THROW2 0x11000403
#endif
#ifndef DIBUTTON_BASEBALLP_BACK_LINK
#define DIBUTTON_BASEBALLP_BACK_LINK 0x100144E8
#endif
#ifndef DIBUTTON_BASEBALLP_BASE
#define DIBUTTON_BASEBALLP_BASE 0x10000403
#endif
#ifndef DIBUTTON_BASEBALLP_DEVICE
#define DIBUTTON_BASEBALLP_DEVICE 0x100044FE
#endif
#ifndef DIBUTTON_BASEBALLP_FAKE
#define DIBUTTON_BASEBALLP_FAKE 0x10000405
#endif
#ifndef DIBUTTON_BASEBALLP_FORWARD_LINK
#define DIBUTTON_BASEBALLP_FORWARD_LINK 0x100144E0
#endif
#ifndef DIBUTTON_BASEBALLP_LEFT_LINK
#define DIBUTTON_BASEBALLP_LEFT_LINK 0x1000C4E4
#endif
#ifndef DIBUTTON_BASEBALLP_LOOK
#define DIBUTTON_BASEBALLP_LOOK 0x10004407
#endif
#ifndef DIBUTTON_BASEBALLP_MENU
#define DIBUTTON_BASEBALLP_MENU 0x100004FD
#endif
#ifndef DIBUTTON_BASEBALLP_PAUSE
#define DIBUTTON_BASEBALLP_PAUSE 0x100044FC
#endif
#ifndef DIBUTTON_BASEBALLP_PITCH
#define DIBUTTON_BASEBALLP_PITCH 0x10000402
#endif
#ifndef DIBUTTON_BASEBALLP_RIGHT_LINK
#define DIBUTTON_BASEBALLP_RIGHT_LINK 0x1000C4EC
#endif
#ifndef DIBUTTON_BASEBALLP_SELECT
#define DIBUTTON_BASEBALLP_SELECT 0x10000401
#endif
#ifndef DIBUTTON_BASEBALLP_THROW
#define DIBUTTON_BASEBALLP_THROW 0x10000404
#endif
#ifndef DIBUTTON_BASEBALLP_WALK
#define DIBUTTON_BASEBALLP_WALK 0x10004406
#endif
#ifndef DIBUTTON_BBALLD_BACK_LINK
#define DIBUTTON_BBALLD_BACK_LINK 0x130144E8
#endif
#ifndef DIBUTTON_BBALLD_BURST
#define DIBUTTON_BBALLD_BURST 0x13000406
#endif
#ifndef DIBUTTON_BBALLD_DEVICE
#define DIBUTTON_BBALLD_DEVICE 0x130044FE
#endif
#ifndef DIBUTTON_BBALLD_FAKE
#define DIBUTTON_BBALLD_FAKE 0x13000403
#endif
#ifndef DIBUTTON_BBALLD_FORWARD_LINK
#define DIBUTTON_BBALLD_FORWARD_LINK 0x130144E0
#endif
#ifndef DIBUTTON_BBALLD_JUMP
#define DIBUTTON_BBALLD_JUMP 0x13000401
#endif
#ifndef DIBUTTON_BBALLD_LEFT_LINK
#define DIBUTTON_BBALLD_LEFT_LINK 0x1300C4E4
#endif
#ifndef DIBUTTON_BBALLD_MENU
#define DIBUTTON_BBALLD_MENU 0x130004FD
#endif
#ifndef DIBUTTON_BBALLD_PAUSE
#define DIBUTTON_BBALLD_PAUSE 0x130044FC
#endif
#ifndef DIBUTTON_BBALLD_PLAY
#define DIBUTTON_BBALLD_PLAY 0x13000407
#endif
#ifndef DIBUTTON_BBALLD_PLAYER
#define DIBUTTON_BBALLD_PLAYER 0x13000405
#endif
#ifndef DIBUTTON_BBALLD_RIGHT_LINK
#define DIBUTTON_BBALLD_RIGHT_LINK 0x1300C4EC
#endif
#ifndef DIBUTTON_BBALLD_SPECIAL
#define DIBUTTON_BBALLD_SPECIAL 0x13000404
#endif
#ifndef DIBUTTON_BBALLD_STEAL
#define DIBUTTON_BBALLD_STEAL 0x13000402
#endif
#ifndef DIBUTTON_BBALLD_SUBSTITUTE
#define DIBUTTON_BBALLD_SUBSTITUTE 0x13004409
#endif
#ifndef DIBUTTON_BBALLD_TIMEOUT
#define DIBUTTON_BBALLD_TIMEOUT 0x13004408
#endif
#ifndef DIBUTTON_BBALLO_BACK_LINK
#define DIBUTTON_BBALLO_BACK_LINK 0x120144E8
#endif
#ifndef DIBUTTON_BBALLO_BURST
#define DIBUTTON_BBALLO_BURST 0x12000407
#endif
#ifndef DIBUTTON_BBALLO_CALL
#define DIBUTTON_BBALLO_CALL 0x12000408
#endif
#ifndef DIBUTTON_BBALLO_DEVICE
#define DIBUTTON_BBALLO_DEVICE 0x120044FE
#endif
#ifndef DIBUTTON_BBALLO_DUNK
#define DIBUTTON_BBALLO_DUNK 0x12000402
#endif
#ifndef DIBUTTON_BBALLO_FAKE
#define DIBUTTON_BBALLO_FAKE 0x12000404
#endif
#ifndef DIBUTTON_BBALLO_FORWARD_LINK
#define DIBUTTON_BBALLO_FORWARD_LINK 0x120144E0
#endif
#ifndef DIBUTTON_BBALLO_JAB
#define DIBUTTON_BBALLO_JAB 0x1200440B
#endif
#ifndef DIBUTTON_BBALLO_LEFT_LINK
#define DIBUTTON_BBALLO_LEFT_LINK 0x1200C4E4
#endif
#ifndef DIBUTTON_BBALLO_MENU
#define DIBUTTON_BBALLO_MENU 0x120004FD
#endif
#ifndef DIBUTTON_BBALLO_PASS
#define DIBUTTON_BBALLO_PASS 0x12000403
#endif
#ifndef DIBUTTON_BBALLO_PAUSE
#define DIBUTTON_BBALLO_PAUSE 0x120044FC
#endif
#ifndef DIBUTTON_BBALLO_PLAY
#define DIBUTTON_BBALLO_PLAY 0x1200440A
#endif
#ifndef DIBUTTON_BBALLO_PLAYER
#define DIBUTTON_BBALLO_PLAYER 0x12000406
#endif
#ifndef DIBUTTON_BBALLO_POST
#define DIBUTTON_BBALLO_POST 0x1200440C
#endif
#ifndef DIBUTTON_BBALLO_RIGHT_LINK
#define DIBUTTON_BBALLO_RIGHT_LINK 0x1200C4EC
#endif
#ifndef DIBUTTON_BBALLO_SCREEN
#define DIBUTTON_BBALLO_SCREEN 0x12004409
#endif
#ifndef DIBUTTON_BBALLO_SHOOT
#define DIBUTTON_BBALLO_SHOOT 0x12000401
#endif
#ifndef DIBUTTON_BBALLO_SPECIAL
#define DIBUTTON_BBALLO_SPECIAL 0x12000405
#endif
#ifndef DIBUTTON_BBALLO_SUBSTITUTE
#define DIBUTTON_BBALLO_SUBSTITUTE 0x1200440E
#endif
#ifndef DIBUTTON_BBALLO_TIMEOUT
#define DIBUTTON_BBALLO_TIMEOUT 0x1200440D
#endif
#ifndef DIBUTTON_BIKINGM_BRAKE_BUTTON_LINK
#define DIBUTTON_BIKINGM_BRAKE_BUTTON_LINK 0x1C0444E8
#endif
#ifndef DIBUTTON_BIKINGM_CAMERA
#define DIBUTTON_BIKINGM_CAMERA 0x1C000402
#endif
#ifndef DIBUTTON_BIKINGM_DEVICE
#define DIBUTTON_BIKINGM_DEVICE 0x1C0044FE
#endif
#ifndef DIBUTTON_BIKINGM_FASTER_LINK
#define DIBUTTON_BIKINGM_FASTER_LINK 0x1C0144E0
#endif
#ifndef DIBUTTON_BIKINGM_JUMP
#define DIBUTTON_BIKINGM_JUMP 0x1C000401
#endif
#ifndef DIBUTTON_BIKINGM_LEFT_LINK
#define DIBUTTON_BIKINGM_LEFT_LINK 0x1C00C4E4
#endif
#ifndef DIBUTTON_BIKINGM_MENU
#define DIBUTTON_BIKINGM_MENU 0x1C0004FD
#endif
#ifndef DIBUTTON_BIKINGM_PAUSE
#define DIBUTTON_BIKINGM_PAUSE 0x1C0044FC
#endif
#ifndef DIBUTTON_BIKINGM_RIGHT_LINK
#define DIBUTTON_BIKINGM_RIGHT_LINK 0x1C00C4EC
#endif
#ifndef DIBUTTON_BIKINGM_SELECT
#define DIBUTTON_BIKINGM_SELECT 0x1C000404
#endif
#ifndef DIBUTTON_BIKINGM_SLOWER_LINK
#define DIBUTTON_BIKINGM_SLOWER_LINK 0x1C0144E8
#endif
#ifndef DIBUTTON_BIKINGM_SPECIAL1
#define DIBUTTON_BIKINGM_SPECIAL1 0x1C000403
#endif
#ifndef DIBUTTON_BIKINGM_SPECIAL2
#define DIBUTTON_BIKINGM_SPECIAL2 0x1C000405
#endif
#ifndef DIBUTTON_BIKINGM_ZOOM
#define DIBUTTON_BIKINGM_ZOOM 0x1C004406
#endif
#ifndef DIBUTTON_BROWSER_DEVICE
#define DIBUTTON_BROWSER_DEVICE 0x280044FE
#endif
#ifndef DIBUTTON_BROWSER_FAVORITES
#define DIBUTTON_BROWSER_FAVORITES 0x28004406
#endif
#ifndef DIBUTTON_BROWSER_HISTORY
#define DIBUTTON_BROWSER_HISTORY 0x28004409
#endif
#ifndef DIBUTTON_BROWSER_HOME
#define DIBUTTON_BROWSER_HOME 0x28004405
#endif
#ifndef DIBUTTON_BROWSER_MENU
#define DIBUTTON_BROWSER_MENU 0x280004FD
#endif
#ifndef DIBUTTON_BROWSER_NEXT
#define DIBUTTON_BROWSER_NEXT 0x28004407
#endif
#ifndef DIBUTTON_BROWSER_PAUSE
#define DIBUTTON_BROWSER_PAUSE 0x280044FC
#endif
#ifndef DIBUTTON_BROWSER_PREVIOUS
#define DIBUTTON_BROWSER_PREVIOUS 0x28004408
#endif
#ifndef DIBUTTON_BROWSER_PRINT
#define DIBUTTON_BROWSER_PRINT 0x2800440A
#endif
#ifndef DIBUTTON_BROWSER_REFRESH
#define DIBUTTON_BROWSER_REFRESH 0x28000402
#endif
#ifndef DIBUTTON_BROWSER_SEARCH
#define DIBUTTON_BROWSER_SEARCH 0x28004403
#endif
#ifndef DIBUTTON_BROWSER_SELECT
#define DIBUTTON_BROWSER_SELECT 0x28000401
#endif
#ifndef DIBUTTON_BROWSER_STOP
#define DIBUTTON_BROWSER_STOP 0x28004404
#endif
#ifndef DIBUTTON_CADF_DEVICE
#define DIBUTTON_CADF_DEVICE 0x250044FE
#endif
#ifndef DIBUTTON_CADF_DISPLAY
#define DIBUTTON_CADF_DISPLAY 0x25004405
#endif
#ifndef DIBUTTON_CADF_MENU
#define DIBUTTON_CADF_MENU 0x250004FD
#endif
#ifndef DIBUTTON_CADF_PAUSE
#define DIBUTTON_CADF_PAUSE 0x250044FC
#endif
#ifndef DIBUTTON_CADF_SELECT
#define DIBUTTON_CADF_SELECT 0x25000401
#endif
#ifndef DIBUTTON_CADF_SPECIAL
#define DIBUTTON_CADF_SPECIAL 0x25000403
#endif
#ifndef DIBUTTON_CADF_SPECIAL1
#define DIBUTTON_CADF_SPECIAL1 0x25000402
#endif
#ifndef DIBUTTON_CADF_SPECIAL2
#define DIBUTTON_CADF_SPECIAL2 0x25000404
#endif
#ifndef DIBUTTON_CADM_DEVICE
#define DIBUTTON_CADM_DEVICE 0x260044FE
#endif
#ifndef DIBUTTON_CADM_DISPLAY
#define DIBUTTON_CADM_DISPLAY 0x26004405
#endif
#ifndef DIBUTTON_CADM_MENU
#define DIBUTTON_CADM_MENU 0x260004FD
#endif
#ifndef DIBUTTON_CADM_PAUSE
#define DIBUTTON_CADM_PAUSE 0x260044FC
#endif
#ifndef DIBUTTON_CADM_SELECT
#define DIBUTTON_CADM_SELECT 0x26000401
#endif
#ifndef DIBUTTON_CADM_SPECIAL
#define DIBUTTON_CADM_SPECIAL 0x26000403
#endif
#ifndef DIBUTTON_CADM_SPECIAL1
#define DIBUTTON_CADM_SPECIAL1 0x26000402
#endif
#ifndef DIBUTTON_CADM_SPECIAL2
#define DIBUTTON_CADM_SPECIAL2 0x26000404
#endif
#ifndef DIBUTTON_DRIVINGC_ACCELERATE_LINK
#define DIBUTTON_DRIVINGC_ACCELERATE_LINK 0x0203D4E0
#endif
#ifndef DIBUTTON_DRIVINGC_AIDS
#define DIBUTTON_DRIVINGC_AIDS 0x02004407
#endif
#ifndef DIBUTTON_DRIVINGC_BRAKE
#define DIBUTTON_DRIVINGC_BRAKE 0x02004C08
#endif
#ifndef DIBUTTON_DRIVINGC_DASHBOARD
#define DIBUTTON_DRIVINGC_DASHBOARD 0x02004406
#endif
#ifndef DIBUTTON_DRIVINGC_DEVICE
#define DIBUTTON_DRIVINGC_DEVICE 0x020044FE
#endif
#ifndef DIBUTTON_DRIVINGC_FIRE
#define DIBUTTON_DRIVINGC_FIRE 0x02000C01
#endif
#ifndef DIBUTTON_DRIVINGC_FIRESECONDARY
#define DIBUTTON_DRIVINGC_FIRESECONDARY 0x02004C09
#endif
#ifndef DIBUTTON_DRIVINGC_GLANCE_LEFT_LINK
#define DIBUTTON_DRIVINGC_GLANCE_LEFT_LINK 0x0207C4E4
#endif
#ifndef DIBUTTON_DRIVINGC_GLANCE_RIGHT_LINK
#define DIBUTTON_DRIVINGC_GLANCE_RIGHT_LINK 0x0207C4EC
#endif
#ifndef DIBUTTON_DRIVINGC_MENU
#define DIBUTTON_DRIVINGC_MENU 0x020004FD
#endif
#ifndef DIBUTTON_DRIVINGC_PAUSE
#define DIBUTTON_DRIVINGC_PAUSE 0x020044FC
#endif
#ifndef DIBUTTON_DRIVINGC_SHIFTDOWN
#define DIBUTTON_DRIVINGC_SHIFTDOWN 0x02004C05
#endif
#ifndef DIBUTTON_DRIVINGC_SHIFTUP
#define DIBUTTON_DRIVINGC_SHIFTUP 0x02004C04
#endif
#ifndef DIBUTTON_DRIVINGC_STEER_LEFT_LINK
#define DIBUTTON_DRIVINGC_STEER_LEFT_LINK 0x0200CCE4
#endif
#ifndef DIBUTTON_DRIVINGC_STEER_RIGHT_LINK
#define DIBUTTON_DRIVINGC_STEER_RIGHT_LINK 0x0200CCEC
#endif
#ifndef DIBUTTON_DRIVINGC_TARGET
#define DIBUTTON_DRIVINGC_TARGET 0x02000C03
#endif
#ifndef DIBUTTON_DRIVINGC_WEAPONS
#define DIBUTTON_DRIVINGC_WEAPONS 0x02000C02
#endif
#ifndef DIBUTTON_DRIVINGR_ACCELERATE_LINK
#define DIBUTTON_DRIVINGR_ACCELERATE_LINK 0x0103D4E0
#endif
#ifndef DIBUTTON_DRIVINGR_AIDS
#define DIBUTTON_DRIVINGR_AIDS 0x01004406
#endif
#ifndef DIBUTTON_DRIVINGR_BOOST
#define DIBUTTON_DRIVINGR_BOOST 0x01004408
#endif
#ifndef DIBUTTON_DRIVINGR_BRAKE
#define DIBUTTON_DRIVINGR_BRAKE 0x01004C04
#endif
#ifndef DIBUTTON_DRIVINGR_DASHBOARD
#define DIBUTTON_DRIVINGR_DASHBOARD 0x01004405
#endif
#ifndef DIBUTTON_DRIVINGR_DEVICE
#define DIBUTTON_DRIVINGR_DEVICE 0x010044FE
#endif
#ifndef DIBUTTON_DRIVINGR_GLANCE_LEFT_LINK
#define DIBUTTON_DRIVINGR_GLANCE_LEFT_LINK 0x0107C4E4
#endif
#ifndef DIBUTTON_DRIVINGR_GLANCE_RIGHT_LINK
#define DIBUTTON_DRIVINGR_GLANCE_RIGHT_LINK 0x0107C4EC
#endif
#ifndef DIBUTTON_DRIVINGR_MAP
#define DIBUTTON_DRIVINGR_MAP 0x01004407
#endif
#ifndef DIBUTTON_DRIVINGR_MENU
#define DIBUTTON_DRIVINGR_MENU 0x010004FD
#endif
#ifndef DIBUTTON_DRIVINGR_PAUSE
#define DIBUTTON_DRIVINGR_PAUSE 0x010044FC
#endif
#ifndef DIBUTTON_DRIVINGR_PIT
#define DIBUTTON_DRIVINGR_PIT 0x01004409
#endif
#ifndef DIBUTTON_DRIVINGR_SHIFTDOWN
#define DIBUTTON_DRIVINGR_SHIFTDOWN 0x01000C02
#endif
#ifndef DIBUTTON_DRIVINGR_SHIFTUP
#define DIBUTTON_DRIVINGR_SHIFTUP 0x01000C01
#endif
#ifndef DIBUTTON_DRIVINGR_STEER_LEFT_LINK
#define DIBUTTON_DRIVINGR_STEER_LEFT_LINK 0x0100CCE4
#endif
#ifndef DIBUTTON_DRIVINGR_STEER_RIGHT_LINK
#define DIBUTTON_DRIVINGR_STEER_RIGHT_LINK 0x0100CCEC
#endif
#ifndef DIBUTTON_DRIVINGR_VIEW
#define DIBUTTON_DRIVINGR_VIEW 0x01001C03
#endif
#ifndef DIBUTTON_DRIVINGT_ACCELERATE_LINK
#define DIBUTTON_DRIVINGT_ACCELERATE_LINK 0x0303D4E0
#endif
#ifndef DIBUTTON_DRIVINGT_BARREL_DOWN_LINK
#define DIBUTTON_DRIVINGT_BARREL_DOWN_LINK 0x030144E8
#endif
#ifndef DIBUTTON_DRIVINGT_BARREL_UP_LINK
#define DIBUTTON_DRIVINGT_BARREL_UP_LINK 0x030144E0
#endif
#ifndef DIBUTTON_DRIVINGT_BRAKE
#define DIBUTTON_DRIVINGT_BRAKE 0x03004C06
#endif
#ifndef DIBUTTON_DRIVINGT_DASHBOARD
#define DIBUTTON_DRIVINGT_DASHBOARD 0x03005C05
#endif
#ifndef DIBUTTON_DRIVINGT_DEVICE
#define DIBUTTON_DRIVINGT_DEVICE 0x030044FE
#endif
#ifndef DIBUTTON_DRIVINGT_FIRE
#define DIBUTTON_DRIVINGT_FIRE 0x03000C01
#endif
#ifndef DIBUTTON_DRIVINGT_FIRESECONDARY
#define DIBUTTON_DRIVINGT_FIRESECONDARY 0x03004C07
#endif
#ifndef DIBUTTON_DRIVINGT_GLANCE_LEFT_LINK
#define DIBUTTON_DRIVINGT_GLANCE_LEFT_LINK 0x0307C4E4
#endif
#ifndef DIBUTTON_DRIVINGT_GLANCE_RIGHT_LINK
#define DIBUTTON_DRIVINGT_GLANCE_RIGHT_LINK 0x0307C4EC
#endif
#ifndef DIBUTTON_DRIVINGT_MENU
#define DIBUTTON_DRIVINGT_MENU 0x030004FD
#endif
#ifndef DIBUTTON_DRIVINGT_PAUSE
#define DIBUTTON_DRIVINGT_PAUSE 0x030044FC
#endif
#ifndef DIBUTTON_DRIVINGT_ROTATE_LEFT_LINK
#define DIBUTTON_DRIVINGT_ROTATE_LEFT_LINK 0x030244E4
#endif
#ifndef DIBUTTON_DRIVINGT_ROTATE_RIGHT_LINK
#define DIBUTTON_DRIVINGT_ROTATE_RIGHT_LINK 0x030244EC
#endif
#ifndef DIBUTTON_DRIVINGT_STEER_LEFT_LINK
#define DIBUTTON_DRIVINGT_STEER_LEFT_LINK 0x0300CCE4
#endif
#ifndef DIBUTTON_DRIVINGT_STEER_RIGHT_LINK
#define DIBUTTON_DRIVINGT_STEER_RIGHT_LINK 0x0300CCEC
#endif
#ifndef DIBUTTON_DRIVINGT_TARGET
#define DIBUTTON_DRIVINGT_TARGET 0x03000C03
#endif
#ifndef DIBUTTON_DRIVINGT_VIEW
#define DIBUTTON_DRIVINGT_VIEW 0x03005C04
#endif
#ifndef DIBUTTON_DRIVINGT_WEAPONS
#define DIBUTTON_DRIVINGT_WEAPONS 0x03000C02
#endif
#ifndef DIBUTTON_FIGHTINGH_BACKWARD_LINK
#define DIBUTTON_FIGHTINGH_BACKWARD_LINK 0x080144E8
#endif
#ifndef DIBUTTON_FIGHTINGH_BLOCK
#define DIBUTTON_FIGHTINGH_BLOCK 0x08000403
#endif
#ifndef DIBUTTON_FIGHTINGH_CROUCH
#define DIBUTTON_FIGHTINGH_CROUCH 0x08000404
#endif
#ifndef DIBUTTON_FIGHTINGH_DEVICE
#define DIBUTTON_FIGHTINGH_DEVICE 0x080044FE
#endif
#ifndef DIBUTTON_FIGHTINGH_DISPLAY
#define DIBUTTON_FIGHTINGH_DISPLAY 0x08004409
#endif
#ifndef DIBUTTON_FIGHTINGH_DODGE
#define DIBUTTON_FIGHTINGH_DODGE 0x0800440A
#endif
#ifndef DIBUTTON_FIGHTINGH_FORWARD_LINK
#define DIBUTTON_FIGHTINGH_FORWARD_LINK 0x080144E0
#endif
#ifndef DIBUTTON_FIGHTINGH_JUMP
#define DIBUTTON_FIGHTINGH_JUMP 0x08000405
#endif
#ifndef DIBUTTON_FIGHTINGH_KICK
#define DIBUTTON_FIGHTINGH_KICK 0x08000402
#endif
#ifndef DIBUTTON_FIGHTINGH_LEFT_LINK
#define DIBUTTON_FIGHTINGH_LEFT_LINK 0x0800C4E4
#endif
#ifndef DIBUTTON_FIGHTINGH_MENU
#define DIBUTTON_FIGHTINGH_MENU 0x080004FD
#endif
#ifndef DIBUTTON_FIGHTINGH_PAUSE
#define DIBUTTON_FIGHTINGH_PAUSE 0x080044FC
#endif
#ifndef DIBUTTON_FIGHTINGH_PUNCH
#define DIBUTTON_FIGHTINGH_PUNCH 0x08000401
#endif
#ifndef DIBUTTON_FIGHTINGH_RIGHT_LINK
#define DIBUTTON_FIGHTINGH_RIGHT_LINK 0x0800C4EC
#endif
#ifndef DIBUTTON_FIGHTINGH_SELECT
#define DIBUTTON_FIGHTINGH_SELECT 0x08004408
#endif
#ifndef DIBUTTON_FIGHTINGH_SPECIAL1
#define DIBUTTON_FIGHTINGH_SPECIAL1 0x08000406
#endif
#ifndef DIBUTTON_FIGHTINGH_SPECIAL2
#define DIBUTTON_FIGHTINGH_SPECIAL2 0x08000407
#endif
#ifndef DIBUTTON_FISHING_BACK_LINK
#define DIBUTTON_FISHING_BACK_LINK 0x0E0144E8
#endif
#ifndef DIBUTTON_FISHING_BAIT
#define DIBUTTON_FISHING_BAIT 0x0E000404
#endif
#ifndef DIBUTTON_FISHING_BINOCULAR
#define DIBUTTON_FISHING_BINOCULAR 0x0E000403
#endif
#ifndef DIBUTTON_FISHING_CAST
#define DIBUTTON_FISHING_CAST 0x0E000401
#endif
#ifndef DIBUTTON_FISHING_CROUCH
#define DIBUTTON_FISHING_CROUCH 0x0E004407
#endif
#ifndef DIBUTTON_FISHING_DEVICE
#define DIBUTTON_FISHING_DEVICE 0x0E0044FE
#endif
#ifndef DIBUTTON_FISHING_DISPLAY
#define DIBUTTON_FISHING_DISPLAY 0x0E004406
#endif
#ifndef DIBUTTON_FISHING_FORWARD_LINK
#define DIBUTTON_FISHING_FORWARD_LINK 0x0E0144E0
#endif
#ifndef DIBUTTON_FISHING_JUMP
#define DIBUTTON_FISHING_JUMP 0x0E004408
#endif
#ifndef DIBUTTON_FISHING_LEFT_LINK
#define DIBUTTON_FISHING_LEFT_LINK 0x0E00C4E4
#endif
#ifndef DIBUTTON_FISHING_MAP
#define DIBUTTON_FISHING_MAP 0x0E000405
#endif
#ifndef DIBUTTON_FISHING_MENU
#define DIBUTTON_FISHING_MENU 0x0E0004FD
#endif
#ifndef DIBUTTON_FISHING_PAUSE
#define DIBUTTON_FISHING_PAUSE 0x0E0044FC
#endif
#ifndef DIBUTTON_FISHING_RIGHT_LINK
#define DIBUTTON_FISHING_RIGHT_LINK 0x0E00C4EC
#endif
#ifndef DIBUTTON_FISHING_ROTATE_LEFT_LINK
#define DIBUTTON_FISHING_ROTATE_LEFT_LINK 0x0E0244E4
#endif
#ifndef DIBUTTON_FISHING_ROTATE_RIGHT_LINK
#define DIBUTTON_FISHING_ROTATE_RIGHT_LINK 0x0E0244EC
#endif
#ifndef DIBUTTON_FISHING_TYPE
#define DIBUTTON_FISHING_TYPE 0x0E000402
#endif
#ifndef DIBUTTON_FLYINGC_BRAKE_LINK
#define DIBUTTON_FLYINGC_BRAKE_LINK 0x04046CE0
#endif
#ifndef DIBUTTON_FLYINGC_DEVICE
#define DIBUTTON_FLYINGC_DEVICE 0x040044FE
#endif
#ifndef DIBUTTON_FLYINGC_DISPLAY
#define DIBUTTON_FLYINGC_DISPLAY 0x04002402
#endif
#ifndef DIBUTTON_FLYINGC_FASTER_LINK
#define DIBUTTON_FLYINGC_FASTER_LINK 0x0403D4E0
#endif
#ifndef DIBUTTON_FLYINGC_FLAPSDOWN
#define DIBUTTON_FLYINGC_FLAPSDOWN 0x04006405
#endif
#ifndef DIBUTTON_FLYINGC_FLAPSUP
#define DIBUTTON_FLYINGC_FLAPSUP 0x04006404
#endif
#ifndef DIBUTTON_FLYINGC_GEAR
#define DIBUTTON_FLYINGC_GEAR 0x04002C03
#endif
#ifndef DIBUTTON_FLYINGC_GLANCE_DOWN_LINK
#define DIBUTTON_FLYINGC_GLANCE_DOWN_LINK 0x0407C4E8
#endif
#ifndef DIBUTTON_FLYINGC_GLANCE_LEFT_LINK
#define DIBUTTON_FLYINGC_GLANCE_LEFT_LINK 0x0407C4E4
#endif
#ifndef DIBUTTON_FLYINGC_GLANCE_RIGHT_LINK
#define DIBUTTON_FLYINGC_GLANCE_RIGHT_LINK 0x0407C4EC
#endif
#ifndef DIBUTTON_FLYINGC_GLANCE_UP_LINK
#define DIBUTTON_FLYINGC_GLANCE_UP_LINK 0x0407C4E0
#endif
#ifndef DIBUTTON_FLYINGC_MENU
#define DIBUTTON_FLYINGC_MENU 0x040004FD
#endif
#ifndef DIBUTTON_FLYINGC_PAUSE
#define DIBUTTON_FLYINGC_PAUSE 0x040044FC
#endif
#ifndef DIBUTTON_FLYINGC_SLOWER_LINK
#define DIBUTTON_FLYINGC_SLOWER_LINK 0x0403D4E8
#endif
#ifndef DIBUTTON_FLYINGC_VIEW
#define DIBUTTON_FLYINGC_VIEW 0x04002401
#endif
#ifndef DIBUTTON_FLYINGM_BRAKE_LINK
#define DIBUTTON_FLYINGM_BRAKE_LINK 0x050464E0
#endif
#ifndef DIBUTTON_FLYINGM_COUNTER
#define DIBUTTON_FLYINGM_COUNTER 0x05005C04
#endif
#ifndef DIBUTTON_FLYINGM_DEVICE
#define DIBUTTON_FLYINGM_DEVICE 0x050044FE
#endif
#ifndef DIBUTTON_FLYINGM_DISPLAY
#define DIBUTTON_FLYINGM_DISPLAY 0x05006406
#endif
#ifndef DIBUTTON_FLYINGM_FASTER_LINK
#define DIBUTTON_FLYINGM_FASTER_LINK 0x0503D4E0
#endif
#ifndef DIBUTTON_FLYINGM_FIRE
#define DIBUTTON_FLYINGM_FIRE 0x05000C01
#endif
#ifndef DIBUTTON_FLYINGM_FIRESECONDARY
#define DIBUTTON_FLYINGM_FIRESECONDARY 0x05004C09
#endif
#ifndef DIBUTTON_FLYINGM_FLAPSDOWN
#define DIBUTTON_FLYINGM_FLAPSDOWN 0x05005408
#endif
#ifndef DIBUTTON_FLYINGM_FLAPSUP
#define DIBUTTON_FLYINGM_FLAPSUP 0x05005407
#endif
#ifndef DIBUTTON_FLYINGM_GEAR
#define DIBUTTON_FLYINGM_GEAR 0x0500640A
#endif
#ifndef DIBUTTON_FLYINGM_GLANCE_DOWN_LINK
#define DIBUTTON_FLYINGM_GLANCE_DOWN_LINK 0x0507C4E8
#endif
#ifndef DIBUTTON_FLYINGM_GLANCE_LEFT_LINK
#define DIBUTTON_FLYINGM_GLANCE_LEFT_LINK 0x0507C4E4
#endif
#ifndef DIBUTTON_FLYINGM_GLANCE_RIGHT_LINK
#define DIBUTTON_FLYINGM_GLANCE_RIGHT_LINK 0x0507C4EC
#endif
#ifndef DIBUTTON_FLYINGM_GLANCE_UP_LINK
#define DIBUTTON_FLYINGM_GLANCE_UP_LINK 0x0507C4E0
#endif
#ifndef DIBUTTON_FLYINGM_MENU
#define DIBUTTON_FLYINGM_MENU 0x050004FD
#endif
#ifndef DIBUTTON_FLYINGM_PAUSE
#define DIBUTTON_FLYINGM_PAUSE 0x050044FC
#endif
#ifndef DIBUTTON_FLYINGM_SLOWER_LINK
#define DIBUTTON_FLYINGM_SLOWER_LINK 0x0503D4E8
#endif
#ifndef DIBUTTON_FLYINGM_TARGET
#define DIBUTTON_FLYINGM_TARGET 0x05000C03
#endif
#ifndef DIBUTTON_FLYINGM_VIEW
#define DIBUTTON_FLYINGM_VIEW 0x05006405
#endif
#ifndef DIBUTTON_FLYINGM_WEAPONS
#define DIBUTTON_FLYINGM_WEAPONS 0x05000C02
#endif
#ifndef DIBUTTON_FOOTBALLD_AUDIBLE
#define DIBUTTON_FOOTBALLD_AUDIBLE 0x1700440B
#endif
#ifndef DIBUTTON_FOOTBALLD_BACK_LINK
#define DIBUTTON_FOOTBALLD_BACK_LINK 0x170144E8
#endif
#ifndef DIBUTTON_FOOTBALLD_BULLRUSH
#define DIBUTTON_FOOTBALLD_BULLRUSH 0x17004409
#endif
#ifndef DIBUTTON_FOOTBALLD_DEVICE
#define DIBUTTON_FOOTBALLD_DEVICE 0x170044FE
#endif
#ifndef DIBUTTON_FOOTBALLD_FAKE
#define DIBUTTON_FOOTBALLD_FAKE 0x17000405
#endif
#ifndef DIBUTTON_FOOTBALLD_FORWARD_LINK
#define DIBUTTON_FOOTBALLD_FORWARD_LINK 0x170144E0
#endif
#ifndef DIBUTTON_FOOTBALLD_JUMP
#define DIBUTTON_FOOTBALLD_JUMP 0x17000403
#endif
#ifndef DIBUTTON_FOOTBALLD_LEFT_LINK
#define DIBUTTON_FOOTBALLD_LEFT_LINK 0x1700C4E4
#endif
#ifndef DIBUTTON_FOOTBALLD_MENU
#define DIBUTTON_FOOTBALLD_MENU 0x170004FD
#endif
#ifndef DIBUTTON_FOOTBALLD_PAUSE
#define DIBUTTON_FOOTBALLD_PAUSE 0x170044FC
#endif
#ifndef DIBUTTON_FOOTBALLD_PLAY
#define DIBUTTON_FOOTBALLD_PLAY 0x17000401
#endif
#ifndef DIBUTTON_FOOTBALLD_RIGHT_LINK
#define DIBUTTON_FOOTBALLD_RIGHT_LINK 0x1700C4EC
#endif
#ifndef DIBUTTON_FOOTBALLD_RIP
#define DIBUTTON_FOOTBALLD_RIP 0x1700440A
#endif
#ifndef DIBUTTON_FOOTBALLD_SELECT
#define DIBUTTON_FOOTBALLD_SELECT 0x17000402
#endif
#ifndef DIBUTTON_FOOTBALLD_SPIN
#define DIBUTTON_FOOTBALLD_SPIN 0x17004407
#endif
#ifndef DIBUTTON_FOOTBALLD_SUBSTITUTE
#define DIBUTTON_FOOTBALLD_SUBSTITUTE 0x1700440D
#endif
#ifndef DIBUTTON_FOOTBALLD_SUPERTACKLE
#define DIBUTTON_FOOTBALLD_SUPERTACKLE 0x17000406
#endif
#ifndef DIBUTTON_FOOTBALLD_SWIM
#define DIBUTTON_FOOTBALLD_SWIM 0x17004408
#endif
#ifndef DIBUTTON_FOOTBALLD_TACKLE
#define DIBUTTON_FOOTBALLD_TACKLE 0x17000404
#endif
#ifndef DIBUTTON_FOOTBALLD_ZOOM
#define DIBUTTON_FOOTBALLD_ZOOM 0x1700440C
#endif
#ifndef DIBUTTON_FOOTBALLO_BACK_LINK
#define DIBUTTON_FOOTBALLO_BACK_LINK 0x160144E8
#endif
#ifndef DIBUTTON_FOOTBALLO_DEVICE
#define DIBUTTON_FOOTBALLO_DEVICE 0x160044FE
#endif
#ifndef DIBUTTON_FOOTBALLO_DIVE
#define DIBUTTON_FOOTBALLO_DIVE 0x16004409
#endif
#ifndef DIBUTTON_FOOTBALLO_FORWARD_LINK
#define DIBUTTON_FOOTBALLO_FORWARD_LINK 0x160144E0
#endif
#ifndef DIBUTTON_FOOTBALLO_JUKE
#define DIBUTTON_FOOTBALLO_JUKE 0x16004406
#endif
#ifndef DIBUTTON_FOOTBALLO_JUMP
#define DIBUTTON_FOOTBALLO_JUMP 0x16000401
#endif
#ifndef DIBUTTON_FOOTBALLO_LEFTARM
#define DIBUTTON_FOOTBALLO_LEFTARM 0x16000402
#endif
#ifndef DIBUTTON_FOOTBALLO_LEFT_LINK
#define DIBUTTON_FOOTBALLO_LEFT_LINK 0x1600C4E4
#endif
#ifndef DIBUTTON_FOOTBALLO_MENU
#define DIBUTTON_FOOTBALLO_MENU 0x160004FD
#endif
#ifndef DIBUTTON_FOOTBALLO_PAUSE
#define DIBUTTON_FOOTBALLO_PAUSE 0x160044FC
#endif
#ifndef DIBUTTON_FOOTBALLO_RIGHTARM
#define DIBUTTON_FOOTBALLO_RIGHTARM 0x16000403
#endif
#ifndef DIBUTTON_FOOTBALLO_RIGHT_LINK
#define DIBUTTON_FOOTBALLO_RIGHT_LINK 0x1600C4EC
#endif
#ifndef DIBUTTON_FOOTBALLO_SHOULDER
#define DIBUTTON_FOOTBALLO_SHOULDER 0x16004407
#endif
#ifndef DIBUTTON_FOOTBALLO_SPIN
#define DIBUTTON_FOOTBALLO_SPIN 0x16000405
#endif
#ifndef DIBUTTON_FOOTBALLO_SUBSTITUTE
#define DIBUTTON_FOOTBALLO_SUBSTITUTE 0x1600440B
#endif
#ifndef DIBUTTON_FOOTBALLO_THROW
#define DIBUTTON_FOOTBALLO_THROW 0x16000404
#endif
#ifndef DIBUTTON_FOOTBALLO_TURBO
#define DIBUTTON_FOOTBALLO_TURBO 0x16004408
#endif
#ifndef DIBUTTON_FOOTBALLO_ZOOM
#define DIBUTTON_FOOTBALLO_ZOOM 0x1600440A
#endif
#ifndef DIBUTTON_FOOTBALLP_DEVICE
#define DIBUTTON_FOOTBALLP_DEVICE 0x140044FE
#endif
#ifndef DIBUTTON_FOOTBALLP_HELP
#define DIBUTTON_FOOTBALLP_HELP 0x14000403
#endif
#ifndef DIBUTTON_FOOTBALLP_MENU
#define DIBUTTON_FOOTBALLP_MENU 0x140004FD
#endif
#ifndef DIBUTTON_FOOTBALLP_PAUSE
#define DIBUTTON_FOOTBALLP_PAUSE 0x140044FC
#endif
#ifndef DIBUTTON_FOOTBALLP_PLAY
#define DIBUTTON_FOOTBALLP_PLAY 0x14000401
#endif
#ifndef DIBUTTON_FOOTBALLP_SELECT
#define DIBUTTON_FOOTBALLP_SELECT 0x14000402
#endif
#ifndef DIBUTTON_FOOTBALLQ_AUDIBLE
#define DIBUTTON_FOOTBALLQ_AUDIBLE 0x15004409
#endif
#ifndef DIBUTTON_FOOTBALLQ_BACK_LINK
#define DIBUTTON_FOOTBALLQ_BACK_LINK 0x150144E8
#endif
#ifndef DIBUTTON_FOOTBALLQ_DEVICE
#define DIBUTTON_FOOTBALLQ_DEVICE 0x150044FE
#endif
#ifndef DIBUTTON_FOOTBALLQ_FAKE
#define DIBUTTON_FOOTBALLQ_FAKE 0x15000406
#endif
#ifndef DIBUTTON_FOOTBALLQ_FAKESNAP
#define DIBUTTON_FOOTBALLQ_FAKESNAP 0x15004407
#endif
#ifndef DIBUTTON_FOOTBALLQ_FORWARD_LINK
#define DIBUTTON_FOOTBALLQ_FORWARD_LINK 0x150144E0
#endif
#ifndef DIBUTTON_FOOTBALLQ_JUMP
#define DIBUTTON_FOOTBALLQ_JUMP 0x15000403
#endif
#ifndef DIBUTTON_FOOTBALLQ_LEFT_LINK
#define DIBUTTON_FOOTBALLQ_LEFT_LINK 0x1500C4E4
#endif
#ifndef DIBUTTON_FOOTBALLQ_MENU
#define DIBUTTON_FOOTBALLQ_MENU 0x150004FD
#endif
#ifndef DIBUTTON_FOOTBALLQ_MOTION
#define DIBUTTON_FOOTBALLQ_MOTION 0x15004408
#endif
#ifndef DIBUTTON_FOOTBALLQ_PASS
#define DIBUTTON_FOOTBALLQ_PASS 0x15000405
#endif
#ifndef DIBUTTON_FOOTBALLQ_PAUSE
#define DIBUTTON_FOOTBALLQ_PAUSE 0x150044FC
#endif
#ifndef DIBUTTON_FOOTBALLQ_RIGHT_LINK
#define DIBUTTON_FOOTBALLQ_RIGHT_LINK 0x1500C4EC
#endif
#ifndef DIBUTTON_FOOTBALLQ_SELECT
#define DIBUTTON_FOOTBALLQ_SELECT 0x15000401
#endif
#ifndef DIBUTTON_FOOTBALLQ_SLIDE
#define DIBUTTON_FOOTBALLQ_SLIDE 0x15000404
#endif
#ifndef DIBUTTON_FOOTBALLQ_SNAP
#define DIBUTTON_FOOTBALLQ_SNAP 0x15000402
#endif
#ifndef DIBUTTON_FPS_APPLY
#define DIBUTTON_FPS_APPLY 0x09000403
#endif
#ifndef DIBUTTON_FPS_BACKWARD_LINK
#define DIBUTTON_FPS_BACKWARD_LINK 0x090144E8
#endif
#ifndef DIBUTTON_FPS_CROUCH
#define DIBUTTON_FPS_CROUCH 0x09000405
#endif
#ifndef DIBUTTON_FPS_DEVICE
#define DIBUTTON_FPS_DEVICE 0x090044FE
#endif
#ifndef DIBUTTON_FPS_DISPLAY
#define DIBUTTON_FPS_DISPLAY 0x09004408
#endif
#ifndef DIBUTTON_FPS_DODGE
#define DIBUTTON_FPS_DODGE 0x09004409
#endif
#ifndef DIBUTTON_FPS_FIRE
#define DIBUTTON_FPS_FIRE 0x09000401
#endif
#ifndef DIBUTTON_FPS_FIRESECONDARY
#define DIBUTTON_FPS_FIRESECONDARY 0x0900440C
#endif
#ifndef DIBUTTON_FPS_FORWARD_LINK
#define DIBUTTON_FPS_FORWARD_LINK 0x090144E0
#endif
#ifndef DIBUTTON_FPS_GLANCEL
#define DIBUTTON_FPS_GLANCEL 0x0900440A
#endif
#ifndef DIBUTTON_FPS_GLANCER
#define DIBUTTON_FPS_GLANCER 0x0900440B
#endif
#ifndef DIBUTTON_FPS_GLANCE_DOWN_LINK
#define DIBUTTON_FPS_GLANCE_DOWN_LINK 0x0901C4E8
#endif
#ifndef DIBUTTON_FPS_GLANCE_UP_LINK
#define DIBUTTON_FPS_GLANCE_UP_LINK 0x0901C4E0
#endif
#ifndef DIBUTTON_FPS_JUMP
#define DIBUTTON_FPS_JUMP 0x09000406
#endif
#ifndef DIBUTTON_FPS_MENU
#define DIBUTTON_FPS_MENU 0x090004FD
#endif
#ifndef DIBUTTON_FPS_PAUSE
#define DIBUTTON_FPS_PAUSE 0x090044FC
#endif
#ifndef DIBUTTON_FPS_ROTATE_LEFT_LINK
#define DIBUTTON_FPS_ROTATE_LEFT_LINK 0x0900C4E4
#endif
#ifndef DIBUTTON_FPS_ROTATE_RIGHT_LINK
#define DIBUTTON_FPS_ROTATE_RIGHT_LINK 0x0900C4EC
#endif
#ifndef DIBUTTON_FPS_SELECT
#define DIBUTTON_FPS_SELECT 0x09000404
#endif
#ifndef DIBUTTON_FPS_STEP_LEFT_LINK
#define DIBUTTON_FPS_STEP_LEFT_LINK 0x090244E4
#endif
#ifndef DIBUTTON_FPS_STEP_RIGHT_LINK
#define DIBUTTON_FPS_STEP_RIGHT_LINK 0x090244EC
#endif
#ifndef DIBUTTON_FPS_STRAFE
#define DIBUTTON_FPS_STRAFE 0x09000407
#endif
#ifndef DIBUTTON_FPS_WEAPONS
#define DIBUTTON_FPS_WEAPONS 0x09000402
#endif
#ifndef DIBUTTON_GOLF_BACK_LINK
#define DIBUTTON_GOLF_BACK_LINK 0x180144E8
#endif
#ifndef DIBUTTON_GOLF_DEVICE
#define DIBUTTON_GOLF_DEVICE 0x180044FE
#endif
#ifndef DIBUTTON_GOLF_DOWN
#define DIBUTTON_GOLF_DOWN 0x18000404
#endif
#ifndef DIBUTTON_GOLF_FLYBY
#define DIBUTTON_GOLF_FLYBY 0x18000406
#endif
#ifndef DIBUTTON_GOLF_FORWARD_LINK
#define DIBUTTON_GOLF_FORWARD_LINK 0x180144E0
#endif
#ifndef DIBUTTON_GOLF_LEFT_LINK
#define DIBUTTON_GOLF_LEFT_LINK 0x1800C4E4
#endif
#ifndef DIBUTTON_GOLF_MENU
#define DIBUTTON_GOLF_MENU 0x180004FD
#endif
#ifndef DIBUTTON_GOLF_PAUSE
#define DIBUTTON_GOLF_PAUSE 0x180044FC
#endif
#ifndef DIBUTTON_GOLF_RIGHT_LINK
#define DIBUTTON_GOLF_RIGHT_LINK 0x1800C4EC
#endif
#ifndef DIBUTTON_GOLF_SELECT
#define DIBUTTON_GOLF_SELECT 0x18000402
#endif
#ifndef DIBUTTON_GOLF_SUBSTITUTE
#define DIBUTTON_GOLF_SUBSTITUTE 0x18004409
#endif
#ifndef DIBUTTON_GOLF_SWING
#define DIBUTTON_GOLF_SWING 0x18000401
#endif
#ifndef DIBUTTON_GOLF_TERRAIN
#define DIBUTTON_GOLF_TERRAIN 0x18000405
#endif
#ifndef DIBUTTON_GOLF_TIMEOUT
#define DIBUTTON_GOLF_TIMEOUT 0x18004408
#endif
#ifndef DIBUTTON_GOLF_UP
#define DIBUTTON_GOLF_UP 0x18000403
#endif
#ifndef DIBUTTON_GOLF_ZOOM
#define DIBUTTON_GOLF_ZOOM 0x18004407
#endif
#ifndef DIBUTTON_HOCKEYD_BACK_LINK
#define DIBUTTON_HOCKEYD_BACK_LINK 0x1A0144E8
#endif
#ifndef DIBUTTON_HOCKEYD_BLOCK
#define DIBUTTON_HOCKEYD_BLOCK 0x1A000404
#endif
#ifndef DIBUTTON_HOCKEYD_BURST
#define DIBUTTON_HOCKEYD_BURST 0x1A000403
#endif
#ifndef DIBUTTON_HOCKEYD_DEVICE
#define DIBUTTON_HOCKEYD_DEVICE 0x1A0044FE
#endif
#ifndef DIBUTTON_HOCKEYD_FAKE
#define DIBUTTON_HOCKEYD_FAKE 0x1A000405
#endif
#ifndef DIBUTTON_HOCKEYD_FORWARD_LINK
#define DIBUTTON_HOCKEYD_FORWARD_LINK 0x1A0144E0
#endif
#ifndef DIBUTTON_HOCKEYD_LEFT_LINK
#define DIBUTTON_HOCKEYD_LEFT_LINK 0x1A00C4E4
#endif
#ifndef DIBUTTON_HOCKEYD_MENU
#define DIBUTTON_HOCKEYD_MENU 0x1A0004FD
#endif
#ifndef DIBUTTON_HOCKEYD_PAUSE
#define DIBUTTON_HOCKEYD_PAUSE 0x1A0044FC
#endif
#ifndef DIBUTTON_HOCKEYD_PLAYER
#define DIBUTTON_HOCKEYD_PLAYER 0x1A000401
#endif
#ifndef DIBUTTON_HOCKEYD_RIGHT_LINK
#define DIBUTTON_HOCKEYD_RIGHT_LINK 0x1A00C4EC
#endif
#ifndef DIBUTTON_HOCKEYD_STEAL
#define DIBUTTON_HOCKEYD_STEAL 0x1A000402
#endif
#ifndef DIBUTTON_HOCKEYD_STRATEGY
#define DIBUTTON_HOCKEYD_STRATEGY 0x1A004407
#endif
#ifndef DIBUTTON_HOCKEYD_SUBSTITUTE
#define DIBUTTON_HOCKEYD_SUBSTITUTE 0x1A004409
#endif
#ifndef DIBUTTON_HOCKEYD_TIMEOUT
#define DIBUTTON_HOCKEYD_TIMEOUT 0x1A004408
#endif
#ifndef DIBUTTON_HOCKEYD_ZOOM
#define DIBUTTON_HOCKEYD_ZOOM 0x1A004406
#endif
#ifndef DIBUTTON_HOCKEYG_BACK_LINK
#define DIBUTTON_HOCKEYG_BACK_LINK 0x1B0144E8
#endif
#ifndef DIBUTTON_HOCKEYG_BLOCK
#define DIBUTTON_HOCKEYG_BLOCK 0x1B000404
#endif
#ifndef DIBUTTON_HOCKEYG_DEVICE
#define DIBUTTON_HOCKEYG_DEVICE 0x1B0044FE
#endif
#ifndef DIBUTTON_HOCKEYG_FORWARD_LINK
#define DIBUTTON_HOCKEYG_FORWARD_LINK 0x1B0144E0
#endif
#ifndef DIBUTTON_HOCKEYG_LEFT_LINK
#define DIBUTTON_HOCKEYG_LEFT_LINK 0x1B00C4E4
#endif
#ifndef DIBUTTON_HOCKEYG_MENU
#define DIBUTTON_HOCKEYG_MENU 0x1B0004FD
#endif
#ifndef DIBUTTON_HOCKEYG_PASS
#define DIBUTTON_HOCKEYG_PASS 0x1B000401
#endif
#ifndef DIBUTTON_HOCKEYG_PAUSE
#define DIBUTTON_HOCKEYG_PAUSE 0x1B0044FC
#endif
#ifndef DIBUTTON_HOCKEYG_POKE
#define DIBUTTON_HOCKEYG_POKE 0x1B000402
#endif
#ifndef DIBUTTON_HOCKEYG_RIGHT_LINK
#define DIBUTTON_HOCKEYG_RIGHT_LINK 0x1B00C4EC
#endif
#ifndef DIBUTTON_HOCKEYG_STEAL
#define DIBUTTON_HOCKEYG_STEAL 0x1B000403
#endif
#ifndef DIBUTTON_HOCKEYG_STRATEGY
#define DIBUTTON_HOCKEYG_STRATEGY 0x1B004406
#endif
#ifndef DIBUTTON_HOCKEYG_SUBSTITUTE
#define DIBUTTON_HOCKEYG_SUBSTITUTE 0x1B004408
#endif
#ifndef DIBUTTON_HOCKEYG_TIMEOUT
#define DIBUTTON_HOCKEYG_TIMEOUT 0x1B004407
#endif
#ifndef DIBUTTON_HOCKEYG_ZOOM
#define DIBUTTON_HOCKEYG_ZOOM 0x1B004405
#endif
#ifndef DIBUTTON_HOCKEYO_BACK_LINK
#define DIBUTTON_HOCKEYO_BACK_LINK 0x190144E8
#endif
#ifndef DIBUTTON_HOCKEYO_BURST
#define DIBUTTON_HOCKEYO_BURST 0x19000403
#endif
#ifndef DIBUTTON_HOCKEYO_DEVICE
#define DIBUTTON_HOCKEYO_DEVICE 0x190044FE
#endif
#ifndef DIBUTTON_HOCKEYO_FAKE
#define DIBUTTON_HOCKEYO_FAKE 0x19000405
#endif
#ifndef DIBUTTON_HOCKEYO_FORWARD_LINK
#define DIBUTTON_HOCKEYO_FORWARD_LINK 0x190144E0
#endif
#ifndef DIBUTTON_HOCKEYO_LEFT_LINK
#define DIBUTTON_HOCKEYO_LEFT_LINK 0x1900C4E4
#endif
#ifndef DIBUTTON_HOCKEYO_MENU
#define DIBUTTON_HOCKEYO_MENU 0x190004FD
#endif
#ifndef DIBUTTON_HOCKEYO_PASS
#define DIBUTTON_HOCKEYO_PASS 0x19000402
#endif
#ifndef DIBUTTON_HOCKEYO_PAUSE
#define DIBUTTON_HOCKEYO_PAUSE 0x190044FC
#endif
#ifndef DIBUTTON_HOCKEYO_RIGHT_LINK
#define DIBUTTON_HOCKEYO_RIGHT_LINK 0x1900C4EC
#endif
#ifndef DIBUTTON_HOCKEYO_SHOOT
#define DIBUTTON_HOCKEYO_SHOOT 0x19000401
#endif
#ifndef DIBUTTON_HOCKEYO_SPECIAL
#define DIBUTTON_HOCKEYO_SPECIAL 0x19000404
#endif
#ifndef DIBUTTON_HOCKEYO_STRATEGY
#define DIBUTTON_HOCKEYO_STRATEGY 0x19004407
#endif
#ifndef DIBUTTON_HOCKEYO_SUBSTITUTE
#define DIBUTTON_HOCKEYO_SUBSTITUTE 0x19004409
#endif
#ifndef DIBUTTON_HOCKEYO_TIMEOUT
#define DIBUTTON_HOCKEYO_TIMEOUT 0x19004408
#endif
#ifndef DIBUTTON_HOCKEYO_ZOOM
#define DIBUTTON_HOCKEYO_ZOOM 0x19004406
#endif
#ifndef DIBUTTON_HUNTING_AIM
#define DIBUTTON_HUNTING_AIM 0x0D000402
#endif
#ifndef DIBUTTON_HUNTING_BACK_LINK
#define DIBUTTON_HUNTING_BACK_LINK 0x0D0144E8
#endif
#ifndef DIBUTTON_HUNTING_BINOCULAR
#define DIBUTTON_HUNTING_BINOCULAR 0x0D000404
#endif
#ifndef DIBUTTON_HUNTING_CALL
#define DIBUTTON_HUNTING_CALL 0x0D000405
#endif
#ifndef DIBUTTON_HUNTING_CROUCH
#define DIBUTTON_HUNTING_CROUCH 0x0D004409
#endif
#ifndef DIBUTTON_HUNTING_DEVICE
#define DIBUTTON_HUNTING_DEVICE 0x0D0044FE
#endif
#ifndef DIBUTTON_HUNTING_DISPLAY
#define DIBUTTON_HUNTING_DISPLAY 0x0D004408
#endif
#ifndef DIBUTTON_HUNTING_FIRE
#define DIBUTTON_HUNTING_FIRE 0x0D000401
#endif
#ifndef DIBUTTON_HUNTING_FIRESECONDARY
#define DIBUTTON_HUNTING_FIRESECONDARY 0x0D00440B
#endif
#ifndef DIBUTTON_HUNTING_FORWARD_LINK
#define DIBUTTON_HUNTING_FORWARD_LINK 0x0D0144E0
#endif
#ifndef DIBUTTON_HUNTING_JUMP
#define DIBUTTON_HUNTING_JUMP 0x0D00440A
#endif
#ifndef DIBUTTON_HUNTING_LEFT_LINK
#define DIBUTTON_HUNTING_LEFT_LINK 0x0D00C4E4
#endif
#ifndef DIBUTTON_HUNTING_MAP
#define DIBUTTON_HUNTING_MAP 0x0D000406
#endif
#ifndef DIBUTTON_HUNTING_MENU
#define DIBUTTON_HUNTING_MENU 0x0D0004FD
#endif
#ifndef DIBUTTON_HUNTING_PAUSE
#define DIBUTTON_HUNTING_PAUSE 0x0D0044FC
#endif
#ifndef DIBUTTON_HUNTING_RIGHT_LINK
#define DIBUTTON_HUNTING_RIGHT_LINK 0x0D00C4EC
#endif
#ifndef DIBUTTON_HUNTING_ROTATE_LEFT_LINK
#define DIBUTTON_HUNTING_ROTATE_LEFT_LINK 0x0D0244E4
#endif
#ifndef DIBUTTON_HUNTING_ROTATE_RIGHT_LINK
#define DIBUTTON_HUNTING_ROTATE_RIGHT_LINK 0x0D0244EC
#endif
#ifndef DIBUTTON_HUNTING_SPECIAL
#define DIBUTTON_HUNTING_SPECIAL 0x0D000407
#endif
#ifndef DIBUTTON_HUNTING_WEAPON
#define DIBUTTON_HUNTING_WEAPON 0x0D000403
#endif
#ifndef DIBUTTON_MECHA_BACK_LINK
#define DIBUTTON_MECHA_BACK_LINK 0x290144E8
#endif
#ifndef DIBUTTON_MECHA_CENTER
#define DIBUTTON_MECHA_CENTER 0x29004407
#endif
#ifndef DIBUTTON_MECHA_DEVICE
#define DIBUTTON_MECHA_DEVICE 0x290044FE
#endif
#ifndef DIBUTTON_MECHA_FASTER_LINK
#define DIBUTTON_MECHA_FASTER_LINK 0x2903C4E0
#endif
#ifndef DIBUTTON_MECHA_FIRE
#define DIBUTTON_MECHA_FIRE 0x29000401
#endif
#ifndef DIBUTTON_MECHA_FIRESECONDARY
#define DIBUTTON_MECHA_FIRESECONDARY 0x29004409
#endif
#ifndef DIBUTTON_MECHA_FORWARD_LINK
#define DIBUTTON_MECHA_FORWARD_LINK 0x290144E0
#endif
#ifndef DIBUTTON_MECHA_JUMP
#define DIBUTTON_MECHA_JUMP 0x29000406
#endif
#ifndef DIBUTTON_MECHA_LEFT_LINK
#define DIBUTTON_MECHA_LEFT_LINK 0x2900C4E4
#endif
#ifndef DIBUTTON_MECHA_MENU
#define DIBUTTON_MECHA_MENU 0x290004FD
#endif
#ifndef DIBUTTON_MECHA_PAUSE
#define DIBUTTON_MECHA_PAUSE 0x290044FC
#endif
#ifndef DIBUTTON_MECHA_REVERSE
#define DIBUTTON_MECHA_REVERSE 0x29000404
#endif
#ifndef DIBUTTON_MECHA_RIGHT_LINK
#define DIBUTTON_MECHA_RIGHT_LINK 0x2900C4EC
#endif
#ifndef DIBUTTON_MECHA_ROTATE_LEFT_LINK
#define DIBUTTON_MECHA_ROTATE_LEFT_LINK 0x290244E4
#endif
#ifndef DIBUTTON_MECHA_ROTATE_RIGHT_LINK
#define DIBUTTON_MECHA_ROTATE_RIGHT_LINK 0x290244EC
#endif
#ifndef DIBUTTON_MECHA_SLOWER_LINK
#define DIBUTTON_MECHA_SLOWER_LINK 0x2903C4E8
#endif
#ifndef DIBUTTON_MECHA_TARGET
#define DIBUTTON_MECHA_TARGET 0x29000403
#endif
#ifndef DIBUTTON_MECHA_VIEW
#define DIBUTTON_MECHA_VIEW 0x29004408
#endif
#ifndef DIBUTTON_MECHA_WEAPONS
#define DIBUTTON_MECHA_WEAPONS 0x29000402
#endif
#ifndef DIBUTTON_MECHA_ZOOM
#define DIBUTTON_MECHA_ZOOM 0x29000405
#endif
#ifndef DIBUTTON_RACQUET_BACKSWING
#define DIBUTTON_RACQUET_BACKSWING 0x20000402
#endif
#ifndef DIBUTTON_RACQUET_BACK_LINK
#define DIBUTTON_RACQUET_BACK_LINK 0x200144E8
#endif
#ifndef DIBUTTON_RACQUET_DEVICE
#define DIBUTTON_RACQUET_DEVICE 0x200044FE
#endif
#ifndef DIBUTTON_RACQUET_FORWARD_LINK
#define DIBUTTON_RACQUET_FORWARD_LINK 0x200144E0
#endif
#ifndef DIBUTTON_RACQUET_LEFT_LINK
#define DIBUTTON_RACQUET_LEFT_LINK 0x2000C4E4
#endif
#ifndef DIBUTTON_RACQUET_MENU
#define DIBUTTON_RACQUET_MENU 0x200004FD
#endif
#ifndef DIBUTTON_RACQUET_PAUSE
#define DIBUTTON_RACQUET_PAUSE 0x200044FC
#endif
#ifndef DIBUTTON_RACQUET_RIGHT_LINK
#define DIBUTTON_RACQUET_RIGHT_LINK 0x2000C4EC
#endif
#ifndef DIBUTTON_RACQUET_SELECT
#define DIBUTTON_RACQUET_SELECT 0x20000405
#endif
#ifndef DIBUTTON_RACQUET_SMASH
#define DIBUTTON_RACQUET_SMASH 0x20000403
#endif
#ifndef DIBUTTON_RACQUET_SPECIAL
#define DIBUTTON_RACQUET_SPECIAL 0x20000404
#endif
#ifndef DIBUTTON_RACQUET_SUBSTITUTE
#define DIBUTTON_RACQUET_SUBSTITUTE 0x20004407
#endif
#ifndef DIBUTTON_RACQUET_SWING
#define DIBUTTON_RACQUET_SWING 0x20000401
#endif
#ifndef DIBUTTON_RACQUET_TIMEOUT
#define DIBUTTON_RACQUET_TIMEOUT 0x20004406
#endif
#ifndef DIBUTTON_REMOTE_ADJUST
#define DIBUTTON_REMOTE_ADJUST 0x27005C0E
#endif
#ifndef DIBUTTON_REMOTE_CABLE
#define DIBUTTON_REMOTE_CABLE 0x27005C09
#endif
#ifndef DIBUTTON_REMOTE_CD
#define DIBUTTON_REMOTE_CD 0x27005C0A
#endif
#ifndef DIBUTTON_REMOTE_CHANGE
#define DIBUTTON_REMOTE_CHANGE 0x27002406
#endif
#ifndef DIBUTTON_REMOTE_CUE
#define DIBUTTON_REMOTE_CUE 0x27002404
#endif
#ifndef DIBUTTON_REMOTE_DEVICE
#define DIBUTTON_REMOTE_DEVICE 0x270044FE
#endif
#ifndef DIBUTTON_REMOTE_DIGIT0
#define DIBUTTON_REMOTE_DIGIT0 0x2700540F
#endif
#ifndef DIBUTTON_REMOTE_DIGIT1
#define DIBUTTON_REMOTE_DIGIT1 0x27005410
#endif
#ifndef DIBUTTON_REMOTE_DIGIT2
#define DIBUTTON_REMOTE_DIGIT2 0x27005411
#endif
#ifndef DIBUTTON_REMOTE_DIGIT3
#define DIBUTTON_REMOTE_DIGIT3 0x27005412
#endif
#ifndef DIBUTTON_REMOTE_DIGIT4
#define DIBUTTON_REMOTE_DIGIT4 0x27005413
#endif
#ifndef DIBUTTON_REMOTE_DIGIT5
#define DIBUTTON_REMOTE_DIGIT5 0x27005414
#endif
#ifndef DIBUTTON_REMOTE_DIGIT6
#define DIBUTTON_REMOTE_DIGIT6 0x27005415
#endif
#ifndef DIBUTTON_REMOTE_DIGIT7
#define DIBUTTON_REMOTE_DIGIT7 0x27005416
#endif
#ifndef DIBUTTON_REMOTE_DIGIT8
#define DIBUTTON_REMOTE_DIGIT8 0x27005417
#endif
#ifndef DIBUTTON_REMOTE_DIGIT9
#define DIBUTTON_REMOTE_DIGIT9 0x27005418
#endif
#ifndef DIBUTTON_REMOTE_DVD
#define DIBUTTON_REMOTE_DVD 0x27005C0D
#endif
#ifndef DIBUTTON_REMOTE_MENU
#define DIBUTTON_REMOTE_MENU 0x270004FD
#endif
#ifndef DIBUTTON_REMOTE_MUTE
#define DIBUTTON_REMOTE_MUTE 0x27000401
#endif
#ifndef DIBUTTON_REMOTE_PAUSE
#define DIBUTTON_REMOTE_PAUSE 0x270044FC
#endif
#ifndef DIBUTTON_REMOTE_PLAY
#define DIBUTTON_REMOTE_PLAY 0x27002403
#endif
#ifndef DIBUTTON_REMOTE_RECORD
#define DIBUTTON_REMOTE_RECORD 0x27002407
#endif
#ifndef DIBUTTON_REMOTE_REVIEW
#define DIBUTTON_REMOTE_REVIEW 0x27002405
#endif
#ifndef DIBUTTON_REMOTE_SELECT
#define DIBUTTON_REMOTE_SELECT 0x27000402
#endif
#ifndef DIBUTTON_REMOTE_TUNER
#define DIBUTTON_REMOTE_TUNER 0x27005C0C
#endif
#ifndef DIBUTTON_REMOTE_TV
#define DIBUTTON_REMOTE_TV 0x27005C08
#endif
#ifndef DIBUTTON_REMOTE_VCR
#define DIBUTTON_REMOTE_VCR 0x27005C0B
#endif
#ifndef DIBUTTON_SKIING_CAMERA
#define DIBUTTON_SKIING_CAMERA 0x1D000403
#endif
#ifndef DIBUTTON_SKIING_CROUCH
#define DIBUTTON_SKIING_CROUCH 0x1D000402
#endif
#ifndef DIBUTTON_SKIING_DEVICE
#define DIBUTTON_SKIING_DEVICE 0x1D0044FE
#endif
#ifndef DIBUTTON_SKIING_FASTER_LINK
#define DIBUTTON_SKIING_FASTER_LINK 0x1D0144E0
#endif
#ifndef DIBUTTON_SKIING_JUMP
#define DIBUTTON_SKIING_JUMP 0x1D000401
#endif
#ifndef DIBUTTON_SKIING_LEFT_LINK
#define DIBUTTON_SKIING_LEFT_LINK 0x1D00C4E4
#endif
#ifndef DIBUTTON_SKIING_MENU
#define DIBUTTON_SKIING_MENU 0x1D0004FD
#endif
#ifndef DIBUTTON_SKIING_PAUSE
#define DIBUTTON_SKIING_PAUSE 0x1D0044FC
#endif
#ifndef DIBUTTON_SKIING_RIGHT_LINK
#define DIBUTTON_SKIING_RIGHT_LINK 0x1D00C4EC
#endif
#ifndef DIBUTTON_SKIING_SELECT
#define DIBUTTON_SKIING_SELECT 0x1D000405
#endif
#ifndef DIBUTTON_SKIING_SLOWER_LINK
#define DIBUTTON_SKIING_SLOWER_LINK 0x1D0144E8
#endif
#ifndef DIBUTTON_SKIING_SPECIAL1
#define DIBUTTON_SKIING_SPECIAL1 0x1D000404
#endif
#ifndef DIBUTTON_SKIING_SPECIAL2
#define DIBUTTON_SKIING_SPECIAL2 0x1D000406
#endif
#ifndef DIBUTTON_SKIING_ZOOM
#define DIBUTTON_SKIING_ZOOM 0x1D004407
#endif
#ifndef DIBUTTON_SOCCERD_BACK_LINK
#define DIBUTTON_SOCCERD_BACK_LINK 0x1F0144E8
#endif
#ifndef DIBUTTON_SOCCERD_BLOCK
#define DIBUTTON_SOCCERD_BLOCK 0x1F000401
#endif
#ifndef DIBUTTON_SOCCERD_CLEAR
#define DIBUTTON_SOCCERD_CLEAR 0x1F00440A
#endif
#ifndef DIBUTTON_SOCCERD_DEVICE
#define DIBUTTON_SOCCERD_DEVICE 0x1F0044FE
#endif
#ifndef DIBUTTON_SOCCERD_FAKE
#define DIBUTTON_SOCCERD_FAKE 0x1F000403
#endif
#ifndef DIBUTTON_SOCCERD_FORWARD_LINK
#define DIBUTTON_SOCCERD_FORWARD_LINK 0x1F0144E0
#endif
#ifndef DIBUTTON_SOCCERD_FOUL
#define DIBUTTON_SOCCERD_FOUL 0x1F004408
#endif
#ifndef DIBUTTON_SOCCERD_GOALIECHARGE
#define DIBUTTON_SOCCERD_GOALIECHARGE 0x1F00440B
#endif
#ifndef DIBUTTON_SOCCERD_HEAD
#define DIBUTTON_SOCCERD_HEAD 0x1F004409
#endif
#ifndef DIBUTTON_SOCCERD_LEFT_LINK
#define DIBUTTON_SOCCERD_LEFT_LINK 0x1F00C4E4
#endif
#ifndef DIBUTTON_SOCCERD_MENU
#define DIBUTTON_SOCCERD_MENU 0x1F0004FD
#endif
#ifndef DIBUTTON_SOCCERD_PAUSE
#define DIBUTTON_SOCCERD_PAUSE 0x1F0044FC
#endif
#ifndef DIBUTTON_SOCCERD_PLAYER
#define DIBUTTON_SOCCERD_PLAYER 0x1F000404
#endif
#ifndef DIBUTTON_SOCCERD_RIGHT_LINK
#define DIBUTTON_SOCCERD_RIGHT_LINK 0x1F00C4EC
#endif
#ifndef DIBUTTON_SOCCERD_SELECT
#define DIBUTTON_SOCCERD_SELECT 0x1F000406
#endif
#ifndef DIBUTTON_SOCCERD_SLIDE
#define DIBUTTON_SOCCERD_SLIDE 0x1F000407
#endif
#ifndef DIBUTTON_SOCCERD_SPECIAL
#define DIBUTTON_SOCCERD_SPECIAL 0x1F000405
#endif
#ifndef DIBUTTON_SOCCERD_STEAL
#define DIBUTTON_SOCCERD_STEAL 0x1F000402
#endif
#ifndef DIBUTTON_SOCCERD_SUBSTITUTE
#define DIBUTTON_SOCCERD_SUBSTITUTE 0x1F00440C
#endif
#ifndef DIBUTTON_SOCCERO_BACK_LINK
#define DIBUTTON_SOCCERO_BACK_LINK 0x1E0144E8
#endif
#ifndef DIBUTTON_SOCCERO_CONTROL
#define DIBUTTON_SOCCERO_CONTROL 0x1E00440C
#endif
#ifndef DIBUTTON_SOCCERO_DEVICE
#define DIBUTTON_SOCCERO_DEVICE 0x1E0044FE
#endif
#ifndef DIBUTTON_SOCCERO_FAKE
#define DIBUTTON_SOCCERO_FAKE 0x1E000403
#endif
#ifndef DIBUTTON_SOCCERO_FORWARD_LINK
#define DIBUTTON_SOCCERO_FORWARD_LINK 0x1E0144E0
#endif
#ifndef DIBUTTON_SOCCERO_HEAD
#define DIBUTTON_SOCCERO_HEAD 0x1E00440D
#endif
#ifndef DIBUTTON_SOCCERO_LEFT_LINK
#define DIBUTTON_SOCCERO_LEFT_LINK 0x1E00C4E4
#endif
#ifndef DIBUTTON_SOCCERO_MENU
#define DIBUTTON_SOCCERO_MENU 0x1E0004FD
#endif
#ifndef DIBUTTON_SOCCERO_PASS
#define DIBUTTON_SOCCERO_PASS 0x1E000402
#endif
#ifndef DIBUTTON_SOCCERO_PASSTHRU
#define DIBUTTON_SOCCERO_PASSTHRU 0x1E00440A
#endif
#ifndef DIBUTTON_SOCCERO_PAUSE
#define DIBUTTON_SOCCERO_PAUSE 0x1E0044FC
#endif
#ifndef DIBUTTON_SOCCERO_PLAYER
#define DIBUTTON_SOCCERO_PLAYER 0x1E000404
#endif
#ifndef DIBUTTON_SOCCERO_RIGHT_LINK
#define DIBUTTON_SOCCERO_RIGHT_LINK 0x1E00C4EC
#endif
#ifndef DIBUTTON_SOCCERO_SELECT
#define DIBUTTON_SOCCERO_SELECT 0x1E000406
#endif
#ifndef DIBUTTON_SOCCERO_SHOOT
#define DIBUTTON_SOCCERO_SHOOT 0x1E000401
#endif
#ifndef DIBUTTON_SOCCERO_SHOOTHIGH
#define DIBUTTON_SOCCERO_SHOOTHIGH 0x1E004409
#endif
#ifndef DIBUTTON_SOCCERO_SHOOTLOW
#define DIBUTTON_SOCCERO_SHOOTLOW 0x1E004408
#endif
#ifndef DIBUTTON_SOCCERO_SPECIAL1
#define DIBUTTON_SOCCERO_SPECIAL1 0x1E000405
#endif
#ifndef DIBUTTON_SOCCERO_SPRINT
#define DIBUTTON_SOCCERO_SPRINT 0x1E00440B
#endif
#ifndef DIBUTTON_SOCCERO_SUBSTITUTE
#define DIBUTTON_SOCCERO_SUBSTITUTE 0x1E004407
#endif
#ifndef DIBUTTON_STRATEGYR_APPLY
#define DIBUTTON_STRATEGYR_APPLY 0x0B000402
#endif
#ifndef DIBUTTON_STRATEGYR_ATTACK
#define DIBUTTON_STRATEGYR_ATTACK 0x0B000404
#endif
#ifndef DIBUTTON_STRATEGYR_BACK_LINK
#define DIBUTTON_STRATEGYR_BACK_LINK 0x0B0144E8
#endif
#ifndef DIBUTTON_STRATEGYR_CAST
#define DIBUTTON_STRATEGYR_CAST 0x0B000405
#endif
#ifndef DIBUTTON_STRATEGYR_CROUCH
#define DIBUTTON_STRATEGYR_CROUCH 0x0B000406
#endif
#ifndef DIBUTTON_STRATEGYR_DEVICE
#define DIBUTTON_STRATEGYR_DEVICE 0x0B0044FE
#endif
#ifndef DIBUTTON_STRATEGYR_DISPLAY
#define DIBUTTON_STRATEGYR_DISPLAY 0x0B004409
#endif
#ifndef DIBUTTON_STRATEGYR_FORWARD_LINK
#define DIBUTTON_STRATEGYR_FORWARD_LINK 0x0B0144E0
#endif
#ifndef DIBUTTON_STRATEGYR_GET
#define DIBUTTON_STRATEGYR_GET 0x0B000401
#endif
#ifndef DIBUTTON_STRATEGYR_JUMP
#define DIBUTTON_STRATEGYR_JUMP 0x0B000407
#endif
#ifndef DIBUTTON_STRATEGYR_LEFT_LINK
#define DIBUTTON_STRATEGYR_LEFT_LINK 0x0B00C4E4
#endif
#ifndef DIBUTTON_STRATEGYR_MAP
#define DIBUTTON_STRATEGYR_MAP 0x0B004408
#endif
#ifndef DIBUTTON_STRATEGYR_MENU
#define DIBUTTON_STRATEGYR_MENU 0x0B0004FD
#endif
#ifndef DIBUTTON_STRATEGYR_PAUSE
#define DIBUTTON_STRATEGYR_PAUSE 0x0B0044FC
#endif
#ifndef DIBUTTON_STRATEGYR_RIGHT_LINK
#define DIBUTTON_STRATEGYR_RIGHT_LINK 0x0B00C4EC
#endif
#ifndef DIBUTTON_STRATEGYR_ROTATE_LEFT_LINK
#define DIBUTTON_STRATEGYR_ROTATE_LEFT_LINK 0x0B0244E4
#endif
#ifndef DIBUTTON_STRATEGYR_ROTATE_RIGHT_LINK
#define DIBUTTON_STRATEGYR_ROTATE_RIGHT_LINK 0x0B0244EC
#endif
#ifndef DIBUTTON_STRATEGYR_SELECT
#define DIBUTTON_STRATEGYR_SELECT 0x0B000403
#endif
#ifndef DIBUTTON_STRATEGYT_APPLY
#define DIBUTTON_STRATEGYT_APPLY 0x0C000403
#endif
#ifndef DIBUTTON_STRATEGYT_BACK_LINK
#define DIBUTTON_STRATEGYT_BACK_LINK 0x0C0144E8
#endif
#ifndef DIBUTTON_STRATEGYT_DEVICE
#define DIBUTTON_STRATEGYT_DEVICE 0x0C0044FE
#endif
#ifndef DIBUTTON_STRATEGYT_DISPLAY
#define DIBUTTON_STRATEGYT_DISPLAY 0x0C004408
#endif
#ifndef DIBUTTON_STRATEGYT_FORWARD_LINK
#define DIBUTTON_STRATEGYT_FORWARD_LINK 0x0C0144E0
#endif
#ifndef DIBUTTON_STRATEGYT_INSTRUCT
#define DIBUTTON_STRATEGYT_INSTRUCT 0x0C000402
#endif
#ifndef DIBUTTON_STRATEGYT_LEFT_LINK
#define DIBUTTON_STRATEGYT_LEFT_LINK 0x0C00C4E4
#endif
#ifndef DIBUTTON_STRATEGYT_MAP
#define DIBUTTON_STRATEGYT_MAP 0x0C004407
#endif
#ifndef DIBUTTON_STRATEGYT_MENU
#define DIBUTTON_STRATEGYT_MENU 0x0C0004FD
#endif
#ifndef DIBUTTON_STRATEGYT_PAUSE
#define DIBUTTON_STRATEGYT_PAUSE 0x0C0044FC
#endif
#ifndef DIBUTTON_STRATEGYT_RIGHT_LINK
#define DIBUTTON_STRATEGYT_RIGHT_LINK 0x0C00C4EC
#endif
#ifndef DIBUTTON_STRATEGYT_SELECT
#define DIBUTTON_STRATEGYT_SELECT 0x0C000401
#endif
#ifndef DIBUTTON_STRATEGYT_TEAM
#define DIBUTTON_STRATEGYT_TEAM 0x0C000404
#endif
#ifndef DIBUTTON_STRATEGYT_TURN
#define DIBUTTON_STRATEGYT_TURN 0x0C000405
#endif
#ifndef DIBUTTON_STRATEGYT_ZOOM
#define DIBUTTON_STRATEGYT_ZOOM 0x0C004406
#endif
#ifndef DIBUTTON_TPS_ACTION
#define DIBUTTON_TPS_ACTION 0x0A000402
#endif
#ifndef DIBUTTON_TPS_BACKWARD_LINK
#define DIBUTTON_TPS_BACKWARD_LINK 0x0A0144E8
#endif
#ifndef DIBUTTON_TPS_DEVICE
#define DIBUTTON_TPS_DEVICE 0x0A0044FE
#endif
#ifndef DIBUTTON_TPS_DODGE
#define DIBUTTON_TPS_DODGE 0x0A004409
#endif
#ifndef DIBUTTON_TPS_FORWARD_LINK
#define DIBUTTON_TPS_FORWARD_LINK 0x0A0144E0
#endif
#ifndef DIBUTTON_TPS_GLANCE_DOWN_LINK
#define DIBUTTON_TPS_GLANCE_DOWN_LINK 0x0A07C4E8
#endif
#ifndef DIBUTTON_TPS_GLANCE_LEFT_LINK
#define DIBUTTON_TPS_GLANCE_LEFT_LINK 0x0A07C4E4
#endif
#ifndef DIBUTTON_TPS_GLANCE_RIGHT_LINK
#define DIBUTTON_TPS_GLANCE_RIGHT_LINK 0x0A07C4EC
#endif
#ifndef DIBUTTON_TPS_GLANCE_UP_LINK
#define DIBUTTON_TPS_GLANCE_UP_LINK 0x0A07C4E0
#endif
#ifndef DIBUTTON_TPS_INVENTORY
#define DIBUTTON_TPS_INVENTORY 0x0A00440A
#endif
#ifndef DIBUTTON_TPS_JUMP
#define DIBUTTON_TPS_JUMP 0x0A000405
#endif
#ifndef DIBUTTON_TPS_MENU
#define DIBUTTON_TPS_MENU 0x0A0004FD
#endif
#ifndef DIBUTTON_TPS_PAUSE
#define DIBUTTON_TPS_PAUSE 0x0A0044FC
#endif
#ifndef DIBUTTON_TPS_RUN
#define DIBUTTON_TPS_RUN 0x0A000401
#endif
#ifndef DIBUTTON_TPS_SELECT
#define DIBUTTON_TPS_SELECT 0x0A000403
#endif
#ifndef DIBUTTON_TPS_STEPLEFT
#define DIBUTTON_TPS_STEPLEFT 0x0A004407
#endif
#ifndef DIBUTTON_TPS_STEPRIGHT
#define DIBUTTON_TPS_STEPRIGHT 0x0A004408
#endif
#ifndef DIBUTTON_TPS_TURN_LEFT_LINK
#define DIBUTTON_TPS_TURN_LEFT_LINK 0x0A0244E4
#endif
#ifndef DIBUTTON_TPS_TURN_RIGHT_LINK
#define DIBUTTON_TPS_TURN_RIGHT_LINK 0x0A0244EC
#endif
#ifndef DIBUTTON_TPS_USE
#define DIBUTTON_TPS_USE 0x0A000404
#endif
#ifndef DIBUTTON_TPS_VIEW
#define DIBUTTON_TPS_VIEW 0x0A004406
#endif
#ifndef DIFEF_INCLUDENONSTANDARD
#define DIFEF_INCLUDENONSTANDARD 0x00000001
#endif
#ifndef DIFEF_MODIFYIFNEEDED
#define DIFEF_MODIFYIFNEEDED 0x00000010
#endif
#ifndef DIHATSWITCH_2DCONTROL_HATSWITCH
#define DIHATSWITCH_2DCONTROL_HATSWITCH 0x23004601
#endif
#ifndef DIHATSWITCH_3DCONTROL_HATSWITCH
#define DIHATSWITCH_3DCONTROL_HATSWITCH 0x24004601
#endif
#ifndef DIHATSWITCH_ARCADEP_VIEW
#define DIHATSWITCH_ARCADEP_VIEW 0x22004601
#endif
#ifndef DIHATSWITCH_ARCADES_VIEW
#define DIHATSWITCH_ARCADES_VIEW 0x21004601
#endif
#ifndef DIHATSWITCH_BBALLD_GLANCE
#define DIHATSWITCH_BBALLD_GLANCE 0x13004601
#endif
#ifndef DIHATSWITCH_BBALLO_GLANCE
#define DIHATSWITCH_BBALLO_GLANCE 0x12004601
#endif
#ifndef DIHATSWITCH_BIKINGM_SCROLL
#define DIHATSWITCH_BIKINGM_SCROLL 0x1C004601
#endif
#ifndef DIHATSWITCH_CADF_HATSWITCH
#define DIHATSWITCH_CADF_HATSWITCH 0x25004601
#endif
#ifndef DIHATSWITCH_CADM_HATSWITCH
#define DIHATSWITCH_CADM_HATSWITCH 0x26004601
#endif
#ifndef DIHATSWITCH_DRIVINGC_GLANCE
#define DIHATSWITCH_DRIVINGC_GLANCE 0x02004601
#endif
#ifndef DIHATSWITCH_DRIVINGR_GLANCE
#define DIHATSWITCH_DRIVINGR_GLANCE 0x01004601
#endif
#ifndef DIHATSWITCH_DRIVINGT_GLANCE
#define DIHATSWITCH_DRIVINGT_GLANCE 0x03004601
#endif
#ifndef DIHATSWITCH_FIGHTINGH_SLIDE
#define DIHATSWITCH_FIGHTINGH_SLIDE 0x08004601
#endif
#ifndef DIHATSWITCH_FISHING_GLANCE
#define DIHATSWITCH_FISHING_GLANCE 0x0E004601
#endif
#ifndef DIHATSWITCH_FLYINGC_GLANCE
#define DIHATSWITCH_FLYINGC_GLANCE 0x04004601
#endif
#ifndef DIHATSWITCH_FLYINGM_GLANCE
#define DIHATSWITCH_FLYINGM_GLANCE 0x05004601
#endif
#ifndef DIHATSWITCH_FPS_GLANCE
#define DIHATSWITCH_FPS_GLANCE 0x09004601
#endif
#ifndef DIHATSWITCH_GOLF_SCROLL
#define DIHATSWITCH_GOLF_SCROLL 0x18004601
#endif
#ifndef DIHATSWITCH_HOCKEYD_SCROLL
#define DIHATSWITCH_HOCKEYD_SCROLL 0x1A004601
#endif
#ifndef DIHATSWITCH_HOCKEYG_SCROLL
#define DIHATSWITCH_HOCKEYG_SCROLL 0x1B004601
#endif
#ifndef DIHATSWITCH_HOCKEYO_SCROLL
#define DIHATSWITCH_HOCKEYO_SCROLL 0x19004601
#endif
#ifndef DIHATSWITCH_HUNTING_GLANCE
#define DIHATSWITCH_HUNTING_GLANCE 0x0D004601
#endif
#ifndef DIHATSWITCH_MECHA_GLANCE
#define DIHATSWITCH_MECHA_GLANCE 0x29004601
#endif
#ifndef DIHATSWITCH_RACQUET_GLANCE
#define DIHATSWITCH_RACQUET_GLANCE 0x20004601
#endif
#ifndef DIHATSWITCH_SKIING_GLANCE
#define DIHATSWITCH_SKIING_GLANCE 0x1D004601
#endif
#ifndef DIHATSWITCH_SOCCERD_GLANCE
#define DIHATSWITCH_SOCCERD_GLANCE 0x1F004601
#endif
#ifndef DIHATSWITCH_SOCCERO_GLANCE
#define DIHATSWITCH_SOCCERO_GLANCE 0x1E004601
#endif
#ifndef DIHATSWITCH_STRATEGYR_GLANCE
#define DIHATSWITCH_STRATEGYR_GLANCE 0x0B004601
#endif
#ifndef DIHATSWITCH_TPS_GLANCE
#define DIHATSWITCH_TPS_GLANCE 0x0A004601
#endif
#ifndef DIKEYBOARD_ABNT_C1
#define DIKEYBOARD_ABNT_C1 0x81000473
#endif
#ifndef DIKEYBOARD_ABNT_C2
#define DIKEYBOARD_ABNT_C2 0x8100047E
#endif
#ifndef DIKEYBOARD_CALCULATOR
#define DIKEYBOARD_CALCULATOR 0x810004A1
#endif
#ifndef DIKEYBOARD_MAIL
#define DIKEYBOARD_MAIL 0x810004EC
#endif
#ifndef DIKEYBOARD_MEDIASELECT
#define DIKEYBOARD_MEDIASELECT 0x810004ED
#endif
#ifndef DIKEYBOARD_MEDIASTOP
#define DIKEYBOARD_MEDIASTOP 0x810004A4
#endif
#ifndef DIKEYBOARD_MUTE
#define DIKEYBOARD_MUTE 0x810004A0
#endif
#ifndef DIKEYBOARD_MYCOMPUTER
#define DIKEYBOARD_MYCOMPUTER 0x810004EB
#endif
#ifndef DIKEYBOARD_NEXTTRACK
#define DIKEYBOARD_NEXTTRACK 0x81000499
#endif
#ifndef DIKEYBOARD_OEM_102
#define DIKEYBOARD_OEM_102 0x81000456
#endif
#ifndef DIKEYBOARD_PLAYPAUSE
#define DIKEYBOARD_PLAYPAUSE 0x810004A2
#endif
#ifndef DIKEYBOARD_PREVTRACK
#define DIKEYBOARD_PREVTRACK 0x81000490
#endif
#ifndef DIKEYBOARD_VOLUMEDOWN
#define DIKEYBOARD_VOLUMEDOWN 0x810004AE
#endif
#ifndef DIKEYBOARD_VOLUMEUP
#define DIKEYBOARD_VOLUMEUP 0x810004B0
#endif
#ifndef DIKEYBOARD_WAKE
#define DIKEYBOARD_WAKE 0x810004E3
#endif
#ifndef DIKEYBOARD_WEBBACK
#define DIKEYBOARD_WEBBACK 0x810004EA
#endif
#ifndef DIKEYBOARD_WEBFAVORITES
#define DIKEYBOARD_WEBFAVORITES 0x810004E6
#endif
#ifndef DIKEYBOARD_WEBFORWARD
#define DIKEYBOARD_WEBFORWARD 0x810004E9
#endif
#ifndef DIKEYBOARD_WEBHOME
#define DIKEYBOARD_WEBHOME 0x810004B2
#endif
#ifndef DIKEYBOARD_WEBREFRESH
#define DIKEYBOARD_WEBREFRESH 0x810004E7
#endif
#ifndef DIKEYBOARD_WEBSEARCH
#define DIKEYBOARD_WEBSEARCH 0x810004E5
#endif
#ifndef DIKEYBOARD_WEBSTOP
#define DIKEYBOARD_WEBSTOP 0x810004E8
#endif
#ifndef DIK_ABNT_C1
#define DIK_ABNT_C1 0x73
#endif
#ifndef DIK_ABNT_C2
#define DIK_ABNT_C2 0x7E
#endif
#ifndef DIK_CALCULATOR
#define DIK_CALCULATOR 0xA1
#endif
#ifndef DIK_MAIL
#define DIK_MAIL 0xEC
#endif
#ifndef DIK_MEDIASELECT
#define DIK_MEDIASELECT 0xED
#endif
#ifndef DIK_MEDIASTOP
#define DIK_MEDIASTOP 0xA4
#endif
#ifndef DIK_MUTE
#define DIK_MUTE 0xA0
#endif
#ifndef DIK_MYCOMPUTER
#define DIK_MYCOMPUTER 0xEB
#endif
#ifndef DIK_NEXTTRACK
#define DIK_NEXTTRACK 0x99
#endif
#ifndef DIK_OEM_102
#define DIK_OEM_102 0x56
#endif
#ifndef DIK_PLAYPAUSE
#define DIK_PLAYPAUSE 0xA2
#endif
#ifndef DIK_PREVTRACK
#define DIK_PREVTRACK 0x90
#endif
#ifndef DIK_VOLUMEDOWN
#define DIK_VOLUMEDOWN 0xAE
#endif
#ifndef DIK_VOLUMEUP
#define DIK_VOLUMEUP 0xB0
#endif
#ifndef DIK_WAKE
#define DIK_WAKE 0xE3
#endif
#ifndef DIK_WEBBACK
#define DIK_WEBBACK 0xEA
#endif
#ifndef DIK_WEBFAVORITES
#define DIK_WEBFAVORITES 0xE6
#endif
#ifndef DIK_WEBFORWARD
#define DIK_WEBFORWARD 0xE9
#endif
#ifndef DIK_WEBHOME
#define DIK_WEBHOME 0xB2
#endif
#ifndef DIK_WEBREFRESH
#define DIK_WEBREFRESH 0xE7
#endif
#ifndef DIK_WEBSEARCH
#define DIK_WEBSEARCH 0xE5
#endif
#ifndef DIK_WEBSTOP
#define DIK_WEBSTOP 0xE8
#endif
#ifndef DIRECTINPUT_HEADER_VERSION
#define DIRECTINPUT_HEADER_VERSION 0x0800
#endif
#ifndef DISDD_CONTINUE
#define DISDD_CONTINUE 0x00000001
#endif
#ifndef DIVIRTUAL_ARCADE_PLATFORM
#define DIVIRTUAL_ARCADE_PLATFORM 0x22000000
#endif
#ifndef DIVIRTUAL_ARCADE_SIDE2SIDE
#define DIVIRTUAL_ARCADE_SIDE2SIDE 0x21000000
#endif
#ifndef DIVIRTUAL_BROWSER_CONTROL
#define DIVIRTUAL_BROWSER_CONTROL 0x28000000
#endif
#ifndef DIVIRTUAL_CAD_2DCONTROL
#define DIVIRTUAL_CAD_2DCONTROL 0x23000000
#endif
#ifndef DIVIRTUAL_CAD_3DCONTROL
#define DIVIRTUAL_CAD_3DCONTROL 0x24000000
#endif
#ifndef DIVIRTUAL_CAD_FLYBY
#define DIVIRTUAL_CAD_FLYBY 0x25000000
#endif
#ifndef DIVIRTUAL_CAD_MODEL
#define DIVIRTUAL_CAD_MODEL 0x26000000
#endif
#ifndef DIVIRTUAL_DRIVING_COMBAT
#define DIVIRTUAL_DRIVING_COMBAT 0x02000000
#endif
#ifndef DIVIRTUAL_DRIVING_MECHA
#define DIVIRTUAL_DRIVING_MECHA 0x29000000
#endif
#ifndef DIVIRTUAL_DRIVING_TANK
#define DIVIRTUAL_DRIVING_TANK 0x03000000
#endif
#ifndef DIVIRTUAL_FIGHTING_FPS
#define DIVIRTUAL_FIGHTING_FPS 0x09000000
#endif
#ifndef DIVIRTUAL_FIGHTING_HAND2HAND
#define DIVIRTUAL_FIGHTING_HAND2HAND 0x08000000
#endif
#ifndef DIVIRTUAL_FIGHTING_THIRDPERSON
#define DIVIRTUAL_FIGHTING_THIRDPERSON 0x0A000000
#endif
#ifndef DIVIRTUAL_FLYING_CIVILIAN
#define DIVIRTUAL_FLYING_CIVILIAN 0x04000000
#endif
#ifndef DIVIRTUAL_FLYING_MILITARY
#define DIVIRTUAL_FLYING_MILITARY 0x05000000
#endif
#ifndef DIVIRTUAL_REMOTE_CONTROL
#define DIVIRTUAL_REMOTE_CONTROL 0x27000000
#endif
#ifndef DIVIRTUAL_SPORTS_BASEBALL_BAT
#define DIVIRTUAL_SPORTS_BASEBALL_BAT 0x0F000000
#endif
#ifndef DIVIRTUAL_SPORTS_BASEBALL_FIELD
#define DIVIRTUAL_SPORTS_BASEBALL_FIELD 0x11000000
#endif
#ifndef DIVIRTUAL_SPORTS_BASEBALL_PITCH
#define DIVIRTUAL_SPORTS_BASEBALL_PITCH 0x10000000
#endif
#ifndef DIVIRTUAL_SPORTS_BASKETBALL_DEFENSE
#define DIVIRTUAL_SPORTS_BASKETBALL_DEFENSE 0x13000000
#endif
#ifndef DIVIRTUAL_SPORTS_BASKETBALL_OFFENSE
#define DIVIRTUAL_SPORTS_BASKETBALL_OFFENSE 0x12000000
#endif
#ifndef DIVIRTUAL_SPORTS_BIKING_MOUNTAIN
#define DIVIRTUAL_SPORTS_BIKING_MOUNTAIN 0x1C000000
#endif
#ifndef DIVIRTUAL_SPORTS_FISHING
#define DIVIRTUAL_SPORTS_FISHING 0x0E000000
#endif
#ifndef DIVIRTUAL_SPORTS_FOOTBALL_DEFENSE
#define DIVIRTUAL_SPORTS_FOOTBALL_DEFENSE 0x17000000
#endif
#ifndef DIVIRTUAL_SPORTS_FOOTBALL_FIELD
#define DIVIRTUAL_SPORTS_FOOTBALL_FIELD 0x14000000
#endif
#ifndef DIVIRTUAL_SPORTS_FOOTBALL_OFFENSE
#define DIVIRTUAL_SPORTS_FOOTBALL_OFFENSE 0x16000000
#endif
#ifndef DIVIRTUAL_SPORTS_FOOTBALL_QBCK
#define DIVIRTUAL_SPORTS_FOOTBALL_QBCK 0x15000000
#endif
#ifndef DIVIRTUAL_SPORTS_GOLF
#define DIVIRTUAL_SPORTS_GOLF 0x18000000
#endif
#ifndef DIVIRTUAL_SPORTS_HOCKEY_DEFENSE
#define DIVIRTUAL_SPORTS_HOCKEY_DEFENSE 0x1A000000
#endif
#ifndef DIVIRTUAL_SPORTS_HOCKEY_GOALIE
#define DIVIRTUAL_SPORTS_HOCKEY_GOALIE 0x1B000000
#endif
#ifndef DIVIRTUAL_SPORTS_HOCKEY_OFFENSE
#define DIVIRTUAL_SPORTS_HOCKEY_OFFENSE 0x19000000
#endif
#ifndef DIVIRTUAL_SPORTS_HUNTING
#define DIVIRTUAL_SPORTS_HUNTING 0x0D000000
#endif
#ifndef DIVIRTUAL_SPORTS_RACQUET
#define DIVIRTUAL_SPORTS_RACQUET 0x20000000
#endif
#ifndef DIVIRTUAL_SPORTS_SKIING
#define DIVIRTUAL_SPORTS_SKIING 0x1D000000
#endif
#ifndef DIVIRTUAL_SPORTS_SOCCER_DEFENSE
#define DIVIRTUAL_SPORTS_SOCCER_DEFENSE 0x1F000000
#endif
#ifndef DIVIRTUAL_SPORTS_SOCCER_OFFENSE
#define DIVIRTUAL_SPORTS_SOCCER_OFFENSE 0x1E000000
#endif
#ifndef DIVIRTUAL_STRATEGY_ROLEPLAYING
#define DIVIRTUAL_STRATEGY_ROLEPLAYING 0x0B000000
#endif
#ifndef DIVIRTUAL_STRATEGY_TURN
#define DIVIRTUAL_STRATEGY_TURN 0x0C000000
#endif
#ifndef DIVOICE_ALL
#define DIVOICE_ALL 0x8300040A
#endif
#ifndef DIVOICE_CHANNEL2
#define DIVOICE_CHANNEL2 0x83000402
#endif
#ifndef DIVOICE_CHANNEL3
#define DIVOICE_CHANNEL3 0x83000403
#endif
#ifndef DIVOICE_CHANNEL4
#define DIVOICE_CHANNEL4 0x83000404
#endif
#ifndef DIVOICE_CHANNEL5
#define DIVOICE_CHANNEL5 0x83000405
#endif
#ifndef DIVOICE_CHANNEL6
#define DIVOICE_CHANNEL6 0x83000406
#endif
#ifndef DIVOICE_CHANNEL7
#define DIVOICE_CHANNEL7 0x83000407
#endif
#ifndef DIVOICE_CHANNEL8
#define DIVOICE_CHANNEL8 0x83000408
#endif
#ifndef DIVOICE_PLAYBACKMUTE
#define DIVOICE_PLAYBACKMUTE 0x8300040C
#endif
#ifndef DIVOICE_RECORDMUTE
#define DIVOICE_RECORDMUTE 0x8300040B
#endif
#ifndef DIVOICE_TEAM
#define DIVOICE_TEAM 0x83000409
#endif
#ifndef DIVOICE_TRANSMIT
#define DIVOICE_TRANSMIT 0x8300040D
#endif
#ifndef DIVOICE_VOICECOMMAND
#define DIVOICE_VOICECOMMAND 0x83000410
#endif
#ifndef JOY_HWS_AUTOLOAD
#define JOY_HWS_AUTOLOAD 0x10000000l
#endif
#ifndef JOY_HWS_GAMEPORTBUSBUSY
#define JOY_HWS_GAMEPORTBUSBUSY 0x00000001l
#endif
#ifndef JOY_HWS_ISANALOGPORTDRIVER
#define JOY_HWS_ISANALOGPORTDRIVER 0x08000000l
#endif
#ifndef JOY_HWS_ISGAMEPORTBUS
#define JOY_HWS_ISGAMEPORTBUS 0x80000000l
#endif
#ifndef JOY_HWS_ISGAMEPORTDRIVER
#define JOY_HWS_ISGAMEPORTDRIVER 0x04000000l
#endif
#ifndef JOY_HWS_ISHEADTRACKER
#define JOY_HWS_ISHEADTRACKER 0x02000000l
#endif
#ifndef JOY_HWS_NODEVNODE
#define JOY_HWS_NODEVNODE 0x20000000l
#endif
#ifndef JOY_PASSDRIVERDATA
#define JOY_PASSDRIVERDATA 0x10000000l
#endif
#ifndef JOY_US_VOLATILE
#define JOY_US_VOLATILE 0x00000008L
#endif
#ifndef MAXCPOINTSNUM
#define MAXCPOINTSNUM 8
#endif

typedef const DIDEVICEOBJECTDATA_DX3 * LPCDIDEVICEOBJECTDATA_DX;

typedef struct _CPOINT {
  LONG lP;
  DWORD dwLog;
} CPOINT, *PCPOINT;

typedef struct DIPROPCALPOV {
  DIPROPHEADER diph;
  LONG lMin[5];
  LONG lMax[5];
} DIPROPCALPOV, *LPDIPROPCALPOV;

typedef struct DIPROPCPOINTS {
  DIPROPHEADER diph;
  DWORD dwCPointsNum;
  CPOINT cp[8];
} DIPROPCPOINTS, *LPDIPROPCPOINTS;

#define DIFEF_DEFAULT 0x00000000
#define DIPROP_GETPORTDISPLAYNAME 0x10
#ifdef UNICODE
#define IDirectInput IDirectInputW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define IDirectInput IDirectInputW
#else
#define IDirectInput IDirectInputA
#endif
#endif
#endif
#ifdef UNICODE
#define IDirectInput2 IDirectInput2W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define IDirectInput2 IDirectInput2W
#else
#define IDirectInput2 IDirectInput2A
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#define IDirectInput2Vtbl IDirectInput2AVtbl
#endif
#ifdef UNICODE
#define IDirectInput7 IDirectInput7W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define IDirectInput7 IDirectInput7W
#else
#define IDirectInput7 IDirectInput7A
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#define IDirectInput7Vtbl IDirectInput7AVtbl
#endif
#ifdef UNICODE
#define IDirectInput8 IDirectInput8W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define IDirectInput8 IDirectInput8W
#else
#define IDirectInput8 IDirectInput8A
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#define IDirectInput8Vtbl IDirectInput8AVtbl
#endif
#ifdef UNICODE
#define IDirectInputDevice IDirectInputDeviceW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define IDirectInputDevice IDirectInputDeviceW
#else
#define IDirectInputDevice IDirectInputDeviceA
#endif
#endif
#endif
#ifdef UNICODE
#define IDirectInputDevice2 IDirectInputDevice2W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define IDirectInputDevice2 IDirectInputDevice2W
#else
#define IDirectInputDevice2 IDirectInputDevice2A
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#define IDirectInputDevice2Vtbl IDirectInputDevice2AVtbl
#endif
#ifdef UNICODE
#define IDirectInputDevice7 IDirectInputDevice7W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define IDirectInputDevice7 IDirectInputDevice7W
#else
#define IDirectInputDevice7 IDirectInputDevice7A
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#define IDirectInputDevice7Vtbl IDirectInputDevice7AVtbl
#endif
#ifdef UNICODE
#define IDirectInputDevice8 IDirectInputDevice8W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define IDirectInputDevice8 IDirectInputDevice8W
#else
#define IDirectInputDevice8 IDirectInputDevice8A
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#define IDirectInputDevice8Vtbl IDirectInputDevice8AVtbl
#define IDirectInputDeviceVtbl IDirectInputDeviceAVtbl
#define IDirectInputVtbl IDirectInputAVtbl
#ifdef UNICODE
#define LPDIENUMEFFECTSCALLBACK LPDIENUMEFFECTSCALLBACKW
#else
#define LPDIENUMEFFECTSCALLBACK LPDIENUMEFFECTSCALLBACKA
#endif
#endif

struct _DIMOUSESTATE
{
    LONG lX;
    LONG lY;
    LONG lZ;
    BYTE rgbButtons[4];
};

struct _DIMOUSESTATE2
{
    LONG lX;
    LONG lY;
    LONG lZ;
    BYTE rgbButtons[8];
};

typedef struct IDirectInput2AVtbl IDirectInput2AVtbl;

typedef struct IDirectInput2WVtbl IDirectInput2WVtbl;

typedef struct IDirectInput7AVtbl IDirectInput7AVtbl;

typedef struct IDirectInput7WVtbl IDirectInput7WVtbl;

typedef struct IDirectInput8AVtbl IDirectInput8AVtbl;

typedef struct IDirectInput8WVtbl IDirectInput8WVtbl;

typedef struct IDirectInputAVtbl IDirectInputAVtbl;

typedef struct IDirectInputDevice2AVtbl IDirectInputDevice2AVtbl;

typedef struct IDirectInputDevice2WVtbl IDirectInputDevice2WVtbl;

typedef struct IDirectInputDevice7AVtbl IDirectInputDevice7AVtbl;

typedef struct IDirectInputDevice7WVtbl IDirectInputDevice7WVtbl;

typedef struct IDirectInputDevice8AVtbl IDirectInputDevice8AVtbl;

typedef struct IDirectInputDevice8WVtbl IDirectInputDevice8WVtbl;

typedef struct IDirectInputDeviceAVtbl IDirectInputDeviceAVtbl;

typedef struct IDirectInputDeviceWVtbl IDirectInputDeviceWVtbl;

typedef struct IDirectInputEffectVtbl IDirectInputEffectVtbl;

typedef struct IDirectInputWVtbl IDirectInputWVtbl;

typedef const DIPROPCALPOV *LPCDIPROPCALPOV;

typedef const DIPROPCPOINTS *LPCDIPROPCPOINTS;

typedef struct _DIMOUSESTATE *LPDIMOUSESTATE;

typedef struct _DIMOUSESTATE2 *LPDIMOUSESTATE2;

typedef void (WINAPI *LPFNSHOWJOYCPL)(HWND);

#ifdef __cplusplus
extern "C" {
#endif

MMRESULT WINAPI joyConfigChanged(DWORD dwFlags);

/* TODO: LiberNT does not implement the functions below yet */
LPCDIDATAFORMAT WINAPI GetdfDIJoystick(void);
void WINAPI ShowJoyCPL(HWND hWnd);

#ifdef __cplusplus
}
#endif

#endif /* __DINPUT_INCLUDED__ */
