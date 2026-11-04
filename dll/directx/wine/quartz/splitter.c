/*
 * PROJECT:     LiberNT DirectShow
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Native pull-model splitter filters
 * COPYRIGHT:   Copyright 2010 Maarten Lankhorst for CodeWeavers
 *              Copyright 2010 Aric Stewart for CodeWeavers
 *              Copyright 2019-2020 Zebediah Figura
 *              Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "splitter.h"

#include "mmreg.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

static inline struct splitter *impl_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct splitter, filter);
}

static inline struct splitter *impl_from_strmbase_sink(struct strmbase_sink *iface)
{
    return CONTAINING_RECORD(iface, struct splitter, sink);
}

static inline struct splitter_source *impl_source_from_IPin(IPin *iface)
{
    return CONTAINING_RECORD(iface, struct splitter_source, pin.pin.IPin_iface);
}

static inline struct splitter_source *impl_from_IMediaSeeking(IMediaSeeking *iface)
{
    return CONTAINING_RECORD(iface, struct splitter_source, seek.IMediaSeeking_iface);
}

static inline struct splitter_source *impl_from_IQualityControl(IQualityControl *iface)
{
    return CONTAINING_RECORD(iface, struct splitter_source, IQualityControl_iface);
}

HRESULT splitter_read(struct splitter *filter, LONGLONG offset, LONG size, void *data)
{
    if (offset < 0 || offset + size > filter->file_size)
        return E_FAIL;
    return IAsyncReader_SyncRead(filter->reader, offset, size, data);
}

void splitter_init_wave_media_type(AM_MEDIA_TYPE *mt, const WAVEFORMATEX *wfx, DWORD size)
{
    WAVEFORMATEX *format;
    DWORD format_size = max(size, sizeof(WAVEFORMATEX));

    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Audio;
    mt->subtype = MEDIATYPE_Audio;
    mt->subtype.Data1 = wfx->wFormatTag;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && size >= sizeof(WAVEFORMATEXTENSIBLE))
        mt->subtype = ((const WAVEFORMATEXTENSIBLE *)wfx)->SubFormat;
    mt->bFixedSizeSamples = TRUE;
    mt->lSampleSize = wfx->nBlockAlign;
    mt->formattype = FORMAT_WaveFormatEx;
    if (!(format = CoTaskMemAlloc(format_size)))
        return;
    memset(format, 0, format_size);
    memcpy(format, wfx, min(size, format_size));
    format->cbSize = format_size - sizeof(WAVEFORMATEX);
    mt->cbFormat = format_size;
    mt->pbFormat = (BYTE *)format;
}

static HRESULT send_sample(struct splitter_source *pin, const struct splitter_sample *s)
{
    struct splitter *filter = impl_from_strmbase_filter(pin->pin.pin.filter);
    REFERENCE_TIME start, stop;
    IMediaSample *sample;
    DWORD offset = 0;
    HRESULT hr = S_OK;
    BYTE *data;

    while (offset < s->size || !s->size)
    {
        DWORD size;

        if (FAILED(hr = IMemAllocator_GetBuffer(pin->pin.pAllocator, &sample, NULL, NULL, 0)))
        {
            WARN("Failed to get a sample, hr %#lx.\n", hr);
            return hr;
        }

        size = min((DWORD)IMediaSample_GetSize(sample), s->size - offset);
        IMediaSample_GetPointer(sample, &data);
        if (size && FAILED(hr = splitter_read(filter, s->offset + offset, size, data)))
        {
            WARN("Failed to read %lu bytes at %I64d, hr %#lx.\n", size, s->offset + offset, hr);
            IMediaSample_Release(sample);
            return hr;
        }
        IMediaSample_SetActualDataLength(sample, size);

        start = s->start;
        stop = s->stop;
        if (s->has_time && size < s->size)
        {
            start = s->start + (s->stop - s->start) * offset / s->size;
            stop = s->start + (s->stop - s->start) * (offset + size) / s->size;
        }
        start = (REFERENCE_TIME)((start - pin->seek.llCurrent) / pin->seek.dRate);
        stop = (REFERENCE_TIME)((stop - pin->seek.llCurrent) / pin->seek.dRate);
        IMediaSample_SetTime(sample, s->has_time ? &start : NULL, s->has_time ? &stop : NULL);
        IMediaSample_SetMediaTime(sample, NULL, NULL);
        IMediaSample_SetSyncPoint(sample, s->sync_point && !offset);
        IMediaSample_SetDiscontinuity(sample, pin->discontinuity && !offset);
        IMediaSample_SetPreroll(sample, FALSE);
        pin->discontinuity = FALSE;

        hr = pin->pin.pin.peer ? IMemInputPin_Receive(pin->pin.pMemInputPin, sample) : VFW_E_NOT_CONNECTED;
        IMediaSample_Release(sample);
        if (hr != S_OK)
            return hr;
        if (!s->size)
            break;
        offset += size;
    }
    return hr;
}

static DWORD CALLBACK stream_thread(void *arg)
{
    struct splitter_source *pin = arg;
    struct splitter *filter = impl_from_strmbase_filter(pin->pin.pin.filter);

    TRACE("Starting streaming thread for pin %p.\n", pin);

    for (;;)
    {
        struct splitter_sample sample;
        HRESULT hr;

        EnterCriticalSection(&filter->streaming_cs);
        while (filter->flushing)
            SleepConditionVariableCS(&filter->flushing_cv, &filter->streaming_cs, INFINITE);
        if (!filter->streaming)
        {
            LeaveCriticalSection(&filter->streaming_cs);
            break;
        }
        LeaveCriticalSection(&filter->streaming_cs);

        EnterCriticalSection(&pin->flushing_cs);

        if (pin->eos)
        {
            SleepConditionVariableCS(&pin->eos_cv, &pin->flushing_cs, INFINITE);
            LeaveCriticalSection(&pin->flushing_cs);
            continue;
        }

        if (pin->need_segment)
        {
            if (FAILED(hr = IPin_NewSegment(pin->pin.pin.peer, pin->seek.llCurrent, pin->seek.llStop, pin->seek.dRate)))
                WARN("Failed to deliver new segment, hr %#lx.\n", hr);
            pin->need_segment = FALSE;
        }

        hr = filter->ops->next_sample(pin, &sample);
        if (hr == S_OK && sample.has_time && sample.start >= pin->seek.llStop)
            hr = S_FALSE;

        if (hr == S_OK)
        {
            if (FAILED(hr = send_sample(pin, &sample)))
                WARN("Failed to send sample, hr %#lx.\n", hr);
        }
        else
        {
            TRACE("Sending EOS, hr %#lx.\n", hr);
            IPin_EndOfStream(pin->pin.pin.peer);
            pin->eos = TRUE;
        }

        LeaveCriticalSection(&pin->flushing_cs);
    }

    TRACE("Streaming stopped; exiting.\n");
    return 0;
}

static void free_source_pin(struct splitter_source *pin)
{
    unsigned int i;

    if (pin->pin.pin.peer)
    {
        if (SUCCEEDED(IMemAllocator_Decommit(pin->pin.pAllocator)))
            IPin_Disconnect(pin->pin.pin.peer);
        IPin_Disconnect(&pin->pin.pin.IPin_iface);
    }

    for (i = 0; i < pin->media_type_count; ++i)
        FreeMediaType(&pin->media_types[i]);
    free(pin->media_types);

    pin->flushing_cs.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection(&pin->flushing_cs);

    strmbase_seeking_cleanup(&pin->seek);
    strmbase_source_cleanup(&pin->pin);
    free(pin);
}

static void remove_source_pins(struct splitter *filter)
{
    unsigned int i;

    for (i = 0; i < filter->source_count; ++i)
        free_source_pin(filter->sources[i]);
    free(filter->sources);
    filter->sources = NULL;
    filter->source_count = 0;

    if (filter->ops->cleanup)
        filter->ops->cleanup(filter);

    BaseFilterImpl_IncrementPinVersion(&filter->filter);
}

static struct strmbase_pin *splitter_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct splitter *filter = impl_from_strmbase_filter(iface);

    if (filter->enum_sink_first)
    {
        if (!index)
            return &filter->sink.pin;
        if (index <= filter->source_count)
            return &filter->sources[index - 1]->pin.pin;
    }
    else
    {
        if (index < filter->source_count)
            return &filter->sources[index]->pin.pin;
        if (index == filter->source_count)
            return &filter->sink.pin;
    }
    return NULL;
}

static void splitter_destroy(struct strmbase_filter *iface)
{
    struct splitter *filter = impl_from_strmbase_filter(iface);

    if (filter->sink.pin.peer)
    {
        IPin_Disconnect(filter->sink.pin.peer);
        IPin_Disconnect(&filter->sink.pin.IPin_iface);
    }

    if (filter->ops->destroy)
        filter->ops->destroy(filter);

    filter->streaming_cs.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection(&filter->streaming_cs);

    strmbase_sink_cleanup(&filter->sink);
    strmbase_filter_cleanup(&filter->filter);
    free(filter);
}

static HRESULT splitter_query_interface(struct strmbase_filter *iface, REFIID iid, void **out)
{
    struct splitter *filter = impl_from_strmbase_filter(iface);

    if (filter->ops->query_interface)
        return filter->ops->query_interface(filter, iid, out);
    return E_NOINTERFACE;
}

static HRESULT splitter_init_stream(struct strmbase_filter *iface)
{
    struct splitter *filter = impl_from_strmbase_filter(iface);
    unsigned int i;

    if (!filter->sink_connected)
        return S_OK;

    filter->streaming = TRUE;

    for (i = 0; i < filter->source_count; ++i)
    {
        struct splitter_source *pin = filter->sources[i];
        HRESULT hr;

        if (!pin->pin.pin.peer)
            continue;

        if (FAILED(hr = IMemAllocator_Commit(pin->pin.pAllocator)))
            ERR("Failed to commit allocator, hr %#lx.\n", hr);

        filter->ops->seek(pin, pin->seek.llCurrent);
        pin->need_segment = TRUE;
        pin->discontinuity = TRUE;
        pin->eos = FALSE;
        pin->thread = CreateThread(NULL, 0, stream_thread, pin, 0, NULL);
    }

    return S_OK;
}

static HRESULT splitter_cleanup_stream(struct strmbase_filter *iface)
{
    struct splitter *filter = impl_from_strmbase_filter(iface);
    unsigned int i;

    if (!filter->sink_connected)
        return S_OK;

    EnterCriticalSection(&filter->streaming_cs);
    filter->streaming = FALSE;
    LeaveCriticalSection(&filter->streaming_cs);

    for (i = 0; i < filter->source_count; ++i)
    {
        struct splitter_source *pin = filter->sources[i];

        if (!pin->thread)
            continue;

        IMemAllocator_Decommit(pin->pin.pAllocator);

        EnterCriticalSection(&pin->flushing_cs);
        LeaveCriticalSection(&pin->flushing_cs);
        WakeConditionVariable(&pin->eos_cv);
        WaitForSingleObject(pin->thread, INFINITE);
        CloseHandle(pin->thread);
        pin->thread = NULL;
    }

    return S_OK;
}

static const struct strmbase_filter_ops filter_ops =
{
    .filter_get_pin = splitter_get_pin,
    .filter_destroy = splitter_destroy,
    .filter_query_interface = splitter_query_interface,
    .filter_init_stream = splitter_init_stream,
    .filter_cleanup_stream = splitter_cleanup_stream,
};

static HRESULT splitter_sink_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    struct splitter *filter = impl_from_strmbase_filter(iface->filter);
    return filter->ops->sink_query_accept(mt);
}

static HRESULT splitter_sink_connect(struct strmbase_sink *iface, IPin *peer, const AM_MEDIA_TYPE *mt)
{
    struct splitter *filter = impl_from_strmbase_sink(iface);
    LONGLONG available;
    HRESULT hr;

    if (FAILED(hr = IPin_QueryInterface(peer, &IID_IAsyncReader, (void **)&filter->reader)))
        return hr;

    IAsyncReader_Length(filter->reader, &filter->file_size, &available);

    if (FAILED(hr = filter->ops->parse(filter)))
    {
        remove_source_pins(filter);
        IAsyncReader_Release(filter->reader);
        filter->reader = NULL;
        return hr;
    }

    filter->sink_connected = TRUE;
    return S_OK;
}

static void splitter_sink_disconnect(struct strmbase_sink *iface)
{
    struct splitter *filter = impl_from_strmbase_sink(iface);

    if (!filter->sink_connected)
        return;

    remove_source_pins(filter);
    filter->sink_connected = FALSE;
    IAsyncReader_Release(filter->reader);
    filter->reader = NULL;
}

static HRESULT splitter_sink_get_media_type(struct strmbase_pin *iface, unsigned int index, AM_MEDIA_TYPE *mt)
{
    struct splitter *filter = impl_from_strmbase_filter(iface->filter);

    if (filter->ops->sink_get_media_type)
        return filter->ops->sink_get_media_type(index, mt);
    return VFW_S_NO_MORE_ITEMS;
}

static HRESULT splitter_sink_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    struct splitter *filter = impl_from_strmbase_filter(iface->filter);

    if (filter->ops->sink_query_interface)
        return filter->ops->sink_query_interface(filter, iid, out);
    return E_NOINTERFACE;
}

static const struct strmbase_sink_ops sink_ops =
{
    .base.pin_query_accept = splitter_sink_query_accept,
    .base.pin_get_media_type = splitter_sink_get_media_type,
    .base.pin_query_interface = splitter_sink_query_interface,
    .sink_connect = splitter_sink_connect,
    .sink_disconnect = splitter_sink_disconnect,
};

static LONGLONG to_current_format(const struct splitter_source *pin, LONGLONG time);
static LONGLONG from_current_format(const struct splitter_source *pin, LONGLONG value);

static HRESULT WINAPI splitter_change_current(IMediaSeeking *iface)
{
    return S_OK;
}

static HRESULT WINAPI splitter_change_stop(IMediaSeeking *iface)
{
    return S_OK;
}

static HRESULT WINAPI splitter_change_rate(IMediaSeeking *iface)
{
    return S_OK;
}

static HRESULT WINAPI seeking_QueryInterface(IMediaSeeking *iface, REFIID iid, void **out)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);
    return IPin_QueryInterface(&pin->pin.pin.IPin_iface, iid, out);
}

static ULONG WINAPI seeking_AddRef(IMediaSeeking *iface)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);
    return IPin_AddRef(&pin->pin.pin.IPin_iface);
}

static ULONG WINAPI seeking_Release(IMediaSeeking *iface)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);
    return IPin_Release(&pin->pin.pin.IPin_iface);
}

static HRESULT WINAPI seeking_SetPositions(IMediaSeeking *iface,
        LONGLONG *current, DWORD current_flags, LONGLONG *stop, DWORD stop_flags)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);
    struct splitter *filter = impl_from_strmbase_filter(pin->pin.pin.filter);
    unsigned int i;
    int j;

    LONGLONG current_time, stop_time;

    TRACE("pin %p, current %s, current_flags %#lx, stop %s, stop_flags %#lx.\n",
            pin, current ? debugstr_time(*current) : "<null>", current_flags,
            stop ? debugstr_time(*stop) : "<null>", stop_flags);

    if (current)
        current_time = from_current_format(pin, *current);
    if (stop)
        stop_time = from_current_format(pin, *stop);

    if (pin->pin.pin.filter->state == State_Stopped)
    {
        for (i = 0; i < filter->source_count; ++i)
            SourceSeekingImpl_SetPositions(&filter->sources[i]->seek.IMediaSeeking_iface,
                    current ? &current_time : NULL, current_flags, stop ? &stop_time : NULL, stop_flags);
        goto done;
    }

    if (!(current_flags & AM_SEEKING_NoFlush))
    {
        for (i = 0; i < filter->source_count; ++i)
        {
            if (filter->sources[i]->pin.pin.peer)
                IPin_BeginFlush(filter->sources[i]->pin.pin.peer);
        }
    }

    EnterCriticalSection(&filter->streaming_cs);
    filter->flushing = TRUE;
    LeaveCriticalSection(&filter->streaming_cs);

    for (i = 0; i < filter->source_count; ++i)
    {
        if (filter->sources[i]->pin.pin.peer)
            EnterCriticalSection(&filter->sources[i]->flushing_cs);
    }

    for (i = 0; i < filter->source_count; ++i)
    {
        struct splitter_source *source = filter->sources[i];

        SourceSeekingImpl_SetPositions(&source->seek.IMediaSeeking_iface,
                current ? &current_time : NULL, current_flags, stop ? &stop_time : NULL, stop_flags);
        if (source->pin.pin.peer)
            filter->ops->seek(source, source->seek.llCurrent);
    }

    if (!(current_flags & AM_SEEKING_NoFlush))
    {
        for (i = 0; i < filter->source_count; ++i)
        {
            if (filter->sources[i]->pin.pin.peer)
                IPin_EndFlush(filter->sources[i]->pin.pin.peer);
        }
    }

    for (j = filter->source_count - 1; j >= 0; --j)
    {
        struct splitter_source *source = filter->sources[j];

        source->need_segment = TRUE;
        source->discontinuity = TRUE;
        source->eos = FALSE;

        if (source->pin.pin.peer)
        {
            LeaveCriticalSection(&source->flushing_cs);
            WakeConditionVariable(&source->eos_cv);
        }
    }

    EnterCriticalSection(&filter->streaming_cs);
    filter->flushing = FALSE;
    LeaveCriticalSection(&filter->streaming_cs);
    WakeAllConditionVariable(&filter->flushing_cv);

done:
    if (current && (current_flags & AM_SEEKING_ReturnTime))
        *current = to_current_format(pin, current_time);
    if (stop && (stop_flags & AM_SEEKING_ReturnTime))
        *stop = to_current_format(pin, stop_time);
    return S_OK;
}

static BOOL is_unit_format(const struct splitter_source *pin, const GUID *format)
{
    unsigned int i;

    for (i = 0; i < pin->unit_format_count; ++i)
    {
        if (IsEqualGUID(format, pin->unit_formats[i]))
            return TRUE;
    }
    return FALSE;
}

static HRESULT convert_time(const struct splitter_source *pin, LONGLONG *target, const GUID *target_format,
        LONGLONG source, const GUID *source_format)
{
    if (IsEqualGUID(target_format, source_format) && !IsEqualGUID(target_format, &TIME_FORMAT_NONE))
    {
        *target = source;
        return S_OK;
    }
    if (IsEqualGUID(source_format, &TIME_FORMAT_MEDIA_TIME) && is_unit_format(pin, target_format))
    {
        *target = source * pin->unit_time_den / pin->unit_time_num;
        return S_OK;
    }
    if (IsEqualGUID(target_format, &TIME_FORMAT_MEDIA_TIME) && is_unit_format(pin, source_format))
    {
        *target = source * pin->unit_time_num / pin->unit_time_den;
        return S_OK;
    }
    return E_INVALIDARG;
}

static LONGLONG to_current_format(const struct splitter_source *pin, LONGLONG time)
{
    LONGLONG result = time;

    convert_time(pin, &result, &pin->seek.timeformat, time, &TIME_FORMAT_MEDIA_TIME);
    return result;
}

static LONGLONG from_current_format(const struct splitter_source *pin, LONGLONG value)
{
    LONGLONG result = value;

    convert_time(pin, &result, &TIME_FORMAT_MEDIA_TIME, value, &pin->seek.timeformat);
    return result;
}

static HRESULT WINAPI seeking_IsFormatSupported(IMediaSeeking *iface, const GUID *format)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);

    TRACE("pin %p, format %s.\n", pin, debugstr_guid(format));

    if (IsEqualGUID(format, &TIME_FORMAT_MEDIA_TIME) || (pin->time_format && IsEqualGUID(format, pin->time_format)))
        return S_OK;
    return S_FALSE;
}

static HRESULT WINAPI seeking_GetTimeFormat(IMediaSeeking *iface, GUID *format)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);

    EnterCriticalSection(&pin->seek.cs);
    *format = pin->seek.timeformat;
    LeaveCriticalSection(&pin->seek.cs);
    return S_OK;
}

static HRESULT WINAPI seeking_IsUsingTimeFormat(IMediaSeeking *iface, const GUID *format)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);
    HRESULT hr;

    EnterCriticalSection(&pin->seek.cs);
    hr = IsEqualGUID(format, &pin->seek.timeformat) ? S_OK : S_FALSE;
    LeaveCriticalSection(&pin->seek.cs);
    return hr;
}

static HRESULT WINAPI seeking_SetTimeFormat(IMediaSeeking *iface, const GUID *format)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);

    TRACE("pin %p, format %s.\n", pin, debugstr_guid(format));

    if (seeking_IsFormatSupported(iface, format) != S_OK)
        return E_INVALIDARG;

    EnterCriticalSection(&pin->seek.cs);
    pin->seek.timeformat = *format;
    LeaveCriticalSection(&pin->seek.cs);
    return S_OK;
}

static HRESULT WINAPI seeking_GetDuration(IMediaSeeking *iface, LONGLONG *duration)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);

    EnterCriticalSection(&pin->seek.cs);
    *duration = to_current_format(pin, pin->seek.llDuration);
    LeaveCriticalSection(&pin->seek.cs);
    return S_OK;
}

static HRESULT WINAPI seeking_GetStopPosition(IMediaSeeking *iface, LONGLONG *stop)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);

    EnterCriticalSection(&pin->seek.cs);
    *stop = to_current_format(pin, pin->seek.llStop);
    LeaveCriticalSection(&pin->seek.cs);
    return S_OK;
}

static HRESULT WINAPI seeking_GetCurrentPosition(IMediaSeeking *iface, LONGLONG *current)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);

    EnterCriticalSection(&pin->seek.cs);
    *current = to_current_format(pin, pin->seek.llCurrent);
    LeaveCriticalSection(&pin->seek.cs);
    return S_OK;
}

static HRESULT WINAPI seeking_ConvertTimeFormat(IMediaSeeking *iface, LONGLONG *target,
        const GUID *target_format, LONGLONG source, const GUID *source_format)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);
    GUID format;

    TRACE("pin %p, target %p, target_format %s, source %s, source_format %s.\n", pin, target,
            debugstr_guid(target_format), wine_dbgstr_longlong(source), debugstr_guid(source_format));

    EnterCriticalSection(&pin->seek.cs);
    format = pin->seek.timeformat;
    LeaveCriticalSection(&pin->seek.cs);

    return convert_time(pin, target, target_format ? target_format : &format,
            source, source_format ? source_format : &format);
}

static HRESULT WINAPI seeking_GetPositions(IMediaSeeking *iface, LONGLONG *current, LONGLONG *stop)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);

    EnterCriticalSection(&pin->seek.cs);
    if (current)
        *current = to_current_format(pin, pin->seek.llCurrent);
    if (stop)
        *stop = to_current_format(pin, pin->seek.llStop);
    LeaveCriticalSection(&pin->seek.cs);
    return S_OK;
}

static HRESULT WINAPI seeking_GetAvailable(IMediaSeeking *iface, LONGLONG *earliest, LONGLONG *latest)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);

    EnterCriticalSection(&pin->seek.cs);
    if (earliest)
        *earliest = 0;
    if (latest)
        *latest = to_current_format(pin, pin->seek.llDuration);
    LeaveCriticalSection(&pin->seek.cs);
    return S_OK;
}

static HRESULT WINAPI seeking_SetRate(IMediaSeeking *iface, double rate)
{
    struct splitter_source *pin = impl_from_IMediaSeeking(iface);
    struct splitter *filter = impl_from_strmbase_filter(pin->pin.pin.filter);
    unsigned int i;

    TRACE("pin %p, rate %.16e.\n", pin, rate);

    if (rate <= 0.0)
        return E_INVALIDARG;

    for (i = 0; i < filter->source_count; ++i)
    {
        SourceSeeking *seek = &filter->sources[i]->seek;

        EnterCriticalSection(&seek->cs);
        seek->dRate = rate;
        LeaveCriticalSection(&seek->cs);
    }
    return S_OK;
}

static HRESULT WINAPI seeking_GetPreroll(IMediaSeeking *iface, LONGLONG *preroll)
{
    TRACE("iface %p, preroll %p.\n", iface, preroll);
    return E_NOTIMPL;
}

static const IMediaSeekingVtbl seeking_vtbl =
{
    seeking_QueryInterface,
    seeking_AddRef,
    seeking_Release,
    SourceSeekingImpl_GetCapabilities,
    SourceSeekingImpl_CheckCapabilities,
    seeking_IsFormatSupported,
    SourceSeekingImpl_QueryPreferredFormat,
    seeking_GetTimeFormat,
    seeking_IsUsingTimeFormat,
    seeking_SetTimeFormat,
    seeking_GetDuration,
    seeking_GetStopPosition,
    seeking_GetCurrentPosition,
    seeking_ConvertTimeFormat,
    seeking_SetPositions,
    seeking_GetPositions,
    seeking_GetAvailable,
    seeking_SetRate,
    SourceSeekingImpl_GetRate,
    seeking_GetPreroll,
};

static inline struct splitter_source *impl_from_IMediaPosition(IMediaPosition *iface)
{
    return CONTAINING_RECORD(iface, struct splitter_source, IMediaPosition_iface);
}

static HRESULT WINAPI position_QueryInterface(IMediaPosition *iface, REFIID iid, void **out)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);
    return IPin_QueryInterface(&pin->pin.pin.IPin_iface, iid, out);
}

static ULONG WINAPI position_AddRef(IMediaPosition *iface)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);
    return IPin_AddRef(&pin->pin.pin.IPin_iface);
}

static ULONG WINAPI position_Release(IMediaPosition *iface)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);
    return IPin_Release(&pin->pin.pin.IPin_iface);
}

static HRESULT WINAPI position_GetTypeInfoCount(IMediaPosition *iface, UINT *count)
{
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI position_GetTypeInfo(IMediaPosition *iface, UINT index, LCID lcid, ITypeInfo **typeinfo)
{
    return strmbase_get_typeinfo(IMediaPosition_tid, typeinfo);
}

static HRESULT WINAPI position_GetIDsOfNames(IMediaPosition *iface, REFIID iid,
        LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    ITypeInfo *typeinfo;
    HRESULT hr;

    if (SUCCEEDED(hr = strmbase_get_typeinfo(IMediaPosition_tid, &typeinfo)))
    {
        hr = ITypeInfo_GetIDsOfNames(typeinfo, names, count, ids);
        ITypeInfo_Release(typeinfo);
    }
    return hr;
}

static HRESULT WINAPI position_Invoke(IMediaPosition *iface, DISPID id, REFIID iid, LCID lcid,
        WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *excepinfo, UINT *error_arg)
{
    ITypeInfo *typeinfo;
    HRESULT hr;

    if (SUCCEEDED(hr = strmbase_get_typeinfo(IMediaPosition_tid, &typeinfo)))
    {
        hr = ITypeInfo_Invoke(typeinfo, iface, id, flags, params, result, excepinfo, error_arg);
        ITypeInfo_Release(typeinfo);
    }
    return hr;
}

static HRESULT WINAPI position_get_Duration(IMediaPosition *iface, REFTIME *duration)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);

    EnterCriticalSection(&pin->seek.cs);
    *duration = (REFTIME)pin->seek.llDuration / 10000000;
    LeaveCriticalSection(&pin->seek.cs);
    return S_OK;
}

static HRESULT WINAPI position_put_CurrentPosition(IMediaPosition *iface, REFTIME time)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);
    LONGLONG current = (LONGLONG)(time * 10000000);

    current = to_current_format(pin, current);
    return IMediaSeeking_SetPositions(&pin->seek.IMediaSeeking_iface, &current,
            AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
}

static HRESULT WINAPI position_get_CurrentPosition(IMediaPosition *iface, REFTIME *time)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);

    EnterCriticalSection(&pin->seek.cs);
    *time = (REFTIME)pin->seek.llCurrent / 10000000;
    LeaveCriticalSection(&pin->seek.cs);
    return S_OK;
}

static HRESULT WINAPI position_get_StopTime(IMediaPosition *iface, REFTIME *time)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);

    EnterCriticalSection(&pin->seek.cs);
    *time = (REFTIME)pin->seek.llStop / 10000000;
    LeaveCriticalSection(&pin->seek.cs);
    return S_OK;
}

static HRESULT WINAPI position_put_StopTime(IMediaPosition *iface, REFTIME time)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);
    LONGLONG stop = (LONGLONG)(time * 10000000);

    stop = to_current_format(pin, stop);
    return IMediaSeeking_SetPositions(&pin->seek.IMediaSeeking_iface, NULL,
            AM_SEEKING_NoPositioning, &stop, AM_SEEKING_AbsolutePositioning);
}

static HRESULT WINAPI position_get_PrerollTime(IMediaPosition *iface, REFTIME *time)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI position_put_PrerollTime(IMediaPosition *iface, REFTIME time)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI position_put_Rate(IMediaPosition *iface, double rate)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);
    return IMediaSeeking_SetRate(&pin->seek.IMediaSeeking_iface, rate);
}

static HRESULT WINAPI position_get_Rate(IMediaPosition *iface, double *rate)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);
    return IMediaSeeking_GetRate(&pin->seek.IMediaSeeking_iface, rate);
}

static HRESULT WINAPI position_CanSeekForward(IMediaPosition *iface, LONG *can_seek)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);

    *can_seek = (pin->seek.dwCapabilities & AM_SEEKING_CanSeekForwards) ? OATRUE : OAFALSE;
    return S_OK;
}

static HRESULT WINAPI position_CanSeekBackward(IMediaPosition *iface, LONG *can_seek)
{
    struct splitter_source *pin = impl_from_IMediaPosition(iface);

    *can_seek = (pin->seek.dwCapabilities & AM_SEEKING_CanSeekBackwards) ? OATRUE : OAFALSE;
    return S_OK;
}

static const IMediaPositionVtbl position_vtbl =
{
    position_QueryInterface,
    position_AddRef,
    position_Release,
    position_GetTypeInfoCount,
    position_GetTypeInfo,
    position_GetIDsOfNames,
    position_Invoke,
    position_get_Duration,
    position_put_CurrentPosition,
    position_get_CurrentPosition,
    position_get_StopTime,
    position_put_StopTime,
    position_get_PrerollTime,
    position_put_PrerollTime,
    position_put_Rate,
    position_get_Rate,
    position_CanSeekForward,
    position_CanSeekBackward,
};

static HRESULT WINAPI quality_control_QueryInterface(IQualityControl *iface, REFIID iid, void **out)
{
    struct splitter_source *pin = impl_from_IQualityControl(iface);
    return IPin_QueryInterface(&pin->pin.pin.IPin_iface, iid, out);
}

static ULONG WINAPI quality_control_AddRef(IQualityControl *iface)
{
    struct splitter_source *pin = impl_from_IQualityControl(iface);
    return IPin_AddRef(&pin->pin.pin.IPin_iface);
}

static ULONG WINAPI quality_control_Release(IQualityControl *iface)
{
    struct splitter_source *pin = impl_from_IQualityControl(iface);
    return IPin_Release(&pin->pin.pin.IPin_iface);
}

static HRESULT WINAPI quality_control_Notify(IQualityControl *iface, IBaseFilter *sender, Quality q)
{
    TRACE("iface %p, sender %p, type %u, proportion %ld.\n", iface, sender, q.Type, q.Proportion);
    return S_OK;
}

static HRESULT WINAPI quality_control_SetSink(IQualityControl *iface, IQualityControl *sink)
{
    TRACE("iface %p, sink %p.\n", iface, sink);
    return S_OK;
}

static const IQualityControlVtbl quality_control_vtbl =
{
    quality_control_QueryInterface,
    quality_control_AddRef,
    quality_control_Release,
    quality_control_Notify,
    quality_control_SetSink,
};

static HRESULT source_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    struct splitter_source *pin = impl_source_from_IPin(&iface->IPin_iface);

    if (IsEqualGUID(iid, &IID_IMediaSeeking))
        *out = &pin->seek.IMediaSeeking_iface;
    else if (IsEqualGUID(iid, &IID_IQualityControl))
        *out = &pin->IQualityControl_iface;
    else if (IsEqualGUID(iid, &IID_IMediaPosition)
            && !impl_from_strmbase_filter(iface->filter)->no_source_position)
        *out = &pin->IMediaPosition_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static BOOL compare_media_types(const AM_MEDIA_TYPE *a, const AM_MEDIA_TYPE *b)
{
    return IsEqualGUID(&a->majortype, &b->majortype)
            && IsEqualGUID(&a->subtype, &b->subtype)
            && IsEqualGUID(&a->formattype, &b->formattype)
            && a->cbFormat == b->cbFormat
            && !memcmp(a->pbFormat, b->pbFormat, a->cbFormat);
}

static HRESULT source_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    struct splitter_source *pin = impl_source_from_IPin(&iface->IPin_iface);
    unsigned int i;

    for (i = 0; i < pin->media_type_count; ++i)
    {
        if (compare_media_types(mt, &pin->media_types[i]))
            return S_OK;
    }
    return S_FALSE;
}

static HRESULT source_get_media_type(struct strmbase_pin *iface, unsigned int index, AM_MEDIA_TYPE *mt)
{
    struct splitter_source *pin = impl_source_from_IPin(&iface->IPin_iface);

    if (index >= pin->media_type_count)
        return VFW_S_NO_MORE_ITEMS;
    return CopyMediaType(mt, &pin->media_types[index]);
}

static HRESULT WINAPI source_DecideBufferSize(struct strmbase_source *iface,
        IMemAllocator *allocator, ALLOCATOR_PROPERTIES *props)
{
    struct splitter_source *pin = impl_source_from_IPin(&iface->pin.IPin_iface);
    ALLOCATOR_PROPERTIES ret_props;

    props->cBuffers = max(props->cBuffers, (LONG)pin->buffer_count);
    props->cbBuffer = max(props->cbBuffer, (LONG)pin->buffer_size);
    props->cbAlign = max(props->cbAlign, 1);
    return IMemAllocator_SetProperties(allocator, props, &ret_props);
}

static const struct strmbase_source_ops source_ops =
{
    .base.pin_query_interface = source_query_interface,
    .base.pin_query_accept = source_query_accept,
    .base.pin_get_media_type = source_get_media_type,
    .pfnAttemptConnection = BaseOutputPinImpl_AttemptConnection,
    .pfnDecideAllocator = BaseOutputPinImpl_DecideAllocator,
    .pfnDecideBufferSize = source_DecideBufferSize,
};

struct splitter_source *splitter_add_source(struct splitter *filter, const WCHAR *name,
        const AM_MEDIA_TYPE *media_types, unsigned int media_type_count, REFERENCE_TIME duration)
{
    struct splitter_source *pin, **new_array;
    unsigned int i;

    if (!(new_array = realloc(filter->sources, (filter->source_count + 1) * sizeof(*filter->sources))))
        return NULL;
    filter->sources = new_array;

    if (!(pin = calloc(1, sizeof(*pin))))
        return NULL;
    if (!(pin->media_types = calloc(media_type_count, sizeof(*pin->media_types))))
    {
        free(pin);
        return NULL;
    }
    for (i = 0; i < media_type_count; ++i)
        CopyMediaType(&pin->media_types[i], &media_types[i]);
    pin->media_type_count = media_type_count;
    pin->buffer_count = 1;
    pin->index = filter->source_count;

    strmbase_source_init(&pin->pin, &filter->filter, name, &source_ops);
    pin->IQualityControl_iface.lpVtbl = &quality_control_vtbl;
    pin->IMediaPosition_iface.lpVtbl = &position_vtbl;
    strmbase_seeking_init(&pin->seek, &seeking_vtbl, splitter_change_stop,
            splitter_change_current, splitter_change_rate);
    pin->seek.llDuration = pin->seek.llStop = duration;
    pin->unit_time_num = pin->unit_time_den = 1;

    InitializeCriticalSectionEx(&pin->flushing_cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO);
    pin->flushing_cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": splitter_source.flushing_cs");
    InitializeConditionVariable(&pin->eos_cv);

    filter->sources[filter->source_count++] = pin;
    BaseFilterImpl_IncrementPinVersion(&filter->filter);
    return pin;
}

HRESULT splitter_create(IUnknown *outer, const CLSID *clsid, const struct splitter_ops *ops,
        const WCHAR *sink_name, BOOL enum_sink_first, struct splitter **out)
{
    struct splitter *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    strmbase_filter_init(&object->filter, outer, clsid, &filter_ops);
    strmbase_sink_init(&object->sink, &object->filter, sink_name, &sink_ops, NULL);
    object->ops = ops;
    object->enum_sink_first = enum_sink_first;

    InitializeCriticalSectionEx(&object->streaming_cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO);
    object->streaming_cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": splitter.streaming_cs");
    InitializeConditionVariable(&object->flushing_cv);

    *out = object;
    return S_OK;
}
