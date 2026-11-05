/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the pixel conversions of the Color Converter MFT against Windows 11 output
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>

#define COBJMACROS
#include <windef.h>
#include <winbase.h>
#include <initguid.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>

DEFINE_GUID(CLSID_CColorConvertDMO_, 0x98230571, 0x0087, 0x4204, 0xb0, 0x20, 0x32, 0x82, 0x53, 0x8e, 0x57, 0xd3);

static const struct
{
    UINT id;
    DWORD input;
    UINT32 input_width, input_height;
    DWORD output;
    UINT32 output_width, output_height;
    LONG stride;
    HRESULT set_output_hr;
    DWORD size;
    DWORD hash;
}
tests[] =
{
    { 1, 0x56555941, 128, 64, 0x00000016, 128, 64, 0, 0x00000000, 32768, 0x32d3c84c},
    { 2, 0x3231564e, 32, 16, 0x00000016, 32, 16, 0, 0x00000000, 2048, 0xb6cca595},
    { 3, 0x30323449, 32, 16, 0x00000016, 32, 16, 0, 0x00000000, 2048, 0xfe31b6ed},
    { 4, 0x32315659, 32, 16, 0x00000016, 32, 16, 0, 0x00000000, 2048, 0x6f9ca508},
    { 5, 0x32595559, 32, 16, 0x00000016, 32, 16, 0, 0x00000000, 2048, 0x1cb3d882},
    { 6, 0x59565955, 32, 16, 0x00000016, 32, 16, 0, 0x00000000, 2048, 0xdd745a3b},
    { 7, 0x55595659, 32, 16, 0x00000016, 32, 16, 0, 0x00000000, 2048, 0xc79a92b5},
    { 8, 0x56555949, 32, 16, 0x00000016, 32, 16, 0, 0x00000000, 2048, 0xca2ded06},
    { 9, 0x00000016, 64, 64, 0x56555941, 64, 64, 0, 0x00000000, 16384, 0x3fff6ac5},
    {10, 0x00000016, 32, 16, 0x3231564e, 32, 16, 0, 0x00000000, 768, 0x00d2a146},
    {11, 0x00000016, 32, 16, 0x32595559, 32, 16, 0, 0x00000000, 1024, 0x6e5da6ac},
    {12, 0x00000016, 32, 16, 0x30323449, 32, 16, 0, 0x00000000, 768, 0xafe951dc},
    {13, 0x00000016, 32, 16, 0x32315659, 32, 16, 0, 0x00000000, 768, 0x98eb9b5e},
    {14, 0x56555941, 32, 16, 0x00000014, 32, 16, 0, 0x00000000, 1536, 0xf32b8169},
    {15, 0x56555941, 32, 16, 0x00000017, 32, 16, 0, 0x00000000, 1024, 0x3d87eff8},
    {16, 0x56555941, 32, 16, 0x00000018, 32, 16, 0, 0x00000000, 1024, 0x481d539a},
    {17, 0x56555941, 32, 16, 0x00000029, 32, 16, 0, MF_E_INVALIDMEDIATYPE, 0, 0},
    {18, 0x00000014, 32, 16, 0x00000016, 32, 16, 0, 0x00000000, 2048, 0x240c9d00},
    {19, 0x00000017, 32, 16, 0x00000016, 32, 16, 0, 0x00000000, 2048, 0x517f4fc1},
    {20, 0x00000018, 32, 16, 0x00000016, 32, 16, 0, 0x00000000, 2048, 0x10c5c3cd},
    {21, 0x3231564e, 32, 16, 0x32595559, 32, 16, 0, 0x00000000, 1024, 0x67f10904},
    {22, 0x3231564e, 32, 16, 0x30323449, 32, 16, 0, 0x00000000, 768, 0x1c29854e},
    {23, 0x32595559, 32, 16, 0x3231564e, 32, 16, 0, 0x00000000, 768, 0xc149cfc2},
    {24, 0x56555941, 32, 16, 0x3231564e, 32, 16, 0, 0x00000000, 768, 0x6477f3f9},
    {25, 0x3231564e, 32, 16, 0x56555941, 32, 16, 0, 0x00000000, 2048, 0x22d92e20},
    {26, 0x00000016, 32, 16, 0x00000014, 32, 16, 0, 0x00000000, 1536, 0x0c6a1b32},
    {27, 0x00000016, 32, 16, 0x00000017, 32, 16, 0, 0x00000000, 1024, 0xee8e1e0b},
    {28, 0x3231564e, 32, 16, 0x00000016, 64, 32, 0, MF_E_INVALIDMEDIATYPE, 0, 0},
    {29, 0x3231564e, 34, 18, 0x00000016, 34, 18, 0, 0x00000000, 2448, 0x343c39d0},
    {30, 0x00000016, 33, 17, 0x00000016, 33, 17, 0, 0x00000000, 2244, 0xc5e44351},
    {31, 0x3231564e, 32, 16, 0x00000016, 32, 16, 128, 0x00000000, 2048, 0x835d9790},
    {32, 0x3231564e, 32, 16, 0x00000016, 32, 16, -128, 0x00000000, 2048, 0x045a515f},
    {33, 0x3231564e, 32, 16, 0x3231564e, 32, 16, 0, 0x00000000, 768, 0xe38c93e1},
};

static DWORD frame_size(DWORD fourcc, UINT32 width, UINT32 height)
{
    switch (fourcc)
    {
    case MAKEFOURCC('N','V','1','2'): case MAKEFOURCC('I','4','2','0'): case MAKEFOURCC('I','Y','U','V'):
    case MAKEFOURCC('Y','V','1','2'):
        return width * height * 3 / 2;
    case MAKEFOURCC('Y','U','Y','2'): case MAKEFOURCC('U','Y','V','Y'): case MAKEFOURCC('Y','V','Y','U'): case 23: case 24:
        return width * height * 2;
    case 20:
        return width * height * 3;
    }
    return width * height * 4;
}

