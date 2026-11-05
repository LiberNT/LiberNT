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

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"

#include "d3d9types.h"
#include "dmoreg.h"
#include "dmort.h"
#include "dshow.h"
#include "dvdmedia.h"
#include "mediaerr.h"
#include "mfapi.h"
#include "mferror.h"
#include "mfidl.h"
#include "mfobjects.h"
#include "mftransform.h"
#include "wmcodecdsp.h"

#include "colorcnv_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dmo);

DEFINE_MEDIATYPE_GUID(MFVideoFormat_ABGR32, D3DFMT_A8B8G8R8);
DEFINE_GUID(DMOVideoFormat_RGB32,D3DFMT_X8R8G8B8,0x524f,0x11ce,0x9f,0x53,0x00,0x20,0xaf,0x0b,0xa7,0x70);
DEFINE_GUID(DMOVideoFormat_RGB24,D3DFMT_R8G8B8,0x524f,0x11ce,0x9f,0x53,0x00,0x20,0xaf,0x0b,0xa7,0x70);
DEFINE_GUID(DMOVideoFormat_RGB565,D3DFMT_R5G6B5,0x524f,0x11ce,0x9f,0x53,0x00,0x20,0xaf,0x0b,0xa7,0x70);
DEFINE_GUID(DMOVideoFormat_RGB555,D3DFMT_X1R5G5B5,0x524f,0x11ce,0x9f,0x53,0x00,0x20,0xaf,0x0b,0xa7,0x70);
DEFINE_GUID(DMOVideoFormat_RGB8,D3DFMT_P8,0x524f,0x11ce,0x9f,0x53,0x00,0x20,0xaf,0x0b,0xa7,0x70);

static HRESULT lock_media_buffer(IMediaBuffer *media_buffer, BOOL write, BYTE **data, DWORD *size, LONG *pitch)
{
    IMF2DBuffer2 *buffer2;
    BYTE *scanline0;
    HRESULT hr;

    if (SUCCEEDED(hr = IMediaBuffer_QueryInterface(media_buffer, &IID_IMF2DBuffer2, (void **)&buffer2)))
    {
        hr = IMF2DBuffer2_Lock2DSize(buffer2, write ? MF2DBuffer_LockFlags_Write : MF2DBuffer_LockFlags_Read,
                &scanline0, pitch, data, size);
        IMF2DBuffer2_Release(buffer2);
        if (SUCCEEDED(hr))
            return S_OK;
    }
    if (FAILED(hr = IMediaBuffer_GetBufferAndLength(media_buffer, data, size)))
        return hr;
    if (write && FAILED(hr = IMediaBuffer_GetMaxLength(media_buffer, size)))
        return hr;
    *pitch = 0;
    return S_OK;
}

static void unlock_media_buffer(IMediaBuffer *media_buffer)
{
    IMF2DBuffer2 *buffer2;

    if (SUCCEEDED(IMediaBuffer_QueryInterface(media_buffer, &IID_IMF2DBuffer2, (void **)&buffer2)))
    {
        IMF2DBuffer2_Unlock2D(buffer2);
        IMF2DBuffer2_Release(buffer2);
    }
}

static const GUID *const converter_input_types[] =
{
    &MEDIASUBTYPE_YV12,
    &MEDIASUBTYPE_YUY2,
    &MEDIASUBTYPE_UYVY,
    &MEDIASUBTYPE_AYUV,
    &MEDIASUBTYPE_NV12,
    &MEDIASUBTYPE_RGB32,
    &MEDIASUBTYPE_RGB565,
    &MEDIASUBTYPE_I420,
    &MEDIASUBTYPE_IYUV,
    &MEDIASUBTYPE_YVYU,
    &MEDIASUBTYPE_RGB24,
    &MEDIASUBTYPE_RGB555,
    &MEDIASUBTYPE_RGB8,
    &MEDIASUBTYPE_V216,
    &MEDIASUBTYPE_V410,
    &MEDIASUBTYPE_NV11,
    &MEDIASUBTYPE_Y41P,
    &MEDIASUBTYPE_Y41T,
    &MEDIASUBTYPE_Y42T,
    &MEDIASUBTYPE_YVU9,
};
static const GUID *const converter_output_types[] =
{
    &MEDIASUBTYPE_YV12,
    &MEDIASUBTYPE_YUY2,
    &MEDIASUBTYPE_UYVY,
    &MEDIASUBTYPE_AYUV,
    &MEDIASUBTYPE_NV12,
    &MEDIASUBTYPE_RGB32,
    &MEDIASUBTYPE_RGB565,
    &MEDIASUBTYPE_I420,
    &MEDIASUBTYPE_IYUV,
    &MEDIASUBTYPE_YVYU,
    &MEDIASUBTYPE_RGB24,
    &MEDIASUBTYPE_RGB555,
    &MEDIASUBTYPE_RGB8,
    &MEDIASUBTYPE_V216,
    &MEDIASUBTYPE_V410,
    &MEDIASUBTYPE_NV11,
};

static const GUID *const resizer_input_types[] =
{
    &MEDIASUBTYPE_IYUV,
    &MEDIASUBTYPE_YUY2,
    &MEDIASUBTYPE_UYVY,
    &MEDIASUBTYPE_I420,
    &MEDIASUBTYPE_RGB32,
    &MEDIASUBTYPE_RGB24,
    &MEDIASUBTYPE_RGB565,
    &MEDIASUBTYPE_RGB8,
    &MEDIASUBTYPE_RGB555,
    &MEDIASUBTYPE_AYUV,
    &MEDIASUBTYPE_V216,
    &MEDIASUBTYPE_YV12,
};
static const GUID *const resizer_output_types[] =
{
    &MEDIASUBTYPE_IYUV,
    &MEDIASUBTYPE_YUY2,
    &MEDIASUBTYPE_UYVY,
    &MEDIASUBTYPE_I420,
    &MEDIASUBTYPE_RGB32,
    &MEDIASUBTYPE_RGB24,
    &MEDIASUBTYPE_RGB565,
    &MEDIASUBTYPE_RGB8,
    &MEDIASUBTYPE_RGB555,
    &MEDIASUBTYPE_AYUV,
    &MEDIASUBTYPE_V216,
    &MEDIASUBTYPE_YV12,
    /* duplicate the list, Windows does the same */
    &MEDIASUBTYPE_IYUV,
    &MEDIASUBTYPE_YUY2,
    &MEDIASUBTYPE_UYVY,
    &MEDIASUBTYPE_I420,
    &MEDIASUBTYPE_RGB32,
    &MEDIASUBTYPE_RGB24,
    &MEDIASUBTYPE_RGB565,
    &MEDIASUBTYPE_RGB8,
    &MEDIASUBTYPE_RGB555,
    &MEDIASUBTYPE_AYUV,
    &MEDIASUBTYPE_V216,
    &MEDIASUBTYPE_YV12,
};

