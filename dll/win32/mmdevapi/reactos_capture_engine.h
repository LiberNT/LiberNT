/*
 * PROJECT:     LiberNT Core Audio
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Cross-process shared-mode capture engine contract
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

struct reactos_shared_capture_client;
struct reactos_shared_capture_sink;

struct reactos_shared_capture_read_result
{
    UINT64 lost_frames;
    BOOL discontinuity;
    BOOL invalidated;
    BOOL device_action;
};

struct reactos_capture_transport_ops
{
    HRESULT (*create)(DWORD endpoint_index, const WAVEFORMATEXTENSIBLE *format,
                      struct reactos_shared_capture_sink *sink, void **transport);
    void (*destroy)(void *transport);
    HRESULT (*start)(void *transport);
};

HRESULT
reactos_shared_capture_create(
    DWORD endpoint_index,
    BOOL loopback,
    const WAVEFORMATEXTENSIBLE *format,
    const struct reactos_capture_transport_ops *transport_ops,
    struct reactos_shared_capture_client **client);
void
reactos_shared_capture_release(struct reactos_shared_capture_client *client);
HRESULT
reactos_shared_capture_start(struct reactos_shared_capture_client *client);
void
reactos_shared_capture_stop(struct reactos_shared_capture_client *client);
UINT32
reactos_shared_capture_read(
    struct reactos_shared_capture_client *client,
    BYTE *data,
    UINT32 frames,
    struct reactos_shared_capture_read_result *result);
void
reactos_shared_capture_service_device(struct reactos_shared_capture_client *client);
BOOL
reactos_shared_capture_invalidated(const struct reactos_shared_capture_client *client);
void
reactos_shared_capture_write(struct reactos_shared_capture_sink *sink, const BYTE *data, UINT32 frames);
HRESULT
reactos_shared_capture_open_exclusive(
    DWORD endpoint_index,
    const WAVEFORMATEXTENSIBLE *format,
    const struct reactos_capture_transport_ops *transport_ops,
    struct reactos_shared_capture_sink **endpoint);
void
reactos_shared_capture_close_exclusive(struct reactos_shared_capture_sink *endpoint);
void
reactos_shared_capture_invalidate_clients(
    DWORD endpoint_index,
    const WAVEFORMATEXTENSIBLE *format,
    const struct reactos_capture_transport_ops *transport_ops);
HRESULT
reactos_shared_capture_open_loopback_sink(
    DWORD endpoint_index,
    const WAVEFORMATEXTENSIBLE *format,
    struct reactos_shared_capture_sink **sink);
void
reactos_shared_capture_close_sink(struct reactos_shared_capture_sink *sink);
