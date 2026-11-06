/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the interfaces and streams of a USB video media source
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <stdio.h>
#include "lntuvc_test.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <dshow.h>

#define UVC_CAMERACONTROL_EXPOSURE 4

static const GUID UvcIidKsControl = { 0x28F54685, 0x06FD, 0x11D2, { 0xB2, 0x7A, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };

typedef struct _UVC_KSCONTROL UVC_KSCONTROL;
typedef struct _UVC_KSCONTROL_VTBL
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UVC_KSCONTROL *This, REFIID Iid, void **Out);
    ULONG (STDMETHODCALLTYPE *AddRef)(UVC_KSCONTROL *This);
    ULONG (STDMETHODCALLTYPE *Release)(UVC_KSCONTROL *This);
    HRESULT (STDMETHODCALLTYPE *KsProperty)(UVC_KSCONTROL *This, KSPROPERTY *Property, ULONG PropertyLength, void *Data, ULONG DataLength, ULONG *Returned);
    HRESULT (STDMETHODCALLTYPE *KsMethod)(UVC_KSCONTROL *This, KSMETHOD *Method, ULONG MethodLength, void *Data, ULONG DataLength, ULONG *Returned);
    HRESULT (STDMETHODCALLTYPE *KsEvent)(UVC_KSCONTROL *This, KSEVENT *Event, ULONG EventLength, void *Data, ULONG DataLength, ULONG *Returned);
} UVC_KSCONTROL_VTBL;
struct _UVC_KSCONTROL
{
    const UVC_KSCONTROL_VTBL *lpVtbl;
};

static void
DumpAttributes(const char *What, IMFAttributes *Attributes)
{
    UINT32 Count = 0, Index;

    IMFAttributes_GetCount(Attributes, &Count);
    trace("MEASURE %s attributes %u\n", What, Count);
    for (Index = 0; Index < Count; Index++)
    {
        GUID Key;
        PROPVARIANT Value;

        PropVariantInit(&Value);
        if (FAILED(IMFAttributes_GetItemByIndex(Attributes, Index, &Key, &Value)))
            continue;
        if (Value.vt == VT_UI4)
            trace("MEASURE %s %s value %lu\n", What, wine_dbgstr_guid(&Key), Value.ulVal);
        else if (Value.vt == VT_UI8)
            trace("MEASURE %s %s value %I64x\n", What, wine_dbgstr_guid(&Key), Value.uhVal.QuadPart);
        else if (Value.vt == VT_CLSID)
            trace("MEASURE %s %s value %s\n", What, wine_dbgstr_guid(&Key), wine_dbgstr_guid(Value.puuid));
        else if (Value.vt == VT_LPWSTR)
            trace("MEASURE %s %s value %ls\n", What, wine_dbgstr_guid(&Key), Value.pwszVal);
        else
            trace("MEASURE %s %s vt %u\n", What, wine_dbgstr_guid(&Key), Value.vt);
        PropVariantClear(&Value);
    }
}

static void
DumpInterfaces(IMFMediaSource *Source)
{
    static const struct { const char *Name; const GUID *Iid; } Probes[] =
    {
        { "IAMCameraControl", &IID_IAMCameraControl },
        { "IAMVideoProcAmp", &IID_IAMVideoProcAmp },
        { "IAMVideoControl", &IID_IAMVideoControl },
        { "IAMStreamConfig", &IID_IAMStreamConfig },
        { "IMFGetService", &IID_IMFGetService },
        { "IKsControl", &UvcIidKsControl },
    };
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Probes); Index++)
    {
        IUnknown *Unknown = NULL;
        HRESULT hr = IMFMediaSource_QueryInterface(Source, Probes[Index].Iid, (void **)&Unknown);

        trace("MEASURE interface %s %#lx\n", Probes[Index].Name, hr);
        if (SUCCEEDED(hr))
            IUnknown_Release(Unknown);
    }
}

