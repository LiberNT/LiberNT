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
#include <math.h>

WINE_DEFAULT_DEBUG_CHANNEL(d3d12);

static const GUID Native12LegacyShareGuid =
    { 0xb76833be, 0x2939, 0x4a19, { 0x81, 0x2a, 0xc4, 0xd6, 0xb4, 0x80, 0x1a, 0x6c } };

extern "C" DWORD_PTR NTAPI NtUserCallOneParam(DWORD_PTR, DWORD);

#define NATIVE12_SWAPCHAIN_BUFFERS 16
#define NATIVE12_SWAPCHAIN_WAIT 5000
#define NATIVE12_SWAPCHAIN_RECORDS 16

struct Native12PresentRecord
{
    UINT buffer;
    bool foreign;
    UINT64 copy_value;
    UINT64 release_value;
    UINT source_width, source_height;
    DWM_DX_SURFACE_EXCHANGE exchange;
};

struct Native12ReleaseWait
{
    ID3D12Fence *fence;
    UINT64 value;
    HANDLE wait;
};

static VOID CALLBACK Native12ReleaseCallback(PVOID context, BOOLEAN)
{
    Native12ReleaseWait *wait = static_cast<Native12ReleaseWait *>(context);
    wait->fence->Signal(wait->value);
}

static LONG Native12FullscreenStyle(LONG style)
{
    return style & ~(WS_POPUP | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME | WS_SYSMENU | WS_DLGFRAME | WS_BORDER);
}

static LONG Native12FullscreenExStyle(LONG exstyle)
{
    return exstyle & ~(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_CONTEXTHELP);
}

struct Native12WindowState : public Native12Allocation
{
    HWND window;
    HWND after;
    LONG style, exstyle;
    int x, y, width, height;
    UINT flags;
    bool set_style;
    bool foreground;
};

static DWORD WINAPI Native12ApplyWindowState(void *context)
{
    Native12WindowState *state = static_cast<Native12WindowState *>(context);
    if (state->set_style)
    {
        SetWindowLongW(state->window, GWL_STYLE, state->style);
        SetWindowLongW(state->window, GWL_EXSTYLE, state->exstyle);
    }
    SetWindowPos(state->window, state->after, state->x, state->y, state->width, state->height, state->flags);
    if (state->foreground) SetForegroundWindow(state->window);
    delete state;
    return 0;
}

static void Native12SetWindowState(Native12WindowState *state)
{
    if (GetWindowThreadProcessId(state->window, NULL) == GetCurrentThreadId())
    {
        Native12ApplyWindowState(state);
        return;
    }
    HANDLE thread = CreateThread(NULL, 0, Native12ApplyWindowState, state, 0, NULL);
    if (thread) CloseHandle(thread);
    else delete state;
}

class Native12SwapChain;

struct Native12AltEnterHook : public Native12Allocation
{
    Native12AltEnterHook *next;
    DWORD thread;
    HHOOK hook;
    UINT users;
};

static SRWLOCK native12_alt_enter_lock = SRWLOCK_INIT;
static Native12SwapChain *native12_alt_enter_chains;
static Native12AltEnterHook *native12_alt_enter_hooks;
static LRESULT CALLBACK Native12AltEnterProc(int code, WPARAM wparam, LPARAM lparam);

static HRESULT Native12ResizeFlags(UINT current, UINT flags)
{
    static const UINT creation_only = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
            | DXGI_SWAP_CHAIN_FLAG_FOREGROUND_LAYER | DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    if (flags & (DXGI_SWAP_CHAIN_FLAG_NONPREROTATED | DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY
            | DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO)) return DXGI_ERROR_INVALID_CALL;
    if ((flags ^ current) & creation_only) return E_INVALIDARG;
    if ((current & DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED) && !(flags & DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED)) return E_INVALIDARG;
    return S_OK;
}

