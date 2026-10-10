/*
 * PROJECT:     LiberNT MSXML 6.0
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows.Data.Xml.Dom runtime classes over MSXML
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef __MSXML6_XMLDOM_PRIVATE_H
#define __MSXML6_XMLDOM_PRIVATE_H

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "objbase.h"
#include "msxml6.h"

#include "activation.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Storage_Streams
#define WIDL_using_Windows_Data_Xml_Dom
#include "windows.foundation.h"
#include "windows.storage.streams.h"
#include "windows.storage.h"
#include "robuffer.h"
#include "windows.data.xml.dom.h"

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

extern IActivationFactory *xml_document_factory;
extern IActivationFactory *xml_load_settings_factory;

HRESULT xml_node_create( IXMLDOMNode *node, IXmlNode **out );
HRESULT xml_node_unwrap( IXmlNode *iface, IXMLDOMNode **out );
HRESULT xml_node_list_create( IXMLDOMNodeList *list, IXmlNodeList **out );
HRESULT xml_named_node_map_create( IXMLDOMNamedNodeMap *map, IXmlNamedNodeMap **out );
HRESULT xml_dom_implementation_create( IXMLDOMImplementation *impl, IXmlDomImplementation **out );
HRESULT xml_load_settings_apply( IXmlLoadSettings *settings, IXMLDOMDocument2 *doc );
void xml_load_settings_apply_defaults( IXMLDOMDocument2 *doc );

HRESULT bstr_to_hstring( BSTR str, HSTRING *out );
BSTR hstring_to_bstr( HSTRING str );
HRESULT box_bstr( BSTR str, IInspectable **out );
HRESULT unbox_bstr( IInspectable *value, BSTR *out );

#endif
