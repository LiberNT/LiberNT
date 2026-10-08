/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DXGI flip swap chain presented through desktop composition
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#define INITGUID
#include "native12.h"
#include "native12_device.h"
#include "native12_objects.h"
#include <d3d11.h>
#include <dwmframe.h>

WINE_DEFAULT_DEBUG_CHANNEL(d3d12);

static const GUID Native12LegacyShareGuid =
    { 0xb76833be, 0x2939, 0x4a19, { 0x81, 0x2a, 0xc4, 0xd6, 0xb4, 0x80, 0x1a, 0x6c } };

extern "C" DWORD_PTR NTAPI NtUserCallOneParam(DWORD_PTR, DWORD);

#define NATIVE12_SWAPCHAIN_BUFFERS 16
#define NATIVE12_SWAPCHAIN_WAIT 5000

class Native12SwapChain final : public IDXGISwapChain4, public Native12Allocation
{
public:
    LONG references = 1;
    Native12Device *device;
    Native12CommandQueue *queue;
    IDXGIFactory *factory;
    HWND window;
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc = {};
    DXGI_RGBA background = {};
    Native12PrivateData private_data;
    Native12Resource *buffers[NATIVE12_SWAPCHAIN_BUFFERS] = {};
    D3DKMT_HANDLE shares[NATIVE12_SWAPCHAIN_BUFFERS] = {};
    HANDLE release_events[NATIVE12_SWAPCHAIN_BUFFERS] = {};
    DWM_DX_SURFACE_EXCHANGE publication = {};
    HANDLE completion = NULL;
    HANDLE latency = NULL;
    UINT latency_max = 1;
    IDXGIOutput *fullscreen_output = NULL;
    LONG_PTR saved_style = 0;
    LONG_PTR saved_ex_style = 0;
    RECT saved_rect = {};
    SRWLOCK lock = SRWLOCK_INIT;
    UINT current = 0;
    UINT present_count = 0;

    Native12SwapChain(Native12CommandQueue *owner, IDXGIFactory *parent, HWND target)
        : device(owner->device), queue(owner), factory(parent), window(target)
    {
        queue->AddRef();
        factory->AddRef();
    }
    ~Native12SwapChain()
    {
        if (fullscreen_output) SetFullscreenState(FALSE, NULL);
        RetirePublication();
        ReleaseBuffers();
        if (completion) CloseHandle(completion);
        if (latency) CloseHandle(latency);
        factory->Release();
        queue->Release();
    }

