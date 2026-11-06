/*
 * Implementation of the SmartTee filter
 *
 * Copyright 2015 Damjan Jovanovic
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

#include "qcap_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

struct output_control
{
    IQualityControl IQualityControl_iface;
    IAMStreamControl IAMStreamControl_iface;
    struct strmbase_source *source;

    CRITICAL_SECTION cs;
    HANDLE event;
    FILTER_STATE state;
    REFERENCE_TIME run_start, start, stop;
    DWORD start_cookie, stop_cookie;
    BOOL discarding, discard_on_stop, send_extra, extra_sent, flushing;
};

struct stream_control_event
{
    LONG code;
    DWORD cookie;
};

static void output_control_init(struct output_control *control, struct strmbase_source *source);
static void output_control_cleanup(struct output_control *control);
static void output_control_set_state(struct output_control *control, FILTER_STATE state, REFERENCE_TIME start);

typedef struct {
    struct strmbase_filter filter;
    struct strmbase_sink sink;
    struct strmbase_source capture, preview;
    IQualityControl IQualityControl_iface;
    struct output_control capture_control, preview_control;
} SmartTeeFilter;

static inline SmartTeeFilter *impl_from_strmbase_filter(struct strmbase_filter *filter)
{
    return CONTAINING_RECORD(filter, SmartTeeFilter, filter);
}

static inline SmartTeeFilter *impl_from_strmbase_pin(struct strmbase_pin *pin)
{
    return impl_from_strmbase_filter(pin->filter);
}

static struct strmbase_pin *smart_tee_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    SmartTeeFilter *filter = impl_from_strmbase_filter(iface);

    if (index == 0)
        return &filter->sink.pin;
    else if (index == 1)
        return &filter->capture.pin;
    else if (index == 2)
        return &filter->preview.pin;
    return NULL;
}

static void smart_tee_destroy(struct strmbase_filter *iface)
{
    SmartTeeFilter *filter = impl_from_strmbase_filter(iface);

    strmbase_sink_cleanup(&filter->sink);
    strmbase_source_cleanup(&filter->capture);
    strmbase_source_cleanup(&filter->preview);
    output_control_cleanup(&filter->capture_control);
    output_control_cleanup(&filter->preview_control);
    strmbase_filter_cleanup(&filter->filter);
    free(filter);
}

static HRESULT smart_tee_wait_state(struct strmbase_filter *iface, DWORD timeout)
{
    return iface->state == State_Paused ? VFW_S_CANT_CUE : S_OK;
}

static HRESULT smart_tee_init_stream(struct strmbase_filter *iface)
{
    SmartTeeFilter *filter = impl_from_strmbase_filter(iface);
    HRESULT hr;

    if (filter->capture.pin.peer && FAILED(hr = IMemAllocator_Commit(filter->capture.pAllocator)))
        return hr;
    if (filter->preview.pin.peer && FAILED(hr = IMemAllocator_Commit(filter->preview.pAllocator)))
        return hr;
    output_control_set_state(&filter->capture_control, State_Paused, 0);
    output_control_set_state(&filter->preview_control, State_Paused, 0);
    return S_OK;
}

static HRESULT smart_tee_start_stream(struct strmbase_filter *iface, REFERENCE_TIME time)
{
    SmartTeeFilter *filter = impl_from_strmbase_filter(iface);

    output_control_set_state(&filter->capture_control, State_Running, time);
    output_control_set_state(&filter->preview_control, State_Running, time);
    return S_OK;
}

static HRESULT smart_tee_stop_stream(struct strmbase_filter *iface)
{
    SmartTeeFilter *filter = impl_from_strmbase_filter(iface);

    output_control_set_state(&filter->capture_control, State_Paused, 0);
    output_control_set_state(&filter->preview_control, State_Paused, 0);
    return S_OK;
}

static HRESULT smart_tee_cleanup_stream(struct strmbase_filter *iface)
{
    SmartTeeFilter *filter = impl_from_strmbase_filter(iface);

    output_control_set_state(&filter->capture_control, State_Stopped, 0);
    output_control_set_state(&filter->preview_control, State_Stopped, 0);
    if (filter->capture.pin.peer)
        IMemAllocator_Decommit(filter->capture.pAllocator);
    if (filter->preview.pin.peer)
        IMemAllocator_Decommit(filter->preview.pAllocator);
    return S_OK;
}

static const struct strmbase_filter_ops filter_ops =
{
    .filter_get_pin = smart_tee_get_pin,
    .filter_destroy = smart_tee_destroy,
    .filter_init_stream = smart_tee_init_stream,
    .filter_start_stream = smart_tee_start_stream,
    .filter_stop_stream = smart_tee_stop_stream,
    .filter_cleanup_stream = smart_tee_cleanup_stream,
    .filter_wait_state = smart_tee_wait_state,
};

static HRESULT sink_query_accept(struct strmbase_pin *base, const AM_MEDIA_TYPE *pmt)
{
    SmartTeeFilter *This = impl_from_strmbase_pin(base);
    TRACE("(%p, AM_MEDIA_TYPE(%p))\n", This, pmt);
    if (!pmt)
        return VFW_E_TYPE_NOT_ACCEPTED;
    /* We'll take any media type, but the output pins will later
     * struggle to connect downstream. */
    return S_OK;
}

