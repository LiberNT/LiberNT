/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exclusive-mode event-driven render stream tests
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */
#define COBJMACROS
#include <apitest.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <math.h>

static const DWORD rates[] = { 48000, 44100 };
static UINT64 tone_frame;

static void init_format(WAVEFORMATEX *format, DWORD rate)
{
    ZeroMemory(format, sizeof(*format));
    format->wFormatTag = WAVE_FORMAT_PCM;
    format->nChannels = 2;
    format->nSamplesPerSec = rate;
    format->wBitsPerSample = 16;
    format->nBlockAlign = format->nChannels * format->wBitsPerSample / 8;
    format->nAvgBytesPerSec = rate * format->nBlockAlign;
}

static IAudioClient *activate_client(IMMDevice *device)
{
    IAudioClient *client = NULL;
    HRESULT hr;

    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    return SUCCEEDED(hr) ? client : NULL;
}

static IAudioClient *initialize_aligned(IMMDevice *device, const WAVEFORMATEX *format,
                                        REFERENCE_TIME period, UINT32 *frames)
{
    IAudioClient *client;
    REFERENCE_TIME aligned;
    HRESULT hr;

    client = activate_client(device);
    if (!client) return NULL;

    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 period, period, format, NULL);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
    {
        hr = IAudioClient_GetBufferSize(client, frames);
        ok(hr == S_OK && *frames != 0, "period %I64d: aligned size %#lx, %u\n", period, hr, *frames);
        IAudioClient_Release(client);
        if (FAILED(hr) || !*frames) return NULL;

        aligned = (REFERENCE_TIME)((10000000.0 * *frames / format->nSamplesPerSec) + 0.5);
        trace("period %I64d realigned to %u frames, %I64d\n", period, *frames, aligned);
        client = activate_client(device);
        if (!client) return NULL;
        hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                     aligned, aligned, format, NULL);
        ok(hr == S_OK, "period %I64d: Initialize after realignment %#lx\n", aligned, hr);
    }
    else
    {
        ok(hr == S_OK, "period %I64d: Initialize %#lx\n", period, hr);
    }
    if (FAILED(hr))
    {
        IAudioClient_Release(client);
        return NULL;
    }

    hr = IAudioClient_GetBufferSize(client, frames);
    ok(hr == S_OK && *frames != 0, "period %I64d: GetBufferSize %#lx, %u\n", period, hr, *frames);
    if (FAILED(hr) || !*frames)
    {
        IAudioClient_Release(client);
        return NULL;
    }
    return client;
}

static BOOL write_tone(IAudioRenderClient *render, UINT32 frames, const WAVEFORMATEX *format,
                       const char *when)
{
    BYTE *data;
    UINT32 i;
    HRESULT hr;

    hr = IAudioRenderClient_GetBuffer(render, frames, &data);
    ok(hr == S_OK, "%s: GetBuffer(%u) %#lx\n", when, frames, hr);
    if (FAILED(hr)) return FALSE;
    for (i = 0; i < frames; ++i, ++tone_frame)
    {
        SHORT sample = (SHORT)(8000.0 * sin(2.0 * 3.14159265358979 * 1000.0 * tone_frame / format->nSamplesPerSec));
        ((SHORT *)data)[2 * i] = sample;
        ((SHORT *)data)[2 * i + 1] = sample;
    }
    hr = IAudioRenderClient_ReleaseBuffer(render, frames, 0);
    ok(hr == S_OK, "%s: ReleaseBuffer(%u) %#lx\n", when, frames, hr);
    return SUCCEEDED(hr);
}

