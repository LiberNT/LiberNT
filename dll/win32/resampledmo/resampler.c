/*
 * PROJECT:     LiberNT Audio Resampler DSP
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Resampler object: IMFTransform, IMediaObject and its property interfaces
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "resampledmo_private.h"

#include <stdlib.h>
#include <string.h>

#include "mfapi.h"
#include "mferror.h"
#include "mfobjects.h"
#include "mftransform.h"
#include "mediaobj.h"
#include "mediaerr.h"
#include "dmort.h"
#include "propsys.h"
#include "uuids.h"
#include "wmcodecdsp.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(resampler);

#include "initguid.h"
#include "propkeydef.h"

DEFINE_PROPERTYKEY(MFPKEY_WMRESAMP_FILTERQUALITY, 0xaf1adc73, 0xa210, 0x4b05, 0x96, 0x6e, 0x54, 0x91, 0xcf, 0xf4, 0x8b, 0x1d, 1);
DEFINE_PROPERTYKEY(MFPKEY_WMRESAMP_CHANNELMTX, 0xaf1adc73, 0xa210, 0x4b05, 0x96, 0x6e, 0x54, 0x91, 0xcf, 0xf4, 0x8b, 0x1d, 2);
DEFINE_PROPERTYKEY(MFPKEY_WMRESAMP_LOWPASS_BANDWIDTH, 0xaf1adc73, 0xa210, 0x4b05, 0x96, 0x6e, 0x54, 0x91, 0xcf, 0xf4, 0x8b, 0x1d, 3);

static const PROPERTYKEY *const resampler_property_keys[] =
{
    &MFPKEY_WMRESAMP_FILTERQUALITY,
    &MFPKEY_WMRESAMP_CHANNELMTX,
    &MFPKEY_WMRESAMP_LOWPASS_BANDWIDTH,
};

static const GUID *const resampler_subtypes[] =
{
    &MFAudioFormat_Float,
    &MFAudioFormat_PCM,
};

struct resampler
{
    IUnknown IUnknown_inner;
    IMFTransform IMFTransform_iface;
    IMediaObject IMediaObject_iface;
    IPropertyStore IPropertyStore_iface;
    IPropertyBag IPropertyBag_iface;
    IWMResamplerProps IWMResamplerProps_iface;
    IUnknown *outer;
    LONG refcount;

    CRITICAL_SECTION cs;
    WAVEFORMATEX *input_format;
    UINT32 input_size;
    WAVEFORMATEX *output_format;
    UINT32 output_size;

    LONG quality;
    float lowpass_bandwidth;
    PROPVARIANT channel_matrix;

    struct resampler_dsp dsp;
    BOOL dsp_ready;
    BOOL draining;
    BOOL time_valid;
    LONGLONG next_time;
};

static inline struct resampler *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct resampler, IUnknown_inner);
}

static inline struct resampler *impl_from_IMFTransform(IMFTransform *iface)
{
    return CONTAINING_RECORD(iface, struct resampler, IMFTransform_iface);
}

static inline struct resampler *impl_from_IMediaObject(IMediaObject *iface)
{
    return CONTAINING_RECORD(iface, struct resampler, IMediaObject_iface);
}

static inline struct resampler *impl_from_IPropertyStore(IPropertyStore *iface)
{
    return CONTAINING_RECORD(iface, struct resampler, IPropertyStore_iface);
}

static inline struct resampler *impl_from_IPropertyBag(IPropertyBag *iface)
{
    return CONTAINING_RECORD(iface, struct resampler, IPropertyBag_iface);
}

static inline struct resampler *impl_from_IWMResamplerProps(IWMResamplerProps *iface)
{
    return CONTAINING_RECORD(iface, struct resampler, IWMResamplerProps_iface);
}

static DWORD default_channel_mask(UINT channels)
{
    switch (channels)
    {
        case 1: return KSAUDIO_SPEAKER_MONO;
        case 2: return KSAUDIO_SPEAKER_STEREO;
        case 4: return KSAUDIO_SPEAKER_QUAD;
        case 6: return KSAUDIO_SPEAKER_5POINT1;
        case 8: return KSAUDIO_SPEAKER_7POINT1_SURROUND;
    }
    return 0;
}

static HRESULT wave_format_from_media_type(IMFMediaType *type, WAVEFORMATEX **out, UINT32 *out_size)
{
    UINT32 channels, bits, rate, block, avg, value;
    WAVEFORMATEXTENSIBLE *ext;
    WAVEFORMATEX *format;
    BOOL extensible;
    GUID major, subtype;
    UINT32 size;

    if (FAILED(IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major)) || !IsEqualGUID(&major, &MFMediaType_Audio))
        return MF_E_INVALIDMEDIATYPE;
    if (FAILED(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype))
            || (!IsEqualGUID(&subtype, &MFAudioFormat_PCM) && !IsEqualGUID(&subtype, &MFAudioFormat_Float)))
        return MF_E_INVALIDMEDIATYPE;
    if (FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, &channels))
            || FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits))
            || FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate))
            || FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_BLOCK_ALIGNMENT, &block))
            || FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &avg)))
        return MF_E_INVALIDMEDIATYPE;
    if (!channels || channels > 0xffff || bits > 0xffff || block > 0xffff)
        return MF_E_INVALIDMEDIATYPE;

    extensible = bits > 16 || channels > 2;
    size = extensible ? sizeof(WAVEFORMATEXTENSIBLE) : sizeof(WAVEFORMATEX);
    if (!(format = CoTaskMemAlloc(size)))
        return E_OUTOFMEMORY;
    memset(format, 0, size);
    format->wFormatTag = IsEqualGUID(&subtype, &MFAudioFormat_Float) ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
    format->nChannels = channels;
    format->nSamplesPerSec = rate;
    format->nAvgBytesPerSec = avg;
    format->nBlockAlign = block;
    format->wBitsPerSample = bits;
    if (extensible)
    {
        ext = (WAVEFORMATEXTENSIBLE *)format;
        ext->SubFormat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
        format->wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        format->cbSize = sizeof(*ext) - sizeof(*format);
        if (SUCCEEDED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_VALID_BITS_PER_SAMPLE, &value)))
            ext->Samples.wValidBitsPerSample = value;
        if (SUCCEEDED(IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_CHANNEL_MASK, &value)))
            ext->dwChannelMask = value;
        else
            ext->dwChannelMask = default_channel_mask(channels);
    }

    if (!resampler_wave_format_valid(format, size))
    {
        CoTaskMemFree(format);
        return MF_E_INVALIDMEDIATYPE;
    }
    *out = format;
    *out_size = size;
    return S_OK;
}

static HRESULT wave_format_from_dmo_type(const DMO_MEDIA_TYPE *type, WAVEFORMATEX **out, UINT32 *out_size)
{
    WAVEFORMATEX *format;

    if (!IsEqualGUID(&type->majortype, &MEDIATYPE_Audio) || !IsEqualGUID(&type->formattype, &FORMAT_WaveFormatEx)
            || !type->pbFormat || !resampler_wave_format_valid((WAVEFORMATEX *)type->pbFormat, type->cbFormat))
        return DMO_E_TYPE_NOT_ACCEPTED;
    if (!(format = CoTaskMemAlloc(type->cbFormat)))
        return E_OUTOFMEMORY;
    memcpy(format, type->pbFormat, type->cbFormat);
    *out = format;
    *out_size = type->cbFormat;
    return S_OK;
}

static void resampler_reset_stream(struct resampler *resampler)
{
    resampler->draining = FALSE;
    resampler->time_valid = FALSE;
    resampler->next_time = 0;
    if (resampler->dsp_ready)
        resampler_dsp_reset(&resampler->dsp);
}

static void resampler_release_dsp(struct resampler *resampler)
{
    if (resampler->dsp_ready)
        resampler_dsp_cleanup(&resampler->dsp);
    resampler->dsp_ready = FALSE;
    resampler->draining = FALSE;
    resampler->time_valid = FALSE;
}

static void resampler_set_format(struct resampler *resampler, BOOL output, WAVEFORMATEX *format, UINT32 size)
{
    WAVEFORMATEX **slot = output ? &resampler->output_format : &resampler->input_format;

    resampler_release_dsp(resampler);
    CoTaskMemFree(*slot);
    *slot = format;
    if (output)
        resampler->output_size = size;
    else
        resampler->input_size = size;
}

static HRESULT resampler_prepare(struct resampler *resampler)
{
    HRESULT hr;

    if (!resampler->input_format || !resampler->output_format)
        return MF_E_TRANSFORM_TYPE_NOT_SET;
    if (resampler->dsp_ready)
        return S_OK;
    if (FAILED(hr = resampler_dsp_init(&resampler->dsp, resampler->input_format, resampler->output_format, resampler->quality)))
        return hr;
    resampler->dsp_ready = TRUE;
    resampler_reset_stream(resampler);
    return S_OK;
}

static BOOL resampler_accepting(struct resampler *resampler)
{
    return !resampler->draining && !resampler_dsp_available(&resampler->dsp, FALSE);
}

static HRESULT resampler_push(struct resampler *resampler, const BYTE *data, DWORD length, BOOL has_time, LONGLONG time)
{
    if (!resampler->time_valid)
    {
        resampler->next_time = has_time ? time : 0;
        resampler->time_valid = TRUE;
    }
    return resampler_dsp_push(&resampler->dsp, data, length / resampler->input_format->nBlockAlign);
}

static UINT resampler_pull(struct resampler *resampler, BYTE *data, DWORD space, BOOL *incomplete, LONGLONG *time,
        LONGLONG *duration)
{
    UINT block = resampler->output_format->nBlockAlign, frames;
    UINT64 available = resampler_dsp_available(&resampler->dsp, FALSE);
    BOOL tail = FALSE;

    if (!available && resampler->draining)
    {
        available = resampler_dsp_available(&resampler->dsp, TRUE);
        tail = TRUE;
    }
    if (!available)
    {
        if (resampler->draining)
            resampler_reset_stream(resampler);
        return 0;
    }

    frames = (UINT)min(available, space / block);
    if (!frames)
        return 0;
    resampler_dsp_pull(&resampler->dsp, data, frames);

    *time = resampler->next_time;
    *duration = ((LONGLONG)frames * 20000000 / resampler->output_format->nSamplesPerSec + 1) / 2;
    resampler->next_time += *duration;
    *incomplete = tail || resampler_dsp_available(&resampler->dsp, FALSE);
    return frames;
}

static HRESULT media_type_from_wave_format(const WAVEFORMATEX *format, UINT32 size, IMFMediaType **out)
{
    IMFMediaType *type;
    HRESULT hr;

    if (FAILED(hr = MFCreateMediaType(&type)))
        return hr;
    if (FAILED(hr = MFInitMediaTypeFromWaveFormatEx(type, format, size)))
    {
        IMFMediaType_Release(type);
        return hr;
    }
    *out = type;
    return S_OK;
}

static HRESULT dmo_type_from_wave_format(const WAVEFORMATEX *format, UINT32 size, DMO_MEDIA_TYPE *type)
{
    HRESULT hr;

    if (FAILED(hr = MoInitMediaType(type, size)))
        return hr;
    type->majortype = MEDIATYPE_Audio;
    type->subtype = resampler_wave_format_is_float(format) ? MEDIASUBTYPE_IEEE_FLOAT : MEDIASUBTYPE_PCM;
    type->bFixedSizeSamples = TRUE;
    type->bTemporalCompression = FALSE;
    type->lSampleSize = format->nBlockAlign;
    type->formattype = FORMAT_WaveFormatEx;
    memcpy(type->pbFormat, format, size);
    return S_OK;
}

static HRESULT WINAPI unknown_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    struct resampler *resampler = impl_from_IUnknown(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown))
        *out = &resampler->IUnknown_inner;
    else if (IsEqualGUID(iid, &IID_IMFTransform))
        *out = &resampler->IMFTransform_iface;
    else if (IsEqualGUID(iid, &IID_IMediaObject))
        *out = &resampler->IMediaObject_iface;
    else if (IsEqualGUID(iid, &IID_IPropertyStore))
        *out = &resampler->IPropertyStore_iface;
    else if (IsEqualGUID(iid, &IID_IPropertyBag))
        *out = &resampler->IPropertyBag_iface;
    else if (IsEqualGUID(iid, &IID_IWMResamplerProps))
        *out = &resampler->IWMResamplerProps_iface;
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
    struct resampler *resampler = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedIncrement(&resampler->refcount);

    TRACE("iface %p increasing refcount to %lu.\n", iface, refcount);
    return refcount;
}

static ULONG WINAPI unknown_Release(IUnknown *iface)
{
    struct resampler *resampler = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedDecrement(&resampler->refcount);

    TRACE("iface %p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        resampler_release_dsp(resampler);
        CoTaskMemFree(resampler->input_format);
        CoTaskMemFree(resampler->output_format);
        PropVariantClear(&resampler->channel_matrix);
        DeleteCriticalSection(&resampler->cs);
        free(resampler);
    }
    return refcount;
}

static const IUnknownVtbl unknown_vtbl =
{
    unknown_QueryInterface,
    unknown_AddRef,
    unknown_Release,
};

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
    TRACE("iface %p.\n", iface);

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
    TRACE("iface %p.\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetInputStreamInfo(IMFTransform *iface, DWORD id, MFT_INPUT_STREAM_INFO *info)
{
    struct resampler *resampler = impl_from_IMFTransform(iface);
    HRESULT hr = S_OK;

    TRACE("iface %p, id %#lx, info %p.\n", iface, id, info);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;

    memset(info, 0, sizeof(*info));
    EnterCriticalSection(&resampler->cs);
    if (!resampler->input_format || !resampler->output_format)
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else
    {
        info->cbSize = resampler->input_format->nBlockAlign;
        info->cbAlignment = 1;
    }
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI transform_GetOutputStreamInfo(IMFTransform *iface, DWORD id, MFT_OUTPUT_STREAM_INFO *info)
{
    struct resampler *resampler = impl_from_IMFTransform(iface);
    HRESULT hr = S_OK;

    TRACE("iface %p, id %#lx, info %p.\n", iface, id, info);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;

    memset(info, 0, sizeof(*info));
    EnterCriticalSection(&resampler->cs);
    if (!resampler->input_format || !resampler->output_format)
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else
    {
        info->cbSize = resampler->output_format->nBlockAlign;
        info->cbAlignment = 1;
    }
    LeaveCriticalSection(&resampler->cs);
    return hr;
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

static HRESULT create_partial_type(const GUID *subtype, IMFMediaType **out)
{
    IMFMediaType *type;
    HRESULT hr;

    if (FAILED(hr = MFCreateMediaType(&type)))
        return hr;
    if (FAILED(hr = IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio))
            || FAILED(hr = IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, subtype))
            || FAILED(hr = IMFMediaType_SetUINT32(type, &MF_MT_ALL_SAMPLES_INDEPENDENT, 1)))
    {
        IMFMediaType_Release(type);
        return hr;
    }
    *out = type;
    return S_OK;
}

static HRESULT WINAPI transform_GetInputAvailableType(IMFTransform *iface, DWORD id, DWORD index, IMFMediaType **type)
{
    TRACE("iface %p, id %#lx, index %#lx, type %p.\n", iface, id, index, type);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;
    if (index >= ARRAY_SIZE(resampler_subtypes))
        return MF_E_NO_MORE_TYPES;
    return create_partial_type(resampler_subtypes[index], type);
}

static HRESULT WINAPI transform_GetOutputAvailableType(IMFTransform *iface, DWORD id, DWORD index, IMFMediaType **type)
{
    IMFMediaType *media_type;
    UINT32 bits, block;
    HRESULT hr;

    TRACE("iface %p, id %#lx, index %#lx, type %p.\n", iface, id, index, type);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;
    if (index >= 2 * ARRAY_SIZE(resampler_subtypes))
        return MF_E_NO_MORE_TYPES;
    if (FAILED(hr = create_partial_type(resampler_subtypes[index % ARRAY_SIZE(resampler_subtypes)], &media_type)))
        return hr;

    if (index >= ARRAY_SIZE(resampler_subtypes))
    {
        bits = index == ARRAY_SIZE(resampler_subtypes) ? 32 : 16;
        block = 2 * bits / 8;
        if (FAILED(hr = IMFMediaType_SetUINT32(media_type, &MF_MT_AUDIO_BITS_PER_SAMPLE, bits))
                || FAILED(hr = IMFMediaType_SetUINT32(media_type, &MF_MT_AUDIO_NUM_CHANNELS, 2))
                || FAILED(hr = IMFMediaType_SetUINT32(media_type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000))
                || FAILED(hr = IMFMediaType_SetUINT32(media_type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * block))
                || FAILED(hr = IMFMediaType_SetUINT32(media_type, &MF_MT_AUDIO_BLOCK_ALIGNMENT, block))
                || FAILED(hr = IMFMediaType_SetUINT32(media_type, &MF_MT_AUDIO_PREFER_WAVEFORMATEX, 1)))
        {
            IMFMediaType_Release(media_type);
            return hr;
        }
    }
    *type = media_type;
    return S_OK;
}

static HRESULT WINAPI transform_SetInputType(IMFTransform *iface, DWORD id, IMFMediaType *type, DWORD flags)
{
    struct resampler *resampler = impl_from_IMFTransform(iface);
    WAVEFORMATEX *format;
    UINT32 size;
    HRESULT hr;

    TRACE("iface %p, id %#lx, type %p, flags %#lx.\n", iface, id, type, flags);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;

    if (!type)
    {
        if (!(flags & MFT_SET_TYPE_TEST_ONLY))
        {
            EnterCriticalSection(&resampler->cs);
            resampler_set_format(resampler, FALSE, NULL, 0);
            LeaveCriticalSection(&resampler->cs);
        }
        return S_OK;
    }

    if (FAILED(hr = wave_format_from_media_type(type, &format, &size)))
        return hr;
    if (flags & MFT_SET_TYPE_TEST_ONLY)
    {
        CoTaskMemFree(format);
        return S_OK;
    }

    EnterCriticalSection(&resampler->cs);
    resampler_set_format(resampler, FALSE, format, size);
    LeaveCriticalSection(&resampler->cs);
    return S_OK;
}

static HRESULT WINAPI transform_SetOutputType(IMFTransform *iface, DWORD id, IMFMediaType *type, DWORD flags)
{
    struct resampler *resampler = impl_from_IMFTransform(iface);
    WAVEFORMATEX *format;
    UINT32 size;
    HRESULT hr;

    TRACE("iface %p, id %#lx, type %p, flags %#lx.\n", iface, id, type, flags);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;

    if (!type)
    {
        if (!(flags & MFT_SET_TYPE_TEST_ONLY))
        {
            EnterCriticalSection(&resampler->cs);
            resampler_set_format(resampler, TRUE, NULL, 0);
            LeaveCriticalSection(&resampler->cs);
        }
        return S_OK;
    }

    EnterCriticalSection(&resampler->cs);
    hr = resampler->input_format ? S_OK : MF_E_TRANSFORM_TYPE_NOT_SET;
    LeaveCriticalSection(&resampler->cs);
    if (FAILED(hr))
        return hr;

    if (FAILED(hr = wave_format_from_media_type(type, &format, &size)))
        return hr;
    if (flags & MFT_SET_TYPE_TEST_ONLY)
    {
        CoTaskMemFree(format);
        return S_OK;
    }

    EnterCriticalSection(&resampler->cs);
    resampler_set_format(resampler, TRUE, format, size);
    LeaveCriticalSection(&resampler->cs);
    return S_OK;
}

static HRESULT WINAPI transform_GetInputCurrentType(IMFTransform *iface, DWORD id, IMFMediaType **type)
{
    struct resampler *resampler = impl_from_IMFTransform(iface);
    HRESULT hr;

    TRACE("iface %p, id %#lx, type %p.\n", iface, id, type);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;

    EnterCriticalSection(&resampler->cs);
    if (!resampler->input_format)
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else
        hr = media_type_from_wave_format(resampler->input_format, resampler->input_size, type);
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI transform_GetOutputCurrentType(IMFTransform *iface, DWORD id, IMFMediaType **type)
{
    struct resampler *resampler = impl_from_IMFTransform(iface);
    HRESULT hr;

    TRACE("iface %p, id %#lx, type %p.\n", iface, id, type);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;

    EnterCriticalSection(&resampler->cs);
    if (!resampler->output_format)
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
    else
        hr = media_type_from_wave_format(resampler->output_format, resampler->output_size, type);
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI transform_GetInputStatus(IMFTransform *iface, DWORD id, DWORD *flags)
{
    struct resampler *resampler = impl_from_IMFTransform(iface);
    HRESULT hr;

    TRACE("iface %p, id %#lx, flags %p.\n", iface, id, flags);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;

    EnterCriticalSection(&resampler->cs);
    if (SUCCEEDED(hr = resampler_prepare(resampler)))
        *flags = resampler_accepting(resampler) ? MFT_INPUT_STATUS_ACCEPT_DATA : 0;
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI transform_GetOutputStatus(IMFTransform *iface, DWORD *flags)
{
    struct resampler *resampler = impl_from_IMFTransform(iface);
    HRESULT hr;

    TRACE("iface %p, flags %p.\n", iface, flags);

    EnterCriticalSection(&resampler->cs);
    if (SUCCEEDED(hr = resampler_prepare(resampler)))
        *flags = resampler_dsp_available(&resampler->dsp, resampler->draining) ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI transform_SetOutputBounds(IMFTransform *iface, LONGLONG lower, LONGLONG upper)
{
    TRACE("iface %p, lower %I64d, upper %I64d.\n", iface, lower, upper);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessEvent(IMFTransform *iface, DWORD id, IMFMediaEvent *event)
{
    TRACE("iface %p, id %#lx, event %p.\n", iface, id, event);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessMessage(IMFTransform *iface, MFT_MESSAGE_TYPE message, ULONG_PTR param)
{
    struct resampler *resampler = impl_from_IMFTransform(iface);
    HRESULT hr = S_OK;

    TRACE("iface %p, message %#x, param %Ix.\n", iface, message, param);

    EnterCriticalSection(&resampler->cs);
    switch (message)
    {
        case MFT_MESSAGE_COMMAND_FLUSH:
            resampler_reset_stream(resampler);
            break;

        case MFT_MESSAGE_COMMAND_DRAIN:
            resampler->draining = TRUE;
            break;

        case MFT_MESSAGE_NOTIFY_BEGIN_STREAMING:
        case MFT_MESSAGE_NOTIFY_END_STREAMING:
        case MFT_MESSAGE_NOTIFY_START_OF_STREAM:
        case MFT_MESSAGE_NOTIFY_END_OF_STREAM:
            break;

        default:
            FIXME("Ignoring message %#x.\n", message);
            hr = E_NOTIMPL;
            break;
    }
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI transform_ProcessInput(IMFTransform *iface, DWORD id, IMFSample *sample, DWORD flags)
{
    struct resampler *resampler = impl_from_IMFTransform(iface);
    IMFMediaBuffer *buffer;
    LONGLONG time = 0;
    DWORD length;
    BOOL has_time;
    BYTE *data;
    HRESULT hr;

    TRACE("iface %p, id %#lx, sample %p, flags %#lx.\n", iface, id, sample, flags);

    if (id)
        return MF_E_INVALIDSTREAMNUMBER;
    if (!sample)
        return E_POINTER;

    EnterCriticalSection(&resampler->cs);
    if (FAILED(hr = resampler_prepare(resampler)))
        goto done;
    if (!resampler_accepting(resampler))
    {
        hr = MF_E_NOTACCEPTING;
        goto done;
    }

    if (FAILED(hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer)))
        goto done;
    if (SUCCEEDED(hr = IMFMediaBuffer_Lock(buffer, &data, NULL, &length)))
    {
        has_time = SUCCEEDED(IMFSample_GetSampleTime(sample, &time));
        hr = resampler_push(resampler, data, length, has_time, time);
        IMFMediaBuffer_Unlock(buffer);
    }
    IMFMediaBuffer_Release(buffer);

done:
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI transform_ProcessOutput(IMFTransform *iface, DWORD flags, DWORD count,
        MFT_OUTPUT_DATA_BUFFER *samples, DWORD *status)
{
    struct resampler *resampler = impl_from_IMFTransform(iface);
    LONGLONG time, duration;
    IMFMediaBuffer *buffer;
    DWORD max_length;
    BOOL incomplete;
    UINT frames;
    BYTE *data;
    HRESULT hr;

    TRACE("iface %p, flags %#lx, count %lu, samples %p, status %p.\n", iface, flags, count, samples, status);

    if (count != 1)
        return E_INVALIDARG;

    *status = 0;
    samples[0].dwStatus = 0;
    if (!samples[0].pSample)
        return E_INVALIDARG;

    EnterCriticalSection(&resampler->cs);
    if (FAILED(hr = resampler_prepare(resampler)))
        goto done;

    if (FAILED(hr = IMFSample_ConvertToContiguousBuffer(samples[0].pSample, &buffer)))
        goto done;
    if (FAILED(hr = IMFMediaBuffer_Lock(buffer, &data, &max_length, NULL)))
    {
        IMFMediaBuffer_Release(buffer);
        goto done;
    }

    frames = resampler_pull(resampler, data, max_length, &incomplete, &time, &duration);
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_SetCurrentLength(buffer, frames * resampler->output_format->nBlockAlign);
    IMFMediaBuffer_Release(buffer);

    if (!frames)
    {
        hr = MF_E_TRANSFORM_NEED_MORE_INPUT;
        goto done;
    }

    IMFSample_SetSampleTime(samples[0].pSample, time);
    IMFSample_SetSampleDuration(samples[0].pSample, duration);
    IMFSample_SetUINT32(samples[0].pSample, &MFSampleExtension_CleanPoint, 1);
    if (incomplete)
        samples[0].dwStatus |= MFT_OUTPUT_DATA_BUFFER_INCOMPLETE;

done:
    LeaveCriticalSection(&resampler->cs);
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

static HRESULT WINAPI media_object_QueryInterface(IMediaObject *iface, REFIID iid, void **out)
{
    return IUnknown_QueryInterface(impl_from_IMediaObject(iface)->outer, iid, out);
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
    TRACE("iface %p, input %p, output %p.\n", iface, input, output);

    *input = *output = 1;
    return S_OK;
}

static HRESULT WINAPI media_object_GetInputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    TRACE("iface %p, index %lu, flags %p.\n", iface, index, flags);

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;
    *flags = 0;
    return S_OK;
}

static HRESULT WINAPI media_object_GetOutputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    TRACE("iface %p, index %lu, flags %p.\n", iface, index, flags);

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;
    *flags = 0;
    return S_OK;
}

static HRESULT WINAPI media_object_GetInputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type)
{
    TRACE("iface %p, index %lu, type_index %lu, type %p.\n", iface, index, type_index, type);

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;
    if (type_index >= ARRAY_SIZE(resampler_subtypes))
        return DMO_E_NO_MORE_ITEMS;
    if (!type)
        return S_OK;

    memset(type, 0, sizeof(*type));
    type->majortype = MEDIATYPE_Audio;
    type->subtype = *resampler_subtypes[type_index];
    return S_OK;
}

static HRESULT WINAPI media_object_GetOutputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *type)
{
    TRACE("iface %p, index %lu, type_index %lu, type %p.\n", iface, index, type_index, type);

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;
    if (type_index >= ARRAY_SIZE(resampler_subtypes))
        return DMO_E_NO_MORE_ITEMS;
    if (!type)
        return S_OK;

    memset(type, 0, sizeof(*type));
    type->majortype = MEDIATYPE_Audio;
    type->subtype = *resampler_subtypes[type_index];
    return S_OK;
}

static HRESULT media_object_set_type(struct resampler *resampler, BOOL output, DWORD index,
        const DMO_MEDIA_TYPE *type, DWORD flags)
{
    WAVEFORMATEX *format;
    UINT32 size;
    HRESULT hr;

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;

    if (flags & DMO_SET_TYPEF_CLEAR)
    {
        EnterCriticalSection(&resampler->cs);
        resampler_set_format(resampler, output, NULL, 0);
        LeaveCriticalSection(&resampler->cs);
        return S_OK;
    }
    if (!type)
        return E_POINTER;

    if (FAILED(hr = wave_format_from_dmo_type(type, &format, &size)))
        return hr;
    if (flags & DMO_SET_TYPEF_TEST_ONLY)
    {
        CoTaskMemFree(format);
        return S_OK;
    }

    EnterCriticalSection(&resampler->cs);
    resampler_set_format(resampler, output, format, size);
    LeaveCriticalSection(&resampler->cs);
    return S_OK;
}

static HRESULT WINAPI media_object_SetInputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *type, DWORD flags)
{
    TRACE("iface %p, index %lu, type %p, flags %#lx.\n", iface, index, type, flags);
    return media_object_set_type(impl_from_IMediaObject(iface), FALSE, index, type, flags);
}

static HRESULT WINAPI media_object_SetOutputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *type, DWORD flags)
{
    TRACE("iface %p, index %lu, type %p, flags %#lx.\n", iface, index, type, flags);
    return media_object_set_type(impl_from_IMediaObject(iface), TRUE, index, type, flags);
}

static HRESULT WINAPI media_object_GetInputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type)
{
    struct resampler *resampler = impl_from_IMediaObject(iface);
    HRESULT hr;

    TRACE("iface %p, index %lu, type %p.\n", iface, index, type);

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;

    EnterCriticalSection(&resampler->cs);
    if (!resampler->input_format)
        hr = DMO_E_TYPE_NOT_SET;
    else
        hr = dmo_type_from_wave_format(resampler->input_format, resampler->input_size, type);
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI media_object_GetOutputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *type)
{
    struct resampler *resampler = impl_from_IMediaObject(iface);
    HRESULT hr;

    TRACE("iface %p, index %lu, type %p.\n", iface, index, type);

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;

    EnterCriticalSection(&resampler->cs);
    if (!resampler->output_format)
        hr = DMO_E_TYPE_NOT_SET;
    else
        hr = dmo_type_from_wave_format(resampler->output_format, resampler->output_size, type);
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI media_object_GetInputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size,
        DWORD *lookahead, DWORD *alignment)
{
    struct resampler *resampler = impl_from_IMediaObject(iface);
    HRESULT hr = S_OK;

    TRACE("iface %p, index %lu, size %p, lookahead %p, alignment %p.\n", iface, index, size, lookahead, alignment);

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;

    EnterCriticalSection(&resampler->cs);
    if (!resampler->input_format || !resampler->output_format)
        hr = DMO_E_TYPE_NOT_SET;
    else
    {
        *size = resampler->input_format->nBlockAlign;
        *lookahead = 0;
        *alignment = 1;
    }
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI media_object_GetOutputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size, DWORD *alignment)
{
    struct resampler *resampler = impl_from_IMediaObject(iface);
    HRESULT hr = S_OK;

    TRACE("iface %p, index %lu, size %p, alignment %p.\n", iface, index, size, alignment);

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;

    EnterCriticalSection(&resampler->cs);
    if (!resampler->input_format || !resampler->output_format)
        hr = DMO_E_TYPE_NOT_SET;
    else
    {
        *size = resampler->output_format->nBlockAlign;
        *alignment = 1;
    }
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI media_object_GetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME *latency)
{
    TRACE("iface %p, index %lu, latency %p.\n", iface, index, latency);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_SetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME latency)
{
    TRACE("iface %p, index %lu, latency %I64d.\n", iface, index, latency);
    return E_NOTIMPL;
}

static HRESULT WINAPI media_object_Flush(IMediaObject *iface)
{
    struct resampler *resampler = impl_from_IMediaObject(iface);

    TRACE("iface %p.\n", iface);

    EnterCriticalSection(&resampler->cs);
    resampler_reset_stream(resampler);
    LeaveCriticalSection(&resampler->cs);
    return S_OK;
}

static HRESULT WINAPI media_object_Discontinuity(IMediaObject *iface, DWORD index)
{
    struct resampler *resampler = impl_from_IMediaObject(iface);
    HRESULT hr;

    TRACE("iface %p, index %lu.\n", iface, index);

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;

    EnterCriticalSection(&resampler->cs);
    if (SUCCEEDED(hr = resampler_prepare(resampler)))
        resampler->draining = TRUE;
    else
        hr = DMO_E_TYPE_NOT_SET;
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI media_object_AllocateStreamingResources(IMediaObject *iface)
{
    struct resampler *resampler = impl_from_IMediaObject(iface);
    HRESULT hr;

    TRACE("iface %p.\n", iface);

    EnterCriticalSection(&resampler->cs);
    if (FAILED(hr = resampler_prepare(resampler)) && hr == MF_E_TRANSFORM_TYPE_NOT_SET)
        hr = DMO_E_TYPE_NOT_SET;
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI media_object_FreeStreamingResources(IMediaObject *iface)
{
    struct resampler *resampler = impl_from_IMediaObject(iface);

    TRACE("iface %p.\n", iface);

    EnterCriticalSection(&resampler->cs);
    resampler_release_dsp(resampler);
    LeaveCriticalSection(&resampler->cs);
    return S_OK;
}

static HRESULT WINAPI media_object_GetInputStatus(IMediaObject *iface, DWORD index, DWORD *flags)
{
    struct resampler *resampler = impl_from_IMediaObject(iface);
    HRESULT hr;

    TRACE("iface %p, index %lu, flags %p.\n", iface, index, flags);

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;

    EnterCriticalSection(&resampler->cs);
    if (SUCCEEDED(hr = resampler_prepare(resampler)))
        *flags = resampler_accepting(resampler) ? DMO_INPUT_STATUSF_ACCEPT_DATA : 0;
    else
        hr = DMO_E_TYPE_NOT_SET;
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI media_object_ProcessInput(IMediaObject *iface, DWORD index, IMediaBuffer *buffer,
        DWORD flags, REFERENCE_TIME timestamp, REFERENCE_TIME timelength)
{
    struct resampler *resampler = impl_from_IMediaObject(iface);
    DWORD length;
    BYTE *data;
    HRESULT hr;

    TRACE("iface %p, index %lu, buffer %p, flags %#lx, timestamp %I64d, timelength %I64d.\n",
            iface, index, buffer, flags, timestamp, timelength);

    if (index)
        return DMO_E_INVALIDSTREAMINDEX;
    if (!buffer)
        return E_POINTER;

    EnterCriticalSection(&resampler->cs);
    if (FAILED(hr = resampler_prepare(resampler)))
        hr = DMO_E_TYPE_NOT_SET;
    else if (!resampler_accepting(resampler))
        hr = DMO_E_NOTACCEPTING;
    else if (SUCCEEDED(hr = IMediaBuffer_GetBufferAndLength(buffer, &data, &length)))
        hr = resampler_push(resampler, data, length, !!(flags & DMO_INPUT_DATA_BUFFERF_TIME), timestamp);
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI media_object_ProcessOutput(IMediaObject *iface, DWORD flags, DWORD count,
        DMO_OUTPUT_DATA_BUFFER *buffers, DWORD *status)
{
    struct resampler *resampler = impl_from_IMediaObject(iface);
    DWORD length, max_length, block;
    LONGLONG time, duration;
    BOOL incomplete;
    UINT frames;
    BYTE *data;
    HRESULT hr;

    TRACE("iface %p, flags %#lx, count %lu, buffers %p, status %p.\n", iface, flags, count, buffers, status);

    if (count != 1)
        return E_INVALIDARG;

    buffers[0].dwStatus = 0;
    if (!buffers[0].pBuffer)
        return E_POINTER;

    EnterCriticalSection(&resampler->cs);
    if (FAILED(hr = resampler_prepare(resampler)))
    {
        hr = DMO_E_TYPE_NOT_SET;
        goto done;
    }

    if (FAILED(hr = IMediaBuffer_GetBufferAndLength(buffers[0].pBuffer, &data, &length))
            || FAILED(hr = IMediaBuffer_GetMaxLength(buffers[0].pBuffer, &max_length)))
        goto done;

    block = resampler->output_format->nBlockAlign;
    frames = resampler_pull(resampler, data + length, max_length > length ? max_length - length : 0,
            &incomplete, &time, &duration);
    if (!frames)
    {
        hr = S_FALSE;
        goto done;
    }

    IMediaBuffer_SetLength(buffers[0].pBuffer, length + frames * block);
    buffers[0].dwStatus = DMO_OUTPUT_DATA_BUFFERF_SYNCPOINT | DMO_OUTPUT_DATA_BUFFERF_TIME | DMO_OUTPUT_DATA_BUFFERF_TIMELENGTH;
    if (incomplete)
        buffers[0].dwStatus |= DMO_OUTPUT_DATA_BUFFERF_INCOMPLETE;
    buffers[0].rtTimestamp = time;
    buffers[0].rtTimelength = duration;

done:
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI media_object_Lock(IMediaObject *iface, LONG lock)
{
    TRACE("iface %p, lock %ld.\n", iface, lock);
    return S_OK;
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
    TRACE("iface %p, count %p.\n", iface, count);

    if (!count)
        return E_POINTER;
    *count = ARRAY_SIZE(resampler_property_keys);
    return S_OK;
}

static HRESULT WINAPI property_store_GetAt(IPropertyStore *iface, DWORD index, PROPERTYKEY *key)
{
    TRACE("iface %p, index %lu, key %p.\n", iface, index, key);

    if (!key)
        return E_POINTER;
    if (index >= ARRAY_SIZE(resampler_property_keys))
        return E_INVALIDARG;
    *key = *resampler_property_keys[index];
    return S_OK;
}

static HRESULT WINAPI property_store_GetValue(IPropertyStore *iface, REFPROPERTYKEY key, PROPVARIANT *value)
{
    struct resampler *resampler = impl_from_IPropertyStore(iface);
    HRESULT hr = S_OK;

    TRACE("iface %p, key %s, value %p.\n", iface, debugstr_guid(&key->fmtid), value);

    if (!key || !value)
        return E_POINTER;

    PropVariantInit(value);
    EnterCriticalSection(&resampler->cs);
    if (IsEqualPropertyKey(*key, MFPKEY_WMRESAMP_FILTERQUALITY))
    {
        value->vt = VT_I4;
        value->lVal = resampler->quality;
    }
    else if (IsEqualPropertyKey(*key, MFPKEY_WMRESAMP_LOWPASS_BANDWIDTH))
    {
        value->vt = VT_R4;
        value->fltVal = resampler->lowpass_bandwidth;
    }
    else if (IsEqualPropertyKey(*key, MFPKEY_WMRESAMP_CHANNELMTX))
        hr = PropVariantCopy(value, &resampler->channel_matrix);
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI property_store_SetValue(IPropertyStore *iface, REFPROPERTYKEY key, REFPROPVARIANT value)
{
    struct resampler *resampler = impl_from_IPropertyStore(iface);
    HRESULT hr = S_OK;

    TRACE("iface %p, key %s, value %p.\n", iface, debugstr_guid(&key->fmtid), value);

    if (!key || !value)
        return E_POINTER;

    EnterCriticalSection(&resampler->cs);
    if (IsEqualPropertyKey(*key, MFPKEY_WMRESAMP_FILTERQUALITY) && value->vt == VT_I4)
    {
        if (value->lVal < 1 || value->lVal > RESAMPLER_MAX_QUALITY)
            hr = E_INVALIDARG;
        else
        {
            resampler->quality = value->lVal;
            resampler_release_dsp(resampler);
        }
    }
    else if (IsEqualPropertyKey(*key, MFPKEY_WMRESAMP_LOWPASS_BANDWIDTH) && value->vt == VT_R4)
    {
        /* TODO: apply the low-pass bandwidth to the filter cutoff. */
        if (value->fltVal < 0.0f || value->fltVal > 1.0f)
            hr = E_INVALIDARG;
        else
            resampler->lowpass_bandwidth = value->fltVal;
    }
    else if (IsEqualPropertyKey(*key, MFPKEY_WMRESAMP_CHANNELMTX) && value->vt == (VT_I4 | VT_ARRAY))
    {
        /* TODO: apply a user channel matrix. */
        PropVariantClear(&resampler->channel_matrix);
        hr = PropVariantCopy(&resampler->channel_matrix, value);
    }
    else
        hr = E_INVALIDARG;
    LeaveCriticalSection(&resampler->cs);
    return hr;
}

