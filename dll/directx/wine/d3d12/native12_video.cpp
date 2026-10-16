/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Video decode on the driver's video DDI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "native12.h"
#include "native12_device.h"
#include "native12_objects.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d12);

static_assert(sizeof(D3D12DDI_VIDEO_DECODE_SUPPORT_DATA_0020) == 0x4c, "decode support layout");
static_assert(sizeof(D3D12DDI_DEVICE_FUNCS_VIDEO_0082_0) == 26 * sizeof(void *), "video device table");
static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_DECODE_0063) == 14 * sizeof(void *), "decode list table");
static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_DECODE_0094) == 15 * sizeof(void *), "decode list table");
static_assert(sizeof(D3D12DDI_COMMAND_QUEUE_FUNCS_VIDEO_0020) == 5 * sizeof(void *), "video queue table");
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT) == 0x40, "decode support API layout");

static const GUID Native12EncryptionNone = {0x1b81bed0, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};
static const GUID Native12EncryptionCenc = {0x89d6ac4f, 0x09f2, 0x4229, {0xb2, 0xcd, 0x37, 0x74, 0x0a, 0x6d, 0xfd, 0x81}};

static SRWLOCK Native12VideoListLock = SRWLOCK_INIT;
static Native12VideoDecodeCommandList *Native12VideoLists[64];

static D3D12DDI_VIDEO_DECODE_CONFIGURATION_0020 Native12DecodeConfiguration(
        const D3D12_VIDEO_DECODE_CONFIGURATION &input)
{
    D3D12DDI_VIDEO_DECODE_CONFIGURATION_0020 output = {};

    output.DecodeProfile = input.DecodeProfile;
    output.BitstreamEncryption = static_cast<UINT>(input.BitstreamEncryption) == 1 ? Native12EncryptionCenc
            : Native12EncryptionNone;
    output.InterlaceType = static_cast<D3D12DDI_VIDEO_CODED_INTERLACE_TYPE_0020>(input.InterlaceType);
    return output;
}

static D3D12DDI_HRESOURCE Native12VideoResource(ID3D12Resource *resource)
{
    D3D12DDI_HRESOURCE handle = {};
    if (resource) handle = Native12UnwrapResource(resource)->driver;
    return handle;
}

bool Native12VideoListError(HANDLE handle, HRESULT hr)
{
    bool found = false;

    AcquireSRWLockShared(&Native12VideoListLock);
    for (UINT i = 0; i < ARRAYSIZE(Native12VideoLists) && !found; ++i)
    {
        if (Native12VideoLists[i] != handle) continue;
        found = true;
        if (FAILED(hr) && SUCCEEDED(Native12VideoLists[i]->error)) Native12VideoLists[i]->error = hr;
    }
    ReleaseSRWLockShared(&Native12VideoListLock);
    return found;
}

