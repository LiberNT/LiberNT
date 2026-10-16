/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 fences, command queues and command allocators
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "native12.h"
#include "native12_device.h"
#include "native12_objects.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d12);

D3D12DDI_COMMAND_QUEUE_FLAGS Native12QueueFlags(D3D12_COMMAND_LIST_TYPE type)
{
    switch (type)
    {
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            return static_cast<D3D12DDI_COMMAND_QUEUE_FLAGS>(
                    D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE | D3D12DDI_COMMAND_QUEUE_FLAG_COPY);
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return D3D12DDI_COMMAND_QUEUE_FLAG_COPY;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
            return D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_DECODE;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
            return D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_PROCESS;
        default:
            return static_cast<D3D12DDI_COMMAND_QUEUE_FLAGS>(D3D12DDI_COMMAND_QUEUE_FLAG_3D
                    | D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE | D3D12DDI_COMMAND_QUEUE_FLAG_COPY);
    }
}

Native12Device *Native12QueueDevice(HANDLE queue)
{
    return queue ? static_cast<Native12CommandQueue *>(queue)->device : NULL;
}

HRESULT Native12Fence::Initialize(UINT64 initial_value, D3D12_FENCE_FLAGS creation_flags)
{
    flags = creation_flags;
    if (!device->kernel_callbacks.pfnCreateSynchronizationObject2Cb) return E_NOINTERFACE;
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 create = {};
    create.Info.Type = D3DDDI_MONITORED_FENCE;
    create.Info.MonitoredFence.InitialFenceValue = initial_value;
    create.Info.MonitoredFence.EngineAffinity = 1;
    HRESULT hr = device->kernel_callbacks.pfnCreateSynchronizationObject2Cb(device->runtime_device, &create);
    if (FAILED(hr))
    {
        WARN("Failed to create a monitored fence, hr %#lx.\n", hr);
        return hr;
    }
    sync = create.hSyncObject;
    value = static_cast<const volatile UINT64 *>(create.Info.MonitoredFence.FenceValueCPUVirtualAddress);
    if (!value) return E_FAIL;

    D3D12DDI_FENCE info = {};
    info.FenceValue.BaseAddress = create.Info.MonitoredFence.FenceValueGPUVirtualAddress;
    D3D12DDIARG_CREATE_FENCE args = {};
    args.FenceCount = 1;
    args.Fences = &info;
    SIZE_T size = device->functions.pfnCalcPrivateFenceSize(device->driver_device, &args);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!driver.pDrvPrivate) return E_OUTOFMEMORY;
    if (FAILED(hr = device->functions.pfnCreateFence(device->driver_device, driver, &args)))
    {
        WARN("The driver failed to create a fence at %#I64x, hr %#lx.\n", info.FenceValue.BaseAddress, hr);
        return hr;
    }
    driver_created = true;
    TRACE("Fence %p, sync %#x, cpu %p, gpu %#I64x.\n", this, sync, value, info.FenceValue.BaseAddress);
    return S_OK;
}

Native12Fence::~Native12Fence()
{
    if (driver_created) device->functions.pfnDestroyFence(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
    if (sync && device->kernel_callbacks.pfnDestroySynchronizationObjectCb)
    {
        D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT destroy = {};
        destroy.hSyncObject = sync;
        device->kernel_callbacks.pfnDestroySynchronizationObjectCb(device->runtime_device, &destroy);
    }
}

HRESULT STDMETHODCALLTYPE Native12Fence::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12Fence) || IsEqualGUID(iid, IID_ID3D12Fence1)
            || IsEqualGUID(iid, IID_ID3D12Pageable) || IsEqualGUID(iid, IID_ID3D12DeviceChild)
            || IsEqualGUID(iid, IID_ID3D12Object) || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12Fence1 *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

D3D12_FENCE_FLAGS STDMETHODCALLTYPE Native12Fence::GetCreationFlags()
{
    return flags;
}

UINT64 STDMETHODCALLTYPE Native12Fence::GetCompletedValue()
{
    return *value;
}

