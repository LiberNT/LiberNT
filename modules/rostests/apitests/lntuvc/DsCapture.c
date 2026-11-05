/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for USB video capture through DirectShow
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include "lntuvc_test.h"
#include <dshow.h>
#include <qedit.h>

#define UVC_CAMERACONTROL_EXPOSURE 4

static const CLSID UvcClsidSampleGrabber = { 0xC1F400A0, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
static const CLSID UvcClsidNullRenderer = { 0xC1F400A4, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
static const CLSID UvcClsidProxy = { 0x17CCA71B, 0xECD7, 0x11D0, { 0xB9, 0x08, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };

static void
CheckMonikerName(IMoniker *Moniker, const WCHAR *Display)
{
    IBindCtx *Context = NULL;
    IMoniker *Parsed = NULL;
    ULONG Eaten = 0;
    HRESULT hr;

    if (!wcsstr(Display, L"vid_46f4&pid_0099"))
        return;

    ok(!wcsncmp(Display, L"@device:pnp:\\\\?\\usb#", 20), "Display name '%ls'\n", Display);
    hr = CreateBindCtx(0, &Context);
    ok(hr == S_OK, "CreateBindCtx returned %#lx\n", hr);
    if (FAILED(hr))
        return;
    hr = MkParseDisplayName(Context, Display, &Eaten, &Parsed);
    ok(hr == S_OK, "MkParseDisplayName returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(Eaten == wcslen(Display), "Eaten %lu\n", Eaten);
        hr = IMoniker_IsEqual(Moniker, Parsed);
        ok(hr == S_OK, "IsEqual returned %#lx\n", hr);
        IMoniker_Release(Parsed);
    }
    IBindCtx_Release(Context);
}

static void
FreeType(AM_MEDIA_TYPE *Type)
{
    CoTaskMemFree(Type->pbFormat);
    if (Type->pUnk)
        IUnknown_Release(Type->pUnk);
    CoTaskMemFree(Type);
}

static IPin *
FindPin(IBaseFilter *Filter, PIN_DIRECTION Direction)
{
    IEnumPins *Enum = NULL;
    IPin *Pin = NULL, *Result = NULL;

    if (FAILED(IBaseFilter_EnumPins(Filter, &Enum)))
        return NULL;
    while (!Result && IEnumPins_Next(Enum, 1, &Pin, NULL) == S_OK)
    {
        PIN_DIRECTION Current;

        IPin_QueryDirection(Pin, &Current);
        if (Current == Direction)
            Result = Pin;
        else
            IPin_Release(Pin);
    }
    IEnumPins_Release(Enum);
    return Result;
}

static void
DumpPins(IBaseFilter *Filter)
{
    IEnumPins *Enum = NULL;
    IPin *Pin = NULL;
    HRESULT hr;

    hr = IBaseFilter_EnumPins(Filter, &Enum);
    ok(hr == S_OK, "EnumPins returned %#lx\n", hr);
    if (FAILED(hr))
        return;

    while (IEnumPins_Next(Enum, 1, &Pin, NULL) == S_OK)
    {
        PIN_INFO Info = { 0 };
        IEnumMediaTypes *Types = NULL;
        AM_MEDIA_TYPE *Type;
        IAMStreamConfig *Config = NULL;
        IKsPropertySet *Set = NULL;
        ULONG Index = 0;

        IPin_QueryPinInfo(Pin, &Info);
        trace("Pin '%ls' direction %d\n", Info.achName, Info.dir);
        if (Info.dir == PINDIR_OUTPUT)
            ok(!wcscmp(Info.achName, L"Capture") || !wcscmp(Info.achName, L"Still"), "Output pin name '%ls'\n", Info.achName);
        if (Info.pFilter)
            IBaseFilter_Release(Info.pFilter);

        hr = IPin_QueryInterface(Pin, &IID_IKsPropertySet, (void **)&Set);
        trace(" IKsPropertySet %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            GUID Category = { 0 };
            DWORD Returned = 0;

            hr = IKsPropertySet_Get(Set, &AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, NULL, 0, &Category, sizeof(Category), &Returned);
            trace(" pin category hr %#lx %s\n", hr, wine_dbgstr_guid(&Category));
            IKsPropertySet_Release(Set);
        }

        if (SUCCEEDED(IPin_EnumMediaTypes(Pin, &Types)))
        {
            while (IEnumMediaTypes_Next(Types, 1, &Type, NULL) == S_OK)
            {
                trace(" type %lu: major %s sub %s fixed %d temporal %d sample %lu format %s cb %lu\n", Index, wine_dbgstr_guid(&Type->majortype),
                      wine_dbgstr_guid(&Type->subtype), Type->bFixedSizeSamples, Type->bTemporalCompression, Type->lSampleSize,
                      wine_dbgstr_guid(&Type->formattype), Type->cbFormat);
                FreeType(Type);
                Index++;
            }
            IEnumMediaTypes_Release(Types);
        }

        hr = IPin_QueryInterface(Pin, &IID_IAMStreamConfig, (void **)&Config);
        trace(" IAMStreamConfig %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            int Count = 0, Size = 0, Caps;

            hr = IAMStreamConfig_GetNumberOfCapabilities(Config, &Count, &Size);
            trace(" capabilities hr %#lx count %d size %d\n", hr, Count, Size);
            for (Caps = 0; Caps < Count && Size == sizeof(VIDEO_STREAM_CONFIG_CAPS); Caps++)
            {
                VIDEO_STREAM_CONFIG_CAPS Scc;

                hr = IAMStreamConfig_GetStreamCaps(Config, Caps, &Type, (BYTE *)&Scc);
                if (SUCCEEDED(hr))
                {
                    VIDEOINFOHEADER *Vih = (PVOID)Type->pbFormat;

                    trace(" caps %d: sub %s format %s size %ldx%ld avg %ld interval %ld..%ld\n", Caps, wine_dbgstr_guid(&Type->subtype),
                          wine_dbgstr_guid(&Type->formattype), Vih->bmiHeader.biWidth, Vih->bmiHeader.biHeight, (LONG)Vih->AvgTimePerFrame,
                          (LONG)Scc.MinFrameInterval, (LONG)Scc.MaxFrameInterval);
                    FreeType(Type);
                }
            }
            hr = IAMStreamConfig_GetFormat(Config, &Type);
            trace(" GetFormat hr %#lx\n", hr);
            if (SUCCEEDED(hr))
                FreeType(Type);
            IAMStreamConfig_Release(Config);
        }
        IPin_Release(Pin);
    }
    IEnumPins_Release(Enum);
}

static void
DumpProcAmp(IBaseFilter *Filter)
{
    IAMVideoProcAmp *ProcAmp = NULL;
    IAMCameraControl *Camera = NULL;
    HRESULT hr;
    long Min, Max, Step, Default, Flags, Value;

    hr = IBaseFilter_QueryInterface(Filter, &IID_IAMVideoProcAmp, (void **)&ProcAmp);
    trace("IAMVideoProcAmp %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Min = Max = Step = Default = Flags = Value = 0;
        hr = IAMVideoProcAmp_GetRange(ProcAmp, VideoProcAmp_Brightness, &Min, &Max, &Step, &Default, &Flags);
        ok(hr == S_OK && Min == -64 && Max == 64 && Step == 1 && Default == 0 && Flags == 2,
           "Brightness range hr %#lx %ld..%ld step %ld default %ld flags %lx\n", hr, Min, Max, Step, Default, Flags);
        hr = IAMVideoProcAmp_Get(ProcAmp, VideoProcAmp_Brightness, &Value, &Flags);
        ok(hr == S_OK && Value == 0 && Flags == 2, "Brightness get hr %#lx value %ld flags %lx\n", hr, Value, Flags);
        hr = IAMVideoProcAmp_Set(ProcAmp, VideoProcAmp_Brightness, 12, VideoProcAmp_Flags_Manual);
        ok(hr == S_OK, "Brightness set hr %#lx\n", hr);
        hr = IAMVideoProcAmp_Get(ProcAmp, VideoProcAmp_Brightness, &Value, &Flags);
        ok(hr == S_OK && Value == 12 && Flags == 2, "Brightness get hr %#lx value %ld flags %lx\n", hr, Value, Flags);
        hr = IAMVideoProcAmp_Set(ProcAmp, VideoProcAmp_Brightness, 0, VideoProcAmp_Flags_Manual);
        ok(hr == S_OK, "Brightness restore hr %#lx\n", hr);
        IAMVideoProcAmp_Release(ProcAmp);
    }

    hr = IBaseFilter_QueryInterface(Filter, &IID_IAMCameraControl, (void **)&Camera);
    ok(hr == S_OK, "IAMCameraControl %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        Min = Max = Step = Default = Flags = Value = 0;
        hr = IAMCameraControl_GetRange(Camera, UVC_CAMERACONTROL_EXPOSURE, &Min, &Max, &Step, &Default, &Flags);
        ok(hr == S_OK && Min == -13 && Max == 0 && Step == 1 && Default == -6 && Flags == 3,
           "Exposure range hr %#lx %ld..%ld step %ld default %ld flags %lx\n", hr, Min, Max, Step, Default, Flags);
        hr = IAMCameraControl_Get(Camera, UVC_CAMERACONTROL_EXPOSURE, &Value, &Flags);
        ok(hr == S_OK && Value == -6 && Flags == 1, "Exposure get hr %#lx value %ld flags %lx\n", hr, Value, Flags);
        IAMCameraControl_Release(Camera);
    }
}

static LONG
CheckDroppedFrames(IBaseFilter *Filter, IPin *Pin, const char *When)
{
    IAMDroppedFrames *Dropped = NULL;
    long Value = -1, Frames[4] = { -1, -1, -1, -1 }, Count = -1, Delivered = -1;
    HRESULT hr;

    hr = IBaseFilter_QueryInterface(Filter, &IID_IAMDroppedFrames, (void **)&Dropped);
    ok(hr == S_OK, "IAMDroppedFrames on the filter %s %#lx\n", When, hr);
    if (SUCCEEDED(hr))
        IAMDroppedFrames_Release(Dropped);
    Dropped = NULL;
    hr = IPin_QueryInterface(Pin, &IID_IAMDroppedFrames, (void **)&Dropped);
    ok(hr == S_OK, "IAMDroppedFrames on the pin %s %#lx\n", When, hr);
    if (FAILED(hr))
        return -1;
    hr = IAMDroppedFrames_GetNumDropped(Dropped, &Value);
    ok(hr == S_OK && Value == 0, "GetNumDropped %s %#lx %ld\n", When, hr, Value);
    hr = IAMDroppedFrames_GetNumNotDropped(Dropped, &Delivered);
    trace("MEASURE ds dropped %s not dropped %#lx %ld\n", When, hr, Delivered);
    ok(hr == S_OK, "GetNumNotDropped %s %#lx\n", When, hr);
    Value = -1;
    hr = IAMDroppedFrames_GetAverageFrameSize(Dropped, &Value);
    ok(hr == S_OK && Value == 640 * 480 * 2, "GetAverageFrameSize %s %#lx %ld\n", When, hr, Value);
    hr = IAMDroppedFrames_GetDroppedInfo(Dropped, 4, Frames, &Count);
    ok(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && Count == -1 && Frames[0] == -1, "GetDroppedInfo %s %#lx count %ld frame %ld\n",
       When, hr, Count, Frames[0]);
    hr = IAMDroppedFrames_GetNumDropped(Dropped, NULL);
    ok(hr == E_POINTER, "GetNumDropped without a pointer %s %#lx\n", When, hr);
    IAMDroppedFrames_Release(Dropped);
    return Delivered;
}

static void
Capture(IBaseFilter *Source)
{
    IGraphBuilder *Graph = NULL;
    IBaseFilter *Grabber = NULL, *Null = NULL;
    ISampleGrabber *SampleGrabber = NULL;
    IMediaControl *Control = NULL;
    IPin *Out = NULL, *In = NULL, *GrabOut = NULL, *NullIn = NULL;
    IAMStreamConfig *Config = NULL;
    AM_MEDIA_TYPE Type, *Format = NULL;
    VIDEO_STREAM_CONFIG_CAPS Caps;
    int Count = 0, Size = 0, Index;
    ULONG Tries, Good = 0;
    BYTE *Buffer = HeapAlloc(GetProcessHeap(), 0, 640 * 480 * 2);
    ULONG LastSequence = (ULONG)-1;
    LONG Delivered, Stopped;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IGraphBuilder, (void **)&Graph);
    ok(hr == S_OK, "FilterGraph returned %#lx\n", hr);
    if (FAILED(hr))
        return;
    hr = CoCreateInstance(&UvcClsidSampleGrabber, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&Grabber);
    ok(hr == S_OK, "SampleGrabber returned %#lx\n", hr);
    hr = CoCreateInstance(&UvcClsidNullRenderer, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&Null);
    ok(hr == S_OK, "NullRenderer returned %#lx\n", hr);
    if (!Grabber || !Null)
        goto done;

    IGraphBuilder_AddFilter(Graph, Source, L"Source");
    IGraphBuilder_AddFilter(Graph, Grabber, L"Grabber");
    IGraphBuilder_AddFilter(Graph, Null, L"Null");
    IBaseFilter_QueryInterface(Grabber, &IID_ISampleGrabber, (void **)&SampleGrabber);

    Out = FindPin(Source, PINDIR_OUTPUT);
    In = FindPin(Grabber, PINDIR_INPUT);
    GrabOut = FindPin(Grabber, PINDIR_OUTPUT);
    NullIn = FindPin(Null, PINDIR_INPUT);
    ok(Out && In && GrabOut && NullIn, "Missing pins %p %p %p %p\n", Out, In, GrabOut, NullIn);
    if (!Out || !In || !GrabOut || !NullIn)
        goto done;

    hr = IPin_QueryInterface(Out, &IID_IAMStreamConfig, (void **)&Config);
    ok(hr == S_OK, "IAMStreamConfig returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    IAMStreamConfig_GetNumberOfCapabilities(Config, &Count, &Size);
    for (Index = 0; Index < Count; Index++)
    {
        BOOL Match = FALSE;

        if (FAILED(IAMStreamConfig_GetStreamCaps(Config, Index, &Format, (BYTE *)&Caps)))
            continue;
        if (IsEqualGUID(&Format->subtype, &MEDIASUBTYPE_YUY2) && IsEqualGUID(&Format->formattype, &FORMAT_VideoInfo))
        {
            VIDEOINFOHEADER *Vih = (PVOID)Format->pbFormat;

            Match = Vih->bmiHeader.biWidth == 640 && Vih->bmiHeader.biHeight == 480;
        }
        if (Match)
        {
            hr = IAMStreamConfig_SetFormat(Config, Format);
            ok(hr == S_OK, "SetFormat returned %#lx\n", hr);
            FreeType(Format);
            break;
        }
        FreeType(Format);
    }
    ok(Index < Count, "No YUY2 640x480 caps\n");
    IAMStreamConfig_Release(Config);

    ZeroMemory(&Type, sizeof(Type));
    Type.majortype = MEDIATYPE_Video;
    Type.subtype = MEDIASUBTYPE_YUY2;
    Type.formattype = FORMAT_VideoInfo;
    hr = ISampleGrabber_SetMediaType(SampleGrabber, &Type);
    ok(hr == S_OK, "SetMediaType returned %#lx\n", hr);
    hr = ISampleGrabber_SetBufferSamples(SampleGrabber, TRUE);
    ok(hr == S_OK, "SetBufferSamples returned %#lx\n", hr);

    hr = IGraphBuilder_ConnectDirect(Graph, Out, In, NULL);
    ok(hr == S_OK, "Connect source returned %#lx\n", hr);
    hr = IGraphBuilder_ConnectDirect(Graph, GrabOut, NullIn, NULL);
    ok(hr == S_OK, "Connect null returned %#lx\n", hr);

    Delivered = CheckDroppedFrames(Source, Out, "connected");
    ok(Delivered == 0, "Frames delivered before the run %ld\n", Delivered);
    hr = IGraphBuilder_QueryInterface(Graph, &IID_IMediaControl, (void **)&Control);
    ok(hr == S_OK, "IMediaControl returned %#lx\n", hr);
    hr = IMediaControl_Run(Control);
    trace("Run hr %#lx\n", hr);
    ok(SUCCEEDED(hr), "Run returned %#lx\n", hr);

    for (Tries = 0; Tries < 60 && Good < UVC_TEST_FRAMES; Tries++)
    {
        long Length = 640 * 480 * 2;

        Sleep(50);
        hr = ISampleGrabber_GetCurrentBuffer(SampleGrabber, &Length, (long *)Buffer);
        if (SUCCEEDED(hr) && Length == 640 * 480 * 2)
        {
            ULONG Sequence = 0;
            ULONG Errors = UvcCheckYuy2Frame(Buffer, 640, 480, &Sequence);

            if (Errors == (ULONG)-1)
            {
                trace("Buffer %lu has no signature: %02x %02x %02x %02x\n", Tries, Buffer[0], Buffer[1], Buffer[2], Buffer[3]);
            }
            else if (Sequence != LastSequence)
            {
                trace("Buffer %lu sequence %lu errors %lu\n", Tries, Sequence, Errors);
                ok(Errors == 0, "Sequence %lu has %lu wrong pixels\n", Sequence, Errors);
                if (!Errors)
                    Good++;
                LastSequence = Sequence;
            }
        }
    }
    ok(Good >= UVC_TEST_FRAMES, "Only %lu good samples\n", Good);
    Delivered = CheckDroppedFrames(Source, Out, "running");
    ok(Delivered > 0, "Frames delivered while running %ld\n", Delivered);

    hr = IMediaControl_Stop(Control);
    ok(SUCCEEDED(hr), "Stop returned %#lx\n", hr);
    Stopped = CheckDroppedFrames(Source, Out, "stopped");
    ok(Stopped >= Delivered, "Frames delivered after the stop %ld, while running %ld\n", Stopped, Delivered);

done:
    HeapFree(GetProcessHeap(), 0, Buffer);
    if (Control)
        IMediaControl_Release(Control);
    if (SampleGrabber)
        ISampleGrabber_Release(SampleGrabber);
    if (NullIn)
        IPin_Release(NullIn);
    if (GrabOut)
        IPin_Release(GrabOut);
    if (In)
        IPin_Release(In);
    if (Out)
        IPin_Release(Out);
    if (Null)
        IBaseFilter_Release(Null);
    if (Grabber)
        IBaseFilter_Release(Grabber);
    IGraphBuilder_Release(Graph);
}

START_TEST(DsCapture)
{
    ICreateDevEnum *DevEnum = NULL;
    IEnumMoniker *Enum = NULL;
    IMoniker *Moniker = NULL;
    IBaseFilter *Target = NULL;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx returned %#lx\n", hr);

    hr = CoCreateInstance(&CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, &IID_ICreateDevEnum, (void **)&DevEnum);
    ok(hr == S_OK, "SystemDeviceEnum returned %#lx\n", hr);
    if (FAILED(hr))
        goto done;

    hr = ICreateDevEnum_CreateClassEnumerator(DevEnum, &CLSID_VideoInputDeviceCategory, &Enum, 0);
    trace("CreateClassEnumerator hr %#lx\n", hr);
    if (hr == S_OK)
    {
        while (IEnumMoniker_Next(Enum, 1, &Moniker, NULL) == S_OK)
        {
            WCHAR *Display = NULL;
            IPropertyBag *Bag = NULL;

            IMoniker_GetDisplayName(Moniker, NULL, NULL, &Display);
            trace("Moniker '%ls'\n", Display);
            CheckMonikerName(Moniker, Display);
            CoTaskMemFree(Display);

            hr = IMoniker_BindToStorage(Moniker, NULL, NULL, &IID_IPropertyBag, (void **)&Bag);
            ok(hr == S_OK, "BindToStorage returned %#lx\n", hr);
            if (SUCCEEDED(hr))
            {
                static const WCHAR * const Names[] = { L"FriendlyName", L"DevicePath", L"CLSID", L"Description", L"WaveInID", L"FilterData", L"VFWIndex" };
                ULONG Index;
                BOOL IsTarget = FALSE;

                for (Index = 0; Index < ARRAYSIZE(Names); Index++)
                {
                    VARIANT Value;

                    VariantInit(&Value);
                    hr = IPropertyBag_Read(Bag, Names[Index], &Value, NULL);
                    if (SUCCEEDED(hr) && V_VT(&Value) == VT_BSTR)
                    {
                        trace(" %ls = '%ls'\n", Names[Index], V_BSTR(&Value));
                        if (Index == 1 && wcsstr(_wcslwr(V_BSTR(&Value)), L"vid_46f4&pid_0099"))
                        {
                            IsTarget = TRUE;
                            ok(wcsstr(V_BSTR(&Value), L"{65e8773d-8f56-11d0-a3b9-00a0c9223196}\\global") != NULL, "DevicePath '%ls'\n", V_BSTR(&Value));
                        }
                        if (Index == 2)
                            ok(!wcsicmp(V_BSTR(&Value), L"{17CCA71B-ECD7-11D0-B908-00A0C9223196}"), "CLSID '%ls'\n", V_BSTR(&Value));
                    }
                    else
                    {
                        trace(" %ls hr %#lx vt %u\n", Names[Index], hr, V_VT(&Value));
                    }
                    VariantClear(&Value);
                }
                IPropertyBag_Release(Bag);

                if (IsTarget && !Target)
                {
                    hr = IMoniker_BindToObject(Moniker, NULL, NULL, &IID_IBaseFilter, (void **)&Target);
                    ok(hr == S_OK, "BindToObject returned %#lx\n", hr);
                }
            }
            IMoniker_Release(Moniker);
        }
        IEnumMoniker_Release(Enum);
    }

    if (!Target)
    {
        skip("No usb-uvc test camera\n");
    }
    else
    {
        CLSID Clsid = { 0 };
        IPersist *Persist = NULL;

        if (SUCCEEDED(IBaseFilter_QueryInterface(Target, &IID_IPersist, (void **)&Persist)))
        {
            IPersist_GetClassID(Persist, &Clsid);
            trace("Filter CLSID %s\n", wine_dbgstr_guid(&Clsid));
            ok(IsEqualGUID(&Clsid, &UvcClsidProxy), "Filter CLSID %s\n", wine_dbgstr_guid(&Clsid));
            IPersist_Release(Persist);
        }
        DumpPins(Target);
        DumpProcAmp(Target);
        Capture(Target);
        IBaseFilter_Release(Target);
    }
    ICreateDevEnum_Release(DevEnum);

done:
    CoUninitialize();
}
