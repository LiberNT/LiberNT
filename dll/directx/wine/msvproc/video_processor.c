/* Copyright 2022 Rémi Bernon for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stddef.h>
#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"

#include "d3d9types.h"
#include "d3d11.h"
#include "dshow.h"
#include "dvdmedia.h"
#include "mfapi.h"
#include "mfidl.h"
#include "mferror.h"
#include "mfobjects.h"
#include "mftransform.h"

#include "colorcnv_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dmo);

DEFINE_MEDIATYPE_GUID(MFVideoFormat_ABGR32, D3DFMT_A8B8G8R8);
DEFINE_MEDIATYPE_GUID(MFVideoFormat_P208,MAKEFOURCC('P','2','0','8'));
DEFINE_MEDIATYPE_GUID(MFVideoFormat_I422,MAKEFOURCC('I','4','2','2'));
DEFINE_MEDIATYPE_GUID(MFVideoFormat_I444,MAKEFOURCC('I','4','4','4'));

static const GUID MF_XVP_PLAYBACK_MODE = { 0x3c5d293f, 0xad67, 0x4e29, { 0xaf, 0x12, 0xcf, 0x3e, 0x23, 0x8a, 0xcc, 0xe9 } };

#define MAX_INPUT_STREAMS 4

static const GUID *const input_types[] =
{
    &MFVideoFormat_L8,
    &MFVideoFormat_L16,
    &MFVideoFormat_D16,
    &MFVideoFormat_IYUV,
    &MFVideoFormat_YV12,
    &MFVideoFormat_NV12,
    &MFVideoFormat_NV21,
    &MFVideoFormat_420O,
    &MFVideoFormat_P010,
    &MFVideoFormat_P016,
    &MFVideoFormat_UYVY,
    &MFVideoFormat_YUY2,
    &MFVideoFormat_P208,
    &MFVideoFormat_Y210,
    &MFVideoFormat_Y216,
    &MFVideoFormat_NV11,
    &MFVideoFormat_AYUV,
    &MFVideoFormat_Y410,
    &MFVideoFormat_ARGB32,
    &MFVideoFormat_ABGR32,
    &MFVideoFormat_RGB32,
    &MFVideoFormat_A2R10G10B10,
    &MFVideoFormat_Y416,
    &MFVideoFormat_A16B16G16R16F,
    &MFVideoFormat_RGB24,
    &MFVideoFormat_I420,
    &MFVideoFormat_I422,
    &MFVideoFormat_I444,
    &MFVideoFormat_YVYU,
    &MFVideoFormat_RGB555,
    &MFVideoFormat_RGB565,
    &MFVideoFormat_RGB8,
    &MFVideoFormat_v410,
    &MFVideoFormat_Y41P,
    &MFVideoFormat_Y41T,
    &MFVideoFormat_Y42T,
};
static const GUID *const output_types[] =
{
    &MFVideoFormat_YUY2,
    &MFVideoFormat_IYUV,
    &MFVideoFormat_I420,
    &MFVideoFormat_NV12,
    &MFVideoFormat_RGB24,
    &MFVideoFormat_ARGB32,
    &MFVideoFormat_RGB32,
    &MFVideoFormat_YV12,
    &MFVideoFormat_UYVY,
    &MFVideoFormat_YVYU,
    &MFVideoFormat_AYUV,
    &MFVideoFormat_RGB555,
    &MFVideoFormat_RGB565,
    &MFVideoFormat_ABGR32,
};
static const GUID *const d3d_output_types[] =
{
    &MFVideoFormat_ARGB32,
    &MFVideoFormat_ABGR32,
    &MFVideoFormat_NV12,
    &MFVideoFormat_YUY2,
};

struct video_processor
{
    IMFTransform IMFTransform_iface;
    IMFVideoProcessorControl IMFVideoProcessorControl_iface;
    IMFRealTimeClientEx IMFRealTimeClientEx_iface;
    LONG refcount;

    IMFAttributes *attributes;
    IMFAttributes *output_attributes;

    DWORD input_ids[MAX_INPUT_STREAMS];
    DWORD input_count;

    IMFMediaType *input_type;
    MFVIDEOFORMAT *input_format;
    MFT_INPUT_STREAM_INFO input_info;
    IMFMediaType *output_type;
    MFVIDEOFORMAT *output_format;
    MFT_OUTPUT_STREAM_INFO output_info;

    IMFSample *input_sample;

    IUnknown *device_manager;
    IMFVideoSampleAllocatorEx *allocator;
};

static BOOL is_input_stream(struct video_processor *impl, DWORD id)
{
    DWORD i;

    for (i = 0; i < impl->input_count; i++)
        if (impl->input_ids[i] == id)
            return TRUE;
    return FALSE;
}

static HRESULT check_input_stream(struct video_processor *impl, DWORD id)
{
    if (!is_input_stream(impl, id))
        return MF_E_INVALIDSTREAMNUMBER;
    /* TODO: substreams are accepted but not composited over the reference stream */
    if (id != impl->input_ids[0])
        return E_NOTIMPL;
    return S_OK;
}

static HRESULT lock_media_buffer(IMFMediaBuffer *buffer, BOOL write, BYTE **data, DWORD *size, LONG *pitch)
{
    IMF2DBuffer2 *buffer2;
    BYTE *scanline0;
    HRESULT hr;

    if (SUCCEEDED(hr = IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer2, (void **)&buffer2)))
    {
        hr = IMF2DBuffer2_Lock2DSize(buffer2, write ? MF2DBuffer_LockFlags_Write : MF2DBuffer_LockFlags_Read,
                &scanline0, pitch, data, size);
        IMF2DBuffer2_Release(buffer2);
        if (SUCCEEDED(hr))
            return S_OK;
    }
    *pitch = 0;
    return IMFMediaBuffer_Lock(buffer, data, size, NULL);
}

