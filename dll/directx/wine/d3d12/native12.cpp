/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 device on a user-mode display driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "native12.h"
#include "native12_device.h"
#include "native12_objects.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d12);

static SRWLOCK Native12DeviceListLock = SRWLOCK_INIT;
static Native12Device *Native12DeviceList;

Native12PrivateData::~Native12PrivateData()
{
    while (head)
    {
        Entry *entry = head;
        head = entry->next;
        if (entry->object) entry->object->Release();
        HeapFree(GetProcessHeap(), 0, entry);
    }
}

HRESULT Native12PrivateData::Get(REFGUID guid, UINT *size, void *data)
{
    if (!size) return E_INVALIDARG;
    AcquireSRWLockShared(&lock);
    for (Entry *entry = head; entry; entry = entry->next)
    {
        if (!IsEqualGUID(entry->guid, guid)) continue;
        UINT needed = entry->is_interface ? sizeof(IUnknown *) : entry->size;
        HRESULT hr = S_OK;
        if (!data) *size = needed;
        else if (*size < needed) { *size = needed; hr = DXGI_ERROR_MORE_DATA; }
        else
        {
            if (entry->is_interface)
            {
                if (entry->object) entry->object->AddRef();
                *static_cast<IUnknown **>(data) = entry->object;
            }
            else memcpy(data, entry->data, entry->size);
            *size = needed;
        }
        ReleaseSRWLockShared(&lock);
        return hr;
    }
    ReleaseSRWLockShared(&lock);
    *size = 0;
    return DXGI_ERROR_NOT_FOUND;
}

HRESULT Native12PrivateData::Set(REFGUID guid, UINT size, const void *data, IUnknown *object)
{
    if (object) return Store(guid, 0, NULL, object, true);
    return Store(guid, size, data, NULL, false);
}

HRESULT Native12PrivateData::SetInterface(REFGUID guid, IUnknown *object)
{
    return Store(guid, 0, NULL, object, true);
}

HRESULT Native12PrivateData::Store(REFGUID guid, UINT size, const void *data, IUnknown *object, bool is_interface)
{
    Entry *fresh = NULL;
    if (is_interface || (data && size))
    {
        fresh = static_cast<Entry *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                sizeof(Entry) + (is_interface ? 0 : size)));
        if (!fresh) return E_OUTOFMEMORY;
        fresh->guid = guid;
        fresh->size = is_interface ? 0 : size;
        fresh->object = object;
        fresh->is_interface = is_interface;
        if (object) object->AddRef();
        else if (!is_interface) memcpy(fresh->data, data, size);
    }
    AcquireSRWLockExclusive(&lock);
    Entry **link = &head, *old = NULL;
    while (*link)
    {
        if (IsEqualGUID((*link)->guid, guid))
        {
            old = *link;
            *link = old->next;
            break;
        }
        link = &(*link)->next;
    }
    if (fresh)
    {
        fresh->next = head;
        head = fresh;
    }
    ReleaseSRWLockExclusive(&lock);
    if (old)
    {
        if (old->object) old->object->Release();
        HeapFree(GetProcessHeap(), 0, old);
    }
    return (fresh || old) ? S_OK : S_FALSE;
}

static HRESULT APIENTRY Native12QueryAdapter(HANDLE handle, const D3DDDICB_QUERYADAPTERINFO *args)
{
    if (!args || !handle || reinterpret_cast<ULONG_PTR>(handle) > ~0u) return E_INVALIDARG;
    D3DKMT_QUERYADAPTERINFO query = {};
    query.hAdapter = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(handle));
    query.Type = KMTQAITYPE_UMDRIVERPRIVATE;
    query.pPrivateDriverData = args->pPrivateDriverData;
    query.PrivateDriverDataSize = args->PrivateDriverDataSize;
    return Native12StatusToHresult(D3DKMTQueryAdapterInfo(&query));
}

static HRESULT APIENTRY Native12QueryAdapter2(HANDLE handle, const D3DDDICB_QUERYADAPTERINFO2 *args)
{
    return RosUmdQueryAdapterInfo2(handle, args, D3DKMTQueryAdapterInfo);
}

Native12Device *Native12Device::FromRuntime(HANDLE runtime)
{
    Native12Device *found = NULL;
    AcquireSRWLockShared(&Native12DeviceListLock);
    for (Native12Device *device = Native12DeviceList; device; device = device->next)
    {
        if (device->runtime_device == runtime)
        {
            found = device;
            break;
        }
    }
    ReleaseSRWLockShared(&Native12DeviceListLock);
    return found;
}

Native12Device *Native12Device::FromKernelHandle(HANDLE handle)
{
    Native12Device *found = NULL;
    AcquireSRWLockShared(&Native12DeviceListLock);
    for (Native12Device *device = Native12DeviceList; device; device = device->next)
    {
        if (device->runtime_device == handle
                || reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(device->km_adapter)) == handle)
        {
            found = device;
            break;
        }
    }
    ReleaseSRWLockShared(&Native12DeviceListLock);
    return found;
}

typedef HRESULT (APIENTRY *Native12KernelCallback)(HANDLE, void *);


static HRESULT Native12TraceKernelCallback(UINT index, HANDLE handle, void *args)
{
    Native12Device *device = Native12Device::FromKernelHandle(handle);
    if (!device) return E_INVALIDARG;
    Native12KernelCallback real = reinterpret_cast<Native12KernelCallback *>(&device->driver_callbacks)[index];
    HRESULT hr = real(handle, args);
    if (FAILED(hr)) WARN("Kernel callback slot %u failed, hr %#lx.\n", index, hr);
    else if (index == offsetof(D3DDDI_DEVICECALLBACKS, pfnMapGpuVirtualAddressCb) / sizeof(void *))
    {
        const D3DDDI_MAPGPUVIRTUALADDRESS *map = static_cast<const D3DDDI_MAPGPUVIRTUALADDRESS *>(args);
        TRACE("Map allocation %#x, base %#I64x, range %#I64x-%#I64x, offset %#I64x, pages %#I64x, "
                "protection %#I64x/%#I64x -> %#I64x, fence %#I64x, hr %#lx.\n", map->hAllocation,
                map->BaseAddress, map->MinimumAddress, map->MaximumAddress, map->OffsetInPages,
                map->SizeInPages, map->Protection.Value, map->DriverProtection, map->VirtualAddress,
                map->PagingFenceValue, hr);
    }
    else if (index == offsetof(D3DDDI_DEVICECALLBACKS, pfnSubmitCommandCb) / sizeof(void *))
    {
        const D3DDDICB_SUBMITCOMMAND *submit = static_cast<const D3DDDICB_SUBMITCOMMAND *>(args);
        TRACE("Submit commands %#I64x, length %u, flags %#x, %u contexts, first %p, private data %u bytes, hr %#lx.\n",
                submit->Commands, submit->CommandLength, submit->Flags.Value, submit->BroadcastContextCount,
                submit->BroadcastContext[0], submit->PrivateDriverDataSize, hr);
    }
    else if (index == offsetof(D3DDDI_DEVICECALLBACKS, pfnSignalSynchronizationObjectFromGpu2Cb) / sizeof(void *))
    {
        const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 *signal =
                static_cast<const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 *>(args);
        TRACE("Signal %u objects, first %#x, flags %#x, %lu contexts, value %#I64x, hr %#lx.\n",
                signal->ObjectCount, signal->ObjectCount ? signal->ObjectHandleArray[0] : 0, signal->Flags.Value,
                signal->BroadcastContextCount, signal->FenceValue, hr);
    }
    else TRACE("Kernel callback slot %u.\n", index);
    return hr;
}

template <UINT Index> static HRESULT APIENTRY Native12KernelThunk(HANDLE handle, void *args)
{
    return Native12TraceKernelCallback(Index, handle, args);
}

#define NATIVE12_THUNK(n) reinterpret_cast<void *>(static_cast<Native12KernelCallback>(Native12KernelThunk<n>))
#define NATIVE12_THUNKS8(n) NATIVE12_THUNK(n), NATIVE12_THUNK(n + 1), NATIVE12_THUNK(n + 2), \
        NATIVE12_THUNK(n + 3), NATIVE12_THUNK(n + 4), NATIVE12_THUNK(n + 5), NATIVE12_THUNK(n + 6), \
        NATIVE12_THUNK(n + 7)

