/*
 * PROJECT:     LiberNT DirectShow
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     MPEG-1 stream splitter filter
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "splitter.h"

#include "mmreg.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

#define PACK_START_CODE     0x000001ba
#define SYSTEM_START_CODE   0x000001bb
#define END_START_CODE      0x000001b9
#define SEQUENCE_START_CODE 0x000001b3
#define GOP_START_CODE      0x000001b8

#define SCAN_SIZE (256 * 1024)
#define TAIL_SIZE (64 * 1024)
#define VIDEO_CHUNK_SIZE 16384

enum mpeg_type
{
    MPEG_SYSTEM,
    MPEG_AUDIO,
    MPEG_VIDEO,
};

struct mpeg_audio_header
{
    unsigned int version, layer, bitrate, sample_rate, channels, frame_size, samples;
    unsigned int mode, mode_ext, emphasis;
    BOOL protection, private_bit, copyright, original, padding;
};

struct mpeg_stream
{
    struct splitter *filter;
    BYTE id;
    BOOL video;
    LONGLONG position;
    LONGLONG first_pts;
    struct mpeg_audio_header audio;
    REFERENCE_TIME frame_time;
    DWORD bitrate;
    BYTE sequence_header[140];
    DWORD sequence_header_size;
    DWORD start_time_code;
};

struct mpeg_splitter
{
    IAMStreamSelect IAMStreamSelect_iface;
    IQualityControl IQualityControl_iface;
    struct splitter *filter;

    enum mpeg_type type;
    LONGLONG data_start, data_end;
    LONGLONG base_pts;
    REFERENCE_TIME duration;
    struct mpeg_stream streams[2];
    unsigned int stream_count;
};

static const GUID *const sink_subtypes[] =
{
    &MEDIASUBTYPE_MPEG1System,
    &MEDIASUBTYPE_MPEG1VideoCD,
    &MEDIASUBTYPE_MPEG1Video,
    &MEDIASUBTYPE_MPEG1Audio,
};

static struct mpeg_splitter *get_mpeg(struct splitter *filter)
{
    return filter->priv;
}

static DWORD read_be32(const BYTE *p)
{
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static LONGLONG read_pts(const BYTE *p)
{
    return ((LONGLONG)(p[0] & 0x0e) << 29) | (p[1] << 22) | ((p[2] & 0xfe) << 14) | (p[3] << 7) | (p[4] >> 1);
}

static BOOL parse_audio_header(const BYTE *p, struct mpeg_audio_header *header)
{
    static const unsigned int bitrates[2][3][15] =
    {
        {
            {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
            {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
            {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
        },
        {
            {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
            {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
            {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
        },
    };
    static const unsigned int sample_rates[3] = {44100, 48000, 32000};
    unsigned int version_bits, layer_bits, bitrate_index, rate_index, lsf;

    if (p[0] != 0xff || (p[1] & 0xe0) != 0xe0)
        return FALSE;
    version_bits = (p[1] >> 3) & 3;
    layer_bits = (p[1] >> 1) & 3;
    bitrate_index = p[2] >> 4;
    rate_index = (p[2] >> 2) & 3;
    if (version_bits == 1 || !layer_bits || bitrate_index == 0 || bitrate_index == 15 || rate_index == 3)
        return FALSE;

    lsf = version_bits != 3;
    header->version = version_bits == 3 ? 1 : version_bits == 2 ? 2 : 25;
    header->layer = 4 - layer_bits;
    header->bitrate = bitrates[lsf][header->layer - 1][bitrate_index] * 1000;
    header->sample_rate = sample_rates[rate_index] >> (version_bits == 3 ? 0 : version_bits == 2 ? 1 : 2);
    header->padding = (p[2] >> 1) & 1;
    header->private_bit = p[2] & 1;
    header->protection = p[1] & 1;
    header->mode = p[3] >> 6;
    header->mode_ext = (p[3] >> 4) & 3;
    header->copyright = (p[3] >> 3) & 1;
    header->original = (p[3] >> 2) & 1;
    header->emphasis = p[3] & 3;
    header->channels = header->mode == 3 ? 1 : 2;

    if (header->layer == 1)
    {
        header->samples = 384;
        header->frame_size = (12 * header->bitrate / header->sample_rate + header->padding) * 4;
    }
    else
    {
        header->samples = (header->layer == 3 && lsf) ? 576 : 1152;
        header->frame_size = header->samples / 8 * header->bitrate / header->sample_rate + header->padding;
    }
    return header->frame_size > 4;
}

static HRESULT init_audio_media_types(const struct mpeg_audio_header *header, LONGLONG pts,
        AM_MEDIA_TYPE *mts, unsigned int *count)
{
    static const WORD modes[4] = {ACM_MPEG_STEREO, ACM_MPEG_JOINTSTEREO, ACM_MPEG_DUALCHANNEL, ACM_MPEG_SINGLECHANNEL};
    static const WORD layers[4] = {0, ACM_MPEG_LAYER1, ACM_MPEG_LAYER2, ACM_MPEG_LAYER3};
    MPEGLAYER3WAVEFORMAT *mp3;
    MPEG1WAVEFORMAT *wfx;
    unsigned int i;

    *count = header->layer == 3 ? 3 : 2;
    for (i = 0; i < *count; ++i)
    {
        AM_MEDIA_TYPE *mt = &mts[i];

        memset(mt, 0, sizeof(*mt));
        mt->majortype = MEDIATYPE_Audio;
        mt->bFixedSizeSamples = TRUE;
        mt->lSampleSize = 1;
        mt->formattype = FORMAT_WaveFormatEx;
    }

    mts[0].subtype = MEDIASUBTYPE_MPEG1AudioPayload;
    mts[1].subtype = MEDIASUBTYPE_MPEG1Payload;
    for (i = 0; i < 2; ++i)
    {
        if (!(wfx = CoTaskMemAlloc(sizeof(*wfx))))
            return E_OUTOFMEMORY;
        memset(wfx, 0, sizeof(*wfx));
        wfx->wfx.wFormatTag = WAVE_FORMAT_MPEG;
        wfx->wfx.nChannels = header->channels;
        wfx->wfx.nSamplesPerSec = header->sample_rate;
        wfx->wfx.nAvgBytesPerSec = header->bitrate / 8;
        wfx->wfx.nBlockAlign = header->frame_size - header->padding;
        wfx->wfx.cbSize = sizeof(*wfx) - sizeof(WAVEFORMATEX);
        wfx->fwHeadLayer = layers[header->layer];
        wfx->dwHeadBitrate = header->bitrate;
        wfx->fwHeadMode = modes[header->mode];
        wfx->fwHeadModeExt = 1 << header->mode_ext;
        wfx->wHeadEmphasis = header->emphasis + 1;
        if (header->private_bit)
            wfx->fwHeadFlags |= ACM_MPEG_PRIVATEBIT;
        if (header->copyright)
            wfx->fwHeadFlags |= ACM_MPEG_COPYRIGHT;
        if (header->original)
            wfx->fwHeadFlags |= ACM_MPEG_ORIGINALHOME;
        if (header->protection)
            wfx->fwHeadFlags |= ACM_MPEG_PROTECTIONBIT;
        if (header->version == 1)
            wfx->fwHeadFlags |= ACM_MPEG_ID_MPEG1;
        wfx->dwPTSLow = (DWORD)pts;
        wfx->dwPTSHigh = (DWORD)(pts >> 32);
        mts[i].cbFormat = sizeof(*wfx);
        mts[i].pbFormat = (BYTE *)wfx;
    }

    if (header->layer == 3)
    {
        mts[2].subtype = MEDIATYPE_Audio;
        mts[2].subtype.Data1 = WAVE_FORMAT_MPEGLAYER3;
        if (!(mp3 = CoTaskMemAlloc(sizeof(*mp3))))
            return E_OUTOFMEMORY;
        memset(mp3, 0, sizeof(*mp3));
        mp3->wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
        mp3->wfx.nChannels = header->channels;
        mp3->wfx.nSamplesPerSec = header->sample_rate;
        mp3->wfx.nAvgBytesPerSec = header->bitrate / 8;
        mp3->wfx.nBlockAlign = 1;
        mp3->wfx.cbSize = sizeof(*mp3) - sizeof(WAVEFORMATEX);
        mp3->wID = MPEGLAYER3_ID_MPEG;
        mp3->fdwFlags = MPEGLAYER3_FLAG_PADDING_ISO;
        mp3->nBlockSize = header->frame_size - header->padding;
        mp3->nFramesPerBlock = 1;
        mts[2].cbFormat = sizeof(*mp3);
        mts[2].pbFormat = (BYTE *)mp3;
    }
    return S_OK;
}

static BOOL parse_sequence_header(struct mpeg_stream *stream, const BYTE *p, DWORD size)
{
    static const REFERENCE_TIME frame_times[16] =
    {
        0, 417083, 416667, 400000, 333667, 333333, 200000, 166833, 166667,
    };
    DWORD header_size = 12;

    if (size < 12 || read_be32(p) != SEQUENCE_START_CODE)
        return FALSE;
    if (p[11] & 2)
        header_size += 64;
    if (header_size + 1 <= size && (p[header_size - 1] & 1))
        header_size += 64;
    if (header_size > size)
        return FALSE;

    memcpy(stream->sequence_header, p, header_size);
    stream->sequence_header_size = header_size;
    stream->frame_time = frame_times[p[7] & 0xf];
    stream->bitrate = ((p[8] << 10) | (p[9] << 2) | (p[10] >> 6)) * 400;
    return TRUE;
}

static void find_gop(struct mpeg_stream *stream, const BYTE *p, DWORD size)
{
    DWORD i;

    for (i = 0; i + 8 <= size; ++i)
    {
        if (read_be32(p + i) == GOP_START_CODE)
        {
            stream->start_time_code = read_be32(p + i + 4) >> 7;
            return;
        }
    }
}

static HRESULT init_video_media_type(const struct mpeg_stream *stream, AM_MEDIA_TYPE *mt)
{
    const BYTE *p = stream->sequence_header;
    MPEG1VIDEOINFO *info;
    DWORD size = FIELD_OFFSET(MPEG1VIDEOINFO, bSequenceHeader[stream->sequence_header_size]);

    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Video;
    mt->subtype = MEDIASUBTYPE_MPEG1Payload;
    mt->formattype = FORMAT_MPEGVideo;
    if (!(info = CoTaskMemAlloc(size)))
        return E_OUTOFMEMORY;
    memset(info, 0, size);
    info->hdr.AvgTimePerFrame = stream->frame_time;
    info->hdr.dwBitRate = stream->bitrate;
    info->hdr.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info->hdr.bmiHeader.biWidth = (p[4] << 4) | (p[5] >> 4);
    info->hdr.bmiHeader.biHeight = ((p[5] & 0xf) << 8) | p[6];
    info->dwStartTimeCode = stream->start_time_code;
    info->cbSequenceHeader = stream->sequence_header_size;
    memcpy(info->bSequenceHeader, p, stream->sequence_header_size);
    mt->cbFormat = size;
    mt->pbFormat = (BYTE *)info;
    return S_OK;
}

struct pes_packet
{
    BYTE id;
    LONGLONG payload_offset;
    DWORD payload_size;
    LONGLONG pts;
    BOOL has_pts;
};

static HRESULT read_pes(struct splitter *filter, LONGLONG *offset, LONGLONG end, struct pes_packet *packet)
{
    BYTE buffer[32];

    while (*offset + 4 <= end)
    {
        DWORD code, length, header;
        LONG size = (LONG)min((LONGLONG)sizeof(buffer), end - *offset);
        HRESULT hr;

        if (FAILED(hr = splitter_read(filter, *offset, size, buffer)))
            return hr;
        code = read_be32(buffer);

        if ((code & 0xffffff00) != 0x00000100)
        {
            ++*offset;
            continue;
        }
        if (code == END_START_CODE)
            return S_FALSE;
        if (code == PACK_START_CODE)
        {
            *offset += (size >= 5 && (buffer[4] & 0xc0) == 0x40) ? 14 + (size >= 14 ? (buffer[13] & 7) : 0) : 12;
            continue;
        }
        if (size < 6)
            return S_FALSE;
        length = (buffer[4] << 8) | buffer[5];
        if (code < 0x000001bc)
        {
            ++*offset;
            continue;
        }
        if (code == SYSTEM_START_CODE || (code & 0xff) == 0xbc || (code & 0xff) == 0xbe
                || (code & 0xff) == 0xbf || (code & 0xff) == 0xbd)
        {
            *offset += 6 + length;
            continue;
        }

        packet->id = code & 0xff;
        packet->has_pts = FALSE;
        header = 6;
        while (header < (DWORD)size && buffer[header] == 0xff)
            ++header;
        if (header < (DWORD)size && (buffer[header] & 0xc0) == 0x40)
            header += 2;
        if (header + 5 <= (DWORD)size && (buffer[header] & 0xf0) == 0x20)
        {
            packet->pts = read_pts(buffer + header);
            packet->has_pts = TRUE;
            header += 5;
        }
        else if (header + 10 <= (DWORD)size && (buffer[header] & 0xf0) == 0x30)
        {
            packet->pts = read_pts(buffer + header);
            packet->has_pts = TRUE;
            header += 10;
        }
        else
            ++header;

        if (header > 6 + length)
            header = 6 + length;
        packet->payload_offset = *offset + header;
        packet->payload_size = 6 + length - header;
        if (packet->payload_offset + packet->payload_size > end)
            packet->payload_size = (DWORD)(end - packet->payload_offset);
        *offset += 6 + length;
        return S_OK;
    }
    return S_FALSE;
}

static HRESULT find_last_pts(struct splitter *filter, LONGLONG *last_pts)
{
    LONGLONG start = max(0, filter->file_size - TAIL_SIZE);
    DWORD size = (DWORD)(filter->file_size - start), i;
    BYTE *data;
    HRESULT hr;

    if (!(data = malloc(size)))
        return E_OUTOFMEMORY;
    if (FAILED(hr = splitter_read(filter, start, size, data)))
    {
        free(data);
        return hr;
    }

    for (i = 0; i + 4 <= size && read_be32(data + i) != PACK_START_CODE; ++i)
        ;
    while (i + 6 <= size)
    {
        DWORD code = read_be32(data + i), length, header;

        if ((code & 0xffffff00) != 0x00000100 || code == END_START_CODE)
            break;
        if (code == PACK_START_CODE)
        {
            i += (data[i + 4] & 0xc0) == 0x40 ? 14 + (i + 13 < size ? (data[i + 13] & 7) : 0) : 12;
            continue;
        }
        length = (data[i + 4] << 8) | data[i + 5];
        if ((code & 0xff) >= 0xc0 && (code & 0xff) <= 0xef)
        {
            header = i + 6;
            while (header < size && data[header] == 0xff)
                ++header;
            if (header < size && (data[header] & 0xc0) == 0x40)
                header += 2;
            if (header + 5 <= size && (data[header] & 0xe0) == 0x20)
                *last_pts = max(*last_pts, read_pts(data + header));
        }
        i += 6 + length;
    }

    free(data);
    return S_OK;
}

static HRESULT parse_system(struct splitter *filter, struct mpeg_splitter *mpeg)
{
    LONGLONG offset = 0, end = min(filter->file_size, (LONGLONG)SCAN_SIZE), last_pts = -1;
    struct mpeg_stream *video = NULL, *audio = NULL;
    BOOL have_video = FALSE, have_audio = FALSE;
    struct pes_packet packet;
    BYTE buffer[160];
    HRESULT hr;

    mpeg->base_pts = -1;
    while ((hr = read_pes(filter, &offset, end, &packet)) == S_OK)
    {
        DWORD size = min(packet.payload_size, sizeof(buffer));

        if (packet.has_pts && (mpeg->base_pts < 0 || packet.pts < mpeg->base_pts))
            mpeg->base_pts = packet.pts;

        if (packet.id >= 0xe0 && packet.id <= 0xef && !have_video)
        {
            if (!video)
            {
                video = &mpeg->streams[mpeg->stream_count++];
                video->id = packet.id;
                video->video = TRUE;
                video->first_pts = -1;
            }
            if (packet.id != video->id)
                continue;
            if (packet.has_pts && video->first_pts < 0)
                video->first_pts = packet.pts;
            if (FAILED(hr = splitter_read(filter, packet.payload_offset, size, buffer)))
                return hr;
            if (!video->sequence_header_size)
            {
                DWORD i;

                for (i = 0; i + 4 <= size; ++i)
                {
                    if (read_be32(buffer + i) == SEQUENCE_START_CODE)
                    {
                        parse_sequence_header(video, buffer + i, size - i);
                        break;
                    }
                }
            }
            find_gop(video, buffer, size);
            have_video = !!video->sequence_header_size;
        }
        else if (packet.id >= 0xc0 && packet.id <= 0xdf && !have_audio)
        {
            if (!audio)
            {
                audio = &mpeg->streams[mpeg->stream_count++];
                audio->id = packet.id;
                audio->first_pts = -1;
            }
            if (packet.id != audio->id)
                continue;
            if (packet.has_pts && audio->first_pts < 0)
                audio->first_pts = packet.pts;
            if (FAILED(hr = splitter_read(filter, packet.payload_offset, size, buffer)))
                return hr;
            if (size >= 4 && parse_audio_header(buffer, &audio->audio))
                have_audio = TRUE;
        }

        if ((have_video || !video) && (have_audio || !audio) && (video || audio) && offset > 65536)
            break;
    }
    if (FAILED(hr))
        return hr;
    if ((video && !have_video) || (audio && !have_audio) || !mpeg->stream_count)
        return VFW_E_INVALID_FILE_FORMAT;
    if (mpeg->base_pts < 0)
        mpeg->base_pts = 0;

    if (FAILED(hr = find_last_pts(filter, &last_pts)))
        return hr;

    if (last_pts > mpeg->base_pts)
        mpeg->duration = (last_pts - mpeg->base_pts) * 1000 / 9;
    if (video)
        mpeg->duration += video->frame_time;
    mpeg->data_start = 0;
    mpeg->data_end = filter->file_size;
    return S_OK;
}

static HRESULT parse_audio(struct splitter *filter, struct mpeg_splitter *mpeg)
{
    LONGLONG offset = 0, end = filter->file_size;
    struct mpeg_stream *stream = &mpeg->streams[0];
    struct mpeg_audio_header next;
    BYTE buffer[10];
    HRESULT hr;

    if (FAILED(hr = splitter_read(filter, 0, min((LONGLONG)sizeof(buffer), end), buffer)))
        return hr;
    if (end >= 10 && !memcmp(buffer, "ID3", 3))
        offset = 10 + ((buffer[6] & 0x7f) << 21 | (buffer[7] & 0x7f) << 14 | (buffer[8] & 0x7f) << 7 | (buffer[9] & 0x7f));
    if (end >= 128 && SUCCEEDED(splitter_read(filter, end - 128, 3, buffer)) && !memcmp(buffer, "TAG", 3))
        end -= 128;

    for (; offset + 4 <= min(end, (LONGLONG)SCAN_SIZE); ++offset)
    {
        if (FAILED(hr = splitter_read(filter, offset, 4, buffer)))
            return hr;
        if (!parse_audio_header(buffer, &stream->audio))
            continue;
        if (offset + stream->audio.frame_size + 4 > end)
            break;
        if (FAILED(hr = splitter_read(filter, offset + stream->audio.frame_size, 4, buffer)))
            return hr;
        if (parse_audio_header(buffer, &next) && next.layer == stream->audio.layer
                && next.sample_rate == stream->audio.sample_rate)
            break;
    }
    if (offset + 4 > end || !stream->audio.bitrate)
        return VFW_E_INVALID_FILE_FORMAT;

    mpeg->stream_count = 1;
    stream->id = 0xc0;
    stream->first_pts = -1;
    mpeg->data_start = offset;
    mpeg->data_end = end;
    mpeg->duration = (end - offset) * 8 * 10000000 / stream->audio.bitrate;
    return S_OK;
}

static HRESULT parse_video(struct splitter *filter, struct mpeg_splitter *mpeg)
{
    struct mpeg_stream *stream = &mpeg->streams[0];
    BYTE buffer[160];
    LONG size = (LONG)min(filter->file_size, (LONGLONG)sizeof(buffer));
    HRESULT hr;

    if (FAILED(hr = splitter_read(filter, 0, size, buffer)))
        return hr;
    if (!parse_sequence_header(stream, buffer, size))
        return VFW_E_INVALID_FILE_FORMAT;
    find_gop(stream, buffer, size);

    mpeg->stream_count = 1;
    stream->id = 0xe0;
    stream->video = TRUE;
    stream->first_pts = -1;
    mpeg->data_start = 0;
    mpeg->data_end = filter->file_size;
    if (stream->bitrate)
        mpeg->duration = filter->file_size * 8 * 10000000 / stream->bitrate;
    return S_OK;
}

static HRESULT mpeg_splitter_sink_query_accept(const AM_MEDIA_TYPE *mt)
{
    unsigned int i;

    if (!IsEqualGUID(&mt->majortype, &MEDIATYPE_Stream))
        return S_FALSE;
    if (!IsEqualGUID(&mt->formattype, &GUID_NULL) && !IsEqualGUID(&mt->formattype, &FORMAT_WaveFormatEx))
        return S_FALSE;
    for (i = 0; i < ARRAY_SIZE(sink_subtypes); ++i)
    {
        if (IsEqualGUID(&mt->subtype, sink_subtypes[i]))
            return S_OK;
    }
    return S_FALSE;
}

static HRESULT mpeg_splitter_sink_get_media_type(unsigned int index, AM_MEDIA_TYPE *mt)
{
    if (index >= ARRAY_SIZE(sink_subtypes))
        return VFW_S_NO_MORE_ITEMS;

    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Stream;
    mt->subtype = *sink_subtypes[index];
    mt->bFixedSizeSamples = TRUE;
    mt->bTemporalCompression = TRUE;
    mt->lSampleSize = 1;
    return S_OK;
}

static HRESULT mpeg_splitter_sink_query_interface(struct splitter *filter, REFIID iid, void **out)
{
    struct mpeg_splitter *mpeg = get_mpeg(filter);

    if (IsEqualGUID(iid, &IID_IMemInputPin))
        *out = &filter->sink.IMemInputPin_iface;
    else if (IsEqualGUID(iid, &IID_IQualityControl))
        *out = &mpeg->IQualityControl_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT mpeg_splitter_parse(struct splitter *filter)
{
    struct mpeg_splitter *mpeg = get_mpeg(filter);
    BYTE header[4];
    unsigned int i;
    HRESULT hr;

    memset(mpeg->streams, 0, sizeof(mpeg->streams));
    mpeg->stream_count = 0;
    mpeg->duration = 0;

    if (filter->file_size < 4)
        return VFW_E_INVALID_FILE_FORMAT;
    if (FAILED(hr = splitter_read(filter, 0, sizeof(header), header)))
        return hr;

    if (read_be32(header) == PACK_START_CODE)
    {
        mpeg->type = MPEG_SYSTEM;
        hr = parse_system(filter, mpeg);
    }
    else if (read_be32(header) == SEQUENCE_START_CODE)
    {
        mpeg->type = MPEG_VIDEO;
        hr = parse_video(filter, mpeg);
    }
    else
    {
        mpeg->type = MPEG_AUDIO;
        hr = parse_audio(filter, mpeg);
    }
    if (FAILED(hr))
        return hr;

    for (i = 0; i < mpeg->stream_count; ++i)
    {
        struct mpeg_stream *stream = &mpeg->streams[i];
        struct splitter_source *source;
        AM_MEDIA_TYPE mts[3];
        unsigned int count = 1, j;

        stream->filter = filter;
        if (stream->video)
            hr = init_video_media_type(stream, &mts[0]);
        else
            hr = init_audio_media_types(&stream->audio,
                    stream->first_pts >= 0 ? stream->first_pts : 0, mts, &count);
        if (FAILED(hr))
        {
            for (j = 0; j < count; ++j)
                FreeMediaType(&mts[j]);
            return hr;
        }

        source = splitter_add_source(filter, stream->video ? L"Video" : L"Audio", mts, count, mpeg->duration);
        for (j = 0; j < count; ++j)
            FreeMediaType(&mts[j]);
        if (!source)
            return E_OUTOFMEMORY;
        source->priv = stream;
        if (stream->video)
        {
            source->buffer_size = 65536;
            source->buffer_count = 30;
        }
        else
        {
            source->buffer_size = max(stream->audio.frame_size * 4, 8192);
            source->buffer_count = 3;
        }
    }
    return S_OK;
}

static void mpeg_splitter_cleanup(struct splitter *filter)
{
    struct mpeg_splitter *mpeg = get_mpeg(filter);

    memset(mpeg->streams, 0, sizeof(mpeg->streams));
    mpeg->stream_count = 0;
}

static void mpeg_splitter_destroy(struct splitter *filter)
{
    free(filter->priv);
    filter->priv = NULL;
}

static void mpeg_splitter_seek(struct splitter_source *source, REFERENCE_TIME time)
{
    struct mpeg_stream *stream = source->priv;
    struct mpeg_splitter *mpeg = get_mpeg(stream->filter);
    LONGLONG size = mpeg->data_end - mpeg->data_start, offset = 0;

    if (time > 0 && mpeg->duration > 0)
    {
        if (mpeg->type == MPEG_AUDIO)
            offset = time * (stream->audio.bitrate / 8) / 10000000;
        else
            offset = (LONGLONG)((double)size * time / mpeg->duration);
        offset = min(offset, size);
    }
    stream->position = mpeg->data_start + offset;
}

static HRESULT next_audio_frame(struct mpeg_splitter *mpeg, struct mpeg_stream *stream, struct splitter_sample *sample)
{
    struct mpeg_audio_header header;
    BYTE buffer[4];
    HRESULT hr;

    while (stream->position + 4 <= mpeg->data_end)
    {
        if (FAILED(hr = splitter_read(stream->filter, stream->position, 4, buffer)))
            return hr;
        if (parse_audio_header(buffer, &header) && header.layer == stream->audio.layer
                && header.sample_rate == stream->audio.sample_rate)
            break;
        ++stream->position;
    }
    if (stream->position + 4 > mpeg->data_end)
        return S_FALSE;

    sample->offset = stream->position;
    sample->size = (DWORD)min((LONGLONG)header.frame_size, mpeg->data_end - stream->position);
    sample->start = (stream->position - mpeg->data_start) * 8 * 10000000 / stream->audio.bitrate;
    sample->stop = sample->start + (REFERENCE_TIME)header.samples * 10000000 / header.sample_rate;
    sample->has_time = TRUE;
    sample->sync_point = TRUE;
    stream->position += sample->size;
    return S_OK;
}

static HRESULT mpeg_splitter_next_sample(struct splitter_source *source, struct splitter_sample *sample)
{
    struct mpeg_stream *stream = source->priv;
    struct mpeg_splitter *mpeg = get_mpeg(stream->filter);
    struct pes_packet packet;
    HRESULT hr;

    if (mpeg->type == MPEG_AUDIO)
        return next_audio_frame(mpeg, stream, sample);

    if (mpeg->type == MPEG_VIDEO)
    {
        if (stream->position >= mpeg->data_end)
            return S_FALSE;
        sample->offset = stream->position;
        sample->size = (DWORD)min((LONGLONG)VIDEO_CHUNK_SIZE, mpeg->data_end - stream->position);
        sample->has_time = stream->position == mpeg->data_start;
        sample->start = 0;
        sample->stop = stream->frame_time;
        sample->sync_point = sample->has_time;
        stream->position += sample->size;
        return S_OK;
    }

    while ((hr = read_pes(stream->filter, &stream->position, mpeg->data_end, &packet)) == S_OK)
    {
        if (packet.id != stream->id || !packet.payload_size)
            continue;

        sample->offset = packet.payload_offset;
        sample->size = packet.payload_size;
        sample->has_time = packet.has_pts;
        sample->sync_point = packet.has_pts;
        if (packet.has_pts)
        {
            sample->start = (packet.pts - mpeg->base_pts) * 1000 / 9;
            if (stream->video)
                sample->stop = sample->start + stream->frame_time;
            else
                sample->stop = sample->start + (REFERENCE_TIME)packet.payload_size * 8 * 10000000 / stream->audio.bitrate;
        }
        return S_OK;
    }
    return hr;
}

static inline struct mpeg_splitter *impl_from_IAMStreamSelect(IAMStreamSelect *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_splitter, IAMStreamSelect_iface);
}

static HRESULT WINAPI stream_select_QueryInterface(IAMStreamSelect *iface, REFIID iid, void **out)
{
    struct mpeg_splitter *mpeg = impl_from_IAMStreamSelect(iface);
    return IUnknown_QueryInterface(mpeg->filter->filter.outer_unk, iid, out);
}

static ULONG WINAPI stream_select_AddRef(IAMStreamSelect *iface)
{
    struct mpeg_splitter *mpeg = impl_from_IAMStreamSelect(iface);
    return IUnknown_AddRef(mpeg->filter->filter.outer_unk);
}

static ULONG WINAPI stream_select_Release(IAMStreamSelect *iface)
{
    struct mpeg_splitter *mpeg = impl_from_IAMStreamSelect(iface);
    return IUnknown_Release(mpeg->filter->filter.outer_unk);
}

static HRESULT WINAPI stream_select_Count(IAMStreamSelect *iface, DWORD *count)
{
    struct mpeg_splitter *mpeg = impl_from_IAMStreamSelect(iface);

    TRACE("iface %p, count %p.\n", iface, count);

    EnterCriticalSection(&mpeg->filter->filter.filter_cs);
    *count = mpeg->filter->source_count;
    LeaveCriticalSection(&mpeg->filter->filter.filter_cs);
    return S_OK;
}

static HRESULT WINAPI stream_select_Info(IAMStreamSelect *iface, LONG index, AM_MEDIA_TYPE **mt,
        DWORD *flags, LCID *lcid, DWORD *group, WCHAR **name, IUnknown **object, IUnknown **unknown)
{
    struct mpeg_splitter *mpeg = impl_from_IAMStreamSelect(iface);
    struct splitter *filter = mpeg->filter;
    struct splitter_source *source;
    struct mpeg_stream *stream;
    HRESULT hr = S_OK;

    TRACE("iface %p, index %ld.\n", iface, index);

    EnterCriticalSection(&filter->filter.filter_cs);

    if (index < 0 || (DWORD)index >= filter->source_count)
    {
        LeaveCriticalSection(&filter->filter.filter_cs);
        return S_FALSE;
    }
    source = filter->sources[index];
    stream = source->priv;

    if (mt)
        *mt = CreateMediaType(source->pin.pin.peer ? &source->pin.pin.mt : &source->media_types[0]);
    if (flags)
        *flags = AMSTREAMSELECTINFO_ENABLED;
    if (lcid)
        *lcid = 0;
    if (group)
        *group = stream->video ? 0 : 1;
    if (name)
    {
        if ((*name = CoTaskMemAlloc(11 * sizeof(WCHAR))))
            swprintf(*name, 11, L"Stream(%02X)", stream->id);
        else
            hr = E_OUTOFMEMORY;
    }
    if (object)
    {
        *object = (IUnknown *)&source->pin.pin.IPin_iface;
        IUnknown_AddRef(*object);
    }
    if (unknown)
        *unknown = NULL;

    LeaveCriticalSection(&filter->filter.filter_cs);
    return hr;
}

static HRESULT WINAPI stream_select_Enable(IAMStreamSelect *iface, LONG index, DWORD flags)
{
    FIXME("iface %p, index %ld, flags %#lx, stub!\n", iface, index, flags);
    return E_NOTIMPL;
}

static const IAMStreamSelectVtbl stream_select_vtbl =
{
    stream_select_QueryInterface,
    stream_select_AddRef,
    stream_select_Release,
    stream_select_Count,
    stream_select_Info,
    stream_select_Enable,
};

static inline struct mpeg_splitter *impl_from_IQualityControl(IQualityControl *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_splitter, IQualityControl_iface);
}

static HRESULT WINAPI sink_quality_QueryInterface(IQualityControl *iface, REFIID iid, void **out)
{
    struct mpeg_splitter *mpeg = impl_from_IQualityControl(iface);
    return IPin_QueryInterface(&mpeg->filter->sink.pin.IPin_iface, iid, out);
}

static ULONG WINAPI sink_quality_AddRef(IQualityControl *iface)
{
    struct mpeg_splitter *mpeg = impl_from_IQualityControl(iface);
    return IPin_AddRef(&mpeg->filter->sink.pin.IPin_iface);
}

static ULONG WINAPI sink_quality_Release(IQualityControl *iface)
{
    struct mpeg_splitter *mpeg = impl_from_IQualityControl(iface);
    return IPin_Release(&mpeg->filter->sink.pin.IPin_iface);
}

static HRESULT WINAPI sink_quality_Notify(IQualityControl *iface, IBaseFilter *sender, Quality q)
{
    TRACE("iface %p, sender %p, type %u, proportion %ld.\n", iface, sender, q.Type, q.Proportion);
    return S_OK;
}

static HRESULT WINAPI sink_quality_SetSink(IQualityControl *iface, IQualityControl *sink)
{
    TRACE("iface %p, sink %p.\n", iface, sink);
    return S_OK;
}

static const IQualityControlVtbl sink_quality_vtbl =
{
    sink_quality_QueryInterface,
    sink_quality_AddRef,
    sink_quality_Release,
    sink_quality_Notify,
    sink_quality_SetSink,
};

static HRESULT mpeg_splitter_query_interface(struct splitter *filter, REFIID iid, void **out)
{
    struct mpeg_splitter *mpeg = get_mpeg(filter);

    if (IsEqualGUID(iid, &IID_IAMStreamSelect))
    {
        *out = &mpeg->IAMStreamSelect_iface;
        IUnknown_AddRef((IUnknown *)*out);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static const struct splitter_ops mpeg_splitter_ops =
{
    .sink_query_accept = mpeg_splitter_sink_query_accept,
    .sink_get_media_type = mpeg_splitter_sink_get_media_type,
    .sink_query_interface = mpeg_splitter_sink_query_interface,
    .parse = mpeg_splitter_parse,
    .cleanup = mpeg_splitter_cleanup,
    .destroy = mpeg_splitter_destroy,
    .seek = mpeg_splitter_seek,
    .next_sample = mpeg_splitter_next_sample,
    .query_interface = mpeg_splitter_query_interface,
};

HRESULT mpeg1_splitter_create(IUnknown *outer, IUnknown **out)
{
    struct mpeg_splitter *mpeg;
    struct splitter *object;
    HRESULT hr;

    if (!(mpeg = calloc(1, sizeof(*mpeg))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = splitter_create(outer, &CLSID_MPEG1Splitter, &mpeg_splitter_ops, L"Input", TRUE, &object)))
    {
        free(mpeg);
        return hr;
    }

    mpeg->IAMStreamSelect_iface.lpVtbl = &stream_select_vtbl;
    mpeg->IQualityControl_iface.lpVtbl = &sink_quality_vtbl;
    mpeg->filter = object;
    object->priv = mpeg;
    object->no_source_position = TRUE;

    TRACE("Created MPEG-1 splitter %p.\n", object);
    *out = &object->filter.IUnknown_inner;
    return S_OK;
}