    HRESULT AllocateBuffers(const DXGI_SWAP_CHAIN_DESC1 &requested);
    void ReleaseBuffers();
    HRESULT RetirePublication();
    HRESULT WaitForQueue();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        if (IsEqualGUID(iid, IID_IDXGISwapChain4) || IsEqualGUID(iid, IID_IDXGISwapChain3)
                || IsEqualGUID(iid, IID_IDXGISwapChain2) || IsEqualGUID(iid, IID_IDXGISwapChain1)
                || IsEqualGUID(iid, IID_IDXGISwapChain) || IsEqualGUID(iid, IID_IDXGIDeviceSubObject)
                || IsEqualGUID(iid, IID_IDXGIObject) || IsEqualGUID(iid, IID_IUnknown))
        {
            *out = static_cast<IDXGISwapChain4 *>(this);
            AddRef();
            return S_OK;
        }
        *out = NULL;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG count = InterlockedDecrement(&references);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void *data) override
    {
        return private_data.Set(guid, size, data, NULL);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *data) override
    {
        return private_data.SetInterface(guid, const_cast<IUnknown *>(data));
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *size, void *data) override
    {
        return private_data.Get(guid, size, data);
    }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID iid, void **out) override { return factory->QueryInterface(iid, out); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID iid, void **out) override { return queue->QueryInterface(iid, out); }
    HRESULT STDMETHODCALLTYPE Present(UINT interval, UINT flags) override { return Present1(interval, flags, NULL); }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index, REFIID iid, void **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        if (index >= desc.BufferCount || !buffers[index]) return DXGI_ERROR_INVALID_CALL;
        return buffers[index]->QueryInterface(iid, out);
    }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen, IDXGIOutput *output) override
    {
        if (!fullscreen)
        {
            if (!fullscreen_output) return S_OK;
            fullscreen_output->Release();
            fullscreen_output = NULL;
            fullscreen_desc.Windowed = TRUE;
            SetWindowLongPtrW(window, GWL_STYLE, saved_style);
            SetWindowLongPtrW(window, GWL_EXSTYLE, saved_ex_style);
            SetWindowPos(window, HWND_NOTOPMOST, saved_rect.left, saved_rect.top, saved_rect.right - saved_rect.left,
                    saved_rect.bottom - saved_rect.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
            return S_OK;
        }
        IDXGIOutput *target = output;
        HRESULT hr = S_OK;
        if (target) target->AddRef();
        else hr = GetContainingOutput(&target);
        if (FAILED(hr)) return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        DXGI_OUTPUT_DESC output_desc;
        if (FAILED(hr = target->GetDesc(&output_desc)))
        {
            target->Release();
            return hr;
        }
        if (fullscreen_output) fullscreen_output->Release();
        else
        {
            saved_style = GetWindowLongPtrW(window, GWL_STYLE);
            saved_ex_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
            GetWindowRect(window, &saved_rect);
        }
        fullscreen_output = target;
        fullscreen_desc.Windowed = FALSE;
        const RECT &area = output_desc.DesktopCoordinates;
        SetWindowLongPtrW(window, GWL_STYLE, (saved_style & ~(WS_OVERLAPPEDWINDOW | WS_DLGFRAME)) | WS_POPUP);
        SetWindowLongPtrW(window, GWL_EXSTYLE, saved_ex_style & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME));
        SetWindowPos(window, HWND_TOPMOST, area.left, area.top, area.right - area.left, area.bottom - area.top,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL *fullscreen, IDXGIOutput **output) override
    {
        if (fullscreen) *fullscreen = fullscreen_output != NULL;
        if (output)
        {
            *output = fullscreen_output;
            if (fullscreen_output) fullscreen_output->AddRef();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC *out) override
    {
        if (!out) return E_INVALIDARG;
        ZeroMemory(out, sizeof(*out));
        out->BufferDesc.Width = desc.Width;
        out->BufferDesc.Height = desc.Height;
        out->BufferDesc.RefreshRate = fullscreen_desc.RefreshRate;
        out->BufferDesc.Format = desc.Format;
        out->BufferDesc.ScanlineOrdering = fullscreen_desc.ScanlineOrdering;
        out->BufferDesc.Scaling = fullscreen_desc.Scaling;
        out->SampleDesc = desc.SampleDesc;
        out->BufferUsage = desc.BufferUsage;
        out->BufferCount = desc.BufferCount;
        out->OutputWindow = window;
        out->Windowed = fullscreen_output == NULL;
        out->SwapEffect = desc.SwapEffect;
        out->Flags = desc.Flags;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count, UINT width, UINT height, DXGI_FORMAT format,
            UINT flags) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC *mode) override
    {
        if (!mode) return DXGI_ERROR_INVALID_CALL;
        if (fullscreen_output || !mode->Width || !mode->Height) return S_OK;
        RECT rect = {0, 0, static_cast<LONG>(mode->Width), static_cast<LONG>(mode->Height)};
        AdjustWindowRectEx(&rect, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), GetMenu(window) != NULL,
                static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
        SetWindowPos(window, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        for (UINT index = 0;; ++index)
        {
            IDXGIOutput *candidate = NULL;
            HRESULT hr = device->adapter->EnumOutputs(index, &candidate);
            if (FAILED(hr)) return hr;
            DXGI_OUTPUT_DESC output_desc;
            hr = candidate->GetDesc(&output_desc);
            if (SUCCEEDED(hr) && output_desc.Monitor == monitor)
            {
                *out = candidate;
                return S_OK;
            }
            candidate->Release();
        }
    }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS *out) override
    {
        if (!out) return E_INVALIDARG;
        ZeroMemory(out, sizeof(*out));
        return DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
    }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT *out) override
    {
        if (!out) return E_INVALIDARG;
        *out = present_count;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1 *out) override
    {
        if (!out) return E_INVALIDARG;
        *out = desc;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *out) override
    {
        if (!out) return E_INVALIDARG;
        *out = fullscreen_desc;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND *out) override
    {
        if (!out) return E_INVALIDARG;
        *out = window;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID, void **out) override
    {
        if (out) *out = NULL;
        return DXGI_ERROR_INVALID_CALL;
    }
    HRESULT STDMETHODCALLTYPE Present1(UINT interval, UINT flags, const DXGI_PRESENT_PARAMETERS *parameters) override;
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA *value) override
    {
        if (!value) return E_INVALIDARG;
        background = *value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA *out) override
    {
        if (!out) return E_INVALIDARG;
        *out = background;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation) override
    {
        return rotation == DXGI_MODE_ROTATION_IDENTITY ? S_OK : DXGI_ERROR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION *out) override
    {
        if (!out) return E_INVALIDARG;
        *out = DXGI_MODE_ROTATION_IDENTITY;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT width, UINT height) override
    {
        if (!width || !height || width > desc.Width || height > desc.Height) return E_INVALIDARG;
        return width == desc.Width && height == desc.Height ? S_OK : DXGI_ERROR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT *width, UINT *height) override
    {
        if (!width || !height) return E_INVALIDARG;
        *width = desc.Width;
        *height = desc.Height;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT value) override
    {
        if (!latency || !value || value > DXGI_MAX_SWAP_CHAIN_BUFFERS) return DXGI_ERROR_INVALID_CALL;
        AcquireSRWLockExclusive(&lock);
        if (value > latency_max) ReleaseSemaphore(latency, value - latency_max, NULL);
        latency_max = value;
        ReleaseSRWLockExclusive(&lock);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *out) override
    {
        if (!latency || !out) return DXGI_ERROR_INVALID_CALL;
        *out = latency_max;
        return S_OK;
    }
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override
    {
        HANDLE handle = NULL;
        if (!latency || !DuplicateHandle(GetCurrentProcess(), latency, GetCurrentProcess(), &handle, 0, FALSE,
                DUPLICATE_SAME_ACCESS))
            return NULL;
        return handle;
    }
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F *) override { return DXGI_ERROR_INVALID_CALL; }
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F *) override { return DXGI_ERROR_INVALID_CALL; }
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return current; }
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space, UINT *support) override
    {
        if (!support) return E_INVALIDARG;
        *support = color_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
                ? DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE color_space) override
    {
        return color_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ? S_OK : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags,
            const UINT *, IUnknown *const *) override
    {
        return ResizeBuffers(count, width, height, format, flags);
    }
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE type, UINT, void *) override
    {
        return type == DXGI_HDR_METADATA_TYPE_NONE ? S_OK : DXGI_ERROR_UNSUPPORTED;
    }
};