class Native12SwapChain final : public IDXGISwapChain4, public Native12Allocation
{
public:
    LONG references = 1;
    Native12Device *device;
    Native12CommandQueue *queue;
    IDXGIFactory *factory;
    HWND window;
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    UINT source_width = 0, source_height = 0;
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
    LONG in_set_fullscreen_state = 0;
    LONG windowed_style = 0;
    LONG windowed_exstyle = 0;
    RECT windowed_rect = {};
    bool fullscreen_layout = false;
    bool fullscreen_focused = false;
    Native12SwapChain *alt_enter_next = NULL;
    DWORD alt_enter_thread = 0;
    bool alt_enter_registered = false;
    void RegisterAltEnter();
    void UnregisterAltEnter();
    void ToggleFullscreen();
    bool TryAddRef()
    {
        for (LONG count = references; count > 0; count = references)
            if (InterlockedCompareExchange(&references, count + 1, count) == count) return true;
        return false;
    }
    bool FocusLost()
    {
        if (fullscreen_desc.Windowed || !IsWindow(window)) return false;
        HWND foreground = GetForegroundWindow();
        if (foreground == window) fullscreen_focused = true;
        return fullscreen_focused && foreground && foreground != window && IsWindowVisible(foreground);
    }
    WCHAR fullscreen_device[32] = {};
    bool fullscreen_mode = false;
    bool foreign = false;
    bool foreign_known = false;
    D3DKMT_HANDLE foreign_share = 0;
    D3DKMT_HANDLE foreign_resource = 0;
    D3DKMT_HANDLE foreign_allocation = 0;
    UINT foreign_width = 0;
    UINT foreign_height = 0;
    UINT foreign_pitch = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT readback_layout = {};
    UINT64 readback_size = 0;
    ID3D12Fence *copy_fence = NULL;
    UINT64 copy_value = 0;
    Native12PresentRecord records[NATIVE12_SWAPCHAIN_RECORDS] = {};
    HANDLE record_events[NATIVE12_SWAPCHAIN_RECORDS] = {};
    ID3D12Resource *record_readback[NATIVE12_SWAPCHAIN_RECORDS] = {};
    ID3D12CommandAllocator *record_allocator[NATIVE12_SWAPCHAIN_RECORDS] = {};
    ID3D12GraphicsCommandList *record_list[NATIVE12_SWAPCHAIN_RECORDS] = {};
    UINT record_order[NATIVE12_SWAPCHAIN_RECORDS] = {};
    bool record_busy[NATIVE12_SWAPCHAIN_RECORDS] = {};
    UINT record_head = 0, record_count = 0;
    SRWLOCK record_lock = SRWLOCK_INIT;
    HANDLE worker = NULL, worker_wake = NULL, worker_space = NULL, worker_idle = NULL, worker_stop = NULL;
    HANDLE worker_copy = NULL;
    ID3D12Fence *release_fences[NATIVE12_SWAPCHAIN_BUFFERS] = {};
    UINT64 release_values[NATIVE12_SWAPCHAIN_BUFFERS] = {};
    Native12ReleaseWait release_waits[NATIVE12_SWAPCHAIN_BUFFERS] = {};
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
        UnregisterAltEnter();
        StopWorker();
        if (!fullscreen_desc.Windowed) SetFullscreenState(FALSE, NULL);
        if (fullscreen_output) fullscreen_output->Release();
        RetirePublication();
        WaitForQueue();
        ReleaseForeign();
        ReleaseBuffers();
        for (UINT i = 0; i < NATIVE12_SWAPCHAIN_BUFFERS; ++i)
            if (release_fences[i]) release_fences[i]->Release();
        HANDLE handles[] = {worker_wake, worker_space, worker_idle, worker_stop, worker_copy};
        for (UINT i = 0; i < ARRAYSIZE(handles); ++i)
            if (handles[i]) CloseHandle(handles[i]);
        if (completion) CloseHandle(completion);
        if (latency) CloseHandle(latency);
        factory->Release();
        queue->Release();
    }

    HRESULT AllocateBuffers(const DXGI_SWAP_CHAIN_DESC1 &requested);
    void ReleaseBuffers();
    HRESULT ChangeMode(IDXGIOutput *output, const DXGI_MODE_DESC &requested);
    void RestoreMode();
    HRESULT SetupFullscreenWindow(int x, int y, int width, int height);
    void RestoreWindow(const RECT *rect);
    bool ForeignAdapter();
    HRESULT OpenForeignSurface(D3DKMT_HANDLE share);
    void CloseForeignSurface();
    HRESULT RecordReadBack(UINT slot, UINT buffer);
    HRESULT OpenRecordList(UINT slot);
    HRESULT PresentToDriver(UINT slot, UINT buffer);
    HRESULT WriteForeign(RECT *rect, ID3D12Resource *readback, UINT width_limit, UINT height_limit);
    HRESULT PrepareExchange(UINT slot, UINT buffer);
    HRESULT StartWorker();
    void StopWorker();
    HRESULT AcquireRecord(UINT *slot);
    void QueueRecord(UINT slot);
    void FreeRecord(UINT slot);
    void CompleteRecord(UINT slot);
    void ArmRelease(UINT buffer, UINT64 value, bool published);
    void WorkerLoop();
    static DWORD WINAPI WorkerMain(void *context)
    {
        static_cast<Native12SwapChain *>(context)->WorkerLoop();
        return 0;
    }
    HRESULT PublishForeign(UINT slot);
    void ReleaseForeign();
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
        if (!fullscreen && output) return DXGI_ERROR_INVALID_CALL;
        if (fullscreen && (static_cast<UINT>(fullscreen_desc.ScanlineOrdering) > DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST
                || static_cast<UINT>(fullscreen_desc.Scaling) > DXGI_MODE_SCALING_STRETCHED)) return DXGI_ERROR_INVALID_CALL;
        HRESULT hr = S_OK;
        if (output) output->AddRef();
        else if (FAILED(hr = GetContainingOutput(&output))) return hr;
        if (InterlockedExchange(&in_set_fullscreen_state, 1))
        {
            output->Release();
            return !fullscreen_desc.Windowed == !!fullscreen ? S_OK : DXGI_STATUS_MODE_CHANGE_IN_PROGRESS;
        }
        if (FAILED(hr = ApplyFullscreen(fullscreen, output)))
        {
            output->Release();
            hr = DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        }
        else
        {
            if (!fullscreen)
            {
                output->Release();
                output = NULL;
            }
            if (fullscreen_output) fullscreen_output->Release();
            fullscreen_output = output;
        }
        InterlockedExchange(&in_set_fullscreen_state, 0);
        return hr;
    }
    HRESULT ApplyFullscreen(BOOL fullscreen, IDXGIOutput *output)
    {
        DXGI_OUTPUT_DESC output_desc;
        HRESULT hr;
        if (desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH)
        {
            if (fullscreen)
            {
                DXGI_MODE_DESC mode = {};
                mode.Width = desc.Width;
                mode.Height = desc.Height;
                mode.RefreshRate = fullscreen_desc.RefreshRate;
                mode.Format = desc.Format;
                if (FAILED(hr = ChangeMode(output, mode))) return hr;
            }
            else RestoreMode();
        }
        if (fullscreen)
        {
            if (FAILED(hr = output->GetDesc(&output_desc))) return hr;
            const RECT &area = output_desc.DesktopCoordinates;
            if (fullscreen_desc.Windowed)
            {
                if (FAILED(hr = SetupFullscreenWindow(area.left, area.top, area.right - area.left, area.bottom - area.top)))
                    return hr;
                fullscreen_focused = GetForegroundWindow() == window;
            }
            else
            {
                MoveWindow(window, area.left, area.top, area.right - area.left, area.bottom - area.top, TRUE);
                ShowWindow(window, SW_SHOW);
            }
        }
        else if (!fullscreen_desc.Windowed) RestoreWindow(&windowed_rect);
        fullscreen_desc.Windowed = !fullscreen;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL *fullscreen, IDXGIOutput **output) override
    {
        if (fullscreen) *fullscreen = !fullscreen_desc.Windowed;
        if (output)
        {
            *output = NULL;
            if (!fullscreen_desc.Windowed)
            {
                if (!fullscreen_output)
                {
                    HRESULT hr = GetContainingOutput(&fullscreen_output);
                    if (FAILED(hr)) return hr;
                }
                fullscreen_output->AddRef();
                *output = fullscreen_output;
            }
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
        out->Windowed = fullscreen_desc.Windowed;
        out->SwapEffect = desc.SwapEffect;
        out->Flags = desc.Flags;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count, UINT width, UINT height, DXGI_FORMAT format,
            UINT flags) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC *mode) override
    {
        if (!mode) return DXGI_ERROR_INVALID_CALL;
        RECT current = {};
        GetWindowRect(window, &current);
        if (fullscreen_desc.Windowed)
        {
            RECT rect = {0, 0, static_cast<LONG>(mode->Width), static_cast<LONG>(mode->Height)};
            AdjustWindowRectEx(&rect, GetWindowLongW(window, GWL_STYLE), FALSE, GetWindowLongW(window, GWL_EXSTYLE));
            MoveWindow(window, current.left, current.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);
            return S_OK;
        }
        IDXGIOutput *output = NULL;
        DXGI_OUTPUT_DESC output_desc;
        HRESULT hr = GetContainingOutput(&output);
        if (SUCCEEDED(hr)) hr = output->GetDesc(&output_desc);
        bool moved = SUCCEEDED(hr) && (output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left != current.right - current.left
                || output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top != current.bottom - current.top);
        if (moved) windowed_rect = current;
        if (SUCCEEDED(hr) && (desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH))
        {
            hr = ChangeMode(output, *mode);
            if (SUCCEEDED(hr)) hr = output->GetDesc(&output_desc);
        }
        if (output) output->Release();
        if (FAILED(hr)) return hr;
        const RECT &area = output_desc.DesktopCoordinates;
        if (moved) return SetupFullscreenWindow(area.left, area.top, area.right - area.left, area.bottom - area.top);
        MoveWindow(window, area.left, area.top, area.right - area.left, area.bottom - area.top, TRUE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        if (fullscreen_output)
        {
            fullscreen_output->AddRef();
            *out = fullscreen_output;
            return S_OK;
        }
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
        AcquireSRWLockExclusive(&lock);
        HRESULT hr = width > desc.Width || height > desc.Height ? DXGI_ERROR_INVALID_CALL : S_OK;
        if (hr == S_OK)
        {
            source_width = width;
            source_height = height;
        }
        ReleaseSRWLockExclusive(&lock);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT *width, UINT *height) override
    {
        if (!width || !height) return E_INVALIDARG;
        AcquireSRWLockShared(&lock);
        *width = source_width;
        *height = source_height;
        ReleaseSRWLockShared(&lock);
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
            const UINT *node_masks, IUnknown *const *queues) override
    {
        UINT nodes = device->GetNodeCount();
        UINT used = count ? count : desc.BufferCount;
        if (!node_masks || !queues) return DXGI_ERROR_INVALID_CALL;
        for (UINT i = 0; i < used; ++i)
        {
            UINT mask = node_masks[i];
            if (!queues[i] || !mask || (mask & (mask - 1)) || (nodes < 32 && mask >= (1u << nodes)))
                return DXGI_ERROR_INVALID_CALL;
        }
        return ResizeBuffers(count, width, height, format, flags);
    }
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE type, UINT, void *) override
    {
        return type == DXGI_HDR_METADATA_TYPE_NONE ? S_OK : DXGI_ERROR_UNSUPPORTED;
    }
};

