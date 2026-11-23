/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.Web.Http.Headers collections and header values
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(webhttp);

struct header
{
    WCHAR *name;
    WCHAR *value;
};

struct header_collection
{
    IHttpRequestHeaderCollection IHttpRequestHeaderCollection_iface;
    IHttpResponseHeaderCollection IHttpResponseHeaderCollection_iface;
    IHttpContentHeaderCollection IHttpContentHeaderCollection_iface;
    IMap_HSTRING_HSTRING IMap_HSTRING_HSTRING_iface;
    IIterable_IKeyValuePair_HSTRING_HSTRING IIterable_IKeyValuePair_HSTRING_HSTRING_iface;
    IStringable IStringable_iface;
    LONG ref;
    enum header_kind kind;
    SRWLOCK lock;
    struct header *items;
    UINT32 count;
    UINT32 capacity;
};

static const WCHAR *const content_headers[] =
{
    L"Content-Disposition",
    L"Content-Encoding",
    L"Content-Language",
    L"Content-Length",
    L"Content-Location",
    L"Content-MD5",
    L"Content-Range",
    L"Content-Type",
    L"Expires",
    L"Last-Modified",
};

BOOL header_is_content( const WCHAR *name, UINT32 name_len )
{
    UINT32 i;

    for (i = 0; i < ARRAY_SIZE(content_headers); i++)
    {
        if (wcslen( content_headers[i] ) == name_len && !_wcsnicmp( content_headers[i], name, name_len ))
            return TRUE;
    }
    return FALSE;
}

static BOOL is_token_char( WCHAR c )
{
    if (c <= 0x20 || c >= 0x7f) return FALSE;
    return !wcschr( L"()<>@,;:\\\"/[]?={}", c );
}

static BOOL is_valid_name( const WCHAR *name, UINT32 len )
{
    UINT32 i;

    if (!len) return FALSE;
    for (i = 0; i < len; i++) if (!is_token_char( name[i] )) return FALSE;
    return TRUE;
}

static BOOL is_valid_value( const WCHAR *value, UINT32 len )
{
    UINT32 i;

    for (i = 0; i < len; i++) if (value[i] == '\r' || value[i] == '\n' || !value[i]) return FALSE;
    return TRUE;
}

static WCHAR *dup_range( const WCHAR *text, UINT32 len )
{
    WCHAR *ret = malloc( (len + 1) * sizeof(WCHAR) );

    if (!ret) return NULL;
    memcpy( ret, text, len * sizeof(WCHAR) );
    ret[len] = 0;
    return ret;
}

static void trim( const WCHAR **text, UINT32 *len )
{
    while (*len && (**text == ' ' || **text == '\t')) { (*text)++; (*len)--; }
    while (*len && ((*text)[*len - 1] == ' ' || (*text)[*len - 1] == '\t')) (*len)--;
}

static struct header *find_header( struct header_collection *headers, const WCHAR *name, UINT32 name_len )
{
    UINT32 i;

    for (i = 0; i < headers->count; i++)
    {
        if (wcslen( headers->items[i].name ) == name_len && !_wcsnicmp( headers->items[i].name, name, name_len ))
            return &headers->items[i];
    }
    return NULL;
}

static HRESULT append_locked( struct header_collection *headers, const WCHAR *name, UINT32 name_len,
                              const WCHAR *value, UINT32 value_len, const WCHAR *separator )
{
    struct header *header;
    WCHAR *joined;
    size_t old_len, sep_len = wcslen( separator );

    trim( &value, &value_len );
    if ((header = find_header( headers, name, name_len )))
    {
        old_len = wcslen( header->value );
        if (!value_len) return S_OK;
        if (!old_len)
        {
            if (!(joined = dup_range( value, value_len ))) return E_OUTOFMEMORY;
        }
        else
        {
            if (!(joined = malloc( (old_len + sep_len + value_len + 1) * sizeof(WCHAR) ))) return E_OUTOFMEMORY;
            memcpy( joined, header->value, old_len * sizeof(WCHAR) );
            memcpy( joined + old_len, separator, sep_len * sizeof(WCHAR) );
            memcpy( joined + old_len + sep_len, value, value_len * sizeof(WCHAR) );
            joined[old_len + sep_len + value_len] = 0;
        }
        free( header->value );
        header->value = joined;
        return S_OK;
    }

    if (headers->count == headers->capacity)
    {
        UINT32 capacity = headers->capacity ? headers->capacity * 2 : 8;
        struct header *items = realloc( headers->items, capacity * sizeof(*items) );

        if (!items) return E_OUTOFMEMORY;
        headers->items = items;
        headers->capacity = capacity;
    }
    header = &headers->items[headers->count];
    header->name = dup_range( name, name_len );
    header->value = dup_range( value, value_len );
    if (!header->name || !header->value)
    {
        free( header->name );
        free( header->value );
        return E_OUTOFMEMORY;
    }
    headers->count++;
    return S_OK;
}

static void remove_locked( struct header_collection *headers, struct header *header )
{
    UINT32 index = header - headers->items;

    assert( index < headers->count );
    free( header->name );
    free( header->value );
    memmove( header, header + 1, (headers->count - index - 1) * sizeof(*header) );
    headers->count--;
}

static const WCHAR *list_separator( const WCHAR *name, UINT32 name_len )
{
    if (name_len == 10 && !_wcsnicmp( name, L"User-Agent", 10 )) return L" ";
    return L", ";
}

HRESULT header_collection_append( struct header_collection *headers, const WCHAR *name, UINT32 name_len,
                                  const WCHAR *value, UINT32 value_len )
{
    HRESULT hr;

    AcquireSRWLockExclusive( &headers->lock );
    hr = append_locked( headers, name, name_len, value, value_len, list_separator( name, name_len ) );
    ReleaseSRWLockExclusive( &headers->lock );
    return hr;
}

BOOL header_collection_has( struct header_collection *headers, const WCHAR *name )
{
    BOOL ret;

    AcquireSRWLockShared( &headers->lock );
    ret = find_header( headers, name, wcslen( name ) ) != NULL;
    ReleaseSRWLockShared( &headers->lock );
    return ret;
}

HRESULT header_collection_lookup( struct header_collection *headers, const WCHAR *name, HSTRING *value )
{
    struct header *header;
    HRESULT hr = E_BOUNDS;

    *value = NULL;
    AcquireSRWLockShared( &headers->lock );
    if ((header = find_header( headers, name, wcslen( name ) )))
        hr = WindowsCreateString( header->value, wcslen( header->value ), value );
    ReleaseSRWLockShared( &headers->lock );
    return hr;
}

static HRESULT append_text( WCHAR **text, UINT32 *length, UINT32 *capacity, const WCHAR *name, const WCHAR *value )
{
    UINT32 name_len = wcslen( name ), value_len = wcslen( value ), need = name_len + value_len + 4;

    if (*length + need + 1 > *capacity)
    {
        UINT32 size = max( *capacity * 2, *length + need + 1 );
        WCHAR *grown = realloc( *text, size * sizeof(WCHAR) );

        if (!grown) return E_OUTOFMEMORY;
        *text = grown;
        *capacity = size;
    }
    memcpy( *text + *length, name, name_len * sizeof(WCHAR) );
    *length += name_len;
    memcpy( *text + *length, L": ", 2 * sizeof(WCHAR) );
    *length += 2;
    memcpy( *text + *length, value, value_len * sizeof(WCHAR) );
    *length += value_len;
    memcpy( *text + *length, L"\r\n", 2 * sizeof(WCHAR) );
    *length += 2;
    (*text)[*length] = 0;
    return S_OK;
}

HRESULT header_collection_merge( struct header_collection *headers, struct header_collection *defaults )
{
    UINT32 length, pos = 0;
    WCHAR *text;
    HRESULT hr;

    if (FAILED(hr = header_collection_format( NULL, defaults, &text, &length ))) return hr;
    while (SUCCEEDED(hr) && pos < length)
    {
        WCHAR *line = text + pos, *end = wcsstr( line, L"\r\n" ), *colon = wcschr( line, ':' );

        assert( end && colon && colon < end );
        AcquireSRWLockExclusive( &headers->lock );
        if (!find_header( headers, line, colon - line ))
            hr = append_locked( headers, line, colon - line, colon + 2, end - colon - 2, list_separator( line, colon - line ) );
        ReleaseSRWLockExclusive( &headers->lock );
        pos = end + 2 - text;
    }
    free( text );
    return hr;
}

static BOOL text_has_name( const WCHAR *text, UINT32 length, const WCHAR *name )
{
    UINT32 name_len = wcslen( name ), pos = 0;

    while (pos < length)
    {
        const WCHAR *line = text + pos, *end = wcsstr( line, L"\r\n" );

        assert( end );
        if (line[name_len] == ':' && !_wcsnicmp( line, name, name_len )) return TRUE;
        pos = end + 2 - text;
    }
    return FALSE;
}

HRESULT header_collection_format( struct header_collection *headers, struct header_collection *defaults,
                                  WCHAR **text, UINT32 *length )
{
    UINT32 capacity = 0, i, own;
    HRESULT hr = S_OK;

    *text = NULL;
    *length = 0;
    if (headers)
    {
        AcquireSRWLockShared( &headers->lock );
        for (i = 0; SUCCEEDED(hr) && i < headers->count; i++)
            hr = append_text( text, length, &capacity, headers->items[i].name, headers->items[i].value );
        ReleaseSRWLockShared( &headers->lock );
    }
    own = *length;
    if (defaults && SUCCEEDED(hr))
    {
        AcquireSRWLockShared( &defaults->lock );
        for (i = 0; SUCCEEDED(hr) && i < defaults->count; i++)
        {
            if (*text && text_has_name( *text, own, defaults->items[i].name )) continue;
            hr = append_text( text, length, &capacity, defaults->items[i].name, defaults->items[i].value );
        }
        ReleaseSRWLockShared( &defaults->lock );
    }
    if (FAILED(hr))
    {
        free( *text );
        *text = NULL;
        *length = 0;
    }
    return hr;
}