static void unlock_media_buffer(IMFMediaBuffer *buffer)
{
    IMF2DBuffer2 *buffer2;

    if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer2, (void **)&buffer2)))
    {
        if (SUCCEEDED(IMF2DBuffer2_Unlock2D(buffer2)))
        {
            IMF2DBuffer2_Release(buffer2);
            return;
        }
        IMF2DBuffer2_Release(buffer2);
    }
    IMFMediaBuffer_Unlock(buffer);
}

static BOOL is_mf_video_area_empty(const MFVideoArea *area)
{
    return !area->OffsetX.value && !area->OffsetY.value && !area->Area.cx && !area->Area.cy;
}

static void get_mf_video_content_rect(const MFVideoInfo *info, RECT *rect)
{
    if (!is_mf_video_area_empty(&info->MinimumDisplayAperture))
    {
        rect->left = info->MinimumDisplayAperture.OffsetX.value;
        rect->top = info->MinimumDisplayAperture.OffsetY.value;
        rect->right = rect->left + info->MinimumDisplayAperture.Area.cx;
        rect->bottom = rect->top + info->MinimumDisplayAperture.Area.cy;
    }
    else
    {
        rect->left = 0;
        rect->top = 0;
        rect->right = info->dwWidth;
        rect->bottom = info->dwHeight;
    }
}

static HRESULT fill_planes(const MFVIDEOFORMAT *format, BYTE *data, DWORD size, LONG pitch, struct pixel_planes *planes)
{
    MFVIDEOFORMAT layout = *format;

    if (pitch > 0)
        layout.videoInfo.VideoFlags &= ~MFVideoFlag_BottomUpLinearRep;
    if (!pixels_fill_planes(&layout, data, size, pitch, planes))
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT video_processor_convert(struct video_processor *impl, IMFSample *input, IMFSample *output, DWORD *length)
{
    MFVIDEOFORMAT input_format = *impl->input_format, output_format = *impl->output_format;
    IMFMediaBuffer *input_buffer, *output_buffer;
    DWORD input_size, output_size, max_length;
    struct pixel_planes src, dst;
    BYTE *input_data, *output_data;
    LONG input_pitch, output_pitch;
    RECT input_rect, output_rect;
    HRESULT hr;

    if (FAILED(IMFSample_ConvertToContiguousBuffer(output, &output_buffer)))
        return E_INVALIDARG;
    if (FAILED(hr = IMFSample_ConvertToContiguousBuffer(input, &input_buffer)))
    {
        IMFMediaBuffer_Release(output_buffer);
        return hr;
    }

    if (SUCCEEDED(hr = lock_media_buffer(input_buffer, FALSE, &input_data, &input_size, &input_pitch)))
    {
        if (!input_pitch && SUCCEEDED(IMFMediaBuffer_GetMaxLength(input_buffer, &max_length)))
            input_size = max(input_size, max_length);
        if (SUCCEEDED(hr = lock_media_buffer(output_buffer, TRUE, &output_data, &output_size, &output_pitch)))
        {
            if (!output_pitch && SUCCEEDED(IMFMediaBuffer_GetMaxLength(output_buffer, &max_length)))
                output_size = max_length;
            if (SUCCEEDED(hr = fill_planes(impl->input_format, input_data, input_size, input_pitch, &src))
                    && SUCCEEDED(hr = fill_planes(impl->output_format, output_data, output_size, output_pitch, &dst)))
            {
                get_mf_video_content_rect(&impl->input_format->videoInfo, &input_rect);
                get_mf_video_content_rect(&impl->output_format->videoInfo, &output_rect);
                /* FIXME: content rectangles of different sizes are cropped to the common area instead of scaled */
                input_format.videoInfo.dwWidth = output_format.videoInfo.dwWidth
                        = min(input_rect.right, output_rect.right);
                input_format.videoInfo.dwHeight = output_format.videoInfo.dwHeight
                        = min(input_rect.bottom, output_rect.bottom);
                pixels_convert(&input_format, &src, &output_format, &dst,
                        PIXELS_OPAQUE_ALPHA | PIXELS_NEAREST_CHROMA | PIXELS_ROUNDED_MATRIX);
                *length = pixels_fill_planes(impl->output_format, NULL, ~0u, 0, NULL);
            }
            unlock_media_buffer(output_buffer);
            if (SUCCEEDED(hr))
                IMFMediaBuffer_SetCurrentLength(output_buffer, *length);
        }
        unlock_media_buffer(input_buffer);
    }

    IMFMediaBuffer_Release(output_buffer);
    IMFMediaBuffer_Release(input_buffer);
    return hr;
}

static void video_processor_cleanup(struct video_processor *impl)
{
    CoTaskMemFree(impl->output_format);
    impl->output_format = NULL;
    CoTaskMemFree(impl->input_format);
    impl->input_format = NULL;
}

static void video_processor_flush(struct video_processor *impl)
{
    if (impl->input_sample)
        IMFSample_Release(impl->input_sample);
    impl->input_sample = NULL;
}

static HRESULT create_video_format(BOOL bottom_up, IMFMediaType *media_type, MFVIDEOFORMAT **format)
{
    LONG stride;
    UINT32 size;
    HRESULT hr;

    if (FAILED(hr = MFCreateMFVideoFormatFromMFMediaType(media_type, format, &size)))
        return hr;

    if (bottom_up && FAILED(IMFMediaType_GetItem(media_type, &MF_MT_DEFAULT_STRIDE, NULL))
            && SUCCEEDED(MFGetStrideForBitmapInfoHeader((*format)->guidFormat.Data1, 1, &stride)) && stride < 0)
        (*format)->videoInfo.VideoFlags |= MFVideoFlag_BottomUpLinearRep;

    return hr;
}

static HRESULT video_processor_init(struct video_processor *impl)
{
    BOOL bottom_up = !impl->device_manager; /* when not D3D-enabled, the transform outputs bottom up RGB buffers */
    HRESULT hr;

    video_processor_cleanup(impl);

    if (FAILED(hr = create_video_format(bottom_up, impl->input_type, &impl->input_format))
            || FAILED(hr = create_video_format(bottom_up, impl->output_type, &impl->output_format)))
    {
        video_processor_cleanup(impl);
        return hr;
    }

    if (!pixels_can_convert(impl->input_format, impl->output_format))
    {
        /* TODO: formats outside the shared pixel layer (10 and 16 bit, 4:1:1, I422, I444, RGB8, ...) */
        FIXME("Unsupported conversion %s -> %s\n", debugstr_guid(&impl->input_format->guidFormat),
                debugstr_guid(&impl->output_format->guidFormat));
        video_processor_cleanup(impl);
        return MF_E_INVALIDMEDIATYPE;
    }

    return S_OK;
}

static HRESULT video_processor_init_allocator(struct video_processor *processor)
{
    IMFVideoSampleAllocatorEx *allocator;
    IMFAttributes *attributes;
    UINT32 count, bind_flags;
    GUID subtype;
    HRESULT hr;

    if (processor->allocator)
        return S_OK;

    if (FAILED(hr = MFCreateAttributes(&attributes, 1)))
        return hr;
    bind_flags = D3D11_BIND_RENDER_TARGET;
    if (SUCCEEDED(IMFMediaType_GetGUID(processor->output_type, &MF_MT_SUBTYPE, &subtype))
            && (IsEqualGUID(&subtype, &MFVideoFormat_ARGB32) || IsEqualGUID(&subtype, &MFVideoFormat_RGB32)))
        bind_flags |= D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(hr = IMFAttributes_CopyAllItems(processor->output_attributes, attributes))
            || (processor->device_manager && FAILED(hr = IMFAttributes_SetUINT32(attributes,
            &MF_SA_D3D11_BINDFLAGS, bind_flags)))
            || FAILED(hr = MFCreateVideoSampleAllocatorEx(&IID_IMFVideoSampleAllocatorEx, (void **)&allocator)))
    {
        IMFAttributes_Release(attributes);
        return hr;
    }
    if (FAILED(IMFAttributes_GetUINT32(processor->attributes, &MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT, &count)))
        count = 2;
    hr = IMFVideoSampleAllocatorEx_SetDirectXManager(allocator, processor->device_manager);
    if (SUCCEEDED(hr))
        hr = IMFVideoSampleAllocatorEx_InitializeSampleAllocatorEx(allocator, count, max(count + 2, 10),
                attributes, processor->output_type);
    IMFAttributes_Release(attributes);
    if (FAILED(hr))
    {
        IMFVideoSampleAllocatorEx_Release(allocator);
        return hr;
    }

    processor->allocator = allocator;
    return S_OK;
}