static HRESULT sink_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    SmartTeeFilter *filter = impl_from_strmbase_pin(iface);

    if (IsEqualGUID(iid, &IID_IMemInputPin))
        *out = &filter->sink.IMemInputPin_iface;
    else if (IsEqualGUID(iid, &IID_IQualityControl))
        *out = &filter->IQualityControl_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static inline SmartTeeFilter *impl_from_IQualityControl(IQualityControl *iface)
{
    return CONTAINING_RECORD(iface, SmartTeeFilter, IQualityControl_iface);
}

static HRESULT WINAPI sink_quality_control_QueryInterface(IQualityControl *iface, REFIID iid, void **out)
{
    SmartTeeFilter *filter = impl_from_IQualityControl(iface);
    return IPin_QueryInterface(&filter->sink.pin.IPin_iface, iid, out);
}

static ULONG WINAPI sink_quality_control_AddRef(IQualityControl *iface)
{
    SmartTeeFilter *filter = impl_from_IQualityControl(iface);
    return IPin_AddRef(&filter->sink.pin.IPin_iface);
}

static ULONG WINAPI sink_quality_control_Release(IQualityControl *iface)
{
    SmartTeeFilter *filter = impl_from_IQualityControl(iface);
    return IPin_Release(&filter->sink.pin.IPin_iface);
}

static HRESULT WINAPI sink_quality_control_Notify(IQualityControl *iface, IBaseFilter *sender, Quality q)
{
    SmartTeeFilter *filter = impl_from_IQualityControl(iface);

    TRACE("filter %p, sender %p, type %#x, proportion %ld, late %s, timestamp %s.\n",
            filter, sender, q.Type, q.Proportion,
            wine_dbgstr_longlong(q.Late), wine_dbgstr_longlong(q.TimeStamp));

    if (!sender)
        return E_POINTER;
    return S_OK;
}

static HRESULT WINAPI sink_quality_control_SetSink(IQualityControl *iface, IQualityControl *sink)
{
    SmartTeeFilter *filter = impl_from_IQualityControl(iface);

    TRACE("filter %p, sink %p.\n", filter, sink);

    return S_OK;
}

static const IQualityControlVtbl sink_quality_control_vtbl =
{
    sink_quality_control_QueryInterface,
    sink_quality_control_AddRef,
    sink_quality_control_Release,
    sink_quality_control_Notify,
    sink_quality_control_SetSink,
};

static void output_control_notify(struct output_control *control, const struct stream_control_event *events, unsigned int count)
{
    IFilterGraph *graph = control->source->pin.filter->graph;
    IMediaEventSink *sink;
    unsigned int i;

    if (!count || !graph || FAILED(IFilterGraph_QueryInterface(graph, &IID_IMediaEventSink, (void **)&sink)))
        return;
    for (i = 0; i < count; ++i)
        IMediaEventSink_Notify(sink, events[i].code, (LONG_PTR)&control->IAMStreamControl_iface, events[i].cookie);
    IMediaEventSink_Release(sink);
}

static void output_control_cancel_start(struct output_control *control)
{
    control->start = MAXLONGLONG;
    control->start_cookie = 0;
}

static void output_control_cancel_stop(struct output_control *control)
{
    control->stop = MAXLONGLONG;
    control->stop_cookie = 0;
    control->discard_on_stop = FALSE;
}

static void output_control_execute_start(struct output_control *control,
        struct stream_control_event *events, unsigned int *count)
{
    control->discarding = FALSE;
    if (control->start_cookie)
    {
        events[*count].code = EC_STREAM_CONTROL_STARTED;
        events[(*count)++].cookie = control->start_cookie;
    }
    output_control_cancel_start(control);
}

