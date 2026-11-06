/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the still pin and IAMVideoControl of a USB video device through DirectShow
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include "lntuvc_test.h"
#include <dshow.h>
#include <qedit.h>

#define UVC_VIDEOCONTROL_TRIGGER 8

static const CLSID UvcClsidSampleGrabber = { 0xC1F400A0, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
static const CLSID UvcClsidNullRenderer = { 0xC1F400A4, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };

static IBaseFilter *
BindCamera(void)
{
    ICreateDevEnum *DevEnum = NULL;
    IEnumMoniker *Enum = NULL;
    IMoniker *Moniker = NULL;
    IBaseFilter *Filter = NULL;

    if (FAILED(CoCreateInstance(&CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, &IID_ICreateDevEnum, (void **)&DevEnum)))
        return NULL;
    if (ICreateDevEnum_CreateClassEnumerator(DevEnum, &CLSID_VideoInputDeviceCategory, &Enum, 0) == S_OK)
    {
        while (!Filter && IEnumMoniker_Next(Enum, 1, &Moniker, NULL) == S_OK)
        {
            IMoniker_BindToObject(Moniker, NULL, NULL, &IID_IBaseFilter, (void **)&Filter);
            IMoniker_Release(Moniker);
        }
        IEnumMoniker_Release(Enum);
    }
    ICreateDevEnum_Release(DevEnum);
    return Filter;
}

static IPin *
FindPinByName(IBaseFilter *Filter, const WCHAR *Name)
{
    IEnumPins *Enum = NULL;
    IPin *Pin = NULL, *Result = NULL;

    if (FAILED(IBaseFilter_EnumPins(Filter, &Enum)))
        return NULL;
    while (!Result && IEnumPins_Next(Enum, 1, &Pin, NULL) == S_OK)
    {
        PIN_INFO Info = { 0 };

        IPin_QueryPinInfo(Pin, &Info);
        if (Info.pFilter)
            IBaseFilter_Release(Info.pFilter);
        if (!wcscmp(Info.achName, Name))
            Result = Pin;
        else
            IPin_Release(Pin);
    }
    IEnumPins_Release(Enum);
    return Result;
}

static IPin *
FindPinByDirection(IBaseFilter *Filter, PIN_DIRECTION Direction)
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
DumpVideoControl(IBaseFilter *Filter, IAMVideoControl *Control)
{
    IEnumPins *Enum = NULL;
    IPin *Pin = NULL;

    if (FAILED(IBaseFilter_EnumPins(Filter, &Enum)))
        return;

    while (IEnumPins_Next(Enum, 1, &Pin, NULL) == S_OK)
    {
        PIN_INFO Info = { 0 };
        LONG Flags = 0, Mode = 0;
        LONGLONG Rate = 0, Maximum = 0;
        LONGLONG *List = NULL;
        LONG Count = 0, Index;
        SIZE Dimensions = { 640, 480 };
        HRESULT Caps, ModeHr, Actual, Max, Rates;

        IPin_QueryPinInfo(Pin, &Info);
        if (Info.pFilter)
            IBaseFilter_Release(Info.pFilter);

        Caps = IAMVideoControl_GetCaps(Control, Pin, &Flags);
        ModeHr = IAMVideoControl_GetMode(Control, Pin, &Mode);
        Actual = IAMVideoControl_GetCurrentActualFrameRate(Control, Pin, &Rate);
        Max = IAMVideoControl_GetMaxAvailableFrameRate(Control, Pin, 0, Dimensions, &Maximum);
        Rates = IAMVideoControl_GetFrameRateList(Control, Pin, 0, Dimensions, &Count, &List);
        trace("MEASURE pin '%ls' dir %d caps %#lx %#lx mode %#lx %#lx actual %#lx %I64d max %#lx %I64d list %#lx %ld\n",
              Info.achName, Info.dir, Caps, Flags, ModeHr, Mode, Actual, Rate, Max, Maximum, Rates, Count);
        if (!wcscmp(Info.achName, L"Capture"))
        {
            ok(Caps == S_OK && Flags == 0 && ModeHr == S_OK && Mode == 0, "Capture caps %#lx %#lx mode %#lx %#lx\n", Caps, Flags, ModeHr, Mode);
            ok(Actual == S_OK && Max == S_OK && Rates == S_OK, "Capture rates %#lx %#lx %#lx\n", Actual, Max, Rates);
            ok(Count >= 1, "Capture rate count %ld\n", Count);
        }
        else if (!wcscmp(Info.achName, L"Still"))
        {
            ok(Caps == S_OK && Flags == 0xc && ModeHr == S_OK && Mode == 0xc, "Still caps %#lx %#lx mode %#lx %#lx\n", Caps, Flags, ModeHr, Mode);
            ok(Actual == S_OK && Rate == 0 && Max == S_OK && Maximum == 0 && Rates == S_OK && Count == 0, "Still rates %#lx %#lx %#lx %ld\n", Actual, Max, Rates, Count);
        }
        else
        {
            ok(!wcscmp(Info.achName, L"Video Camera Terminal"), "Pin '%ls'\n", Info.achName);
            ok(Info.dir == PINDIR_INPUT, "Terminal direction %d\n", Info.dir);
            ok(Caps == S_OK && Flags == 0 && ModeHr == S_OK && Mode == 0, "Terminal caps %#lx %#lx mode %#lx %#lx\n", Caps, Flags, ModeHr, Mode);
            ok(Actual == E_INVALIDARG && Max == E_INVALIDARG && Rates == E_INVALIDARG, "Terminal rates %#lx %#lx %#lx\n", Actual, Max, Rates);
        }
        if (SUCCEEDED(Rates) && List)
        {
            for (Index = 0; Index < Count; Index++)
                trace("MEASURE pin '%ls' rate %ld = %I64d\n", Info.achName, Index, List[Index]);
            CoTaskMemFree(List);
        }
        IPin_Release(Pin);
    }
    IEnumPins_Release(Enum);
}

static void
DumpBridgePin(IPin *Pin)
{
    IEnumMediaTypes *Types = NULL;
    AM_MEDIA_TYPE *Type = NULL;
    ULONG Count = 0;
    HRESULT hr;

    hr = IPin_EnumMediaTypes(Pin, &Types);
    trace("MEASURE bridge EnumMediaTypes %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        while (IEnumMediaTypes_Next(Types, 1, &Type, NULL) == S_OK)
        {
            Count++;
            if (Type->pbFormat)
                CoTaskMemFree(Type->pbFormat);
            CoTaskMemFree(Type);
        }
        IEnumMediaTypes_Release(Types);
    }
    trace("MEASURE bridge types %lu\n", Count);
}

static void
CaptureStill(IBaseFilter *Source, IAMVideoControl *Control, IPin *Capture, IPin *Still)
{
    IGraphBuilder *Graph = NULL;
    IBaseFilter *Grabber = NULL, *Null = NULL, *CaptureNull = NULL;
    ISampleGrabber *SampleGrabber = NULL;
    IMediaControl *Media = NULL;
    IPin *In = NULL, *GrabOut = NULL, *NullIn = NULL, *CaptureIn = NULL;
    BYTE *Buffer = HeapAlloc(GetProcessHeap(), 0, 2 * 1024 * 1024);
    AM_MEDIA_TYPE Current;
    ULONG Tries;
    HRESULT hr;

    CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IGraphBuilder, (void **)&Graph);
    CoCreateInstance(&UvcClsidSampleGrabber, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&Grabber);
    CoCreateInstance(&UvcClsidNullRenderer, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&Null);
    CoCreateInstance(&UvcClsidNullRenderer, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&CaptureNull);
    ok(Graph && Grabber && Null && CaptureNull, "Creating the graph failed\n");
    if (!Graph || !Grabber || !Null || !CaptureNull)
        goto done;

    IGraphBuilder_AddFilter(Graph, Source, L"Source");
    IGraphBuilder_AddFilter(Graph, Grabber, L"Grabber");
    IGraphBuilder_AddFilter(Graph, Null, L"Null");
    IGraphBuilder_AddFilter(Graph, CaptureNull, L"CaptureNull");
    IBaseFilter_QueryInterface(Grabber, &IID_ISampleGrabber, (void **)&SampleGrabber);
    ISampleGrabber_SetBufferSamples(SampleGrabber, TRUE);

    In = FindPinByDirection(Grabber, PINDIR_INPUT);
    GrabOut = FindPinByDirection(Grabber, PINDIR_OUTPUT);
    NullIn = FindPinByDirection(Null, PINDIR_INPUT);
    CaptureIn = FindPinByDirection(CaptureNull, PINDIR_INPUT);

    hr = IGraphBuilder_ConnectDirect(Graph, Capture, CaptureIn, NULL);
    ok(hr == S_OK, "Connect capture returned %#lx\n", hr);
    hr = IGraphBuilder_ConnectDirect(Graph, Still, In, NULL);
    ok(hr == S_OK, "Connect still returned %#lx\n", hr);
    hr = IGraphBuilder_ConnectDirect(Graph, GrabOut, NullIn, NULL);
    ok(hr == S_OK, "Connect null returned %#lx\n", hr);

    ZeroMemory(&Current, sizeof(Current));
    hr = IPin_ConnectionMediaType(Still, &Current);
    trace("MEASURE still connection %#lx sub %s format %s sample %lu\n", hr, wine_dbgstr_guid(&Current.subtype), wine_dbgstr_guid(&Current.formattype), Current.lSampleSize);
    if (Current.pbFormat)
        CoTaskMemFree(Current.pbFormat);

    IGraphBuilder_QueryInterface(Graph, &IID_IMediaControl, (void **)&Media);
    hr = IMediaControl_Run(Media);
    ok(SUCCEEDED(hr), "Run returned %#lx\n", hr);
    Sleep(500);

    hr = IAMVideoControl_SetMode(Control, Still, UVC_VIDEOCONTROL_TRIGGER);
    ok(hr == S_OK, "SetMode trigger returned %#lx\n", hr);

    for (Tries = 0; Tries < 40; Tries++)
    {
        long Length = 2 * 1024 * 1024;

        Sleep(50);
        hr = ISampleGrabber_GetCurrentBuffer(SampleGrabber, &Length, (long *)Buffer);
        if (SUCCEEDED(hr) && Length)
        {
            trace("MEASURE still sample length %ld data %02x %02x %02x %02x after %lu\n", Length, Buffer[0], Buffer[1], Buffer[2], Buffer[3], Tries);
            ok(Length == 614400 || Length == 921600 || Length == 153600 || Length == 230400, "Still length %ld\n", Length);
            break;
        }
    }
    ok(Tries < 40, "No still sample\n");
    hr = IMediaControl_Stop(Media);
    ok(SUCCEEDED(hr), "Stop returned %#lx\n", hr);

done:
    HeapFree(GetProcessHeap(), 0, Buffer);
    if (Media)
        IMediaControl_Release(Media);
    if (SampleGrabber)
        ISampleGrabber_Release(SampleGrabber);
    if (CaptureIn)
        IPin_Release(CaptureIn);
    if (NullIn)
        IPin_Release(NullIn);
    if (GrabOut)
        IPin_Release(GrabOut);
    if (In)
        IPin_Release(In);
    if (CaptureNull)
        IBaseFilter_Release(CaptureNull);
    if (Null)
        IBaseFilter_Release(Null);
    if (Grabber)
        IBaseFilter_Release(Grabber);
    if (Graph)
        IGraphBuilder_Release(Graph);
}

