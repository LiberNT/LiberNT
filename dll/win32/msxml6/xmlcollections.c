/*
 * PROJECT:     LiberNT MSXML 6.0
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows.Data.Xml.Dom node lists and named node maps over MSXML
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "xmldom_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(msxml);

struct xml_collection
{
    IXmlNodeList IXmlNodeList_iface;
    IXmlNamedNodeMap IXmlNamedNodeMap_iface;
    IVectorView_IXmlNode IVectorView_IXmlNode_iface;
    IIterable_IXmlNode IIterable_IXmlNode_iface;
    LONG ref;

    IXMLDOMNodeList *list;
    IXMLDOMNamedNodeMap *map;
};

static HRESULT collection_length( struct xml_collection *impl, UINT32 *value )
{
    LONG length = 0;
    HRESULT hr;

    if (impl->list) hr = IXMLDOMNodeList_get_length( impl->list, &length );
    else hr = IXMLDOMNamedNodeMap_get_length( impl->map, &length );
    *value = length;
    return hr;
}

static HRESULT collection_item( struct xml_collection *impl, UINT32 index, IXmlNode **node )
{
    IXMLDOMNode *item = NULL;
    HRESULT hr;

    *node = NULL;
    if (impl->list) hr = IXMLDOMNodeList_get_item( impl->list, index, &item );
    else hr = IXMLDOMNamedNodeMap_get_item( impl->map, index, &item );
    if (FAILED(hr) || !item) return FAILED(hr) ? hr : S_OK;
    hr = xml_node_create( item, node );
    IXMLDOMNode_Release( item );
    return hr;
}

static inline struct xml_collection *impl_from_IVectorView_IXmlNode( IVectorView_IXmlNode *iface )
{
    return CONTAINING_RECORD( iface, struct xml_collection, IVectorView_IXmlNode_iface );
}

static IInspectable *collection_default( struct xml_collection *impl )
{
    return impl->list ? (IInspectable *)&impl->IXmlNodeList_iface : (IInspectable *)&impl->IXmlNamedNodeMap_iface;
}

static HRESULT WINAPI vector_view_QueryInterface( IVectorView_IXmlNode *iface, REFIID iid, void **out )
{
    struct xml_collection *impl = impl_from_IVectorView_IXmlNode( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
        *out = collection_default( impl );
    else if (impl->list && IsEqualGUID( iid, &IID_IXmlNodeList ))
        *out = &impl->IXmlNodeList_iface;
    else if (impl->map && IsEqualGUID( iid, &IID_IXmlNamedNodeMap ))
        *out = &impl->IXmlNamedNodeMap_iface;
    else if (IsEqualGUID( iid, &IID_IVectorView_IXmlNode ))
        *out = &impl->IVectorView_IXmlNode_iface;
    else if (IsEqualGUID( iid, &IID_IIterable_IXmlNode ))
        *out = &impl->IIterable_IXmlNode_iface;
    else
    {
        *out = NULL;
        WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid) );
        return E_NOINTERFACE;
    }

    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI vector_view_AddRef( IVectorView_IXmlNode *iface )
{
    struct xml_collection *impl = impl_from_IVectorView_IXmlNode( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI vector_view_Release( IVectorView_IXmlNode *iface )
{
    struct xml_collection *impl = impl_from_IVectorView_IXmlNode( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->list) IXMLDOMNodeList_Release( impl->list );
        if (impl->map) IXMLDOMNamedNodeMap_Release( impl->map );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI vector_view_GetIids( IVectorView_IXmlNode *iface, ULONG *iid_count, IID **iids )
{
    struct xml_collection *impl = impl_from_IVectorView_IXmlNode( iface );

    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (!(*iids = CoTaskMemAlloc( 3 * sizeof(IID) ))) return E_OUTOFMEMORY;
    (*iids)[0] = impl->list ? IID_IXmlNodeList : IID_IXmlNamedNodeMap;
    (*iids)[1] = IID_IIterable_IXmlNode;
    (*iids)[2] = IID_IVectorView_IXmlNode;
    *iid_count = 3;
    return S_OK;
}

static HRESULT WINAPI vector_view_GetRuntimeClassName( IVectorView_IXmlNode *iface, HSTRING *class_name )
{
    struct xml_collection *impl = impl_from_IVectorView_IXmlNode( iface );
    const WCHAR *name = impl->list ? RuntimeClass_Windows_Data_Xml_Dom_XmlNodeList
                                   : RuntimeClass_Windows_Data_Xml_Dom_XmlNamedNodeMap;

    TRACE( "iface %p, class_name %p.\n", iface, class_name );

    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI vector_view_GetTrustLevel( IVectorView_IXmlNode *iface, TrustLevel *trust_level )
{
    TRACE( "iface %p, trust_level %p.\n", iface, trust_level );
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI vector_view_GetAt( IVectorView_IXmlNode *iface, UINT32 index, IXmlNode **value )
{
    struct xml_collection *impl = impl_from_IVectorView_IXmlNode( iface );
    UINT32 size;
    HRESULT hr;

    TRACE( "iface %p, index %u, value %p.\n", iface, index, value );

    *value = NULL;
    if (FAILED(hr = collection_length( impl, &size ))) return hr;
    if (index >= size) return E_BOUNDS;
    return collection_item( impl, index, value );
}

static HRESULT WINAPI vector_view_get_Size( IVectorView_IXmlNode *iface, UINT32 *value )
{
    struct xml_collection *impl = impl_from_IVectorView_IXmlNode( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return collection_length( impl, value );
}

static HRESULT WINAPI vector_view_IndexOf( IVectorView_IXmlNode *iface, IXmlNode *element, UINT32 *index, BOOLEAN *found )
{
    struct xml_collection *impl = impl_from_IVectorView_IXmlNode( iface );
    IXMLDOMNode *target, *item;
    IUnknown *target_unk, *item_unk;
    UINT32 size, i;
    HRESULT hr;

    TRACE( "iface %p, element %p, index %p, found %p.\n", iface, element, index, found );

    *index = 0;
    *found = FALSE;
    if (FAILED(hr = collection_length( impl, &size ))) return hr;
    if (FAILED(xml_node_unwrap( element, &target ))) return S_OK;
    IXMLDOMNode_QueryInterface( target, &IID_IUnknown, (void **)&target_unk );
    for (i = 0; i < size && !*found; i++)
    {
        item = NULL;
        if (impl->list) hr = IXMLDOMNodeList_get_item( impl->list, i, &item );
        else hr = IXMLDOMNamedNodeMap_get_item( impl->map, i, &item );
        if (FAILED(hr) || !item) continue;
        IXMLDOMNode_QueryInterface( item, &IID_IUnknown, (void **)&item_unk );
        if (item_unk == target_unk)
        {
            *found = TRUE;
            *index = i;
        }
        IUnknown_Release( item_unk );
        IXMLDOMNode_Release( item );
    }
    IUnknown_Release( target_unk );
    IXMLDOMNode_Release( target );
    return S_OK;
}

static HRESULT WINAPI vector_view_GetMany( IVectorView_IXmlNode *iface, UINT32 start_index, UINT32 items_size,
                                           IXmlNode **items, UINT32 *count )
{
    struct xml_collection *impl = impl_from_IVectorView_IXmlNode( iface );
    UINT32 size, i;
    HRESULT hr;

    TRACE( "iface %p, start_index %u, items_size %u, items %p, count %p.\n", iface, start_index, items_size, items, count );

    *count = 0;
    if (FAILED(hr = collection_length( impl, &size ))) return hr;
    if (start_index > size) return E_BOUNDS;
    for (i = start_index; i < size && *count < items_size; i++)
    {
        if (FAILED(hr = collection_item( impl, i, &items[*count] )))
        {
            while (*count) IXmlNode_Release( items[--*count] );
            return hr;
        }
        (*count)++;
    }
    return S_OK;
}

static const IVectorView_IXmlNodeVtbl vector_view_vtbl =
{
    vector_view_QueryInterface,
    vector_view_AddRef,
    vector_view_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    vector_view_GetAt,
    vector_view_get_Size,
    vector_view_IndexOf,
    vector_view_GetMany,
};

DEFINE_IINSPECTABLE( node_list, IXmlNodeList, struct xml_collection, IVectorView_IXmlNode_iface )

static HRESULT WINAPI node_list_get_Length( IXmlNodeList *iface, UINT32 *value )
{
    struct xml_collection *impl = impl_from_IXmlNodeList( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return collection_length( impl, value );
}

static HRESULT WINAPI node_list_Item( IXmlNodeList *iface, UINT32 index, IXmlNode **node )
{
    struct xml_collection *impl = impl_from_IXmlNodeList( iface );
    TRACE( "iface %p, index %u, node %p.\n", iface, index, node );
    return collection_item( impl, index, node );
}

static const IXmlNodeListVtbl node_list_vtbl =
{
    node_list_QueryInterface,
    node_list_AddRef,
    node_list_Release,
    node_list_GetIids,
    node_list_GetRuntimeClassName,
    node_list_GetTrustLevel,
    node_list_get_Length,
    node_list_Item,
};

DEFINE_IINSPECTABLE( named_node_map, IXmlNamedNodeMap, struct xml_collection, IVectorView_IXmlNode_iface )

static HRESULT WINAPI named_node_map_get_Length( IXmlNamedNodeMap *iface, UINT32 *value )
{
    struct xml_collection *impl = impl_from_IXmlNamedNodeMap( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return collection_length( impl, value );
}

static HRESULT WINAPI named_node_map_Item( IXmlNamedNodeMap *iface, UINT32 index, IXmlNode **node )
{
    struct xml_collection *impl = impl_from_IXmlNamedNodeMap( iface );
    TRACE( "iface %p, index %u, node %p.\n", iface, index, node );
    return collection_item( impl, index, node );
}

static HRESULT wrap_result( IXMLDOMNode *node, HRESULT hr, IXmlNode **out )
{
    *out = NULL;
    if (hr == S_FALSE) hr = S_OK;
    if (FAILED(hr) || !node) return hr;
    hr = xml_node_create( node, out );
    IXMLDOMNode_Release( node );
    return hr;
}

static HRESULT WINAPI named_node_map_GetNamedItem( IXmlNamedNodeMap *iface, HSTRING name, IXmlNode **node )
{
    struct xml_collection *impl = impl_from_IXmlNamedNodeMap( iface );
    IXMLDOMNode *item = NULL;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, name %s, node %p.\n", iface, debugstr_hstring(name), node );

    if (!(str = hstring_to_bstr( name ))) return E_OUTOFMEMORY;
    hr = IXMLDOMNamedNodeMap_getNamedItem( impl->map, str, &item );
    SysFreeString( str );
    return wrap_result( item, hr, node );
}

static HRESULT WINAPI named_node_map_SetNamedItem( IXmlNamedNodeMap *iface, IXmlNode *node, IXmlNode **previous_node )
{
    struct xml_collection *impl = impl_from_IXmlNamedNodeMap( iface );
    IXMLDOMNode *item, *previous = NULL;
    HRESULT hr;

    TRACE( "iface %p, node %p, previous_node %p.\n", iface, node, previous_node );

    *previous_node = NULL;
    if (FAILED(hr = xml_node_unwrap( node, &item ))) return hr;
    hr = IXMLDOMNamedNodeMap_setNamedItem( impl->map, item, &previous );
    IXMLDOMNode_Release( item );
    return wrap_result( previous, hr, previous_node );
}

static HRESULT WINAPI named_node_map_RemoveNamedItem( IXmlNamedNodeMap *iface, HSTRING name, IXmlNode **previous_node )
{
    struct xml_collection *impl = impl_from_IXmlNamedNodeMap( iface );
    IXMLDOMNode *previous = NULL;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, name %s, previous_node %p.\n", iface, debugstr_hstring(name), previous_node );

    if (!(str = hstring_to_bstr( name ))) return E_OUTOFMEMORY;
    hr = IXMLDOMNamedNodeMap_removeNamedItem( impl->map, str, &previous );
    SysFreeString( str );
    return wrap_result( previous, hr, previous_node );
}

static HRESULT WINAPI named_node_map_GetNamedItemNS( IXmlNamedNodeMap *iface, IInspectable *namespace_uri, HSTRING name,
                                                     IXmlNode **node )
{
    struct xml_collection *impl = impl_from_IXmlNamedNodeMap( iface );
    IXMLDOMNode *item = NULL;
    BSTR ns, str;
    HRESULT hr;

    TRACE( "iface %p, namespace_uri %p, name %s, node %p.\n", iface, namespace_uri, debugstr_hstring(name), node );

    *node = NULL;
    if (FAILED(hr = unbox_bstr( namespace_uri, &ns ))) return hr;
    if ((str = hstring_to_bstr( name ))) hr = IXMLDOMNamedNodeMap_getQualifiedItem( impl->map, str, ns ? ns : (BSTR)L"", &item );
    else hr = E_OUTOFMEMORY;
    SysFreeString( str );
    SysFreeString( ns );
    return wrap_result( item, hr, node );
}

static HRESULT WINAPI named_node_map_RemoveNamedItemNS( IXmlNamedNodeMap *iface, IInspectable *namespace_uri, HSTRING name,
                                                        IXmlNode **previous_node )
{
    struct xml_collection *impl = impl_from_IXmlNamedNodeMap( iface );
    IXMLDOMNode *previous = NULL;
    BSTR ns, str;
    HRESULT hr;

    TRACE( "iface %p, namespace_uri %p, name %s, previous_node %p.\n", iface, namespace_uri, debugstr_hstring(name), previous_node );

    *previous_node = NULL;
    if (FAILED(hr = unbox_bstr( namespace_uri, &ns ))) return hr;
    if ((str = hstring_to_bstr( name ))) hr = IXMLDOMNamedNodeMap_removeQualifiedItem( impl->map, str, ns ? ns : (BSTR)L"", &previous );
    else hr = E_OUTOFMEMORY;
    SysFreeString( str );
    SysFreeString( ns );
    return wrap_result( previous, hr, previous_node );
}

static HRESULT WINAPI named_node_map_SetNamedItemNS( IXmlNamedNodeMap *iface, IXmlNode *node, IXmlNode **previous_node )
{
    TRACE( "iface %p, node %p, previous_node %p.\n", iface, node, previous_node );
    return named_node_map_SetNamedItem( iface, node, previous_node );
}

static const IXmlNamedNodeMapVtbl named_node_map_vtbl =
{
    named_node_map_QueryInterface,
    named_node_map_AddRef,
    named_node_map_Release,
    named_node_map_GetIids,
    named_node_map_GetRuntimeClassName,
    named_node_map_GetTrustLevel,
    named_node_map_get_Length,
    named_node_map_Item,
    named_node_map_GetNamedItem,
    named_node_map_SetNamedItem,
    named_node_map_RemoveNamedItem,
    named_node_map_GetNamedItemNS,
    named_node_map_RemoveNamedItemNS,
    named_node_map_SetNamedItemNS,
};

struct node_iterator
{
    IIterator_IXmlNode IIterator_IXmlNode_iface;
    LONG ref;

    IVectorView_IXmlNode *view;
    UINT32 index;
    UINT32 size;
};

static inline struct node_iterator *impl_from_IIterator_IXmlNode( IIterator_IXmlNode *iface )
{
    return CONTAINING_RECORD( iface, struct node_iterator, IIterator_IXmlNode_iface );
}

static HRESULT WINAPI iterator_QueryInterface( IIterator_IXmlNode *iface, REFIID iid, void **out )
{
    struct node_iterator *impl = impl_from_IIterator_IXmlNode( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IIterator_IXmlNode ))
    {
        IInspectable_AddRef( (*out = &impl->IIterator_IXmlNode_iface) );
        return S_OK;
    }

    *out = NULL;
    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid) );
    return E_NOINTERFACE;
}

static ULONG WINAPI iterator_AddRef( IIterator_IXmlNode *iface )
{
    struct node_iterator *impl = impl_from_IIterator_IXmlNode( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI iterator_Release( IIterator_IXmlNode *iface )
{
    struct node_iterator *impl = impl_from_IIterator_IXmlNode( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        IVectorView_IXmlNode_Release( impl->view );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI iterator_GetIids( IIterator_IXmlNode *iface, ULONG *iid_count, IID **iids )
{
    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (!(*iids = CoTaskMemAlloc( sizeof(IID) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IIterator_IXmlNode;
    *iid_count = 1;
    return S_OK;
}

static HRESULT WINAPI iterator_GetRuntimeClassName( IIterator_IXmlNode *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IIterator`1<Windows.Data.Xml.Dom.IXmlNode>";
    TRACE( "iface %p, class_name %p.\n", iface, class_name );
    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, class_name );
}

static HRESULT WINAPI iterator_GetTrustLevel( IIterator_IXmlNode *iface, TrustLevel *trust_level )
{
    TRACE( "iface %p, trust_level %p.\n", iface, trust_level );
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI iterator_get_Current( IIterator_IXmlNode *iface, IXmlNode **value )
{
    struct node_iterator *impl = impl_from_IIterator_IXmlNode( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return IVectorView_IXmlNode_GetAt( impl->view, impl->index, value );
}

static HRESULT WINAPI iterator_get_HasCurrent( IIterator_IXmlNode *iface, BOOLEAN *value )
{
    struct node_iterator *impl = impl_from_IIterator_IXmlNode( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    *value = impl->index < impl->size;
    return S_OK;
}

static HRESULT WINAPI iterator_MoveNext( IIterator_IXmlNode *iface, BOOLEAN *value )
{
    struct node_iterator *impl = impl_from_IIterator_IXmlNode( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (impl->index < impl->size) impl->index++;
    *value = impl->index < impl->size;
    return S_OK;
}

static HRESULT WINAPI iterator_GetMany( IIterator_IXmlNode *iface, UINT32 items_size, IXmlNode **items, UINT32 *count )
{
    struct node_iterator *impl = impl_from_IIterator_IXmlNode( iface );
    HRESULT hr;

    TRACE( "iface %p, items_size %u, items %p, count %p.\n", iface, items_size, items, count );

    if (SUCCEEDED(hr = IVectorView_IXmlNode_GetMany( impl->view, impl->index, items_size, items, count )))
        impl->index += *count;
    return hr;
}

static const IIterator_IXmlNodeVtbl iterator_vtbl =
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

DEFINE_IINSPECTABLE( iterable, IIterable_IXmlNode, struct xml_collection, IVectorView_IXmlNode_iface )

static HRESULT WINAPI iterable_First( IIterable_IXmlNode *iface, IIterator_IXmlNode **value )
{
    struct xml_collection *impl = impl_from_IIterable_IXmlNode( iface );
    struct node_iterator *iter;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;
    iter->IIterator_IXmlNode_iface.lpVtbl = &iterator_vtbl;
    iter->ref = 1;
    if (FAILED(hr = collection_length( impl, &iter->size )))
    {
        free( iter );
        return hr;
    }
    IVectorView_IXmlNode_AddRef( (iter->view = &impl->IVectorView_IXmlNode_iface) );
    *value = &iter->IIterator_IXmlNode_iface;
    return S_OK;
}

static const IIterable_IXmlNodeVtbl iterable_vtbl =
{
    iterable_QueryInterface,
    iterable_AddRef,
    iterable_Release,
    iterable_GetIids,
    iterable_GetRuntimeClassName,
    iterable_GetTrustLevel,
    iterable_First,
};

static struct xml_collection *collection_alloc( void )
{
    struct xml_collection *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IXmlNodeList_iface.lpVtbl = &node_list_vtbl;
    impl->IXmlNamedNodeMap_iface.lpVtbl = &named_node_map_vtbl;
    impl->IVectorView_IXmlNode_iface.lpVtbl = &vector_view_vtbl;
    impl->IIterable_IXmlNode_iface.lpVtbl = &iterable_vtbl;
    impl->ref = 1;
    return impl;
}

HRESULT xml_node_list_create( IXMLDOMNodeList *list, IXmlNodeList **out )
{
    struct xml_collection *impl;

    *out = NULL;
    if (!(impl = collection_alloc())) return E_OUTOFMEMORY;
    IXMLDOMNodeList_AddRef( (impl->list = list) );
    *out = &impl->IXmlNodeList_iface;
    return S_OK;
}

HRESULT xml_named_node_map_create( IXMLDOMNamedNodeMap *map, IXmlNamedNodeMap **out )
{
    struct xml_collection *impl;

    *out = NULL;
    if (!(impl = collection_alloc())) return E_OUTOFMEMORY;
    IXMLDOMNamedNodeMap_AddRef( (impl->map = map) );
    *out = &impl->IXmlNamedNodeMap_iface;
    return S_OK;
}
