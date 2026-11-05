/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared capture format, packets, reset and concurrent clients
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdio.h>

#define BUFFER_DURATION 5000000
#define WAIT_MILLISECONDS 2000
#define CHILD_TIMEOUT_MILLISECONDS 30000

struct capture_client
{
    IAudioClient *client;
    IAudioCaptureClient *capture;
    HANDLE event;
    UINT32 period_frames;
    UINT32 buffer_frames;
    UINT64 next_position;
    BOOL converted;
};

static IMMDevice *capture_device;

static BOOL open_capture_device(void)
{
    IMMDeviceEnumerator *enumerator;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&enumerator);
    if (FAILED(hr))
        return FALSE;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eCapture, eConsole, &capture_device);
    IMMDeviceEnumerator_Release(enumerator);
    return SUCCEEDED(hr);
}

static BOOL open_client(struct capture_client *client, const WAVEFORMATEX *format, DWORD flags)
{
    REFERENCE_TIME period = 0;
    HRESULT hr;

    ZeroMemory(client, sizeof(*client));
    hr = IMMDevice_Activate(capture_device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client->client);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    if (FAILED(hr))
        return FALSE;
    hr = IAudioClient_Initialize(client->client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | flags,
                                 BUFFER_DURATION, 0, format, NULL);
    ok(hr == S_OK, "Initialize: %#lx\n", hr);
    if (FAILED(hr))
        return FALSE;
    client->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    hr = IAudioClient_SetEventHandle(client->client, client->event);
    ok(hr == S_OK, "SetEventHandle: %#lx\n", hr);
    hr = IAudioClient_GetService(client->client, &IID_IAudioCaptureClient, (void **)&client->capture);
    ok(hr == S_OK, "GetService: %#lx\n", hr);
    if (FAILED(hr))
        return FALSE;
    hr = IAudioClient_GetDevicePeriod(client->client, &period, NULL);
    ok(hr == S_OK, "GetDevicePeriod: %#lx\n", hr);
    client->period_frames = MulDiv((int)period, format->nSamplesPerSec, 10000000);
    hr = IAudioClient_GetBufferSize(client->client, &client->buffer_frames);
    ok(hr == S_OK, "GetBufferSize: %#lx\n", hr);
    client->next_position = ~(UINT64)0;
    return TRUE;
}

static void close_client(struct capture_client *client)
{
    if (client->capture)
        IAudioCaptureClient_Release(client->capture);
    if (client->client)
        IAudioClient_Release(client->client);
    if (client->event)
        CloseHandle(client->event);
    ZeroMemory(client, sizeof(*client));
}

static BOOL next_packet(struct capture_client *client, BYTE **data, UINT32 *frames, DWORD *flags, UINT64 *position)
{
    ULONGLONG deadline = GetTickCount64() + WAIT_MILLISECONDS;
    UINT32 next;
    HRESULT hr;

    for (;;)
    {
        hr = IAudioCaptureClient_GetNextPacketSize(client->capture, &next);
        if (FAILED(hr))
            return FALSE;
        if (next)
            break;
        if (GetTickCount64() >= deadline)
            return FALSE;
        WaitForSingleObject(client->event, WAIT_MILLISECONDS);
    }
    hr = IAudioCaptureClient_GetBuffer(client->capture, data, frames, flags, position, NULL);
    return hr == S_OK;
}

static BOOL read_packet(struct capture_client *client, DWORD *flags_out)
{
    UINT64 position;
    UINT32 frames;
    DWORD flags;
    BYTE *data;
    HRESULT hr;

    if (!next_packet(client, &data, &frames, &flags, &position))
        return FALSE;
    if (!client->converted && client->next_position != ~(UINT64)0 && !(flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY))
        ok(position == client->next_position, "position %I64u, expected %I64u\n", position, client->next_position);
    client->next_position = position + frames;
    if (flags_out)
        *flags_out = flags;
    hr = IAudioCaptureClient_ReleaseBuffer(client->capture, frames);
    ok(hr == S_OK, "ReleaseBuffer: %#lx\n", hr);
    return hr == S_OK;
}

