/*
 * PROJECT:     LiberNT Core Audio
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Cross-process shared-mode capture engine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "mmreg.h"
#include "audioclient.h"

#include "reactos_capture_engine.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#define CAPTURE_REGION_MAGIC 0x41435352 /* RSCA */
#define CAPTURE_REGION_VERSION 1
#define CAPTURE_CLIENT_CAPACITY 16
#define CAPTURE_RING_FRAMES 65536
#define CAPTURE_MAX_FRAME_SIZE 32
#define CAPTURE_OWNER_RETRY_MS 100
#define CAPTURE_PREEMPT_TIMEOUT_MS 2000
#define CAPTURE_PREEMPT_POLL_MS 5
#define CAPTURE_WRITE_WAIT_MS 2

struct shared_capture_slot
{
    LONG occupied;
    LONG started;
    DWORD process_id;
    FILETIME process_start_time;
};

struct shared_capture_region
{
    DWORD magic;
    DWORD version;
    DWORD size;
    DWORD endpoint_index;
    WAVEFORMATEXTENSIBLE format;
    DWORD owner_process_id;
    FILETIME owner_process_start_time;
    DWORD exclusive_process_id;
    FILETIME exclusive_process_start_time;
    LONG exclusive_generation;
    DWORD owner_epoch;
    UINT64 write_position;
    struct shared_capture_slot clients[CAPTURE_CLIENT_CAPACITY];
    BYTE data[CAPTURE_RING_FRAMES * CAPTURE_MAX_FRAME_SIZE];
};

struct reactos_shared_capture_sink
{
    struct reactos_shared_capture_sink *next;
    DWORD endpoint_index;
    BOOL loopback;
    DWORD reference_count;
    FILETIME process_start_time;
    HANDLE mapping;
    HANDLE mutex;
    HANDLE alive_event;
    struct shared_capture_region *region;
    struct reactos_capture_transport_ops transport_ops;
    CRITICAL_SECTION transport_lock;
    void *transport;
    ULONGLONG next_owner_attempt;
    BOOL write_dropped;
};

struct reactos_shared_capture_client
{
    struct reactos_shared_capture_sink *endpoint;
    DWORD slot_index;
    LONG exclusive_generation;
    DWORD owner_epoch;
    UINT64 read_position;
};

static INIT_ONCE endpoint_list_init_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION endpoint_list_lock;
static struct reactos_shared_capture_sink *endpoint_list;

static BOOL WINAPI
initialize_endpoint_list(INIT_ONCE *once, void *parameter, void **context)
{
    UNREFERENCED_PARAMETER(once);
    UNREFERENCED_PARAMETER(parameter);
    UNREFERENCED_PARAMETER(context);

    InitializeCriticalSection(&endpoint_list_lock);
    return TRUE;
}

static BOOL
lock_region(struct reactos_shared_capture_sink *endpoint)
{
    DWORD wait = WaitForSingleObject(endpoint->mutex, INFINITE);

    return wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
}

static void
unlock_region(struct reactos_shared_capture_sink *endpoint)
{
    ReleaseMutex(endpoint->mutex);
}

static void
format_alive_event_name(WCHAR *name, SIZE_T name_cch, DWORD process_id, const FILETIME *process_start_time)
{
    swprintf(name, name_cch, L"Local\\LiberNT.CoreAudio.Alive.%lu.%lx.%lx", process_id,
             process_start_time->dwHighDateTime, process_start_time->dwLowDateTime);
}

static BOOL
process_record_is_alive(DWORD process_id, const FILETIME *process_start_time)
{
    WCHAR name[96];
    HANDLE event;
    DWORD error;

    if (!process_id)
        return FALSE;

    format_alive_event_name(name, ARRAY_SIZE(name), process_id, process_start_time);
    event = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (event)
    {
        CloseHandle(event);
        return TRUE;
    }
    error = GetLastError();
    return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
}

static BOOL
slot_is_current_process(const struct reactos_shared_capture_sink *endpoint, const struct shared_capture_slot *slot)
{
    return slot->process_id == GetCurrentProcessId() &&
           !CompareFileTime(&slot->process_start_time, &endpoint->process_start_time);
}