static void output_control_execute_stop(struct output_control *control,
        struct stream_control_event *events, unsigned int *count)
{
    control->discarding = control->discard_on_stop;
    if (control->stop_cookie)
    {
        events[*count].code = EC_STREAM_CONTROL_STOPPED;
        events[(*count)++].cookie = control->stop_cookie;
    }
    output_control_cancel_stop(control);
}

static void output_control_execute_both(struct output_control *control,
        struct stream_control_event *events, unsigned int *count)
{
    if (control->start <= control->stop)
    {
        output_control_execute_start(control, events, count);
        output_control_execute_stop(control, events, count);
    }
    else
    {
        output_control_execute_stop(control, events, count);
        output_control_execute_start(control, events, count);
    }
}

static void output_control_check_times(struct output_control *control, REFERENCE_TIME start,
        REFERENCE_TIME stop, struct stream_control_event *events, unsigned int *count)
{
    if (control->stop >= start)
    {
        if (control->start >= stop)
            return;
        if (control->stop < control->start)
            output_control_execute_stop(control, events, count);
        output_control_execute_start(control, events, count);
    }
    else if (control->start >= stop)
        output_control_execute_stop(control, events, count);
    else
        output_control_execute_both(control, events, count);
}

static BOOL output_control_check_sample(struct output_control *control, IMediaSample *sample)
{
    struct strmbase_filter *filter = control->source->pin.filter;
    REFERENCE_TIME start, stop, now, ahead;
    struct stream_control_event events[2];
    IReferenceClock *clock;
    unsigned int count;
    BOOL deliver;
    DWORD wait;

    if (sample && FAILED(IMediaSample_GetTime(sample, &start, &stop)))
        sample = NULL;

    for (;;)
    {
        EnterCriticalSection(&filter->filter_cs);
        if ((clock = filter->clock))
            IReferenceClock_AddRef(clock);
        LeaveCriticalSection(&filter->filter_cs);

        count = 0;
        wait = 0;
        EnterCriticalSection(&control->cs);
        if (control->flushing || control->state == State_Stopped)
            deliver = FALSE;
        else if (!sample)
            deliver = !control->discarding;
        else
        {
            output_control_check_times(control, start, stop, events, &count);
            deliver = !control->discarding;
            if (!deliver && control->send_extra && !control->extra_sent && control->stop == MAXLONGLONG)
            {
                control->extra_sent = TRUE;
                deliver = TRUE;
            }
            if (!deliver && clock)
            {
                if (control->state == State_Paused)
                    wait = INFINITE;
                else if (SUCCEEDED(IReferenceClock_GetTime(clock, &now))
                        && (ahead = (start - (now - control->run_start)) / 10000) >= 10)
                    wait = min(ahead, INFINITE - 1);
            }
        }
        LeaveCriticalSection(&control->cs);

        if (clock)
            IReferenceClock_Release(clock);
        output_control_notify(control, events, count);

        if (!wait || WaitForSingleObject(control->event, wait) == WAIT_TIMEOUT)
            return deliver;
    }
}

static void output_control_set_state(struct output_control *control, FILTER_STATE state, REFERENCE_TIME start)
{
    struct stream_control_event events[2];
    unsigned int count = 0;

    EnterCriticalSection(&control->cs);
    if (control->state != state)
    {
        if (state == State_Stopped)
        {
            if (control->start != MAXLONGLONG && control->stop != MAXLONGLONG)
                output_control_execute_both(control, events, &count);
            else if (control->start != MAXLONGLONG)
                output_control_execute_start(control, events, &count);
            else if (control->stop != MAXLONGLONG)
                output_control_execute_stop(control, events, &count);
            control->discarding = FALSE;
        }
        else if (state == State_Running)
            control->run_start = start;
        control->state = state;
        SetEvent(control->event);
    }
    LeaveCriticalSection(&control->cs);

    output_control_notify(control, events, count);
}

static void output_control_set_flushing(struct output_control *control, BOOL flushing)
{
    EnterCriticalSection(&control->cs);
    control->flushing = flushing;
    SetEvent(control->event);
    LeaveCriticalSection(&control->cs);
}

static inline struct output_control *impl_from_IAMStreamControl(IAMStreamControl *iface)
{
    return CONTAINING_RECORD(iface, struct output_control, IAMStreamControl_iface);
}

