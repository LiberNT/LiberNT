/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 user-mode display driver backend, shared declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <d3d12.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <wine/winedxgi.h>
#include <d3dkmthk.h>
#include <d3d12umddi.h>
#include <drivers/directx/umd_adapter.h>
#include <wine/debug.h>
#include <stddef.h>
#include <stdlib.h>

#define NATIVE12_DDI_INTERFACE D3D12DDI_INTERFACE_VERSION_R8
#define NATIVE12_DDI_BUILD D3D12DDI_BUILD_VERSION_0110

typedef D3D12DDI_DEVICE_FUNCS_CORE_0109 NATIVE12_DEVICE_FUNCS;
typedef D3D12DDI_COMMAND_LIST_FUNCS_3D_0108 NATIVE12_LIST_FUNCS;
typedef D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 NATIVE12_QUEUE_FUNCS;
typedef D3D12DDI_CORELAYER_DEVICECALLBACKS_0062 NATIVE12_CORE_CALLBACKS;

static inline HRESULT Native12StatusToHresult(NTSTATUS status)
{
    return status >= 0 ? S_OK : HRESULT_FROM_NT(status);
}

class Native12Allocation
{
public:
    static void *operator new(size_t size) noexcept { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size); }
    static void operator delete(void *ptr) noexcept { HeapFree(GetProcessHeap(), 0, ptr); }
};

class Native12PrivateData
{
    struct Entry
    {
        Entry *next;
        GUID guid;
        UINT size;
        IUnknown *object;
        bool is_interface;
        BYTE data[1];
    };
    HRESULT Store(REFGUID guid, UINT size, const void *data, IUnknown *object, bool is_interface);
    Entry *head = NULL;
    SRWLOCK lock = SRWLOCK_INIT;

public:
    ~Native12PrivateData();
    HRESULT Get(REFGUID guid, UINT *size, void *data);
    HRESULT Set(REFGUID guid, UINT size, const void *data, IUnknown *object);
    HRESULT SetInterface(REFGUID guid, IUnknown *object);
};

class Native12Device;

HRESULT Native12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_level, REFIID iid, void **out);
extern "C" void d3d12_dred_enablement(D3D12_DRED_ENABLEMENT *auto_breadcrumbs, D3D12_DRED_ENABLEMENT *page_fault);
extern "C" HRESULT vkd3d_create_shader_cache_session(ID3D12Device *device,
        const D3D12_SHADER_CACHE_SESSION_DESC *desc, const IID *iid, void **session);