static bool Native12FlipFormat(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM
            || format == DXGI_FORMAT_R10G10B10A2_UNORM || format == DXGI_FORMAT_R16G16B16A16_FLOAT;
}

static UINT32 Native12Unorm8(UINT32 value, UINT32 max)
{
    return (value * 255 + max / 2) / max;
}

static float Native12HalfToFloat(USHORT half)
{
    UINT32 sign = static_cast<UINT32>(half & 0x8000) << 16, exponent = (half >> 10) & 0x1f, mantissa = half & 0x3ff;
    UINT32 bits;
    if (exponent == 0x1f) bits = sign | 0x7f800000 | (mantissa << 13);
    else if (exponent) bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    else if (!mantissa) bits = sign;
    else
    {
        exponent = 113;
        while (!(mantissa & 0x400))
        {
            mantissa <<= 1;
            --exponent;
        }
        bits = sign | (exponent << 23) | ((mantissa & 0x3ff) << 13);
    }
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static INIT_ONCE native12_srgb_once = INIT_ONCE_STATIC_INIT;
static BYTE native12_srgb_table[4096];

static BOOL CALLBACK Native12InitSrgbTable(INIT_ONCE *, void *, void **)
{
    for (UINT i = 0; i < ARRAYSIZE(native12_srgb_table); ++i)
    {
        double linear = i / 4095.0;
        double encoded = linear <= 0.0031308 ? linear * 12.92 : 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
        native12_srgb_table[i] = static_cast<BYTE>(encoded * 255.0 + 0.5);
    }
    return TRUE;
}

static UINT32 Native12LinearToSrgb8(float value)
{
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return native12_srgb_table[static_cast<UINT>(value * 4095.0f + 0.5f)];
}

static UINT32 Native12FloatToUnorm8(float value)
{
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<UINT32>(value * 255.0f + 0.5f);
}

/* TODO: color spaces other than the format default (SetColorSpace1) are not converted */
static UINT32 Native12ToBgra8(DXGI_FORMAT format, const BYTE *texel)
{
    UINT32 pixel;
    memcpy(&pixel, texel, sizeof(pixel));
    switch (format)
    {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return (pixel & 0xff00ff00) | ((pixel >> 16) & 0xff) | ((pixel & 0xff) << 16);
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return Native12Unorm8((pixel >> 20) & 0x3ff, 0x3ff) | Native12Unorm8((pixel >> 10) & 0x3ff, 0x3ff) << 8
                    | Native12Unorm8(pixel & 0x3ff, 0x3ff) << 16 | Native12Unorm8(pixel >> 30, 3) << 24;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        {
            USHORT channels[4];
            memcpy(channels, texel, sizeof(channels));
            return Native12LinearToSrgb8(Native12HalfToFloat(channels[2])) | Native12LinearToSrgb8(Native12HalfToFloat(channels[1])) << 8
                    | Native12LinearToSrgb8(Native12HalfToFloat(channels[0])) << 16 | Native12FloatToUnorm8(Native12HalfToFloat(channels[3])) << 24;
        }
        default:
            return pixel;
    }
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

HRESULT Native12SwapChain::ChangeMode(IDXGIOutput *output, const DXGI_MODE_DESC &requested)
{
    DXGI_OUTPUT_DESC output_desc;
    HRESULT hr = output->GetDesc(&output_desc);
    if (FAILED(hr)) return hr;
    DXGI_MODE_DESC closest = {};
    if (FAILED(output->FindClosestMatchingMode(&requested, &closest, NULL))) closest = requested;
    if (fullscreen_mode && lstrcmpiW(fullscreen_device, output_desc.DeviceName)) RestoreMode();
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    mode.dmPelsWidth = closest.Width;
    mode.dmPelsHeight = closest.Height;
    LONG changed = DISP_CHANGE_BADMODE;
    if (closest.RefreshRate.Numerator && closest.RefreshRate.Denominator)
    {
        mode.dmFields |= DM_DISPLAYFREQUENCY;
        mode.dmDisplayFrequency = (closest.RefreshRate.Numerator + closest.RefreshRate.Denominator / 2)
                / closest.RefreshRate.Denominator;
        changed = ChangeDisplaySettingsExW(output_desc.DeviceName, &mode, NULL, CDS_FULLSCREEN, NULL);
        mode.dmFields &= ~DM_DISPLAYFREQUENCY;
    }
    if (changed != DISP_CHANGE_SUCCESSFUL)
        changed = ChangeDisplaySettingsExW(output_desc.DeviceName, &mode, NULL, CDS_FULLSCREEN, NULL);
    if (changed != DISP_CHANGE_SUCCESSFUL) return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    lstrcpynW(fullscreen_device, output_desc.DeviceName, ARRAYSIZE(fullscreen_device));
    fullscreen_mode = true;
    return S_OK;
}

HRESULT Native12SwapChain::SetupFullscreenWindow(int x, int y, int width, int height)
{
    if (!IsWindow(window)) return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    Native12WindowState *state = new Native12WindowState;
    if (!state) return E_OUTOFMEMORY;
    windowed_style = GetWindowLongW(window, GWL_STYLE);
    windowed_exstyle = GetWindowLongW(window, GWL_EXSTYLE);
    fullscreen_layout = true;
    state->window = window;
    state->after = HWND_TOPMOST;
    state->x = x;
    state->y = y;
    state->width = width;
    state->height = height;
    state->flags = SWP_FRAMECHANGED | SWP_NOACTIVATE;
    state->style = Native12FullscreenStyle(windowed_style);
    state->exstyle = Native12FullscreenExStyle(windowed_exstyle);
    state->set_style = true;
    state->foreground = true;
    Native12SetWindowState(state);
    return S_OK;
}

void Native12SwapChain::RestoreWindow(const RECT *rect)
{
    if (!fullscreen_layout) return;
    fullscreen_layout = false;
    Native12WindowState *state = new Native12WindowState;
    if (!state) return;
    state->window = window;
    state->foreground = false;
    state->after = (windowed_exstyle & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST;
    state->flags = SWP_FRAMECHANGED | SWP_NOACTIVATE | ((windowed_style & WS_VISIBLE) ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
    LONG style = GetWindowLongW(window, GWL_STYLE);
    LONG exstyle = GetWindowLongW(window, GWL_EXSTYLE);
    windowed_style ^= (windowed_style ^ style) & WS_VISIBLE;
    windowed_exstyle ^= (windowed_exstyle ^ exstyle) & WS_EX_TOPMOST;
    state->style = windowed_style;
    state->exstyle = windowed_exstyle;
    state->set_style = style == Native12FullscreenStyle(windowed_style) && exstyle == Native12FullscreenExStyle(windowed_exstyle);
    if (rect)
    {
        state->x = rect->left;
        state->y = rect->top;
        state->width = rect->right - rect->left;
        state->height = rect->bottom - rect->top;
    }
    else
    {
        state->x = state->y = state->width = state->height = 0;
        state->flags |= SWP_NOMOVE | SWP_NOSIZE;
    }
    Native12SetWindowState(state);
}

static LRESULT CALLBACK Native12AltEnterProc(int code, WPARAM wparam, LPARAM lparam)
{
    const MSG *msg = reinterpret_cast<const MSG *>(lparam);
    if (code == HC_ACTION && wparam == PM_REMOVE && msg->message == WM_SYSKEYDOWN
            && msg->wParam == VK_RETURN && (msg->lParam & (KF_ALTDOWN << 16)))
    {
        Native12SwapChain *chain;
        AcquireSRWLockShared(&native12_alt_enter_lock);
        for (chain = native12_alt_enter_chains; chain; chain = chain->alt_enter_next)
            if (chain->window == msg->hwnd && chain->TryAddRef()) break;
        ReleaseSRWLockShared(&native12_alt_enter_lock);
        if (chain)
        {
            chain->ToggleFullscreen();
            chain->Release();
        }
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

void Native12SwapChain::RegisterAltEnter()
{
    DWORD process = 0;
    DWORD thread = GetWindowThreadProcessId(window, &process);
    if (!thread || process != GetCurrentProcessId()) return;
    AcquireSRWLockExclusive(&native12_alt_enter_lock);
    Native12AltEnterHook *hook;
    for (hook = native12_alt_enter_hooks; hook; hook = hook->next)
        if (hook->thread == thread) break;
    if (!hook && (hook = new Native12AltEnterHook))
    {
        hook->thread = thread;
        hook->users = 0;
        if ((hook->hook = SetWindowsHookExW(WH_GETMESSAGE, Native12AltEnterProc, NULL, thread)))
        {
            hook->next = native12_alt_enter_hooks;
            native12_alt_enter_hooks = hook;
        }
        else
        {
            delete hook;
            hook = NULL;
        }
    }
    if (hook)
    {
        ++hook->users;
        alt_enter_thread = thread;
        alt_enter_next = native12_alt_enter_chains;
        native12_alt_enter_chains = this;
        alt_enter_registered = true;
    }
    ReleaseSRWLockExclusive(&native12_alt_enter_lock);
}

void Native12SwapChain::UnregisterAltEnter()
{
    if (!alt_enter_registered) return;
    HHOOK unhook = NULL;
    AcquireSRWLockExclusive(&native12_alt_enter_lock);
    for (Native12SwapChain **link = &native12_alt_enter_chains; *link; link = &(*link)->alt_enter_next)
    {
        if (*link != this) continue;
        *link = alt_enter_next;
        break;
    }
    for (Native12AltEnterHook **link = &native12_alt_enter_hooks; *link; link = &(*link)->next)
    {
        Native12AltEnterHook *hook = *link;
        if (hook->thread != alt_enter_thread) continue;
        if (!--hook->users)
        {
            *link = hook->next;
            unhook = hook->hook;
            delete hook;
        }
        break;
    }
    alt_enter_registered = false;
    ReleaseSRWLockExclusive(&native12_alt_enter_lock);
    if (unhook) UnhookWindowsHookEx(unhook);
}

void Native12SwapChain::ToggleFullscreen()
{
    typedef BOOL (WINAPI *PFN_ASSOCIATION)(IDXGIFactory *, HWND, UINT *);
    static PFN_ASSOCIATION association;
    if (!association)
    {
        HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
        if (dxgi) association = reinterpret_cast<PFN_ASSOCIATION>(GetProcAddress(dxgi, "RosDxgiGetWindowAssociation"));
    }
    UINT flags = 0;
    if (association && association(factory, window, &flags)
            && (flags & (DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES))) return;
    SetFullscreenState(fullscreen_desc.Windowed, NULL);
}

void Native12SwapChain::RestoreMode()
{
    if (!fullscreen_mode) return;
    ChangeDisplaySettingsExW(fullscreen_device, NULL, NULL, 0, NULL);
    fullscreen_mode = false;
}

bool Native12SwapChain::ForeignAdapter()
{
    if (foreign_known) return foreign;
    foreign_known = true;
    foreign = false;
    DXGI_ADAPTER_DESC adapter_desc;
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    if (FAILED(device->adapter->GetDesc(&adapter_desc)) || !monitor
            || !GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO *>(&info))) return false;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME open = {};
    lstrcpynW(open.DeviceName, info.szDevice, ARRAYSIZE(open.DeviceName));
    if (D3DKMTOpenAdapterFromGdiDisplayName(&open) < 0) return false;
    D3DKMT_CLOSEADAPTER close = {};
    close.hAdapter = open.hAdapter;
    D3DKMTCloseAdapter(&close);
    foreign = open.AdapterLuid.LowPart != adapter_desc.AdapterLuid.LowPart
            || open.AdapterLuid.HighPart != adapter_desc.AdapterLuid.HighPart;
    return foreign;
}

HRESULT Native12SwapChain::OpenForeignSurface(D3DKMT_HANDLE share)
{
    DWM_DX_SHARED_SURFACE_INFO info = {};
    D3DKMT_QUERYRESOURCEINFO query = {};
    query.hDevice = device->km_device;
    query.hGlobalShare = share;
    query.pPrivateRuntimeData = &info;
    query.PrivateRuntimeDataSize = sizeof(info);
    if (D3DKMTQueryResourceInfo(&query) < 0 || query.NumAllocations != 1 || query.PrivateRuntimeDataSize != sizeof(info)
            || info.Magic != DWM_DX_SURFACE_INFO_MAGIC || info.Version != DWM_DX_SURFACE_INFO_VERSION
            || info.Format != DWM_DX_FORMAT_B8G8R8A8_UNORM || !info.Width || !info.Height
            || info.Pitch / sizeof(UINT32) < info.Width) return DXGI_ERROR_UNSUPPORTED;
    void *resource_private = query.ResourcePrivateDriverDataSize
            ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, query.ResourcePrivateDriverDataSize) : NULL;
    void *total_private = query.TotalPrivateDriverDataSize
            ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, query.TotalPrivateDriverDataSize) : NULL;
    HRESULT hr = S_OK;
    if ((query.ResourcePrivateDriverDataSize && !resource_private) || (query.TotalPrivateDriverDataSize && !total_private))
        hr = E_OUTOFMEMORY;
    D3DDDI_OPENALLOCATIONINFO allocation = {};
    D3DKMT_OPENRESOURCE open = {};
    if (SUCCEEDED(hr))
    {
        open.hDevice = device->km_device;
        open.hGlobalShare = share;
        open.NumAllocations = 1;
        open.pOpenAllocationInfo = &allocation;
        open.pPrivateRuntimeData = &info;
        open.PrivateRuntimeDataSize = sizeof(info);
        open.pResourcePrivateDriverData = resource_private;
        open.ResourcePrivateDriverDataSize = query.ResourcePrivateDriverDataSize;
        open.pTotalPrivateDriverDataBuffer = total_private;
        open.TotalPrivateDriverDataBufferSize = query.TotalPrivateDriverDataSize;
        hr = Native12StatusToHresult(D3DKMTOpenResource(&open));
        if (SUCCEEDED(hr) && (!open.hResource || !allocation.hAllocation)) hr = E_FAIL;
    }
    if (resource_private) HeapFree(GetProcessHeap(), 0, resource_private);
    if (total_private) HeapFree(GetProcessHeap(), 0, total_private);
    if (FAILED(hr)) return hr;
    foreign_share = share;
    foreign_resource = open.hResource;
    foreign_allocation = allocation.hAllocation;
    foreign_width = info.Width;
    foreign_height = info.Height;
    foreign_pitch = info.Pitch;
    return S_OK;
}

void Native12SwapChain::CloseForeignSurface()
{
    if (foreign_resource)
    {
        D3DKMT_DESTROYALLOCATION destroy = {};
        destroy.hDevice = device->km_device;
        destroy.hResource = foreign_resource;
        D3DKMTDestroyAllocation(&destroy);
    }
    foreign_share = 0;
    foreign_resource = 0;
    foreign_allocation = 0;
}

HRESULT Native12SwapChain::OpenRecordList(UINT slot)
{
    ID3D12CommandAllocator *&allocator = record_allocator[slot];
    ID3D12GraphicsCommandList *&list = record_list[slot];
    HRESULT hr;

    if (!allocator && FAILED(hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator), reinterpret_cast<void **>(&allocator)))) return hr;
    if (!list)
        return device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void **>(&list));
    if (FAILED(hr = allocator->Reset())) return hr;
    return list->Reset(allocator, NULL);
}

