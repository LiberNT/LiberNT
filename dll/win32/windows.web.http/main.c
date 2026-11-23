/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.Web.Http activation and shared helpers
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "initguid.h"
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(webhttp);

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );
    IActivationFactory *found = NULL;

    TRACE( "class %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;
    if (!wcscmp( buffer, RuntimeClass_Windows_Web_Http_HttpClient )) found = http_client_factory;
    else if (!wcscmp( buffer, RuntimeClass_Windows_Web_Http_HttpMethod )) found = http_method_factory;
    else if (!wcscmp( buffer, RuntimeClass_Windows_Web_Http_HttpRequestMessage )) found = http_request_message_factory;
    else if (!wcscmp( buffer, RuntimeClass_Windows_Web_Http_Filters_HttpBaseProtocolFilter ))
        found = http_base_protocol_filter_factory;
    else if (!wcscmp( buffer, RuntimeClass_Windows_Web_Http_Headers_HttpDateOrDeltaHeaderValue ))
        found = http_date_or_delta_factory;

    if (!found) return CLASS_E_CLASSNOTAVAILABLE;
    IActivationFactory_AddRef( (*factory = found) );
    return S_OK;
}

static HRESULT get_property_value_statics( IPropertyValueStatics **statics )
{
    static const WCHAR name[] = L"Windows.Foundation.PropertyValue";
    HSTRING class_name;
    HRESULT hr;

    if (FAILED(hr = WindowsCreateString( name, ARRAY_SIZE(name) - 1, &class_name ))) return hr;
    hr = RoGetActivationFactory( class_name, &IID_IPropertyValueStatics, (void **)statics );
    WindowsDeleteString( class_name );
    return hr;
}

HRESULT box_uint64( UINT64 value, IInspectable **out )
{
    IPropertyValueStatics *statics;
    HRESULT hr;

    if (FAILED(hr = get_property_value_statics( &statics ))) return hr;
    hr = IPropertyValueStatics_CreateUInt64( statics, value, out );
    IPropertyValueStatics_Release( statics );
    return hr;
}

HRESULT box_date_time( DateTime value, IInspectable **out )
{
    IPropertyValueStatics *statics;
    HRESULT hr;

    if (FAILED(hr = get_property_value_statics( &statics ))) return hr;
    hr = IPropertyValueStatics_CreateDateTime( statics, value, out );
    IPropertyValueStatics_Release( statics );
    return hr;
}

HRESULT box_time_span( TimeSpan value, IInspectable **out )
{
    IPropertyValueStatics *statics;
    HRESULT hr;

    if (FAILED(hr = get_property_value_statics( &statics ))) return hr;
    hr = IPropertyValueStatics_CreateTimeSpan( statics, value, out );
    IPropertyValueStatics_Release( statics );
    return hr;
}

HRESULT create_uri( const WCHAR *text, IUriRuntimeClass **out )
{
    static const WCHAR name[] = L"Windows.Foundation.Uri";
    IUriRuntimeClassFactory *factory;
    HSTRING class_name, string;
    HRESULT hr;

    *out = NULL;
    if (FAILED(hr = WindowsCreateString( name, ARRAY_SIZE(name) - 1, &class_name ))) return hr;
    hr = RoGetActivationFactory( class_name, &IID_IUriRuntimeClassFactory, (void **)&factory );
    WindowsDeleteString( class_name );
    if (FAILED(hr)) return hr;
    if (SUCCEEDED(hr = WindowsCreateString( text, wcslen( text ), &string )))
    {
        hr = IUriRuntimeClassFactory_CreateUri( factory, string, out );
        WindowsDeleteString( string );
    }
    IUriRuntimeClassFactory_Release( factory );
    return hr;
}
