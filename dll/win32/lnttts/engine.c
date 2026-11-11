/*
 * PROJECT:     LiberNT Text-to-Speech Engine
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SAPI text-to-speech engine object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lnttts.h"

#define NATIVE_SAMPLE_RATE 16000

typedef struct _ENGINE
{
    ISpTTSEngine ISpTTSEngine_iface;
    ISpObjectWithToken ISpObjectWithToken_iface;
    LONG Ref;
    ISpObjectToken *Token;
} ENGINE;

typedef struct _SPEAK_CONTEXT
{
    ISpTTSEngineSite *Site;
    const SPVTEXTFRAG *Fragment;
    ULONG BytesPerSample;
    ULONG Channels;
    ULONGLONG EventInterest;
    HRESULT Abort;
} SPEAK_CONTEXT;

LONG EngineObjects;

static ENGINE *
EngineFromTts(
    _In_ ISpTTSEngine *Iface)
{
    return CONTAINING_RECORD(Iface, ENGINE, ISpTTSEngine_iface);
}

static ENGINE *
EngineFromToken(
    _In_ ISpObjectWithToken *Iface)
{
    return CONTAINING_RECORD(Iface, ENGINE, ISpObjectWithToken_iface);
}

static HRESULT WINAPI
Engine_QueryInterface(
    _In_ ISpTTSEngine *Iface,
    _In_ REFIID Iid,
    _Outptr_ void **Object)
{
    ENGINE *This = EngineFromTts(Iface);

    if (IsEqualIID(Iid, &IID_IUnknown) || IsEqualIID(Iid, &IID_ISpTTSEngine))
        *Object = &This->ISpTTSEngine_iface;
    else if (IsEqualIID(Iid, &IID_ISpObjectWithToken))
        *Object = &This->ISpObjectWithToken_iface;
    else
    {
        *Object = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*Object);
    return S_OK;
}

static ULONG WINAPI
Engine_AddRef(
    _In_ ISpTTSEngine *Iface)
{
    return InterlockedIncrement(&EngineFromTts(Iface)->Ref);
}

static ULONG WINAPI
Engine_Release(
    _In_ ISpTTSEngine *Iface)
{
    ENGINE *This = EngineFromTts(Iface);
    ULONG Ref = InterlockedDecrement(&This->Ref);

    if (!Ref)
    {
        if (This->Token)
            ISpObjectToken_Release(This->Token);
        CoTaskMemFree(This);
        InterlockedDecrement(&EngineObjects);
    }
    return Ref;
}

static HRESULT
PollActions(
    _Inout_ SPEAK_CONTEXT *Context,
    _Inout_ SYNTH_PARAMS *Params)
{
    DWORD Actions = ISpTTSEngineSite_GetActions(Context->Site);
    USHORT Volume;
    LONG Rate;

    if (Actions & SPVES_ABORT)
        return E_ABORT;
    if ((Actions & SPVES_RATE) && SUCCEEDED(ISpTTSEngineSite_GetRate(Context->Site, &Rate)))
        Params->Rate = Rate + Context->Fragment->State.RateAdj;
    if ((Actions & SPVES_VOLUME) && SUCCEEDED(ISpTTSEngineSite_GetVolume(Context->Site, &Volume)))
        Params->Volume = Volume * Context->Fragment->State.Volume / 100;
    return S_OK;
}

static HRESULT
WriteSamples(
    _In_ PVOID Opaque,
    _In_reads_(Count) const SHORT *Samples,
    _In_ ULONG Count)
{
    SPEAK_CONTEXT *Context = Opaque;
    BYTE Converted[4096];
    ULONG Done = 0, Chunk, Frame, i, c, Written;
    HRESULT hr = S_OK;

    if (ISpTTSEngineSite_GetActions(Context->Site) & SPVES_ABORT)
        return E_ABORT;

    Frame = Context->BytesPerSample * Context->Channels;
    while (Done < Count && SUCCEEDED(hr))
    {
        Chunk = min(Count - Done, (ULONG)(sizeof(Converted) / Frame));
        for (i = 0; i < Chunk; i++)
        {
            for (c = 0; c < Context->Channels; c++)
            {
                if (Context->BytesPerSample == 1)
                    Converted[i * Frame + c] = (BYTE)((Samples[Done + i] >> 8) + 128);
                else
                    memcpy(Converted + i * Frame + c * 2, &Samples[Done + i], 2);
            }
        }
        hr = ISpTTSEngineSite_Write(Context->Site, Converted, Chunk * Frame, &Written);
        Done += Chunk;
    }
    return hr;
}

static HRESULT
ReportWord(
    _In_ PVOID Opaque,
    _In_ ULONG TextOffset,
    _In_ ULONG TextLength,
    _In_ ULONGLONG SampleOffset)
{
    SPEAK_CONTEXT *Context = Opaque;
    SPEVENT Event;

    if (!(Context->EventInterest & (1ULL << SPEI_WORD_BOUNDARY)))
        return S_OK;

    memset(&Event, 0, sizeof(Event));
    Event.eEventId = SPEI_WORD_BOUNDARY;
    Event.ullAudioStreamOffset = SampleOffset * Context->BytesPerSample * Context->Channels;
    Event.lParam = Context->Fragment->ulTextSrcOffset + TextOffset;
    Event.wParam = TextLength;
    return ISpTTSEngineSite_AddEvents(Context->Site, &Event, 1);
}

static BOOL
IsUsableFormat(
    _In_opt_ const GUID *FormatId,
    _In_opt_ const WAVEFORMATEX *Format)
{
    return FormatId && Format && IsEqualGUID(FormatId, &SPDFID_WaveFormatEx) &&
           Format->wFormatTag == WAVE_FORMAT_PCM &&
           (Format->nChannels == 1 || Format->nChannels == 2) &&
           (Format->wBitsPerSample == 8 || Format->wBitsPerSample == 16) &&
           Format->nSamplesPerSec >= 8000 && Format->nSamplesPerSec <= 48000;
}

static HRESULT WINAPI
Engine_Speak(
    _In_ ISpTTSEngine *Iface,
    _In_ DWORD Flags,
    _In_ REFGUID FormatId,
    _In_ const WAVEFORMATEX *Format,
    _In_ const SPVTEXTFRAG *Fragments,
    _In_ ISpTTSEngineSite *Site)
{
    SYNTH_OUTPUT Output = { WriteSamples, ReportWord, NULL, 0 };
    SPEAK_CONTEXT Context;
    SYNTH_PARAMS Params;
    const SPVTEXTFRAG *Fragment;
    USHORT Volume = 100;
    LONG Rate = 0;
    HRESULT hr = S_OK;

    UNREFERENCED_PARAMETER(Iface);
    UNREFERENCED_PARAMETER(Flags);

    if (!Site || !IsUsableFormat(FormatId, Format))
        return E_INVALIDARG;

    memset(&Context, 0, sizeof(Context));
    Context.Site = Site;
    Context.BytesPerSample = Format->wBitsPerSample / 8;
    Context.Channels = Format->nChannels;
    if (FAILED(ISpTTSEngineSite_GetEventInterest(Site, &Context.EventInterest)))
        Context.EventInterest = 0;
    ISpTTSEngineSite_GetRate(Site, &Rate);
    ISpTTSEngineSite_GetVolume(Site, &Volume);
    Output.Context = &Context;

    for (Fragment = Fragments; Fragment && SUCCEEDED(hr); Fragment = Fragment->pNext)
    {
        Context.Fragment = Fragment;
        Params.SampleRate = Format->nSamplesPerSec;
        Params.Rate = Rate + Fragment->State.RateAdj;
        Params.Volume = Volume * Fragment->State.Volume / 100;
        Params.Pitch = Fragment->State.PitchAdj.MiddleAdj;

        hr = PollActions(&Context, &Params);
        if (FAILED(hr))
            break;

        switch (Fragment->State.eAction)
        {
            case SPVA_Speak:
            case SPVA_Pronounce:
                hr = SynthSpeakText(Fragment->pTextStart, Fragment->ulTextLen, FALSE, &Params, &Output);
                break;
            case SPVA_SpellOut:
                hr = SynthSpeakText(Fragment->pTextStart, Fragment->ulTextLen, TRUE, &Params, &Output);
                break;
            case SPVA_Silence:
                hr = SynthSilence(Fragment->State.SilenceMSecs, &Params, &Output);
                break;
            default:
                break;
        }
    }

    return hr == E_ABORT ? S_OK : hr;
}

static HRESULT WINAPI
Engine_GetOutputFormat(
    _In_ ISpTTSEngine *Iface,
    _In_opt_ const GUID *TargetFormatId,
    _In_opt_ const WAVEFORMATEX *TargetFormat,
    _Out_ GUID *FormatId,
    _Out_ WAVEFORMATEX **Format)
{
    WAVEFORMATEX *Chosen;

    UNREFERENCED_PARAMETER(Iface);

    if (!FormatId || !Format)
        return E_POINTER;

    Chosen = CoTaskMemAlloc(sizeof(*Chosen));
    if (!Chosen)
        return E_OUTOFMEMORY;

    if (IsUsableFormat(TargetFormatId, TargetFormat))
    {
        *Chosen = *TargetFormat;
        Chosen->cbSize = 0;
    }
    else
    {
        Chosen->wFormatTag = WAVE_FORMAT_PCM;
        Chosen->nChannels = 1;
        Chosen->nSamplesPerSec = NATIVE_SAMPLE_RATE;
        Chosen->wBitsPerSample = 16;
        Chosen->nBlockAlign = 2;
        Chosen->nAvgBytesPerSec = NATIVE_SAMPLE_RATE * 2;
        Chosen->cbSize = 0;
    }

    *FormatId = SPDFID_WaveFormatEx;
    *Format = Chosen;
    return S_OK;
}

static const ISpTTSEngineVtbl EngineVtbl =
{
    Engine_QueryInterface,
    Engine_AddRef,
    Engine_Release,
    Engine_Speak,
    Engine_GetOutputFormat,
};

static HRESULT WINAPI
Token_QueryInterface(
    _In_ ISpObjectWithToken *Iface,
    _In_ REFIID Iid,
    _Outptr_ void **Object)
{
    return Engine_QueryInterface(&EngineFromToken(Iface)->ISpTTSEngine_iface, Iid, Object);
}

static ULONG WINAPI
Token_AddRef(
    _In_ ISpObjectWithToken *Iface)
{
    return Engine_AddRef(&EngineFromToken(Iface)->ISpTTSEngine_iface);
}

static ULONG WINAPI
Token_Release(
    _In_ ISpObjectWithToken *Iface)
{
    return Engine_Release(&EngineFromToken(Iface)->ISpTTSEngine_iface);
}

static HRESULT WINAPI
Token_SetObjectToken(
    _In_ ISpObjectWithToken *Iface,
    _In_ ISpObjectToken *Token)
{
    ENGINE *This = EngineFromToken(Iface);

    if (!Token)
        return E_INVALIDARG;
    if (This->Token)
        return SPERR_ALREADY_INITIALIZED;

    ISpObjectToken_AddRef(Token);
    This->Token = Token;
    return S_OK;
}

static HRESULT WINAPI
Token_GetObjectToken(
    _In_ ISpObjectWithToken *Iface,
    _Outptr_result_maybenull_ ISpObjectToken **Token)
{
    ENGINE *This = EngineFromToken(Iface);

    if (!Token)
        return E_POINTER;

    *Token = This->Token;
    if (!*Token)
        return S_FALSE;
    ISpObjectToken_AddRef(*Token);
    return S_OK;
}

static const ISpObjectWithTokenVtbl TokenVtbl =
{
    Token_QueryInterface,
    Token_AddRef,
    Token_Release,
    Token_SetObjectToken,
    Token_GetObjectToken,
};

HRESULT
EngineCreate(
    _In_ REFIID Iid,
    _Outptr_ void **Object)
{
    ENGINE *This;
    HRESULT hr;

    This = CoTaskMemAlloc(sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;

    This->ISpTTSEngine_iface.lpVtbl = &EngineVtbl;
    This->ISpObjectWithToken_iface.lpVtbl = &TokenVtbl;
    This->Ref = 1;
    This->Token = NULL;
    InterlockedIncrement(&EngineObjects);

    hr = ISpTTSEngine_QueryInterface(&This->ISpTTSEngine_iface, Iid, Object);
    ISpTTSEngine_Release(&This->ISpTTSEngine_iface);
    return hr;
}