static void
DumpControls(IMFMediaSource *Source)
{
    IAMVideoProcAmp *ProcAmp = NULL;
    IAMCameraControl *Camera = NULL;
    UVC_KSCONTROL *Control = NULL;
    LONG Min = 0, Max = 0, Step = 0, Default = 0, Flags = 0, Value = 0;
    HRESULT hr;

    if (SUCCEEDED(IMFMediaSource_QueryInterface(Source, &IID_IAMVideoProcAmp, (void **)&ProcAmp)))
    {
        hr = IAMVideoProcAmp_GetRange(ProcAmp, VideoProcAmp_Brightness, &Min, &Max, &Step, &Default, &Flags);
        trace("MEASURE procamp range %#lx %ld %ld %ld %ld %lx\n", hr, Min, Max, Step, Default, Flags);
        hr = IAMVideoProcAmp_Get(ProcAmp, VideoProcAmp_Brightness, &Value, &Flags);
        trace("MEASURE procamp get %#lx %ld %lx\n", hr, Value, Flags);
        hr = IAMVideoProcAmp_Set(ProcAmp, VideoProcAmp_Brightness, 5, VideoProcAmp_Flags_Manual);
        trace("MEASURE procamp set %#lx\n", hr);
        hr = IAMVideoProcAmp_Get(ProcAmp, VideoProcAmp_Brightness, &Value, &Flags);
        trace("MEASURE procamp after set %#lx %ld %lx\n", hr, Value, Flags);
        IAMVideoProcAmp_Set(ProcAmp, VideoProcAmp_Brightness, 0, VideoProcAmp_Flags_Manual);
        IAMVideoProcAmp_Release(ProcAmp);
    }
    if (SUCCEEDED(IMFMediaSource_QueryInterface(Source, &IID_IAMCameraControl, (void **)&Camera)))
    {
        hr = IAMCameraControl_GetRange(Camera, UVC_CAMERACONTROL_EXPOSURE, &Min, &Max, &Step, &Default, &Flags);
        trace("MEASURE camera range %#lx %ld %ld %ld %ld %lx\n", hr, Min, Max, Step, Default, Flags);
        hr = IAMCameraControl_Get(Camera, UVC_CAMERACONTROL_EXPOSURE, &Value, &Flags);
        trace("MEASURE camera get %#lx %ld %lx\n", hr, Value, Flags);
        IAMCameraControl_Release(Camera);
    }
    {
        IAMVideoControl *Video = NULL;

        if (SUCCEEDED(IMFMediaSource_QueryInterface(Source, &IID_IAMVideoControl, (void **)&Video)))
        {
            LONGLONG Rate = 0;
            LONGLONG *List = NULL;
            LONG Count = 0;
            SIZE Dimensions = { 640, 480 };

            Flags = 0;
            hr = IAMVideoControl_GetCaps(Video, NULL, &Flags);
            trace("MEASURE videocontrol caps %#lx %#lx\n", hr, Flags);
            hr = IAMVideoControl_GetMode(Video, NULL, &Flags);
            trace("MEASURE videocontrol mode %#lx %#lx\n", hr, Flags);
            hr = IAMVideoControl_GetCurrentActualFrameRate(Video, NULL, &Rate);
            trace("MEASURE videocontrol actual %#lx %I64d\n", hr, Rate);
            hr = IAMVideoControl_GetMaxAvailableFrameRate(Video, NULL, 0, Dimensions, &Rate);
            trace("MEASURE videocontrol max %#lx %I64d\n", hr, Rate);
            hr = IAMVideoControl_GetFrameRateList(Video, NULL, 0, Dimensions, &Count, &List);
            trace("MEASURE videocontrol list %#lx %ld\n", hr, Count);
            if (SUCCEEDED(hr))
                CoTaskMemFree(List);
            IAMVideoControl_Release(Video);
        }
    }
    {
        IMFGetService *Service = NULL;

        if (SUCCEEDED(IMFMediaSource_QueryInterface(Source, &IID_IMFGetService, (void **)&Service)))
        {
            IUnknown *Unknown = NULL;

            hr = IMFGetService_GetService(Service, &GUID_NULL, &IID_IUnknown, (void **)&Unknown);
            trace("MEASURE getservice null %#lx\n", hr);
            if (SUCCEEDED(hr))
                IUnknown_Release(Unknown);
            IMFGetService_Release(Service);
        }
    }
    if (SUCCEEDED(IMFMediaSource_QueryInterface(Source, &UvcIidKsControl, (void **)&Control)))
    {
        KSPROPERTY_VIDEOPROCAMP_S Request, Reply;
        ULONG Returned = 0;

        ZeroMemory(&Request, sizeof(Request));
        ZeroMemory(&Reply, sizeof(Reply));
        Request.Property.Set = UvcPropsetProcAmp;
        Request.Property.Id = KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS;
        Request.Property.Flags = KSPROPERTY_TYPE_GET;
        hr = Control->lpVtbl->KsProperty(Control, &Request.Property, sizeof(Request), &Reply, sizeof(Reply), &Returned);
        trace("MEASURE kscontrol get %#lx returned %lu value %ld flags %lx caps %lx\n", hr, Returned, Reply.Value, Reply.Flags, Reply.Capabilities);
        Control->lpVtbl->Release(Control);
    }
}