static void test_mix_format(const WAVEFORMATEX *format)
{
    const WAVEFORMATEXTENSIBLE *extensible = (const WAVEFORMATEXTENSIBLE *)format;

    ok(format->wFormatTag == WAVE_FORMAT_EXTENSIBLE, "tag %#x\n", format->wFormatTag);
    ok(format->wBitsPerSample == 32, "bits %u\n", format->wBitsPerSample);
    ok(format->nBlockAlign == format->nChannels * sizeof(float), "block align %u\n", format->nBlockAlign);
    ok(IsEqualGUID(&extensible->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT), "subformat is not float\n");
    ok(extensible->Samples.wValidBitsPerSample == 32, "valid bits %u\n", extensible->Samples.wValidBitsPerSample);
}

static void test_packets(const WAVEFORMATEX *format)
{
    struct capture_client client;
    UINT64 position, frequency, captured, first_position;
    REFERENCE_TIME latency;
    UINT32 frames, padding;
    IAudioClock *clock;
    DWORD flags;
    BYTE *data;
    HRESULT hr;
    int i;

    if (!open_client(&client, format, 0))
        goto done;

    hr = IAudioClient_GetStreamLatency(client.client, &latency);
    ok(hr == S_OK && latency == 0, "latency %#lx, %I64d\n", hr, latency);

    hr = IAudioClient_GetService(client.client, &IID_IAudioClock, (void **)&clock);
    ok(hr == S_OK, "clock: %#lx\n", hr);
    hr = IAudioClock_GetFrequency(clock, &frequency);
    ok(hr == S_OK, "GetFrequency: %#lx\n", hr);
    ok(frequency == (UINT64)format->nSamplesPerSec * format->nBlockAlign, "frequency %I64u\n", frequency);

    hr = IAudioClient_Start(client.client);
    ok(hr == S_OK, "Start: %#lx\n", hr);

    ok(next_packet(&client, &data, &frames, &flags, &position), "no first packet\n");
    ok(flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY, "first packet flags %#lx\n", flags);
    ok(frames == client.period_frames, "first packet %u frames, period %u\n", frames, client.period_frames);
    first_position = position;
    hr = IAudioCaptureClient_ReleaseBuffer(client.capture, frames);
    ok(hr == S_OK, "ReleaseBuffer: %#lx\n", hr);
    client.next_position = position + frames;

    for (i = 0; i < 5; ++i)
    {
        ok(read_packet(&client, NULL), "packet %d missing\n", i);
    }

    Sleep(60);
    hr = IAudioClient_GetCurrentPadding(client.client, &padding);
    ok(hr == S_OK, "GetCurrentPadding: %#lx\n", hr);
    ok(padding >= client.period_frames, "padding %u after a wait, period %u\n", padding, client.period_frames);

    hr = IAudioClient_Stop(client.client);
    ok(hr == S_OK, "Stop: %#lx\n", hr);
    hr = IAudioClient_GetCurrentPadding(client.client, &padding);
    ok(hr == S_OK && padding >= client.period_frames, "stopped padding %#lx, %u\n", hr, padding);
    hr = IAudioClock_GetPosition(clock, &position, NULL);
    ok(hr == S_OK, "GetPosition: %#lx\n", hr);
    captured = client.next_position + padding;
    ok(position == (captured - first_position) * format->nBlockAlign, "clock position %I64u, captured %I64u frames from %I64u\n",
       position, captured, first_position);

    ok(next_packet(&client, &data, &frames, &flags, &position), "no packet after Stop\n");
    ok(position == client.next_position, "position after Stop %I64u, expected %I64u\n", position, client.next_position);
    hr = IAudioClient_Reset(client.client);
    ok(hr == AUDCLNT_E_BUFFER_OPERATION_PENDING, "Reset with a held packet: %#lx\n", hr);
    hr = IAudioCaptureClient_ReleaseBuffer(client.capture, frames);
    ok(hr == S_OK, "ReleaseBuffer: %#lx\n", hr);

    hr = IAudioClient_Reset(client.client);
    ok(hr == S_OK, "Reset: %#lx\n", hr);
    hr = IAudioClient_GetCurrentPadding(client.client, &padding);
    ok(hr == S_OK && !padding, "padding after Reset %#lx, %u\n", hr, padding);
    hr = IAudioCaptureClient_GetNextPacketSize(client.capture, &frames);
    ok(hr == S_OK && !frames, "packet after Reset %#lx, %u\n", hr, frames);
    hr = IAudioClock_GetPosition(clock, &position, NULL);
    ok(hr == S_OK, "GetPosition: %#lx\n", hr);
    ok(position == 0, "clock position after Reset %I64u\n", position);

    hr = IAudioClient_Start(client.client);
    ok(hr == S_OK, "Start after Reset: %#lx\n", hr);
    ok(next_packet(&client, &data, &frames, &flags, &position), "no packet after Reset\n");
    ok(position >= captured, "position after Reset %I64u, captured %I64u\n", position, captured);
    hr = IAudioCaptureClient_ReleaseBuffer(client.capture, frames);
    ok(hr == S_OK, "ReleaseBuffer: %#lx\n", hr);
    IAudioClock_Release(clock);
    IAudioClient_Stop(client.client);

done:
    close_client(&client);
}