static void *const Native12KernelThunks[] =
{
    NATIVE12_THUNKS8(0), NATIVE12_THUNKS8(8), NATIVE12_THUNKS8(16), NATIVE12_THUNKS8(24),
    NATIVE12_THUNKS8(32), NATIVE12_THUNKS8(40), NATIVE12_THUNKS8(48), NATIVE12_THUNKS8(56),
    NATIVE12_THUNKS8(64), NATIVE12_THUNKS8(72), NATIVE12_THUNKS8(80), NATIVE12_THUNKS8(88),
};

static_assert(sizeof(D3DDDI_DEVICECALLBACKS) <= sizeof(Native12KernelThunks), "kernel callback thunk count");

static void APIENTRY Native12SetError(D3D10DDI_HRTDEVICE handle, HRESULT hr)
{
    Native12Device *device = Native12Device::FromRuntime(handle.handle);
    WARN("Driver reported device error %#lx.\n", hr);
    if (device && FAILED(hr) && hr != E_OUTOFMEMORY)
        InterlockedCompareExchange(reinterpret_cast<LONG *>(&device->removed_reason), hr, S_OK);
}

static void APIENTRY Native12SetCommandListError(D3D12DDI_HRTCOMMANDLIST handle, HRESULT hr)
{
    Native12SetCommandListErrorObject(handle.handle, hr);
}

static void APIENTRY Native12SetCommandListTable(D3D12DDI_HRTCOMMANDLIST list, D3D12DDI_HRTTABLE table)
{
    Native12SetCommandListTableObject(list.handle, table.handle);
}

static HRESULT APIENTRY Native12CreateContext(D3D12DDI_HRTCOMMANDQUEUE queue, D3DDDICB_CREATECONTEXT *args)
{
    Native12Device *device = Native12QueueDevice(queue.handle);
    if (!device || !device->kernel_callbacks.pfnCreateContextCb) return E_INVALIDARG;
    HRESULT hr = device->kernel_callbacks.pfnCreateContextCb(device->runtime_device, args);
    TRACE("queue %p, node %u, engine affinity %#x, flags %#x, context %p, hr %#lx.\n", queue.handle,
            args->NodeOrdinal, args->EngineAffinity, args->Flags.Value, args->hContext, hr);
    if (SUCCEEDED(hr)) Native12QueueAddContext(queue.handle, args->hContext);
    return hr;
}

static HRESULT APIENTRY Native12CreateContextVirtual(D3D12DDI_HRTCOMMANDQUEUE queue,
        D3DDDICB_CREATECONTEXTVIRTUAL *args)
{
    Native12Device *device = Native12QueueDevice(queue.handle);
    if (!device || !device->kernel_callbacks.pfnCreateContextVirtualCb) return E_INVALIDARG;
    HRESULT hr = device->kernel_callbacks.pfnCreateContextVirtualCb(device->runtime_device, args);
    TRACE("queue %p, node %u, engine affinity %#x, flags %#x, context %p, hr %#lx.\n", queue.handle,
            args->NodeOrdinal, args->EngineAffinity, args->Flags.Value, args->hContext, hr);
    if (SUCCEEDED(hr)) Native12QueueAddContext(queue.handle, args->hContext);
    return hr;
}

static HRESULT APIENTRY Native12DestroyContext(D3D12DDI_HRTCOMMANDQUEUE queue, const D3DDDICB_DESTROYCONTEXT *args)
{
    Native12Device *device = Native12QueueDevice(queue.handle);
    if (!device || !device->kernel_callbacks.pfnDestroyContextCb) return E_INVALIDARG;
    Native12QueueRemoveContext(queue.handle, args->hContext);
    HRESULT hr = device->kernel_callbacks.pfnDestroyContextCb(device->runtime_device, args);
    TRACE("queue %p, context %p, hr %#lx.\n", queue.handle, args->hContext, hr);
    return hr;
}

static HRESULT APIENTRY Native12CreatePagingQueue(D3D12DDI_HRTCOMMANDQUEUE queue, D3DDDICB_CREATEPAGINGQUEUE *args)
{
    Native12Device *device = Native12QueueDevice(queue.handle);
    if (!device || !device->kernel_callbacks.pfnCreatePagingQueueCb) return E_INVALIDARG;
    HRESULT hr = device->kernel_callbacks.pfnCreatePagingQueueCb(device->runtime_device, args);
    TRACE("queue %p, paging queue %#x, hr %#lx.\n", queue.handle, args->hPagingQueue, hr);
    return hr;
}

static HRESULT APIENTRY Native12DestroyPagingQueue(D3D12DDI_HRTCOMMANDQUEUE queue,
        const D3DDDI_DESTROYPAGINGQUEUE *args)
{
    Native12Device *device = Native12QueueDevice(queue.handle);
    if (!device || !device->kernel_callbacks.pfnDestroyPagingQueueCb) return E_INVALIDARG;
    return device->kernel_callbacks.pfnDestroyPagingQueueCb(device->runtime_device, args);
}

static HRESULT APIENTRY Native12MakeResident(D3D12DDI_HRTDEVICE handle, D3D12DDI_HRTPAGINGQUEUE queue,
        D3DDDI_MAKERESIDENT *args)
{
    Native12Device *device = Native12Device::FromRuntime(handle.handle);
    if (!device || !args || !device->kernel_callbacks.pfnMakeResidentCb) return E_INVALIDARG;
    if (queue.handle == &device->paging_queue) args->hPagingQueue = device->paging_queue.hPagingQueue;
    HRESULT hr = device->kernel_callbacks.pfnMakeResidentCb(device->runtime_device, args);
    TRACE("paging queue %#x, %u allocations, flags %#x, fence %#I64x, hr %#lx.\n", args->hPagingQueue,
            args->NumAllocations, args->Flags.Value, args->PagingFenceValue, hr);
    return hr;
}

static HRESULT APIENTRY Native12Evict(D3D12DDI_HRTDEVICE handle, const D3DDDICB_EVICT *args)
{
    Native12Device *device = Native12Device::FromRuntime(handle.handle);
    if (!device || !args || !device->kernel_callbacks.pfnEvictCb) return E_INVALIDARG;
    D3DDDICB_EVICT copy = *args;
    HRESULT hr = device->kernel_callbacks.pfnEvictCb(device->runtime_device, &copy);
    TRACE("%u allocations, flags %#x, hr %#lx.\n", args->NumAllocations, args->Flags.Value, hr);
    return hr;
}

static HRESULT APIENTRY Native12ReclaimAllocations2(D3D12DDI_HRTDEVICE handle, D3D12DDI_HRTPAGINGQUEUE queue,
        D3D12DDICB_RECLAIMALLOCATIONS2 *args)
{
    Native12Device *device = Native12Device::FromRuntime(handle.handle);
    if (!device || !args || !device->kernel_callbacks.pfnReclaimAllocations2Cb) return E_INVALIDARG;
    D3DDDICB_RECLAIMALLOCATIONS2 reclaim = {};
    reclaim.PagingQueue = queue.handle == &device->paging_queue ? device->paging_queue.hPagingQueue : 0;
    reclaim.NumAllocations = args->NumAllocations;
    reclaim.HandleList = args->HandleList;
    reclaim.pDiscarded = args->pDiscarded;
    HRESULT hr = device->kernel_callbacks.pfnReclaimAllocations2Cb(device->runtime_device, &reclaim);
    args->PagingFenceValue = reclaim.PagingFenceValue;
    TRACE("%u allocations, hr %#lx.\n", args->NumAllocations, hr);
    return hr;
}

static HRESULT APIENTRY Native12OfferAllocations(D3D12DDI_HRTDEVICE handle, const D3D12DDICB_OFFERALLOCATIONS *args)
{
    Native12Device *device = Native12Device::FromRuntime(handle.handle);
    if (!device || !args || !device->kernel_callbacks.pfnOfferAllocationsCb) return E_INVALIDARG;
    D3DDDICB_OFFERALLOCATIONS offer = {};
    offer.HandleList = args->HandleList;
    offer.NumAllocations = args->NumAllocations;
    offer.Priority = args->Priority;
    HRESULT hr = device->kernel_callbacks.pfnOfferAllocationsCb(device->runtime_device, &offer);
    TRACE("%u allocations, priority %u, hr %#lx.\n", args->NumAllocations, args->Priority, hr);
    return hr;
}