struct color_convert
{
    IUnknown IUnknown_inner;
    IMFTransform IMFTransform_iface;
    IMediaObject IMediaObject_iface;
    IMFRealTimeClient IMFRealTimeClient_iface;
    IPropertyStore IPropertyStore_iface;
    IUnknown *outer;
    LONG refcount;

    UINT input_type_count;
    const GUID *const *input_types;
    UINT output_type_count;
    const GUID *const *output_types;

    DMO_MEDIA_TYPE input_type;
    MFVIDEOFORMAT input_format;
    MFT_INPUT_STREAM_INFO input_info;
    DMO_MEDIA_TYPE output_type;
    MFVIDEOFORMAT output_format;
    MFT_OUTPUT_STREAM_INFO output_info;

    BOOL ready;
    BYTE *frame;
    DWORD frame_size;
    BOOL has_frame;
    BOOL frame_sync_point;
    LONGLONG frame_time;
    LONGLONG frame_duration;
};

static HRESULT color_convert_process_input(struct color_convert *impl, const DMO_OUTPUT_DATA_BUFFER *input)
{
    LONGLONG pts = input->rtTimestamp, duration = input->rtTimelength;
    struct pixel_planes src, dst;
    IMFSample *sample;
    DWORD size;
    HRESULT hr;
    BYTE *data;
    LONG pitch;

    if (!impl->ready)
        return DMO_E_TYPE_NOT_SET;
    if (impl->has_frame)
        return DMO_E_NOTACCEPTING;

    if (FAILED(hr = lock_media_buffer(input->pBuffer, FALSE, &data, &size, &pitch)))
        return hr;
    if (pixels_fill_planes(&impl->input_format, data, size, pitch, &src))
    {
        pixels_fill_planes(&impl->input_format, impl->frame, impl->frame_size, 0, &dst);
        pixels_copy(&impl->input_format, &src, &dst);
        impl->has_frame = TRUE;
    }
    else
    {
        ERR("input buffer size %#lx too small for %#lx\n", size, impl->frame_size);
        hr = E_INVALIDARG;
    }
    unlock_media_buffer(input->pBuffer);
    if (FAILED(hr))
        return hr;

    if (!(input->dwStatus & DMO_INPUT_DATA_BUFFERF_TIME))
        pts = INT64_MIN;
    if (!(input->dwStatus & DMO_INPUT_DATA_BUFFERF_TIMELENGTH))
        duration = INT64_MIN;
    if (SUCCEEDED(IMediaBuffer_QueryInterface(input->pBuffer, &IID_IMFSample, (void **)&sample)))
    {
        if (FAILED(IMFSample_GetSampleTime(sample, &pts)))
            pts = INT64_MIN;
        if (FAILED(IMFSample_GetSampleDuration(sample, &duration)))
            duration = INT64_MIN;
        IMFSample_Release(sample);
    }

    impl->frame_sync_point = !!(input->dwStatus & DMO_INPUT_DATA_BUFFERF_SYNCPOINT);
    impl->frame_time = pts;
    impl->frame_duration = duration;
    TRACE("input time %I64d, duration %I64d\n", pts, duration);
    return S_OK;
}

static HRESULT color_convert_process_output(struct color_convert *impl, DMO_OUTPUT_DATA_BUFFER *output)
{
    struct pixel_planes src, dst;
    IMFSample *sample;
    DWORD size, ret;
    HRESULT hr;
    BYTE *data;
    LONG pitch;

    if (!impl->ready)
        return DMO_E_TYPE_NOT_SET;
    if (!impl->has_frame)
        return S_FALSE;

    if (FAILED(hr = lock_media_buffer(output->pBuffer, TRUE, &data, &size, &pitch)))
        return hr;
    if ((ret = pixels_fill_planes(&impl->output_format, data, size, pitch, &dst)))
    {
        pixels_fill_planes(&impl->input_format, impl->frame, impl->frame_size, 0, &src);
        pixels_convert(&impl->input_format, &src, &impl->output_format, &dst);
    }
    unlock_media_buffer(output->pBuffer);
    impl->has_frame = FALSE;

    output->dwStatus = 0;
    IMediaBuffer_SetLength(output->pBuffer, ret);
    if (!ret)
    {
        ERR("output buffer size %#lx too small\n", size);
        return E_FAIL;
    }

    if (impl->frame_sync_point)
        output->dwStatus |= DMO_OUTPUT_DATA_BUFFERF_SYNCPOINT;
    output->rtTimestamp = impl->frame_time;
    if (output->rtTimestamp != INT64_MIN)
        output->dwStatus |= DMO_OUTPUT_DATA_BUFFERF_TIME;
    output->rtTimelength = impl->frame_duration;
    if (output->rtTimelength != INT64_MIN)
        output->dwStatus |= DMO_OUTPUT_DATA_BUFFERF_TIMELENGTH;

    if (SUCCEEDED(IMediaBuffer_QueryInterface(output->pBuffer, &IID_IMFSample, (void **)&sample)))
    {
        if (output->rtTimestamp != INT64_MIN)
            IMFSample_SetSampleTime(sample, output->rtTimestamp);
        if (output->rtTimelength != INT64_MIN)
            IMFSample_SetSampleDuration(sample, output->rtTimelength);
        IMFSample_SetUINT32(sample, &MFSampleExtension_CleanPoint, impl->frame_sync_point);
        IMFSample_Release(sample);
    }

    TRACE("returning output size %#lx, status %#lx\n", ret, output->dwStatus);
    return S_OK;
}

static void color_convert_cleanup(struct color_convert *impl)
{
    memset(&impl->input_format, 0, sizeof(impl->input_format));
    memset(&impl->output_format, 0, sizeof(impl->output_format));
    free(impl->frame);
    impl->frame = NULL;
    impl->frame_size = 0;
    impl->has_frame = FALSE;
    impl->ready = FALSE;
}

static BOOL get_default_stride_sign(const BITMAPINFOHEADER *header)
{
    return header->biCompression == BI_RGB || header->biCompression == BI_BITFIELDS ? -1 : 1;
}

