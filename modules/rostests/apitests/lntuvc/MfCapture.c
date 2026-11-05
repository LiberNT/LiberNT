/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for USB video capture through Media Foundation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include "lntuvc_test.h"
#include <stdio.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

typedef struct _EXPECT_TYPE
{
    UINT32 Width;
    UINT32 Height;
    UINT32 Rate;
    UINT32 Bitrate;
} EXPECT_TYPE;

static const EXPECT_TYPE ExpectTypes[] =
{
    { 640, 480, 30, 147456000 },
    { 640, 480, 15, 147456000 },
    { 320, 240, 30, 36864000 },
    { 320, 240, 15, 36864000 },
    { 160, 120, 30, 9216000 },
    { 160, 120, 15, 9216000 },
    { 1280, 720, 10, 147456000 },
    { 1280, 720, 5, 147456000 },
};

static void
DumpAttributes(const char *What, IMFAttributes *Attributes)
{
    UINT32 Count = 0, Index;

    IMFAttributes_GetCount(Attributes, &Count);
    for (Index = 0; Index < Count; Index++)
    {
        GUID Key;
        PROPVARIANT Value;

        PropVariantInit(&Value);
        if (FAILED(IMFAttributes_GetItemByIndex(Attributes, Index, &Key, &Value)))
            continue;
        if (Value.vt == VT_UI4)
            trace("MEASURE %s %s vt %u value %lu\n", What, wine_dbgstr_guid(&Key), Value.vt, Value.ulVal);
        else if (Value.vt == VT_UI8)
            trace("MEASURE %s %s vt %u value %I64x\n", What, wine_dbgstr_guid(&Key), Value.vt, Value.uhVal.QuadPart);
        else if (Value.vt == VT_CLSID)
            trace("MEASURE %s %s vt %u value %s\n", What, wine_dbgstr_guid(&Key), Value.vt, wine_dbgstr_guid(Value.puuid));
        else if (Value.vt == VT_LPWSTR)
            trace("MEASURE %s %s vt %u value %ls\n", What, wine_dbgstr_guid(&Key), Value.vt, Value.pwszVal);
        else if (Value.vt == (VT_VECTOR | VT_UI1))
        {
            ULONG Byte;

            trace("MEASURE %s %s blob %lu bytes\n", What, wine_dbgstr_guid(&Key), Value.caub.cElems);
            for (Byte = 0; Byte + sizeof(GUID) <= Value.caub.cElems; Byte += sizeof(GUID))
                trace("MEASURE %s blob %lu %s\n", What, Byte, wine_dbgstr_guid((const GUID *)(Value.caub.pElems + Byte)));
        }
        else
            trace("MEASURE %s %s vt %u\n", What, wine_dbgstr_guid(&Key), Value.vt);
        PropVariantClear(&Value);
    }
}

