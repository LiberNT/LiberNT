/*
 * PROJECT:     LiberNT DirectShow
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Native pull-model splitter filters
 * COPYRIGHT:   Copyright 2010 Maarten Lankhorst for CodeWeavers
 *              Copyright 2010 Aric Stewart for CodeWeavers
 *              Copyright 2019-2020 Zebediah Figura
 *              Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#ifndef __QUARTZ_SPLITTER_H
#define __QUARTZ_SPLITTER_H

#include "quartz_private.h"

struct splitter;

struct splitter_sample
{
    LONGLONG offset;
    DWORD size;
    REFERENCE_TIME start, stop;
    BOOL has_time;
    BOOL sync_point;
};

struct splitter_source
{
    struct strmbase_source pin;
    IQualityControl IQualityControl_iface;
    IMediaPosition IMediaPosition_iface;
    SourceSeeking seek;

    const GUID *time_format;
    const GUID *unit_formats[2];
    unsigned int unit_format_count;
    LONGLONG unit_time_num, unit_time_den;

    CRITICAL_SECTION flushing_cs;
    CONDITION_VARIABLE eos_cv;
    HANDLE thread;
    BOOL need_segment, eos, discontinuity;

    AM_MEDIA_TYPE *media_types;
    unsigned int media_type_count;
    DWORD buffer_size, buffer_count;

    unsigned int index;
    void *priv;
};

struct splitter_ops
{
    HRESULT (*sink_query_accept)(const AM_MEDIA_TYPE *mt);
    HRESULT (*sink_get_media_type)(unsigned int index, AM_MEDIA_TYPE *mt);
    HRESULT (*sink_query_interface)(struct splitter *filter, REFIID iid, void **out);
    HRESULT (*parse)(struct splitter *filter);
    void (*cleanup)(struct splitter *filter);
    void (*destroy)(struct splitter *filter);
    void (*seek)(struct splitter_source *source, REFERENCE_TIME time);
    HRESULT (*next_sample)(struct splitter_source *source, struct splitter_sample *sample);
    HRESULT (*query_interface)(struct splitter *filter, REFIID iid, void **out);
};

struct splitter
{
    struct strmbase_filter filter;
    struct strmbase_sink sink;

    IAsyncReader *reader;
    LONGLONG file_size;

    const struct splitter_ops *ops;
    struct splitter_source **sources;
    unsigned int source_count;
    BOOL enum_sink_first;
    BOOL no_source_position;

    CRITICAL_SECTION streaming_cs;
    CONDITION_VARIABLE flushing_cv;
    BOOL streaming, flushing, sink_connected;

    void *priv;
};

HRESULT splitter_create(IUnknown *outer, const CLSID *clsid, const struct splitter_ops *ops,
        const WCHAR *sink_name, BOOL enum_sink_first, struct splitter **out);
struct splitter_source *splitter_add_source(struct splitter *filter, const WCHAR *name,
        const AM_MEDIA_TYPE *media_types, unsigned int media_type_count, REFERENCE_TIME duration);
HRESULT splitter_read(struct splitter *filter, LONGLONG offset, LONG size, void *data);
void splitter_init_wave_media_type(AM_MEDIA_TYPE *mt, const WAVEFORMATEX *wfx, DWORD size);

#endif /* __QUARTZ_SPLITTER_H */
