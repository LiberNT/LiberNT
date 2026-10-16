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
static_assert(sizeof(D3D12DDI_VIDEO_PROCESS_SUPPORT_DATA_0032) == 0x260, "process support layout");
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT) == 0x260, "process support API layout");
static_assert(sizeof(D3D12DDI_VIDEO_PROCESS_REFERENCE_INFO_DATA_0020) == 0x2c, "process reference layout");
static_assert(sizeof(D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0043) == 0x6c, "processor input layout");
static_assert(sizeof(D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC) == 0x70, "processor input API layout");
static_assert(sizeof(D3D12DDI_VIDEO_PROCESS_OUTPUT_STREAM_DESC_0032) == 0x2c, "processor output layout");
static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_PROCESS_0063) == 14 * sizeof(void *), "process list table");
static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_PROCESS_0094) == 15 * sizeof(void *), "process list table");
static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_ENCODE_0082_0) == 17 * sizeof(void *), "encode list table");
static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_ENCODE_0094) == 18 * sizeof(void *), "encode list table");
static_assert(sizeof(D3D12DDI_VIDEO_MOTION_ESTIMATOR_DATA_0060) == 0x20, "motion estimator caps layout");
static_assert(sizeof(D3D12DDI_VIDEO_MOTION_ESTIMATOR_SIZE_DATA_0060) == 0x48, "motion estimator size layout");
static_assert(sizeof(D3D12DDI_RESOURCE_COORDINATE_0053) == sizeof(D3D12_RESOURCE_COORDINATE), "coordinate layout");

static const GUID Native12EncryptionNone = {0x1b81bed0, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};
static const GUID Native12EncryptionCenc = {0x89d6ac4f, 0x09f2, 0x4229, {0xb2, 0xcd, 0x37, 0x74, 0x0a, 0x6d, 0xfd, 0x81}};

static SRWLOCK Native12VideoListLock = SRWLOCK_INIT;
static Native12VideoListCore *Native12VideoLists[64];

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
            if (video_version)
            {
                SIZE_T process_size = version >= D3D12DDI_FEATURE_VERSION_VIDEO_0094_0
                        ? sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_PROCESS_0094)
                        : sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_PROCESS_0063);

                table.handle = &process_queue_functions;
                if (FAILED(adapter_functions.pfnFillDDITable(driver_adapter,
                        D3D12DDI_TABLE_TYPE_0022_COMMAND_QUEUE_VIDEO_PROCESS, &process_queue_functions,
                        sizeof(process_queue_functions), 0, table)))
                    memset(&process_queue_functions, 0, sizeof(process_queue_functions));
                table.handle = &process_list_functions;
                if (FAILED(adapter_functions.pfnFillDDITable(driver_adapter,
                        D3D12DDI_TABLE_TYPE_0022_COMMAND_LIST_VIDEO_PROCESS, &process_list_functions, process_size, 0,
                        table)))
                    memset(&process_list_functions, 0, sizeof(process_list_functions));
                SIZE_T encode_size = version >= D3D12DDI_FEATURE_VERSION_VIDEO_0094_0
                        ? sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_ENCODE_0094)
                        : sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_ENCODE_0082_0);

                table.handle = &encode_queue_functions;
                if (FAILED(adapter_functions.pfnFillDDITable(driver_adapter,
                        D3D12DDI_TABLE_TYPE_0053_COMMAND_QUEUE_VIDEO_ENCODE, &encode_queue_functions,
                        sizeof(encode_queue_functions), 0, table)))
                    memset(&encode_queue_functions, 0, sizeof(encode_queue_functions));
                table.handle = &encode_list_functions;
                if (FAILED(adapter_functions.pfnFillDDITable(driver_adapter,
                        D3D12DDI_TABLE_TYPE_0053_COMMAND_LIST_VIDEO_ENCODE, &encode_list_functions, encode_size, 0,
                        table)))
                    memset(&encode_list_functions, 0, sizeof(encode_list_functions));
                if (FAILED(QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0053_FEATURE_AREA_SUPPORT, &video_areas,
                        sizeof(video_areas))))
                    memset(&video_areas, 0, sizeof(video_areas));
            }
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

bool Native12Device::EnsureVideoProcess()
{
    return EnsureVideo() && video_functions.pfnCreateVideoProcessor && process_queue_functions.pfnExecuteCommandLists
            && process_list_functions.pfnProcessFrame;
}

static bool Native12ValidSizeRange(const D3D12_VIDEO_SIZE_RANGE &range)
{
    return range.MinWidth && range.MinHeight && range.MinWidth <= range.MaxWidth && range.MinHeight <= range.MaxHeight
            && range.MaxWidth <= 16384 && range.MaxHeight <= 16384;
}

bool Native12Device::EnsureVideoEncode()
{
    return EnsureVideo() && video_areas.VideoEncodeSupport && encode_queue_functions.pfnExecuteCommandLists
            && encode_list_functions.pfnEstimateMotion;
}

HRESULT Native12Device::CheckMotionEstimator(D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR *caps)
{
    D3D12DDI_VIDEO_MOTION_ESTIMATOR_DATA_0060 query = {};
    const D3D12DDI_VIDEO_SIZE_RANGE_0032 &range = query.SizeRange;
    bool none, any;
    HRESULT hr;

    if (caps->NodeIndex >= GetNodeCount()) return E_INVALIDARG;
    caps->BlockSizeFlags = D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAG_NONE;
    caps->PrecisionFlags = D3D12_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_FLAG_NONE;
    memset(&caps->SizeRange, 0, sizeof(caps->SizeRange));
    if (!video_areas.VideoEncodeSupport || caps->InputFormat != DXGI_FORMAT_NV12) return S_OK;
    query.NodeIndex = caps->NodeIndex;
    query.InputFormat = caps->InputFormat;
    if (FAILED(hr = QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0053_MOTION_ESTIMATOR, &query, sizeof(query)))) return hr;
    none = !range.MaxWidth || !range.MaxHeight || !query.BlockSizeFlags || !query.PrecisionFlags;
    any = range.MaxWidth || range.MaxHeight || query.BlockSizeFlags || query.PrecisionFlags;
    if ((query.BlockSizeFlags & ~3u) || (query.PrecisionFlags & ~1u) || range.MaxWidth > 16384 || range.MaxHeight > 16384
            || range.MaxWidth < range.MinWidth || range.MaxHeight < range.MinHeight || (none && any))
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    caps->BlockSizeFlags = static_cast<D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAGS>(query.BlockSizeFlags & 3);
    caps->PrecisionFlags = static_cast<D3D12_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_FLAGS>(query.PrecisionFlags & 1);
    memcpy(&caps->SizeRange, &query.SizeRange, sizeof(caps->SizeRange));
    return S_OK;
}

HRESULT Native12Device::ValidateMotionEstimator(const D3D12_VIDEO_MOTION_ESTIMATOR_DESC *desc, BOOL protected_session)
{
    D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR caps = {};
    UINT mask, index = 0;
    HRESULT hr;

    if (!desc || (desc->NodeMask & (desc->NodeMask - 1))) return E_INVALIDARG;
    mask = desc->NodeMask ? desc->NodeMask : 1;
    while (!(mask & (1u << index))) ++index;
    if (index >= GetNodeCount() || !Native12ValidSizeRange(desc->SizeRange)) return E_INVALIDARG;
    caps.NodeIndex = index;
    caps.InputFormat = desc->InputFormat;
    if (FAILED(hr = CheckMotionEstimator(&caps))) return hr;
    if (!caps.BlockSizeFlags || !(caps.BlockSizeFlags & (1u << (desc->BlockSize & 0x1f)))
            || !(caps.PrecisionFlags & (1u << (desc->Precision & 0x1f)))
            || desc->SizeRange.MaxWidth > caps.SizeRange.MaxWidth || desc->SizeRange.MaxHeight > caps.SizeRange.MaxHeight
            || desc->SizeRange.MinWidth < caps.SizeRange.MinWidth || desc->SizeRange.MinHeight < caps.SizeRange.MinHeight)
        return E_INVALIDARG;
    if (protected_session)
    {
        struct { UINT NodeIndex; UINT SupportFlags; } protection = { index, 0 };

        if (FAILED(QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0072_MOTION_ESTIMATOR_PROTECTED_RESOURCES, &protection,
                sizeof(protection))) || !(protection.SupportFlags & 1))
            return E_INVALIDARG;
    }
    return S_OK;
}