static HRESULT video_processor_uninit_allocator(struct video_processor *processor)
{
    HRESULT hr;

    if (!processor->allocator)
        return S_OK;

    if (SUCCEEDED(hr = IMFVideoSampleAllocatorEx_UninitializeSampleAllocator(processor->allocator)))
        hr = IMFVideoSampleAllocatorEx_SetDirectXManager(processor->allocator, NULL);
    IMFVideoSampleAllocatorEx_Release(processor->allocator);
    processor->allocator = NULL;

    return hr;
}

static struct video_processor *impl_from_IMFTransform(IMFTransform *iface)
{
    return CONTAINING_RECORD(iface, struct video_processor, IMFTransform_iface);
}

static HRESULT WINAPI video_processor_QueryInterface(IMFTransform *iface, REFIID iid, void **out)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IMFTransform))
        *out = &impl->IMFTransform_iface;
    else if (IsEqualGUID(iid, &IID_IMFVideoProcessorControl))
        *out = &impl->IMFVideoProcessorControl_iface;
    else if (IsEqualGUID(iid, &IID_IMFRealTimeClientEx))
        *out = &impl->IMFRealTimeClientEx_iface;
    else
    {
        /* TODO: IMFVideoProcessorControl2, IMFVideoProcessorControl3 and IMFGetService */
        *out = NULL;
        WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI video_processor_AddRef(IMFTransform *iface)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    ULONG refcount = InterlockedIncrement(&impl->refcount);

    TRACE("iface %p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI video_processor_Release(IMFTransform *iface)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    ULONG refcount = InterlockedDecrement(&impl->refcount);

    TRACE("iface %p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        video_processor_flush(impl);
        video_processor_uninit_allocator(impl);
        if (impl->device_manager)
            IUnknown_Release(impl->device_manager);
        if (impl->input_type)
            IMFMediaType_Release(impl->input_type);
        if (impl->output_type)
            IMFMediaType_Release(impl->output_type);
        if (impl->attributes)
            IMFAttributes_Release(impl->attributes);
        if (impl->output_attributes)
            IMFAttributes_Release(impl->output_attributes);
        video_processor_cleanup(impl);
        free(impl);
    }

    return refcount;
}

static HRESULT WINAPI video_processor_GetStreamLimits(IMFTransform *iface, DWORD *input_minimum,
        DWORD *input_maximum, DWORD *output_minimum, DWORD *output_maximum)
{
    TRACE("iface %p, input_minimum %p, input_maximum %p, output_minimum %p, output_maximum %p.\n",
            iface, input_minimum, input_maximum, output_minimum, output_maximum);
    *input_minimum = *output_minimum = *output_maximum = 1;
    *input_maximum = MAX_INPUT_STREAMS;
    return S_OK;
}

static HRESULT WINAPI video_processor_GetStreamCount(IMFTransform *iface, DWORD *inputs, DWORD *outputs)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);

    TRACE("iface %p, inputs %p, outputs %p.\n", iface, inputs, outputs);
    *inputs = impl->input_count;
    *outputs = 1;
    return S_OK;
}