static void test_stream(IMMDevice *device, const WAVEFORMATEX *format, REFERENCE_TIME period)
{
    IAudioClient *client;
    IAudioRenderClient *render = NULL;
    IAudioClock *clock = NULL;
    HANDLE event = NULL;
    UINT64 position = 0, frequency = 0;
    UINT32 frames;
    DWORD start, wait;
    unsigned events = 0, pause, i;
    HRESULT hr;

    trace("exclusive %lu Hz, period %I64d\n", format->nSamplesPerSec, period);
    client = initialize_aligned(device, format, period, &frames);
    if (!client) return;
    trace("buffer %u frames\n", frames);

    hr = IAudioClient_GetService(client, &IID_IAudioRenderClient, (void **)&render);
    ok(hr == S_OK, "Render service %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IAudioClient_GetService(client, &IID_IAudioClock, (void **)&clock);
    ok(hr == S_OK, "Clock service %#lx\n", hr);
    if (FAILED(hr)) goto done;

    hr = IAudioClient_Start(client);
    ok(hr == AUDCLNT_E_EVENTHANDLE_NOT_SET, "Start without event %#lx\n", hr);
    if (SUCCEEDED(hr)) IAudioClient_Stop(client);

    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(event != NULL, "CreateEvent failed %lu\n", GetLastError());
    if (!event) goto done;
    hr = IAudioClient_SetEventHandle(client, event);
    ok(hr == S_OK, "SetEventHandle %#lx\n", hr);
    if (FAILED(hr)) goto done;

    if (!write_tone(render, frames, format, "prefill")) goto done;
    hr = IAudioClient_Start(client);
    ok(hr == S_OK, "Start %#lx\n", hr);
    if (FAILED(hr)) goto done;

    start = GetTickCount();
    while (GetTickCount() - start < 1000)
    {
        wait = WaitForSingleObject(event, 2000);
        ok(wait == WAIT_OBJECT_0, "event %u: wait %lu\n", events, wait);
        if (wait != WAIT_OBJECT_0) break;
        ++events;
        if (!write_tone(render, frames, format, "refill")) break;
    }
    trace("%u events in %lu ms\n", events, GetTickCount() - start);
    ok(events >= 2, "only %u events\n", events);

    for (pause = 0; pause < 16; ++pause)
    {
        hr = IAudioClient_Stop(client);
        ok(hr == S_OK, "pause %u: Stop %#lx\n", pause, hr);
        Sleep(pause % 4);
        hr = IAudioClient_Start(client);
        ok(hr == S_OK, "pause %u: Start %#lx\n", pause, hr);
        if (FAILED(hr)) break;
        for (i = 0; i < 4; ++i)
        {
            wait = WaitForSingleObject(event, 2000);
            ok(wait == WAIT_OBJECT_0, "pause %u event %u: wait %lu\n", pause, i, wait);
            if (wait != WAIT_OBJECT_0 || !write_tone(render, frames, format, "resume")) break;
        }
        if (i < 4) break;
    }

    for (i = 0; i < 8; ++i)
    {
        wait = WaitForSingleObject(event, 2000);
        ok(wait == WAIT_OBJECT_0, "late %u: wait %lu\n", i, wait);
        if (wait != WAIT_OBJECT_0) break;
        Sleep(3 * frames * 1000 / format->nSamplesPerSec + 1);
        if (!write_tone(render, frames, format, "late")) break;
        wait = WaitForSingleObject(event, 2000);
        ok(wait == WAIT_OBJECT_0, "after late %u: wait %lu\n", i, wait);
        if (wait != WAIT_OBJECT_0 || !write_tone(render, frames, format, "after late")) break;
    }

    hr = IAudioClock_GetFrequency(clock, &frequency);
    ok(hr == S_OK && frequency != 0, "GetFrequency %#lx, %I64u\n", hr, frequency);
    hr = IAudioClock_GetPosition(clock, &position, NULL);
    ok(hr == S_OK, "GetPosition %#lx\n", hr);
    ok(position != 0, "audio clock did not advance\n");

    hr = IAudioClient_Stop(client);
    ok(hr == S_OK, "Stop %#lx\n", hr);

done:
    if (clock) IAudioClock_Release(clock);
    if (render) IAudioRenderClient_Release(render);
    IAudioClient_Release(client);
    if (event) CloseHandle(event);
}

static void test_unequal_duration(IMMDevice *device, const WAVEFORMATEX *format, REFERENCE_TIME period)
{
    IAudioClient *client;
    HRESULT hr;

    client = activate_client(device);
    if (!client) return;
    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 period * 2, period, format, NULL);
    ok(hr == AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL, "Initialize with unequal duration %#lx\n", hr);
    IAudioClient_Release(client);
}

START_TEST(exclusivestream)
{
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *client = NULL;
    REFERENCE_TIME default_period = 0, minimum_period = 0;
    WAVEFORMATEX format;
    HRESULT hr;
    unsigned i;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx %#lx\n", hr);
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&enumerator);
    ok(hr == S_OK, "MMDeviceEnumerator %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);
    if (hr == E_NOTFOUND)
    {
        skip("No render endpoint\n");
        goto done;
    }
    ok(hr == S_OK, "GetDefaultAudioEndpoint %#lx\n", hr);
    if (FAILED(hr)) goto done;

    client = activate_client(device);
    if (!client) goto done;
    hr = IAudioClient_GetDevicePeriod(client, &default_period, &minimum_period);
    ok(hr == S_OK && default_period != 0 && minimum_period != 0 && minimum_period <= default_period,
       "GetDevicePeriod %#lx, %I64d, %I64d\n", hr, default_period, minimum_period);
    trace("device period default %I64d, minimum %I64d\n", default_period, minimum_period);
    for (i = 0; i < ARRAY_SIZE(rates); ++i)
    {
        init_format(&format, rates[i]);
        hr = IAudioClient_IsFormatSupported(client, AUDCLNT_SHAREMODE_EXCLUSIVE, &format, NULL);
        if (hr == S_OK) break;
        trace("%lu Hz not supported in exclusive mode: %#lx\n", rates[i], hr);
    }
    IAudioClient_Release(client);
    if (!default_period || !minimum_period) goto done;
    if (hr != S_OK)
    {
        skip("No 16-bit stereo exclusive format\n");
        goto done;
    }

    test_unequal_duration(device, &format, default_period);
    test_stream(device, &format, default_period);
    test_stream(device, &format, minimum_period);

done:
    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    CoUninitialize();
}