static bool Native12PublishableFormat(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM;
}

void Native12SwapChain::ReleaseBuffers()
{
    for (UINT i = 0; i < NATIVE12_SWAPCHAIN_BUFFERS; ++i)
    {
        if (buffers[i]) buffers[i]->Release();
        buffers[i] = NULL;
        shares[i] = 0;
        if (release_events[i]) CloseHandle(release_events[i]);
        release_events[i] = NULL;
    }
}

HRESULT Native12SwapChain::AllocateBuffers(const DXGI_SWAP_CHAIN_DESC1 &requested)
{
    Native12Resource *created[NATIVE12_SWAPCHAIN_BUFFERS] = {};
    D3DKMT_HANDLE created_shares[NATIVE12_SWAPCHAIN_BUFFERS] = {};
    HANDLE created_events[NATIVE12_SWAPCHAIN_BUFFERS] = {};
    HRESULT hr = S_OK;

    if (requested.BufferCount < 2 || requested.BufferCount > NATIVE12_SWAPCHAIN_BUFFERS) return DXGI_ERROR_INVALID_CALL;
    if (requested.SampleDesc.Count != 1 || requested.Stereo) return DXGI_ERROR_INVALID_CALL;
    if (requested.SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL && requested.SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD)
        return DXGI_ERROR_INVALID_CALL;
    if (!Native12PublishableFormat(requested.Format)) return DXGI_ERROR_UNSUPPORTED;
    if (!device->register_resource || !device->get_resource_handles || !device->enqueue_set_event)
        return DXGI_ERROR_UNSUPPORTED;

    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texture = {};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = requested.Width;
    texture.Height = requested.Height;
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 1;
    texture.Format = requested.Format;
    texture.SampleDesc.Count = 1;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    Native12SharedTextureData shared = {};
    shared.signature = 0x54313144;
    shared.version = 1;
    shared.desc.Width = requested.Width;
    shared.desc.Height = requested.Height;
    shared.desc.MipLevels = 1;
    shared.desc.ArraySize = 1;
    shared.desc.Format = requested.Format;
    shared.desc.SampleDesc.Count = 1;
    shared.desc.Usage = D3D11_USAGE_DEFAULT;
    shared.desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    shared.desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    for (UINT i = 0; i < requested.BufferCount && SUCCEEDED(hr); ++i)
    {
        created[i] = new Native12Resource(device);
        if (!created[i])
        {
            hr = E_OUTOFMEMORY;
            break;
        }
        created[i]->shared_data = &shared;
        created[i]->shared_data_size = sizeof(shared);
        hr = created[i]->Initialize(&properties, D3D12_HEAP_FLAG_SHARED, &texture, D3D12_RESOURCE_STATE_COMMON, NULL);
        created[i]->shared_data = NULL;
        if (FAILED(hr)) break;
        hr = device->get_resource_handles(device->runtime_device, created[i]->identity, NULL, &created_shares[i]);
        if (SUCCEEDED(hr) && !created_shares[i]) hr = E_FAIL;
        if (SUCCEEDED(hr))
            hr = created[i]->SetPrivateData(Native12LegacyShareGuid, sizeof(created_shares[i]), &created_shares[i]);
        if (FAILED(hr)) break;
        created_events[i] = CreateEventW(NULL, TRUE, TRUE, NULL);
        if (!created_events[i]) hr = HRESULT_FROM_WIN32(GetLastError());
    }
    if (FAILED(hr))
    {
        WARN("Failed to create %u x %u back buffers of format %#x, hr %#lx.\n", requested.Width, requested.Height,
                requested.Format, hr);
        for (UINT i = 0; i < NATIVE12_SWAPCHAIN_BUFFERS; ++i)
        {
            if (created[i]) created[i]->Release();
            if (created_events[i]) CloseHandle(created_events[i]);
        }
        return hr;
    }

    RetirePublication();
    ReleaseBuffers();
    for (UINT i = 0; i < requested.BufferCount; ++i)
    {
        buffers[i] = created[i];
        shares[i] = created_shares[i];
        release_events[i] = created_events[i];
        TRACE("Back buffer %u: resource %p, share %#x.\n", i, buffers[i], shares[i]);
    }
    desc = requested;
    current = 0;
    return S_OK;
}