static void test_concurrent_clients(const WAVEFORMATEX *format)
{
    struct capture_client first, second;
    WAVEFORMATEX pcm = *(const WAVEFORMATEX *)format;
    HRESULT hr;
    int i;

    pcm.wFormatTag = WAVE_FORMAT_PCM;
    pcm.wBitsPerSample = 16;
    pcm.nBlockAlign = pcm.nChannels * 2;
    pcm.nAvgBytesPerSec = pcm.nSamplesPerSec * pcm.nBlockAlign;
    pcm.cbSize = 0;

    if (!open_client(&first, format, 0))
    {
        close_client(&first);
        return;
    }
    if (!open_client(&second, &pcm, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM))
    {
        close_client(&first);
        close_client(&second);
        return;
    }

    second.converted = TRUE;
    hr = IAudioClient_Start(first.client);
    ok(hr == S_OK, "first Start: %#lx\n", hr);
    hr = IAudioClient_Start(second.client);
    ok(hr == S_OK, "second Start: %#lx\n", hr);
    for (i = 0; i < 5; ++i)
    {
        ok(read_packet(&first, NULL), "first client packet %d missing\n", i);
        ok(read_packet(&second, NULL), "second client packet %d missing\n", i);
    }

    hr = IAudioClient_Stop(first.client);
    ok(hr == S_OK, "first Stop: %#lx\n", hr);
    for (i = 0; i < 5; ++i)
    {
        ok(read_packet(&second, NULL), "second client packet %d missing after the first stopped\n", i);
    }

    hr = IAudioClient_Start(first.client);
    ok(hr == S_OK, "first restart: %#lx\n", hr);
    hr = IAudioClient_Stop(second.client);
    ok(hr == S_OK, "second Stop: %#lx\n", hr);
    for (i = 0; i < 5; ++i)
    {
        ok(read_packet(&first, NULL), "first client packet %d missing after the second stopped\n", i);
    }
    IAudioClient_Stop(first.client);
    close_client(&second);
    close_client(&first);
}

static void event_names(DWORD process_id, WCHAR *ready, WCHAR *leave)
{
    swprintf(ready, MAX_PATH, L"Local\\MmdevapiCaptureReady%lu", process_id);
    swprintf(leave, MAX_PATH, L"Local\\MmdevapiCaptureLeave%lu", process_id);
}