static BOOL
region_format_matches(const struct shared_capture_region *region, const WAVEFORMATEXTENSIBLE *format)
{
    return region->format.Format.nSamplesPerSec == format->Format.nSamplesPerSec &&
           region->format.Format.nChannels == format->Format.nChannels &&
           region->format.Format.wBitsPerSample == format->Format.wBitsPerSample &&
           region->format.Format.nBlockAlign == format->Format.nBlockAlign;
}

static BOOL
owner_is_current_process_locked(const struct reactos_shared_capture_sink *endpoint)
{
    return endpoint->region->owner_process_id == GetCurrentProcessId() &&
           !CompareFileTime(&endpoint->region->owner_process_start_time, &endpoint->process_start_time);
}

static void
claim_owner_locked(struct reactos_shared_capture_sink *endpoint)
{
    endpoint->region->owner_process_id = GetCurrentProcessId();
    endpoint->region->owner_process_start_time = endpoint->process_start_time;
    ++endpoint->region->owner_epoch;
}

static void
clear_owner(struct reactos_shared_capture_sink *endpoint)
{
    if (!lock_region(endpoint))
        return;
    endpoint->region->owner_process_id = 0;
    ZeroMemory(&endpoint->region->owner_process_start_time, sizeof(endpoint->region->owner_process_start_time));
    unlock_region(endpoint);
}

static HRESULT
open_transport(struct reactos_shared_capture_sink *endpoint)
{
    HRESULT hr;

    hr = endpoint->transport_ops.create(endpoint->endpoint_index, &endpoint->region->format, endpoint, &endpoint->transport);
    if (FAILED(hr))
    {
        endpoint->transport = NULL;
        clear_owner(endpoint);
        return hr;
    }
    hr = endpoint->transport_ops.start(endpoint->transport);
    if (FAILED(hr))
    {
        endpoint->transport_ops.destroy(endpoint->transport);
        endpoint->transport = NULL;
        clear_owner(endpoint);
    }
    return hr;
}

static void
close_transport(struct reactos_shared_capture_sink *endpoint)
{
    endpoint->transport_ops.destroy(endpoint->transport);
    endpoint->transport = NULL;
    clear_owner(endpoint);
}

static void
destroy_endpoint(struct reactos_shared_capture_sink *endpoint)
{
    if (endpoint->region)
        UnmapViewOfFile(endpoint->region);
    if (endpoint->mapping)
        CloseHandle(endpoint->mapping);
    if (endpoint->mutex)
        CloseHandle(endpoint->mutex);
    if (endpoint->alive_event)
        CloseHandle(endpoint->alive_event);
    DeleteCriticalSection(&endpoint->transport_lock);
    free(endpoint);
}

static HRESULT
create_endpoint(
    DWORD endpoint_index,
    BOOL loopback,
    const WAVEFORMATEXTENSIBLE *format,
    const struct reactos_capture_transport_ops *transport_ops,
    struct reactos_shared_capture_sink **created)
{
    FILETIME exit_time, kernel_time, user_time;
    WCHAR mapping_name[96], mutex_name[104], alive_name[96];
    struct reactos_shared_capture_sink *endpoint;
    struct shared_capture_region *region;
    HRESULT hr = S_OK;