static void
DumpStreams(IMFMediaSource *Source)
{
    IMFPresentationDescriptor *Presentation = NULL;
    DWORD Streams = 0, Index;

    if (FAILED(IMFMediaSource_CreatePresentationDescriptor(Source, &Presentation)))
        return;
    DumpAttributes("presentation", (IMFAttributes *)Presentation);
    IMFPresentationDescriptor_GetStreamDescriptorCount(Presentation, &Streams);
    trace("MEASURE streams %lu\n", Streams);
    for (Index = 0; Index < Streams; Index++)
    {
        IMFStreamDescriptor *Stream = NULL;
        IMFMediaTypeHandler *Handler = NULL;
        BOOL Selected = FALSE;
        DWORD Id = 0, Count = 0, Type;
        char What[32];

        if (FAILED(IMFPresentationDescriptor_GetStreamDescriptorByIndex(Presentation, Index, &Selected, &Stream)))
            continue;
        IMFStreamDescriptor_GetStreamIdentifier(Stream, &Id);
        trace("MEASURE stream %lu id %lu selected %d\n", Index, Id, Selected);
        sprintf(What, "stream%lu", Index);
        DumpAttributes(What, (IMFAttributes *)Stream);
        if (SUCCEEDED(IMFStreamDescriptor_GetMediaTypeHandler(Stream, &Handler)))
        {
            IMFMediaTypeHandler_GetMediaTypeCount(Handler, &Count);
            trace("MEASURE stream %lu types %lu\n", Index, Count);
            for (Type = 0; Type < Count; Type++)
            {
                IMFMediaType *MediaType = NULL;
                GUID Sub = { 0 };
                UINT64 Size = 0, Rate = 0;

                if (FAILED(IMFMediaTypeHandler_GetMediaTypeByIndex(Handler, Type, &MediaType)))
                    continue;
                IMFMediaType_GetGUID(MediaType, &MF_MT_SUBTYPE, &Sub);
                IMFMediaType_GetUINT64(MediaType, &MF_MT_FRAME_SIZE, &Size);
                IMFMediaType_GetUINT64(MediaType, &MF_MT_FRAME_RATE, &Rate);
                trace("MEASURE stream %lu type %lu %s %ux%u rate %u/%u\n", Index, Type, wine_dbgstr_guid(&Sub), (UINT32)(Size >> 32), (UINT32)Size, (UINT32)(Rate >> 32), (UINT32)Rate);
                if (Index && Type == 0)
                    DumpAttributes("stilltype", (IMFAttributes *)MediaType);
                IMFMediaType_Release(MediaType);
            }
            IMFMediaTypeHandler_Release(Handler);
        }
        IMFStreamDescriptor_Release(Stream);
    }
    IMFPresentationDescriptor_Release(Presentation);
}

typedef struct _UVC_MODE_REQUEST
{
    KSPROPERTY Property;
    ULONG StreamIndex;
    LONG Mode;
} UVC_MODE_REQUEST;