HRESULT STDMETHODCALLTYPE Native12Fence::SetEventOnCompletion(UINT64 fence_value, HANDLE event)
{
    if (!device->kernel_callbacks.pfnWaitForSynchronizationObjectFromCpuCb) return E_NOINTERFACE;
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait = {};
    wait.ObjectCount = 1;
    wait.ObjectHandleArray = &sync;
    wait.FenceValueArray = &fence_value;
    wait.hAsyncEvent = event;
    return device->kernel_callbacks.pfnWaitForSynchronizationObjectFromCpuCb(device->runtime_device, &wait);
}

HRESULT STDMETHODCALLTYPE Native12Fence::Signal(UINT64 fence_value)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU signal = {};
    signal.hDevice = device->km_device;
    signal.ObjectCount = 1;
    signal.ObjectHandleArray = &sync;
    signal.FenceValueArray = &fence_value;
    signal.Flags.AllowFenceRewind = 1;
    return Native12StatusToHresult(D3DKMTSignalSynchronizationObjectFromCpu(&signal));
}

HRESULT Native12CommandQueue::Initialize(const D3D12_COMMAND_QUEUE_DESC *input)
{
    desc = *input;
    if (!desc.NodeMask) desc.NodeMask = 1;
    if (desc.Type == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE && (!device->EnsureVideo() || !device->video_decode_engine))
        return E_INVALIDARG;
    if (desc.Type == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS && !device->EnsureVideoProcess())
        return E_INVALIDARG;
    if (desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT && desc.Type != D3D12_COMMAND_LIST_TYPE_COMPUTE
            && desc.Type != D3D12_COMMAND_LIST_TYPE_COPY && desc.Type != D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE
            && desc.Type != D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS)
        return E_INVALIDARG;
    D3D12DDIARG_CREATECOMMANDQUEUE_0050 args = {};
    args.QueueFlags = Native12QueueFlags(desc.Type);
    args.NodeMask = desc.NodeMask ? desc.NodeMask : 1;
    if (desc.Priority == D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME)
        args.QueueCreationFlags = D3D12DDI_COMMAND_QUEUE_CREATION_FLAG_GLOBAL_REALTIME_PRIORITY;
    SIZE_T size = device->functions.pfnCalcPrivateCommandQueueSize(device->driver_device, &args);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!driver.pDrvPrivate) return E_OUTOFMEMORY;
    D3D12DDI_HRTCOMMANDQUEUE runtime = {};
    runtime.handle = this;
    AcquireSRWLockExclusive(&device->queue_lock);
    next_queue = device->queues;
    device->queues = this;
    listed = true;
    ReleaseSRWLockExclusive(&device->queue_lock);
    HRESULT hr = device->functions.pfnCreateCommandQueue(device->driver_device, &args, driver, runtime);
    if (FAILED(hr))
    {
        WARN("The driver failed to create a command queue of type %u, hr %#lx.\n", desc.Type, hr);
        return hr;
    }
    driver_created = true;
    return S_OK;
}

Native12CommandQueue::~Native12CommandQueue()
{
    if (driver_created) device->functions.pfnDestroyCommandQueue(device->driver_device, driver);
    if (listed)
    {
        AcquireSRWLockExclusive(&device->queue_lock);
        for (Native12CommandQueue **link = &device->queues; *link; link = &(*link)->next_queue)
        {
            if (*link == this)
            {
                *link = next_queue;
                break;
            }
        }
        ReleaseSRWLockExclusive(&device->queue_lock);
    }
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
}

void Native12CommandQueue::AddContext(HANDLE context)
{
    AcquireSRWLockExclusive(&context_lock);
    if (context_count < ARRAYSIZE(contexts))
    {
        contexts[context_count] = context;
        submitted[context_count] = false;
        ++context_count;
    }
    else WARN("Queue %p has no room to track context %p.\n", this, context);
    ReleaseSRWLockExclusive(&context_lock);
}

void Native12CommandQueue::RemoveContext(HANDLE context)
{
    AcquireSRWLockExclusive(&context_lock);
    for (UINT i = 0; i < context_count; ++i)
    {
        if (contexts[i] != context) continue;
        --context_count;
        for (UINT j = i; j < context_count; ++j)
        {
            contexts[j] = contexts[j + 1];
            submitted[j] = submitted[j + 1];
        }
        break;
    }
    ReleaseSRWLockExclusive(&context_lock);
}