static HRESULT APIENTRY Native12Allocate(D3D12DDI_HRTDEVICE handle, D3D12DDICB_ALLOCATE_0022 *args)
{
    Native12Device *device = Native12Device::FromRuntime(handle.handle);
    if (!device || !args || !args->NumAllocations || !args->pAllocationInfo
            || !device->kernel_callbacks.pfnAllocateCb)
        return E_INVALIDARG;

    D3DDDI_ALLOCATIONINFO2 local[8];
    D3DDDI_ALLOCATIONINFO2 *info = local;
    if (args->NumAllocations > ARRAYSIZE(local))
    {
        info = static_cast<D3DDDI_ALLOCATIONINFO2 *>(
                HeapAlloc(GetProcessHeap(), 0, args->NumAllocations * sizeof(*info)));
        if (!info) return E_OUTOFMEMORY;
    }
    ZeroMemory(info, args->NumAllocations * sizeof(*info));
    for (UINT i = 0; i < args->NumAllocations; ++i)
    {
        const D3D12DDI_ALLOCATION_INFO_0022 &source = args->pAllocationInfo[i];
        info[i].pSystemMem = source.pSystemMem;
        info[i].pPrivateDriverData = source.pPrivateDriverData;
        info[i].PrivateDriverDataSize = source.PrivateDriverDataSize;
        info[i].VidPnSourceId = source.VidPnSourceId;
        info[i].Flags.Primary = !!(source.Flags & D3D12DDI_ALLOCATION_INFO_FLAGS_0022_PRIMARY);
        info[i].Flags.Stereo = !!(source.Flags & D3D12DDI_ALLOCATION_INFO_FLAGS_0022_STEREO);
        if (source.Flags & D3D12DDI_ALLOCATION_INFO_FLAGS_0022_OVERRIDE_PRIORITY)
        {
            info[i].Flags.OverridePriority = 1;
            info[i].Priority = source.Priority;
        }
    }

    D3DDDICB_ALLOCATE allocate = {};
    allocate.pPrivateDriverData = args->pPrivateDriverData;
    allocate.PrivateDriverDataSize = args->PrivateDriverDataSize;
    allocate.hResource = args->hResource;
    allocate.NumAllocations = args->NumAllocations;
    allocate.pAllocationInfo2 = info;
    HRESULT hr = device->kernel_callbacks.pfnAllocateCb(device->runtime_device, &allocate);
    TRACE("resource %p, %u allocations, private data %u bytes, first %#x, hr %#lx.\n", args->hResource,
            args->NumAllocations, args->PrivateDriverDataSize, info[0].hAllocation, hr);
    if (SUCCEEDED(hr))
    {
        args->hKMResource = allocate.hKMResource;
        for (UINT i = 0; i < args->NumAllocations; ++i)
        {
            args->pAllocationInfo[i].hAllocation = info[i].hAllocation;
            args->pAllocationInfo[i].GpuVirtualAddress = info[i].GpuVirtualAddress;
        }
    }
    else
    {
        WARN("Allocation of %u items for resource %p failed, hr %#lx.\n", args->NumAllocations,
                args->hResource, hr);
    }
    if (info != local) HeapFree(GetProcessHeap(), 0, info);
    return hr;
}

static HRESULT APIENTRY Native12KernelAllocate(HANDLE handle, D3DDDICB_ALLOCATE *args)
{
    Native12Device *device = Native12Device::FromKernelHandle(handle);
    if (!device || !args || !args->NumAllocations || !args->pAllocationInfo
            || !device->kernel_callbacks.pfnAllocateCb)
        return E_INVALIDARG;

    D3DDDI_ALLOCATIONINFO2 local[8];
    D3DDDI_ALLOCATIONINFO2 *info = local;
    if (args->NumAllocations > ARRAYSIZE(local))
    {
        info = static_cast<D3DDDI_ALLOCATIONINFO2 *>(
                HeapAlloc(GetProcessHeap(), 0, args->NumAllocations * sizeof(*info)));
        if (!info) return E_OUTOFMEMORY;
    }
    ZeroMemory(info, args->NumAllocations * sizeof(*info));
    for (UINT i = 0; i < args->NumAllocations; ++i)
    {
        const D3DDDI_ALLOCATIONINFO &source = args->pAllocationInfo[i];
        info[i].pSystemMem = source.pSystemMem;
        info[i].pPrivateDriverData = source.pPrivateDriverData;
        info[i].PrivateDriverDataSize = source.PrivateDriverDataSize;
        info[i].VidPnSourceId = source.VidPnSourceId;
        info[i].Flags.Primary = source.Flags.Primary;
        info[i].Flags.Stereo = source.Flags.Stereo;
    }

    D3DDDICB_ALLOCATE allocate = *args;
    allocate.pAllocationInfo2 = info;
    HRESULT hr = device->kernel_callbacks.pfnAllocateCb(handle, &allocate);
    TRACE("resource %p, %u allocations, private data %u bytes, first %#x, hr %#lx.\n", args->hResource,
            args->NumAllocations, args->PrivateDriverDataSize, info[0].hAllocation, hr);
    if (SUCCEEDED(hr))
    {
        args->hKMResource = allocate.hKMResource;
        for (UINT i = 0; i < args->NumAllocations; ++i)
            args->pAllocationInfo[i].hAllocation = info[i].hAllocation;
    }
    if (info != local) HeapFree(GetProcessHeap(), 0, info);
    return hr;
}

static HRESULT APIENTRY Native12KernelSubmit(HANDLE handle, const D3DDDICB_SUBMITCOMMAND *args)
{
    Native12Device *device = Native12Device::FromKernelHandle(handle);
    if (!device || !args || !device->kernel_callbacks.pfnSubmitCommandCb) return E_INVALIDARG;
    for (UINT i = 0; i < args->BroadcastContextCount && i < ARRAYSIZE(args->BroadcastContext); ++i)
        Native12NoteSubmission(device, args->BroadcastContext[i]);
    return device->kernel_callbacks.pfnSubmitCommandCb(handle, args);
}

static HRESULT APIENTRY Native12Deallocate(D3D12DDI_HRTDEVICE handle, const D3D12DDICB_DEALLOCATE_0022 *args)
{
    Native12Device *device = Native12Device::FromRuntime(handle.handle);
    if (!device || !args || !device->kernel_callbacks.pfnDeallocate2Cb) return E_INVALIDARG;
    D3DDDICB_DEALLOCATE2 deallocate = {};
    deallocate.hResource = args->hResource;
    deallocate.NumAllocations = args->NumAllocations;
    deallocate.HandleList = args->HandleList;
    deallocate.Flags.AssumeNotInUse = !!(args->Flags & D3D12DDI_DEALLOCATE_FLAGS_0022_ASSUME_NOT_IN_USE);
    deallocate.Flags.SynchronousDestroy = !!(args->Flags & D3D12DDI_DEALLOCATE_FLAGS_0022_SYNCHRONOUS_DESTROY);
    HRESULT hr = device->kernel_callbacks.pfnDeallocate2Cb(device->runtime_device, &deallocate);
    TRACE("resource %p, %u allocations, flags %#x, hr %#lx.\n", args->hResource, args->NumAllocations,
            args->Flags, hr);
    return hr;
}

static HRESULT APIENTRY Native12CreateSchedulingGroupContext(D3D12DDI_HRTSCHEDULINGGROUP_0050,
        D3DDDICB_CREATECONTEXT *)
{
    FIXME("Scheduling group contexts are not implemented.\n");
    return E_NOTIMPL;
}

static HRESULT APIENTRY Native12CreateSchedulingGroupContextVirtual(D3D12DDI_HRTSCHEDULINGGROUP_0050,
        D3DDDICB_CREATECONTEXTVIRTUAL *)
{
    FIXME("Scheduling group contexts are not implemented.\n");
    return E_NOTIMPL;
}

static HRESULT APIENTRY Native12CreateHwQueue(D3D12DDI_HRTCOMMANDQUEUE, D3DDDICB_CREATEHWQUEUE *)
{
    FIXME("Hardware queues are not implemented.\n");
    return E_NOTIMPL;
}

struct Native12BackgroundWork
{
    PFND3D12DDI_UMD_CALLBACK_METHOD callback;
    void *context;
};

static DWORD WINAPI Native12BackgroundWorker(void *parameter)
{
    Native12BackgroundWork *work = static_cast<Native12BackgroundWork *>(parameter);
    work->callback(work->context);
    HeapFree(GetProcessHeap(), 0, work);
    return 0;
}

