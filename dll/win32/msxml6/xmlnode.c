/*
 * PROJECT:     LiberNT MSXML 6.0
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows.Data.Xml.Dom node classes over MSXML
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "xmldom_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(msxml);

struct xml_node
{
    IXmlNode IXmlNode_iface;
    IXmlNodeSelector IXmlNodeSelector_iface;
    IXmlNodeSerializer IXmlNodeSerializer_iface;
    IXmlElement IXmlElement_iface;
    IXmlAttribute IXmlAttribute_iface;
    IXmlCharacterData IXmlCharacterData_iface;
    IXmlText IXmlText_iface;
    IXmlComment IXmlComment_iface;
    IXmlCDataSection IXmlCDataSection_iface;
    IXmlProcessingInstruction IXmlProcessingInstruction_iface;
    IXmlDocumentFragment IXmlDocumentFragment_iface;
    IXmlEntityReference IXmlEntityReference_iface;
    IXmlDocumentType IXmlDocumentType_iface;
    IDtdEntity IDtdEntity_iface;
    IDtdNotation IDtdNotation_iface;
    IXmlDocument IXmlDocument_iface;
    IXmlDocumentIO IXmlDocumentIO_iface;
    IXmlDocumentIO2 IXmlDocumentIO2_iface;
    LONG ref;

    IXMLDOMNode *node;
    DOMNodeType type;
};

static const IXmlNodeVtbl xml_node_vtbl;

static const WCHAR *runtime_class_name( DOMNodeType type )
{
    switch (type)
    {
    case NODE_ELEMENT: return RuntimeClass_Windows_Data_Xml_Dom_XmlElement;
    case NODE_ATTRIBUTE: return RuntimeClass_Windows_Data_Xml_Dom_XmlAttribute;
    case NODE_TEXT: return RuntimeClass_Windows_Data_Xml_Dom_XmlText;
    case NODE_CDATA_SECTION: return RuntimeClass_Windows_Data_Xml_Dom_XmlCDataSection;
    case NODE_ENTITY_REFERENCE: return RuntimeClass_Windows_Data_Xml_Dom_XmlEntityReference;
    case NODE_ENTITY: return RuntimeClass_Windows_Data_Xml_Dom_DtdEntity;
    case NODE_PROCESSING_INSTRUCTION: return RuntimeClass_Windows_Data_Xml_Dom_XmlProcessingInstruction;
    case NODE_COMMENT: return RuntimeClass_Windows_Data_Xml_Dom_XmlComment;
    case NODE_DOCUMENT: return RuntimeClass_Windows_Data_Xml_Dom_XmlDocument;
    case NODE_DOCUMENT_TYPE: return RuntimeClass_Windows_Data_Xml_Dom_XmlDocumentType;
    case NODE_DOCUMENT_FRAGMENT: return RuntimeClass_Windows_Data_Xml_Dom_XmlDocumentFragment;
    case NODE_NOTATION: return RuntimeClass_Windows_Data_Xml_Dom_DtdNotation;
    default: return NULL;
    }
}

static const IID *default_iid( DOMNodeType type )
{
    switch (type)
    {
    case NODE_ELEMENT: return &IID_IXmlElement;
    case NODE_ATTRIBUTE: return &IID_IXmlAttribute;
    case NODE_TEXT: return &IID_IXmlText;
    case NODE_CDATA_SECTION: return &IID_IXmlCDataSection;
    case NODE_ENTITY_REFERENCE: return &IID_IXmlEntityReference;
    case NODE_ENTITY: return &IID_IDtdEntity;
    case NODE_PROCESSING_INSTRUCTION: return &IID_IXmlProcessingInstruction;
    case NODE_COMMENT: return &IID_IXmlComment;
    case NODE_DOCUMENT: return &IID_IXmlDocument;
    case NODE_DOCUMENT_TYPE: return &IID_IXmlDocumentType;
    case NODE_DOCUMENT_FRAGMENT: return &IID_IXmlDocumentFragment;
    case NODE_NOTATION: return &IID_IDtdNotation;
    default: return &IID_IXmlNode;
    }
}

static void *node_interface( struct xml_node *impl, REFIID iid )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IXmlNode ))
        return &impl->IXmlNode_iface;
    if (IsEqualGUID( iid, &IID_IXmlNodeSelector )) return &impl->IXmlNodeSelector_iface;
    if (IsEqualGUID( iid, &IID_IXmlNodeSerializer )) return &impl->IXmlNodeSerializer_iface;

    switch (impl->type)
    {
    case NODE_ELEMENT:
        if (IsEqualGUID( iid, &IID_IXmlElement )) return &impl->IXmlElement_iface;
        break;
    case NODE_ATTRIBUTE:
        if (IsEqualGUID( iid, &IID_IXmlAttribute )) return &impl->IXmlAttribute_iface;
        break;
    case NODE_CDATA_SECTION:
        if (IsEqualGUID( iid, &IID_IXmlCDataSection )) return &impl->IXmlCDataSection_iface;
        if (IsEqualGUID( iid, &IID_IXmlText )) return &impl->IXmlText_iface;
        if (IsEqualGUID( iid, &IID_IXmlCharacterData )) return &impl->IXmlCharacterData_iface;
        break;
    case NODE_TEXT:
        if (IsEqualGUID( iid, &IID_IXmlText )) return &impl->IXmlText_iface;
        if (IsEqualGUID( iid, &IID_IXmlCharacterData )) return &impl->IXmlCharacterData_iface;
        break;
    case NODE_COMMENT:
        if (IsEqualGUID( iid, &IID_IXmlComment )) return &impl->IXmlComment_iface;
        if (IsEqualGUID( iid, &IID_IXmlCharacterData )) return &impl->IXmlCharacterData_iface;
        break;
    case NODE_PROCESSING_INSTRUCTION:
        if (IsEqualGUID( iid, &IID_IXmlProcessingInstruction )) return &impl->IXmlProcessingInstruction_iface;
        break;
    case NODE_DOCUMENT_FRAGMENT:
        if (IsEqualGUID( iid, &IID_IXmlDocumentFragment )) return &impl->IXmlDocumentFragment_iface;
        break;
    case NODE_ENTITY_REFERENCE:
        if (IsEqualGUID( iid, &IID_IXmlEntityReference )) return &impl->IXmlEntityReference_iface;
        break;
    case NODE_DOCUMENT_TYPE:
        if (IsEqualGUID( iid, &IID_IXmlDocumentType )) return &impl->IXmlDocumentType_iface;
        break;
    case NODE_ENTITY:
        if (IsEqualGUID( iid, &IID_IDtdEntity )) return &impl->IDtdEntity_iface;
        break;
    case NODE_NOTATION:
        if (IsEqualGUID( iid, &IID_IDtdNotation )) return &impl->IDtdNotation_iface;
        break;
    case NODE_DOCUMENT:
        if (IsEqualGUID( iid, &IID_IXmlDocument )) return &impl->IXmlDocument_iface;
        if (IsEqualGUID( iid, &IID_IXmlDocumentIO )) return &impl->IXmlDocumentIO_iface;
        if (IsEqualGUID( iid, &IID_IXmlDocumentIO2 )) return &impl->IXmlDocumentIO2_iface;
        break;
    default:
        break;
    }
    return NULL;
}

static inline struct xml_node *impl_from_IXmlNode( IXmlNode *iface )
{
    return CONTAINING_RECORD( iface, struct xml_node, IXmlNode_iface );
}

static HRESULT WINAPI xml_node_QueryInterface( IXmlNode *iface, REFIID iid, void **out )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out );

    if ((*out = node_interface( impl, iid )))
    {
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid) );
    return E_NOINTERFACE;
}

static ULONG WINAPI xml_node_AddRef( IXmlNode *iface )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI xml_node_Release( IXmlNode *iface )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        IXMLDOMNode_Release( impl->node );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI xml_node_GetIids( IXmlNode *iface, ULONG *iid_count, IID **iids )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    const IID *list[6];
    ULONG count = 0, i;

    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    list[count++] = &IID_IXmlNode;
    list[count++] = &IID_IXmlNodeSerializer;
    list[count++] = &IID_IXmlNodeSelector;
    switch (impl->type)
    {
    case NODE_TEXT:
        list[count++] = &IID_IXmlCharacterData;
        list[count++] = &IID_IXmlText;
        break;
    case NODE_CDATA_SECTION:
        list[count++] = &IID_IXmlCharacterData;
        list[count++] = &IID_IXmlText;
        list[count++] = &IID_IXmlCDataSection;
        break;
    case NODE_COMMENT:
        list[count++] = &IID_IXmlCharacterData;
        list[count++] = &IID_IXmlComment;
        break;
    default:
        if (default_iid( impl->type ) != &IID_IXmlNode) list[count++] = default_iid( impl->type );
        break;
    }

    if (!(*iids = CoTaskMemAlloc( count * sizeof(IID) ))) return E_OUTOFMEMORY;
    for (i = 0; i < count; i++) (*iids)[i] = *list[i];
    *iid_count = count;
    return S_OK;
}

static HRESULT WINAPI xml_node_GetRuntimeClassName( IXmlNode *iface, HSTRING *class_name )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    const WCHAR *name = runtime_class_name( impl->type );

    TRACE( "iface %p, class_name %p.\n", iface, class_name );

    if (!name) return E_UNEXPECTED;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI xml_node_GetTrustLevel( IXmlNode *iface, TrustLevel *trust_level )
{
    TRACE( "iface %p, trust_level %p.\n", iface, trust_level );
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT return_node( IXMLDOMNode *node, HRESULT hr, IXmlNode **value )
{
    *value = NULL;
    if (FAILED(hr)) return hr;
    if (!node) return S_OK;
    hr = xml_node_create( node, value );
    IXMLDOMNode_Release( node );
    return hr;
}

static HRESULT return_node_as( IXMLDOMNode *node, HRESULT hr, REFIID iid, void **value )
{
    IXmlNode *wrapped;

    *value = NULL;
    if (FAILED(hr = return_node( node, hr, &wrapped ))) return hr;
    if (!wrapped) return S_OK;
    hr = IXmlNode_QueryInterface( wrapped, iid, value );
    IXmlNode_Release( wrapped );
    return hr;
}

static HRESULT return_bstr( BSTR str, HRESULT hr, HSTRING *value )
{
    *value = NULL;
    if (SUCCEEDED(hr) && str) hr = bstr_to_hstring( str, value );
    SysFreeString( str );
    return SUCCEEDED(hr) ? S_OK : hr;
}

static HRESULT return_variant( VARIANT *var, HRESULT hr, IInspectable **value )
{
    *value = NULL;
    if (SUCCEEDED(hr) && V_VT(var) == VT_BSTR) hr = box_bstr( V_BSTR(var), value );
    VariantClear( var );
    return SUCCEEDED(hr) ? S_OK : hr;
}

static HRESULT get_owner_document( struct xml_node *impl, IXMLDOMDocument3 **doc )
{
    IXMLDOMDocument *owner = NULL;
    HRESULT hr;

    if (impl->type == NODE_DOCUMENT)
        return IXMLDOMNode_QueryInterface( impl->node, &IID_IXMLDOMDocument3, (void **)doc );
    if (FAILED(hr = IXMLDOMNode_get_ownerDocument( impl->node, &owner ))) return hr;
    if (!owner) return E_UNEXPECTED;
    hr = IXMLDOMDocument_QueryInterface( owner, &IID_IXMLDOMDocument3, (void **)doc );
    IXMLDOMDocument_Release( owner );
    return hr;
}

static HRESULT WINAPI xml_node_get_NodeValue( IXmlNode *iface, IInspectable **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    VARIANT var;

    TRACE( "iface %p, value %p.\n", iface, value );

    VariantInit( &var );
    return return_variant( &var, IXMLDOMNode_get_nodeValue( impl->node, &var ), value );
}

static HRESULT WINAPI xml_node_put_NodeValue( IXmlNode *iface, IInspectable *value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    VARIANT var;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (FAILED(hr = unbox_bstr( value, &str ))) return hr;
    if (str)
    {
        V_VT(&var) = VT_BSTR;
        V_BSTR(&var) = str;
    }
    else
        V_VT(&var) = VT_NULL;
    hr = IXMLDOMNode_put_nodeValue( impl->node, var );
    SysFreeString( str );
    return hr;
}

static HRESULT WINAPI xml_node_get_NodeType( IXmlNode *iface, NodeType *value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    *value = (NodeType)impl->type;
    return S_OK;
}

static HRESULT WINAPI xml_node_get_NodeName( IXmlNode *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    BSTR str = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_nodeName( impl->node, &str );
    return return_bstr( str, hr, value );
}

static HRESULT WINAPI xml_node_get_ParentNode( IXmlNode *iface, IXmlNode **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *node = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_parentNode( impl->node, &node );
    return return_node( node, hr, value );
}

static HRESULT WINAPI xml_node_get_ChildNodes( IXmlNode *iface, IXmlNodeList **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNodeList *list = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    if (FAILED(hr = IXMLDOMNode_get_childNodes( impl->node, &list ))) return hr;
    hr = xml_node_list_create( list, value );
    IXMLDOMNodeList_Release( list );
    return hr;
}

static HRESULT WINAPI xml_node_get_FirstChild( IXmlNode *iface, IXmlNode **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *node = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_firstChild( impl->node, &node );
    return return_node( node, hr, value );
}

static HRESULT WINAPI xml_node_get_LastChild( IXmlNode *iface, IXmlNode **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *node = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_lastChild( impl->node, &node );
    return return_node( node, hr, value );
}

static HRESULT WINAPI xml_node_get_PreviousSibling( IXmlNode *iface, IXmlNode **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *node = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_previousSibling( impl->node, &node );
    return return_node( node, hr, value );
}

static HRESULT WINAPI xml_node_get_NextSibling( IXmlNode *iface, IXmlNode **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *node = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_nextSibling( impl->node, &node );
    return return_node( node, hr, value );
}

static HRESULT WINAPI xml_node_get_Attributes( IXmlNode *iface, IXmlNamedNodeMap **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNamedNodeMap *map = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    if (FAILED(hr = IXMLDOMNode_get_attributes( impl->node, &map ))) return hr;
    if (!map) return S_OK;
    hr = xml_named_node_map_create( map, value );
    IXMLDOMNamedNodeMap_Release( map );
    return hr;
}

static HRESULT WINAPI xml_node_HasChildNodes( IXmlNode *iface, boolean *value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    VARIANT_BOOL has = VARIANT_FALSE;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    hr = IXMLDOMNode_hasChildNodes( impl->node, &has );
    *value = has != VARIANT_FALSE;
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT WINAPI xml_node_get_OwnerDocument( IXmlNode *iface, IXmlDocument **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMDocument *doc = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    if (FAILED(hr = IXMLDOMNode_get_ownerDocument( impl->node, &doc )) || !doc) return FAILED(hr) ? hr : S_OK;
    return return_node_as( (IXMLDOMNode *)doc, S_OK, &IID_IXmlDocument, (void **)value );
}

static HRESULT unwrap_optional( IXmlNode *node, VARIANT *var )
{
    IXMLDOMNode *dom;
    HRESULT hr;

    VariantInit( var );
    if (!node) return S_OK;
    if (FAILED(hr = xml_node_unwrap( node, &dom ))) return hr;
    V_VT(var) = VT_UNKNOWN;
    V_UNKNOWN(var) = (IUnknown *)dom;
    return S_OK;
}

static HRESULT WINAPI xml_node_InsertBefore( IXmlNode *iface, IXmlNode *new_child, IXmlNode *reference_child,
                                             IXmlNode **inserted_child )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *child, *result = NULL;
    VARIANT ref;
    HRESULT hr;

    TRACE( "iface %p, new_child %p, reference_child %p, inserted_child %p.\n", iface, new_child, reference_child, inserted_child );

    *inserted_child = NULL;
    if (FAILED(hr = xml_node_unwrap( new_child, &child ))) return hr;
    if (SUCCEEDED(hr = unwrap_optional( reference_child, &ref )))
    {
        hr = IXMLDOMNode_insertBefore( impl->node, child, ref, &result );
        VariantClear( &ref );
    }
    IXMLDOMNode_Release( child );
    return return_node( result, hr, inserted_child );
}

static HRESULT WINAPI xml_node_ReplaceChild( IXmlNode *iface, IXmlNode *new_child, IXmlNode *reference_child,
                                             IXmlNode **previous_child )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *child, *old, *result = NULL;
    HRESULT hr;

    TRACE( "iface %p, new_child %p, reference_child %p, previous_child %p.\n", iface, new_child, reference_child, previous_child );

    *previous_child = NULL;
    if (FAILED(hr = xml_node_unwrap( new_child, &child ))) return hr;
    if (SUCCEEDED(hr = xml_node_unwrap( reference_child, &old )))
    {
        hr = IXMLDOMNode_replaceChild( impl->node, child, old, &result );
        IXMLDOMNode_Release( old );
    }
    IXMLDOMNode_Release( child );
    return return_node( result, hr, previous_child );
}

static HRESULT WINAPI xml_node_RemoveChild( IXmlNode *iface, IXmlNode *child_node, IXmlNode **removed_child )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *child, *result = NULL;
    HRESULT hr;

    TRACE( "iface %p, child_node %p, removed_child %p.\n", iface, child_node, removed_child );

    *removed_child = NULL;
    if (FAILED(hr = xml_node_unwrap( child_node, &child ))) return hr;
    hr = IXMLDOMNode_removeChild( impl->node, child, &result );
    IXMLDOMNode_Release( child );
    return return_node( result, hr, removed_child );
}

static HRESULT WINAPI xml_node_AppendChild( IXmlNode *iface, IXmlNode *new_child, IXmlNode **appended_child )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *child, *result = NULL;
    HRESULT hr;

    TRACE( "iface %p, new_child %p, appended_child %p.\n", iface, new_child, appended_child );

    *appended_child = NULL;
    if (FAILED(hr = xml_node_unwrap( new_child, &child ))) return hr;
    hr = IXMLDOMNode_appendChild( impl->node, child, &result );
    IXMLDOMNode_Release( child );
    return return_node( result, hr, appended_child );
}

static HRESULT WINAPI xml_node_CloneNode( IXmlNode *iface, boolean deep, IXmlNode **new_node )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *node = NULL;
    HRESULT hr;

    TRACE( "iface %p, deep %d, new_node %p.\n", iface, deep, new_node );

    hr = IXMLDOMNode_cloneNode( impl->node, deep ? VARIANT_TRUE : VARIANT_FALSE, &node );
    return return_node( node, hr, new_node );
}

static HRESULT box_or_null( BSTR str, HRESULT hr, IInspectable **value )
{
    *value = NULL;
    if (SUCCEEDED(hr) && str && *str) hr = box_bstr( str, value );
    SysFreeString( str );
    return SUCCEEDED(hr) ? S_OK : hr;
}

static HRESULT WINAPI xml_node_get_NamespaceUri( IXmlNode *iface, IInspectable **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    BSTR str = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_namespaceURI( impl->node, &str );
    return box_or_null( str, hr, value );
}

static HRESULT WINAPI xml_node_get_LocalName( IXmlNode *iface, IInspectable **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    BSTR str = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_baseName( impl->node, &str );
    return box_or_null( str, hr, value );
}

static HRESULT WINAPI xml_node_get_Prefix( IXmlNode *iface, IInspectable **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    BSTR str = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_prefix( impl->node, &str );
    return box_or_null( str, hr, value );
}

static HRESULT WINAPI xml_node_Normalize( IXmlNode *iface )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMElement *element;
    HRESULT hr;

    TRACE( "iface %p.\n", iface );

    if (impl->type == NODE_DOCUMENT)
    {
        IXMLDOMDocument3 *doc;
        if (FAILED(hr = get_owner_document( impl, &doc ))) return hr;
        hr = IXMLDOMDocument3_get_documentElement( doc, &element );
        IXMLDOMDocument3_Release( doc );
        if (FAILED(hr) || !element) return FAILED(hr) ? hr : S_OK;
    }
    else if (FAILED(IXMLDOMNode_QueryInterface( impl->node, &IID_IXMLDOMElement, (void **)&element )))
        return S_OK;

    hr = IXMLDOMElement_normalize( element );
    IXMLDOMElement_Release( element );
    return hr;
}

static HRESULT WINAPI xml_node_put_Prefix( IXmlNode *iface, IInspectable *value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMDocument3 *doc;
    IXMLDOMNode *parent = NULL, *replacement = NULL, *old = NULL;
    BSTR prefix, local = NULL, ns = NULL, name;
    VARIANT type;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (impl->type != NODE_ELEMENT && impl->type != NODE_ATTRIBUTE) return E_INVALIDARG;
    if (FAILED(hr = unbox_bstr( value, &prefix ))) return hr;
    if (FAILED(hr = get_owner_document( impl, &doc ))) goto done;
    IXMLDOMNode_get_baseName( impl->node, &local );
    IXMLDOMNode_get_namespaceURI( impl->node, &ns );
    if (!ns || !*ns)
    {
        hr = E_INVALIDARG;
        goto done_doc;
    }
    if (prefix && *prefix)
    {
        if (!(name = SysAllocStringLen( NULL, SysStringLen( prefix ) + 1 + SysStringLen( local ) )))
        {
            hr = E_OUTOFMEMORY;
            goto done_doc;
        }
        swprintf( name, SysStringLen( name ) + 1, L"%s:%s", prefix, local );
    }
    else
        name = SysAllocString( local );

    V_VT(&type) = VT_I4;
    V_I4(&type) = impl->type;
    if (SUCCEEDED(hr = IXMLDOMDocument3_createNode( doc, type, name, ns, &replacement )))
    {
        if (impl->type == NODE_ELEMENT)
        {
            IXMLDOMNode *child = NULL;
            IXMLDOMNamedNodeMap *attrs = NULL;
            IXMLDOMNamedNodeMap *new_attrs = NULL;
            IXMLDOMNode *attr = NULL;

            while (SUCCEEDED(IXMLDOMNode_get_firstChild( impl->node, &child )) && child)
            {
                IXMLDOMNode *moved = NULL;
                hr = IXMLDOMNode_appendChild( replacement, child, &moved );
                IXMLDOMNode_Release( child );
                if (moved) IXMLDOMNode_Release( moved );
                if (FAILED(hr)) break;
            }
            if (SUCCEEDED(hr) && SUCCEEDED(IXMLDOMNode_get_attributes( impl->node, &attrs )) && attrs &&
                SUCCEEDED(IXMLDOMNode_get_attributes( replacement, &new_attrs )) && new_attrs)
            {
                while (SUCCEEDED(IXMLDOMNamedNodeMap_get_item( attrs, 0, &attr )) && attr)
                {
                    IXMLDOMNode *removed = NULL, *added = NULL;
                    BSTR attr_name = NULL;
                    IXMLDOMNode_get_nodeName( attr, &attr_name );
                    IXMLDOMNamedNodeMap_removeNamedItem( attrs, attr_name, &removed );
                    SysFreeString( attr_name );
                    if (removed)
                    {
                        hr = IXMLDOMNamedNodeMap_setNamedItem( new_attrs, removed, &added );
                        IXMLDOMNode_Release( removed );
                        if (added) IXMLDOMNode_Release( added );
                    }
                    IXMLDOMNode_Release( attr );
                    if (FAILED(hr)) break;
                }
            }
            if (new_attrs) IXMLDOMNamedNodeMap_Release( new_attrs );
            if (attrs) IXMLDOMNamedNodeMap_Release( attrs );
        }
        else
        {
            VARIANT val;
            VariantInit( &val );
            if (SUCCEEDED(IXMLDOMNode_get_nodeValue( impl->node, &val )))
                IXMLDOMNode_put_nodeValue( replacement, val );
            VariantClear( &val );
        }

        if (SUCCEEDED(hr) && SUCCEEDED(IXMLDOMNode_get_parentNode( impl->node, &parent )) && parent)
        {
            hr = IXMLDOMNode_replaceChild( parent, replacement, impl->node, &old );
            if (old) IXMLDOMNode_Release( old );
            IXMLDOMNode_Release( parent );
        }
        if (SUCCEEDED(hr))
        {
            IXMLDOMNode_Release( impl->node );
            impl->node = replacement;
            replacement = NULL;
        }
        if (replacement) IXMLDOMNode_Release( replacement );
    }
    SysFreeString( name );

done_doc:
    IXMLDOMDocument3_Release( doc );
done:
    SysFreeString( local );
    SysFreeString( ns );
    SysFreeString( prefix );
    return hr;
}

static const IXmlNodeVtbl xml_node_vtbl =
{
    xml_node_QueryInterface,
    xml_node_AddRef,
    xml_node_Release,
    xml_node_GetIids,
    xml_node_GetRuntimeClassName,
    xml_node_GetTrustLevel,
    xml_node_get_NodeValue,
    xml_node_put_NodeValue,
    xml_node_get_NodeType,
    xml_node_get_NodeName,
    xml_node_get_ParentNode,
    xml_node_get_ChildNodes,
    xml_node_get_FirstChild,
    xml_node_get_LastChild,
    xml_node_get_PreviousSibling,
    xml_node_get_NextSibling,
    xml_node_get_Attributes,
    xml_node_HasChildNodes,
    xml_node_get_OwnerDocument,
    xml_node_InsertBefore,
    xml_node_ReplaceChild,
    xml_node_RemoveChild,
    xml_node_AppendChild,
    xml_node_CloneNode,
    xml_node_get_NamespaceUri,
    xml_node_get_LocalName,
    xml_node_get_Prefix,
    xml_node_Normalize,
    xml_node_put_Prefix,
};

DEFINE_IINSPECTABLE( xml_node_selector, IXmlNodeSelector, struct xml_node, IXmlNode_iface )

static HRESULT select_with_namespaces( struct xml_node *impl, IInspectable *namespaces, BOOL *restore, VARIANT *previous )
{
    IXMLDOMDocument3 *doc;
    VARIANT value;
    BSTR ns;
    HRESULT hr;

    *restore = FALSE;
    VariantInit( previous );
    if (FAILED(hr = unbox_bstr( namespaces, &ns ))) return hr;
    if (FAILED(hr = get_owner_document( impl, &doc )))
    {
        SysFreeString( ns );
        return hr;
    }
    IXMLDOMDocument3_getProperty( doc, (BSTR)L"SelectionNamespaces", previous );
    V_VT(&value) = VT_BSTR;
    V_BSTR(&value) = ns ? ns : SysAllocString( L"" );
    hr = IXMLDOMDocument3_setProperty( doc, (BSTR)L"SelectionNamespaces", value );
    VariantClear( &value );
    IXMLDOMDocument3_Release( doc );
    *restore = SUCCEEDED(hr);
    return hr;
}

static void restore_namespaces( struct xml_node *impl, VARIANT *previous )
{
    IXMLDOMDocument3 *doc;

    if (SUCCEEDED(get_owner_document( impl, &doc )))
    {
        if (V_VT(previous) == VT_EMPTY)
        {
            V_VT(previous) = VT_BSTR;
            V_BSTR(previous) = SysAllocString( L"" );
        }
        IXMLDOMDocument3_setProperty( doc, (BSTR)L"SelectionNamespaces", *previous );
        IXMLDOMDocument3_Release( doc );
    }
    VariantClear( previous );
}

static HRESULT select_single( struct xml_node *impl, HSTRING xpath, IXmlNode **node )
{
    IXMLDOMNode *result = NULL;
    BSTR query;
    HRESULT hr;

    *node = NULL;
    if (!(query = hstring_to_bstr( xpath ))) return E_OUTOFMEMORY;
    hr = IXMLDOMNode_selectSingleNode( impl->node, query, &result );
    SysFreeString( query );
    if (hr == S_FALSE) hr = S_OK;
    return return_node( result, hr, node );
}

static HRESULT select_nodes( struct xml_node *impl, HSTRING xpath, IXmlNodeList **node_list )
{
    IXMLDOMNodeList *list = NULL;
    BSTR query;
    HRESULT hr;

    *node_list = NULL;
    if (!(query = hstring_to_bstr( xpath ))) return E_OUTOFMEMORY;
    hr = IXMLDOMNode_selectNodes( impl->node, query, &list );
    SysFreeString( query );
    if (FAILED(hr)) return hr;
    hr = xml_node_list_create( list, node_list );
    IXMLDOMNodeList_Release( list );
    return hr;
}

static HRESULT WINAPI xml_node_selector_SelectSingleNode( IXmlNodeSelector *iface, HSTRING xpath, IXmlNode **node )
{
    struct xml_node *impl = impl_from_IXmlNodeSelector( iface );
    TRACE( "iface %p, xpath %s, node %p.\n", iface, debugstr_hstring(xpath), node );
    return select_single( impl, xpath, node );
}

static HRESULT WINAPI xml_node_selector_SelectNodes( IXmlNodeSelector *iface, HSTRING xpath, IXmlNodeList **node_list )
{
    struct xml_node *impl = impl_from_IXmlNodeSelector( iface );
    TRACE( "iface %p, xpath %s, node_list %p.\n", iface, debugstr_hstring(xpath), node_list );
    return select_nodes( impl, xpath, node_list );
}

static HRESULT WINAPI xml_node_selector_SelectSingleNodeNS( IXmlNodeSelector *iface, HSTRING xpath,
                                                            IInspectable *namespaces, IXmlNode **node )
{
    struct xml_node *impl = impl_from_IXmlNodeSelector( iface );
    VARIANT previous;
    BOOL restore;
    HRESULT hr;

    TRACE( "iface %p, xpath %s, namespaces %p, node %p.\n", iface, debugstr_hstring(xpath), namespaces, node );

    *node = NULL;
    if (FAILED(hr = select_with_namespaces( impl, namespaces, &restore, &previous ))) return hr;
    hr = select_single( impl, xpath, node );
    if (restore) restore_namespaces( impl, &previous );
    return hr;
}

static HRESULT WINAPI xml_node_selector_SelectNodesNS( IXmlNodeSelector *iface, HSTRING xpath,
                                                       IInspectable *namespaces, IXmlNodeList **node_list )
{
    struct xml_node *impl = impl_from_IXmlNodeSelector( iface );
    VARIANT previous;
    BOOL restore;
    HRESULT hr;

    TRACE( "iface %p, xpath %s, namespaces %p, node_list %p.\n", iface, debugstr_hstring(xpath), namespaces, node_list );

    *node_list = NULL;
    if (FAILED(hr = select_with_namespaces( impl, namespaces, &restore, &previous ))) return hr;
    hr = select_nodes( impl, xpath, node_list );
    if (restore) restore_namespaces( impl, &previous );
    return hr;
}

static const IXmlNodeSelectorVtbl xml_node_selector_vtbl =
{
    xml_node_selector_QueryInterface,
    xml_node_selector_AddRef,
    xml_node_selector_Release,
    xml_node_selector_GetIids,
    xml_node_selector_GetRuntimeClassName,
    xml_node_selector_GetTrustLevel,
    xml_node_selector_SelectSingleNode,
    xml_node_selector_SelectNodes,
    xml_node_selector_SelectSingleNodeNS,
    xml_node_selector_SelectNodesNS,
};

DEFINE_IINSPECTABLE( xml_node_serializer, IXmlNodeSerializer, struct xml_node, IXmlNode_iface )

static HRESULT document_xml( IXMLDOMNode *document, HSTRING *value )
{
    IXMLDOMNodeList *children;
    IXMLDOMNode *child;
    WCHAR *buffer = NULL, *grown;
    UINT32 length = 0, part_length;
    LONG count = 0, i;
    BSTR part;
    HRESULT hr;

    *value = NULL;
    if (FAILED(hr = IXMLDOMNode_get_childNodes( document, &children ))) return hr;
    hr = IXMLDOMNodeList_get_length( children, &count );
    for (i = 0; SUCCEEDED(hr) && i < count; i++)
    {
        if (FAILED(hr = IXMLDOMNodeList_get_item( children, i, &child ))) break;
        part = NULL;
        hr = IXMLDOMNode_get_xml( child, &part );
        IXMLDOMNode_Release( child );
        if (FAILED(hr)) break;
        part_length = SysStringLen( part );
        if (part_length && !(grown = realloc( buffer, (length + part_length) * sizeof(WCHAR) )))
            hr = E_OUTOFMEMORY;
        else if (part_length)
        {
            buffer = grown;
            memcpy( buffer + length, part, part_length * sizeof(WCHAR) );
            length += part_length;
        }
        SysFreeString( part );
    }
    IXMLDOMNodeList_Release( children );
    if (SUCCEEDED(hr)) hr = WindowsCreateString( buffer, length, value );
    free( buffer );
    return hr;
}

static HRESULT WINAPI xml_node_serializer_GetXml( IXmlNodeSerializer *iface, HSTRING *outer_xml )
{
    struct xml_node *impl = impl_from_IXmlNodeSerializer( iface );
    DOMNodeType type;
    BSTR str = NULL;
    HRESULT hr;
    TRACE( "iface %p, outer_xml %p.\n", iface, outer_xml );
    if (SUCCEEDED(IXMLDOMNode_get_nodeType( impl->node, &type )) && type == NODE_DOCUMENT)
        return document_xml( impl->node, outer_xml );
    hr = IXMLDOMNode_get_xml( impl->node, &str );
    return return_bstr( str, hr, outer_xml );
}

static HRESULT WINAPI xml_node_serializer_get_InnerText( IXmlNodeSerializer *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlNodeSerializer( iface );
    BSTR str = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_text( impl->node, &str );
    return return_bstr( str, hr, value );
}

static HRESULT WINAPI xml_node_serializer_put_InnerText( IXmlNodeSerializer *iface, HSTRING value )
{
    struct xml_node *impl = impl_from_IXmlNodeSerializer( iface );
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring(value) );

    if (!(str = hstring_to_bstr( value ))) return E_OUTOFMEMORY;
    hr = IXMLDOMNode_put_text( impl->node, str );
    SysFreeString( str );
    return hr;
}

static const IXmlNodeSerializerVtbl xml_node_serializer_vtbl =
{
    xml_node_serializer_QueryInterface,
    xml_node_serializer_AddRef,
    xml_node_serializer_Release,
    xml_node_serializer_GetIids,
    xml_node_serializer_GetRuntimeClassName,
    xml_node_serializer_GetTrustLevel,
    xml_node_serializer_GetXml,
    xml_node_serializer_get_InnerText,
    xml_node_serializer_put_InnerText,
};

DEFINE_IINSPECTABLE( xml_element, IXmlElement, struct xml_node, IXmlNode_iface )

static HRESULT get_element( struct xml_node *impl, IXMLDOMElement **element )
{
    return IXMLDOMNode_QueryInterface( impl->node, &IID_IXMLDOMElement, (void **)element );
}

static HRESULT get_qualified_attribute( struct xml_node *impl, IInspectable *namespace_uri, HSTRING local_name,
                                        IXMLDOMNode **attr )
{
    IXMLDOMNamedNodeMap *map = NULL;
    BSTR ns, name;
    HRESULT hr;

    *attr = NULL;
    if (FAILED(hr = unbox_bstr( namespace_uri, &ns ))) return hr;
    if (!(name = hstring_to_bstr( local_name )))
    {
        SysFreeString( ns );
        return E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr = IXMLDOMNode_get_attributes( impl->node, &map )) && map)
    {
        hr = IXMLDOMNamedNodeMap_getQualifiedItem( map, name, ns ? ns : (BSTR)L"", attr );
        IXMLDOMNamedNodeMap_Release( map );
        if (hr == S_FALSE) hr = S_OK;
    }
    SysFreeString( name );
    SysFreeString( ns );
    return hr;
}

static HRESULT WINAPI xml_element_get_TagName( IXmlElement *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMElement *element;
    BSTR str = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    if (FAILED(hr = get_element( impl, &element ))) return hr;
    hr = IXMLDOMElement_get_tagName( element, &str );
    IXMLDOMElement_Release( element );
    return return_bstr( str, hr, value );
}

static HRESULT WINAPI xml_element_GetAttribute( IXmlElement *iface, HSTRING attribute_name, HSTRING *attribute_value )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMElement *element;
    VARIANT var;
    BSTR name;
    HRESULT hr;

    TRACE( "iface %p, attribute_name %s, attribute_value %p.\n", iface, debugstr_hstring(attribute_name), attribute_value );

    *attribute_value = NULL;
    if (FAILED(hr = get_element( impl, &element ))) return hr;
    if (!(name = hstring_to_bstr( attribute_name )))
    {
        IXMLDOMElement_Release( element );
        return E_OUTOFMEMORY;
    }
    VariantInit( &var );
    hr = IXMLDOMElement_getAttribute( element, name, &var );
    if (SUCCEEDED(hr) && V_VT(&var) == VT_BSTR) hr = bstr_to_hstring( V_BSTR(&var), attribute_value );
    VariantClear( &var );
    SysFreeString( name );
    IXMLDOMElement_Release( element );
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT WINAPI xml_element_SetAttribute( IXmlElement *iface, HSTRING attribute_name, HSTRING attribute_value )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMElement *element;
    VARIANT var;
    BSTR name;
    HRESULT hr;

    TRACE( "iface %p, attribute_name %s, attribute_value %s.\n", iface, debugstr_hstring(attribute_name),
           debugstr_hstring(attribute_value) );

    if (FAILED(hr = get_element( impl, &element ))) return hr;
    name = hstring_to_bstr( attribute_name );
    V_VT(&var) = VT_BSTR;
    V_BSTR(&var) = hstring_to_bstr( attribute_value );
    if (name && V_BSTR(&var)) hr = IXMLDOMElement_setAttribute( element, name, var );
    else hr = E_OUTOFMEMORY;
    VariantClear( &var );
    SysFreeString( name );
    IXMLDOMElement_Release( element );
    return hr;
}

static HRESULT WINAPI xml_element_RemoveAttribute( IXmlElement *iface, HSTRING attribute_name )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMElement *element;
    BSTR name;
    HRESULT hr;

    TRACE( "iface %p, attribute_name %s.\n", iface, debugstr_hstring(attribute_name) );

    if (FAILED(hr = get_element( impl, &element ))) return hr;
    if ((name = hstring_to_bstr( attribute_name ))) hr = IXMLDOMElement_removeAttribute( element, name );
    else hr = E_OUTOFMEMORY;
    SysFreeString( name );
    IXMLDOMElement_Release( element );
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT WINAPI xml_element_GetAttributeNode( IXmlElement *iface, HSTRING attribute_name, IXmlAttribute **attribute_node )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMElement *element;
    IXMLDOMAttribute *attr = NULL;
    BSTR name;
    HRESULT hr;

    TRACE( "iface %p, attribute_name %s, attribute_node %p.\n", iface, debugstr_hstring(attribute_name), attribute_node );

    *attribute_node = NULL;
    if (FAILED(hr = get_element( impl, &element ))) return hr;
    if ((name = hstring_to_bstr( attribute_name ))) hr = IXMLDOMElement_getAttributeNode( element, name, &attr );
    else hr = E_OUTOFMEMORY;
    SysFreeString( name );
    IXMLDOMElement_Release( element );
    if (hr == S_FALSE) hr = S_OK;
    return return_node_as( (IXMLDOMNode *)attr, hr, &IID_IXmlAttribute, (void **)attribute_node );
}

static HRESULT unwrap_attribute( IXmlAttribute *attribute, IXMLDOMAttribute **out )
{
    IXmlNode *node;
    IXMLDOMNode *dom;
    HRESULT hr;

    *out = NULL;
    if (!attribute) return E_INVALIDARG;
    if (FAILED(hr = IXmlAttribute_QueryInterface( attribute, &IID_IXmlNode, (void **)&node ))) return hr;
    hr = xml_node_unwrap( node, &dom );
    IXmlNode_Release( node );
    if (FAILED(hr)) return hr;
    hr = IXMLDOMNode_QueryInterface( dom, &IID_IXMLDOMAttribute, (void **)out );
    IXMLDOMNode_Release( dom );
    return hr;
}

static HRESULT WINAPI xml_element_SetAttributeNode( IXmlElement *iface, IXmlAttribute *new_attribute,
                                                    IXmlAttribute **previous_attribute )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMElement *element;
    IXMLDOMAttribute *attr, *previous = NULL;
    HRESULT hr;

    TRACE( "iface %p, new_attribute %p, previous_attribute %p.\n", iface, new_attribute, previous_attribute );

    *previous_attribute = NULL;
    if (FAILED(hr = unwrap_attribute( new_attribute, &attr ))) return hr;
    if (SUCCEEDED(hr = get_element( impl, &element )))
    {
        hr = IXMLDOMElement_setAttributeNode( element, attr, &previous );
        IXMLDOMElement_Release( element );
    }
    IXMLDOMAttribute_Release( attr );
    return return_node_as( (IXMLDOMNode *)previous, hr, &IID_IXmlAttribute, (void **)previous_attribute );
}

static HRESULT WINAPI xml_element_RemoveAttributeNode( IXmlElement *iface, IXmlAttribute *attribute_node,
                                                       IXmlAttribute **removed_attribute )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMElement *element;
    IXMLDOMAttribute *attr, *removed = NULL;
    HRESULT hr;

    TRACE( "iface %p, attribute_node %p, removed_attribute %p.\n", iface, attribute_node, removed_attribute );

    *removed_attribute = NULL;
    if (FAILED(hr = unwrap_attribute( attribute_node, &attr ))) return hr;
    if (SUCCEEDED(hr = get_element( impl, &element )))
    {
        hr = IXMLDOMElement_removeAttributeNode( element, attr, &removed );
        IXMLDOMElement_Release( element );
    }
    IXMLDOMAttribute_Release( attr );
    return return_node_as( (IXMLDOMNode *)removed, hr, &IID_IXmlAttribute, (void **)removed_attribute );
}

static HRESULT elements_by_tag_name( IXMLDOMNode *node, HSTRING tag_name, IXmlNodeList **elements )
{
    IXMLDOMNodeList *list = NULL;
    IXMLDOMElement *element;
    IXMLDOMDocument *doc;
    BSTR name;
    HRESULT hr;

    *elements = NULL;
    if (!(name = hstring_to_bstr( tag_name ))) return E_OUTOFMEMORY;
    if (SUCCEEDED(IXMLDOMNode_QueryInterface( node, &IID_IXMLDOMElement, (void **)&element )))
    {
        hr = IXMLDOMElement_getElementsByTagName( element, name, &list );
        IXMLDOMElement_Release( element );
    }
    else if (SUCCEEDED(hr = IXMLDOMNode_QueryInterface( node, &IID_IXMLDOMDocument, (void **)&doc )))
    {
        hr = IXMLDOMDocument_getElementsByTagName( doc, name, &list );
        IXMLDOMDocument_Release( doc );
    }
    SysFreeString( name );
    if (FAILED(hr)) return hr;
    hr = xml_node_list_create( list, elements );
    IXMLDOMNodeList_Release( list );
    return hr;
}

static HRESULT WINAPI xml_element_GetElementsByTagName( IXmlElement *iface, HSTRING tag_name, IXmlNodeList **elements )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    TRACE( "iface %p, tag_name %s, elements %p.\n", iface, debugstr_hstring(tag_name), elements );
    return elements_by_tag_name( impl->node, tag_name, elements );
}

static HRESULT create_qualified_node( struct xml_node *impl, DOMNodeType type, IInspectable *namespace_uri,
                                      HSTRING qualified_name, IXMLDOMNode **node )
{
    IXMLDOMDocument3 *doc;
    VARIANT var;
    BSTR ns, name;
    HRESULT hr;

    *node = NULL;
    if (FAILED(hr = unbox_bstr( namespace_uri, &ns ))) return hr;
    if (!(name = hstring_to_bstr( qualified_name )))
    {
        SysFreeString( ns );
        return E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr = get_owner_document( impl, &doc )))
    {
        V_VT(&var) = VT_I4;
        V_I4(&var) = type;
        hr = IXMLDOMDocument3_createNode( doc, var, name, ns ? ns : (BSTR)L"", node );
        IXMLDOMDocument3_Release( doc );
    }
    SysFreeString( name );
    SysFreeString( ns );
    return hr;
}

static HRESULT WINAPI xml_element_SetAttributeNS( IXmlElement *iface, IInspectable *namespace_uri, HSTRING qualified_name,
                                                  HSTRING value )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMNamedNodeMap *map = NULL;
    IXMLDOMNode *attr, *previous = NULL;
    VARIANT var;
    HRESULT hr;

    TRACE( "iface %p, namespace_uri %p, qualified_name %s, value %s.\n", iface, namespace_uri,
           debugstr_hstring(qualified_name), debugstr_hstring(value) );

    if (FAILED(hr = create_qualified_node( impl, NODE_ATTRIBUTE, namespace_uri, qualified_name, &attr ))) return hr;
    V_VT(&var) = VT_BSTR;
    V_BSTR(&var) = hstring_to_bstr( value );
    if (SUCCEEDED(hr = IXMLDOMNode_put_nodeValue( attr, var )) &&
        SUCCEEDED(hr = IXMLDOMNode_get_attributes( impl->node, &map )) && map)
    {
        hr = IXMLDOMNamedNodeMap_setNamedItem( map, attr, &previous );
        if (previous) IXMLDOMNode_Release( previous );
        IXMLDOMNamedNodeMap_Release( map );
    }
    VariantClear( &var );
    IXMLDOMNode_Release( attr );
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT WINAPI xml_element_GetAttributeNS( IXmlElement *iface, IInspectable *namespace_uri, HSTRING local_name,
                                                  HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMNode *attr;
    VARIANT var;
    HRESULT hr;

    TRACE( "iface %p, namespace_uri %p, local_name %s, value %p.\n", iface, namespace_uri, debugstr_hstring(local_name), value );

    *value = NULL;
    if (FAILED(hr = get_qualified_attribute( impl, namespace_uri, local_name, &attr )) || !attr) return hr;
    VariantInit( &var );
    hr = IXMLDOMNode_get_nodeValue( attr, &var );
    if (SUCCEEDED(hr) && V_VT(&var) == VT_BSTR) hr = bstr_to_hstring( V_BSTR(&var), value );
    VariantClear( &var );
    IXMLDOMNode_Release( attr );
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT WINAPI xml_element_RemoveAttributeNS( IXmlElement *iface, IInspectable *namespace_uri, HSTRING local_name )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMNamedNodeMap *map = NULL;
    IXMLDOMNode *removed = NULL;
    BSTR ns, name;
    HRESULT hr;

    TRACE( "iface %p, namespace_uri %p, local_name %s.\n", iface, namespace_uri, debugstr_hstring(local_name) );

    if (FAILED(hr = unbox_bstr( namespace_uri, &ns ))) return hr;
    name = hstring_to_bstr( local_name );
    if (SUCCEEDED(hr = IXMLDOMNode_get_attributes( impl->node, &map )) && map)
    {
        hr = IXMLDOMNamedNodeMap_removeQualifiedItem( map, name, ns ? ns : (BSTR)L"", &removed );
        if (removed) IXMLDOMNode_Release( removed );
        IXMLDOMNamedNodeMap_Release( map );
    }
    SysFreeString( name );
    SysFreeString( ns );
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT WINAPI xml_element_SetAttributeNodeNS( IXmlElement *iface, IXmlAttribute *new_attribute,
                                                      IXmlAttribute **previous_attribute )
{
    TRACE( "iface %p, new_attribute %p, previous_attribute %p.\n", iface, new_attribute, previous_attribute );
    return xml_element_SetAttributeNode( iface, new_attribute, previous_attribute );
}

static HRESULT WINAPI xml_element_GetAttributeNodeNS( IXmlElement *iface, IInspectable *namespace_uri, HSTRING local_name,
                                                      IXmlAttribute **previous_attribute )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMNode *attr;
    HRESULT hr;

    TRACE( "iface %p, namespace_uri %p, local_name %s, previous_attribute %p.\n", iface, namespace_uri,
           debugstr_hstring(local_name), previous_attribute );

    *previous_attribute = NULL;
    hr = get_qualified_attribute( impl, namespace_uri, local_name, &attr );
    return return_node_as( attr, hr, &IID_IXmlAttribute, (void **)previous_attribute );
}

static const IXmlElementVtbl xml_element_vtbl =
{
    xml_element_QueryInterface,
    xml_element_AddRef,
    xml_element_Release,
    xml_element_GetIids,
    xml_element_GetRuntimeClassName,
    xml_element_GetTrustLevel,
    xml_element_get_TagName,
    xml_element_GetAttribute,
    xml_element_SetAttribute,
    xml_element_RemoveAttribute,
    xml_element_GetAttributeNode,
    xml_element_SetAttributeNode,
    xml_element_RemoveAttributeNode,
    xml_element_GetElementsByTagName,
    xml_element_SetAttributeNS,
    xml_element_GetAttributeNS,
    xml_element_RemoveAttributeNS,
    xml_element_SetAttributeNodeNS,
    xml_element_GetAttributeNodeNS,
};

DEFINE_IINSPECTABLE( xml_attribute, IXmlAttribute, struct xml_node, IXmlNode_iface )

static HRESULT WINAPI xml_attribute_get_Name( IXmlAttribute *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlAttribute( iface );
    BSTR str = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_nodeName( impl->node, &str );
    return return_bstr( str, hr, value );
}

static HRESULT WINAPI xml_attribute_get_Specified( IXmlAttribute *iface, boolean *value )
{
    struct xml_node *impl = impl_from_IXmlAttribute( iface );
    VARIANT_BOOL specified = VARIANT_FALSE;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    hr = IXMLDOMNode_get_specified( impl->node, &specified );
    *value = specified != VARIANT_FALSE;
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT WINAPI xml_attribute_get_Value( IXmlAttribute *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlAttribute( iface );
    VARIANT var;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    VariantInit( &var );
    hr = IXMLDOMNode_get_nodeValue( impl->node, &var );
    if (SUCCEEDED(hr) && V_VT(&var) == VT_BSTR) hr = bstr_to_hstring( V_BSTR(&var), value );
    VariantClear( &var );
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT WINAPI xml_attribute_put_Value( IXmlAttribute *iface, HSTRING value )
{
    struct xml_node *impl = impl_from_IXmlAttribute( iface );
    VARIANT var;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring(value) );

    V_VT(&var) = VT_BSTR;
    if (!(V_BSTR(&var) = hstring_to_bstr( value ))) return E_OUTOFMEMORY;
    hr = IXMLDOMNode_put_nodeValue( impl->node, var );
    VariantClear( &var );
    return hr;
}

static const IXmlAttributeVtbl xml_attribute_vtbl =
{
    xml_attribute_QueryInterface,
    xml_attribute_AddRef,
    xml_attribute_Release,
    xml_attribute_GetIids,
    xml_attribute_GetRuntimeClassName,
    xml_attribute_GetTrustLevel,
    xml_attribute_get_Name,
    xml_attribute_get_Specified,
    xml_attribute_get_Value,
    xml_attribute_put_Value,
};

DEFINE_IINSPECTABLE( xml_character_data, IXmlCharacterData, struct xml_node, IXmlNode_iface )

static HRESULT get_character_data( struct xml_node *impl, IXMLDOMCharacterData **data )
{
    return IXMLDOMNode_QueryInterface( impl->node, &IID_IXMLDOMCharacterData, (void **)data );
}

static HRESULT WINAPI xml_character_data_get_Data( IXmlCharacterData *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlCharacterData( iface );
    IXMLDOMCharacterData *data;
    BSTR str = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    if (FAILED(hr = get_character_data( impl, &data ))) return hr;
    hr = IXMLDOMCharacterData_get_data( data, &str );
    IXMLDOMCharacterData_Release( data );
    return return_bstr( str, hr, value );
}

static HRESULT WINAPI xml_character_data_put_Data( IXmlCharacterData *iface, HSTRING value )
{
    struct xml_node *impl = impl_from_IXmlCharacterData( iface );
    IXMLDOMCharacterData *data;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring(value) );

    if (FAILED(hr = get_character_data( impl, &data ))) return hr;
    if ((str = hstring_to_bstr( value ))) hr = IXMLDOMCharacterData_put_data( data, str );
    else hr = E_OUTOFMEMORY;
    SysFreeString( str );
    IXMLDOMCharacterData_Release( data );
    return hr;
}

static HRESULT WINAPI xml_character_data_get_Length( IXmlCharacterData *iface, UINT32 *value )
{
    struct xml_node *impl = impl_from_IXmlCharacterData( iface );
    IXMLDOMCharacterData *data;
    LONG length = 0;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (FAILED(hr = get_character_data( impl, &data ))) return hr;
    hr = IXMLDOMCharacterData_get_length( data, &length );
    IXMLDOMCharacterData_Release( data );
    *value = length;
    return hr;
}

static HRESULT WINAPI xml_character_data_SubstringData( IXmlCharacterData *iface, UINT32 offset, UINT32 count, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlCharacterData( iface );
    IXMLDOMCharacterData *data;
    BSTR str = NULL;
    HRESULT hr;

    TRACE( "iface %p, offset %u, count %u, value %p.\n", iface, offset, count, value );

    *value = NULL;
    if (FAILED(hr = get_character_data( impl, &data ))) return hr;
    hr = IXMLDOMCharacterData_substringData( data, offset, count, &str );
    IXMLDOMCharacterData_Release( data );
    return return_bstr( str, hr, value );
}

static HRESULT WINAPI xml_character_data_AppendData( IXmlCharacterData *iface, HSTRING value )
{
    struct xml_node *impl = impl_from_IXmlCharacterData( iface );
    IXMLDOMCharacterData *data;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring(value) );

    if (FAILED(hr = get_character_data( impl, &data ))) return hr;
    if ((str = hstring_to_bstr( value ))) hr = IXMLDOMCharacterData_appendData( data, str );
    else hr = E_OUTOFMEMORY;
    SysFreeString( str );
    IXMLDOMCharacterData_Release( data );
    return hr;
}

static HRESULT WINAPI xml_character_data_InsertData( IXmlCharacterData *iface, UINT32 offset, HSTRING value )
{
    struct xml_node *impl = impl_from_IXmlCharacterData( iface );
    IXMLDOMCharacterData *data;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, offset %u, value %s.\n", iface, offset, debugstr_hstring(value) );

    if (FAILED(hr = get_character_data( impl, &data ))) return hr;
    if ((str = hstring_to_bstr( value ))) hr = IXMLDOMCharacterData_insertData( data, offset, str );
    else hr = E_OUTOFMEMORY;
    SysFreeString( str );
    IXMLDOMCharacterData_Release( data );
    return hr;
}

static HRESULT WINAPI xml_character_data_DeleteData( IXmlCharacterData *iface, UINT32 offset, UINT32 count )
{
    struct xml_node *impl = impl_from_IXmlCharacterData( iface );
    IXMLDOMCharacterData *data;
    HRESULT hr;

    TRACE( "iface %p, offset %u, count %u.\n", iface, offset, count );

    if (FAILED(hr = get_character_data( impl, &data ))) return hr;
    hr = IXMLDOMCharacterData_deleteData( data, offset, count );
    IXMLDOMCharacterData_Release( data );
    return hr;
}

static HRESULT WINAPI xml_character_data_ReplaceData( IXmlCharacterData *iface, UINT32 offset, UINT32 count, HSTRING value )
{
    struct xml_node *impl = impl_from_IXmlCharacterData( iface );
    IXMLDOMCharacterData *data;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, offset %u, count %u, value %s.\n", iface, offset, count, debugstr_hstring(value) );

    if (FAILED(hr = get_character_data( impl, &data ))) return hr;
    if ((str = hstring_to_bstr( value ))) hr = IXMLDOMCharacterData_replaceData( data, offset, count, str );
    else hr = E_OUTOFMEMORY;
    SysFreeString( str );
    IXMLDOMCharacterData_Release( data );
    return hr;
}

static const IXmlCharacterDataVtbl xml_character_data_vtbl =
{
    xml_character_data_QueryInterface,
    xml_character_data_AddRef,
    xml_character_data_Release,
    xml_character_data_GetIids,
    xml_character_data_GetRuntimeClassName,
    xml_character_data_GetTrustLevel,
    xml_character_data_get_Data,
    xml_character_data_put_Data,
    xml_character_data_get_Length,
    xml_character_data_SubstringData,
    xml_character_data_AppendData,
    xml_character_data_InsertData,
    xml_character_data_DeleteData,
    xml_character_data_ReplaceData,
};

DEFINE_IINSPECTABLE( xml_text, IXmlText, struct xml_node, IXmlNode_iface )

static HRESULT WINAPI xml_text_SplitText( IXmlText *iface, UINT32 offset, IXmlText **second_part )
{
    struct xml_node *impl = impl_from_IXmlText( iface );
    IXMLDOMText *text, *second = NULL;
    HRESULT hr;

    TRACE( "iface %p, offset %u, second_part %p.\n", iface, offset, second_part );

    *second_part = NULL;
    if (FAILED(hr = IXMLDOMNode_QueryInterface( impl->node, &IID_IXMLDOMText, (void **)&text ))) return hr;
    hr = IXMLDOMText_splitText( text, offset, &second );
    IXMLDOMText_Release( text );
    return return_node_as( (IXMLDOMNode *)second, hr, &IID_IXmlText, (void **)second_part );
}

static const IXmlTextVtbl xml_text_vtbl =
{
    xml_text_QueryInterface,
    xml_text_AddRef,
    xml_text_Release,
    xml_text_GetIids,
    xml_text_GetRuntimeClassName,
    xml_text_GetTrustLevel,
    xml_text_SplitText,
};

DEFINE_IINSPECTABLE( xml_comment, IXmlComment, struct xml_node, IXmlNode_iface )

static const IXmlCommentVtbl xml_comment_vtbl =
{
    xml_comment_QueryInterface,
    xml_comment_AddRef,
    xml_comment_Release,
    xml_comment_GetIids,
    xml_comment_GetRuntimeClassName,
    xml_comment_GetTrustLevel,
};

DEFINE_IINSPECTABLE( xml_cdata_section, IXmlCDataSection, struct xml_node, IXmlNode_iface )

static const IXmlCDataSectionVtbl xml_cdata_section_vtbl =
{
    xml_cdata_section_QueryInterface,
    xml_cdata_section_AddRef,
    xml_cdata_section_Release,
    xml_cdata_section_GetIids,
    xml_cdata_section_GetRuntimeClassName,
    xml_cdata_section_GetTrustLevel,
};

DEFINE_IINSPECTABLE( xml_processing_instruction, IXmlProcessingInstruction, struct xml_node, IXmlNode_iface )

static HRESULT WINAPI xml_processing_instruction_get_Target( IXmlProcessingInstruction *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlProcessingInstruction( iface );
    BSTR str = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_nodeName( impl->node, &str );
    return return_bstr( str, hr, value );
}

static HRESULT WINAPI xml_processing_instruction_get_Data( IXmlProcessingInstruction *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlProcessingInstruction( iface );
    VARIANT var;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    VariantInit( &var );
    hr = IXMLDOMNode_get_nodeValue( impl->node, &var );
    if (SUCCEEDED(hr) && V_VT(&var) == VT_BSTR) hr = bstr_to_hstring( V_BSTR(&var), value );
    VariantClear( &var );
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT WINAPI xml_processing_instruction_put_Data( IXmlProcessingInstruction *iface, HSTRING value )
{
    struct xml_node *impl = impl_from_IXmlProcessingInstruction( iface );
    VARIANT var;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring(value) );

    V_VT(&var) = VT_BSTR;
    if (!(V_BSTR(&var) = hstring_to_bstr( value ))) return E_OUTOFMEMORY;
    hr = IXMLDOMNode_put_nodeValue( impl->node, var );
    VariantClear( &var );
    return hr;
}

static const IXmlProcessingInstructionVtbl xml_processing_instruction_vtbl =
{
    xml_processing_instruction_QueryInterface,
    xml_processing_instruction_AddRef,
    xml_processing_instruction_Release,
    xml_processing_instruction_GetIids,
    xml_processing_instruction_GetRuntimeClassName,
    xml_processing_instruction_GetTrustLevel,
    xml_processing_instruction_get_Target,
    xml_processing_instruction_get_Data,
    xml_processing_instruction_put_Data,
};

DEFINE_IINSPECTABLE( xml_document_fragment, IXmlDocumentFragment, struct xml_node, IXmlNode_iface )

static const IXmlDocumentFragmentVtbl xml_document_fragment_vtbl =
{
    xml_document_fragment_QueryInterface,
    xml_document_fragment_AddRef,
    xml_document_fragment_Release,
    xml_document_fragment_GetIids,
    xml_document_fragment_GetRuntimeClassName,
    xml_document_fragment_GetTrustLevel,
};

DEFINE_IINSPECTABLE( xml_entity_reference, IXmlEntityReference, struct xml_node, IXmlNode_iface )

static const IXmlEntityReferenceVtbl xml_entity_reference_vtbl =
{
    xml_entity_reference_QueryInterface,
    xml_entity_reference_AddRef,
    xml_entity_reference_Release,
    xml_entity_reference_GetIids,
    xml_entity_reference_GetRuntimeClassName,
    xml_entity_reference_GetTrustLevel,
};

DEFINE_IINSPECTABLE( xml_document_type, IXmlDocumentType, struct xml_node, IXmlNode_iface )

static HRESULT WINAPI xml_document_type_get_Name( IXmlDocumentType *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlDocumentType( iface );
    BSTR str = NULL;
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    hr = IXMLDOMNode_get_nodeName( impl->node, &str );
    return return_bstr( str, hr, value );
}

static HRESULT document_type_map( struct xml_node *impl, BOOL entities, IXmlNamedNodeMap **value )
{
    IXMLDOMDocumentType *type;
    IXMLDOMNamedNodeMap *map = NULL;
    HRESULT hr;

    *value = NULL;
    if (FAILED(hr = IXMLDOMNode_QueryInterface( impl->node, &IID_IXMLDOMDocumentType, (void **)&type ))) return hr;
    hr = entities ? IXMLDOMDocumentType_get_entities( type, &map ) : IXMLDOMDocumentType_get_notations( type, &map );
    IXMLDOMDocumentType_Release( type );
    if (FAILED(hr) || !map) return FAILED(hr) ? hr : S_OK;
    hr = xml_named_node_map_create( map, value );
    IXMLDOMNamedNodeMap_Release( map );
    return hr;
}

static HRESULT WINAPI xml_document_type_get_Entities( IXmlDocumentType *iface, IXmlNamedNodeMap **value )
{
    struct xml_node *impl = impl_from_IXmlDocumentType( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return document_type_map( impl, TRUE, value );
}

static HRESULT WINAPI xml_document_type_get_Notations( IXmlDocumentType *iface, IXmlNamedNodeMap **value )
{
    struct xml_node *impl = impl_from_IXmlDocumentType( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return document_type_map( impl, FALSE, value );
}

static const IXmlDocumentTypeVtbl xml_document_type_vtbl =
{
    xml_document_type_QueryInterface,
    xml_document_type_AddRef,
    xml_document_type_Release,
    xml_document_type_GetIids,
    xml_document_type_GetRuntimeClassName,
    xml_document_type_GetTrustLevel,
    xml_document_type_get_Name,
    xml_document_type_get_Entities,
    xml_document_type_get_Notations,
};

DEFINE_IINSPECTABLE( dtd_entity, IDtdEntity, struct xml_node, IXmlNode_iface )

static HRESULT boxed_variant_property( IXMLDOMNode *node, REFIID iid, int which, IInspectable **value )
{
    IUnknown *unk;
    VARIANT var;
    HRESULT hr;

    *value = NULL;
    if (FAILED(hr = IXMLDOMNode_QueryInterface( node, iid, (void **)&unk ))) return hr;
    VariantInit( &var );
    if (IsEqualGUID( iid, &IID_IXMLDOMEntity ))
    {
        IXMLDOMEntity *entity = (IXMLDOMEntity *)unk;
        if (which == 0) hr = IXMLDOMEntity_get_publicId( entity, &var );
        else if (which == 1) hr = IXMLDOMEntity_get_systemId( entity, &var );
        else
        {
            BSTR name = NULL;
            hr = IXMLDOMEntity_get_notationName( entity, &name );
            if (SUCCEEDED(hr) && name)
            {
                V_VT(&var) = VT_BSTR;
                V_BSTR(&var) = name;
            }
        }
    }
    else
    {
        IXMLDOMNotation *notation = (IXMLDOMNotation *)unk;
        if (which == 0) hr = IXMLDOMNotation_get_publicId( notation, &var );
        else hr = IXMLDOMNotation_get_systemId( notation, &var );
    }
    IUnknown_Release( unk );
    return return_variant( &var, hr, value );
}

static HRESULT WINAPI dtd_entity_get_PublicId( IDtdEntity *iface, IInspectable **value )
{
    struct xml_node *impl = impl_from_IDtdEntity( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return boxed_variant_property( impl->node, &IID_IXMLDOMEntity, 0, value );
}

static HRESULT WINAPI dtd_entity_get_SystemId( IDtdEntity *iface, IInspectable **value )
{
    struct xml_node *impl = impl_from_IDtdEntity( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return boxed_variant_property( impl->node, &IID_IXMLDOMEntity, 1, value );
}

static HRESULT WINAPI dtd_entity_get_NotationName( IDtdEntity *iface, IInspectable **value )
{
    struct xml_node *impl = impl_from_IDtdEntity( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return boxed_variant_property( impl->node, &IID_IXMLDOMEntity, 2, value );
}

static const IDtdEntityVtbl dtd_entity_vtbl =
{
    dtd_entity_QueryInterface,
    dtd_entity_AddRef,
    dtd_entity_Release,
    dtd_entity_GetIids,
    dtd_entity_GetRuntimeClassName,
    dtd_entity_GetTrustLevel,
    dtd_entity_get_PublicId,
    dtd_entity_get_SystemId,
    dtd_entity_get_NotationName,
};

DEFINE_IINSPECTABLE( dtd_notation, IDtdNotation, struct xml_node, IXmlNode_iface )

static HRESULT WINAPI dtd_notation_get_PublicId( IDtdNotation *iface, IInspectable **value )
{
    struct xml_node *impl = impl_from_IDtdNotation( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return boxed_variant_property( impl->node, &IID_IXMLDOMNotation, 0, value );
}

static HRESULT WINAPI dtd_notation_get_SystemId( IDtdNotation *iface, IInspectable **value )
{
    struct xml_node *impl = impl_from_IDtdNotation( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    return boxed_variant_property( impl->node, &IID_IXMLDOMNotation, 1, value );
}

static const IDtdNotationVtbl dtd_notation_vtbl =
{
    dtd_notation_QueryInterface,
    dtd_notation_AddRef,
    dtd_notation_Release,
    dtd_notation_GetIids,
    dtd_notation_GetRuntimeClassName,
    dtd_notation_GetTrustLevel,
    dtd_notation_get_PublicId,
    dtd_notation_get_SystemId,
};

DEFINE_IINSPECTABLE( xml_document, IXmlDocument, struct xml_node, IXmlNode_iface )

static HRESULT get_document( struct xml_node *impl, IXMLDOMDocument3 **doc )
{
    return IXMLDOMNode_QueryInterface( impl->node, &IID_IXMLDOMDocument3, (void **)doc );
}

static HRESULT WINAPI xml_document_get_Doctype( IXmlDocument *iface, IXmlDocumentType **value )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    IXMLDOMDocumentType *type = NULL;
    IXMLDOMDocument3 *doc;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    if (FAILED(hr = get_document( impl, &doc ))) return hr;
    hr = IXMLDOMDocument3_get_doctype( doc, &type );
    IXMLDOMDocument3_Release( doc );
    if (hr == S_FALSE) hr = S_OK;
    return return_node_as( (IXMLDOMNode *)type, hr, &IID_IXmlDocumentType, (void **)value );
}

static HRESULT WINAPI xml_document_get_Implementation( IXmlDocument *iface, IXmlDomImplementation **value )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    IXMLDOMImplementation *dom_impl = NULL;
    IXMLDOMDocument3 *doc;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    if (FAILED(hr = get_document( impl, &doc ))) return hr;
    hr = IXMLDOMDocument3_get_implementation( doc, &dom_impl );
    IXMLDOMDocument3_Release( doc );
    if (FAILED(hr)) return hr;
    hr = xml_dom_implementation_create( dom_impl, value );
    IXMLDOMImplementation_Release( dom_impl );
    return hr;
}

static HRESULT WINAPI xml_document_get_DocumentElement( IXmlDocument *iface, IXmlElement **value )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    IXMLDOMElement *element = NULL;
    IXMLDOMDocument3 *doc;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    if (FAILED(hr = get_document( impl, &doc ))) return hr;
    hr = IXMLDOMDocument3_get_documentElement( doc, &element );
    IXMLDOMDocument3_Release( doc );
    if (hr == S_FALSE) hr = S_OK;
    return return_node_as( (IXMLDOMNode *)element, hr, &IID_IXmlElement, (void **)value );
}

static HRESULT create_named( struct xml_node *impl, DOMNodeType type, HSTRING name, HSTRING data, REFIID iid, void **out )
{
    IXMLDOMDocument3 *doc;
    IXMLDOMNode *node = NULL;
    BSTR str = NULL, value = NULL;
    HRESULT hr;

    *out = NULL;
    if (FAILED(hr = get_document( impl, &doc ))) return hr;
    if (name && !(str = hstring_to_bstr( name ))) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && data && !(value = hstring_to_bstr( data ))) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        switch (type)
        {
        case NODE_ELEMENT:
            hr = IXMLDOMDocument3_createElement( doc, str, (IXMLDOMElement **)&node );
            break;
        case NODE_DOCUMENT_FRAGMENT:
            hr = IXMLDOMDocument3_createDocumentFragment( doc, (IXMLDOMDocumentFragment **)&node );
            break;
        case NODE_TEXT:
            hr = IXMLDOMDocument3_createTextNode( doc, value, (IXMLDOMText **)&node );
            break;
        case NODE_COMMENT:
            hr = IXMLDOMDocument3_createComment( doc, value, (IXMLDOMComment **)&node );
            break;
        case NODE_CDATA_SECTION:
            hr = IXMLDOMDocument3_createCDATASection( doc, value, (IXMLDOMCDATASection **)&node );
            break;
        case NODE_PROCESSING_INSTRUCTION:
            hr = IXMLDOMDocument3_createProcessingInstruction( doc, str, value, (IXMLDOMProcessingInstruction **)&node );
            break;
        case NODE_ATTRIBUTE:
            hr = IXMLDOMDocument3_createAttribute( doc, str, (IXMLDOMAttribute **)&node );
            break;
        case NODE_ENTITY_REFERENCE:
            hr = IXMLDOMDocument3_createEntityReference( doc, str, (IXMLDOMEntityReference **)&node );
            break;
        default:
            hr = E_INVALIDARG;
            break;
        }
    }
    SysFreeString( str );
    SysFreeString( value );
    IXMLDOMDocument3_Release( doc );
    return return_node_as( node, hr, iid, out );
}

static HRESULT WINAPI xml_document_CreateElement( IXmlDocument *iface, HSTRING tag_name, IXmlElement **new_element )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    TRACE( "iface %p, tag_name %s, new_element %p.\n", iface, debugstr_hstring(tag_name), new_element );
    return create_named( impl, NODE_ELEMENT, tag_name, NULL, &IID_IXmlElement, (void **)new_element );
}

static HRESULT WINAPI xml_document_CreateDocumentFragment( IXmlDocument *iface, IXmlDocumentFragment **new_document_fragment )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    TRACE( "iface %p, new_document_fragment %p.\n", iface, new_document_fragment );
    return create_named( impl, NODE_DOCUMENT_FRAGMENT, NULL, NULL, &IID_IXmlDocumentFragment, (void **)new_document_fragment );
}

static HRESULT WINAPI xml_document_CreateTextNode( IXmlDocument *iface, HSTRING data, IXmlText **new_text_node )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    TRACE( "iface %p, data %s, new_text_node %p.\n", iface, debugstr_hstring(data), new_text_node );
    return create_named( impl, NODE_TEXT, NULL, data, &IID_IXmlText, (void **)new_text_node );
}

static HRESULT WINAPI xml_document_CreateComment( IXmlDocument *iface, HSTRING data, IXmlComment **new_comment )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    TRACE( "iface %p, data %s, new_comment %p.\n", iface, debugstr_hstring(data), new_comment );
    return create_named( impl, NODE_COMMENT, NULL, data, &IID_IXmlComment, (void **)new_comment );
}

static HRESULT WINAPI xml_document_CreateProcessingInstruction( IXmlDocument *iface, HSTRING target, HSTRING data,
                                                                IXmlProcessingInstruction **new_processing_instruction )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    TRACE( "iface %p, target %s, data %s, new_processing_instruction %p.\n", iface, debugstr_hstring(target),
           debugstr_hstring(data), new_processing_instruction );
    return create_named( impl, NODE_PROCESSING_INSTRUCTION, target, data, &IID_IXmlProcessingInstruction,
                         (void **)new_processing_instruction );
}

static HRESULT WINAPI xml_document_CreateAttribute( IXmlDocument *iface, HSTRING name, IXmlAttribute **new_attribute )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    TRACE( "iface %p, name %s, new_attribute %p.\n", iface, debugstr_hstring(name), new_attribute );
    return create_named( impl, NODE_ATTRIBUTE, name, NULL, &IID_IXmlAttribute, (void **)new_attribute );
}

static HRESULT WINAPI xml_document_CreateEntityReference( IXmlDocument *iface, HSTRING name,
                                                          IXmlEntityReference **new_entity_reference )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    TRACE( "iface %p, name %s, new_entity_reference %p.\n", iface, debugstr_hstring(name), new_entity_reference );
    return create_named( impl, NODE_ENTITY_REFERENCE, name, NULL, &IID_IXmlEntityReference, (void **)new_entity_reference );
}

static HRESULT WINAPI xml_document_GetElementsByTagName( IXmlDocument *iface, HSTRING tag_name, IXmlNodeList **elements )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    TRACE( "iface %p, tag_name %s, elements %p.\n", iface, debugstr_hstring(tag_name), elements );
    return elements_by_tag_name( impl->node, tag_name, elements );
}

static HRESULT WINAPI xml_document_CreateCDataSection( IXmlDocument *iface, HSTRING data, IXmlCDataSection **new_cdata_section )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    TRACE( "iface %p, data %s, new_cdata_section %p.\n", iface, debugstr_hstring(data), new_cdata_section );
    return create_named( impl, NODE_CDATA_SECTION, NULL, data, &IID_IXmlCDataSection, (void **)new_cdata_section );
}

static HRESULT WINAPI xml_document_get_DocumentUri( IXmlDocument *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    IXMLDOMDocument3 *doc;
    BSTR str = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    *value = NULL;
    if (FAILED(hr = get_document( impl, &doc ))) return hr;
    hr = IXMLDOMDocument3_get_url( doc, &str );
    IXMLDOMDocument3_Release( doc );
    return return_bstr( str, hr, value );
}

static HRESULT WINAPI xml_document_CreateAttributeNS( IXmlDocument *iface, IInspectable *namespace_uri,
                                                      HSTRING qualified_name, IXmlAttribute **new_attribute )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    IXMLDOMNode *node;
    HRESULT hr;

    TRACE( "iface %p, namespace_uri %p, qualified_name %s, new_attribute %p.\n", iface, namespace_uri,
           debugstr_hstring(qualified_name), new_attribute );

    *new_attribute = NULL;
    hr = create_qualified_node( impl, NODE_ATTRIBUTE, namespace_uri, qualified_name, &node );
    return return_node_as( node, hr, &IID_IXmlAttribute, (void **)new_attribute );
}

static HRESULT WINAPI xml_document_CreateElementNS( IXmlDocument *iface, IInspectable *namespace_uri,
                                                    HSTRING qualified_name, IXmlElement **new_element )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    IXMLDOMNode *node;
    HRESULT hr;

    TRACE( "iface %p, namespace_uri %p, qualified_name %s, new_element %p.\n", iface, namespace_uri,
           debugstr_hstring(qualified_name), new_element );

    *new_element = NULL;
    hr = create_qualified_node( impl, NODE_ELEMENT, namespace_uri, qualified_name, &node );
    return return_node_as( node, hr, &IID_IXmlElement, (void **)new_element );
}

static HRESULT WINAPI xml_document_GetElementById( IXmlDocument *iface, HSTRING element_id, IXmlElement **element )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    IXMLDOMDocument3 *doc;
    IXMLDOMNode *node = NULL;
    BSTR id;
    HRESULT hr;

    TRACE( "iface %p, element_id %s, element %p.\n", iface, debugstr_hstring(element_id), element );

    *element = NULL;
    if (FAILED(hr = get_document( impl, &doc ))) return hr;
    if ((id = hstring_to_bstr( element_id ))) hr = IXMLDOMDocument3_nodeFromID( doc, id, &node );
    else hr = E_OUTOFMEMORY;
    SysFreeString( id );
    IXMLDOMDocument3_Release( doc );
    if (hr == S_FALSE) hr = S_OK;
    return return_node_as( node, hr, &IID_IXmlElement, (void **)element );
}

static HRESULT WINAPI xml_document_ImportNode( IXmlDocument *iface, IXmlNode *node, boolean deep, IXmlNode **new_node )
{
    struct xml_node *impl = impl_from_IXmlDocument( iface );
    IXMLDOMDocument3 *doc;
    IXMLDOMNode *source, *result = NULL;
    HRESULT hr;

    TRACE( "iface %p, node %p, deep %d, new_node %p.\n", iface, node, deep, new_node );

    *new_node = NULL;
    if (FAILED(hr = xml_node_unwrap( node, &source ))) return hr;
    if (SUCCEEDED(hr = get_document( impl, &doc )))
    {
        hr = IXMLDOMDocument3_importNode( doc, source, deep ? VARIANT_TRUE : VARIANT_FALSE, &result );
        IXMLDOMDocument3_Release( doc );
    }
    IXMLDOMNode_Release( source );
    return return_node( result, hr, new_node );
}

static const IXmlDocumentVtbl xml_document_vtbl =
{
    xml_document_QueryInterface,
    xml_document_AddRef,
    xml_document_Release,
    xml_document_GetIids,
    xml_document_GetRuntimeClassName,
    xml_document_GetTrustLevel,
    xml_document_get_Doctype,
    xml_document_get_Implementation,
    xml_document_get_DocumentElement,
    xml_document_CreateElement,
    xml_document_CreateDocumentFragment,
    xml_document_CreateTextNode,
    xml_document_CreateComment,
    xml_document_CreateProcessingInstruction,
    xml_document_CreateAttribute,
    xml_document_CreateEntityReference,
    xml_document_GetElementsByTagName,
    xml_document_CreateCDataSection,
    xml_document_get_DocumentUri,
    xml_document_CreateAttributeNS,
    xml_document_CreateElementNS,
    xml_document_GetElementById,
    xml_document_ImportNode,
};

static HRESULT load_result( IXMLDOMDocument3 *doc, VARIANT_BOOL success, HRESULT hr )
{
    IXMLDOMParseError *error;
    LONG code = 0;

    if (FAILED(hr)) return hr;
    if (success != VARIANT_FALSE) return S_OK;
    if (SUCCEEDED(IXMLDOMDocument3_get_parseError( doc, &error )))
    {
        IXMLDOMParseError_get_errorCode( error, &code );
        IXMLDOMParseError_Release( error );
    }
    return code ? code : E_FAIL;
}

static HRESULT load_xml( struct xml_node *impl, HSTRING xml, IXmlLoadSettings *settings )
{
    IXMLDOMDocument3 *doc;
    VARIANT_BOOL success = VARIANT_FALSE;
    BSTR str;
    HRESULT hr;

    if (FAILED(hr = get_document( impl, &doc ))) return hr;
    if (settings) hr = xml_load_settings_apply( settings, (IXMLDOMDocument2 *)doc );
    else xml_load_settings_apply_defaults( (IXMLDOMDocument2 *)doc );
    if (SUCCEEDED(hr))
    {
        if ((str = hstring_to_bstr( xml ))) hr = IXMLDOMDocument3_loadXML( doc, str, &success );
        else hr = E_OUTOFMEMORY;
        SysFreeString( str );
        hr = load_result( doc, success, hr );
    }
    IXMLDOMDocument3_Release( doc );
    return hr;
}

static HRESULT load_buffer( struct xml_node *impl, IBuffer *buffer, IXmlLoadSettings *settings )
{
    IBufferByteAccess *access;
    IXMLDOMDocument3 *doc;
    VARIANT_BOOL success = VARIANT_FALSE;
    SAFEARRAY *array;
    VARIANT source;
    UINT32 length = 0;
    BYTE *bytes, *data;
    HRESULT hr;

    if (!buffer) return E_INVALIDARG;
    if (FAILED(hr = IBuffer_get_Length( buffer, &length ))) return hr;
    if (FAILED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access ))) return hr;
    hr = IBufferByteAccess_Buffer( access, &bytes );
    IBufferByteAccess_Release( access );
    if (FAILED(hr)) return hr;

    if (!(array = SafeArrayCreateVector( VT_UI1, 0, length ))) return E_OUTOFMEMORY;
    if (SUCCEEDED(hr = SafeArrayAccessData( array, (void **)&data )))
    {
        memcpy( data, bytes, length );
        SafeArrayUnaccessData( array );
    }
    V_VT(&source) = VT_ARRAY | VT_UI1;
    V_ARRAY(&source) = array;

    if (SUCCEEDED(hr) && SUCCEEDED(hr = get_document( impl, &doc )))
    {
        if (settings) hr = xml_load_settings_apply( settings, (IXMLDOMDocument2 *)doc );
        else xml_load_settings_apply_defaults( (IXMLDOMDocument2 *)doc );
        if (SUCCEEDED(hr))
        {
            hr = IXMLDOMDocument3_load( doc, source, &success );
            hr = load_result( doc, success, hr );
        }
        IXMLDOMDocument3_Release( doc );
    }
    VariantClear( &source );
    return hr;
}

DEFINE_IINSPECTABLE( xml_document_io, IXmlDocumentIO, struct xml_node, IXmlNode_iface )

static HRESULT WINAPI xml_document_io_LoadXml( IXmlDocumentIO *iface, HSTRING xml )
{
    struct xml_node *impl = impl_from_IXmlDocumentIO( iface );
    TRACE( "iface %p, xml %s.\n", iface, debugstr_hstring(xml) );
    return load_xml( impl, xml, NULL );
}

static HRESULT WINAPI xml_document_io_LoadXmlWithSettings( IXmlDocumentIO *iface, HSTRING xml, IXmlLoadSettings *load_settings )
{
    struct xml_node *impl = impl_from_IXmlDocumentIO( iface );
    TRACE( "iface %p, xml %s, load_settings %p.\n", iface, debugstr_hstring(xml), load_settings );
    return load_xml( impl, xml, load_settings );
}

static HRESULT WINAPI xml_document_io_SaveToFileAsync( IXmlDocumentIO *iface, IStorageFile *file, IAsyncAction **async_info )
{
    FIXME( "iface %p, file %p, async_info %p stub!\n", iface, file, async_info );
    return E_NOTIMPL;
}

static const IXmlDocumentIOVtbl xml_document_io_vtbl =
{
    xml_document_io_QueryInterface,
    xml_document_io_AddRef,
    xml_document_io_Release,
    xml_document_io_GetIids,
    xml_document_io_GetRuntimeClassName,
    xml_document_io_GetTrustLevel,
    xml_document_io_LoadXml,
    xml_document_io_LoadXmlWithSettings,
    xml_document_io_SaveToFileAsync,
};

DEFINE_IINSPECTABLE( xml_document_io2, IXmlDocumentIO2, struct xml_node, IXmlNode_iface )

static HRESULT WINAPI xml_document_io2_LoadXmlFromBuffer( IXmlDocumentIO2 *iface, IBuffer *buffer )
{
    struct xml_node *impl = impl_from_IXmlDocumentIO2( iface );
    TRACE( "iface %p, buffer %p.\n", iface, buffer );
    return load_buffer( impl, buffer, NULL );
}

static HRESULT WINAPI xml_document_io2_LoadXmlFromBufferWithSettings( IXmlDocumentIO2 *iface, IBuffer *buffer,
                                                                      IXmlLoadSettings *load_settings )
{
    struct xml_node *impl = impl_from_IXmlDocumentIO2( iface );
    TRACE( "iface %p, buffer %p, load_settings %p.\n", iface, buffer, load_settings );
    return load_buffer( impl, buffer, load_settings );
}

static const IXmlDocumentIO2Vtbl xml_document_io2_vtbl =
{
    xml_document_io2_QueryInterface,
    xml_document_io2_AddRef,
    xml_document_io2_Release,
    xml_document_io2_GetIids,
    xml_document_io2_GetRuntimeClassName,
    xml_document_io2_GetTrustLevel,
    xml_document_io2_LoadXmlFromBuffer,
    xml_document_io2_LoadXmlFromBufferWithSettings,
};

HRESULT xml_node_create( IXMLDOMNode *node, IXmlNode **out )
{
    struct xml_node *impl;
    DOMNodeType type;
    HRESULT hr;

    *out = NULL;
    if (FAILED(hr = IXMLDOMNode_get_nodeType( node, &type ))) return hr;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IXmlNode_iface.lpVtbl = &xml_node_vtbl;
    impl->IXmlNodeSelector_iface.lpVtbl = &xml_node_selector_vtbl;
    impl->IXmlNodeSerializer_iface.lpVtbl = &xml_node_serializer_vtbl;
    impl->IXmlElement_iface.lpVtbl = &xml_element_vtbl;
    impl->IXmlAttribute_iface.lpVtbl = &xml_attribute_vtbl;
    impl->IXmlCharacterData_iface.lpVtbl = &xml_character_data_vtbl;
    impl->IXmlText_iface.lpVtbl = &xml_text_vtbl;
    impl->IXmlComment_iface.lpVtbl = &xml_comment_vtbl;
    impl->IXmlCDataSection_iface.lpVtbl = &xml_cdata_section_vtbl;
    impl->IXmlProcessingInstruction_iface.lpVtbl = &xml_processing_instruction_vtbl;
    impl->IXmlDocumentFragment_iface.lpVtbl = &xml_document_fragment_vtbl;
    impl->IXmlEntityReference_iface.lpVtbl = &xml_entity_reference_vtbl;
    impl->IXmlDocumentType_iface.lpVtbl = &xml_document_type_vtbl;
    impl->IDtdEntity_iface.lpVtbl = &dtd_entity_vtbl;
    impl->IDtdNotation_iface.lpVtbl = &dtd_notation_vtbl;
    impl->IXmlDocument_iface.lpVtbl = &xml_document_vtbl;
    impl->IXmlDocumentIO_iface.lpVtbl = &xml_document_io_vtbl;
    impl->IXmlDocumentIO2_iface.lpVtbl = &xml_document_io2_vtbl;
    impl->ref = 1;
    impl->type = type;
    impl->node = node;
    IXMLDOMNode_AddRef( node );

    *out = &impl->IXmlNode_iface;
    return S_OK;
}

HRESULT xml_node_unwrap( IXmlNode *iface, IXMLDOMNode **out )
{
    struct xml_node *impl;

    *out = NULL;
    if (!iface) return E_INVALIDARG;
    if (iface->lpVtbl != &xml_node_vtbl)
    {
        IXmlNode *node;
        HRESULT hr;

        if (FAILED(hr = IXmlNode_QueryInterface( iface, &IID_IXmlNode, (void **)&node ))) return hr;
        if (node->lpVtbl != &xml_node_vtbl)
        {
            IXmlNode_Release( node );
            return E_INVALIDARG;
        }
        impl = impl_from_IXmlNode( node );
        IXMLDOMNode_AddRef( (*out = impl->node) );
        IXmlNode_Release( node );
        return S_OK;
    }
    impl = impl_from_IXmlNode( iface );
    IXMLDOMNode_AddRef( (*out = impl->node) );
    return S_OK;
}