static D3D12DDI_VIDEO_FRAME_STEREO_FORMAT_0020 Native12StereoFormat(D3D12_VIDEO_FRAME_STEREO_FORMAT format)
{
    switch (format)
    {
        case D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE:
        case D3D12_VIDEO_FRAME_STEREO_FORMAT_MONO:
            return D3D12DDI_VIDEO_FRAME_STEREO_FORMAT_0020_MONO;
        case D3D12_VIDEO_FRAME_STEREO_FORMAT_HORIZONTAL:
            return D3D12DDI_VIDEO_FRAME_STEREO_FORMAT_0020_HORIZONTAL;
        case D3D12_VIDEO_FRAME_STEREO_FORMAT_VERTICAL:
            return D3D12DDI_VIDEO_FRAME_STEREO_FORMAT_0020_VERTICAL;
        default:
            return D3D12DDI_VIDEO_FRAME_STEREO_FORMAT_0020_SEPARATE;
    }
}

static D3D12DDI_VIDEO_PROCESS_OUTPUT_STREAM_DESC_0032 Native12ProcessOutputDesc(
        const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC &input)
{
    D3D12DDI_VIDEO_PROCESS_OUTPUT_STREAM_DESC_0032 output = {};

    output.Format = input.Format;
    output.ColorSpace = input.ColorSpace;
    output.AlphaFillMode = static_cast<D3D12DDI_VIDEO_PROCESS_ALPHA_FILL_MODE_0020>(input.AlphaFillMode);
    output.AlphaFillModeSourceStreamIndex = input.AlphaFillModeSourceStreamIndex;
    memcpy(output.BackgroundColor, input.BackgroundColor, sizeof(output.BackgroundColor));
    output.FrameRate = input.FrameRate;
    output.EnableStereo = input.EnableStereo;
    return output;
}

static D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0043 Native12ProcessInputDesc(
        const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC &input)
{
    D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0043 output = {};

    output.Format = input.Format;
    output.ColorSpace = input.ColorSpace;
    output.SourceAspectRatio = input.SourceAspectRatio;
    output.DestinationAspectRatio = input.DestinationAspectRatio;
    output.FrameRate = input.FrameRate;
    memcpy(&output.SourceSizeRange, &input.SourceSizeRange, sizeof(output.SourceSizeRange));
    memcpy(&output.DestinationSizeRange, &input.DestinationSizeRange, sizeof(output.DestinationSizeRange));
    output.EnableOrientation = input.EnableOrientation;
    output.FilterFlags = static_cast<D3D12DDI_VIDEO_PROCESS_FILTER_FLAGS_0020>(input.FilterFlags);
    output.StereoFormat = Native12StereoFormat(input.StereoFormat);
    output.DeinterlaceMode = static_cast<D3D12DDI_VIDEO_PROCESS_DEINTERLACE_FLAGS_0020>(input.DeinterlaceMode);
    output.EnableAlphaBlending = input.EnableAlphaBlending;
    output.LumaKey.Enable = input.LumaKey.Enable;
    output.LumaKey.Lower = input.LumaKey.Lower;
    output.LumaKey.Upper = input.LumaKey.Upper;
    output.NumPastFrames = input.NumPastFrames;
    output.NumFutureFrames = input.NumFutureFrames;
    output.EnableAutoProcessing = input.EnableAutoProcessing;
    return output;
}

static bool Native12ScaleValid(const D3D12DDI_VIDEO_SCALE_SUPPORT_0032 &scale, UINT width, UINT height)
{
    const D3D12DDI_VIDEO_SIZE_RANGE_0032 &range = scale.OutputSizeRange;
    UINT size, steps_x = 0, steps_y = 0;

    if ((scale.Flags & ~3u) || range.MaxWidth > 16384 || range.MaxHeight > 16384 || range.MinWidth > range.MaxWidth
            || range.MinHeight > range.MaxHeight || !range.MinWidth || !range.MinHeight)
        return false;
    if (!(scale.Flags & D3D12DDI_VIDEO_SCALE_SUPPORT_FLAG_0022_POW2_ONLY)) return true;
    for (size = range.MaxWidth; size > range.MinWidth; size = (size + 1) >> 1) ++steps_x;
    if (size != range.MinWidth) return false;
    for (size = range.MaxHeight; size > range.MinHeight; size = (size + 1) >> 1) ++steps_y;
    if (size != range.MinHeight || steps_x != steps_y) return false;
    steps_x = steps_y = 0;
    for (size = range.MaxWidth; size > range.MinWidth && size != width; size = (size + 1) >> 1) ++steps_x;
    if (size != width) return false;
    for (size = range.MaxHeight; size > range.MinHeight && size != height; size = (size + 1) >> 1) ++steps_y;
    if (size != height || steps_x != steps_y) return false;
    return !((range.MaxWidth | range.MinWidth | range.MaxHeight | range.MinHeight) & 1);
}

HRESULT Native12Device::CheckVideoProcessSupport(D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT *caps)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format = {};
    D3D12DDI_VIDEO_PROCESS_SUPPORT_DATA_0032 query = {};
    bool supported = true;
    HRESULT hr;

    caps->SupportFlags = D3D12_VIDEO_PROCESS_SUPPORT_FLAG_NONE;
    memset(&caps->ScaleSupport, 0, sizeof(*caps) - FIELD_OFFSET(D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT, ScaleSupport));
    if (caps->NodeIndex >= GetNodeCount() || !caps->InputSample.Width || !caps->InputSample.Height
            || caps->InputSample.Width > 16384 || caps->InputSample.Height > 16384
            || static_cast<UINT>(caps->InputSample.Format.ColorSpace) > 23
            || static_cast<UINT>(caps->InputFieldType) > 2 || static_cast<UINT>(caps->InputStereoFormat) > 4
            || static_cast<UINT>(caps->OutputFormat.ColorSpace) > 23 || static_cast<UINT>(caps->OutputStereoFormat) > 4)
        return E_INVALIDARG;
    format.Format = caps->InputSample.Format.Format;
    if (FAILED(CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &format, sizeof(format)))
            || !(format.Support1 & D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_INPUT))
        supported = false;
    format.Format = caps->OutputFormat.Format;
    format.Support1 = D3D12_FORMAT_SUPPORT1_NONE;
    if (FAILED(CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &format, sizeof(format)))
            || !(format.Support1 & D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_OUTPUT))
        supported = false;
    if (!supported) return S_OK;

    query.NodeIndex = caps->NodeIndex;
    query.InputSample.Width = caps->InputSample.Width;
    query.InputSample.Height = caps->InputSample.Height;
    query.InputSample.Format.Format = caps->InputSample.Format.Format;
    query.InputSample.Format.ColorSpace = caps->InputSample.Format.ColorSpace;
    query.InputFieldType = static_cast<D3D12DDI_VIDEO_FIELD_TYPE_0020>(caps->InputFieldType);
    query.InputStereoFormat = Native12StereoFormat(caps->InputStereoFormat);
    query.InputFrameRate = caps->InputFrameRate;
    query.OutputFormat.Format = caps->OutputFormat.Format;
    query.OutputFormat.ColorSpace = caps->OutputFormat.ColorSpace;
    query.OutputStereoFormat = Native12StereoFormat(caps->OutputStereoFormat);
    query.OutputFrameRate = caps->OutputFrameRate;
    hr = QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0020_PROCESS_SUPPORT, &query, sizeof(query));
    if ((query.SupportFlags & ~1u) || (query.FeatureSupport & ~0x7fu) || (query.DeinterlaceSupport & 0x7ffffffeu)
            || (query.AutoProcessingSupport & ~0x1ffu) || (query.FilterSupport & ~0xffu))
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    if (FAILED(hr) || !(query.SupportFlags & 1)) return hr;
    if (!Native12ScaleValid(query.ScaleSupport, caps->InputSample.Width, caps->InputSample.Height))
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    caps->SupportFlags = D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED;
    memcpy(&caps->ScaleSupport.OutputSizeRange, &query.ScaleSupport.OutputSizeRange,
            sizeof(caps->ScaleSupport.OutputSizeRange));
    caps->ScaleSupport.Flags = static_cast<D3D12_VIDEO_SCALE_SUPPORT_FLAGS>(query.ScaleSupport.Flags & 3);
    caps->FeatureSupport = static_cast<D3D12_VIDEO_PROCESS_FEATURE_FLAGS>(query.FeatureSupport & 0x7f);
    caps->DeinterlaceSupport = static_cast<D3D12_VIDEO_PROCESS_DEINTERLACE_FLAGS>(query.DeinterlaceSupport & 0x80000001u);
    caps->AutoProcessingSupport = static_cast<D3D12_VIDEO_PROCESS_AUTO_PROCESSING_FLAGS>((query.AutoProcessingSupport & 0xff)
            | ((query.AutoProcessingSupport & D3D12DDI_VIDEO_PROCESS_AUTO_PROCESSING_FLAG_0022_CUSTOM) ? 0x80000000u : 0));
    caps->FilterSupport = static_cast<D3D12_VIDEO_PROCESS_FILTER_FLAGS>(query.FilterSupport & 0xff);
    memcpy(caps->FilterRangeSupport, query.FilterRangeSupport, sizeof(caps->FilterRangeSupport));
    return hr;
}