/* A driver may render into memory the shared allocation does not name; its Present brings that allocation up to date. */
HRESULT Native12SwapChain::PresentToDriver(UINT slot, UINT buffer)
{
    D3D12DDI_ARG_PRESENTSURFACE surface = {};
    D3D12DDIARG_PRESENT_0001 args = {};
    D3D12DDI_PRESENT_0051 output = {};
    D3D12DDI_PRESENT_CONTEXTS_0051 contexts = {};
    D3D12DDI_PRESENT_HWQUEUES_0051 hw_queues = {};
    HRESULT hr;

    if (!device->list_functions.pfnPresent) return S_OK;
    if (FAILED(hr = OpenRecordList(slot))) return hr;
    surface.hSurface = static_cast<Native12Resource *>(buffers[buffer])->driver;
    args.phSurfacesToPresent = &surface;
    args.SurfacesToPresent = 1;
    args.Flags.Blt = 1;
    if (device->functions.pfnGetPresentPrivateDriverDataSize)
        args.PrivateDriverDataSize = device->functions.pfnGetPresentPrivateDriverDataSize(device->driver_device, &args);
    if (args.PrivateDriverDataSize
            && !(args.pPrivateDriverData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, args.PrivateDriverDataSize)))
    {
        record_list[slot]->Close();
        return E_OUTOFMEMORY;
    }
    device->list_functions.pfnPresent(static_cast<Native12CommandList *>(record_list[slot])->driver,
            queue->driver, &args, &output, &contexts, &hw_queues);
    HeapFree(GetProcessHeap(), 0, args.pPrivateDriverData);
    if (FAILED(hr = record_list[slot]->Close())) return hr;
    if (output.AddedGpuWork)
    {
        ID3D12CommandList *lists[] = {record_list[slot]};
        queue->ExecuteCommandLists(1, lists);
    }
    return S_OK;
}