HRESULT Native12SwapChain::RetirePublication()
{
    if (!publication.GlobalShare) return S_OK;
    DWM_DX_SURFACE_EXCHANGE exchange = publication;
    exchange.Action = DWM_DX_SURFACE_UNREGISTER;
    NTSTATUS status = static_cast<NTSTATUS>(NtUserCallOneParam(reinterpret_cast<DWORD_PTR>(&exchange),
            DWM_ROUTINE_DXSURFACE));
    HRESULT hr = status == STATUS_NOT_FOUND ? S_OK : Native12StatusToHresult(status);
    if (SUCCEEDED(hr)) ZeroMemory(&publication, sizeof(publication));
    DWORD started = GetTickCount();
    for (UINT i = 0; i < NATIVE12_SWAPCHAIN_BUFFERS && SUCCEEDED(hr); ++i)
    {
        if (!release_events[i]) continue;
        DWORD elapsed = GetTickCount() - started;
        if (WaitForSingleObject(release_events[i], elapsed < NATIVE12_SWAPCHAIN_WAIT
                ? NATIVE12_SWAPCHAIN_WAIT - elapsed : 0) != WAIT_OBJECT_0)
            hr = DXGI_ERROR_WAS_STILL_DRAWING;
    }
    return hr;
}

HRESULT Native12SwapChain::WaitForQueue()
{
    if (!completion) completion = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!completion) return HRESULT_FROM_WIN32(GetLastError());
    HRESULT hr = device->enqueue_set_event(device->runtime_device, completion);
    if (FAILED(hr)) return hr;
    return WaitForSingleObject(completion, NATIVE12_SWAPCHAIN_WAIT) == WAIT_OBJECT_0
            ? S_OK : DXGI_ERROR_WAS_STILL_DRAWING;
}

