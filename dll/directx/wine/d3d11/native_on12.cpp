/*
 * PROJECT:     LiberNT Direct3D 11 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 on 12 device over the native Direct3D 11 runtime
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_4.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

extern "C" HRESULT d3d11_native_device_attach_on12(ID3D11Device *device, IUnknown *on12);

static const GUID NativeOn12ResourceGuid =
    { 0x6f1e5a3c, 0x2b7d, 0x4c41, { 0x9a, 0x0e, 0x51, 0x8d, 0x3c, 0x72, 0xb4, 0x16 } };
static const GUID Native12LegacyShareGuid =
    { 0xb76833be, 0x2939, 0x4a19, { 0x81, 0x2a, 0xc4, 0xd6, 0xb4, 0x80, 0x1a, 0x6c } };

class NativeOn12 final : public ID3D11On12Device1
{
public:
    ID3D11Device *device11 = NULL;
    ID3D12Device *device12 = NULL;
    ID3D12CommandQueue *queue12 = NULL;
    ID3D12Fence *fence = NULL;
    UINT64 fence_value = 0;
    HANDLE event = NULL;
    CRITICAL_SECTION lock;

    NativeOn12() { InitializeCriticalSection(&lock); }
    ~NativeOn12()
    {
        if (fence) fence->Release();
        if (queue12) queue12->Release();
        if (device12) device12->Release();
        if (event) CloseHandle(event);
        DeleteCriticalSection(&lock);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        if (IsEqualGUID(iid, IID_ID3D11On12Device) || IsEqualGUID(iid, IID_ID3D11On12Device1))
        {
            *out = static_cast<ID3D11On12Device1 *>(this);
            device11->AddRef();
            return S_OK;
        }
        return device11->QueryInterface(iid, out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return device11->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return device11->Release(); }

    HRESULT STDMETHODCALLTYPE CreateWrappedResource(IUnknown *resource, const D3D11_RESOURCE_FLAGS *flags,
            D3D12_RESOURCE_STATES input_state, D3D12_RESOURCE_STATES output_state, REFIID iid, void **out) override
    {
        ID3D12Resource *resource12 = NULL;
        ID3D11Device1 *device1 = NULL;
        HANDLE shared = NULL;
        HRESULT hr;

        if (!resource || !out) return E_INVALIDARG;
        *out = NULL;
        if (FAILED(resource->QueryInterface(IID_ID3D12Resource, reinterpret_cast<void **>(&resource12))))
            return E_INVALIDARG;
        UINT legacy = 0, legacy_size = sizeof(legacy);
        if (SUCCEEDED(resource12->GetPrivateData(Native12LegacyShareGuid, &legacy_size, &legacy)) && legacy)
        {
            hr = device11->OpenSharedResource(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(legacy)), iid, out);
        }
        else
        {
            hr = device12->CreateSharedHandle(resource12, NULL, GENERIC_ALL, NULL, &shared);
            if (FAILED(hr))
            {
                FIXME("Only shareable Direct3D 12 resources can be wrapped, hr %#lx.\n", hr);
                resource12->Release();
                return hr;
            }
            hr = device11->QueryInterface(IID_ID3D11Device1, reinterpret_cast<void **>(&device1));
            if (SUCCEEDED(hr))
            {
                hr = device1->OpenSharedResource1(shared, iid, out);
                device1->Release();
            }
            CloseHandle(shared);
        }
        if (SUCCEEDED(hr))
        {
            ID3D11DeviceChild *child = NULL;
            if (SUCCEEDED(static_cast<IUnknown *>(*out)->QueryInterface(IID_ID3D11DeviceChild,
                    reinterpret_cast<void **>(&child))))
            {
                child->SetPrivateDataInterface(NativeOn12ResourceGuid, resource12);
                child->Release();
            }
        }
        resource12->Release();
        return hr;
    }

    void STDMETHODCALLTYPE ReleaseWrappedResources(ID3D11Resource *const *resources, UINT count) override
    {
        ID3D11DeviceContext *context = NULL;
        D3D11_QUERY_DESC desc = { D3D11_QUERY_EVENT, 0 };
        ID3D11Query *query = NULL;
        BOOL done = FALSE;

        if (!count) return;
        device11->GetImmediateContext(&context);
        if (!context) return;
        if (SUCCEEDED(device11->CreateQuery(&desc, &query)))
        {
            context->End(query);
            context->Flush();
            while (context->GetData(query, &done, sizeof(done), 0) == S_FALSE)
                SwitchToThread();
            query->Release();
        }
        else
        {
            context->Flush();
        }
        context->Release();
    }

    void STDMETHODCALLTYPE AcquireWrappedResources(ID3D11Resource *const *resources, UINT count) override
    {
        if (!count) return;
        EnterCriticalSection(&lock);
        UINT64 value = ++fence_value;
        if (SUCCEEDED(queue12->Signal(fence, value)) && fence->GetCompletedValue() < value
                && SUCCEEDED(fence->SetEventOnCompletion(value, event)))
            WaitForSingleObject(event, INFINITE);
        LeaveCriticalSection(&lock);
    }

    HRESULT STDMETHODCALLTYPE GetD3D12Device(REFIID iid, ID3D12Device **out) override
    {
        if (!out) return E_INVALIDARG;
        return device12->QueryInterface(iid, reinterpret_cast<void **>(out));
    }
};

extern "C" void d3d11_native_on12_free(IUnknown *on12)
{
    delete static_cast<NativeOn12 *>(static_cast<ID3D11On12Device1 *>(on12));
}

extern "C" HRESULT d3d11_native_on12_create_device(IUnknown *device, UINT flags,
        const D3D_FEATURE_LEVEL *feature_levels, UINT feature_level_count, IUnknown *const *queues,
        UINT queue_count, UINT node_mask, ID3D11Device **d3d11_device, ID3D11DeviceContext **d3d11_context,
        D3D_FEATURE_LEVEL *obtained_feature_level)
{
    ID3D12Device *device12 = NULL;
    ID3D12CommandQueue *queue12 = NULL;
    IDXGIFactory4 *factory = NULL;
    IDXGIAdapter *adapter = NULL;
    ID3D11Device *device11 = NULL;
    ID3D11DeviceContext *context11 = NULL;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    NativeOn12 *on12 = NULL;
    HRESULT hr;

    if (d3d11_device) *d3d11_device = NULL;
    if (d3d11_context) *d3d11_context = NULL;
    if (!device || !queues || !queue_count || !queues[0] || node_mask > 1) return E_INVALIDARG;
    if (FAILED(device->QueryInterface(IID_ID3D12Device, reinterpret_cast<void **>(&device12))))
        return E_INVALIDARG;
    hr = queues[0]->QueryInterface(IID_ID3D12CommandQueue, reinterpret_cast<void **>(&queue12));
    if (SUCCEEDED(hr)) hr = CreateDXGIFactory1(IID_IDXGIFactory4, reinterpret_cast<void **>(&factory));
    if (SUCCEEDED(hr))
        hr = factory->EnumAdapterByLuid(device12->GetAdapterLuid(), IID_IDXGIAdapter,
                reinterpret_cast<void **>(&adapter));
    if (SUCCEEDED(hr))
        hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, flags, feature_levels, feature_level_count,
                D3D11_SDK_VERSION, &device11, &level, &context11);
    if (SUCCEEDED(hr) && !(on12 = new NativeOn12())) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        on12->device11 = device11;
        on12->device12 = device12;
        on12->queue12 = queue12;
        device12 = NULL;
        queue12 = NULL;
        hr = on12->device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence,
                reinterpret_cast<void **>(&on12->fence));
        if (SUCCEEDED(hr) && !(on12->event = CreateEventW(NULL, FALSE, FALSE, NULL)))
            hr = HRESULT_FROM_WIN32(GetLastError());
        if (SUCCEEDED(hr))
            hr = d3d11_native_device_attach_on12(device11, static_cast<ID3D11On12Device1 *>(on12));
        if (FAILED(hr))
        {
            delete on12;
            on12 = NULL;
        }
    }
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (queue12) queue12->Release();
    if (device12) device12->Release();
    if (FAILED(hr))
    {
        WARN("Failed to create a Direct3D 11 on 12 device, hr %#lx.\n", hr);
        if (context11) context11->Release();
        if (device11) device11->Release();
        return hr;
    }
    if (obtained_feature_level) *obtained_feature_level = level;
    if (d3d11_context) *d3d11_context = context11;
    else context11->Release();
    if (d3d11_device) *d3d11_device = device11;
    else device11->Release();
    return S_OK;
}