static HRESULT WINAPI stream_control_QueryInterface(IAMStreamControl *iface, REFIID iid, void **out)
{
    struct output_control *control = impl_from_IAMStreamControl(iface);
    return IPin_QueryInterface(&control->source->pin.IPin_iface, iid, out);
}

static ULONG WINAPI stream_control_AddRef(IAMStreamControl *iface)
{
    struct output_control *control = impl_from_IAMStreamControl(iface);
    return IPin_AddRef(&control->source->pin.IPin_iface);
}

static ULONG WINAPI stream_control_Release(IAMStreamControl *iface)
{
    struct output_control *control = impl_from_IAMStreamControl(iface);
    return IPin_Release(&control->source->pin.IPin_iface);
}

static HRESULT WINAPI stream_control_StartAt(IAMStreamControl *iface, const REFERENCE_TIME *start, DWORD cookie)
{
    struct output_control *control = impl_from_IAMStreamControl(iface);

    TRACE("control %p, start %s, cookie %#lx.\n", control,
            start ? wine_dbgstr_longlong(*start) : "(null)", cookie);

    EnterCriticalSection(&control->cs);
    if (!start)
    {
        output_control_cancel_start(control);
        control->discarding = FALSE;
    }
    else if (*start == MAXLONGLONG)
    {
        output_control_cancel_start(control);
        if (control->state == State_Stopped && control->stop != MAXLONGLONG)
            control->discarding = FALSE;
    }
    else
    {
        if (control->state == State_Stopped && control->stop >= *start)
            control->discarding = TRUE;
        control->start = *start;
        control->start_cookie = cookie;
    }
    SetEvent(control->event);
    LeaveCriticalSection(&control->cs);
    return S_OK;
}

static HRESULT WINAPI stream_control_StopAt(IAMStreamControl *iface, const REFERENCE_TIME *stop,
        BOOL send_extra, DWORD cookie)
{
    struct output_control *control = impl_from_IAMStreamControl(iface);

    TRACE("control %p, stop %s, send_extra %d, cookie %#lx.\n", control,
            stop ? wine_dbgstr_longlong(*stop) : "(null)", send_extra, cookie);

    EnterCriticalSection(&control->cs);
    control->send_extra = FALSE;
    control->extra_sent = FALSE;
    if (!stop)
    {
        output_control_cancel_stop(control);
        control->discarding = TRUE;
    }
    else if (*stop == MAXLONGLONG)
    {
        output_control_cancel_stop(control);
        if (control->state == State_Stopped && control->start != MAXLONGLONG)
            control->discarding = TRUE;
    }
    else
    {
        if (control->state == State_Stopped && control->start > *stop)
            control->discarding = FALSE;
        control->send_extra = send_extra;
        control->stop = *stop;
        control->stop_cookie = cookie;
        control->discard_on_stop = TRUE;
    }
    SetEvent(control->event);
    LeaveCriticalSection(&control->cs);
    return S_OK;
}

static HRESULT WINAPI stream_control_GetInfo(IAMStreamControl *iface, AM_STREAM_INFO *info)
{
    struct output_control *control = impl_from_IAMStreamControl(iface);

    TRACE("control %p, info %p.\n", control, info);

    if (!info)
        return E_POINTER;

    EnterCriticalSection(&control->cs);
    info->tStart = control->start;
    info->tStop = control->stop;
    info->dwStartCookie = control->start_cookie;
    info->dwStopCookie = control->stop_cookie;
    info->dwFlags = 0;
    if (control->start != MAXLONGLONG)
        info->dwFlags |= AM_STREAM_INFO_START_DEFINED;
    if (control->stop != MAXLONGLONG)
        info->dwFlags |= AM_STREAM_INFO_STOP_DEFINED;
    if (control->discarding)
        info->dwFlags |= AM_STREAM_INFO_DISCARDING;
    if (control->send_extra)
        info->dwFlags |= AM_STREAM_INFO_STOP_SEND_EXTRA;
    LeaveCriticalSection(&control->cs);
    return S_OK;
}

static const IAMStreamControlVtbl stream_control_vtbl =
{
    stream_control_QueryInterface,
    stream_control_AddRef,
    stream_control_Release,
    stream_control_StartAt,
    stream_control_StopAt,
    stream_control_GetInfo,
};

