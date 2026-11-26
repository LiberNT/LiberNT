/*
 * HTTP Server API definitions
 *
 * Copyright (C) 2009 Andrey Turkin
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

#ifndef __WINE_HTTP_H
#define __WINE_HTTP_H

#if !defined(_NTDDK_) && !defined(_NTIFS_)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <ws2def.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _HTTPAPI_VERSION
{
    USHORT HttpApiMajorVersion;
    USHORT HttpApiMinorVersion;
} HTTPAPI_VERSION, *PHTTPAPI_VERSION;

#define HTTPAPI_VERSION_1 {1,0}
#define HTTPAPI_VERSION_2 {2,0}

#define HTTP_CREATE_REQUEST_QUEUE_FLAG_OPEN_EXISTING    0x00000001
#define HTTP_CREATE_REQUEST_QUEUE_FLAG_CONTROLLER       0x00000002

#define HTTP_INITIALIZE_SERVER 0x00000001
#define HTTP_INITIALIZE_CONFIG 0x00000002

#define HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY     0x00000001
#define HTTP_RECEIVE_REQUEST_FLAG_FLUSH_BODY    0x00000002

#define HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS   0x00000001
#define HTTP_REQUEST_FLAG_IP_ROUTED                 0x00000002
#define HTTP_REQUEST_FLAG_HTTP2                     0x00000004

#define HTTP_SEND_RESPONSE_FLAG_DISCONNECT      0x00000001
#define HTTP_SEND_RESPONSE_FLAG_MORE_DATA       0x00000002
#define HTTP_SEND_RESPONSE_FLAG_BUFFER_DATA     0x00000004
#define HTTP_SEND_RESPONSE_FLAG_ENABLE_NAGLING  0x00000008
#define HTTP_SEND_RESPONSE_FLAG_PROCESS_RANGES  0x00000020
#define HTTP_SEND_RESPONSE_FLAG_OPAQUE          0x00000040

#define HTTP_URL_FLAG_REMOVE_ALL    0x0000001

typedef enum _HTTP_SERVICE_CONFIG_ID
{
    HttpServiceConfigIPListenList,
    HttpServiceConfigSSLCertInfo,
    HttpServiceConfigUrlAclInfo,
    HttpServiceConfigTimeout,
    HttpServiceConfigMax,
    HttpServiceConfigCache = 4,
    HttpServiceConfigSslSniCertInfo = 5,
    HttpServiceConfigSslCcsCertInfo = 6,
    HttpServiceConfigSetting = 7,
    HttpServiceConfigSslCertInfoEx = 8,
    HttpServiceConfigSslSniCertInfoEx = 9,
    HttpServiceConfigSslCcsCertInfoEx = 0xA,
    HttpServiceConfigSslScopedCcsCertInfo = 0xB,
    HttpServiceConfigSslScopedCcsCertInfoEx = 0xC,
} HTTP_SERVICE_CONFIG_ID, *PHTTP_SERVICE_CONFIG_ID;

#define HTTP_NULL_ID ((ULONGLONG)0)

typedef ULONGLONG HTTP_OPAQUE_ID, *PHTTP_OPAQUE_ID;
typedef HTTP_OPAQUE_ID HTTP_CONNECTION_ID, *PHTTP_CONNECTION_ID;
typedef HTTP_OPAQUE_ID HTTP_RAW_CONNECTION_ID, *PHTTP_RAW_CONNECTION_ID;
typedef HTTP_OPAQUE_ID HTTP_REQUEST_ID, *PHTTP_REQUEST_ID;
typedef HTTP_OPAQUE_ID HTTP_SERVER_SESSION_ID, *PHTTP_SERVER_SESSION_ID;
typedef HTTP_OPAQUE_ID HTTP_URL_GROUP_ID, *PHTTP_URL_GROUP_ID;
typedef ULONGLONG HTTP_URL_CONTEXT;

typedef struct _HTTP_VERSION
{
    USHORT MajorVersion;
    USHORT MinorVersion;
} HTTP_VERSION, *PHTTP_VERSION;

typedef enum _HTTP_VERB
{
    HttpVerbUnparsed = 0,
    HttpVerbUnknown,
    HttpVerbInvalid,
    HttpVerbOPTIONS,
    HttpVerbGET,
    HttpVerbHEAD,
    HttpVerbPOST,
    HttpVerbPUT,
    HttpVerbDELETE,
    HttpVerbTRACE,
    HttpVerbCONNECT,
    HttpVerbTRACK,
    HttpVerbMOVE,
    HttpVerbCOPY,
    HttpVerbPROPFIND,
    HttpVerbPROPPATCH,
    HttpVerbMKCOL,
    HttpVerbLOCK,
    HttpVerbUNLOCK,
    HttpVerbSEARCH,
    HttpVerbMaximum,
} HTTP_VERB, *PHTTP_VERB;

typedef struct _HTTP_COOKED_URL
{
    USHORT FullUrlLength;
    USHORT HostLength;
    USHORT AbsPathLength;
    USHORT QueryStringLength;
    const WCHAR *pFullUrl;
    const WCHAR *pHost;
    const WCHAR *pAbsPath;
    const WCHAR *pQueryString;
} HTTP_COOKED_URL, *PHTTP_COOKED_URL;

typedef struct _HTTP_TRANSPORT_ADDRESS
{
    SOCKADDR *pRemoteAddress;
    SOCKADDR *pLocalAddress;
} HTTP_TRANSPORT_ADDRESS, *PHTTP_TRANSPORT_ADDRESS;

typedef struct _HTTP_UNKNOWN_HEADER
{
    USHORT NameLength;
    USHORT RawValueLength;
    const char *pName;
    const char *pRawValue;
} HTTP_UNKNOWN_HEADER, *PHTTP_UNKNOWN_HEADER;

typedef struct _HTTP_KNOWN_HEADER
{
    USHORT RawValueLength;
    const char *pRawValue;
} HTTP_KNOWN_HEADER, *PHTTP_KNOWN_HEADER;

typedef enum _HTTP_HEADER_ID
{
    HttpHeaderCacheControl = 0,
    HttpHeaderConnection = 1,
    HttpHeaderDate = 2,
    HttpHeaderKeepAlive = 3,
    HttpHeaderPragma = 4,
    HttpHeaderTrailer = 5,
    HttpHeaderTransferEncoding = 6,
    HttpHeaderUpgrade = 7,
    HttpHeaderVia = 8,
    HttpHeaderWarning = 9,
    HttpHeaderAllow = 10,
    HttpHeaderContentLength = 11,
    HttpHeaderContentType = 12,
    HttpHeaderContentEncoding = 13,
    HttpHeaderContentLanguage = 14,
    HttpHeaderContentLocation = 15,
    HttpHeaderContentMd5 = 16,
    HttpHeaderContentRange = 17,
    HttpHeaderExpires = 18,
    HttpHeaderLastModified = 19,

    HttpHeaderAccept = 20,
    HttpHeaderAcceptCharset = 21,
    HttpHeaderAcceptEncoding = 22,
    HttpHeaderAcceptLanguage = 23,
    HttpHeaderAuthorization = 24,
    HttpHeaderCookie = 25,
    HttpHeaderExpect = 26,
    HttpHeaderFrom = 27,
    HttpHeaderHost = 28,
    HttpHeaderIfMatch = 29,
    HttpHeaderIfModifiedSince = 30,
    HttpHeaderIfNoneMatch = 31,
    HttpHeaderIfRange = 32,
    HttpHeaderIfUnmodifiedSince = 33,
    HttpHeaderMaxForwards = 34,
    HttpHeaderProxyAuthorization = 35,
    HttpHeaderReferer = 36,
    HttpHeaderRange = 37,
    HttpHeaderTe = 38,
    HttpHeaderTranslate = 39,
    HttpHeaderUserAgent = 40,
    HttpHeaderRequestMaximum = 41,

    HttpHeaderAcceptRanges = 20,
    HttpHeaderAge = 21,
    HttpHeaderEtag = 22,
    HttpHeaderLocation = 23,
    HttpHeaderProxyAuthenticate = 24,
    HttpHeaderRetryAfter = 25,
    HttpHeaderServer = 26,
    HttpHeaderSetCookie = 27,
    HttpHeaderVary = 28,
    HttpHeaderWwwAuthenticate = 29,
    HttpHeaderResponseMaximum = 30,

    HttpHeaderMaximum = 41,
} HTTP_HEADER_ID, *PHTTP_HEADER_ID;

typedef struct _HTTP_REQUEST_HEADERS
{
    USHORT UnknownHeaderCount;
    HTTP_UNKNOWN_HEADER *pUnknownHeaders;
    USHORT TrailerCount;
    HTTP_UNKNOWN_HEADER *pTrailers;
    HTTP_KNOWN_HEADER KnownHeaders[HttpHeaderRequestMaximum];
} HTTP_REQUEST_HEADERS, *PHTTP_REQUEST_HEADERS;

typedef enum _HTTP_DATA_CHUNK_TYPE
{
    HttpDataChunkFromMemory = 0,
    HttpDataChunkFromFileHandle,
    HttpDataChunkFromFragmentCache,
    HttpDataChunkFromFragmentCacheEx,
    HttpDataChunkMaximum,
    HttpDataChunkTrailers = 4,
    HttpDataChunkFromWinHttpFastForwarding = 5,
} HTTP_DATA_CHUNK_TYPE, *PHTTP_DATA_CHUNK_TYPE;

#define HTTP_BYTE_RANGE_TO_EOF ((ULONGLONG)-1)

typedef struct _HTTP_BYTE_RANGE
{
    ULARGE_INTEGER StartingOffset;
    ULARGE_INTEGER Length;
} HTTP_BYTE_RANGE, *PHTTP_BYTE_RANGE;

typedef struct _HTTP_DATA_CHUNK
{
    HTTP_DATA_CHUNK_TYPE DataChunkType;
    __C89_NAMELESS union
    {
        struct
        {
            void *pBuffer;
            ULONG BufferLength;
        } FromMemory;
        struct
        {
            HTTP_BYTE_RANGE ByteRange;
            HANDLE FileHandle;
        } FromFileHandle;
        struct
        {
            USHORT FragmentNameLength;
            const WCHAR *pFragmentName;
        } FromFragmentCache;
    } DUMMYUNIONNAME;
} HTTP_DATA_CHUNK, *PHTTP_DATA_CHUNK;

typedef struct _HTTP_SSL_CLIENT_CERT_INFO
{
    ULONG CertFlags;
    ULONG CertEncodedSize;
    UCHAR *pCertEncoded;
    HANDLE Token;
    BOOLEAN CertDeniedByMapper;
} HTTP_SSL_CLIENT_CERT_INFO, *PHTTP_SSL_CLIENT_CERT_INFO;

typedef struct _HTTP_SSL_INFO
{
    USHORT ServerCertKeySize;
    USHORT ConnectionKeySize;
    ULONG ServerCertIssuerSize;
    ULONG ServerCertSubjectSize;
    const char *pServerCertIssuer;
    const char *pServerCertSubject;
    HTTP_SSL_CLIENT_CERT_INFO *pClientCertInfo;
    ULONG SslClientCertNegotiated;
} HTTP_SSL_INFO, *PHTTP_SSL_INFO;

typedef struct _HTTP_REQUEST_V1
{
    ULONG Flags;
    HTTP_CONNECTION_ID ConnectionId;
    HTTP_REQUEST_ID RequestId;
    HTTP_URL_CONTEXT UrlContext;
    HTTP_VERSION Version;
    HTTP_VERB Verb;
    USHORT UnknownVerbLength;
    USHORT RawUrlLength;
    const char *pUnknownVerb;
    const char *pRawUrl;
    HTTP_COOKED_URL CookedUrl;
    HTTP_TRANSPORT_ADDRESS Address;
    HTTP_REQUEST_HEADERS Headers;
    ULONGLONG BytesReceived;
    USHORT EntityChunkCount;
    HTTP_DATA_CHUNK *pEntityChunks;
    HTTP_RAW_CONNECTION_ID RawConnectionId;
    HTTP_SSL_INFO *pSslInfo;
} HTTP_REQUEST_V1;

typedef enum _HTTP_REQUEST_INFO_TYPE
{
    HttpRequestInfoTypeAuth = 0,
    HttpRequestInfoTypeChannelBind = 1,
    HttpRequestInfoTypeSslProtocol = 2,
    HttpRequestInfoTypeSslTokenBindingDraft = 3,
    HttpRequestInfoTypeSslTokenBinding = 4,
    HttpRequestInfoTypeRequestTiming = 5,
    HttpRequestInfoTypeTcpInfoV0 = 6,
    HttpRequestInfoTypeRequestSizing = 7,
    HttpRequestInfoTypeQuicStats = 8,
    HttpRequestInfoTypeTcpInfoV1 = 9,
    HttpRequestInfoTypeQuicStatsV2 = 0xA,
    HttpRequestInfoTypeTcpInfoV2 = 0xB,
    HttpRequestInfoTypeTransportIdleConnectionTimeout = 0xC,
    HttpRequestInfoTypeDscpTag = 0xD,
    HttpRequestInfoTypeInitialPacketTtl = 0xE,
} HTTP_REQUEST_INFO_TYPE, *PHTTP_REQUEST_INFO_TYPE;

typedef struct _HTTP_REQUEST_INFO
{
    HTTP_REQUEST_INFO_TYPE InfoType;
    ULONG InfoLength;
    void *pInfo;
} HTTP_REQUEST_INFO, *PHTTP_REQUEST_INFO;

#ifdef __cplusplus
typedef struct _HTTP_REQUEST_V2 : HTTP_REQUEST_V1
{
    USHORT RequestInfoCount;
    HTTP_REQUEST_INFO *pRequestInfo;
} HTTP_REQUEST_V2, *PHTTP_REQUEST_V2;
#else
typedef struct _HTTP_REQUEST_V2
{
    HTTP_REQUEST_V1 s;
    USHORT RequestInfoCount;
    HTTP_REQUEST_INFO *pRequestInfo;
} HTTP_REQUEST_V2, *PHTTP_REQUEST_V2;
#endif

typedef HTTP_REQUEST_V2 HTTP_REQUEST, *PHTTP_REQUEST;

typedef struct _HTTP_RESPONSE_HEADERS
{
    USHORT UnknownHeaderCount;
    HTTP_UNKNOWN_HEADER *pUnknownHeaders;
    USHORT TrailerCount;
    HTTP_UNKNOWN_HEADER *pTrailers;
    HTTP_KNOWN_HEADER KnownHeaders[HttpHeaderResponseMaximum];
} HTTP_RESPONSE_HEADERS,*PHTTP_RESPONSE_HEADERS;

typedef struct _HTTP_RESPONSE_V1
{
    ULONG Flags;
    HTTP_VERSION Version;
    USHORT StatusCode;
    USHORT ReasonLength;
    const char *pReason;
    HTTP_RESPONSE_HEADERS Headers;
    USHORT EntityChunkCount;
    HTTP_DATA_CHUNK *pEntityChunks;
} HTTP_RESPONSE_V1, *PHTTP_RESPONSE_V1;

typedef enum _HTTP_RESPONSE_INFO_TYPE
{
    HttpResponseInfoTypeMultipleKnownHeaders = 0,
    HttpResponseInfoTypeAuthenticationProperty,
    HttpResponseInfoTypeQosProperty,
    HttpResponseInfoTypeChannelBind,
    HttpResponseInfoTypeQoSProperty = 2,
} HTTP_RESPONSE_INFO_TYPE, *PHTTP_RESPONSE_INFO_TYPE;

typedef struct _HTTP_RESPONSE_INFO
{
    HTTP_RESPONSE_INFO_TYPE Type;
    ULONG Length;
    void *pInfo;
} HTTP_RESPONSE_INFO, *PHTTP_RESPONSE_INFO;

#ifdef __cplusplus
typedef struct _HTTP_RESPONSE_V2 : HTTP_RESPONSE_V1
{
    USHORT ResponseInfoCount;
    HTTP_RESPONSE_INFO *pResponseInfo;
} HTTP_RESPONSE_V2, *PHTTP_RESPONSE_V2;
#else
typedef struct _HTTP_RESPONSE_V2
{
    HTTP_RESPONSE_V1 s;
    USHORT ResponseInfoCount;
    HTTP_RESPONSE_INFO *pResponseInfo;
} HTTP_RESPONSE_V2, *PHTTP_RESPONSE_V2;
#endif

typedef HTTP_RESPONSE_V2 HTTP_RESPONSE, *PHTTP_RESPONSE;

typedef enum _HTTP_CACHE_POLICY_TYPE
{
    HttpCachePolicyNocache,
    HttpCachePolicyUserInvalidates,
    HttpCachePolicyTimeToLive,
    HttpCachePolicyMaximum,
} HTTP_CACHE_POLICY_TYPE, *PHTTP_CACHE_POLICY_TYPE;

typedef struct _HTTP_CACHE_POLICY
{
    HTTP_CACHE_POLICY_TYPE Policy;
    ULONG SecondsToLive;
} HTTP_CACHE_POLICY, *PHTTP_CACHE_POLICY;

typedef enum _HTTP_LOG_DATA_TYPE
{
    HttpLogDataTypeFields = 0,
} HTTP_LOG_DATA_TYPE, *PHTTP_LOG_DATA_TYPE;

typedef struct _HTTP_LOG_DATA
{
    HTTP_LOG_DATA_TYPE Type;
} HTTP_LOG_DATA, *PHTTP_LOG_DATA;

typedef enum _HTTP_SERVER_PROPERTY
{
    HttpServerAuthenticationProperty,
    HttpServerLoggingProperty,
    HttpServerQosProperty,
    HttpServerTimeoutsProperty,
    HttpServerQueueLengthProperty,
    HttpServerStateProperty,
    HttpServer503VerbosityProperty,
    HttpServerBindingProperty,
    HttpServerExtendedAuthenticationProperty,
    HttpServerListenEndpointProperty,
    HttpServerChannelBindProperty,
    HttpServerProtectionLevelProperty,
    HttpServerDelegationProperty = 0x10,
    HttpServerFastForwardingProperty = 0x12,
    HttpServerRequestInfoProperty = 0x13,
} HTTP_SERVER_PROPERTY, *PHTTP_SERVER_PROPERTY;

typedef struct _HTTP_PROPERTY_FLAGS
{
    ULONG Present : 1;
} HTTP_PROPERTY_FLAGS, *PHTTP_PROPERTY_FLAGS;

typedef struct _HTTP_BINDING_INFO
{
    HTTP_PROPERTY_FLAGS Flags;
    HANDLE RequestQueueHandle;
} HTTP_BINDING_INFO, *PHTTP_BINDING_INFO;

#if !defined(_NTDDK_) && !defined(_NTIFS_)
ULONG WINAPI HttpAddUrl(HANDLE,PCWSTR,PVOID);
ULONG WINAPI HttpAddUrlToUrlGroup(HTTP_URL_GROUP_ID id, const WCHAR *url, HTTP_URL_CONTEXT context, ULONG reserved);
ULONG WINAPI HttpCloseRequestQueue(HANDLE handle);
ULONG WINAPI HttpCloseServerSession(HTTP_SERVER_SESSION_ID id);
ULONG WINAPI HttpCloseUrlGroup(HTTP_URL_GROUP_ID id);
ULONG WINAPI HttpCreateHttpHandle(PHANDLE,ULONG);
ULONG WINAPI HttpCreateRequestQueue(HTTPAPI_VERSION version, const WCHAR *name, SECURITY_ATTRIBUTES *sa, ULONG flags, HANDLE *handle);
ULONG WINAPI HttpCreateServerSession(HTTPAPI_VERSION,PHTTP_SERVER_SESSION_ID,ULONG);
ULONG WINAPI HttpCreateUrlGroup(HTTP_SERVER_SESSION_ID session_id, HTTP_URL_GROUP_ID *group_id, ULONG reserved);
ULONG WINAPI HttpDeleteServiceConfiguration(HANDLE,HTTP_SERVICE_CONFIG_ID,PVOID,ULONG,LPOVERLAPPED);
ULONG WINAPI HttpInitialize(HTTPAPI_VERSION version, ULONG flags, void *reserved);
ULONG WINAPI HttpTerminate(ULONG flags, void *reserved);
ULONG WINAPI HttpQueryServiceConfiguration(HANDLE,HTTP_SERVICE_CONFIG_ID,PVOID,ULONG,PVOID,ULONG,PULONG,LPOVERLAPPED);
ULONG WINAPI HttpReceiveHttpRequest(HANDLE queue, HTTP_REQUEST_ID id, ULONG flags, HTTP_REQUEST *request, ULONG size, ULONG *ret_size, OVERLAPPED *ovl);
ULONG WINAPI HttpRemoveUrl(HANDLE queue, const WCHAR *url);
ULONG WINAPI HttpRemoveUrlFromUrlGroup(HTTP_URL_GROUP_ID id, const WCHAR *url, ULONG flags);
ULONG WINAPI HttpSendHttpResponse(HANDLE queue, HTTP_REQUEST_ID id, ULONG flags, HTTP_RESPONSE *response, HTTP_CACHE_POLICY *cache_policy, ULONG *ret_size, void *reserved1, ULONG reserved2, OVERLAPPED *ovl, HTTP_LOG_DATA *log_data);
ULONG WINAPI HttpSetServiceConfiguration(HANDLE,HTTP_SERVICE_CONFIG_ID,PVOID,ULONG,LPOVERLAPPED);
ULONG WINAPI HttpSetUrlGroupProperty(HTTP_URL_GROUP_ID id, HTTP_SERVER_PROPERTY property, void *value, ULONG length);
#endif

#ifdef __cplusplus
}
#endif

#ifndef HTTP_AUTH_ENABLE_BASIC
#define HTTP_AUTH_ENABLE_BASIC 0x00000001
#endif
#ifndef HTTP_AUTH_ENABLE_DIGEST
#define HTTP_AUTH_ENABLE_DIGEST 0x00000002
#endif
#ifndef HTTP_AUTH_ENABLE_KERBEROS
#define HTTP_AUTH_ENABLE_KERBEROS 0x00000010
#endif
#ifndef HTTP_AUTH_ENABLE_NEGOTIATE
#define HTTP_AUTH_ENABLE_NEGOTIATE 0x00000008
#endif
#ifndef HTTP_AUTH_ENABLE_NTLM
#define HTTP_AUTH_ENABLE_NTLM 0x00000004
#endif
#ifndef HTTP_AUTH_EX_FLAG_CAPTURE_CREDENTIAL
#define HTTP_AUTH_EX_FLAG_CAPTURE_CREDENTIAL 0x02
#endif
#ifndef HTTP_AUTH_EX_FLAG_ENABLE_KERBEROS_CREDENTIAL_CACHING
#define HTTP_AUTH_EX_FLAG_ENABLE_KERBEROS_CREDENTIAL_CACHING 0x01
#endif
#ifndef HTTP_CERT_CHECK_MODE_CACHED_REVOCATION
#define HTTP_CERT_CHECK_MODE_CACHED_REVOCATION 0x00002
#endif
#ifndef HTTP_CERT_CHECK_MODE_CACHED_URLS
#define HTTP_CERT_CHECK_MODE_CACHED_URLS 0x00008
#endif
#ifndef HTTP_CERT_CHECK_MODE_NO_AIA
#define HTTP_CERT_CHECK_MODE_NO_AIA 0x00010
#endif
#ifndef HTTP_CERT_CHECK_MODE_NO_USAGE_CHECK
#define HTTP_CERT_CHECK_MODE_NO_USAGE_CHECK 0x10000
#endif
#ifndef HTTP_CERT_CHECK_MODE_USE_REVOCATION_FRESHNESS
#define HTTP_CERT_CHECK_MODE_USE_REVOCATION_FRESHNESS 0x00004
#endif
#ifndef HTTP_CHANNEL_BIND_CLIENT_SERVICE
#define HTTP_CHANNEL_BIND_CLIENT_SERVICE 0x10
#endif
#ifndef HTTP_CHANNEL_BIND_DOTLESS_SERVICE
#define HTTP_CHANNEL_BIND_DOTLESS_SERVICE 0x4
#endif
#ifndef HTTP_CHANNEL_BIND_NO_SERVICE_NAME_CHECK
#define HTTP_CHANNEL_BIND_NO_SERVICE_NAME_CHECK 0x2
#endif
#ifndef HTTP_CHANNEL_BIND_PROXY
#define HTTP_CHANNEL_BIND_PROXY 0x1
#endif
#ifndef HTTP_CHANNEL_BIND_PROXY_COHOSTING
#define HTTP_CHANNEL_BIND_PROXY_COHOSTING 0x20
#endif
#ifndef HTTP_CHANNEL_BIND_SECURE_CHANNEL_TOKEN
#define HTTP_CHANNEL_BIND_SECURE_CHANNEL_TOKEN 0x8
#endif
#ifndef HTTP_CREATE_REQUEST_QUEUE_FLAG_DELEGATION
#define HTTP_CREATE_REQUEST_QUEUE_FLAG_DELEGATION 0x00000008
#endif
#ifndef HTTP_DEMAND_CBT
#define HTTP_DEMAND_CBT 0x00000004
#endif
#ifndef HTTP_FLUSH_RESPONSE_FLAG_RECURSIVE
#define HTTP_FLUSH_RESPONSE_FLAG_RECURSIVE 0x00000001
#endif
#ifndef HTTP_LOGGING_FLAG_LOCAL_TIME_ROLLOVER
#define HTTP_LOGGING_FLAG_LOCAL_TIME_ROLLOVER 0x00000001
#endif
#ifndef HTTP_LOGGING_FLAG_LOG_ERRORS_ONLY
#define HTTP_LOGGING_FLAG_LOG_ERRORS_ONLY 0x00000004
#endif
#ifndef HTTP_LOGGING_FLAG_LOG_SUCCESS_ONLY
#define HTTP_LOGGING_FLAG_LOG_SUCCESS_ONLY 0x00000008
#endif
#ifndef HTTP_LOGGING_FLAG_USE_UTF8_CONVERSION
#define HTTP_LOGGING_FLAG_USE_UTF8_CONVERSION 0x00000002
#endif
#ifndef HTTP_LOG_FIELD_BYTES_RECV
#define HTTP_LOG_FIELD_BYTES_RECV 0x00002000
#endif
#ifndef HTTP_LOG_FIELD_BYTES_SENT
#define HTTP_LOG_FIELD_BYTES_SENT 0x00001000
#endif
#ifndef HTTP_LOG_FIELD_CLIENT_IP
#define HTTP_LOG_FIELD_CLIENT_IP 0x00000004
#endif
#ifndef HTTP_LOG_FIELD_CLIENT_PORT
#define HTTP_LOG_FIELD_CLIENT_PORT 0x00400000
#endif
#ifndef HTTP_LOG_FIELD_COMPUTER_NAME
#define HTTP_LOG_FIELD_COMPUTER_NAME 0x00000020
#endif
#ifndef HTTP_LOG_FIELD_COOKIE
#define HTTP_LOG_FIELD_COOKIE 0x00020000
#endif
#ifndef HTTP_LOG_FIELD_CORRELATION_ID
#define HTTP_LOG_FIELD_CORRELATION_ID 0x40000000
#endif
#ifndef HTTP_LOG_FIELD_DATE
#define HTTP_LOG_FIELD_DATE 0x00000001
#endif
#ifndef HTTP_LOG_FIELD_EXT_FAULT_CODE_EXT
#define HTTP_LOG_FIELD_EXT_FAULT_CODE_EXT 0x0000000000000001
#endif
#ifndef HTTP_LOG_FIELD_FAULT_CODE
#define HTTP_LOG_FIELD_FAULT_CODE 0x80000000
#endif
#ifndef HTTP_LOG_FIELD_HOST
#define HTTP_LOG_FIELD_HOST 0x00100000
#endif
#ifndef HTTP_LOG_FIELD_METHOD
#define HTTP_LOG_FIELD_METHOD 0x00000080
#endif
#ifndef HTTP_LOG_FIELD_QUEUE_NAME
#define HTTP_LOG_FIELD_QUEUE_NAME 0x04000000
#endif
#ifndef HTTP_LOG_FIELD_REASON
#define HTTP_LOG_FIELD_REASON 0x02000000
#endif
#ifndef HTTP_LOG_FIELD_REFERER
#define HTTP_LOG_FIELD_REFERER 0x00040000
#endif
#ifndef HTTP_LOG_FIELD_SERVER_IP
#define HTTP_LOG_FIELD_SERVER_IP 0x00000040
#endif
#ifndef HTTP_LOG_FIELD_SERVER_PORT
#define HTTP_LOG_FIELD_SERVER_PORT 0x00008000
#endif
#ifndef HTTP_LOG_FIELD_SITE_ID
#define HTTP_LOG_FIELD_SITE_ID 0x01000000
#endif
#ifndef HTTP_LOG_FIELD_SITE_NAME
#define HTTP_LOG_FIELD_SITE_NAME 0x00000010
#endif
#ifndef HTTP_LOG_FIELD_STATUS
#define HTTP_LOG_FIELD_STATUS 0x00000400
#endif
#ifndef HTTP_LOG_FIELD_STREAM_ID
#define HTTP_LOG_FIELD_STREAM_ID 0x08000000
#endif
#ifndef HTTP_LOG_FIELD_STREAM_ID_EX
#define HTTP_LOG_FIELD_STREAM_ID_EX 0x10000000
#endif
#ifndef HTTP_LOG_FIELD_SUB_STATUS
#define HTTP_LOG_FIELD_SUB_STATUS 0x00200000
#endif
#ifndef HTTP_LOG_FIELD_TIME
#define HTTP_LOG_FIELD_TIME 0x00000002
#endif
#ifndef HTTP_LOG_FIELD_TIME_TAKEN
#define HTTP_LOG_FIELD_TIME_TAKEN 0x00004000
#endif
#ifndef HTTP_LOG_FIELD_TRANSPORT_TYPE
#define HTTP_LOG_FIELD_TRANSPORT_TYPE 0x20000000
#endif
#ifndef HTTP_LOG_FIELD_URI
#define HTTP_LOG_FIELD_URI 0x00800000
#endif
#ifndef HTTP_LOG_FIELD_URI_QUERY
#define HTTP_LOG_FIELD_URI_QUERY 0x00000200
#endif
#ifndef HTTP_LOG_FIELD_URI_STEM
#define HTTP_LOG_FIELD_URI_STEM 0x00000100
#endif
#ifndef HTTP_LOG_FIELD_USER_AGENT
#define HTTP_LOG_FIELD_USER_AGENT 0x00010000
#endif
#ifndef HTTP_LOG_FIELD_USER_NAME
#define HTTP_LOG_FIELD_USER_NAME 0x00000008
#endif
#ifndef HTTP_LOG_FIELD_VERSION
#define HTTP_LOG_FIELD_VERSION 0x00080000
#endif
#ifndef HTTP_LOG_FIELD_WIN32_STATUS
#define HTTP_LOG_FIELD_WIN32_STATUS 0x00000800
#endif
#ifndef HTTP_MAX_SERVER_QUEUE_LENGTH
#define HTTP_MAX_SERVER_QUEUE_LENGTH 0x7FFFFFFF
#endif
#ifndef HTTP_MIN_SERVER_QUEUE_LENGTH
#define HTTP_MIN_SERVER_QUEUE_LENGTH 1
#endif
#ifndef HTTP_RECEIVE_FULL_CHAIN
#define HTTP_RECEIVE_FULL_CHAIN 0x2
#endif
#ifndef HTTP_RECEIVE_REQUEST_ENTITY_BODY_FLAG_FILL_BUFFER
#define HTTP_RECEIVE_REQUEST_ENTITY_BODY_FLAG_FILL_BUFFER 0x00000001
#endif
#ifndef HTTP_RECEIVE_SECURE_CHANNEL_TOKEN
#define HTTP_RECEIVE_SECURE_CHANNEL_TOKEN 0x1
#endif
#ifndef HTTP_REQUEST_AUTH_FLAG_TOKEN_FOR_CACHED_CRED
#define HTTP_REQUEST_AUTH_FLAG_TOKEN_FOR_CACHED_CRED 0x00000001
#endif
#ifndef HTTP_REQUEST_FLAG_FAST_FORWARDING_ALLOWED
#define HTTP_REQUEST_FLAG_FAST_FORWARDING_ALLOWED 0x00000010
#endif
#ifndef HTTP_REQUEST_FLAG_HTTP3
#define HTTP_REQUEST_FLAG_HTTP3 0x00000008
#endif
#ifndef HTTP_REQUEST_INFO_FLAG_INITIAL_TTL
#define HTTP_REQUEST_INFO_FLAG_INITIAL_TTL 0x00000001ULL
#endif
#ifndef HTTP_REQUEST_PROPERTY_SNI_FLAG_NO_SNI
#define HTTP_REQUEST_PROPERTY_SNI_FLAG_NO_SNI 0x00000002
#endif
#ifndef HTTP_REQUEST_PROPERTY_SNI_FLAG_SNI_USED
#define HTTP_REQUEST_PROPERTY_SNI_FLAG_SNI_USED 0x00000001
#endif
#ifndef HTTP_REQUEST_PROPERTY_SNI_HOST_MAX_LENGTH
#define HTTP_REQUEST_PROPERTY_SNI_HOST_MAX_LENGTH 255
#endif
#ifndef HTTP_REQUEST_SIZING_INFO_FLAG_FIRST_REQUEST
#define HTTP_REQUEST_SIZING_INFO_FLAG_FIRST_REQUEST 0x00000008
#endif
#ifndef HTTP_REQUEST_SIZING_INFO_FLAG_TCP_FAST_OPEN
#define HTTP_REQUEST_SIZING_INFO_FLAG_TCP_FAST_OPEN 0x00000001
#endif
#ifndef HTTP_REQUEST_SIZING_INFO_FLAG_TLS_FALSE_START
#define HTTP_REQUEST_SIZING_INFO_FLAG_TLS_FALSE_START 0x00000004
#endif
#ifndef HTTP_REQUEST_SIZING_INFO_FLAG_TLS_SESSION_RESUMPTION
#define HTTP_REQUEST_SIZING_INFO_FLAG_TLS_SESSION_RESUMPTION 0x00000002
#endif
#ifndef HTTP_RESPONSE_FLAG_MORE_ENTITY_BODY_EXISTS
#define HTTP_RESPONSE_FLAG_MORE_ENTITY_BODY_EXISTS 0x00000002
#endif
#ifndef HTTP_RESPONSE_FLAG_MULTIPLE_ENCODINGS_AVAILABLE
#define HTTP_RESPONSE_FLAG_MULTIPLE_ENCODINGS_AVAILABLE 0x00000001
#endif
#ifndef HTTP_RESPONSE_INFO_FLAGS_PRESERVE_ORDER
#define HTTP_RESPONSE_INFO_FLAGS_PRESERVE_ORDER 0x00000001
#endif
#ifndef HTTP_SEND_RESPONSE_FLAG_AUTOMATIC_CHUNKING
#define HTTP_SEND_RESPONSE_FLAG_AUTOMATIC_CHUNKING 0x00000200
#endif
#ifndef HTTP_SEND_RESPONSE_FLAG_GOAWAY
#define HTTP_SEND_RESPONSE_FLAG_GOAWAY 0x00000100
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_HTTP2
#define HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_HTTP2 0x00000010
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_LEGACY_TLS
#define HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_LEGACY_TLS 0x00000400
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_OCSP_STAPLING
#define HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_OCSP_STAPLING 0x00000080
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_QUIC
#define HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_QUIC 0x00000020
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_SESSION_ID
#define HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_SESSION_ID 0x00004000
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_TLS12
#define HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_TLS12 0x00001000
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_TLS13
#define HTTP_SERVICE_CONFIG_SSL_FLAG_DISABLE_TLS13 0x00000040
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_ENABLE_CACHE_CLIENT_HELLO
#define HTTP_SERVICE_CONFIG_SSL_FLAG_ENABLE_CACHE_CLIENT_HELLO 0x00008000
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_ENABLE_CLIENT_CORRELATION
#define HTTP_SERVICE_CONFIG_SSL_FLAG_ENABLE_CLIENT_CORRELATION 0x00002000
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_ENABLE_SESSION_TICKET
#define HTTP_SERVICE_CONFIG_SSL_FLAG_ENABLE_SESSION_TICKET 0x00000800
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_ENABLE_TOKEN_BINDING
#define HTTP_SERVICE_CONFIG_SSL_FLAG_ENABLE_TOKEN_BINDING 0x00000100
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_LOG_EXTENDED_EVENTS
#define HTTP_SERVICE_CONFIG_SSL_FLAG_LOG_EXTENDED_EVENTS 0x00000200
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_NEGOTIATE_CLIENT_CERT
#define HTTP_SERVICE_CONFIG_SSL_FLAG_NEGOTIATE_CLIENT_CERT 0x00000002
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_REJECT
#define HTTP_SERVICE_CONFIG_SSL_FLAG_REJECT 0x00000008
#endif
#ifndef HTTP_SERVICE_CONFIG_SSL_FLAG_USE_DS_MAPPER
#define HTTP_SERVICE_CONFIG_SSL_FLAG_USE_DS_MAPPER 0x00000001
#endif
#ifndef HTTP_SSL_CERT_SHA_HASH_LENGTH
#define HTTP_SSL_CERT_SHA_HASH_LENGTH 20
#endif
#ifndef HTTP_SSL_CERT_STORE_NAME_LENGTH
#define HTTP_SSL_CERT_STORE_NAME_LENGTH 128
#endif

typedef enum _HTTP_503_RESPONSE_VERBOSITY {
    Http503ResponseVerbosityBasic = 0,
    Http503ResponseVerbosityLimited = 1,
    Http503ResponseVerbosityFull = 2
} HTTP_503_RESPONSE_VERBOSITY, *PHTTP_503_RESPONSE_VERBOSITY;

typedef enum _HTTP_AUTH_STATUS {
    HttpAuthStatusSuccess = 0,
    HttpAuthStatusNotAuthenticated = 1,
    HttpAuthStatusFailure = 2
} HTTP_AUTH_STATUS, *PHTTP_AUTH_STATUS;

typedef enum _HTTP_AUTHENTICATION_HARDENING_LEVELS {
    HttpAuthenticationHardeningLegacy = 0,
    HttpAuthenticationHardeningMedium = 1,
    HttpAuthenticationHardeningStrict = 2
} HTTP_AUTHENTICATION_HARDENING_LEVELS;

typedef enum _HTTP_CREATE_REQUEST_QUEUE_PROPERTY_ID {
    CreateRequestQueueExternalIdProperty = 1,
    CreateRequestQueueMax = 2
} HTTP_CREATE_REQUEST_QUEUE_PROPERTY_ID, *PHTTP_CREATE_REQUEST_QUEUE_PROPERTY_ID;

typedef enum _HTTP_DELEGATE_REQUEST_PROPERTY_ID {
    DelegateRequestReservedProperty = 0,
    DelegateRequestDelegateUrlProperty = 1
} HTTP_DELEGATE_REQUEST_PROPERTY_ID, *PHTTP_DELEGATE_REQUEST_PROPERTY_ID;

typedef enum _HTTP_ENABLED_STATE {
    HttpEnabledStateActive = 0,
    HttpEnabledStateInactive = 1
} HTTP_ENABLED_STATE, *PHTTP_ENABLED_STATE;

typedef enum _HTTP_FEATURE_ID {
    HttpFeatureUnknown = 0,
    HttpFeatureResponseTrailers = 1,
    HttpFeatureApiTimings = 2,
    HttpFeatureDelegateEx = 3,
    HttpFeatureHttp3 = 4,
    HttpFeatureTlsSessionTickets = 5,
    HttpFeatureDisableTlsSessionId = 6,
    HttpFeatureTlsDualCerts = 7,
    HttpFeatureAutomaticChunkedEncoding = 8,
    HttpFeatureDedicatedReqQueueDelegationType = 9,
    HttpFeatureFastForwardResponse = 10,
    HttpFeatureCacheTlsClientHello = 11,
    HttpFeatureIdleConnectionTimeoutRequestProperty = 12,
    HttpFeatureDisableAiaFlag = 13,
    HttpFeatureDscp = 14,
    HttpFeatureQueryCipherInfo = 15,
    HttpFeatureQueryInitialPacketTtl = 16,
    HttpFeatureTlsHandshakePerformanceCounters = 17,
    HttpFeatureLast = 18,
    HttpFeaturemax = (-1)
} HTTP_FEATURE_ID, *PHTTP_FEATURE_ID;

typedef enum _HTTP_LOGGING_ROLLOVER_TYPE {
    HttpLoggingRolloverSize = 0,
    HttpLoggingRolloverDaily = 1,
    HttpLoggingRolloverWeekly = 2,
    HttpLoggingRolloverMonthly = 3,
    HttpLoggingRolloverHourly = 4
} HTTP_LOGGING_ROLLOVER_TYPE, *PHTTP_LOGGING_ROLLOVER_TYPE;

typedef enum _HTTP_LOGGING_TYPE {
    HttpLoggingTypeW3C = 0,
    HttpLoggingTypeIIS = 1,
    HttpLoggingTypeNCSA = 2,
    HttpLoggingTypeRaw = 3
} HTTP_LOGGING_TYPE, *PHTTP_LOGGING_TYPE;

typedef enum _HTTP_PERFORMANCE_PARAM_TYPE {
    PerformanceParamSendBufferingFlags = 0,
    PerformanceParamAggressiveICW = 1,
    PerformanceParamMaxSendBufferSize = 2,
    PerformanceParamMaxConcurrentClientStreams = 3,
    PerformanceParamMaxReceiveBufferSize = 4,
    PerformanceParamDecryptOnSspiThread = 5,
    PerformanceParamMax = 6
} HTTP_PERFORMANCE_PARAM_TYPE, *PHTTP_PERFORMANCE_PARAM_TYPE;

typedef enum _HTTP_PROTECTION_LEVEL_TYPE {
    HttpProtectionLevelUnrestricted = 0,
    HttpProtectionLevelEdgeRestricted = 1,
    HttpProtectionLevelRestricted = 2
} HTTP_PROTECTION_LEVEL_TYPE, *PHTTP_PROTECTION_LEVEL_TYPE;

typedef enum _HTTP_QOS_SETTING_TYPE {
    HttpQosSettingTypeBandwidth = 0,
    HttpQosSettingTypeConnectionLimit = 1,
    HttpQosSettingTypeFlowRate = 2
} HTTP_QOS_SETTING_TYPE, *PHTTP_QOS_SETTING_TYPE;

typedef enum _HTTP_REQUEST_AUTH_TYPE {
    HttpRequestAuthTypeNone = 0,
    HttpRequestAuthTypeBasic = 1,
    HttpRequestAuthTypeDigest = 2,
    HttpRequestAuthTypeNTLM = 3,
    HttpRequestAuthTypeNegotiate = 4,
    HttpRequestAuthTypeKerberos = 5
} HTTP_REQUEST_AUTH_TYPE, *PHTTP_REQUEST_AUTH_TYPE;

typedef enum _HTTP_REQUEST_PROPERTY {
    HttpRequestPropertyIsb = 0,
    HttpRequestPropertyTcpInfoV0 = 1,
    HttpRequestPropertyQuicStats = 2,
    HttpRequestPropertyTcpInfoV1 = 3,
    HttpRequestPropertySni = 4,
    HttpRequestPropertyStreamError = 5,
    HttpRequestPropertyWskApiTimings = 6,
    HttpRequestPropertyQuicApiTimings = 7,
    HttpRequestPropertyQuicStatsV2 = 8,
    HttpRequestPropertyQuicStreamStats = 9,
    HttpRequestPropertyTcpInfoV2 = 10,
    HttpRequestPropertyTlsClientHello = 11,
    HttpRequestPropertyTransportIdleConnectionTimeout = 12,
    HttpRequestPropertyDscpTag = 13,
    HttpRequestPropertyTlsCipherInfo = 14
} HTTP_REQUEST_PROPERTY, *PHTTP_REQUEST_PROPERTY;

typedef enum _HTTP_REQUEST_SIZING_TYPE {
    HttpRequestSizingTypeTlsHandshakeLeg1ClientData = 0,
    HttpRequestSizingTypeTlsHandshakeLeg1ServerData = 1,
    HttpRequestSizingTypeTlsHandshakeLeg2ClientData = 2,
    HttpRequestSizingTypeTlsHandshakeLeg2ServerData = 3,
    HttpRequestSizingTypeHeaders = 4,
    HttpRequestSizingTypeMax = 5
} HTTP_REQUEST_SIZING_TYPE, *PHTTP_REQUEST_SIZING_TYPE;

typedef enum _HTTP_REQUEST_TIMING_TYPE {
    HttpRequestTimingTypeConnectionStart = 0,
    HttpRequestTimingTypeDataStart = 1,
    HttpRequestTimingTypeTlsCertificateLoadStart = 2,
    HttpRequestTimingTypeTlsCertificateLoadEnd = 3,
    HttpRequestTimingTypeTlsHandshakeLeg1Start = 4,
    HttpRequestTimingTypeTlsHandshakeLeg1End = 5,
    HttpRequestTimingTypeTlsHandshakeLeg2Start = 6,
    HttpRequestTimingTypeTlsHandshakeLeg2End = 7,
    HttpRequestTimingTypeTlsAttributesQueryStart = 8,
    HttpRequestTimingTypeTlsAttributesQueryEnd = 9,
    HttpRequestTimingTypeTlsClientCertQueryStart = 10,
    HttpRequestTimingTypeTlsClientCertQueryEnd = 11,
    HttpRequestTimingTypeHttp2StreamStart = 12,
    HttpRequestTimingTypeHttp2HeaderDecodeStart = 13,
    HttpRequestTimingTypeHttp2HeaderDecodeEnd = 14,
    HttpRequestTimingTypeRequestHeaderParseStart = 15,
    HttpRequestTimingTypeRequestHeaderParseEnd = 16,
    HttpRequestTimingTypeRequestRoutingStart = 17,
    HttpRequestTimingTypeRequestRoutingEnd = 18,
    HttpRequestTimingTypeRequestQueuedForInspection = 19,
    HttpRequestTimingTypeRequestDeliveredForInspection = 20,
    HttpRequestTimingTypeRequestReturnedAfterInspection = 21,
    HttpRequestTimingTypeRequestQueuedForDelegation = 22,
    HttpRequestTimingTypeRequestDeliveredForDelegation = 23,
    HttpRequestTimingTypeRequestReturnedAfterDelegation = 24,
    HttpRequestTimingTypeRequestQueuedForIO = 25,
    HttpRequestTimingTypeRequestDeliveredForIO = 26,
    HttpRequestTimingTypeHttp3StreamStart = 27,
    HttpRequestTimingTypeHttp3HeaderDecodeStart = 28,
    HttpRequestTimingTypeHttp3HeaderDecodeEnd = 29,
    HttpRequestTimingTypeMax = 30
} HTTP_REQUEST_TIMING_TYPE, *PHTTP_REQUEST_TIMING_TYPE;

typedef enum _HTTP_SERVICE_BINDING_TYPE {
    HttpServiceBindingTypeNone = 0,
    HttpServiceBindingTypeW = 1,
    HttpServiceBindingTypeA = 2
} HTTP_SERVICE_BINDING_TYPE;

typedef enum _HTTP_SERVICE_CONFIG_CACHE_KEY {
    MaxCacheResponseSize = 0,
    CacheRangeChunkSize = 1
} HTTP_SERVICE_CONFIG_CACHE_KEY, *PHTTP_SERVICE_CONFIG_CACHE_KEY;

typedef enum _HTTP_SERVICE_CONFIG_QUERY_TYPE {
    HttpServiceConfigQueryExact = 0,
    HttpServiceConfigQueryNext = 1,
    HttpServiceConfigQueryMax = 2
} HTTP_SERVICE_CONFIG_QUERY_TYPE, *PHTTP_SERVICE_CONFIG_QUERY_TYPE;

typedef enum _HTTP_SERVICE_CONFIG_SETTING_KEY {
    HttpNone = 0,
    HttpTlsThrottle = 1
} HTTP_SERVICE_CONFIG_SETTING_KEY, *PHTTP_SERVICE_CONFIG_SETTING_KEY;

typedef enum _HTTP_SERVICE_CONFIG_TIMEOUT_KEY {
    IdleConnectionTimeout = 0,
    HeaderWaitTimeout = 1
} HTTP_SERVICE_CONFIG_TIMEOUT_KEY, *PHTTP_SERVICE_CONFIG_TIMEOUT_KEY;

typedef enum _HTTP_SSL_SERVICE_CONFIG_EX_PARAM_TYPE {
    ExParamTypeHttp2Window = 0,
    ExParamTypeHttp2SettingsLimits = 1,
    ExParamTypeHttpPerformance = 2,
    ExParamTypeTlsRestrictions = 3,
    ExParamTypeErrorHeaders = 4,
    ExParamTypeTlsSessionTicketKeys = 5,
    ExParamTypeCertConfig = 6,
    ExParamTypeMax = 7
} HTTP_SSL_SERVICE_CONFIG_EX_PARAM_TYPE, *PHTTP_SSL_SERVICE_CONFIG_EX_PARAM_TYPE;

typedef enum _HTTP_URI_SCHEME {
    HttpSchemeHttp = 0,
    HttpSchemeHttps = 1,
    HttpSchemeMaximum = 2
} HTTP_SCHEME, *PHTTP_URI_SCHEME;

typedef HTTP_OPAQUE_ID HTTP_CLIENT_CONNECTION_ID;
typedef HTTP_OPAQUE_ID HTTP_CLIENT_CREDENTIAL_ID;
typedef HTTP_OPAQUE_ID HTTP_CLIENT_REQUEST_ID;
typedef HTTP_OPAQUE_ID HTTP_CLIENT_STREAM_ID;
typedef ULONG HTTP_SERVICE_CONFIG_CACHE_PARAM;
typedef ULONG HTTP_SERVICE_CONFIG_SETTING_PARAM;
typedef USHORT HTTP_SERVICE_CONFIG_TIMEOUT_PARAM;
typedef HTTP_OPAQUE_ID * PHTTP_CLIENT_CONNECTION_ID;
typedef HTTP_OPAQUE_ID * PHTTP_CLIENT_CREDENTIAL_ID;
typedef HTTP_OPAQUE_ID * PHTTP_CLIENT_REQUEST_ID;
typedef HTTP_OPAQUE_ID * PHTTP_CLIENT_STREAM_ID;
typedef struct _HTTP_REQUEST_V1 * PHTTP_REQUEST_V1;
typedef ULONG * PHTTP_SERVICE_CONFIG_CACHE_PARAM;
typedef ULONG * PHTTP_SERVICE_CONFIG_SETTING_PARAM;
typedef USHORT * PHTTP_SERVICE_CONFIG_TIMEOUT_PARAM;

#ifdef __cplusplus
extern "C" {
#endif
ULONG WINAPI HttpShutdownRequestQueue(HANDLE RequestQueueHandle);
#ifdef __cplusplus
}
#endif

typedef struct _HTTP2_SETTINGS_LIMITS_PARAM {
  DWORD Http2MaxSettingsPerFrame;
  DWORD Http2MaxSettingsPerMinute;
} HTTP2_SETTINGS_LIMITS_PARAM, *PHTTP2_SETTINGS_LIMITS_PARAM;

typedef struct _HTTP2_WINDOW_SIZE_PARAM {
  DWORD Http2ReceiveWindowSize;
} HTTP2_WINDOW_SIZE_PARAM, *PHTTP2_WINDOW_SIZE_PARAM;

typedef struct _HTTP_BANDWIDTH_LIMIT_INFO {
  HTTP_PROPERTY_FLAGS Flags;
  ULONG MaxBandwidth;
} HTTP_BANDWIDTH_LIMIT_INFO, *PHTTP_BANDWIDTH_LIMIT_INFO;

typedef struct _HTTP_CERT_CONFIG_ENTRY {
  BYTE CertHash[20];
  WCHAR CertStoreName[128];
} HTTP_CERT_CONFIG_ENTRY, *PHTTP_CERT_CONFIG_ENTRY;

typedef struct _HTTP_CERT_CONFIG_PARAM {
  ULONG CertConfigCount;
  PHTTP_CERT_CONFIG_ENTRY CertConfigs;
} HTTP_CERT_CONFIG_PARAM, *PHTTP_CERT_CONFIG_PARAM;

typedef struct _HTTP_CONNECTION_LIMIT_INFO {
  HTTP_PROPERTY_FLAGS Flags;
  ULONG MaxConnections;
} HTTP_CONNECTION_LIMIT_INFO, *PHTTP_CONNECTION_LIMIT_INFO;

typedef struct _HTTP_CREATE_REQUEST_QUEUE_PROPERTY_INFO {
  HTTP_CREATE_REQUEST_QUEUE_PROPERTY_ID PropertyId;
  ULONG PropertyInfoLength;
  PVOID PropertyInfo;
} HTTP_CREATE_REQUEST_QUEUE_PROPERTY_INFO, *PHTTP_CREATE_REQUEST_QUEUE_PROPERTY_INFO;

typedef struct _HTTP_DELEGATE_REQUEST_PROPERTY_INFO {
  HTTP_DELEGATE_REQUEST_PROPERTY_ID PropertyId;
  ULONG PropertyInfoLength;
  PVOID PropertyInfo;
} HTTP_DELEGATE_REQUEST_PROPERTY_INFO, *PHTTP_DELEGATE_REQUEST_PROPERTY_INFO;

typedef struct _HTTP_ERROR_HEADERS_PARAM {
  USHORT StatusCode;
  USHORT HeaderCount;
  PHTTP_UNKNOWN_HEADER Headers;
} HTTP_ERROR_HEADERS_PARAM, *PHTTP_ERROR_HEADERS_PARAM;

typedef struct _HTTP_FAST_FORWARD_INFO {
  HTTP_PROPERTY_FLAGS Flags;
  BOOLEAN EnableFastForwarding;
} HTTP_FAST_FORWARD_INFO, *PHTTP_FAST_FORWARD_INFO;

typedef struct _HTTP_FLOWRATE_INFO {
  HTTP_PROPERTY_FLAGS Flags;
  ULONG MaxBandwidth;
  ULONG MaxPeakBandwidth;
  ULONG BurstSize;
} HTTP_FLOWRATE_INFO, *PHTTP_FLOWRATE_INFO;

typedef struct _HTTP_LISTEN_ENDPOINT_INFO {
  HTTP_PROPERTY_FLAGS Flags;
  BOOLEAN EnableSharing;
} HTTP_LISTEN_ENDPOINT_INFO, *PHTTP_LISTEN_ENDPOINT_INFO;

typedef struct _HTTP_LOG_FIELDS_DATA {
  HTTP_LOG_DATA Base;
  USHORT UserNameLength;
  USHORT UriStemLength;
  USHORT ClientIpLength;
  USHORT ServerNameLength;
  USHORT ServiceNameLength;
  USHORT ServerIpLength;
  USHORT MethodLength;
  USHORT UriQueryLength;
  USHORT HostLength;
  USHORT UserAgentLength;
  USHORT CookieLength;
  USHORT ReferrerLength;
  PWCHAR UserName;
  PWCHAR UriStem;
  PCHAR ClientIp;
  PCHAR ServerName;
  PCHAR ServiceName;
  PCHAR ServerIp;
  PCHAR Method;
  PCHAR UriQuery;
  PCHAR Host;
  PCHAR UserAgent;
  PCHAR Cookie;
  PCHAR Referrer;
  USHORT ServerPort;
  USHORT ProtocolStatus;
  ULONG Win32Status;
  HTTP_VERB MethodNum;
  USHORT SubStatus;
} HTTP_LOG_FIELDS_DATA, *PHTTP_LOG_FIELDS_DATA;

typedef struct _HTTP_LOGGING_INFO {
  HTTP_PROPERTY_FLAGS Flags;
  ULONG LoggingFlags;
  PCWSTR SoftwareName;
  USHORT SoftwareNameLength;
  USHORT DirectoryNameLength;
  PCWSTR DirectoryName;
  HTTP_LOGGING_TYPE Format;
  ULONG Fields;
  PVOID pExtFields;
  USHORT NumOfExtFields;
  USHORT MaxRecordSize;
  HTTP_LOGGING_ROLLOVER_TYPE RolloverType;
  ULONG RolloverSize;
  PSECURITY_DESCRIPTOR pSecurityDescriptor;
} HTTP_LOGGING_INFO, *PHTTP_LOGGING_INFO;

typedef struct _HTTP_MULTIPLE_KNOWN_HEADERS {
  HTTP_HEADER_ID HeaderId;
  ULONG Flags;
  USHORT KnownHeaderCount;
  PHTTP_KNOWN_HEADER KnownHeaders;
} HTTP_MULTIPLE_KNOWN_HEADERS, *PHTTP_MULTIPLE_KNOWN_HEADERS;

typedef struct _HTTP_PERFORMANCE_PARAM {
  HTTP_PERFORMANCE_PARAM_TYPE Type;
  ULONG BufferSize;
  PVOID Buffer;
} HTTP_PERFORMANCE_PARAM, *PHTTP_PERFORMANCE_PARAM;

typedef struct _HTTP_PROTECTION_LEVEL_INFO {
  HTTP_PROPERTY_FLAGS Flags;
  HTTP_PROTECTION_LEVEL_TYPE Level;
} HTTP_PROTECTION_LEVEL_INFO, *PHTTP_PROTECTION_LEVEL_INFO;

typedef struct _HTTP_QOS_SETTING_INFO {
  HTTP_QOS_SETTING_TYPE QosType;
  PVOID QosSetting;
} HTTP_QOS_SETTING_INFO, *PHTTP_QOS_SETTING_INFO;

typedef struct _HTTP_QUERY_REQUEST_QUALIFIER_QUIC {
  ULONGLONG Freshness;
} HTTP_QUERY_REQUEST_QUALIFIER_QUIC, *PHTTP_QUERY_REQUEST_QUALIFIER_QUIC;

typedef struct _HTTP_QUERY_REQUEST_QUALIFIER_TCP {
  ULONGLONG Freshness;
} HTTP_QUERY_REQUEST_QUALIFIER_TCP, *PHTTP_QUERY_REQUEST_QUALIFIER_TCP;

typedef struct _HTTP_QUIC_STREAM_API_TIMINGS {
  ULONGLONG OpenCount;
  ULONGLONG OpenSum;
  ULONGLONG CloseCount;
  ULONGLONG CloseSum;
  ULONGLONG StartCount;
  ULONGLONG StartSum;
  ULONGLONG ShutdownCount;
  ULONGLONG ShutdownSum;
  ULONGLONG SendCount;
  ULONGLONG SendSum;
  ULONGLONG ReceiveSetEnabledCount;
  ULONGLONG ReceiveSetEnabledSum;
  ULONGLONG GetParamCount;
  ULONGLONG GetParamSum;
  ULONGLONG SetParamCount;
  ULONGLONG SetParamSum;
  ULONGLONG SetCallbackHandlerCount;
  ULONGLONG SetCallbackHandlerSum;
} HTTP_QUIC_STREAM_API_TIMINGS, *PHTTP_QUIC_STREAM_API_TIMINGS;

typedef struct _HTTP_QUIC_CONNECTION_API_TIMINGS {
  ULONGLONG OpenTime;
  ULONGLONG CloseTime;
  ULONGLONG StartTime;
  ULONGLONG ShutdownTime;
  ULONGLONG SecConfigCreateTime;
  ULONGLONG SecConfigDeleteTime;
  ULONGLONG GetParamCount;
  ULONGLONG GetParamSum;
  ULONGLONG SetParamCount;
  ULONGLONG SetParamSum;
  ULONGLONG SetCallbackHandlerCount;
  ULONGLONG SetCallbackHandlerSum;
  HTTP_QUIC_STREAM_API_TIMINGS ControlStreamTimings;
} HTTP_QUIC_CONNECTION_API_TIMINGS, *PHTTP_QUIC_CONNECTION_API_TIMINGS;

typedef struct _HTTP_QUIC_API_TIMINGS {
  HTTP_QUIC_CONNECTION_API_TIMINGS ConnectionTimings;
  HTTP_QUIC_STREAM_API_TIMINGS StreamTimings;
} HTTP_QUIC_API_TIMINGS, *PHTTP_QUIC_API_TIMINGS;

typedef struct _HTTP_QUIC_STREAM_REQUEST_STATS {
  ULONGLONG StreamWaitStart;
  ULONGLONG StreamWaitEnd;
  ULONGLONG RequestHeadersCompressionStart;
  ULONGLONG RequestHeadersCompressionEnd;
  ULONGLONG ResponseHeadersDecompressionStart;
  ULONGLONG ResponseHeadersDecompressionEnd;
  ULONGLONG RequestHeadersCompressedSize;
  ULONGLONG ResponseHeadersCompressedSize;
} HTTP_QUIC_STREAM_REQUEST_STATS, *PHTTP_QUIC_STREAM_REQUEST_STATS;

typedef struct _HTTP_REQUEST_AUTH_INFO {
  HTTP_AUTH_STATUS AuthStatus;
  SECURITY_STATUS SecStatus;
  ULONG Flags;
  HTTP_REQUEST_AUTH_TYPE AuthType;
  HANDLE AccessToken;
  ULONG ContextAttributes;
  ULONG PackedContextLength;
  ULONG PackedContextType;
  PVOID PackedContext;
  ULONG MutualAuthDataLength;
  PCHAR pMutualAuthData;
  USHORT PackageNameLength;
  PWSTR pPackageName;
} HTTP_REQUEST_AUTH_INFO, *PHTTP_REQUEST_AUTH_INFO;

typedef struct _HTTP_REQUEST_DSCP_INFO {
  BYTE DscpTag;
} HTTP_REQUEST_DSCP_INFO, *PHTTP_REQUEST_DSCP_INFO;

typedef struct _HTTP_REQUEST_INFO_PROPERTY_INFO {
  HTTP_PROPERTY_FLAGS Flags;
  ULONGLONG RequestInfoFlags;
} HTTP_REQUEST_INFO_PROPERTY_INFO, *PHTTP_REQUEST_INFO_PROPERTY_INFO;

typedef struct _HTTP_REQUEST_INITIAL_PACKET_TTL_INFO {
  BYTE InitialPacketTtl;
} HTTP_REQUEST_INITIAL_PACKET_TTL_INFO, *PHTTP_REQUEST_INITIAL_PACKET_TTL_INFO;

typedef struct _HTTP_REQUEST_PROPERTY_SNI {
  WCHAR Hostname[256];
  ULONG Flags;
} HTTP_REQUEST_PROPERTY_SNI, *PHTTP_REQUEST_PROPERTY_SNI;

typedef struct _HTTP_REQUEST_PROPERTY_STREAM_ERROR {
  ULONG ErrorCode;
} HTTP_REQUEST_PROPERTY_STREAM_ERROR, *PHTTP_REQUEST_PROPERTY_STREAM_ERROR;

typedef struct _HTTP_REQUEST_SIZING_INFO {
  ULONGLONG Flags;
  ULONG RequestIndex;
  ULONG RequestSizingCount;
  ULONGLONG RequestSizing[5];
} HTTP_REQUEST_SIZING_INFO, *PHTTP_REQUEST_SIZING_INFO;

typedef struct _HTTP_REQUEST_TIMING_INFO {
  ULONG RequestTimingCount;
  ULONGLONG RequestTiming[30];
} HTTP_REQUEST_TIMING_INFO, *PHTTP_REQUEST_TIMING_INFO;

typedef struct _HTTP_REQUEST_TOKEN_BINDING_INFO {
  PUCHAR TokenBinding;
  ULONG TokenBindingSize;
  PUCHAR EKM;
  ULONG EKMSize;
  UCHAR KeyType;
} HTTP_REQUEST_TOKEN_BINDING_INFO, *PHTTP_REQUEST_TOKEN_BINDING_INFO;

typedef struct _HTTP_REQUEST_TRANSPORT_IDLE_CONNECTION_TIMEOUT_INFO {
  USHORT TransportIdleConnectionTimeout;
} HTTP_REQUEST_TRANSPORT_IDLE_CONNECTION_TIMEOUT_INFO, *PHTTP_REQUEST_TRANSPORT_IDLE_CONNECTION_TIMEOUT_INFO;

typedef struct _HTTP_SERVER_AUTHENTICATION_BASIC_PARAMS {
  USHORT RealmLength;
  PWSTR Realm;
} HTTP_SERVER_AUTHENTICATION_BASIC_PARAMS, *PHTTP_SERVER_AUTHENTICATION_BASIC_PARAMS;

typedef struct _HTTP_SERVER_AUTHENTICATION_DIGEST_PARAMS {
  USHORT DomainNameLength;
  PWSTR DomainName;
  USHORT RealmLength;
  PWSTR Realm;
} HTTP_SERVER_AUTHENTICATION_DIGEST_PARAMS, *PHTTP_SERVER_AUTHENTICATION_DIGEST_PARAMS;

typedef struct _HTTP_SERVER_AUTHENTICATION_INFO {
  HTTP_PROPERTY_FLAGS Flags;
  ULONG AuthSchemes;
  BOOLEAN ReceiveMutualAuth;
  BOOLEAN ReceiveContextHandle;
  BOOLEAN DisableNTLMCredentialCaching;
  UCHAR ExFlags;
  HTTP_SERVER_AUTHENTICATION_DIGEST_PARAMS DigestParams;
  HTTP_SERVER_AUTHENTICATION_BASIC_PARAMS BasicParams;
} HTTP_SERVER_AUTHENTICATION_INFO, *PHTTP_SERVER_AUTHENTICATION_INFO;

typedef struct _HTTP_SERVICE_BINDING_BASE {
  HTTP_SERVICE_BINDING_TYPE Type;
} HTTP_SERVICE_BINDING_BASE, *PHTTP_SERVICE_BINDING_BASE;

typedef struct _HTTP_CHANNEL_BIND_INFO {
  HTTP_AUTHENTICATION_HARDENING_LEVELS Hardening;
  ULONG Flags;
  PHTTP_SERVICE_BINDING_BASE * ServiceNames;
  ULONG NumberOfServiceNames;
} HTTP_CHANNEL_BIND_INFO, *PHTTP_CHANNEL_BIND_INFO;

typedef struct _HTTP_REQUEST_CHANNEL_BIND_STATUS {
  PHTTP_SERVICE_BINDING_BASE ServiceName;
  PUCHAR ChannelToken;
  ULONG ChannelTokenSize;
  ULONG Flags;
} HTTP_REQUEST_CHANNEL_BIND_STATUS, *PHTTP_REQUEST_CHANNEL_BIND_STATUS;

typedef struct _HTTP_SERVICE_BINDING_A {
  HTTP_SERVICE_BINDING_BASE Base;
  PCHAR Buffer;
  ULONG BufferSize;
} HTTP_SERVICE_BINDING_A, *PHTTP_SERVICE_BINDING_A;

typedef struct _HTTP_SERVICE_BINDING_W {
  HTTP_SERVICE_BINDING_BASE Base;
  PWCHAR Buffer;
  ULONG BufferSize;
} HTTP_SERVICE_BINDING_W, *PHTTP_SERVICE_BINDING_W;

typedef struct _HTTP_SERVICE_CONFIG_IP_LISTEN_PARAM {
  USHORT AddrLength;
  PSOCKADDR pAddress;
} HTTP_SERVICE_CONFIG_IP_LISTEN_PARAM, *PHTTP_SERVICE_CONFIG_IP_LISTEN_PARAM;

typedef struct _HTTP_SERVICE_CONFIG_IP_LISTEN_QUERY {
  ULONG AddrCount;
  SOCKADDR_STORAGE AddrList[1];
} HTTP_SERVICE_CONFIG_IP_LISTEN_QUERY, *PHTTP_SERVICE_CONFIG_IP_LISTEN_QUERY;

typedef struct _HTTP_SERVICE_CONFIG_SETTING_SET {
  HTTP_SERVICE_CONFIG_SETTING_KEY KeyDesc;
  HTTP_SERVICE_CONFIG_SETTING_PARAM ParamDesc;
} HTTP_SERVICE_CONFIG_SETTING_SET, *PHTTP_SERVICE_CONFIG_SETTING_SET;

typedef struct _HTTP_SERVICE_CONFIG_SSL_CCS_KEY {
  SOCKADDR_STORAGE LocalAddress;
} HTTP_SERVICE_CONFIG_SSL_CCS_KEY, *PHTTP_SERVICE_CONFIG_SSL_CCS_KEY;

typedef struct _HTTP_SERVICE_CONFIG_SSL_CCS_QUERY {
  HTTP_SERVICE_CONFIG_QUERY_TYPE QueryDesc;
  HTTP_SERVICE_CONFIG_SSL_CCS_KEY KeyDesc;
  DWORD dwToken;
} HTTP_SERVICE_CONFIG_SSL_CCS_QUERY, *PHTTP_SERVICE_CONFIG_SSL_CCS_QUERY;

typedef struct _HTTP_SERVICE_CONFIG_SSL_CCS_QUERY_EX {
  HTTP_SERVICE_CONFIG_QUERY_TYPE QueryDesc;
  HTTP_SERVICE_CONFIG_SSL_CCS_KEY KeyDesc;
  DWORD dwToken;
  HTTP_SSL_SERVICE_CONFIG_EX_PARAM_TYPE ParamType;
} HTTP_SERVICE_CONFIG_SSL_CCS_QUERY_EX, *PHTTP_SERVICE_CONFIG_SSL_CCS_QUERY_EX;

typedef struct _HTTP_SERVICE_CONFIG_SSL_KEY {
  PSOCKADDR pIpPort;
} HTTP_SERVICE_CONFIG_SSL_KEY, *PHTTP_SERVICE_CONFIG_SSL_KEY;

typedef struct _HTTP_SERVICE_CONFIG_SSL_KEY_EX {
  SOCKADDR_STORAGE IpPort;
} HTTP_SERVICE_CONFIG_SSL_KEY_EX, *PHTTP_SERVICE_CONFIG_SSL_KEY_EX;

typedef struct _HTTP_SERVICE_CONFIG_SSL_PARAM {
  ULONG SslHashLength;
  PVOID pSslHash;
  GUID AppId;
  PWSTR pSslCertStoreName;
  DWORD DefaultCertCheckMode;
  DWORD DefaultRevocationFreshnessTime;
  DWORD DefaultRevocationUrlRetrievalTimeout;
  PWSTR pDefaultSslCtlIdentifier;
  PWSTR pDefaultSslCtlStoreName;
  DWORD DefaultFlags;
} HTTP_SERVICE_CONFIG_SSL_PARAM, *PHTTP_SERVICE_CONFIG_SSL_PARAM;

typedef struct _HTTP_SERVICE_CONFIG_SSL_CCS_SET {
  HTTP_SERVICE_CONFIG_SSL_CCS_KEY KeyDesc;
  HTTP_SERVICE_CONFIG_SSL_PARAM ParamDesc;
} HTTP_SERVICE_CONFIG_SSL_CCS_SET, *PHTTP_SERVICE_CONFIG_SSL_CCS_SET;

typedef struct _HTTP_SERVICE_CONFIG_SSL_QUERY {
  HTTP_SERVICE_CONFIG_QUERY_TYPE QueryDesc;
  HTTP_SERVICE_CONFIG_SSL_KEY KeyDesc;
  DWORD dwToken;
} HTTP_SERVICE_CONFIG_SSL_QUERY, *PHTTP_SERVICE_CONFIG_SSL_QUERY;

typedef struct _HTTP_SERVICE_CONFIG_SSL_QUERY_EX {
  HTTP_SERVICE_CONFIG_QUERY_TYPE QueryDesc;
  HTTP_SERVICE_CONFIG_SSL_KEY_EX KeyDesc;
  DWORD dwToken;
  HTTP_SSL_SERVICE_CONFIG_EX_PARAM_TYPE ParamType;
} HTTP_SERVICE_CONFIG_SSL_QUERY_EX, *PHTTP_SERVICE_CONFIG_SSL_QUERY_EX;

typedef struct _HTTP_SERVICE_CONFIG_SSL_SET {
  HTTP_SERVICE_CONFIG_SSL_KEY KeyDesc;
  HTTP_SERVICE_CONFIG_SSL_PARAM ParamDesc;
} HTTP_SERVICE_CONFIG_SSL_SET, *PHTTP_SERVICE_CONFIG_SSL_SET;

typedef struct _HTTP_SERVICE_CONFIG_SSL_SNI_KEY {
  SOCKADDR_STORAGE IpPort;
  PWSTR Host;
} HTTP_SERVICE_CONFIG_SSL_SNI_KEY, *PHTTP_SERVICE_CONFIG_SSL_SNI_KEY;

typedef struct _HTTP_SERVICE_CONFIG_SSL_SNI_QUERY {
  HTTP_SERVICE_CONFIG_QUERY_TYPE QueryDesc;
  HTTP_SERVICE_CONFIG_SSL_SNI_KEY KeyDesc;
  DWORD dwToken;
} HTTP_SERVICE_CONFIG_SSL_SNI_QUERY, *PHTTP_SERVICE_CONFIG_SSL_SNI_QUERY;

typedef struct _HTTP_SERVICE_CONFIG_SSL_SNI_QUERY_EX {
  HTTP_SERVICE_CONFIG_QUERY_TYPE QueryDesc;
  HTTP_SERVICE_CONFIG_SSL_SNI_KEY KeyDesc;
  DWORD dwToken;
  HTTP_SSL_SERVICE_CONFIG_EX_PARAM_TYPE ParamType;
} HTTP_SERVICE_CONFIG_SSL_SNI_QUERY_EX, *PHTTP_SERVICE_CONFIG_SSL_SNI_QUERY_EX;

typedef struct _HTTP_SERVICE_CONFIG_SSL_SNI_SET {
  HTTP_SERVICE_CONFIG_SSL_SNI_KEY KeyDesc;
  HTTP_SERVICE_CONFIG_SSL_PARAM ParamDesc;
} HTTP_SERVICE_CONFIG_SSL_SNI_SET, *PHTTP_SERVICE_CONFIG_SSL_SNI_SET;

typedef struct _HTTP_SERVICE_CONFIG_TIMEOUT_SET {
  HTTP_SERVICE_CONFIG_TIMEOUT_KEY KeyDesc;
  HTTP_SERVICE_CONFIG_TIMEOUT_PARAM ParamDesc;
} HTTP_SERVICE_CONFIG_TIMEOUT_SET, *PHTTP_SERVICE_CONFIG_TIMEOUT_SET;

typedef struct _HTTP_SERVICE_CONFIG_URLACL_KEY {
  PWSTR pUrlPrefix;
} HTTP_SERVICE_CONFIG_URLACL_KEY, *PHTTP_SERVICE_CONFIG_URLACL_KEY;

typedef struct _HTTP_SERVICE_CONFIG_URLACL_PARAM {
  PWSTR pStringSecurityDescriptor;
} HTTP_SERVICE_CONFIG_URLACL_PARAM, *PHTTP_SERVICE_CONFIG_URLACL_PARAM;

typedef struct _HTTP_SERVICE_CONFIG_URLACL_QUERY {
  HTTP_SERVICE_CONFIG_QUERY_TYPE QueryDesc;
  HTTP_SERVICE_CONFIG_URLACL_KEY KeyDesc;
  DWORD dwToken;
} HTTP_SERVICE_CONFIG_URLACL_QUERY, *PHTTP_SERVICE_CONFIG_URLACL_QUERY;

typedef struct _HTTP_SERVICE_CONFIG_URLACL_SET {
  HTTP_SERVICE_CONFIG_URLACL_KEY KeyDesc;
  HTTP_SERVICE_CONFIG_URLACL_PARAM ParamDesc;
} HTTP_SERVICE_CONFIG_URLACL_SET, *PHTTP_SERVICE_CONFIG_URLACL_SET;

typedef struct _HTTP_SSL_PROTOCOL_INFO {
  ULONG Protocol;
  ULONG CipherType;
  ULONG CipherStrength;
  ULONG HashType;
  ULONG HashStrength;
  ULONG KeyExchangeType;
  ULONG KeyExchangeStrength;
} HTTP_SSL_PROTOCOL_INFO, *PHTTP_SSL_PROTOCOL_INFO;

typedef struct _HTTP_STATE_INFO {
  HTTP_PROPERTY_FLAGS Flags;
  HTTP_ENABLED_STATE State;
} HTTP_STATE_INFO, *PHTTP_STATE_INFO;

typedef struct _HTTP_TIMEOUT_LIMIT_INFO {
  HTTP_PROPERTY_FLAGS Flags;
  USHORT EntityBody;
  USHORT DrainEntityBody;
  USHORT RequestQueue;
  USHORT IdleConnection;
  USHORT HeaderWait;
  ULONG MinSendRate;
} HTTP_TIMEOUT_LIMIT_INFO, *PHTTP_TIMEOUT_LIMIT_INFO;

typedef struct _HTTP_TLS_RESTRICTIONS_PARAM {
  ULONG RestrictionCount;
  PVOID TlsRestrictions;
} HTTP_TLS_RESTRICTIONS_PARAM, *PHTTP_TLS_RESTRICTIONS_PARAM;

typedef struct _HTTP_TLS_SESSION_TICKET_KEYS_PARAM {
  ULONG SessionTicketKeyCount;
  PVOID SessionTicketKeys;
} HTTP_TLS_SESSION_TICKET_KEYS_PARAM, *PHTTP_TLS_SESSION_TICKET_KEYS_PARAM;

typedef struct _HTTP_WINHTTP_FAST_FORWARDING_DATA {
  UCHAR Reserved[16];
} HTTP_WINHTTP_FAST_FORWARDING_DATA, *PHTTP_WINHTTP_FAST_FORWARDING_DATA;

typedef struct _HTTP_WSK_API_TIMINGS {
  ULONGLONG ConnectCount;
  ULONGLONG ConnectSum;
  ULONGLONG DisconnectCount;
  ULONGLONG DisconnectSum;
  ULONGLONG SendCount;
  ULONGLONG SendSum;
  ULONGLONG ReceiveCount;
  ULONGLONG ReceiveSum;
  ULONGLONG ReleaseCount;
  ULONGLONG ReleaseSum;
  ULONGLONG ControlSocketCount;
  ULONGLONG ControlSocketSum;
} HTTP_WSK_API_TIMINGS, *PHTTP_WSK_API_TIMINGS;

typedef struct HTTP_SERVICE_CONFIG_CACHE_SET {
  HTTP_SERVICE_CONFIG_CACHE_KEY KeyDesc;
  HTTP_SERVICE_CONFIG_CACHE_PARAM ParamDesc;
} HTTP_SERVICE_CONFIG_CACHE_SET, *PHTTP_SERVICE_CONFIG_CACHE_SET;

#define HTTP_AUTH_ENABLE_ALL 0x1F
#define HTTP_CERT_CHECK_MODE_NO_REVOCATION 0x00001
#define HTTP_LIMIT_INFINITE ((ULONG)0xFFFFFFFF)
#define HTTP_MIN_ALLOWED_BANDWIDTH_THROTTLING_RATE ((ULONG)0x400)
#define HTTP_MIN_ALLOWED_LOG_FILE_ROLLOVER_SIZE ((ULONG)0x100000)
#define HTTP_QUIC_KEEPALIVE_TIMEOUT_DISABLED ((ULONG)0xFFFFFFFF)
#define HTTP_REQUEST_FLAG_FAST_FORWARDING_RESPONSE_ALLOWED 0x10
#define HTTP_VERSION_0_9 { 0, 9 }
#define HTTP_VERSION_1_0 { 1, 0 }
#define HTTP_VERSION_1_1 { 1, 1 }
#define HTTP_VERSION_2_0 { 2, 0 }
#define HTTP_VERSION_3_0 { 3, 0 }
#define HTTP_VERSION_UNKNOWN { 0, 0 }
#ifndef HTTPAPI_LINKAGE
#define HTTPAPI_LINKAGE DECLSPEC_IMPORT
#endif

struct _HTTP_SERVICE_CONFIG_SSL_PARAM_EX
{
    HTTP_SSL_SERVICE_CONFIG_EX_PARAM_TYPE ParamType;
    ULONGLONG Flags;
    union
    {
        HTTP2_WINDOW_SIZE_PARAM Http2WindowSizeParam;
        HTTP2_SETTINGS_LIMITS_PARAM Http2SettingsLimitsParam;
        HTTP_PERFORMANCE_PARAM HttpPerformanceParam;
        HTTP_TLS_RESTRICTIONS_PARAM HttpTlsRestrictionsParam;
        HTTP_ERROR_HEADERS_PARAM HttpErrorHeadersParam;
        HTTP_TLS_SESSION_TICKET_KEYS_PARAM HttpTlsSessionTicketKeysParam;
        HTTP_CERT_CONFIG_PARAM HttpCertConfigParam;
    };
};

typedef struct _HTTP_SERVICE_CONFIG_SSL_PARAM_EX HTTP_SERVICE_CONFIG_SSL_PARAM_EX;

struct _HTTP_SERVICE_CONFIG_SSL_CCS_SET_EX
{
    HTTP_SERVICE_CONFIG_SSL_CCS_KEY KeyDesc;
    HTTP_SERVICE_CONFIG_SSL_PARAM_EX ParamDesc;
};

struct _HTTP_SERVICE_CONFIG_SSL_SET_EX
{
    HTTP_SERVICE_CONFIG_SSL_KEY_EX KeyDesc;
    HTTP_SERVICE_CONFIG_SSL_PARAM_EX ParamDesc;
};

struct _HTTP_SERVICE_CONFIG_SSL_SNI_SET_EX
{
    HTTP_SERVICE_CONFIG_SSL_SNI_KEY KeyDesc;
    HTTP_SERVICE_CONFIG_SSL_PARAM_EX ParamDesc;
};

typedef struct _HTTP_SERVICE_CONFIG_SSL_CCS_SET_EX HTTP_SERVICE_CONFIG_SSL_CCS_SET_EX;

typedef struct _HTTP_SERVICE_CONFIG_SSL_SET_EX HTTP_SERVICE_CONFIG_SSL_SET_EX;

typedef struct _HTTP_SERVICE_CONFIG_SSL_SNI_SET_EX HTTP_SERVICE_CONFIG_SSL_SNI_SET_EX;

typedef struct _HTTP_SERVICE_CONFIG_SSL_CCS_SET_EX *PHTTP_SERVICE_CONFIG_SSL_CCS_SET_EX;

typedef struct _HTTP_SERVICE_CONFIG_SSL_PARAM_EX *PHTTP_SERVICE_CONFIG_SSL_PARAM_EX;

typedef struct _HTTP_SERVICE_CONFIG_SSL_SET_EX *PHTTP_SERVICE_CONFIG_SSL_SET_EX;

typedef struct _HTTP_SERVICE_CONFIG_SSL_SNI_SET_EX *PHTTP_SERVICE_CONFIG_SSL_SNI_SET_EX;

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(_NTDDK_) && !defined(_NTIFS_)
ULONG WINAPI HttpReceiveRequestEntityBody(HANDLE RequestQueueHandle, HTTP_REQUEST_ID RequestId, ULONG Flags, PVOID EntityBuffer, ULONG EntityBufferLength, PULONG BytesReturned, LPOVERLAPPED Overlapped);

/* TODO: LiberNT does not implement the functions below yet */
ULONG WINAPI HttpAddFragmentToCache(HANDLE RequestQueueHandle, PCWSTR UrlPrefix, PHTTP_DATA_CHUNK DataChunk, PHTTP_CACHE_POLICY CachePolicy, LPOVERLAPPED Overlapped);
ULONG WINAPI HttpCancelHttpRequest(HANDLE RequestQueueHandle, HTTP_REQUEST_ID RequestId, LPOVERLAPPED Overlapped);
ULONG WINAPI HttpDeclarePush(HANDLE RequestQueueHandle, HTTP_REQUEST_ID RequestId, HTTP_VERB Verb, PCWSTR Path, PCSTR Query, PHTTP_REQUEST_HEADERS Headers);
ULONG WINAPI HttpDelegateRequestEx(HANDLE RequestQueueHandle, HANDLE DelegateQueueHandle, HTTP_REQUEST_ID RequestId, HTTP_URL_GROUP_ID DelegateUrlGroupId, ULONG PropertyInfoSetSize, PHTTP_DELEGATE_REQUEST_PROPERTY_INFO PropertyInfoSet);
ULONG WINAPI HttpFindUrlGroupId(PCWSTR FullyQualifiedUrl, HANDLE RequestQueueHandle, PHTTP_URL_GROUP_ID UrlGroupId);
ULONG WINAPI HttpFlushResponseCache(HANDLE RequestQueueHandle, PCWSTR UrlPrefix, ULONG Flags, LPOVERLAPPED Overlapped);
ULONG WINAPI HttpGetExtension(HTTPAPI_VERSION Version, ULONG Extension, PVOID Buffer, ULONG BufferSize);
BOOL WINAPI HttpIsFeatureSupported(HTTP_FEATURE_ID FeatureId);
ULONG WINAPI HttpPrepareUrl(PVOID Reserved, ULONG Flags, PCWSTR Url, PWSTR *PreparedUrl);
ULONG WINAPI HttpQueryRequestProperty(HANDLE RequestQueueHandle, HTTP_OPAQUE_ID Id, HTTP_REQUEST_PROPERTY PropertyId, const void *Qualifier, ULONG QualifierSize, PVOID Output, ULONG OutputBufferSize, PULONG BytesReturned, LPOVERLAPPED Overlapped);
ULONG WINAPI HttpQueryRequestQueueProperty(HANDLE RequestQueueHandle, HTTP_SERVER_PROPERTY Property, PVOID PropertyInformation, ULONG PropertyInformationLength, ULONG Reserved1, PULONG ReturnLength, PVOID Reserved2);
ULONG WINAPI HttpQueryServerSessionProperty(HTTP_SERVER_SESSION_ID ServerSessionId, HTTP_SERVER_PROPERTY Property, PVOID PropertyInformation, ULONG PropertyInformationLength, PULONG ReturnLength);
ULONG WINAPI HttpQueryUrlGroupProperty(HTTP_URL_GROUP_ID UrlGroupId, HTTP_SERVER_PROPERTY Property, PVOID PropertyInformation, ULONG PropertyInformationLength, PULONG ReturnLength);
ULONG WINAPI HttpReadFragmentFromCache(HANDLE RequestQueueHandle, PCWSTR UrlPrefix, PHTTP_BYTE_RANGE ByteRange, PVOID Buffer, ULONG BufferLength, PULONG BytesRead, LPOVERLAPPED Overlapped);
ULONG WINAPI HttpReceiveClientCertificate(HANDLE RequestQueueHandle, HTTP_CONNECTION_ID ConnectionId, ULONG Flags, PHTTP_SSL_CLIENT_CERT_INFO SslClientCertInfo, ULONG SslClientCertInfoSize, PULONG BytesReceived, LPOVERLAPPED Overlapped);
ULONG WINAPI HttpSendResponseEntityBody(HANDLE RequestQueueHandle, HTTP_REQUEST_ID RequestId, ULONG Flags, USHORT EntityChunkCount, PHTTP_DATA_CHUNK EntityChunks, PULONG BytesSent, PVOID Reserved1, ULONG Reserved2, LPOVERLAPPED Overlapped, PHTTP_LOG_DATA LogData);
ULONG WINAPI HttpSetRequestProperty(HANDLE RequestQueueHandle, HTTP_OPAQUE_ID Id, HTTP_REQUEST_PROPERTY PropertyId, PVOID Input, ULONG InputPropertySize, LPOVERLAPPED Overlapped);
ULONG WINAPI HttpSetRequestQueueProperty(HANDLE RequestQueueHandle, HTTP_SERVER_PROPERTY Property, PVOID PropertyInformation, ULONG PropertyInformationLength, ULONG Reserved1, PVOID Reserved2);
ULONG WINAPI HttpSetServerSessionProperty(HTTP_SERVER_SESSION_ID ServerSessionId, HTTP_SERVER_PROPERTY Property, PVOID PropertyInformation, ULONG PropertyInformationLength);
ULONG WINAPI HttpUpdateServiceConfiguration(HANDLE Handle, HTTP_SERVICE_CONFIG_ID ConfigId, PVOID ConfigInfo, ULONG ConfigInfoLength, LPOVERLAPPED Overlapped);
ULONG WINAPI HttpWaitForDemandStart(HANDLE RequestQueueHandle, LPOVERLAPPED Overlapped);
ULONG WINAPI HttpWaitForDisconnect(HANDLE RequestQueueHandle, HTTP_CONNECTION_ID ConnectionId, LPOVERLAPPED Overlapped);
ULONG WINAPI HttpWaitForDisconnectEx(HANDLE RequestQueueHandle, HTTP_CONNECTION_ID ConnectionId, ULONG Reserved, LPOVERLAPPED Overlapped);
#endif