static void
DumpMediaTypes(IMFMediaSource *Source, IMFMediaType **Yuy2)
{
    BOOL HasMjpeg = FALSE;
    IMFPresentationDescriptor *Presentation = NULL;
    IMFStreamDescriptor *Stream = NULL;
    IMFMediaTypeHandler *Handler = NULL;
    BOOL Selected = FALSE;
    DWORD Streams = 0, Count = 0, Index, StreamId = 0;
    HRESULT hr;

    hr = IMFMediaSource_CreatePresentationDescriptor(Source, &Presentation);
    ok(hr == S_OK, "CreatePresentationDescriptor returned %#lx\n", hr);
    if (FAILED(hr))
        return;

    hr = IMFPresentationDescriptor_GetStreamDescriptorCount(Presentation, &Streams);
    ok(hr == S_OK, "GetStreamDescriptorCount returned %#lx\n", hr);
    trace("MEASURE streams %lu\n", Streams);
    ok(Streams >= 1, "Streams %lu\n", Streams);

    hr = IMFPresentationDescriptor_GetStreamDescriptorByIndex(Presentation, 0, &Selected, &Stream);
    ok(hr == S_OK, "GetStreamDescriptorByIndex returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    IMFStreamDescriptor_GetStreamIdentifier(Stream, &StreamId);
    ok(Selected && StreamId == 0, "Stream 0: selected %d id %lu\n", Selected, StreamId);

    hr = IMFStreamDescriptor_GetMediaTypeHandler(Stream, &Handler);
    ok(hr == S_OK, "GetMediaTypeHandler returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;

    hr = IMFMediaTypeHandler_GetMediaTypeCount(Handler, &Count);
    ok(hr == S_OK, "GetMediaTypeCount returned %#lx\n", hr);
    trace("Media types %lu\n", Count);
    for (Index = 0; Index < Count; Index++)
    {
        IMFMediaType *Type;
        GUID Major = { 0 }, Sub = { 0 };
        UINT64 Size = 0, Rate = 0, RateMin = 0, RateMax = 0, Aspect = 0;
        UINT32 Stride = 0, Fixed = 0, Temporal = 0, Sample = 0, Bitrate = 0, Interlace = 0, Attributes = 0;

        if (FAILED(IMFMediaTypeHandler_GetMediaTypeByIndex(Handler, Index, &Type)))
            continue;

        if (Index == 0 || Index == 8 || Index == 9)
        {
            char What[16];

            sprintf(What, "type%lu", Index);
            DumpAttributes(What, (IMFAttributes *)Type);
        }

        IMFMediaType_GetGUID(Type, &MF_MT_MAJOR_TYPE, &Major);
        IMFMediaType_GetGUID(Type, &MF_MT_SUBTYPE, &Sub);
        IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_SIZE, &Size);
        IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_RATE, &Rate);
        IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_RATE_RANGE_MIN, &RateMin);
        IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_RATE_RANGE_MAX, &RateMax);
        IMFMediaType_GetUINT64(Type, &MF_MT_PIXEL_ASPECT_RATIO, &Aspect);
        IMFMediaType_GetUINT32(Type, &MF_MT_DEFAULT_STRIDE, &Stride);
        IMFMediaType_GetUINT32(Type, &MF_MT_FIXED_SIZE_SAMPLES, &Fixed);
        IMFMediaType_GetUINT32(Type, &MF_MT_ALL_SAMPLES_INDEPENDENT, &Temporal);
        IMFMediaType_GetUINT32(Type, &MF_MT_SAMPLE_SIZE, &Sample);
        IMFMediaType_GetUINT32(Type, &MF_MT_AVG_BITRATE, &Bitrate);
        IMFMediaType_GetUINT32(Type, &MF_MT_INTERLACE_MODE, &Interlace);
        IMFMediaType_GetCount(Type, &Attributes);
        if (Index < ARRAYSIZE(ExpectTypes))
        {
            const EXPECT_TYPE *Expect = &ExpectTypes[Index];

            ok(IsEqualGUID(&Major, &MFMediaType_Video), "Type %lu major %s\n", Index, wine_dbgstr_guid(&Major));
            ok(IsEqualGUID(&Sub, &MFVideoFormat_YUY2), "Type %lu subtype %s\n", Index, wine_dbgstr_guid(&Sub));
            ok((UINT32)(Size >> 32) == Expect->Width && (UINT32)Size == Expect->Height, "Type %lu size %ux%u\n", Index, (UINT32)(Size >> 32), (UINT32)Size);
            ok((UINT32)(Rate >> 32) == Expect->Rate && (UINT32)Rate == 1, "Type %lu rate %u/%u\n", Index, (UINT32)(Rate >> 32), (UINT32)Rate);
            ok(RateMin == Rate && RateMax == Rate, "Type %lu rate range\n", Index);
            ok((UINT32)(Aspect >> 32) == 1 && (UINT32)Aspect == 1, "Type %lu aspect %u:%u\n", Index, (UINT32)(Aspect >> 32), (UINT32)Aspect);
            ok(Stride == Expect->Width * 2, "Type %lu stride %u\n", Index, Stride);
            ok(Fixed == 1 && Temporal == 1, "Type %lu fixed %u independent %u\n", Index, Fixed, Temporal);
            ok(Sample == Expect->Width * Expect->Height * 2, "Type %lu sample %u\n", Index, Sample);
            ok(Bitrate == Expect->Bitrate, "Type %lu bitrate %u\n", Index, Bitrate);
            ok(Interlace == MFVideoInterlace_Progressive, "Type %lu interlace %u\n", Index, Interlace);
        }
        if (IsEqualGUID(&Sub, &MFVideoFormat_MJPG))
            HasMjpeg = TRUE;

        trace("Type %lu: %s %ux%u rate %u/%u min %u/%u max %u/%u aspect %u:%u stride %d fixed %u indep %u sample %u bitrate %u interlace %u attrs %u\n",
              Index, wine_dbgstr_guid(&Sub), (UINT32)(Size >> 32), (UINT32)Size, (UINT32)(Rate >> 32), (UINT32)Rate,
              (UINT32)(RateMin >> 32), (UINT32)RateMin, (UINT32)(RateMax >> 32), (UINT32)RateMax,
              (UINT32)(Aspect >> 32), (UINT32)Aspect, (INT32)Stride, Fixed, Temporal, Sample, Bitrate, Interlace, Attributes);

        if (!*Yuy2 && IsEqualGUID(&Sub, &MFVideoFormat_YUY2) && (UINT32)(Size >> 32) == 640 && (UINT32)Size == 480 &&
            (UINT32)(Rate >> 32) == 30 && (UINT32)Rate == 1)
        {
            *Yuy2 = Type;
            IMFMediaType_AddRef(*Yuy2);
        }
        IMFMediaType_Release(Type);
    }

    ok(HasMjpeg, "No MJPG media type\n");