static HRESULT copy_sample(IMediaSample *inputSample, IMemAllocator *allocator, BOOL preview, IMediaSample **pOutputSample)
{
    REFERENCE_TIME startTime, endTime;
    BOOL haveStartTime = TRUE, haveEndTime = TRUE;
    IMediaSample *outputSample = NULL;
    BYTE *ptrIn, *ptrOut;
    AM_MEDIA_TYPE *mediaType = NULL;
    HRESULT hr;

    hr = IMediaSample_GetTime(inputSample, &startTime, &endTime);
    if (hr == S_OK)
        ;
    else if (hr == VFW_S_NO_STOP_TIME)
        haveEndTime = FALSE;
    else if (hr == VFW_E_SAMPLE_TIME_NOT_SET)
        haveStartTime = haveEndTime = FALSE;
    else
        goto end;

    hr = IMemAllocator_GetBuffer(allocator, &outputSample,
            haveStartTime ? &startTime : NULL, haveEndTime ? &endTime : NULL, 0);
    if (FAILED(hr)) goto end;
    if (IMediaSample_GetSize(outputSample) < IMediaSample_GetActualDataLength(inputSample)) {
        ERR("insufficient space in sample\n");
        hr = VFW_E_BUFFER_OVERFLOW;
        goto end;
    }

    hr = IMediaSample_SetTime(outputSample, haveStartTime ? &startTime : NULL, haveEndTime ? &endTime : NULL);
    if (FAILED(hr)) goto end;

    hr = IMediaSample_GetPointer(inputSample, &ptrIn);
    if (FAILED(hr)) goto end;
    hr = IMediaSample_GetPointer(outputSample, &ptrOut);
    if (FAILED(hr)) goto end;
    memcpy(ptrOut, ptrIn, IMediaSample_GetActualDataLength(inputSample));
    IMediaSample_SetActualDataLength(outputSample, IMediaSample_GetActualDataLength(inputSample));

    hr = IMediaSample_SetDiscontinuity(outputSample, !preview && IMediaSample_IsDiscontinuity(inputSample) == S_OK);
    if (FAILED(hr)) goto end;

    if (!preview)
    {
        haveStartTime = haveEndTime = TRUE;
        hr = IMediaSample_GetMediaTime(inputSample, &startTime, &endTime);
        if (hr == S_OK)
            ;
        else if (hr == VFW_S_NO_STOP_TIME)
            haveEndTime = FALSE;
        else if (hr == VFW_E_MEDIA_TIME_NOT_SET)
            haveStartTime = haveEndTime = FALSE;
        else
            goto end;
        hr = IMediaSample_SetMediaTime(outputSample, haveStartTime ? &startTime : NULL, haveEndTime ? &endTime : NULL);
        if (FAILED(hr)) goto end;
    }

    hr = IMediaSample_GetMediaType(inputSample, &mediaType);
    if (FAILED(hr)) goto end;
    if (hr == S_OK) {
        hr = IMediaSample_SetMediaType(outputSample, mediaType);
        if (FAILED(hr)) goto end;
    }

    hr = IMediaSample_SetPreroll(outputSample, IMediaSample_IsPreroll(inputSample) == S_OK);
    if (FAILED(hr)) goto end;

    hr = IMediaSample_SetSyncPoint(outputSample, IMediaSample_IsSyncPoint(inputSample) == S_OK);
    if (FAILED(hr)) goto end;

end:
    if (mediaType)
        DeleteMediaType(mediaType);
    if (FAILED(hr) && outputSample) {
        IMediaSample_Release(outputSample);
        outputSample = NULL;
    }
    *pOutputSample = outputSample;
    return hr;
}