static HRESULT APIENTRY Native12QueueBackgroundWork(D3D12DDI_HRTDEVICE, PFND3D12DDI_UMD_CALLBACK_METHOD callback,
        PFND3D12DDI_UMD_CALLBACK_METHOD, void *context)
{
    if (!callback) return E_INVALIDARG;
    Native12BackgroundWork *work = static_cast<Native12BackgroundWork *>(
            HeapAlloc(GetProcessHeap(), 0, sizeof(*work)));
    if (!work) return E_OUTOFMEMORY;
    work->callback = callback;
    work->context = context;
    if (!QueueUserWorkItem(Native12BackgroundWorker, work, WT_EXECUTEDEFAULT))
    {
        HeapFree(GetProcessHeap(), 0, work);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

static D3D_FEATURE_LEVEL Native12LevelFromPipeline(D3D12DDI_3DPIPELINELEVEL level)
{
    switch (level)
    {
        case D3D12DDI_3DPIPELINELEVEL_11_0: return D3D_FEATURE_LEVEL_11_0;
        case D3D12DDI_3DPIPELINELEVEL_11_1: return D3D_FEATURE_LEVEL_11_1;
        case D3D12DDI_3DPIPELINELEVEL_12_0: return D3D_FEATURE_LEVEL_12_0;
        case D3D12DDI_3DPIPELINELEVEL_12_1: return D3D_FEATURE_LEVEL_12_1;
        case D3D12DDI_3DPIPELINELEVEL_12_2: return D3D_FEATURE_LEVEL_12_1;
        default: return static_cast<D3D_FEATURE_LEVEL>(0);
    }
}

HRESULT Native12Device::QueryCaps(D3D12DDICAPS_TYPE type, void *info, void *data, UINT size)
{
    D3D12DDIARG_GETCAPS caps = {};
    caps.Type = type;
    caps.pInfo = info;
    caps.pData = data;
    caps.DataSize = size;
    return adapter_functions.pfnGetCaps(driver_adapter, &caps);
}

HRESULT Native12Device::Initialize(IDXGIAdapter *selected_adapter, D3D_FEATURE_LEVEL minimum_level)
{
    DXGI_ADAPTER_DESC desc;
    HRESULT hr = selected_adapter->GetDesc(&desc);
    if (FAILED(hr)) return hr;
    luid = desc.AdapterLuid;
    d3d12_dred_enablement(&dred_auto_breadcrumbs, &dred_page_fault);

    D3DKMT_OPENADAPTERFROMLUID open = {};
    open.AdapterLuid = desc.AdapterLuid;
    if (FAILED(hr = Native12StatusToHresult(D3DKMTOpenAdapterFromLuid(&open)))) return DXGI_ERROR_UNSUPPORTED;
    km_adapter = open.hAdapter;

    D3DKMT_UMDFILENAMEINFO name = {};
    name.Version = KMTUMDVERSION_DX12;
    D3DKMT_QUERYADAPTERINFO query = {};
    query.hAdapter = km_adapter;
    query.Type = KMTQAITYPE_UMDRIVERNAME;
    query.pPrivateDriverData = &name;
    query.PrivateDriverDataSize = sizeof(name);
    if (D3DKMTQueryAdapterInfo(&query) < 0 || !name.UmdFileName[0]) return DXGI_ERROR_UNSUPPORTED;
    name.UmdFileName[MAX_PATH - 1] = 0;
    DWORD load_flags = 0;
    if ((name.UmdFileName[0] == '\\' && name.UmdFileName[1] == '\\')
            || (name.UmdFileName[1] == ':' && name.UmdFileName[2] == '\\'))
        load_flags = LOAD_WITH_ALTERED_SEARCH_PATH;
    umd = LoadLibraryExW(name.UmdFileName, NULL, load_flags);
    if (!umd) return DXGI_ERROR_UNSUPPORTED;
    PFND3D12DDI_OPENADAPTER open_adapter =
            reinterpret_cast<PFND3D12DDI_OPENADAPTER>(GetProcAddress(umd, "OpenAdapter12"));
    if (!open_adapter) return DXGI_ERROR_UNSUPPORTED;

    adapter_callbacks.pfnQueryAdapterInfoCb = Native12QueryAdapter;
    adapter_callbacks.pfnQueryAdapterInfoCb2 = Native12QueryAdapter2;
    D3D12DDIARG_OPENADAPTER args = {};
    args.hRTAdapter.handle = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(km_adapter));
    args.pAdapterCallbacks = reinterpret_cast<const D3DDDI_ADAPTERCALLBACKS *>(&adapter_callbacks);
    args.pAdapterFuncs = reinterpret_cast<D3D12DDI_ADAPTERFUNCS *>(&adapter_functions);
    if (FAILED(hr = open_adapter(&args)))
    {
        WARN("OpenAdapter12 failed, hr %#lx.\n", hr);
        return DXGI_ERROR_UNSUPPORTED;
    }
    driver_adapter = args.hAdapter;
    adapter_open = true;
    if (!adapter_functions.pfnGetSupportedVersions || !adapter_functions.pfnGetCaps
            || !adapter_functions.pfnCalcPrivateDeviceSize || !adapter_functions.pfnCreateDevice
            || !adapter_functions.pfnFillDDITable || !adapter_functions.pfnCloseAdapter)
        return DXGI_ERROR_UNSUPPORTED;

    UINT32 version_count = 0;
    if (FAILED(adapter_functions.pfnGetSupportedVersions(driver_adapter, &version_count, NULL))
            || !version_count || version_count > 4096)
        return DXGI_ERROR_UNSUPPORTED;
    UINT64 *versions = static_cast<UINT64 *>(HeapAlloc(GetProcessHeap(), 0, version_count * sizeof(*versions)));
    if (!versions) return E_OUTOFMEMORY;
    UINT32 capacity = version_count;
    bool offers_0110 = false, offers_0102 = false;
    if (SUCCEEDED(adapter_functions.pfnGetSupportedVersions(driver_adapter, &version_count, versions)))
        for (UINT32 i = 0; i < version_count && i < capacity; ++i)
        {
            if (versions[i] == D3D12DDI_SUPPORTED_0110) offers_0110 = true;
            if (versions[i] == D3D12DDI_SUPPORTED_0102) offers_0102 = true;
        }
    HeapFree(GetProcessHeap(), 0, versions);
    if (offers_0110) ddi_build = D3D12DDI_BUILD_VERSION_0110;
    else if (offers_0102) ddi_build = D3D12DDI_BUILD_VERSION_0102;
    else
    {
        WARN("The driver offers neither D3D12 DDI build %u nor %u.\n", D3D12DDI_BUILD_VERSION_0110,
                D3D12DDI_BUILD_VERSION_0102);
        return DXGI_ERROR_UNSUPPORTED;
    }

    D3D12DDI_3DPIPELINESUPPORT1_DATA_0081 pipeline = {};
    pipeline.HighestRuntimeSupportedFeatureLevel = D3D12DDI_3DPIPELINELEVEL_12_1;
    if (FAILED(hr = QueryCaps(D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1, NULL, &pipeline, sizeof(pipeline))))
        return DXGI_ERROR_UNSUPPORTED;
    feature_level = Native12LevelFromPipeline(pipeline.MaximumDriverSupportedFeatureLevel);
    if (!feature_level || feature_level < minimum_level) return DXGI_ERROR_UNSUPPORTED;

    D3DKMT_CREATEDEVICE create = {};
    create.hAdapter = km_adapter;
    if (FAILED(hr = Native12StatusToHresult(D3DKMTCreateDevice(&create)))) return hr;
    km_device = create.hDevice;

    runtime = LoadLibraryW(L"d3dumdrt.dll");
    if (!runtime) return HRESULT_FROM_WIN32(GetLastError());
    typedef HRESULT (WINAPI *CreateCallbacks)(D3DKMT_HANDLE, D3DKMT_HANDLE, UINT, D3DDDI_DEVICECALLBACKS *, HANDLE *);
    CreateCallbacks create_callbacks =
            reinterpret_cast<CreateCallbacks>(GetProcAddress(runtime, "D3DUmdRtCreateDeviceCallbacksEx"));
    destroy_callbacks =
            reinterpret_cast<HRESULT (WINAPI *)(HANDLE)>(GetProcAddress(runtime, "D3DUmdRtDestroyDeviceCallbacks"));
    release_resource = reinterpret_cast<HRESULT (WINAPI *)(HANDLE, HANDLE)>(
            GetProcAddress(runtime, "D3DUmdRtReleaseResource"));
    register_resource = reinterpret_cast<HRESULT (WINAPI *)(HANDLE, HANDLE, D3DKMT_CREATEALLOCATIONFLAGS,
            const void *, UINT)>(GetProcAddress(runtime, "D3DUmdRtRegisterResource"));
    get_resource_handles = reinterpret_cast<HRESULT (WINAPI *)(HANDLE, HANDLE, D3DKMT_HANDLE *, D3DKMT_HANDLE *)>(
            GetProcAddress(runtime, "D3DUmdRtGetResourceHandles"));
    adopt_resource = reinterpret_cast<HRESULT (WINAPI *)(HANDLE, HANDLE, D3DKMT_HANDLE, D3DKMT_HANDLE, D3DKMT_HANDLE)>(
            GetProcAddress(runtime, "D3DUmdRtAdoptResource"));
    enqueue_set_event = reinterpret_cast<HRESULT (WINAPI *)(HANDLE, HANDLE)>(
            GetProcAddress(runtime, "D3DUmdRtEnqueueSetEvent"));
    if (!create_callbacks || !destroy_callbacks || !release_resource) return E_NOINTERFACE;
    if (FAILED(hr = create_callbacks(km_adapter, km_device, 2, &kernel_callbacks, &runtime_device))) return hr;

    AcquireSRWLockExclusive(&Native12DeviceListLock);
    next = Native12DeviceList;
    Native12DeviceList = this;
    listed = true;
    ReleaseSRWLockExclusive(&Native12DeviceListLock);

    D3DDDICB_CREATEPAGINGQUEUE paging = {};
    paging.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    if (!kernel_callbacks.pfnCreatePagingQueueCb
            || FAILED(hr = kernel_callbacks.pfnCreatePagingQueueCb(runtime_device, &paging)))
    {
        WARN("Failed to create the device paging queue, hr %#lx.\n", hr);
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }
    paging_queue = paging;

    core_callbacks.pfnSetErrorCb = Native12SetError;
    core_callbacks.pfnSetCommandListErrorCb = Native12SetCommandListError;
    core_callbacks.pfnSetCommandListDDITableCb = Native12SetCommandListTable;
    core_callbacks.pfnCreateContextCb = Native12CreateContext;
    core_callbacks.pfnCreateContextVirtualCb = Native12CreateContextVirtual;
    core_callbacks.pfnDestroyContextCb = Native12DestroyContext;
    core_callbacks.pfnCreatePagingQueueCb = Native12CreatePagingQueue;
    core_callbacks.pfnDestroyPagingQueueCb = Native12DestroyPagingQueue;
    core_callbacks.pfnMakeResidentCb = Native12MakeResident;
    core_callbacks.pfnEvictCb = Native12Evict;
    core_callbacks.pfnReclaimAllocations2Cb = Native12ReclaimAllocations2;
    core_callbacks.pfnOfferAllocationsCb = Native12OfferAllocations;
    core_callbacks.pfnAllocateCb = Native12Allocate;
    core_callbacks.pfnDeallocateCb = Native12Deallocate;
    core_callbacks.pfnCreateSchedulingGroupContextCb = Native12CreateSchedulingGroupContext;
    core_callbacks.pfnCreateSchedulingGroupContextVirtualCb = Native12CreateSchedulingGroupContextVirtual;
    core_callbacks.pfnCreateHwQueueCb = Native12CreateHwQueue;
    core_callbacks.pfnQueueBackgroundProcessingWorkCb = Native12QueueBackgroundWork;

    D3D12DDIARG_CALCPRIVATEDEVICESIZE size_args = {};
    size_args.Interface = NATIVE12_DDI_INTERFACE;
    size_args.Version = ddi_build << 16;
    SIZE_T private_size = adapter_functions.pfnCalcPrivateDeviceSize(driver_adapter, &size_args);
    if (!private_size) return E_FAIL;
    driver_device_storage = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, private_size);
    if (!driver_device_storage) return E_OUTOFMEMORY;
    driver_device.pDrvPrivate = driver_device_storage;

    D3D12DDIARG_CREATEDEVICE_0109 create_args = {};
    create_args.hRTDevice.handle = runtime_device;
    create_args.Interface = size_args.Interface;
    create_args.Version = size_args.Version;
    driver_callbacks = kernel_callbacks;
    driver_callbacks.pfnAllocateCb = Native12KernelAllocate;
    if (kernel_callbacks.pfnSubmitCommandCb) driver_callbacks.pfnSubmitCommandCb = Native12KernelSubmit;
    create_args.pKTCallbacks = &driver_callbacks;
    if (WARN_ON(d3d12))
    {
        void **real = reinterpret_cast<void **>(&driver_callbacks);
        void **traced = reinterpret_cast<void **>(&traced_callbacks);
        for (UINT i = 0; i < sizeof(driver_callbacks) / sizeof(void *); ++i)
            traced[i] = real[i] ? Native12KernelThunks[i] : NULL;
        create_args.pKTCallbacks = &traced_callbacks;
    }
    create_args.hDrvDevice = driver_device;
    create_args.p12UMCallbacks_0062 = &core_callbacks;
    if (FAILED(hr = adapter_functions.pfnCreateDevice(driver_adapter, &create_args)))
    {
        WARN("The driver failed to create a DDI build %u device, hr %#lx.\n", ddi_build, hr);
        return hr;
    }
    driver_created = true;

    D3D12DDI_HRTTABLE table = {};
    if (FAILED(hr = adapter_functions.pfnFillDDITable(driver_adapter, D3D12DDI_TABLE_TYPE_DEVICE_CORE,
            &functions, ddi_build >= D3D12DDI_BUILD_VERSION_0110 ? sizeof(functions)
                    : FIELD_OFFSET(NATIVE12_DEVICE_FUNCS, pfnGetProgramIdentifier), 0, table)))
    {
        WARN("The driver failed to fill the device table, hr %#lx.\n", hr);
        return hr;
    }
    if (FAILED(hr = adapter_functions.pfnFillDDITable(driver_adapter, D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D,
            &queue_functions, sizeof(queue_functions), 0, table)))
    {
        WARN("The driver failed to fill the command queue table, hr %#lx.\n", hr);
        return hr;
    }
    SIZE_T list_table_size = ddi_build >= D3D12DDI_BUILD_VERSION_0110 ? sizeof(list_functions)
            : FIELD_OFFSET(NATIVE12_LIST_FUNCS, pfnSetProgram);
    table.handle = &list_functions;
    if (FAILED(hr = adapter_functions.pfnFillDDITable(driver_adapter, D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,
            &list_functions, list_table_size, 0, table)))
    {
        WARN("The driver failed to fill the command list table, hr %#lx.\n", hr);
        return hr;
    }
    if (FAILED(hr = FillListTables(list_table_size))) return hr;

    QueryCaps(D3D12DDICAPS_TYPE_D3D12_OPTIONS, NULL, &options, sizeof(options));
    QueryCaps(D3D12DDICAPS_TYPE_SHADER, NULL, &shader_caps, sizeof(shader_caps));
    QueryCaps(D3D12DDICAPS_TYPE_ARCHITECTURE_INFO, NULL, &architecture, sizeof(architecture));
    UINT node = 0;
    QueryCaps(D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE, &node, &memory, sizeof(memory));
    QueryCaps(D3D12DDICAPS_TYPE_GPUVA_CAPS, &node, &gpuva, sizeof(gpuva));

    UINT model_count = 0;
    D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 models = {};
    models.pNumShaderModelsSupported = &model_count;
    if (SUCCEEDED(QueryCaps(D3D12DDICAPS_TYPE_0011_SHADER_MODELS, NULL, &models, sizeof(models)))
            && model_count && model_count <= 64)
    {
        D3D12DDI_SHADER_MODEL list[64];
        models.pShaderModelsSupported = list;
        if (SUCCEEDED(QueryCaps(D3D12DDICAPS_TYPE_0011_SHADER_MODELS, NULL, &models, sizeof(models))))
            for (UINT i = 0; i < model_count && i < 64; ++i)
            {
                UINT major = (list[i] >> 16) & 0xff, minor = (list[i] >> 4) & 0xf;
                if (!(list[i] & 0x5)) continue;
                UINT model = (major << 4) | minor;
                if (model > highest_shader_model) highest_shader_model = model;
            }
    }
    if (highest_shader_model > D3D_HIGHEST_SHADER_MODEL) highest_shader_model = D3D_HIGHEST_SHADER_MODEL;

    adapter = selected_adapter;
    adapter->AddRef();
    TRACE("Native UMD %s created D3D12 device %p, feature level %#x.\n",
            debugstr_w(name.UmdFileName), this, feature_level);
    return S_OK;
}