static DWORD child_main(const WAVEFORMATEX *format, DWORD parent)
{
    struct capture_client client;
    WCHAR ready_name[MAX_PATH], leave_name[MAX_PATH];
    HANDLE ready, leave;
    DWORD result = 1;
    int i;

    event_names(parent, ready_name, leave_name);
    ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, ready_name);
    leave = OpenEventW(SYNCHRONIZE, FALSE, leave_name);
    if (!ready || !leave || !open_client(&client, format, 0))
        return 2;
    if (FAILED(IAudioClient_Start(client.client)))
        return 3;
    for (i = 0; i < 3; ++i)
    {
        if (!read_packet(&client, NULL))
            return 4;
    }
    SetEvent(ready);
    if (WaitForSingleObject(leave, CHILD_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0)
        return 5;
    for (i = 0; i < 10; ++i)
    {
        if (!read_packet(&client, NULL))
            return 6;
    }
    IAudioClient_Stop(client.client);
    result = 0;
    close_client(&client);
    return result;
}

static void test_other_process(const WAVEFORMATEX *format)
{
    WCHAR path[MAX_PATH], command[MAX_PATH * 2], ready_name[MAX_PATH], leave_name[MAX_PATH];
    PROCESS_INFORMATION process = { 0 };
    STARTUPINFOW startup = { sizeof(startup) };
    struct capture_client client;
    HANDLE ready, leave;
    DWORD code = 0xdead;
    HRESULT hr;
    int i;

    if (!open_client(&client, format, 0))
    {
        close_client(&client);
        return;
    }
    hr = IAudioClient_Start(client.client);
    ok(hr == S_OK, "Start: %#lx\n", hr);
    ok(read_packet(&client, NULL), "no packet before the other process\n");

    event_names(GetCurrentProcessId(), ready_name, leave_name);
    ready = CreateEventW(NULL, TRUE, FALSE, ready_name);
    leave = CreateEventW(NULL, TRUE, FALSE, leave_name);
    GetModuleFileNameW(NULL, path, MAX_PATH);
    swprintf(command, ARRAYSIZE(command), L"\"%s\" capturestream child %lu", path, GetCurrentProcessId());
    ok(CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process), "CreateProcess: %lu\n", GetLastError());
    if (process.hProcess)
    {
        HANDLE waits[2] = { ready, process.hProcess };

        ok(WaitForMultipleObjects(2, waits, FALSE, CHILD_TIMEOUT_MILLISECONDS) == WAIT_OBJECT_0, "the other process did not capture\n");
        for (i = 0; i < 5; ++i)
        {
            ok(read_packet(&client, NULL), "packet %d missing beside the other process\n", i);
        }
        hr = IAudioClient_Stop(client.client);
        ok(hr == S_OK, "Stop: %#lx\n", hr);
        close_client(&client);
        SetEvent(leave);
        ok(WaitForSingleObject(process.hProcess, CHILD_TIMEOUT_MILLISECONDS) == WAIT_OBJECT_0, "the other process did not finish\n");
        GetExitCodeProcess(process.hProcess, &code);
        ok(code == 0, "the other process failed with %lu\n", code);
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
    }
    close_client(&client);
    CloseHandle(ready);
    CloseHandle(leave);
}