static inline struct header_collection *impl_from_IMap_HSTRING_HSTRING( IMap_HSTRING_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct header_collection, IMap_HSTRING_HSTRING_iface );
}

static IInspectable *header_collection_outer( struct header_collection *impl )
{
    switch (impl->kind)
    {
    case HEADERS_REQUEST: return (IInspectable *)&impl->IHttpRequestHeaderCollection_iface;
    case HEADERS_RESPONSE: return (IInspectable *)&impl->IHttpResponseHeaderCollection_iface;
    case HEADERS_CONTENT: return (IInspectable *)&impl->IHttpContentHeaderCollection_iface;
    }
    assert( 0 );
    return NULL;
}

static HRESULT header_collection_query( struct header_collection *impl, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
        *out = header_collection_outer( impl );
    else if (impl->kind == HEADERS_REQUEST && IsEqualGUID( iid, &IID_IHttpRequestHeaderCollection ))
        *out = &impl->IHttpRequestHeaderCollection_iface;
    else if (impl->kind == HEADERS_RESPONSE && IsEqualGUID( iid, &IID_IHttpResponseHeaderCollection ))
        *out = &impl->IHttpResponseHeaderCollection_iface;
    else if (impl->kind == HEADERS_CONTENT && IsEqualGUID( iid, &IID_IHttpContentHeaderCollection ))
        *out = &impl->IHttpContentHeaderCollection_iface;
    else if (IsEqualGUID( iid, &IID_IMap_HSTRING_HSTRING ))
        *out = &impl->IMap_HSTRING_HSTRING_iface;
    else if (IsEqualGUID( iid, &IID_IIterable_IKeyValuePair_HSTRING_HSTRING ))
        *out = &impl->IIterable_IKeyValuePair_HSTRING_HSTRING_iface;
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

void header_collection_addref( struct header_collection *headers )
{
    InterlockedIncrement( &headers->ref );
}

void header_collection_release( struct header_collection *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );
    UINT32 i;

    if (ref) return;
    for (i = 0; i < impl->count; i++)
    {
        free( impl->items[i].name );
        free( impl->items[i].value );
    }
    free( impl->items );
    free( impl );
}

HRESULT header_collection_get_iface( struct header_collection *headers, REFIID iid, void **out )
{
    return header_collection_query( headers, iid, out );
}

static HRESULT WINAPI map_QueryInterface( IMap_HSTRING_HSTRING *iface, REFIID iid, void **out )
{
    return header_collection_query( impl_from_IMap_HSTRING_HSTRING( iface ), iid, out );
}

static ULONG WINAPI map_AddRef( IMap_HSTRING_HSTRING *iface )
{
    return InterlockedIncrement( &impl_from_IMap_HSTRING_HSTRING( iface )->ref );
}

static ULONG WINAPI map_Release( IMap_HSTRING_HSTRING *iface )
{
    struct header_collection *impl = impl_from_IMap_HSTRING_HSTRING( iface );
    ULONG ref = impl->ref - 1;

    header_collection_release( impl );
    return ref;
}