done:
    if (Handler)
        IMFMediaTypeHandler_Release(Handler);
    if (Stream)
        IMFStreamDescriptor_Release(Stream);
    IMFPresentationDescriptor_Release(Presentation);
}

static void
ReadFrames(IMFMediaSource *Source, IMFMediaType *Type)
{
    IMFSourceReader *Reader = NULL;
    ULONG Frame, Good = 0;
    HRESULT hr;

    hr = MFCreateSourceReaderFromMediaSource(Source, NULL, &Reader);
    ok(hr == S_OK, "MFCreateSourceReaderFromMediaSource returned %#lx\n", hr);
    if (FAILED(hr))
        return;

    hr = IMFSourceReader_SetCurrentMediaType(Reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, Type);
    ok(hr == S_OK, "SetCurrentMediaType returned %#lx\n", hr);

    for (Frame = 0; Frame < 40 && Good < UVC_TEST_FRAMES; Frame++)
    {
        IMFSample *Sample = NULL;
        IMFMediaBuffer *Buffer = NULL;
        DWORD Stream = 0, Flags = 0, Length = 0, Max = 0;
        LONGLONG Time = 0, Duration = 0;
        BYTE *Data = NULL;

        hr = IMFSourceReader_ReadSample(Reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &Stream, &Flags, &Time, &Sample);
        ok(hr == S_OK, "ReadSample returned %#lx\n", hr);
        if (FAILED(hr))
            break;
        ok(Stream == 0, "Sample %lu stream %lu\n", Frame, Stream);
        if (Frame == 0)
            ok(Flags == MF_SOURCE_READERF_STREAMTICK && !Sample, "First read flags %lx sample %p\n", Flags, Sample);
        if (!Sample)
            continue;

        IMFSample_GetSampleDuration(Sample, &Duration);
        hr = IMFSample_ConvertToContiguousBuffer(Sample, &Buffer);
        ok(hr == S_OK, "ConvertToContiguousBuffer returned %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = IMFMediaBuffer_Lock(Buffer, &Data, &Max, &Length);
            ok(hr == S_OK, "Lock returned %#lx\n", hr);
            if (SUCCEEDED(hr))
            {
                ULONG Sequence = 0;
                ULONG Errors = Length == 640 * 480 * 2 ? UvcCheckYuy2Frame(Data, 640, 480, &Sequence) : (ULONG)-2;

                ok(Duration == 333333, "Sample %lu duration %ld\n", Frame, (LONG)Duration);
                ok(Max == Length, "Sample %lu max %lu\n", Frame, Max);
                ok(Length == 640 * 480 * 2, "Sample length %lu\n", Length);
                ok(Errors == 0, "Sample %lu has %ld wrong pixels\n", Frame, (LONG)Errors);
                if (Errors == 0)
                    Good++;
                IMFMediaBuffer_Unlock(Buffer);
            }
            IMFMediaBuffer_Release(Buffer);
        }
        IMFSample_Release(Sample);
    }
    ok(Good == UVC_TEST_FRAMES, "Only %lu good samples of %u\n", Good, UVC_TEST_FRAMES);
    IMFSourceReader_Release(Reader);
}