static HRESULT WINAPI video_processor_GetStreamIDs(IMFTransform *iface, DWORD input_size, DWORD *inputs,
        DWORD output_size, DWORD *outputs)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);

    TRACE("iface %p, input_size %lu, inputs %p, output_size %lu, outputs %p.\n", iface,
            input_size, inputs, output_size, outputs);

    if (inputs)
        memcpy(inputs, impl->input_ids, min(input_size, impl->input_count) * sizeof(*inputs));
    if (outputs && output_size)
        outputs[0] = 0;
    return S_OK;
}

static HRESULT WINAPI video_processor_GetInputStreamInfo(IMFTransform *iface, DWORD id, MFT_INPUT_STREAM_INFO *info)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);

    TRACE("iface %p, id %#lx, info %p.\n", iface, id, info);

    if (!is_input_stream(impl, id))
        return MF_E_INVALIDSTREAMNUMBER;

    if (id == impl->input_ids[0])
        *info = impl->input_info;
    else
        memset(info, 0, sizeof(*info));
    return S_OK;
}

static HRESULT WINAPI video_processor_GetOutputStreamInfo(IMFTransform *iface, DWORD id, MFT_OUTPUT_STREAM_INFO *info)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);

    TRACE("iface %p, id %#lx, info %p.\n", iface, id, info);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;

    *info = impl->output_info;
    return S_OK;
}

static HRESULT WINAPI video_processor_GetAttributes(IMFTransform *iface, IMFAttributes **attributes)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);

    TRACE("iface %p, attributes %p\n", iface, attributes);

    if (!attributes)
        return E_POINTER;

    IMFAttributes_AddRef((*attributes = impl->attributes));
    return S_OK;
}

static HRESULT WINAPI video_processor_GetInputStreamAttributes(IMFTransform *iface, DWORD id, IMFAttributes **attributes)
{
    TRACE("iface %p, id %#lx, attributes %p.\n", iface, id, attributes);
    return E_NOTIMPL;
}

static HRESULT WINAPI video_processor_GetOutputStreamAttributes(IMFTransform *iface, DWORD id, IMFAttributes **attributes)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);

    TRACE("iface %p, id %#lx, attributes %p\n", iface, id, attributes);

    if (!attributes)
        return E_POINTER;
    if (id)
        return MF_E_INVALIDSTREAMNUMBER;

    IMFAttributes_AddRef((*attributes = impl->output_attributes));
    return S_OK;
}

static HRESULT WINAPI video_processor_DeleteInputStream(IMFTransform *iface, DWORD id)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    DWORD i;

    TRACE("iface %p, id %#lx.\n", iface, id);

    for (i = 0; i < impl->input_count; i++)
        if (impl->input_ids[i] == id)
            break;
    if (i == impl->input_count)
        return MF_E_INVALIDSTREAMNUMBER;
    if (impl->input_count == 1)
        return MF_E_INVALIDREQUEST;

    memmove(impl->input_ids + i, impl->input_ids + i + 1, (impl->input_count - i - 1) * sizeof(*impl->input_ids));
    impl->input_count--;
    return S_OK;
}

static HRESULT WINAPI video_processor_AddInputStreams(IMFTransform *iface, DWORD streams, DWORD *ids)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);

    TRACE("iface %p, streams %lu, ids %p.\n", iface, streams, ids);

    if (!streams || !ids)
        return E_INVALIDARG;
    if (streams > MAX_INPUT_STREAMS - impl->input_count)
        return MF_E_INVALIDREQUEST;

    memcpy(impl->input_ids + impl->input_count, ids, streams * sizeof(*ids));
    impl->input_count += streams;
    return S_OK;
}

static HRESULT WINAPI video_processor_GetInputAvailableType(IMFTransform *iface, DWORD id, DWORD index,
        IMFMediaType **type)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    IMFMediaType *media_type;
    const GUID *subtype;
    HRESULT hr;

    TRACE("iface %p, id %#lx, index %#lx, type %p.\n", iface, id, index, type);

    if (FAILED(hr = check_input_stream(impl, id)))
        return hr;

    *type = NULL;

    if (index >= ARRAY_SIZE(input_types))
        return MF_E_NO_MORE_TYPES;
    subtype = input_types[index];

    if (FAILED(hr = MFCreateMediaType(&media_type)))
        return hr;
    if (FAILED(hr = IMFMediaType_SetGUID(media_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video)))
        goto done;
    if (FAILED(hr = IMFMediaType_SetGUID(media_type, &MF_MT_SUBTYPE, subtype)))
        goto done;

    IMFMediaType_AddRef((*type = media_type));

