/*
 * PROJECT:     LiberNT DirectShow
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     AVI splitter filter
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "splitter.h"

#include "mmreg.h"
#include "vfw.h"
#include "aviriff.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

struct avi_entry
{
    LONGLONG offset;
    LONGLONG bytes_before;
    DWORD size;
    BOOL key;
};

struct avi_stream
{
    AVISTREAMHEADER strh;
    BYTE *strf;
    DWORD strf_size;
    LONGLONG superindex_offset;
    DWORD superindex_size;

    struct avi_entry *entries;
    size_t count, capacity;
    LONGLONG total_bytes;
    DWORD max_size;
    size_t position;
};

struct avi_splitter
{
    struct avi_stream *streams;
    unsigned int stream_count;
    LONGLONG movi_offset, movi_end;
    LONGLONG idx1_offset;
    DWORD idx1_size;
};

static int stream_from_ckid(DWORD ckid)
{
    BYTE c0 = ckid & 0xff, c1 = (ckid >> 8) & 0xff;

    if (c0 < '0' || c0 > '9' || c1 < '0' || c1 > '9')
        return -1;
    return (c0 - '0') * 10 + (c1 - '0');
}

static REFERENCE_TIME units_to_time(const struct avi_stream *stream, LONGLONG units)
{
    if (!stream->strh.dwRate)
        return 0;
    return units * stream->strh.dwScale * 10000000 / stream->strh.dwRate;
}

static BOOL stream_has_sample_size(const struct avi_stream *stream)
{
    return stream->strh.fccType == streamtypeAUDIO && stream->strh.dwSampleSize;
}

static HRESULT add_entry(struct avi_stream *stream, LONGLONG offset, DWORD size, BOOL key)
{
    struct avi_entry *entry;

    if (!array_reserve((void **)&stream->entries, &stream->capacity, stream->count + 1, sizeof(*stream->entries)))
        return E_OUTOFMEMORY;
    entry = &stream->entries[stream->count++];
    entry->offset = offset;
    entry->size = size;
    entry->key = key;
    entry->bytes_before = stream->total_bytes;
    stream->total_bytes += size;
    stream->max_size = max(stream->max_size, size);
    return S_OK;
}

static HRESULT parse_strl(struct splitter *filter, struct avi_splitter *avi, LONGLONG offset, LONGLONG end)
{
    struct avi_stream *stream, *new_streams;
    DWORD chunk[2];
    HRESULT hr;

    if (!(new_streams = realloc(avi->streams, (avi->stream_count + 1) * sizeof(*avi->streams))))
        return E_OUTOFMEMORY;
    avi->streams = new_streams;
    stream = &avi->streams[avi->stream_count++];
    memset(stream, 0, sizeof(*stream));

    while (offset + sizeof(chunk) <= end)
    {
        if (FAILED(hr = splitter_read(filter, offset, sizeof(chunk), chunk)))
            return hr;
        offset += sizeof(chunk);

        if (chunk[0] == ckidSTREAMHEADER)
        {
            DWORD size = min(chunk[1], sizeof(stream->strh) - FIELD_OFFSET(AVISTREAMHEADER, fccType));
            if (FAILED(hr = splitter_read(filter, offset, size, &stream->strh.fccType)))
                return hr;
        }
        else if (chunk[0] == ckidSTREAMFORMAT && !stream->strf && chunk[1] && chunk[1] <= 0x100000)
        {
            if (!(stream->strf = calloc(1, chunk[1])))
                return E_OUTOFMEMORY;
            if (FAILED(hr = splitter_read(filter, offset, chunk[1], stream->strf)))
                return hr;
            stream->strf_size = chunk[1];
        }
        else if (chunk[0] == FCC('i','n','d','x'))
        {
            stream->superindex_offset = offset;
            stream->superindex_size = chunk[1];
        }

        offset += (chunk[1] + 1) & ~1;
    }
    return S_OK;
}

static HRESULT parse_list(struct splitter *filter, struct avi_splitter *avi, LONGLONG offset, LONGLONG end, BOOL top)
{
    DWORD chunk[3];
    HRESULT hr;

    while (offset + 8 <= end)
    {
        if (FAILED(hr = splitter_read(filter, offset, 8, chunk)))
            return hr;

        if (chunk[0] == FCC('L','I','S','T') && chunk[1] >= 4)
        {
            LONGLONG list_end = min(offset + 8 + chunk[1], end);

            if (FAILED(hr = splitter_read(filter, offset + 8, 4, &chunk[2])))
                return hr;
            if (chunk[2] == listtypeAVIHEADER)
                hr = parse_list(filter, avi, offset + 12, list_end, FALSE);
            else if (chunk[2] == listtypeSTREAMHEADER)
                hr = parse_strl(filter, avi, offset + 12, list_end);
            else if (chunk[2] == listtypeAVIMOVIE && top && !avi->movi_offset)
            {
                avi->movi_offset = offset + 8;
                avi->movi_end = list_end;
            }
            if (FAILED(hr))
                return hr;
        }
        else if (chunk[0] == ckidAVIOLDINDEX && top)
        {
            avi->idx1_offset = offset + 8;
            avi->idx1_size = chunk[1];
        }

        offset += 8 + ((chunk[1] + 1) & ~1);
    }
    return S_OK;
}

static HRESULT read_idx1(struct splitter *filter, struct avi_splitter *avi)
{
    struct _avioldindex_entry *entries;
    DWORD count = avi->idx1_size / sizeof(*entries), i;
    LONGLONG base = 0;
    BOOL first = TRUE;
    HRESULT hr;

    if (!(entries = malloc(count * sizeof(*entries))))
        return E_OUTOFMEMORY;
    if (FAILED(hr = splitter_read(filter, avi->idx1_offset, count * sizeof(*entries), entries)))
    {
        free(entries);
        return hr;
    }

    for (i = 0; i < count && SUCCEEDED(hr); ++i)
    {
        int index = stream_from_ckid(entries[i].dwChunkId);

        if (entries[i].dwFlags & AVIIF_LIST || index < 0 || index >= (int)avi->stream_count)
            continue;
        if (first)
        {
            if (entries[i].dwOffset < avi->movi_offset)
                base = avi->movi_offset;
            first = FALSE;
        }
        hr = add_entry(&avi->streams[index], base + entries[i].dwOffset + 8,
                entries[i].dwSize, !!(entries[i].dwFlags & AVIIF_KEYFRAME));
    }

    free(entries);
    return hr;
}

static HRESULT read_std_index(struct splitter *filter, struct avi_stream *stream, LONGLONG offset)
{
    AVISTDINDEX header;
    AVISTDINDEX_ENTRY *entries;
    DWORD i;
    HRESULT hr;

    if (FAILED(hr = splitter_read(filter, offset, FIELD_OFFSET(AVISTDINDEX, aIndex), &header)))
        return hr;
    if (header.bIndexType != AVI_INDEX_OF_CHUNKS || header.wLongsPerEntry != 2 || !header.nEntriesInUse)
        return S_OK;

    if (!(entries = malloc(header.nEntriesInUse * sizeof(*entries))))
        return E_OUTOFMEMORY;
    if (SUCCEEDED(hr = splitter_read(filter, offset + FIELD_OFFSET(AVISTDINDEX, aIndex),
            header.nEntriesInUse * sizeof(*entries), entries)))
    {
        for (i = 0; i < header.nEntriesInUse && SUCCEEDED(hr); ++i)
            hr = add_entry(stream, header.qwBaseOffset + entries[i].dwOffset,
                    entries[i].dwSize & AVISTDINDEX_SIZEMASK, !(entries[i].dwSize & AVISTDINDEX_DELTAFRAME));
    }
    free(entries);
    return hr;
}

static HRESULT read_super_index(struct splitter *filter, struct avi_stream *stream)
{
    AVISUPERINDEX header;
    struct _avisuperindex_entry *entries;
    LONGLONG offset = stream->superindex_offset - 8;
    DWORD i;
    HRESULT hr;

    if (FAILED(hr = splitter_read(filter, offset, FIELD_OFFSET(AVISUPERINDEX, aIndex), &header)))
        return hr;
    if (header.bIndexType != AVI_INDEX_OF_INDEXES || header.wLongsPerEntry != 4 || !header.nEntriesInUse)
        return S_OK;

    if (!(entries = malloc(header.nEntriesInUse * sizeof(*entries))))
        return E_OUTOFMEMORY;
    if (SUCCEEDED(hr = splitter_read(filter, offset + FIELD_OFFSET(AVISUPERINDEX, aIndex),
            header.nEntriesInUse * sizeof(*entries), entries)))
    {
        for (i = 0; i < header.nEntriesInUse && SUCCEEDED(hr); ++i)
            hr = read_std_index(filter, stream, entries[i].qwOffset);
    }
    free(entries);
    return hr;
}

static HRESULT scan_movi(struct splitter *filter, struct avi_splitter *avi, LONGLONG offset, LONGLONG end)
{
    DWORD chunk[3];
    HRESULT hr;

    while (offset + 8 <= end)
    {
        int index;

        if (FAILED(hr = splitter_read(filter, offset, 8, chunk)))
            return hr;

        if (chunk[0] == FCC('L','I','S','T'))
        {
            if (FAILED(hr = scan_movi(filter, avi, offset + 12, min(offset + 8 + chunk[1], end))))
                return hr;
        }
        else if ((index = stream_from_ckid(chunk[0])) >= 0 && index < (int)avi->stream_count)
        {
            if (FAILED(hr = add_entry(&avi->streams[index], offset + 8, chunk[1], TRUE)))
                return hr;
        }

        offset += 8 + ((chunk[1] + 1) & ~1);
    }
    return S_OK;
}

static HRESULT init_video_media_types(const struct avi_stream *stream, AM_MEDIA_TYPE *mts, unsigned int *count)
{
    const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)stream->strf;
    DWORD extra = stream->strf_size - sizeof(BITMAPINFOHEADER);
    VIDEOINFOHEADER *vih;
    AM_MEDIA_TYPE *mt = &mts[0];

    if (stream->strf_size < sizeof(BITMAPINFOHEADER))
        return VFW_E_INVALID_FILE_FORMAT;

    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Video;
    mt->formattype = FORMAT_VideoInfo;
    mt->lSampleSize = 1;
    mt->cbFormat = sizeof(VIDEOINFOHEADER) + extra;
    if (!(mt->pbFormat = CoTaskMemAlloc(mt->cbFormat)))
        return E_OUTOFMEMORY;
    vih = (VIDEOINFOHEADER *)mt->pbFormat;
    memset(vih, 0, sizeof(*vih));
    vih->AvgTimePerFrame = units_to_time(stream, 1);
    memcpy(&vih->bmiHeader, bih, stream->strf_size);

    if (bih->biCompression == BI_RGB || bih->biCompression == BI_BITFIELDS)
    {
        switch (bih->biBitCount)
        {
            case 1: mt->subtype = MEDIASUBTYPE_RGB1; break;
            case 4: mt->subtype = MEDIASUBTYPE_RGB4; break;
            case 8: mt->subtype = MEDIASUBTYPE_RGB8; break;
            case 16: mt->subtype = MEDIASUBTYPE_RGB555; break;
            case 24: mt->subtype = MEDIASUBTYPE_RGB24; break;
            case 32: mt->subtype = MEDIASUBTYPE_RGB32; break;
            default:
                FreeMediaType(mt);
                return VFW_E_INVALID_FILE_FORMAT;
        }
    }
    else
    {
        mt->subtype = MEDIATYPE_Video;
        mt->subtype.Data1 = bih->biCompression;
    }

    *count = 1;
    if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB32))
    {
        CopyMediaType(&mts[1], mt);
        mts[0].subtype = MEDIASUBTYPE_ARGB32;
        *count = 2;
    }
    return S_OK;
}

static HRESULT avi_splitter_sink_query_accept(const AM_MEDIA_TYPE *mt)
{
    if (IsEqualGUID(&mt->majortype, &MEDIATYPE_Stream) && IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_Avi))
        return S_OK;
    return S_FALSE;
}

static void avi_splitter_cleanup(struct splitter *filter)
{
    struct avi_splitter *avi = filter->priv;
    unsigned int i;

    if (!avi)
        return;
    for (i = 0; i < avi->stream_count; ++i)
    {
        free(avi->streams[i].strf);
        free(avi->streams[i].entries);
    }
    free(avi->streams);
    free(avi);
    filter->priv = NULL;
}

static HRESULT avi_splitter_parse(struct splitter *filter)
{
    struct avi_splitter *avi;
    BOOL have_superindex = FALSE;
    DWORD header[3];
    unsigned int i;
    HRESULT hr;

    if (FAILED(hr = splitter_read(filter, 0, sizeof(header), header)))
        return hr;
    if (header[0] != FCC('R','I','F','F') || header[2] != formtypeAVI)
        return VFW_E_INVALID_FILE_FORMAT;

    if (!(avi = calloc(1, sizeof(*avi))))
        return E_OUTOFMEMORY;
    filter->priv = avi;

    if (FAILED(hr = parse_list(filter, avi, 12, min((LONGLONG)header[1] + 8, filter->file_size), TRUE)))
        return hr;
    if (!avi->stream_count || !avi->movi_offset)
        return VFW_E_INVALID_FILE_FORMAT;

    for (i = 0; i < avi->stream_count; ++i)
    {
        if (avi->streams[i].superindex_offset)
        {
            have_superindex = TRUE;
            if (FAILED(hr = read_super_index(filter, &avi->streams[i])))
                return hr;
        }
    }
    if (!have_superindex)
    {
        if (avi->idx1_offset)
            hr = read_idx1(filter, avi);
        else
            hr = scan_movi(filter, avi, avi->movi_offset + 4, avi->movi_end);
        if (FAILED(hr))
            return hr;
    }

    for (i = 0; i < avi->stream_count; ++i)
    {
        struct avi_stream *stream = &avi->streams[i];
        struct splitter_source *source;
        AM_MEDIA_TYPE mts[2];
        unsigned int count = 0, j;
        REFERENCE_TIME duration;
        WCHAR name[20];

        if (stream->strh.fccType == streamtypeVIDEO)
            hr = init_video_media_types(stream, mts, &count);
        else if (stream->strh.fccType == streamtypeAUDIO && stream->strf_size >= sizeof(PCMWAVEFORMAT))
        {
            splitter_init_wave_media_type(&mts[0], (const WAVEFORMATEX *)stream->strf, stream->strf_size);
            hr = mts[0].pbFormat ? S_OK : E_OUTOFMEMORY;
            count = 1;
        }
        else
        {
            FIXME("Ignoring stream %u of type %s.\n", i, debugstr_fourcc(stream->strh.fccType));
            continue;
        }
        if (FAILED(hr))
            return hr;

        duration = units_to_time(stream, stream->strh.dwLength);
        swprintf(name, ARRAY_SIZE(name), L"Stream %02u", i);
        source = splitter_add_source(filter, name, mts, count, duration);
        for (j = 0; j < count; ++j)
            FreeMediaType(&mts[j]);
        if (!source)
            return E_OUTOFMEMORY;

        source->buffer_size = max(stream->max_size, stream->strh.dwSuggestedBufferSize);
        if (stream->strh.fccType == streamtypeVIDEO)
        {
            source->buffer_size = max(source->buffer_size, ((const BITMAPINFOHEADER *)stream->strf)->biSizeImage);
            if (stream->strh.dwRate && stream->strh.dwScale)
            {
                source->time_format = &TIME_FORMAT_FRAME;
                source->unit_formats[0] = &TIME_FORMAT_FRAME;
                source->unit_formats[1] = &TIME_FORMAT_SAMPLE;
                source->unit_format_count = 2;
                source->unit_time_num = (LONGLONG)stream->strh.dwScale * 10000000;
                source->unit_time_den = stream->strh.dwRate;
            }
        }
        source->buffer_size = max(source->buffer_size, 1);
        source->priv = stream;
    }

    return filter->source_count ? S_OK : VFW_E_INVALID_FILE_FORMAT;
}

static REFERENCE_TIME entry_start(const struct avi_stream *stream, size_t index)
{
    if (stream_has_sample_size(stream))
        return units_to_time(stream, stream->strh.dwStart + stream->entries[index].bytes_before / stream->strh.dwSampleSize);
    return units_to_time(stream, stream->strh.dwStart + index);
}

static REFERENCE_TIME entry_stop(const struct avi_stream *stream, size_t index)
{
    const struct avi_entry *entry = &stream->entries[index];

    if (stream_has_sample_size(stream))
        return units_to_time(stream, stream->strh.dwStart + (entry->bytes_before + entry->size) / stream->strh.dwSampleSize);
    return units_to_time(stream, stream->strh.dwStart + index + 1);
}

static void avi_splitter_seek(struct splitter_source *source, REFERENCE_TIME time)
{
    struct avi_stream *stream = source->priv;
    size_t index = 0;

    while (index + 1 < stream->count && entry_stop(stream, index) <= time)
        ++index;
    while (index && !stream->entries[index].key)
        --index;
    stream->position = time > 0 ? index : 0;
}

static HRESULT avi_splitter_next_sample(struct splitter_source *source, struct splitter_sample *sample)
{
    struct avi_stream *stream = source->priv;
    size_t index;

    while (stream->position < stream->count && !stream->entries[stream->position].size
            && stream->strh.fccType != streamtypeVIDEO)
        ++stream->position;
    if (stream->position >= stream->count)
        return S_FALSE;

    index = stream->position++;
    sample->offset = stream->entries[index].offset;
    sample->size = stream->entries[index].size;
    sample->start = entry_start(stream, index);
    sample->stop = entry_stop(stream, index);
    sample->has_time = TRUE;
    sample->sync_point = stream->entries[index].key;
    return S_OK;
}

static const struct splitter_ops avi_splitter_ops =
{
    .sink_query_accept = avi_splitter_sink_query_accept,
    .parse = avi_splitter_parse,
    .cleanup = avi_splitter_cleanup,
    .seek = avi_splitter_seek,
    .next_sample = avi_splitter_next_sample,
};

HRESULT avi_splitter_create(IUnknown *outer, IUnknown **out)
{
    struct splitter *object;
    HRESULT hr;

    if (FAILED(hr = splitter_create(outer, &CLSID_AviSplitter, &avi_splitter_ops, L"input pin", FALSE, &object)))
        return hr;

    TRACE("Created AVI splitter %p.\n", object);
    *out = &object->filter.IUnknown_inner;
    return S_OK;
}
