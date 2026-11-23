/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.Web.Http internal declarations
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#ifndef __LIBERNT_WINDOWS_WEB_HTTP_PRIVATE_H
#define __LIBERNT_WINDOWS_WEB_HTTP_PRIVATE_H

#include <stdarg.h>
#include <assert.h>
#include <wchar.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "roapi.h"
#include "winhttp.h"

#include "activation.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Storage_Streams
#include "windows.storage.streams.h"
#define WIDL_using_Windows_Security_Credentials
#define WIDL_using_Windows_Security_Cryptography_Certificates
#define WIDL_using_Windows_Web_Http
#define WIDL_using_Windows_Web_Http_Filters
#define WIDL_using_Windows_Web_Http_Headers
#include "windows.web.http.h"
#include "robuffer.h"
#include "async_private.h"

#include "wine/debug.h"

#define DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from, iface_mem, expr )             \
    static inline impl_type *impl_from( iface_type *iface )                                        \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_mem );                                   \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_QueryInterface( (IInspectable *)(expr), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_AddRef( (IInspectable *)(expr) );                                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_Release( (IInspectable *)(expr) );                                     \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetIids( (IInspectable *)(expr), iid_count, iids );                    \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(expr), class_name );             \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetTrustLevel( (IInspectable *)(expr), trust_level );                  \
    }
#define DEFINE_IINSPECTABLE( pfx, iface_type, impl_type, base_iface )                              \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, &impl->base_iface )
#define DEFINE_IINSPECTABLE_OUTER( pfx, iface_type, impl_type, outer_iface )                       \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, impl->outer_iface )

enum header_kind
{
    HEADERS_REQUEST,
    HEADERS_RESPONSE,
    HEADERS_CONTENT,
};

struct header_collection;
struct filter;

extern IActivationFactory *http_client_factory;
extern IActivationFactory *http_method_factory;
extern IActivationFactory *http_request_message_factory;
extern IActivationFactory *http_base_protocol_filter_factory;
extern IActivationFactory *http_date_or_delta_factory;

HRESULT async_operation_response_create( IUnknown *invoker, IUnknown *param, async_operation_callback callback,
        IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **out );
HRESULT async_operation_buffer_create( IUnknown *invoker, IUnknown *param, async_operation_callback callback,
        IAsyncOperationWithProgress_IBuffer_UINT64 **out );

HRESULT header_collection_create( enum header_kind kind, struct header_collection **out );
void header_collection_addref( struct header_collection *headers );
void header_collection_release( struct header_collection *headers );
HRESULT header_collection_get_iface( struct header_collection *headers, REFIID iid, void **out );
struct header_collection *header_collection_from_request_iface( IHttpRequestHeaderCollection *iface );
HRESULT header_collection_append( struct header_collection *headers, const WCHAR *name, UINT32 name_len,
        const WCHAR *value, UINT32 value_len );
BOOL header_collection_has( struct header_collection *headers, const WCHAR *name );
HRESULT header_collection_lookup( struct header_collection *headers, const WCHAR *name, HSTRING *value );
HRESULT header_collection_merge( struct header_collection *headers, struct header_collection *defaults );
HRESULT header_collection_format( struct header_collection *headers, struct header_collection *defaults,
        WCHAR **text, UINT32 *length );
BOOL header_is_content( const WCHAR *name, UINT32 name_len );

HRESULT date_or_delta_parse( HSTRING input, IHttpDateOrDeltaHeaderValue **value );

HRESULT http_method_create( const WCHAR *name, IHttpMethod **out );
HRESULT http_method_name( IHttpMethod *method, WCHAR **name );
HRESULT http_request_message_create( IHttpMethod *method, IUriRuntimeClass *uri, IHttpRequestMessage **out );
struct header_collection *http_request_message_headers( IHttpRequestMessage *request );
HRESULT http_request_message_set_uri( IHttpRequestMessage *request, const WCHAR *url );
HRESULT http_response_message_create( IHttpRequestMessage *request, HINTERNET handle, BOOL read_content,
        IHttpResponseMessage **out );

BOOL filter_is_base_protocol( IHttpFilter *iface );
HRESULT filter_create( IHttpFilter **out );
HRESULT filter_send( IHttpFilter *iface, IHttpRequestMessage *request, HttpCompletionOption option,
        IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation );

HRESULT create_uri( const WCHAR *text, IUriRuntimeClass **out );
HRESULT box_uint64( UINT64 value, IInspectable **out );
HRESULT box_date_time( DateTime value, IInspectable **out );
HRESULT box_time_span( TimeSpan value, IInspectable **out );

static inline const WCHAR *hstring_buffer( HSTRING string, UINT32 *length )
{
    return WindowsGetStringRawBuffer( string, length );
}

#endif
