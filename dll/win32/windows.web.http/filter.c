/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.Web.Http.Filters.HttpBaseProtocolFilter over WinHTTP
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(webhttp);

struct cache_control
{
    IHttpCacheControl IHttpCacheControl_iface;
    LONG ref;
    LONG read_behavior;
    LONG write_behavior;
};

static inline struct cache_control *impl_from_IHttpCacheControl( IHttpCacheControl *iface )
{
    return CONTAINING_RECORD( iface, struct cache_control, IHttpCacheControl_iface );
}

static HRESULT WINAPI cache_QueryInterface( IHttpCacheControl *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IHttpCacheControl ))
    {
        IHttpCacheControl_AddRef( (*out = iface) );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI cache_AddRef( IHttpCacheControl *iface )
{
    return InterlockedIncrement( &impl_from_IHttpCacheControl( iface )->ref );
}

static ULONG WINAPI cache_Release( IHttpCacheControl *iface )
{
    struct cache_control *impl = impl_from_IHttpCacheControl( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI cache_GetIids( IHttpCacheControl *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI cache_GetRuntimeClassName( IHttpCacheControl *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_Filters_HttpCacheControl;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI cache_GetTrustLevel( IHttpCacheControl *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI cache_get_ReadBehavior( IHttpCacheControl *iface, HttpCacheReadBehavior *value )
{
    if (!value) return E_POINTER;
    *value = ReadAcquire( &impl_from_IHttpCacheControl( iface )->read_behavior );
    return S_OK;
}

static HRESULT WINAPI cache_put_ReadBehavior( IHttpCacheControl *iface, HttpCacheReadBehavior value )
{
    if (value < HttpCacheReadBehavior_Default || value > HttpCacheReadBehavior_NoCache) return E_INVALIDARG;
    WriteRelease( &impl_from_IHttpCacheControl( iface )->read_behavior, value );
    return S_OK;
}

static HRESULT WINAPI cache_get_WriteBehavior( IHttpCacheControl *iface, HttpCacheWriteBehavior *value )
{
    if (!value) return E_POINTER;
    *value = ReadAcquire( &impl_from_IHttpCacheControl( iface )->write_behavior );
    return S_OK;
}

static HRESULT WINAPI cache_put_WriteBehavior( IHttpCacheControl *iface, HttpCacheWriteBehavior value )
{
    if (value < HttpCacheWriteBehavior_Default || value > HttpCacheWriteBehavior_NoCache) return E_INVALIDARG;
    WriteRelease( &impl_from_IHttpCacheControl( iface )->write_behavior, value );
    return S_OK;
}

static const struct IHttpCacheControlVtbl cache_vtbl =
{
    cache_QueryInterface,
    cache_AddRef,
    cache_Release,
    cache_GetIids,
    cache_GetRuntimeClassName,
    cache_GetTrustLevel,
    cache_get_ReadBehavior,
    cache_put_ReadBehavior,
    cache_get_WriteBehavior,
    cache_put_WriteBehavior,
};

struct filter
{
    IHttpBaseProtocolFilter IHttpBaseProtocolFilter_iface;
    IHttpFilter IHttpFilter_iface;
    IClosable IClosable_iface;
    LONG ref;
    SRWLOCK lock;
    HINTERNET session;
    struct cache_control *cache;
    boolean allow_redirect;
    boolean allow_ui;
    boolean decompression;
    boolean use_proxy;
    UINT32 max_connections;
};

static const struct IHttpFilterVtbl filter_http_vtbl;

static inline struct filter *impl_from_IHttpBaseProtocolFilter( IHttpBaseProtocolFilter *iface )
{
    return CONTAINING_RECORD( iface, struct filter, IHttpBaseProtocolFilter_iface );
}

static HRESULT WINAPI filter_QueryInterface( IHttpBaseProtocolFilter *iface, REFIID iid, void **out )
{
    struct filter *impl = impl_from_IHttpBaseProtocolFilter( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IHttpBaseProtocolFilter ))
        *out = &impl->IHttpBaseProtocolFilter_iface;
    else if (IsEqualGUID( iid, &IID_IHttpFilter ))
        *out = &impl->IHttpFilter_iface;
    else if (IsEqualGUID( iid, &IID_IClosable ))
        *out = &impl->IClosable_iface;
    else
    {
        /* TODO: IHttpBaseProtocolFilter2 to IHttpBaseProtocolFilter5 */
        *out = NULL;
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI filter_AddRef( IHttpBaseProtocolFilter *iface )
{
    return InterlockedIncrement( &impl_from_IHttpBaseProtocolFilter( iface )->ref );
}

static ULONG WINAPI filter_Release( IHttpBaseProtocolFilter *iface )
{
    struct filter *impl = impl_from_IHttpBaseProtocolFilter( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->session) WinHttpCloseHandle( impl->session );
        IHttpCacheControl_Release( &impl->cache->IHttpCacheControl_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI filter_GetIids( IHttpBaseProtocolFilter *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI filter_GetRuntimeClassName( IHttpBaseProtocolFilter *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_Filters_HttpBaseProtocolFilter;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI filter_GetTrustLevel( IHttpBaseProtocolFilter *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

#define FILTER_FLAG( member, field )                                                               \
    static HRESULT WINAPI filter_get_##member( IHttpBaseProtocolFilter *iface, boolean *value )    \
    {                                                                                              \
        struct filter *impl = impl_from_IHttpBaseProtocolFilter( iface );                          \
        if (!value) return E_POINTER;                                                              \
        AcquireSRWLockShared( &impl->lock );                                                       \
        *value = impl->field;                                                                      \
        ReleaseSRWLockShared( &impl->lock );                                                       \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI filter_put_##member( IHttpBaseProtocolFilter *iface, boolean value )     \
    {                                                                                              \
        struct filter *impl = impl_from_IHttpBaseProtocolFilter( iface );                          \
        AcquireSRWLockExclusive( &impl->lock );                                                    \
        impl->field = !!value;                                                                     \
        ReleaseSRWLockExclusive( &impl->lock );                                                    \
        return S_OK;                                                                               \
    }

FILTER_FLAG( AllowAutoRedirect, allow_redirect )
FILTER_FLAG( AllowUI, allow_ui )
FILTER_FLAG( AutomaticDecompression, decompression )

static HRESULT WINAPI filter_get_UseProxy( IHttpBaseProtocolFilter *iface, boolean *value )
{
    struct filter *impl = impl_from_IHttpBaseProtocolFilter( iface );

    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *value = impl->use_proxy;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI filter_put_UseProxy( IHttpBaseProtocolFilter *iface, boolean value )
{
    struct filter *impl = impl_from_IHttpBaseProtocolFilter( iface );
    HINTERNET session = NULL;

    AcquireSRWLockExclusive( &impl->lock );
    if (impl->use_proxy != !!value)
    {
        impl->use_proxy = !!value;
        session = impl->session;
        impl->session = NULL;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    if (session) WinHttpCloseHandle( session );
    return S_OK;
}

static HRESULT WINAPI filter_get_CacheControl( IHttpBaseProtocolFilter *iface, IHttpCacheControl **value )
{
    struct filter *impl = impl_from_IHttpBaseProtocolFilter( iface );

    if (!value) return E_POINTER;
    IHttpCacheControl_AddRef( (*value = &impl->cache->IHttpCacheControl_iface) );
    return S_OK;
}

static HRESULT WINAPI filter_get_CookieManager( IHttpBaseProtocolFilter *iface, IHttpCookieManager **value )
{
    /* TODO: HttpCookieManager */
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI filter_get_ClientCertificate( IHttpBaseProtocolFilter *iface, ICertificate **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI filter_put_ClientCertificate( IHttpBaseProtocolFilter *iface, ICertificate *value )
{
    /* TODO: client certificates */
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI filter_get_IgnorableServerCertificateErrors( IHttpBaseProtocolFilter *iface,
                                                                   IVector_ChainValidationResult **value )
{
    /* TODO: IgnorableServerCertificateErrors */
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI filter_get_MaxConnectionsPerServer( IHttpBaseProtocolFilter *iface, UINT32 *value )
{
    struct filter *impl = impl_from_IHttpBaseProtocolFilter( iface );

    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *value = impl->max_connections;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI filter_put_MaxConnectionsPerServer( IHttpBaseProtocolFilter *iface, UINT32 value )
{
    struct filter *impl = impl_from_IHttpBaseProtocolFilter( iface );

    if (!value) return E_INVALIDARG;
    AcquireSRWLockExclusive( &impl->lock );
    impl->max_connections = value;
    ReleaseSRWLockExclusive( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI filter_get_ProxyCredential( IHttpBaseProtocolFilter *iface, IPasswordCredential **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI filter_put_ProxyCredential( IHttpBaseProtocolFilter *iface, IPasswordCredential *value )
{
    /* TODO: proxy credentials */
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI filter_get_ServerCredential( IHttpBaseProtocolFilter *iface, IPasswordCredential **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI filter_put_ServerCredential( IHttpBaseProtocolFilter *iface, IPasswordCredential *value )
{
    /* TODO: server credentials */
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static const struct IHttpBaseProtocolFilterVtbl filter_vtbl =
{
    filter_QueryInterface,
    filter_AddRef,
    filter_Release,
    filter_GetIids,
    filter_GetRuntimeClassName,
    filter_GetTrustLevel,
    filter_get_AllowAutoRedirect,
    filter_put_AllowAutoRedirect,
    filter_get_AllowUI,
    filter_put_AllowUI,
    filter_get_AutomaticDecompression,
    filter_put_AutomaticDecompression,
    filter_get_CacheControl,
    filter_get_CookieManager,
    filter_get_ClientCertificate,
    filter_put_ClientCertificate,
    filter_get_IgnorableServerCertificateErrors,
    filter_get_MaxConnectionsPerServer,
    filter_put_MaxConnectionsPerServer,
    filter_get_ProxyCredential,
    filter_put_ProxyCredential,
    filter_get_ServerCredential,
    filter_put_ServerCredential,
    filter_get_UseProxy,
    filter_put_UseProxy,
};

struct send_params
{
    IUnknown IUnknown_iface;
    LONG ref;
    IHttpRequestMessage *request;
    HttpCompletionOption option;
};

static inline struct send_params *impl_from_send_params( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct send_params, IUnknown_iface );
}

static HRESULT WINAPI send_params_QueryInterface( IUnknown *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI send_params_AddRef( IUnknown *iface )
{
    return InterlockedIncrement( &impl_from_send_params( iface )->ref );
}

static ULONG WINAPI send_params_Release( IUnknown *iface )
{
    struct send_params *impl = impl_from_send_params( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        IHttpRequestMessage_Release( impl->request );
        free( impl );
    }
    return ref;
}

static const IUnknownVtbl send_params_vtbl =
{
    send_params_QueryInterface,
    send_params_AddRef,
    send_params_Release,
};

static HRESULT open_session( struct filter *impl, HINTERNET *out )
{
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config = {0};
    HINTERNET session;
    boolean use_proxy;

    *out = NULL;
    AcquireSRWLockShared( &impl->lock );
    session = impl->session;
    use_proxy = impl->use_proxy;
    ReleaseSRWLockShared( &impl->lock );
    if (session)
    {
        *out = session;
        return S_OK;
    }

    if (!use_proxy)
        session = WinHttpOpen( NULL, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
    else
    {
        /* TODO: proxy auto-detection and auto-configuration scripts of the user's Internet settings */
        if (WinHttpGetIEProxyConfigForCurrentUser( &config ) && config.lpszProxy)
            session = WinHttpOpen( NULL, WINHTTP_ACCESS_TYPE_NAMED_PROXY, config.lpszProxy,
                                   config.lpszProxyBypass ? config.lpszProxyBypass : WINHTTP_NO_PROXY_BYPASS, 0 );
        else
            session = WinHttpOpen( NULL, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
        GlobalFree( config.lpszAutoConfigUrl );
        GlobalFree( config.lpszProxy );
        GlobalFree( config.lpszProxyBypass );
    }
    if (!session) return HRESULT_FROM_WIN32( GetLastError() );

    AcquireSRWLockExclusive( &impl->lock );
    if (impl->session)
    {
        WinHttpCloseHandle( session );
        session = impl->session;
    }
    else impl->session = session;
    ReleaseSRWLockExclusive( &impl->lock );
    *out = session;
    return S_OK;
}

static HRESULT query_url( HINTERNET request, WCHAR **url )
{
    DWORD size = 0;

    *url = NULL;
    if (WinHttpQueryOption( request, WINHTTP_OPTION_URL, NULL, &size ) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return HRESULT_FROM_WIN32( GetLastError() );
    if (!(*url = malloc( size + sizeof(WCHAR) ))) return E_OUTOFMEMORY;
    if (!WinHttpQueryOption( request, WINHTTP_OPTION_URL, *url, &size ))
    {
        free( *url );
        *url = NULL;
        return HRESULT_FROM_WIN32( GetLastError() );
    }
    (*url)[size / sizeof(WCHAR)] = 0;
    return S_OK;
}

static HRESULT send_request( struct filter *impl, struct send_params *params, IHttpResponseMessage **response )
{
    URL_COMPONENTS components = {sizeof(components)};
    struct header_collection *headers;
    IHttpContent *content = NULL;
    IUriRuntimeClass *uri = NULL;
    IHttpMethod *method = NULL;
    HINTERNET session, connect = NULL, request = NULL;
    WCHAR *verb = NULL, *host = NULL, *text = NULL, *final_url = NULL;
    const WCHAR *url;
    HSTRING absolute = NULL;
    boolean allow_redirect, decompression;
    UINT32 length;
    DWORD value;
    HRESULT hr;

    *response = NULL;
    if (!(headers = http_request_message_headers( params->request ))) return E_INVALIDARG;
    if (FAILED(hr = IHttpRequestMessage_get_Content( params->request, &content ))) return hr;
    if (content)
    {
        /* TODO: request content */
        FIXME( "request content not implemented\n" );
        IHttpContent_Release( content );
        return E_NOTIMPL;
    }
    if (FAILED(hr = IHttpRequestMessage_get_Method( params->request, &method ))) return hr;
    hr = http_method_name( method, &verb );
    IHttpMethod_Release( method );
    if (FAILED(hr)) return hr;
    if (FAILED(hr = IHttpRequestMessage_get_RequestUri( params->request, &uri )) || !uri)
    {
        hr = FAILED(hr) ? hr : E_ILLEGAL_METHOD_CALL;
        goto done;
    }
    hr = IUriRuntimeClass_get_AbsoluteUri( uri, &absolute );
    IUriRuntimeClass_Release( uri );
    if (FAILED(hr)) goto done;
    url = hstring_buffer( absolute, &length );
    TRACE( "%s %s\n", debugstr_w( verb ), debugstr_w( url ) );

    components.dwHostNameLength = 1;
    components.dwUrlPathLength = 1;
    components.dwExtraInfoLength = 1;
    if (!WinHttpCrackUrl( url, length, 0, &components ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS)
    {
        hr = E_INVALIDARG;
        goto done;
    }
    if (!(host = malloc( (components.dwHostNameLength + 1) * sizeof(WCHAR) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    memcpy( host, components.lpszHostName, components.dwHostNameLength * sizeof(WCHAR) );
    host[components.dwHostNameLength] = 0;

    AcquireSRWLockShared( &impl->lock );
    allow_redirect = impl->allow_redirect;
    decompression = impl->decompression;
    ReleaseSRWLockShared( &impl->lock );

    if (FAILED(hr = open_session( impl, &session ))) goto done;
    if (!(connect = WinHttpConnect( session, host, components.nPort, 0 )))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    /* TODO: HTTP cache of HttpCacheControl; every request goes to the network */
    if (!(request = WinHttpOpenRequest( connect, verb, components.lpszUrlPath, NULL, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0 )))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    if (decompression)
    {
        value = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption( request, WINHTTP_OPTION_DECOMPRESSION, &value, sizeof(value) );
    }
    if (!allow_redirect)
    {
        value = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption( request, WINHTTP_OPTION_DISABLE_FEATURE, &value, sizeof(value) );
    }
    if (FAILED(hr = header_collection_format( headers, NULL, &text, &length ))) goto done;
    if (length && !WinHttpAddRequestHeaders( request, text, length, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    if (!WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) ||
        !WinHttpReceiveResponse( request, NULL ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    if (SUCCEEDED(query_url( request, &final_url )) && wcscmp( final_url, url ) &&
        FAILED(hr = http_request_message_set_uri( params->request, final_url )))
        goto done;

    hr = http_response_message_create( params->request, request,
                                       params->option == HttpCompletionOption_ResponseContentRead, response );
    request = NULL;

done:
    if (request) WinHttpCloseHandle( request );
    if (connect) WinHttpCloseHandle( connect );
    WindowsDeleteString( absolute );
    free( final_url );
    free( text );
    free( host );
    free( verb );
    TRACE( "returning %#lx\n", hr );
    return hr;
}

static HRESULT send_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct filter *impl = CONTAINING_RECORD( (IHttpFilter *)invoker, struct filter, IHttpFilter_iface );
    IHttpResponseMessage *response;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;
    if (FAILED(hr = send_request( impl, impl_from_send_params( param ), &response ))) return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)response;
    return S_OK;
}

BOOL filter_is_base_protocol( IHttpFilter *iface )
{
    return iface->lpVtbl == &filter_http_vtbl;
}

HRESULT filter_send( IHttpFilter *iface, IHttpRequestMessage *request, HttpCompletionOption option,
                     IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation )
{
    struct send_params *params;
    HRESULT hr;

    assert( filter_is_base_protocol( iface ) );
    *operation = NULL;
    if (!request) return E_INVALIDARG;
    if (option != HttpCompletionOption_ResponseContentRead && option != HttpCompletionOption_ResponseHeadersRead)
        return E_INVALIDARG;
    if (!(params = calloc( 1, sizeof(*params) ))) return E_OUTOFMEMORY;
    params->IUnknown_iface.lpVtbl = &send_params_vtbl;
    params->ref = 1;
    IHttpRequestMessage_AddRef( (params->request = request) );
    params->option = option;
    hr = async_operation_response_create( (IUnknown *)iface, &params->IUnknown_iface, send_async, operation );
    IUnknown_Release( &params->IUnknown_iface );
    return hr;
}

DEFINE_IINSPECTABLE( filter_http, IHttpFilter, struct filter, IHttpBaseProtocolFilter_iface )

static HRESULT WINAPI filter_http_SendRequestAsync( IHttpFilter *iface, IHttpRequestMessage *request,
                                                    IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation )
{
    TRACE( "iface %p, request %p, operation %p.\n", iface, request, operation );

    if (!operation) return E_POINTER;
    return filter_send( iface, request, HttpCompletionOption_ResponseContentRead, operation );
}

static const struct IHttpFilterVtbl filter_http_vtbl =
{
    filter_http_QueryInterface,
    filter_http_AddRef,
    filter_http_Release,
    filter_http_GetIids,
    filter_http_GetRuntimeClassName,
    filter_http_GetTrustLevel,
    filter_http_SendRequestAsync,
};

DEFINE_IINSPECTABLE( filter_closable, IClosable, struct filter, IHttpBaseProtocolFilter_iface )

static HRESULT WINAPI filter_closable_Close( IClosable *iface )
{
    struct filter *impl = impl_from_IClosable( iface );
    HINTERNET session;

    AcquireSRWLockExclusive( &impl->lock );
    session = impl->session;
    impl->session = NULL;
    ReleaseSRWLockExclusive( &impl->lock );
    if (session) WinHttpCloseHandle( session );
    return S_OK;
}

static const struct IClosableVtbl filter_closable_vtbl =
{
    filter_closable_QueryInterface,
    filter_closable_AddRef,
    filter_closable_Release,
    filter_closable_GetIids,
    filter_closable_GetRuntimeClassName,
    filter_closable_GetTrustLevel,
    filter_closable_Close,
};

HRESULT filter_create( IHttpFilter **out )
{
    struct filter *impl;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    if (!(impl->cache = calloc( 1, sizeof(*impl->cache) )))
    {
        free( impl );
        return E_OUTOFMEMORY;
    }
    impl->cache->IHttpCacheControl_iface.lpVtbl = &cache_vtbl;
    impl->cache->ref = 1;
    impl->cache->read_behavior = HttpCacheReadBehavior_Default;
    impl->cache->write_behavior = HttpCacheWriteBehavior_Default;
    impl->IHttpBaseProtocolFilter_iface.lpVtbl = &filter_vtbl;
    impl->IHttpFilter_iface.lpVtbl = &filter_http_vtbl;
    impl->IClosable_iface.lpVtbl = &filter_closable_vtbl;
    impl->ref = 1;
    InitializeSRWLock( &impl->lock );
    impl->allow_redirect = TRUE;
    impl->allow_ui = TRUE;
    impl->decompression = TRUE;
    impl->use_proxy = TRUE;
    impl->max_connections = 6;
    *out = &impl->IHttpFilter_iface;
    return S_OK;
}

struct filter_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct filter_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct filter_statics, IActivationFactory_iface );
}

static HRESULT WINAPI filter_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( (*out = iface) );
        return S_OK;
    }
    /* TODO: IHttpBaseProtocolFilterStatics */
    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI filter_factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_IActivationFactory( iface )->ref );
}

static ULONG WINAPI filter_factory_Release( IActivationFactory *iface )
{
    ULONG ref = InterlockedDecrement( &impl_from_IActivationFactory( iface )->ref );

    assert( ref );
    return ref;
}

static HRESULT WINAPI filter_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI filter_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_Filters_HttpBaseProtocolFilter;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI filter_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI filter_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    IHttpFilter *filter;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    *instance = NULL;
    if (FAILED(hr = filter_create( &filter ))) return hr;
    hr = IHttpFilter_QueryInterface( filter, &IID_IInspectable, (void **)instance );
    IHttpFilter_Release( filter );
    return hr;
}

static const struct IActivationFactoryVtbl filter_factory_vtbl =
{
    filter_factory_QueryInterface,
    filter_factory_AddRef,
    filter_factory_Release,
    filter_factory_GetIids,
    filter_factory_GetRuntimeClassName,
    filter_factory_GetTrustLevel,
    filter_factory_ActivateInstance,
};

static struct filter_statics filter_statics = {{&filter_factory_vtbl}, 1};

IActivationFactory *http_base_protocol_filter_factory = &filter_statics.IActivationFactory_iface;