bool Native12CommandQueue::NoteSubmission(HANDLE context)
{
    bool found = false;
    AcquireSRWLockExclusive(&context_lock);
    for (UINT i = 0; i < context_count; ++i)
    {
        if (contexts[i] != context) continue;
        submitted[i] = true;
        found = true;
        break;
    }
    ReleaseSRWLockExclusive(&context_lock);
    return found;
}

UINT Native12CommandQueue::SignalContexts(HANDLE *out)
{
    UINT count = 0;
    AcquireSRWLockExclusive(&context_lock);
    for (UINT i = 0; i < context_count; ++i)
    {
        if (!submitted[i]) continue;
        submitted[i] = false;
        out[count++] = contexts[i];
    }
    if (!count && context_count) out[count++] = contexts[0];
    ReleaseSRWLockExclusive(&context_lock);
    return count;
}

UINT Native12CommandQueue::WaitContexts(HANDLE *out)
{
    AcquireSRWLockShared(&context_lock);
    UINT count = context_count;
    for (UINT i = 0; i < count; ++i) out[i] = contexts[i];
    ReleaseSRWLockShared(&context_lock);
    return count;
}

void Native12QueueAddContext(HANDLE queue, HANDLE context)
{
    if (queue) static_cast<Native12CommandQueue *>(queue)->AddContext(context);
}

void Native12QueueRemoveContext(HANDLE queue, HANDLE context)
{
    if (queue) static_cast<Native12CommandQueue *>(queue)->RemoveContext(context);
}

void Native12NoteSubmission(Native12Device *device, HANDLE context)
{
    AcquireSRWLockShared(&device->queue_lock);
    for (Native12CommandQueue *queue = device->queues; queue; queue = queue->next_queue)
    {
        if (queue->NoteSubmission(context)) break;
    }
    ReleaseSRWLockShared(&device->queue_lock);
}