bool Native12Device::EnsureVideo()
{
    bool ready;

    AcquireSRWLockExclusive(&video_lock);
    if (!video_checked)
    {
        D3D12DDI_EXTENDED_FEATURES_FUNCS_0096 extended = {};
        D3D12DDI_HRTTABLE table = {};
        UINT32 versions[64] = {}, count = ARRAYSIZE(versions);
        UINT version = 0;
        HRESULT hr = E_NOTIMPL;

        video_checked = true;
        if (ddi_build >= D3D12DDI_BUILD_VERSION_0096
                && SUCCEEDED(adapter_functions.pfnFillDDITable(driver_adapter, D3D12DDI_TABLE_TYPE_0096_EXTENDED_FEATURES,
                &extended, sizeof(extended), 0, table))
                && extended.pfnGetSupportedExtendedFeatureVersions && extended.pfnEnableExtendedFeature
                && SUCCEEDED(extended.pfnGetSupportedExtendedFeatureVersions(driver_device, D3D12DDI_FEATURE_0020_VIDEO,
                &count, versions)))
        {
            for (UINT32 i = 0; i < count && i < ARRAYSIZE(versions); ++i)
            {
                if ((versions[i] == D3D12DDI_FEATURE_VERSION_VIDEO_0094_0
                        || versions[i] == D3D12DDI_FEATURE_VERSION_VIDEO_0083_0) && versions[i] > version)
                    version = versions[i];
            }
        }
        if (version && SUCCEEDED(hr = extended.pfnEnableExtendedFeature(driver_device, D3D12DDI_FEATURE_0020_VIDEO,
                version)))
        {
            SIZE_T list_size = version >= D3D12DDI_FEATURE_VERSION_VIDEO_0094_0
                    ? sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_DECODE_0094)
                    : sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_DECODE_0063);

            if (SUCCEEDED(hr = adapter_functions.pfnFillDDITable(driver_adapter, D3D12DDI_TABLE_TYPE_0020_DEVICE_VIDEO,
                    &video_functions, sizeof(video_functions), 0, table)))
            {
                table.handle = &video_queue_functions;
                hr = adapter_functions.pfnFillDDITable(driver_adapter,
                        D3D12DDI_TABLE_TYPE_0022_COMMAND_QUEUE_VIDEO_DECODE, &video_queue_functions,
                        sizeof(video_queue_functions), 0, table);
            }
            if (SUCCEEDED(hr))
            {
                table.handle = &decode_list_functions;
                hr = adapter_functions.pfnFillDDITable(driver_adapter,
                        D3D12DDI_TABLE_TYPE_0022_COMMAND_LIST_VIDEO_DECODE, &decode_list_functions, list_size, 0,
                        table);
            }
            if (SUCCEEDED(hr) && video_functions.pfnGetCaps && video_functions.pfnCreateVideoDecoder
                    && video_queue_functions.pfnExecuteCommandLists && decode_list_functions.pfnDecodeFrame)
                video_version = version;
        }
        for (UINT node = 0; video_version && node < 64 && !video_decode_engine; ++node)
        {
            D3DKMT_NODEMETADATA metadata = {};
            D3DKMT_QUERYADAPTERINFO query = {};

            metadata.NodeOrdinalAndAdapterIndex = node;
            query.hAdapter = km_adapter;
            query.Type = KMTQAITYPE_NODEMETADATA;
            query.pPrivateDriverData = &metadata;
            query.PrivateDriverDataSize = sizeof(metadata);
            if (D3DKMTQueryAdapterInfo(&query) < 0) break;
            video_decode_engine = metadata.NodeData.EngineType == DXGK_ENGINE_TYPE_VIDEO_DECODE;
        }
        TRACE("Video feature version %u, hr %#lx.\n", video_version, hr);
    }
    ready = video_version != 0;
    ReleaseSRWLockExclusive(&video_lock);
    return ready;
}

HRESULT Native12Device::QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0020 type, void *data, UINT size)
{
    D3D12DDIARG_VIDEO_GETCAPS_0020 args = {};

    args.Type = type;
    args.pData = data;
    args.DataSize = size;
    return video_functions.pfnGetCaps(driver_device, &args);
}

