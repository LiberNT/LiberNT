/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.Web.Http methods, request and response messages and response content
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(webhttp);

struct http_method
{
    IHttpMethod IHttpMethod_iface;
    IStringable IStringable_iface;
    LONG ref;
    HSTRING name;
};

static inline struct http_method *impl_from_IHttpMethod( IHttpMethod *iface )
{
    return CONTAINING_RECORD( iface, struct http_method, IHttpMethod_iface );
}

static HRESULT WINAPI method_QueryInterface( IHttpMethod *iface, REFIID iid, void **out )
{
    struct http_method *impl = impl_from_IHttpMethod( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IHttpMethod ))
        *out = &impl->IHttpMethod_iface;
    else if (IsEqualGUID( iid, &IID_IStringable ))
        *out = &impl->IStringable_iface;
    else
    {
        *out = NULL;
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI method_AddRef( IHttpMethod *iface )
{
    return InterlockedIncrement( &impl_from_IHttpMethod( iface )->ref );
}

static ULONG WINAPI method_Release( IHttpMethod *iface )
{
    struct http_method *impl = impl_from_IHttpMethod( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->name );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI method_GetIids( IHttpMethod *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI method_GetRuntimeClassName( IHttpMethod *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_HttpMethod;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI method_GetTrustLevel( IHttpMethod *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI method_get_Method( IHttpMethod *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl_from_IHttpMethod( iface )->name, value );
}

static const struct IHttpMethodVtbl method_vtbl =
{
    method_QueryInterface,
    method_AddRef,
    method_Release,
    method_GetIids,
    method_GetRuntimeClassName,
    method_GetTrustLevel,
    method_get_Method,
};

DEFINE_IINSPECTABLE_( method_stringable, IStringable, struct http_method, impl_from_method_IStringable,
                      IStringable_iface, &impl->IHttpMethod_iface )

static HRESULT WINAPI method_stringable_ToString( IStringable *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl_from_method_IStringable( iface )->name, value );
}

static const struct IStringableVtbl method_stringable_vtbl =
{
    method_stringable_QueryInterface,
    method_stringable_AddRef,
    method_stringable_Release,
    method_stringable_GetIids,
    method_stringable_GetRuntimeClassName,
    method_stringable_GetTrustLevel,
    method_stringable_ToString,
};

static BOOL is_method_token( const WCHAR *name, UINT32 len )
{
    UINT32 i;

    if (!len) return FALSE;
    for (i = 0; i < len; i++)
    {
        if (name[i] <= 0x20 || name[i] >= 0x7f || wcschr( L"()<>@,;:\\\"/[]?={}", name[i] )) return FALSE;
    }
    return TRUE;
}

HRESULT http_method_create( const WCHAR *name, IHttpMethod **out )
{
    struct http_method *impl;
    HRESULT hr;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHttpMethod_iface.lpVtbl = &method_vtbl;
    impl->IStringable_iface.lpVtbl = &method_stringable_vtbl;
    impl->ref = 1;
    if (FAILED(hr = WindowsCreateString( name, wcslen( name ), &impl->name )))
    {
        free( impl );
        return hr;
    }
    *out = &impl->IHttpMethod_iface;
    return S_OK;
}

HRESULT http_method_name( IHttpMethod *method, WCHAR **name )
{
    HSTRING value;
    UINT32 len;
    HRESULT hr;

    *name = NULL;
    if (FAILED(hr = IHttpMethod_get_Method( method, &value ))) return hr;
    if (!(*name = malloc( (WindowsGetStringLen( value ) + 1) * sizeof(WCHAR) ))) hr = E_OUTOFMEMORY;
    else wcscpy( *name, hstring_buffer( value, &len ) );
    WindowsDeleteString( value );
    return hr;
}

struct method_statics
{
    IActivationFactory IActivationFactory_iface;
    IHttpMethodFactory IHttpMethodFactory_iface;
    IHttpMethodStatics IHttpMethodStatics_iface;
    LONG ref;
};

static inline struct method_statics *impl_from_method_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct method_statics, IActivationFactory_iface );
}

static HRESULT WINAPI method_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct method_statics *impl = impl_from_method_factory( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IHttpMethodFactory ))
        *out = &impl->IHttpMethodFactory_iface;
    else if (IsEqualGUID( iid, &IID_IHttpMethodStatics ))
        *out = &impl->IHttpMethodStatics_iface;
    else
    {
        *out = NULL;
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI method_factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_method_factory( iface )->ref );
}

static ULONG WINAPI method_factory_Release( IActivationFactory *iface )
{
    ULONG ref = InterlockedDecrement( &impl_from_method_factory( iface )->ref );

    assert( ref );
    return ref;
}