HRESULT Native12SwapChain::RecordReadBack(UINT slot, UINT buffer)
{
    ID3D12Resource *source = buffers[buffer];
    if (!source) return DXGI_ERROR_INVALID_CALL;
    D3D12_RESOURCE_DESC source_desc = source->GetDesc();
    HRESULT hr;
    if (readback_layout.Footprint.Width != source_desc.Width || readback_layout.Footprint.Height != source_desc.Height
            || readback_layout.Footprint.Format != source_desc.Format)
        device->GetCopyableFootprints(&source_desc, 0, 1, 0, &readback_layout, NULL, NULL, &readback_size);
    ID3D12Resource *&readback = record_readback[slot];
    if (readback && readback->GetDesc().Width != readback_size)
    {
        readback->Release();
        readback = NULL;
    }
    if (!readback)
    {
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer_desc = {};
        buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer_desc.Width = readback_size;
        buffer_desc.Height = 1;
        buffer_desc.DepthOrArraySize = 1;
        buffer_desc.MipLevels = 1;
        buffer_desc.SampleDesc.Count = 1;
        buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                D3D12_RESOURCE_STATE_COPY_DEST, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&readback))))
            return hr;
    }
    if (FAILED(hr = OpenRecordList(slot))) return hr;
    ID3D12GraphicsCommandList *list = record_list[slot];
    if (!copy_fence && FAILED(hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
            reinterpret_cast<void **>(&copy_fence)))) return hr;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = source;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION destination = {}, origin = {};
    destination.pResource = readback;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = readback_layout;
    origin.pResource = source;
    origin.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&destination, 0, 0, 0, &origin, NULL);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    list->ResourceBarrier(1, &barrier);
    if (FAILED(hr = list->Close())) return hr;
    ID3D12CommandList *lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    if (FAILED(hr = queue->Signal(copy_fence, ++copy_value))) return hr;
    records[slot].copy_value = copy_value;
    return S_OK;
}