done:
    IMFMediaType_Release(media_type);
    return hr;
}

static BOOL can_convert_subtypes(const GUID *input, const GUID *output)
{
    MFVIDEOFORMAT input_format = {.guidFormat = *input}, output_format = {.guidFormat = *output};

    input_format.videoInfo.dwWidth = output_format.videoInfo.dwWidth = 2;
    input_format.videoInfo.dwHeight = output_format.videoInfo.dwHeight = 2;
    return pixels_can_convert(&input_format, &output_format);
}

static HRESULT WINAPI video_processor_GetOutputAvailableType(IMFTransform *iface, DWORD id, DWORD index,
        IMFMediaType **type)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    IMFMediaType *media_type;
    GUID subtype;
    HRESULT hr;
    DWORD i;

    TRACE("iface %p, id %#lx, index %#lx, type %p.\n", iface, id, index, type);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;

    *type = NULL;

    if (!impl->input_type)
        return MF_E_NO_MORE_TYPES;

    if (FAILED(hr = IMFMediaType_GetGUID(impl->input_type, &MF_MT_SUBTYPE, &subtype)))
        return hr;

    if (index)
    {
        const GUID *const *types = impl->device_manager ? d3d_output_types : output_types;
        DWORD count = impl->device_manager ? ARRAY_SIZE(d3d_output_types) : ARRAY_SIZE(output_types);

        for (i = 0; i < count; i++)
        {
            if (IsEqualGUID(types[i], &subtype) || !can_convert_subtypes(&subtype, types[i]))
                continue;
            if (!--index)
                break;
        }
        if (i == count)
            return MF_E_NO_MORE_TYPES;
        subtype = *types[i];
    }

    if (FAILED(hr = MFCreateMediaType(&media_type)))
        return hr;
    if (FAILED(hr = IMFMediaType_SetGUID(media_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video)))
        goto done;
    if (FAILED(hr = IMFMediaType_SetGUID(media_type, &MF_MT_SUBTYPE, &subtype)))
        goto done;

    IMFMediaType_AddRef((*type = media_type));

done:
    IMFMediaType_Release(media_type);
    return hr;
}

static HRESULT WINAPI video_processor_SetInputType(IMFTransform *iface, DWORD id, IMFMediaType *type, DWORD flags)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    GUID major, subtype;
    UINT64 frame_size;
    HRESULT hr;
    ULONG i;

    TRACE("iface %p, id %#lx, type %p, flags %#lx.\n", iface, id, type, flags);

    if (FAILED(hr = check_input_stream(impl, id)))
        return hr;

    if (!type)
    {
        if (impl->input_type)
        {
            IMFMediaType_Release(impl->input_type);
            impl->input_type = NULL;
        }
        video_processor_flush(impl);
        video_processor_cleanup(impl);

        return S_OK;
    }

    if (FAILED(IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major))
            || !IsEqualGUID(&major, &MFMediaType_Video))
        return E_INVALIDARG;
    if (FAILED(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype)))
        return MF_E_INVALIDMEDIATYPE;
    if (FAILED(hr = IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &frame_size)))
        return hr;

    for (i = 0; i < ARRAY_SIZE(input_types); ++i)
        if (IsEqualGUID(&subtype, input_types[i]))
            break;
    if (i == ARRAY_SIZE(input_types) || IsEqualGUID(&subtype, &MFVideoFormat_420O))
        return MF_E_INVALIDMEDIATYPE;

    if (flags & MFT_SET_TYPE_TEST_ONLY)
        return S_OK;

    video_processor_flush(impl);
    if (impl->input_type)
        IMFMediaType_Release(impl->input_type);
    IMFMediaType_AddRef((impl->input_type = type));

    if (impl->output_type && FAILED(hr = video_processor_init(impl)))
    {
        IMFMediaType_Release(impl->input_type);
        impl->input_type = NULL;
    }

    if (FAILED(hr) || FAILED(MFGetPlaneSize(subtype.Data1, frame_size >> 32, (UINT32)frame_size,
            &impl->input_info.cbSize)))
        impl->input_info.cbSize = 0;

    return hr;
}

static HRESULT WINAPI video_processor_SetOutputType(IMFTransform *iface, DWORD id, IMFMediaType *type, DWORD flags)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    GUID major, subtype;
    UINT64 frame_size;
    HRESULT hr;
    ULONG i;

    TRACE("iface %p, id %#lx, type %p, flags %#lx.\n", iface, id, type, flags);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;

    if (!type)
    {
        if (impl->output_type)
        {
            IMFMediaType_Release(impl->output_type);
            impl->output_type = NULL;
        }
        video_processor_cleanup(impl);

        return S_OK;
    }

    if (FAILED(IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major))
            || !IsEqualGUID(&major, &MFMediaType_Video))
        return E_INVALIDARG;
    if (FAILED(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype)))
        return MF_E_INVALIDMEDIATYPE;
    if (FAILED(hr = IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &frame_size)))
        return hr;

    for (i = 0; i < ARRAY_SIZE(output_types); ++i)
        if (IsEqualGUID(&subtype, output_types[i]))
            break;
    if (i == ARRAY_SIZE(output_types))
        return MF_E_INVALIDMEDIATYPE;

    if (flags & MFT_SET_TYPE_TEST_ONLY)
        return S_OK;

    if (FAILED(hr = video_processor_uninit_allocator(impl)))
        return hr;

    if (impl->output_type)
        IMFMediaType_Release(impl->output_type);
    IMFMediaType_AddRef((impl->output_type = type));

    if (impl->input_type && FAILED(hr = video_processor_init(impl)))
    {
        IMFMediaType_Release(impl->output_type);
        impl->output_type = NULL;
    }

    if (FAILED(hr) || FAILED(MFCalculateImageSize(&subtype, frame_size >> 32, (UINT32)frame_size,
            (UINT32 *)&impl->output_info.cbSize)))
        impl->output_info.cbSize = 0;

    return hr;
}