static void
ReadDecoded(IMFActivate *Activate)
{
    IMFMediaSource *Source = NULL;
    IMFPresentationDescriptor *Presentation = NULL;
    IMFStreamDescriptor *Stream = NULL;
    IMFMediaTypeHandler *Handler = NULL;
    IMFMediaType *Nv12 = NULL;
    IMFSourceReader *Reader = NULL;
    BOOL Selected = FALSE;
    DWORD Count = 0, Index;
    ULONG Frame, Good = 0;
    HRESULT hr;

    IMFActivate_ShutdownObject(Activate);
    hr = IMFActivate_ActivateObject(Activate, &IID_IMFMediaSource, (void **)&Source);
    ok(hr == S_OK, "ActivateObject returned %#lx\n", hr);
    if (FAILED(hr))
        return;

    hr = IMFMediaSource_CreatePresentationDescriptor(Source, &Presentation);
    ok(hr == S_OK, "CreatePresentationDescriptor returned %#lx\n", hr);
    if (SUCCEEDED(hr))
        hr = IMFPresentationDescriptor_GetStreamDescriptorByIndex(Presentation, 0, &Selected, &Stream);
    if (SUCCEEDED(hr))
        hr = IMFStreamDescriptor_GetMediaTypeHandler(Stream, &Handler);
    if (SUCCEEDED(hr))
        hr = IMFMediaTypeHandler_GetMediaTypeCount(Handler, &Count);

    for (Index = 0; SUCCEEDED(hr) && Index < Count && !Nv12; Index++)
    {
        IMFMediaType *Type;
        GUID Sub = { 0 };

        if (FAILED(IMFMediaTypeHandler_GetMediaTypeByIndex(Handler, Index, &Type)))
            continue;
        IMFMediaType_GetGUID(Type, &MF_MT_SUBTYPE, &Sub);
        if (IsEqualGUID(&Sub, &MFVideoFormat_NV12))
            Nv12 = Type;
        else
            IMFMediaType_Release(Type);
    }
    ok(Nv12 != NULL, "No NV12 media type\n");

    if (Nv12)
        hr = MFCreateSourceReaderFromMediaSource(Source, NULL, &Reader);
    if (Nv12 && SUCCEEDED(hr))
    {
        hr = IMFSourceReader_SetCurrentMediaType(Reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, Nv12);
        ok(hr == S_OK, "SetCurrentMediaType NV12 returned %#lx\n", hr);
        for (Frame = 0; Frame < 40 && Good < 3; Frame++)
        {
            IMFSample *Sample = NULL;
            IMFMediaBuffer *Buffer = NULL;
            DWORD StreamIndex = 0, Flags = 0, Length = 0, Max = 0;
            LONGLONG Time = 0;
            BYTE *Data = NULL;

            hr = IMFSourceReader_ReadSample(Reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &StreamIndex, &Flags, &Time, &Sample);
            ok(hr == S_OK, "ReadSample NV12 returned %#lx\n", hr);
            if (FAILED(hr))
                break;
            if (!Sample)
                continue;
            if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(Sample, &Buffer)))
            {
                if (SUCCEEDED(IMFMediaBuffer_Lock(Buffer, &Data, &Max, &Length)))
                {
                    ok(Length == 640 * 480 * 3 / 2, "NV12 sample length %lu\n", Length);
                    if (Length == 640 * 480 * 3 / 2)
                    {
                        trace("MEASURE nv12 %lu max %lu y %u %u %u uv %u %u %u %u\n", Frame, Max, Data[0], Data[240 * 640 + 320], Data[479 * 640 + 639],
                              Data[640 * 480], Data[640 * 480 + 1], Data[640 * 480 + 120 * 640 + 320], Data[640 * 480 + 120 * 640 + 321]);
                        Good++;
                    }
                    IMFMediaBuffer_Unlock(Buffer);
                }
                IMFMediaBuffer_Release(Buffer);
            }
            IMFSample_Release(Sample);
        }
        ok(Good == 3, "Only %lu NV12 samples\n", Good);
        IMFSourceReader_Release(Reader);
    }

    if (Nv12)
        IMFMediaType_Release(Nv12);
    if (Handler)
        IMFMediaTypeHandler_Release(Handler);
    if (Stream)
        IMFStreamDescriptor_Release(Stream);
    if (Presentation)
        IMFPresentationDescriptor_Release(Presentation);
    IMFMediaSource_Shutdown(Source);
    IMFMediaSource_Release(Source);
}

