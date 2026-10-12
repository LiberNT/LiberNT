/*
 * PROJECT:     LiberNT MSXML 6.0
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     String and value conversions for the Windows.Data.Xml.Dom classes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "xmldom_private.h"

#include "wine/debug.h"

HRESULT bstr_to_hstring( BSTR str, HSTRING *out )
{
    return WindowsCreateString( str, SysStringLen( str ), out );
}

BSTR hstring_to_bstr( HSTRING str )
{
    UINT32 len;
    const WCHAR *buffer = WindowsGetStringRawBuffer( str, &len );
    return SysAllocStringLen( buffer, len );
}

HRESULT box_bstr( BSTR str, IInspectable **out )
{
    static const WCHAR class_name[] = L"Windows.Foundation.PropertyValue";
    IPropertyValueStatics *statics;
    HSTRING_HEADER header;
    HSTRING name, value;
    HRESULT hr;

    *out = NULL;
    if (FAILED(hr = WindowsCreateStringReference( class_name, ARRAY_SIZE(class_name) - 1, &header, &name )))
        return hr;
    if (FAILED(hr = RoGetActivationFactory( name, &IID_IPropertyValueStatics, (void **)&statics )))
        return hr;
    if (SUCCEEDED(hr = bstr_to_hstring( str, &value )))
    {
        hr = IPropertyValueStatics_CreateString( statics, value, out );
        WindowsDeleteString( value );
    }
    IPropertyValueStatics_Release( statics );
    return hr;
}

HRESULT unbox_bstr( IInspectable *value, BSTR *out )
{
    IPropertyValue *property;
    HSTRING str;
    HRESULT hr;

    *out = NULL;
    if (!value) return S_OK;
    if (FAILED(hr = IInspectable_QueryInterface( value, &IID_IPropertyValue, (void **)&property )))
        return E_INVALIDARG;
    hr = IPropertyValue_GetString( property, &str );
    IPropertyValue_Release( property );
    if (FAILED(hr)) return hr;
    *out = hstring_to_bstr( str );
    WindowsDeleteString( str );
    return *out ? S_OK : E_OUTOFMEMORY;
}