static HRESULT WINAPI video_processor_GetInputCurrentType(IMFTransform *iface, DWORD id, IMFMediaType **type)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    HRESULT hr;

    TRACE("iface %p, id %#lx, type %p.\n", iface, id, type);

    if (FAILED(hr = check_input_stream(impl, id)))
        return hr;
    if (!impl->input_type)
        return MF_E_TRANSFORM_TYPE_NOT_SET;

    if (FAILED(hr = MFCreateMediaType(type)))
        return hr;
    if (FAILED(hr = IMFMediaType_CopyAllItems(impl->input_type, (IMFAttributes *)*type)))
        IMFMediaType_Release(*type);

    return hr;
}

static HRESULT WINAPI video_processor_GetOutputCurrentType(IMFTransform *iface, DWORD id, IMFMediaType **type)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    HRESULT hr;

    TRACE("iface %p, id %#lx, type %p.\n", iface, id, type);

    if (id != 0)
        return MF_E_INVALIDSTREAMNUMBER;
    if (!impl->output_type)
        return MF_E_TRANSFORM_TYPE_NOT_SET;

    if (FAILED(hr = MFCreateMediaType(type)))
        return hr;
    if (FAILED(hr = IMFMediaType_CopyAllItems(impl->output_type, (IMFAttributes *)*type)))
        IMFMediaType_Release(*type);

    return hr;
}

static HRESULT WINAPI video_processor_GetInputStatus(IMFTransform *iface, DWORD id, DWORD *flags)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    HRESULT hr;

    TRACE("iface %p, id %#lx, flags %p.\n", iface, id, flags);

    if (FAILED(hr = check_input_stream(impl, id)))
        return hr;
    if (!impl->input_type)
        return MF_E_TRANSFORM_TYPE_NOT_SET;

    *flags = impl->input_sample ? 0 : MFT_INPUT_STATUS_ACCEPT_DATA;
    return S_OK;
}

static HRESULT WINAPI video_processor_GetOutputStatus(IMFTransform *iface, DWORD *flags)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);

    TRACE("iface %p, flags %p.\n", iface, flags);

    if (!impl->output_type)
        return MF_E_TRANSFORM_TYPE_NOT_SET;

    *flags = impl->input_sample ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
    return S_OK;
}

static HRESULT WINAPI video_processor_SetOutputBounds(IMFTransform *iface, LONGLONG lower, LONGLONG upper)
{
    TRACE("iface %p, lower %I64d, upper %I64d.\n", iface, lower, upper);
    return E_NOTIMPL;
}

static HRESULT WINAPI video_processor_ProcessEvent(IMFTransform *iface, DWORD id, IMFMediaEvent *event)
{
    TRACE("iface %p, id %#lx, event %p.\n", iface, id, event);

    if (!event)
        return E_POINTER;
    return S_OK;
}

static HRESULT WINAPI video_processor_ProcessMessage(IMFTransform *iface, MFT_MESSAGE_TYPE message, ULONG_PTR param)
{
    struct video_processor *processor = impl_from_IMFTransform(iface);
    HRESULT hr;

    TRACE("iface %p, message %#x, param %Ix.\n", iface, message, param);

    switch (message)
    {
    case MFT_MESSAGE_SET_D3D_MANAGER:
    {
        IMFDXGIDeviceManager *manager = NULL;

        if (param && FAILED(hr = IUnknown_QueryInterface((IUnknown *)param, &IID_IMFDXGIDeviceManager,
                (void **)&manager)))
            return hr;
        if (FAILED(hr = video_processor_uninit_allocator(processor)))
        {
            if (manager)
                IMFDXGIDeviceManager_Release(manager);
            return hr;
        }

        if (processor->device_manager)
            IUnknown_Release(processor->device_manager);
        if ((processor->device_manager = (IUnknown *)manager))
            processor->output_info.dwFlags |= MFT_OUTPUT_STREAM_PROVIDES_SAMPLES;
        return S_OK;
    }

    case MFT_MESSAGE_COMMAND_FLUSH:
        video_processor_flush(processor);
        return S_OK;

    default:
        return S_OK;
    }
}

static HRESULT WINAPI video_processor_ProcessInput(IMFTransform *iface, DWORD id, IMFSample *sample, DWORD flags)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    HRESULT hr;

    TRACE("iface %p, id %#lx, sample %p, flags %#lx.\n", iface, id, sample, flags);

    if (FAILED(hr = check_input_stream(impl, id)))
        return hr;
    if (!sample)
        return E_POINTER;
    if (!impl->input_type || !impl->output_type)
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    if (impl->input_sample)
        return MF_E_NOTACCEPTING;

    IMFSample_AddRef((impl->input_sample = sample));
    return S_OK;
}