HRESULT Native12Device::FillListTables(SIZE_T list_table_size)
{
    D3D12DDI_TABLE_REQUEST *requests;
    UINT32 count = 0, total = 2;
    HRESULT hr = S_OK;

    if (adapter_functions.pfnGetOptionalDDITables
            && SUCCEEDED(adapter_functions.pfnGetOptionalDDITables(driver_adapter, &count, NULL)) && count)
    {
        if (!(requests = static_cast<D3D12DDI_TABLE_REQUEST *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                count * sizeof(*requests)))))
            return E_OUTOFMEMORY;
        if (SUCCEEDED(adapter_functions.pfnGetOptionalDDITables(driver_adapter, &count, requests)))
        {
            for (UINT32 i = 0; i < count; ++i)
                if (requests[i].tableType == D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D) total += requests[i].numTables;
        }
        HeapFree(GetProcessHeap(), 0, requests);
    }

    if (!(list_tables = static_cast<NATIVE12_LIST_FUNCS *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            (total - 1) * sizeof(*list_tables)))))
        return E_OUTOFMEMORY;
    list_table_count = total - 1;
    for (UINT i = 0; i < list_table_count && SUCCEEDED(hr); ++i)
    {
        D3D12DDI_HRTTABLE table;
        table.handle = &list_tables[i];
        if (FAILED(hr = adapter_functions.pfnFillDDITable(driver_adapter, D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,
                &list_tables[i], list_table_size, i + 1, table)))
            WARN("The driver failed to fill command list table %u, hr %#lx.\n", i + 1, hr);
    }
    return hr;
}