HRESULT Native12Device::ValidateVideoProcessor(UINT node_mask, const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC *output,
        UINT count, const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC *inputs, BOOL protected_session)
{
    UINT node_index = 0;

    if (!output || (count && !inputs) || (node_mask & (node_mask - 1)) || !count) return E_INVALIDARG;
    if (!node_mask) node_mask = 1;
    while (!(node_mask & (1u << node_index))) ++node_index;
    for (UINT i = 0; i < count; ++i)
    {
        const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC &input = inputs[i];
        D3D12DDI_VIDEO_PROCESS_SUPPORT_DATA_0032 support = {};
        D3D12DDI_VIDEO_PROCESS_REFERENCE_INFO_DATA_0020 reference = {};
        const D3D12DDI_VIDEO_SIZE_RANGE_0032 &range = support.ScaleSupport.OutputSizeRange;
        UINT features;

        if (!Native12ValidSizeRange(input.SourceSizeRange) || !Native12ValidSizeRange(input.DestinationSizeRange))
            return E_INVALIDARG;
        support.NodeIndex = node_index;
        support.InputSample.Width = input.SourceSizeRange.MaxWidth;
        support.InputSample.Height = input.SourceSizeRange.MaxHeight;
        support.InputSample.Format.Format = input.Format;
        support.InputSample.Format.ColorSpace = input.ColorSpace;
        support.InputFieldType = static_cast<D3D12DDI_VIDEO_FIELD_TYPE_0020>(input.FieldType);
        support.InputStereoFormat = static_cast<D3D12DDI_VIDEO_FRAME_STEREO_FORMAT_0020>(input.StereoFormat);
        support.InputFrameRate = input.FrameRate;
        support.OutputFormat.Format = output->Format;
        support.OutputFormat.ColorSpace = output->ColorSpace;
        support.OutputStereoFormat = static_cast<D3D12DDI_VIDEO_FRAME_STEREO_FORMAT_0020>(output->EnableStereo ? 4 : 0);
        support.OutputFrameRate = output->FrameRate;
        if (FAILED(QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0020_PROCESS_SUPPORT, &support, sizeof(support)))
                || !(support.SupportFlags & 1))
            return E_INVALIDARG;
        features = support.FeatureSupport;
        if (input.DestinationSizeRange.MaxWidth > range.MaxWidth || input.DestinationSizeRange.MaxHeight > range.MaxHeight
                || input.DestinationSizeRange.MinWidth < range.MinWidth
                || input.DestinationSizeRange.MinHeight < range.MinHeight
                || (output->AlphaFillMode && !(features & D3D12DDI_VIDEO_PROCESS_FEATURE_SUPPORT_FLAG_0020_ALPHA_FILL))
                || (input.LumaKey.Enable && !(features & D3D12DDI_VIDEO_PROCESS_FEATURE_SUPPORT_FLAG_0020_LUMA_KEY))
                || (input.StereoFormat && !(features & D3D12DDI_VIDEO_PROCESS_FEATURE_SUPPORT_FLAG_0020_STEREO))
                || (input.EnableOrientation && !(features & (D3D12DDI_VIDEO_PROCESS_FEATURE_SUPPORT_FLAG_0020_ROTATION
                        | D3D12DDI_VIDEO_PROCESS_FEATURE_SUPPORT_FLAG_0020_FLIP)))
                || (input.EnableAlphaBlending
                        && !(features & D3D12DDI_VIDEO_PROCESS_FEATURE_SUPPORT_FLAG_0020_ALPHA_BLENDING))
                || ((input.SourceAspectRatio.Numerator != input.DestinationAspectRatio.Numerator
                        || input.SourceAspectRatio.Denominator != input.DestinationAspectRatio.Denominator)
                        && !(features & D3D12DDI_VIDEO_PROCESS_FEATURE_SUPPORT_FLAG_0020_PIXEL_ASPECT_RATIO))
                || (input.DeinterlaceMode && !(input.DeinterlaceMode & support.DeinterlaceSupport))
                || (input.EnableAutoProcessing && !support.AutoProcessingSupport))
            return E_INVALIDARG;
        reference.NodeIndex = node_index;
        reference.DeinterlaceMode = static_cast<D3D12DDI_VIDEO_PROCESS_DEINTERLACE_FLAGS_0020>(input.DeinterlaceMode);
        reference.Filters = static_cast<D3D12DDI_VIDEO_PROCESS_FILTER_FLAGS_0020>(input.FilterFlags);
        reference.InputFrameRate = input.FrameRate;
        reference.OutputFrameRate = output->FrameRate;
        reference.EnableAutoProcessing = input.EnableAutoProcessing;
        if (FAILED(QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0020_PROCESS_REFERENCE_INFO, &reference, sizeof(reference)))
                || (input.NumPastFrames != reference.PastFrames && input.NumFutureFrames != reference.FutureFrames))
            return E_INVALIDARG;
    }
    if (protected_session)
    {
        struct { UINT NodeIndex; UINT SupportFlags; } protection = { node_index, 0 };

        if (FAILED(QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0072_PROCESS_PROTECTED_RESOURCES, &protection,
                sizeof(protection))) || !(protection.SupportFlags & 1))
            return E_INVALIDARG;
    }
    return S_OK;
}