static HRESULT WINAPI SmartTeeFilterInput_Receive(struct strmbase_sink *base, IMediaSample *inputSample)
{
    SmartTeeFilter *This = impl_from_strmbase_pin(&base->pin);
    IMediaSample *captureSample = NULL;
    IMediaSample *previewSample = NULL;
    HRESULT hrCapture = VFW_E_NOT_CONNECTED, hrPreview = VFW_E_NOT_CONNECTED;

    TRACE("(%p)->(%p)\n", This, inputSample);

    if (This->filter.state == State_Stopped)
        return VFW_E_WRONG_STATE;
    if (This->sink.flushing)
        return S_FALSE;

    /* Modifying the image coming out of one pin doesn't modify the image
     * coming out of the other. MSDN claims the filter doesn't copy,
     * but unless it somehow uses copy-on-write, I just don't see how
     * that's possible. */

    /* FIXME: we should ideally do each of these in a separate thread */
    if (This->capture.pin.peer && !output_control_check_sample(&This->capture_control, inputSample))
        hrCapture = S_FALSE;
    EnterCriticalSection(&This->filter.filter_cs);
    if (This->capture.pin.peer && hrCapture == VFW_E_NOT_CONNECTED)
        hrCapture = copy_sample(inputSample, This->capture.pAllocator, FALSE, &captureSample);
    LeaveCriticalSection(&This->filter.filter_cs);
    if (captureSample && This->capture.pMemInputPin)
        hrCapture = IMemInputPin_Receive(This->capture.pMemInputPin, captureSample);
    else if (hrCapture == S_FALSE)
        hrCapture = S_OK;
    if (captureSample)
        IMediaSample_Release(captureSample);

    if (This->preview.pin.peer && !output_control_check_sample(&This->preview_control, NULL))
        hrPreview = S_FALSE;
    EnterCriticalSection(&This->filter.filter_cs);
    if (This->preview.pin.peer && hrPreview == VFW_E_NOT_CONNECTED)
        hrPreview = copy_sample(inputSample, This->preview.pAllocator, TRUE, &previewSample);
    LeaveCriticalSection(&This->filter.filter_cs);
    /* No timestamps on preview stream: */
    if (previewSample)
        hrPreview = IMediaSample_SetTime(previewSample, NULL, NULL);
    else if (hrPreview == S_FALSE)
        hrPreview = S_OK;
    if (previewSample && SUCCEEDED(hrPreview) && This->preview.pMemInputPin)
        hrPreview = IMemInputPin_Receive(This->preview.pMemInputPin, previewSample);
    if (previewSample)
        IMediaSample_Release(previewSample);

    if (hrCapture == VFW_E_NOT_CONNECTED && hrPreview == VFW_E_NOT_CONNECTED)
        return S_OK;

    /* FIXME: how to merge the HRESULTs from the 2 pins? */
    if (SUCCEEDED(hrCapture))
        return hrCapture;
    else
        return hrPreview;
}

static HRESULT sink_eos(struct strmbase_sink *iface)
{
    SmartTeeFilter *filter = impl_from_strmbase_pin(&iface->pin);
    HRESULT hr = S_OK;

    if (filter->sink.flushing || filter->filter.state == State_Stopped)
        return S_OK;
    if (filter->capture.pin.peer)
        hr = IPin_EndOfStream(filter->capture.pin.peer);
    if (filter->preview.pin.peer)
    {
        HRESULT preview_hr = IPin_EndOfStream(filter->preview.pin.peer);
        if (SUCCEEDED(hr))
            hr = preview_hr;
    }
    return hr;
}

static HRESULT sink_begin_flush(struct strmbase_sink *iface)
{
    SmartTeeFilter *filter = impl_from_strmbase_pin(&iface->pin);
    HRESULT hr = S_OK;

    output_control_set_flushing(&filter->capture_control, TRUE);
    output_control_set_flushing(&filter->preview_control, TRUE);
    if (filter->capture.pin.peer)
        hr = IPin_BeginFlush(filter->capture.pin.peer);
    if (filter->preview.pin.peer)
    {
        HRESULT preview_hr = IPin_BeginFlush(filter->preview.pin.peer);
        if (SUCCEEDED(hr))
            hr = preview_hr;
    }
    return hr;
}

static HRESULT sink_end_flush(struct strmbase_sink *iface)
{
    SmartTeeFilter *filter = impl_from_strmbase_pin(&iface->pin);
    HRESULT hr = S_OK;

    output_control_set_flushing(&filter->capture_control, FALSE);
    output_control_set_flushing(&filter->preview_control, FALSE);
    if (filter->capture.pin.peer)
        hr = IPin_EndFlush(filter->capture.pin.peer);
    if (filter->preview.pin.peer)
    {
        HRESULT preview_hr = IPin_EndFlush(filter->preview.pin.peer);
        if (SUCCEEDED(hr))
            hr = preview_hr;
    }
    return hr;
}

static const struct strmbase_sink_ops sink_ops =
{
    .base.pin_query_accept = sink_query_accept,
    .base.pin_query_interface = sink_query_interface,
    .pfnReceive = SmartTeeFilterInput_Receive,
    .sink_eos = sink_eos,
    .sink_begin_flush = sink_begin_flush,
    .sink_end_flush = sink_end_flush,
};