static const GUID *get_dmo_subtype(const GUID *subtype)
{
    if (IsEqualGUID(subtype, &MFVideoFormat_RGB8))
        return &MEDIASUBTYPE_RGB8;
    if (IsEqualGUID(subtype, &MFVideoFormat_RGB555))
        return &MEDIASUBTYPE_RGB555;
    if (IsEqualGUID(subtype, &MFVideoFormat_RGB565))
        return &MEDIASUBTYPE_RGB565;
    if (IsEqualGUID(subtype, &MFVideoFormat_RGB24))
        return &MEDIASUBTYPE_RGB24;
    if (IsEqualGUID(subtype, &MFVideoFormat_RGB32))
        return &MEDIASUBTYPE_RGB32;
    return subtype;
}

static HRESULT init_video_format(const DMO_MEDIA_TYPE *type, MFVIDEOFORMAT *format)
{
    if (IsEqualGUID(&type->formattype, &FORMAT_VideoInfo))
    {
        const VIDEOINFOHEADER *vih = (VIDEOINFOHEADER *)type->pbFormat;
        memset(format, 0, sizeof(*format));
        format->videoInfo.dwWidth = vih->bmiHeader.biWidth;
        format->videoInfo.dwHeight = abs(vih->bmiHeader.biHeight);
        if (get_default_stride_sign(&vih->bmiHeader) < 0 && vih->bmiHeader.biHeight > 0)
            format->videoInfo.VideoFlags |= MFVideoFlag_BottomUpLinearRep;
        format->guidFormat = type->subtype;
        return S_OK;
    }
    if (IsEqualGUID(&type->formattype, &FORMAT_VideoInfo2))
    {
        const VIDEOINFOHEADER2 *vih = (VIDEOINFOHEADER2 *)type->pbFormat;
        memset(format, 0, sizeof(*format));
        format->videoInfo.dwWidth = vih->bmiHeader.biWidth;
        format->videoInfo.dwHeight = abs(vih->bmiHeader.biHeight);
        if (get_default_stride_sign(&vih->bmiHeader) < 0 && vih->bmiHeader.biHeight > 0)
            format->videoInfo.VideoFlags |= MFVideoFlag_BottomUpLinearRep;
        format->guidFormat = type->subtype;
        return S_OK;
    }
    if (IsEqualGUID(&type->formattype, &FORMAT_MFVideoFormat))
    {
        *format = *(MFVIDEOFORMAT *)type->pbFormat;
        /* normalize subtype with other formats for pixel_format_from_video_subtype */
        format->guidFormat = *get_dmo_subtype(&format->guidFormat);
        return S_OK;
    }

    FIXME("Format %s not implemented\n", debugstr_guid(&type->formattype));
    return E_NOTIMPL;
}

static HRESULT color_convert_init(struct color_convert *impl)
{
    HRESULT hr;

    color_convert_cleanup(impl);

    if (FAILED(hr = init_video_format(&impl->input_type, &impl->input_format))
            || FAILED(hr = init_video_format(&impl->output_type, &impl->output_format)))
    {
        color_convert_cleanup(impl);
        return hr;
    }
    if (impl->input_format.videoInfo.dwWidth != impl->output_format.videoInfo.dwWidth
            || impl->input_format.videoInfo.dwHeight != impl->output_format.videoInfo.dwHeight)
    {
        color_convert_cleanup(impl);
        return DMO_E_TYPE_NOT_ACCEPTED;
    }
    if (!pixels_can_convert(&impl->input_format, &impl->output_format))
    {
        FIXME("Unsupported conversion %s %lux%lu -> %s %lux%lu\n", debugstr_guid(&impl->input_format.guidFormat),
                impl->input_format.videoInfo.dwWidth, impl->input_format.videoInfo.dwHeight,
                debugstr_guid(&impl->output_format.guidFormat), impl->output_format.videoInfo.dwWidth,
                impl->output_format.videoInfo.dwHeight);
        color_convert_cleanup(impl);
        return DMO_E_TYPE_NOT_ACCEPTED;
    }

    impl->frame_size = pixels_fill_planes(&impl->input_format, NULL, ~0u, 0, NULL);
    if (!(impl->frame = malloc(impl->frame_size)))
    {
        color_convert_cleanup(impl);
        return E_OUTOFMEMORY;
    }
    impl->ready = TRUE;
    return S_OK;
}

static struct color_convert *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct color_convert, IUnknown_inner);
}

