/*
 * File writer filter
 *
 * Copyright (C) 2020 Zebediah Figura
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

struct file_writer
{
    struct strmbase_filter filter;
    IAMFilterMiscFlags IAMFilterMiscFlags_iface;
    IFileSinkFilter2 IFileSinkFilter2_iface;
    IStream IStream_iface;
    IQualityControl IQualityControl_iface;

    struct strmbase_sink sink;

    WCHAR *filename;
    GUID majortype, subtype;
    DWORD mode;
    HANDLE file;

    BOOL eos;
};

static inline struct file_writer *impl_from_strmbase_pin(struct strmbase_pin *iface)
{
    return CONTAINING_RECORD(iface, struct file_writer, sink.pin);
}

static HRESULT file_writer_sink_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    struct file_writer *filter = impl_from_strmbase_pin(iface);

    if (IsEqualGUID(iid, &IID_IMemInputPin))
        *out = &filter->sink.IMemInputPin_iface;
    else if (IsEqualGUID(iid, &IID_IStream))
        *out = &filter->IStream_iface;
    else if (IsEqualGUID(iid, &IID_IQualityControl))
        *out = &filter->IQualityControl_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT file_writer_sink_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    struct file_writer *filter = impl_from_strmbase_pin(iface);

    if (!IsEqualGUID(&filter->majortype, &GUID_NULL) && !IsEqualGUID(&mt->majortype, &filter->majortype))
        return S_FALSE;
    if (!IsEqualGUID(&filter->subtype, &GUID_NULL) && !IsEqualGUID(&mt->subtype, &filter->subtype))
        return S_FALSE;
    return S_OK;
}

static HRESULT WINAPI file_writer_sink_receive(struct strmbase_sink *iface, IMediaSample *sample)
{
    struct file_writer *filter = impl_from_strmbase_pin(&iface->pin);
    REFERENCE_TIME start, stop;
    LARGE_INTEGER offset;
    DWORD size, ret_size;
    HRESULT hr;
    BYTE *data;

    if ((hr = IMediaSample_GetTime(sample, &start, &stop)) != S_OK)
        ERR("Failed to get sample time, hr %#lx.\n", hr);
    size = stop - start;

    if ((hr = IMediaSample_GetPointer(sample, &data)) != S_OK)
        ERR("Failed to get sample pointer, hr %#lx.\n", hr);

    offset.QuadPart = start;
    if (!SetFilePointerEx(filter->file, offset, NULL, FILE_BEGIN)
            || !WriteFile(filter->file, data, size, &ret_size, NULL))
    {
        ERR("Failed to write file, error %lu.\n", GetLastError());
        return HRESULT_FROM_WIN32(hr);
    }

    if (ret_size != size)
        ERR("Short write, %lu/%lu.\n", ret_size, size);

    return S_OK;
}

static void deliver_ec_complete(struct file_writer *filter)
{
    IMediaEventSink *event_sink;

    if (SUCCEEDED(IFilterGraph_QueryInterface(filter->filter.graph,
            &IID_IMediaEventSink, (void **)&event_sink)))
    {
        IMediaEventSink_Notify(event_sink, EC_COMPLETE, S_OK,
                (LONG_PTR)&filter->filter.IBaseFilter_iface);
        IMediaEventSink_Release(event_sink);
    }
}

static HRESULT file_writer_sink_eos(struct strmbase_sink *iface)
{
    struct file_writer *filter = impl_from_strmbase_pin(&iface->pin);

    EnterCriticalSection(&filter->filter.filter_cs);

    if (filter->filter.state == State_Running)
        deliver_ec_complete(filter);
    else
        filter->eos = TRUE;

    LeaveCriticalSection(&filter->filter.filter_cs);
    return S_OK;
}

static void close_file(struct file_writer *filter)
{
    if (filter->file)
        CloseHandle(filter->file);
    filter->file = NULL;
}

static void file_writer_sink_disconnect(struct strmbase_sink *iface)
{
    close_file(impl_from_strmbase_pin(&iface->pin));
}

static HRESULT file_writer_sink_get_allocator(struct strmbase_sink *iface, IMemAllocator **allocator)
{
    *allocator = NULL;
    return E_INVALIDARG;
}

static HRESULT file_writer_sink_get_allocator_requirements(struct strmbase_sink *iface, ALLOCATOR_PROPERTIES *props)
{
    memset(props, 0, sizeof(*props));
    props->cbAlign = 512;
    return S_OK;
}

static const struct strmbase_sink_ops sink_ops =
{
    .base.pin_query_interface = file_writer_sink_query_interface,
    .base.pin_query_accept = file_writer_sink_query_accept,
    .pfnReceive = file_writer_sink_receive,
    .sink_eos = file_writer_sink_eos,
    .sink_disconnect = file_writer_sink_disconnect,
    .sink_get_allocator = file_writer_sink_get_allocator,
    .sink_get_allocator_requirements = file_writer_sink_get_allocator_requirements,
};

static inline struct file_writer *impl_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct file_writer, filter);
}

static HRESULT file_writer_query_interface(struct strmbase_filter *iface, REFIID iid, void **out)
{
    struct file_writer *filter = impl_from_strmbase_filter(iface);

    if (IsEqualGUID(iid, &IID_IAMFilterMiscFlags))
        *out = &filter->IAMFilterMiscFlags_iface;
    else if (IsEqualGUID(iid, &IID_IFileSinkFilter) || IsEqualGUID(iid, &IID_IFileSinkFilter2))
        *out = &filter->IFileSinkFilter2_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static struct strmbase_pin *file_writer_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct file_writer *filter = impl_from_strmbase_filter(iface);

    if (!index)
        return &filter->sink.pin;
    return NULL;
}

static void file_writer_destroy(struct strmbase_filter *iface)
{
    struct file_writer *filter = impl_from_strmbase_filter(iface);

    close_file(filter);
    free(filter->filename);
    strmbase_sink_cleanup(&filter->sink);
    strmbase_filter_cleanup(&filter->filter);
    free(filter);
}

static HRESULT file_writer_init_stream(struct strmbase_filter *iface)
{
    struct file_writer *filter = impl_from_strmbase_filter(iface);
    HANDLE file;

    close_file(filter);
    if ((file = CreateFileW(filter->filename, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, (filter->mode & AM_FILE_OVERWRITE) ? CREATE_ALWAYS : OPEN_ALWAYS, 0, NULL)) == INVALID_HANDLE_VALUE)
    {
        ERR("Failed to create %s, error %lu.\n", debugstr_w(filter->filename), GetLastError());
        return HRESULT_FROM_WIN32(GetLastError());
    }
    filter->file = file;
    return S_OK;
}

static HRESULT file_writer_start_stream(struct strmbase_filter *iface, REFERENCE_TIME start)
{
    struct file_writer *filter = impl_from_strmbase_filter(iface);

    if (filter->eos)
        deliver_ec_complete(filter);
    filter->eos = FALSE;
    return S_OK;
}

static HRESULT file_writer_cleanup_stream(struct strmbase_filter *iface)
{
    struct file_writer *filter = impl_from_strmbase_filter(iface);

    if (!filter->sink.pin.peer)
        return S_OK;
    if (!filter->sink.pAllocator)
        return VFW_E_NO_ALLOCATOR;
    return IMemAllocator_Decommit(filter->sink.pAllocator);
}

static struct strmbase_filter_ops filter_ops =
{
    .filter_query_interface = file_writer_query_interface,
    .filter_get_pin = file_writer_get_pin,
    .filter_destroy = file_writer_destroy,
    .filter_init_stream = file_writer_init_stream,
    .filter_start_stream = file_writer_start_stream,
    .filter_cleanup_stream = file_writer_cleanup_stream,
};

static inline struct file_writer *impl_from_IFileSinkFilter2(IFileSinkFilter2 *iface)
{
    return CONTAINING_RECORD(iface, struct file_writer, IFileSinkFilter2_iface);
}

static HRESULT WINAPI filesinkfilter_QueryInterface(IFileSinkFilter2 *iface, REFIID iid, void **out)
{
    struct file_writer *filter = impl_from_IFileSinkFilter2(iface);
    return IUnknown_QueryInterface(filter->filter.outer_unk, iid, out);
}

static ULONG WINAPI filesinkfilter_AddRef(IFileSinkFilter2 *iface)
{
    struct file_writer *filter = impl_from_IFileSinkFilter2(iface);
    return IUnknown_AddRef(filter->filter.outer_unk);
}

static ULONG WINAPI filesinkfilter_Release(IFileSinkFilter2 *iface)
{
    struct file_writer *filter = impl_from_IFileSinkFilter2(iface);
    return IUnknown_Release(filter->filter.outer_unk);
}

static HRESULT WINAPI filesinkfilter_SetFileName(IFileSinkFilter2 *iface,
        LPCOLESTR filename, const AM_MEDIA_TYPE *mt)
{
    struct file_writer *filter = impl_from_IFileSinkFilter2(iface);
    WCHAR *new_filename;

    TRACE("filter %p, filename %s, mt %p.\n", filter, debugstr_w(filename), mt);
    strmbase_dump_media_type(mt);

    if (!filename)
        return E_POINTER;

    if (!(new_filename = wcsdup(filename)))
        return E_OUTOFMEMORY;

    free(filter->filename);
    filter->filename = new_filename;
    filter->majortype = mt ? mt->majortype : MEDIATYPE_Stream;
    filter->subtype = mt ? mt->subtype : GUID_NULL;
    return S_OK;
}

static HRESULT WINAPI filesinkfilter_GetCurFile(IFileSinkFilter2 *iface,
        LPOLESTR *filename, AM_MEDIA_TYPE *mt)
{
    struct file_writer *filter = impl_from_IFileSinkFilter2(iface);

    TRACE("filter %p, filename %p, mt %p.\n", filter, filename, mt);

    if (!filename)
        return E_POINTER;

    *filename = NULL;
    if (filter->filename)
    {
        size_t size = (wcslen(filter->filename) + 1) * sizeof(WCHAR);

        if (!(*filename = CoTaskMemAlloc(size)))
            return E_OUTOFMEMORY;
        memcpy(*filename, filter->filename, size);
    }

    if (mt)
    {
        mt->majortype = filter->majortype;
        mt->subtype = filter->subtype;
    }
    return S_OK;
}

static HRESULT WINAPI filesinkfilter_SetMode(IFileSinkFilter2 *iface, DWORD mode)
{
    struct file_writer *filter = impl_from_IFileSinkFilter2(iface);

    TRACE("filter %p, mode %#lx.\n", filter, mode);

    if (mode & ~AM_FILE_OVERWRITE)
        return E_INVALIDARG;

    filter->mode = mode;
    return S_OK;
}

static HRESULT WINAPI filesinkfilter_GetMode(IFileSinkFilter2 *iface, DWORD *mode)
{
    struct file_writer *filter = impl_from_IFileSinkFilter2(iface);

    TRACE("filter %p, mode %p.\n", filter, mode);

    if (!mode)
        return E_POINTER;

    *mode = filter->mode;
    return S_OK;
}

static const IFileSinkFilter2Vtbl filesinkfilter_vtbl =
{
    filesinkfilter_QueryInterface,
    filesinkfilter_AddRef,
    filesinkfilter_Release,
    filesinkfilter_SetFileName,
    filesinkfilter_GetCurFile,
    filesinkfilter_SetMode,
    filesinkfilter_GetMode,
};

static inline struct file_writer *impl_from_IAMFilterMiscFlags(IAMFilterMiscFlags *iface)
{
    return CONTAINING_RECORD(iface, struct file_writer, IAMFilterMiscFlags_iface);
}

static HRESULT WINAPI misc_flags_QueryInterface(IAMFilterMiscFlags *iface, REFIID iid, void **out)
{
    struct file_writer *filter = impl_from_IAMFilterMiscFlags(iface);
    return IUnknown_QueryInterface(filter->filter.outer_unk, iid, out);
}

static ULONG WINAPI misc_flags_AddRef(IAMFilterMiscFlags *iface)
{
    struct file_writer *filter = impl_from_IAMFilterMiscFlags(iface);
    return IUnknown_AddRef(filter->filter.outer_unk);
}

static ULONG WINAPI misc_flags_Release(IAMFilterMiscFlags *iface)
{
    struct file_writer *filter = impl_from_IAMFilterMiscFlags(iface);
    return IUnknown_Release(filter->filter.outer_unk);
}

static ULONG WINAPI misc_flags_GetMiscFlags(IAMFilterMiscFlags *iface)
{
    struct file_writer *filter = impl_from_IAMFilterMiscFlags(iface);

    TRACE("filter %p.\n", filter);

    return AM_FILTER_MISC_FLAGS_IS_RENDERER;
}

static const IAMFilterMiscFlagsVtbl misc_flags_vtbl =
{
    misc_flags_QueryInterface,
    misc_flags_AddRef,
    misc_flags_Release,
    misc_flags_GetMiscFlags,
};

static inline struct file_writer *impl_from_IStream(IStream *iface)
{
    return CONTAINING_RECORD(iface, struct file_writer, IStream_iface);
}

static HRESULT WINAPI stream_QueryInterface(IStream *iface, REFIID iid, void **out)
{
    struct file_writer *filter = impl_from_IStream(iface);
    return IPin_QueryInterface(&filter->sink.pin.IPin_iface, iid, out);
}

static ULONG WINAPI stream_AddRef(IStream *iface)
{
    struct file_writer *filter = impl_from_IStream(iface);
    return IPin_AddRef(&filter->sink.pin.IPin_iface);
}

static ULONG WINAPI stream_Release(IStream *iface)
{
    struct file_writer *filter = impl_from_IStream(iface);
    return IPin_Release(&filter->sink.pin.IPin_iface);
}

static HRESULT WINAPI stream_Read(IStream *iface, void *data, ULONG size, ULONG *ret_size)
{
    struct file_writer *filter = impl_from_IStream(iface);
    DWORD read = 0;

    TRACE("filter %p, data %p, size %lu, ret_size %p.\n", filter, data, size, ret_size);

    if (!filter->file)
        return E_UNEXPECTED;
    if (!ReadFile(filter->file, data, size, &read, NULL))
        return HRESULT_FROM_WIN32(GetLastError());
    if (ret_size)
        *ret_size = read;
    return S_OK;
}

static HRESULT WINAPI stream_Write(IStream *iface, const void *data, ULONG size, ULONG *ret_size)
{
    struct file_writer *filter = impl_from_IStream(iface);
    DWORD written = 0;

    TRACE("filter %p, data %p, size %lu, ret_size %p.\n", filter, data, size, ret_size);

    if (!filter->file)
        return E_UNEXPECTED;
    if (!WriteFile(filter->file, data, size, &written, NULL))
        return HRESULT_FROM_WIN32(GetLastError());
    if (ret_size)
        *ret_size = written;
    return S_OK;
}

static HRESULT WINAPI stream_Seek(IStream *iface, LARGE_INTEGER offset, DWORD origin, ULARGE_INTEGER *ret_pos)
{
    struct file_writer *filter = impl_from_IStream(iface);
    LARGE_INTEGER pos;

    TRACE("filter %p, offset %s, origin %lu, ret_pos %p.\n", filter,
            wine_dbgstr_longlong(offset.QuadPart), origin, ret_pos);

    if (!filter->file)
        return E_UNEXPECTED;
    if (origin != STREAM_SEEK_SET && origin != STREAM_SEEK_CUR && origin != STREAM_SEEK_END)
        return STG_E_INVALIDFUNCTION;
    if (!SetFilePointerEx(filter->file, offset, &pos, origin))
        return HRESULT_FROM_WIN32(GetLastError());
    if (ret_pos)
        ret_pos->QuadPart = pos.QuadPart;
    return S_OK;
}

static HRESULT WINAPI stream_SetSize(IStream *iface, ULARGE_INTEGER size)
{
    struct file_writer *filter = impl_from_IStream(iface);
    LARGE_INTEGER zero = {{0}}, pos, end;
    HRESULT hr = S_OK;

    TRACE("filter %p, size %s.\n", filter, wine_dbgstr_longlong(size.QuadPart));

    if (!filter->file)
        return E_UNEXPECTED;
    end.QuadPart = size.QuadPart;
    if (!SetFilePointerEx(filter->file, zero, &pos, FILE_CURRENT)
            || !SetFilePointerEx(filter->file, end, NULL, FILE_BEGIN))
        return HRESULT_FROM_WIN32(GetLastError());
    if (!SetEndOfFile(filter->file))
        hr = HRESULT_FROM_WIN32(GetLastError());
    SetFilePointerEx(filter->file, pos, NULL, FILE_BEGIN);
    return hr;
}

static HRESULT WINAPI stream_CopyTo(IStream *iface, IStream *dest, ULARGE_INTEGER size,
        ULARGE_INTEGER *ret_read, ULARGE_INTEGER *ret_written)
{
    TRACE("iface %p, dest %p, size %s, ret_read %p, ret_written %p.\n", iface, dest,
            wine_dbgstr_longlong(size.QuadPart), ret_read, ret_written);
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_Commit(IStream *iface, DWORD flags)
{
    TRACE("iface %p, flags %#lx.\n", iface, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_Revert(IStream *iface)
{
    TRACE("iface %p.\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_LockRegion(IStream *iface, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    TRACE("iface %p, offset %s, size %s, type %lu.\n", iface,
            wine_dbgstr_longlong(offset.QuadPart), wine_dbgstr_longlong(size.QuadPart), type);
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_UnlockRegion(IStream *iface, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    TRACE("iface %p, offset %s, size %s, type %lu.\n", iface,
            wine_dbgstr_longlong(offset.QuadPart), wine_dbgstr_longlong(size.QuadPart), type);
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_Stat(IStream *iface, STATSTG *stat, DWORD flags)
{
    TRACE("iface %p, stat %p, flags %#lx.\n", iface, stat, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_Clone(IStream *iface, IStream **out)
{
    TRACE("iface %p, out %p.\n", iface, out);
    return E_NOTIMPL;
}

static const IStreamVtbl stream_vtbl =
{
    stream_QueryInterface,
    stream_AddRef,
    stream_Release,
    stream_Read,
    stream_Write,
    stream_Seek,
    stream_SetSize,
    stream_CopyTo,
    stream_Commit,
    stream_Revert,
    stream_LockRegion,
    stream_UnlockRegion,
    stream_Stat,
    stream_Clone,
};

static inline struct file_writer *impl_from_IQualityControl(IQualityControl *iface)
{
    return CONTAINING_RECORD(iface, struct file_writer, IQualityControl_iface);
}

static HRESULT WINAPI quality_control_QueryInterface(IQualityControl *iface, REFIID iid, void **out)
{
    struct file_writer *filter = impl_from_IQualityControl(iface);
    return IPin_QueryInterface(&filter->sink.pin.IPin_iface, iid, out);
}

static ULONG WINAPI quality_control_AddRef(IQualityControl *iface)
{
    struct file_writer *filter = impl_from_IQualityControl(iface);
    return IPin_AddRef(&filter->sink.pin.IPin_iface);
}

static ULONG WINAPI quality_control_Release(IQualityControl *iface)
{
    struct file_writer *filter = impl_from_IQualityControl(iface);
    return IPin_Release(&filter->sink.pin.IPin_iface);
}

static HRESULT WINAPI quality_control_Notify(IQualityControl *iface, IBaseFilter *sender, Quality q)
{
    struct file_writer *filter = impl_from_IQualityControl(iface);

    TRACE("filter %p, sender %p, type %#x, proportion %ld, late %s, timestamp %s.\n",
            filter, sender, q.Type, q.Proportion,
            wine_dbgstr_longlong(q.Late), wine_dbgstr_longlong(q.TimeStamp));

    if (!sender)
        return E_POINTER;
    return S_OK;
}

static HRESULT WINAPI quality_control_SetSink(IQualityControl *iface, IQualityControl *sink)
{
    struct file_writer *filter = impl_from_IQualityControl(iface);

    TRACE("filter %p, sink %p.\n", filter, sink);

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

HRESULT file_writer_create(IUnknown *outer, IUnknown **out)
{
    struct file_writer *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    strmbase_filter_init(&object->filter, outer, &CLSID_FileWriter, &filter_ops);
    object->IFileSinkFilter2_iface.lpVtbl = &filesinkfilter_vtbl;
    object->IAMFilterMiscFlags_iface.lpVtbl = &misc_flags_vtbl;
    object->IStream_iface.lpVtbl = &stream_vtbl;
    object->IQualityControl_iface.lpVtbl = &quality_control_vtbl;

    strmbase_sink_init(&object->sink, &object->filter, L"in", &sink_ops, NULL);

    TRACE("Created file writer %p.\n", object);
    *out = &object->filter.IUnknown_inner;
    return S_OK;
}