static HRESULT WINAPI video_processor_ProcessOutput(IMFTransform *iface, DWORD flags, DWORD count,
        MFT_OUTPUT_DATA_BUFFER *output, DWORD *output_status)
{
    struct video_processor *impl = impl_from_IMFTransform(iface);
    BOOL playback_mode, provide_samples;
    LONGLONG time, duration;
    IMFSample *output_sample;
    DWORD length = 0;
    HRESULT hr;

    TRACE("iface %p, flags %#lx, count %lu, output %p, output_status %p.\n", iface, flags, count,
            output, output_status);

    if (count != 1)
        return E_INVALIDARG;

    if (!impl->input_format || !impl->output_format)
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    if (!impl->input_sample)
        return MF_E_TRANSFORM_NEED_MORE_INPUT;
    if (FAILED(IMFSample_GetSampleTime(impl->input_sample, &time)))
        return MF_E_NO_SAMPLE_TIMESTAMP;
    if (FAILED(IMFSample_GetSampleDuration(impl->input_sample, &duration)))
        duration = 0;

    output->dwStatus = 0;

    if (FAILED(IMFAttributes_GetUINT32(impl->attributes, &MF_XVP_PLAYBACK_MODE, (UINT32 *) &playback_mode)))
        playback_mode = FALSE;
    provide_samples = (impl->output_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) && !playback_mode;

    if (provide_samples)
    {
        if (FAILED(hr = video_processor_init_allocator(impl)))
            return hr;
        if (FAILED(hr = IMFVideoSampleAllocatorEx_AllocateSample(impl->allocator, &output_sample)))
            return hr;
    }
    else
    {
        if (!(output_sample = output->pSample))
            return E_INVALIDARG;
        IMFSample_AddRef(output_sample);
    }

    if (SUCCEEDED(hr = video_processor_convert(impl, impl->input_sample, output_sample, &length)))
    {
        IMFSample_SetSampleTime(output_sample, time);
        IMFSample_SetSampleDuration(output_sample, duration);
        IMFSample_SetUINT32(output_sample, &MFSampleExtension_CleanPoint, 1);
        video_processor_flush(impl);

        *output_status = 0;
        if (provide_samples)
        {
            output->pSample = output_sample;
            IMFSample_AddRef(output_sample);
        }
    }

    IMFSample_Release(output_sample);
    return hr;
}

static const IMFTransformVtbl video_processor_vtbl =
{
    video_processor_QueryInterface,
    video_processor_AddRef,
    video_processor_Release,
    video_processor_GetStreamLimits,
    video_processor_GetStreamCount,
    video_processor_GetStreamIDs,
    video_processor_GetInputStreamInfo,
    video_processor_GetOutputStreamInfo,
    video_processor_GetAttributes,
    video_processor_GetInputStreamAttributes,
    video_processor_GetOutputStreamAttributes,
    video_processor_DeleteInputStream,
    video_processor_AddInputStreams,
    video_processor_GetInputAvailableType,
    video_processor_GetOutputAvailableType,
    video_processor_SetInputType,
    video_processor_SetOutputType,
    video_processor_GetInputCurrentType,
    video_processor_GetOutputCurrentType,
    video_processor_GetInputStatus,
    video_processor_GetOutputStatus,
    video_processor_SetOutputBounds,
    video_processor_ProcessEvent,
    video_processor_ProcessMessage,
    video_processor_ProcessInput,
    video_processor_ProcessOutput,
};

static struct video_processor *impl_from_IMFVideoProcessorControl(IMFVideoProcessorControl *iface)
{
    return CONTAINING_RECORD(iface, struct video_processor, IMFVideoProcessorControl_iface);
}

static HRESULT WINAPI video_control_QueryInterface(IMFVideoProcessorControl *iface, REFIID iid, void **out)
{
    struct video_processor *impl = impl_from_IMFVideoProcessorControl(iface);
    return IMFTransform_QueryInterface(&impl->IMFTransform_iface, iid, out);
}

static ULONG WINAPI video_control_AddRef(IMFVideoProcessorControl *iface)
{
    struct video_processor *impl = impl_from_IMFVideoProcessorControl(iface);
    return IMFTransform_AddRef(&impl->IMFTransform_iface);
}

static ULONG WINAPI video_control_Release(IMFVideoProcessorControl *iface)
{
    struct video_processor *impl = impl_from_IMFVideoProcessorControl(iface);
    return IMFTransform_Release(&impl->IMFTransform_iface);
}

static HRESULT WINAPI video_control_SetBorderColor(IMFVideoProcessorControl *iface, MFARGB *color)
{
    FIXME("iface %p, color %p stub!\n", iface, color);
    return E_NOTIMPL;
}

static HRESULT WINAPI video_control_SetSourceRectangle(IMFVideoProcessorControl *iface, RECT *rect)
{
    FIXME("iface %p, rect %s stub!\n", iface, wine_dbgstr_rect(rect));
    return E_NOTIMPL;
}

static HRESULT WINAPI video_control_SetDestinationRectangle(IMFVideoProcessorControl *iface, RECT *rect)
{
    FIXME("iface %p, rect %s stub!\n", iface, wine_dbgstr_rect(rect));
    return E_NOTIMPL;
}

static HRESULT WINAPI video_control_SetMirror(IMFVideoProcessorControl *iface, MF_VIDEO_PROCESSOR_MIRROR mirror)
{
    FIXME("iface %p, mirror %u stub!\n", iface, mirror);
    return E_NOTIMPL;
}

static HRESULT WINAPI video_control_SetRotation(IMFVideoProcessorControl *iface, MF_VIDEO_PROCESSOR_ROTATION rotation)
{
    FIXME("iface %p, rotation %u stub!\n", iface, rotation);
    return E_NOTIMPL;
}