static inline struct output_control *impl_from_output_IQualityControl(IQualityControl *iface)
{
    return CONTAINING_RECORD(iface, struct output_control, IQualityControl_iface);
}

static HRESULT WINAPI source_quality_control_QueryInterface(IQualityControl *iface, REFIID iid, void **out)
{
    struct output_control *control = impl_from_output_IQualityControl(iface);
    return IPin_QueryInterface(&control->source->pin.IPin_iface, iid, out);
}

static ULONG WINAPI source_quality_control_AddRef(IQualityControl *iface)
{
    struct output_control *control = impl_from_output_IQualityControl(iface);
    return IPin_AddRef(&control->source->pin.IPin_iface);
}

static ULONG WINAPI source_quality_control_Release(IQualityControl *iface)
{
    struct output_control *control = impl_from_output_IQualityControl(iface);
    return IPin_Release(&control->source->pin.IPin_iface);
}

static HRESULT WINAPI source_quality_control_Notify(IQualityControl *iface, IBaseFilter *sender, Quality q)
{
    struct output_control *control = impl_from_output_IQualityControl(iface);

    TRACE("control %p, sender %p, type %#x, proportion %ld, late %s, timestamp %s.\n",
            control, sender, q.Type, q.Proportion,
            wine_dbgstr_longlong(q.Late), wine_dbgstr_longlong(q.TimeStamp));

    return E_NOTIMPL;
}

static HRESULT WINAPI source_quality_control_SetSink(IQualityControl *iface, IQualityControl *sink)
{
    struct output_control *control = impl_from_output_IQualityControl(iface);

    TRACE("control %p, sink %p.\n", control, sink);

    return S_OK;
}

static const IQualityControlVtbl source_quality_control_vtbl =
{
    source_quality_control_QueryInterface,
    source_quality_control_AddRef,
    source_quality_control_Release,
    source_quality_control_Notify,
    source_quality_control_SetSink,
};

static void output_control_init(struct output_control *control, struct strmbase_source *source)
{
    control->IQualityControl_iface.lpVtbl = &source_quality_control_vtbl;
    control->IAMStreamControl_iface.lpVtbl = &stream_control_vtbl;
    control->source = source;
    InitializeCriticalSection(&control->cs);
    control->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    control->state = State_Stopped;
    control->start = control->stop = MAXLONGLONG;
}

static void output_control_cleanup(struct output_control *control)
{
    CloseHandle(control->event);
    DeleteCriticalSection(&control->cs);
}

static struct output_control *output_control_from_source(struct strmbase_pin *iface)
{
    SmartTeeFilter *filter = impl_from_strmbase_pin(iface);

    if (iface == &filter->capture.pin)
        return &filter->capture_control;
    return &filter->preview_control;
}

static HRESULT source_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    struct output_control *control = output_control_from_source(iface);

    if (IsEqualGUID(iid, &IID_IQualityControl))
        *out = &control->IQualityControl_iface;
    else if (IsEqualGUID(iid, &IID_IAMStreamControl))
        *out = &control->IAMStreamControl_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT source_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    SmartTeeFilter *filter = impl_from_strmbase_pin(iface);
    const AM_MEDIA_TYPE *input_mt = &filter->sink.pin.mt;
    IPin *peer;
    HRESULT hr;

    EnterCriticalSection(&filter->filter.filter_cs);
    if ((peer = filter->sink.pin.peer))
        IPin_AddRef(peer);
    if (peer && IsEqualGUID(&mt->majortype, &input_mt->majortype)
            && IsEqualGUID(&mt->subtype, &input_mt->subtype)
            && IsEqualGUID(&mt->formattype, &input_mt->formattype)
            && mt->cbFormat == input_mt->cbFormat
            && (!mt->cbFormat || !memcmp(mt->pbFormat, input_mt->pbFormat, mt->cbFormat)))
    {
        LeaveCriticalSection(&filter->filter.filter_cs);
        IPin_Release(peer);
        return S_OK;
    }
    LeaveCriticalSection(&filter->filter.filter_cs);

    if (!peer)
        return S_FALSE;
    hr = IPin_QueryAccept(peer, mt) == S_OK ? S_OK : S_FALSE;
    IPin_Release(peer);
    return hr;
}