static HRESULT Native12ProcessorDesc(UINT node_mask, const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC *output, UINT count,
        const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC *inputs, D3D12DDIARG_CREATE_VIDEO_PROCESSOR_0072 *args)
{
    D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0043 *converted;

    if (!(converted = static_cast<D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0043 *>(
            HeapAlloc(GetProcessHeap(), 0, count * sizeof(*converted)))))
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < count; ++i) converted[i] = Native12ProcessInputDesc(inputs[i]);
    memset(args, 0, sizeof(*args));
    args->NodeMask = node_mask ? node_mask : 1;
    args->OutputStream = Native12ProcessOutputDesc(*output);
    args->pInputStreams = converted;
    args->NumInputStreams = count;
    return S_OK;
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
        case D3D12_FEATURE_VIDEO_PROCESS_SUPPORT:
            if (size != sizeof(D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT)) return E_INVALIDARG;
            return CheckVideoProcessSupport(static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT *>(data));
        case D3D12_FEATURE_VIDEO_PROCESS_MAX_INPUT_STREAMS:
        {
            D3D12_FEATURE_DATA_VIDEO_PROCESS_MAX_INPUT_STREAMS *caps =
                    static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESS_MAX_INPUT_STREAMS *>(data);
            D3D12DDI_VIDEO_PROCESS_MAX_INPUT_STREAMS_DATA_0020 query = {};

            if (size != sizeof(*caps) || caps->NodeIndex >= GetNodeCount()) return E_INVALIDARG;
            query.NodeIndex = caps->NodeIndex;
            hr = QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0020_PROCESS_MAX_INPUT_STREAMS, &query, sizeof(query));
            caps->MaxInputStreams = query.MaxInputStreams;
            return hr;
        }
        case D3D12_FEATURE_VIDEO_PROCESS_REFERENCE_INFO:
        {
            D3D12_FEATURE_DATA_VIDEO_PROCESS_REFERENCE_INFO *caps =
                    static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESS_REFERENCE_INFO *>(data);
            D3D12DDI_VIDEO_PROCESS_REFERENCE_INFO_DATA_0020 query = {};

            if (size != sizeof(*caps) || caps->NodeIndex >= GetNodeCount()) return E_INVALIDARG;
            query.NodeIndex = caps->NodeIndex;
            query.DeinterlaceMode = static_cast<D3D12DDI_VIDEO_PROCESS_DEINTERLACE_FLAGS_0020>(
                    caps->DeinterlaceMode & 0x80000001u);
            query.Filters = static_cast<D3D12DDI_VIDEO_PROCESS_FILTER_FLAGS_0020>(caps->Filters);
            query.FeatureSupport = static_cast<D3D12DDI_VIDEO_PROCESS_FEATURE_SUPPORT_FLAGS_0020>(
                    caps->FeatureSupport & 0x7f);
            query.InputFrameRate = caps->InputFrameRate;
            query.OutputFrameRate = caps->OutputFrameRate;
            query.EnableAutoProcessing = caps->EnableAutoProcessing;
            hr = QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0020_PROCESS_REFERENCE_INFO, &query, sizeof(query));
            caps->PastFrames = query.PastFrames;
            caps->FutureFrames = query.FutureFrames;
            return hr;
        }
        case D3D12_FEATURE_VIDEO_PROCESSOR_SIZE:
        case D3D12_FEATURE_VIDEO_PROCESSOR_SIZE1:
        {
            D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE1 caps = {};
            D3D12DDIARG_CREATE_VIDEO_PROCESSOR_0072 args;
            D3D12DDI_VIDEO_PROCESSOR_SIZE_DATA_0072 query = {};

            if (feature == D3D12_FEATURE_VIDEO_PROCESSOR_SIZE)
            {
                const D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE *input =
                        static_cast<const D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE *>(data);

                if (size != sizeof(*input)) return E_INVALIDARG;
                caps.NodeMask = input->NodeMask;
                caps.pOutputStreamDesc = input->pOutputStreamDesc;
                caps.NumInputStreamDescs = input->NumInputStreamDescs;
                caps.pInputStreamDescs = input->pInputStreamDescs;
            }
            else
            {
                if (size != sizeof(caps)) return E_INVALIDARG;
                caps = *static_cast<const D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE1 *>(data);
            }
            if (FAILED(hr = ValidateVideoProcessor(caps.NodeMask, caps.pOutputStreamDesc, caps.NumInputStreamDescs,
                    caps.pInputStreamDescs, caps.Protected)))
                return hr;
            if (FAILED(hr = Native12ProcessorDesc(caps.NodeMask, caps.pOutputStreamDesc, caps.NumInputStreamDescs,
                    caps.pInputStreamDescs, &args)))
                return hr;
            query.NodeMask = args.NodeMask;
            query.OutputStream = args.OutputStream;
            query.pInputStreams = args.pInputStreams;
            query.NumInputStreams = args.NumInputStreams;
            query.Protected = caps.Protected;
            hr = QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0032_PROCESSOR_SIZE, &query, sizeof(query));
            HeapFree(GetProcessHeap(), 0, args.pInputStreams);
            if (FAILED(hr)) return hr;
            if (feature == D3D12_FEATURE_VIDEO_PROCESSOR_SIZE)
            {
                static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE *>(data)->MemoryPoolL0Size = query.MemoryPoolL0Size;
                static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE *>(data)->MemoryPoolL1Size = query.MemoryPoolL1Size;
            }
            else
            {
                static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE1 *>(data)->MemoryPoolL0Size = query.MemoryPoolL0Size;
                static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE1 *>(data)->MemoryPoolL1Size = query.MemoryPoolL1Size;
            }
            return S_OK;
        }
        case D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR:
            if (size != sizeof(D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR)) return E_INVALIDARG;
            return CheckMotionEstimator(static_cast<D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR *>(data));
        case D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR_SIZE:
        {
            D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR_SIZE *caps =
                    static_cast<D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR_SIZE *>(data);
            D3D12DDI_VIDEO_MOTION_ESTIMATOR_SIZE_DATA_0060 query = {};

            if (size != sizeof(*caps) || caps->NodeIndex >= GetNodeCount()) return E_INVALIDARG;
            caps->MotionVectorHeapMemoryPoolL0Size = caps->MotionVectorHeapMemoryPoolL1Size = 0;
            caps->MotionEstimatorMemoryPoolL0Size = caps->MotionEstimatorMemoryPoolL1Size = 0;
            if (!video_areas.VideoEncodeSupport || caps->InputFormat != DXGI_FORMAT_NV12) return S_OK;
            query.NodeMask = caps->NodeIndex;
            query.InputFormat = caps->InputFormat;
            query.BlockSize = caps->BlockSize ? D3D12DDI_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_0053_16X16
                    : D3D12DDI_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_0053_8X8;
            query.Precision = D3D12DDI_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_0053_QUARTER_PEL;
            memcpy(&query.SizeRange, &caps->SizeRange, sizeof(query.SizeRange));
            query.Protected = caps->Protected;
            if (FAILED(hr = QueryVideoCaps(D3D12DDICAPS_TYPE_VIDEO_0053_MOTION_ESTIMATOR_SIZE, &query, sizeof(query))))
                return hr;
            if (!query.MotionVectorHeapMemoryPoolL0Size && !query.MotionVectorHeapMemoryPoolL1Size)
                return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
            caps->MotionVectorHeapMemoryPoolL0Size = query.MotionVectorHeapMemoryPoolL0Size;
            caps->MotionVectorHeapMemoryPoolL1Size = query.MotionVectorHeapMemoryPoolL1Size;
            caps->MotionEstimatorMemoryPoolL0Size = query.MotionEstimatorMemoryPoolL0Size;
            caps->MotionEstimatorMemoryPoolL1Size = query.MotionEstimatorMemoryPoolL1Size;
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

HRESULT STDMETHODCALLTYPE Native12Device::CreateVideoProcessor(UINT node_mask,
        const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC *output, UINT count,
        const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC *inputs, REFIID iid, void **out)
{
    Native12VideoProcessor *processor;
    HRESULT hr;

    if (out) *out = NULL;
    if (!EnsureVideo()) return E_INVALIDARG;
    if (FAILED(hr = ValidateVideoProcessor(node_mask, output, count, inputs, FALSE))) return hr;
    if (!(processor = new Native12VideoProcessor(this))) return E_OUTOFMEMORY;
    hr = processor->Initialize(node_mask, output, count, inputs);
    if (SUCCEEDED(hr)) hr = out ? processor->QueryInterface(iid, out) : S_FALSE;
    processor->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateVideoMotionEstimator(const D3D12_VIDEO_MOTION_ESTIMATOR_DESC *desc,
        ID3D12ProtectedResourceSession *session, REFIID iid, void **out)
{
    Native12VideoMotionEstimator *estimator;
    HRESULT hr;

    if (out) *out = NULL;
    if (!EnsureVideo()) return E_INVALIDARG;
    if (FAILED(hr = ValidateMotionEstimator(desc, session != NULL))) return hr;
    if (!(estimator = new Native12VideoMotionEstimator(this))) return E_OUTOFMEMORY;
    hr = estimator->Initialize(desc);
    if (SUCCEEDED(hr)) hr = out ? estimator->QueryInterface(iid, out) : S_FALSE;
    estimator->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateVideoMotionVectorHeap(const D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC *desc,
        ID3D12ProtectedResourceSession *session, REFIID iid, void **out)
{
    Native12VideoMotionVectorHeap *heap;
    HRESULT hr;

    if (out) *out = NULL;
    if (!EnsureVideo()) return E_INVALIDARG;
    if (FAILED(hr = ValidateMotionEstimator(reinterpret_cast<const D3D12_VIDEO_MOTION_ESTIMATOR_DESC *>(desc),
            session != NULL)))
        return hr;
    if (!(heap = new Native12VideoMotionVectorHeap(this))) return E_OUTOFMEMORY;
    hr = heap->Initialize(desc);
    if (SUCCEEDED(hr)) hr = out ? heap->QueryInterface(iid, out) : S_FALSE;
    heap->Release();
    return hr;
}

template <class Args, class Desc> static void Native12MotionArgs(const Desc &desc, Args *args)
{
    args->NodeMask = desc.NodeMask ? desc.NodeMask : 1;
    args->InputFormat = desc.InputFormat;
    args->BlockSize = static_cast<D3D12DDI_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_0053>(desc.BlockSize);
    args->Precision = static_cast<D3D12DDI_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_0053>(desc.Precision);
    memcpy(&args->SizeRange, &desc.SizeRange, sizeof(args->SizeRange));
}

HRESULT Native12VideoMotionEstimator::Initialize(const D3D12_VIDEO_MOTION_ESTIMATOR_DESC *input)
{
    D3D12DDIARG_CREATE_VIDEO_MOTION_ESTIMATOR_0060 args = {};
    SIZE_T size;
    HRESULT hr;

    desc = *input;
    Native12MotionArgs(desc, &args);
    size = device->video_functions.pfnCalcPrivateVideoMotionEstimatorSize(device->driver_device, &args);
    if (!(driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1))) return E_OUTOFMEMORY;
    if (FAILED(hr = device->video_functions.pfnCreateVideoMotionEstimator(device->driver_device, &args, driver)))
    {
        WARN("The driver failed to create a motion estimator, hr %#lx.\n", hr);
        return hr;
    }
    driver_created = true;
    return S_OK;
}

Native12VideoMotionEstimator::~Native12VideoMotionEstimator()
{
    if (driver_created) device->video_functions.pfnDestroyVideoMotionEstimator(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
}

HRESULT STDMETHODCALLTYPE Native12VideoMotionEstimator::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, __uuidof(ID3D12VideoMotionEstimator)) || IsEqualGUID(iid, __uuidof(ID3D12Pageable))
            || IsEqualGUID(iid, __uuidof(ID3D12DeviceChild)) || IsEqualGUID(iid, __uuidof(ID3D12Object))
            || IsEqualGUID(iid, __uuidof(IUnknown)))
    {
        AddRef();
        *out = static_cast<ID3D12VideoMotionEstimator *>(this);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

D3D12_VIDEO_MOTION_ESTIMATOR_DESC *STDMETHODCALLTYPE Native12VideoMotionEstimator::GetDesc(
        D3D12_VIDEO_MOTION_ESTIMATOR_DESC *out)
{
    *out = desc;
    return out;
}

HRESULT STDMETHODCALLTYPE Native12VideoMotionEstimator::GetProtectedResourceSession(REFIID, void **out)
{
    *out = NULL;
    return DXGI_ERROR_NOT_FOUND;
}

HRESULT Native12VideoMotionVectorHeap::Initialize(const D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC *input)
{
    D3D12DDIARG_CREATE_VIDEO_MOTION_VECTOR_HEAP_0060 args = {};
    SIZE_T size;
    HRESULT hr;

    desc = *input;
    Native12MotionArgs(desc, &args);
    size = device->video_functions.pfnCalcPrivateVideoMotionVectorHeapSize(device->driver_device, &args);
    if (!(driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1))) return E_OUTOFMEMORY;
    if (FAILED(hr = device->video_functions.pfnCreateVideoMotionVectorHeap(device->driver_device, &args, driver)))
    {
        WARN("The driver failed to create a motion vector heap, hr %#lx.\n", hr);
        return hr;
    }
    driver_created = true;
    return S_OK;
}

Native12VideoMotionVectorHeap::~Native12VideoMotionVectorHeap()
{
    if (driver_created) device->video_functions.pfnDestroyVideoMotionVectorHeap(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
}

HRESULT STDMETHODCALLTYPE Native12VideoMotionVectorHeap::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, __uuidof(ID3D12VideoMotionVectorHeap)) || IsEqualGUID(iid, __uuidof(ID3D12Pageable))
            || IsEqualGUID(iid, __uuidof(ID3D12DeviceChild)) || IsEqualGUID(iid, __uuidof(ID3D12Object))
            || IsEqualGUID(iid, __uuidof(IUnknown)))
    {
        AddRef();
        *out = static_cast<ID3D12VideoMotionVectorHeap *>(this);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC *STDMETHODCALLTYPE Native12VideoMotionVectorHeap::GetDesc(
        D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC *out)
{
    *out = desc;
    return out;
}

HRESULT STDMETHODCALLTYPE Native12VideoMotionVectorHeap::GetProtectedResourceSession(REFIID, void **out)
{
    *out = NULL;
    return DXGI_ERROR_NOT_FOUND;
}

HRESULT Native12VideoProcessor::Initialize(UINT mask, const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC *output_desc,
        UINT count, const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC *input_descs)
{
    D3D12DDIARG_CREATE_VIDEO_PROCESSOR_0072 args;
    SIZE_T size;
    HRESULT hr;

    node_mask = mask;
    output = *output_desc;
    if (!(inputs = static_cast<D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC *>(
            HeapAlloc(GetProcessHeap(), 0, count * sizeof(*inputs)))))
        return E_OUTOFMEMORY;
    memcpy(inputs, input_descs, count * sizeof(*inputs));
    input_count = count;
    if (FAILED(hr = Native12ProcessorDesc(mask, output_desc, count, input_descs, &args))) return hr;
    size = device->video_functions.pfnCalcPrivateVideoProcessorSize(device->driver_device, &args);
    if (!(driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1)))
        hr = E_OUTOFMEMORY;
    else if (FAILED(hr = device->video_functions.pfnCreateVideoProcessor(device->driver_device, &args, driver)))
        WARN("The driver failed to create a video processor, hr %#lx.\n", hr);
    else
        driver_created = true;
    HeapFree(GetProcessHeap(), 0, args.pInputStreams);
    return hr;
}