HRESULT STDMETHODCALLTYPE Native12Device::CheckFeatureSupport(D3D12_FEATURE_VIDEO feature, void *data, UINT size)
{
    HRESULT hr;

    if (!data || !EnsureVideo()) return E_INVALIDARG;
    switch (feature)
    {
        case D3D12_FEATURE_VIDEO_DECODE_SUPPORT:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT *caps = static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT *>(data);
            D3D12DDI_VIDEO_DECODE_SUPPORT_DATA_0020 query = {};

            if (size != sizeof(*caps)) return E_INVALIDARG;
            query.NodeIndex = caps->NodeIndex;
            query.Configuration = Native12DecodeConfiguration(caps->Configuration);
            query.Width = caps->Width;
            query.Height = caps->Height;
            query.DecodeFormat = caps->DecodeFormat;
            query.FrameRate = caps->FrameRate;
            query.BitRate = caps->BitRate;
            if (FAILED(hr = QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0020_DECODE_SUPPORT, &query, sizeof(query))))
                return hr;
            if ((query.SupportFlags & ~D3D12DDI_VIDEO_DECODE_SUPPORT_FLAG_0020_SUPPORTED)
                    || (query.ConfigurationFlags & ~0xf))
                return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
            caps->SupportFlags = static_cast<D3D12_VIDEO_DECODE_SUPPORT_FLAGS>(query.SupportFlags);
            caps->ConfigurationFlags = static_cast<D3D12_VIDEO_DECODE_CONFIGURATION_FLAGS>(query.ConfigurationFlags);
            caps->DecodeTier = static_cast<D3D12_VIDEO_DECODE_TIER>(query.DecodeTier);
            return S_OK;
        }
        case D3D12_FEATURE_VIDEO_DECODE_PROFILE_COUNT:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILE_COUNT *caps =
                    static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILE_COUNT *>(data);
            D3D12DDI_VIDEO_DECODE_PROFILE_COUNT_DATA_0032 query = {};

            if (size != sizeof(*caps)) return E_INVALIDARG;
            query.NodeIndex = caps->NodeIndex;
            if (FAILED(hr = QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0032_DECODE_PROFILE_COUNT, &query, sizeof(query))))
                return hr;
            caps->ProfileCount = query.ProfileCount;
            return S_OK;
        }
        case D3D12_FEATURE_VIDEO_DECODE_PROFILES:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILES *caps = static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILES *>(data);
            D3D12DDI_VIDEO_DECODE_PROFILE_COUNT_DATA_0032 count = {};
            D3D12DDI_VIDEO_DECODE_PROFILES_DATA_0020 query = {};

            if (size != sizeof(*caps) || !caps->pProfiles) return E_INVALIDARG;
            count.NodeIndex = caps->NodeIndex;
            if (FAILED(hr = QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0032_DECODE_PROFILE_COUNT, &count, sizeof(count))))
                return hr;
            if (caps->ProfileCount != count.ProfileCount) return E_INVALIDARG;
            query.NodeIndex = caps->NodeIndex;
            query.ProfileCount = caps->ProfileCount;
            query.pProfiles = caps->pProfiles;
            return QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0020_DECODE_PROFILES, &query, sizeof(query));
        }
        case D3D12_FEATURE_VIDEO_DECODE_FORMAT_COUNT:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_FORMAT_COUNT *caps =
                    static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_FORMAT_COUNT *>(data);
            D3D12DDI_VIDEO_DECODE_FORMAT_COUNT_DATA_0032 query = {};

            if (size != sizeof(*caps)) return E_INVALIDARG;
            query.NodeIndex = caps->NodeIndex;
            query.Configuration = Native12DecodeConfiguration(caps->Configuration);
            if (FAILED(hr = QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0032_DECODE_FORMAT_COUNT, &query, sizeof(query))))
                return hr;
            caps->FormatCount = query.FormatCount;
            return S_OK;
        }
        case D3D12_FEATURE_VIDEO_DECODE_FORMATS:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_FORMATS *caps = static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_FORMATS *>(data);
            D3D12DDI_VIDEO_DECODE_FORMATS_DATA_0020 query = {};

            if (size != sizeof(*caps) || !caps->pOutputFormats) return E_INVALIDARG;
            query.NodeIndex = caps->NodeIndex;
            query.Configuration = Native12DecodeConfiguration(caps->Configuration);
            query.FormatCount = caps->FormatCount;
            query.pOutputFormats = caps->pOutputFormats;
            return QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0020_DECODE_FORMATS, &query, sizeof(query));
        }
        case D3D12_FEATURE_VIDEO_DECODER_HEAP_SIZE:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODER_HEAP_SIZE *caps =
                    static_cast<D3D12_FEATURE_DATA_VIDEO_DECODER_HEAP_SIZE *>(data);
            const D3D12_VIDEO_DECODER_HEAP_DESC &desc = caps->VideoDecoderHeapDesc;
            D3D12DDI_VIDEO_DECODER_HEAP_SIZE_DATA_0072 query = {};

            if (size != sizeof(*caps)) return E_INVALIDARG;
            query.NodeMask = desc.NodeMask ? desc.NodeMask : 1;
            query.Configuration = Native12DecodeConfiguration(desc.Configuration);
            query.DecodeWidth = desc.DecodeWidth;
            query.DecodeHeight = desc.DecodeHeight;
            query.Format = desc.Format;
            query.FrameRate = desc.FrameRate;
            query.BitRate = desc.BitRate;
            query.MaxDecodePictureBufferCount = desc.MaxDecodePictureBufferCount;
            if (FAILED(hr = QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0032_DECODER_HEAP_SIZE, &query, sizeof(query))))
                return hr;
            caps->MemoryPoolL0Size = query.MemoryPoolL0Size;
            caps->MemoryPoolL1Size = query.MemoryPoolL1Size;
            return S_OK;
        }
        default:
            FIXME("Video feature %u is not implemented.\n", feature);
            return E_INVALIDARG;
    }
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateVideoDecoder(const D3D12_VIDEO_DECODER_DESC *desc, REFIID iid,
        void **out)
{
    Native12VideoDecoder *decoder;
    HRESULT hr;

    if (out) *out = NULL;
    if (!desc || !EnsureVideo()) return E_INVALIDARG;
    if (!(decoder = new Native12VideoDecoder(this))) return E_OUTOFMEMORY;
    hr = decoder->Initialize(desc);
    if (SUCCEEDED(hr)) hr = out ? decoder->QueryInterface(iid, out) : S_FALSE;
    decoder->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateVideoDecoderHeap(const D3D12_VIDEO_DECODER_HEAP_DESC *desc, REFIID iid,
        void **out)
{
    Native12VideoDecoderHeap *heap;
    HRESULT hr;

    if (out) *out = NULL;
    if (!desc || !EnsureVideo()) return E_INVALIDARG;
    if (!(heap = new Native12VideoDecoderHeap(this))) return E_OUTOFMEMORY;
    hr = heap->Initialize(desc);
    if (SUCCEEDED(hr)) hr = out ? heap->QueryInterface(iid, out) : S_FALSE;
    heap->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateVideoProcessor(UINT, const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC *,
        UINT, const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC *, REFIID, void **out)
{
    FIXME("Video processors are not implemented.\n");
    if (out) *out = NULL;
    return E_NOTIMPL;
}

HRESULT Native12VideoDecoder::Initialize(const D3D12_VIDEO_DECODER_DESC *input)
{
    D3D12DDIARG_CREATE_VIDEO_DECODER_0072 args = {};
    SIZE_T size;
    HRESULT hr;

    desc = *input;
    args.NodeMask = desc.NodeMask ? desc.NodeMask : 1;
    args.Configuration = Native12DecodeConfiguration(desc.Configuration);
    size = device->video_functions.pfnCalcPrivateVideoDecoderSize(device->driver_device, &args);
    if (!(driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1))) return E_OUTOFMEMORY;
    if (FAILED(hr = device->video_functions.pfnCreateVideoDecoder(device->driver_device, &args, driver)))
    {
        WARN("The driver failed to create a video decoder for %s, hr %#lx.\n",
                debugstr_guid(&desc.Configuration.DecodeProfile), hr);
        return hr;
    }
    driver_created = true;
    return S_OK;
}

Native12VideoDecoder::~Native12VideoDecoder()
{
    if (driver_created) device->video_functions.pfnDestroyVideoDecoder(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
}

HRESULT STDMETHODCALLTYPE Native12VideoDecoder::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, __uuidof(ID3D12VideoDecoder)) || IsEqualGUID(iid, __uuidof(ID3D12Pageable))
            || IsEqualGUID(iid, __uuidof(ID3D12DeviceChild)) || IsEqualGUID(iid, __uuidof(ID3D12Object))
            || IsEqualGUID(iid, __uuidof(IUnknown)))
    {
        AddRef();
        *out = static_cast<ID3D12VideoDecoder *>(this);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

D3D12_VIDEO_DECODER_DESC *STDMETHODCALLTYPE Native12VideoDecoder::GetDesc(D3D12_VIDEO_DECODER_DESC *out)
{
    *out = desc;
    return out;
}

HRESULT Native12VideoDecoderHeap::Initialize(const D3D12_VIDEO_DECODER_HEAP_DESC *input)
{
    D3D12DDIARG_CREATE_VIDEO_DECODER_HEAP_0072 args = {};
    SIZE_T size;
    HRESULT hr;

    desc = *input;
    args.NodeMask = desc.NodeMask ? desc.NodeMask : 1;
    args.Configuration = Native12DecodeConfiguration(desc.Configuration);
    args.DecodeWidth = desc.DecodeWidth;
    args.DecodeHeight = desc.DecodeHeight;
    args.Format = desc.Format;
    args.FrameRate = desc.FrameRate;
    args.BitRate = desc.BitRate;
    args.MaxDecodePictureBufferCount = desc.MaxDecodePictureBufferCount;
    size = device->video_functions.pfnCalcPrivateVideoDecoderHeapSize(device->driver_device, &args);
    if (!(driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1))) return E_OUTOFMEMORY;
    if (FAILED(hr = device->video_functions.pfnCreateVideoDecoderHeap(device->driver_device, &args, driver)))
    {
        WARN("The driver failed to create a video decoder heap, hr %#lx.\n", hr);
        return hr;
    }
    driver_created = true;
    return S_OK;
}

