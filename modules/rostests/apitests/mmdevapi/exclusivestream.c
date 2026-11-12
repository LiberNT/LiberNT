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
        ok(hr == S_OK || hr == AUDCLNT_E_DEVICE_IN_USE, "period %I64d: Initialize after realignment %#lx\n", aligned, hr);
    }
    else
    {
        ok(hr == S_OK || hr == AUDCLNT_E_DEVICE_IN_USE, "period %I64d: Initialize %#lx\n", period, hr);
    }
    if (hr == AUDCLNT_E_DEVICE_IN_USE)
        skip("period %I64d: device in use, no exclusive event access\n", period);
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
    UINT32 frames = 0;
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

#define CHILD_TIMEOUT_MILLISECONDS 30000

struct held_stream
{
    IAudioClient *client;
    IAudioRenderClient *render;
    IAudioClock *clock;
    HANDLE event;
    UINT32 frames;
    const WAVEFORMATEX *format;
};

static HRESULT initialize_shared(IMMDevice *device, DWORD flags, REFERENCE_TIME duration)
{
    IAudioClient *client;
    WAVEFORMATEX *mix = NULL;
    HRESULT hr;

    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    if (FAILED(hr)) return hr;
    hr = IAudioClient_GetMixFormat(client, &mix);
    if (SUCCEEDED(hr))
        hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, mix, NULL);
    CoTaskMemFree(mix);
    IAudioClient_Release(client);
    return hr;
}

static HRESULT initialize_exclusive(IMMDevice *device, const WAVEFORMATEX *format, REFERENCE_TIME period)
{
    IAudioClient *client;
    HRESULT hr;

    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    if (FAILED(hr)) return hr;
    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 period, period, format, NULL);
    IAudioClient_Release(client);
    return hr;
}

static unsigned pump_held(struct held_stream *held, unsigned count, HANDLE process, const char *when)
{
    HANDLE waits[2] = { held->event, process };
    unsigned events = 0;
    DWORD wait;

    while (process || events < count)
    {
        wait = WaitForMultipleObjects(process ? 2 : 1, waits, FALSE, 2000);
        if (wait == WAIT_OBJECT_0 + 1)
            break;
        ok(wait == WAIT_OBJECT_0, "%s: event %u: wait %lu\n", when, events, wait);
        if (wait != WAIT_OBJECT_0 || !write_tone(held->render, held->frames, held->format, when))
            break;
        ++events;
    }
    return events;
}

static void check_held_advances(struct held_stream *held, const char *when)
{
    UINT64 before = 0, after = 0;
    HRESULT hr;

    hr = IAudioClock_GetPosition(held->clock, &before, NULL);
    ok(hr == S_OK, "%s: GetPosition %#lx\n", when, hr);
    ok(pump_held(held, 10, NULL, when) == 10, "%s: exclusive stream stopped getting events\n", when);
    hr = IAudioClock_GetPosition(held->clock, &after, NULL);
    ok(hr == S_OK, "%s: GetPosition %#lx\n", when, hr);
    ok(after > before, "%s: exclusive position %I64u did not advance from %I64u\n", when, after, before);
}

static DWORD run_child(struct held_stream *held, const char *mode, DWORD rate, REFERENCE_TIME period)
{
    WCHAR path[MAX_PATH], command[MAX_PATH * 2];
    PROCESS_INFORMATION process = { 0 };
    STARTUPINFOW startup = { sizeof(startup) };
    DWORD code = 0xdead;

    GetModuleFileNameW(NULL, path, MAX_PATH);
    swprintf(command, ARRAYSIZE(command), L"\"%s\" exclusivestream child %S %lu %I64d", path, mode, rate, period);
    ok(CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process),
       "CreateProcess: %lu\n", GetLastError());
    if (!process.hProcess) return code;
    if (held)
        pump_held(held, 0, process.hProcess, mode);
    ok(WaitForSingleObject(process.hProcess, CHILD_TIMEOUT_MILLISECONDS) == WAIT_OBJECT_0,
       "%s child did not finish\n", mode);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return code;
}

static DWORD child_main(const char *mode, DWORD rate, REFERENCE_TIME period)
{
    IMMDeviceEnumerator *enumerator;
    IMMDevice *device;
    WAVEFORMATEX format;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&enumerator);
    if (FAILED(hr)) return hr;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);
    IMMDeviceEnumerator_Release(enumerator);
    if (FAILED(hr)) return hr;
    init_format(&format, rate);
    if (!strcmp(mode, "shared"))
        hr = initialize_shared(device, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_NOPERSIST |
                                       AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0);
    else
        hr = initialize_exclusive(device, &format, period);
    IMMDevice_Release(device);
    return hr;
}