const NATIVE12_LIST_FUNCS *Native12Device::FindListTable(HANDLE table)
{
    if (table == &list_functions) return &list_functions;
    for (UINT i = 0; i < list_table_count; ++i)
        if (table == &list_tables[i]) return &list_tables[i];
    return NULL;
}

Native12Device::~Native12Device()
{
    if (driver_created && adapter_functions.pfnDestroyDevice) adapter_functions.pfnDestroyDevice(driver_device);
    if (paging_queue.hPagingQueue && kernel_callbacks.pfnDestroyPagingQueueCb)
    {
        D3DDDI_DESTROYPAGINGQUEUE destroy = {};
        destroy.hPagingQueue = paging_queue.hPagingQueue;
        kernel_callbacks.pfnDestroyPagingQueueCb(runtime_device, &destroy);
    }
    if (listed)
    {
        AcquireSRWLockExclusive(&Native12DeviceListLock);
        for (Native12Device **link = &Native12DeviceList; *link; link = &(*link)->next)
        {
            if (*link == this)
            {
                *link = next;
                break;
            }
        }
        ReleaseSRWLockExclusive(&Native12DeviceListLock);
    }
    if (runtime_device && destroy_callbacks) destroy_callbacks(runtime_device);
    if (adapter_open && adapter_functions.pfnCloseAdapter) adapter_functions.pfnCloseAdapter(driver_adapter);
    if (driver_device_storage) HeapFree(GetProcessHeap(), 0, driver_device_storage);
    if (list_tables) HeapFree(GetProcessHeap(), 0, list_tables);
    if (km_device)
    {
        D3DKMT_DESTROYDEVICE destroy = {};
        destroy.hDevice = km_device;
        D3DKMTDestroyDevice(&destroy);
    }
    if (km_adapter)
    {
        D3DKMT_CLOSEADAPTER close = {};
        close.hAdapter = km_adapter;
        D3DKMTCloseAdapter(&close);
    }
    if (adapter) adapter->Release();
    if (runtime) FreeLibrary(runtime);
    if (umd) FreeLibrary(umd);
}