static void
TriggerStill(IMFMediaSource *Source)
{
    UVC_KSCONTROL *Control = NULL;
    UVC_MODE_REQUEST Mode;
    ULONG Returned = 0;
    HRESULT hr;

    if (FAILED(IMFMediaSource_QueryInterface(Source, &UvcIidKsControl, (void **)&Control)))
        return;
    ZeroMemory(&Mode, sizeof(Mode));
    Mode.Property.Set.Data1 = 0x6a2e0670;
    Mode.Property.Set.Data2 = 0x28e4;
    Mode.Property.Set.Data3 = 0x11d0;
    memcpy(Mode.Property.Set.Data4, "\xa1\x8c\x00\xa0\xc9\x11\x89\x56", 8);
    Mode.Property.Id = KSPROPERTY_VIDEOCONTROL_MODE;
    Mode.Property.Flags = KSPROPERTY_TYPE_SET;
    for (Mode.StreamIndex = 0; Mode.StreamIndex < 3; Mode.StreamIndex++)
    {
        UVC_MODE_REQUEST Caps = Mode;

        Caps.Property.Id = KSPROPERTY_VIDEOCONTROL_CAPS;
        Caps.Property.Flags = KSPROPERTY_TYPE_GET;
        Caps.Mode = 0;
        hr = Control->lpVtbl->KsProperty(Control, &Caps.Property, sizeof(Caps), &Caps, sizeof(Caps), &Returned);
        trace("MEASURE kscontrol caps stream %lu %#lx value %lx\n", Mode.StreamIndex, hr, Caps.Mode);
    }
    Mode.StreamIndex = 1;
    Mode.Mode = 8;
    hr = Control->lpVtbl->KsProperty(Control, &Mode.Property, sizeof(Mode), &Mode, sizeof(Mode), &Returned);
    trace("MEASURE trigger through kscontrol %#lx\n", hr);
    Control->lpVtbl->Release(Control);
}

