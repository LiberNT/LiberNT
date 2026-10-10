/*
 * PROJECT:     LiberNT MSXML 6.0
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows.Data.Xml.Dom activation factories, load settings and DOM implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "initguid.h"
#include "xmldom_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(msxml);

#define DEFAULT_MAX_ELEMENT_DEPTH 256

struct xml_load_settings
{
    IXmlLoadSettings IXmlLoadSettings_iface;
    LONG ref;

    UINT32 max_element_depth;
    boolean prohibit_dtd;
    boolean resolve_externals;
    boolean validate_on_parse;
    boolean element_content_white_space;
};

static inline struct xml_load_settings *impl_from_IXmlLoadSettings( IXmlLoadSettings *iface )
{
    return CONTAINING_RECORD( iface, struct xml_load_settings, IXmlLoadSettings_iface );
}

static HRESULT WINAPI load_settings_QueryInterface( IXmlLoadSettings *iface, REFIID iid, void **out )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IXmlLoadSettings ))
    {
        IInspectable_AddRef( (*out = &impl->IXmlLoadSettings_iface) );
        return S_OK;
    }

    *out = NULL;
    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid) );
    return E_NOINTERFACE;
}

static ULONG WINAPI load_settings_AddRef( IXmlLoadSettings *iface )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI load_settings_Release( IXmlLoadSettings *iface )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI load_settings_GetIids( IXmlLoadSettings *iface, ULONG *iid_count, IID **iids )
{
    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(IID) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IXmlLoadSettings;
    (*iids)[1] = IID_IAgileObject;
    *iid_count = 2;
    return S_OK;
}

static HRESULT WINAPI load_settings_GetRuntimeClassName( IXmlLoadSettings *iface, HSTRING *class_name )
{
    TRACE( "iface %p, class_name %p.\n", iface, class_name );
    return WindowsCreateString( RuntimeClass_Windows_Data_Xml_Dom_XmlLoadSettings,
                                wcslen( RuntimeClass_Windows_Data_Xml_Dom_XmlLoadSettings ), class_name );
}

static HRESULT WINAPI load_settings_GetTrustLevel( IXmlLoadSettings *iface, TrustLevel *trust_level )
{
    TRACE( "iface %p, trust_level %p.\n", iface, trust_level );
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI load_settings_get_MaxElementDepth( IXmlLoadSettings *iface, UINT32 *value )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    *value = impl->max_element_depth;
    return S_OK;
}

static HRESULT WINAPI load_settings_put_MaxElementDepth( IXmlLoadSettings *iface, UINT32 value )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    TRACE( "iface %p, value %u.\n", iface, value );
    impl->max_element_depth = value;
    return S_OK;
}

static HRESULT WINAPI load_settings_get_ProhibitDtd( IXmlLoadSettings *iface, boolean *value )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    *value = impl->prohibit_dtd;
    return S_OK;
}

static HRESULT WINAPI load_settings_put_ProhibitDtd( IXmlLoadSettings *iface, boolean value )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    TRACE( "iface %p, value %d.\n", iface, value );
    impl->prohibit_dtd = !!value;
    return S_OK;
}

static HRESULT WINAPI load_settings_get_ResolveExternals( IXmlLoadSettings *iface, boolean *value )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    *value = impl->resolve_externals;
    return S_OK;
}

static HRESULT WINAPI load_settings_put_ResolveExternals( IXmlLoadSettings *iface, boolean value )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    TRACE( "iface %p, value %d.\n", iface, value );
    impl->resolve_externals = !!value;
    return S_OK;
}

static HRESULT WINAPI load_settings_get_ValidateOnParse( IXmlLoadSettings *iface, boolean *value )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    *value = impl->validate_on_parse;
    return S_OK;
}

static HRESULT WINAPI load_settings_put_ValidateOnParse( IXmlLoadSettings *iface, boolean value )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    TRACE( "iface %p, value %d.\n", iface, value );
    impl->validate_on_parse = !!value;
    return S_OK;
}

static HRESULT WINAPI load_settings_get_ElementContentWhiteSpace( IXmlLoadSettings *iface, boolean *value )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    *value = impl->element_content_white_space;
    return S_OK;
}

static HRESULT WINAPI load_settings_put_ElementContentWhiteSpace( IXmlLoadSettings *iface, boolean value )
{
    struct xml_load_settings *impl = impl_from_IXmlLoadSettings( iface );
    TRACE( "iface %p, value %d.\n", iface, value );
    impl->element_content_white_space = !!value;
    return S_OK;
}

static const IXmlLoadSettingsVtbl load_settings_vtbl =
{
    load_settings_QueryInterface,
    load_settings_AddRef,
    load_settings_Release,
    load_settings_GetIids,
    load_settings_GetRuntimeClassName,
    load_settings_GetTrustLevel,
    load_settings_get_MaxElementDepth,
    load_settings_put_MaxElementDepth,
    load_settings_get_ProhibitDtd,
    load_settings_put_ProhibitDtd,
    load_settings_get_ResolveExternals,
    load_settings_put_ResolveExternals,
    load_settings_get_ValidateOnParse,
    load_settings_put_ValidateOnParse,
    load_settings_get_ElementContentWhiteSpace,
    load_settings_put_ElementContentWhiteSpace,
};

static HRESULT apply_settings( IXMLDOMDocument2 *doc, UINT32 depth, boolean prohibit_dtd, boolean resolve_externals,
                               boolean validate_on_parse, boolean white_space )
{
    VARIANT value;
    HRESULT hr;

    V_VT(&value) = VT_BOOL;
    V_BOOL(&value) = prohibit_dtd ? VARIANT_TRUE : VARIANT_FALSE;
    if (FAILED(hr = IXMLDOMDocument2_setProperty( doc, (BSTR)L"ProhibitDTD", value ))) return hr;
    V_VT(&value) = VT_I4;
    V_I4(&value) = depth;
    if (FAILED(hr = IXMLDOMDocument2_setProperty( doc, (BSTR)L"MaxElementDepth", value ))) return hr;
    if (FAILED(hr = IXMLDOMDocument2_put_resolveExternals( doc, resolve_externals ? VARIANT_TRUE : VARIANT_FALSE ))) return hr;
    if (FAILED(hr = IXMLDOMDocument2_put_validateOnParse( doc, validate_on_parse ? VARIANT_TRUE : VARIANT_FALSE ))) return hr;
    return IXMLDOMDocument2_put_preserveWhiteSpace( doc, white_space ? VARIANT_TRUE : VARIANT_FALSE );
}

void xml_load_settings_apply_defaults( IXMLDOMDocument2 *doc )
{
    apply_settings( doc, DEFAULT_MAX_ELEMENT_DEPTH, TRUE, FALSE, FALSE, TRUE );
}

HRESULT xml_load_settings_apply( IXmlLoadSettings *settings, IXMLDOMDocument2 *doc )
{
    boolean prohibit_dtd, resolve_externals, validate_on_parse, white_space;
    UINT32 depth;
    HRESULT hr;

    if (FAILED(hr = IXmlLoadSettings_get_MaxElementDepth( settings, &depth ))) return hr;
    if (FAILED(hr = IXmlLoadSettings_get_ProhibitDtd( settings, &prohibit_dtd ))) return hr;
    if (FAILED(hr = IXmlLoadSettings_get_ResolveExternals( settings, &resolve_externals ))) return hr;
    if (FAILED(hr = IXmlLoadSettings_get_ValidateOnParse( settings, &validate_on_parse ))) return hr;
    if (FAILED(hr = IXmlLoadSettings_get_ElementContentWhiteSpace( settings, &white_space ))) return hr;
    return apply_settings( doc, depth, prohibit_dtd, resolve_externals, validate_on_parse, white_space );
}

struct dom_implementation
{
    IXmlDomImplementation IXmlDomImplementation_iface;
    LONG ref;

    IXMLDOMImplementation *impl;
};

static inline struct dom_implementation *impl_from_IXmlDomImplementation( IXmlDomImplementation *iface )
{
    return CONTAINING_RECORD( iface, struct dom_implementation, IXmlDomImplementation_iface );
}

static HRESULT WINAPI dom_implementation_QueryInterface( IXmlDomImplementation *iface, REFIID iid, void **out )
{
    struct dom_implementation *impl = impl_from_IXmlDomImplementation( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IXmlDomImplementation ))
    {
        IInspectable_AddRef( (*out = &impl->IXmlDomImplementation_iface) );
        return S_OK;
    }

    *out = NULL;
    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid) );
    return E_NOINTERFACE;
}

static ULONG WINAPI dom_implementation_AddRef( IXmlDomImplementation *iface )
{
    struct dom_implementation *impl = impl_from_IXmlDomImplementation( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI dom_implementation_Release( IXmlDomImplementation *iface )
{
    struct dom_implementation *impl = impl_from_IXmlDomImplementation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        IXMLDOMImplementation_Release( impl->impl );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI dom_implementation_GetIids( IXmlDomImplementation *iface, ULONG *iid_count, IID **iids )
{
    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (!(*iids = CoTaskMemAlloc( sizeof(IID) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IXmlDomImplementation;
    *iid_count = 1;
    return S_OK;
}

static HRESULT WINAPI dom_implementation_GetRuntimeClassName( IXmlDomImplementation *iface, HSTRING *class_name )
{
    TRACE( "iface %p, class_name %p.\n", iface, class_name );
    return WindowsCreateString( RuntimeClass_Windows_Data_Xml_Dom_XmlDomImplementation,
                                wcslen( RuntimeClass_Windows_Data_Xml_Dom_XmlDomImplementation ), class_name );
}

static HRESULT WINAPI dom_implementation_GetTrustLevel( IXmlDomImplementation *iface, TrustLevel *trust_level )
{
    TRACE( "iface %p, trust_level %p.\n", iface, trust_level );
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI dom_implementation_HasFeature( IXmlDomImplementation *iface, HSTRING feature, IInspectable *version,
                                                     boolean *feature_supported )
{
    struct dom_implementation *impl = impl_from_IXmlDomImplementation( iface );
    VARIANT_BOOL supported = VARIANT_FALSE;
    BSTR name, ver;
    HRESULT hr;

    TRACE( "iface %p, feature %s, version %p, feature_supported %p.\n", iface, debugstr_hstring(feature), version,
           feature_supported );

    *feature_supported = FALSE;
    if (FAILED(hr = unbox_bstr( version, &ver ))) return hr;
    if ((name = hstring_to_bstr( feature )))
        hr = IXMLDOMImplementation_hasFeature( impl->impl, name, ver ? ver : (BSTR)L"", &supported );
    else
        hr = E_OUTOFMEMORY;
    SysFreeString( name );
    SysFreeString( ver );
    *feature_supported = supported != VARIANT_FALSE;
    return FAILED(hr) ? hr : S_OK;
}

static const IXmlDomImplementationVtbl dom_implementation_vtbl =
{
    dom_implementation_QueryInterface,
    dom_implementation_AddRef,
    dom_implementation_Release,
    dom_implementation_GetIids,
    dom_implementation_GetRuntimeClassName,
    dom_implementation_GetTrustLevel,
    dom_implementation_HasFeature,
};

HRESULT xml_dom_implementation_create( IXMLDOMImplementation *dom_impl, IXmlDomImplementation **out )
{
    struct dom_implementation *impl;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IXmlDomImplementation_iface.lpVtbl = &dom_implementation_vtbl;
    impl->ref = 1;
    IXMLDOMImplementation_AddRef( (impl->impl = dom_impl) );
    *out = &impl->IXmlDomImplementation_iface;
    return S_OK;
}

struct factory
{
    IActivationFactory IActivationFactory_iface;
    IXmlDocumentStatics IXmlDocumentStatics_iface;
    LONG ref;
    BOOL document;
};

static inline struct factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct factory, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IInspectable_AddRef( (*out = &impl->IActivationFactory_iface) );
        return S_OK;
    }
    if (impl->document && IsEqualGUID( iid, &IID_IXmlDocumentStatics ))
    {
        IInspectable_AddRef( (*out = &impl->IXmlDocumentStatics_iface) );
        return S_OK;
    }

    *out = NULL;
    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid) );
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT create_document( IInspectable **instance )
{
    IXMLDOMDocument3 *doc;
    IXmlNode *node;
    HRESULT hr;

    *instance = NULL;
    if (FAILED(hr = CoCreateInstance( &CLSID_DOMDocument60, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument3, (void **)&doc )))
        return hr;
    xml_load_settings_apply_defaults( (IXMLDOMDocument2 *)doc );
    hr = xml_node_create( (IXMLDOMNode *)doc, &node );
    IXMLDOMDocument3_Release( doc );
    if (FAILED(hr)) return hr;
    hr = IXmlNode_QueryInterface( node, &IID_IXmlDocument, (void **)instance );
    IXmlNode_Release( node );
    return hr;
}

static HRESULT create_load_settings( IInspectable **instance )
{
    struct xml_load_settings *impl;

    *instance = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IXmlLoadSettings_iface.lpVtbl = &load_settings_vtbl;
    impl->ref = 1;
    impl->max_element_depth = DEFAULT_MAX_ELEMENT_DEPTH;
    impl->prohibit_dtd = TRUE;
    impl->resolve_externals = FALSE;
    impl->validate_on_parse = FALSE;
    impl->element_content_white_space = TRUE;
    *instance = (IInspectable *)&impl->IXmlLoadSettings_iface;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, instance %p.\n", iface, instance );

    return impl->document ? create_document( instance ) : create_load_settings( instance );
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( document_statics, IXmlDocumentStatics, struct factory, IActivationFactory_iface )

static HRESULT WINAPI document_statics_LoadFromUriAsync( IXmlDocumentStatics *iface, IUriRuntimeClass *uri,
                                                         IAsyncOperation_XmlDocument **async_info )
{
    FIXME( "iface %p, uri %p, async_info %p stub!\n", iface, uri, async_info );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_statics_LoadFromUriWithSettingsAsync( IXmlDocumentStatics *iface, IUriRuntimeClass *uri,
                                                                     IXmlLoadSettings *load_settings,
                                                                     IAsyncOperation_XmlDocument **async_info )
{
    FIXME( "iface %p, uri %p, load_settings %p, async_info %p stub!\n", iface, uri, load_settings, async_info );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_statics_LoadFromFileAsync( IXmlDocumentStatics *iface, IStorageFile *file,
                                                          IAsyncOperation_XmlDocument **async_info )
{
    FIXME( "iface %p, file %p, async_info %p stub!\n", iface, file, async_info );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_statics_LoadFromFileWithSettingsAsync( IXmlDocumentStatics *iface, IStorageFile *file,
                                                                      IXmlLoadSettings *load_settings,
                                                                      IAsyncOperation_XmlDocument **async_info )
{
    FIXME( "iface %p, file %p, load_settings %p, async_info %p stub!\n", iface, file, load_settings, async_info );
    return E_NOTIMPL;
}

static const IXmlDocumentStaticsVtbl document_statics_vtbl =
{
    document_statics_QueryInterface,
    document_statics_AddRef,
    document_statics_Release,
    document_statics_GetIids,
    document_statics_GetRuntimeClassName,
    document_statics_GetTrustLevel,
    document_statics_LoadFromUriAsync,
    document_statics_LoadFromUriWithSettingsAsync,
    document_statics_LoadFromFileAsync,
    document_statics_LoadFromFileWithSettingsAsync,
};

static struct factory document_factory =
{
    {&factory_vtbl},
    {&document_statics_vtbl},
    1,
    TRUE,
};

static struct factory load_settings_factory =
{
    {&factory_vtbl},
    {&document_statics_vtbl},
    1,
    FALSE,
};

IActivationFactory *xml_document_factory = &document_factory.IActivationFactory_iface;
IActivationFactory *xml_load_settings_factory = &load_settings_factory.IActivationFactory_iface;

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring(classid), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_Data_Xml_Dom_XmlDocument ))
        IActivationFactory_QueryInterface( xml_document_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_Data_Xml_Dom_XmlLoadSettings ))
        IActivationFactory_QueryInterface( xml_load_settings_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return CLASS_E_CLASSNOTAVAILABLE;
}