static HRESULT WINAPI map_GetIids( IMap_HSTRING_HSTRING *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI map_GetRuntimeClassName( IMap_HSTRING_HSTRING *iface, HSTRING *class_name )
{
    struct header_collection *impl = impl_from_IMap_HSTRING_HSTRING( iface );
    const WCHAR *name;

    switch (impl->kind)
    {
    case HEADERS_REQUEST: name = RuntimeClass_Windows_Web_Http_Headers_HttpRequestHeaderCollection; break;
    case HEADERS_RESPONSE: name = RuntimeClass_Windows_Web_Http_Headers_HttpResponseHeaderCollection; break;
    default: name = RuntimeClass_Windows_Web_Http_Headers_HttpContentHeaderCollection; break;
    }
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI map_GetTrustLevel( IMap_HSTRING_HSTRING *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI map_Lookup( IMap_HSTRING_HSTRING *iface, HSTRING key, HSTRING *value )
{
    struct header_collection *impl = impl_from_IMap_HSTRING_HSTRING( iface );
    struct header *header;
    UINT32 len;
    const WCHAR *name = hstring_buffer( key, &len );
    HRESULT hr = E_BOUNDS;

    TRACE( "iface %p, key %s, value %p.\n", iface, debugstr_hstring( key ), value );

    if (!value) return E_POINTER;
    *value = NULL;
    AcquireSRWLockShared( &impl->lock );
    if ((header = find_header( impl, name, len )))
        hr = WindowsCreateString( header->value, wcslen( header->value ), value );
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}

static HRESULT WINAPI map_get_Size( IMap_HSTRING_HSTRING *iface, UINT32 *size )
{
    struct header_collection *impl = impl_from_IMap_HSTRING_HSTRING( iface );

    if (!size) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *size = impl->count;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI map_HasKey( IMap_HSTRING_HSTRING *iface, HSTRING key, boolean *found )
{
    struct header_collection *impl = impl_from_IMap_HSTRING_HSTRING( iface );
    UINT32 len;
    const WCHAR *name = hstring_buffer( key, &len );

    if (!found) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *found = find_header( impl, name, len ) != NULL;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI map_GetView( IMap_HSTRING_HSTRING *iface, IMapView_HSTRING_HSTRING **view )
{
    /* TODO: IMapView of the header collections */
    FIXME( "iface %p, view %p stub!\n", iface, view );
    return E_NOTIMPL;
}

static HRESULT WINAPI map_Insert( IMap_HSTRING_HSTRING *iface, HSTRING key, HSTRING value, boolean *replaced )
{
    struct header_collection *impl = impl_from_IMap_HSTRING_HSTRING( iface );
    UINT32 name_len, value_len;
    const WCHAR *name = hstring_buffer( key, &name_len ), *text = hstring_buffer( value, &value_len );
    struct header *header;
    HRESULT hr;

    TRACE( "iface %p, key %s, value %s, replaced %p.\n", iface, debugstr_hstring( key ), debugstr_hstring( value ), replaced );

    if (!replaced) return E_POINTER;
    if (!is_valid_name( name, name_len ) || !is_valid_value( text, value_len )) return E_INVALIDARG;
    AcquireSRWLockExclusive( &impl->lock );
    if ((*replaced = (header = find_header( impl, name, name_len )) != NULL)) remove_locked( impl, header );
    hr = append_locked( impl, name, name_len, text, value_len, L", " );
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}

static HRESULT WINAPI map_Remove( IMap_HSTRING_HSTRING *iface, HSTRING key )
{
    struct header_collection *impl = impl_from_IMap_HSTRING_HSTRING( iface );
    struct header *header;
    UINT32 len;
    const WCHAR *name = hstring_buffer( key, &len );
    HRESULT hr = E_BOUNDS;

    AcquireSRWLockExclusive( &impl->lock );
    if ((header = find_header( impl, name, len )))
    {
        remove_locked( impl, header );
        hr = S_OK;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}

static HRESULT WINAPI map_Clear( IMap_HSTRING_HSTRING *iface )
{
    struct header_collection *impl = impl_from_IMap_HSTRING_HSTRING( iface );

    AcquireSRWLockExclusive( &impl->lock );
    while (impl->count) remove_locked( impl, &impl->items[impl->count - 1] );
    ReleaseSRWLockExclusive( &impl->lock );
    return S_OK;
}

static const struct IMap_HSTRING_HSTRINGVtbl map_vtbl =
{
    map_QueryInterface,
    map_AddRef,
    map_Release,
    map_GetIids,
    map_GetRuntimeClassName,
    map_GetTrustLevel,
    map_Lookup,
    map_get_Size,
    map_HasKey,
    map_GetView,
    map_Insert,
    map_Remove,
    map_Clear,
};

struct key_value_pair
{
    IKeyValuePair_HSTRING_HSTRING IKeyValuePair_HSTRING_HSTRING_iface;
    LONG ref;
    HSTRING key;
    HSTRING value;
};

static inline struct key_value_pair *impl_from_IKeyValuePair_HSTRING_HSTRING( IKeyValuePair_HSTRING_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct key_value_pair, IKeyValuePair_HSTRING_HSTRING_iface );
}

static HRESULT WINAPI pair_QueryInterface( IKeyValuePair_HSTRING_HSTRING *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IKeyValuePair_HSTRING_HSTRING ))
    {
        IKeyValuePair_HSTRING_HSTRING_AddRef( (*out = iface) );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI pair_AddRef( IKeyValuePair_HSTRING_HSTRING *iface )
{
    return InterlockedIncrement( &impl_from_IKeyValuePair_HSTRING_HSTRING( iface )->ref );
}

static ULONG WINAPI pair_Release( IKeyValuePair_HSTRING_HSTRING *iface )
{
    struct key_value_pair *impl = impl_from_IKeyValuePair_HSTRING_HSTRING( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->key );
        WindowsDeleteString( impl->value );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI pair_GetIids( IKeyValuePair_HSTRING_HSTRING *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI pair_GetRuntimeClassName( IKeyValuePair_HSTRING_HSTRING *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IKeyValuePair`2<String, String>";
    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, class_name );
}

static HRESULT WINAPI pair_GetTrustLevel( IKeyValuePair_HSTRING_HSTRING *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI pair_get_Key( IKeyValuePair_HSTRING_HSTRING *iface, HSTRING *key )
{
    return WindowsDuplicateString( impl_from_IKeyValuePair_HSTRING_HSTRING( iface )->key, key );
}

static HRESULT WINAPI pair_get_Value( IKeyValuePair_HSTRING_HSTRING *iface, HSTRING *value )
{
    return WindowsDuplicateString( impl_from_IKeyValuePair_HSTRING_HSTRING( iface )->value, value );
}

static const struct IKeyValuePair_HSTRING_HSTRINGVtbl pair_vtbl =
{
    pair_QueryInterface,
    pair_AddRef,
    pair_Release,
    pair_GetIids,
    pair_GetRuntimeClassName,
    pair_GetTrustLevel,
    pair_get_Key,
    pair_get_Value,
};

struct header_iterator
{
    IIterator_IKeyValuePair_HSTRING_HSTRING IIterator_IKeyValuePair_HSTRING_HSTRING_iface;
    LONG ref;
    UINT32 index;
    UINT32 count;
    IKeyValuePair_HSTRING_HSTRING **pairs;
};

static inline struct header_iterator *impl_from_IIterator_IKeyValuePair_HSTRING_HSTRING( IIterator_IKeyValuePair_HSTRING_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct header_iterator, IIterator_IKeyValuePair_HSTRING_HSTRING_iface );
}

static HRESULT WINAPI iterator_QueryInterface( IIterator_IKeyValuePair_HSTRING_HSTRING *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIterator_IKeyValuePair_HSTRING_HSTRING ))
    {
        IIterator_IKeyValuePair_HSTRING_HSTRING_AddRef( (*out = iface) );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI iterator_AddRef( IIterator_IKeyValuePair_HSTRING_HSTRING *iface )
{
    return InterlockedIncrement( &impl_from_IIterator_IKeyValuePair_HSTRING_HSTRING( iface )->ref );
}

static ULONG WINAPI iterator_Release( IIterator_IKeyValuePair_HSTRING_HSTRING *iface )
{
    struct header_iterator *impl = impl_from_IIterator_IKeyValuePair_HSTRING_HSTRING( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    UINT32 i;

    if (!ref)
    {
        for (i = 0; i < impl->count; i++) IKeyValuePair_HSTRING_HSTRING_Release( impl->pairs[i] );
        free( impl->pairs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI iterator_GetIids( IIterator_IKeyValuePair_HSTRING_HSTRING *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI iterator_GetRuntimeClassName( IIterator_IKeyValuePair_HSTRING_HSTRING *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IIterator`1<Windows.Foundation.Collections.IKeyValuePair`2<String, String>>";
    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, class_name );
}

static HRESULT WINAPI iterator_GetTrustLevel( IIterator_IKeyValuePair_HSTRING_HSTRING *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI iterator_get_Current( IIterator_IKeyValuePair_HSTRING_HSTRING *iface, IKeyValuePair_HSTRING_HSTRING **value )
{
    struct header_iterator *impl = impl_from_IIterator_IKeyValuePair_HSTRING_HSTRING( iface );

    if (!value) return E_POINTER;
    *value = NULL;
    if (impl->index >= impl->count) return E_BOUNDS;
    IKeyValuePair_HSTRING_HSTRING_AddRef( (*value = impl->pairs[impl->index]) );
    return S_OK;
}

static HRESULT WINAPI iterator_get_HasCurrent( IIterator_IKeyValuePair_HSTRING_HSTRING *iface, boolean *value )
{
    struct header_iterator *impl = impl_from_IIterator_IKeyValuePair_HSTRING_HSTRING( iface );

    if (!value) return E_POINTER;
    *value = impl->index < impl->count;
    return S_OK;
}

static HRESULT WINAPI iterator_MoveNext( IIterator_IKeyValuePair_HSTRING_HSTRING *iface, boolean *value )
{
    struct header_iterator *impl = impl_from_IIterator_IKeyValuePair_HSTRING_HSTRING( iface );

    if (!value) return E_POINTER;
    if (impl->index < impl->count) impl->index++;
    *value = impl->index < impl->count;
    return S_OK;
}

static HRESULT WINAPI iterator_GetMany( IIterator_IKeyValuePair_HSTRING_HSTRING *iface, UINT32 items_size,
                                        IKeyValuePair_HSTRING_HSTRING **items, UINT32 *count )
{
    struct header_iterator *impl = impl_from_IIterator_IKeyValuePair_HSTRING_HSTRING( iface );
    UINT32 i;

    if (!count || (items_size && !items)) return E_POINTER;
    for (i = 0; i < items_size && impl->index < impl->count; i++, impl->index++)
        IKeyValuePair_HSTRING_HSTRING_AddRef( (items[i] = impl->pairs[impl->index]) );
    *count = i;
    return S_OK;
}

static const struct IIterator_IKeyValuePair_HSTRING_HSTRINGVtbl iterator_vtbl =
{
    iterator_QueryInterface,
    iterator_AddRef,
    iterator_Release,
    iterator_GetIids,
    iterator_GetRuntimeClassName,
    iterator_GetTrustLevel,
    iterator_get_Current,
    iterator_get_HasCurrent,
    iterator_MoveNext,
    iterator_GetMany,
};

static HRESULT create_pair( const WCHAR *key, const WCHAR *value, IKeyValuePair_HSTRING_HSTRING **out )
{
    struct key_value_pair *pair;
    HRESULT hr;

    if (!(pair = calloc( 1, sizeof(*pair) ))) return E_OUTOFMEMORY;
    pair->IKeyValuePair_HSTRING_HSTRING_iface.lpVtbl = &pair_vtbl;
    pair->ref = 1;
    if (FAILED(hr = WindowsCreateString( key, wcslen( key ), &pair->key )) ||
        FAILED(hr = WindowsCreateString( value, wcslen( value ), &pair->value )))
    {
        IKeyValuePair_HSTRING_HSTRING_Release( &pair->IKeyValuePair_HSTRING_HSTRING_iface );
        return hr;
    }
    *out = &pair->IKeyValuePair_HSTRING_HSTRING_iface;
    return S_OK;
}

DEFINE_IINSPECTABLE_( iterable, IIterable_IKeyValuePair_HSTRING_HSTRING, struct header_collection,
                      impl_from_IIterable_IKeyValuePair_HSTRING_HSTRING, IIterable_IKeyValuePair_HSTRING_HSTRING_iface,
                      &impl->IMap_HSTRING_HSTRING_iface )

static HRESULT WINAPI iterable_First( IIterable_IKeyValuePair_HSTRING_HSTRING *iface, IIterator_IKeyValuePair_HSTRING_HSTRING **value )
{
    struct header_collection *impl = impl_from_IIterable_IKeyValuePair_HSTRING_HSTRING( iface );
    struct header_iterator *iterator;
    HRESULT hr = S_OK;
    UINT32 i;

    if (!value) return E_POINTER;
    *value = NULL;
    if (!(iterator = calloc( 1, sizeof(*iterator) ))) return E_OUTOFMEMORY;
    iterator->IIterator_IKeyValuePair_HSTRING_HSTRING_iface.lpVtbl = &iterator_vtbl;
    iterator->ref = 1;

    AcquireSRWLockShared( &impl->lock );
    if (impl->count && !(iterator->pairs = calloc( impl->count, sizeof(*iterator->pairs) ))) hr = E_OUTOFMEMORY;
    for (i = 0; SUCCEEDED(hr) && i < impl->count; i++)
    {
        if (SUCCEEDED(hr = create_pair( impl->items[i].name, impl->items[i].value, &iterator->pairs[i] )))
            iterator->count++;
    }
    ReleaseSRWLockShared( &impl->lock );

    if (FAILED(hr))
    {
        IIterator_IKeyValuePair_HSTRING_HSTRING_Release( &iterator->IIterator_IKeyValuePair_HSTRING_HSTRING_iface );
        return hr;
    }
    *value = &iterator->IIterator_IKeyValuePair_HSTRING_HSTRING_iface;
    return S_OK;
}

static const struct IIterable_IKeyValuePair_HSTRING_HSTRINGVtbl iterable_vtbl =
{
    iterable_QueryInterface,
    iterable_AddRef,
    iterable_Release,
    iterable_GetIids,
    iterable_GetRuntimeClassName,
    iterable_GetTrustLevel,
    iterable_First,
};

DEFINE_IINSPECTABLE( headers_stringable, IStringable, struct header_collection, IMap_HSTRING_HSTRING_iface )

static HRESULT WINAPI headers_stringable_ToString( IStringable *iface, HSTRING *value )
{
    struct header_collection *impl = impl_from_IStringable( iface );
    UINT32 length;
    WCHAR *text;
    HRESULT hr;

    if (FAILED(hr = header_collection_format( impl, NULL, &text, &length ))) return hr;
    hr = WindowsCreateString( text, length, value );
    free( text );
    return hr;
}

static const struct IStringableVtbl headers_stringable_vtbl =
{
    headers_stringable_QueryInterface,
    headers_stringable_AddRef,
    headers_stringable_Release,
    headers_stringable_GetIids,
    headers_stringable_GetRuntimeClassName,
    headers_stringable_GetTrustLevel,
    headers_stringable_ToString,
};

static HRESULT headers_append( struct header_collection *impl, HSTRING name, HSTRING value )
{
    UINT32 name_len, value_len;
    const WCHAR *name_buf = hstring_buffer( name, &name_len ), *value_buf = hstring_buffer( value, &value_len );

    TRACE( "headers %p, name %s, value %s.\n", impl, debugstr_hstring( name ), debugstr_hstring( value ) );

    if (!is_valid_name( name_buf, name_len ) || !is_valid_value( value_buf, value_len )) return E_INVALIDARG;
    return header_collection_append( impl, name_buf, name_len, value_buf, value_len );
}

static HRESULT headers_try_append( struct header_collection *impl, HSTRING name, HSTRING value, boolean *result )
{
    UINT32 name_len, value_len;
    const WCHAR *name_buf = hstring_buffer( name, &name_len ), *value_buf = hstring_buffer( value, &value_len );
    HRESULT hr;

    if (!result) return E_POINTER;
    *result = FALSE;
    if (!is_valid_name( name_buf, name_len ) || !is_valid_value( value_buf, value_len )) return S_OK;
    if (FAILED(hr = header_collection_append( impl, name_buf, name_len, value_buf, value_len ))) return hr;
    *result = TRUE;
    return S_OK;
}

enum value_list_kind
{
    LIST_CONNECTION,
    LIST_USER_AGENT,
};

static HRESULT value_list_create( struct header_collection *owner, enum value_list_kind kind, void **out );

#define HEADERS_STUB( pfx, iface_type, name, arg_type )                                            \
    static HRESULT WINAPI pfx##_##name( iface_type *iface, arg_type value )                        \
    {                                                                                              \
        FIXME( "iface %p, value %p stub!\n", iface, (void *)(UINT_PTR)value );                     \
        return E_NOTIMPL;                                                                          \
    }

DEFINE_IINSPECTABLE( request_headers, IHttpRequestHeaderCollection, struct header_collection, IMap_HSTRING_HSTRING_iface )

/* TODO: typed accessors of the other request headers */
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_Accept, IHttpMediaTypeWithQualityHeaderValueCollection ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_AcceptEncoding, IHttpContentCodingWithQualityHeaderValueCollection ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_AcceptLanguage, IHttpLanguageRangeWithQualityHeaderValueCollection ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_Authorization, IHttpCredentialsHeaderValue ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, put_Authorization, IHttpCredentialsHeaderValue * )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_CacheControl, IHttpCacheDirectiveHeaderValueCollection ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_Cookie, IHttpCookiePairHeaderValueCollection ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_Date, IReference_DateTime ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, put_Date, IReference_DateTime * )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_Expect, IHttpExpectationHeaderValueCollection ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_From, HSTRING * )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, put_From, HSTRING )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_Host, __x_ABI_CWindows_CNetworking_CIHostName ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, put_Host, __x_ABI_CWindows_CNetworking_CIHostName * )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_IfModifiedSince, IReference_DateTime ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, put_IfModifiedSince, IReference_DateTime * )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_IfUnmodifiedSince, IReference_DateTime ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, put_IfUnmodifiedSince, IReference_DateTime * )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_MaxForwards, IReference_UINT32 ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, put_MaxForwards, IReference_UINT32 * )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_ProxyAuthorization, IHttpCredentialsHeaderValue ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, put_ProxyAuthorization, IHttpCredentialsHeaderValue * )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_Referer, IUriRuntimeClass ** )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, put_Referer, IUriRuntimeClass * )
HEADERS_STUB( request_headers, IHttpRequestHeaderCollection, get_TransferEncoding, IHttpTransferCodingHeaderValueCollection ** )

static HRESULT WINAPI request_headers_get_Connection( IHttpRequestHeaderCollection *iface, IHttpConnectionOptionHeaderValueCollection **value )
{
    if (!value) return E_POINTER;
    return value_list_create( impl_from_IHttpRequestHeaderCollection( iface ), LIST_CONNECTION, (void **)value );
}

static HRESULT WINAPI request_headers_get_UserAgent( IHttpRequestHeaderCollection *iface, IHttpProductInfoHeaderValueCollection **value )
{
    if (!value) return E_POINTER;
    return value_list_create( impl_from_IHttpRequestHeaderCollection( iface ), LIST_USER_AGENT, (void **)value );
}

static HRESULT WINAPI request_headers_Append( IHttpRequestHeaderCollection *iface, HSTRING name, HSTRING value )
{
    return headers_append( impl_from_IHttpRequestHeaderCollection( iface ), name, value );
}

static HRESULT WINAPI request_headers_TryAppendWithoutValidation( IHttpRequestHeaderCollection *iface, HSTRING name,
                                                                  HSTRING value, boolean *result )
{
    return headers_try_append( impl_from_IHttpRequestHeaderCollection( iface ), name, value, result );
}

static const struct IHttpRequestHeaderCollectionVtbl request_headers_vtbl =
{
    request_headers_QueryInterface,
    request_headers_AddRef,
    request_headers_Release,
    request_headers_GetIids,
    request_headers_GetRuntimeClassName,
    request_headers_GetTrustLevel,
    request_headers_get_Accept,
    request_headers_get_AcceptEncoding,
    request_headers_get_AcceptLanguage,
    request_headers_get_Authorization,
    request_headers_put_Authorization,
    request_headers_get_CacheControl,
    request_headers_get_Connection,
    request_headers_get_Cookie,
    request_headers_get_Date,
    request_headers_put_Date,
    request_headers_get_Expect,
    request_headers_get_From,
    request_headers_put_From,
    request_headers_get_Host,
    request_headers_put_Host,
    request_headers_get_IfModifiedSince,
    request_headers_put_IfModifiedSince,
    request_headers_get_IfUnmodifiedSince,
    request_headers_put_IfUnmodifiedSince,
    request_headers_get_MaxForwards,
    request_headers_put_MaxForwards,
    request_headers_get_ProxyAuthorization,
    request_headers_put_ProxyAuthorization,
    request_headers_get_Referer,
    request_headers_put_Referer,
    request_headers_get_TransferEncoding,
    request_headers_get_UserAgent,
    request_headers_Append,
    request_headers_TryAppendWithoutValidation,
};

DEFINE_IINSPECTABLE( response_headers, IHttpResponseHeaderCollection, struct header_collection, IMap_HSTRING_HSTRING_iface )

/* TODO: typed accessors of the other response headers */
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, get_Age, IReference_TimeSpan ** )
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, put_Age, IReference_TimeSpan * )
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, get_Allow, IHttpMethodHeaderValueCollection ** )
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, get_CacheControl, IHttpCacheDirectiveHeaderValueCollection ** )
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, get_Date, IReference_DateTime ** )
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, put_Date, IReference_DateTime * )
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, get_Location, IUriRuntimeClass ** )
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, put_Location, IUriRuntimeClass * )
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, get_ProxyAuthenticate, IHttpChallengeHeaderValueCollection ** )
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, put_RetryAfter, IHttpDateOrDeltaHeaderValue * )
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, get_TransferEncoding, IHttpTransferCodingHeaderValueCollection ** )
HEADERS_STUB( response_headers, IHttpResponseHeaderCollection, get_WwwAuthenticate, IHttpChallengeHeaderValueCollection ** )

static HRESULT WINAPI response_headers_get_Connection( IHttpResponseHeaderCollection *iface, IHttpConnectionOptionHeaderValueCollection **value )
{
    if (!value) return E_POINTER;
    return value_list_create( impl_from_IHttpResponseHeaderCollection( iface ), LIST_CONNECTION, (void **)value );
}

static HRESULT WINAPI response_headers_get_RetryAfter( IHttpResponseHeaderCollection *iface, IHttpDateOrDeltaHeaderValue **value )
{
    struct header_collection *impl = impl_from_IHttpResponseHeaderCollection( iface );
    HSTRING text;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    if (header_collection_lookup( impl, L"Retry-After", &text ) != S_OK) return S_OK;
    hr = date_or_delta_parse( text, value );
    WindowsDeleteString( text );
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT WINAPI response_headers_Append( IHttpResponseHeaderCollection *iface, HSTRING name, HSTRING value )
{
    return headers_append( impl_from_IHttpResponseHeaderCollection( iface ), name, value );
}

static HRESULT WINAPI response_headers_TryAppendWithoutValidation( IHttpResponseHeaderCollection *iface, HSTRING name,
                                                                   HSTRING value, boolean *result )
{
    return headers_try_append( impl_from_IHttpResponseHeaderCollection( iface ), name, value, result );
}

static const struct IHttpResponseHeaderCollectionVtbl response_headers_vtbl =
{
    response_headers_QueryInterface,
    response_headers_AddRef,
    response_headers_Release,
    response_headers_GetIids,
    response_headers_GetRuntimeClassName,
    response_headers_GetTrustLevel,
    response_headers_get_Age,
    response_headers_put_Age,
    response_headers_get_Allow,
    response_headers_get_CacheControl,
    response_headers_get_Connection,
    response_headers_get_Date,
    response_headers_put_Date,
    response_headers_get_Location,
    response_headers_put_Location,
    response_headers_get_ProxyAuthenticate,
    response_headers_get_RetryAfter,
    response_headers_put_RetryAfter,
    response_headers_get_TransferEncoding,
    response_headers_get_WwwAuthenticate,
    response_headers_Append,
    response_headers_TryAppendWithoutValidation,
};

DEFINE_IINSPECTABLE( content_headers, IHttpContentHeaderCollection, struct header_collection, IMap_HSTRING_HSTRING_iface )

/* TODO: typed accessors of the other content headers */
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, get_ContentDisposition, IHttpContentDispositionHeaderValue ** )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, put_ContentDisposition, IHttpContentDispositionHeaderValue * )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, get_ContentEncoding, IHttpContentCodingHeaderValueCollection ** )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, get_ContentLanguage, IHttpLanguageHeaderValueCollection ** )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, put_ContentLength, IReference_UINT64 * )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, get_ContentLocation, IUriRuntimeClass ** )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, put_ContentLocation, IUriRuntimeClass * )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, get_ContentMD5, IBuffer ** )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, put_ContentMD5, IBuffer * )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, get_ContentRange, IHttpContentRangeHeaderValue ** )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, put_ContentRange, IHttpContentRangeHeaderValue * )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, get_ContentType, IHttpMediaTypeHeaderValue ** )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, put_ContentType, IHttpMediaTypeHeaderValue * )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, get_Expires, IReference_DateTime ** )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, put_Expires, IReference_DateTime * )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, get_LastModified, IReference_DateTime ** )
HEADERS_STUB( content_headers, IHttpContentHeaderCollection, put_LastModified, IReference_DateTime * )

static BOOL parse_uint64( const WCHAR *text, UINT32 len, UINT64 *value )
{
    UINT64 result = 0;
    UINT32 i;

    trim( &text, &len );
    if (!len) return FALSE;
    for (i = 0; i < len; i++)
    {
        if (text[i] < '0' || text[i] > '9') return FALSE;
        if (result > (MAXUINT64 - (text[i] - '0')) / 10) return FALSE;
        result = result * 10 + (text[i] - '0');
    }
    *value = result;
    return TRUE;
}

static HRESULT WINAPI content_headers_get_ContentLength( IHttpContentHeaderCollection *iface, IReference_UINT64 **value )
{
    struct header_collection *impl = impl_from_IHttpContentHeaderCollection( iface );
    IInspectable *boxed;
    HSTRING text;
    const WCHAR *buffer;
    UINT64 length;
    UINT32 len;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    if (header_collection_lookup( impl, L"Content-Length", &text ) != S_OK) return S_OK;
    buffer = hstring_buffer( text, &len );
    if (!parse_uint64( buffer, len, &length ))
    {
        WindowsDeleteString( text );
        return S_OK;
    }
    WindowsDeleteString( text );
    if (FAILED(hr = box_uint64( length, &boxed ))) return hr;
    hr = IInspectable_QueryInterface( boxed, &IID_IReference_UINT64, (void **)value );
    IInspectable_Release( boxed );
    return hr;
}

static HRESULT WINAPI content_headers_Append( IHttpContentHeaderCollection *iface, HSTRING name, HSTRING value )
{
    return headers_append( impl_from_IHttpContentHeaderCollection( iface ), name, value );
}

static HRESULT WINAPI content_headers_TryAppendWithoutValidation( IHttpContentHeaderCollection *iface, HSTRING name,
                                                                  HSTRING value, boolean *result )
{
    return headers_try_append( impl_from_IHttpContentHeaderCollection( iface ), name, value, result );
}

static const struct IHttpContentHeaderCollectionVtbl content_headers_vtbl =
{
    content_headers_QueryInterface,
    content_headers_AddRef,
    content_headers_Release,
    content_headers_GetIids,
    content_headers_GetRuntimeClassName,
    content_headers_GetTrustLevel,
    content_headers_get_ContentDisposition,
    content_headers_put_ContentDisposition,
    content_headers_get_ContentEncoding,
    content_headers_get_ContentLanguage,
    content_headers_get_ContentLength,
    content_headers_put_ContentLength,
    content_headers_get_ContentLocation,
    content_headers_put_ContentLocation,
    content_headers_get_ContentMD5,
    content_headers_put_ContentMD5,
    content_headers_get_ContentRange,
    content_headers_put_ContentRange,
    content_headers_get_ContentType,
    content_headers_put_ContentType,
    content_headers_get_Expires,
    content_headers_put_Expires,
    content_headers_get_LastModified,
    content_headers_put_LastModified,
    content_headers_Append,
    content_headers_TryAppendWithoutValidation,
};

HRESULT header_collection_create( enum header_kind kind, struct header_collection **out )
{
    struct header_collection *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHttpRequestHeaderCollection_iface.lpVtbl = &request_headers_vtbl;
    impl->IHttpResponseHeaderCollection_iface.lpVtbl = &response_headers_vtbl;
    impl->IHttpContentHeaderCollection_iface.lpVtbl = &content_headers_vtbl;
    impl->IMap_HSTRING_HSTRING_iface.lpVtbl = &map_vtbl;
    impl->IIterable_IKeyValuePair_HSTRING_HSTRING_iface.lpVtbl = &iterable_vtbl;
    impl->IStringable_iface.lpVtbl = &headers_stringable_vtbl;
    impl->ref = 1;
    impl->kind = kind;
    InitializeSRWLock( &impl->lock );
    *out = impl;
    return S_OK;
}

struct header_collection *header_collection_from_request_iface( IHttpRequestHeaderCollection *iface )
{
    assert( iface->lpVtbl == &request_headers_vtbl );
    return impl_from_IHttpRequestHeaderCollection( iface );
}

struct header_value
{
    IHttpConnectionOptionHeaderValue IHttpConnectionOptionHeaderValue_iface;
    IHttpProductInfoHeaderValue IHttpProductInfoHeaderValue_iface;
    IHttpProductHeaderValue IHttpProductHeaderValue_iface;
    IStringable IStringable_iface;
    LONG ref;
    enum { VALUE_CONNECTION, VALUE_PRODUCT_INFO, VALUE_PRODUCT } kind;
    HSTRING text;
    HSTRING name;
    HSTRING version;
    HSTRING comment;
    struct header_value *product;
};

static IInspectable *header_value_outer( struct header_value *impl )
{
    switch (impl->kind)
    {
    case VALUE_CONNECTION: return (IInspectable *)&impl->IHttpConnectionOptionHeaderValue_iface;
    case VALUE_PRODUCT_INFO: return (IInspectable *)&impl->IHttpProductInfoHeaderValue_iface;
    case VALUE_PRODUCT: return (IInspectable *)&impl->IHttpProductHeaderValue_iface;
    }
    assert( 0 );
    return NULL;
}

static HRESULT header_value_query( struct header_value *impl, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) || IsEqualGUID( iid, &IID_IAgileObject ))
        *out = header_value_outer( impl );
    else if (impl->kind == VALUE_CONNECTION && IsEqualGUID( iid, &IID_IHttpConnectionOptionHeaderValue ))
        *out = &impl->IHttpConnectionOptionHeaderValue_iface;
    else if (impl->kind == VALUE_PRODUCT_INFO && IsEqualGUID( iid, &IID_IHttpProductInfoHeaderValue ))
        *out = &impl->IHttpProductInfoHeaderValue_iface;
    else if (impl->kind == VALUE_PRODUCT && IsEqualGUID( iid, &IID_IHttpProductHeaderValue ))
        *out = &impl->IHttpProductHeaderValue_iface;
    else if (IsEqualGUID( iid, &IID_IStringable ))
        *out = &impl->IStringable_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG header_value_release( struct header_value *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->text );
        WindowsDeleteString( impl->name );
        WindowsDeleteString( impl->version );
        WindowsDeleteString( impl->comment );
        if (impl->product) header_value_release( impl->product );
        free( impl );
    }
    return ref;
}

static const WCHAR *header_value_class( struct header_value *impl )
{
    switch (impl->kind)
    {
    case VALUE_CONNECTION: return RuntimeClass_Windows_Web_Http_Headers_HttpConnectionOptionHeaderValue;
    case VALUE_PRODUCT_INFO: return RuntimeClass_Windows_Web_Http_Headers_HttpProductInfoHeaderValue;
    default: return RuntimeClass_Windows_Web_Http_Headers_HttpProductHeaderValue;
    }
}

#define DEFINE_HEADER_VALUE_IUNKNOWN( pfx, iface_type )                                            \
    static inline struct header_value *impl_from_##iface_type( iface_type *iface )                 \
    {                                                                                              \
        return CONTAINING_RECORD( iface, struct header_value, iface_type##_iface );                \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        return header_value_query( impl_from_##iface_type( iface ), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        return InterlockedIncrement( &impl_from_##iface_type( iface )->ref );                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        return header_value_release( impl_from_##iface_type( iface ) );                            \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );                \
        return E_NOTIMPL;                                                                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        const WCHAR *name = header_value_class( impl_from_##iface_type( iface ) );                 \
        return WindowsCreateString( name, wcslen( name ), class_name );                            \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        *trust_level = BaseTrust;                                                                  \
        return S_OK;                                                                               \
    }

DEFINE_HEADER_VALUE_IUNKNOWN( connection_value, IHttpConnectionOptionHeaderValue )

static HRESULT WINAPI connection_value_get_Token( IHttpConnectionOptionHeaderValue *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl_from_IHttpConnectionOptionHeaderValue( iface )->text, value );
}

static const struct IHttpConnectionOptionHeaderValueVtbl connection_value_vtbl =
{
    connection_value_QueryInterface,
    connection_value_AddRef,
    connection_value_Release,
    connection_value_GetIids,
    connection_value_GetRuntimeClassName,
    connection_value_GetTrustLevel,
    connection_value_get_Token,
};

DEFINE_HEADER_VALUE_IUNKNOWN( product_info_value, IHttpProductInfoHeaderValue )

static HRESULT WINAPI product_info_value_get_Product( IHttpProductInfoHeaderValue *iface, IHttpProductHeaderValue **value )
{
    struct header_value *impl = impl_from_IHttpProductInfoHeaderValue( iface );

    if (!value) return E_POINTER;
    if (!(*value = impl->product ? &impl->product->IHttpProductHeaderValue_iface : NULL)) return S_OK;
    IHttpProductHeaderValue_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI product_info_value_get_Comment( IHttpProductInfoHeaderValue *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl_from_IHttpProductInfoHeaderValue( iface )->comment, value );
}

static const struct IHttpProductInfoHeaderValueVtbl product_info_value_vtbl =
{
    product_info_value_QueryInterface,
    product_info_value_AddRef,
    product_info_value_Release,
    product_info_value_GetIids,
    product_info_value_GetRuntimeClassName,
    product_info_value_GetTrustLevel,
    product_info_value_get_Product,
    product_info_value_get_Comment,
};

DEFINE_HEADER_VALUE_IUNKNOWN( product_value, IHttpProductHeaderValue )

static HRESULT WINAPI product_value_get_Name( IHttpProductHeaderValue *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl_from_IHttpProductHeaderValue( iface )->name, value );
}

static HRESULT WINAPI product_value_get_Version( IHttpProductHeaderValue *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl_from_IHttpProductHeaderValue( iface )->version, value );
}

static const struct IHttpProductHeaderValueVtbl product_value_vtbl =
{
    product_value_QueryInterface,
    product_value_AddRef,
    product_value_Release,
    product_value_GetIids,
    product_value_GetRuntimeClassName,
    product_value_GetTrustLevel,
    product_value_get_Name,
    product_value_get_Version,
};

DEFINE_IINSPECTABLE_( value_stringable, IStringable, struct header_value, impl_from_value_IStringable, IStringable_iface,
                      header_value_outer( impl ) )

static HRESULT WINAPI value_stringable_ToString( IStringable *iface, HSTRING *value )
{
    return WindowsDuplicateString( impl_from_value_IStringable( iface )->text, value );
}

static const struct IStringableVtbl value_stringable_vtbl =
{
    value_stringable_QueryInterface,
    value_stringable_AddRef,
    value_stringable_Release,
    value_stringable_GetIids,
    value_stringable_GetRuntimeClassName,
    value_stringable_GetTrustLevel,
    value_stringable_ToString,
};

static HRESULT header_value_create( int kind, const WCHAR *text, UINT32 len, struct header_value **out )
{
    struct header_value *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHttpConnectionOptionHeaderValue_iface.lpVtbl = &connection_value_vtbl;
    impl->IHttpProductInfoHeaderValue_iface.lpVtbl = &product_info_value_vtbl;
    impl->IHttpProductHeaderValue_iface.lpVtbl = &product_value_vtbl;
    impl->IStringable_iface.lpVtbl = &value_stringable_vtbl;
    impl->ref = 1;
    impl->kind = kind;
    if (FAILED(hr = WindowsCreateString( text, len, &impl->text )))
    {
        free( impl );
        return hr;
    }

    if (kind == VALUE_PRODUCT)
    {
        const WCHAR *slash = wmemchr( text, '/', len );
        UINT32 name_len = slash ? slash - text : len;

        hr = WindowsCreateString( text, name_len, &impl->name );
        if (SUCCEEDED(hr) && slash) hr = WindowsCreateString( slash + 1, len - name_len - 1, &impl->version );
    }
    else if (kind == VALUE_PRODUCT_INFO)
    {
        if (len && text[0] == '(') hr = WindowsCreateString( text, len, &impl->comment );
        else hr = header_value_create( VALUE_PRODUCT, text, len, &impl->product );
    }
    if (FAILED(hr))
    {
        header_value_release( impl );
        return hr;
    }
    *out = impl;
    return S_OK;
}

struct value_list
{
    IHttpConnectionOptionHeaderValueCollection IHttpConnectionOptionHeaderValueCollection_iface;
    IHttpProductInfoHeaderValueCollection IHttpProductInfoHeaderValueCollection_iface;
    IVector_HttpConnectionOptionHeaderValue IVector_HttpConnectionOptionHeaderValue_iface;
    IVector_HttpProductInfoHeaderValue IVector_HttpProductInfoHeaderValue_iface;
    IStringable IStringable_iface;
    LONG ref;
    enum value_list_kind kind;
    struct header_collection *owner;
};

static const WCHAR *value_list_header( struct value_list *impl )
{
    return impl->kind == LIST_CONNECTION ? L"Connection" : L"User-Agent";
}

static IInspectable *value_list_outer( struct value_list *impl )
{
    if (impl->kind == LIST_CONNECTION) return (IInspectable *)&impl->IHttpConnectionOptionHeaderValueCollection_iface;
    return (IInspectable *)&impl->IHttpProductInfoHeaderValueCollection_iface;
}

static HRESULT value_list_query( struct value_list *impl, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) || IsEqualGUID( iid, &IID_IAgileObject ))
        *out = value_list_outer( impl );
    else if (impl->kind == LIST_CONNECTION && IsEqualGUID( iid, &IID_IHttpConnectionOptionHeaderValueCollection ))
        *out = &impl->IHttpConnectionOptionHeaderValueCollection_iface;
    else if (impl->kind == LIST_CONNECTION && IsEqualGUID( iid, &IID_IVector_HttpConnectionOptionHeaderValue ))
        *out = &impl->IVector_HttpConnectionOptionHeaderValue_iface;
    else if (impl->kind == LIST_USER_AGENT && IsEqualGUID( iid, &IID_IHttpProductInfoHeaderValueCollection ))
        *out = &impl->IHttpProductInfoHeaderValueCollection_iface;
    else if (impl->kind == LIST_USER_AGENT && IsEqualGUID( iid, &IID_IVector_HttpProductInfoHeaderValue ))
        *out = &impl->IVector_HttpProductInfoHeaderValue_iface;
    else if (IsEqualGUID( iid, &IID_IStringable ))
        *out = &impl->IStringable_iface;
    else
    {
        /* TODO: IIterable of the typed header value collections */
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG value_list_release( struct value_list *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        header_collection_release( impl->owner );
        free( impl );
    }
    return ref;
}

static UINT32 value_list_split( struct value_list *impl, const WCHAR *text, UINT32 len, UINT32 index,
                                const WCHAR **item, UINT32 *item_len )
{
    UINT32 pos = 0, count = 0;

    while (pos < len)
    {
        UINT32 start, end;

        if (impl->kind == LIST_CONNECTION)
        {
            while (pos < len && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == ',')) pos++;
            start = pos;
            while (pos < len && text[pos] != ',') pos++;
            end = pos;
            while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t')) end--;
        }
        else
        {
            while (pos < len && (text[pos] == ' ' || text[pos] == '\t')) pos++;
            start = pos;
            if (pos < len && text[pos] == '(')
            {
                UINT32 depth = 0;

                for (; pos < len; pos++)
                {
                    if (text[pos] == '(') depth++;
                    else if (text[pos] == ')' && !--depth) { pos++; break; }
                }
            }
            else
            {
                while (pos < len && text[pos] != ' ' && text[pos] != '\t') pos++;
            }
            end = pos;
        }
        if (end == start) continue;
        if (count == index && item)
        {
            *item = text + start;
            *item_len = end - start;
        }
        count++;
    }
    return count;
}

static HRESULT value_list_add( struct value_list *impl, HSTRING input )
{
    UINT32 len, count;
    const WCHAR *text = hstring_buffer( input, &len );

    count = value_list_split( impl, text, len, 0, NULL, NULL );
    if (!count || !is_valid_value( text, len )) return E_INVALIDARG;
    return header_collection_append( impl->owner, value_list_header( impl ), wcslen( value_list_header( impl ) ), text, len );
}

#define DEFINE_VALUE_LIST_IUNKNOWN( pfx, iface_type )                                              \
    static inline struct value_list *impl_from_##iface_type( iface_type *iface )                   \
    {                                                                                              \
        return CONTAINING_RECORD( iface, struct value_list, iface_type##_iface );                  \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        return value_list_query( impl_from_##iface_type( iface ), iid, out );                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        return InterlockedIncrement( &impl_from_##iface_type( iface )->ref );                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        return value_list_release( impl_from_##iface_type( iface ) );                              \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );                \
        return E_NOTIMPL;                                                                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        const WCHAR *name = impl_from_##iface_type( iface )->kind == LIST_CONNECTION ?             \
            RuntimeClass_Windows_Web_Http_Headers_HttpConnectionOptionHeaderValueCollection :      \
            RuntimeClass_Windows_Web_Http_Headers_HttpProductInfoHeaderValueCollection;            \
        return WindowsCreateString( name, wcslen( name ), class_name );                            \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        *trust_level = BaseTrust;                                                                  \
        return S_OK;                                                                               \
    }

#define DEFINE_VALUE_LIST_PARSE( pfx, iface_type )                                                 \
    static HRESULT WINAPI pfx##_ParseAdd( iface_type *iface, HSTRING input )                       \
    {                                                                                              \
        TRACE( "iface %p, input %s.\n", iface, debugstr_hstring( input ) );                        \
        return value_list_add( impl_from_##iface_type( iface ), input );                           \
    }                                                                                              \
    static HRESULT WINAPI pfx##_TryParseAdd( iface_type *iface, HSTRING input, boolean *result )   \
    {                                                                                              \
        HRESULT hr;                                                                                \
        if (!result) return E_POINTER;                                                             \
        hr = value_list_add( impl_from_##iface_type( iface ), input );                             \
        *result = SUCCEEDED(hr);                                                                   \
        return hr == E_INVALIDARG ? S_OK : hr;                                                     \
    }                                                                                              \
    static const struct iface_type##Vtbl pfx##_vtbl =                                             \
    {                                                                                              \
        pfx##_QueryInterface,                                                                      \
        pfx##_AddRef,                                                                              \
        pfx##_Release,                                                                             \
        pfx##_GetIids,                                                                             \
        pfx##_GetRuntimeClassName,                                                                 \
        pfx##_GetTrustLevel,                                                                       \
        pfx##_ParseAdd,                                                                            \
        pfx##_TryParseAdd,                                                                         \
    };

DEFINE_VALUE_LIST_IUNKNOWN( connection_list, IHttpConnectionOptionHeaderValueCollection )
DEFINE_VALUE_LIST_PARSE( connection_list, IHttpConnectionOptionHeaderValueCollection )
DEFINE_VALUE_LIST_IUNKNOWN( product_list, IHttpProductInfoHeaderValueCollection )
DEFINE_VALUE_LIST_PARSE( product_list, IHttpProductInfoHeaderValueCollection )

static HRESULT value_list_size( struct value_list *impl, UINT32 *size )
{
    const WCHAR *buffer;
    HSTRING text;
    UINT32 len;

    if (!size) return E_POINTER;
    *size = 0;
    if (header_collection_lookup( impl->owner, value_list_header( impl ), &text ) != S_OK) return S_OK;
    buffer = hstring_buffer( text, &len );
    *size = value_list_split( impl, buffer, len, 0, NULL, NULL );
    WindowsDeleteString( text );
    return S_OK;
}

static HRESULT value_list_get_at( struct value_list *impl, UINT32 index, IInspectable **value )
{
    struct header_value *item;
    const WCHAR *start = NULL, *buffer;
    UINT32 len, item_len = 0, count;
    HSTRING text;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    if (header_collection_lookup( impl->owner, value_list_header( impl ), &text ) != S_OK) return E_BOUNDS;
    buffer = hstring_buffer( text, &len );
    count = value_list_split( impl, buffer, len, index, &start, &item_len );
    if (index >= count) hr = E_BOUNDS;
    else hr = header_value_create( impl->kind == LIST_CONNECTION ? VALUE_CONNECTION : VALUE_PRODUCT_INFO,
                                   start, item_len, &item );
    WindowsDeleteString( text );
    if (FAILED(hr)) return hr;
    *value = header_value_outer( item );
    return S_OK;
}

static HRESULT value_list_append( struct value_list *impl, IInspectable *value )
{
    IStringable *stringable;
    HSTRING text;
    HRESULT hr;

    if (!value) return E_INVALIDARG;
    if (FAILED(hr = IInspectable_QueryInterface( value, &IID_IStringable, (void **)&stringable ))) return hr;
    hr = IStringable_ToString( stringable, &text );
    IStringable_Release( stringable );
    if (FAILED(hr)) return hr;
    hr = value_list_add( impl, text );
    WindowsDeleteString( text );
    return hr;
}

static HRESULT value_list_clear( struct value_list *impl )
{
    struct header_collection *owner = impl->owner;
    const WCHAR *name = value_list_header( impl );
    struct header *header;

    AcquireSRWLockExclusive( &owner->lock );
    if ((header = find_header( owner, name, wcslen( name ) ))) remove_locked( owner, header );
    ReleaseSRWLockExclusive( &owner->lock );
    return S_OK;
}

#define DEFINE_VALUE_LIST_VECTOR( pfx, iface_type, elem_type, view_type )                          \
    DEFINE_IINSPECTABLE_( pfx, iface_type, struct value_list, impl_from_##iface_type, iface_type##_iface, \
                          value_list_outer( impl ) )                                               \
    static HRESULT WINAPI pfx##_GetAt( iface_type *iface, UINT32 index, elem_type **value )        \
    {                                                                                              \
        return value_list_get_at( impl_from_##iface_type( iface ), index, (IInspectable **)value );\
    }                                                                                              \
    static HRESULT WINAPI pfx##_get_Size( iface_type *iface, UINT32 *size )                        \
    {                                                                                              \
        return value_list_size( impl_from_##iface_type( iface ), size );                           \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetView( iface_type *iface, view_type **view )                     \
    {                                                                                              \
        FIXME( "iface %p, view %p stub!\n", iface, view );                                         \
        return E_NOTIMPL;                                                                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_IndexOf( iface_type *iface, elem_type *value, UINT32 *index, boolean *found ) \
    {                                                                                              \
        FIXME( "iface %p, value %p stub!\n", iface, value );                                       \
        return E_NOTIMPL;                                                                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_SetAt( iface_type *iface, UINT32 index, elem_type *value )         \
    {                                                                                              \
        FIXME( "iface %p, index %u stub!\n", iface, index );                                       \
        return E_NOTIMPL;                                                                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_InsertAt( iface_type *iface, UINT32 index, elem_type *value )      \
    {                                                                                              \
        FIXME( "iface %p, index %u stub!\n", iface, index );                                       \
        return E_NOTIMPL;                                                                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_RemoveAt( iface_type *iface, UINT32 index )                        \
    {                                                                                              \
        FIXME( "iface %p, index %u stub!\n", iface, index );                                       \
        return E_NOTIMPL;                                                                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_Append( iface_type *iface, elem_type *value )                      \
    {                                                                                              \
        return value_list_append( impl_from_##iface_type( iface ), (IInspectable *)value );        \
    }                                                                                              \
    static HRESULT WINAPI pfx##_RemoveAtEnd( iface_type *iface )                                   \
    {                                                                                              \
        FIXME( "iface %p stub!\n", iface );                                                        \
        return E_NOTIMPL;                                                                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_Clear( iface_type *iface )                                         \
    {                                                                                              \
        return value_list_clear( impl_from_##iface_type( iface ) );                                \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetMany( iface_type *iface, UINT32 start, UINT32 items_size,       \
                                         elem_type **items, UINT32 *count )                        \
    {                                                                                              \
        FIXME( "iface %p, start %u stub!\n", iface, start );                                       \
        return E_NOTIMPL;                                                                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_ReplaceAll( iface_type *iface, UINT32 count, elem_type **items )   \
    {                                                                                              \
        FIXME( "iface %p, count %u stub!\n", iface, count );                                       \
        return E_NOTIMPL;                                                                          \
    }                                                                                              \
    static const struct iface_type##Vtbl pfx##_vtbl =                                             \
    {                                                                                              \
        pfx##_QueryInterface,                                                                      \
        pfx##_AddRef,                                                                              \
        pfx##_Release,                                                                             \
        pfx##_GetIids,                                                                             \
        pfx##_GetRuntimeClassName,                                                                 \
        pfx##_GetTrustLevel,                                                                       \
        pfx##_GetAt,                                                                               \
        pfx##_get_Size,                                                                            \
        pfx##_GetView,                                                                             \
        pfx##_IndexOf,                                                                             \
        pfx##_SetAt,                                                                               \
        pfx##_InsertAt,                                                                            \
        pfx##_RemoveAt,                                                                            \
        pfx##_Append,                                                                              \
        pfx##_RemoveAtEnd,                                                                         \
        pfx##_Clear,                                                                               \
        pfx##_GetMany,                                                                             \
        pfx##_ReplaceAll,                                                                          \
    };

/* TODO: GetView, IndexOf, SetAt, InsertAt, RemoveAt, RemoveAtEnd, GetMany and ReplaceAll of the typed header value collections */
DEFINE_VALUE_LIST_VECTOR( connection_vector, IVector_HttpConnectionOptionHeaderValue, IHttpConnectionOptionHeaderValue,
                          IVectorView_HttpConnectionOptionHeaderValue )
DEFINE_VALUE_LIST_VECTOR( product_vector, IVector_HttpProductInfoHeaderValue, IHttpProductInfoHeaderValue,
                          IVectorView_HttpProductInfoHeaderValue )

DEFINE_IINSPECTABLE_( list_stringable, IStringable, struct value_list, impl_from_list_IStringable, IStringable_iface,
                      value_list_outer( impl ) )

static HRESULT WINAPI list_stringable_ToString( IStringable *iface, HSTRING *value )
{
    struct value_list *impl = impl_from_list_IStringable( iface );

    if (!value) return E_POINTER;
    if (header_collection_lookup( impl->owner, value_list_header( impl ), value ) != S_OK) *value = NULL;
    return S_OK;
}

static const struct IStringableVtbl list_stringable_vtbl =
{
    list_stringable_QueryInterface,
    list_stringable_AddRef,
    list_stringable_Release,
    list_stringable_GetIids,
    list_stringable_GetRuntimeClassName,
    list_stringable_GetTrustLevel,
    list_stringable_ToString,
};

static HRESULT value_list_create( struct header_collection *owner, enum value_list_kind kind, void **out )
{
    struct value_list *impl;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHttpConnectionOptionHeaderValueCollection_iface.lpVtbl = &connection_list_vtbl;
    impl->IHttpProductInfoHeaderValueCollection_iface.lpVtbl = &product_list_vtbl;
    impl->IVector_HttpConnectionOptionHeaderValue_iface.lpVtbl = &connection_vector_vtbl;
    impl->IVector_HttpProductInfoHeaderValue_iface.lpVtbl = &product_vector_vtbl;
    impl->IStringable_iface.lpVtbl = &list_stringable_vtbl;
    impl->ref = 1;
    impl->kind = kind;
    impl->owner = owner;
    header_collection_addref( owner );
    *out = value_list_outer( impl );
    return S_OK;
}

struct date_or_delta
{
    IHttpDateOrDeltaHeaderValue IHttpDateOrDeltaHeaderValue_iface;
    IStringable IStringable_iface;
    LONG ref;
    BOOL is_date;
    DateTime date;
    TimeSpan delta;
};

static inline struct date_or_delta *impl_from_IHttpDateOrDeltaHeaderValue( IHttpDateOrDeltaHeaderValue *iface )
{
    return CONTAINING_RECORD( iface, struct date_or_delta, IHttpDateOrDeltaHeaderValue_iface );
}

static HRESULT WINAPI date_or_delta_QueryInterface( IHttpDateOrDeltaHeaderValue *iface, REFIID iid, void **out )
{
    struct date_or_delta *impl = impl_from_IHttpDateOrDeltaHeaderValue( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IHttpDateOrDeltaHeaderValue ))
        *out = &impl->IHttpDateOrDeltaHeaderValue_iface;
    else if (IsEqualGUID( iid, &IID_IStringable ))
        *out = &impl->IStringable_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI date_or_delta_AddRef( IHttpDateOrDeltaHeaderValue *iface )
{
    return InterlockedIncrement( &impl_from_IHttpDateOrDeltaHeaderValue( iface )->ref );
}

static ULONG WINAPI date_or_delta_Release( IHttpDateOrDeltaHeaderValue *iface )
{
    struct date_or_delta *impl = impl_from_IHttpDateOrDeltaHeaderValue( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI date_or_delta_GetIids( IHttpDateOrDeltaHeaderValue *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI date_or_delta_GetRuntimeClassName( IHttpDateOrDeltaHeaderValue *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_Headers_HttpDateOrDeltaHeaderValue;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI date_or_delta_GetTrustLevel( IHttpDateOrDeltaHeaderValue *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI date_or_delta_get_Date( IHttpDateOrDeltaHeaderValue *iface, IReference_DateTime **value )
{
    struct date_or_delta *impl = impl_from_IHttpDateOrDeltaHeaderValue( iface );
    IInspectable *boxed;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->is_date) return S_OK;
    if (FAILED(hr = box_date_time( impl->date, &boxed ))) return hr;
    hr = IInspectable_QueryInterface( boxed, &IID_IReference_DateTime, (void **)value );
    IInspectable_Release( boxed );
    return hr;
}

static HRESULT WINAPI date_or_delta_get_Delta( IHttpDateOrDeltaHeaderValue *iface, IReference_TimeSpan **value )
{
    struct date_or_delta *impl = impl_from_IHttpDateOrDeltaHeaderValue( iface );
    IInspectable *boxed;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    if (impl->is_date) return S_OK;
    if (FAILED(hr = box_time_span( impl->delta, &boxed ))) return hr;
    hr = IInspectable_QueryInterface( boxed, &IID_IReference_TimeSpan, (void **)value );
    IInspectable_Release( boxed );
    return hr;
}

static const struct IHttpDateOrDeltaHeaderValueVtbl date_or_delta_vtbl =
{
    date_or_delta_QueryInterface,
    date_or_delta_AddRef,
    date_or_delta_Release,
    date_or_delta_GetIids,
    date_or_delta_GetRuntimeClassName,
    date_or_delta_GetTrustLevel,
    date_or_delta_get_Date,
    date_or_delta_get_Delta,
};

DEFINE_IINSPECTABLE_( date_or_delta_stringable, IStringable, struct date_or_delta, impl_from_date_IStringable,
                      IStringable_iface, &impl->IHttpDateOrDeltaHeaderValue_iface )

static HRESULT WINAPI date_or_delta_stringable_ToString( IStringable *iface, HSTRING *value )
{
    struct date_or_delta *impl = impl_from_date_IStringable( iface );
    WCHAR text[WINHTTP_TIME_FORMAT_BUFSIZE / sizeof(WCHAR) + 1];
    ULARGE_INTEGER time;
    SYSTEMTIME st;
    FILETIME ft;

    if (!value) return E_POINTER;
    if (!impl->is_date)
    {
        swprintf( text, ARRAY_SIZE(text), L"%I64d", impl->delta.Duration / 10000000 );
        return WindowsCreateString( text, wcslen( text ), value );
    }
    time.QuadPart = impl->date.UniversalTime;
    ft.dwLowDateTime = time.LowPart;
    ft.dwHighDateTime = time.HighPart;
    if (!FileTimeToSystemTime( &ft, &st ) || !WinHttpTimeFromSystemTime( &st, text ))
        return HRESULT_FROM_WIN32( GetLastError() );
    return WindowsCreateString( text, wcslen( text ), value );
}

static const struct IStringableVtbl date_or_delta_stringable_vtbl =
{
    date_or_delta_stringable_QueryInterface,
    date_or_delta_stringable_AddRef,
    date_or_delta_stringable_Release,
    date_or_delta_stringable_GetIids,
    date_or_delta_stringable_GetRuntimeClassName,
    date_or_delta_stringable_GetTrustLevel,
    date_or_delta_stringable_ToString,
};

static const WCHAR *const month_names[] =
{
    L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec",
};

static const WCHAR *const day_names[] =
{
    L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat",
};

static const WCHAR *const long_day_names[] =
{
    L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday",
};

struct date_cursor
{
    const WCHAR *text;
    UINT32 len;
    UINT32 pos;
};

static BOOL date_literal( struct date_cursor *cursor, const WCHAR *literal )
{
    UINT32 len = wcslen( literal );

    if (cursor->len - cursor->pos < len || wcsncmp( cursor->text + cursor->pos, literal, len )) return FALSE;
    cursor->pos += len;
    return TRUE;
}

static BOOL date_number( struct date_cursor *cursor, UINT32 digits, WORD *value )
{
    UINT32 i;

    if (cursor->len - cursor->pos < digits) return FALSE;
    *value = 0;
    for (i = 0; i < digits; i++)
    {
        WCHAR c = cursor->text[cursor->pos + i];

        if (c < '0' || c > '9') return FALSE;
        *value = *value * 10 + (c - '0');
    }
    cursor->pos += digits;
    return TRUE;
}

static BOOL date_name( struct date_cursor *cursor, const WCHAR *const *names, UINT32 count, WORD *index )
{
    for (*index = 0; *index < count; (*index)++)
        if (date_literal( cursor, names[*index] )) return TRUE;
    return FALSE;
}

static BOOL date_time_of_day( struct date_cursor *cursor, SYSTEMTIME *st )
{
    return date_number( cursor, 2, &st->wHour ) && date_literal( cursor, L":" ) &&
           date_number( cursor, 2, &st->wMinute ) && date_literal( cursor, L":" ) &&
           date_number( cursor, 2, &st->wSecond ) && st->wHour < 24 && st->wMinute < 60 && st->wSecond < 61;
}

static BOOL parse_http_date( const WCHAR *text, UINT32 len, SYSTEMTIME *st )
{
    struct date_cursor cursor = {text, len, 0};
    WORD day_of_week, month, year;

    memset( st, 0, sizeof(*st) );
    if (date_name( &cursor, long_day_names, ARRAY_SIZE(long_day_names), &day_of_week ))
    {
        if (!date_literal( &cursor, L", " ) || !date_number( &cursor, 2, &st->wDay ) || !date_literal( &cursor, L"-" ) ||
            !date_name( &cursor, month_names, ARRAY_SIZE(month_names), &month ) || !date_literal( &cursor, L"-" ) ||
            !date_number( &cursor, 2, &year ) || !date_literal( &cursor, L" " ) || !date_time_of_day( &cursor, st ) ||
            !date_literal( &cursor, L" GMT" ))
            return FALSE;
        st->wYear = year < 70 ? 2000 + year : 1900 + year;
    }
    else if (date_name( &cursor, day_names, ARRAY_SIZE(day_names), &day_of_week ))
    {
        if (date_literal( &cursor, L", " ))
        {
            if (!date_number( &cursor, 2, &st->wDay ) || !date_literal( &cursor, L" " ) ||
                !date_name( &cursor, month_names, ARRAY_SIZE(month_names), &month ) || !date_literal( &cursor, L" " ) ||
                !date_number( &cursor, 4, &st->wYear ) || !date_literal( &cursor, L" " ) ||
                !date_time_of_day( &cursor, st ) || !date_literal( &cursor, L" GMT" ))
                return FALSE;
        }
        else
        {
            if (!date_literal( &cursor, L" " ) || !date_name( &cursor, month_names, ARRAY_SIZE(month_names), &month ) ||
                !date_literal( &cursor, L" " ))
                return FALSE;
            if (date_literal( &cursor, L" " ))
            {
                if (!date_number( &cursor, 1, &st->wDay )) return FALSE;
            }
            else if (!date_number( &cursor, 2, &st->wDay )) return FALSE;
            if (!date_literal( &cursor, L" " ) || !date_time_of_day( &cursor, st ) || !date_literal( &cursor, L" " ) ||
                !date_number( &cursor, 4, &st->wYear ))
                return FALSE;
        }
    }
    else return FALSE;

    if (cursor.pos != cursor.len || !st->wDay || st->wDay > 31) return FALSE;
    st->wMonth = month + 1;
    st->wDayOfWeek = day_of_week;
    return TRUE;
}

HRESULT date_or_delta_parse( HSTRING input, IHttpDateOrDeltaHeaderValue **value )
{
    struct date_or_delta *impl;
    UINT32 len;
    const WCHAR *text = hstring_buffer( input, &len );
    UINT64 seconds;
    SYSTEMTIME st;
    FILETIME ft;

    *value = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHttpDateOrDeltaHeaderValue_iface.lpVtbl = &date_or_delta_vtbl;
    impl->IStringable_iface.lpVtbl = &date_or_delta_stringable_vtbl;
    impl->ref = 1;

    if (parse_uint64( text, len, &seconds ) && seconds <= MAXINT64 / 10000000)
        impl->delta.Duration = seconds * 10000000;
    else
    {
        trim( &text, &len );
        if (!parse_http_date( text, len, &st ) || !SystemTimeToFileTime( &st, &ft ))
        {
            free( impl );
            return S_FALSE;
        }
        impl->is_date = TRUE;
        impl->date.UniversalTime = ((UINT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    }
    *value = &impl->IHttpDateOrDeltaHeaderValue_iface;
    return S_OK;
}

struct date_or_delta_statics
{
    IActivationFactory IActivationFactory_iface;
    IHttpDateOrDeltaHeaderValueStatics IHttpDateOrDeltaHeaderValueStatics_iface;
    LONG ref;
};

static inline struct date_or_delta_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct date_or_delta_statics, IActivationFactory_iface );
}

static HRESULT WINAPI date_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct date_or_delta_statics *impl = impl_from_IActivationFactory( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IHttpDateOrDeltaHeaderValueStatics ))
        *out = &impl->IHttpDateOrDeltaHeaderValueStatics_iface;
    else
    {
        *out = NULL;
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI date_factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_IActivationFactory( iface )->ref );
}

static ULONG WINAPI date_factory_Release( IActivationFactory *iface )
{
    ULONG ref = InterlockedDecrement( &impl_from_IActivationFactory( iface )->ref );

    assert( ref );
    return ref;
}

static HRESULT WINAPI date_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI date_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    const WCHAR *name = RuntimeClass_Windows_Web_Http_Headers_HttpDateOrDeltaHeaderValue;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI date_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI date_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (instance) *instance = NULL;
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl date_factory_vtbl =
{
    date_factory_QueryInterface,
    date_factory_AddRef,
    date_factory_Release,
    date_factory_GetIids,
    date_factory_GetRuntimeClassName,
    date_factory_GetTrustLevel,
    date_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( date_statics, IHttpDateOrDeltaHeaderValueStatics, struct date_or_delta_statics, IActivationFactory_iface )

static HRESULT WINAPI date_statics_Parse( IHttpDateOrDeltaHeaderValueStatics *iface, HSTRING input,
                                          IHttpDateOrDeltaHeaderValue **result )
{
    HRESULT hr;

    TRACE( "iface %p, input %s, result %p.\n", iface, debugstr_hstring( input ), result );

    if (!result) return E_POINTER;
    hr = date_or_delta_parse( input, result );
    return hr == S_FALSE ? E_INVALIDARG : hr;
}

static HRESULT WINAPI date_statics_TryParse( IHttpDateOrDeltaHeaderValueStatics *iface, HSTRING input,
                                             IHttpDateOrDeltaHeaderValue **value, boolean *succeeded )
{
    HRESULT hr;

    TRACE( "iface %p, input %s, value %p, succeeded %p.\n", iface, debugstr_hstring( input ), value, succeeded );

    if (!value || !succeeded) return E_POINTER;
    hr = date_or_delta_parse( input, value );
    *succeeded = hr == S_OK;
    return FAILED(hr) ? hr : S_OK;
}

static const struct IHttpDateOrDeltaHeaderValueStaticsVtbl date_statics_vtbl =
{
    date_statics_QueryInterface,
    date_statics_AddRef,
    date_statics_Release,
    date_statics_GetIids,
    date_statics_GetRuntimeClassName,
    date_statics_GetTrustLevel,
    date_statics_Parse,
    date_statics_TryParse,
};

static struct date_or_delta_statics date_or_delta_statics =
{
    {&date_factory_vtbl},
    {&date_statics_vtbl},
    1,
};

IActivationFactory *http_date_or_delta_factory = &date_or_delta_statics.IActivationFactory_iface;