Native12VideoDecoderHeap::~Native12VideoDecoderHeap()
{
    if (driver_created) device->video_functions.pfnDestroyVideoDecoderHeap(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
}

HRESULT STDMETHODCALLTYPE Native12VideoDecoderHeap::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, __uuidof(ID3D12VideoDecoderHeap)) || IsEqualGUID(iid, __uuidof(ID3D12Pageable))
            || IsEqualGUID(iid, __uuidof(ID3D12DeviceChild)) || IsEqualGUID(iid, __uuidof(ID3D12Object))
            || IsEqualGUID(iid, __uuidof(IUnknown)))
    {
        AddRef();
        *out = static_cast<ID3D12VideoDecoderHeap *>(this);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

D3D12_VIDEO_DECODER_HEAP_DESC *STDMETHODCALLTYPE Native12VideoDecoderHeap::GetDesc(D3D12_VIDEO_DECODER_HEAP_DESC *out)
{
    *out = desc;
    return out;
}

HRESULT Native12VideoDecodeCommandList::Initialize(Native12CommandAllocator *pool)
{
    D3D12DDIARG_CREATE_COMMAND_LIST_0040 args = {};
    D3D12DDIARG_CREATE_COMMAND_RECORDER_0040 recorder_args = {};
    D3D12DDI_HRTCOMMANDLIST runtime = {};
    bool registered = false;
    SIZE_T size;
    HRESULT hr;

    AcquireSRWLockExclusive(&Native12VideoListLock);
    for (UINT i = 0; i < ARRAYSIZE(Native12VideoLists) && !registered; ++i)
    {
        if (Native12VideoLists[i]) continue;
        Native12VideoLists[i] = this;
        registered = true;
    }
    ReleaseSRWLockExclusive(&Native12VideoListLock);
    if (!registered) return E_OUTOFMEMORY;

    id = static_cast<UINT64>(InterlockedIncrement64(&device->list_sequence));
    args.Type = D3D12DDI_COMMAND_LIST_TYPE_DIRECT;
    args.QueueFlags = D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_DECODE;
    args.ID = id;
    args.NodeMask = 1;
    size = device->functions.pfnCalcPrivateCommandListSize(device->driver_device, &args);
    if (!(driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1))) return E_OUTOFMEMORY;
    runtime.handle = this;
    if (FAILED(hr = device->functions.pfnCreateCommandList(device->driver_device, &args, driver, runtime)))
    {
        WARN("The driver failed to create a video decode command list, hr %#lx.\n", hr);
        return hr;
    }
    driver_created = true;

    recorder_args.QueueFlags = args.QueueFlags;
    size = device->functions.pfnCalcPrivateCommandRecorderSize(device->driver_device, &recorder_args);
    if (!(recorder.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1))) return E_OUTOFMEMORY;
    if (FAILED(hr = device->functions.pfnCreateCommandRecorder(device->driver_device, &recorder_args, recorder)))
    {
        WARN("The driver failed to create a video decode command recorder, hr %#lx.\n", hr);
        return hr;
    }
    recorder_created = true;
    return pool ? Reset(pool) : S_OK;
}