    endpoint = calloc(1, sizeof(*endpoint));
    if (!endpoint)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&endpoint->transport_lock);
    endpoint->endpoint_index = endpoint_index;
    endpoint->loopback = loopback;
    if (transport_ops)
        endpoint->transport_ops = *transport_ops;
    GetProcessTimes(GetCurrentProcess(), &endpoint->process_start_time, &exit_time, &kernel_time, &user_time);

    swprintf(mapping_name, ARRAY_SIZE(mapping_name), L"Local\\LiberNT.CoreAudio.%s.%lu.v1",
             loopback ? L"Loopback" : L"Capture", endpoint_index);
    swprintf(mutex_name, ARRAY_SIZE(mutex_name), L"%s.Lock", mapping_name);
    format_alive_event_name(alive_name, ARRAY_SIZE(alive_name), GetCurrentProcessId(), &endpoint->process_start_time);
    endpoint->alive_event = CreateEventW(NULL, TRUE, FALSE, alive_name);
    endpoint->mutex = CreateMutexW(NULL, FALSE, mutex_name);
    endpoint->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*region), mapping_name);
    if (endpoint->alive_event && endpoint->mutex && endpoint->mapping)
        endpoint->region = MapViewOfFile(endpoint->mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(*region));
    if (!endpoint->region || !lock_region(endpoint))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        destroy_endpoint(endpoint);
        return FAILED(hr) ? hr : E_FAIL;
    }

    region = endpoint->region;
    if (!region->magic)
    {
        region->version = CAPTURE_REGION_VERSION;
        region->size = sizeof(*region);
        region->endpoint_index = endpoint_index;
        region->format = *format;
        region->magic = CAPTURE_REGION_MAGIC;
    }
    else if (region->magic != CAPTURE_REGION_MAGIC || region->version != CAPTURE_REGION_VERSION ||
             region->size != sizeof(*region) || region->endpoint_index != endpoint_index ||
             !region_format_matches(region, format))
    {
        hr = AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    }
    unlock_region(endpoint);
    if (FAILED(hr))
    {
        destroy_endpoint(endpoint);
        return hr;
    }

    *created = endpoint;
    return S_OK;
}

static HRESULT
acquire_endpoint(
    DWORD endpoint_index,
    BOOL loopback,
    const WAVEFORMATEXTENSIBLE *format,
    const struct reactos_capture_transport_ops *transport_ops,
    struct reactos_shared_capture_sink **acquired)
{
    struct reactos_shared_capture_sink *endpoint;
    HRESULT hr;

    if (format->Format.nBlockAlign > CAPTURE_MAX_FRAME_SIZE)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    if (!InitOnceExecuteOnce(&endpoint_list_init_once, initialize_endpoint_list, NULL, NULL))
        return HRESULT_FROM_WIN32(GetLastError());

    EnterCriticalSection(&endpoint_list_lock);
    for (endpoint = endpoint_list; endpoint; endpoint = endpoint->next)
    {
        if (endpoint->endpoint_index == endpoint_index && endpoint->loopback == loopback)
            break;
    }
    if (!endpoint)
    {
        hr = create_endpoint(endpoint_index, loopback, format, transport_ops, &endpoint);
        if (FAILED(hr))
        {
            LeaveCriticalSection(&endpoint_list_lock);
            return hr;
        }
        endpoint->next = endpoint_list;
        endpoint_list = endpoint;
    }
    else if (!region_format_matches(endpoint->region, format))
    {
        LeaveCriticalSection(&endpoint_list_lock);
        return AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    }
    ++endpoint->reference_count;
    LeaveCriticalSection(&endpoint_list_lock);
    *acquired = endpoint;
    return S_OK;
}

static void
release_endpoint(struct reactos_shared_capture_sink *endpoint)
{
    struct reactos_shared_capture_sink **link;

    EnterCriticalSection(&endpoint_list_lock);
    if (--endpoint->reference_count)
    {
        LeaveCriticalSection(&endpoint_list_lock);
        return;
    }
    for (link = &endpoint_list; *link; link = &(*link)->next)
    {
        if (*link == endpoint)
        {
            *link = endpoint->next;
            break;
        }
    }
    LeaveCriticalSection(&endpoint_list_lock);
    destroy_endpoint(endpoint);
}

HRESULT
reactos_shared_capture_open_exclusive(
    DWORD endpoint_index,
    const WAVEFORMATEXTENSIBLE *format,
    const struct reactos_capture_transport_ops *transport_ops,
    struct reactos_shared_capture_sink **opened)
{
    struct reactos_shared_capture_sink *endpoint;
    struct shared_capture_region *region;
    ULONGLONG deadline;
    HRESULT hr;

    hr = acquire_endpoint(endpoint_index, FALSE, format, transport_ops, &endpoint);
    if (FAILED(hr))
        return hr;