HRESULT STDMETHODCALLTYPE Native12SwapChain::Present1(UINT interval, UINT flags,
        const DXGI_PRESENT_PARAMETERS *)
{
    if (interval > 4) return DXGI_ERROR_INVALID_CALL;
    if (flags & DXGI_PRESENT_TEST) return S_OK;
    if (FAILED(device->removed_reason)) return DXGI_ERROR_DEVICE_REMOVED;
    if (IsIconic(window))
    {
        if (latency) ReleaseSemaphore(latency, 1, NULL);
        return DXGI_STATUS_OCCLUDED;
    }

    AcquireSRWLockExclusive(&lock);
    HRESULT hr = WaitForQueue();
    if (SUCCEEDED(hr))
    {
        DXGI_ADAPTER_DESC adapter_desc;
        DWM_DX_SURFACE_EXCHANGE exchange = {};
        exchange.StructSize = sizeof(exchange);
        exchange.Action = DWM_DX_SURFACE_PUBLISH;
        exchange.Window = reinterpret_cast<ULONG_PTR>(window);
        hr = device->adapter->GetDesc(&adapter_desc);
        exchange.AdapterLuid = adapter_desc.AdapterLuid;
        exchange.GlobalShare = shares[current];
        exchange.Info.Magic = DWM_DX_SURFACE_INFO_MAGIC;
        exchange.Info.Version = DWM_DX_SURFACE_INFO_VERSION_GPU;
        exchange.Info.Width = desc.Width;
        exchange.Info.Height = desc.Height;
        exchange.Info.Format = desc.Format;
        exchange.Flags = DWM_DX_PUBLISH_RETAINED;
        if (desc.AlphaMode == DXGI_ALPHA_MODE_PREMULTIPLIED) exchange.Flags |= DWM_DX_PUBLISH_PREMULTIPLIED;
        exchange.ReadyEvent = reinterpret_cast<ULONG_PTR>(release_events[current]);
        exchange.UpdateRect.right = desc.Width;
        exchange.UpdateRect.bottom = desc.Height;
        if (SUCCEEDED(hr))
        {
            ResetEvent(release_events[current]);
            NTSTATUS status = static_cast<NTSTATUS>(NtUserCallOneParam(reinterpret_cast<DWORD_PTR>(&exchange),
                    DWM_ROUTINE_DXSURFACE));
            if (status >= 0) publication = exchange;
            else
            {
                SetEvent(release_events[current]);
                WARN("Frame %u was not published, status %#lx.\n", present_count, status);
            }
        }
    }
    if (SUCCEEDED(hr))
    {
        ++present_count;
        current = (current + 1) % desc.BufferCount;
        if (WaitForSingleObject(release_events[current], NATIVE12_SWAPCHAIN_WAIT) != WAIT_OBJECT_0)
            hr = DXGI_ERROR_WAS_STILL_DRAWING;
        if (latency) ReleaseSemaphore(latency, 1, NULL);
    }
    ReleaseSRWLockExclusive(&lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12SwapChain::ResizeBuffers(UINT count, UINT width, UINT height,
        DXGI_FORMAT format, UINT flags)
{
    AcquireSRWLockExclusive(&lock);
    for (UINT i = 0; i < desc.BufferCount; ++i)
    {
        if (buffers[i] && buffers[i]->references != 1)
        {
            ReleaseSRWLockExclusive(&lock);
            return DXGI_ERROR_INVALID_CALL;
        }
    }
    DXGI_SWAP_CHAIN_DESC1 requested = desc;
    if (count) requested.BufferCount = count;
    if (format != DXGI_FORMAT_UNKNOWN) requested.Format = format;
    requested.Flags = flags;
    RECT client = {};
    GetClientRect(window, &client);
    requested.Width = width ? width : max(1, client.right - client.left);
    requested.Height = height ? height : max(1, client.bottom - client.top);
    HRESULT hr = AllocateBuffers(requested);
    ReleaseSRWLockExclusive(&lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12CommandQueue::create_swapchain(IDXGIFactory *factory, HWND window,
        const DXGI_SWAP_CHAIN_DESC1 *desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc,
        IDXGIOutput *output, IDXGISwapChain1 **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!factory || !desc) return E_INVALIDARG;
    if (!window) return DXGI_ERROR_UNSUPPORTED;
    if (output) FIXME("Restricting presentation to output %p is not supported.\n", output);
    if (this->desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return DXGI_ERROR_INVALID_CALL;

    Native12SwapChain *swapchain = new Native12SwapChain(this, factory, window);
    if (!swapchain) return E_OUTOFMEMORY;
    if (fullscreen_desc) swapchain->fullscreen_desc = *fullscreen_desc;
    swapchain->fullscreen_desc.Windowed = TRUE;
    DXGI_SWAP_CHAIN_DESC1 requested = *desc;
    RECT client = {};
    GetClientRect(window, &client);
    if (!requested.Width) requested.Width = max(1, client.right - client.left);
    if (!requested.Height) requested.Height = max(1, client.bottom - client.top);
    HRESULT hr = swapchain->AllocateBuffers(requested);
    if (SUCCEEDED(hr) && (requested.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        swapchain->latency = CreateSemaphoreW(NULL, 1, DXGI_MAX_SWAP_CHAIN_BUFFERS, NULL);
        if (!swapchain->latency) hr = HRESULT_FROM_WIN32(GetLastError());
    }
    if (SUCCEEDED(hr) && fullscreen_desc && !fullscreen_desc->Windowed)
        hr = swapchain->SetFullscreenState(TRUE, NULL);
    if (FAILED(hr))
    {
        swapchain->Release();
        return hr;
    }
    *out = swapchain;
    return S_OK;
}