static void test_exclusive_event(const WAVEFORMATEX *mix)
{
    WAVEFORMATEX format = *mix;
    IAudioCaptureClient *capture = NULL;
    IAudioClient *client = NULL, *shared = NULL;
    REFERENCE_TIME period = 100000;
    UINT64 position, frequency, next = ~(UINT64)0;
    IAudioClock *clock = NULL;
    UINT32 frames = 0, packet;
    HANDLE event = NULL;
    DWORD flags;
    BYTE *data;
    HRESULT hr;
    int i;

    format.wFormatTag = WAVE_FORMAT_PCM;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * 2;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;

    hr = IMMDevice_Activate(capture_device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, &format, NULL);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
    {
        hr = IAudioClient_GetBufferSize(client, &frames);
        ok(hr == S_OK && frames, "aligned size %#lx, %u\n", hr, frames);
        IAudioClient_Release(client);
        period = (REFERENCE_TIME)(10000000.0 * frames / format.nSamplesPerSec + 0.5);
        hr = IMMDevice_Activate(capture_device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
        ok(hr == S_OK, "Activate: %#lx\n", hr);
        hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, &format, NULL);
    }
    ok(hr == S_OK, "exclusive Initialize: %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    hr = IAudioClient_GetBufferSize(client, &frames);
    ok(hr == S_OK, "GetBufferSize: %#lx\n", hr);
    trace("exclusive period %I64d, buffer %u\n", period, frames);
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    hr = IAudioClient_SetEventHandle(client, event);
    ok(hr == S_OK, "SetEventHandle: %#lx\n", hr);
    hr = IAudioClient_GetService(client, &IID_IAudioCaptureClient, (void **)&capture);
    ok(hr == S_OK, "GetService: %#lx\n", hr);
    hr = IAudioClient_GetService(client, &IID_IAudioClock, (void **)&clock);
    ok(hr == S_OK, "clock: %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    hr = IAudioClock_GetFrequency(clock, &frequency);
    ok(hr == S_OK && frequency == format.nSamplesPerSec, "exclusive frequency %#lx, %I64u\n", hr, frequency);

    hr = IMMDevice_Activate(capture_device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&shared);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    hr = IAudioClient_Initialize(shared, AUDCLNT_SHAREMODE_SHARED, 0, 5000000, 0, mix, NULL);
    ok(hr == AUDCLNT_E_DEVICE_IN_USE, "shared Initialize beside exclusive: %#lx\n", hr);

    hr = IAudioClient_Start(client);
    ok(hr == S_OK, "Start: %#lx\n", hr);
    for (i = 0; i < 6; ++i)
    {
        ok(WaitForSingleObject(event, WAIT_MILLISECONDS) == WAIT_OBJECT_0, "no event %d\n", i);
        hr = IAudioCaptureClient_GetBuffer(capture, &data, &packet, &flags, &position, NULL);
        ok(hr == S_OK || hr == AUDCLNT_S_BUFFER_EMPTY, "GetBuffer %d: %#lx\n", i, hr);
        if (hr != S_OK)
            continue;
        ok(packet == frames, "packet %d has %u frames, buffer %u\n", i, packet, frames);
        if (next != ~(UINT64)0 && !(flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY))
            ok(position == next, "packet %d position %I64u, expected %I64u\n", i, position, next);
        next = position + packet;
        hr = IAudioCaptureClient_ReleaseBuffer(capture, packet);
        ok(hr == S_OK, "ReleaseBuffer: %#lx\n", hr);
    }
    hr = IAudioClient_Stop(client);
    ok(hr == S_OK, "Stop: %#lx\n", hr);

done:
    if (shared)
        IAudioClient_Release(shared);
    if (clock)
        IAudioClock_Release(clock);
    if (capture)
        IAudioCaptureClient_Release(capture);
    if (client)
        IAudioClient_Release(client);
    if (event)
        CloseHandle(event);
}

static void test_shared_blocks_exclusive(const WAVEFORMATEX *mix)
{
    struct capture_client client;
    WAVEFORMATEX format = *mix, unsupported;
    IAudioClient *exclusive;
    REFERENCE_TIME latency;
    IAudioClock *clock;
    UINT64 position;
    UINT32 padding, frames;
    DWORD flags;
    BYTE *data;
    HRESULT hr;

    format.wFormatTag = WAVE_FORMAT_PCM;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * 2;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;

    if (!open_client(&client, mix, 0))
    {
        close_client(&client);
        return;
    }
    hr = IAudioClient_Start(client.client);
    ok(hr == S_OK, "Start: %#lx\n", hr);
    ok(read_packet(&client, NULL), "no packet before exclusive\n");

    unsupported = format;
    unsupported.nSamplesPerSec = 1234;
    unsupported.nAvgBytesPerSec = unsupported.nSamplesPerSec * unsupported.nBlockAlign;
    hr = IMMDevice_Activate(capture_device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&exclusive);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    hr = IAudioClient_Initialize(exclusive, AUDCLNT_SHAREMODE_EXCLUSIVE, 0, 5000000, 0, &unsupported, NULL);
    ok(FAILED(hr), "exclusive with an unsupported format: %#lx\n", hr);
    IAudioClient_Release(exclusive);
    Sleep(100);
    hr = IAudioCaptureClient_GetNextPacketSize(client.capture, &padding);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "GetNextPacketSize after an unsupported exclusive format: %#lx\n", hr);
    hr = IAudioClient_Stop(client.client);
    ok(hr == S_FALSE, "Stop after an unsupported exclusive format: %#lx\n", hr);
    close_client(&client);

    if (!open_client(&client, mix, 0))
    {
        close_client(&client);
        return;
    }
    hr = IAudioClient_Start(client.client);
    ok(hr == S_OK, "Start: %#lx\n", hr);
    ok(read_packet(&client, NULL), "no packet before exclusive\n");
    hr = IMMDevice_Activate(capture_device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&exclusive);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    hr = IAudioClient_Initialize(exclusive, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 30011, 30011, &format, NULL);
    trace("exclusive event with an unaligned period: %#lx\n", hr);
    IAudioClient_Release(exclusive);
    Sleep(100);
    hr = IAudioCaptureClient_GetNextPacketSize(client.capture, &padding);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "GetNextPacketSize after an exclusive attempt: %#lx\n", hr);
    hr = IAudioClient_Stop(client.client);
    ok(hr == S_FALSE, "Stop after an exclusive attempt: %#lx\n", hr);
    close_client(&client);

    if (!open_client(&client, mix, 0))
    {
        close_client(&client);
        return;
    }
    hr = IAudioClient_Start(client.client);
    ok(hr == S_OK, "Start: %#lx\n", hr);
    ok(read_packet(&client, NULL), "no packet before exclusive\n");
    hr = IAudioClient_GetService(client.client, &IID_IAudioClock, (void **)&clock);
    ok(hr == S_OK, "clock service: %#lx\n", hr);
    hr = IMMDevice_Activate(capture_device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&exclusive);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    hr = IAudioClient_Initialize(exclusive, AUDCLNT_SHAREMODE_EXCLUSIVE, 0, 5000000, 0, &format, NULL);
    ok(hr == S_OK, "exclusive beside a started shared client: %#lx\n", hr);
    hr = IAudioClock_GetPosition(clock, &position, NULL);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "preempted GetPosition: %#lx\n", hr);
    IAudioClock_Release(clock);
    hr = IAudioClient_GetCurrentPadding(client.client, &padding);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "preempted GetCurrentPadding: %#lx\n", hr);
    hr = IAudioCaptureClient_GetNextPacketSize(client.capture, &padding);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "preempted GetNextPacketSize: %#lx\n", hr);
    hr = IAudioCaptureClient_GetBuffer(client.capture, &data, &frames, &flags, NULL, NULL);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "preempted GetBuffer: %#lx\n", hr);
    hr = IAudioCaptureClient_ReleaseBuffer(client.capture, 0);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "preempted ReleaseBuffer: %#lx\n", hr);
    hr = IAudioClient_GetBufferSize(client.client, &frames);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "preempted GetBufferSize: %#lx\n", hr);
    hr = IAudioClient_GetStreamLatency(client.client, &latency);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "preempted GetStreamLatency: %#lx\n", hr);
    hr = IAudioClient_GetService(client.client, &IID_IAudioClock, (void **)&clock);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "preempted clock service: %#lx\n", hr);
    hr = IAudioClient_Reset(client.client);
    ok(hr == S_OK, "preempted Reset: %#lx\n", hr);
    hr = IAudioClient_Stop(client.client);
    ok(hr == S_FALSE, "Stop of a preempted shared client: %#lx\n", hr);
    hr = IAudioClient_Start(client.client);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "Start of a preempted shared client: %#lx\n", hr);
    IAudioClient_Release(exclusive);
    hr = IAudioClient_Start(client.client);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "Start of a preempted shared client after exclusive: %#lx\n", hr);
    close_client(&client);

    if (!open_client(&client, mix, 0))
    {
        close_client(&client);
        return;
    }
    hr = IMMDevice_Activate(capture_device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&exclusive);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    hr = IAudioClient_Initialize(exclusive, AUDCLNT_SHAREMODE_EXCLUSIVE, 0, 5000000, 0, &format, NULL);
    ok(hr == S_OK, "exclusive beside a stopped shared client: %#lx\n", hr);
    hr = IAudioClient_Start(client.client);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "shared Start beside exclusive: %#lx\n", hr);
    IAudioClient_Release(exclusive);
    hr = IAudioClient_Start(client.client);
    ok(hr == AUDCLNT_E_DEVICE_INVALIDATED, "shared Start after exclusive: %#lx\n", hr);
    close_client(&client);

    ok(open_client(&client, mix, 0), "no shared client after exclusive\n");
    hr = IAudioClient_Start(client.client);
    ok(hr == S_OK, "Start of a new shared client: %#lx\n", hr);
    ok(read_packet(&client, NULL), "no packet from a new shared client\n");
    IAudioClient_Stop(client.client);
    close_client(&client);
}