START_TEST(MfCapture)
{
    IMFAttributes *Attributes = NULL;
    IMFActivate **Devices = NULL;
    IMFMediaSource *Source = NULL;
    IMFMediaType *Yuy2 = NULL;
    UINT32 Count = 0, Index, Target = (UINT32)-1;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx returned %#lx\n", hr);
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    ok(hr == S_OK, "MFStartup returned %#lx\n", hr);

    hr = MFCreateAttributes(&Attributes, 1);
    ok(hr == S_OK, "MFCreateAttributes returned %#lx\n", hr);
    hr = IMFAttributes_SetGUID(Attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    ok(hr == S_OK, "SetGUID returned %#lx\n", hr);

    hr = MFEnumDeviceSources(Attributes, &Devices, &Count);
    ok(hr == S_OK, "MFEnumDeviceSources returned %#lx\n", hr);
    trace("Video capture sources: %u\n", Count);
    for (Index = 0; Index < Count; Index++)
    {
        WCHAR *Name = NULL, *Link = NULL;
        UINT32 Length = 0, Attrs = 0;
        GUID Category = { 0 }, SourceType = { 0 };

        IMFActivate_GetCount(Devices[Index], &Attrs);
        IMFActivate_GetAllocatedString(Devices[Index], &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &Name, &Length);
        IMFActivate_GetAllocatedString(Devices[Index], &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &Link, &Length);
        IMFActivate_GetGUID(Devices[Index], &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY, &Category);
        IMFActivate_GetGUID(Devices[Index], &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &SourceType);
        trace("Device %u: attrs %u name '%ls' link '%ls' category %s type %s\n", Index, Attrs, Name, Link,
              wine_dbgstr_guid(&Category), wine_dbgstr_guid(&SourceType));
        if (Link && wcsstr(_wcslwr(Link), L"vid_46f4&pid_0099"))
        {
            Target = Index;
            ok(wcsstr(Link, L"{e5323777-f976-4f5b-9b55-b94699c46e44}\\global") != NULL, "Link '%ls'\n", Link);
            ok(Name && !wcscmp(Name, L"QEMU USB Camera"), "Name '%ls'\n", Name);
            ok(IsEqualGUID(&Category, &UvcCategoryCamera), "Category %s\n", wine_dbgstr_guid(&Category));
            ok(IsEqualGUID(&SourceType, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID), "Source type %s\n", wine_dbgstr_guid(&SourceType));
        }
        CoTaskMemFree(Name);
        CoTaskMemFree(Link);
        DumpAttributes("activate", (IMFAttributes *)Devices[Index]);
    }

    if (Target == (UINT32)-1)
    {
        skip("No usb-uvc test camera\n");
    }
    else
    {
        hr = IMFActivate_ActivateObject(Devices[Target], &IID_IMFMediaSource, (void **)&Source);
        ok(hr == S_OK, "ActivateObject returned %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            DWORD Characteristics = 0;

            hr = IMFMediaSource_GetCharacteristics(Source, &Characteristics);
            ok(hr == S_OK && Characteristics == (MFMEDIASOURCE_IS_LIVE | MFMEDIASOURCE_CAN_PAUSE), "Characteristics hr %#lx value %#lx\n", hr, Characteristics);
            DumpMediaTypes(Source, &Yuy2);
            ok(Yuy2 != NULL, "No YUY2 640x480 30 fps media type\n");
            if (Yuy2)
            {
                ReadFrames(Source, Yuy2);
                IMFMediaType_Release(Yuy2);
            }
            IMFMediaSource_Shutdown(Source);
            IMFMediaSource_Release(Source);
            ReadDecoded(Devices[Target]);
        }
    }

    for (Index = 0; Index < Count; Index++)
        IMFActivate_Release(Devices[Index]);
    CoTaskMemFree(Devices);
    IMFAttributes_Release(Attributes);
    MFShutdown();
    CoUninitialize();
}
