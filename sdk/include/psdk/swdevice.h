/*
 * PROJECT:     LiberNT PSDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Software device API
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#pragma once

#include <devpropdef.h>
#include <swdevicedef.h>

#ifdef __cplusplus
extern "C" {
#endif

DECLARE_HANDLE(HSWDEVICE);
typedef HSWDEVICE *PHSWDEVICE;

typedef VOID (WINAPI *SW_DEVICE_CREATE_CALLBACK)(
    _In_ HSWDEVICE hSwDevice,
    _In_ HRESULT CreateResult,
    _In_opt_ PVOID pContext,
    _In_opt_ PCWSTR pszDeviceInstanceId);

HRESULT WINAPI
SwDeviceCreate(
    _In_ PCWSTR pszEnumeratorName,
    _In_ PCWSTR pszParentDeviceInstance,
    _In_ const SW_DEVICE_CREATE_INFO *pCreateInfo,
    _In_ ULONG cPropertyCount,
    _In_reads_opt_(cPropertyCount) const DEVPROPERTY *pProperties,
    _In_ SW_DEVICE_CREATE_CALLBACK pCallback,
    _In_opt_ PVOID pContext,
    _Out_ PHSWDEVICE phSwDevice);

VOID WINAPI
SwDeviceClose(
    _In_ HSWDEVICE hSwDevice);

HRESULT WINAPI
SwDeviceSetLifetime(
    _In_ HSWDEVICE hSwDevice,
    _In_ SW_DEVICE_LIFETIME Lifetime);

HRESULT WINAPI
SwDeviceGetLifetime(
    _In_ HSWDEVICE hSwDevice,
    _Out_ PSW_DEVICE_LIFETIME pLifetime);

HRESULT WINAPI
SwDevicePropertySet(
    _In_ HSWDEVICE hSwDevice,
    _In_ ULONG cPropertyCount,
    _In_reads_(cPropertyCount) const DEVPROPERTY *pProperties);

HRESULT WINAPI
SwDeviceInterfaceRegister(
    _In_ HSWDEVICE hSwDevice,
    _In_ const GUID *pInterfaceClassGuid,
    _In_opt_ PCWSTR pszReferenceString,
    _In_ ULONG cPropertyCount,
    _In_reads_opt_(cPropertyCount) const DEVPROPERTY *pProperties,
    _In_ BOOL fEnabled,
    _Out_opt_ PWSTR *ppszDeviceInterfaceId);

VOID WINAPI
SwMemFree(
    _In_ PVOID pMem);

HRESULT WINAPI
SwDeviceInterfaceSetState(
    _In_ HSWDEVICE hSwDevice,
    _In_ PCWSTR pszDeviceInterfaceId,
    _In_ BOOL fEnabled);

HRESULT WINAPI
SwDeviceInterfacePropertySet(
    _In_ HSWDEVICE hSwDevice,
    _In_ PCWSTR pszDeviceInterfaceId,
    _In_ ULONG cPropertyCount,
    _In_reads_(cPropertyCount) const DEVPROPERTY *pProperties);

#ifdef __cplusplus
}
#endif
