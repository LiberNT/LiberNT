/*
 * HTTPAPI implementation
 *
 * Copyright 2009 Austin English
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define HTTPAPI_LINKAGE
#include "wine/http.h"
#include "winsvc.h"
#include "winternl.h"
#include "wine/debug.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(http);

#define HTTP_DEFAULT_TIMEOUT_SECONDS 120

/***********************************************************************
 *        HttpInitialize       (HTTPAPI.@)
 *
 * Initializes HTTP Server API engine
 *
 * PARAMS
 *   version  [ I] HTTP API version which caller will use
 *   flags    [ I] initialization options which specify parts of API what will be used
 *   reserved [IO] reserved, must be NULL
 *
 * RETURNS
 *   NO_ERROR if function succeeds, or error code if function fails
 *
 */
ULONG WINAPI HttpInitialize(HTTPAPI_VERSION version, ULONG flags, void *reserved)
{
    SC_HANDLE manager, service;

    TRACE("version %u.%u, flags %#lx, reserved %p.\n", version.HttpApiMajorVersion,
            version.HttpApiMinorVersion, flags, reserved);

    if ((version.HttpApiMajorVersion != 1 || version.HttpApiMinorVersion != 0) &&
        (version.HttpApiMajorVersion != 2 || version.HttpApiMinorVersion != 0))
    {
        return ERROR_INVALID_DLL;
    }

    if (reserved || (flags & ~(HTTP_INITIALIZE_SERVER | HTTP_INITIALIZE_CONFIG)))
        return ERROR_INVALID_PARAMETER;

    if (!(manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT)))
        return GetLastError();

    if (!(service = OpenServiceW(manager, L"http", SERVICE_START)))
    {
        ERR("Failed to open HTTP service, error %lu.\n", GetLastError());
        CloseServiceHandle(manager);
        return GetLastError();
    }

    if (!StartServiceW(service, 0, NULL) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
    {
        ERR("Failed to start HTTP service, error %lu.\n", GetLastError());
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return GetLastError();
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return ERROR_SUCCESS;
}

/***********************************************************************
 *        HttpTerminate       (HTTPAPI.@)
 *
 * Cleans up HTTP Server API engine resources allocated by HttpInitialize
 *
 * PARAMS
 *   flags    [ I] options which specify parts of API what should be released
 *   reserved [IO] reserved, must be NULL
 *
 * RETURNS
 *   NO_ERROR if function succeeds, or error code if function fails
 *
 */
ULONG WINAPI HttpTerminate( ULONG flags, PVOID reserved )
{
    if (reserved || (flags & ~(HTTP_INITIALIZE_SERVER | HTTP_INITIALIZE_CONFIG)))
        return ERROR_INVALID_PARAMETER;
    return NO_ERROR;
}

static const WCHAR *timeout_value_name(HTTP_SERVICE_CONFIG_TIMEOUT_KEY key)
{
    switch (key)
    {
        case IdleConnectionTimeout: return L"IdleConnectionTimeout";
        case HeaderWaitTimeout: return L"HeaderWaitTimeout";
        default: return NULL;
    }
}

static ULONG open_http_parameters(REGSAM access, HKEY *key)
{
    return RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\HTTP\\Parameters",
                           0, NULL, 0, access, NULL, key, NULL);
}

/***********************************************************************
 *        HttpDeleteServiceConfiguration     (HTTPAPI.@)
 *
 * Remove configuration record from HTTP Server API configuration store
 *
 * PARAMS
 *   handle     [I] reserved, must be 0
 *   type       [I] configuration record type
 *   config     [I] buffer which contains configuration record information
 *   length     [I] length of configuration record buffer
 *   overlapped [I] reserved, must be NULL
 *
 * RETURNS
 *   NO_ERROR if function succeeds, or error code if function fails
 *
 */