START_TEST(DsStill)
{
    IBaseFilter *Filter;
    IAMVideoControl *Control = NULL;
    IPin *Capture, *Still, *Bridge;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx returned %#lx\n", hr);

    Filter = BindCamera();
    if (!Filter)
    {
        skip("No usb-uvc test camera\n");
        CoUninitialize();
        return;
    }

    hr = IBaseFilter_QueryInterface(Filter, &IID_IAMVideoControl, (void **)&Control);
    ok(hr == S_OK, "IAMVideoControl returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        DumpVideoControl(Filter, Control);

        Bridge = FindPinByDirection(Filter, PINDIR_INPUT);
        trace("MEASURE bridge pin %p\n", Bridge);
        if (Bridge)
        {
            DumpBridgePin(Bridge);
            IPin_Release(Bridge);
        }

        Capture = FindPinByName(Filter, L"Capture");
        Still = FindPinByName(Filter, L"Still");
        ok(Capture != NULL, "No capture pin\n");
        trace("MEASURE still pin %p\n", Still);
        if (Capture && Still)
            CaptureStill(Filter, Control, Capture, Still);
        if (Still)
            IPin_Release(Still);
        if (Capture)
            IPin_Release(Capture);
        IAMVideoControl_Release(Control);
    }

    IBaseFilter_Release(Filter);
    CoUninitialize();
}