    region = endpoint->region;
    if (!lock_region(endpoint))
    {
        release_endpoint(endpoint);
        return E_FAIL;
    }
    if (process_record_is_alive(region->exclusive_process_id, &region->exclusive_process_start_time))
    {
        unlock_region(endpoint);
        release_endpoint(endpoint);
        return AUDCLNT_E_DEVICE_IN_USE;
    }
    InterlockedIncrement(&region->exclusive_generation);
    region->exclusive_process_id = GetCurrentProcessId();
    region->exclusive_process_start_time = endpoint->process_start_time;
    unlock_region(endpoint);

    deadline = GetTickCount64() + CAPTURE_PREEMPT_TIMEOUT_MS;
    for (;;)
    {
        BOOL owned;

        if (!lock_region(endpoint))
        {
            hr = E_FAIL;
            break;
        }
        owned = process_record_is_alive(region->owner_process_id, &region->owner_process_start_time);
        unlock_region(endpoint);
        if (!owned)
        {
            *opened = endpoint;
            return S_OK;
        }
        if (GetTickCount64() >= deadline)
        {
            hr = AUDCLNT_E_DEVICE_IN_USE;
            break;
        }
        Sleep(CAPTURE_PREEMPT_POLL_MS);
    }
    reactos_shared_capture_close_exclusive(endpoint);
    return hr;
}

void
reactos_shared_capture_invalidate_clients(
    DWORD endpoint_index,
    const WAVEFORMATEXTENSIBLE *format,
    const struct reactos_capture_transport_ops *transport_ops)
{
    struct reactos_shared_capture_sink *endpoint;

    if (FAILED(acquire_endpoint(endpoint_index, FALSE, format, transport_ops, &endpoint)))
        return;
    InterlockedIncrement(&endpoint->region->exclusive_generation);
    release_endpoint(endpoint);
}

void
reactos_shared_capture_close_exclusive(struct reactos_shared_capture_sink *endpoint)
{
    if (lock_region(endpoint))
    {
        endpoint->region->exclusive_process_id = 0;
        ZeroMemory(&endpoint->region->exclusive_process_start_time, sizeof(endpoint->region->exclusive_process_start_time));
        unlock_region(endpoint);
    }
    release_endpoint(endpoint);
}

HRESULT
reactos_shared_capture_open_loopback_sink(
    DWORD endpoint_index,
    const WAVEFORMATEXTENSIBLE *format,
    struct reactos_shared_capture_sink **sink)
{
    return acquire_endpoint(endpoint_index, TRUE, format, NULL, sink);
}

void
reactos_shared_capture_close_sink(struct reactos_shared_capture_sink *sink)
{
    release_endpoint(sink);
}

HRESULT
reactos_shared_capture_create(
    DWORD endpoint_index,
    BOOL loopback,
    const WAVEFORMATEXTENSIBLE *format,
    const struct reactos_capture_transport_ops *transport_ops,
    struct reactos_shared_capture_client **client)
{
    struct reactos_shared_capture_client *new_client;
    struct reactos_shared_capture_sink *endpoint;
    UINT32 index;
    HRESULT hr;

    new_client = calloc(1, sizeof(*new_client));
    if (!new_client)
        return E_OUTOFMEMORY;
    hr = acquire_endpoint(endpoint_index, loopback, format, transport_ops, &endpoint);
    if (FAILED(hr))
    {
        free(new_client);
        return hr;
    }

    hr = AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    new_client->endpoint = endpoint;
    if (lock_region(endpoint))
    {
        if (process_record_is_alive(endpoint->region->exclusive_process_id, &endpoint->region->exclusive_process_start_time))
        {
            unlock_region(endpoint);
            free(new_client);
            release_endpoint(endpoint);
            return AUDCLNT_E_DEVICE_IN_USE;
        }
        new_client->exclusive_generation = endpoint->region->exclusive_generation;
        for (index = 0; index < CAPTURE_CLIENT_CAPACITY; ++index)
        {
            struct shared_capture_slot *slot = &endpoint->region->clients[index];

            if (slot->occupied && !process_record_is_alive(slot->process_id, &slot->process_start_time))
                ZeroMemory(slot, sizeof(*slot));
            if (!slot->occupied)
            {
                slot->occupied = TRUE;
                slot->process_id = GetCurrentProcessId();
                slot->process_start_time = endpoint->process_start_time;
                new_client->slot_index = index;
                hr = S_OK;
                break;
            }
        }
        unlock_region(endpoint);
    }
    if (FAILED(hr))
    {
        free(new_client);
        release_endpoint(endpoint);
        return hr;
    }

