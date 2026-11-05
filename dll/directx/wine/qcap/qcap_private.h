/*
 * DirectShow capture
 *
 * Copyright (C) 2005 Rolf Kalbermatter
 * Copyright 2005 Maarten Lankhorst
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
#ifndef _QCAP_PRIVATE_H_DEFINED
#define _QCAP_PRIVATE_H_DEFINED

#define COBJMACROS
#include <stdbool.h>
#include "dshow.h"
#include "winternl.h"
#ifndef __REACTOS__
#include "wine/unixlib.h"
#endif
#include "wine/debug.h"
#include "wine/strmbase.h"

HRESULT audio_record_create(IUnknown *outer, IUnknown **out);
HRESULT avi_compressor_create(IUnknown *outer, IUnknown **out);
HRESULT avi_mux_create(IUnknown *outer, IUnknown **out);
HRESULT capture_graph_create(IUnknown *outer, IUnknown **out);
HRESULT file_writer_create(IUnknown *outer, IUnknown **out);
HRESULT smart_tee_create(IUnknown *outer, IUnknown **out);
HRESULT vfw_capture_create(IUnknown *outer, IUnknown **out);
#ifdef __REACTOS__
HRESULT ks_proxy_create(IUnknown *outer, IUnknown **out);
#endif

typedef UINT64 video_capture_device_t;

struct create_params
{
#ifdef __REACTOS__
    const WCHAR                  *path;
#else
    unsigned int                  index;
#endif
    video_capture_device_t       *device;
};

struct start_params
{
    video_capture_device_t       device;
};

struct destroy_params
{
    video_capture_device_t       device;
};

struct check_format_params
{
    video_capture_device_t       device;
    const AM_MEDIA_TYPE         *mt;
};

struct set_format_params
{
    video_capture_device_t       device;
    const AM_MEDIA_TYPE         *mt;
};

struct get_format_params
{
    video_capture_device_t       device;
    AM_MEDIA_TYPE               *mt;
    VIDEOINFOHEADER             *format;
};

struct get_media_type_params
{
    video_capture_device_t       device;
    unsigned int                 index;
    AM_MEDIA_TYPE               *mt;
    VIDEOINFOHEADER             *format;
};

struct get_caps_params
{
    video_capture_device_t       device;
    unsigned int                 index;
    AM_MEDIA_TYPE               *mt;
    VIDEOINFOHEADER             *format;
    VIDEO_STREAM_CONFIG_CAPS    *caps;
};

struct get_caps_count_params
{
    video_capture_device_t       device;
    int                         *count;
};

struct get_frame_intervals_params
{
    video_capture_device_t       device;
    unsigned int                 caps_index;
    unsigned int                 width;
    unsigned int                 height;
    unsigned int                *count;
    LONGLONG                    *intervals;
};

struct get_prop_range_params
{
    video_capture_device_t       device;
    VideoProcAmpProperty         property;
    LONG                        *min;
    LONG                        *max;
    LONG                        *step;
    LONG                        *default_value;
    LONG                        *flags;
};

struct get_prop_params
{
    video_capture_device_t       device;
    VideoProcAmpProperty         property;
    LONG                        *value;
    LONG                        *flags;
};

struct set_prop_params
{
    video_capture_device_t       device;
    VideoProcAmpProperty         property;
    LONG                         value;
    LONG                         flags;
};

struct read_frame_params
{
    video_capture_device_t       device;
    void                        *data;
#ifdef __REACTOS__
    unsigned int                 size;
#endif
};

enum unix_funcs
{
    unix_create,
    unix_start,
    unix_destroy,
    unix_check_format,
    unix_set_format,
    unix_get_format,
    unix_get_media_type,
    unix_get_caps,
    unix_get_caps_count,
    unix_get_frame_intervals,
    unix_get_prop_range,
    unix_get_prop,
    unix_set_prop,
    unix_read_frame,
    unix_funcs_count
};

#ifdef __REACTOS__
NTSTATUS qcap_backend_call(enum unix_funcs func, void *params);
void qcap_backend_stop(video_capture_device_t device);
const BITMAPINFOHEADER *qcap_backend_format_bmi(const AM_MEDIA_TYPE *mt);
#define QCAP_MAX_FORMAT_SIZE 256
int qcap_backend_still_count(video_capture_device_t device);
HRESULT qcap_backend_still_media_type(video_capture_device_t device, unsigned int index, AM_MEDIA_TYPE *mt, VIDEO_STREAM_CONFIG_CAPS *caps);
int qcap_backend_still_find(video_capture_device_t device, const AM_MEDIA_TYPE *mt);
HRESULT qcap_backend_still_start(video_capture_device_t device, int index);
void qcap_backend_still_stop(video_capture_device_t device);
HRESULT qcap_backend_still_read(video_capture_device_t device, BYTE *data, DWORD size, DWORD *used, HANDLE stop);
HRESULT qcap_backend_control_get(video_capture_device_t device, ULONG stream, BOOL mode, LONG *value);
HRESULT qcap_backend_control_set_mode(video_capture_device_t device, ULONG stream, LONG mode);
HRESULT qcap_backend_max_frame_rate(video_capture_device_t device, ULONG stream, ULONG range, SIZE dimensions, LONGLONG *rate);
HRESULT qcap_backend_frame_rates(video_capture_device_t device, ULONG stream, ULONG range, SIZE dimensions, LONG *count, LONGLONG **rates);
HRESULT qcap_backend_camera_range(video_capture_device_t device, LONG property, LONG *min, LONG *max, LONG *step, LONG *default_value, LONG *flags);
HRESULT qcap_backend_camera_get(video_capture_device_t device, LONG property, LONG *value, LONG *flags);
HRESULT qcap_backend_camera_set(video_capture_device_t device, LONG property, LONG value, LONG flags);
#endif

#endif
