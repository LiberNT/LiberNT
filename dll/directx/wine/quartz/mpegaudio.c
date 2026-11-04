/*
 * PROJECT:     LiberNT DirectShow
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Native MPEG audio decoder filter
 * COPYRIGHT:   Copyright 2022 Anton Baskanov
 *              Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "quartz_private.h"

#include "mmreg.h"
#include "mpegtype.h"
#include <mpg123.h>

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

struct mpeg_audio_codec
{
    struct strmbase_filter filter;
    IMpegAudioDecoder IMpegAudioDecoder_iface;

    struct strmbase_sink sink;
    struct strmbase_source source;
    struct strmbase_passthrough passthrough;

    IQualityControl sink_IQualityControl_iface;
    IQualityControl source_IQualityControl_iface;
    IQualityControl *qc_sink;

    mpg123_handle *decoder;
    REFERENCE_TIME next_time;
    REFERENCE_TIME pending_time;
    BOOL pending_time_valid;
    BOOL discontinuity;
};

static inline struct mpeg_audio_codec *impl_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_audio_codec, filter);
}

static struct strmbase_pin *mpeg_audio_codec_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(iface);
    if (index == 0)
        return &filter->sink.pin;
    if (index == 1)
        return &filter->source.pin;
    return NULL;
}

static void mpeg_audio_codec_destroy(struct strmbase_filter *iface)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(iface);

    strmbase_passthrough_cleanup(&filter->passthrough);
    strmbase_source_cleanup(&filter->source);
    strmbase_sink_cleanup(&filter->sink);
    strmbase_filter_cleanup(&filter->filter);

    free(filter);
}

static HRESULT mpeg_audio_codec_query_interface(struct strmbase_filter *iface, REFIID iid, void **out)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(iface);

    if (IsEqualGUID(iid, &IID_IMpegAudioDecoder))
        *out = &filter->IMpegAudioDecoder_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static void mpeg_audio_codec_reset(struct mpeg_audio_codec *filter)
{
    mpg123_close(filter->decoder);
    mpg123_open_feed(filter->decoder);
    filter->next_time = 0;
    filter->pending_time_valid = FALSE;
    filter->discontinuity = TRUE;
}

static HRESULT mpeg_audio_codec_init_stream(struct strmbase_filter *iface)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(iface);
    const WAVEFORMATEX *format;
    HRESULT hr;
    int ret;

    if (!filter->source.pin.peer)
        return S_OK;

    format = (const WAVEFORMATEX *)filter->source.pin.mt.pbFormat;

    if (mpg123_init() != MPG123_OK || !(filter->decoder = mpg123_new(NULL, &ret)))
        return E_OUTOFMEMORY;

    mpg123_param(filter->decoder, MPG123_ADD_FLAGS, MPG123_QUIET | MPG123_IGNORE_INFOFRAME | MPG123_NO_READAHEAD, 0);
    mpg123_param(filter->decoder, MPG123_REMOVE_FLAGS, MPG123_GAPLESS, 0);
    mpg123_format_none(filter->decoder);
    if (mpg123_format(filter->decoder, format->nSamplesPerSec,
            format->nChannels == 1 ? MPG123_MONO : MPG123_STEREO,
            format->wBitsPerSample == 8 ? MPG123_ENC_UNSIGNED_8 : MPG123_ENC_SIGNED_16) != MPG123_OK
            || mpg123_open_feed(filter->decoder) != MPG123_OK)
    {
        mpg123_delete(filter->decoder);
        filter->decoder = NULL;
        return E_FAIL;
    }

    filter->next_time = 0;
    filter->pending_time_valid = FALSE;
    filter->discontinuity = TRUE;

    if (FAILED(hr = IMemAllocator_Commit(filter->source.pAllocator)))
        ERR("Failed to commit allocator, hr %#lx.\n", hr);

    return S_OK;
}

static HRESULT mpeg_audio_codec_cleanup_stream(struct strmbase_filter *iface)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(iface);

    if (filter->source.pin.peer)
    {
        IMemAllocator_Decommit(filter->source.pAllocator);

        EnterCriticalSection(&filter->filter.stream_cs);
        if (filter->decoder)
        {
            mpg123_delete(filter->decoder);
            filter->decoder = NULL;
        }
        LeaveCriticalSection(&filter->filter.stream_cs);
    }

    return S_OK;
}

static const struct strmbase_filter_ops filter_ops =
{
    .filter_get_pin = mpeg_audio_codec_get_pin,
    .filter_destroy = mpeg_audio_codec_destroy,
    .filter_query_interface = mpeg_audio_codec_query_interface,
    .filter_init_stream = mpeg_audio_codec_init_stream,
    .filter_cleanup_stream = mpeg_audio_codec_cleanup_stream,
};

static struct mpeg_audio_codec *impl_from_IMpegAudioDecoder(IMpegAudioDecoder *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_audio_codec, IMpegAudioDecoder_iface);
}

static HRESULT WINAPI mpeg_audio_decoder_QueryInterface(IMpegAudioDecoder *iface,
        REFIID iid, void **out)
{
    struct mpeg_audio_codec *filter = impl_from_IMpegAudioDecoder(iface);
    return IUnknown_QueryInterface(filter->filter.outer_unk, iid, out);
}

static ULONG WINAPI mpeg_audio_decoder_AddRef(IMpegAudioDecoder *iface)
{
    struct mpeg_audio_codec *filter = impl_from_IMpegAudioDecoder(iface);
    return IUnknown_AddRef(filter->filter.outer_unk);
}

static ULONG WINAPI mpeg_audio_decoder_Release(IMpegAudioDecoder *iface)
{
    struct mpeg_audio_codec *filter = impl_from_IMpegAudioDecoder(iface);
    return IUnknown_Release(filter->filter.outer_unk);
}

static HRESULT WINAPI mpeg_audio_decoder_get_FrequencyDivider(IMpegAudioDecoder *iface, ULONG *divider)
{
    FIXME("iface %p, divider %p, stub!\n", iface, divider);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_put_FrequencyDivider(IMpegAudioDecoder *iface, ULONG divider)
{
    FIXME("iface %p, divider %lu, stub!\n", iface, divider);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_get_DecoderAccuracy(IMpegAudioDecoder *iface, ULONG *accuracy)
{
    FIXME("iface %p, accuracy %p, stub!\n", iface, accuracy);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_put_DecoderAccuracy(IMpegAudioDecoder *iface, ULONG accuracy)
{
    FIXME("iface %p, accuracy %lu, stub!\n", iface, accuracy);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_get_Stereo(IMpegAudioDecoder *iface, ULONG *stereo)
{
    FIXME("iface %p, stereo %p, stub!\n", iface, stereo);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_put_Stereo(IMpegAudioDecoder *iface, ULONG stereo)
{
    FIXME("iface %p, stereo %lu, stub!\n", iface, stereo);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_get_DecoderWordSize(IMpegAudioDecoder *iface, ULONG *word_size)
{
    FIXME("iface %p, word_size %p, stub!\n", iface, word_size);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_put_DecoderWordSize(IMpegAudioDecoder *iface, ULONG word_size)
{
    FIXME("iface %p, word_size %lu, stub!\n", iface, word_size);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_get_IntegerDecode(IMpegAudioDecoder *iface, ULONG *integer_decode)
{
    FIXME("iface %p, integer_decode %p, stub!\n", iface, integer_decode);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_put_IntegerDecode(IMpegAudioDecoder *iface, ULONG integer_decode)
{
    FIXME("iface %p, integer_decode %lu, stub!\n", iface, integer_decode);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_get_DualMode(IMpegAudioDecoder *iface, ULONG *dual_mode)
{
    FIXME("iface %p, dual_mode %p, stub!\n", iface, dual_mode);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_put_DualMode(IMpegAudioDecoder *iface, ULONG dual_mode)
{
    FIXME("iface %p, dual_mode %lu, stub!\n", iface, dual_mode);
    return E_NOTIMPL;
}

static HRESULT WINAPI mpeg_audio_decoder_get_AudioFormat(IMpegAudioDecoder *iface, MPEG1WAVEFORMAT *format)
{
    FIXME("iface %p, format %p, stub!\n", iface, format);
    return E_NOTIMPL;
}

static const IMpegAudioDecoderVtbl mpeg_audio_decoder_vtbl =
{
    mpeg_audio_decoder_QueryInterface,
    mpeg_audio_decoder_AddRef,
    mpeg_audio_decoder_Release,
    mpeg_audio_decoder_get_FrequencyDivider,
    mpeg_audio_decoder_put_FrequencyDivider,
    mpeg_audio_decoder_get_DecoderAccuracy,
    mpeg_audio_decoder_put_DecoderAccuracy,
    mpeg_audio_decoder_get_Stereo,
    mpeg_audio_decoder_put_Stereo,
    mpeg_audio_decoder_get_DecoderWordSize,
    mpeg_audio_decoder_put_DecoderWordSize,
    mpeg_audio_decoder_get_IntegerDecode,
    mpeg_audio_decoder_put_IntegerDecode,
    mpeg_audio_decoder_get_DualMode,
    mpeg_audio_decoder_put_DualMode,
    mpeg_audio_decoder_get_AudioFormat,
};

static HRESULT mpeg_audio_codec_sink_query_accept(struct strmbase_pin *pin, const AM_MEDIA_TYPE *mt)
{
    const MPEG1WAVEFORMAT *format;

    if (!IsEqualGUID(&mt->majortype, &MEDIATYPE_Audio))
        return S_FALSE;

    if (!IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_MPEG1Packet)
            && !IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_MPEG1Payload)
            && !IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_MPEG1AudioPayload)
            && !IsEqualGUID(&mt->subtype, &GUID_NULL))
        return S_FALSE;

    if (!IsEqualGUID(&mt->formattype, &FORMAT_WaveFormatEx)
            || mt->cbFormat < sizeof(MPEG1WAVEFORMAT))
        return S_FALSE;

    format = (const MPEG1WAVEFORMAT *)mt->pbFormat;

    if (format->wfx.wFormatTag != WAVE_FORMAT_MPEG
            || format->fwHeadLayer == ACM_MPEG_LAYER3)
        return S_FALSE;

    return S_OK;
}

static HRESULT mpeg_audio_codec_sink_query_interface(struct strmbase_pin *pin, REFIID iid, void **out)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(pin->filter);

    if (IsEqualGUID(iid, &IID_IMemInputPin))
        *out = &filter->sink.IMemInputPin_iface;
    else if (IsEqualGUID(iid, &IID_IQualityControl))
        *out = &filter->sink_IQualityControl_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT mpeg_audio_codec_deliver(struct mpeg_audio_codec *filter, const BYTE *data, size_t size)
{
    const WAVEFORMATEX *format = (const WAVEFORMATEX *)filter->source.pin.mt.pbFormat;
    REFERENCE_TIME start, stop;
    IMediaSample *sample;
    LONG length;
    BYTE *ptr;
    HRESULT hr;

    while (size)
    {
        if (FAILED(hr = IMemAllocator_GetBuffer(filter->source.pAllocator, &sample, NULL, NULL, 0)))
            return hr;

        length = min(IMediaSample_GetSize(sample), size);
        length -= length % format->nBlockAlign;
        if (!length)
        {
            IMediaSample_Release(sample);
            return E_FAIL;
        }

        IMediaSample_GetPointer(sample, &ptr);
        memcpy(ptr, data, length);
        IMediaSample_SetActualDataLength(sample, length);

        if (filter->pending_time_valid)
        {
            filter->next_time = filter->pending_time;
            filter->pending_time_valid = FALSE;
        }
        start = filter->next_time;
        stop = start + (REFERENCE_TIME)(length / format->nBlockAlign) * 10000000 / format->nSamplesPerSec;
        filter->next_time = stop;

        IMediaSample_SetTime(sample, &start, &stop);
        IMediaSample_SetMediaTime(sample, NULL, NULL);
        IMediaSample_SetSyncPoint(sample, TRUE);
        IMediaSample_SetDiscontinuity(sample, filter->discontinuity);
        filter->discontinuity = FALSE;

        hr = IMemInputPin_Receive(filter->source.pMemInputPin, sample);
        IMediaSample_Release(sample);
        if (hr != S_OK)
            return hr;

        data += length;
        size -= length;
    }

    return S_OK;
}

static HRESULT WINAPI mpeg_audio_codec_sink_receive(struct strmbase_sink *pin, IMediaSample *sample)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(pin->pin.filter);
    REFERENCE_TIME start, stop;
    unsigned char *audio;
    size_t bytes;
    off_t frame;
    BYTE *data;
    HRESULT hr;
    int ret;

    if (!filter->source.pMemInputPin)
        return VFW_E_NOT_CONNECTED;

    if (filter->filter.state == State_Stopped)
        return VFW_E_WRONG_STATE;

    if (filter->sink.flushing)
        return S_FALSE;

    if (IMediaSample_IsDiscontinuity(sample) == S_OK)
        filter->discontinuity = TRUE;

    if (SUCCEEDED(IMediaSample_GetTime(sample, &start, &stop)))
    {
        filter->pending_time = start;
        filter->pending_time_valid = TRUE;
    }

    IMediaSample_GetPointer(sample, &data);
    if (mpg123_feed(filter->decoder, data, IMediaSample_GetActualDataLength(sample)) != MPG123_OK)
        return E_OUTOFMEMORY;

    for (;;)
    {
        ret = mpg123_decode_frame(filter->decoder, &frame, &audio, &bytes);
        if (ret == MPG123_NEW_FORMAT)
            continue;
        if (ret == MPG123_NEED_MORE)
            break;
        if (ret != MPG123_OK)
        {
            WARN("Failed to decode a frame, error %d.\n", ret);
            break;
        }
        if (bytes && (hr = mpeg_audio_codec_deliver(filter, audio, bytes)) != S_OK)
            return hr;
    }

    return S_OK;
}

static HRESULT mpeg_audio_codec_sink_end_flush(struct strmbase_sink *pin)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(pin->pin.filter);

    EnterCriticalSection(&filter->filter.stream_cs);
    if (filter->decoder)
        mpeg_audio_codec_reset(filter);
    LeaveCriticalSection(&filter->filter.stream_cs);

    if (filter->source.pin.peer)
        return IPin_EndFlush(filter->source.pin.peer);
    return S_OK;
}

static const struct strmbase_sink_ops sink_ops =
{
    .base.pin_query_accept = mpeg_audio_codec_sink_query_accept,
    .base.pin_query_interface = mpeg_audio_codec_sink_query_interface,
    .pfnReceive = mpeg_audio_codec_sink_receive,
    .sink_end_flush = mpeg_audio_codec_sink_end_flush,
};

static HRESULT mpeg_audio_codec_source_query_accept(struct strmbase_pin *pin, const AM_MEDIA_TYPE *mt)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(pin->filter);
    const MPEG1WAVEFORMAT *input_format;
    const WAVEFORMATEX *output_format;
    DWORD expected_avg_bytes_per_sec;
    WORD expected_block_align;

    if (!filter->sink.pin.peer)
        return S_FALSE;

    if (!IsEqualGUID(&mt->majortype, &MEDIATYPE_Audio)
            || !IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_PCM)
            || !IsEqualGUID(&mt->formattype, &FORMAT_WaveFormatEx)
            || mt->cbFormat < sizeof(WAVEFORMATEX))
        return S_FALSE;

    input_format = (const MPEG1WAVEFORMAT *)filter->sink.pin.mt.pbFormat;
    output_format = (const WAVEFORMATEX *)mt->pbFormat;

    if (output_format->wFormatTag != WAVE_FORMAT_PCM
            || input_format->wfx.nSamplesPerSec != output_format->nSamplesPerSec
            || input_format->wfx.nChannels != output_format->nChannels
            || (output_format->wBitsPerSample != 8 && output_format->wBitsPerSample != 16))
        return S_FALSE;

    expected_block_align = output_format->nChannels * output_format->wBitsPerSample / 8;
    expected_avg_bytes_per_sec = expected_block_align * output_format->nSamplesPerSec;

    if (output_format->nBlockAlign != expected_block_align
            || output_format->nAvgBytesPerSec != expected_avg_bytes_per_sec)
        return S_FALSE;

    return S_OK;
}

static HRESULT mpeg_audio_codec_source_get_media_type(struct strmbase_pin *pin, unsigned int index, AM_MEDIA_TYPE *mt)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(pin->filter);
    const MPEG1WAVEFORMAT *input_format;
    WAVEFORMATEX *output_format;

    if (!filter->sink.pin.peer)
        return VFW_S_NO_MORE_ITEMS;

    if (index > 1)
        return VFW_S_NO_MORE_ITEMS;

    input_format = (const MPEG1WAVEFORMAT *)filter->sink.pin.mt.pbFormat;

    output_format = CoTaskMemAlloc(sizeof(*output_format));
    if (!output_format)
        return E_OUTOFMEMORY;

    memset(output_format, 0, sizeof(*output_format));
    output_format->wFormatTag = WAVE_FORMAT_PCM;
    output_format->nSamplesPerSec = input_format->wfx.nSamplesPerSec;
    output_format->nChannels = input_format->wfx.nChannels;
    output_format->wBitsPerSample = index ? 8 : 16;
    output_format->nBlockAlign = output_format->nChannels * output_format->wBitsPerSample / 8;
    output_format->nAvgBytesPerSec = output_format->nBlockAlign * output_format->nSamplesPerSec;

    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Audio;
    mt->subtype = MEDIASUBTYPE_PCM;
    mt->bFixedSizeSamples = TRUE;
    mt->lSampleSize = output_format->nBlockAlign;
    mt->formattype = FORMAT_WaveFormatEx;
    mt->cbFormat = sizeof(*output_format);
    mt->pbFormat = (BYTE *)output_format;

    return S_OK;
}

static HRESULT mpeg_audio_codec_source_query_interface(struct strmbase_pin *pin, REFIID iid, void **out)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(pin->filter);

    if (IsEqualGUID(iid, &IID_IMediaPosition))
        *out = &filter->passthrough.IMediaPosition_iface;
    else if (IsEqualGUID(iid, &IID_IMediaSeeking))
        *out = &filter->passthrough.IMediaSeeking_iface;
    else if (IsEqualGUID(iid, &IID_IQualityControl))
        *out = &filter->source_IQualityControl_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT WINAPI mpeg_audio_codec_source_DecideBufferSize(struct strmbase_source *pin,
        IMemAllocator *allocator, ALLOCATOR_PROPERTIES *props)
{
    struct mpeg_audio_codec *filter = impl_from_strmbase_filter(pin->pin.filter);
    const MPEG1WAVEFORMAT *input_format = (const MPEG1WAVEFORMAT *)filter->sink.pin.mt.pbFormat;
    const WAVEFORMATEX *output_format = (const WAVEFORMATEX *)filter->source.pin.mt.pbFormat;
    LONG frame_samples = (input_format->fwHeadLayer & ACM_MPEG_LAYER2) ? 1152 : 384;
    LONG frame_size = frame_samples * output_format->nBlockAlign;
    ALLOCATOR_PROPERTIES ret_props;

    props->cBuffers = max(props->cBuffers, 8);
    props->cbBuffer = max(props->cbBuffer, frame_size * 4);
    props->cbAlign = max(props->cbAlign, 1);

    return IMemAllocator_SetProperties(allocator, props, &ret_props);
}

static const struct strmbase_source_ops source_ops =
{
    .base.pin_query_accept = mpeg_audio_codec_source_query_accept,
    .base.pin_get_media_type = mpeg_audio_codec_source_get_media_type,
    .base.pin_query_interface = mpeg_audio_codec_source_query_interface,
    .pfnAttemptConnection = BaseOutputPinImpl_AttemptConnection,
    .pfnDecideAllocator = BaseOutputPinImpl_DecideAllocator,
    .pfnDecideBufferSize = mpeg_audio_codec_source_DecideBufferSize,
};

static struct mpeg_audio_codec *impl_from_sink_IQualityControl(IQualityControl *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_audio_codec, sink_IQualityControl_iface);
}

static HRESULT WINAPI sink_quality_control_QueryInterface(IQualityControl *iface, REFIID iid, void **out)
{
    struct mpeg_audio_codec *filter = impl_from_sink_IQualityControl(iface);
    return IPin_QueryInterface(&filter->sink.pin.IPin_iface, iid, out);
}

static ULONG WINAPI sink_quality_control_AddRef(IQualityControl *iface)
{
    struct mpeg_audio_codec *filter = impl_from_sink_IQualityControl(iface);
    return IPin_AddRef(&filter->sink.pin.IPin_iface);
}

static ULONG WINAPI sink_quality_control_Release(IQualityControl *iface)
{
    struct mpeg_audio_codec *filter = impl_from_sink_IQualityControl(iface);
    return IPin_Release(&filter->sink.pin.IPin_iface);
}

static HRESULT WINAPI sink_quality_control_Notify(IQualityControl *iface, IBaseFilter *sender, Quality q)
{
    struct mpeg_audio_codec *filter = impl_from_sink_IQualityControl(iface);

    TRACE("filter %p, sender %p, type %#x, proportion %ld, late %s, timestamp %s.\n",
            filter, sender, q.Type, q.Proportion, debugstr_time(q.Late), debugstr_time(q.TimeStamp));

    return S_OK;
}

static HRESULT WINAPI sink_quality_control_SetSink(IQualityControl *iface, IQualityControl *sink)
{
    struct mpeg_audio_codec *filter = impl_from_sink_IQualityControl(iface);

    TRACE("filter %p, sink %p.\n", filter, sink);

    filter->qc_sink = sink;

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

static struct mpeg_audio_codec *impl_from_source_IQualityControl(IQualityControl *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_audio_codec, source_IQualityControl_iface);
}

static HRESULT WINAPI source_quality_control_QueryInterface(IQualityControl *iface, REFIID iid, void **out)
{
    struct mpeg_audio_codec *filter = impl_from_source_IQualityControl(iface);
    return IPin_QueryInterface(&filter->source.pin.IPin_iface, iid, out);
}

static ULONG WINAPI source_quality_control_AddRef(IQualityControl *iface)
{
    struct mpeg_audio_codec *filter = impl_from_source_IQualityControl(iface);
    return IPin_AddRef(&filter->source.pin.IPin_iface);
}

static ULONG WINAPI source_quality_control_Release(IQualityControl *iface)
{
    struct mpeg_audio_codec *filter = impl_from_source_IQualityControl(iface);
    return IPin_Release(&filter->source.pin.IPin_iface);
}

static HRESULT WINAPI source_quality_control_Notify(IQualityControl *iface, IBaseFilter *sender, Quality q)
{
    struct mpeg_audio_codec *filter = impl_from_source_IQualityControl(iface);
    IQualityControl *peer;
    HRESULT hr = VFW_E_NOT_FOUND;

    TRACE("filter %p, sender %p, type %s, proportion %ld, late %s, timestamp %s.\n",
            filter, sender, q.Type == Famine ? "Famine" : "Flood", q.Proportion,
            debugstr_time(q.Late), debugstr_time(q.TimeStamp));

    if (filter->qc_sink)
        return IQualityControl_Notify(filter->qc_sink, &filter->filter.IBaseFilter_iface, q);

    if (filter->sink.pin.peer
            && SUCCEEDED(IPin_QueryInterface(filter->sink.pin.peer, &IID_IQualityControl, (void **)&peer)))
    {
        hr = IQualityControl_Notify(peer, &filter->filter.IBaseFilter_iface, q);
        IQualityControl_Release(peer);
    }

    return hr;
}

static HRESULT WINAPI source_quality_control_SetSink(IQualityControl *iface, IQualityControl *sink)
{
    struct mpeg_audio_codec *filter = impl_from_source_IQualityControl(iface);

    TRACE("filter %p, sink %p.\n", filter, sink);

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

HRESULT mpeg_audio_codec_create(IUnknown *outer, IUnknown **out)
{
    struct mpeg_audio_codec *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    strmbase_filter_init(&object->filter, outer, &CLSID_CMpegAudioCodec, &filter_ops);
    strmbase_sink_init(&object->sink, &object->filter, L"In", &sink_ops, NULL);
    strmbase_source_init(&object->source, &object->filter, L"Out", &source_ops);
    wcscpy(object->sink.pin.name, L"XForm In");
    wcscpy(object->source.pin.name, L"XForm Out");

    strmbase_passthrough_init(&object->passthrough, (IUnknown *)&object->source.pin.IPin_iface);
    ISeekingPassThru_Init(&object->passthrough.ISeekingPassThru_iface, FALSE,
            &object->sink.pin.IPin_iface);

    object->IMpegAudioDecoder_iface.lpVtbl = &mpeg_audio_decoder_vtbl;
    object->sink_IQualityControl_iface.lpVtbl = &sink_quality_control_vtbl;
    object->source_IQualityControl_iface.lpVtbl = &source_quality_control_vtbl;

    TRACE("Created MPEG audio decoder %p.\n", object);
    *out = &object->filter.IUnknown_inner;
    return S_OK;
}