static HRESULT WINAPI method_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI method_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_HttpMethod;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI method_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI method_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (instance) *instance = NULL;
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl method_factory_vtbl =
{
    method_factory_QueryInterface,
    method_factory_AddRef,
    method_factory_Release,
    method_factory_GetIids,
    method_factory_GetRuntimeClassName,
    method_factory_GetTrustLevel,
    method_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( method_create, IHttpMethodFactory, struct method_statics, IActivationFactory_iface )

static HRESULT WINAPI method_create_Create( IHttpMethodFactory *iface, HSTRING method, IHttpMethod **value )
{
    UINT32 len;
    const WCHAR *name = hstring_buffer( method, &len );

    TRACE( "iface %p, method %s, value %p.\n", iface, debugstr_hstring( method ), value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!is_method_token( name, len )) return E_INVALIDARG;
    return http_method_create( name, value );
}

static const struct IHttpMethodFactoryVtbl method_create_vtbl =
{
    method_create_QueryInterface,
    method_create_AddRef,
    method_create_Release,
    method_create_GetIids,
    method_create_GetRuntimeClassName,
    method_create_GetTrustLevel,
    method_create_Create,
};

DEFINE_IINSPECTABLE( method_statics, IHttpMethodStatics, struct method_statics, IActivationFactory_iface )

#define METHOD_STATIC( member, text )                                                              \
    static HRESULT WINAPI method_statics_get_##member( IHttpMethodStatics *iface, IHttpMethod **value ) \
    {                                                                                              \
        if (!value) return E_POINTER;                                                              \
        return http_method_create( text, value );                                                  \
    }

METHOD_STATIC( Delete, L"DELETE" )
METHOD_STATIC( Get, L"GET" )
METHOD_STATIC( Head, L"HEAD" )
METHOD_STATIC( Options, L"OPTIONS" )
METHOD_STATIC( Patch, L"PATCH" )
METHOD_STATIC( Post, L"POST" )
METHOD_STATIC( Put, L"PUT" )

static const struct IHttpMethodStaticsVtbl method_statics_vtbl =
{
    method_statics_QueryInterface,
    method_statics_AddRef,
    method_statics_Release,
    method_statics_GetIids,
    method_statics_GetRuntimeClassName,
    method_statics_GetTrustLevel,
    method_statics_get_Delete,
    method_statics_get_Get,
    method_statics_get_Head,
    method_statics_get_Options,
    method_statics_get_Patch,
    method_statics_get_Post,
    method_statics_get_Put,
};

static struct method_statics method_statics =
{
    {&method_factory_vtbl},
    {&method_create_vtbl},
    {&method_statics_vtbl},
    1,
};

IActivationFactory *http_method_factory = &method_statics.IActivationFactory_iface;

struct request_message
{
    IHttpRequestMessage IHttpRequestMessage_iface;
    IClosable IClosable_iface;
    IStringable IStringable_iface;
    LONG ref;
    SRWLOCK lock;
    IHttpMethod *method;
    IUriRuntimeClass *uri;
    IHttpContent *content;
    IMap_HSTRING_IInspectable *properties;
    struct header_collection *headers;
};

static const struct IHttpRequestMessageVtbl request_vtbl;

static inline struct request_message *impl_from_IHttpRequestMessage( IHttpRequestMessage *iface )
{
    return CONTAINING_RECORD( iface, struct request_message, IHttpRequestMessage_iface );
}

static HRESULT WINAPI request_QueryInterface( IHttpRequestMessage *iface, REFIID iid, void **out )
{
    struct request_message *impl = impl_from_IHttpRequestMessage( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IHttpRequestMessage ))
        *out = &impl->IHttpRequestMessage_iface;
    else if (IsEqualGUID( iid, &IID_IClosable ))
        *out = &impl->IClosable_iface;
    else if (IsEqualGUID( iid, &IID_IStringable ))
        *out = &impl->IStringable_iface;
    else
    {
        /* TODO: IHttpRequestMessage2 */
        *out = NULL;
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI request_AddRef( IHttpRequestMessage *iface )
{
    return InterlockedIncrement( &impl_from_IHttpRequestMessage( iface )->ref );
}

static ULONG WINAPI request_Release( IHttpRequestMessage *iface )
{
    struct request_message *impl = impl_from_IHttpRequestMessage( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->method) IHttpMethod_Release( impl->method );
        if (impl->uri) IUriRuntimeClass_Release( impl->uri );
        if (impl->content) IHttpContent_Release( impl->content );
        if (impl->properties) IMap_HSTRING_IInspectable_Release( impl->properties );
        header_collection_release( impl->headers );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI request_GetIids( IHttpRequestMessage *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI request_GetRuntimeClassName( IHttpRequestMessage *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_HttpRequestMessage;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI request_GetTrustLevel( IHttpRequestMessage *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

#define REQUEST_GET( member, type, field, addref )                                                 \
    static HRESULT WINAPI request_get_##member( IHttpRequestMessage *iface, type **value )        \
    {                                                                                              \
        struct request_message *impl = impl_from_IHttpRequestMessage( iface );                     \
        if (!value) return E_POINTER;                                                              \
        AcquireSRWLockShared( &impl->lock );                                                       \
        if ((*value = impl->field)) addref( *value );                                              \
        ReleaseSRWLockShared( &impl->lock );                                                       \
        return S_OK;                                                                               \
    }
#define REQUEST_PUT( member, type, field, addref, release )                                        \
    static HRESULT WINAPI request_put_##member( IHttpRequestMessage *iface, type *value )         \
    {                                                                                              \
        struct request_message *impl = impl_from_IHttpRequestMessage( iface );                     \
        type *old;                                                                                 \
        if (value) addref( value );                                                                \
        AcquireSRWLockExclusive( &impl->lock );                                                    \
        old = impl->field;                                                                         \
        impl->field = value;                                                                       \
        ReleaseSRWLockExclusive( &impl->lock );                                                    \
        if (old) release( old );                                                                   \
        return S_OK;                                                                               \
    }

REQUEST_GET( Content, IHttpContent, content, IHttpContent_AddRef )
REQUEST_PUT( Content, IHttpContent, content, IHttpContent_AddRef, IHttpContent_Release )
REQUEST_GET( Method, IHttpMethod, method, IHttpMethod_AddRef )
REQUEST_GET( RequestUri, IUriRuntimeClass, uri, IUriRuntimeClass_AddRef )
REQUEST_PUT( RequestUri, IUriRuntimeClass, uri, IUriRuntimeClass_AddRef, IUriRuntimeClass_Release )

static HRESULT WINAPI request_put_Method( IHttpRequestMessage *iface, IHttpMethod *value )
{
    struct request_message *impl = impl_from_IHttpRequestMessage( iface );
    IHttpMethod *old;

    if (!value) return E_INVALIDARG;
    IHttpMethod_AddRef( value );
    AcquireSRWLockExclusive( &impl->lock );
    old = impl->method;
    impl->method = value;
    ReleaseSRWLockExclusive( &impl->lock );
    if (old) IHttpMethod_Release( old );
    return S_OK;
}

static HRESULT WINAPI request_get_Headers( IHttpRequestMessage *iface, IHttpRequestHeaderCollection **value )
{
    struct request_message *impl = impl_from_IHttpRequestMessage( iface );

    if (!value) return E_POINTER;
    return header_collection_get_iface( impl->headers, &IID_IHttpRequestHeaderCollection, (void **)value );
}

static HRESULT WINAPI request_get_Properties( IHttpRequestMessage *iface, IMap_HSTRING_IInspectable **value )
{
    struct request_message *impl = impl_from_IHttpRequestMessage( iface );
    IMap_HSTRING_IInspectable *map = NULL;
    IInspectable *instance;
    HSTRING class_name;
    HRESULT hr = S_OK;

    if (!value) return E_POINTER;
    *value = NULL;
    AcquireSRWLockExclusive( &impl->lock );
    if (!impl->properties)
    {
        static const WCHAR name[] = L"Windows.Foundation.Collections.PropertySet";

        if (SUCCEEDED(hr = WindowsCreateString( name, ARRAY_SIZE(name) - 1, &class_name )))
        {
            if (SUCCEEDED(hr = RoActivateInstance( class_name, &instance )))
            {
                hr = IInspectable_QueryInterface( instance, &IID_IMap_HSTRING_IInspectable, (void **)&map );
                IInspectable_Release( instance );
                impl->properties = map;
            }
            WindowsDeleteString( class_name );
        }
    }
    if ((*value = impl->properties)) IMap_HSTRING_IInspectable_AddRef( *value );
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}

static HRESULT WINAPI request_get_TransportInformation( IHttpRequestMessage *iface, IHttpTransportInformation **value )
{
    /* TODO: HttpTransportInformation */
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static const struct IHttpRequestMessageVtbl request_vtbl =
{
    request_QueryInterface,
    request_AddRef,
    request_Release,
    request_GetIids,
    request_GetRuntimeClassName,
    request_GetTrustLevel,
    request_get_Content,
    request_put_Content,
    request_get_Headers,
    request_get_Method,
    request_put_Method,
    request_get_Properties,
    request_get_RequestUri,
    request_put_RequestUri,
    request_get_TransportInformation,
};

DEFINE_IINSPECTABLE( request_closable, IClosable, struct request_message, IHttpRequestMessage_iface )

static HRESULT WINAPI request_closable_Close( IClosable *iface )
{
    struct request_message *impl = impl_from_IClosable( iface );
    IHttpContent *content;

    AcquireSRWLockExclusive( &impl->lock );
    content = impl->content;
    impl->content = NULL;
    ReleaseSRWLockExclusive( &impl->lock );
    if (content) IHttpContent_Release( content );
    return S_OK;
}

static const struct IClosableVtbl request_closable_vtbl =
{
    request_closable_QueryInterface,
    request_closable_AddRef,
    request_closable_Release,
    request_closable_GetIids,
    request_closable_GetRuntimeClassName,
    request_closable_GetTrustLevel,
    request_closable_Close,
};

DEFINE_IINSPECTABLE_( request_stringable, IStringable, struct request_message, impl_from_request_IStringable,
                      IStringable_iface, &impl->IHttpRequestMessage_iface )

static HRESULT WINAPI request_stringable_ToString( IStringable *iface, HSTRING *value )
{
    struct request_message *impl = impl_from_request_IStringable( iface );
    HSTRING uri = NULL;
    WCHAR *method = NULL, *text;
    HRESULT hr;

    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    hr = http_method_name( impl->method, &method );
    if (SUCCEEDED(hr) && impl->uri) hr = IUriRuntimeClass_get_AbsoluteUri( impl->uri, &uri );
    ReleaseSRWLockShared( &impl->lock );
    if (SUCCEEDED(hr))
    {
        UINT32 len = wcslen( method ) + WindowsGetStringLen( uri ) + 2;

        if (!(text = malloc( len * sizeof(WCHAR) ))) hr = E_OUTOFMEMORY;
        else
        {
            swprintf( text, len, L"%s %s", method, WindowsGetStringRawBuffer( uri, NULL ) );
            hr = WindowsCreateString( text, wcslen( text ), value );
            free( text );
        }
    }
    WindowsDeleteString( uri );
    free( method );
    return hr;
}

static const struct IStringableVtbl request_stringable_vtbl =
{
    request_stringable_QueryInterface,
    request_stringable_AddRef,
    request_stringable_Release,
    request_stringable_GetIids,
    request_stringable_GetRuntimeClassName,
    request_stringable_GetTrustLevel,
    request_stringable_ToString,
};

HRESULT http_request_message_create( IHttpMethod *method, IUriRuntimeClass *uri, IHttpRequestMessage **out )
{
    struct request_message *impl;
    HRESULT hr;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHttpRequestMessage_iface.lpVtbl = &request_vtbl;
    impl->IClosable_iface.lpVtbl = &request_closable_vtbl;
    impl->IStringable_iface.lpVtbl = &request_stringable_vtbl;
    impl->ref = 1;
    InitializeSRWLock( &impl->lock );
    if (FAILED(hr = header_collection_create( HEADERS_REQUEST, &impl->headers )))
    {
        free( impl );
        return hr;
    }
    if (method) IHttpMethod_AddRef( (impl->method = method) );
    else if (FAILED(hr = http_method_create( L"GET", &impl->method )))
    {
        IHttpRequestMessage_Release( &impl->IHttpRequestMessage_iface );
        return hr;
    }
    if ((impl->uri = uri)) IUriRuntimeClass_AddRef( uri );
    *out = &impl->IHttpRequestMessage_iface;
    return S_OK;
}

struct header_collection *http_request_message_headers( IHttpRequestMessage *request )
{
    if (request->lpVtbl != &request_vtbl) return NULL;
    return impl_from_IHttpRequestMessage( request )->headers;
}

HRESULT http_request_message_set_uri( IHttpRequestMessage *request, const WCHAR *url )
{
    IUriRuntimeClass *uri;
    HRESULT hr;

    if (FAILED(hr = create_uri( url, &uri ))) return hr;
    hr = IHttpRequestMessage_put_RequestUri( request, uri );
    IUriRuntimeClass_Release( uri );
    return hr;
}

struct request_statics
{
    IActivationFactory IActivationFactory_iface;
    IHttpRequestMessageFactory IHttpRequestMessageFactory_iface;
    LONG ref;
};

static inline struct request_statics *impl_from_request_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct request_statics, IActivationFactory_iface );
}

static HRESULT WINAPI request_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct request_statics *impl = impl_from_request_factory( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IHttpRequestMessageFactory ))
        *out = &impl->IHttpRequestMessageFactory_iface;
    else
    {
        *out = NULL;
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI request_factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_request_factory( iface )->ref );
}

static ULONG WINAPI request_factory_Release( IActivationFactory *iface )
{
    ULONG ref = InterlockedDecrement( &impl_from_request_factory( iface )->ref );

    assert( ref );
    return ref;
}

static HRESULT WINAPI request_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI request_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_HttpRequestMessage;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI request_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI request_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    return http_request_message_create( NULL, NULL, (IHttpRequestMessage **)instance );
}

static const struct IActivationFactoryVtbl request_factory_vtbl =
{
    request_factory_QueryInterface,
    request_factory_AddRef,
    request_factory_Release,
    request_factory_GetIids,
    request_factory_GetRuntimeClassName,
    request_factory_GetTrustLevel,
    request_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( request_create, IHttpRequestMessageFactory, struct request_statics, IActivationFactory_iface )

static HRESULT WINAPI request_create_Create( IHttpRequestMessageFactory *iface, IHttpMethod *method, IUriRuntimeClass *uri,
                                             IHttpRequestMessage **value )
{
    TRACE( "iface %p, method %p, uri %p, value %p.\n", iface, method, uri, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!method || !uri) return E_INVALIDARG;
    return http_request_message_create( method, uri, value );
}

static const struct IHttpRequestMessageFactoryVtbl request_create_vtbl =
{
    request_create_QueryInterface,
    request_create_AddRef,
    request_create_Release,
    request_create_GetIids,
    request_create_GetRuntimeClassName,
    request_create_GetTrustLevel,
    request_create_Create,
};

static struct request_statics request_statics =
{
    {&request_factory_vtbl},
    {&request_create_vtbl},
    1,
};

IActivationFactory *http_request_message_factory = &request_statics.IActivationFactory_iface;

struct response_content
{
    IHttpContent IHttpContent_iface;
    IClosable IClosable_iface;
    IStringable IStringable_iface;
    LONG ref;
    CRITICAL_SECTION cs;
    struct header_collection *headers;
    HINTERNET handle;
    BYTE *data;
    SIZE_T size;
    BOOL complete;
    BOOL closed;
};

static inline struct response_content *impl_from_IHttpContent( IHttpContent *iface )
{
    return CONTAINING_RECORD( iface, struct response_content, IHttpContent_iface );
}

static HRESULT WINAPI content_QueryInterface( IHttpContent *iface, REFIID iid, void **out )
{
    struct response_content *impl = impl_from_IHttpContent( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IHttpContent ))
        *out = &impl->IHttpContent_iface;
    else if (IsEqualGUID( iid, &IID_IClosable ))
        *out = &impl->IClosable_iface;
    else if (IsEqualGUID( iid, &IID_IStringable ))
        *out = &impl->IStringable_iface;
    else
    {
        *out = NULL;
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI content_AddRef( IHttpContent *iface )
{
    return InterlockedIncrement( &impl_from_IHttpContent( iface )->ref );
}

static ULONG WINAPI content_Release( IHttpContent *iface )
{
    struct response_content *impl = impl_from_IHttpContent( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->handle) WinHttpCloseHandle( impl->handle );
        header_collection_release( impl->headers );
        free( impl->data );
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI content_GetIids( IHttpContent *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI content_GetRuntimeClassName( IHttpContent *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_HttpStreamContent;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI content_GetTrustLevel( IHttpContent *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI content_get_Headers( IHttpContent *iface, IHttpContentHeaderCollection **value )
{
    if (!value) return E_POINTER;
    return header_collection_get_iface( impl_from_IHttpContent( iface )->headers, &IID_IHttpContentHeaderCollection, (void **)value );
}

static HRESULT content_read_all( struct response_content *impl )
{
    HRESULT hr = S_OK;
    DWORD available, read;

    EnterCriticalSection( &impl->cs );
    if (impl->closed) hr = RO_E_CLOSED;
    while (SUCCEEDED(hr) && !impl->complete)
    {
        BYTE *grown;

        assert( impl->handle );
        if (!WinHttpQueryDataAvailable( impl->handle, &available ))
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            break;
        }
        if (!available)
        {
            impl->complete = TRUE;
            break;
        }
        if (impl->size + available < impl->size || !(grown = realloc( impl->data, impl->size + available )))
        {
            hr = E_OUTOFMEMORY;
            break;
        }
        impl->data = grown;
        if (!WinHttpReadData( impl->handle, impl->data + impl->size, available, &read ))
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            break;
        }
        if (!read) impl->complete = TRUE;
        impl->size += read;
    }
    if (impl->complete && impl->handle)
    {
        WinHttpCloseHandle( impl->handle );
        impl->handle = NULL;
    }
    LeaveCriticalSection( &impl->cs );
    return hr;
}

static HRESULT create_buffer( const BYTE *data, SIZE_T size, IBuffer **out )
{
    static const WCHAR name[] = L"Windows.Storage.Streams.Buffer";
    IBufferFactory *factory;
    IBufferByteAccess *bytes;
    HSTRING class_name;
    BYTE *target;
    HRESULT hr;

    *out = NULL;
    if (size > MAXUINT32) return E_OUTOFMEMORY;
    if (FAILED(hr = WindowsCreateString( name, ARRAY_SIZE(name) - 1, &class_name ))) return hr;
    hr = RoGetActivationFactory( class_name, &IID_IBufferFactory, (void **)&factory );
    WindowsDeleteString( class_name );
    if (FAILED(hr)) return hr;
    hr = IBufferFactory_Create( factory, (UINT32)size, out );
    IBufferFactory_Release( factory );
    if (FAILED(hr)) return hr;
    if (SUCCEEDED(hr = IBuffer_QueryInterface( *out, &IID_IBufferByteAccess, (void **)&bytes )))
    {
        if (SUCCEEDED(hr = IBufferByteAccess_Buffer( bytes, &target )))
        {
            if (size) memcpy( target, data, size );
            hr = IBuffer_put_Length( *out, (UINT32)size );
        }
        IBufferByteAccess_Release( bytes );
    }
    if (FAILED(hr))
    {
        IBuffer_Release( *out );
        *out = NULL;
    }
    return hr;
}

static HRESULT read_buffer_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct response_content *impl = impl_from_IHttpContent( (IHttpContent *)invoker );
    IBuffer *buffer;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;
    if (FAILED(hr = content_read_all( impl ))) return hr;
    EnterCriticalSection( &impl->cs );
    hr = create_buffer( impl->data, impl->size, &buffer );
    LeaveCriticalSection( &impl->cs );
    if (FAILED(hr)) return hr;
    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)buffer;
    return S_OK;
}

static HRESULT WINAPI content_BufferAllAsync( IHttpContent *iface, IAsyncOperationWithProgress_UINT64_UINT64 **operation )
{
    /* TODO: BufferAllAsync */
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI content_ReadAsBufferAsync( IHttpContent *iface, IAsyncOperationWithProgress_IBuffer_UINT64 **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return async_operation_buffer_create( (IUnknown *)iface, NULL, read_buffer_async, operation );
}

static HRESULT WINAPI content_ReadAsInputStreamAsync( IHttpContent *iface, IAsyncOperationWithProgress_IInputStream_UINT64 **operation )
{
    /* TODO: ReadAsInputStreamAsync */
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI content_ReadAsStringAsync( IHttpContent *iface, IAsyncOperationWithProgress_HSTRING_UINT64 **operation )
{
    /* TODO: ReadAsStringAsync */
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI content_TryComputeLength( IHttpContent *iface, UINT64 *length, boolean *succeeded )
{
    struct response_content *impl = impl_from_IHttpContent( iface );

    if (!length || !succeeded) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *length = impl->complete ? impl->size : 0;
    *succeeded = impl->complete;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI content_WriteToStreamAsync( IHttpContent *iface, IOutputStream *stream,
                                                  IAsyncOperationWithProgress_UINT64_UINT64 **operation )
{
    /* TODO: WriteToStreamAsync */
    FIXME( "iface %p, stream %p, operation %p stub!\n", iface, stream, operation );
    return E_NOTIMPL;
}

static const struct IHttpContentVtbl content_vtbl =
{
    content_QueryInterface,
    content_AddRef,
    content_Release,
    content_GetIids,
    content_GetRuntimeClassName,
    content_GetTrustLevel,
    content_get_Headers,
    content_BufferAllAsync,
    content_ReadAsBufferAsync,
    content_ReadAsInputStreamAsync,
    content_ReadAsStringAsync,
    content_TryComputeLength,
    content_WriteToStreamAsync,
};

DEFINE_IINSPECTABLE_( content_closable, IClosable, struct response_content, impl_from_content_IClosable,
                      IClosable_iface, &impl->IHttpContent_iface )

static HRESULT WINAPI content_closable_Close( IClosable *iface )
{
    struct response_content *impl = impl_from_content_IClosable( iface );

    EnterCriticalSection( &impl->cs );
    impl->closed = TRUE;
    if (impl->handle)
    {
        WinHttpCloseHandle( impl->handle );
        impl->handle = NULL;
    }
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const struct IClosableVtbl content_closable_vtbl =
{
    content_closable_QueryInterface,
    content_closable_AddRef,
    content_closable_Release,
    content_closable_GetIids,
    content_closable_GetRuntimeClassName,
    content_closable_GetTrustLevel,
    content_closable_Close,
};

DEFINE_IINSPECTABLE_( content_stringable, IStringable, struct response_content, impl_from_content_IStringable,
                      IStringable_iface, &impl->IHttpContent_iface )

static HRESULT WINAPI content_stringable_ToString( IStringable *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const struct IStringableVtbl content_stringable_vtbl =
{
    content_stringable_QueryInterface,
    content_stringable_AddRef,
    content_stringable_Release,
    content_stringable_GetIids,
    content_stringable_GetRuntimeClassName,
    content_stringable_GetTrustLevel,
    content_stringable_ToString,
};

static HRESULT response_content_create( HINTERNET handle, struct response_content **out )
{
    struct response_content *impl;
    HRESULT hr;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHttpContent_iface.lpVtbl = &content_vtbl;
    impl->IClosable_iface.lpVtbl = &content_closable_vtbl;
    impl->IStringable_iface.lpVtbl = &content_stringable_vtbl;
    impl->ref = 1;
    InitializeCriticalSection( &impl->cs );
    if (FAILED(hr = header_collection_create( HEADERS_CONTENT, &impl->headers )))
    {
        DeleteCriticalSection( &impl->cs );
        free( impl );
        return hr;
    }
    impl->handle = handle;
    impl->complete = !handle;
    *out = impl;
    return S_OK;
}

struct response_message
{
    IHttpResponseMessage IHttpResponseMessage_iface;
    IClosable IClosable_iface;
    IStringable IStringable_iface;
    LONG ref;
    SRWLOCK lock;
    IHttpContent *content;
    IHttpRequestMessage *request;
    struct header_collection *headers;
    HSTRING reason;
    HttpStatusCode status;
    HttpVersion version;
    HttpResponseMessageSource source;
};

static inline struct response_message *impl_from_IHttpResponseMessage( IHttpResponseMessage *iface )
{
    return CONTAINING_RECORD( iface, struct response_message, IHttpResponseMessage_iface );
}

static HRESULT WINAPI response_QueryInterface( IHttpResponseMessage *iface, REFIID iid, void **out )
{
    struct response_message *impl = impl_from_IHttpResponseMessage( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IHttpResponseMessage ))
        *out = &impl->IHttpResponseMessage_iface;
    else if (IsEqualGUID( iid, &IID_IClosable ))
        *out = &impl->IClosable_iface;
    else if (IsEqualGUID( iid, &IID_IStringable ))
        *out = &impl->IStringable_iface;
    else
    {
        *out = NULL;
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI response_AddRef( IHttpResponseMessage *iface )
{
    return InterlockedIncrement( &impl_from_IHttpResponseMessage( iface )->ref );
}

static ULONG WINAPI response_Release( IHttpResponseMessage *iface )
{
    struct response_message *impl = impl_from_IHttpResponseMessage( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->content) IHttpContent_Release( impl->content );
        if (impl->request) IHttpRequestMessage_Release( impl->request );
        header_collection_release( impl->headers );
        WindowsDeleteString( impl->reason );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI response_GetIids( IHttpResponseMessage *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI response_GetRuntimeClassName( IHttpResponseMessage *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_HttpResponseMessage;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI response_GetTrustLevel( IHttpResponseMessage *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI response_get_Content( IHttpResponseMessage *iface, IHttpContent **value )
{
    struct response_message *impl = impl_from_IHttpResponseMessage( iface );

    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    if ((*value = impl->content)) IHttpContent_AddRef( *value );
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI response_put_Content( IHttpResponseMessage *iface, IHttpContent *value )
{
    struct response_message *impl = impl_from_IHttpResponseMessage( iface );
    IHttpContent *old;

    if (value) IHttpContent_AddRef( value );
    AcquireSRWLockExclusive( &impl->lock );
    old = impl->content;
    impl->content = value;
    ReleaseSRWLockExclusive( &impl->lock );
    if (old) IHttpContent_Release( old );
    return S_OK;
}

static HRESULT WINAPI response_get_Headers( IHttpResponseMessage *iface, IHttpResponseHeaderCollection **value )
{
    if (!value) return E_POINTER;
    return header_collection_get_iface( impl_from_IHttpResponseMessage( iface )->headers,
                                        &IID_IHttpResponseHeaderCollection, (void **)value );
}

static HRESULT WINAPI response_get_IsSuccessStatusCode( IHttpResponseMessage *iface, boolean *value )
{
    struct response_message *impl = impl_from_IHttpResponseMessage( iface );

    if (!value) return E_POINTER;
    *value = impl->status >= 200 && impl->status <= 299;
    return S_OK;
}

static HRESULT WINAPI response_get_ReasonPhrase( IHttpResponseMessage *iface, HSTRING *value )
{
    struct response_message *impl = impl_from_IHttpResponseMessage( iface );
    HRESULT hr;

    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    hr = WindowsDuplicateString( impl->reason, value );
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}

static HRESULT WINAPI response_put_ReasonPhrase( IHttpResponseMessage *iface, HSTRING value )
{
    struct response_message *impl = impl_from_IHttpResponseMessage( iface );
    HSTRING copy, old;
    HRESULT hr;

    if (FAILED(hr = WindowsDuplicateString( value, &copy ))) return hr;
    AcquireSRWLockExclusive( &impl->lock );
    old = impl->reason;
    impl->reason = copy;
    ReleaseSRWLockExclusive( &impl->lock );
    WindowsDeleteString( old );
    return S_OK;
}

static HRESULT WINAPI response_get_RequestMessage( IHttpResponseMessage *iface, IHttpRequestMessage **value )
{
    struct response_message *impl = impl_from_IHttpResponseMessage( iface );

    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    if ((*value = impl->request)) IHttpRequestMessage_AddRef( *value );
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI response_put_RequestMessage( IHttpResponseMessage *iface, IHttpRequestMessage *value )
{
    struct response_message *impl = impl_from_IHttpResponseMessage( iface );
    IHttpRequestMessage *old;

    if (value) IHttpRequestMessage_AddRef( value );
    AcquireSRWLockExclusive( &impl->lock );
    old = impl->request;
    impl->request = value;
    ReleaseSRWLockExclusive( &impl->lock );
    if (old) IHttpRequestMessage_Release( old );
    return S_OK;
}

static HRESULT WINAPI response_get_Source( IHttpResponseMessage *iface, HttpResponseMessageSource *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IHttpResponseMessage( iface )->source;
    return S_OK;
}

static HRESULT WINAPI response_put_Source( IHttpResponseMessage *iface, HttpResponseMessageSource value )
{
    impl_from_IHttpResponseMessage( iface )->source = value;
    return S_OK;
}

static HRESULT WINAPI response_get_StatusCode( IHttpResponseMessage *iface, HttpStatusCode *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IHttpResponseMessage( iface )->status;
    return S_OK;
}

static HRESULT WINAPI response_put_StatusCode( IHttpResponseMessage *iface, HttpStatusCode value )
{
    impl_from_IHttpResponseMessage( iface )->status = value;
    return S_OK;
}

static HRESULT WINAPI response_get_Version( IHttpResponseMessage *iface, HttpVersion *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IHttpResponseMessage( iface )->version;
    return S_OK;
}

static HRESULT WINAPI response_put_Version( IHttpResponseMessage *iface, HttpVersion value )
{
    impl_from_IHttpResponseMessage( iface )->version = value;
    return S_OK;
}

static HRESULT WINAPI response_EnsureSuccessStatusCode( IHttpResponseMessage *iface, IHttpResponseMessage **result )
{
    struct response_message *impl = impl_from_IHttpResponseMessage( iface );

    if (!result) return E_POINTER;
    *result = NULL;
    if (impl->status < 200 || impl->status > 299) return MAKE_HRESULT( SEVERITY_ERROR, FACILITY_HTTP, impl->status );
    IHttpResponseMessage_AddRef( (*result = iface) );
    return S_OK;
}

static const struct IHttpResponseMessageVtbl response_vtbl =
{
    response_QueryInterface,
    response_AddRef,
    response_Release,
    response_GetIids,
    response_GetRuntimeClassName,
    response_GetTrustLevel,
    response_get_Content,
    response_put_Content,
    response_get_Headers,
    response_get_IsSuccessStatusCode,
    response_get_ReasonPhrase,
    response_put_ReasonPhrase,
    response_get_RequestMessage,
    response_put_RequestMessage,
    response_get_Source,
    response_put_Source,
    response_get_StatusCode,
    response_put_StatusCode,
    response_get_Version,
    response_put_Version,
    response_EnsureSuccessStatusCode,
};

DEFINE_IINSPECTABLE_( response_closable, IClosable, struct response_message, impl_from_response_IClosable,
                      IClosable_iface, &impl->IHttpResponseMessage_iface )

static HRESULT WINAPI response_closable_Close( IClosable *iface )
{
    struct response_message *impl = impl_from_response_IClosable( iface );
    IHttpContent *content;
    IClosable *closable;

    AcquireSRWLockShared( &impl->lock );
    if ((content = impl->content)) IHttpContent_AddRef( content );
    ReleaseSRWLockShared( &impl->lock );
    if (!content) return S_OK;
    if (SUCCEEDED(IHttpContent_QueryInterface( content, &IID_IClosable, (void **)&closable )))
    {
        IClosable_Close( closable );
        IClosable_Release( closable );
    }
    IHttpContent_Release( content );
    return S_OK;
}

static const struct IClosableVtbl response_closable_vtbl =
{
    response_closable_QueryInterface,
    response_closable_AddRef,
    response_closable_Release,
    response_closable_GetIids,
    response_closable_GetRuntimeClassName,
    response_closable_GetTrustLevel,
    response_closable_Close,
};

DEFINE_IINSPECTABLE_( response_stringable, IStringable, struct response_message, impl_from_response_IStringable,
                      IStringable_iface, &impl->IHttpResponseMessage_iface )

static HRESULT WINAPI response_stringable_ToString( IStringable *iface, HSTRING *value )
{
    struct response_message *impl = impl_from_response_IStringable( iface );
    WCHAR text[128];

    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    swprintf( text, ARRAY_SIZE(text), L"%d %s", impl->status, WindowsGetStringRawBuffer( impl->reason, NULL ) );
    ReleaseSRWLockShared( &impl->lock );
    return WindowsCreateString( text, wcslen( text ), value );
}

static const struct IStringableVtbl response_stringable_vtbl =
{
    response_stringable_QueryInterface,
    response_stringable_AddRef,
    response_stringable_Release,
    response_stringable_GetIids,
    response_stringable_GetRuntimeClassName,
    response_stringable_GetTrustLevel,
    response_stringable_ToString,
};

static HRESULT query_string( HINTERNET handle, DWORD level, WCHAR **out )
{
    DWORD size = 0;
    WCHAR *buffer;

    *out = NULL;
    if (WinHttpQueryHeaders( handle, level, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &size, WINHTTP_NO_HEADER_INDEX ) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return HRESULT_FROM_WIN32( GetLastError() );
    if (!(buffer = malloc( size + sizeof(WCHAR) ))) return E_OUTOFMEMORY;
    if (!WinHttpQueryHeaders( handle, level, WINHTTP_HEADER_NAME_BY_INDEX, buffer, &size, WINHTTP_NO_HEADER_INDEX ))
    {
        HRESULT hr = HRESULT_FROM_WIN32( GetLastError() );
        free( buffer );
        return hr;
    }
    buffer[size / sizeof(WCHAR)] = 0;
    *out = buffer;
    return S_OK;
}

static HRESULT parse_headers( const WCHAR *raw, struct header_collection *response, struct header_collection *content )
{
    const WCHAR *line = wcsstr( raw, L"\r\n" );
    HRESULT hr = S_OK;

    while (line && SUCCEEDED(hr))
    {
        const WCHAR *start = line + 2, *end = wcsstr( start, L"\r\n" ), *colon;
        UINT32 len = end ? end - start : wcslen( start );

        line = end;
        if (!len) continue;
        if (!(colon = wmemchr( start, ':', len ))) continue;
        if (header_is_content( start, colon - start ))
            hr = header_collection_append( content, start, colon - start, colon + 1, len - (colon + 1 - start) );
        else
            hr = header_collection_append( response, start, colon - start, colon + 1, len - (colon + 1 - start) );
    }
    return hr;
}

HRESULT http_response_message_create( IHttpRequestMessage *request, HINTERNET handle, BOOL read_content,
                                      IHttpResponseMessage **out )
{
    struct response_message *impl;
    struct response_content *content;
    DWORD status = 0, size = sizeof(status);
    WCHAR *text = NULL;
    HRESULT hr;

    *out = NULL;
    if (!WinHttpQueryHeaders( handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                              &status, &size, WINHTTP_NO_HEADER_INDEX ))
        return HRESULT_FROM_WIN32( GetLastError() );

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHttpResponseMessage_iface.lpVtbl = &response_vtbl;
    impl->IClosable_iface.lpVtbl = &response_closable_vtbl;
    impl->IStringable_iface.lpVtbl = &response_stringable_vtbl;
    impl->ref = 1;
    InitializeSRWLock( &impl->lock );
    impl->status = status;
    impl->source = HttpResponseMessageSource_Network;
    impl->version = HttpVersion_Http11;
    IHttpRequestMessage_AddRef( (impl->request = request) );

    if (FAILED(hr = header_collection_create( HEADERS_RESPONSE, &impl->headers ))) goto failed;
    if (SUCCEEDED(query_string( handle, WINHTTP_QUERY_STATUS_TEXT, &text )))
    {
        hr = WindowsCreateString( text, wcslen( text ), &impl->reason );
        free( text );
        if (FAILED(hr)) goto failed;
    }
    if (SUCCEEDED(query_string( handle, WINHTTP_QUERY_VERSION, &text )))
    {
        if (!wcscmp( text, L"HTTP/1.0" )) impl->version = HttpVersion_Http10;
        else if (!wcscmp( text, L"HTTP/2" ) || !wcscmp( text, L"HTTP/2.0" )) impl->version = HttpVersion_Http20;
        free( text );
    }
    if (FAILED(hr = response_content_create( handle, &content ))) goto failed;
    impl->content = &content->IHttpContent_iface;
    handle = NULL;
    if (FAILED(hr = query_string( content->handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, &text ))) goto failed;
    hr = parse_headers( text, impl->headers, content->headers );
    free( text );
    if (FAILED(hr)) goto failed;
    if (read_content && FAILED(hr = content_read_all( content ))) goto failed;

    *out = &impl->IHttpResponseMessage_iface;
    return S_OK;

failed:
    if (handle) WinHttpCloseHandle( handle );
    IHttpResponseMessage_Release( &impl->IHttpResponseMessage_iface );
    return hr;
}