static HRESULT WINAPI property_store_Commit(IPropertyStore *iface)
{
    TRACE("iface %p.\n", iface);
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

static HRESULT WINAPI property_bag_QueryInterface(IPropertyBag *iface, REFIID iid, void **out)
{
    return IUnknown_QueryInterface(impl_from_IPropertyBag(iface)->outer, iid, out);
}

static ULONG WINAPI property_bag_AddRef(IPropertyBag *iface)
{
    return IUnknown_AddRef(impl_from_IPropertyBag(iface)->outer);
}

static ULONG WINAPI property_bag_Release(IPropertyBag *iface)
{
    return IUnknown_Release(impl_from_IPropertyBag(iface)->outer);
}

static HRESULT WINAPI property_bag_Read(IPropertyBag *iface, const WCHAR *name, VARIANT *value, IErrorLog *error_log)
{
    FIXME("iface %p, name %s, value %p, error_log %p, ignoring.\n", iface, debugstr_w(name), value, error_log);
    return S_OK;
}

static HRESULT WINAPI property_bag_Write(IPropertyBag *iface, const WCHAR *name, VARIANT *value)
{
    FIXME("iface %p, name %s, value %p, ignoring.\n", iface, debugstr_w(name), value);
    return S_OK;
}

static const IPropertyBagVtbl property_bag_vtbl =
{
    property_bag_QueryInterface,
    property_bag_AddRef,
    property_bag_Release,
    property_bag_Read,
    property_bag_Write,
};

static HRESULT WINAPI resampler_props_QueryInterface(IWMResamplerProps *iface, REFIID iid, void **out)
{
    return IUnknown_QueryInterface(impl_from_IWMResamplerProps(iface)->outer, iid, out);
}

static ULONG WINAPI resampler_props_AddRef(IWMResamplerProps *iface)
{
    return IUnknown_AddRef(impl_from_IWMResamplerProps(iface)->outer);
}

static ULONG WINAPI resampler_props_Release(IWMResamplerProps *iface)
{
    return IUnknown_Release(impl_from_IWMResamplerProps(iface)->outer);
}

static HRESULT WINAPI resampler_props_SetHalfFilterLength(IWMResamplerProps *iface, LONG length)
{
    struct resampler *resampler = impl_from_IWMResamplerProps(iface);

    TRACE("iface %p, length %ld.\n", iface, length);

    if (length < 1 || length > RESAMPLER_MAX_QUALITY)
        return E_INVALIDARG;

    EnterCriticalSection(&resampler->cs);
    resampler->quality = length;
    resampler_release_dsp(resampler);
    LeaveCriticalSection(&resampler->cs);
    return S_OK;
}

static HRESULT WINAPI resampler_props_SetUserChannelMtx(IWMResamplerProps *iface, ChMtxType *matrix)
{
    /* TODO: apply a user channel matrix. */
    FIXME("iface %p, matrix %p, stub!\n", iface, matrix);
    return E_NOTIMPL;
}

static const IWMResamplerPropsVtbl resampler_props_vtbl =
{
    resampler_props_QueryInterface,
    resampler_props_AddRef,
    resampler_props_Release,
    resampler_props_SetHalfFilterLength,
    resampler_props_SetUserChannelMtx,
};

HRESULT resampler_create(IUnknown *outer, REFIID iid, void **out)
{
    struct resampler *resampler;
    SAFEARRAY *matrix;
    HRESULT hr;

    TRACE("outer %p, iid %s, out %p.\n", outer, debugstr_guid(iid), out);

    *out = NULL;
    if (outer && !IsEqualGUID(iid, &IID_IUnknown))
        return CLASS_E_NOAGGREGATION;

    if (!(resampler = calloc(1, sizeof(*resampler))))
        return E_OUTOFMEMORY;
    /* FIXME: the default content of the channel matrix property was not measured. */
    if (!(matrix = SafeArrayCreateVector(VT_I4, 0, 0)))
    {
        free(resampler);
        return E_OUTOFMEMORY;
    }

    resampler->IUnknown_inner.lpVtbl = &unknown_vtbl;
    resampler->IMFTransform_iface.lpVtbl = &transform_vtbl;
    resampler->IMediaObject_iface.lpVtbl = &media_object_vtbl;
    resampler->IPropertyStore_iface.lpVtbl = &property_store_vtbl;
    resampler->IPropertyBag_iface.lpVtbl = &property_bag_vtbl;
    resampler->IWMResamplerProps_iface.lpVtbl = &resampler_props_vtbl;
    resampler->outer = outer ? outer : &resampler->IUnknown_inner;
    resampler->refcount = 1;
    resampler->quality = RESAMPLER_DEFAULT_QUALITY;
    resampler->channel_matrix.vt = VT_I4 | VT_ARRAY;
    resampler->channel_matrix.parray = matrix;
    InitializeCriticalSection(&resampler->cs);

    hr = IUnknown_QueryInterface(&resampler->IUnknown_inner, iid, out);
    IUnknown_Release(&resampler->IUnknown_inner);
    return hr;
}