HRESULT Native12SwapChain::WriteForeign(RECT *rect, ID3D12Resource *readback, UINT width_limit, UINT height_limit)
{
    D3DKMT_LOCK lock_args = {};
    lock_args.hDevice = device->km_device;
    lock_args.hAllocation = foreign_allocation;
    lock_args.Flags.WriteOnly = 1;
    lock_args.Flags.LockEntire = 1;
    if (D3DKMTLock(&lock_args) < 0 || !lock_args.pData) return DXGI_ERROR_UNSUPPORTED;
    void *mapped = NULL;
    D3D12_RANGE range = {0, static_cast<SIZE_T>(readback_size)};
    HRESULT hr = readback->Map(0, &range, &mapped);
    if (SUCCEEDED(hr))
    {
        const BYTE *source = static_cast<const BYTE *>(mapped) + readback_layout.Offset;
        BYTE *target = static_cast<BYTE *>(lock_args.pData);
        UINT width = readback_layout.Footprint.Width, height = readback_layout.Footprint.Height;
        DXGI_FORMAT format = readback_layout.Footprint.Format;
        UINT texel_size = format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
        if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) InitOnceExecuteOnce(&native12_srgb_once, Native12InitSrgbTable, NULL, NULL);
        /* FIXME: a zero-size source region presents the whole buffer; Windows fills the window with one texel */
        if (width_limit && height_limit && (width_limit < width || height_limit < height))
        {
            width = width_limit;
            height = height_limit;
        }
        for (UINT y = 0; y < foreign_height; ++y)
        {
            const BYTE *row = source + static_cast<UINT64>(y) * height / foreign_height * readback_layout.Footprint.RowPitch;
            UINT32 *out = reinterpret_cast<UINT32 *>(target + static_cast<SIZE_T>(y) * foreign_pitch);
            for (UINT x = 0; x < foreign_width; ++x)
                out[x] = Native12ToBgra8(format, row + static_cast<UINT64>(x) * width / foreign_width * texel_size);
        }
        D3D12_RANGE written = {0, 0};
        readback->Unmap(0, &written);
    }
    D3DKMT_UNLOCK unlock = {};
    unlock.hDevice = device->km_device;
    unlock.NumAllocations = 1;
    unlock.phAllocations = &foreign_allocation;
    D3DKMTUnlock(&unlock);
    if (SUCCEEDED(hr)) SetRect(rect, 0, 0, foreign_width, foreign_height);
    return hr;
}

HRESULT Native12SwapChain::PublishForeign(UINT slot)
{
    typedef HRESULT (WINAPI *GetSurface)(HWND, LUID, HMONITOR, DWORD, UINT *, HANDLE *, ULONGLONG *);
    typedef HRESULT (WINAPI *UpdateSurface)(HWND, ULONGLONG, DWORD, HMONITOR, const RECT *);
    static GetSurface get_surface;
    static UpdateSurface update_surface;
    if (!get_surface || !update_surface)
    {
        HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
        if (!dwmapi) return HRESULT_FROM_WIN32(GetLastError());
        get_surface = reinterpret_cast<GetSurface>(GetProcAddress(dwmapi, reinterpret_cast<LPCSTR>(static_cast<ULONG_PTR>(100))));
        update_surface = reinterpret_cast<UpdateSurface>(GetProcAddress(dwmapi, reinterpret_cast<LPCSTR>(static_cast<ULONG_PTR>(101))));
        if (!get_surface || !update_surface) return DXGI_ERROR_UNSUPPORTED;
    }
    if (!Native12FlipFormat(desc.Format)) return DXGI_ERROR_UNSUPPORTED;
    if (IsIconic(window)) return S_OK;
    DXGI_ADAPTER_DESC adapter_desc;
    HRESULT hr = device->adapter->GetDesc(&adapter_desc);
    if (FAILED(hr)) return hr;
    UINT format = 0;
    HANDLE shared = NULL;
    ULONGLONG update = 0;
    hr = get_surface(window, adapter_desc.AdapterLuid, NULL, 0, &format, &shared, &update);
    /* FIXME: DWM composes only visible windows of the presenting process; it drops frames for the others */
    if (hr == S_FALSE || hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
            || hr == HRESULT_FROM_WIN32(ERROR_NOT_READY)) return S_OK;
    if (FAILED(hr)) return hr;
    D3DKMT_HANDLE share = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(shared));
    if (share != foreign_share)
    {
        CloseForeignSurface();
        hr = OpenForeignSurface(share);
    }
    RECT rect = {};
    if (SUCCEEDED(hr)) hr = WriteForeign(&rect, record_readback[slot], records[slot].source_width, records[slot].source_height);
    HRESULT update_hr = update_surface(window, update, FAILED(hr) ? DWM_DX_UPDATE_CANCEL : 0, NULL, FAILED(hr) ? NULL : &rect);
    return FAILED(hr) ? hr : update_hr;
}

void Native12SwapChain::ReleaseForeign()
{
    CloseForeignSurface();
    for (UINT i = 0; i < NATIVE12_SWAPCHAIN_RECORDS; ++i)
    {
        if (record_readback[i]) record_readback[i]->Release();
        if (record_list[i]) record_list[i]->Release();
        if (record_allocator[i]) record_allocator[i]->Release();
        if (record_events[i]) CloseHandle(record_events[i]);
        record_readback[i] = NULL;
        record_list[i] = NULL;
        record_allocator[i] = NULL;
        record_events[i] = NULL;
    }
    if (copy_fence) copy_fence->Release();
    copy_fence = NULL;
}