static HRESULT WINAPI video_control_SetConstrictionSize(IMFVideoProcessorControl *iface, SIZE *size)
{
    FIXME("iface %p, size %p stub!\n", iface, size);
    return E_NOTIMPL;
}

static const IMFVideoProcessorControlVtbl video_control_vtbl =
{
    video_control_QueryInterface,
    video_control_AddRef,
    video_control_Release,
    video_control_SetBorderColor,
    video_control_SetSourceRectangle,
    video_control_SetDestinationRectangle,
    video_control_SetMirror,
    video_control_SetRotation,
    video_control_SetConstrictionSize,
};

static struct video_processor *impl_from_IMFRealTimeClientEx(IMFRealTimeClientEx *iface)
{
    return CONTAINING_RECORD(iface, struct video_processor, IMFRealTimeClientEx_iface);
}

static HRESULT WINAPI real_time_client_QueryInterface(IMFRealTimeClientEx *iface, REFIID iid, void **out)
{
    struct video_processor *impl = impl_from_IMFRealTimeClientEx(iface);
    return IMFTransform_QueryInterface(&impl->IMFTransform_iface, iid, out);
}

static ULONG WINAPI real_time_client_AddRef(IMFRealTimeClientEx *iface)
{
    struct video_processor *impl = impl_from_IMFRealTimeClientEx(iface);
    return IMFTransform_AddRef(&impl->IMFTransform_iface);
}

static ULONG WINAPI real_time_client_Release(IMFRealTimeClientEx *iface)
{
    struct video_processor *impl = impl_from_IMFRealTimeClientEx(iface);
    return IMFTransform_Release(&impl->IMFTransform_iface);
}

static HRESULT WINAPI real_time_client_RegisterThreadsEx(IMFRealTimeClientEx *iface, DWORD *task_index,
        const WCHAR *classname, LONG priority)
{
    TRACE("iface %p, task_index %p, classname %s, priority %ld.\n", iface, task_index, debugstr_w(classname), priority);
    return S_OK;
}

static HRESULT WINAPI real_time_client_UnregisterThreads(IMFRealTimeClientEx *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT WINAPI real_time_client_SetWorkQueueEx(IMFRealTimeClientEx *iface, DWORD queue, LONG priority)
{
    TRACE("iface %p, queue %#lx, priority %ld.\n", iface, queue, priority);
    return S_OK;
}

static const IMFRealTimeClientExVtbl real_time_client_vtbl =
{
    real_time_client_QueryInterface,
    real_time_client_AddRef,
    real_time_client_Release,
    real_time_client_RegisterThreadsEx,
    real_time_client_UnregisterThreads,
    real_time_client_SetWorkQueueEx,
};

static HRESULT WINAPI video_processor_factory_CreateInstance(IClassFactory *iface, IUnknown *outer,
        REFIID riid, void **out)
{
    struct video_processor *impl;
    HRESULT hr;

    TRACE("iface %p, outer %p, riid %s, out %p.\n", iface, outer, debugstr_guid(riid), out);

    *out = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;

    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = MFCreateAttributes(&impl->attributes, 0)))
        goto failed;
    /* FIXME: stereo views are not unpacked, MF_ENABLE_3DVIDEO_OUTPUT outputs the whole frame */
    if (FAILED(hr = IMFAttributes_SetUINT32(impl->attributes, &MFT_SUPPORT_3DVIDEO, TRUE)))
        goto failed;
    if (FAILED(hr = IMFAttributes_SetUINT32(impl->attributes, &MF_SA_D3D11_AWARE, TRUE)))
        goto failed;
    /* native only has MF_SA_D3D_AWARE on Win7, but it is useful to have in mfreadwrite */
    if (FAILED(hr = IMFAttributes_SetUINT32(impl->attributes, &MF_SA_D3D_AWARE, TRUE)))
        goto failed;
    if (FAILED(hr = MFCreateAttributes(&impl->output_attributes, 0)))
        goto failed;

    impl->IMFTransform_iface.lpVtbl = &video_processor_vtbl;
    impl->IMFVideoProcessorControl_iface.lpVtbl = &video_control_vtbl;
    impl->IMFRealTimeClientEx_iface.lpVtbl = &real_time_client_vtbl;
    impl->refcount = 1;
    impl->input_count = 1;

    *out = &impl->IMFTransform_iface;
    TRACE("created %p\n", *out);
    return S_OK;

failed:
    if (impl->output_attributes)
        IMFAttributes_Release(impl->output_attributes);
    if (impl->attributes)
        IMFAttributes_Release(impl->attributes);
    free(impl);
    return hr;
}

static HRESULT WINAPI class_factory_QueryInterface(IClassFactory *iface, REFIID riid, void **out)
{
    *out = IsEqualGUID(riid, &IID_IClassFactory) || IsEqualGUID(riid, &IID_IUnknown) ? iface : NULL;
    return *out ? S_OK : E_NOINTERFACE;
}
static ULONG WINAPI class_factory_AddRef(IClassFactory *iface)
{
    return 2;
}
static ULONG WINAPI class_factory_Release(IClassFactory *iface)
{
    return 1;
}
static HRESULT WINAPI class_factory_LockServer(IClassFactory *iface, BOOL dolock)
{
    TRACE("iface %p, dolock %d.\n", iface, dolock);
    return S_OK;
}

static const IClassFactoryVtbl video_processor_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    video_processor_factory_CreateInstance,
    class_factory_LockServer,
};

IClassFactory video_processor_factory = {&video_processor_factory_vtbl};
