/*
 * File writer filter unit tests
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

#define COBJMACROS
#include "dshow.h"
#include "wine/strmbase.h"
#include "wine/test.h"

static IBaseFilter *create_file_writer(void)
{
    IBaseFilter *filter = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_FileWriter, NULL,
            CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&filter);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    return filter;
}

static WCHAR *set_filename(IBaseFilter *filter)
{
    static WCHAR filename[MAX_PATH];
    IFileSinkFilter *filesink;
    WCHAR path[MAX_PATH];
    HRESULT hr;

    GetTempPathW(ARRAY_SIZE(path), path);
    GetTempFileNameW(path, L"qfw", 0, filename);
    DeleteFileW(filename);

    IBaseFilter_QueryInterface(filter, &IID_IFileSinkFilter, (void **)&filesink);
    hr = IFileSinkFilter_SetFileName(filesink, filename, NULL);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    IFileSinkFilter_Release(filesink);
    return filename;
}

static ULONG get_refcount(void *iface)
{
    IUnknown *unknown = iface;
    IUnknown_AddRef(unknown);
    return IUnknown_Release(unknown);
}

#define check_interface(a, b, c) check_interface_(__LINE__, a, b, c)
static void check_interface_(unsigned int line, void *iface_ptr, REFIID iid, BOOL supported)
{
    IUnknown *iface = iface_ptr;
    HRESULT hr, expected_hr;
    IUnknown *unk;

    expected_hr = supported ? S_OK : E_NOINTERFACE;

    hr = IUnknown_QueryInterface(iface, iid, (void **)&unk);
    ok_(__FILE__, line)(hr == expected_hr, "Got hr %#lx, expected %#lx.\n", hr, expected_hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(unk);
}

static void test_interfaces(void)
{
    IBaseFilter *filter = create_file_writer();
    ULONG ref;
    IPin *pin;

    check_interface(filter, &IID_IAMFilterMiscFlags, TRUE);
    check_interface(filter, &IID_IBaseFilter, TRUE);
    check_interface(filter, &IID_IFileSinkFilter, TRUE);
    todo_wine check_interface(filter, &IID_IFileSinkFilter2, TRUE);
    check_interface(filter, &IID_IMediaFilter, TRUE);
    check_interface(filter, &IID_IPersist, TRUE);
    todo_wine check_interface(filter, &IID_IPersistStream, TRUE);
    check_interface(filter, &IID_IUnknown, TRUE);

    check_interface(filter, &IID_IBasicAudio, FALSE);
    check_interface(filter, &IID_IBasicVideo, FALSE);
    check_interface(filter, &IID_IKsPropertySet, FALSE);
    check_interface(filter, &IID_IMediaPosition, FALSE);
    check_interface(filter, &IID_IMediaSeeking, FALSE);
    check_interface(filter, &IID_IPersistPropertyBag, FALSE);
    check_interface(filter, &IID_IPin, FALSE);
    check_interface(filter, &IID_IQualityControl, FALSE);
    check_interface(filter, &IID_IQualProp, FALSE);
    check_interface(filter, &IID_IReferenceClock, FALSE);
    check_interface(filter, &IID_IVideoWindow, FALSE);

    IBaseFilter_FindPin(filter, L"in", &pin);

    check_interface(pin, &IID_IMemInputPin, TRUE);
    check_interface(pin, &IID_IPin, TRUE);
    todo_wine check_interface(pin, &IID_IQualityControl, TRUE);
    check_interface(pin, &IID_IUnknown, TRUE);

    check_interface(pin, &IID_IKsPropertySet, FALSE);
    check_interface(pin, &IID_IMediaPosition, FALSE);
    check_interface(pin, &IID_IMediaSeeking, FALSE);

    IPin_Release(pin);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got unexpected refcount %ld.\n", ref);
}

static const GUID test_iid = {0x33333333};
static LONG outer_ref = 1;

static HRESULT WINAPI outer_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IBaseFilter)
            || IsEqualGUID(iid, &test_iid))
    {
        *out = (IUnknown *)0xdeadbeef;
        return S_OK;
    }
    ok(0, "unexpected call %s\n", wine_dbgstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI outer_AddRef(IUnknown *iface)
{
    return InterlockedIncrement(&outer_ref);
}

static ULONG WINAPI outer_Release(IUnknown *iface)
{
    return InterlockedDecrement(&outer_ref);
}

static const IUnknownVtbl outer_vtbl =
{
    outer_QueryInterface,
    outer_AddRef,
    outer_Release,
};

static IUnknown test_outer = {&outer_vtbl};

static void test_aggregation(void)
{
    IBaseFilter *filter, *filter2;
    IUnknown *unk, *unk2;
    HRESULT hr;
    ULONG ref;

    filter = (IBaseFilter *)0xdeadbeef;
    hr = CoCreateInstance(&CLSID_FileWriter, &test_outer, CLSCTX_INPROC_SERVER,
            &IID_IBaseFilter, (void **)&filter);
    ok(hr == E_NOINTERFACE, "Got hr %#lx.\n", hr);
    ok(!filter, "Got interface %p.\n", filter);

    hr = CoCreateInstance(&CLSID_FileWriter, &test_outer, CLSCTX_INPROC_SERVER,
            &IID_IUnknown, (void **)&unk);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(outer_ref == 1, "Got unexpected refcount %ld.\n", outer_ref);
    ok(unk != &test_outer, "Returned IUnknown should not be outer IUnknown.\n");
    ref = get_refcount(unk);
    ok(ref == 1, "Got unexpected refcount %ld.\n", ref);

    ref = IUnknown_AddRef(unk);
    ok(ref == 2, "Got unexpected refcount %ld.\n", ref);
    ok(outer_ref == 1, "Got unexpected refcount %ld.\n", outer_ref);

    ref = IUnknown_Release(unk);
    ok(ref == 1, "Got unexpected refcount %ld.\n", ref);
    ok(outer_ref == 1, "Got unexpected refcount %ld.\n", outer_ref);

    hr = IUnknown_QueryInterface(unk, &IID_IUnknown, (void **)&unk2);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(unk2 == unk, "Got unexpected IUnknown %p.\n", unk2);
    IUnknown_Release(unk2);

    hr = IUnknown_QueryInterface(unk, &IID_IBaseFilter, (void **)&filter);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IBaseFilter_QueryInterface(filter, &IID_IUnknown, (void **)&unk2);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(unk2 == (IUnknown *)0xdeadbeef, "Got unexpected IUnknown %p.\n", unk2);

    hr = IBaseFilter_QueryInterface(filter, &IID_IBaseFilter, (void **)&filter2);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(filter2 == (IBaseFilter *)0xdeadbeef, "Got unexpected IBaseFilter %p.\n", filter2);

    hr = IUnknown_QueryInterface(unk, &test_iid, (void **)&unk2);
    ok(hr == E_NOINTERFACE, "Got hr %#lx.\n", hr);
    ok(!unk2, "Got unexpected IUnknown %p.\n", unk2);

    hr = IBaseFilter_QueryInterface(filter, &test_iid, (void **)&unk2);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(unk2 == (IUnknown *)0xdeadbeef, "Got unexpected IUnknown %p.\n", unk2);

    IBaseFilter_Release(filter);
    ref = IUnknown_Release(unk);
    ok(!ref, "Got unexpected refcount %ld.\n", ref);
    ok(outer_ref == 1, "Got unexpected refcount %ld.\n", outer_ref);
}

static void test_enum_pins(void)
{
    IBaseFilter *filter = create_file_writer();
    IEnumPins *enum1, *enum2;
    ULONG count, ref;
    IPin *pins[2];
    HRESULT hr;

    ref = get_refcount(filter);
    ok(ref == 1, "Got unexpected refcount %ld.\n", ref);

    hr = IBaseFilter_EnumPins(filter, NULL);
    ok(hr == E_POINTER, "Got hr %#lx.\n", hr);

    hr = IBaseFilter_EnumPins(filter, &enum1);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ref = get_refcount(filter);
    ok(ref == 2, "Got unexpected refcount %ld.\n", ref);
    ref = get_refcount(enum1);
    ok(ref == 1, "Got unexpected refcount %ld.\n", ref);

    hr = IEnumPins_Next(enum1, 1, NULL, NULL);
    ok(hr == E_POINTER, "Got hr %#lx.\n", hr);

    hr = IEnumPins_Next(enum1, 1, pins, NULL);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ref = get_refcount(filter);
    ok(ref == 3, "Got unexpected refcount %ld.\n", ref);
    ref = get_refcount(pins[0]);
    ok(ref == 3, "Got unexpected refcount %ld.\n", ref);
    ref = get_refcount(enum1);
    ok(ref == 1, "Got unexpected refcount %ld.\n", ref);
    IPin_Release(pins[0]);
    ref = get_refcount(filter);
    ok(ref == 2, "Got unexpected refcount %ld.\n", ref);

    hr = IEnumPins_Next(enum1, 1, pins, NULL);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    hr = IEnumPins_Reset(enum1);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IEnumPins_Next(enum1, 1, pins, &count);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(count == 1, "Got count %lu.\n", count);
    IPin_Release(pins[0]);

    hr = IEnumPins_Next(enum1, 1, pins, &count);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    ok(!count, "Got count %lu.\n", count);

    hr = IEnumPins_Reset(enum1);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IEnumPins_Next(enum1, 2, pins, NULL);
    ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

    hr = IEnumPins_Next(enum1, 2, pins, &count);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    ok(count == 1, "Got count %lu.\n", count);
    IPin_Release(pins[0]);

    hr = IEnumPins_Reset(enum1);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IEnumPins_Clone(enum1, &enum2);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IEnumPins_Skip(enum1, 2);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    hr = IEnumPins_Skip(enum1, 1);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IEnumPins_Skip(enum1, 1);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    hr = IEnumPins_Next(enum1, 1, pins, NULL);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    hr = IEnumPins_Next(enum2, 1, pins, NULL);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    IPin_Release(pins[0]);

    IEnumPins_Release(enum2);
    IEnumPins_Release(enum1);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
}

static void test_find_pin(void)
{
    IBaseFilter *filter = create_file_writer();
    IEnumPins *enum_pins;
    IPin *pin, *pin2;
    HRESULT hr;
    ULONG ref;

    hr = IBaseFilter_EnumPins(filter, &enum_pins);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IBaseFilter_FindPin(filter, L"in", &pin);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IEnumPins_Next(enum_pins, 1, &pin2, NULL);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(pin2 == pin, "Expected pin %p, got %p.\n", pin, pin2);
    IPin_Release(pin2);
    IPin_Release(pin);

    IEnumPins_Release(enum_pins);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
}

static void test_pin_info(void)
{
    IBaseFilter *filter = create_file_writer();
    PIN_DIRECTION dir;
    PIN_INFO info;
    HRESULT hr;
    WCHAR *id;
    ULONG ref;
    IPin *pin;

    hr = IBaseFilter_FindPin(filter, L"in", &pin);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ref = get_refcount(filter);
    ok(ref == 2, "Got unexpected refcount %ld.\n", ref);
    ref = get_refcount(pin);
    ok(ref == 2, "Got unexpected refcount %ld.\n", ref);

    hr = IPin_QueryPinInfo(pin, &info);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(info.pFilter == filter, "Expected filter %p, got %p.\n", filter, info.pFilter);
    ok(info.dir == PINDIR_INPUT, "Got direction %d.\n", info.dir);
    ok(!wcscmp(info.achName, L"in"), "Got name %s.\n", wine_dbgstr_w(info.achName));
    ref = get_refcount(filter);
    ok(ref == 3, "Got unexpected refcount %ld.\n", ref);
    ref = get_refcount(pin);
    ok(ref == 3, "Got unexpected refcount %ld.\n", ref);
    IBaseFilter_Release(info.pFilter);

    hr = IPin_QueryDirection(pin, &dir);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(dir == PINDIR_INPUT, "Got direction %d.\n", dir);

    hr = IPin_QueryId(pin, &id);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!wcscmp(id, L"in"), "Got id %s.\n", wine_dbgstr_w(id));
    CoTaskMemFree(id);

    hr = IPin_QueryInternalConnections(pin, NULL, NULL);
    ok(hr == E_NOTIMPL, "Got hr %#lx.\n", hr);

    IPin_Release(pin);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
}

static void test_media_types(void)
{
    IBaseFilter *filter = create_file_writer();
    AM_MEDIA_TYPE mt = {{0}}, *pmt;
    IEnumMediaTypes *enummt;
    WCHAR *filename;
    HRESULT hr;
    ULONG ref;
    IPin *pin;

    IBaseFilter_FindPin(filter, L"in", &pin);

    hr = IPin_EnumMediaTypes(pin, &enummt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IEnumMediaTypes_Next(enummt, 1, &pmt, NULL);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    IEnumMediaTypes_Release(enummt);

    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    mt.majortype = MEDIATYPE_Audio;
    mt.subtype = MEDIASUBTYPE_PCM;
    mt.formattype = FORMAT_WaveFormatEx;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    filename = set_filename(filter);

    hr = IPin_EnumMediaTypes(pin, &enummt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IEnumMediaTypes_Next(enummt, 1, &pmt, NULL);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    IEnumMediaTypes_Release(enummt);

    memset(&mt, 0, sizeof(AM_MEDIA_TYPE));
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    mt.majortype = MEDIATYPE_Video;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    mt.majortype = MEDIATYPE_Audio;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    mt.majortype = MEDIATYPE_Stream;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    mt.subtype = MEDIASUBTYPE_PCM;
    mt.formattype = FORMAT_WaveFormatEx;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    IPin_Release(pin);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    ok(GetFileAttributesW(filename) == INVALID_FILE_ATTRIBUTES, "File should not exist.\n");
}

static void test_enum_media_types(void)
{
    IBaseFilter *filter = create_file_writer();
    IEnumMediaTypes *enum1, *enum2;
    AM_MEDIA_TYPE *mts[2];
    ULONG ref, count;
    HRESULT hr;
    IPin *pin;

    IBaseFilter_FindPin(filter, L"in", &pin);

    hr = IPin_EnumMediaTypes(pin, &enum1);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IEnumMediaTypes_Next(enum1, 1, mts, NULL);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    hr = IEnumMediaTypes_Next(enum1, 1, mts, &count);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    ok(!count, "Got count %lu.\n", count);

    hr = IEnumMediaTypes_Reset(enum1);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IEnumMediaTypes_Next(enum1, 1, mts, NULL);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    hr = IEnumMediaTypes_Clone(enum1, &enum2);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IEnumMediaTypes_Skip(enum1, 1);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    hr = IEnumMediaTypes_Next(enum2, 1, mts, NULL);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    IEnumMediaTypes_Release(enum1);
    IEnumMediaTypes_Release(enum2);
    IPin_Release(pin);

    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
}

struct testfilter
{
    struct strmbase_filter filter;
    struct strmbase_source source;
};

static inline struct testfilter *impl_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct testfilter, filter);
}

static struct strmbase_pin *testfilter_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct testfilter *filter = impl_from_strmbase_filter(iface);
    if (!index)
        return &filter->source.pin;
    return NULL;
}

static void testfilter_destroy(struct strmbase_filter *iface)
{
    struct testfilter *filter = impl_from_strmbase_filter(iface);
    strmbase_source_cleanup(&filter->source);
    strmbase_filter_cleanup(&filter->filter);
}

static const struct strmbase_filter_ops testfilter_ops =
{
    .filter_get_pin = testfilter_get_pin,
    .filter_destroy = testfilter_destroy,
};

static HRESULT WINAPI testsource_DecideAllocator(struct strmbase_source *iface,
        IMemInputPin *peer, IMemAllocator **allocator)
{
    return S_OK;
}

static const struct strmbase_source_ops testsource_ops =
{
    .pfnAttemptConnection = BaseOutputPinImpl_AttemptConnection,
    .pfnDecideAllocator = testsource_DecideAllocator,
};

static void testfilter_init(struct testfilter *filter)
{
    static const GUID clsid = {0xabacab};
    strmbase_filter_init(&filter->filter, NULL, &clsid, &testfilter_ops);
    strmbase_source_init(&filter->source, &filter->filter, L"", &testsource_ops);
}

static void test_allocator(IMemInputPin *input, IMemAllocator *allocator)
{
    ALLOCATOR_PROPERTIES props, ret_props;
    IMemAllocator *ret_allocator;
    HRESULT hr;

    memset(&props, 0xcc, sizeof(props));
    hr = IMemInputPin_GetAllocatorRequirements(input, &props);
    todo_wine ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (hr == S_OK)
    {
        ok(!props.cBuffers, "Got %ld buffers.\n", props.cBuffers);
        ok(!props.cbBuffer, "Got size %ld.\n", props.cbBuffer);
        ok(props.cbAlign == 512, "Got alignment %ld.\n", props.cbAlign);
        ok(!props.cbPrefix, "Got prefix %ld.\n", props.cbPrefix);
    }

    ret_allocator = (IMemAllocator *)0xdeadbeef;
    hr = IMemInputPin_GetAllocator(input, &ret_allocator);
    todo_wine ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
    todo_wine ok(!ret_allocator, "Got allocator %p.\n", ret_allocator);

    hr = IMemInputPin_NotifyAllocator(input, NULL, TRUE);
    ok(hr == E_POINTER, "Got hr %#lx.\n", hr);

    props.cBuffers = 1;
    props.cbBuffer = 512;
    props.cbAlign = 512;
    props.cbPrefix = 0;
    hr = IMemAllocator_SetProperties(allocator, &props, &ret_props);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMemInputPin_NotifyAllocator(input, allocator, TRUE);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMemInputPin_GetAllocator(input, &ret_allocator);
    todo_wine ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
    if (hr == S_OK)
        IMemAllocator_Release(ret_allocator);
}

static void test_filter_state(IMediaControl *control)
{
    OAFilterState state;
    HRESULT hr;

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(state == State_Stopped, "Got state %lu.\n", state);

    hr = IMediaControl_Pause(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(state == State_Paused, "Got state %lu.\n", state);

    hr = IMediaControl_Run(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(state == State_Running, "Got state %lu.\n", state);

    hr = IMediaControl_Pause(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(state == State_Paused, "Got state %lu.\n", state);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(state == State_Stopped, "Got state %lu.\n", state);

    hr = IMediaControl_Run(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(state == State_Running, "Got state %lu.\n", state);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMediaControl_GetState(control, 0, &state);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(state == State_Stopped, "Got state %lu.\n", state);
}

static void test_sample_processing(IMediaControl *control, IMemInputPin *input,
        IMemAllocator *allocator, const WCHAR *filename)
{
    REFERENCE_TIME start, stop;
    IMediaSample *sample;
    BYTE buffer[600];
    FILE *file;
    HRESULT hr;
    BYTE *data;
    LONG size;

    hr = IMemInputPin_ReceiveCanBlock(input);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMediaControl_Pause(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == VFW_E_NOT_COMMITTED, "Got hr %#lx.\n", hr);

    hr = IMemAllocator_Commit(allocator);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IMediaSample_GetPointer(sample, &data);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    size = IMediaSample_GetSize(sample);
    ok(size == 512, "Got size %ld.\n", size);
    memset(data, 0xcc, size);
    start = 0;
    stop = 512;
    hr = IMediaSample_SetTime(sample, &start, &stop);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    strcpy((char *)data, "abcdefghi");
    hr = IMediaSample_SetActualDataLength(sample, 9);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IMemInputPin_Receive(input, sample);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    memset(data, 0xcc, size);
    strcpy((char *)data, "123456");
    hr = IMediaSample_SetActualDataLength(sample, 6);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IMemInputPin_Receive(input, sample);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    file = _wfopen(filename, L"rb");
    ok(!!file, "Failed to open file: %s.\n", strerror(errno));
    size = fread(buffer, 1, sizeof(buffer), file);
    ok(size == 512, "Got size %ld.\n", size);
    ok(!memcmp(buffer, "123456\0\xcc\xcc\xcc", 10), "Got data %s.\n",
            debugstr_an((char *)buffer, size));
    fclose(file);

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    IMediaSample_Release(sample);

    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, AM_GBF_NOWAIT);
    ok(hr == VFW_E_NOT_COMMITTED, "Got hr %#lx.\n", hr);
    if (hr == S_OK)
        IMediaSample_Release(sample);
}

static unsigned int check_ec_complete(IMediaEvent *eventsrc, DWORD timeout)
{
    LONG_PTR param1, param2;
    unsigned int ret = 0;
    HRESULT hr;
    LONG code;

    while ((hr = IMediaEvent_GetEvent(eventsrc, &code, &param1, &param2, timeout)) == S_OK)
    {
        if (code == EC_COMPLETE)
        {
            ok(param1 == S_OK, "Got param1 %#Ix.\n", param1);
            ok(!param2, "Got param2 %#Ix.\n", param2);
            ret++;
        }
        IMediaEvent_FreeEventParams(eventsrc, code, param1, param2);
        timeout = 0;
    }
    ok(hr == E_ABORT, "Got hr %#lx.\n", hr);

    return ret;
}

static void test_eos(IFilterGraph2 *graph, IMediaControl *control, IPin *pin)
{
    IMediaEvent *eventsrc;
    HRESULT hr;
    BOOL ret;

    IFilterGraph2_QueryInterface(graph, &IID_IMediaEvent, (void **)&eventsrc);

    hr = IMediaControl_Pause(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ret = check_ec_complete(eventsrc, 0);
    ok(!ret, "Got unexpected EC_COMPLETE.\n");

    hr = IPin_EndOfStream(pin);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ret = check_ec_complete(eventsrc, 0);
    ok(!ret, "Got unexpected EC_COMPLETE.\n");

    hr = IMediaControl_Run(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ret = check_ec_complete(eventsrc, 0);
    ok(ret == 1, "Expected EC_COMPLETE.\n");

    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    IMediaEvent_Release(eventsrc);
}

static void test_connect_pin(void)
{
    AM_MEDIA_TYPE req_mt =
    {
        .majortype = {0x1111},
        .subtype = {0x2222},
        .formattype = {0x3333},
    };

    IBaseFilter *filter = create_file_writer();
    WCHAR *filename = set_filename(filter);
    struct testfilter source;
    IMemAllocator *allocator;
    IMediaControl *control;
    IMemInputPin *meminput;
    IFilterGraph2 *graph;
    AM_MEDIA_TYPE mt;
    IPin *pin, *peer;
    HRESULT hr;
    ULONG ref;
    BOOL ret;

    testfilter_init(&source);
    CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IFilterGraph2, (void **)&graph);
    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);
    hr = IFilterGraph2_AddFilter(graph, filter, L"filewriter");
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    IFilterGraph2_AddFilter(graph, &source.filter.IBaseFilter_iface, L"source");
    IBaseFilter_FindPin(filter, L"in", &pin);
    IPin_QueryInterface(pin, &IID_IMemInputPin, (void **)&meminput);

    peer = (IPin *)0xdeadbeef;
    hr = IPin_ConnectedTo(pin, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#lx.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(pin, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#lx.\n", hr);

    hr = IMediaControl_Pause(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IFilterGraph2_ConnectDirect(graph, &source.source.pin.IPin_iface, pin, &req_mt);
    ok(hr == VFW_E_NOT_STOPPED, "Got hr %#lx.\n", hr);
    hr = IMediaControl_Stop(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IFilterGraph2_ConnectDirect(graph, &source.source.pin.IPin_iface, pin, &req_mt);
    ok(hr == VFW_E_TYPE_NOT_ACCEPTED, "Got hr %#lx.\n", hr);
    req_mt.majortype = MEDIATYPE_Stream;
    hr = IFilterGraph2_ConnectDirect(graph, &source.source.pin.IPin_iface, pin, &req_mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    hr = IPin_ConnectedTo(pin, &peer);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(peer == &source.source.pin.IPin_iface, "Got peer %p.\n", peer);
    IPin_Release(peer);

    hr = IPin_ConnectionMediaType(pin, &mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!memcmp(&mt, &req_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");

    hr = IMediaControl_Pause(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IFilterGraph2_Disconnect(graph, pin);
    ok(hr == VFW_E_NOT_STOPPED, "Got hr %#lx.\n", hr);
    hr = IMediaControl_Stop(control);
    todo_wine ok(hr == VFW_E_NO_ALLOCATOR, "Got hr %#lx.\n", hr);

    CoCreateInstance(&CLSID_MemoryAllocator, NULL, CLSCTX_INPROC_SERVER,
            &IID_IMemAllocator, (void **)&allocator);

    test_allocator(meminput, allocator);
    test_filter_state(control);
    test_sample_processing(control, meminput, allocator, filename);
    test_eos(graph, control, pin);

    hr = IFilterGraph2_Disconnect(graph, pin);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IFilterGraph2_Disconnect(graph, pin);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    ok(source.source.pin.peer == pin, "Got peer %p.\n", source.source.pin.peer);
    IFilterGraph2_Disconnect(graph, &source.source.pin.IPin_iface);

    peer = (IPin *)0xdeadbeef;
    hr = IPin_ConnectedTo(pin, &peer);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#lx.\n", hr);
    ok(!peer, "Got peer %p.\n", peer);

    hr = IPin_ConnectionMediaType(pin, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "Got hr %#lx.\n", hr);

    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete file, error %lu.\n", GetLastError());
    IMediaControl_Release(control);
    ref = IFilterGraph2_Release(graph);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    IMemInputPin_Release(meminput);
    IPin_Release(pin);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    ref = IMemAllocator_Release(allocator);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    ref = IBaseFilter_Release(&source.filter.IBaseFilter_iface);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
}

static ALLOCATOR_PROPERTIES decided_props;

static HRESULT WINAPI testsource_DecideBufferSize(struct strmbase_source *iface,
        IMemAllocator *allocator, ALLOCATOR_PROPERTIES *props)
{
    ALLOCATOR_PROPERTIES ret_props;

    decided_props = *props;
    props->cBuffers = 1;
    props->cbBuffer = 512;
    if (!props->cbAlign)
        props->cbAlign = 1;
    return IMemAllocator_SetProperties(allocator, props, &ret_props);
}

static const struct strmbase_source_ops default_allocator_source_ops =
{
    .pfnAttemptConnection = BaseOutputPinImpl_AttemptConnection,
    .pfnDecideBufferSize = testsource_DecideBufferSize,
    .pfnDecideAllocator = BaseOutputPinImpl_DecideAllocator,
};

static void test_default_allocator(void)
{
    static const GUID clsid = {0xabacab};
    AM_MEDIA_TYPE req_mt =
    {
        .majortype = MEDIATYPE_Stream,
        .subtype = {0x2222},
        .formattype = {0x3333},
    };
    IBaseFilter *filter = create_file_writer();
    WCHAR *filename = set_filename(filter);
    struct testfilter source;
    IFilterGraph2 *graph;
    HRESULT hr;
    ULONG ref;
    IPin *pin;

    strmbase_filter_init(&source.filter, NULL, &clsid, &testfilter_ops);
    strmbase_source_init(&source.source, &source.filter, L"", &default_allocator_source_ops);
    CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IFilterGraph2, (void **)&graph);
    IFilterGraph2_AddFilter(graph, filter, L"filewriter");
    IFilterGraph2_AddFilter(graph, &source.filter.IBaseFilter_iface, L"source");
    IBaseFilter_FindPin(filter, L"in", &pin);

    hr = IFilterGraph2_ConnectDirect(graph, &source.source.pin.IPin_iface, pin, &req_mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!!source.source.pAllocator, "Got no allocator.\n");
    ok(decided_props.cbAlign == 512, "Got alignment %ld.\n", decided_props.cbAlign);

    IFilterGraph2_Disconnect(graph, pin);
    IFilterGraph2_Disconnect(graph, &source.source.pin.IPin_iface);
    ref = IFilterGraph2_Release(graph);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    IPin_Release(pin);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    ref = IBaseFilter_Release(&source.filter.IBaseFilter_iface);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    DeleteFileW(filename);
}

static void write_test_file(const WCHAR *filename, DWORD size)
{
    char buffer[1000];
    HANDLE file;
    DWORD ret_size;
    BOOL ret;

    memset(buffer, 'A', sizeof(buffer));
    file = CreateFileW(filename, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "Failed to create file, error %lu.\n", GetLastError());
    ret = WriteFile(file, buffer, size, &ret_size, NULL);
    ok(ret, "Failed to write file, error %lu.\n", GetLastError());
    CloseHandle(file);
}

static DWORD get_file_size(const WCHAR *filename)
{
    WIN32_FILE_ATTRIBUTE_DATA data;

    if (!GetFileAttributesExW(filename, GetFileExInfoStandard, &data))
        return ~0u;
    return data.nFileSizeLow;
}

static void test_file_mode(void)
{
    AM_MEDIA_TYPE req_mt =
    {
        .majortype = MEDIATYPE_Stream,
        .subtype = {0x2222},
        .formattype = {0x3333},
    };

    IBaseFilter *filter = create_file_writer();
    WCHAR *filename = set_filename(filter);
    IFileSinkFilter2 *filesink;
    struct testfilter source;
    IMediaControl *control;
    IFilterGraph2 *graph;
    DWORD mode, size;
    HRESULT hr;
    ULONG ref;
    IPin *pin;

    IBaseFilter_QueryInterface(filter, &IID_IFileSinkFilter2, (void **)&filesink);

    hr = IFileSinkFilter2_GetMode(filesink, NULL);
    ok(hr == E_POINTER, "Got hr %#lx.\n", hr);
    mode = 0xdeadbeef;
    hr = IFileSinkFilter2_GetMode(filesink, &mode);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!mode, "Got mode %#lx.\n", mode);

    hr = IFileSinkFilter2_SetMode(filesink, AM_FILE_OVERWRITE);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IFileSinkFilter2_GetMode(filesink, &mode);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(mode == AM_FILE_OVERWRITE, "Got mode %#lx.\n", mode);

    hr = IFileSinkFilter2_SetMode(filesink, 2);
    ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
    hr = IFileSinkFilter2_SetMode(filesink, 0xffffffff);
    ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
    hr = IFileSinkFilter2_GetMode(filesink, &mode);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(mode == AM_FILE_OVERWRITE, "Got mode %#lx.\n", mode);

    hr = IFileSinkFilter2_SetMode(filesink, 0);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IFileSinkFilter2_GetMode(filesink, &mode);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!mode, "Got mode %#lx.\n", mode);

    testfilter_init(&source);
    CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IFilterGraph2, (void **)&graph);
    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);
    IFilterGraph2_AddFilter(graph, filter, L"filewriter");
    IFilterGraph2_AddFilter(graph, &source.filter.IBaseFilter_iface, L"source");
    IBaseFilter_FindPin(filter, L"in", &pin);
    hr = IFilterGraph2_ConnectDirect(graph, &source.source.pin.IPin_iface, pin, &req_mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    write_test_file(filename, 1000);
    hr = IMediaControl_Pause(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    size = get_file_size(filename);
    ok(size == 1000, "Got size %lu.\n", size);
    IMediaControl_Stop(control);

    hr = IFileSinkFilter2_SetMode(filesink, AM_FILE_OVERWRITE);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    write_test_file(filename, 1000);
    hr = IMediaControl_Pause(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    size = get_file_size(filename);
    ok(!size, "Got size %lu.\n", size);
    IMediaControl_Stop(control);

    IFilterGraph2_Disconnect(graph, pin);
    IFilterGraph2_Disconnect(graph, &source.source.pin.IPin_iface);
    IMediaControl_Release(control);
    ref = IFilterGraph2_Release(graph);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    IPin_Release(pin);
    IFileSinkFilter2_Release(filesink);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    ref = IBaseFilter_Release(&source.filter.IBaseFilter_iface);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    DeleteFileW(filename);
}

static void test_file_lifetime(void)
{
    AM_MEDIA_TYPE req_mt =
    {
        .majortype = MEDIATYPE_Stream,
        .subtype = {0x2222},
        .formattype = {0x3333},
    };

    IBaseFilter *filter = create_file_writer();
    WCHAR *filename = set_filename(filter);
    LARGE_INTEGER zero = {{0}};
    IStream *stream, *stream2;
    struct testfilter source;
    IMediaControl *control;
    IFilterGraph2 *graph;
    ULARGE_INTEGER pos;
    char buffer[4];
    ULONG ret_size;
    HANDLE file;
    DWORD size;
    HRESULT hr;
    ULONG ref;
    IPin *pin;

    IBaseFilter_FindPin(filter, L"in", &pin);

    hr = IPin_QueryInterface(pin, &IID_IStream, (void **)&stream);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IPin_QueryInterface(pin, &IID_IStream, (void **)&stream2);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(stream != stream2, "Got the same stream.\n");
    pos.QuadPart = 0xdeadbeef;
    hr = IStream_Seek(stream, zero, STREAM_SEEK_END, &pos);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!pos.QuadPart, "Got position %s.\n", wine_dbgstr_longlong(pos.QuadPart));
    size = get_file_size(filename);
    ok(!size, "Got size %lu.\n", size);
    hr = IStream_Read(stream, buffer, 1, &ret_size);
    ok(hr == HRESULT_FROM_WIN32(ERROR_HANDLE_EOF), "Got hr %#lx.\n", hr);

    hr = IStream_Write(stream, "AB", 2, &ret_size);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IStream_Write(stream2, "C", 1, &ret_size);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    memset(buffer, 0, sizeof(buffer));
    hr = IStream_Read(stream, buffer, sizeof(buffer), &ret_size);
    ok(hr == HRESULT_FROM_WIN32(ERROR_HANDLE_EOF), "Got hr %#lx.\n", hr);
    ok(ret_size == 2, "Got size %lu.\n", ret_size);
    ok(!memcmp(buffer, "CB", 2), "Got data %s.\n", debugstr_an(buffer, ret_size));
    IStream_Release(stream2);
    IStream_Release(stream);
    DeleteFileW(filename);

    testfilter_init(&source);
    CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IFilterGraph2, (void **)&graph);
    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);
    IFilterGraph2_AddFilter(graph, filter, L"filewriter");
    IFilterGraph2_AddFilter(graph, &source.filter.IBaseFilter_iface, L"source");
    hr = IFilterGraph2_ConnectDirect(graph, &source.source.pin.IPin_iface, pin, &req_mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    size = get_file_size(filename);
    ok(size == ~0u, "Got size %lu.\n", size);

    hr = IMediaControl_Pause(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    size = get_file_size(filename);
    ok(!size, "Got size %lu.\n", size);
    IMediaControl_Stop(control);
    size = get_file_size(filename);
    ok(!size, "Got size %lu.\n", size);
    file = CreateFileW(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "Failed to open file, error %lu.\n", GetLastError());
    CloseHandle(file);

    IPin_QueryInterface(pin, &IID_IStream, (void **)&stream);
    hr = IStream_Seek(stream, zero, STREAM_SEEK_END, &pos);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!pos.QuadPart, "Got position %s.\n", wine_dbgstr_longlong(pos.QuadPart));
    IStream_Release(stream);
    size = get_file_size(filename);
    ok(size == ~0u, "Got size %lu.\n", size);

    hr = IMediaControl_Pause(control);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    IPin_QueryInterface(pin, &IID_IStream, (void **)&stream);
    hr = IStream_Write(stream, "ABCD", 4, &ret_size);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    IStream_Release(stream);
    IMediaControl_Stop(control);
    size = get_file_size(filename);
    ok(size == 4, "Got size %lu.\n", size);
    file = CreateFileW(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "Failed to open file, error %lu.\n", GetLastError());
    CloseHandle(file);

    IFilterGraph2_Disconnect(graph, pin);
    IFilterGraph2_Disconnect(graph, &source.source.pin.IPin_iface);
    IMediaControl_Release(control);
    ref = IFilterGraph2_Release(graph);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);

    IPin_Release(pin);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    ref = IBaseFilter_Release(&source.filter.IBaseFilter_iface);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    DeleteFileW(filename);
}

static HRESULT load_persist_data(IPersistStream *persist, const void *data, ULONG size, ULONG *pos)
{
    LARGE_INTEGER zero = {{0}};
    ULARGE_INTEGER ret_pos;
    IStream *stream;
    HRESULT hr;

    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    IStream_Write(stream, data, size, NULL);
    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    hr = IPersistStream_Load(persist, stream);
    IStream_Seek(stream, zero, STREAM_SEEK_CUR, &ret_pos);
    *pos = ret_pos.LowPart;
    IStream_Release(stream);
    return hr;
}

static void test_persist_stream(void)
{
    static const BYTE expect[32] =
    {
        '0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0,'0',0,' ',0,
        8,0,0,0,1,0,0,0,
    };
    IBaseFilter *filter = create_file_writer();
    LARGE_INTEGER zero = {{0}};
    IFileSinkFilter2 *filesink;
    IPersistStream *persist;
    BYTE data[40], buffer[40];
    ULARGE_INTEGER size;
    STATSTG stat;
    IStream *stream;
    ULONG pos, ret_size;
    CLSID clsid;
    DWORD mode;
    HRESULT hr;
    ULONG ref;

    IBaseFilter_QueryInterface(filter, &IID_IFileSinkFilter2, (void **)&filesink);
    hr = IBaseFilter_QueryInterface(filter, &IID_IPersistStream, (void **)&persist);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (hr != S_OK)
    {
        IFileSinkFilter2_Release(filesink);
        IBaseFilter_Release(filter);
        return;
    }

    hr = IPersistStream_GetClassID(persist, &clsid);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(IsEqualGUID(&clsid, &CLSID_FileWriter), "Got CLSID %s.\n", wine_dbgstr_guid(&clsid));

    hr = IPersistStream_IsDirty(persist);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    IFileSinkFilter2_SetFileName(filesink, L"qfwtest.avi", NULL);
    hr = IPersistStream_IsDirty(persist);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    IFileSinkFilter2_SetMode(filesink, AM_FILE_OVERWRITE);
    hr = IPersistStream_IsDirty(persist);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    size.QuadPart = 0xdeadbeef;
    hr = IPersistStream_GetSizeMax(persist, &size);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(size.QuadPart == sizeof(expect), "Got size %s.\n", wine_dbgstr_longlong(size.QuadPart));

    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    hr = IPersistStream_Save(persist, stream, FALSE);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IPersistStream_IsDirty(persist);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IPersistStream_Save(persist, stream, TRUE);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IPersistStream_IsDirty(persist);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    IStream_Stat(stream, &stat, STATFLAG_NONAME);
    ok(stat.cbSize.QuadPart == 2 * sizeof(expect), "Got size %s.\n", wine_dbgstr_longlong(stat.cbSize.QuadPart));
    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    memset(buffer, 0xcc, sizeof(buffer));
    IStream_Read(stream, buffer, sizeof(expect), &ret_size);
    ok(ret_size == sizeof(expect), "Got size %lu.\n", ret_size);
    ok(!memcmp(buffer, expect, sizeof(expect)), "Got unexpected data.\n");
    IStream_Release(stream);

    hr = IFileSinkFilter2_SetMode(filesink, 0);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    memcpy(data, expect, sizeof(expect));
    memset(data + sizeof(expect), 0x55, sizeof(data) - sizeof(expect));
    hr = load_persist_data(persist, data, sizeof(data), &pos);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(pos == sizeof(expect), "Got position %lu.\n", pos);
    hr = IFileSinkFilter2_GetMode(filesink, &mode);
    ok(mode == AM_FILE_OVERWRITE, "Got mode %#lx.\n", mode);
    hr = IPersistStream_IsDirty(persist);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    data[20] = '2';
    data[28] = 2;
    hr = load_persist_data(persist, data, sizeof(expect), &pos);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = IFileSinkFilter2_GetMode(filesink, &mode);
    ok(mode == 2, "Got mode %#lx.\n", mode);

    data[28] = 1;
    hr = load_persist_data(persist, data, 28, &pos);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(pos == 28, "Got position %lu.\n", pos);
    hr = IFileSinkFilter2_GetMode(filesink, &mode);
    ok(!mode, "Got mode %#lx.\n", mode);

    IFileSinkFilter2_SetMode(filesink, AM_FILE_OVERWRITE);
    hr = load_persist_data(persist, data, 24, &pos);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA), "Got hr %#lx.\n", hr);
    data[24] = 4;
    hr = load_persist_data(persist, data, sizeof(expect), &pos);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA), "Got hr %#lx.\n", hr);
    ok(pos == sizeof(expect), "Got position %lu.\n", pos);
    data[24] = 8;
    data[2] = 'z';
    hr = load_persist_data(persist, data, sizeof(expect), &pos);
    ok(hr == VFW_E_INVALID_FILE_FORMAT, "Got hr %#lx.\n", hr);
    ok(pos == 4, "Got position %lu.\n", pos);
    hr = load_persist_data(persist, data, 0, &pos);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA), "Got hr %#lx.\n", hr);
    hr = IFileSinkFilter2_GetMode(filesink, &mode);
    ok(mode == AM_FILE_OVERWRITE, "Got mode %#lx.\n", mode);

    IPersistStream_Release(persist);
    IFileSinkFilter2_Release(filesink);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
}

static void test_quality_control(void)
{
    IBaseFilter *filter = create_file_writer();
    Quality quality = {Famine, 500, 0, 0};
    IQualityControl *qc;
    HRESULT hr;
    ULONG ref;
    IPin *pin;

    IBaseFilter_FindPin(filter, L"in", &pin);
    hr = IPin_QueryInterface(pin, &IID_IQualityControl, (void **)&qc);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (hr == S_OK)
    {
        hr = IQualityControl_Notify(qc, NULL, quality);
        ok(hr == E_POINTER, "Got hr %#lx.\n", hr);
        hr = IQualityControl_Notify(qc, filter, quality);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = IQualityControl_SetSink(qc, NULL);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = IQualityControl_SetSink(qc, qc);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        IQualityControl_Release(qc);
    }

    IPin_Release(pin);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
}

static void test_misc_flags(void)
{
    IBaseFilter *filter = create_file_writer();
    IAMFilterMiscFlags *misc_flags;
    ULONG flags, ref;

    IBaseFilter_QueryInterface(filter, &IID_IAMFilterMiscFlags, (void **)&misc_flags);

    flags = IAMFilterMiscFlags_GetMiscFlags(misc_flags);
    ok(flags == AM_FILTER_MISC_FLAGS_IS_RENDERER, "Got flags %#lx.\n", flags);

    IAMFilterMiscFlags_Release(misc_flags);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
}

static void test_file_sink(void)
{
    IBaseFilter *filter = create_file_writer();
    AM_MEDIA_TYPE mt, set_mt = {{0}};
    IFileSinkFilter *filesink;
    WCHAR *filename;
    HRESULT hr;
    ULONG ref;
    IPin *pin;

    IBaseFilter_QueryInterface(filter, &IID_IFileSinkFilter, (void **)&filesink);
    IBaseFilter_FindPin(filter, L"in", &pin);

    hr = IFileSinkFilter_GetCurFile(filesink, NULL, &mt);
    ok(hr == E_POINTER, "Got hr %#lx.\n", hr);

    memset(&mt, 0xcc, sizeof(mt));
    filename = (WCHAR *)0xdeadbeef;
    hr = IFileSinkFilter_GetCurFile(filesink, &filename, &mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!filename, "Got filename %s.\n", wine_dbgstr_w(filename));
    ok(IsEqualGUID(&mt.majortype, &GUID_NULL), "Got major type %s.\n", wine_dbgstr_guid(&mt.majortype));
    ok(IsEqualGUID(&mt.subtype, &GUID_NULL), "Got subtype %s.\n", wine_dbgstr_guid(&mt.subtype));
    ok(mt.cbFormat == 0xcccccccc, "Got format size %lu.\n", mt.cbFormat);

    hr = IFileSinkFilter_SetFileName(filesink, NULL, NULL);
    ok(hr == E_POINTER, "Got hr %#lx.\n", hr);

    hr = IFileSinkFilter_SetFileName(filesink, L"qfwtest.avi", NULL);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    memset(&mt, 0xcc, sizeof(mt));
    hr = IFileSinkFilter_GetCurFile(filesink, &filename, &mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!wcscmp(filename, L"qfwtest.avi"), "Got filename %s.\n", wine_dbgstr_w(filename));
    CoTaskMemFree(filename);
    ok(IsEqualGUID(&mt.majortype, &MEDIATYPE_Stream), "Got major type %s.\n", wine_dbgstr_guid(&mt.majortype));
    ok(IsEqualGUID(&mt.subtype, &GUID_NULL), "Got subtype %s.\n", wine_dbgstr_guid(&mt.subtype));
    ok(mt.cbFormat == 0xcccccccc, "Got format size %lu.\n", mt.cbFormat);

    hr = IFileSinkFilter_GetCurFile(filesink, &filename, NULL);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!wcscmp(filename, L"qfwtest.avi"), "Got filename %s.\n", wine_dbgstr_w(filename));
    CoTaskMemFree(filename);

    set_mt.majortype = MEDIATYPE_Video;
    set_mt.subtype = MEDIASUBTYPE_RGB24;
    set_mt.formattype = FORMAT_VideoInfo;
    hr = IFileSinkFilter_SetFileName(filesink, L"qfwtest2.avi", &set_mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    memset(&mt, 0xcc, sizeof(mt));
    hr = IFileSinkFilter_GetCurFile(filesink, &filename, &mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!wcscmp(filename, L"qfwtest2.avi"), "Got filename %s.\n", wine_dbgstr_w(filename));
    CoTaskMemFree(filename);
    ok(IsEqualGUID(&mt.majortype, &MEDIATYPE_Video), "Got major type %s.\n", wine_dbgstr_guid(&mt.majortype));
    ok(IsEqualGUID(&mt.subtype, &MEDIASUBTYPE_RGB24), "Got subtype %s.\n", wine_dbgstr_guid(&mt.subtype));
    ok(mt.formattype.Data1 == 0xcccccccc, "Got format type %s.\n", wine_dbgstr_guid(&mt.formattype));

    hr = IPin_QueryAccept(pin, &set_mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    memset(&mt, 0, sizeof(mt));
    mt.majortype = MEDIATYPE_Stream;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    mt.majortype = MEDIATYPE_Video;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);
    mt.subtype = MEDIASUBTYPE_RGB32;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    memset(&set_mt, 0, sizeof(set_mt));
    set_mt.subtype = MEDIASUBTYPE_RGB24;
    hr = IFileSinkFilter_SetFileName(filesink, L"qfwtest3.avi", &set_mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    mt.majortype = MEDIATYPE_Audio;
    mt.subtype = MEDIASUBTYPE_RGB24;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    mt.subtype = MEDIASUBTYPE_PCM;
    hr = IPin_QueryAccept(pin, &mt);
    ok(hr == S_FALSE, "Got hr %#lx.\n", hr);

    IPin_Release(pin);
    IFileSinkFilter_Release(filesink);
    ref = IBaseFilter_Release(filter);
    ok(!ref, "Got outstanding refcount %ld.\n", ref);
    ok(GetFileAttributesW(L"qfwtest.avi") == INVALID_FILE_ATTRIBUTES, "File should not exist.\n");
}

START_TEST(filewriter)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    test_interfaces();
    test_aggregation();
    test_enum_pins();
    test_find_pin();
    test_pin_info();
    test_media_types();
    test_enum_media_types();
    test_connect_pin();
    test_default_allocator();
    test_misc_flags();
    test_file_sink();
    test_file_mode();
    test_file_lifetime();
    test_persist_stream();
    test_quality_control();

    CoUninitialize();
}
