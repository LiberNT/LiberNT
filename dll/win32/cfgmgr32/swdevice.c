/*
 * PROJECT:     LiberNT Configuration Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Software device API
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "rpc_private.h"
#include <pnpsw_c.h>
#include <swdevice.h>

WINE_DEFAULT_DEBUG_CHANNEL(cfgmgr);

#define SW_DEVICE_MAGIC 0x44575353
#define SW_DEVICE_CAPABILITIES_ALL (SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall | \
                                    SWDeviceCapabilitiesNoDisplayInUI | SWDeviceCapabilitiesDriverRequired)

typedef struct _SW_DEVICE
{
    ULONG Magic;
    LONG References;
    PNP_SWDEVICE_HANDLE Handle;
    SW_DEVICE_CREATE_CALLBACK Callback;
    PVOID Context;
    WCHAR DeviceInstanceId[PNPSW_MAX_DEVICE_ID_LEN];
} SW_DEVICE, *PSW_DEVICE;

static
VOID
ReleaseSwDevice(
    _In_ PSW_DEVICE Device)
{
    if (InterlockedDecrement(&Device->References) == 0)
    {
        Device->Magic = 0;
        HeapFree(GetProcessHeap(), 0, Device);
    }
}

static
PSW_DEVICE
GetSwDevice(
    _In_opt_ HSWDEVICE hSwDevice)
{
    PSW_DEVICE Device = (PSW_DEVICE)hSwDevice;
    PSW_DEVICE Result = NULL;

    if (!Device)
        return NULL;

    _SEH2_TRY
    {
        if (Device->Magic == SW_DEVICE_MAGIC)
            Result = Device;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = NULL;
    }
    _SEH2_END;

    return Result;
}

static
BOOL
IsValidIdPart(
    _In_ PCWSTR Part)
{
    PCWSTR p;

    if (!*Part)
        return FALSE;
    for (p = Part; *p; p++)
    {
        if (*p <= L' ' || *p > 0x7F || *p == L'\\' || *p == L',')
            return FALSE;
    }
    return TRUE;
}

static
ULONG
GetMultiSzCount(
    _In_opt_ PCZZWSTR List)
{
    PCWSTR p;

    if (!List)
        return 0;
    for (p = List; *p; p += wcslen(p) + 1)
        ;
    return (ULONG)(p - List) + 1;
}

static
DWORD
WINAPI
CreateCallbackWorker(
    _In_ PVOID Parameter)
{
    PSW_DEVICE Device = Parameter;

    Device->Callback((HSWDEVICE)Device, S_OK, Device->Context, Device->DeviceInstanceId);
    ReleaseSwDevice(Device);
    return 0;
}

HRESULT
WINAPI
SwDeviceCreate(
    _In_ PCWSTR pszEnumeratorName,
    _In_ PCWSTR pszParentDeviceInstance,
    _In_ const SW_DEVICE_CREATE_INFO *pCreateInfo,
    _In_ ULONG cPropertyCount,
    _In_reads_opt_(cPropertyCount) const DEVPROPERTY *pProperties,
    _In_ SW_DEVICE_CREATE_CALLBACK pCallback,
    _In_opt_ PVOID pContext,
    _Out_ PHSWDEVICE phSwDevice)
{
    RPC_BINDING_HANDLE BindingHandle;
    PSW_DEVICE Device;
    ULONG HardwareCount, CompatibleCount;
    HRESULT hr;

    TRACE("SwDeviceCreate(%s %s %p %lu %p %p %p %p)\n",
          debugstr_w(pszEnumeratorName), debugstr_w(pszParentDeviceInstance), pCreateInfo,
          cPropertyCount, pProperties, pCallback, pContext, phSwDevice);

    if (!phSwDevice)
        return E_INVALIDARG;
    *phSwDevice = NULL;

    if (!pszEnumeratorName || !IsValidIdPart(pszEnumeratorName) ||
        !pszParentDeviceInstance || !pCreateInfo || !pCallback ||
        pCreateInfo->cbSize < sizeof(*pCreateInfo) ||
        !pCreateInfo->pszInstanceId || !IsValidIdPart(pCreateInfo->pszInstanceId) ||
        (pCreateInfo->CapabilityFlags & ~SW_DEVICE_CAPABILITIES_ALL) ||
        (cPropertyCount != 0) != (pProperties != NULL))
    {
        return E_INVALIDARG;
    }

    HardwareCount = GetMultiSzCount(pCreateInfo->pszzHardwareIds);
    CompatibleCount = GetMultiSzCount(pCreateInfo->pszzCompatibleIds);
    if ((pCreateInfo->CapabilityFlags & SWDeviceCapabilitiesDriverRequired) &&
        HardwareCount <= 1 && CompatibleCount <= 1)
    {
        return E_INVALIDARG;
    }
    if (HardwareCount > PNPSW_MAX_ID_LIST_LEN || CompatibleCount > PNPSW_MAX_ID_LIST_LEN)
        return E_INVALIDARG;

    /* TODO: device properties, container IDs and security descriptors */
    if (cPropertyCount || pCreateInfo->pContainerId || pCreateInfo->pSecurityDescriptor)
        return E_NOTIMPL;

    if (!PnpGetLocalHandles(&BindingHandle))
        return HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE);

    Device = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Device));
    if (!Device)
        return E_OUTOFMEMORY;
    Device->Magic = SW_DEVICE_MAGIC;
    Device->References = 1;
    Device->Callback = pCallback;
    Device->Context = pContext;

    RpcTryExcept
    {
        hr = PNP_SwDeviceCreate(BindingHandle,
                                (LPWSTR)pszEnumeratorName,
                                (LPWSTR)pszParentDeviceInstance,
                                (LPWSTR)pCreateInfo->pszInstanceId,
                                (WCHAR *)pCreateInfo->pszzHardwareIds,
                                HardwareCount,
                                (WCHAR *)pCreateInfo->pszzCompatibleIds,
                                CompatibleCount,
                                pCreateInfo->CapabilityFlags,
                                (LPWSTR)pCreateInfo->pszDeviceDescription,
                                (LPWSTR)pCreateInfo->pszDeviceLocation,
                                Device->DeviceInstanceId,
                                &Device->Handle);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        hr = HRESULT_FROM_WIN32(RpcExceptionCode());
    }
    RpcEndExcept;

    if (FAILED(hr))
    {
        ReleaseSwDevice(Device);
        return hr;
    }

    Device->DeviceInstanceId[ARRAYSIZE(Device->DeviceInstanceId) - 1] = UNICODE_NULL;
    InterlockedIncrement(&Device->References);
    if (!QueueUserWorkItem(CreateCallbackWorker, Device, WT_EXECUTEDEFAULT))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        ReleaseSwDevice(Device);
        SwDeviceClose((HSWDEVICE)Device);
        return hr;
    }

    *phSwDevice = (HSWDEVICE)Device;
    return S_OK;
}

