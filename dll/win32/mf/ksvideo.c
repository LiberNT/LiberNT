/*
 * PROJECT:     LiberNT Media Foundation
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Video capture media source over kernel streaming
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS

#include "mf_private.h"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <winioctl.h>
#include <ks.h>
#include <ksmedia.h>
#include <mferror.h>
#include <wincodec.h>
#include <dshow.h>

#include "wine/debug.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);

static const GUID ks_category_camera = {0xe5323777, 0xf976, 0x4f5b, {0x9b, 0x55, 0xb9, 0x46, 0x99, 0xc4, 0x6e, 0x44}};
static const GUID ks_propset_pin = {STATIC_KSPROPSETID_Pin};
static const GUID ks_propset_connection = {STATIC_KSPROPSETID_Connection};
static const GUID ks_interface_standard = {STATIC_KSINTERFACESETID_Standard};
static const GUID ks_medium_standard = {STATIC_KSMEDIUMSETID_Standard};
static const GUID ks_propset_procamp = {0xc6e13360, 0x30ac, 0x11d0, {0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56}};
static const GUID ks_propset_camera = {0xc6e13370, 0x30ac, 0x11d0, {0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56}};
static const GUID ks_propset_videocontrol = {0x6a2e0670, 0x28e4, 0x11d0, {0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56}};
static const GUID ks_pin_capture = {0xfb6c4281, 0x0353, 0x11d1, {0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba}};
static const GUID ks_pin_still = {0xfb6c428a, 0x0353, 0x11d1, {0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba}};
static const GUID ks_stream_category = {0x2939e7b8, 0xa62e, 0x4579, {0xb6, 0x74, 0xd4, 0x07, 0x3d, 0xfa, 0xbb, 0xba}};
static const GUID ks_stream_frame_source_types = {0x17145fd1, 0x1b2b, 0x423c, {0x80, 0x01, 0x2b, 0x68, 0x33, 0xed, 0x35, 0x88}};
static const GUID ks_iid_control = {0x28f54685, 0x06fd, 0x11d2, {0xb2, 0x7a, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96}};
static const GUID ks_specifier_videoinfo2 = {0xf72a76a0, 0xeb0a, 0x11d0, {0xac, 0xe4, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba}};
static const GUID ks_mt_decoded_mjpeg = {0xea031a62, 0x8bbb, 0x43c5, {0xb5, 0xc4, 0x57, 0x2d, 0x2d, 0x23, 0x1c, 0x18}};
static const GUID ks_specifier_videoinfo = {0x05589f80, 0xc356, 0x11ce, {0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a}};

enum ks_source_state
{
    KS_SOURCE_STOPPED,
    KS_SOURCE_RUNNING,
    KS_SOURCE_PAUSED,
    KS_SOURCE_SHUTDOWN,
};

struct ks_format
{
    KS_DATARANGE_VIDEO range;
    LONGLONG interval;
    DWORD control_flags;
    BOOL decode_nv12;
    BOOL still;
};

struct ks_source;

struct ks_pump
{
    struct ks_source *source;
    const struct ks_format *format;
    HANDLE pin;
    struct list *tokens;
    IMFMediaEventQueue *queue;
};

struct ks_control;

struct ks_control_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(struct ks_control *iface, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(struct ks_control *iface);
    ULONG (STDMETHODCALLTYPE *Release)(struct ks_control *iface);
    HRESULT (STDMETHODCALLTYPE *KsProperty)(struct ks_control *iface, KSPROPERTY *property, ULONG property_size, void *data, ULONG data_size, ULONG *returned);
    HRESULT (STDMETHODCALLTYPE *KsMethod)(struct ks_control *iface, KSMETHOD *method, ULONG method_size, void *data, ULONG data_size, ULONG *returned);
    HRESULT (STDMETHODCALLTYPE *KsEvent)(struct ks_control *iface, KSEVENT *event, ULONG event_size, void *data, ULONG data_size, ULONG *returned);
};

struct ks_control
{
    const struct ks_control_vtbl *lpVtbl;
};

struct ks_still_stream
{
    IMFMediaStream IMFMediaStream_iface;
    struct ks_source *source;
    IMFMediaEventQueue *queue;
    IMFStreamDescriptor *descriptor;
    struct ks_format *formats;
    DWORD format_count;
    struct ks_format *current;
    ULONG pin_id;
    HANDLE pin;
    HANDLE thread;
    struct ks_pump pump;
    struct list tokens;
    BOOL announced;
    BOOL active;
};

struct ks_token
{
    struct list entry;
    IUnknown *token;
};

struct ks_source
{
    IMFMediaSource IMFMediaSource_iface;
    IMFMediaStream IMFMediaStream_iface;
    IAMCameraControl IAMCameraControl_iface;
    IAMVideoProcAmp IAMVideoProcAmp_iface;
    IAMVideoControl IAMVideoControl_iface;
    IMFGetService IMFGetService_iface;
    struct ks_control ks_control_iface;
    LONG refcount;

    IMFMediaEventQueue *event_queue;
    IMFMediaEventQueue *stream_queue;
    IMFPresentationDescriptor *descriptor;
    IMFStreamDescriptor *stream_descriptor;
    CRITICAL_SECTION cs;

    HANDLE filter;
    HANDLE pin;
    HANDLE thread;
    HANDLE stop_event;
    HANDLE lost_event;
    HCMNOTIFICATION notify;
    WCHAR *path;
    enum ks_source_state state;
    BOOL stream_announced;
    struct ks_format *formats;
    DWORD format_count;
    struct ks_format *current;
    struct ks_pump pump;
    struct list tokens;
    struct ks_still_stream *still;
};

static struct ks_source *impl_from_IMFMediaSource(IMFMediaSource *iface)
{
    return CONTAINING_RECORD(iface, struct ks_source, IMFMediaSource_iface);
}

static struct ks_source *impl_from_IMFMediaStream(IMFMediaStream *iface)
{
    return CONTAINING_RECORD(iface, struct ks_source, IMFMediaStream_iface);
}

static BOOL ks_ioctl(HANDLE handle, DWORD code, void *in, DWORD in_size, void *out, DWORD out_size, DWORD *returned)
{
    OVERLAPPED overlapped = {0};
    DWORD local = 0;
    BOOL ret;

    if (!(overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)))
        return FALSE;

    ret = DeviceIoControl(handle, code, in, in_size, out, out_size, &local, &overlapped);
    if (!ret && GetLastError() == ERROR_IO_PENDING)
        ret = GetOverlappedResult(handle, &overlapped, &local, TRUE);

    CloseHandle(overlapped.hEvent);
    if (returned)
        *returned = local;
    return ret;
}

static BOOL ks_set_state(HANDLE pin, KSSTATE state)
{
    KSPROPERTY property;
    DWORD returned;

    property.Set = ks_propset_connection;
    property.Id = KSPROPERTY_CONNECTION_STATE;
    property.Flags = KSPROPERTY_TYPE_SET;
    return ks_ioctl(pin, IOCTL_KS_PROPERTY, &property, sizeof(property), &state, sizeof(state), &returned);
}

static void ks_add_format(struct ks_format *formats, DWORD *count, const KS_DATARANGE_VIDEO *video, LONGLONG interval, BOOL still)
{
    struct ks_format *format;

    if (IsEqualGUID(&video->DataRange.SubFormat, &MFVideoFormat_MJPG))
    {
        format = &formats[(*count)++];
        format->range = *video;
        format->interval = interval;
        format->decode_nv12 = TRUE;
        format->still = still;
    }
    format = &formats[(*count)++];
    format->range = *video;
    format->interval = interval;
    format->still = still;
}

static void ks_set_control_flags(struct ks_source *source, const KS_DATARANGE_VIDEO2 *video)
{
    DWORD i;

    for (i = 0; i < source->format_count; ++i)
    {
        struct ks_format *format = &source->formats[i];

        if (IsEqualGUID(&format->range.DataRange.SubFormat, &video->DataRange.SubFormat) &&
                format->range.VideoInfoHeader.bmiHeader.biWidth == video->VideoInfoHeader.bmiHeader.biWidth &&
                format->range.VideoInfoHeader.bmiHeader.biHeight == video->VideoInfoHeader.bmiHeader.biHeight)
            format->control_flags = video->VideoInfoHeader.dwControlFlags;
    }
}

static HRESULT ks_load_formats(struct ks_source *source)
{
    KSMULTIPLE_ITEM *multiple;
    KSP_PIN property;
    DWORD size = 0;
    BYTE *item;
    ULONG i;

    memset(&property, 0, sizeof(property));
    property.Property.Set = ks_propset_pin;
    property.Property.Id = KSPROPERTY_PIN_DATARANGES;
    property.Property.Flags = KSPROPERTY_TYPE_GET;

    ks_ioctl(source->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), NULL, 0, &size);
    if (!size || !(multiple = malloc(size)))
        return MF_E_NOT_FOUND;

    if (!ks_ioctl(source->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), multiple, size, &size) ||
            !(source->formats = calloc(multiple->Count * 4, sizeof(*source->formats))))
    {
        free(multiple);
        return E_FAIL;
    }

    item = (BYTE *)(multiple + 1);
    for (i = 0; i < multiple->Count; ++i)
    {
        KSDATARANGE *range = (KSDATARANGE *)item;

        if (IsEqualGUID(&range->Specifier, &ks_specifier_videoinfo) && range->FormatSize >= sizeof(KS_DATARANGE_VIDEO))
        {
            const KS_DATARANGE_VIDEO *video = (const KS_DATARANGE_VIDEO *)range;

            ks_add_format(source->formats, &source->format_count, video, video->ConfigCaps.MinFrameInterval, FALSE);
            if (video->ConfigCaps.MaxFrameInterval != video->ConfigCaps.MinFrameInterval)
                ks_add_format(source->formats, &source->format_count, video, video->ConfigCaps.MaxFrameInterval, FALSE);
        }
        else if (IsEqualGUID(&range->Specifier, &ks_specifier_videoinfo2) && range->FormatSize >= sizeof(KS_DATARANGE_VIDEO2))
        {
            ks_set_control_flags(source, (const KS_DATARANGE_VIDEO2 *)range);
        }
        item += (range->FormatSize + 7) & ~7u;
    }

    free(multiple);
    return source->format_count ? S_OK : MF_E_NOT_FOUND;
}

static HRESULT ks_load_still_formats(struct ks_source *source)
{
    KSPROPERTY count_property;
    KSMULTIPLE_ITEM *multiple;
    KSP_PIN property;
    ULONG pins = 0, pin, i;
    DWORD size = 0;
    BYTE *item;
    struct ks_still_stream *still;

    count_property.Set = ks_propset_pin;
    count_property.Id = KSPROPERTY_PIN_CTYPES;
    count_property.Flags = KSPROPERTY_TYPE_GET;
    if (!ks_ioctl(source->filter, IOCTL_KS_PROPERTY, &count_property, sizeof(count_property), &pins, sizeof(pins), &size))
        return S_OK;

    memset(&property, 0, sizeof(property));
    property.Property.Set = ks_propset_pin;
    property.Property.Flags = KSPROPERTY_TYPE_GET;
    for (pin = 0; pin < pins; ++pin)
    {
        GUID category = {0};

        property.Property.Id = KSPROPERTY_PIN_CATEGORY;
        property.PinId = pin;
        if (ks_ioctl(source->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), &category, sizeof(category), &size) &&
                IsEqualGUID(&category, &ks_pin_still))
            break;
    }
    if (pin >= pins)
        return S_OK;

    property.Property.Id = KSPROPERTY_PIN_DATARANGES;
    size = 0;
    ks_ioctl(source->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), NULL, 0, &size);
    if (!size || !(multiple = malloc(size)))
        return E_OUTOFMEMORY;

    if (!ks_ioctl(source->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), multiple, size, &size) ||
            !(still = calloc(1, sizeof(*still))))
    {
        free(multiple);
        return E_FAIL;
    }
    if (!(still->formats = calloc(multiple->Count * 2, sizeof(*still->formats))))
    {
        free(still);
        free(multiple);
        return E_OUTOFMEMORY;
    }

    item = (BYTE *)(multiple + 1);
    for (i = 0; i < multiple->Count; ++i)
    {
        KSDATARANGE *range = (KSDATARANGE *)item;

        if (IsEqualGUID(&range->Specifier, &ks_specifier_videoinfo) && range->FormatSize >= sizeof(KS_DATARANGE_VIDEO))
        {
            const KS_DATARANGE_VIDEO *video = (const KS_DATARANGE_VIDEO *)range;

            ks_add_format(still->formats, &still->format_count, video, video->ConfigCaps.MinFrameInterval, TRUE);
        }
        item += (range->FormatSize + 7) & ~7u;
    }
    free(multiple);

    still->source = source;
    still->pin_id = pin;
    list_init(&still->tokens);
    source->still = still;
    return S_OK;
}

static void ks_interval_to_rate(LONGLONG interval, UINT32 *numerator, UINT32 *denominator)
{
    LONGLONG whole = (10000000 + interval / 2) / interval;
    LONGLONG error = whole * interval - 10000000;

    if (error < 0)
        error = -error;

    if (error <= whole)
    {
        *numerator = (UINT32)whole;
        *denominator = 1;
    }
    else
    {
        MFAverageTimePerFrameToFrameRate(interval, numerator, denominator);
    }
}

static HRESULT ks_create_media_type(const struct ks_format *format, IMFMediaType **ret)
{
    const KS_BITMAPINFOHEADER *bmi = &format->range.VideoInfoHeader.bmiHeader;
    UINT32 numerator = 0, denominator = 0;
    IMFMediaType *type;
    HRESULT hr;

    if (FAILED(hr = MFCreateMediaType(&type)))
        return hr;

    ks_interval_to_rate(format->interval, &numerator, &denominator);

    IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, format->decode_nv12 ? &MFVideoFormat_NV12 : &format->range.DataRange.SubFormat);
    IMFMediaType_SetGUID(type, &MF_MT_AM_FORMAT_TYPE, format->still ? &ks_specifier_videoinfo : &ks_specifier_videoinfo2);
    IMFMediaType_SetUINT64(type, &MF_MT_FRAME_SIZE, ((UINT64)bmi->biWidth << 32) | (UINT32)bmi->biHeight);
    IMFMediaType_SetUINT64(type, &MF_MT_FRAME_RATE, ((UINT64)numerator << 32) | denominator);
    IMFMediaType_SetUINT64(type, &MF_MT_FRAME_RATE_RANGE_MIN, ((UINT64)numerator << 32) | denominator);
    IMFMediaType_SetUINT64(type, &MF_MT_FRAME_RATE_RANGE_MAX, ((UINT64)numerator << 32) | denominator);
    IMFMediaType_SetUINT64(type, &MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1 << 32) | 1);
    IMFMediaType_SetUINT32(type, &MF_MT_FIXED_SIZE_SAMPLES, 1);
    IMFMediaType_SetUINT32(type, &MF_MT_ALL_SAMPLES_INDEPENDENT, 1);
    IMFMediaType_SetUINT32(type, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (!format->still)
    {
        IMFMediaType_SetUINT32(type, &MF_MT_VIDEO_CHROMA_SITING, (format->control_flags >> 8) & 0xf);
        IMFMediaType_SetUINT32(type, &MF_MT_VIDEO_NOMINAL_RANGE, (format->control_flags >> 12) & 0x7);
        IMFMediaType_SetUINT32(type, &MF_MT_YUV_MATRIX, (format->control_flags >> 15) & 0x7);
        IMFMediaType_SetUINT32(type, &MF_MT_VIDEO_LIGHTING, (format->control_flags >> 18) & 0xf);
        IMFMediaType_SetUINT32(type, &MF_MT_VIDEO_PRIMARIES, (format->control_flags >> 22) & 0x1f);
    }
    if (format->decode_nv12)
    {
        IMFMediaType_SetUINT32(type, &ks_mt_decoded_mjpeg, 1);
    }
    else
    {
        IMFMediaType_SetUINT32(type, &MF_MT_SAMPLE_SIZE, bmi->biSizeImage);
        IMFMediaType_SetUINT32(type, &MF_MT_AVG_BITRATE, format->range.VideoInfoHeader.dwBitRate);
        if (format->range.bFixedSizeSamples)
            IMFMediaType_SetUINT32(type, &MF_MT_DEFAULT_STRIDE, bmi->biWidth * bmi->biBitCount / 8);
    }

    *ret = type;
    return S_OK;
}

static struct ks_format *ks_find_format(struct ks_format *formats, DWORD count, IMFMediaType *type)
{
    UINT64 size = 0, rate = 0;
    GUID subtype;
    DWORD i;

    if (FAILED(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype)) ||
            FAILED(IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &size)))
    {
        return NULL;
    }
    IMFMediaType_GetUINT64(type, &MF_MT_FRAME_RATE, &rate);

    for (i = 0; i < count; ++i)
    {
        struct ks_format *format = &formats[i];
        const KS_BITMAPINFOHEADER *bmi = &format->range.VideoInfoHeader.bmiHeader;
        UINT32 numerator = 0, denominator = 0;

        if (!IsEqualGUID(&subtype, format->decode_nv12 ? &MFVideoFormat_NV12 : &format->range.DataRange.SubFormat) ||
                (UINT32)(size >> 32) != (UINT32)bmi->biWidth || (UINT32)size != (UINT32)bmi->biHeight)
        {
            continue;
        }

        ks_interval_to_rate(format->interval, &numerator, &denominator);
        if (!rate || rate == (((UINT64)numerator << 32) | denominator))
            return format;
    }

    return NULL;
}

static HRESULT ks_open_pin(struct ks_source *source, ULONG pin_id, const struct ks_format *format, HANDLE *pin)
{
    struct
    {
        KSPIN_CONNECT connect;
        KS_DATAFORMAT_VIDEOINFOHEADER format;
    } request;
    DWORD error;

    memset(&request, 0, sizeof(request));
    request.connect.Interface.Set = ks_interface_standard;
    request.connect.Interface.Id = KSINTERFACE_STANDARD_STREAMING;
    request.connect.Medium.Set = ks_medium_standard;
    request.connect.Medium.Id = KSMEDIUM_TYPE_ANYINSTANCE;
    request.connect.PinId = pin_id;
    request.connect.Priority.PriorityClass = KSPRIORITY_NORMAL;
    request.connect.Priority.PrioritySubClass = KSPRIORITY_NORMAL;
    request.format.DataFormat.FormatSize = sizeof(request.format);
    request.format.DataFormat.SampleSize = format->range.DataRange.SampleSize;
    request.format.DataFormat.MajorFormat = format->range.DataRange.MajorFormat;
    request.format.DataFormat.SubFormat = format->range.DataRange.SubFormat;
    request.format.DataFormat.Specifier = format->range.DataRange.Specifier;
    request.format.VideoInfoHeader = format->range.VideoInfoHeader;
    request.format.VideoInfoHeader.AvgTimePerFrame = format->interval;

    if ((error = KsCreatePin(source->filter, &request.connect, GENERIC_READ, pin)))
    {
        ERR("KsCreatePin failed, error %lu.\n", error);
        *pin = NULL;
        return HRESULT_FROM_WIN32(error);
    }

    if (!ks_set_state(*pin, KSSTATE_ACQUIRE) || !ks_set_state(*pin, KSSTATE_PAUSE) ||
            !ks_set_state(*pin, KSSTATE_RUN))
    {
        error = GetLastError();
        CloseHandle(*pin);
        *pin = NULL;
        return HRESULT_FROM_WIN32(error);
    }

    return S_OK;
}

static BOOL ks_read_frame(struct ks_source *source, HANDLE pin, BYTE *data, DWORD size, DWORD *used)
{
    struct
    {
        KSSTREAM_HEADER header;
        KS_FRAME_INFO info;
    } request;
    OVERLAPPED overlapped = {0};
    HANDLE handles[3];
    DWORD returned = 0;
    BOOL ret;

    memset(&request, 0, sizeof(request));
    request.header.Size = sizeof(request);
    request.header.FrameExtent = size;
    request.header.Data = data;
    request.info.ExtendedHeaderSize = sizeof(request.info);

    if (!(overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)))
        return FALSE;

    ret = DeviceIoControl(pin, IOCTL_KS_READ_STREAM, NULL, 0, &request, sizeof(request), &returned, &overlapped);
    if (!ret && GetLastError() == ERROR_IO_PENDING)
    {
        handles[0] = source->stop_event;
        handles[1] = source->lost_event;
        handles[2] = overlapped.hEvent;
        if (WaitForMultipleObjects(3, handles, FALSE, INFINITE) != WAIT_OBJECT_0 + 2)
        {
            CancelIo(pin);
            GetOverlappedResult(pin, &overlapped, &returned, TRUE);
            CloseHandle(overlapped.hEvent);
            return FALSE;
        }
        ret = GetOverlappedResult(pin, &overlapped, &returned, FALSE);
    }

    CloseHandle(overlapped.hEvent);
    *used = request.header.DataUsed;
    return ret;
}

static void ks_deliver_sample(struct ks_pump *pump, IMFMediaBuffer *buffer)
{
    struct ks_source *source = pump->source;
    LONGLONG interval = pump->format->interval;
    struct ks_token *token = NULL;
    IMFSample *sample;

    EnterCriticalSection(&source->cs);
    if (source->state == KS_SOURCE_RUNNING && !list_empty(pump->tokens))
    {
        token = LIST_ENTRY(list_head(pump->tokens), struct ks_token, entry);
        list_remove(&token->entry);
    }
    LeaveCriticalSection(&source->cs);

    if (!token)
        return;

    if (SUCCEEDED(MFCreateSample(&sample)))
    {
        IMFSample_AddBuffer(sample, buffer);
        IMFSample_SetSampleTime(sample, MFGetSystemTime());
        IMFSample_SetSampleDuration(sample, pump->format->still ? 0 : interval);
        IMFSample_SetUINT32(sample, &MFSampleExtension_CleanPoint, 1);
        if (token->token)
            IMFSample_SetUnknown(sample, &MFSampleExtension_Token, token->token);
        IMFMediaEventQueue_QueueEventParamUnk(pump->queue, MEMediaSample, &GUID_NULL, S_OK, (IUnknown *)sample);
        IMFSample_Release(sample);
    }

    if (token->token)
        IUnknown_Release(token->token);
    free(token);
}

static HRESULT ks_decode_mjpeg(IWICImagingFactory *factory, const BYTE *jpeg, DWORD used, BYTE *bgr, UINT stride, UINT size)
{
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICFormatConverter *converter = NULL;
    IWICStream *stream = NULL;
    HRESULT hr;

    if (FAILED(hr = IWICImagingFactory_CreateStream(factory, &stream)))
        return hr;
    if (SUCCEEDED(hr = IWICStream_InitializeFromMemory(stream, (BYTE *)jpeg, used)))
        hr = IWICImagingFactory_CreateDecoderFromStream(factory, (IStream *)stream, NULL, WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr))
        hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (SUCCEEDED(hr))
        hr = IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame, &GUID_WICPixelFormat24bppBGR,
                                            WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr))
        hr = IWICFormatConverter_CopyPixels(converter, NULL, stride, size, bgr);

    if (converter)
        IWICFormatConverter_Release(converter);
    if (frame)
        IWICBitmapFrameDecode_Release(frame);
    if (decoder)
        IWICBitmapDecoder_Release(decoder);
    IWICStream_Release(stream);
    return hr;
}

static void ks_bgr_to_nv12(const BYTE *bgr, UINT stride, UINT width, UINT height, BYTE *nv12)
{
    BYTE *chroma = nv12 + width * height;
    UINT x, y;

    for (y = 0; y < height; ++y)
    {
        const BYTE *row = bgr + y * stride;
        BYTE *luma = nv12 + y * width;

        for (x = 0; x < width; ++x)
        {
            INT b = row[x * 3], g = row[x * 3 + 1], r = row[x * 3 + 2];

            luma[x] = (BYTE)((19595 * r + 38470 * g + 7471 * b + 32768) >> 16);
            if (!(x & 1) && !(y & 1))
            {
                *chroma++ = (BYTE)min(max((-11059 * r - 21709 * g + 32768 * b + 8421376) >> 16, 0), 255);
                *chroma++ = (BYTE)min(max((32768 * r - 27439 * g - 5329 * b + 8421376) >> 16, 0), 255);
            }
        }
    }
}

static DWORD ks_stream_decoded(struct ks_pump *pump, BYTE *jpeg, DWORD jpeg_size,
        IWICImagingFactory *factory, BYTE *bgr, UINT stride, BOOL *lost)
{
    struct ks_source *source = pump->source;
    const struct ks_format *format = pump->format;
    UINT width = format->range.VideoInfoHeader.bmiHeader.biWidth;
    UINT height = format->range.VideoInfoHeader.bmiHeader.biHeight;
    DWORD nv12_size = width * height * 3 / 2, used = 0;
    IMFMediaBuffer *buffer;
    BYTE *data;

    if (!ks_read_frame(source, pump->pin, jpeg, jpeg_size, &used))
    {
        *lost = TRUE;
        return 0;
    }

    if (FAILED(ks_decode_mjpeg(factory, jpeg, used, bgr, stride, stride * height)))
        return 0;

    if (FAILED(MFCreateMemoryBuffer(nv12_size, &buffer)))
        return 0;
    if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, NULL, NULL)))
    {
        ks_bgr_to_nv12(bgr, stride, width, height, data);
        IMFMediaBuffer_Unlock(buffer);
        IMFMediaBuffer_SetCurrentLength(buffer, nv12_size);
        ks_deliver_sample(pump, buffer);
    }
    IMFMediaBuffer_Release(buffer);
    return nv12_size;
}

static void ks_stream_decoding(struct ks_pump *pump)
{
    struct ks_source *source = pump->source;
    const struct ks_format *format = pump->format;
    UINT width = format->range.VideoInfoHeader.bmiHeader.biWidth;
    UINT height = format->range.VideoInfoHeader.bmiHeader.biHeight;
    DWORD size = format->range.VideoInfoHeader.bmiHeader.biSizeImage;
    UINT stride = (width * 3 + 3) & ~3u;
    IWICImagingFactory *factory = NULL;
    BYTE *jpeg = malloc(size), *bgr = malloc(stride * height);
    BOOL lost = FALSE, uninit;

    uninit = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    if (jpeg && bgr && SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
            &IID_IWICImagingFactory, (void **)&factory)))
    {
        while (!lost && WaitForSingleObject(source->stop_event, 0) != WAIT_OBJECT_0)
            ks_stream_decoded(pump, jpeg, size, factory, bgr, stride, &lost);
        IWICImagingFactory_Release(factory);
    }
    else
    {
        lost = TRUE;
    }
    if (uninit)
        CoUninitialize();
    free(bgr);
    free(jpeg);

    if (lost && WaitForSingleObject(source->stop_event, 0) != WAIT_OBJECT_0)
        IMFMediaEventQueue_QueueEventParamVar(pump->queue, MEError, &GUID_NULL, MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED, NULL);
}

static DWORD CALLBACK ks_stream_thread(void *arg)
{
    struct ks_pump *pump = arg;
    struct ks_source *source = pump->source;
    const struct ks_format *format = pump->format;
    DWORD size = format->range.VideoInfoHeader.bmiHeader.biSizeImage;

    if (format->decode_nv12)
    {
        ks_stream_decoding(pump);
        return 0;
    }

    while (WaitForSingleObject(source->stop_event, 0) != WAIT_OBJECT_0)
    {
        IMFMediaBuffer *buffer;
        DWORD used = 0;
        BYTE *data;
        BOOL ret;

        if (FAILED(MFCreateMemoryBuffer(size, &buffer)))
            break;

        if (FAILED(IMFMediaBuffer_Lock(buffer, &data, NULL, NULL)))
        {
            IMFMediaBuffer_Release(buffer);
            break;
        }

        ret = ks_read_frame(source, pump->pin, data, size, &used);
        IMFMediaBuffer_Unlock(buffer);

        if (ret)
        {
            IMFMediaBuffer_SetCurrentLength(buffer, used);
            ks_deliver_sample(pump, buffer);
        }
        IMFMediaBuffer_Release(buffer);

        if (!ret)
        {
            if (WaitForSingleObject(source->stop_event, 0) != WAIT_OBJECT_0)
                IMFMediaEventQueue_QueueEventParamVar(pump->queue, MEError, &GUID_NULL, MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED, NULL);
            break;
        }
    }

    return 0;
}

static void ks_stop_stream(struct ks_source *source)
{
    if (source->thread || (source->still && source->still->thread))
        SetEvent(source->stop_event);
    if (source->still && source->still->thread)
    {
        WaitForSingleObject(source->still->thread, INFINITE);
        CloseHandle(source->still->thread);
        source->still->thread = NULL;
    }
    if (source->still && source->still->pin)
    {
        ks_set_state(source->still->pin, KSSTATE_PAUSE);
        ks_set_state(source->still->pin, KSSTATE_ACQUIRE);
        ks_set_state(source->still->pin, KSSTATE_STOP);
        CloseHandle(source->still->pin);
        source->still->pin = NULL;
    }
    if (source->thread)
    {
        WaitForSingleObject(source->thread, INFINITE);
        CloseHandle(source->thread);
        source->thread = NULL;
    }
    ResetEvent(source->stop_event);

    if (source->pin)
    {
        ks_set_state(source->pin, KSSTATE_PAUSE);
        ks_set_state(source->pin, KSSTATE_ACQUIRE);
        ks_set_state(source->pin, KSSTATE_STOP);
        CloseHandle(source->pin);
        source->pin = NULL;
    }
}

static void ks_flush_token_list(struct list *tokens)
{
    struct ks_token *token, *next;

    LIST_FOR_EACH_ENTRY_SAFE(token, next, tokens, struct ks_token, entry)
    {
        list_remove(&token->entry);
        if (token->token)
            IUnknown_Release(token->token);
        free(token);
    }
}

static void ks_flush_tokens(struct ks_source *source)
{
    ks_flush_token_list(&source->tokens);
    if (source->still)
        ks_flush_token_list(&source->still->tokens);
}

static HRESULT WINAPI stream_QueryInterface(IMFMediaStream *iface, REFIID riid, void **obj)
{
    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IMFMediaStream) ||
            IsEqualIID(riid, &IID_IMFMediaEventGenerator) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IMFMediaStream_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI stream_AddRef(IMFMediaStream *iface)
{
    return IMFMediaSource_AddRef(&impl_from_IMFMediaStream(iface)->IMFMediaSource_iface);
}

static ULONG WINAPI stream_Release(IMFMediaStream *iface)
{
    return IMFMediaSource_Release(&impl_from_IMFMediaStream(iface)->IMFMediaSource_iface);
}

static HRESULT WINAPI stream_GetEvent(IMFMediaStream *iface, DWORD flags, IMFMediaEvent **event)
{
    return IMFMediaEventQueue_GetEvent(impl_from_IMFMediaStream(iface)->stream_queue, flags, event);
}

static HRESULT WINAPI stream_BeginGetEvent(IMFMediaStream *iface, IMFAsyncCallback *callback, IUnknown *state)
{
    return IMFMediaEventQueue_BeginGetEvent(impl_from_IMFMediaStream(iface)->stream_queue, callback, state);
}

static HRESULT WINAPI stream_EndGetEvent(IMFMediaStream *iface, IMFAsyncResult *result, IMFMediaEvent **event)
{
    return IMFMediaEventQueue_EndGetEvent(impl_from_IMFMediaStream(iface)->stream_queue, result, event);
}

static HRESULT WINAPI stream_QueueEvent(IMFMediaStream *iface, MediaEventType type, REFGUID ext, HRESULT hr, const PROPVARIANT *value)
{
    return IMFMediaEventQueue_QueueEventParamVar(impl_from_IMFMediaStream(iface)->stream_queue, type, ext, hr, value);
}

static HRESULT WINAPI stream_GetMediaSource(IMFMediaStream *iface, IMFMediaSource **ret)
{
    struct ks_source *source = impl_from_IMFMediaStream(iface);

    *ret = &source->IMFMediaSource_iface;
    IMFMediaSource_AddRef(*ret);
    return S_OK;
}

static HRESULT WINAPI stream_GetStreamDescriptor(IMFMediaStream *iface, IMFStreamDescriptor **ret)
{
    struct ks_source *source = impl_from_IMFMediaStream(iface);

    *ret = source->stream_descriptor;
    IMFStreamDescriptor_AddRef(*ret);
    return S_OK;
}

static HRESULT WINAPI stream_RequestSample(IMFMediaStream *iface, IUnknown *token)
{
    struct ks_source *source = impl_from_IMFMediaStream(iface);
    struct ks_token *entry;
    HRESULT hr = S_OK;

    TRACE("%p, %p.\n", iface, token);

    if (!(entry = malloc(sizeof(*entry))))
        return E_OUTOFMEMORY;

    entry->token = token;
    if (token)
        IUnknown_AddRef(token);

    EnterCriticalSection(&source->cs);
    if (source->state == KS_SOURCE_SHUTDOWN)
        hr = MF_E_SHUTDOWN;
    else if (source->state == KS_SOURCE_STOPPED)
        hr = MF_E_MEDIA_SOURCE_WRONGSTATE;
    else if (WaitForSingleObject(source->lost_event, 0) == WAIT_OBJECT_0)
        IMFMediaEventQueue_QueueEventParamVar(source->stream_queue, MEError, &GUID_NULL, MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED, NULL);
    else
    {
        list_add_tail(&source->tokens, &entry->entry);
        entry = NULL;
    }
    LeaveCriticalSection(&source->cs);

    if (entry)
    {
        if (token)
            IUnknown_Release(token);
        free(entry);
    }

    return hr;
}

static const IMFMediaStreamVtbl ks_stream_vtbl =
{
    stream_QueryInterface,
    stream_AddRef,
    stream_Release,
    stream_GetEvent,
    stream_BeginGetEvent,
    stream_EndGetEvent,
    stream_QueueEvent,
    stream_GetMediaSource,
    stream_GetStreamDescriptor,
    stream_RequestSample,
};

static inline struct ks_still_stream *impl_from_still_stream(IMFMediaStream *iface)
{
    return CONTAINING_RECORD(iface, struct ks_still_stream, IMFMediaStream_iface);
}

static HRESULT WINAPI still_stream_QueryInterface(IMFMediaStream *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IMFMediaStream) ||
            IsEqualIID(riid, &IID_IMFMediaEventGenerator) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IMFMediaStream_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI still_stream_AddRef(IMFMediaStream *iface)
{
    return IMFMediaSource_AddRef(&impl_from_still_stream(iface)->source->IMFMediaSource_iface);
}

static ULONG WINAPI still_stream_Release(IMFMediaStream *iface)
{
    return IMFMediaSource_Release(&impl_from_still_stream(iface)->source->IMFMediaSource_iface);
}

static HRESULT WINAPI still_stream_GetEvent(IMFMediaStream *iface, DWORD flags, IMFMediaEvent **event)
{
    return IMFMediaEventQueue_GetEvent(impl_from_still_stream(iface)->queue, flags, event);
}

static HRESULT WINAPI still_stream_BeginGetEvent(IMFMediaStream *iface, IMFAsyncCallback *callback, IUnknown *state)
{
    return IMFMediaEventQueue_BeginGetEvent(impl_from_still_stream(iface)->queue, callback, state);
}

static HRESULT WINAPI still_stream_EndGetEvent(IMFMediaStream *iface, IMFAsyncResult *result, IMFMediaEvent **event)
{
    return IMFMediaEventQueue_EndGetEvent(impl_from_still_stream(iface)->queue, result, event);
}

static HRESULT WINAPI still_stream_QueueEvent(IMFMediaStream *iface, MediaEventType type, REFGUID ext, HRESULT hr, const PROPVARIANT *value)
{
    return IMFMediaEventQueue_QueueEventParamVar(impl_from_still_stream(iface)->queue, type, ext, hr, value);
}

static HRESULT WINAPI still_stream_GetMediaSource(IMFMediaStream *iface, IMFMediaSource **ret)
{
    struct ks_still_stream *stream = impl_from_still_stream(iface);

    *ret = &stream->source->IMFMediaSource_iface;
    IMFMediaSource_AddRef(*ret);
    return S_OK;
}

static HRESULT WINAPI still_stream_GetStreamDescriptor(IMFMediaStream *iface, IMFStreamDescriptor **ret)
{
    struct ks_still_stream *stream = impl_from_still_stream(iface);

    *ret = stream->descriptor;
    IMFStreamDescriptor_AddRef(*ret);
    return S_OK;
}

static HRESULT WINAPI still_stream_RequestSample(IMFMediaStream *iface, IUnknown *token)
{
    struct ks_still_stream *stream = impl_from_still_stream(iface);
    struct ks_source *source = stream->source;
    struct ks_token *entry;
    HRESULT hr = S_OK;

    if (!(entry = malloc(sizeof(*entry))))
        return E_OUTOFMEMORY;

    entry->token = token;
    if (token)
        IUnknown_AddRef(token);

    EnterCriticalSection(&source->cs);
    if (source->state == KS_SOURCE_SHUTDOWN)
        hr = MF_E_SHUTDOWN;
    else if (source->state == KS_SOURCE_STOPPED)
        hr = MF_E_MEDIA_SOURCE_WRONGSTATE;
    else
    {
        list_add_tail(&stream->tokens, &entry->entry);
        entry = NULL;
    }
    LeaveCriticalSection(&source->cs);

    if (entry)
    {
        if (token)
            IUnknown_Release(token);
        free(entry);
    }
    return hr;
}

static const IMFMediaStreamVtbl ks_still_stream_vtbl =
{
    still_stream_QueryInterface,
    still_stream_AddRef,
    still_stream_Release,
    still_stream_GetEvent,
    still_stream_BeginGetEvent,
    still_stream_EndGetEvent,
    still_stream_QueueEvent,
    still_stream_GetMediaSource,
    still_stream_GetStreamDescriptor,
    still_stream_RequestSample,
};

static HRESULT ks_property_range(struct ks_source *source, const GUID *set, LONG id, LONG *min, LONG *max, LONG *step, LONG *default_value, LONG *flags)
{
    KSPROPERTY_VIDEOPROCAMP_S value;
    KSPROPERTY property;
    BYTE data[sizeof(KSPROPERTY_DESCRIPTION) + sizeof(KSPROPERTY_MEMBERSHEADER) + sizeof(KSPROPERTY_STEPPING_LONG)];
    KSPROPERTY_MEMBERSHEADER *header = (void *)(data + sizeof(KSPROPERTY_DESCRIPTION));
    KSPROPERTY_STEPPING_LONG *range = (void *)(header + 1);
    DWORD returned;

    property.Set = *set;
    property.Id = id;
    property.Flags = KSPROPERTY_TYPE_BASICSUPPORT;
    if (!ks_ioctl(source->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), data, sizeof(data), &returned) || returned < sizeof(data))
        return E_PROP_ID_UNSUPPORTED;

    *min = range->Bounds.SignedMinimum;
    *max = range->Bounds.SignedMaximum;
    *step = range->SteppingDelta;

    property.Flags = KSPROPERTY_TYPE_DEFAULTVALUES;
    if (ks_ioctl(source->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), data, sizeof(data), &returned) &&
            returned >= sizeof(KSPROPERTY_DESCRIPTION) + sizeof(KSPROPERTY_MEMBERSHEADER) + sizeof(LONG))
        *default_value = *(LONG *)(header + 1);

    memset(&value, 0, sizeof(value));
    value.Property.Set = *set;
    value.Property.Id = id;
    value.Property.Flags = KSPROPERTY_TYPE_GET;
    if (!ks_ioctl(source->filter, IOCTL_KS_PROPERTY, &value, sizeof(value), &value, sizeof(value), &returned))
        return E_PROP_ID_UNSUPPORTED;

    *flags = value.Capabilities;
    return S_OK;
}

static HRESULT ks_property_get(struct ks_source *source, const GUID *set, LONG id, LONG *result, LONG *flags)
{
    KSPROPERTY_VIDEOPROCAMP_S value;
    DWORD returned;

    memset(&value, 0, sizeof(value));
    value.Property.Set = *set;
    value.Property.Id = id;
    value.Property.Flags = KSPROPERTY_TYPE_GET;
    if (!ks_ioctl(source->filter, IOCTL_KS_PROPERTY, &value, sizeof(value), &value, sizeof(value), &returned))
        return E_PROP_ID_UNSUPPORTED;

    *result = value.Value;
    *flags = value.Flags;
    return S_OK;
}

static HRESULT ks_property_set(struct ks_source *source, const GUID *set, LONG id, LONG new_value, LONG flags)
{
    KSPROPERTY_VIDEOPROCAMP_S value;
    DWORD returned;

    memset(&value, 0, sizeof(value));
    value.Property.Set = *set;
    value.Property.Id = id;
    value.Property.Flags = KSPROPERTY_TYPE_SET;
    value.Value = new_value;
    value.Flags = flags;
    if (!ks_ioctl(source->filter, IOCTL_KS_PROPERTY, &value, sizeof(value), &value, sizeof(value), &returned))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

static HRESULT WINAPI source_QueryInterface(IMFMediaSource *iface, REFIID riid, void **obj);

static inline struct ks_source *impl_from_IAMVideoProcAmp(IAMVideoProcAmp *iface)
{
    return CONTAINING_RECORD(iface, struct ks_source, IAMVideoProcAmp_iface);
}

static HRESULT WINAPI procamp_QueryInterface(IAMVideoProcAmp *iface, REFIID iid, void **out)
{
    return IMFMediaSource_QueryInterface(&impl_from_IAMVideoProcAmp(iface)->IMFMediaSource_iface, iid, out);
}

static ULONG WINAPI procamp_AddRef(IAMVideoProcAmp *iface)
{
    return IMFMediaSource_AddRef(&impl_from_IAMVideoProcAmp(iface)->IMFMediaSource_iface);
}

static ULONG WINAPI procamp_Release(IAMVideoProcAmp *iface)
{
    return IMFMediaSource_Release(&impl_from_IAMVideoProcAmp(iface)->IMFMediaSource_iface);
}

static HRESULT WINAPI procamp_GetRange(IAMVideoProcAmp *iface, LONG property, LONG *min, LONG *max, LONG *step, LONG *default_value, LONG *flags)
{
    return ks_property_range(impl_from_IAMVideoProcAmp(iface), &ks_propset_procamp, property, min, max, step, default_value, flags);
}

static HRESULT WINAPI procamp_Set(IAMVideoProcAmp *iface, LONG property, LONG value, LONG flags)
{
    return ks_property_set(impl_from_IAMVideoProcAmp(iface), &ks_propset_procamp, property, value, flags);
}

static HRESULT WINAPI procamp_Get(IAMVideoProcAmp *iface, LONG property, LONG *value, LONG *flags)
{
    return ks_property_get(impl_from_IAMVideoProcAmp(iface), &ks_propset_procamp, property, value, flags);
}

static const IAMVideoProcAmpVtbl ks_procamp_vtbl =
{
    procamp_QueryInterface,
    procamp_AddRef,
    procamp_Release,
    procamp_GetRange,
    procamp_Set,
    procamp_Get,
};

static inline struct ks_source *impl_from_IAMCameraControl(IAMCameraControl *iface)
{
    return CONTAINING_RECORD(iface, struct ks_source, IAMCameraControl_iface);
}

static HRESULT WINAPI camera_QueryInterface(IAMCameraControl *iface, REFIID iid, void **out)
{
    return IMFMediaSource_QueryInterface(&impl_from_IAMCameraControl(iface)->IMFMediaSource_iface, iid, out);
}

static ULONG WINAPI camera_AddRef(IAMCameraControl *iface)
{
    return IMFMediaSource_AddRef(&impl_from_IAMCameraControl(iface)->IMFMediaSource_iface);
}

static ULONG WINAPI camera_Release(IAMCameraControl *iface)
{
    return IMFMediaSource_Release(&impl_from_IAMCameraControl(iface)->IMFMediaSource_iface);
}

static HRESULT WINAPI camera_GetRange(IAMCameraControl *iface, LONG property, LONG *min, LONG *max, LONG *step, LONG *default_value, LONG *flags)
{
    return ks_property_range(impl_from_IAMCameraControl(iface), &ks_propset_camera, property, min, max, step, default_value, flags);
}

static HRESULT WINAPI camera_Set(IAMCameraControl *iface, LONG property, LONG value, LONG flags)
{
    return ks_property_set(impl_from_IAMCameraControl(iface), &ks_propset_camera, property, value, flags);
}

static HRESULT WINAPI camera_Get(IAMCameraControl *iface, LONG property, LONG *value, LONG *flags)
{
    return ks_property_get(impl_from_IAMCameraControl(iface), &ks_propset_camera, property, value, flags);
}

static const IAMCameraControlVtbl ks_camera_vtbl =
{
    camera_QueryInterface,
    camera_AddRef,
    camera_Release,
    camera_GetRange,
    camera_Set,
    camera_Get,
};

static inline struct ks_source *impl_from_IAMVideoControl(IAMVideoControl *iface)
{
    return CONTAINING_RECORD(iface, struct ks_source, IAMVideoControl_iface);
}

static HRESULT WINAPI video_control_QueryInterface(IAMVideoControl *iface, REFIID iid, void **out)
{
    return IMFMediaSource_QueryInterface(&impl_from_IAMVideoControl(iface)->IMFMediaSource_iface, iid, out);
}

static ULONG WINAPI video_control_AddRef(IAMVideoControl *iface)
{
    return IMFMediaSource_AddRef(&impl_from_IAMVideoControl(iface)->IMFMediaSource_iface);
}

static ULONG WINAPI video_control_Release(IAMVideoControl *iface)
{
    return IMFMediaSource_Release(&impl_from_IAMVideoControl(iface)->IMFMediaSource_iface);
}

static HRESULT WINAPI video_control_GetCaps(IAMVideoControl *iface, IPin *pin, LONG *flags)
{
    return E_INVALIDARG;
}

static HRESULT WINAPI video_control_SetMode(IAMVideoControl *iface, IPin *pin, LONG mode)
{
    return E_INVALIDARG;
}

static HRESULT WINAPI video_control_GetMode(IAMVideoControl *iface, IPin *pin, LONG *mode)
{
    return E_INVALIDARG;
}

static HRESULT WINAPI video_control_GetCurrentActualFrameRate(IAMVideoControl *iface, IPin *pin, LONGLONG *rate)
{
    return E_INVALIDARG;
}

static HRESULT WINAPI video_control_GetMaxAvailableFrameRate(IAMVideoControl *iface, IPin *pin, LONG index, SIZE dimensions, LONGLONG *rate)
{
    return E_INVALIDARG;
}

static HRESULT WINAPI video_control_GetFrameRateList(IAMVideoControl *iface, IPin *pin, LONG index, SIZE dimensions, LONG *count, LONGLONG **rates)
{
    return E_INVALIDARG;
}

static const IAMVideoControlVtbl ks_video_control_vtbl =
{
    video_control_QueryInterface,
    video_control_AddRef,
    video_control_Release,
    video_control_GetCaps,
    video_control_SetMode,
    video_control_GetMode,
    video_control_GetCurrentActualFrameRate,
    video_control_GetMaxAvailableFrameRate,
    video_control_GetFrameRateList,
};

static inline struct ks_source *impl_from_IMFGetService(IMFGetService *iface)
{
    return CONTAINING_RECORD(iface, struct ks_source, IMFGetService_iface);
}

static HRESULT WINAPI get_service_QueryInterface(IMFGetService *iface, REFIID iid, void **out)
{
    return IMFMediaSource_QueryInterface(&impl_from_IMFGetService(iface)->IMFMediaSource_iface, iid, out);
}

static ULONG WINAPI get_service_AddRef(IMFGetService *iface)
{
    return IMFMediaSource_AddRef(&impl_from_IMFGetService(iface)->IMFMediaSource_iface);
}

static ULONG WINAPI get_service_Release(IMFGetService *iface)
{
    return IMFMediaSource_Release(&impl_from_IMFGetService(iface)->IMFMediaSource_iface);
}

static HRESULT WINAPI get_service_GetService(IMFGetService *iface, REFGUID service, REFIID iid, void **out)
{
    if (IsEqualGUID(service, &GUID_NULL))
        return IMFGetService_QueryInterface(iface, iid, out);
    if (out)
        *out = NULL;
    return MF_E_UNSUPPORTED_SERVICE;
}

static const IMFGetServiceVtbl ks_get_service_vtbl =
{
    get_service_QueryInterface,
    get_service_AddRef,
    get_service_Release,
    get_service_GetService,
};

static inline struct ks_source *impl_from_ks_control(struct ks_control *iface)
{
    return CONTAINING_RECORD(iface, struct ks_source, ks_control_iface);
}

static HRESULT STDMETHODCALLTYPE ks_control_QueryInterface(struct ks_control *iface, REFIID iid, void **out)
{
    return IMFMediaSource_QueryInterface(&impl_from_ks_control(iface)->IMFMediaSource_iface, iid, out);
}

static ULONG STDMETHODCALLTYPE ks_control_AddRef(struct ks_control *iface)
{
    return IMFMediaSource_AddRef(&impl_from_ks_control(iface)->IMFMediaSource_iface);
}

static ULONG STDMETHODCALLTYPE ks_control_Release(struct ks_control *iface)
{
    return IMFMediaSource_Release(&impl_from_ks_control(iface)->IMFMediaSource_iface);
}

static HRESULT ks_control_call(struct ks_control *iface, DWORD code, void *in, ULONG in_size, void *data, ULONG data_size, ULONG *returned)
{
    struct ks_source *source = impl_from_ks_control(iface);
    DWORD local = 0;
    BOOL ret;

    ret = ks_ioctl(source->filter, code, in, in_size, data, data_size, &local);
    if (returned)
        *returned = local;
    return ret ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

static HRESULT STDMETHODCALLTYPE ks_control_KsProperty(struct ks_control *iface, KSPROPERTY *property, ULONG property_size, void *data, ULONG data_size, ULONG *returned)
{
    struct ks_source *source = impl_from_ks_control(iface);
    KSPROPERTY_VIDEOCONTROL_MODE_S *request, *value = NULL;
    ULONG stream, pin;
    HRESULT hr;

    if (!property || property_size < sizeof(KSPROPERTY_VIDEOCONTROL_MODE_S) ||
            !IsEqualGUID(&property->Set, &ks_propset_videocontrol))
        return ks_control_call(iface, IOCTL_KS_PROPERTY, property, property_size, data, data_size, returned);

    stream = ((KSPROPERTY_VIDEOCONTROL_MODE_S *)property)->StreamIndex;
    if (stream == 0)
        pin = 0;
    else if (stream == 1 && source->still)
        pin = source->still->pin_id;
    else
        return E_INVALIDARG;

    if (!(request = malloc(property_size)))
        return E_OUTOFMEMORY;
    memcpy(request, property, property_size);
    request->StreamIndex = pin;
    if ((property->Flags & KSPROPERTY_TYPE_SET) && data && data_size >= sizeof(*value))
    {
        if (!(value = malloc(data_size)))
        {
            free(request);
            return E_OUTOFMEMORY;
        }
        memcpy(value, data, data_size);
        value->StreamIndex = pin;
    }
    hr = ks_control_call(iface, IOCTL_KS_PROPERTY, request, property_size, value ? value : data, data_size, returned);
    free(value);
    free(request);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ks_control_KsMethod(struct ks_control *iface, KSMETHOD *method, ULONG method_size, void *data, ULONG data_size, ULONG *returned)
{
    return ks_control_call(iface, IOCTL_KS_METHOD, method, method_size, data, data_size, returned);
}

static HRESULT STDMETHODCALLTYPE ks_control_KsEvent(struct ks_control *iface, KSEVENT *event, ULONG event_size, void *data, ULONG data_size, ULONG *returned)
{
    return ks_control_call(iface, event ? IOCTL_KS_ENABLE_EVENT : IOCTL_KS_DISABLE_EVENT, event, event_size, data, data_size, returned);
}

static const struct ks_control_vtbl ks_control_vtbl =
{
    ks_control_QueryInterface,
    ks_control_AddRef,
    ks_control_Release,
    ks_control_KsProperty,
    ks_control_KsMethod,
    ks_control_KsEvent,
};

static HRESULT WINAPI source_QueryInterface(IMFMediaSource *iface, REFIID riid, void **obj)
{
    struct ks_source *source = impl_from_IMFMediaSource(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IMFMediaSource) ||
            IsEqualIID(riid, &IID_IMFMediaEventGenerator) ||
            IsEqualIID(riid, &IID_IUnknown))
        *obj = iface;
    else if (IsEqualIID(riid, &IID_IAMVideoProcAmp))
        *obj = &source->IAMVideoProcAmp_iface;
    else if (IsEqualIID(riid, &IID_IAMCameraControl))
        *obj = &source->IAMCameraControl_iface;
    else if (IsEqualIID(riid, &IID_IAMVideoControl))
        *obj = &source->IAMVideoControl_iface;
    else if (IsEqualIID(riid, &IID_IMFGetService))
        *obj = &source->IMFGetService_iface;
    else if (IsEqualIID(riid, &ks_iid_control))
        *obj = &source->ks_control_iface;
    else
    {
        *obj = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*obj);
    return S_OK;
}

static ULONG WINAPI source_AddRef(IMFMediaSource *iface)
{
    return InterlockedIncrement(&impl_from_IMFMediaSource(iface)->refcount);
}

static ULONG WINAPI source_Release(IMFMediaSource *iface)
{
    struct ks_source *source = impl_from_IMFMediaSource(iface);
    ULONG refcount = InterlockedDecrement(&source->refcount);

    if (!refcount)
    {
        ks_stop_stream(source);
        ks_flush_tokens(source);
        if (source->still)
        {
            if (source->still->queue)
                IMFMediaEventQueue_Release(source->still->queue);
            if (source->still->descriptor)
                IMFStreamDescriptor_Release(source->still->descriptor);
            free(source->still->formats);
            free(source->still);
        }
        if (source->event_queue)
            IMFMediaEventQueue_Release(source->event_queue);
        if (source->stream_queue)
            IMFMediaEventQueue_Release(source->stream_queue);
        if (source->descriptor)
            IMFPresentationDescriptor_Release(source->descriptor);
        if (source->stream_descriptor)
            IMFStreamDescriptor_Release(source->stream_descriptor);
        if (source->notify)
            CM_Unregister_Notification(source->notify);
        if (source->stop_event)
            CloseHandle(source->stop_event);
        if (source->lost_event)
            CloseHandle(source->lost_event);
        free(source->path);
        if (source->filter && source->filter != INVALID_HANDLE_VALUE)
            CloseHandle(source->filter);
        DeleteCriticalSection(&source->cs);
        free(source->formats);
        free(source);
    }

    return refcount;
}

static HRESULT WINAPI source_GetEvent(IMFMediaSource *iface, DWORD flags, IMFMediaEvent **event)
{
    return IMFMediaEventQueue_GetEvent(impl_from_IMFMediaSource(iface)->event_queue, flags, event);
}

static HRESULT WINAPI source_BeginGetEvent(IMFMediaSource *iface, IMFAsyncCallback *callback, IUnknown *state)
{
    return IMFMediaEventQueue_BeginGetEvent(impl_from_IMFMediaSource(iface)->event_queue, callback, state);
}

static HRESULT WINAPI source_EndGetEvent(IMFMediaSource *iface, IMFAsyncResult *result, IMFMediaEvent **event)
{
    return IMFMediaEventQueue_EndGetEvent(impl_from_IMFMediaSource(iface)->event_queue, result, event);
}

static HRESULT WINAPI source_QueueEvent(IMFMediaSource *iface, MediaEventType type, REFGUID ext, HRESULT hr, const PROPVARIANT *value)
{
    return IMFMediaEventQueue_QueueEventParamVar(impl_from_IMFMediaSource(iface)->event_queue, type, ext, hr, value);
}

static HRESULT WINAPI source_GetCharacteristics(IMFMediaSource *iface, DWORD *flags)
{
    *flags = MFMEDIASOURCE_IS_LIVE | MFMEDIASOURCE_CAN_PAUSE;
    return S_OK;
}

static HRESULT WINAPI source_CreatePresentationDescriptor(IMFMediaSource *iface, IMFPresentationDescriptor **descriptor)
{
    struct ks_source *source = impl_from_IMFMediaSource(iface);

    if (source->state == KS_SOURCE_SHUTDOWN)
        return MF_E_SHUTDOWN;

    return IMFPresentationDescriptor_Clone(source->descriptor, descriptor);
}

static HRESULT ks_select_still_format(struct ks_source *source, IMFPresentationDescriptor *descriptor)
{
    struct ks_still_stream *still = source->still;
    IMFStreamDescriptor *stream = NULL;
    IMFMediaTypeHandler *handler = NULL;
    IMFMediaType *type = NULL;
    BOOL selected = FALSE;
    HRESULT hr;

    still->active = FALSE;
    if (FAILED(hr = IMFPresentationDescriptor_GetStreamDescriptorByIndex(descriptor, 1, &selected, &stream)))
        return S_OK;

    if (selected &&
            SUCCEEDED(hr = IMFStreamDescriptor_GetMediaTypeHandler(stream, &handler)) &&
            SUCCEEDED(hr = IMFMediaTypeHandler_GetCurrentMediaType(handler, &type)))
    {
        still->current = ks_find_format(still->formats, still->format_count, type);
        hr = still->current ? S_OK : MF_E_INVALIDMEDIATYPE;
        still->active = SUCCEEDED(hr);
    }

    if (type)
        IMFMediaType_Release(type);
    if (handler)
        IMFMediaTypeHandler_Release(handler);
    IMFStreamDescriptor_Release(stream);
    return hr;
}

static HRESULT ks_select_format(struct ks_source *source, IMFPresentationDescriptor *descriptor)
{
    IMFStreamDescriptor *stream = NULL;
    IMFMediaTypeHandler *handler = NULL;
    IMFMediaType *type = NULL;
    BOOL selected = FALSE;
    HRESULT hr;

    if (FAILED(hr = IMFPresentationDescriptor_GetStreamDescriptorByIndex(descriptor, 0, &selected, &stream)))
        return hr;

    if (selected &&
            SUCCEEDED(hr = IMFStreamDescriptor_GetMediaTypeHandler(stream, &handler)) &&
            SUCCEEDED(hr = IMFMediaTypeHandler_GetCurrentMediaType(handler, &type)))
    {
        source->current = ks_find_format(source->formats, source->format_count, type);
        hr = source->current ? S_OK : MF_E_INVALIDMEDIATYPE;
    }
    else if (!selected)
    {
        hr = MF_E_INVALIDREQUEST;
    }

    if (type)
        IMFMediaType_Release(type);
    if (handler)
        IMFMediaTypeHandler_Release(handler);
    IMFStreamDescriptor_Release(stream);
    return hr;
}

static HRESULT WINAPI source_Start(IMFMediaSource *iface, IMFPresentationDescriptor *descriptor, const GUID *time_format, const PROPVARIANT *start)
{
    struct ks_source *source = impl_from_IMFMediaSource(iface);
    PROPVARIANT time;
    HRESULT hr = S_OK;

    TRACE("%p, %p, %s, %p.\n", iface, descriptor, debugstr_guid(time_format), start);

    if (time_format && !IsEqualGUID(time_format, &GUID_NULL))
        return MF_E_UNSUPPORTED_TIME_FORMAT;

    EnterCriticalSection(&source->cs);

    if (source->state == KS_SOURCE_SHUTDOWN)
    {
        LeaveCriticalSection(&source->cs);
        return MF_E_SHUTDOWN;
    }

    if (source->state == KS_SOURCE_STOPPED)
    {
        if (SUCCEEDED(hr = ks_select_format(source, descriptor)) &&
                SUCCEEDED(hr = ks_open_pin(source, 0, source->current, &source->pin)))
        {
            source->pump.source = source;
            source->pump.format = source->current;
            source->pump.pin = source->pin;
            source->pump.tokens = &source->tokens;
            source->pump.queue = source->stream_queue;
            if (!(source->thread = CreateThread(NULL, 0, ks_stream_thread, &source->pump, 0, NULL)))
                hr = HRESULT_FROM_WIN32(GetLastError());
        }
        if (SUCCEEDED(hr) && source->still && SUCCEEDED(hr = ks_select_still_format(source, descriptor)) && source->still->active &&
                SUCCEEDED(hr = ks_open_pin(source, source->still->pin_id, source->still->current, &source->still->pin)))
        {
            struct ks_still_stream *still = source->still;

            still->pump.source = source;
            still->pump.format = still->current;
            still->pump.pin = still->pin;
            still->pump.tokens = &still->tokens;
            still->pump.queue = still->queue;
            if (!(still->thread = CreateThread(NULL, 0, ks_stream_thread, &still->pump, 0, NULL)))
                hr = HRESULT_FROM_WIN32(GetLastError());
        }
        if (FAILED(hr))
        {
            LeaveCriticalSection(&source->cs);
            ks_stop_stream(source);
            return hr;
        }
    }

    source->state = KS_SOURCE_RUNNING;

    IMFMediaEventQueue_QueueEventParamUnk(source->event_queue,
                                          source->stream_announced ? MEUpdatedStream : MENewStream,
                                          &GUID_NULL,
                                          S_OK,
                                          (IUnknown *)&source->IMFMediaStream_iface);
    source->stream_announced = TRUE;
    if (source->still && source->still->active)
    {
        IMFMediaEventQueue_QueueEventParamUnk(source->event_queue,
                                              source->still->announced ? MEUpdatedStream : MENewStream,
                                              &GUID_NULL,
                                              S_OK,
                                              (IUnknown *)&source->still->IMFMediaStream_iface);
        source->still->announced = TRUE;
    }

    PropVariantInit(&time);
    time.vt = VT_I8;
    time.hVal.QuadPart = MFGetSystemTime();
    IMFMediaEventQueue_QueueEventParamVar(source->event_queue, MESourceStarted, &GUID_NULL, S_OK, &time);
    IMFMediaEventQueue_QueueEventParamVar(source->stream_queue, MEStreamStarted, &GUID_NULL, S_OK, &time);
    IMFMediaEventQueue_QueueEventParamVar(source->stream_queue, MEStreamTick, &GUID_NULL, S_OK, &time);
    if (source->still && source->still->active)
    {
        IMFMediaEventQueue_QueueEventParamVar(source->still->queue, MEStreamStarted, &GUID_NULL, S_OK, &time);
        IMFMediaEventQueue_QueueEventParamVar(source->still->queue, MEStreamTick, &GUID_NULL, S_OK, &time);
    }

    LeaveCriticalSection(&source->cs);
    return S_OK;
}

static HRESULT WINAPI source_Stop(IMFMediaSource *iface)
{
    struct ks_source *source = impl_from_IMFMediaSource(iface);

    TRACE("%p.\n", iface);

    EnterCriticalSection(&source->cs);
    if (source->state == KS_SOURCE_SHUTDOWN)
    {
        LeaveCriticalSection(&source->cs);
        return MF_E_SHUTDOWN;
    }
    source->state = KS_SOURCE_STOPPED;
    ks_flush_tokens(source);
    LeaveCriticalSection(&source->cs);

    ks_stop_stream(source);

    if (source->still && source->still->active)
        IMFMediaEventQueue_QueueEventParamVar(source->still->queue, MEStreamStopped, &GUID_NULL, S_OK, NULL);
    IMFMediaEventQueue_QueueEventParamVar(source->stream_queue, MEStreamStopped, &GUID_NULL, S_OK, NULL);
    IMFMediaEventQueue_QueueEventParamVar(source->event_queue, MESourceStopped, &GUID_NULL, S_OK, NULL);
    return S_OK;
}

static HRESULT WINAPI source_Pause(IMFMediaSource *iface)
{
    struct ks_source *source = impl_from_IMFMediaSource(iface);
    HRESULT hr = S_OK;

    TRACE("%p.\n", iface);

    EnterCriticalSection(&source->cs);
    if (source->state == KS_SOURCE_SHUTDOWN)
    {
        hr = MF_E_SHUTDOWN;
    }
    else if (source->state != KS_SOURCE_RUNNING)
    {
        hr = MF_E_INVALID_STATE_TRANSITION;
    }
    else
    {
        source->state = KS_SOURCE_PAUSED;
        if (source->still && source->still->active)
            IMFMediaEventQueue_QueueEventParamVar(source->still->queue, MEStreamPaused, &GUID_NULL, S_OK, NULL);
        IMFMediaEventQueue_QueueEventParamVar(source->stream_queue, MEStreamPaused, &GUID_NULL, S_OK, NULL);
        IMFMediaEventQueue_QueueEventParamVar(source->event_queue, MESourcePaused, &GUID_NULL, S_OK, NULL);
    }
    LeaveCriticalSection(&source->cs);
    return hr;
}

static HRESULT WINAPI source_Shutdown(IMFMediaSource *iface)
{
    struct ks_source *source = impl_from_IMFMediaSource(iface);

    TRACE("%p.\n", iface);

    EnterCriticalSection(&source->cs);
    if (source->state == KS_SOURCE_SHUTDOWN)
    {
        LeaveCriticalSection(&source->cs);
        return MF_E_SHUTDOWN;
    }
    source->state = KS_SOURCE_SHUTDOWN;
    ks_flush_tokens(source);
    LeaveCriticalSection(&source->cs);

    ks_stop_stream(source);
    if (source->still && source->still->queue)
        IMFMediaEventQueue_Shutdown(source->still->queue);
    IMFMediaEventQueue_Shutdown(source->stream_queue);
    IMFMediaEventQueue_Shutdown(source->event_queue);
    return S_OK;
}

static const IMFMediaSourceVtbl ks_source_vtbl =
{
    source_QueryInterface,
    source_AddRef,
    source_Release,
    source_GetEvent,
    source_BeginGetEvent,
    source_EndGetEvent,
    source_QueueEvent,
    source_GetCharacteristics,
    source_CreatePresentationDescriptor,
    source_Start,
    source_Stop,
    source_Pause,
    source_Shutdown,
};

static HRESULT ks_build_still_descriptor(struct ks_source *source)
{
    struct ks_still_stream *still = source->still;
    IMFMediaTypeHandler *handler = NULL;
    IMFMediaType **types;
    DWORD i, count = 0;
    HRESULT hr;

    if (!(types = calloc(still->format_count, sizeof(*types))))
        return E_OUTOFMEMORY;

    for (i = 0; i < still->format_count; ++i)
    {
        if (FAILED(hr = ks_create_media_type(&still->formats[i], &types[i])))
            goto done;
        count++;
    }

    if (FAILED(hr = MFCreateEventQueue(&still->queue)))
        goto done;
    if (FAILED(hr = MFCreateStreamDescriptor(1, count, types, &still->descriptor)))
        goto done;
    IMFStreamDescriptor_SetUINT32(still->descriptor, &ks_stream_frame_source_types, 1);
    IMFStreamDescriptor_SetGUID(still->descriptor, &ks_stream_category, &ks_pin_still);
    if (FAILED(hr = IMFStreamDescriptor_GetMediaTypeHandler(still->descriptor, &handler)))
        goto done;
    hr = IMFMediaTypeHandler_SetCurrentMediaType(handler, types[0]);
    still->IMFMediaStream_iface.lpVtbl = &ks_still_stream_vtbl;

done:
    if (handler)
        IMFMediaTypeHandler_Release(handler);
    for (i = 0; i < count; ++i)
        IMFMediaType_Release(types[i]);
    free(types);
    return hr;
}

static HRESULT ks_source_build_descriptors(struct ks_source *source)
{
    IMFStreamDescriptor *streams[2];
    IMFMediaTypeHandler *handler = NULL;
    IMFMediaType **types;
    DWORD i, count = 0;
    HRESULT hr;

    if (!(types = calloc(source->format_count, sizeof(*types))))
        return E_OUTOFMEMORY;

    for (i = 0; i < source->format_count; ++i)
    {
        if (FAILED(hr = ks_create_media_type(&source->formats[i], &types[i])))
            goto done;
        count++;
    }

    if (FAILED(hr = MFCreateStreamDescriptor(0, count, types, &source->stream_descriptor)))
        goto done;
    IMFStreamDescriptor_SetUINT32(source->stream_descriptor, &ks_stream_frame_source_types, 1);
    IMFStreamDescriptor_SetGUID(source->stream_descriptor, &ks_stream_category, &ks_pin_capture);
    if (FAILED(hr = IMFStreamDescriptor_GetMediaTypeHandler(source->stream_descriptor, &handler)))
        goto done;
    if (FAILED(hr = IMFMediaTypeHandler_SetCurrentMediaType(handler, types[0])))
        goto done;
    if (source->still && FAILED(hr = ks_build_still_descriptor(source)))
        goto done;

    streams[0] = source->stream_descriptor;
    streams[1] = source->still ? source->still->descriptor : NULL;
    if (FAILED(hr = MFCreatePresentationDescriptor(source->still ? 2 : 1, streams, &source->descriptor)))
        goto done;
    hr = IMFPresentationDescriptor_SelectStream(source->descriptor, 0);

done:
    if (handler)
        IMFMediaTypeHandler_Release(handler);
    for (i = 0; i < count; ++i)
        IMFMediaType_Release(types[i]);
    free(types);
    return hr;
}

static DWORD CALLBACK ks_device_notify(HCMNOTIFICATION notify, void *context, CM_NOTIFY_ACTION action,
        CM_NOTIFY_EVENT_DATA *data, DWORD size)
{
    struct ks_source *source = context;

    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL &&
        !_wcsicmp(data->u.DeviceInterface.SymbolicLink + 4, source->path + 4))
        SetEvent(source->lost_event);
    return ERROR_SUCCESS;
}

static HRESULT ks_source_create(const WCHAR *path, IMFMediaSource **ret)
{
    CM_NOTIFY_FILTER filter = {sizeof(filter)};
    struct ks_source *source;
    HRESULT hr;

    if (!(source = calloc(1, sizeof(*source))))
        return E_OUTOFMEMORY;

    source->IMFMediaSource_iface.lpVtbl = &ks_source_vtbl;
    source->IMFMediaStream_iface.lpVtbl = &ks_stream_vtbl;
    source->IAMVideoProcAmp_iface.lpVtbl = &ks_procamp_vtbl;
    source->IAMCameraControl_iface.lpVtbl = &ks_camera_vtbl;
    source->IAMVideoControl_iface.lpVtbl = &ks_video_control_vtbl;
    source->IMFGetService_iface.lpVtbl = &ks_get_service_vtbl;
    source->ks_control_iface.lpVtbl = &ks_control_vtbl;
    source->refcount = 1;
    InitializeCriticalSection(&source->cs);
    list_init(&source->tokens);

    source->filter = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (source->filter == INVALID_HANDLE_VALUE)
    {
        hr = MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED;
        goto failed;
    }

    if (!(source->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL)) ||
        !(source->lost_event = CreateEventW(NULL, TRUE, FALSE, NULL)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto failed;
    }

    if (!(source->path = _wcsdup(path)))
    {
        hr = E_OUTOFMEMORY;
        goto failed;
    }

    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = ks_category_camera;
    if (CM_Register_Notification(&filter, source, ks_device_notify, &source->notify) != CR_SUCCESS)
        source->notify = NULL;

    if (FAILED(hr = ks_load_formats(source)))
        goto failed;
    if (FAILED(hr = ks_load_still_formats(source)))
        goto failed;
    if (FAILED(hr = MFCreateEventQueue(&source->event_queue)))
        goto failed;
    if (FAILED(hr = MFCreateEventQueue(&source->stream_queue)))
        goto failed;
    if (FAILED(hr = ks_source_build_descriptors(source)))
        goto failed;

    *ret = &source->IMFMediaSource_iface;
    return S_OK;

failed:
    IMFMediaSource_Release(&source->IMFMediaSource_iface);
    return hr;
}

static HRESULT ks_video_create_object(IMFAttributes *attributes, void *context, IUnknown **object)
{
    WCHAR *link = NULL;
    UINT32 length;
    HRESULT hr;

    if (FAILED(hr = IMFAttributes_GetAllocatedString(attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &length)))
        return hr;

    hr = ks_source_create(link, (IMFMediaSource **)object);
    CoTaskMemFree(link);
    return hr;
}

static void ks_video_shutdown_object(void *context, IUnknown *object)
{
    IMFMediaSource *source;

    if (SUCCEEDED(IUnknown_QueryInterface(object, &IID_IMFMediaSource, (void **)&source)))
    {
        IMFMediaSource_Shutdown(source);
        IMFMediaSource_Release(source);
    }
}

static void ks_video_free_private(void *context)
{
}

static const struct activate_funcs ks_video_activate_funcs =
{
    ks_video_create_object,
    ks_video_shutdown_object,
    ks_video_free_private,
};

HRESULT create_ks_video_source(IMFAttributes *attributes, IMFMediaSource **source)
{
    return ks_video_create_object(attributes, NULL, (IUnknown **)source);
}

static void ks_set_activate_media_type(IMFActivate *activate, const WCHAR *path)
{
    MFT_REGISTER_TYPE_INFO info;
    struct ks_source source;
    KSP_PIN property;
    KSMULTIPLE_ITEM *multiple;
    DWORD size = 0;

    memset(&source, 0, sizeof(source));
    source.filter = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (source.filter == INVALID_HANDLE_VALUE)
        return;

    memset(&property, 0, sizeof(property));
    property.Property.Set = ks_propset_pin;
    property.Property.Id = KSPROPERTY_PIN_DATARANGES;
    property.Property.Flags = KSPROPERTY_TYPE_GET;
    ks_ioctl(source.filter, IOCTL_KS_PROPERTY, &property, sizeof(property), NULL, 0, &size);
    if (size && (multiple = malloc(size)))
    {
        if (ks_ioctl(source.filter, IOCTL_KS_PROPERTY, &property, sizeof(property), multiple, size, &size) && multiple->Count)
        {
            const KSDATARANGE *range = (const KSDATARANGE *)(multiple + 1);

            info.guidMajorType = MFMediaType_Video;
            info.guidSubtype = range->SubFormat;
            IMFActivate_SetBlob(activate, &MF_DEVSOURCE_ATTRIBUTE_MEDIA_TYPE, (const UINT8 *)&info, sizeof(info));
        }
        free(multiple);
    }
    CloseHandle(source.filter);
}

static HRESULT ks_create_activate(HDEVINFO set, SP_DEVICE_INTERFACE_DATA *data, const WCHAR *path, SP_DEVINFO_DATA *info, IMFActivate **ret)
{
    WCHAR name[128] = L"";
    DWORD size = sizeof(name);
    IMFActivate *activate;
    HKEY key;
    HRESULT hr;

    key = SetupDiOpenDeviceInterfaceRegKey(set, data, 0, KEY_READ);
    if (key != INVALID_HANDLE_VALUE)
    {
        RegQueryValueExW(key, L"FriendlyName", NULL, NULL, (BYTE *)name, &size);
        RegCloseKey(key);
    }
    if (!name[0] && !SetupDiGetDeviceRegistryPropertyW(set, info, SPDRP_FRIENDLYNAME, NULL, (BYTE *)name, sizeof(name), NULL))
        SetupDiGetDeviceRegistryPropertyW(set, info, SPDRP_DEVICEDESC, NULL, (BYTE *)name, sizeof(name), NULL);

    if (FAILED(hr = create_activation_object(NULL, &ks_video_activate_funcs, &activate)))
        return hr;

    IMFActivate_SetGUID(activate, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate_SetGUID(activate, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY, &ks_category_camera);
    IMFActivate_SetString(activate, &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, name);
    IMFActivate_SetString(activate, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, path);
    IMFActivate_SetUINT32(activate, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_HW_SOURCE, 4);
    ks_set_activate_media_type(activate, path);

    *ret = activate;
    return S_OK;
}

HRESULT enum_ks_video_sources(IMFActivate ***ret_sources, UINT32 *ret_count)
{
    SP_DEVICE_INTERFACE_DATA data = {sizeof(data)};
    BYTE buffer[1024];
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = (void *)buffer;
    SP_DEVINFO_DATA info = {sizeof(info)};
    IMFActivate **sources = NULL;
    HDEVINFO set;
    DWORD index, count = 0, capacity = 0;
    HRESULT hr = S_OK;

    *ret_sources = NULL;
    *ret_count = 0;

    set = SetupDiGetClassDevsW(&ks_category_camera, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return S_OK;

    while (SetupDiEnumDeviceInterfaces(set, NULL, &ks_category_camera, capacity, &data))
        capacity++;

    if (capacity && !(sources = CoTaskMemAlloc(capacity * sizeof(*sources))))
    {
        SetupDiDestroyDeviceInfoList(set);
        return E_OUTOFMEMORY;
    }

    for (index = 0; index < capacity; ++index)
    {
        if (!SetupDiEnumDeviceInterfaces(set, NULL, &ks_category_camera, index, &data))
            continue;

        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &data, detail, sizeof(buffer), NULL, &info))
            continue;

        if (SUCCEEDED(hr = ks_create_activate(set, &data, detail->DevicePath, &info, &sources[count])))
            count++;
    }

    SetupDiDestroyDeviceInfoList(set);

    if (!count)
    {
        CoTaskMemFree(sources);
        sources = NULL;
    }

    *ret_sources = sources;
    *ret_count = count;
    return S_OK;
}