HRESULT Native12SwapChain::AllocateBuffers(const DXGI_SWAP_CHAIN_DESC1 &requested)
{
    foreign_known = false;
    Native12Resource *created[NATIVE12_SWAPCHAIN_BUFFERS] = {};
    D3DKMT_HANDLE created_shares[NATIVE12_SWAPCHAIN_BUFFERS] = {};
    HANDLE created_events[NATIVE12_SWAPCHAIN_BUFFERS] = {};
    HRESULT hr = S_OK;

    if (requested.BufferCount < 2 || requested.BufferCount > NATIVE12_SWAPCHAIN_BUFFERS) return DXGI_ERROR_INVALID_CALL;
    if (requested.SampleDesc.Count != 1 || requested.Stereo) return DXGI_ERROR_INVALID_CALL;
    if (requested.SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL && requested.SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD)
        return DXGI_ERROR_INVALID_CALL;
    if (!Native12FlipFormat(requested.Format)) return DXGI_ERROR_INVALID_CALL;
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
    source_width = desc.Width;
    source_height = desc.Height;
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

HRESULT Native12SwapChain::PrepareExchange(UINT slot, UINT buffer)
{
    Native12PresentRecord &record = records[slot];
    DXGI_ADAPTER_DESC adapter_desc;
    HRESULT hr = device->adapter->GetDesc(&adapter_desc);
    if (FAILED(hr)) return hr;
    DWM_DX_SURFACE_EXCHANGE &exchange = record.exchange;
    exchange.StructSize = sizeof(exchange);
    exchange.Action = DWM_DX_SURFACE_PUBLISH;
    exchange.Window = reinterpret_cast<ULONG_PTR>(window);
    exchange.AdapterLuid = adapter_desc.AdapterLuid;
    exchange.GlobalShare = shares[buffer];
    exchange.Info.Magic = DWM_DX_SURFACE_INFO_MAGIC;
    exchange.Info.Version = DWM_DX_SURFACE_INFO_VERSION_GPU;
    exchange.Info.Width = desc.Width;
    exchange.Info.Height = desc.Height;
    exchange.Info.Format = desc.Format;
    exchange.Flags = DWM_DX_PUBLISH_RETAINED;
    if (desc.AlphaMode == DXGI_ALPHA_MODE_PREMULTIPLIED) exchange.Flags |= DWM_DX_PUBLISH_PREMULTIPLIED;
    /* FIXME: a zero-size source region presents the whole buffer; Windows fills the window with one texel */
    if (source_width && source_height && (source_width < desc.Width || source_height < desc.Height))
    {
        exchange.Flags |= DWM_DX_PUBLISH_SOURCE_REGION;
        exchange.SourceWidth = source_width;
        exchange.SourceHeight = source_height;
    }
    exchange.ReadyEvent = reinterpret_cast<ULONG_PTR>(release_events[buffer]);
    exchange.UpdateRect.right = desc.Width;
    exchange.UpdateRect.bottom = desc.Height;
    if (!release_fences[buffer] && FAILED(hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
            reinterpret_cast<void **>(&release_fences[buffer])))) return hr;
    if (!record_events[slot] && !(record_events[slot] = CreateEventW(NULL, FALSE, FALSE, NULL)))
        return HRESULT_FROM_WIN32(GetLastError());
    if (FAILED(hr = device->enqueue_set_event(device->runtime_device, record_events[slot]))) return hr;
    record.release_value = ++release_values[buffer];
    return S_OK;
}

HRESULT Native12SwapChain::StartWorker()
{
    if (worker) return S_OK;
    if (!worker_wake) worker_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!worker_space) worker_space = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!worker_idle) worker_idle = CreateEventW(NULL, TRUE, TRUE, NULL);
    if (!worker_stop) worker_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!worker_copy) worker_copy = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!worker_wake || !worker_space || !worker_idle || !worker_stop || !worker_copy)
        return HRESULT_FROM_WIN32(GetLastError());
    if (!(worker = CreateThread(NULL, 0, WorkerMain, this, 0, NULL))) return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

/* Lets the queued frames retire, or drops those whose GPU work does not retire in time. */
void Native12SwapChain::StopWorker()
{
    if (!worker) return;
    if (WaitForSingleObject(worker_idle, NATIVE12_SWAPCHAIN_WAIT) != WAIT_OBJECT_0)
        WARN("Dropping %u queued frames.\n", record_count);
    SetEvent(worker_stop);
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
    worker = NULL;
    ResetEvent(worker_stop);
    for (UINT i = 0; i < NATIVE12_SWAPCHAIN_BUFFERS; ++i)
    {
        if (release_waits[i].wait) UnregisterWaitEx(release_waits[i].wait, INVALID_HANDLE_VALUE);
        release_waits[i].wait = NULL;
        if (release_fences[i]) release_fences[i]->Signal(release_values[i]);
    }
}

HRESULT Native12SwapChain::AcquireRecord(UINT *slot)
{
    for (;;)
    {
        AcquireSRWLockExclusive(&record_lock);
        for (UINT i = 0; i < NATIVE12_SWAPCHAIN_RECORDS; ++i)
        {
            if (record_busy[i]) continue;
            record_busy[i] = true;
            ReleaseSRWLockExclusive(&record_lock);
            ZeroMemory(&records[i], sizeof(records[i]));
            *slot = i;
            return S_OK;
        }
        ReleaseSRWLockExclusive(&record_lock);
        if (WaitForSingleObject(worker_space, NATIVE12_SWAPCHAIN_WAIT) != WAIT_OBJECT_0)
            return DXGI_ERROR_WAS_STILL_DRAWING;
    }
}

void Native12SwapChain::QueueRecord(UINT slot)
{
    AcquireSRWLockExclusive(&record_lock);
    record_order[(record_head + record_count) % NATIVE12_SWAPCHAIN_RECORDS] = slot;
    ++record_count;
    ResetEvent(worker_idle);
    ReleaseSRWLockExclusive(&record_lock);
    SetEvent(worker_wake);
}

void Native12SwapChain::FreeRecord(UINT slot)
{
    AcquireSRWLockExclusive(&record_lock);
    record_busy[slot] = false;
    ReleaseSRWLockExclusive(&record_lock);
    SetEvent(worker_space);
}