Native12VideoProcessor::~Native12VideoProcessor()
{
    if (driver_created) device->video_functions.pfnDestroyVideoProcessor(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
    if (inputs) HeapFree(GetProcessHeap(), 0, inputs);
}

HRESULT STDMETHODCALLTYPE Native12VideoProcessor::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, __uuidof(ID3D12VideoProcessor)) || IsEqualGUID(iid, __uuidof(ID3D12Pageable))
            || IsEqualGUID(iid, __uuidof(ID3D12DeviceChild)) || IsEqualGUID(iid, __uuidof(ID3D12Object))
            || IsEqualGUID(iid, __uuidof(IUnknown)))
    {
        AddRef();
        *out = static_cast<ID3D12VideoProcessor *>(this);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

UINT STDMETHODCALLTYPE Native12VideoProcessor::GetNodeMask()
{
    return node_mask;
}

UINT STDMETHODCALLTYPE Native12VideoProcessor::GetNumInputStreamDescs()
{
    return input_count;
}

HRESULT STDMETHODCALLTYPE Native12VideoProcessor::GetInputStreamDescs(UINT count, D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC *descs)
{
    if (count != input_count || !descs) return E_INVALIDARG;
    memcpy(descs, inputs, count * sizeof(*descs));
    return S_OK;
}

D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC *STDMETHODCALLTYPE Native12VideoProcessor::GetOutputStreamDesc(
        D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC *out)
{
    *out = output;
    return out;
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

template <class Table> static void Native12FillVideoListFunctions(Native12VideoListFunctions *out, const Table &table)
{
    out->close = table.pfnCloseCommandList;
    out->reset = table.pfnResetCommandList;
    out->discard = table.pfnDiscardResource;
    out->set_predication = table.pfnSetPredication;
    out->begin_query = table.pfnBeginQuery;
    out->end_query = table.pfnEndQuery;
    out->resolve_query = table.pfnResolveQueryData;
    out->barrier = table.pfnResourceBarrier;
    out->write_buffer_immediate = table.pfnWriteBufferImmediate;
}

Native12VideoListCore *Native12VideoListFromInterface(ID3D12CommandList *list)
{
    switch (list->GetType())
    {
        case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
            return static_cast<Native12VideoDecodeCommandList *>(static_cast<ID3D12VideoDecodeCommandList1 *>(list));
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
            return static_cast<Native12VideoProcessCommandList *>(static_cast<ID3D12VideoProcessCommandList1 *>(list));
        case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
            return static_cast<Native12VideoEncodeCommandList *>(static_cast<ID3D12VideoEncodeCommandList *>(list));
        default:
            return NULL;
    }
}

Native12VideoListCore::Native12VideoListCore(Native12Device *device, D3D12_COMMAND_LIST_TYPE type)
        : owner(device), list_type(type)
{
    if (type == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS)
        Native12FillVideoListFunctions(&table, device->process_list_functions);
    else if (type == D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE)
        Native12FillVideoListFunctions(&table, device->encode_list_functions);
    else
        Native12FillVideoListFunctions(&table, device->decode_list_functions);
}

HRESULT Native12VideoListCore::InitializeCore()
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

    id = static_cast<UINT64>(InterlockedIncrement64(&owner->list_sequence));
    args.Type = D3D12DDI_COMMAND_LIST_TYPE_DIRECT;
    args.QueueFlags = Native12QueueFlags(list_type);
    args.ID = id;
    args.NodeMask = 1;
    size = owner->functions.pfnCalcPrivateCommandListSize(owner->driver_device, &args);
    if (!(driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1))) return E_OUTOFMEMORY;
    runtime.handle = this;
    if (FAILED(hr = owner->functions.pfnCreateCommandList(owner->driver_device, &args, driver, runtime)))
    {
        WARN("The driver failed to create a video command list of type %u, hr %#lx.\n", list_type, hr);
        return hr;
    }
    driver_created = true;

    recorder_args.QueueFlags = args.QueueFlags;
    size = owner->functions.pfnCalcPrivateCommandRecorderSize(owner->driver_device, &recorder_args);
    if (!(recorder.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1))) return E_OUTOFMEMORY;
    if (FAILED(hr = owner->functions.pfnCreateCommandRecorder(owner->driver_device, &recorder_args, recorder)))
    {
        WARN("The driver failed to create a video command recorder, hr %#lx.\n", hr);
        return hr;
    }
    recorder_created = true;
    return S_OK;
}

