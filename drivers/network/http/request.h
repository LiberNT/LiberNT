/*
 * PROJECT:     LiberNT HTTP.sys driver
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Request serialization for 32-bit and 64-bit HTTP_REQUEST structures
 * COPYRIGHT:   Copyright 2019 Zebediah Figura
 *              Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

struct http_unknown_header
{
    USHORT NameLength;
    USHORT RawValueLength;
    POINTER pName;
    POINTER pRawValue;
};

struct http_data_chunk
{
    HTTP_DATA_CHUNK_TYPE DataChunkType;
    union
    {
        struct
        {
            POINTER pBuffer;
            ULONG BufferLength;
        } FromMemory;
        struct
        {
            ULARGE_INTEGER StartingOffset;
            ULARGE_INTEGER Length;
            POINTER FileHandle;
        } FromFileHandle;
    };
};

struct http_request
{
    ULONG Flags;
    HTTP_CONNECTION_ID ConnectionId;
    HTTP_REQUEST_ID RequestId;
    HTTP_URL_CONTEXT UrlContext;
    HTTP_VERSION Version;
    HTTP_VERB Verb;
    USHORT UnknownVerbLength;
    USHORT RawUrlLength;
    POINTER pUnknownVerb;
    POINTER pRawUrl;
    struct
    {
        USHORT FullUrlLength;
        USHORT HostLength;
        USHORT AbsPathLength;
        USHORT QueryStringLength;
        POINTER pFullUrl;
        POINTER pHost;
        POINTER pAbsPath;
        POINTER pQueryString;
    } CookedUrl;
    struct
    {
        POINTER pRemoteAddress;
        POINTER pLocalAddress;
    } Address;
    struct
    {
        USHORT UnknownHeaderCount;
        POINTER pUnknownHeaders;
        USHORT TrailerCount;
        POINTER pTrailers;
        struct
        {
            USHORT RawValueLength;
            POINTER pRawValue;
        } KnownHeaders[HttpHeaderRequestMaximum];
    } Headers;
    ULONGLONG BytesReceived;
    USHORT EntityChunkCount;
    POINTER pEntityChunks;
    HTTP_RAW_CONNECTION_ID RawConnectionId;
    POINTER pSslInfo;
    USHORT RequestInfoCount;
    POINTER pRequestInfo;
};

static NTSTATUS complete_irp(PHTTP_CONNECTION conn, PIRP irp)
{
    const struct http_receive_request_params params
            = *(struct http_receive_request_params *)irp->AssociatedIrp.SystemBuffer;
    ULONG cooked_len, host_len, abs_path_len, query_len, chunk_len = 0, offset, processed;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    const ULONG output_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    struct http_request *req = (struct http_request *)irp->AssociatedIrp.SystemBuffer;
    const char *p, *host, *abs_path, *query;
    struct http_unknown_header *unk_headers = NULL;
    char *buffer = (char *)irp->AssociatedIrp.SystemBuffer;
    ULONG irp_size = sizeof(struct http_request);
    USHORT unk_headers_count = 0, unk_header_idx;
    struct http_data_chunk *chunk = NULL;
    struct sockaddr_in addr;
    ULONG i;

    if (conn->UnknownVerbLength)
        irp_size += conn->UnknownVerbLength + 1;
    irp_size += conn->UrlLength + 1;

    p = host = conn->Host;
    while (p < conn->Host + conn->HostLength && *p != ' ' && *p != '\t') ++p;
    host_len = (ULONG)(p - conn->Host);
    abs_path = conn->Url;
    abs_path_len = conn->UrlLength;

    query = (const char *)memchr(abs_path, '?', abs_path_len);
    if (query)
    {
        query_len = (ULONG)((abs_path + abs_path_len) - query);
        abs_path_len = (ULONG)(query - abs_path);
    }
    else
    {
        query_len = 0;
    }

    cooked_len = (ULONG)((7 + host_len + abs_path_len + query_len) * sizeof(WCHAR));
    irp_size += cooked_len + sizeof(WCHAR);

    irp_size += 2 * sizeof(addr);

    for (i = 0; i < conn->HeaderCount; ++i)
    {
        const HTTP_PARSED_HEADER *header = &conn->Headers[i];
        if (header->Id == HttpHeaderRequestMaximum)
        {
            irp_size += header->NameLength + 1;
            ++unk_headers_count;
        }
        irp_size += header->ValueLength + 1;
    }

    irp_size += unk_headers_count * sizeof(struct http_unknown_header);

    irp->IoStatus.Information = irp_size;
    memset(irp->AssociatedIrp.SystemBuffer, 0, output_len);

    if (output_len < irp_size)
    {
        req->ConnectionId = conn->ConnectionId;
        req->RequestId = conn->RequestId;
        return STATUS_BUFFER_OVERFLOW;
    }

    offset = sizeof(*req);

    req->UrlContext = conn->Context;
    req->ConnectionId = conn->ConnectionId;
    req->RequestId = conn->RequestId;
    req->Version = conn->Version;
    req->Verb = conn->Verb;
    req->UnknownVerbLength = (USHORT)conn->UnknownVerbLength;
    req->RawUrlLength = (USHORT)conn->UrlLength;

    if (conn->UnknownVerbLength)
    {
        req->pUnknownVerb = (POINTER)(params.addr + offset);
        memcpy(buffer + offset, conn->Buffer + conn->MethodOffset, conn->UnknownVerbLength);
        offset += conn->UnknownVerbLength;
        buffer[offset++] = 0;
    }

    req->pRawUrl = (POINTER)(params.addr + offset);
    memcpy(buffer + offset, conn->Url, conn->UrlLength);
    offset += conn->UrlLength;
    buffer[offset++] = 0;

    req->CookedUrl.FullUrlLength = (USHORT)cooked_len;
    req->CookedUrl.HostLength = (USHORT)(host_len * sizeof(WCHAR));
    req->CookedUrl.AbsPathLength = (USHORT)(abs_path_len * sizeof(WCHAR));
    req->CookedUrl.QueryStringLength = (USHORT)(query_len * sizeof(WCHAR));
    req->CookedUrl.pFullUrl = (POINTER)(params.addr + offset);
    req->CookedUrl.pHost = (POINTER)(req->CookedUrl.pFullUrl + 7 * sizeof(WCHAR));
    req->CookedUrl.pAbsPath = (POINTER)(req->CookedUrl.pHost + host_len * sizeof(WCHAR));
    if (query)
        req->CookedUrl.pQueryString = (POINTER)(req->CookedUrl.pAbsPath + abs_path_len * sizeof(WCHAR));

    memcpy(buffer + offset, L"http://", 7 * sizeof(WCHAR));
    offset += 7 * sizeof(WCHAR);

    for (i = 0; i < host_len; ++i)
        ((WCHAR *)(buffer + offset))[i] = (WCHAR)(UCHAR)host[i];
    offset += host_len * sizeof(WCHAR);

    for (i = 0; i < abs_path_len + query_len; ++i)
        ((WCHAR *)(buffer + offset))[i] = (WCHAR)(UCHAR)abs_path[i];
    offset += (abs_path_len + query_len) * sizeof(WCHAR);
    buffer[offset++] = 0;
    buffer[offset++] = 0;

    req->Address.pRemoteAddress = (POINTER)(params.addr + offset);
    memcpy(buffer + offset, &conn->RemoteAddress, sizeof(struct sockaddr_in));
    offset += sizeof(struct sockaddr_in);

    req->Address.pLocalAddress = (POINTER)(params.addr + offset);
    memcpy(buffer + offset, &conn->LocalAddress, sizeof(struct sockaddr_in));
    offset += sizeof(struct sockaddr_in);

    req->Headers.UnknownHeaderCount = unk_headers_count;
    if (unk_headers_count)
    {
        req->Headers.pUnknownHeaders = (POINTER)(params.addr + offset);
        unk_headers = (struct http_unknown_header *)(buffer + offset);
        offset += unk_headers_count * sizeof(*unk_headers);
    }

    unk_header_idx = 0;
    for (i = 0; i < conn->HeaderCount; ++i)
    {
        const HTTP_PARSED_HEADER *header = &conn->Headers[i];
        const char *name = conn->Buffer + header->NameOffset;
        const char *value = conn->Buffer + header->ValueOffset;

        if (header->Id == HttpHeaderRequestMaximum)
        {
            unk_headers[unk_header_idx].NameLength = (USHORT)header->NameLength;
            unk_headers[unk_header_idx].RawValueLength = (USHORT)header->ValueLength;
            unk_headers[unk_header_idx].pName = (POINTER)(params.addr + offset);
            memcpy(buffer + offset, name, header->NameLength);
            offset += header->NameLength;
            buffer[offset++] = 0;
            unk_headers[unk_header_idx].pRawValue = (POINTER)(params.addr + offset);
            memcpy(buffer + offset, value, header->ValueLength);
            offset += header->ValueLength;
            buffer[offset++] = 0;
            ++unk_header_idx;
        }
        else
        {
            req->Headers.KnownHeaders[header->Id].RawValueLength = (USHORT)header->ValueLength;
            req->Headers.KnownHeaders[header->Id].pRawValue = (POINTER)(params.addr + offset);
            memcpy(buffer + offset, value, header->ValueLength);
            offset += header->ValueLength;
            buffer[offset++] = 0;
        }
    }
    p = conn->Buffer + conn->HeaderLength;

    if (irp_size + sizeof(*chunk) < output_len && (params.flags & HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY))
        chunk_len = min(conn->ContentLength, output_len - (irp_size + (ULONG)sizeof(*chunk)));
    if (chunk_len)
    {
        req->EntityChunkCount = 1;
        req->pEntityChunks = (POINTER)(params.addr + offset);
        chunk = (struct http_data_chunk *)(buffer + offset);
        offset += sizeof(*chunk);
        chunk->DataChunkType = HttpDataChunkFromMemory;
        chunk->FromMemory.BufferLength = chunk_len;
        chunk->FromMemory.pBuffer = (POINTER)(params.addr + offset);
        memcpy(buffer + offset, p, chunk_len);
        offset += chunk_len;

        irp->IoStatus.Information = irp_size + sizeof(*chunk) + chunk_len;
    }

    if (chunk_len < conn->ContentLength)
        req->Flags |= HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS;

    req->BytesReceived = conn->RequestLength;

    conn->Available = FALSE;
    processed = conn->RequestLength - (conn->ContentLength - chunk_len);
    RtlMoveMemory(conn->Buffer, conn->Buffer + processed, conn->BufferLength - processed);
    conn->ContentLength -= chunk_len;
    conn->BufferLength -= processed;

    return STATUS_SUCCESS;
}