/* A queue that renders into a buffer again first waits until DWM no longer reads its last publication. */
void Native12SwapChain::ArmRelease(UINT buffer, UINT64 value, bool published)
{
    Native12ReleaseWait &wait = release_waits[buffer];
    if (wait.wait) UnregisterWaitEx(wait.wait, INVALID_HANDLE_VALUE);
    wait.wait = NULL;
    wait.fence = release_fences[buffer];
    wait.value = value;
    if (published && RegisterWaitForSingleObject(&wait.wait, release_events[buffer], Native12ReleaseCallback, &wait,
            INFINITE, WT_EXECUTEONLYONCE))
        return;
    wait.wait = NULL;
    wait.fence->Signal(value);
}

/* Runs on the worker thread, which never takes the swap chain lock. */
void Native12SwapChain::CompleteRecord(UINT slot)
{
    Native12PresentRecord &record = records[slot];
    HANDLE done = record.foreign ? worker_copy : record_events[slot];
    if (record.foreign && FAILED(copy_fence->SetEventOnCompletion(record.copy_value, worker_copy))) done = NULL;
    HANDLE handles[2] = {worker_stop, done};
    bool completed = done && WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1;
    if (record.foreign)
    {
        HRESULT hr = completed ? PublishForeign(slot) : S_OK;
        if (FAILED(hr)) WARN("Cross-adapter frame was not published, hr %#lx.\n", hr);
    }
    else
    {
        HANDLE release = release_events[record.buffer];
        NTSTATUS status = STATUS_CANCELLED;
        if (completed)
        {
            ResetEvent(release);
            status = static_cast<NTSTATUS>(NtUserCallOneParam(reinterpret_cast<DWORD_PTR>(&record.exchange),
                    DWM_ROUTINE_DXSURFACE));
            if (status >= 0) publication = record.exchange;
            else
            {
                SetEvent(release);
                WARN("Frame was not published, status %#lx.\n", status);
            }
        }
        ArmRelease(record.buffer, record.release_value, completed && status >= 0);
    }
    if (latency) ReleaseSemaphore(latency, 1, NULL);
}

void Native12SwapChain::WorkerLoop()
{
    HANDLE wake[2] = {worker_stop, worker_wake};
    for (;;)
    {
        UINT slot = 0;
        AcquireSRWLockExclusive(&record_lock);
        bool queued = record_count != 0;
        if (queued) slot = record_order[record_head];
        else SetEvent(worker_idle);
        ReleaseSRWLockExclusive(&record_lock);
        if (!queued)
        {
            if (WaitForMultipleObjects(2, wake, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) return;
            continue;
        }
        CompleteRecord(slot);
        AcquireSRWLockExclusive(&record_lock);
        record_head = (record_head + 1) % NATIVE12_SWAPCHAIN_RECORDS;
        --record_count;
        record_busy[slot] = false;
        ReleaseSRWLockExclusive(&record_lock);
        SetEvent(worker_space);
    }
}

HRESULT STDMETHODCALLTYPE Native12SwapChain::Present1(UINT interval, UINT flags,
        const DXGI_PRESENT_PARAMETERS *)
{
    if (interval > 4) return DXGI_ERROR_INVALID_CALL;
    if (FocusLost())
    {
        if (!(flags & DXGI_PRESENT_TEST)) SetFullscreenState(FALSE, NULL);
        return DXGI_STATUS_OCCLUDED;
    }
    if (flags & DXGI_PRESENT_TEST) return S_OK;
    if (FAILED(device->removed_reason)) return DXGI_ERROR_DEVICE_REMOVED;

    AcquireSRWLockExclusive(&lock);
    UINT slot = 0;
    HRESULT hr = StartWorker();
    if (SUCCEEDED(hr)) hr = AcquireRecord(&slot);
    if (SUCCEEDED(hr))
    {
        Native12PresentRecord &record = records[slot];
        record.buffer = current;
        record.source_width = source_width;
        record.source_height = source_height;
        record.foreign = ForeignAdapter();
        hr = record.foreign ? RecordReadBack(slot, current) : PresentToDriver(slot, current);
        if (SUCCEEDED(hr) && !record.foreign) hr = PrepareExchange(slot, current);
        if (SUCCEEDED(hr)) QueueRecord(slot);
        else FreeRecord(slot);
    }
    if (SUCCEEDED(hr))
    {
        ++present_count;
        current = (current + 1) % desc.BufferCount;
        if (release_values[current]) hr = queue->Wait(release_fences[current], release_values[current]);
    }
    ReleaseSRWLockExclusive(&lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12SwapChain::ResizeBuffers(UINT count, UINT width, UINT height,
        DXGI_FORMAT format, UINT flags)
{
    AcquireSRWLockExclusive(&lock);
    StopWorker();
    for (UINT i = 0; i < desc.BufferCount; ++i)
    {
        if (buffers[i] && buffers[i]->references != 1)
        {
            ReleaseSRWLockExclusive(&lock);
            return DXGI_ERROR_INVALID_CALL;
        }
    }
    HRESULT hr = Native12ResizeFlags(desc.Flags, flags);
    if (FAILED(hr))
    {
        ReleaseSRWLockExclusive(&lock);
        return hr;
    }
    DXGI_SWAP_CHAIN_DESC1 requested = desc;
    if (count) requested.BufferCount = count;
    if (format != DXGI_FORMAT_UNKNOWN) requested.Format = format;
    requested.Flags = flags;
    RECT client = {};
    GetClientRect(window, &client);
    requested.Width = width ? width : max(1, client.right - client.left);
    requested.Height = height ? height : max(1, client.bottom - client.top);
    hr = AllocateBuffers(requested);
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
    if (desc->Flags & (DXGI_SWAP_CHAIN_FLAG_NONPREROTATED | DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE
            | DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT | DXGI_SWAP_CHAIN_FLAG_RESTRICT_SHARED_RESOURCE_DRIVER
            | DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY | DXGI_SWAP_CHAIN_FLAG_FOREGROUND_LAYER
            | DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO | DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO)) return DXGI_ERROR_INVALID_CALL;

    Native12SwapChain *swapchain = new Native12SwapChain(this, factory, window);
    if (!swapchain) return E_OUTOFMEMORY;
    if (fullscreen_desc) swapchain->fullscreen_desc = *fullscreen_desc;
    swapchain->fullscreen_desc.Windowed = TRUE;
    GetWindowRect(window, &swapchain->windowed_rect);
    DXGI_SWAP_CHAIN_DESC1 requested = *desc;
    RECT client = {};
    GetClientRect(window, &client);
    if (fullscreen_desc && !fullscreen_desc->Windowed)
    {
        MONITORINFO monitor = {};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor)) client = monitor.rcMonitor;
    }
    if (!requested.Width) requested.Width = client.right - client.left > 0 ? client.right - client.left : 8;
    if (!requested.Height) requested.Height = client.bottom - client.top > 0 ? client.bottom - client.top : 8;
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
    swapchain->RegisterAltEnter();
    *out = swapchain;
    return S_OK;
}
