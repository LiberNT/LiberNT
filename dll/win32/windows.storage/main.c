/* WinRT Windows.Storage Implementation
 *
 * Copyright (C) 2025 Mohamad Al-Jaf
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "initguid.h"
#include "private.h"
#ifdef __REACTOS__
#include "rpcproxy.h"
#include "objects.h"
#endif

WINE_DEFAULT_DEBUG_CHANNEL(storage);

#ifdef __REACTOS__
BOOL WINAPI DllMain( HINSTANCE instance, DWORD reason, void *reserved )
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls( instance );
        WSTORAGE_InitObjects( instance );
        break;
    case DLL_PROCESS_DETACH:
        if (reserved) break;
        WSTORAGE_TermObjects();
        break;
    }
    return TRUE;
}
#endif

#ifdef __REACTOS__
HRESULT WINAPI DllRegisterServer( void )
{
    HRESULT hr = __wine_register_resources();
    if (FAILED(hr)) return hr;
    return WSTORAGE_UpdateRegistry( TRUE );
}

HRESULT WINAPI DllUnregisterServer( void )
{
    HRESULT hr = WSTORAGE_UpdateRegistry( FALSE );
    if (FAILED(hr)) return hr;
    return __wine_unregister_resources();
}
#endif

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
#ifdef __REACTOS__
    HRESULT hr = WSTORAGE_GetObjectsClassObject( clsid, riid, out );
    if (hr != CLASS_E_CLASSNOTAVAILABLE) return hr;
#endif
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_Storage_Streams_RandomAccessStreamReference ))
        IActivationFactory_QueryInterface( random_access_stream_reference_factory, &IID_IActivationFactory, (void **)factory );
    if (!wcscmp( buffer, RuntimeClass_Windows_Storage_Streams_InMemoryRandomAccessStream ))
        IActivationFactory_QueryInterface( memory_stream_activation_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return CLASS_E_CLASSNOTAVAILABLE;
}