static void test_held_endpoint(IMMDevice *device, const WAVEFORMATEX *format, REFERENCE_TIME period)
{
    struct held_stream held = { NULL, NULL, NULL, NULL, 0, format };
    REFERENCE_TIME aligned;
    DWORD code;
    HRESULT hr;

    held.client = initialize_aligned(device, format, period, &held.frames);
    if (!held.client) return;
    aligned = (REFERENCE_TIME)((10000000.0 * held.frames / format->nSamplesPerSec) + 0.5);
    hr = IAudioClient_GetService(held.client, &IID_IAudioRenderClient, (void **)&held.render);
    ok(hr == S_OK, "Render service %#lx\n", hr);
    hr = IAudioClient_GetService(held.client, &IID_IAudioClock, (void **)&held.clock);
    ok(hr == S_OK, "Clock service %#lx\n", hr);
    held.event = CreateEventW(NULL, FALSE, FALSE, NULL);
    hr = IAudioClient_SetEventHandle(held.client, held.event);
    ok(hr == S_OK, "SetEventHandle %#lx\n", hr);
    if (!held.render || !held.clock || FAILED(hr) || !write_tone(held.render, held.frames, format, "held prefill"))
        goto done;
    hr = IAudioClient_Start(held.client);
    ok(hr == S_OK, "Start %#lx\n", hr);
    if (FAILED(hr)) goto done;
    check_held_advances(&held, "started");

    hr = initialize_shared(device, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_NOPERSIST |
                                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0);
    ok(hr == AUDCLNT_E_DEVICE_IN_USE, "shared event Initialize beside exclusive %#lx\n", hr);
    check_held_advances(&held, "after shared event");
    hr = initialize_shared(device, 0, 5000000);
    ok(hr == AUDCLNT_E_DEVICE_IN_USE, "shared Initialize beside exclusive %#lx\n", hr);
    check_held_advances(&held, "after shared");
    hr = initialize_exclusive(device, format, aligned);
    ok(hr == AUDCLNT_E_DEVICE_IN_USE, "second exclusive Initialize %#lx\n", hr);
    check_held_advances(&held, "after second exclusive");

    code = run_child(&held, "shared", format->nSamplesPerSec, aligned);
    ok(code == AUDCLNT_E_DEVICE_IN_USE, "shared Initialize in another process %#lx\n", code);
    check_held_advances(&held, "after other process shared");
    code = run_child(&held, "exclusive", format->nSamplesPerSec, aligned);
    ok(code == AUDCLNT_E_DEVICE_IN_USE, "exclusive Initialize in another process %#lx\n", code);
    check_held_advances(&held, "after other process exclusive");

    hr = IAudioClient_Stop(held.client);
    ok(hr == S_OK, "Stop %#lx\n", hr);
    hr = IAudioClient_Reset(held.client);
    ok(hr == S_OK, "Reset %#lx\n", hr);
    if (!write_tone(held.render, held.frames, format, "restart prefill")) goto done;
    hr = IAudioClient_Start(held.client);
    ok(hr == S_OK, "Start after the refused opens %#lx\n", hr);
    if (SUCCEEDED(hr))
        check_held_advances(&held, "restarted");
    hr = IAudioClient_Stop(held.client);
    ok(hr == S_OK, "Stop %#lx\n", hr);

done:
    if (held.clock) IAudioClock_Release(held.clock);
    if (held.render) IAudioRenderClient_Release(held.render);
    IAudioClient_Release(held.client);
    if (held.event) CloseHandle(held.event);

    hr = initialize_shared(device, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_NOPERSIST |
                                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0);
    ok(hr == S_OK, "shared Initialize after exclusive %#lx\n", hr);
    code = run_child(NULL, "shared", format->nSamplesPerSec, aligned);
    ok(code == S_OK, "shared Initialize in another process after exclusive %#lx\n", code);
    code = run_child(NULL, "exclusive", format->nSamplesPerSec, aligned);
    ok(code == S_OK, "exclusive Initialize in another process after exclusive %#lx\n", code);
}

static void test_exclusive_beside_shared(IMMDevice *device, const WAVEFORMATEX *format, REFERENCE_TIME period)
{
    IAudioClient *shared = NULL;
    IAudioRenderClient *render = NULL;
    WAVEFORMATEX *mix = NULL;
    UINT32 frames = 0, padding;
    BYTE *data;
    HRESULT hr;

    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&shared);
    ok(hr == S_OK, "Activate %#lx\n", hr);
    if (FAILED(hr)) return;
    hr = IAudioClient_GetMixFormat(shared, &mix);
    ok(hr == S_OK, "GetMixFormat %#lx\n", hr);
    if (SUCCEEDED(hr))
        hr = IAudioClient_Initialize(shared, AUDCLNT_SHAREMODE_SHARED, 0, 5000000, 0, mix, NULL);
    ok(hr == S_OK, "shared Initialize %#lx\n", hr);
    if (SUCCEEDED(hr))
        hr = IAudioClient_GetBufferSize(shared, &frames);
    if (SUCCEEDED(hr))
        hr = IAudioClient_GetService(shared, &IID_IAudioRenderClient, (void **)&render);
    if (SUCCEEDED(hr))
        hr = IAudioRenderClient_GetBuffer(render, frames, &data);
    if (SUCCEEDED(hr))
        hr = IAudioRenderClient_ReleaseBuffer(render, frames, AUDCLNT_BUFFERFLAGS_SILENT);
    if (SUCCEEDED(hr))
        hr = IAudioClient_Start(shared);
    ok(hr == S_OK, "shared stream setup %#lx\n", hr);
    if (FAILED(hr)) goto done;
    Sleep(50);

    hr = initialize_exclusive(device, format, period);
    ok(hr == S_OK, "exclusive Initialize beside a started shared stream %#lx\n", hr);
    hr = IAudioClient_GetCurrentPadding(shared, &padding);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "preempted shared GetCurrentPadding %#lx\n", hr);
    hr = IAudioClient_Stop(shared);
    ok(hr == S_FALSE, "preempted shared Stop %#lx\n", hr);

done:
    if (render) IAudioRenderClient_Release(render);
    IAudioClient_Release(shared);
    CoTaskMemFree(mix);
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
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 6 && !strcmp(argv[2], "child"))
        ExitProcess(child_main(argv[3], strtoul(argv[4], NULL, 10), _strtoi64(argv[5], NULL, 10)));

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
    test_held_endpoint(device, &format, default_period);
    test_exclusive_beside_shared(device, &format, default_period);

done:
    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    CoUninitialize();
}