static IMFMediaType *create_type(DWORD fourcc, UINT32 width, UINT32 height, LONG stride)
{
    GUID subtype = MFVideoFormat_Base;
    IMFMediaType *type;
    HRESULT hr;

    subtype.Data1 = fourcc;
    hr = MFCreateMediaType(&type);
    ok(hr == S_OK, "MFCreateMediaType returned %#lx\n", hr);
    IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, &subtype);
    IMFMediaType_SetUINT64(type, &MF_MT_FRAME_SIZE, ((UINT64)width << 32) | height);
    if (stride)
        IMFMediaType_SetUINT32(type, &MF_MT_DEFAULT_STRIDE, (UINT32)stride);
    return type;
}

static IMFSample *create_sample(DWORD size, const BYTE *data)
{
    IMFMediaBuffer *buffer;
    IMFSample *sample;
    BYTE *ptr;
    HRESULT hr;

    hr = MFCreateMemoryBuffer(size, &buffer);
    ok(hr == S_OK, "MFCreateMemoryBuffer returned %#lx\n", hr);
    IMFMediaBuffer_Lock(buffer, &ptr, NULL, NULL);
    if (data)
        memcpy(ptr, data, size);
    else
        memset(ptr, 0xcd, size);
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_SetCurrentLength(buffer, data ? size : 0);
    MFCreateSample(&sample);
    IMFSample_AddBuffer(sample, buffer);
    IMFMediaBuffer_Release(buffer);
    return sample;
}

static DWORD hash_bytes(const BYTE *data, DWORD size)
{
    DWORD hash = 0x811c9dc5, i;

    for (i = 0; i < size; i++)
        hash = (hash ^ data[i]) * 0x01000193;
    return hash;
}

static void test_conversion(UINT index)
{
    MFT_OUTPUT_STREAM_INFO output_info = {0};
    MFT_OUTPUT_DATA_BUFFER output = {0};
    IMFMediaType *input_type, *output_type;
    IMFSample *input_sample, *output_sample;
    DWORD input_size, i, x, status, length;
    IMFMediaBuffer *buffer;
    IMFTransform *transform;
    BYTE *data;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_CColorConvertDMO_, NULL, CLSCTX_INPROC_SERVER, &IID_IMFTransform, (void **)&transform);
    ok(hr == S_OK, "CoCreateInstance returned %#lx\n", hr);
    if (FAILED(hr))
        return;

    input_type = create_type(tests[index].input, tests[index].input_width, tests[index].input_height, 0);
    output_type = create_type(tests[index].output, tests[index].output_width, tests[index].output_height, tests[index].stride);
    hr = IMFTransform_SetInputType(transform, 0, input_type, 0);
    ok(hr == S_OK, "SetInputType returned %#lx\n", hr);
    hr = IMFTransform_SetOutputType(transform, 0, output_type, 0);
    ok(hr == tests[index].set_output_hr, "SetOutputType returned %#lx\n", hr);
    IMFMediaType_Release(input_type);
    IMFMediaType_Release(output_type);
    if (FAILED(hr))
    {
        IMFTransform_Release(transform);
        return;
    }

    hr = IMFTransform_GetOutputStreamInfo(transform, 0, &output_info);
    ok(hr == S_OK, "GetOutputStreamInfo returned %#lx\n", hr);
    ok(output_info.cbSize == tests[index].size, "got cbSize %lu\n", output_info.cbSize);

    input_size = frame_size(tests[index].input, tests[index].input_width, tests[index].input_height);
    data = HeapAlloc(GetProcessHeap(), 0, input_size);
    for (i = 0, x = tests[index].id; i < input_size; i++)
    {
        x = x * 1103515245 + 12345;
        data[i] = (x >> 16) & 0xff;
    }
    input_sample = create_sample(input_size, data);
    HeapFree(GetProcessHeap(), 0, data);
    IMFSample_SetSampleTime(input_sample, 0);
    IMFSample_SetSampleDuration(input_sample, 10000000);
    hr = IMFTransform_ProcessInput(transform, 0, input_sample, 0);
    ok(hr == S_OK, "ProcessInput returned %#lx\n", hr);
    IMFSample_Release(input_sample);

    output_sample = create_sample(output_info.cbSize, NULL);
    output.pSample = output_sample;
    hr = IMFTransform_ProcessOutput(transform, 0, 1, &output, &status);
    ok(hr == S_OK, "ProcessOutput returned %#lx\n", hr);
    hr = IMFSample_ConvertToContiguousBuffer(output_sample, &buffer);
    ok(hr == S_OK, "ConvertToContiguousBuffer returned %#lx\n", hr);
    IMFMediaBuffer_Lock(buffer, &data, NULL, &length);
    ok(length == tests[index].size, "got length %lu\n", length);
    ok(hash_bytes(data, tests[index].size) == tests[index].hash, "got hash %#lx\n", hash_bytes(data, tests[index].size));
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
    if (output.pEvents)
        IMFCollection_Release(output.pEvents);
    IMFSample_Release(output_sample);
    IMFTransform_Release(transform);
}

START_TEST(convert)
{
    UINT i;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_FULL);

    for (i = 0; i < ARRAYSIZE(tests); i++)
    {
        winetest_push_context("case %u", tests[i].id);
        test_conversion(i);
        winetest_pop_context();
    }

    MFShutdown();
    CoUninitialize();
}