#ifdef __cplusplus
}
#endif

#define HTTP_EQUAL_VERSION(version, major, minor) ((version) . MajorVersion == (major) && (version) . MinorVersion == (minor))
#define HTTP_GREATER_EQUAL_VERSION(version, major, minor) (!HTTP_LESS_VERSION(version, major, minor))
#define HTTP_GREATER_VERSION(version, major, minor) ((version) . MajorVersion > (major) || ((version) . MajorVersion == (major) && (version) . MinorVersion > (minor)))
#define HTTP_IS_NULL_ID(pid) (HTTP_NULL_ID == * (pid))
#define HTTP_LESS_EQUAL_VERSION(version, major, minor) (!HTTP_GREATER_VERSION(version, major, minor))
#define HTTP_LESS_VERSION(version, major, minor) ((version) . MajorVersion < (major) || ((version) . MajorVersion == (major) && (version) . MinorVersion < (minor)))
#define HTTP_NOT_EQUAL_VERSION(version, major, minor) (!HTTP_EQUAL_VERSION(version, major, minor))
#define HTTP_SET_NULL_ID(pid) (* (pid) = HTTP_NULL_ID)
#define HTTP_SET_VERSION(version, major, minor) do { (version) . MajorVersion = (major); (version) . MinorVersion = (minor); } while (0)
#define HTTPAPI_EQUAL_VERSION(version, major, minor) ((version) . HttpApiMajorVersion == (major) && (version) . HttpApiMinorVersion == (minor))
#define HTTPAPI_GREATER_VERSION(version, major, minor) ((version) . HttpApiMajorVersion > (major) || ((version) . HttpApiMajorVersion == (major) && (version) . HttpApiMinorVersion > (minor)))
#define HTTPAPI_LESS_VERSION(version, major, minor) ((version) . HttpApiMajorVersion < (major) || ((version) . HttpApiMajorVersion == (major) && (version) . HttpApiMinorVersion < (minor)))
#define HTTPAPI_VERSION_GREATER_OR_EQUAL(version, major, minor) (!HTTPAPI_LESS_VERSION(version, major, minor))

#endif  /* __WINE_HTTP_H */