static void test_loopback(void)
{
    IAudioClient *loopback = NULL, *render = NULL, *exclusive = NULL;
    IAudioCaptureClient *capture = NULL;
    IAudioRenderClient *writer = NULL;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    WAVEFORMATEX *format = NULL;
    UINT64 position, next = ~(UINT64)0, frequency;
    UINT32 frames, packet, available, i;
    ULONGLONG deadline;
    IAudioClock *clock = NULL;
    BOOL audible = FALSE;
    DWORD flags;
    BYTE *data;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&enumerator);
    ok(hr == S_OK, "enumerator: %#lx\n", hr);
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);
    IMMDeviceEnumerator_Release(enumerator);
    if (FAILED(hr))
    {
        skip("no render device\n");
        return;
    }
    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&loopback);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    hr = IAudioClient_GetMixFormat(loopback, &format);
    ok(hr == S_OK, "GetMixFormat: %#lx\n", hr);
    if (FAILED(hr))
        goto done;

    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&exclusive);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    hr = IAudioClient_Initialize(exclusive, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_LOOPBACK, 5000000, 0, format, NULL);
    ok(hr == E_INVALIDARG, "exclusive loopback: %#lx\n", hr);
    IAudioClient_Release(exclusive);

    hr = IAudioClient_Initialize(loopback, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 5000000, 0, format, NULL);
    ok(hr == S_OK, "loopback Initialize: %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    hr = IAudioClient_GetService(loopback, &IID_IAudioCaptureClient, (void **)&capture);
    ok(hr == S_OK, "capture service: %#lx\n", hr);
    hr = IAudioClient_GetService(loopback, &IID_IAudioClock, (void **)&clock);
    ok(hr == S_OK, "clock service: %#lx\n", hr);
    hr = IAudioClock_GetFrequency(clock, &frequency);
    ok(hr == S_OK && frequency == (UINT64)format->nSamplesPerSec * format->nBlockAlign, "loopback frequency %#lx, %I64u\n", hr, frequency);
    hr = IAudioClient_Start(loopback);
    ok(hr == S_OK, "loopback Start: %#lx\n", hr);

    Sleep(60);
    hr = IAudioCaptureClient_GetNextPacketSize(capture, &packet);
    ok(hr == S_OK, "GetNextPacketSize: %#lx\n", hr);
    ok(!packet, "loopback packet of %u frames while nothing plays\n", packet);

    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&render);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    hr = IAudioClient_Initialize(render, AUDCLNT_SHAREMODE_SHARED, 0, 5000000, 0, format, NULL);
    ok(hr == S_OK, "render Initialize: %#lx\n", hr);
    hr = IAudioClient_GetBufferSize(render, &frames);
    ok(hr == S_OK, "GetBufferSize: %#lx\n", hr);
    hr = IAudioClient_GetService(render, &IID_IAudioRenderClient, (void **)&writer);
    ok(hr == S_OK, "render service: %#lx\n", hr);
    if (FAILED(hr))
        goto stop;
    hr = IAudioRenderClient_GetBuffer(writer, frames, &data);
    ok(hr == S_OK, "render GetBuffer: %#lx\n", hr);
    if (FAILED(hr))
        goto stop;
    for (i = 0; i < frames * format->nChannels; ++i)
        ((float *)data)[i] = (i / format->nChannels) & 16 ? 0.25f : -0.25f;
    hr = IAudioRenderClient_ReleaseBuffer(writer, frames, 0);
    ok(hr == S_OK, "render ReleaseBuffer: %#lx\n", hr);
    hr = IAudioClient_Start(render);
    ok(hr == S_OK, "render Start: %#lx\n", hr);

    deadline = GetTickCount64() + WAIT_MILLISECONDS;
    while (!audible && GetTickCount64() < deadline)
    {
        hr = IAudioCaptureClient_GetNextPacketSize(capture, &available);
        ok(hr == S_OK, "GetNextPacketSize: %#lx\n", hr);
        if (!available)
        {
            Sleep(5);
            continue;
        }
        hr = IAudioCaptureClient_GetBuffer(capture, &data, &packet, &flags, &position, NULL);
        ok(hr == S_OK, "loopback GetBuffer: %#lx\n", hr);
        if (FAILED(hr))
            break;
        if (next != ~(UINT64)0 && !(flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY))
            ok(position == next, "loopback position %I64u, expected %I64u\n", position, next);
        next = position + packet;
        for (i = 0; i < packet * format->nChannels && !(flags & AUDCLNT_BUFFERFLAGS_SILENT); ++i)
        {
            if (((float *)data)[i] > 0.1f || ((float *)data)[i] < -0.1f)
                audible = TRUE;
        }
        hr = IAudioCaptureClient_ReleaseBuffer(capture, packet);
        ok(hr == S_OK, "loopback ReleaseBuffer: %#lx\n", hr);
    }
    ok(audible, "loopback captured nothing of what played\n");
    IAudioClient_Stop(render);

