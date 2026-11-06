/*
 * PROJECT:     LiberNT DirectShow
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     WAVE parser filter
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "splitter.h"

#include "mmreg.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

struct wave_parser
{
    LONGLONG data_offset;
    DWORD data_size;
    DWORD samples_per_sec;
    DWORD avg_bytes_per_sec;
    DWORD block_align;
    DWORD chunk_size;
    DWORD position;
};

static REFERENCE_TIME bytes_to_time(const struct wave_parser *parser, DWORD bytes)
{
    return (REFERENCE_TIME)bytes * 10000000 / parser->avg_bytes_per_sec;
}

static HRESULT wave_parser_sink_query_accept(const AM_MEDIA_TYPE *mt)
{
    if (IsEqualGUID(&mt->majortype, &MEDIATYPE_Stream) && IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_WAVE))
        return S_OK;
    return S_FALSE;
}

static HRESULT wave_parser_parse(struct splitter *filter)
{
    WAVEFORMATEX *wfx = NULL;
    struct splitter_source *source;
    struct wave_parser *parser;
    DWORD header[3], chunk[2];
    LONGLONG offset = 12;
    DWORD wfx_size = 0;
    AM_MEDIA_TYPE mt;
    HRESULT hr;

    if (FAILED(hr = splitter_read(filter, 0, sizeof(header), header)))
        return hr;
    if (header[0] != mmioFOURCC('R','I','F','F') || header[2] != mmioFOURCC('W','A','V','E'))
        return VFW_E_INVALID_FILE_FORMAT;

    if (!(parser = calloc(1, sizeof(*parser))))
        return E_OUTOFMEMORY;
    filter->priv = parser;

    while (offset + sizeof(chunk) <= filter->file_size)
    {
        if (FAILED(hr = splitter_read(filter, offset, sizeof(chunk), chunk)))
            break;
        offset += sizeof(chunk);

        if (chunk[0] == mmioFOURCC('f','m','t',' ') && !wfx)
        {
            if (chunk[1] < sizeof(PCMWAVEFORMAT) || chunk[1] > 0x10000)
                break;
            if (!(wfx = calloc(1, max(chunk[1], sizeof(WAVEFORMATEX)))))
            {
                hr = E_OUTOFMEMORY;
                break;
            }
            if (FAILED(hr = splitter_read(filter, offset, chunk[1], wfx)))
                break;
            wfx_size = chunk[1];
        }
        else if (chunk[0] == mmioFOURCC('d','a','t','a'))
        {
            parser->data_offset = offset;
            parser->data_size = (DWORD)min((LONGLONG)chunk[1], filter->file_size - offset);
            break;
        }

        offset += (chunk[1] + 1) & ~1;
    }

    if (!wfx || !parser->data_offset || !wfx->nAvgBytesPerSec || !wfx->nBlockAlign)
    {
        free(wfx);
        return FAILED(hr) ? hr : VFW_E_INVALID_FILE_FORMAT;
    }

    parser->samples_per_sec = wfx->nSamplesPerSec;
    parser->avg_bytes_per_sec = wfx->nAvgBytesPerSec;
    parser->block_align = wfx->nBlockAlign;
    parser->chunk_size = max(parser->block_align,
            parser->avg_bytes_per_sec / 2 / parser->block_align * parser->block_align);

    splitter_init_wave_media_type(&mt, wfx, wfx_size);
    free(wfx);
    if (!mt.pbFormat)
        return E_OUTOFMEMORY;

    source = splitter_add_source(filter, L"output", &mt, 1, bytes_to_time(parser, parser->data_size));
    FreeMediaType(&mt);
    if (!source)
        return E_OUTOFMEMORY;
    source->buffer_size = parser->chunk_size;
    source->buffer_count = 4;
    source->priv = parser;
    source->seek.dwCapabilities |= AM_SEEKING_CanDoSegments;
    if (parser->samples_per_sec)
    {
        source->time_format = &TIME_FORMAT_SAMPLE;
        source->unit_formats[0] = &TIME_FORMAT_SAMPLE;
        source->unit_format_count = 1;
        source->unit_time_num = 10000000;
        source->unit_time_den = parser->samples_per_sec;
    }
    return S_OK;
}

static void wave_parser_cleanup(struct splitter *filter)
{
    free(filter->priv);
    filter->priv = NULL;
}

static void wave_parser_seek(struct splitter_source *source, REFERENCE_TIME time)
{
    struct wave_parser *parser = source->priv;
    LONGLONG position = max(time, 0) * parser->avg_bytes_per_sec / 10000000;

    position -= position % parser->block_align;
    parser->position = (DWORD)min(position, (LONGLONG)parser->data_size);
}

static HRESULT wave_parser_next_sample(struct splitter_source *source, struct splitter_sample *sample)
{
    struct wave_parser *parser = source->priv;

    if (parser->position >= parser->data_size)
        return S_FALSE;

    sample->offset = parser->data_offset + parser->position;
    sample->size = min(parser->chunk_size, parser->data_size - parser->position);
    sample->start = bytes_to_time(parser, parser->position);
    sample->stop = bytes_to_time(parser, parser->position + sample->size);
    sample->has_time = TRUE;
    sample->sync_point = TRUE;
    parser->position += sample->size;
    return S_OK;
}

static const struct splitter_ops wave_parser_ops =
{
    .sink_query_accept = wave_parser_sink_query_accept,
    .parse = wave_parser_parse,
    .cleanup = wave_parser_cleanup,
    .seek = wave_parser_seek,
    .next_sample = wave_parser_next_sample,
};

HRESULT wave_parser_create(IUnknown *outer, IUnknown **out)
{
    struct splitter *object;
    HRESULT hr;

    if (FAILED(hr = splitter_create(outer, &CLSID_WAVEParser, &wave_parser_ops, L"input pin", FALSE, &object)))
        return hr;

    TRACE("Created WAVE parser %p.\n", object);
    *out = &object->filter.IUnknown_inner;
    return S_OK;
}