HRESULT STDMETHODCALLTYPE Native12Device::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12Device) || IsEqualGUID(iid, IID_ID3D12Device1)
            || IsEqualGUID(iid, IID_ID3D12Device2) || IsEqualGUID(iid, IID_ID3D12Device3)
            || IsEqualGUID(iid, IID_ID3D12Device4) || IsEqualGUID(iid, IID_ID3D12Device5)
            || IsEqualGUID(iid, IID_ID3D12Device6) || IsEqualGUID(iid, IID_ID3D12Device7)
            || IsEqualGUID(iid, IID_ID3D12Device8) || IsEqualGUID(iid, IID_ID3D12Device9)
            || IsEqualGUID(iid, IID_ID3D12Device10) || IsEqualGUID(iid, IID_ID3D12Device11)
            || IsEqualGUID(iid, IID_ID3D12Device12) || IsEqualGUID(iid, IID_ID3D12Device13)
            || IsEqualGUID(iid, IID_ID3D12Device14) || IsEqualGUID(iid, IID_ID3D12Object)
            || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12Device14 *>(this);
        AddRef();
        return S_OK;
    }
    if (IsEqualGUID(iid, IID_ID3D12DeviceRemovedExtendedData) || IsEqualGUID(iid, IID_ID3D12DeviceRemovedExtendedData1))
    {
        *out = static_cast<ID3D12DeviceRemovedExtendedData1 *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Native12Device::GetAutoBreadcrumbsOutput(D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT *)
{
    return dred_auto_breadcrumbs == D3D12_DRED_ENABLEMENT_FORCED_ON ? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE : DXGI_ERROR_UNSUPPORTED;
}

HRESULT STDMETHODCALLTYPE Native12Device::GetPageFaultAllocationOutput(D3D12_DRED_PAGE_FAULT_OUTPUT *)
{
    return dred_page_fault == D3D12_DRED_ENABLEMENT_FORCED_ON ? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE : DXGI_ERROR_UNSUPPORTED;
}

HRESULT STDMETHODCALLTYPE Native12Device::GetAutoBreadcrumbsOutput1(D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 *)
{
    return dred_auto_breadcrumbs == D3D12_DRED_ENABLEMENT_FORCED_ON ? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE : DXGI_ERROR_UNSUPPORTED;
}

HRESULT STDMETHODCALLTYPE Native12Device::GetPageFaultAllocationOutput1(D3D12_DRED_PAGE_FAULT_OUTPUT1 *)
{
    return dred_page_fault == D3D12_DRED_ENABLEMENT_FORCED_ON ? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE : DXGI_ERROR_UNSUPPORTED;
}

ULONG STDMETHODCALLTYPE Native12Device::AddRef() { return InterlockedIncrement(&references); }

ULONG STDMETHODCALLTYPE Native12Device::Release()
{
    ULONG count = InterlockedDecrement(&references);
    if (!count) delete this;
    return count;
}

HRESULT STDMETHODCALLTYPE Native12Device::GetPrivateData(REFGUID guid, UINT *size, void *data)
{
    return private_data.Get(guid, size, data);
}

HRESULT STDMETHODCALLTYPE Native12Device::SetPrivateData(REFGUID guid, UINT size, const void *data)
{
    return private_data.Set(guid, size, data, NULL);
}

HRESULT STDMETHODCALLTYPE Native12Device::SetPrivateDataInterface(REFGUID guid, const IUnknown *data)
{
    return private_data.SetInterface(guid, const_cast<IUnknown *>(data));
}

HRESULT STDMETHODCALLTYPE Native12Device::SetName(const WCHAR *) { return S_OK; }
UINT STDMETHODCALLTYPE Native12Device::GetNodeCount() { return 1; }

template <typename T> static bool Native12FeatureData(void *data, UINT size, T **out)
{
    if (!data || size != sizeof(T)) return false;
    *out = static_cast<T *>(data);
    return true;
}

HRESULT STDMETHODCALLTYPE Native12Device::CheckFeatureSupport(D3D12_FEATURE feature, void *data, UINT size)
{
    switch (feature)
    {
        case D3D12_FEATURE_D3D12_OPTIONS:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            caps->DoublePrecisionFloatShaderOps = !!shader_caps.DoubleOps;
            caps->OutputMergerLogicOp = !!options.OutputMergerLogicOp;
            caps->MinPrecisionSupport = static_cast<D3D12_SHADER_MIN_PRECISION_SUPPORT>(shader_caps.MinPrecision);
            caps->TiledResourcesTier = static_cast<D3D12_TILED_RESOURCES_TIER>(options.TiledResourcesTier);
            caps->ResourceBindingTier = static_cast<D3D12_RESOURCE_BINDING_TIER>(options.ResourceBindingTier);
            caps->PSSpecifiedStencilRefSupported = !!shader_caps.ShaderSpecifiedStencilRef;
            caps->TypedUAVLoadAdditionalFormats = !!shader_caps.TypedUAVLoadAdditionalFormats;
            caps->ROVsSupported = !!shader_caps.ROVs;
            caps->ConservativeRasterizationTier =
                    static_cast<D3D12_CONSERVATIVE_RASTERIZATION_TIER>(options.ConservativeRasterizationTier);
            caps->MaxGPUVirtualAddressBitsPerResource = gpuva.MaxGPUVirtualAddressBitsPerResource;
            caps->StandardSwizzle64KBSupported = FALSE;
            caps->CrossNodeSharingTier = D3D12_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED;
            caps->CrossAdapterRowMajorTextureSupported = FALSE;
            caps->VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation =
                    !!options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation;
            caps->ResourceHeapTier = static_cast<D3D12_RESOURCE_HEAP_TIER>(options.ResourceHeapTier);
            return S_OK;
        }
        case D3D12_FEATURE_ARCHITECTURE:
        {
            D3D12_FEATURE_DATA_ARCHITECTURE *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            if (caps->NodeIndex) return E_INVALIDARG;
            caps->TileBasedRenderer = !!architecture.TileBasedDeferredRenderer;
            caps->UMA = !!memory.UMA;
            caps->CacheCoherentUMA = !!memory.CacheCoherent;
            return S_OK;
        }
        case D3D12_FEATURE_ARCHITECTURE1:
        {
            D3D12_FEATURE_DATA_ARCHITECTURE1 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            if (caps->NodeIndex) return E_INVALIDARG;
            caps->TileBasedRenderer = !!architecture.TileBasedDeferredRenderer;
            caps->UMA = !!memory.UMA;
            caps->CacheCoherentUMA = !!memory.CacheCoherent;
            caps->IsolatedMMU = FALSE;
            return S_OK;
        }
        case D3D12_FEATURE_FEATURE_LEVELS:
        {
            D3D12_FEATURE_DATA_FEATURE_LEVELS *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            if (!caps->NumFeatureLevels || !caps->pFeatureLevelsRequested) return E_INVALIDARG;
            caps->MaxSupportedFeatureLevel = static_cast<D3D_FEATURE_LEVEL>(0);
            for (UINT i = 0; i < caps->NumFeatureLevels; ++i)
            {
                D3D_FEATURE_LEVEL level = caps->pFeatureLevelsRequested[i];
                if (level <= feature_level && level > caps->MaxSupportedFeatureLevel)
                    caps->MaxSupportedFeatureLevel = level;
            }
            return S_OK;
        }
        case D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT:
        {
            D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->MaxGPUVirtualAddressBitsPerResource = gpuva.MaxGPUVirtualAddressBitsPerResource;
            caps->MaxGPUVirtualAddressBitsPerProcess = gpuva.MaxGPUVirtualAddressBitsPerResource;
            return S_OK;
        }
        case D3D12_FEATURE_SHADER_MODEL:
        {
            D3D12_FEATURE_DATA_SHADER_MODEL *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            if (caps->HighestShaderModel != D3D_SHADER_MODEL_5_1 && (caps->HighestShaderModel < D3D_SHADER_MODEL_6_0
                    || caps->HighestShaderModel > D3D_HIGHEST_SHADER_MODEL))
                return E_INVALIDARG;
            if (caps->HighestShaderModel > highest_shader_model)
                caps->HighestShaderModel = static_cast<D3D_SHADER_MODEL>(highest_shader_model);
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS1:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS1 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            caps->WaveOps = !!shader_caps.WaveOps;
            caps->WaveLaneCountMin = shader_caps.WaveLaneCountMin;
            caps->WaveLaneCountMax = shader_caps.WaveLaneCountMax;
            caps->TotalLaneCount = shader_caps.TotalLaneCount;
            caps->Int64ShaderOps = !!shader_caps.Int64Ops;
            return S_OK;
        }
        case D3D12_FEATURE_FORMAT_SUPPORT:
        {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            if (caps->Format == DXGI_FORMAT_UNKNOWN)
            {
                caps->Support1 = D3D12_FORMAT_SUPPORT1_BUFFER;
                caps->Support2 = D3D12_FORMAT_SUPPORT2_NONE;
                return S_OK;
            }
            UINT ddi_support = 0;
            functions.pfnCheckFormatSupport(driver_device, caps->Format, &ddi_support);
            Native12FormatSupport(caps->Format, ddi_support, &caps->Support1, &caps->Support2);
            return S_OK;
        }
        case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS:
        {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->NumQualityLevels = 0;
            if (!caps->SampleCount) return E_FAIL;
            if (caps->SampleCount == 1)
            {
                caps->NumQualityLevels = 1;
                return S_OK;
            }
            if (caps->Format == DXGI_FORMAT_UNKNOWN) return S_OK;
            functions.pfnCheckMultisampleQualityLevels(driver_device, caps->Format, caps->SampleCount,
                    static_cast<D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAGS>(caps->Flags), &caps->NumQualityLevels);
            return S_OK;
        }
        case D3D12_FEATURE_FORMAT_INFO:
        {
            D3D12_FEATURE_DATA_FORMAT_INFO *caps;
            Native12FormatInfo info = {};
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            if (caps->Format == DXGI_FORMAT_UNKNOWN)
            {
                caps->PlaneCount = 1;
                return S_OK;
            }
            if (Native12GetFormatInfo(caps->Format, &info) && info.block_bytes)
            {
                caps->PlaneCount = static_cast<UINT8>(info.planes);
                return S_OK;
            }
            switch (caps->Format)
            {
                case DXGI_FORMAT_NV12: case DXGI_FORMAT_P010: case DXGI_FORMAT_P016: case DXGI_FORMAT_NV11:
                    caps->PlaneCount = 2;
                    return S_OK;
                case DXGI_FORMAT_AYUV: case DXGI_FORMAT_Y410: case DXGI_FORMAT_Y416: case DXGI_FORMAT_420_OPAQUE:
                case DXGI_FORMAT_YUY2: case DXGI_FORMAT_Y210: case DXGI_FORMAT_Y216: case DXGI_FORMAT_AI44:
                case DXGI_FORMAT_IA44: case DXGI_FORMAT_P8: case DXGI_FORMAT_A8P8:
                    caps->PlaneCount = 1;
                    return S_OK;
                default:
                    return E_INVALIDARG;
            }
        }
        case D3D12_FEATURE_D3D12_OPTIONS2:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS2 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->DepthBoundsTestSupported = !!options.DepthBoundsTestSupported;
            caps->ProgrammableSamplePositionsTier =
                    static_cast<D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER>(options.ProgrammableSamplePositionsTier);
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS3:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS3 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            caps->CopyQueueTimestampQueriesSupported = !!options.CopyQueueTimestampQueriesSupported;
            caps->BarycentricsSupported = !!options.BarycentricsSupported;
            if (options.WriteBufferImmediateQueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_3D)
                caps->WriteBufferImmediateSupportFlags |= D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT;
            if (options.WriteBufferImmediateQueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE)
                caps->WriteBufferImmediateSupportFlags |= D3D12_COMMAND_LIST_SUPPORT_FLAG_COMPUTE;
            if (options.WriteBufferImmediateQueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_COPY)
                caps->WriteBufferImmediateSupportFlags |= D3D12_COMMAND_LIST_SUPPORT_FLAG_COPY;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS4:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS4 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            caps->Native16BitShaderOpsSupported = !!shader_caps.Native16BitOps;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS5:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            caps->SRVOnlyTiledResourceTier3 = !!options.SRVOnlyTiledResourceTier3;
            caps->RenderPassesTier = D3D12_RENDER_PASS_TIER_0;
            caps->RaytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS6:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS6 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS7:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS7 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            return S_OK;
        }
        case D3D12_FEATURE_EXISTING_HEAPS:
        {
            D3D12_FEATURE_DATA_EXISTING_HEAPS *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->Supported = FALSE;
            return S_OK;
        }
        case D3D12_FEATURE_CROSS_NODE:
        {
            D3D12_FEATURE_DATA_CROSS_NODE *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->SharingTier = static_cast<D3D12_CROSS_NODE_SHARING_TIER>(options.CrossNodeSharingTier);
            caps->AtomicShaderInstructions = FALSE;
            return S_OK;
        }
        case D3D12_FEATURE_SERIALIZATION:
        {
            D3D12_FEATURE_DATA_SERIALIZATION *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            if (caps->NodeIndex) return E_INVALIDARG;
            caps->HeapSerializationTier = D3D12_HEAP_SERIALIZATION_TIER_0;
            return S_OK;
        }
        case D3D12_FEATURE_SHADER_CACHE:
        {
            D3D12_FEATURE_DATA_SHADER_CACHE *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->SupportFlags = D3D12_SHADER_CACHE_SUPPORT_NONE;
            return S_OK;
        }
        case D3D12_FEATURE_COMMAND_QUEUE_PRIORITY:
        {
            D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->PriorityForTypeIsSupported = caps->Priority == D3D12_COMMAND_QUEUE_PRIORITY_NORMAL
                    || caps->Priority == D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
            return S_OK;
        }
        case D3D12_FEATURE_ROOT_SIGNATURE:
        {
            D3D12_FEATURE_DATA_ROOT_SIGNATURE *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            if (caps->HighestVersion > D3D_ROOT_SIGNATURE_VERSION_1_2) return E_INVALIDARG;
            return S_OK;
        }
        case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT:
        {
            D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            if (caps->NodeIndex) return E_INVALIDARG;
            caps->Support = D3D12_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_NONE;
            return S_OK;
        }
        case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT:
        {
            D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            if (caps->NodeIndex) return E_INVALIDARG;
            caps->Count = 0;
            return S_OK;
        }
        case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES:
        {
            D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            if (caps->NodeIndex || caps->Count) return E_INVALIDARG;
            return S_OK;
        }
        case D3D12_FEATURE_DISPLAYABLE:
        {
            D3D12_FEATURE_DATA_DISPLAYABLE *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->DisplayableTexture = FALSE;
            caps->SharedResourceCompatibilityTier = D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER_0;
            return S_OK;
        }
        case D3D12_FEATURE_QUERY_META_COMMAND:
            return E_INVALIDARG;
        case D3D12_FEATURE_D3D12_OPTIONS8:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS8 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->UnalignedBlockTexturesSupported = TRUE;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS9:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS9 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            caps->AtomicInt64OnTypedResourceSupported = !!shader_caps.AtomicInt64OnTypedResource;
            caps->AtomicInt64OnGroupSharedSupported = !!shader_caps.AtomicInt64OnGroupShared;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS10:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS10 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS11:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS11 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->AtomicInt64OnDescriptorHeapResourceSupported = !!shader_caps.AtomicInt64OnDescriptorHeapResource;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS12:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS12 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->MSPrimitivesPipelineStatisticIncludesCulledPrimitives = D3D12_TRI_STATE_UNKNOWN;
            caps->EnhancedBarriersSupported = FALSE;
            caps->RelaxedFormatCastingSupported = FALSE;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS13:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS13 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS14:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS14 *caps;
            D3D12DDI_OPTIONS_DATA_0093 texture_ops = {};
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            if (highest_shader_model >= D3D_SHADER_MODEL_6_7
                    && SUCCEEDED(QueryCaps(D3D12DDICAPS_TYPE_OPTIONS_0093, NULL, &texture_ops, sizeof(texture_ops))))
            {
                caps->AdvancedTextureOpsSupported = !!texture_ops.AdvancedTextureOpsSupported;
                caps->WriteableMSAATexturesSupported = !!texture_ops.WriteableMSAATexturesSupported;
            }
            caps->IndependentFrontAndBackStencilRefMaskSupported = ddi_build >= D3D12DDI_BUILD_VERSION_0095;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS15:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS15 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            caps->TriangleFanSupported = ddi_build >= D3D12DDI_BUILD_VERSION_0097;
            caps->DynamicIndexBufferStripCutSupported = ddi_build >= D3D12DDI_BUILD_VERSION_0099;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS16:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS16 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            caps->DynamicDepthBiasSupported = ddi_build >= D3D12DDI_BUILD_VERSION_0099;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS17:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS17 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS18:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS18 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->RenderPassesValid = TRUE;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS19:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS19 *caps;
            D3D12DDI_OPTIONS_DATA_0101 compute = {};
            D3D12DDI_OPTIONS_DATA_0102 limits = {};
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            ZeroMemory(caps, sizeof(*caps));
            caps->RasterizerDesc2Supported = TRUE;
            caps->NarrowQuadrilateralLinesSupported = ddi_build >= D3D12DDI_BUILD_VERSION_0102;
            caps->AnisoFilterWithPointMipSupported = ddi_build >= D3D12DDI_BUILD_VERSION_0102;
            caps->SupportedSampleCountsWithNoOutputs = 1;
            caps->MaxSamplerDescriptorHeapSize = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
            caps->MaxSamplerDescriptorHeapSizeWithStaticSamplers = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
            caps->MaxViewDescriptorHeapSize = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2;
            if (ddi_build >= D3D12DDI_BUILD_VERSION_0102
                    && SUCCEEDED(QueryCaps(D3D12DDICAPS_TYPE_OPTIONS_0102, NULL, &limits, sizeof(limits))))
            {
                caps->MismatchingOutputDimensionsSupported = TRUE;
                caps->PointSamplingAddressesNeverRoundUp = TRUE;
                caps->SupportedSampleCountsWithNoOutputs = limits.SupportedSampleCountsWithNoOutputs;
                caps->MaxSamplerDescriptorHeapSize = limits.MaxSamplerDescriptorHeapSize;
                caps->MaxSamplerDescriptorHeapSizeWithStaticSamplers = limits.MaxSamplerDescriptorHeapSizeWithStaticSamplers;
                caps->MaxViewDescriptorHeapSize = limits.MaxViewDescriptorHeapSize;
            }
            if (ddi_build >= D3D12DDI_BUILD_VERSION_0101
                    && SUCCEEDED(QueryCaps(D3D12DDICAPS_TYPE_OPTIONS_0101, NULL, &compute, sizeof(compute))))
                caps->ComputeOnlyCustomHeapSupported = !!compute.ComputeOnlyCustomHeapSupported;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS20:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS20 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->ComputeOnlyWriteWatchSupported = FALSE;
            caps->RecreateAtTier = D3D12_RECREATE_AT_TIER_NOT_SUPPORTED;
            return S_OK;
        }
        case D3D12_FEATURE_D3D12_OPTIONS21:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS21 *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            caps->WorkGraphsTier = D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED;
            caps->ExecuteIndirectTier = D3D12_EXECUTE_INDIRECT_TIER_1_0;
            caps->SampleCmpGradientAndBiasSupported = FALSE;
            caps->ExtendedCommandInfoSupported = FALSE;
            return S_OK;
        }
        case D3D12_FEATURE_PREDICATION:
        {
            D3D12_FEATURE_DATA_PREDICATION *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            return E_UNEXPECTED;
        }
        case D3D12_FEATURE_PLACED_RESOURCE_SUPPORT_INFO:
        {
            D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            return E_UNEXPECTED;
        }
        case D3D12_FEATURE_HARDWARE_COPY:
        {
            D3D12_FEATURE_DATA_HARDWARE_COPY *caps;
            if (!Native12FeatureData(data, size, &caps)) return E_INVALIDARG;
            return E_UNEXPECTED;
        }
        default:
            WARN("Unknown feature %#x.\n", feature);
            return E_INVALIDARG;
    }
}