static void
ReadStill(IMFMediaSource *Source)
{
    IMFPresentationDescriptor *Presentation = NULL;
    IMFMediaStream *Streams[2] = { NULL, NULL };
    PROPVARIANT Start;
    DWORD Count = 0, Tries;
    BOOL Started = FALSE, Got = FALSE;
    ULONG CaptureSamples = 0;
    HRESULT hr;

    if (FAILED(IMFMediaSource_CreatePresentationDescriptor(Source, &Presentation)))
        return;
    IMFPresentationDescriptor_GetStreamDescriptorCount(Presentation, &Count);
    if (Count < 2)
    {
        IMFPresentationDescriptor_Release(Presentation);
        return;
    }
    hr = IMFPresentationDescriptor_SelectStream(Presentation, 1);
    trace("MEASURE select still %#lx\n", hr);

    PropVariantInit(&Start);
    hr = IMFMediaSource_Start(Source, Presentation, NULL, &Start);
    trace("MEASURE source start %#lx\n", hr);
    ok(hr == S_OK, "Start returned %#lx\n", hr);

    for (Tries = 0; Tries < 200 && !Started; Tries++)
    {
        IMFMediaEvent *Event = NULL;
        MediaEventType Type = 0;

        if (FAILED(IMFMediaSource_GetEvent(Source, MF_EVENT_FLAG_NO_WAIT, &Event)))
        {
            Sleep(10);
            continue;
        }
        IMFMediaEvent_GetType(Event, &Type);
        trace("MEASURE source event %lu\n", Type);
        if (Type == MENewStream || Type == MEUpdatedStream)
        {
            PROPVARIANT Value;
            IMFMediaStream *Stream = NULL;

            PropVariantInit(&Value);
            IMFMediaEvent_GetValue(Event, &Value);
            if (Value.vt == VT_UNKNOWN && SUCCEEDED(IUnknown_QueryInterface(Value.punkVal, &IID_IMFMediaStream, (void **)&Stream)))
            {
                IMFStreamDescriptor *Descriptor = NULL;
                DWORD Id = 0;

                IMFMediaStream_GetStreamDescriptor(Stream, &Descriptor);
                IMFStreamDescriptor_GetStreamIdentifier(Descriptor, &Id);
                IMFStreamDescriptor_Release(Descriptor);
                trace("MEASURE new stream id %lu\n", Id);
                if (Id < 2 && !Streams[Id])
                    Streams[Id] = Stream;
                else
                    IMFMediaStream_Release(Stream);
            }
            PropVariantClear(&Value);
        }
        if (Type == MESourceStarted)
            Started = TRUE;
        IMFMediaEvent_Release(Event);
    }
    ok(Started, "The source never started\n");

    if (Streams[1])
    {
        hr = IMFMediaStream_RequestSample(Streams[1], NULL);
        trace("MEASURE still request %#lx\n", hr);
        if (Streams[0])
        {
            IMFMediaStream_RequestSample(Streams[0], NULL);
            IMFMediaStream_RequestSample(Streams[0], NULL);
        }
        Sleep(300);
        TriggerStill(Source);
        for (Tries = 0; Tries < 300 && !Got; Tries++)
        {
            IMFMediaEvent *Event = NULL;
            MediaEventType Type = 0;

            if (Streams[0] && SUCCEEDED(IMFMediaStream_GetEvent(Streams[0], MF_EVENT_FLAG_NO_WAIT, &Event)))
            {
                IMFMediaEvent_GetType(Event, &Type);
                if (Type == MEMediaSample)
                {
                    CaptureSamples++;
                    IMFMediaStream_RequestSample(Streams[0], NULL);
                }
                IMFMediaEvent_Release(Event);
                Event = NULL;
            }

            if (FAILED(IMFMediaStream_GetEvent(Streams[1], MF_EVENT_FLAG_NO_WAIT, &Event)))
            {
                Sleep(10);
                continue;
            }
            IMFMediaEvent_GetType(Event, &Type);
            trace("MEASURE still event %lu\n", Type);
            if (Type == MEMediaSample)
            {
                PROPVARIANT Value;
                IMFSample *Sample = NULL;

                PropVariantInit(&Value);
                IMFMediaEvent_GetValue(Event, &Value);
                if (Value.vt == VT_UNKNOWN && SUCCEEDED(IUnknown_QueryInterface(Value.punkVal, &IID_IMFSample, (void **)&Sample)))
                {
                    DWORD Length = 0;
                    LONGLONG Duration = -1, Time = -1;
                    HRESULT DurationResult, TimeResult;

                    IMFSample_GetTotalLength(Sample, &Length);
                    DurationResult = IMFSample_GetSampleDuration(Sample, &Duration);
                    TimeResult = IMFSample_GetSampleTime(Sample, &Time);
                    trace("MEASURE still sample length %lu duration %#lx %I64d time %#lx\n", Length, DurationResult, Duration, TimeResult);
                    ok(DurationResult == S_OK && Duration == 0 && TimeResult == S_OK, "Still sample duration %#lx %I64d time %#lx\n",
                       DurationResult, Duration, TimeResult);
                    IMFSample_Release(Sample);
                }
                PropVariantClear(&Value);
                Got = TRUE;
            }
            IMFMediaEvent_Release(Event);
        }
        trace("MEASURE still sample received %d, capture samples %lu\n", Got, CaptureSamples);
    }

    IMFMediaSource_Stop(Source);
    if (Streams[0])
        IMFMediaStream_Release(Streams[0]);
    if (Streams[1])
        IMFMediaStream_Release(Streams[1]);
    IMFPresentationDescriptor_Release(Presentation);
}

START_TEST(MfControls)
{
    IMFAttributes *Attributes = NULL;
    IMFActivate **Devices = NULL;
    IMFMediaSource *Source = NULL;
    UINT32 Count = 0, Index;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx returned %#lx\n", hr);
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    ok(hr == S_OK, "MFStartup returned %#lx\n", hr);

    MFCreateAttributes(&Attributes, 1);
    IMFAttributes_SetGUID(Attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    hr = MFEnumDeviceSources(Attributes, &Devices, &Count);
    ok(hr == S_OK, "MFEnumDeviceSources returned %#lx\n", hr);

    if (!Count)
    {
        skip("No usb-uvc test camera\n");
    }
    else
    {
        hr = IMFActivate_ActivateObject(Devices[0], &IID_IMFMediaSource, (void **)&Source);
        ok(hr == S_OK, "ActivateObject returned %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            DumpInterfaces(Source);
            DumpControls(Source);
            DumpStreams(Source);
            ReadStill(Source);
            IMFMediaSource_Shutdown(Source);
            IMFMediaSource_Release(Source);
        }
    }

    for (Index = 0; Index < Count; Index++)
        IMFActivate_Release(Devices[Index]);
    CoTaskMemFree(Devices);
    IMFAttributes_Release(Attributes);
    MFShutdown();
    CoUninitialize();
}