static HRESULT source_get_media_type(struct strmbase_pin *iface,
        unsigned int index, AM_MEDIA_TYPE *mt)
{
    SmartTeeFilter *filter = impl_from_strmbase_pin(iface);
    IEnumMediaTypes *enummt;
    AM_MEDIA_TYPE *peer_mt;
    IPin *peer;
    HRESULT hr;

    EnterCriticalSection(&filter->filter.filter_cs);
    if (!(peer = filter->sink.pin.peer))
    {
        LeaveCriticalSection(&filter->filter.filter_cs);
        return VFW_E_NOT_CONNECTED;
    }
    if (!index)
    {
        CopyMediaType(mt, &filter->sink.pin.mt);
        LeaveCriticalSection(&filter->filter.filter_cs);
        return S_OK;
    }
    IPin_AddRef(peer);
    LeaveCriticalSection(&filter->filter.filter_cs);

    hr = IPin_EnumMediaTypes(peer, &enummt);
    IPin_Release(peer);
    if (FAILED(hr))
        return VFW_S_NO_MORE_ITEMS;
    if (index > 1)
        hr = IEnumMediaTypes_Skip(enummt, index - 1);
    if (hr == S_OK && IEnumMediaTypes_Next(enummt, 1, &peer_mt, NULL) == S_OK)
    {
        *mt = *peer_mt;
        CoTaskMemFree(peer_mt);
        hr = S_OK;
    }
    else
        hr = VFW_S_NO_MORE_ITEMS;
    IEnumMediaTypes_Release(enummt);
    return hr;
}

static HRESULT WINAPI SmartTeeFilterCapture_DecideAllocator(struct strmbase_source *base,
        IMemInputPin *pPin, IMemAllocator **pAlloc)
{
    SmartTeeFilter *This = impl_from_strmbase_pin(&base->pin);
    TRACE("(%p, %p, %p)\n", This, pPin, pAlloc);
    *pAlloc = This->sink.pAllocator;
    IMemAllocator_AddRef(This->sink.pAllocator);
    return IMemInputPin_NotifyAllocator(pPin, This->sink.pAllocator, TRUE);
}

static const struct strmbase_source_ops capture_ops =
{
    .base.pin_query_accept = source_query_accept,
    .base.pin_get_media_type = source_get_media_type,
    .base.pin_query_interface = source_query_interface,
    .pfnAttemptConnection = BaseOutputPinImpl_AttemptConnection,
    .pfnDecideAllocator = SmartTeeFilterCapture_DecideAllocator,
};

static HRESULT WINAPI SmartTeeFilterPreview_DecideAllocator(struct strmbase_source *base,
        IMemInputPin *pPin, IMemAllocator **pAlloc)
{
    SmartTeeFilter *This = impl_from_strmbase_pin(&base->pin);
    TRACE("(%p, %p, %p)\n", This, pPin, pAlloc);
    *pAlloc = This->sink.pAllocator;
    IMemAllocator_AddRef(This->sink.pAllocator);
    return IMemInputPin_NotifyAllocator(pPin, This->sink.pAllocator, TRUE);
}

static const struct strmbase_source_ops preview_ops =
{
    .base.pin_query_accept = source_query_accept,
    .base.pin_get_media_type = source_get_media_type,
    .base.pin_query_interface = source_query_interface,
    .pfnAttemptConnection = BaseOutputPinImpl_AttemptConnection,
    .pfnDecideAllocator = SmartTeeFilterPreview_DecideAllocator,
};

HRESULT smart_tee_create(IUnknown *outer, IUnknown **out)
{
    SmartTeeFilter *object;
    HRESULT hr;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    strmbase_filter_init(&object->filter, outer, &CLSID_SmartTee, &filter_ops);
    strmbase_sink_init(&object->sink, &object->filter, L"Input", &sink_ops, NULL);
    hr = CoCreateInstance(&CLSID_MemoryAllocator, NULL, CLSCTX_INPROC_SERVER,
            &IID_IMemAllocator, (void **)&object->sink.pAllocator);
    if (FAILED(hr))
    {
        strmbase_filter_cleanup(&object->filter);
        free(object);
        return hr;
    }

    strmbase_source_init(&object->capture, &object->filter, L"Capture", &capture_ops);
    strmbase_source_init(&object->preview, &object->filter, L"Preview", &preview_ops);
    object->IQualityControl_iface.lpVtbl = &sink_quality_control_vtbl;
    output_control_init(&object->capture_control, &object->capture);
    output_control_init(&object->preview_control, &object->preview);

    TRACE("Created smart tee %p.\n", object);
    *out = &object->filter.IUnknown_inner;
    return S_OK;
}