LUID *STDMETHODCALLTYPE Native12Device::GetAdapterLuid(LUID *out)
{
    *out = luid;
    return out;
}

HRESULT STDMETHODCALLTYPE Native12Device::GetDeviceRemovedReason() { return removed_reason; }

HRESULT Native12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_level, REFIID iid, void **out)
{
    IDXGIAdapter *selected = NULL;
    HRESULT hr;

    if (minimum_level < D3D_FEATURE_LEVEL_11_0) return E_INVALIDARG;
    if (adapter)
    {
        if (FAILED(hr = adapter->QueryInterface(IID_IDXGIAdapter, reinterpret_cast<void **>(&selected))))
            return E_INVALIDARG;
    }
    else
    {
        IDXGIFactory1 *factory;
        if (FAILED(hr = CreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void **>(&factory)))) return hr;
        hr = factory->EnumAdapters(0, &selected);
        factory->Release();
        if (FAILED(hr)) return hr;
    }

    Native12Device *device = new Native12Device();
    if (!device)
    {
        selected->Release();
        return E_OUTOFMEMORY;
    }
    hr = device->Initialize(selected, minimum_level);
    selected->Release();
    if (SUCCEEDED(hr))
    {
        if (!out) hr = S_FALSE;
        else hr = device->QueryInterface(iid, out);
    }
    device->Release();
    return hr;
}

extern "C" HRESULT d3d12_native_create_device(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_level, REFIID iid,
        void **out)
{
    return Native12CreateDevice(adapter, minimum_level, iid, out);
}