Native12VideoDecodeCommandList::~Native12VideoDecodeCommandList()
{
    AcquireSRWLockExclusive(&Native12VideoListLock);
    for (UINT i = 0; i < ARRAYSIZE(Native12VideoLists); ++i)
    {
        if (Native12VideoLists[i] == this) Native12VideoLists[i] = NULL;
    }
    ReleaseSRWLockExclusive(&Native12VideoListLock);
    if (recording && allocator)
        InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&allocator->recording_list), NULL, this);
    if (driver_created) device->functions.pfnDestroyCommandList(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
    if (recorder_created) device->functions.pfnDestroyCommandRecorder(device->driver_device, recorder);
    if (recorder.pDrvPrivate) HeapFree(GetProcessHeap(), 0, recorder.pDrvPrivate);
    if (allocator) allocator->ReleaseInternal();
}

HRESULT STDMETHODCALLTYPE Native12VideoDecodeCommandList::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, __uuidof(ID3D12VideoDecodeCommandList1)) || IsEqualGUID(iid, __uuidof(ID3D12VideoDecodeCommandList))
            || IsEqualGUID(iid, __uuidof(ID3D12CommandList)) || IsEqualGUID(iid, __uuidof(ID3D12DeviceChild))
            || IsEqualGUID(iid, __uuidof(ID3D12Object)) || IsEqualGUID(iid, __uuidof(IUnknown)))
    {
        AddRef();
        *out = static_cast<ID3D12VideoDecodeCommandList1 *>(this);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE Native12VideoDecodeCommandList::GetType()
{
    return D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE;
}

HRESULT STDMETHODCALLTYPE Native12VideoDecodeCommandList::Close()
{
    if (!recording) return E_FAIL;
    device->decode_list_functions.pfnCloseCommandList(driver);
    recording = false;
    if (allocator)
        InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&allocator->recording_list), NULL, this);
    return error;
}