ULONG WINAPI HttpDeleteServiceConfiguration( HANDLE handle, HTTP_SERVICE_CONFIG_ID type,
                 PVOID config, ULONG length, LPOVERLAPPED overlapped )
{
    const HTTP_SERVICE_CONFIG_TIMEOUT_SET *timeout = config;
    const WCHAR *name;
    HKEY key;
    ULONG ret;

    TRACE("handle %p, type %d, config %p, length %lu, overlapped %p.\n",
          handle, type, config, length, overlapped);

    if (type != HttpServiceConfigTimeout)
    {
        FIXME("Unhandled configuration type %d.\n", type);
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
    if (handle || overlapped)
        return ERROR_INVALID_PARAMETER;
    if (!config)
        return ERROR_NOACCESS;
    if (length < sizeof(*timeout))
        return ERROR_INSUFFICIENT_BUFFER;
    if (!(name = timeout_value_name(timeout->KeyDesc)))
        return ERROR_INVALID_PARAMETER;

    if ((ret = open_http_parameters(KEY_SET_VALUE, &key)))
        return ret;
    ret = RegDeleteValueW(key, name);
    RegCloseKey(key);
    return ret;
}

/***********************************************************************
 *        HttpQueryServiceConfiguration     (HTTPAPI.@)
 *
 * Retrieves configuration records from HTTP Server API configuration store
 *
 * PARAMS
 *   handle     [ I] reserved, must be 0
 *   type       [ I] configuration records type
 *   query      [ I] buffer which contains query data used to retrieve records
 *   query_len  [ I] length of query buffer
 *   buffer     [IO] buffer to store query results
 *   buffer_len [ I] length of output buffer
 *   data_len   [ O] optional pointer to a buffer which receives query result length
 *   overlapped [ I] reserved, must be NULL
 *
 * RETURNS
 *   NO_ERROR if function succeeds, or error code if function fails
 *
 */
ULONG WINAPI HttpQueryServiceConfiguration( HANDLE handle, HTTP_SERVICE_CONFIG_ID type,
                 PVOID query, ULONG query_len, PVOID buffer, ULONG buffer_len,
                 PULONG data_len, LPOVERLAPPED overlapped )
{
    HTTP_SERVICE_CONFIG_TIMEOUT_PARAM seconds = HTTP_DEFAULT_TIMEOUT_SECONDS;
    DWORD value, value_size = sizeof(value), value_type;
    const WCHAR *name;
    HKEY key;

    TRACE("handle %p, type %d, query %p, query_len %lu, buffer %p, buffer_len %lu, data_len %p, overlapped %p.\n",
          handle, type, query, query_len, buffer, buffer_len, data_len, overlapped);

    if (type != HttpServiceConfigTimeout)
    {
        FIXME("Unhandled configuration type %d.\n", type);
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
    if (handle || overlapped)
        return ERROR_INVALID_PARAMETER;
    if (!query || query_len < sizeof(HTTP_SERVICE_CONFIG_TIMEOUT_KEY) ||
        !(name = timeout_value_name(*(const HTTP_SERVICE_CONFIG_TIMEOUT_KEY *)query)))
    {
        if (data_len)
            *data_len = 0;
        return ERROR_INVALID_PARAMETER;
    }

    if (data_len)
        *data_len = sizeof(seconds);
    if (!buffer || buffer_len < sizeof(seconds))
        return ERROR_MORE_DATA;

    if (!open_http_parameters(KEY_QUERY_VALUE, &key))
    {
        if (!RegQueryValueExW(key, name, NULL, &value_type, (BYTE *)&value, &value_size) &&
            value_type == REG_DWORD && value && value <= USHRT_MAX)
            seconds = (HTTP_SERVICE_CONFIG_TIMEOUT_PARAM)value;
        RegCloseKey(key);
    }

    memcpy(buffer, &seconds, sizeof(seconds));
    return ERROR_SUCCESS;
}

/***********************************************************************
 *        HttpSetServiceConfiguration     (HTTPAPI.@)
 *
 * Add configuration record to HTTP Server API configuration store
 *
 * PARAMS
 *   handle     [I] reserved, must be 0
 *   type       [I] configuration record type
 *   config     [I] buffer which contains configuration record information
 *   length     [I] length of configuration record buffer
 *   overlapped [I] reserved, must be NULL
 *
 * RETURNS
 *   NO_ERROR if function succeeds, or error code if function fails
 *
 */
ULONG WINAPI HttpSetServiceConfiguration( HANDLE handle, HTTP_SERVICE_CONFIG_ID type,
                 PVOID config, ULONG length, LPOVERLAPPED overlapped )
{
    const HTTP_SERVICE_CONFIG_TIMEOUT_SET *timeout = config;
    const WCHAR *name;
    DWORD value;
    HKEY key;
    ULONG ret;

    TRACE("handle %p, type %d, config %p, length %lu, overlapped %p.\n",
          handle, type, config, length, overlapped);

    if (type != HttpServiceConfigTimeout)
    {
        FIXME("Unhandled configuration type %d.\n", type);
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
    if (handle || overlapped)
        return ERROR_INVALID_PARAMETER;
    if (!config)
        return ERROR_NOACCESS;
    if (length < sizeof(*timeout))
        return ERROR_INSUFFICIENT_BUFFER;
    if (!(name = timeout_value_name(timeout->KeyDesc)) || !timeout->ParamDesc)
        return ERROR_INVALID_PARAMETER;

    if ((ret = open_http_parameters(KEY_SET_VALUE, &key)))
        return ret;
    value = timeout->ParamDesc;
    ret = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return ret;
}

/***********************************************************************
 *        HttpCreateHttpHandle     (HTTPAPI.@)
 *
 * Creates a handle to the HTTP request queue
 *
 * PARAMS
 *   handle     [O] handle to request queue
 *   reserved   [I] reserved, must be NULL
 *
 * RETURNS
 *   NO_ERROR if function succeeds, or error code if function fails
 *
 */
ULONG WINAPI HttpCreateHttpHandle(HANDLE *handle, ULONG reserved)
{
    OBJECT_ATTRIBUTES attr = {sizeof(attr)};
    UNICODE_STRING string;
    IO_STATUS_BLOCK iosb;

    TRACE("handle %p, reserved %#lx.\n", handle, reserved);

    if (!handle || reserved)
        return ERROR_INVALID_PARAMETER;

    RtlInitUnicodeString(&string, L"\\Device\\Http\\ReqQueue");
    attr.ObjectName = &string;
    return RtlNtStatusToDosError(NtCreateFile(handle, SYNCHRONIZE, &attr, &iosb, NULL,
            FILE_ATTRIBUTE_NORMAL, 0, FILE_OPEN, FILE_NON_DIRECTORY_FILE, NULL, 0));
}

static ULONG add_url(HANDLE queue, const WCHAR *urlW, HTTP_URL_CONTEXT context)
{
    struct http_add_url_params *params;
    ULONG ret = ERROR_SUCCESS;
    OVERLAPPED ovl = {};
    int len;

    if (!queue || !urlW)
        return ERROR_INVALID_PARAMETER;

    len = WideCharToMultiByte(CP_ACP, 0, urlW, -1, NULL, 0, NULL, NULL);
    if (!len)
        return ERROR_INVALID_PARAMETER;
    if (!(params = malloc(FIELD_OFFSET(struct http_add_url_params, url) + len)))
        return ERROR_OUTOFMEMORY;
    if (!WideCharToMultiByte(CP_ACP, 0, urlW, -1, params->url, len, NULL, NULL))
    {
        free(params);
        return ERROR_INVALID_PARAMETER;
    }
    params->context = context;

    ovl.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ovl.hEvent)
    {
        free(params);
        return GetLastError();
    }
    ovl.hEvent = (HANDLE)((ULONG_PTR)ovl.hEvent | 1);

    if (!DeviceIoControl(queue, IOCTL_HTTP_ADD_URL, params,
            FIELD_OFFSET(struct http_add_url_params, url) + len, NULL, 0, NULL, &ovl))
        ret = GetLastError();
    CloseHandle(ovl.hEvent);
    free(params);
    return ret;
}

/***********************************************************************
 *        HttpAddUrl     (HTTPAPI.@)
 */
ULONG WINAPI HttpAddUrl(HANDLE queue, const WCHAR *url, void *reserved)
{
    TRACE("queue %p, url %s, reserved %p.\n", queue, debugstr_w(url), reserved);

    if (!url || reserved)
        return ERROR_INVALID_PARAMETER;
    return add_url(queue, url, 0);
}

static ULONG remove_url(HANDLE queue, const WCHAR *urlW)
{
    ULONG ret = ERROR_SUCCESS;
    OVERLAPPED ovl = {};
    char *url;
    int len;

    if (!queue || !urlW)
        return ERROR_INVALID_PARAMETER;

    len = WideCharToMultiByte(CP_ACP, 0, urlW, -1, NULL, 0, NULL, NULL);
    if (!len)
        return ERROR_INVALID_PARAMETER;
    if (!(url = malloc(len)))
        return ERROR_OUTOFMEMORY;
    if (!WideCharToMultiByte(CP_ACP, 0, urlW, -1, url, len, NULL, NULL))
    {
        free(url);
        return ERROR_INVALID_PARAMETER;
    }

    ovl.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ovl.hEvent)
    {
        free(url);
        return GetLastError();
    }
    ovl.hEvent = (HANDLE)((ULONG_PTR)ovl.hEvent | 1);

    if (!DeviceIoControl(queue, IOCTL_HTTP_REMOVE_URL, url, len, NULL, 0, NULL, &ovl))
        ret = GetLastError();
    CloseHandle(ovl.hEvent);
    free(url);
    return ret;
}

/***********************************************************************
 *        HttpRemoveUrl     (HTTPAPI.@)
 */
ULONG WINAPI HttpRemoveUrl(HANDLE queue, const WCHAR *url)
{
    TRACE("queue %p, url %s.\n", queue, debugstr_w(url));

    if (!queue || !url)
        return ERROR_INVALID_PARAMETER;

    return remove_url(queue, url);
}

/***********************************************************************
 *        HttpReceiveRequestEntityBody     (HTTPAPI.@)
 */
ULONG WINAPI HttpReceiveRequestEntityBody(HANDLE queue, HTTP_REQUEST_ID id, ULONG flags,
        void *buffer, ULONG size, ULONG *ret_size, OVERLAPPED *ovl)
{
    struct http_receive_body_params params =
    {
        .id = id,
        .bits = sizeof(void *) * 8,
        .flags = flags,
    };
    ULONG ret = ERROR_SUCCESS;
    ULONG local_ret_size;
    OVERLAPPED sync_ovl;

    TRACE("queue %p, id %s, flags %#lx, buffer %p, size %#lx, ret_size %p, ovl %p.\n",
            queue, wine_dbgstr_longlong(id), flags, buffer, size, ret_size, ovl);

    if (flags & ~HTTP_RECEIVE_REQUEST_ENTITY_BODY_FLAG_FILL_BUFFER)
        FIXME("Ignoring flags %#lx.\n", flags & ~HTTP_RECEIVE_REQUEST_ENTITY_BODY_FLAG_FILL_BUFFER);

    if (!ovl)
    {
        sync_ovl.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        ovl = &sync_ovl;
    }

    if (!ret_size)
        ret_size = &local_ret_size;

    if (!DeviceIoControl(queue, IOCTL_HTTP_RECEIVE_BODY, &params, sizeof(params), buffer, size, ret_size, ovl))
        ret = GetLastError();

    if (ovl == &sync_ovl)
    {
        if (ret == ERROR_IO_PENDING)
        {
            ret = ERROR_SUCCESS;
            if (!GetOverlappedResult(queue, ovl, ret_size, TRUE))
                ret = GetLastError();
        }
        CloseHandle(sync_ovl.hEvent);
    }

    return ret;
}

/***********************************************************************
 *        HttpReceiveHttpRequest     (HTTPAPI.@)
 */
ULONG WINAPI HttpReceiveHttpRequest(HANDLE queue, HTTP_REQUEST_ID id, ULONG flags,
        HTTP_REQUEST *request, ULONG size, ULONG *ret_size, OVERLAPPED *ovl)
{
    struct http_receive_request_params params =
    {
        .addr = (ULONG_PTR)request,
        .id = id,
        .flags = flags,
        .bits = sizeof(void *) * 8,
    };
    ULONG ret = ERROR_SUCCESS;
    ULONG local_ret_size;
    OVERLAPPED sync_ovl;

    TRACE("queue %p, id %s, flags %#lx, request %p, size %#lx, ret_size %p, ovl %p.\n",
            queue, wine_dbgstr_longlong(id), flags, request, size, ret_size, ovl);

    if (flags & ~HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY)
        FIXME("Ignoring flags %#lx.\n", flags & ~HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY);

    if (size < sizeof(HTTP_REQUEST_V1))
        return ERROR_INSUFFICIENT_BUFFER;

    if (!ovl)
    {
        sync_ovl.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        ovl = &sync_ovl;
    }

    if (!ret_size)
        ret_size = &local_ret_size;

    if (!DeviceIoControl(queue, IOCTL_HTTP_RECEIVE_REQUEST, &params, sizeof(params), request, size, ret_size, ovl))
        ret = GetLastError();

    if (ovl == &sync_ovl)
    {
        if (ret == ERROR_IO_PENDING)
        {
            ret = ERROR_SUCCESS;
            if (!GetOverlappedResult(queue, ovl, ret_size, TRUE))
                ret = GetLastError();
        }
        CloseHandle(sync_ovl.hEvent);
    }

    return ret;
}

static void format_date(char *buffer)
{
    static const char day_names[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char month_names[12][4] =
            {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    SYSTEMTIME date;
    GetSystemTime(&date);
    sprintf(buffer + strlen(buffer), "Date: %s, %02u %s %u %02u:%02u:%02u GMT\r\n",
            day_names[date.wDayOfWeek], date.wDay, month_names[date.wMonth - 1],
            date.wYear, date.wHour, date.wMinute, date.wSecond);
}

#define FILE_PIECE_SIZE 0x10000

struct response_sender
{
    HANDLE queue;
    HTTP_REQUEST_ID id;
    HANDLE event;
    ULONG sent;
    ULONG size;
    struct http_response *buffer;
};

static ULONG send_response_buffer(struct response_sender *sender, ULONG flags, BOOL reset, OVERLAPPED *ovl)
{
    OVERLAPPED sync_ovl = {0};
    ULONG ret = ERROR_SUCCESS;
    DWORD size = 0;

    sender->buffer->id = sender->id;
    sender->buffer->response_flags = flags;
    sender->buffer->sent = sender->sent;
    sender->buffer->reset = reset;
    if (!ovl)
    {
        sync_ovl.hEvent = (HANDLE)((ULONG_PTR)sender->event | 1);
        ovl = &sync_ovl;
    }

    if (!DeviceIoControl(sender->queue, IOCTL_HTTP_SEND_RESPONSE, sender->buffer,
            FIELD_OFFSET(struct http_response, buffer) + sender->buffer->len, NULL, 0, &size, ovl))
        ret = GetLastError();

    if (ovl == &sync_ovl)
    {
        if (ret == ERROR_IO_PENDING)
        {
            ret = ERROR_SUCCESS;
            if (!GetOverlappedResult(sender->queue, ovl, &size, TRUE))
                ret = GetLastError();
        }
        if (!ret)
            sender->sent = size;
    }
    sender->buffer->len = 0;
    return ret;
}

static void reset_response(HANDLE queue, HTTP_REQUEST_ID id)
{
    struct response_sender sender = {queue, id};
    struct http_response buffer;

    buffer.len = 0;
    sender.buffer = &buffer;
    if ((sender.event = CreateEventW(NULL, TRUE, FALSE, NULL)))
    {
        send_response_buffer(&sender, 0, TRUE, NULL);
        CloseHandle(sender.event);
    }
}

static ULONG get_file_chunk_length(const HTTP_DATA_CHUNK *chunk, ULONGLONG *length)
{
    const HTTP_BYTE_RANGE *range = &chunk->FromFileHandle.ByteRange;
    LARGE_INTEGER size;

    if (!GetFileSizeEx(chunk->FromFileHandle.FileHandle, &size))
        return GetLastError();
    if (range->StartingOffset.QuadPart > (ULONGLONG)size.QuadPart)
        return ERROR_FILE_INVALID;
    *length = size.QuadPart - range->StartingOffset.QuadPart;
    if (range->Length.QuadPart != HTTP_BYTE_RANGE_TO_EOF)
    {
        if (range->Length.QuadPart > *length)
            return ERROR_FILE_INVALID;
        *length = range->Length.QuadPart;
    }
    return ERROR_SUCCESS;
}

static ULONG get_chunk_lengths(USHORT count, const HTTP_DATA_CHUNK *chunks, ULONGLONG **file_lengths,
        ULONG *memory_len, ULONGLONG *body_len)
{
    ULONGLONG length;
    ULONG ret;
    USHORT i;

    *file_lengths = NULL;
    *memory_len = 0;
    *body_len = 0;
    for (i = 0; i < count; ++i)
    {
        if (chunks[i].DataChunkType == HttpDataChunkFromMemory)
        {
            *memory_len += chunks[i].FromMemory.BufferLength;
            *body_len += chunks[i].FromMemory.BufferLength;
            continue;
        }
        if (chunks[i].DataChunkType != HttpDataChunkFromFileHandle)
        {
            FIXME("Unhandled data chunk type %u.\n", chunks[i].DataChunkType);
            free(*file_lengths);
            return ERROR_CALL_NOT_IMPLEMENTED;
        }
        if (!*file_lengths && !(*file_lengths = calloc(count, sizeof(**file_lengths))))
            return ERROR_OUTOFMEMORY;
        if ((ret = get_file_chunk_length(&chunks[i], &length)))
        {
            free(*file_lengths);
            return ret;
        }
        (*file_lengths)[i] = length;
        *body_len += length;
    }
    return ERROR_SUCCESS;
}

static ULONG send_file_chunk(struct response_sender *sender, const HTTP_DATA_CHUNK *chunk, ULONGLONG length)
{
    ULONGLONG offset = chunk->FromFileHandle.ByteRange.StartingOffset.QuadPart;
    OVERLAPPED read_ovl;
    DWORD size, read;
    ULONG ret;

    while (length)
    {
        if (sender->buffer->len == sender->size &&
                (ret = send_response_buffer(sender, HTTP_SEND_RESPONSE_FLAG_MORE_DATA, FALSE, NULL)))
            return ret;
        size = (DWORD)min(length, sender->size - sender->buffer->len);
        memset(&read_ovl, 0, sizeof(read_ovl));
        read_ovl.Offset = (DWORD)offset;
        read_ovl.OffsetHigh = (DWORD)(offset >> 32);
        read_ovl.hEvent = (HANDLE)((ULONG_PTR)sender->event | 1);
        if (!ReadFile(chunk->FromFileHandle.FileHandle, sender->buffer->buffer + sender->buffer->len,
                size, &read, &read_ovl) &&
                (GetLastError() != ERROR_IO_PENDING ||
                 !GetOverlappedResult(chunk->FromFileHandle.FileHandle, &read_ovl, &read, TRUE)))
            return GetLastError() == ERROR_HANDLE_EOF ? ERROR_FILE_INVALID : GetLastError();
        if (!read)
            return ERROR_FILE_INVALID;
        sender->buffer->len += read;
        offset += read;
        length -= read;
    }
    return ERROR_SUCCESS;
}

static ULONG send_response_chunks(struct response_sender *sender, ULONG flags, USHORT count,
        const HTTP_DATA_CHUNK *chunks, const ULONGLONG *file_lengths, ULONG *ret_size, OVERLAPPED *ovl)
{
    ULONG ret = ERROR_SUCCESS;
    USHORT i;

    if (!(sender->event = CreateEventW(NULL, TRUE, FALSE, NULL)))
        return GetLastError();

    for (i = 0; i < count && !ret; ++i)
    {
        const HTTP_DATA_CHUNK *chunk = &chunks[i];

        if (chunk->DataChunkType == HttpDataChunkFromFileHandle)
        {
            if ((ret = send_file_chunk(sender, chunk, file_lengths[i])) && ret != ERROR_CONNECTION_INVALID)
                send_response_buffer(sender, 0, TRUE, NULL);
            continue;
        }
        if (chunk->FromMemory.BufferLength > sender->size - sender->buffer->len)
            ret = send_response_buffer(sender, HTTP_SEND_RESPONSE_FLAG_MORE_DATA, FALSE, NULL);
        if (!ret)
        {
            memcpy(sender->buffer->buffer + sender->buffer->len, chunk->FromMemory.pBuffer,
                    chunk->FromMemory.BufferLength);
            sender->buffer->len += chunk->FromMemory.BufferLength;
        }
    }

    if (!ret)
        ret = send_response_buffer(sender, flags, FALSE, ovl);
    if (ret_size && !ovl)
        *ret_size = ret ? 0 : sender->sent;

    CloseHandle(sender->event);
    return ret;
}

/***********************************************************************
 *        HttpSendHttpResponse     (HTTPAPI.@)
 */
ULONG WINAPI HttpSendHttpResponse(HANDLE queue, HTTP_REQUEST_ID id, ULONG flags,
        HTTP_RESPONSE *response, HTTP_CACHE_POLICY *cache_policy, ULONG *ret_size,
        void *reserved1, ULONG reserved2, OVERLAPPED *ovl, HTTP_LOG_DATA *log_data)
{
    static const char *const header_names[] =
    {
        "Cache-Control",
        "Connection",
        "Date",
        "Keep-Alive",
        "Pragma",
        "Trailer",
        "Transfer-Encoding",
        "Upgrade",
        "Via",
        "Warning",
        "Allow",
        "Content-Length",
        "Content-Type",
        "Content-Encoding",
        "Content-Language",
        "Content-Location",
        "Content-MD5",
        "Content-Range",
        "Expires",
        "Last-Modified",
        "Accept-Ranges",
        "Age",
        "ETag",
        "Location",
        "Proxy-Authenticate",
        "Retry-After",
        "Server",
        "Set-Cookie",
        "Vary",
        "WWW-Authenticate",
    };

    struct response_sender sender = {queue, id};
    struct http_response *buffer;
    ULONGLONG *file_lengths, body_len;
    ULONG ret, memory_len;
    char *p, dummy[21];
    int len;
    USHORT i;

    TRACE("queue %p, id %s, flags %#lx, response %p, cache_policy %p, "
            "ret_size %p, reserved1 %p, reserved2 %#lx, ovl %p, log_data %p.\n",
            queue, wine_dbgstr_longlong(id), flags, response, cache_policy,
            ret_size, reserved1, reserved2, ovl, log_data);

    if (flags & ~(HTTP_SEND_RESPONSE_FLAG_MORE_DATA | HTTP_SEND_RESPONSE_FLAG_DISCONNECT))
        FIXME("Unhandled flags %#lx.\n",
              flags & ~(HTTP_SEND_RESPONSE_FLAG_MORE_DATA | HTTP_SEND_RESPONSE_FLAG_DISCONNECT));
    if (response->s.Flags)
        FIXME("Unhandled response flags %#lx.\n", response->s.Flags);
    if (cache_policy)
        WARN("Ignoring cache_policy.\n");
    if (log_data)
        WARN("Ignoring log_data.\n");

    if ((ret = get_chunk_lengths(response->s.EntityChunkCount, response->s.pEntityChunks,
            &file_lengths, &memory_len, &body_len)))
    {
        if (ret != ERROR_CALL_NOT_IMPLEMENTED && ret != ERROR_OUTOFMEMORY)
            reset_response(queue, id);
        if (ret_size)
            *ret_size = 0;
        return ret;
    }

    len = 12 + sprintf(dummy, "%hu", response->s.StatusCode) + response->s.ReasonLength;
    for (i = 0; i < HttpHeaderResponseMaximum; ++i)
    {
        if (i == HttpHeaderDate)
            len += 37;
        else if (response->s.Headers.KnownHeaders[i].RawValueLength)
            len += strlen(header_names[i]) + 2 + response->s.Headers.KnownHeaders[i].RawValueLength + 2;
        else if (i == HttpHeaderContentLength && !(flags & HTTP_SEND_RESPONSE_FLAG_MORE_DATA))
            len += strlen(header_names[i]) + 2 + sprintf(dummy, "%I64u", body_len) + 2;
    }
    for (i = 0; i < response->s.Headers.UnknownHeaderCount; ++i)
    {
        len += response->s.Headers.pUnknownHeaders[i].NameLength + 2;
        len += response->s.Headers.pUnknownHeaders[i].RawValueLength + 2;
    }
    len += 2;

    sender.size = len + memory_len;
    if (file_lengths)
        sender.size = max(sender.size, FILE_PIECE_SIZE);
    if (!(buffer = malloc(FIELD_OFFSET(struct http_response, buffer) + sender.size)))
    {
        free(file_lengths);
        return ERROR_OUTOFMEMORY;
    }
    buffer->len = len;
    sprintf(buffer->buffer, "HTTP/1.1 %u %.*s\r\n", response->s.StatusCode,
            response->s.ReasonLength, response->s.pReason);

    for (i = 0; i < HttpHeaderResponseMaximum; ++i)
    {
        const HTTP_KNOWN_HEADER *header = &response->s.Headers.KnownHeaders[i];
        if (i == HttpHeaderDate)
            format_date(buffer->buffer);
        else if (header->RawValueLength)
            sprintf(buffer->buffer + strlen(buffer->buffer), "%s: %.*s\r\n",
                    header_names[i], header->RawValueLength, header->pRawValue);
        else if (i == HttpHeaderContentLength && !(flags & HTTP_SEND_RESPONSE_FLAG_MORE_DATA))
            sprintf(buffer->buffer + strlen(buffer->buffer), "Content-Length: %I64u\r\n", body_len);
    }
    for (i = 0; i < response->s.Headers.UnknownHeaderCount; ++i)
    {
        const HTTP_UNKNOWN_HEADER *header = &response->s.Headers.pUnknownHeaders[i];
        sprintf(buffer->buffer + strlen(buffer->buffer), "%.*s: %.*s\r\n", header->NameLength,
                header->pName, header->RawValueLength, header->pRawValue);
    }
    p = buffer->buffer + strlen(buffer->buffer);
    /* Don't use strcat, because this might be the end of the buffer. */
    memcpy(p, "\r\n", 2);

    sender.buffer = buffer;
    ret = send_response_chunks(&sender, flags, response->s.EntityChunkCount, response->s.pEntityChunks,
            file_lengths, ret_size, ovl);

    free(file_lengths);
    free(buffer);
    return ret;
}

/***********************************************************************
 *        HttpSendResponseEntityBody     (HTTPAPI.@)
 *
 * Sends entity-body data for a response.
 *
 * PARAMS
 *   queue              [I] The request queue handle
 *   id                 [I] The ID of the request to which this response corresponds
 *   flags              [I] Flags to control the response
 *   entity_chunk_count [I] The number of entities pointed to by entity_chunks
 *   entity_chunks      [I] The entities to be sent
 *   ret_size           [O] The number of bytes sent
 *   reserved1          [I] Reserved, must be NULL
 *   reserved2          [I] Reserved, must be zero
 *   ovl                [I] Must be set to an OVERLAP pointer when making async calls
 *   log_data           [I] Optional log data structure for logging the call
 *
 * RETURNS
 *   NO_ERROR on success, or an error code on failure.
 */
ULONG WINAPI HttpSendResponseEntityBody(HANDLE queue, HTTP_REQUEST_ID id,
       ULONG flags, USHORT entity_chunk_count, PHTTP_DATA_CHUNK entity_chunks,
       ULONG *ret_size, void *reserved1, ULONG reserved2, OVERLAPPED *ovl,
       HTTP_LOG_DATA *log_data)
{
    struct response_sender sender = {queue, id};
    ULONGLONG *file_lengths, body_len;
    ULONG ret, memory_len;

    TRACE("queue %p, id %s, flags %#lx, entity_chunk_count %u, entity_chunks %p, "
            "ret_size %p, reserved1 %p, reserved2 %#lx, ovl %p, log_data %p\n",
            queue, wine_dbgstr_longlong(id), flags, entity_chunk_count, entity_chunks,
            ret_size, reserved1, reserved2, ovl, log_data);

    if (!id)
        return ERROR_CONNECTION_INVALID;

    if (flags & ~(HTTP_SEND_RESPONSE_FLAG_MORE_DATA | HTTP_SEND_RESPONSE_FLAG_DISCONNECT))
        FIXME("Unhandled flags %#lx.\n",
              flags & ~(HTTP_SEND_RESPONSE_FLAG_MORE_DATA | HTTP_SEND_RESPONSE_FLAG_DISCONNECT));
    if (log_data)
        WARN("Ignoring log_data.\n");

    if ((ret = get_chunk_lengths(entity_chunk_count, entity_chunks, &file_lengths, &memory_len, &body_len)))
    {
        if (ret != ERROR_CALL_NOT_IMPLEMENTED && ret != ERROR_OUTOFMEMORY)
            reset_response(queue, id);
        if (ret_size)
            *ret_size = 0;
        return ret;
    }

    sender.size = memory_len;
    if (file_lengths)
        sender.size = max(sender.size, FILE_PIECE_SIZE);
    if (!(sender.buffer = malloc(FIELD_OFFSET(struct http_response, buffer) + sender.size)))
    {
        free(file_lengths);
        return ERROR_OUTOFMEMORY;
    }
    sender.buffer->len = 0;
    ret = send_response_chunks(&sender, flags, entity_chunk_count, entity_chunks, file_lengths, ret_size, ovl);

    free(file_lengths);
    free(sender.buffer);
    return ret;
}

struct url_group
{
    struct list entry, session_entry;
    HANDLE queue;
    struct list urls;
};

struct url_registration
{
    struct list entry;
    WCHAR *url;
    HTTP_URL_CONTEXT context;
    BOOLEAN old_removed;
    BOOLEAN new_added;
};

static struct list url_groups = LIST_INIT(url_groups);
static SRWLOCK url_group_lock = SRWLOCK_INIT;

static struct url_group *get_url_group(HTTP_URL_GROUP_ID id)
{
    struct url_group *group;
    LIST_FOR_EACH_ENTRY(group, &url_groups, struct url_group, entry)
    {
        if ((HTTP_URL_GROUP_ID)(ULONG_PTR)group == id)
            return group;
    }
    return NULL;
}

struct server_session
{
    struct list entry;
    struct list groups;
};

static struct list server_sessions = LIST_INIT(server_sessions);

static struct server_session *get_server_session(HTTP_SERVER_SESSION_ID id)
{
    struct server_session *session;
    LIST_FOR_EACH_ENTRY(session, &server_sessions, struct server_session, entry)
    {
        if ((HTTP_SERVER_SESSION_ID)(ULONG_PTR)session == id)
            return session;
    }
    return NULL;
}

/***********************************************************************
 *        HttpCreateServerSession     (HTTPAPI.@)
 */
ULONG WINAPI HttpCreateServerSession(HTTPAPI_VERSION version, HTTP_SERVER_SESSION_ID *id, ULONG reserved)
{
    struct server_session *session;

    TRACE("version %u.%u, id %p, reserved %lu.\n", version.HttpApiMajorVersion,
            version.HttpApiMinorVersion, id, reserved);

    if ((version.HttpApiMajorVersion != 1 && version.HttpApiMajorVersion != 2) ||
        version.HttpApiMinorVersion)
        return ERROR_REVISION_MISMATCH;
    if (!id || reserved)
        return ERROR_INVALID_PARAMETER;

    if (!(session = malloc(sizeof(*session))))
        return ERROR_OUTOFMEMORY;

    AcquireSRWLockExclusive(&url_group_lock);
    list_add_tail(&server_sessions, &session->entry);
    list_init(&session->groups);
    ReleaseSRWLockExclusive(&url_group_lock);

    *id = (ULONG_PTR)session;
    return ERROR_SUCCESS;
}

/***********************************************************************
 *        HttpCloseServerSession     (HTTPAPI.@)
 */
static ULONG close_url_group(struct url_group *group)
{
    struct url_registration *registration, *registration_next;
    ULONG ret;

    LIST_FOR_EACH_ENTRY_SAFE(registration, registration_next, &group->urls,
            struct url_registration, entry)
    {
        if (group->queue && (ret = remove_url(group->queue, registration->url)))
            return ret;
        list_remove(&registration->entry);
        free(registration->url);
        free(registration);
    }

    list_remove(&group->session_entry);
    list_remove(&group->entry);
    free(group);
    return ERROR_SUCCESS;
}

ULONG WINAPI HttpCloseServerSession(HTTP_SERVER_SESSION_ID id)
{
    struct server_session *session;
    ULONG ret = ERROR_SUCCESS;

    TRACE("id %s.\n", wine_dbgstr_longlong(id));

    AcquireSRWLockExclusive(&url_group_lock);
    if (!(session = get_server_session(id)))
    {
        ReleaseSRWLockExclusive(&url_group_lock);
        return ERROR_INVALID_PARAMETER;
    }

    while (!list_empty(&session->groups))
    {
        struct url_group *group = LIST_ENTRY(session->groups.next, struct url_group, session_entry);
        if ((ret = close_url_group(group)))
        {
            ReleaseSRWLockExclusive(&url_group_lock);
            return ret;
        }
    }

    list_remove(&session->entry);
    free(session);
    ReleaseSRWLockExclusive(&url_group_lock);
    return ERROR_SUCCESS;
}

/***********************************************************************
 *        HttpCreateUrlGroup     (HTTPAPI.@)
 */
ULONG WINAPI HttpCreateUrlGroup(HTTP_SERVER_SESSION_ID session_id, HTTP_URL_GROUP_ID *group_id, ULONG reserved)
{
    struct server_session *session;
    struct url_group *group;

    TRACE("session_id %s, group_id %p, reserved %#lx.\n",
          wine_dbgstr_longlong(session_id), group_id, reserved);

    if (!group_id || reserved)
        return ERROR_INVALID_PARAMETER;

    AcquireSRWLockExclusive(&url_group_lock);
    if (!(session = get_server_session(session_id)))
    {
        ReleaseSRWLockExclusive(&url_group_lock);
        return ERROR_INVALID_PARAMETER;
    }

    if (!(group = calloc(1, sizeof(*group))))
    {
        ReleaseSRWLockExclusive(&url_group_lock);
        return ERROR_OUTOFMEMORY;
    }
    list_add_tail(&url_groups, &group->entry);
    list_add_tail(&session->groups, &group->session_entry);
    list_init(&group->urls);

    *group_id = (ULONG_PTR)group;

    ReleaseSRWLockExclusive(&url_group_lock);
    return ERROR_SUCCESS;
}

/***********************************************************************
 *        HttpCloseUrlGroup     (HTTPAPI.@)
 */
ULONG WINAPI HttpCloseUrlGroup(HTTP_URL_GROUP_ID id)
{
    struct url_group *group;
    ULONG ret;

    TRACE("id %s.\n", wine_dbgstr_longlong(id));

    AcquireSRWLockExclusive(&url_group_lock);
    if (!(group = get_url_group(id)))
    {
        ReleaseSRWLockExclusive(&url_group_lock);
        return ERROR_INVALID_PARAMETER;
    }

    ret = close_url_group(group);
    ReleaseSRWLockExclusive(&url_group_lock);
    return ret;
}

/***********************************************************************
 *        HttpSetUrlGroupProperty     (HTTPAPI.@)
 */
ULONG WINAPI HttpSetUrlGroupProperty(HTTP_URL_GROUP_ID id, HTTP_SERVER_PROPERTY property, void *value, ULONG length)
{
    struct url_registration *registration;
    struct url_group *group;
    const HTTP_BINDING_INFO *info;
    HANDLE new_queue;
    ULONG ret = ERROR_SUCCESS;

    TRACE("id %s, property %u, value %p, length %lu.\n",
            wine_dbgstr_longlong(id), property, value, length);

    AcquireSRWLockExclusive(&url_group_lock);
    if (!(group = get_url_group(id)))
    {
        ReleaseSRWLockExclusive(&url_group_lock);
        return ERROR_INVALID_PARAMETER;
    }

    switch (property)
    {
        case HttpServerBindingProperty:
        {
            if (!value || length < sizeof(*info))
            {
                ReleaseSRWLockExclusive(&url_group_lock);
                return ERROR_INVALID_PARAMETER;
            }

            info = value;
            new_queue = info->Flags.Present ? info->RequestQueueHandle : NULL;
            if (info->Flags.Present && !new_queue)
            {
                ReleaseSRWLockExclusive(&url_group_lock);
                return ERROR_INVALID_PARAMETER;
            }
            TRACE("Binding to queue %p.\n", info->RequestQueueHandle);
            if (new_queue == group->queue)
            {
                ReleaseSRWLockExclusive(&url_group_lock);
                return ERROR_SUCCESS;
            }

            LIST_FOR_EACH_ENTRY(registration, &group->urls, struct url_registration, entry)
            {
                if (new_queue && (ret = add_url(new_queue, registration->url, registration->context)))
                    break;
                registration->new_added = !!new_queue;
            }

            if (ret)
            {
                LIST_FOR_EACH_ENTRY(registration, &group->urls, struct url_registration, entry)
                {
                    if (registration->new_added)
                    {
                        remove_url(new_queue, registration->url);
                        registration->new_added = FALSE;
                    }
                }
                ReleaseSRWLockExclusive(&url_group_lock);
                return ret;
            }

            LIST_FOR_EACH_ENTRY(registration, &group->urls, struct url_registration, entry)
            {
                if (group->queue && (ret = remove_url(group->queue, registration->url)))
                    break;
                registration->old_removed = !!group->queue;
            }

            if (ret)
            {
                LIST_FOR_EACH_ENTRY(registration, &group->urls, struct url_registration, entry)
                {
                    if (registration->old_removed)
                    {
                        add_url(group->queue, registration->url, registration->context);
                        registration->old_removed = FALSE;
                    }
                    if (registration->new_added)
                    {
                        remove_url(new_queue, registration->url);
                        registration->new_added = FALSE;
                    }
                }
                ReleaseSRWLockExclusive(&url_group_lock);
                return ret;
            }

            group->queue = new_queue;
            LIST_FOR_EACH_ENTRY(registration, &group->urls, struct url_registration, entry)
            {
                registration->new_added = FALSE;
                registration->old_removed = FALSE;
            }
            ReleaseSRWLockExclusive(&url_group_lock);
            return ERROR_SUCCESS;
        }
        case HttpServerLoggingProperty:
            WARN("Ignoring logging property.\n");
            ReleaseSRWLockExclusive(&url_group_lock);
            return ERROR_CALL_NOT_IMPLEMENTED;
        default:
            FIXME("Unhandled property %u.\n", property);
            ReleaseSRWLockExclusive(&url_group_lock);
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

/***********************************************************************
 *        HttpAddUrlToUrlGroup     (HTTPAPI.@)
 */
ULONG WINAPI HttpAddUrlToUrlGroup(HTTP_URL_GROUP_ID id, const WCHAR *url,
        HTTP_URL_CONTEXT context, ULONG reserved)
{
    struct url_registration *registration, *existing;
    struct url_group *group;
    ULONG ret;

    TRACE("id %s, url %s, context %s, reserved %#lx.\n", wine_dbgstr_longlong(id),
            debugstr_w(url), wine_dbgstr_longlong(context), reserved);

    if (!url || reserved)
        return ERROR_INVALID_PARAMETER;

    AcquireSRWLockExclusive(&url_group_lock);
    if (!(group = get_url_group(id)))
    {
        ReleaseSRWLockExclusive(&url_group_lock);
        return ERROR_INVALID_PARAMETER;
    }

    LIST_FOR_EACH_ENTRY(existing, &group->urls, struct url_registration, entry)
    {
        if (!_wcsicmp(existing->url, url))
        {
            ReleaseSRWLockExclusive(&url_group_lock);
            return ERROR_ALREADY_EXISTS;
        }
    }

    if (!(registration = calloc(1, sizeof(*registration))) ||
        !(registration->url = wcsdup(url)))
    {
        free(registration);
        ReleaseSRWLockExclusive(&url_group_lock);
        return ERROR_OUTOFMEMORY;
    }
    registration->context = context;

    if (group->queue)
    {
        ret = add_url(group->queue, registration->url, context);
        if (ret)
        {
            free(registration->url);
            free(registration);
            ReleaseSRWLockExclusive(&url_group_lock);
            return ret;
        }
    }

    list_add_tail(&group->urls, &registration->entry);
    ReleaseSRWLockExclusive(&url_group_lock);
    return ERROR_SUCCESS;
}

/***********************************************************************
 *        HttpRemoveUrlFromUrlGroup     (HTTPAPI.@)
 */
ULONG WINAPI HttpRemoveUrlFromUrlGroup(HTTP_URL_GROUP_ID id, const WCHAR *url, ULONG flags)
{
    struct url_registration *registration, *registration_next;
    struct url_group *group;
    ULONG ret;

    TRACE("id %s, url %s, flags %#lx.\n", wine_dbgstr_longlong(id), debugstr_w(url), flags);

    if ((flags & ~HTTP_URL_FLAG_REMOVE_ALL) ||
        ((flags & HTTP_URL_FLAG_REMOVE_ALL) ? url != NULL : url == NULL))
        return ERROR_INVALID_PARAMETER;

    AcquireSRWLockExclusive(&url_group_lock);
    if (!(group = get_url_group(id)))
    {
        ReleaseSRWLockExclusive(&url_group_lock);
        return ERROR_INVALID_PARAMETER;
    }

    if (flags & HTTP_URL_FLAG_REMOVE_ALL)
    {
        LIST_FOR_EACH_ENTRY_SAFE(registration, registration_next, &group->urls,
                struct url_registration, entry)
        {
            if (group->queue && (ret = remove_url(group->queue, registration->url)))
            {
                ReleaseSRWLockExclusive(&url_group_lock);
                return ret;
            }
            list_remove(&registration->entry);
            free(registration->url);
            free(registration);
        }
        ReleaseSRWLockExclusive(&url_group_lock);
        return ERROR_SUCCESS;
    }

    LIST_FOR_EACH_ENTRY(registration, &group->urls, struct url_registration, entry)
    {
        if (!_wcsicmp(registration->url, url))
        {
            if (group->queue && (ret = remove_url(group->queue, registration->url)))
            {
                ReleaseSRWLockExclusive(&url_group_lock);
                return ret;
            }
            list_remove(&registration->entry);
            free(registration->url);
            free(registration);
            ReleaseSRWLockExclusive(&url_group_lock);
            return ERROR_SUCCESS;
        }
    }

    ReleaseSRWLockExclusive(&url_group_lock);
    return ERROR_FILE_NOT_FOUND;
}

/***********************************************************************
 *        HttpCreateRequestQueue     (HTTPAPI.@)
 */
ULONG WINAPI HttpCreateRequestQueue(HTTPAPI_VERSION version, const WCHAR *name,
        SECURITY_ATTRIBUTES *sa, ULONG flags, HANDLE *handle)
{
    OBJECT_ATTRIBUTES attr = {sizeof(attr)};
    UNICODE_STRING string;
    IO_STATUS_BLOCK iosb;

    TRACE("version %u.%u, name %s, sa %p, flags %#lx, handle %p.\n",
            version.HttpApiMajorVersion, version.HttpApiMinorVersion,
            debugstr_w(name), sa, flags, handle);

    if ((version.HttpApiMajorVersion != 1 && version.HttpApiMajorVersion != 2) ||
        version.HttpApiMinorVersion)
        return ERROR_REVISION_MISMATCH;
    if (!handle || (flags & ~(HTTP_CREATE_REQUEST_QUEUE_FLAG_OPEN_EXISTING |
                              HTTP_CREATE_REQUEST_QUEUE_FLAG_CONTROLLER)))
        return ERROR_INVALID_PARAMETER;
    if (name && wcslen(name) > MAX_PATH / sizeof(WCHAR))
        return ERROR_INVALID_PARAMETER;
    if ((flags & HTTP_CREATE_REQUEST_QUEUE_FLAG_OPEN_EXISTING) && (!name || sa))
        return ERROR_INVALID_PARAMETER;
    if (name || flags)
        return ERROR_CALL_NOT_IMPLEMENTED;

    RtlInitUnicodeString(&string, L"\\Device\\Http\\ReqQueue");
    attr.ObjectName = &string;
    if (sa && sa->bInheritHandle)
        attr.Attributes |= OBJ_INHERIT;
    attr.SecurityDescriptor = sa ? sa->lpSecurityDescriptor : NULL;
    return RtlNtStatusToDosError(NtCreateFile(handle, SYNCHRONIZE, &attr, &iosb, NULL,
            FILE_ATTRIBUTE_NORMAL, 0, FILE_OPEN, FILE_NON_DIRECTORY_FILE, NULL, 0));
}

/***********************************************************************
 *        HttpCloseRequestQueue     (HTTPAPI.@)
 */
ULONG WINAPI HttpCloseRequestQueue(HANDLE handle)
{
    TRACE("handle %p.\n", handle);
    if (!CloseHandle(handle))
        return GetLastError();
    return ERROR_SUCCESS;
}

/***********************************************************************
 *        HttpSetRequestQueueProperty     (HTTPAPI.@)
 */
ULONG WINAPI HttpSetRequestQueueProperty(HANDLE queue, HTTP_SERVER_PROPERTY property,
        void *value, ULONG length, ULONG reserved1, void *reserved2)
{
    FIXME("queue %p, property %u, value %p, length %lu, reserved1 %#lx, reserved2 %p, stub!\n",
            queue, property, value, length, reserved1, reserved2);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/***********************************************************************
 *        HttpSetServerSessionProperty     (HTTPAPI.@)
 */
ULONG WINAPI HttpSetServerSessionProperty(HTTP_SERVER_SESSION_ID id,
        HTTP_SERVER_PROPERTY property, void *value, ULONG length)
{
    struct server_session *session;

    TRACE("id %s, property %u, value %p, length %lu.\n",
            wine_dbgstr_longlong(id), property, value, length);

    AcquireSRWLockExclusive(&url_group_lock);
    session = get_server_session(id);
    ReleaseSRWLockExclusive(&url_group_lock);
    if (!session)
        return ERROR_INVALID_PARAMETER;

    switch (property)
    {
        case HttpServerQosProperty:
        {
            if (!value || length < sizeof(HTTP_QOS_SETTING_INFO))
                return ERROR_INVALID_PARAMETER;
            return ERROR_CALL_NOT_IMPLEMENTED;
        }
        default:
            FIXME("Unhandled property %u.\n", property);
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}
