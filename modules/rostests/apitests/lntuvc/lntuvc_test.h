/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared helpers for the USB video class tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>

#define UVC_TEST_FRAMES 8
#ifndef KSPROPERTY_TYPE_DEFAULTVALUES
#define KSPROPERTY_TYPE_DEFAULTVALUES 0x00010000
#endif
#define UVC_FOURCC(a, b, c, d) ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))

extern const GUID UvcCategoryCapture;
extern const GUID UvcCategoryVideo;
extern const GUID UvcCategoryCamera;
extern const GUID UvcPropsetPin;
extern const GUID UvcPropsetTopology;
extern const GUID UvcPropsetConnection;
extern const GUID UvcPropsetGeneral;
extern const GUID UvcPropsetProcAmp;
extern const GUID UvcPropsetCamera;
extern const GUID UvcPropsetStream;
extern const GUID UvcExtensionUnit;
extern const GUID UvcFormatVideo;
extern const GUID UvcSpecifierVideoInfo;
extern const GUID UvcSpecifierVideoInfo2;
extern const GUID UvcPinCapture;
extern const GUID UvcPinBridge;
extern const GUID UvcNodeCamera;
extern const GUID UvcNodeProcessing;
extern const GUID UvcNodeExtension;
extern const GUID UvcNodeStreaming;
extern const GUID UvcComponentCamera;
extern const GUID UvcMemoryNonPaged;
extern const GUID UvcSubtypeYuy2;
extern const GUID UvcSubtypeMjpg;
extern const GUID UvcInterfaceStandard;
extern const GUID UvcMediumStandard;

BOOL UvcFindCamera(const GUID *Category, WCHAR *Path, DWORD PathChars);
BOOL UvcFindCameraId(const GUID *Category, const WCHAR *Id, WCHAR *Path, DWORD PathChars);
HANDLE UvcOpenFilter(const WCHAR *Path);
extern DWORD UvcIoctlTimeout;
BOOL UvcIoctl(HANDLE Handle, DWORD Code, PVOID In, DWORD InLength, PVOID Out, DWORD OutLength, DWORD *Returned);
DWORD UvcGetProperty(HANDLE Handle, const GUID *Set, ULONG Id, PVOID Data, DWORD Length, DWORD *Returned);
DWORD UvcSetProperty(HANDLE Handle, const GUID *Set, ULONG Id, PVOID Data, DWORD Length);
DWORD UvcGetPinProperty(HANDLE Filter, ULONG PinId, ULONG Id, PVOID Data, DWORD Length, DWORD *Returned);
DWORD UvcSetPinState(HANDLE Pin, KSSTATE State);
void UvcBuildFormat(KS_DATAFORMAT_VIDEOINFOHEADER *Format, const GUID *Subtype, DWORD FourCc, LONG Width, LONG Height, LONGLONG Interval);
DWORD UvcCreatePin(HANDLE Filter, ULONG PinId, const KS_DATAFORMAT_VIDEOINFOHEADER *Format, HANDLE *Pin);
ULONG UvcCheckYuy2Frame(const BYTE *Data, LONG Width, LONG Height, ULONG *Sequence);