static HRESULT WINAPI unknown_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    struct color_convert *impl = impl_from_IUnknown(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown))
        *out = &impl->IUnknown_inner;
    else if (IsEqualGUID(iid, &IID_IMFTransform))
        *out = &impl->IMFTransform_iface;
    else if (IsEqualGUID(iid, &IID_IMediaObject))
        *out = &impl->IMediaObject_iface;
    else if (IsEqualIID(iid, &IID_IMFRealTimeClient))
        *out = &impl->IMFRealTimeClient_iface;
    else if (IsEqualIID(iid, &IID_IPropertyStore))
        *out = &impl->IPropertyStore_iface;
    else
    {
        *out = NULL;
        WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI unknown_AddRef(IUnknown *iface)
{
    struct color_convert *impl = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedIncrement(&impl->refcount);

    TRACE("iface %p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI unknown_Release(IUnknown *iface)
{
    struct color_convert *impl = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedDecrement(&impl->refcount);

    TRACE("iface %p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        color_convert_cleanup(impl);
        free(impl);
    }

    return refcount;
}

static const IUnknownVtbl unknown_vtbl =
{
    unknown_QueryInterface,
    unknown_AddRef,
    unknown_Release,
};

static HRESULT MF_RESULT_FROM_DMO(HRESULT hr)
{
    switch (hr)
    {
    case DMO_E_INVALIDSTREAMINDEX: return MF_E_INVALIDSTREAMNUMBER;
    case DMO_E_INVALIDTYPE: return MF_E_INVALIDMEDIATYPE;
    case DMO_E_TYPE_NOT_SET: return MF_E_TRANSFORM_TYPE_NOT_SET;
    case DMO_E_NOTACCEPTING: return MF_E_NOTACCEPTING;
    case DMO_E_TYPE_NOT_ACCEPTED: return MF_E_INVALIDMEDIATYPE;
    case DMO_E_NO_MORE_ITEMS: return MF_E_NO_MORE_TYPES;
    case S_FALSE: return MF_E_TRANSFORM_NEED_MORE_INPUT;
    }

    return hr;
}

static struct color_convert *impl_from_IMFTransform(IMFTransform *iface)
{
    return CONTAINING_RECORD(iface, struct color_convert, IMFTransform_iface);
}

static HRESULT WINAPI transform_QueryInterface(IMFTransform *iface, REFIID iid, void **out)
{
    return IUnknown_QueryInterface(impl_from_IMFTransform(iface)->outer, iid, out);
}

static ULONG WINAPI transform_AddRef(IMFTransform *iface)
{
    return IUnknown_AddRef(impl_from_IMFTransform(iface)->outer);
}

static ULONG WINAPI transform_Release(IMFTransform *iface)
{
    return IUnknown_Release(impl_from_IMFTransform(iface)->outer);
}

static HRESULT WINAPI transform_GetStreamLimits(IMFTransform *iface, DWORD *input_minimum,
        DWORD *input_maximum, DWORD *output_minimum, DWORD *output_maximum)
{
    TRACE("iface %p, input_minimum %p, input_maximum %p, output_minimum %p, output_maximum %p.\n",
            iface, input_minimum, input_maximum, output_minimum, output_maximum);
    *input_minimum = *input_maximum = *output_minimum = *output_maximum = 1;
    return S_OK;
}

static HRESULT WINAPI transform_GetStreamCount(IMFTransform *iface, DWORD *inputs, DWORD *outputs)
{
    TRACE("iface %p, inputs %p, outputs %p.\n", iface, inputs, outputs);
    *inputs = *outputs = 1;
    return S_OK;
}

static HRESULT WINAPI transform_GetStreamIDs(IMFTransform *iface, DWORD input_size, DWORD *inputs,
        DWORD output_size, DWORD *outputs)
{
    TRACE("iface %p, input_size %lu, inputs %p, output_size %lu, outputs %p.\n", iface,
            input_size, inputs, output_size, outputs);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetInputStreamInfo(IMFTransform *iface, DWORD id, MFT_INPUT_STREAM_INFO *info)
{
    struct color_convert *impl = impl_from_IMFTransform(iface);

    TRACE("iface %p, id %#lx, info %p.\n", iface, id, info);

    if (IsEqualGUID(&impl->input_type.majortype, &GUID_NULL) || IsEqualGUID(&impl->output_type.majortype, &GUID_NULL))
    {
        memset(info, 0, sizeof(*info));
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }

    *info = impl->input_info;
    return S_OK;
}

static HRESULT WINAPI transform_GetOutputStreamInfo(IMFTransform *iface, DWORD id, MFT_OUTPUT_STREAM_INFO *info)
{
    struct color_convert *impl = impl_from_IMFTransform(iface);

    TRACE("iface %p, id %#lx, info %p.\n", iface, id, info);

    if (IsEqualGUID(&impl->input_type.majortype, &GUID_NULL) || IsEqualGUID(&impl->output_type.majortype, &GUID_NULL))
    {
        memset(info, 0, sizeof(*info));
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    }

    *info = impl->output_info;
    return S_OK;
}

static HRESULT WINAPI transform_GetAttributes(IMFTransform *iface, IMFAttributes **attributes)
{
    TRACE("iface %p, attributes %p.\n", iface, attributes);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetInputStreamAttributes(IMFTransform *iface, DWORD id, IMFAttributes **attributes)
{
    TRACE("iface %p, id %#lx, attributes %p.\n", iface, id, attributes);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputStreamAttributes(IMFTransform *iface, DWORD id, IMFAttributes **attributes)
{
    TRACE("iface %p, id %#lx, attributes %p.\n", iface, id, attributes);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_DeleteInputStream(IMFTransform *iface, DWORD id)
{
    TRACE("iface %p, id %#lx.\n", iface, id);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_AddInputStreams(IMFTransform *iface, DWORD streams, DWORD *ids)
{
    TRACE("iface %p, streams %lu, ids %p.\n", iface, streams, ids);
    return E_NOTIMPL;
}

/* some exotic YUV formats aren't supported by MFCreateMediaTypeFromRepresentation, fill missing attributes */
static HRESULT create_media_type_from_dmo(DMO_MEDIA_TYPE *mt, IMFMediaType **type)
{
    HRESULT hr;

    if (FAILED(hr = MFCreateMediaTypeFromRepresentation(AM_MEDIA_TYPE_REPRESENTATION, mt, type)))
        return hr;
    if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB8))
        IMFMediaType_SetGUID(*type, &MF_MT_SUBTYPE, &mt->subtype);
    IMFMediaType_SetUINT32(*type, &MF_MT_FIXED_SIZE_SAMPLES, 1);

    return hr;
}

static HRESULT WINAPI transform_GetInputAvailableType(IMFTransform *iface, DWORD id, DWORD index,
        IMFMediaType **type)
{
    struct color_convert *impl = impl_from_IMFTransform(iface);
    DMO_MEDIA_TYPE mt = {0};
    HRESULT hr;

    TRACE("iface %p, id %#lx, index %#lx, type %p.\n", iface, id, index, type);

    if (FAILED(hr = MF_RESULT_FROM_DMO(IMediaObject_GetInputType(&impl->IMediaObject_iface, id, index, &mt))))
        return hr;
    hr = create_media_type_from_dmo(&mt, type);
    MoFreeMediaType(&mt);
    return hr;
}

static HRESULT WINAPI transform_GetOutputAvailableType(IMFTransform *iface, DWORD id, DWORD index,
        IMFMediaType **type)
{
    struct color_convert *impl = impl_from_IMFTransform(iface);
    DMO_MEDIA_TYPE mt = {0};
    HRESULT hr;

    TRACE("iface %p, id %#lx, index %#lx, type %p.\n", iface, id, index, type);

    if (FAILED(hr = MF_RESULT_FROM_DMO(IMediaObject_GetOutputType(&impl->IMediaObject_iface, id, index, &mt))))
        return hr;
    hr = create_media_type_from_dmo(&mt, type);
    MoFreeMediaType(&mt);
    return hr;
}

static HRESULT init_dmo_media_type(IMFMediaType *type, DMO_MEDIA_TYPE *mt, BOOL resizer_input)
{
    MFVIDEOFORMAT *format;
    HRESULT hr;

    if (FAILED(hr = MFInitAMMediaTypeFromMFMediaType(type, FORMAT_MFVideoFormat, (AM_MEDIA_TYPE *)mt)))
        return hr;

    format = (MFVIDEOFORMAT *)mt->pbFormat;
    mt->subtype = *get_dmo_subtype(&format->guidFormat); /* need to use DMO subtypes to be accepted */
    if (!format->videoInfo.PixelAspectRatio.Numerator)
        format->videoInfo.PixelAspectRatio.Numerator = 1;
    if (!format->videoInfo.PixelAspectRatio.Denominator)
        format->videoInfo.PixelAspectRatio.Denominator = 1;
    if (resizer_input)
        memset(&format->videoInfo.MinimumDisplayAperture, 0, sizeof(format->videoInfo.MinimumDisplayAperture));

    return hr;
}

static HRESULT WINAPI transform_SetInputType(IMFTransform *iface, DWORD id, IMFMediaType *type, DWORD flags)
{
    DWORD dmo_flags = flags & MFT_SET_TYPE_TEST_ONLY ? DMO_SET_TYPEF_TEST_ONLY : 0;
    struct color_convert *impl = impl_from_IMFTransform(iface);
    DMO_MEDIA_TYPE tmp = {0}, *mt = type ? &tmp : NULL;
    HRESULT hr;

    TRACE("converter %p, id %#lx, type %p, flags %#lx.\n", impl, id, type, flags);

    if (type && FAILED(hr = init_dmo_media_type(type, &tmp, impl->input_types == resizer_input_types)))
        return hr;
    hr = MF_RESULT_FROM_DMO(IMediaObject_SetInputType(&impl->IMediaObject_iface, id, mt, dmo_flags));
    MoFreeMediaType(&tmp);
    return hr;
}

static HRESULT WINAPI transform_SetOutputType(IMFTransform *iface, DWORD id, IMFMediaType *type, DWORD flags)
{
    DWORD dmo_flags = flags & MFT_SET_TYPE_TEST_ONLY ? DMO_SET_TYPEF_TEST_ONLY : 0;
    struct color_convert *impl = impl_from_IMFTransform(iface);
    DMO_MEDIA_TYPE tmp = {0}, *mt = type ? &tmp : NULL;
    HRESULT hr;

    TRACE("converter %p, id %#lx, type %p, flags %#lx.\n", impl, id, type, flags);

    if (type && FAILED(hr = init_dmo_media_type(type, &tmp, FALSE)))
        return hr;
    hr = MF_RESULT_FROM_DMO(IMediaObject_SetOutputType(&impl->IMediaObject_iface, id, mt, dmo_flags));
    MoFreeMediaType(&tmp);
    return hr;
}

static HRESULT WINAPI transform_GetInputCurrentType(IMFTransform *iface, DWORD id, IMFMediaType **type)
{
    struct color_convert *impl = impl_from_IMFTransform(iface);

    TRACE("converter %p, id %#lx, type %p.\n", impl, id, type);

    if (id != 0)
        return MF_E_INVALIDSTREAMNUMBER;
    if (IsEqualGUID(&impl->input_type.majortype, &GUID_NULL))
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    return MFCreateMediaTypeFromRepresentation(AM_MEDIA_TYPE_REPRESENTATION, &impl->input_type, type);
}

static HRESULT WINAPI transform_GetOutputCurrentType(IMFTransform *iface, DWORD id, IMFMediaType **type)
{
    struct color_convert *impl = impl_from_IMFTransform(iface);

    TRACE("converter %p, id %#lx, type %p.\n", impl, id, type);

    if (id != 0)
        return MF_E_INVALIDSTREAMNUMBER;
    if (IsEqualGUID(&impl->output_type.majortype, &GUID_NULL))
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    return MFCreateMediaTypeFromRepresentation(AM_MEDIA_TYPE_REPRESENTATION, &impl->output_type, type);
}

static HRESULT WINAPI transform_GetInputStatus(IMFTransform *iface, DWORD id, DWORD *flags)
{
    FIXME("iface %p, id %#lx, flags %p stub!\n", iface, id, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputStatus(IMFTransform *iface, DWORD *flags)
{
    FIXME("iface %p, flags %p stub!\n", iface, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_SetOutputBounds(IMFTransform *iface, LONGLONG lower, LONGLONG upper)
{
    TRACE("iface %p, lower %I64d, upper %I64d.\n", iface, lower, upper);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessEvent(IMFTransform *iface, DWORD id, IMFMediaEvent *event)
{
    FIXME("iface %p, id %#lx, event %p stub!\n", iface, id, event);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessMessage(IMFTransform *iface, MFT_MESSAGE_TYPE message, ULONG_PTR param)
{
    FIXME("iface %p, message %#x, param %#Ix stub!\n", iface, message, param);
    return S_OK;
}

static HRESULT WINAPI transform_ProcessInput(IMFTransform *iface, DWORD id, IMFSample *sample, DWORD flags)
{
    struct color_convert *impl = impl_from_IMFTransform(iface);
    IMediaBuffer *dmo_buffer;
    IMFMediaBuffer *buffer;
    HRESULT hr;

    TRACE("converter %p, id %#lx, sample %p, flags %#lx.\n", impl, id, sample, flags);

    if (FAILED(hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer)))
        return hr;
    if (SUCCEEDED(hr = MFCreateLegacyMediaBufferOnMFMediaBuffer(sample, buffer, 0, &dmo_buffer)))
    {
        hr = MF_RESULT_FROM_DMO(IMediaObject_ProcessInput(&impl->IMediaObject_iface, id, dmo_buffer, 0, 0, 0));
        IMediaBuffer_Release(dmo_buffer);
    }
    IMFMediaBuffer_Release(buffer);

    return hr;
}

static HRESULT WINAPI transform_ProcessOutput(IMFTransform *iface, DWORD flags, DWORD count,
        MFT_OUTPUT_DATA_BUFFER *output, DWORD *output_status)
{
    struct color_convert *impl = impl_from_IMFTransform(iface);
    DMO_OUTPUT_DATA_BUFFER dmo_output = {0};
    IMFMediaBuffer *buffer;
    DWORD dmo_status;
    HRESULT hr;

    TRACE("converter %p, flags %#lx, count %lu, output %p, output_status %p.\n", impl, flags, count,
            output, output_status);

    if (count != 1)
        return E_INVALIDARG;

    if (FAILED(hr = IMFSample_ConvertToContiguousBuffer(output->pSample, &buffer)))
        return hr;
    if (SUCCEEDED(hr = MFCreateLegacyMediaBufferOnMFMediaBuffer(output->pSample, buffer, 0,
            &dmo_output.pBuffer)))
    {
        hr = MF_RESULT_FROM_DMO(IMediaObject_ProcessOutput(&impl->IMediaObject_iface, flags, 1,
                &dmo_output, &dmo_status));
        IMediaBuffer_Release(dmo_output.pBuffer);
    }
    IMFMediaBuffer_Release(buffer);

    output->dwStatus = *output_status = 0;
    return hr;
}

static const IMFTransformVtbl transform_vtbl =
{
    transform_QueryInterface,
    transform_AddRef,
    transform_Release,
    transform_GetStreamLimits,
    transform_GetStreamCount,
    transform_GetStreamIDs,
    transform_GetInputStreamInfo,
    transform_GetOutputStreamInfo,
    transform_GetAttributes,
    transform_GetInputStreamAttributes,
    transform_GetOutputStreamAttributes,
    transform_DeleteInputStream,
    transform_AddInputStreams,
    transform_GetInputAvailableType,
    transform_GetOutputAvailableType,
    transform_SetInputType,
    transform_SetOutputType,
    transform_GetInputCurrentType,
    transform_GetOutputCurrentType,
    transform_GetInputStatus,
    transform_GetOutputStatus,
    transform_SetOutputBounds,
    transform_ProcessEvent,
    transform_ProcessMessage,
    transform_ProcessInput,
    transform_ProcessOutput,
};

static struct color_convert *impl_from_IMediaObject(IMediaObject *iface)
{
    return CONTAINING_RECORD(iface, struct color_convert, IMediaObject_iface);
}

static HRESULT WINAPI media_object_QueryInterface(IMediaObject *iface, REFIID iid, void **obj)
{
    return IUnknown_QueryInterface(impl_from_IMediaObject(iface)->outer, iid, obj);
}

static ULONG WINAPI media_object_AddRef(IMediaObject *iface)
{
    return IUnknown_AddRef(impl_from_IMediaObject(iface)->outer);
}

static ULONG WINAPI media_object_Release(IMediaObject *iface)
{
    return IUnknown_Release(impl_from_IMediaObject(iface)->outer);
}

static HRESULT WINAPI media_object_GetStreamCount(IMediaObject *iface, DWORD *input, DWORD *output)
{
    FIXME("iface %p, input %p, output %p stub!\n", iface, input, output);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_GetInputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    FIXME("iface %p, index %lu, flags %p stub!\n", iface, index, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_GetOutputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    FIXME("iface %p, index %lu, flags %p stub!\n", iface, index, flags);
    return E_NOTIMPL;
}

static HRESULT get_available_dmo_media_type(const GUID *subtype, DMO_MEDIA_TYPE *type, BOOL output)
{
    VIDEOINFOHEADER *vih;

    if (!(vih = CoTaskMemAlloc(sizeof(*vih))))
        return E_OUTOFMEMORY;
    memset(vih, 0, sizeof(*vih));

    memset(type, 0, sizeof(*type));
    type->majortype = MEDIATYPE_Video;
    type->formattype = FORMAT_VideoInfo;
    type->subtype = *subtype;
    type->pbFormat = (BYTE *)vih;
    type->cbFormat = sizeof(*vih);
    return S_OK;
}

static HRESULT WINAPI media_object_GetInputType(IMediaObject *iface, DWORD index, DWORD type_index,
        DMO_MEDIA_TYPE *type)
{
    struct color_convert *impl = impl_from_IMediaObject(iface);

    TRACE("iface %p, index %lu, type_index %lu, type %p\n", iface, index, type_index, type);

    if (index > 0)
        return DMO_E_INVALIDSTREAMINDEX;
    if (type_index >= impl->input_type_count)
        return DMO_E_NO_MORE_ITEMS;
    return get_available_dmo_media_type(impl->input_types[type_index], type, FALSE);
}

static HRESULT WINAPI media_object_GetOutputType(IMediaObject *iface, DWORD index, DWORD type_index,
        DMO_MEDIA_TYPE *type)
{
    struct color_convert *impl = impl_from_IMediaObject(iface);

    TRACE("iface %p, index %lu, type_index %lu, type %p\n", iface, index, type_index, type);

    if (index > 0)
        return DMO_E_INVALIDSTREAMINDEX;
    if (type_index >= impl->output_type_count)
        return DMO_E_NO_MORE_ITEMS;
    return get_available_dmo_media_type(impl->output_types[type_index], type, TRUE);
}

static HRESULT check_dmo_media_type(const DMO_MEDIA_TYPE *type, UINT32 *image_size,
        const GUID *const *formats, UINT format_count)
{
    ULONG i;

    if (!IsEqualGUID(&type->majortype, &MEDIATYPE_Video))
        return DMO_E_INVALIDTYPE;
    for (i = 0; i < format_count; ++i)
        if (IsEqualGUID(&type->subtype, formats[i]))
            break;
    if (i == format_count)
        return DMO_E_INVALIDTYPE;

    if (IsEqualGUID(&type->formattype, &FORMAT_VideoInfo)
            && type->cbFormat >= sizeof(VIDEOINFOHEADER))
    {
        VIDEOINFOHEADER *vih = (VIDEOINFOHEADER *)type->pbFormat;
        if (!vih->bmiHeader.biWidth || !vih->bmiHeader.biHeight)
            return DMO_E_INVALIDTYPE;
        *image_size = vih->bmiHeader.biSizeImage;
        return S_OK;
    }
    if (IsEqualGUID(&type->formattype, &FORMAT_VideoInfo2)
            && type->cbFormat >= sizeof(VIDEOINFOHEADER2))
    {
        VIDEOINFOHEADER2 *vih = (VIDEOINFOHEADER2 *)type->pbFormat;
        if (!vih->bmiHeader.biWidth || !vih->bmiHeader.biHeight)
            return DMO_E_INVALIDTYPE;
        *image_size = vih->bmiHeader.biSizeImage;
        return S_OK;
    }
    if (IsEqualGUID(&type->formattype, &FORMAT_MFVideoFormat)
            && type->cbFormat >= sizeof(MFVIDEOFORMAT))
    {
        MFVIDEOFORMAT *fmt = (MFVIDEOFORMAT *)type->pbFormat;
        if (!fmt->videoInfo.dwWidth || !fmt->videoInfo.dwHeight)
            return DMO_E_INVALIDTYPE;
        return MFCalculateImageSize(&type->subtype, fmt->videoInfo.dwWidth, fmt->videoInfo.dwHeight,
            (UINT32 *)image_size);
    }

    return DMO_E_INVALIDTYPE;
}

static void clear_dmo_media_type(DMO_MEDIA_TYPE *mt)
{
    MoFreeMediaType(mt);
    memset(mt, 0, sizeof(*mt));
}

static HRESULT WINAPI media_object_SetInputType(IMediaObject *iface, DWORD index,
        const DMO_MEDIA_TYPE *type, DWORD flags)
{
    struct color_convert *impl = impl_from_IMediaObject(iface);
    UINT32 image_size;
    HRESULT hr;

    TRACE("converter %p, index %#lx, type %p, flags %#lx.\n", impl, index, type, flags);

    if (index != 0)
        return DMO_E_INVALIDSTREAMINDEX;
    if (!type)
    {
        clear_dmo_media_type(&impl->input_type);
        color_convert_cleanup(impl);
        return S_OK;
    }

    if (FAILED(hr = check_dmo_media_type(type, &image_size, impl->input_types, impl->input_type_count)))
        return hr;
    if (flags & DMO_SET_TYPEF_TEST_ONLY)
        return S_OK;

    clear_dmo_media_type(&impl->input_type);
    if (FAILED(hr = MoCopyMediaType(&impl->input_type, type)))
        return hr;
    impl->input_info.cbSize = image_size;

    if (!IsEqualGUID(&impl->output_type.majortype, &GUID_NULL)
            && FAILED(hr = color_convert_init(impl)))
    {
        clear_dmo_media_type(&impl->input_type);
        impl->input_info.cbSize = 0;
    }
    return hr;
}

static HRESULT WINAPI media_object_SetOutputType(IMediaObject *iface, DWORD index,
        const DMO_MEDIA_TYPE *type, DWORD flags)
{
    struct color_convert *impl = impl_from_IMediaObject(iface);
    UINT32 image_size;
    HRESULT hr;

    TRACE("converter %p, index %#lx, type %p, flags %#lx.\n", impl, index, type, flags);

    if (index != 0)
        return DMO_E_INVALIDSTREAMINDEX;
    if (!type)
    {
        clear_dmo_media_type(&impl->output_type);
        color_convert_cleanup(impl);
        return S_OK;
    }

    /* resizer DMO doesn't support color conversion */
    if (impl->input_types == resizer_input_types && !IsEqualGUID(&impl->input_type.subtype, &type->subtype))
        return DMO_E_INVALIDTYPE;
    if (FAILED(hr = check_dmo_media_type(type, &image_size, impl->output_types, impl->output_type_count)))
        return hr;
    if (flags & DMO_SET_TYPEF_TEST_ONLY)
        return S_OK;

    clear_dmo_media_type(&impl->output_type);
    if (FAILED(hr = MoCopyMediaType(&impl->output_type, type)))
        return hr;
    impl->output_info.cbSize = image_size;

    if (!IsEqualGUID(&impl->input_type.majortype, &GUID_NULL)
            && FAILED(hr = color_convert_init(impl)))
    {
        clear_dmo_media_type(&impl->output_type);
        impl->output_info.cbSize = 0;
    }
    return hr;
}

static HRESULT WINAPI media_object_GetInputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type)
{
    struct color_convert *impl = impl_from_IMediaObject(iface);

    TRACE("iface %p, index %#lx, type %p.\n", iface, index, type);

    if (index != 0)
        return DMO_E_INVALIDSTREAMINDEX;
    if (IsEqualGUID(&impl->input_type.majortype, &GUID_NULL))
        return DMO_E_TYPE_NOT_SET;
    return MoCopyMediaType(type, &impl->input_type);
}

static HRESULT WINAPI media_object_GetOutputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type)
{
    struct color_convert *impl = impl_from_IMediaObject(iface);

    TRACE("iface %p, index %#lx, type %p.\n", iface, index, type);

    if (index != 0)
        return DMO_E_INVALIDSTREAMINDEX;
    if (IsEqualGUID(&impl->output_type.majortype, &GUID_NULL))
        return DMO_E_TYPE_NOT_SET;
    return MoCopyMediaType(type, &impl->output_type);
}

static HRESULT WINAPI media_object_GetInputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size,
        DWORD *lookahead, DWORD *alignment)
{
    FIXME("iface %p, index %lu, size %p, lookahead %p, alignment %p stub!\n", iface, index, size,
            lookahead, alignment);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_GetOutputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size, DWORD *alignment)
{
    FIXME("iface %p, index %lu, size %p, alignment %p stub!\n", iface, index, size, alignment);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_GetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME *latency)
{
    FIXME("iface %p, index %lu, latency %p stub!\n", iface, index, latency);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_SetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME latency)
{
    FIXME("iface %p, index %lu, latency %s stub!\n", iface, index, wine_dbgstr_longlong(latency));
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_Flush(IMediaObject *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_Discontinuity(IMediaObject *iface, DWORD index)
{
    FIXME("iface %p, index %lu stub!\n", iface, index);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_AllocateStreamingResources(IMediaObject *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_FreeStreamingResources(IMediaObject *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_GetInputStatus(IMediaObject *iface, DWORD index, DWORD *flags)
{
    FIXME("iface %p, index %lu, flags %p stub!\n", iface, index, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_ProcessInput(IMediaObject *iface, DWORD index,
        IMediaBuffer *buffer, DWORD flags, REFERENCE_TIME timestamp, REFERENCE_TIME timelength)
{
    DMO_OUTPUT_DATA_BUFFER input = {.pBuffer = buffer, .dwStatus = flags, .rtTimestamp = timestamp,
            .rtTimelength = timelength};
    struct color_convert *impl = impl_from_IMediaObject(iface);

    TRACE("converter %p, index %lu, buffer %p, flags %#lx, timestamp %I64d, timelength %I64d\n",
            impl, index, buffer, flags, timestamp, timelength);

    return color_convert_process_input(impl, &input);
}

static HRESULT WINAPI media_object_ProcessOutput(IMediaObject *iface, DWORD flags, DWORD count,
        DMO_OUTPUT_DATA_BUFFER *output, DWORD *output_status)
{
    struct color_convert *impl = impl_from_IMediaObject(iface);

    TRACE("converter %p, flags %#lx, count %lu, output %p, output_status %p\n", impl, flags, count,
            output, output_status);

    if (count != 1)
        return E_INVALIDARG;
    if (flags)
        FIXME("Unimplemented flags %#lx\n", flags);

    return color_convert_process_output(impl, output);
}

static HRESULT WINAPI media_object_Lock(IMediaObject *iface, LONG lock)
{
    FIXME("iface %p, lock %ld stub!\n", iface, lock);
    return E_NOTIMPL;
}

static const IMediaObjectVtbl media_object_vtbl =
{
    media_object_QueryInterface,
    media_object_AddRef,
    media_object_Release,
    media_object_GetStreamCount,
    media_object_GetInputStreamInfo,
    media_object_GetOutputStreamInfo,
    media_object_GetInputType,
    media_object_GetOutputType,
    media_object_SetInputType,
    media_object_SetOutputType,
    media_object_GetInputCurrentType,
    media_object_GetOutputCurrentType,
    media_object_GetInputSizeInfo,
    media_object_GetOutputSizeInfo,
    media_object_GetInputMaxLatency,
    media_object_SetInputMaxLatency,
    media_object_Flush,
    media_object_Discontinuity,
    media_object_AllocateStreamingResources,
    media_object_FreeStreamingResources,
    media_object_GetInputStatus,
    media_object_ProcessInput,
    media_object_ProcessOutput,
    media_object_Lock,
};

static struct color_convert *impl_from_IMFRealTimeClient(IMFRealTimeClient *iface)
{
    return CONTAINING_RECORD(iface, struct color_convert, IMFRealTimeClient_iface);
}

static HRESULT WINAPI real_time_client_QueryInterface(IMFRealTimeClient *iface, REFIID iid, void **out)
{
    return IUnknown_QueryInterface(impl_from_IMFRealTimeClient(iface)->outer, iid, out);
}

static ULONG WINAPI real_time_client_AddRef(IMFRealTimeClient *iface)
{
    return IUnknown_AddRef(impl_from_IMFRealTimeClient(iface)->outer);
}

static ULONG WINAPI real_time_client_Release(IMFRealTimeClient *iface)
{
    return IUnknown_Release(impl_from_IMFRealTimeClient(iface)->outer);
}

static HRESULT WINAPI real_time_client_RegisterThreads(IMFRealTimeClient *iface, DWORD task_index, const WCHAR *class)
{
    TRACE("iface %p, task_index %lu, class %s.\n", iface, task_index, debugstr_w(class));
    return S_OK;
}

static HRESULT WINAPI real_time_client_UnregisterThreads(IMFRealTimeClient *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT WINAPI real_time_client_SetWorkQueue(IMFRealTimeClient *iface, DWORD queue)
{
    TRACE("iface %p, queue %#lx.\n", iface, queue);
    return S_OK;
}

static const IMFRealTimeClientVtbl real_time_client_vtbl =
{
    real_time_client_QueryInterface,
    real_time_client_AddRef,
    real_time_client_Release,
    real_time_client_RegisterThreads,
    real_time_client_UnregisterThreads,
    real_time_client_SetWorkQueue,
};

static struct color_convert *impl_from_IPropertyStore(IPropertyStore *iface)
{
    return CONTAINING_RECORD(iface, struct color_convert, IPropertyStore_iface);
}

static HRESULT WINAPI property_store_QueryInterface(IPropertyStore *iface, REFIID iid, void **out)
{
    return IUnknown_QueryInterface(impl_from_IPropertyStore(iface)->outer, iid, out);
}

static ULONG WINAPI property_store_AddRef(IPropertyStore *iface)
{
    return IUnknown_AddRef(impl_from_IPropertyStore(iface)->outer);
}

static ULONG WINAPI property_store_Release(IPropertyStore *iface)
{
    return IUnknown_Release(impl_from_IPropertyStore(iface)->outer);
}

static HRESULT WINAPI property_store_GetCount(IPropertyStore *iface, DWORD *count)
{
    FIXME("iface %p, count %p stub!\n", iface, count);
    return E_NOTIMPL;
}

static HRESULT WINAPI property_store_GetAt(IPropertyStore *iface, DWORD index, PROPERTYKEY *key)
{
    FIXME("iface %p, index %lu, key %p stub!\n", iface, index, key);
    return E_NOTIMPL;
}

static HRESULT WINAPI property_store_GetValue(IPropertyStore *iface, REFPROPERTYKEY key, PROPVARIANT *value)
{
    FIXME("iface %p, key %p, value %p stub!\n", iface, key, value);
    return E_NOTIMPL;
}

static HRESULT WINAPI property_store_SetValue(IPropertyStore *iface, REFPROPERTYKEY key, REFPROPVARIANT value)
{
    FIXME("iface %p, key %p, value %p stub!\n", iface, key, value);
    return E_NOTIMPL;
}

static HRESULT WINAPI property_store_Commit(IPropertyStore *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static const IPropertyStoreVtbl property_store_vtbl =
{
    property_store_QueryInterface,
    property_store_AddRef,
    property_store_Release,
    property_store_GetCount,
    property_store_GetAt,
    property_store_GetValue,
    property_store_SetValue,
    property_store_Commit,
};

static HRESULT color_converter_create(const GUID *const *input_types, UINT input_type_count,
        const GUID *const *output_types, UINT output_type_count,
        IUnknown *outer, REFIID riid, void **out)
{
    struct color_convert *impl;
    HRESULT hr;

    if (outer && !IsEqualGUID(riid, &IID_IUnknown))
        return E_NOINTERFACE;


    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IUnknown_inner.lpVtbl = &unknown_vtbl;
    impl->IMFTransform_iface.lpVtbl = &transform_vtbl;
    impl->IMediaObject_iface.lpVtbl = &media_object_vtbl;
    impl->IMFRealTimeClient_iface.lpVtbl = &real_time_client_vtbl;
    impl->IPropertyStore_iface.lpVtbl = &property_store_vtbl;
    impl->refcount = 1;
    impl->outer = outer ? outer : &impl->IUnknown_inner;

    impl->input_type_count = input_type_count;
    impl->input_types = input_types;
    impl->output_type_count = output_type_count;
    impl->output_types = output_types;

    impl->input_info.cbAlignment = 1;
    impl->output_info.cbAlignment = 1;

    TRACE("Created converter %p\n", impl);

    hr = IUnknown_QueryInterface(&impl->IUnknown_inner, riid, out);
    IUnknown_Release(&impl->IUnknown_inner);
    return hr;
}

static HRESULT WINAPI color_converter_factory_CreateInstance(IClassFactory *iface, IUnknown *outer,
        REFIID riid, void **out)
{
    TRACE("outer %p, riid %s, out %p.\n", outer, debugstr_guid(riid), out);
    return color_converter_create(converter_input_types, ARRAY_SIZE(converter_input_types),
            converter_output_types, ARRAY_SIZE(converter_output_types), outer, riid, out);
}

static HRESULT WINAPI resizer_factory_CreateInstance(IClassFactory *iface, IUnknown *outer,
        REFIID riid, void **out)
{
    TRACE("outer %p, riid %s, out %p.\n", outer, debugstr_guid(riid), out);
    return color_converter_create(resizer_input_types, ARRAY_SIZE(resizer_input_types),
            resizer_output_types, ARRAY_SIZE(resizer_output_types), outer, riid, out);
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
    return S_OK;
}

static const IClassFactoryVtbl color_converter_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    color_converter_factory_CreateInstance,
    class_factory_LockServer,
};

static const IClassFactoryVtbl resizer_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    resizer_factory_CreateInstance,
    class_factory_LockServer,
};

IClassFactory color_converter_factory = {&color_converter_factory_vtbl};
IClassFactory resizer_factory = {&resizer_factory_vtbl};