HRESULT STDMETHODCALLTYPE Native12VideoDecodeCommandList::Reset(ID3D12CommandAllocator *pool)
{
    D3D12DDIARG_RESETCOMMANDLIST_0040 args = {};
    Native12CommandAllocator *target, *previous;

    if (recording || !pool) return E_FAIL;
    target = static_cast<Native12CommandAllocator *>(pool);
    if (target->type != D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE) return E_INVALIDARG;
    if (InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&target->recording_list), this, NULL))
        return E_INVALIDARG;
    previous = allocator;
    allocator = target;
    allocator->AddInternal();
    error = S_OK;
    device->functions.pfnCommandRecorderSetCommandPoolAsTarget(device->driver_device, recorder, target->driver);
    args.hDrvCommandRecorder = recorder;
    args.ID = id;
    device->decode_list_functions.pfnResetCommandList(driver, &args);
    recording = true;
    if (previous) previous->ReleaseInternal();
    return error;
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::ClearState()
{
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER *barriers)
{
    D3D12DDIARG_RESOURCE_BARRIER_0022 local[16];
    D3D12DDIARG_RESOURCE_BARRIER_0022 *converted = local;
    UINT used;

    if (!count || !barriers) return;
    if (count > ARRAYSIZE(local))
    {
        converted = static_cast<D3D12DDIARG_RESOURCE_BARRIER_0022 *>(
                HeapAlloc(GetProcessHeap(), 0, count * sizeof(*converted)));
        if (!converted) return;
    }
    used = Native12ConvertResourceBarriers(count, barriers, converted);
    if (used) device->decode_list_functions.pfnResourceBarrier(driver, used, converted);
    if (converted != local) HeapFree(GetProcessHeap(), 0, converted);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::DiscardResource(ID3D12Resource *resource,
        const D3D12_DISCARD_REGION *region)
{
    D3D12DDIARG_DISCARD_RESOURCE_0003 args = {};
    Native12Resource *target;

    if (!resource || !device->decode_list_functions.pfnDiscardResource) return;
    target = Native12UnwrapResource(resource);
    if (region)
    {
        args.NumRects = region->NumRects;
        args.pRects = region->pRects;
        args.FirstSubresource = region->FirstSubresource;
        args.NumSubresources = region->NumSubresources;
    }
    else args.NumSubresources = target->SubresourceCount();
    device->decode_list_functions.pfnDiscardResource(driver, target->driver, &args);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::BeginQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT index)
{
    if (!heap || !device->decode_list_functions.pfnBeginQuery) return;
    device->decode_list_functions.pfnBeginQuery(driver, static_cast<Native12QueryHeap *>(heap)->driver,
            static_cast<D3D12DDI_QUERY_TYPE>(type), index);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::EndQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT index)
{
    if (!heap || !device->decode_list_functions.pfnEndQuery) return;
    device->decode_list_functions.pfnEndQuery(driver, static_cast<Native12QueryHeap *>(heap)->driver,
            static_cast<D3D12DDI_QUERY_TYPE>(type), index);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::ResolveQueryData(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT start, UINT count, ID3D12Resource *buffer, UINT64 offset)
{
    if (!heap || !buffer || !device->decode_list_functions.pfnResolveQueryData) return;
    device->decode_list_functions.pfnResolveQueryData(driver, static_cast<Native12QueryHeap *>(heap)->driver,
            static_cast<D3D12DDI_QUERY_TYPE>(type), start, count, Native12UnwrapResource(buffer)->driver, offset);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::SetPredication(ID3D12Resource *buffer, UINT64 offset,
        D3D12_PREDICATION_OP operation)
{
    if (!device->decode_list_functions.pfnSetPredication) return;
    device->decode_list_functions.pfnSetPredication(driver, Native12VideoResource(buffer), offset,
            static_cast<D3D12DDI_PREDICATION_OP>(operation));
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::SetMarker(UINT, const void *, UINT)
{
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::BeginEvent(UINT, const void *, UINT)
{
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::EndEvent()
{
}

void Native12VideoDecodeCommandList::Decode(ID3D12VideoDecoder *decoder, ID3D12Resource *output,
        UINT output_subresource, const D3D12DDI_VIDEO_DECODE_CONVERSION_ARGUMENTS_0041 &conversion,
        const D3D12_VIDEO_DECODE_OUTPUT_HISTOGRAM *histograms, const D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS *input)
{
    D3D12DDI_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS_0041_1 out_args = {};
    D3D12DDI_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS_0032 in_args = {};
    D3D12DDI_HRESOURCE local_references[32] = {};
    D3D12DDI_HVIDEODECODERHEAP_0032 local_heaps[ARRAYSIZE(local_references)] = {};
    D3D12DDI_HRESOURCE *references = local_references;
    D3D12DDI_HVIDEODECODERHEAP_0032 *reference_heaps = local_heaps;
    UINT reference_count;

    if (!decoder || !input || input->NumFrameArguments > D3D12_VIDEO_DECODE_MAX_ARGUMENTS)
    {
        if (SUCCEEDED(error)) error = E_INVALIDARG;
        return;
    }
    reference_count = input->ReferenceFrames.NumTexture2Ds;
    if (reference_count > ARRAYSIZE(local_references))
    {
        references = static_cast<D3D12DDI_HRESOURCE *>(
                HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, reference_count * sizeof(*references)));
        reference_heaps = static_cast<D3D12DDI_HVIDEODECODERHEAP_0032 *>(
                HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, reference_count * sizeof(*reference_heaps)));
        if (!references || !reference_heaps)
        {
            if (references) HeapFree(GetProcessHeap(), 0, references);
            if (reference_heaps) HeapFree(GetProcessHeap(), 0, reference_heaps);
            if (SUCCEEDED(error)) error = E_OUTOFMEMORY;
            return;
        }
    }
    out_args.hDrvOutputTexture2D = Native12VideoResource(output);
    out_args.OutputSubresource = output_subresource;
    out_args.ConversionArguments = conversion;
    for (UINT i = 0; histograms && i < D3D12DDI_VIDEO_DECODE_MAX_HISTOGRAM_COMPONENTS; ++i)
    {
        out_args.Histograms[i].Offset = histograms[i].Offset;
        out_args.Histograms[i].hDrvBuffer = Native12VideoResource(histograms[i].pBuffer);
    }
    for (UINT i = 0; i < input->NumFrameArguments; ++i)
    {
        in_args.FrameArguments[i].Type =
                static_cast<D3D12DDI_VIDEO_DECODE_PARAMETER_TYPE_0020>(input->FrameArguments[i].Type);
        in_args.FrameArguments[i].pData = input->FrameArguments[i].pData;
        in_args.FrameArguments[i].Size = input->FrameArguments[i].Size;
    }
    in_args.NumFrameArguments = input->NumFrameArguments;
    for (UINT i = 0; i < reference_count; ++i)
    {
        if (input->ReferenceFrames.ppTexture2Ds)
            references[i] = Native12VideoResource(input->ReferenceFrames.ppTexture2Ds[i]);
        if (input->ReferenceFrames.ppHeaps && input->ReferenceFrames.ppHeaps[i])
            reference_heaps[i] = static_cast<Native12VideoDecoderHeap *>(input->ReferenceFrames.ppHeaps[i])->driver;
    }
    in_args.ReferenceFrames.hDrvTexture2Ds = reference_count ? references : NULL;
    in_args.ReferenceFrames.pSubresources = input->ReferenceFrames.pSubresources;
    in_args.ReferenceFrames.hDrvVideoDecoderHeaps = reference_count && input->ReferenceFrames.ppHeaps ? reference_heaps
            : NULL;
    in_args.ReferenceFrames.NumTexture2Ds = reference_count;
    in_args.CompressedBitstream.hDrvBuffer = Native12VideoResource(input->CompressedBitstream.pBuffer);
    in_args.CompressedBitstream.Offset = input->CompressedBitstream.Offset;
    in_args.CompressedBitstream.Size = input->CompressedBitstream.Size;
    if (input->pHeap) in_args.hDrvVideoDecoderHeap = static_cast<Native12VideoDecoderHeap *>(input->pHeap)->driver;
    device->decode_list_functions.pfnDecodeFrame(driver, static_cast<Native12VideoDecoder *>(decoder)->driver,
            &out_args, &in_args);
    if (references != local_references) HeapFree(GetProcessHeap(), 0, references);
    if (reference_heaps != local_heaps) HeapFree(GetProcessHeap(), 0, reference_heaps);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::DecodeFrame(ID3D12VideoDecoder *decoder,
        const D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS *output, const D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS *input)
{
    D3D12DDI_VIDEO_DECODE_CONVERSION_ARGUMENTS_0041 conversion = {};

    if (!output)
    {
        if (SUCCEEDED(error)) error = E_INVALIDARG;
        return;
    }
    conversion.Enable = output->ConversionArguments.Enable;
    conversion.hDrvReferenceTexture2D = Native12VideoResource(output->ConversionArguments.pReferenceTexture2D);
    conversion.ReferenceSubresource = output->ConversionArguments.ReferenceSubresource;
    conversion.OutputColorSpace = output->ConversionArguments.OutputColorSpace;
    conversion.DecodeColorSpace = output->ConversionArguments.DecodeColorSpace;
    Decode(decoder, output->pOutputTexture2D, output->OutputSubresource, conversion, NULL, input);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::DecodeFrame1(ID3D12VideoDecoder *decoder,
        const D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS1 *output, const D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS *input)
{
    D3D12DDI_VIDEO_DECODE_CONVERSION_ARGUMENTS_0041 conversion = {};

    if (!output)
    {
        if (SUCCEEDED(error)) error = E_INVALIDARG;
        return;
    }
    conversion.Enable = output->ConversionArguments.Enable;
    conversion.hDrvReferenceTexture2D = Native12VideoResource(output->ConversionArguments.pReferenceTexture2D);
    conversion.ReferenceSubresource = output->ConversionArguments.ReferenceSubresource;
    conversion.OutputColorSpace = output->ConversionArguments.OutputColorSpace;
    conversion.DecodeColorSpace = output->ConversionArguments.DecodeColorSpace;
    conversion.OutputWidth = output->ConversionArguments.OutputWidth;
    conversion.OutputHeight = output->ConversionArguments.OutputHeight;
    Decode(decoder, output->pOutputTexture2D, output->OutputSubresource, conversion, output->Histograms, input);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::WriteBufferImmediate(UINT count,
        const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *parameters, const D3D12_WRITEBUFFERIMMEDIATE_MODE *modes)
{
    if (!count || !parameters || !device->decode_list_functions.pfnWriteBufferImmediate) return;
    device->decode_list_functions.pfnWriteBufferImmediate(driver, count,
            reinterpret_cast<const D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032 *>(parameters),
            reinterpret_cast<const D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_0032 *>(modes));
}