void Native12VideoListCore::DestroyCore()
{
    AcquireSRWLockExclusive(&Native12VideoListLock);
    for (UINT i = 0; i < ARRAYSIZE(Native12VideoLists); ++i)
    {
        if (Native12VideoLists[i] == this) Native12VideoLists[i] = NULL;
    }
    ReleaseSRWLockExclusive(&Native12VideoListLock);
    if (recording && allocator)
        InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&allocator->recording_list), NULL, this);
    if (driver_created) owner->functions.pfnDestroyCommandList(owner->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
    if (recorder_created) owner->functions.pfnDestroyCommandRecorder(owner->driver_device, recorder);
    if (recorder.pDrvPrivate) HeapFree(GetProcessHeap(), 0, recorder.pDrvPrivate);
    if (allocator) allocator->ReleaseInternal();
}

HRESULT Native12VideoListCore::CoreClose()
{
    if (!recording) return E_FAIL;
    table.close(driver);
    recording = false;
    if (allocator)
        InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&allocator->recording_list), NULL, this);
    return error;
}

HRESULT Native12VideoListCore::CoreReset(ID3D12CommandAllocator *pool)
{
    D3D12DDIARG_RESETCOMMANDLIST_0040 args = {};
    Native12CommandAllocator *target, *previous;

    if (recording || !pool) return E_FAIL;
    target = static_cast<Native12CommandAllocator *>(pool);
    if (target->type != list_type) return E_INVALIDARG;
    if (InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&target->recording_list), this, NULL))
        return E_INVALIDARG;
    previous = allocator;
    allocator = target;
    allocator->AddInternal();
    error = S_OK;
    if (recorder_pool != target->driver.pDrvPrivate)
    {
        owner->functions.pfnCommandRecorderSetCommandPoolAsTarget(owner->driver_device, recorder, target->driver);
        recorder_pool = target->driver.pDrvPrivate;
    }
    args.hDrvCommandRecorder = recorder;
    args.ID = id;
    table.reset(driver, &args);
    recording = true;
    if (previous) previous->ReleaseInternal();
    return error;
}

void Native12VideoListCore::CoreResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER *barriers)
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
    if (used) table.barrier(driver, used, converted);
    if (converted != local) HeapFree(GetProcessHeap(), 0, converted);
}

void Native12VideoListCore::CoreDiscardResource(ID3D12Resource *resource, const D3D12_DISCARD_REGION *region)
{
    D3D12DDIARG_DISCARD_RESOURCE_0003 args = {};
    Native12Resource *target;

    if (!resource || !table.discard) return;
    target = Native12UnwrapResource(resource);
    if (region)
    {
        args.NumRects = region->NumRects;
        args.pRects = region->pRects;
        args.FirstSubresource = region->FirstSubresource;
        args.NumSubresources = region->NumSubresources;
    }
    else args.NumSubresources = target->SubresourceCount();
    table.discard(driver, target->driver, &args);
}

void Native12VideoListCore::CoreQuery(bool begin, ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    PFND3D12DDI_BEGIN_END_QUERY_0003 function = begin ? table.begin_query : table.end_query;

    if (!heap || !function) return;
    function(driver, static_cast<Native12QueryHeap *>(heap)->driver, static_cast<D3D12DDI_QUERY_TYPE>(type), index);
}

void Native12VideoListCore::CoreResolveQueryData(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start, UINT count,
        ID3D12Resource *buffer, UINT64 offset)
{
    if (!heap || !buffer || !table.resolve_query) return;
    table.resolve_query(driver, static_cast<Native12QueryHeap *>(heap)->driver, static_cast<D3D12DDI_QUERY_TYPE>(type),
            start, count, Native12UnwrapResource(buffer)->driver, offset);
}

void Native12VideoListCore::CoreSetPredication(ID3D12Resource *buffer, UINT64 offset, D3D12_PREDICATION_OP operation)
{
    if (!table.set_predication) return;
    table.set_predication(driver, Native12VideoResource(buffer), offset, static_cast<D3D12DDI_PREDICATION_OP>(operation));
}