HRESULT STDMETHODCALLTYPE Native12CommandQueue::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12CommandQueue) || IsEqualGUID(iid, IID_ID3D12Pageable)
            || IsEqualGUID(iid, IID_ID3D12DeviceChild) || IsEqualGUID(iid, IID_ID3D12Object)
            || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12CommandQueue *>(this);
        AddRef();
        return S_OK;
    }
    if (IsEqualGUID(iid, IID_IWineDXGISwapChainFactory))
    {
        *out = static_cast<IWineDXGISwapChainFactory *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static_assert(sizeof(D3D12DDI_TILED_RESOURCE_COORDINATE) == sizeof(D3D12_TILED_RESOURCE_COORDINATE),
        "tile coordinate layout");
static_assert(sizeof(D3D12DDI_TILE_REGION_SIZE) == sizeof(D3D12_TILE_REGION_SIZE), "tile region layout");
static_assert(sizeof(D3D12DDI_TILE_RANGE_FLAGS) == sizeof(D3D12_TILE_RANGE_FLAGS), "tile range flags layout");

void STDMETHODCALLTYPE Native12CommandQueue::UpdateTileMappings(ID3D12Resource *resource, UINT region_count,
        const D3D12_TILED_RESOURCE_COORDINATE *coordinates, const D3D12_TILE_REGION_SIZE *sizes,
        ID3D12Heap *heap, UINT range_count, const D3D12_TILE_RANGE_FLAGS *range_flags,
        const UINT *heap_range_offsets, const UINT *range_tile_counts, D3D12_TILE_MAPPING_FLAGS flags)
{
    if (!resource) return;
    D3D12DDI_HHEAP driver_heap = {};
    if (heap) driver_heap = static_cast<Native12Heap *>(heap)->driver;
    const D3D12DDI_COMMAND_QUEUE_FUNCS_VIDEO_0020 *video = VideoFunctions();
    AcquireSRWLockExclusive(&submit_lock);
    (video ? video->pfnUpdateTileMappings : device->queue_functions.pfnUpdateTileMappings)(driver, Native12UnwrapResource(resource)->driver, region_count,
            reinterpret_cast<const D3D12DDI_TILED_RESOURCE_COORDINATE *>(coordinates),
            reinterpret_cast<const D3D12DDI_TILE_REGION_SIZE *>(sizes), driver_heap, range_count,
            reinterpret_cast<const D3D12DDI_TILE_RANGE_FLAGS *>(range_flags), heap_range_offsets,
            range_tile_counts, static_cast<D3D12DDI_TILE_MAPPING_FLAGS>(flags));
    ReleaseSRWLockExclusive(&submit_lock);
}

void STDMETHODCALLTYPE Native12CommandQueue::CopyTileMappings(ID3D12Resource *dst,
        const D3D12_TILED_RESOURCE_COORDINATE *dst_start, ID3D12Resource *src,
        const D3D12_TILED_RESOURCE_COORDINATE *src_start, const D3D12_TILE_REGION_SIZE *size,
        D3D12_TILE_MAPPING_FLAGS flags)
{
    if (!dst || !src || !dst_start || !src_start || !size) return;
    const D3D12DDI_COMMAND_QUEUE_FUNCS_VIDEO_0020 *video = VideoFunctions();
    AcquireSRWLockExclusive(&submit_lock);
    (video ? video->pfnCopyTileMappings : device->queue_functions.pfnCopyTileMappings)(driver, Native12UnwrapResource(dst)->driver,
            reinterpret_cast<const D3D12DDI_TILED_RESOURCE_COORDINATE *>(dst_start),
            Native12UnwrapResource(src)->driver,
            reinterpret_cast<const D3D12DDI_TILED_RESOURCE_COORDINATE *>(src_start),
            reinterpret_cast<const D3D12DDI_TILE_REGION_SIZE *>(size),
            static_cast<D3D12DDI_TILE_MAPPING_FLAGS>(flags));
    ReleaseSRWLockExclusive(&submit_lock);
}

void STDMETHODCALLTYPE Native12CommandQueue::ExecuteCommandLists(UINT count, ID3D12CommandList *const *lists)
{
    D3D12DDI_HCOMMANDLIST local[16];
    D3D12DDI_HCOMMANDLIST *handles = local;

    if (!count || !lists) return;
    if (VideoFunctions())
    {
        ExecuteVideoLists(count, lists);
        return;
    }
    for (UINT i = 0; i < count; ++i)
    {
        if (static_cast<Native12CommandList *>(static_cast<ID3D12GraphicsCommandList *>(lists[i]))->recording)
        {
            InterlockedCompareExchange(reinterpret_cast<LONG *>(&device->removed_reason), DXGI_ERROR_INVALID_CALL,
                    S_OK);
            return;
        }
    }
    if (count > ARRAYSIZE(local))
    {
        handles = static_cast<D3D12DDI_HCOMMANDLIST *>(HeapAlloc(GetProcessHeap(), 0, count * sizeof(*handles)));
        if (!handles) return;
    }
    for (UINT i = 0; i < count; ++i)
    {
        Native12CommandList *list = static_cast<Native12CommandList *>(
                static_cast<ID3D12GraphicsCommandList *>(lists[i]));
        handles[i] = list->driver;
    }
    TRACE("queue %p, %u lists.\n", this, count);
    AcquireSRWLockExclusive(&submit_lock);
    device->queue_functions.pfnExecuteCommandLists(driver, count, handles);
    ReleaseSRWLockExclusive(&submit_lock);
    TRACE("queue %p done.\n", this);
    if (handles != local) HeapFree(GetProcessHeap(), 0, handles);
}

void Native12CommandQueue::ExecuteVideoLists(UINT count, ID3D12CommandList *const *lists)
{
    D3D12DDI_HCOMMANDLIST local[16];
    D3D12DDI_HCOMMANDLIST *handles = local;

    for (UINT i = 0; i < count; ++i)
    {
        Native12VideoListCore *list = lists[i]->GetType() == desc.Type ? Native12VideoListFromInterface(lists[i]) : NULL;

        if (!list || list->recording)
        {
            InterlockedCompareExchange(reinterpret_cast<LONG *>(&device->removed_reason), DXGI_ERROR_INVALID_CALL,
                    S_OK);
            return;
        }
    }
    if (count > ARRAYSIZE(local))
    {
        handles = static_cast<D3D12DDI_HCOMMANDLIST *>(HeapAlloc(GetProcessHeap(), 0, count * sizeof(*handles)));
        if (!handles) return;
    }
    for (UINT i = 0; i < count; ++i)
        handles[i] = Native12VideoListFromInterface(lists[i])->driver;
    AcquireSRWLockExclusive(&submit_lock);
    VideoFunctions()->pfnExecuteCommandLists(driver, count, handles);
    ReleaseSRWLockExclusive(&submit_lock);
    if (handles != local) HeapFree(GetProcessHeap(), 0, handles);
}

const D3D12DDI_COMMAND_QUEUE_FUNCS_VIDEO_0020 *Native12CommandQueue::VideoFunctions()
{
    if (desc.Type == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE) return &device->video_queue_functions;
    if (desc.Type == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS) return &device->process_queue_functions;
    return NULL;
}

void STDMETHODCALLTYPE Native12CommandQueue::SetMarker(UINT, const void *, UINT) {}
void STDMETHODCALLTYPE Native12CommandQueue::BeginEvent(UINT, const void *, UINT) {}
void STDMETHODCALLTYPE Native12CommandQueue::EndEvent() {}

HRESULT STDMETHODCALLTYPE Native12CommandQueue::Signal(ID3D12Fence *fence, UINT64 value)
{
    if (!fence) return E_INVALIDARG;
    if (!device->kernel_callbacks.pfnSignalSynchronizationObjectFromGpu2Cb) return E_NOINTERFACE;
    Native12Fence *native = static_cast<Native12Fence *>(fence);
    D3D12DDIARG_FENCE_OPERATION operation = {};
    operation.Fence = native->driver;
    operation.Value = value;
    operation.PhysicalAdapterMask = 1;
    AcquireSRWLockExclusive(&submit_lock);
    if (VideoFunctions())
        VideoFunctions()->pfnSignalFence(driver, &operation);
    else
        device->queue_functions.pfnSignalFence(driver, &operation);
    if (FAILED(device->removed_reason))
    {
        ReleaseSRWLockExclusive(&submit_lock);
        return device->removed_reason;
    }

    HANDLE targets[D3DDDI_MAX_BROADCAST_CONTEXT];
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 signal = {};
    signal.ObjectCount = 1;
    signal.ObjectHandleArray = &native->sync;
    signal.BroadcastContextCount = SignalContexts(targets);
    signal.BroadcastContextArray = targets;
    signal.MonitoredFenceValueArray = &value;
    signal.Flags.AllowFenceRewind = 1;
    if (!signal.BroadcastContextCount)
    {
        ReleaseSRWLockExclusive(&submit_lock);
        return E_FAIL;
    }
    HRESULT hr = device->kernel_callbacks.pfnSignalSynchronizationObjectFromGpu2Cb(device->runtime_device, &signal);
    ReleaseSRWLockExclusive(&submit_lock);
    TRACE("queue %p, fence %p, value %#I64x, %lu contexts, first %p, hr %#lx.\n", this, fence, value,
            signal.BroadcastContextCount, targets[0], hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12CommandQueue::Wait(ID3D12Fence *fence, UINT64 value)
{
    if (!fence) return E_INVALIDARG;
    if (!device->kernel_callbacks.pfnWaitForSynchronizationObjectFromGpuCb) return E_NOINTERFACE;
    Native12Fence *native = static_cast<Native12Fence *>(fence);
    D3D12DDIARG_FENCE_OPERATION operation = {};
    operation.Fence = native->driver;
    operation.Value = value;
    operation.PhysicalAdapterMask = 1;
    AcquireSRWLockExclusive(&submit_lock);
    if (VideoFunctions())
        VideoFunctions()->pfnWaitForFence(driver, &operation);
    else
        device->queue_functions.pfnWaitForFence(driver, &operation);
    if (FAILED(device->removed_reason))
    {
        ReleaseSRWLockExclusive(&submit_lock);
        return device->removed_reason;
    }

    HANDLE targets[D3DDDI_MAX_BROADCAST_CONTEXT];
    UINT count = WaitContexts(targets);
    HRESULT hr = count ? S_OK : E_FAIL;
    for (UINT i = 0; i < count && SUCCEEDED(hr); ++i)
    {
        D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU wait = {};
        wait.hContext = targets[i];
        wait.ObjectCount = 1;
        wait.ObjectHandleArray = &native->sync;
        wait.MonitoredFenceValueArray = &value;
        hr = device->kernel_callbacks.pfnWaitForSynchronizationObjectFromGpuCb(device->runtime_device, &wait);
    }
    ReleaseSRWLockExclusive(&submit_lock);
    TRACE("queue %p, fence %p, value %#I64x, %u contexts, hr %#lx.\n", this, fence, value, count, hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12CommandQueue::GetTimestampFrequency(UINT64 *frequency)
{
    LARGE_INTEGER counter;
    if (!frequency) return E_INVALIDARG;
    QueryPerformanceFrequency(&counter);
    *frequency = counter.QuadPart;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Native12CommandQueue::GetClockCalibration(UINT64 *gpu_timestamp, UINT64 *cpu_timestamp)
{
    LARGE_INTEGER counter;
    if (!gpu_timestamp || !cpu_timestamp) return E_INVALIDARG;
    QueryPerformanceCounter(&counter);
    *gpu_timestamp = counter.QuadPart;
    *cpu_timestamp = counter.QuadPart;
    return S_OK;
}

D3D12_COMMAND_QUEUE_DESC *STDMETHODCALLTYPE Native12CommandQueue::GetDesc(D3D12_COMMAND_QUEUE_DESC *out)
{
    *out = desc;
    return out;
}

HRESULT Native12CommandAllocator::Initialize(D3D12_COMMAND_LIST_TYPE list_type)
{
    type = list_type;
    D3D12DDIARG_CREATE_COMMAND_POOL_0040 args = {};
    SIZE_T size = device->functions.pfnCalcPrivateCommandPoolSize(device->driver_device, &args);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!driver.pDrvPrivate) return E_OUTOFMEMORY;
    HRESULT hr = device->functions.pfnCreateCommandPool(device->driver_device, &args, driver);
    if (FAILED(hr))
    {
        WARN("The driver failed to create a command pool, hr %#lx.\n", hr);
        return hr;
    }
    driver_created = true;
    return S_OK;
}

Native12CommandAllocator::~Native12CommandAllocator()
{
    if (driver_created) device->functions.pfnDestroyCommandPool(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
}

HRESULT STDMETHODCALLTYPE Native12CommandAllocator::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12CommandAllocator) || IsEqualGUID(iid, IID_ID3D12Pageable)
            || IsEqualGUID(iid, IID_ID3D12DeviceChild) || IsEqualGUID(iid, IID_ID3D12Object)
            || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12CommandAllocator *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Native12CommandAllocator::Reset()
{
    if (recording_list) return E_FAIL;
    device->functions.pfnResetCommandPool(device->driver_device, driver);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC *desc, REFIID iid,
        void **out)
{
    if (!desc || !out) return E_INVALIDARG;
    *out = NULL;
    if (FAILED(removed_reason)) return DXGI_ERROR_DEVICE_REMOVED;
    Native12CommandQueue *queue = new Native12CommandQueue(this);
    if (!queue) return E_OUTOFMEMORY;
    HRESULT hr = queue->Initialize(desc);
    if (SUCCEEDED(hr)) hr = queue->QueryInterface(iid, out);
    queue->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID iid,
        void **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (type == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE && !EnsureVideo()) return E_INVALIDARG;
    if (type == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS && !EnsureVideoProcess()) return E_INVALIDARG;
    if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_BUNDLE
            && type != D3D12_COMMAND_LIST_TYPE_COMPUTE && type != D3D12_COMMAND_LIST_TYPE_COPY
            && type != D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE && type != D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS)
        return E_INVALIDARG;
    Native12CommandAllocator *allocator = new Native12CommandAllocator(this);
    if (!allocator) return E_OUTOFMEMORY;
    HRESULT hr = allocator->Initialize(type);
    if (SUCCEEDED(hr)) hr = allocator->QueryInterface(iid, out);
    allocator->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateFence(UINT64 initial_value, D3D12_FENCE_FLAGS flags, REFIID iid,
        void **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    Native12Fence *fence = new Native12Fence(this);
    if (!fence) return E_OUTOFMEMORY;
    HRESULT hr = fence->Initialize(initial_value, flags);
    if (SUCCEEDED(hr)) hr = fence->QueryInterface(iid, out);
    fence->Release();
    return hr;
}