stop:
    IAudioClient_Stop(loopback);
done:
    if (writer)
        IAudioRenderClient_Release(writer);
    if (render)
        IAudioClient_Release(render);
    if (clock)
        IAudioClock_Release(clock);
    if (capture)
        IAudioCaptureClient_Release(capture);
    if (loopback)
        IAudioClient_Release(loopback);
    CoTaskMemFree(format);
    IMMDevice_Release(device);
}

START_TEST(capturestream)
{
    IAudioClient *client;
    WAVEFORMATEX *format;
    char **argv;
    int argc;
    HRESULT hr;

    argc = winetest_get_mainargs(&argv);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (!open_capture_device())
    {
        skip("no capture device\n");
        CoUninitialize();
        return;
    }
    hr = IMMDevice_Activate(capture_device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    ok(hr == S_OK, "Activate: %#lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IAudioClient_GetMixFormat(client, &format);
    ok(hr == S_OK, "GetMixFormat: %#lx\n", hr);
    IAudioClient_Release(client);
    if (FAILED(hr))
        return;

    if (argc >= 4 && !strcmp(argv[2], "child"))
        ExitProcess(child_main(format, strtoul(argv[3], NULL, 10)));

    test_mix_format(format);
    test_packets(format);
    test_concurrent_clients(format);
    test_other_process(format);
    test_loopback();
    test_shared_blocks_exclusive(format);
    test_exclusive_event(format);

    CoTaskMemFree(format);
    IMMDevice_Release(capture_device);
    CoUninitialize();
}