VOID
WINAPI
SwDeviceClose(
    _In_ HSWDEVICE hSwDevice)
{
    PSW_DEVICE Device = GetSwDevice(hSwDevice);

    TRACE("SwDeviceClose(%p)\n", hSwDevice);

    if (!Device)
        return;

    if (Device->Handle)
    {
        RpcTryExcept
        {
            PNP_SwDeviceClose(&Device->Handle);
        }
        RpcExcept(EXCEPTION_EXECUTE_HANDLER)
        {
            ERR("PNP_SwDeviceClose failed: 0x%lx\n", RpcExceptionCode());
            RpcSsDestroyClientContext(&Device->Handle);
        }
        RpcEndExcept;
    }

    Device->Magic = 0;
    ReleaseSwDevice(Device);
}

HRESULT
WINAPI
SwDeviceSetLifetime(
    _In_ HSWDEVICE hSwDevice,
    _In_ SW_DEVICE_LIFETIME Lifetime)
{
    PSW_DEVICE Device = GetSwDevice(hSwDevice);
    HRESULT hr;

    TRACE("SwDeviceSetLifetime(%p %d)\n", hSwDevice, Lifetime);

    if (!Device || (ULONG)Lifetime >= SWDeviceLifetimeMax)
        return E_INVALIDARG;

    RpcTryExcept
    {
        hr = PNP_SwDeviceSetLifetime(Device->Handle, (DWORD)Lifetime);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        hr = HRESULT_FROM_WIN32(RpcExceptionCode());
    }
    RpcEndExcept;

    return hr;
}

HRESULT
WINAPI
SwDeviceGetLifetime(
    _In_ HSWDEVICE hSwDevice,
    _Out_ PSW_DEVICE_LIFETIME pLifetime)
{
    PSW_DEVICE Device = GetSwDevice(hSwDevice);
    DWORD Lifetime = SWDeviceLifetimeHandle;
    HRESULT hr;

    TRACE("SwDeviceGetLifetime(%p %p)\n", hSwDevice, pLifetime);

    if (!Device || !pLifetime)
        return E_INVALIDARG;

    RpcTryExcept
    {
        hr = PNP_SwDeviceGetLifetime(Device->Handle, &Lifetime);
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        hr = HRESULT_FROM_WIN32(RpcExceptionCode());
    }
    RpcEndExcept;

    if (SUCCEEDED(hr))
        *pLifetime = (SW_DEVICE_LIFETIME)Lifetime;
    return hr;
}

VOID
WINAPI
SwMemFree(
    _In_ PVOID pMem)
{
    TRACE("SwMemFree(%p)\n", pMem);

    if (pMem)
        HeapFree(GetProcessHeap(), 0, pMem);
}