void Native12VideoListCore::CoreWriteBufferImmediate(UINT count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *parameters,
        const D3D12_WRITEBUFFERIMMEDIATE_MODE *modes)
{
    if (!count || !parameters || !table.write_buffer_immediate) return;
    table.write_buffer_immediate(driver, count,
            reinterpret_cast<const D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032 *>(parameters),
            reinterpret_cast<const D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_0032 *>(modes));
}

HRESULT Native12VideoDecodeCommandList::Initialize(Native12CommandAllocator *pool)
{
    HRESULT hr = InitializeCore();
    if (FAILED(hr)) return hr;
    return pool ? Reset(pool) : S_OK;
}

Native12VideoDecodeCommandList::~Native12VideoDecodeCommandList()
{
    DestroyCore();
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
    return CoreClose();
}

HRESULT STDMETHODCALLTYPE Native12VideoDecodeCommandList::Reset(ID3D12CommandAllocator *pool)
{
    return CoreReset(pool);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::ClearState()
{
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER *barriers)
{
    CoreResourceBarrier(count, barriers);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::DiscardResource(ID3D12Resource *resource,
        const D3D12_DISCARD_REGION *region)
{
    CoreDiscardResource(resource, region);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::BeginQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT index)
{
    CoreQuery(true, heap, type, index);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::EndQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT index)
{
    CoreQuery(false, heap, type, index);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::ResolveQueryData(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT start, UINT count, ID3D12Resource *buffer, UINT64 offset)
{
    CoreResolveQueryData(heap, type, start, count, buffer, offset);
}

void STDMETHODCALLTYPE Native12VideoDecodeCommandList::SetPredication(ID3D12Resource *buffer, UINT64 offset,
        D3D12_PREDICATION_OP operation)
{
    CoreSetPredication(buffer, offset, operation);
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
    CoreWriteBufferImmediate(count, parameters, modes);
}

HRESULT Native12VideoProcessCommandList::Initialize(Native12CommandAllocator *pool)
{
    HRESULT hr = InitializeCore();
    if (FAILED(hr)) return hr;
    return pool ? Reset(pool) : S_OK;
}

Native12VideoProcessCommandList::~Native12VideoProcessCommandList()
{
    DestroyCore();
}

HRESULT STDMETHODCALLTYPE Native12VideoProcessCommandList::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, __uuidof(ID3D12VideoProcessCommandList1)) || IsEqualGUID(iid, __uuidof(ID3D12VideoProcessCommandList))
            || IsEqualGUID(iid, __uuidof(ID3D12CommandList)) || IsEqualGUID(iid, __uuidof(ID3D12DeviceChild))
            || IsEqualGUID(iid, __uuidof(ID3D12Object)) || IsEqualGUID(iid, __uuidof(IUnknown)))
    {
        AddRef();
        *out = static_cast<ID3D12VideoProcessCommandList1 *>(this);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE Native12VideoProcessCommandList::GetType()
{
    return D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS;
}

HRESULT STDMETHODCALLTYPE Native12VideoProcessCommandList::Close()
{
    return CoreClose();
}

HRESULT STDMETHODCALLTYPE Native12VideoProcessCommandList::Reset(ID3D12CommandAllocator *pool)
{
    return CoreReset(pool);
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::ClearState()
{
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER *barriers)
{
    CoreResourceBarrier(count, barriers);
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::DiscardResource(ID3D12Resource *resource,
        const D3D12_DISCARD_REGION *region)
{
    CoreDiscardResource(resource, region);
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::BeginQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT index)
{
    CoreQuery(true, heap, type, index);
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::EndQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT index)
{
    CoreQuery(false, heap, type, index);
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::ResolveQueryData(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT start, UINT count, ID3D12Resource *buffer, UINT64 offset)
{
    CoreResolveQueryData(heap, type, start, count, buffer, offset);
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::SetPredication(ID3D12Resource *buffer, UINT64 offset,
        D3D12_PREDICATION_OP operation)
{
    CoreSetPredication(buffer, offset, operation);
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::SetMarker(UINT, const void *, UINT)
{
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::BeginEvent(UINT, const void *, UINT)
{
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::EndEvent()
{
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::WriteBufferImmediate(UINT count,
        const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *parameters, const D3D12_WRITEBUFFERIMMEDIATE_MODE *modes)
{
    CoreWriteBufferImmediate(count, parameters, modes);
}

void Native12VideoProcessCommandList::Process(ID3D12VideoProcessor *processor,
        const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS *output, UINT count,
        const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS1 *inputs)
{
    D3D12DDIARG_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS_0032 out_args = {};
    D3D12DDIARG_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS_0043 *in_args;
    D3D12DDI_HRESOURCE *references;
    UINT reference_count = 0, used = 0;

    if (!processor || !output || (count && !inputs))
    {
        if (SUCCEEDED(error)) error = E_INVALIDARG;
        return;
    }
    for (UINT i = 0; i < count; ++i)
        for (UINT view = 0; view < D3D12_VIDEO_PROCESS_STEREO_VIEWS; ++view)
            reference_count += inputs[i].InputStream[view].ReferenceSet.NumPastFrames
                    + inputs[i].InputStream[view].ReferenceSet.NumFutureFrames;
    in_args = static_cast<D3D12DDIARG_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS_0043 *>(
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*in_args) + reference_count * sizeof(*references) + 1));
    if (!in_args)
    {
        if (SUCCEEDED(error)) error = E_OUTOFMEMORY;
        return;
    }
    references = reinterpret_cast<D3D12DDI_HRESOURCE *>(in_args + count);
    for (UINT view = 0; view < D3D12_VIDEO_PROCESS_STEREO_VIEWS; ++view)
    {
        out_args.OutputStream[view].hDrvTexture2D = Native12VideoResource(output->OutputStream[view].pTexture2D);
        out_args.OutputStream[view].Subresource = output->OutputStream[view].Subresource;
    }
    out_args.TargetRectangle = output->TargetRectangle;
    for (UINT i = 0; i < count; ++i)
    {
        const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS1 &input = inputs[i];
        D3D12DDIARG_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS_0043 &args = in_args[i];

        for (UINT view = 0; view < D3D12_VIDEO_PROCESS_STEREO_VIEWS; ++view)
        {
            const D3D12_VIDEO_PROCESS_INPUT_STREAM &stream = input.InputStream[view];
            D3D12DDI_VIDEO_PROCESS_INPUT_STREAM_0020 &target = args.InputStream[view];

            target.hDrvInputTexture = Native12VideoResource(stream.pTexture2D);
            target.Subresource = stream.Subresource;
            target.ReferenceInfo.NumPastFrames = stream.ReferenceSet.NumPastFrames;
            target.ReferenceInfo.hDrvPastFrames = references + used;
            for (UINT j = 0; j < stream.ReferenceSet.NumPastFrames; ++j)
                references[used++] = Native12VideoResource(stream.ReferenceSet.ppPastFrames[j]);
            target.ReferenceInfo.pPastSubresources = stream.ReferenceSet.pPastSubresources;
            target.ReferenceInfo.NumFutureFrames = stream.ReferenceSet.NumFutureFrames;
            target.ReferenceInfo.hDrvFutureFrames = references + used;
            for (UINT j = 0; j < stream.ReferenceSet.NumFutureFrames; ++j)
                references[used++] = Native12VideoResource(stream.ReferenceSet.ppFutureFrames[j]);
            target.ReferenceInfo.pFutureSubresources = stream.ReferenceSet.pFutureSubresources;
        }
        args.Transform.SourceRectangle = input.Transform.SourceRectangle;
        args.Transform.DestinationRectangle = input.Transform.DestinationRectangle;
        args.Transform.Orientation = static_cast<D3D12DDI_VIDEO_PROCESS_ORIENTATION_0020>(input.Transform.Orientation);
        args.Flags = !input.Flags ? D3D12DDI_VIDEO_PROCESS_INPUT_STREAM_FLAG_0020_NONE
                : input.Flags == D3D12_VIDEO_PROCESS_INPUT_STREAM_FLAG_FRAME_DISCONTINUITY
                ? D3D12DDI_VIDEO_PROCESS_INPUT_STREAM_FLAG_0020_FRAME_DISCONTINUITY
                : D3D12DDI_VIDEO_PROCESS_INPUT_STREAM_FLAG_0020_FRAME_REPEAT;
        args.RateInfo.OutputIndex = input.RateInfo.OutputIndex;
        args.RateInfo.InputFrameOrField = input.RateInfo.InputFrameOrField;
        memcpy(args.FilterLevels, input.FilterLevels, sizeof(args.FilterLevels));
        args.AlphaBlending.Enable = input.AlphaBlending.Enable;
        args.AlphaBlending.Alpha = input.AlphaBlending.Alpha;
        args.FieldType = static_cast<D3D12DDI_VIDEO_FIELD_TYPE_0020>(input.FieldType);
    }
    device->process_list_functions.pfnProcessFrame(driver, static_cast<Native12VideoProcessor *>(processor)->driver,
            &out_args, in_args, count);
    HeapFree(GetProcessHeap(), 0, in_args);
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::ProcessFrames(ID3D12VideoProcessor *processor,
        const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS *output, UINT count,
        const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS *inputs)
{
    D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS1 *converted;
    Native12VideoProcessor *native = static_cast<Native12VideoProcessor *>(processor);

    if (!processor || (count && !inputs))
    {
        if (SUCCEEDED(error)) error = E_INVALIDARG;
        return;
    }
    if (!(converted = static_cast<D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS1 *>(
            HeapAlloc(GetProcessHeap(), 0, count * sizeof(*converted) + 1))))
    {
        if (SUCCEEDED(error)) error = E_OUTOFMEMORY;
        return;
    }
    for (UINT i = 0; i < count; ++i)
    {
        memcpy(&converted[i], &inputs[i], sizeof(inputs[i]));
        converted[i].FieldType = i < native->input_count ? native->inputs[i].FieldType : D3D12_VIDEO_FIELD_TYPE_NONE;
    }
    Process(processor, output, count, converted);
    HeapFree(GetProcessHeap(), 0, converted);
}

void STDMETHODCALLTYPE Native12VideoProcessCommandList::ProcessFrames1(ID3D12VideoProcessor *processor,
        const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS *output, UINT count,
        const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS1 *inputs)
{
    Process(processor, output, count, inputs);
}

HRESULT Native12VideoEncodeCommandList::Initialize(Native12CommandAllocator *pool)
{
    HRESULT hr = InitializeCore();
    if (FAILED(hr)) return hr;
    return pool ? Reset(pool) : S_OK;
}

Native12VideoEncodeCommandList::~Native12VideoEncodeCommandList()
{
    DestroyCore();
}

HRESULT STDMETHODCALLTYPE Native12VideoEncodeCommandList::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, __uuidof(ID3D12VideoEncodeCommandList)) || IsEqualGUID(iid, __uuidof(ID3D12CommandList))
            || IsEqualGUID(iid, __uuidof(ID3D12DeviceChild)) || IsEqualGUID(iid, __uuidof(ID3D12Object))
            || IsEqualGUID(iid, __uuidof(IUnknown)))
    {
        AddRef();
        *out = static_cast<ID3D12VideoEncodeCommandList *>(this);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE Native12VideoEncodeCommandList::GetType()
{
    return D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE;
}

HRESULT STDMETHODCALLTYPE Native12VideoEncodeCommandList::Close()
{
    return CoreClose();
}

HRESULT STDMETHODCALLTYPE Native12VideoEncodeCommandList::Reset(ID3D12CommandAllocator *pool)
{
    return CoreReset(pool);
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::ClearState()
{
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER *barriers)
{
    CoreResourceBarrier(count, barriers);
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::DiscardResource(ID3D12Resource *resource,
        const D3D12_DISCARD_REGION *region)
{
    CoreDiscardResource(resource, region);
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::BeginQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT index)
{
    CoreQuery(true, heap, type, index);
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::EndQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT index)
{
    CoreQuery(false, heap, type, index);
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::ResolveQueryData(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type,
        UINT start, UINT count, ID3D12Resource *buffer, UINT64 offset)
{
    CoreResolveQueryData(heap, type, start, count, buffer, offset);
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::SetPredication(ID3D12Resource *buffer, UINT64 offset,
        D3D12_PREDICATION_OP operation)
{
    CoreSetPredication(buffer, offset, operation);
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::SetMarker(UINT, const void *, UINT)
{
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::BeginEvent(UINT, const void *, UINT)
{
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::EndEvent()
{
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::WriteBufferImmediate(UINT count,
        const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *parameters, const D3D12_WRITEBUFFERIMMEDIATE_MODE *modes)
{
    CoreWriteBufferImmediate(count, parameters, modes);
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::SetProtectedResourceSession(ID3D12ProtectedResourceSession *session)
{
    if (session && SUCCEEDED(error)) error = E_INVALIDARG;
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::EstimateMotion(ID3D12VideoMotionEstimator *estimator,
        const D3D12_VIDEO_MOTION_ESTIMATOR_OUTPUT *output, const D3D12_VIDEO_MOTION_ESTIMATOR_INPUT *input)
{
    D3D12DDI_VIDEO_MOTION_ESTIMATOR_OUTPUT_0053 out_args = {};
    D3D12DDI_VIDEO_MOTION_ESTIMATOR_INPUT_0053 in_args = {};

    if (!estimator || !output || !input || !output->pMotionVectorHeap || !input->pInputTexture2D
            || !input->pReferenceTexture2D)
    {
        if (SUCCEEDED(error)) error = E_INVALIDARG;
        return;
    }
    out_args.hDrvMotionVectorHeap = static_cast<Native12VideoMotionVectorHeap *>(output->pMotionVectorHeap)->driver;
    in_args.hDrvInputTexture2D = Native12VideoResource(input->pInputTexture2D);
    in_args.InputSubresourceIndex = input->InputSubresourceIndex;
    in_args.hDrvReferenceTexture2D = Native12VideoResource(input->pReferenceTexture2D);
    in_args.ReferenceSubresourceIndex = input->ReferenceSubresourceIndex;
    if (input->pHintMotionVectorHeap)
        in_args.hDrvPreviousMotionVectorHeap =
                static_cast<Native12VideoMotionVectorHeap *>(input->pHintMotionVectorHeap)->driver;
    device->encode_list_functions.pfnEstimateMotion(driver, static_cast<Native12VideoMotionEstimator *>(estimator)->driver,
            &out_args, &in_args);
}

void STDMETHODCALLTYPE Native12VideoEncodeCommandList::ResolveMotionVectorHeap(
        const D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_OUTPUT *output, const D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_INPUT *input)
{
    D3D12DDI_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_OUTPUT_0060 out_args = {};
    D3D12DDI_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_INPUT_0053 in_args = {};

    if (!output || !input || !output->pMotionVectorTexture2D || !input->pMotionVectorHeap
            || !device->encode_list_functions.pfnResolveVideoMotionVectorHeap)
    {
        if (SUCCEEDED(error)) error = E_INVALIDARG;
        return;
    }
    out_args.hDrvMotionVectorTexture2D = Native12VideoResource(output->pMotionVectorTexture2D);
    memcpy(&out_args.MotionVectorCoordinate, &output->MotionVectorCoordinate, sizeof(out_args.MotionVectorCoordinate));
    in_args.hDrvMotionVectorHeap = static_cast<Native12VideoMotionVectorHeap *>(input->pMotionVectorHeap)->driver;
    in_args.PixelWidth = input->PixelWidth;
    in_args.PixelHeight = input->PixelHeight;
    device->encode_list_functions.pfnResolveVideoMotionVectorHeap(driver, &out_args, &in_args);
}
