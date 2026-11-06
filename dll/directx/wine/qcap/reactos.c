/*
 * PROJECT:     LiberNT DirectShow capture
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Kernel streaming backend for Wine qcap capture calls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "qcap_private.h"

#include <winioctl.h>
#include <ks.h>
#include <ksmedia.h>

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

#define KS_FRAME_TIMEOUT_MS 5000

static const GUID ks_propset_pin = { STATIC_KSPROPSETID_Pin };
static const GUID ks_propset_connection = { STATIC_KSPROPSETID_Connection };
static const GUID ks_propset_procamp = { 0xc6e13360, 0x30ac, 0x11d0, { 0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56 } };
static const GUID ks_propset_camera = { 0xc6e13370, 0x30ac, 0x11d0, { 0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56 } };
static const GUID ks_interface_standard = { STATIC_KSINTERFACESETID_Standard };
static const GUID ks_medium_standard = { STATIC_KSMEDIUMSETID_Standard };
static const GUID ks_specifier_videoinfo2 = { 0xf72a76a0, 0xeb0a, 0x11d0, { 0xac, 0xe4, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba } };
static const GUID ks_propset_videocontrol = { 0x6a2e0670, 0x28e4, 0x11d0, { 0xa1, 0x8c, 0x00, 0xa0, 0xc9, 0x11, 0x89, 0x56 } };
static const GUID ks_pin_still = { 0xfb6c428a, 0x0353, 0x11d1, { 0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba } };
static const GUID ks_specifier_videoinfo = { 0x05589f80, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };

struct ks_caps
{
    AM_MEDIA_TYPE mt;
    union
    {
        VIDEOINFOHEADER format;
        KS_VIDEOINFOHEADER2 format2;
    };
    VIDEO_STREAM_CONFIG_CAPS caps;
};

const BITMAPINFOHEADER *qcap_backend_format_bmi(const AM_MEDIA_TYPE *mt)
{
    if (!mt->pbFormat)
        return NULL;
    if (IsEqualGUID(&mt->formattype, &FORMAT_VideoInfo) && mt->cbFormat >= sizeof(VIDEOINFOHEADER))
        return &((const VIDEOINFOHEADER *)mt->pbFormat)->bmiHeader;
    if (IsEqualGUID(&mt->formattype, &ks_specifier_videoinfo2) && mt->cbFormat >= sizeof(KS_VIDEOINFOHEADER2))
        return (const BITMAPINFOHEADER *)&((const KS_VIDEOINFOHEADER2 *)mt->pbFormat)->bmiHeader;
    return NULL;
}

static const BITMAPINFOHEADER *ks_caps_bmi(const struct ks_caps *caps)
{
    return IsEqualGUID(&caps->mt.formattype, &ks_specifier_videoinfo2) ?
            (const BITMAPINFOHEADER *)&caps->format2.bmiHeader : &caps->format.bmiHeader;
}

struct ks_device
{
    HANDLE filter;
    HANDLE pin;
    HANDLE event;
    struct ks_caps *caps;
    int count;
    int current;
    REFERENCE_TIME interval;
    struct ks_caps *still_caps;
    int still_count;
    ULONG still_id;
    HANDLE still;
};

static struct ks_device *get_device(video_capture_device_t device)
{
    return (struct ks_device *)(UINT_PTR)device;
}

static BOOL ks_ioctl(HANDLE handle, DWORD code, void *in, DWORD in_size, void *out, DWORD out_size, DWORD *returned, DWORD timeout)
{
    OVERLAPPED overlapped = {0};
    DWORD local = 0;
    BOOL ret;

    if (!(overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)))
        return FALSE;

    ret = DeviceIoControl(handle, code, in, in_size, out, out_size, &local, &overlapped);
    if (!ret && GetLastError() == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject(overlapped.hEvent, timeout) != WAIT_OBJECT_0)
        {
            CancelIo(handle);
            GetOverlappedResult(handle, &overlapped, &local, TRUE);
            CloseHandle(overlapped.hEvent);
            SetLastError(WAIT_TIMEOUT);
            return FALSE;
        }
        ret = GetOverlappedResult(handle, &overlapped, &local, FALSE);
    }

    CloseHandle(overlapped.hEvent);
    if (returned)
        *returned = local;
    return ret;
}

static void ks_load_still_caps(struct ks_device *device)
{
    KSP_PIN property = {{{0}}};
    KSPROPERTY count_property;
    KSMULTIPLE_ITEM *multiple;
    ULONG pins = 0, pin;
    DWORD size = 0;
    BYTE *item;
    ULONG i;

    count_property.Set = ks_propset_pin;
    count_property.Id = KSPROPERTY_PIN_CTYPES;
    count_property.Flags = KSPROPERTY_TYPE_GET;
    if (!ks_ioctl(device->filter, IOCTL_KS_PROPERTY, &count_property, sizeof(count_property), &pins, sizeof(pins), &size, INFINITE))
        return;

    property.Property.Set = ks_propset_pin;
    property.Property.Flags = KSPROPERTY_TYPE_GET;
    for (pin = 0; pin < pins; ++pin)
    {
        GUID category = {0};

        property.Property.Id = KSPROPERTY_PIN_CATEGORY;
        property.PinId = pin;
        if (!ks_ioctl(device->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), &category, sizeof(category), &size, INFINITE))
            continue;
        if (IsEqualGUID(&category, &ks_pin_still))
            break;
    }
    if (pin >= pins)
        return;

    property.Property.Id = KSPROPERTY_PIN_DATARANGES;
    size = 0;
    ks_ioctl(device->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), NULL, 0, &size, INFINITE);
    if (!size || !(multiple = malloc(size)))
        return;

    if (ks_ioctl(device->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), multiple, size, &size, INFINITE) &&
            (device->still_caps = calloc(multiple->Count, sizeof(*device->still_caps))))
    {
        item = (BYTE *)(multiple + 1);
        for (i = 0; i < multiple->Count; ++i)
        {
            KSDATARANGE *range = (KSDATARANGE *)item;

            if (IsEqualGUID(&range->Specifier, &ks_specifier_videoinfo) && range->FormatSize >= sizeof(KS_DATARANGE_VIDEO))
            {
                KS_DATARANGE_VIDEO *video = (KS_DATARANGE_VIDEO *)range;
                struct ks_caps *caps = &device->still_caps[device->still_count++];

                caps->mt.majortype = range->MajorFormat;
                caps->mt.subtype = range->SubFormat;
                caps->mt.bFixedSizeSamples = TRUE;
                caps->mt.bTemporalCompression = video->bTemporalCompression;
                caps->mt.lSampleSize = range->SampleSize;
                caps->mt.formattype = FORMAT_VideoInfo;
                caps->mt.cbFormat = sizeof(VIDEOINFOHEADER);
                memcpy(&caps->format, &video->VideoInfoHeader, sizeof(caps->format));
                memcpy(&caps->caps, &video->ConfigCaps, sizeof(caps->caps));
            }
            item += (range->FormatSize + 7) & ~7u;
        }
        device->still_id = pin;
    }
    free(multiple);
}

int qcap_backend_still_count(video_capture_device_t device)
{
    return get_device(device)->still_count;
}

HRESULT qcap_backend_still_media_type(video_capture_device_t device, unsigned int index, AM_MEDIA_TYPE *mt, VIDEO_STREAM_CONFIG_CAPS *caps)
{
    struct ks_device *ks = get_device(device);
    const struct ks_caps *still;

    if (index >= (unsigned int)ks->still_count)
        return VFW_S_NO_MORE_ITEMS;

    still = &ks->still_caps[index];
    *mt = still->mt;
    mt->pUnk = NULL;
    if (!(mt->pbFormat = CoTaskMemAlloc(sizeof(still->format))))
        return E_OUTOFMEMORY;
    memcpy(mt->pbFormat, &still->format, sizeof(still->format));
    if (caps)
        *caps = still->caps;
    return S_OK;
}

int qcap_backend_still_find(video_capture_device_t device, const AM_MEDIA_TYPE *mt)
{
    struct ks_device *ks = get_device(device);
    const BITMAPINFOHEADER *bmi = qcap_backend_format_bmi(mt);
    int index;

    if (!bmi || !IsEqualGUID(&mt->formattype, &FORMAT_VideoInfo))
        return -1;

    for (index = 0; index < ks->still_count; ++index)
    {
        const struct ks_caps *still = &ks->still_caps[index];

        if (IsEqualGUID(&still->mt.subtype, &mt->subtype) &&
                still->format.bmiHeader.biWidth == bmi->biWidth &&
                still->format.bmiHeader.biHeight == bmi->biHeight)
            return index;
    }
    return -1;
}

HRESULT qcap_backend_still_start(video_capture_device_t device, int index)
{
    struct ks_device *ks = get_device(device);
    const struct ks_caps *still = &ks->still_caps[index];
    struct
    {
        KSPIN_CONNECT connect;
        KS_DATAFORMAT_VIDEOINFOHEADER format;
    } request;
    static const KSSTATE states[] = { KSSTATE_ACQUIRE, KSSTATE_PAUSE, KSSTATE_RUN };
    KSPROPERTY property;
    DWORD error, returned;
    unsigned int i;

    if (ks->still)
        return S_OK;

    memset(&request, 0, sizeof(request));
    request.connect.Interface.Set = ks_interface_standard;
    request.connect.Interface.Id = KSINTERFACE_STANDARD_STREAMING;
    request.connect.Medium.Set = ks_medium_standard;
    request.connect.Medium.Id = KSMEDIUM_TYPE_ANYINSTANCE;
    request.connect.PinId = ks->still_id;
    request.connect.Priority.PriorityClass = KSPRIORITY_NORMAL;
    request.connect.Priority.PrioritySubClass = KSPRIORITY_NORMAL;
    request.format.DataFormat.FormatSize = sizeof(request.format);
    request.format.DataFormat.SampleSize = still->mt.lSampleSize;
    request.format.DataFormat.MajorFormat = still->mt.majortype;
    request.format.DataFormat.SubFormat = still->mt.subtype;
    request.format.DataFormat.Specifier = still->mt.formattype;
    memcpy(&request.format.VideoInfoHeader, &still->format, sizeof(still->format));

    if ((error = KsCreatePin(ks->filter, &request.connect, GENERIC_READ, &ks->still)))
    {
        ks->still = NULL;
        return HRESULT_FROM_WIN32(error);
    }

    property.Set = ks_propset_connection;
    property.Id = KSPROPERTY_CONNECTION_STATE;
    property.Flags = KSPROPERTY_TYPE_SET;
    for (i = 0; i < ARRAY_SIZE(states); ++i)
    {
        KSSTATE state = states[i];

        if (!ks_ioctl(ks->still, IOCTL_KS_PROPERTY, &property, sizeof(property), &state, sizeof(state), &returned, INFINITE))
        {
            error = GetLastError();
            CloseHandle(ks->still);
            ks->still = NULL;
            return HRESULT_FROM_WIN32(error);
        }
    }
    return S_OK;
}

void qcap_backend_still_stop(video_capture_device_t device)
{
    struct ks_device *ks = get_device(device);
    static const KSSTATE states[] = { KSSTATE_PAUSE, KSSTATE_ACQUIRE, KSSTATE_STOP };
    KSPROPERTY property;
    DWORD returned;
    unsigned int i;

    if (!ks->still)
        return;

    property.Set = ks_propset_connection;
    property.Id = KSPROPERTY_CONNECTION_STATE;
    property.Flags = KSPROPERTY_TYPE_SET;
    for (i = 0; i < ARRAY_SIZE(states); ++i)
    {
        KSSTATE state = states[i];

        ks_ioctl(ks->still, IOCTL_KS_PROPERTY, &property, sizeof(property), &state, sizeof(state), &returned, INFINITE);
    }
    CloseHandle(ks->still);
    ks->still = NULL;
}

HRESULT qcap_backend_still_read(video_capture_device_t device, BYTE *data, DWORD size, DWORD *used, HANDLE stop)
{
    struct ks_device *ks = get_device(device);
    struct
    {
        KSSTREAM_HEADER header;
        KS_FRAME_INFO info;
    } request;
    OVERLAPPED overlapped = {0};
    HANDLE handles[2];
    DWORD returned = 0;
    BOOL ret;

    memset(&request, 0, sizeof(request));
    request.header.Size = sizeof(request);
    request.header.FrameExtent = size;
    request.header.Data = data;
    request.info.ExtendedHeaderSize = sizeof(request.info);

    if (!(overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)))
        return HRESULT_FROM_WIN32(GetLastError());

    ret = DeviceIoControl(ks->still, IOCTL_KS_READ_STREAM, NULL, 0, &request, sizeof(request), &returned, &overlapped);
    if (!ret && GetLastError() == ERROR_IO_PENDING)
    {
        handles[0] = stop;
        handles[1] = overlapped.hEvent;
        if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1)
        {
            CancelIo(ks->still);
            GetOverlappedResult(ks->still, &overlapped, &returned, TRUE);
            CloseHandle(overlapped.hEvent);
            return S_FALSE;
        }
        ret = GetOverlappedResult(ks->still, &overlapped, &returned, FALSE);
    }
    CloseHandle(overlapped.hEvent);
    if (!ret)
        return HRESULT_FROM_WIN32(GetLastError());

    *used = request.header.DataUsed;
    return S_OK;
}

HRESULT qcap_backend_control_get(video_capture_device_t device, ULONG stream, BOOL mode, LONG *value)
{
    KSPROPERTY_VIDEOCONTROL_MODE_S request;
    DWORD returned;

    memset(&request, 0, sizeof(request));
    request.Property.Set = ks_propset_videocontrol;
    request.Property.Id = mode ? KSPROPERTY_VIDEOCONTROL_MODE : KSPROPERTY_VIDEOCONTROL_CAPS;
    request.Property.Flags = KSPROPERTY_TYPE_GET;
    request.StreamIndex = stream;
    if (!ks_ioctl(get_device(device)->filter, IOCTL_KS_PROPERTY, &request, sizeof(request), &request, sizeof(request), &returned, INFINITE))
        return HRESULT_FROM_WIN32(GetLastError());

    *value = request.Mode;
    return S_OK;
}

HRESULT qcap_backend_control_set_mode(video_capture_device_t device, ULONG stream, LONG mode)
{
    KSPROPERTY_VIDEOCONTROL_MODE_S request;
    DWORD returned;

    memset(&request, 0, sizeof(request));
    request.Property.Set = ks_propset_videocontrol;
    request.Property.Id = KSPROPERTY_VIDEOCONTROL_MODE;
    request.Property.Flags = KSPROPERTY_TYPE_SET;
    request.StreamIndex = stream;
    request.Mode = mode;
    if (!ks_ioctl(get_device(device)->filter, IOCTL_KS_PROPERTY, &request, sizeof(request), &request, sizeof(request), &returned, INFINITE))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

HRESULT qcap_backend_max_frame_rate(video_capture_device_t device, ULONG stream, ULONG range, SIZE dimensions, LONGLONG *rate)
{
    KSPROPERTY_VIDEOCONTROL_ACTUAL_FRAME_RATE_S request;
    DWORD returned;

    memset(&request, 0, sizeof(request));
    request.Property.Set = ks_propset_videocontrol;
    request.Property.Id = KSPROPERTY_VIDEOCONTROL_ACTUAL_FRAME_RATE;
    request.Property.Flags = KSPROPERTY_TYPE_GET;
    request.StreamIndex = stream;
    request.RangeIndex = range;
    request.Dimensions = dimensions;
    if (!ks_ioctl(get_device(device)->filter, IOCTL_KS_PROPERTY, &request, sizeof(request), &request, sizeof(request), &returned, INFINITE))
        return HRESULT_FROM_WIN32(GetLastError());

    *rate = request.CurrentMaxAvailableFrameRate;
    return S_OK;
}

HRESULT qcap_backend_frame_rates(video_capture_device_t device, ULONG stream, ULONG range, SIZE dimensions, LONG *count, LONGLONG **rates)
{
    KSPROPERTY_VIDEOCONTROL_FRAME_RATES_S request;
    KSMULTIPLE_ITEM *items;
    DWORD size = 0;
    HRESULT hr = S_OK;

    memset(&request, 0, sizeof(request));
    request.Property.Set = ks_propset_videocontrol;
    request.Property.Id = KSPROPERTY_VIDEOCONTROL_FRAME_RATES;
    request.Property.Flags = KSPROPERTY_TYPE_GET;
    request.StreamIndex = stream;
    request.RangeIndex = range;
    request.Dimensions = dimensions;
    if (!ks_ioctl(get_device(device)->filter, IOCTL_KS_PROPERTY, &request, sizeof(request), NULL, 0, &size, INFINITE) &&
            GetLastError() != ERROR_MORE_DATA)
        return HRESULT_FROM_WIN32(GetLastError());
    if (!size || !(items = malloc(size)))
        return E_OUTOFMEMORY;

    if (!ks_ioctl(get_device(device)->filter, IOCTL_KS_PROPERTY, &request, sizeof(request), items, size, &size, INFINITE))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
    }
    else if (!(*rates = CoTaskMemAlloc(items->Count * sizeof(LONGLONG))))
    {
        hr = E_OUTOFMEMORY;
    }
    else
    {
        memcpy(*rates, items + 1, items->Count * sizeof(LONGLONG));
        *count = items->Count;
    }
    free(items);
    return hr;
}

static NTSTATUS ks_load_caps(struct ks_device *device)
{
    KSP_PIN property = {{{0}}};
    KSMULTIPLE_ITEM *multiple;
    DWORD size = 0;
    BYTE *item;
    ULONG i;

    property.Property.Set = ks_propset_pin;
    property.Property.Id = KSPROPERTY_PIN_DATARANGES;
    property.Property.Flags = KSPROPERTY_TYPE_GET;
    property.PinId = 0;

    ks_ioctl(device->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), NULL, 0, &size, INFINITE);
    if (!size || !(multiple = malloc(size)))
        return E_FAIL;

    if (!ks_ioctl(device->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), multiple, size, &size, INFINITE))
    {
        free(multiple);
        return E_FAIL;
    }

    if (!(device->caps = calloc(multiple->Count, sizeof(*device->caps))))
    {
        free(multiple);
        return E_OUTOFMEMORY;
    }

    item = (BYTE *)(multiple + 1);
    for (i = 0; i < multiple->Count; ++i)
    {
        KSDATARANGE *range = (KSDATARANGE *)item;

        if (IsEqualGUID(&range->Specifier, &ks_specifier_videoinfo) && range->FormatSize >= sizeof(KS_DATARANGE_VIDEO))
        {
            KS_DATARANGE_VIDEO *video = (KS_DATARANGE_VIDEO *)range;
            struct ks_caps *caps = &device->caps[device->count++];

            caps->mt.majortype = range->MajorFormat;
            caps->mt.subtype = range->SubFormat;
            caps->mt.bFixedSizeSamples = TRUE;
            caps->mt.bTemporalCompression = video->bTemporalCompression;
            caps->mt.lSampleSize = range->SampleSize;
            caps->mt.formattype = FORMAT_VideoInfo;
            caps->mt.cbFormat = sizeof(VIDEOINFOHEADER);
            memcpy(&caps->format, &video->VideoInfoHeader, sizeof(caps->format));
            memcpy(&caps->caps, &video->ConfigCaps, sizeof(caps->caps));
        }
        else if (IsEqualGUID(&range->Specifier, &ks_specifier_videoinfo2) && range->FormatSize >= sizeof(KS_DATARANGE_VIDEO2))
        {
            KS_DATARANGE_VIDEO2 *video = (KS_DATARANGE_VIDEO2 *)range;
            struct ks_caps *caps = &device->caps[device->count++];

            caps->mt.majortype = range->MajorFormat;
            caps->mt.subtype = range->SubFormat;
            caps->mt.bFixedSizeSamples = TRUE;
            caps->mt.bTemporalCompression = video->bTemporalCompression;
            caps->mt.lSampleSize = range->SampleSize;
            caps->mt.formattype = ks_specifier_videoinfo2;
            caps->mt.cbFormat = sizeof(KS_VIDEOINFOHEADER2);
            memcpy(&caps->format2, &video->VideoInfoHeader, sizeof(caps->format2));
            memcpy(&caps->caps, &video->ConfigCaps, sizeof(caps->caps));
        }
        item += (range->FormatSize + 7) & ~7u;
    }

    free(multiple);
    return device->count ? S_OK : E_FAIL;
}

static NTSTATUS ks_create(void *args)
{
    const struct create_params *params = args;
    struct ks_device *device;
    NTSTATUS hr;

    if (!(device = calloc(1, sizeof(*device))))
        return E_OUTOFMEMORY;

    device->filter = CreateFileW(params->path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (device->filter == INVALID_HANDLE_VALUE)
    {
        ERR("Failed to open %s, error %lu.\n", debugstr_w(params->path), GetLastError());
        free(device);
        return E_FAIL;
    }

    if (FAILED(hr = ks_load_caps(device)))
    {
        CloseHandle(device->filter);
        free(device->caps);
        free(device);
        return hr;
    }

    ks_load_still_caps(device);
    device->interval = device->caps[0].format.AvgTimePerFrame;
    *params->device = (video_capture_device_t)(UINT_PTR)device;
    return S_OK;
}

static BOOL ks_set_state(struct ks_device *device, KSSTATE state)
{
    KSPROPERTY property;
    DWORD returned;

    property.Set = ks_propset_connection;
    property.Id = KSPROPERTY_CONNECTION_STATE;
    property.Flags = KSPROPERTY_TYPE_SET;
    return ks_ioctl(device->pin, IOCTL_KS_PROPERTY, &property, sizeof(property), &state, sizeof(state), &returned, INFINITE);
}

void qcap_backend_stop(video_capture_device_t handle)
{
    struct ks_device *device = get_device(handle);

    if (!device->pin)
        return;

    ks_set_state(device, KSSTATE_PAUSE);
    ks_set_state(device, KSSTATE_ACQUIRE);
    ks_set_state(device, KSSTATE_STOP);
    CloseHandle(device->pin);
    device->pin = NULL;
}

static NTSTATUS ks_start(void *args)
{
    const struct start_params *params = args;
    struct ks_device *device = get_device(params->device);
    const struct ks_caps *caps = &device->caps[device->current];
    struct
    {
        KSPIN_CONNECT connect;
        union
        {
            KS_DATAFORMAT_VIDEOINFOHEADER format;
            KS_DATAFORMAT_VIDEOINFOHEADER2 format2;
        };
    } request;
    BOOL video2 = IsEqualGUID(&caps->mt.formattype, &ks_specifier_videoinfo2);
    DWORD error;

    if (device->pin)
        return S_OK;

    memset(&request, 0, sizeof(request));
    request.connect.Interface.Set = ks_interface_standard;
    request.connect.Interface.Id = KSINTERFACE_STANDARD_STREAMING;
    request.connect.Medium.Set = ks_medium_standard;
    request.connect.Medium.Id = KSMEDIUM_TYPE_ANYINSTANCE;
    request.connect.PinId = 0;
    request.connect.Priority.PriorityClass = KSPRIORITY_NORMAL;
    request.connect.Priority.PrioritySubClass = KSPRIORITY_NORMAL;
    request.format.DataFormat.FormatSize = video2 ? sizeof(request.format2) : sizeof(request.format);
    request.format.DataFormat.SampleSize = caps->mt.lSampleSize;
    request.format.DataFormat.MajorFormat = caps->mt.majortype;
    request.format.DataFormat.SubFormat = caps->mt.subtype;
    request.format.DataFormat.Specifier = caps->mt.formattype;
    if (video2)
    {
        memcpy(&request.format2.VideoInfoHeader2, &caps->format2, sizeof(caps->format2));
        request.format2.VideoInfoHeader2.AvgTimePerFrame = device->interval;
    }
    else
    {
        memcpy(&request.format.VideoInfoHeader, &caps->format, sizeof(caps->format));
        request.format.VideoInfoHeader.AvgTimePerFrame = device->interval;
    }

    if ((error = KsCreatePin(device->filter, &request.connect, GENERIC_READ, &device->pin)))
    {
        ERR("KsCreatePin failed, error %lu.\n", error);
        device->pin = NULL;
        return HRESULT_FROM_WIN32(error);
    }

    if (!ks_set_state(device, KSSTATE_ACQUIRE) || !ks_set_state(device, KSSTATE_PAUSE) || !ks_set_state(device, KSSTATE_RUN))
    {
        error = GetLastError();
        ERR("Failed to run the pin, error %lu.\n", error);
        CloseHandle(device->pin);
        device->pin = NULL;
        return HRESULT_FROM_WIN32(error);
    }

    return S_OK;
}

static NTSTATUS ks_destroy(void *args)
{
    const struct destroy_params *params = args;
    struct ks_device *device = get_device(params->device);

    qcap_backend_still_stop(params->device);
    qcap_backend_stop(params->device);
    CloseHandle(device->filter);
    free(device->still_caps);
    free(device->caps);
    free(device);
    return S_OK;
}

static int ks_find_caps(const struct ks_device *device, const AM_MEDIA_TYPE *mt)
{
    const BITMAPINFOHEADER *bmi = qcap_backend_format_bmi(mt);
    int i;

    if (!bmi)
        return -1;

    for (i = 0; i < device->count; ++i)
    {
        const struct ks_caps *caps = &device->caps[i];

        if (IsEqualGUID(&mt->subtype, &caps->mt.subtype) &&
            IsEqualGUID(&mt->formattype, &caps->mt.formattype) &&
            bmi->biWidth == ks_caps_bmi(caps)->biWidth &&
            bmi->biHeight == ks_caps_bmi(caps)->biHeight)
        {
            return i;
        }
    }

    return -1;
}

static NTSTATUS ks_check_format(void *args)
{
    const struct check_format_params *params = args;

    return ks_find_caps(get_device(params->device), params->mt) < 0 ? E_FAIL : S_OK;
}

static NTSTATUS ks_set_format(void *args)
{
    const struct set_format_params *params = args;
    struct ks_device *device = get_device(params->device);
    const VIDEOINFOHEADER *format = (const VIDEOINFOHEADER *)params->mt->pbFormat;
    int index = ks_find_caps(device, params->mt);
    REFERENCE_TIME interval;

    if (index < 0)
        return VFW_E_INVALIDMEDIATYPE;
    if (device->pin)
        return VFW_E_WRONG_STATE;

    interval = format->AvgTimePerFrame;
    if (interval < device->caps[index].caps.MinFrameInterval || interval > device->caps[index].caps.MaxFrameInterval)
        interval = device->caps[index].format.AvgTimePerFrame;

    device->current = index;
    device->interval = interval;
    return S_OK;
}

static void ks_fill_type(const struct ks_caps *caps, AM_MEDIA_TYPE *mt, VIDEOINFOHEADER *format)
{
    *mt = caps->mt;
    mt->pbFormat = NULL;
    mt->pUnk = NULL;
    memcpy(format, &caps->format, caps->mt.cbFormat);
}

static NTSTATUS ks_get_format(void *args)
{
    const struct get_format_params *params = args;
    struct ks_device *device = get_device(params->device);

    ks_fill_type(&device->caps[device->current], params->mt, params->format);
    params->format->AvgTimePerFrame = device->interval;
    return S_OK;
}

static NTSTATUS ks_get_media_type(void *args)
{
    const struct get_media_type_params *params = args;
    struct ks_device *device = get_device(params->device);

    if (params->index >= (unsigned int)device->count)
        return VFW_S_NO_MORE_ITEMS;

    ks_fill_type(&device->caps[params->index], params->mt, params->format);
    return S_OK;
}

static NTSTATUS ks_get_caps(void *args)
{
    const struct get_caps_params *params = args;
    struct ks_device *device = get_device(params->device);

    ks_fill_type(&device->caps[params->index], params->mt, params->format);
    *params->caps = device->caps[params->index].caps;
    return S_OK;
}

static NTSTATUS ks_get_caps_count(void *args)
{
    const struct get_caps_count_params *params = args;

    *params->count = get_device(params->device)->count;
    return S_OK;
}

static NTSTATUS ks_get_frame_intervals(void *args)
{
    const struct get_frame_intervals_params *params = args;
    SIZE dimensions = {params->width, params->height};
    LONGLONG *rates = NULL;
    LONG count = 0;
    HRESULT hr;

    if (FAILED(hr = qcap_backend_frame_rates(params->device, 0, params->caps_index, dimensions, &count, &rates)))
        return hr;

    *params->count = count;
    if (params->intervals)
        memcpy(params->intervals, rates, count * sizeof(LONGLONG));
    CoTaskMemFree(rates);
    return S_OK;
}

static HRESULT ks_prop_range(struct ks_device *device, const GUID *set, LONG id, LONG *min, LONG *max, LONG *step, LONG *default_value, LONG *flags)
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
    if (!ks_ioctl(device->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), data, sizeof(data), &returned, INFINITE) ||
        returned < sizeof(data))
    {
        return E_PROP_ID_UNSUPPORTED;
    }

    *min = range->Bounds.SignedMinimum;
    *max = range->Bounds.SignedMaximum;
    *step = range->SteppingDelta;

    property.Flags = KSPROPERTY_TYPE_DEFAULTVALUES;
    if (ks_ioctl(device->filter, IOCTL_KS_PROPERTY, &property, sizeof(property), data, sizeof(data), &returned, INFINITE) &&
        returned >= sizeof(KSPROPERTY_DESCRIPTION) + sizeof(KSPROPERTY_MEMBERSHEADER) + sizeof(LONG))
    {
        *default_value = *(LONG *)(header + 1);
    }

    memset(&value, 0, sizeof(value));
    value.Property.Set = *set;
    value.Property.Id = id;
    value.Property.Flags = KSPROPERTY_TYPE_GET;
    if (!ks_ioctl(device->filter, IOCTL_KS_PROPERTY, &value, sizeof(value), &value, sizeof(value), &returned, INFINITE))
        return E_PROP_ID_UNSUPPORTED;

    *flags = value.Capabilities;
    return S_OK;
}

static HRESULT ks_prop_get(struct ks_device *device, const GUID *set, LONG id, LONG *result, LONG *flags)
{
    KSPROPERTY_VIDEOPROCAMP_S value;
    DWORD returned;

    memset(&value, 0, sizeof(value));
    value.Property.Set = *set;
    value.Property.Id = id;
    value.Property.Flags = KSPROPERTY_TYPE_GET;
    if (!ks_ioctl(device->filter, IOCTL_KS_PROPERTY, &value, sizeof(value), &value, sizeof(value), &returned, INFINITE))
        return E_PROP_ID_UNSUPPORTED;

    *result = value.Value;
    *flags = value.Flags;
    return S_OK;
}

static HRESULT ks_prop_set(struct ks_device *device, const GUID *set, LONG id, LONG new_value, LONG flags)
{
    KSPROPERTY_VIDEOPROCAMP_S value;
    DWORD returned;

    memset(&value, 0, sizeof(value));
    value.Property.Set = *set;
    value.Property.Id = id;
    value.Property.Flags = KSPROPERTY_TYPE_SET;
    value.Value = new_value;
    value.Flags = flags;
    if (!ks_ioctl(device->filter, IOCTL_KS_PROPERTY, &value, sizeof(value), &value, sizeof(value), &returned, INFINITE))
        return E_PROP_ID_UNSUPPORTED;

    return S_OK;
}

static NTSTATUS ks_get_prop_range(void *args)
{
    const struct get_prop_range_params *params = args;

    return ks_prop_range(get_device(params->device), &ks_propset_procamp, params->property,
                         params->min, params->max, params->step, params->default_value, params->flags);
}

static NTSTATUS ks_get_prop(void *args)
{
    const struct get_prop_params *params = args;

    return ks_prop_get(get_device(params->device), &ks_propset_procamp, params->property, params->value, params->flags);
}

static NTSTATUS ks_set_prop(void *args)
{
    const struct set_prop_params *params = args;

    return ks_prop_set(get_device(params->device), &ks_propset_procamp, params->property, params->value, params->flags);
}

HRESULT qcap_backend_camera_range(video_capture_device_t device, LONG property, LONG *min, LONG *max, LONG *step, LONG *default_value, LONG *flags)
{
    return ks_prop_range(get_device(device), &ks_propset_camera, property, min, max, step, default_value, flags);
}

HRESULT qcap_backend_camera_get(video_capture_device_t device, LONG property, LONG *value, LONG *flags)
{
    return ks_prop_get(get_device(device), &ks_propset_camera, property, value, flags);
}

HRESULT qcap_backend_camera_set(video_capture_device_t device, LONG property, LONG value, LONG flags)
{
    return ks_prop_set(get_device(device), &ks_propset_camera, property, value, flags);
}

static NTSTATUS ks_read_frame(void *args)
{
    struct read_frame_params *params = args;
    struct ks_device *device = get_device(params->device);
    const struct ks_caps *caps = &device->caps[device->current];
    struct
    {
        KSSTREAM_HEADER header;
        KS_FRAME_INFO info;
    } request;
    DWORD returned;

    if (!device->pin)
        return FALSE;

    memset(&request, 0, sizeof(request));
    request.header.Size = sizeof(request);
    request.header.FrameExtent = ks_caps_bmi(caps)->biSizeImage;
    request.header.Data = params->data;
    request.info.ExtendedHeaderSize = sizeof(request.info);

    if (!ks_ioctl(device->pin, IOCTL_KS_READ_STREAM, NULL, 0, &request, sizeof(request), &returned, KS_FRAME_TIMEOUT_MS))
    {
        WARN("Reading a frame failed, error %lu.\n", GetLastError());
        return FALSE;
    }

    params->size = request.header.DataUsed;
    return TRUE;
}

NTSTATUS qcap_backend_call(enum unix_funcs func, void *params)
{
    switch (func)
    {
        case unix_create:              return ks_create(params);
        case unix_start:               return ks_start(params);
        case unix_destroy:             return ks_destroy(params);
        case unix_check_format:        return ks_check_format(params);
        case unix_set_format:          return ks_set_format(params);
        case unix_get_format:          return ks_get_format(params);
        case unix_get_media_type:      return ks_get_media_type(params);
        case unix_get_caps:            return ks_get_caps(params);
        case unix_get_caps_count:      return ks_get_caps_count(params);
        case unix_get_frame_intervals: return ks_get_frame_intervals(params);
        case unix_get_prop_range:      return ks_get_prop_range(params);
        case unix_get_prop:            return ks_get_prop(params);
        case unix_set_prop:            return ks_set_prop(params);
        case unix_read_frame:          return ks_read_frame(params);
        default:                       return E_NOTIMPL;
    }
}
