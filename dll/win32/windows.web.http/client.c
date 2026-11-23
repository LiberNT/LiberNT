/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.Web.Http.HttpClient
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(webhttp);

struct client
{
    IHttpClient IHttpClient_iface;
    IClosable IClosable_iface;
    IStringable IStringable_iface;
    LONG ref;
    IHttpFilter *filter;
    struct header_collection *defaults;
};

static inline struct client *impl_from_IHttpClient( IHttpClient *iface )
{
    return CONTAINING_RECORD( iface, struct client, IHttpClient_iface );
}

static HRESULT WINAPI client_QueryInterface( IHttpClient *iface, REFIID iid, void **out )
{
    struct client *impl = impl_from_IHttpClient( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IHttpClient ))
        *out = &impl->IHttpClient_iface;
    else if (IsEqualGUID( iid, &IID_IClosable ))
        *out = &impl->IClosable_iface;
    else if (IsEqualGUID( iid, &IID_IStringable ))
        *out = &impl->IStringable_iface;
    else
    {
        /* TODO: IHttpClient2 and IHttpClient3 */
        *out = NULL;
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI client_AddRef( IHttpClient *iface )
{
    return InterlockedIncrement( &impl_from_IHttpClient( iface )->ref );
}

static ULONG WINAPI client_Release( IHttpClient *iface )
{
    struct client *impl = impl_from_IHttpClient( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        IHttpFilter_Release( impl->filter );
        header_collection_release( impl->defaults );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI client_GetIids( IHttpClient *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI client_GetRuntimeClassName( IHttpClient *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_HttpClient;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI client_GetTrustLevel( IHttpClient *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI client_SendRequestWithOptionAsync( IHttpClient *iface, IHttpRequestMessage *request,
                                                         HttpCompletionOption option,
                                                         IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation )
{
    struct client *impl = impl_from_IHttpClient( iface );
    struct header_collection *headers;
    HRESULT hr;

    TRACE( "iface %p, request %p, option %d, operation %p.\n", iface, request, option, operation );

    if (!operation) return E_POINTER;
    *operation = NULL;
    if (!request) return E_INVALIDARG;
    if ((headers = http_request_message_headers( request )) &&
        FAILED(hr = header_collection_merge( headers, impl->defaults )))
        return hr;
    if (filter_is_base_protocol( impl->filter )) return filter_send( impl->filter, request, option, operation );
    /* TODO: HttpCompletionOption for filters of the application */
    return IHttpFilter_SendRequestAsync( impl->filter, request, operation );
}

static HRESULT send_new_request( IHttpClient *iface, const WCHAR *verb, IUriRuntimeClass *uri, IHttpContent *content,
                                 HttpCompletionOption option,
                                 IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation )
{
    IHttpRequestMessage *request;
    IHttpMethod *method;
    HRESULT hr;

    if (!operation) return E_POINTER;
    *operation = NULL;
    if (!uri) return E_INVALIDARG;
    if (FAILED(hr = http_method_create( verb, &method ))) return hr;
    hr = http_request_message_create( method, uri, &request );
    IHttpMethod_Release( method );
    if (FAILED(hr)) return hr;
    if (content) hr = IHttpRequestMessage_put_Content( request, content );
    if (SUCCEEDED(hr)) hr = client_SendRequestWithOptionAsync( iface, request, option, operation );
    IHttpRequestMessage_Release( request );
    return hr;
}

static HRESULT WINAPI client_DeleteAsync( IHttpClient *iface, IUriRuntimeClass *uri,
                                          IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation )
{
    return send_new_request( iface, L"DELETE", uri, NULL, HttpCompletionOption_ResponseContentRead, operation );
}

static HRESULT WINAPI client_GetAsync( IHttpClient *iface, IUriRuntimeClass *uri,
                                       IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation )
{
    return send_new_request( iface, L"GET", uri, NULL, HttpCompletionOption_ResponseContentRead, operation );
}

static HRESULT WINAPI client_GetWithOptionAsync( IHttpClient *iface, IUriRuntimeClass *uri, HttpCompletionOption option,
                                                 IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation )
{
    return send_new_request( iface, L"GET", uri, NULL, option, operation );
}

static HRESULT WINAPI client_GetBufferAsync( IHttpClient *iface, IUriRuntimeClass *uri,
                                             IAsyncOperationWithProgress_IBuffer_HttpProgress **operation )
{
    /* TODO: GetBufferAsync */
    FIXME( "iface %p, uri %p, operation %p stub!\n", iface, uri, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI client_GetInputStreamAsync( IHttpClient *iface, IUriRuntimeClass *uri,
                                                  IAsyncOperationWithProgress_IInputStream_HttpProgress **operation )
{
    /* TODO: GetInputStreamAsync */
    FIXME( "iface %p, uri %p, operation %p stub!\n", iface, uri, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI client_GetStringAsync( IHttpClient *iface, IUriRuntimeClass *uri,
                                             IAsyncOperationWithProgress_HSTRING_HttpProgress **operation )
{
    /* TODO: GetStringAsync */
    FIXME( "iface %p, uri %p, operation %p stub!\n", iface, uri, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI client_PostAsync( IHttpClient *iface, IUriRuntimeClass *uri, IHttpContent *content,
                                        IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation )
{
    return send_new_request( iface, L"POST", uri, content, HttpCompletionOption_ResponseContentRead, operation );
}

static HRESULT WINAPI client_PutAsync( IHttpClient *iface, IUriRuntimeClass *uri, IHttpContent *content,
                                       IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation )
{
    return send_new_request( iface, L"PUT", uri, content, HttpCompletionOption_ResponseContentRead, operation );
}

static HRESULT WINAPI client_SendRequestAsync( IHttpClient *iface, IHttpRequestMessage *request,
                                               IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation )
{
    return client_SendRequestWithOptionAsync( iface, request, HttpCompletionOption_ResponseContentRead, operation );
}

static HRESULT WINAPI client_get_DefaultRequestHeaders( IHttpClient *iface, IHttpRequestHeaderCollection **value )
{
    if (!value) return E_POINTER;
    return header_collection_get_iface( impl_from_IHttpClient( iface )->defaults, &IID_IHttpRequestHeaderCollection,
                                        (void **)value );
}

static const struct IHttpClientVtbl client_vtbl =
{
    client_QueryInterface,
    client_AddRef,
    client_Release,
    client_GetIids,
    client_GetRuntimeClassName,
    client_GetTrustLevel,
    client_DeleteAsync,
    client_GetAsync,
    client_GetWithOptionAsync,
    client_GetBufferAsync,
    client_GetInputStreamAsync,
    client_GetStringAsync,
    client_PostAsync,
    client_PutAsync,
    client_SendRequestAsync,
    client_SendRequestWithOptionAsync,
    client_get_DefaultRequestHeaders,
};

DEFINE_IINSPECTABLE( client_closable, IClosable, struct client, IHttpClient_iface )

static HRESULT WINAPI client_closable_Close( IClosable *iface )
{
    struct client *impl = impl_from_IClosable( iface );
    IClosable *closable;

    if (SUCCEEDED(IHttpFilter_QueryInterface( impl->filter, &IID_IClosable, (void **)&closable )))
    {
        IClosable_Close( closable );
        IClosable_Release( closable );
    }
    return S_OK;
}

static const struct IClosableVtbl client_closable_vtbl =
{
    client_closable_QueryInterface,
    client_closable_AddRef,
    client_closable_Release,
    client_closable_GetIids,
    client_closable_GetRuntimeClassName,
    client_closable_GetTrustLevel,
    client_closable_Close,
};

DEFINE_IINSPECTABLE( client_stringable, IStringable, struct client, IHttpClient_iface )

static HRESULT WINAPI client_stringable_ToString( IStringable *iface, HSTRING *value )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_HttpClient;

    if (!value) return E_POINTER;
    return WindowsCreateString( name, wcslen( name ), value );
}

static const struct IStringableVtbl client_stringable_vtbl =
{
    client_stringable_QueryInterface,
    client_stringable_AddRef,
    client_stringable_Release,
    client_stringable_GetIids,
    client_stringable_GetRuntimeClassName,
    client_stringable_GetTrustLevel,
    client_stringable_ToString,
};

static HRESULT client_create( IHttpFilter *filter, IHttpClient **out )
{
    struct client *impl;
    HRESULT hr;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHttpClient_iface.lpVtbl = &client_vtbl;
    impl->IClosable_iface.lpVtbl = &client_closable_vtbl;
    impl->IStringable_iface.lpVtbl = &client_stringable_vtbl;
    impl->ref = 1;
    if (FAILED(hr = header_collection_create( HEADERS_REQUEST, &impl->defaults )))
    {
        free( impl );
        return hr;
    }
    IHttpFilter_AddRef( (impl->filter = filter) );
    *out = &impl->IHttpClient_iface;
    return S_OK;
}

struct client_statics
{
    IActivationFactory IActivationFactory_iface;
    IHttpClientFactory IHttpClientFactory_iface;
    LONG ref;
};

static inline struct client_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct client_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct client_statics *impl = impl_from_IActivationFactory( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IHttpClientFactory ))
        *out = &impl->IHttpClientFactory_iface;
    else
    {
        *out = NULL;
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_IActivationFactory( iface )->ref );
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    ULONG ref = InterlockedDecrement( &impl_from_IActivationFactory( iface )->ref );

    assert( ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_HttpClient;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    IHttpFilter *filter;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    *instance = NULL;
    if (FAILED(hr = filter_create( &filter ))) return hr;
    hr = client_create( filter, (IHttpClient **)instance );
    IHttpFilter_Release( filter );
    return hr;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( client_factory, IHttpClientFactory, struct client_statics, IActivationFactory_iface )

static HRESULT WINAPI client_factory_Create( IHttpClientFactory *iface, IHttpFilter *filter, IHttpClient **value )
{
    TRACE( "iface %p, filter %p, value %p.\n", iface, filter, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!filter) return E_INVALIDARG;
    return client_create( filter, value );
}

static const struct IHttpClientFactoryVtbl client_factory_vtbl =
{
    client_factory_QueryInterface,
    client_factory_AddRef,
    client_factory_Release,
    client_factory_GetIids,
    client_factory_GetRuntimeClassName,
    client_factory_GetTrustLevel,
    client_factory_Create,
};

static struct client_statics client_statics =
{
    {&factory_vtbl},
    {&client_factory_vtbl},
    1,
};

IActivationFactory *http_client_factory = &client_statics.IActivationFactory_iface;