    *client = new_client;
    return S_OK;
}

void
reactos_shared_capture_release(struct reactos_shared_capture_client *client)
{
    struct reactos_shared_capture_sink *endpoint = client->endpoint;

    reactos_shared_capture_stop(client);
    if (lock_region(endpoint))
    {
        struct shared_capture_slot *slot = &endpoint->region->clients[client->slot_index];

        if (slot->occupied && slot_is_current_process(endpoint, slot))
            ZeroMemory(slot, sizeof(*slot));
        unlock_region(endpoint);
    }
    free(client);
    release_endpoint(endpoint);
}

HRESULT
reactos_shared_capture_start(struct reactos_shared_capture_client *client)
{
    struct reactos_shared_capture_sink *endpoint = client->endpoint;
    struct shared_capture_region *region = endpoint->region;
    BOOL become_owner;
    HRESULT hr = S_OK;

    EnterCriticalSection(&endpoint->transport_lock);
    if (!lock_region(endpoint))
    {
        LeaveCriticalSection(&endpoint->transport_lock);
        return E_FAIL;
    }
    if (region->exclusive_generation != client->exclusive_generation)
    {
        unlock_region(endpoint);
        LeaveCriticalSection(&endpoint->transport_lock);
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    region->clients[client->slot_index].started = TRUE;
    client->read_position = region->write_position;
    become_owner = endpoint->transport_ops.create &&
                   !process_record_is_alive(region->owner_process_id, &region->owner_process_start_time) &&
                   !process_record_is_alive(region->exclusive_process_id, &region->exclusive_process_start_time);
    if (become_owner)
        claim_owner_locked(endpoint);
    client->owner_epoch = region->owner_epoch;
    unlock_region(endpoint);

    if (become_owner)
    {
        hr = open_transport(endpoint);
        if (FAILED(hr) && lock_region(endpoint))
        {
            region->clients[client->slot_index].started = FALSE;
            unlock_region(endpoint);
        }
    }
    LeaveCriticalSection(&endpoint->transport_lock);
    return hr;
}

void
reactos_shared_capture_stop(struct reactos_shared_capture_client *client)
{
    struct reactos_shared_capture_sink *endpoint = client->endpoint;
    struct shared_capture_region *region = endpoint->region;
    BOOL local_clients = FALSE;
    UINT32 index;

    EnterCriticalSection(&endpoint->transport_lock);
    if (!lock_region(endpoint))
    {
        LeaveCriticalSection(&endpoint->transport_lock);
        return;
    }
    region->clients[client->slot_index].started = FALSE;
    for (index = 0; index < CAPTURE_CLIENT_CAPACITY; ++index)
    {
        if (region->clients[index].occupied && region->clients[index].started &&
            slot_is_current_process(endpoint, &region->clients[index]))
        {
            local_clients = TRUE;
        }
    }
    unlock_region(endpoint);

    if (!local_clients && endpoint->transport)
        close_transport(endpoint);
    LeaveCriticalSection(&endpoint->transport_lock);
}

void
reactos_shared_capture_service_device(struct reactos_shared_capture_client *client)
{
    struct reactos_shared_capture_sink *endpoint = client->endpoint;
    struct shared_capture_region *region = endpoint->region;
    ULONGLONG now = GetTickCount64();
    BOOL become_owner = FALSE;

    EnterCriticalSection(&endpoint->transport_lock);
    if (lock_region(endpoint))
    {
        if (!endpoint->transport && endpoint->transport_ops.create && now >= endpoint->next_owner_attempt &&
            region->exclusive_generation == client->exclusive_generation &&
            !process_record_is_alive(region->exclusive_process_id, &region->exclusive_process_start_time) &&
            !process_record_is_alive(region->owner_process_id, &region->owner_process_start_time))
        {
            become_owner = TRUE;
            claim_owner_locked(endpoint);
        }
        unlock_region(endpoint);
    }
    if (become_owner && FAILED(open_transport(endpoint)))
        endpoint->next_owner_attempt = now + CAPTURE_OWNER_RETRY_MS;
    LeaveCriticalSection(&endpoint->transport_lock);
}

BOOL
reactos_shared_capture_invalidated(const struct reactos_shared_capture_client *client)
{
    return InterlockedCompareExchange(&client->endpoint->region->exclusive_generation, 0, 0) != client->exclusive_generation;
}

UINT32
reactos_shared_capture_read(
    struct reactos_shared_capture_client *client,
    BYTE *data,
    UINT32 frames,
    struct reactos_shared_capture_read_result *result)
{
    struct reactos_shared_capture_sink *endpoint = client->endpoint;
    struct shared_capture_region *region = endpoint->region;
    UINT32 frame_size = region->format.Format.nBlockAlign;
    UINT32 offset, first;
    UINT64 available;

    ZeroMemory(result, sizeof(*result));
    if (!lock_region(endpoint))
        return 0;
    if (region->exclusive_generation != client->exclusive_generation)
    {
        unlock_region(endpoint);
        result->invalidated = TRUE;
        return 0;
    }
    if (client->owner_epoch != region->owner_epoch)
    {
        client->owner_epoch = region->owner_epoch;
        result->discontinuity = TRUE;
    }
    available = region->write_position - client->read_position;
    if (available > CAPTURE_RING_FRAMES)
    {
        result->lost_frames = available - CAPTURE_RING_FRAMES;
        client->read_position = region->write_position - CAPTURE_RING_FRAMES;
        available = CAPTURE_RING_FRAMES;
    }
    frames = (UINT32)min(available, (UINT64)frames);
    offset = (UINT32)(client->read_position % CAPTURE_RING_FRAMES);
    first = min(frames, (UINT32)CAPTURE_RING_FRAMES - offset);
    memcpy(data, region->data + (SIZE_T)offset * frame_size, (SIZE_T)first * frame_size);
    memcpy(data + (SIZE_T)first * frame_size, region->data, (SIZE_T)(frames - first) * frame_size);
    client->read_position += frames;
    result->device_action = !frames && endpoint->transport_ops.create && !owner_is_current_process_locked(endpoint) &&
                            !process_record_is_alive(region->owner_process_id, &region->owner_process_start_time);
    unlock_region(endpoint);
    return frames;
}

void
reactos_shared_capture_write(struct reactos_shared_capture_sink *endpoint, const BYTE *data, UINT32 frames)
{
    struct shared_capture_region *region = endpoint->region;
    UINT32 frame_size = region->format.Format.nBlockAlign;
    UINT32 offset, first;
    DWORD wait;

    wait = WaitForSingleObject(endpoint->mutex, CAPTURE_WRITE_WAIT_MS);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
    {
        endpoint->write_dropped = TRUE;
        return;
    }
    if (endpoint->write_dropped)
    {
        endpoint->write_dropped = FALSE;
        ++region->owner_epoch;
    }
    if (frames > CAPTURE_RING_FRAMES)
    {
        data += (SIZE_T)(frames - CAPTURE_RING_FRAMES) * frame_size;
        region->write_position += frames - CAPTURE_RING_FRAMES;
        frames = CAPTURE_RING_FRAMES;
    }
    offset = (UINT32)(region->write_position % CAPTURE_RING_FRAMES);
    first = min(frames, (UINT32)CAPTURE_RING_FRAMES - offset);
    memcpy(region->data + (SIZE_T)offset * frame_size, data, (SIZE_T)first * frame_size);
    memcpy(region->data, data + (SIZE_T)first * frame_size, (SIZE_T)(frames - first) * frame_size);
    region->write_position += frames;
    unlock_region(endpoint);
}
