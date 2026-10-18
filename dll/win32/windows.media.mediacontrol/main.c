/* WinRT Windows.Media.MediaControl Implementation
 *
 * Copyright (C) 2023 Mohamad Al-Jaf
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

#include "wine/debug.h"
#ifdef __REACTOS__
#include "winuser.h"
#include "wine/list.h"

#define MEDIA_SESSION_CLASS L"LiberNTMediaSession"
#define MEDIA_SESSION_STATUS L"LiberNTMediaStatus"
#define MEDIA_SESSION_TICK L"LiberNTMediaTick"
#endif

WINE_DEFAULT_DEBUG_CHANNEL(mediacontrol);

#ifndef __REACTOS__
static EventRegistrationToken dummy_token = {.value = 0xdeadbeef};
#endif

struct media_control_statics
{
    IActivationFactory IActivationFactory_iface;
    ISystemMediaTransportControlsInterop ISystemMediaTransportControlsInterop_iface;
    LONG ref;
};

static inline struct media_control_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct media_control_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct media_control_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ISystemMediaTransportControlsInterop ))
    {
        *out = &impl->ISystemMediaTransportControlsInterop_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct media_control_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct media_control_statics *impl = impl_from_IActivationFactory( iface );
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

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

struct music_properties
{
    IMusicDisplayProperties IMusicDisplayProperties_iface;
    IMusicDisplayProperties2 IMusicDisplayProperties2_iface;
    LONG ref;

    HSTRING album_title;
    HSTRING artist;
    HSTRING title;
};

static inline struct music_properties *impl_from_IMusicDisplayProperties( IMusicDisplayProperties *iface )
{
    return CONTAINING_RECORD( iface, struct music_properties, IMusicDisplayProperties_iface );
}

static HRESULT WINAPI music_properties_QueryInterface( IMusicDisplayProperties *iface, REFIID iid, void **out )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IMusicDisplayProperties ))
    {
        *out = &impl->IMusicDisplayProperties_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IMusicDisplayProperties2 ))
    {
        *out = &impl->IMusicDisplayProperties2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI music_properties_AddRef( IMusicDisplayProperties *iface )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI music_properties_Release( IMusicDisplayProperties *iface )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        WindowsDeleteString( impl->album_title );
        WindowsDeleteString( impl->artist );
        WindowsDeleteString( impl->title );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI music_properties_GetIids( IMusicDisplayProperties *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI music_properties_GetRuntimeClassName( IMusicDisplayProperties *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI music_properties_GetTrustLevel( IMusicDisplayProperties *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI music_properties_get_Title( IMusicDisplayProperties *iface, HSTRING *value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    return WindowsDuplicateString( impl->title, value );
}

static HRESULT WINAPI music_properties_put_Title( IMusicDisplayProperties *iface, HSTRING value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    WindowsDeleteString( impl->title );
    return WindowsDuplicateString( value, &impl->title );
}

static HRESULT WINAPI music_properties_get_AlbumArtist( IMusicDisplayProperties *iface, HSTRING *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI music_properties_put_AlbumArtist( IMusicDisplayProperties *iface, HSTRING value )
{
    FIXME( "iface %p, value %s stub!\n", iface, debugstr_hstring(value) );
    return E_NOTIMPL;
}

static HRESULT WINAPI music_properties_get_Artist( IMusicDisplayProperties *iface, HSTRING *value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    return WindowsDuplicateString( impl->artist, value );
}

static HRESULT WINAPI music_properties_put_Artist( IMusicDisplayProperties *iface, HSTRING value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    WindowsDeleteString( impl->artist );
    return WindowsDuplicateString( value, &impl->artist );
}

static const IMusicDisplayPropertiesVtbl music_properties_vtbl =
{
    music_properties_QueryInterface,
    music_properties_AddRef,
    music_properties_Release,
    /* IInspectable methods */
    music_properties_GetIids,
    music_properties_GetRuntimeClassName,
    music_properties_GetTrustLevel,
    /* IMusicDisplayProperties methods */
    music_properties_get_Title,
    music_properties_put_Title,
    music_properties_get_AlbumArtist,
    music_properties_put_AlbumArtist,
    music_properties_get_Artist,
    music_properties_put_Artist,
};

DEFINE_IINSPECTABLE( music_properties2, IMusicDisplayProperties2, struct music_properties, IMusicDisplayProperties_iface )

static HRESULT STDMETHODCALLTYPE music_properties2_get_AlbumTitle( IMusicDisplayProperties2 *iface, HSTRING *value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties2( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    return WindowsDuplicateString( impl->album_title, value );
}

static HRESULT STDMETHODCALLTYPE music_properties2_put_AlbumTitle( IMusicDisplayProperties2 *iface, HSTRING value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties2( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    WindowsDeleteString( impl->album_title );
    return WindowsDuplicateString( value, &impl->album_title );
}

static HRESULT STDMETHODCALLTYPE music_properties2_get_TrackNumber( IMusicDisplayProperties2 *iface, UINT32 *value )
{
    FIXME( "iface %p, value %p stub\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE music_properties2_put_TrackNumber( IMusicDisplayProperties2 *iface, UINT32 value )
{
    FIXME( "iface %p, value %u stub\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE music_properties2_get_Genres( IMusicDisplayProperties2 *iface, IVector_HSTRING **value )
{
    FIXME( "iface %p, value %p stub\n", iface, value );
    return E_NOTIMPL;
}

static const struct IMusicDisplayProperties2Vtbl music_properties2_vtbl =
{
    music_properties2_QueryInterface,
    music_properties2_AddRef,
    music_properties2_Release,
    /* IInspectable methods */
    music_properties2_GetIids,
    music_properties2_GetRuntimeClassName,
    music_properties2_GetTrustLevel,
    /* IMusicDisplayProperties2 methods */
    music_properties2_get_AlbumTitle,
    music_properties2_put_AlbumTitle,
    music_properties2_get_TrackNumber,
    music_properties2_put_TrackNumber,
    music_properties2_get_Genres,
};

struct display_updater
{
    ISystemMediaTransportControlsDisplayUpdater ISystemMediaTransportControlsDisplayUpdater_iface;
    LONG ref;

    MediaPlaybackType playback_type;
#ifdef __REACTOS__
    HWND session;
    struct music_properties *music;
#endif
};

static inline struct display_updater *impl_from_ISystemMediaTransportControlsDisplayUpdater( ISystemMediaTransportControlsDisplayUpdater *iface )
{
    return CONTAINING_RECORD( iface, struct display_updater, ISystemMediaTransportControlsDisplayUpdater_iface );
}

static HRESULT WINAPI display_updater_QueryInterface( ISystemMediaTransportControlsDisplayUpdater *iface, REFIID iid, void **out )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ISystemMediaTransportControlsDisplayUpdater ))
    {
        *out = &impl->ISystemMediaTransportControlsDisplayUpdater_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI display_updater_AddRef( ISystemMediaTransportControlsDisplayUpdater *iface )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI display_updater_Release( ISystemMediaTransportControlsDisplayUpdater *iface )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

#ifdef __REACTOS__
    if (!ref)
    {
        if (impl->music) IMusicDisplayProperties_Release( &impl->music->IMusicDisplayProperties_iface );
        free( impl );
    }
#else
    if (!ref) free( impl );
#endif
    return ref;
}

#ifdef __REACTOS__
static void display_updater_publish( struct display_updater *impl )
{
    const WCHAR *title = L"", *artist = L"";
    WCHAR text[512];

    if (!impl->session) return;
    if (impl->music)
    {
        title = WindowsGetStringRawBuffer( impl->music->title, NULL );
        artist = WindowsGetStringRawBuffer( impl->music->artist, NULL );
    }
    swprintf( text, ARRAY_SIZE(text), L"%s\n%s", title, artist );
    SendMessageTimeoutW( impl->session, WM_SETTEXT, 0, (LPARAM)text, SMTO_ABORTIFHUNG, 200, NULL );
}
#endif

static HRESULT WINAPI display_updater_GetIids( ISystemMediaTransportControlsDisplayUpdater *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_GetRuntimeClassName( ISystemMediaTransportControlsDisplayUpdater *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_GetTrustLevel( ISystemMediaTransportControlsDisplayUpdater *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_get_Type( ISystemMediaTransportControlsDisplayUpdater *iface, MediaPlaybackType *value )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->playback_type;
    return S_OK;
}

static HRESULT WINAPI display_updater_put_Type( ISystemMediaTransportControlsDisplayUpdater *iface, MediaPlaybackType value )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    if (value < MediaPlaybackType_Unknown || value > MediaPlaybackType_Image) return E_INVALIDARG;
    impl->playback_type = value;
    return S_OK;
}

static HRESULT WINAPI display_updater_get_AppMediaId( ISystemMediaTransportControlsDisplayUpdater *iface, HSTRING *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_put_AppMediaId( ISystemMediaTransportControlsDisplayUpdater *iface, HSTRING value )
{
    FIXME( "iface %p, value %s stub!\n", iface, debugstr_hstring( value ) );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_get_Thumbnail( ISystemMediaTransportControlsDisplayUpdater *iface, IRandomAccessStreamReference **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );

    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI display_updater_put_Thumbnail( ISystemMediaTransportControlsDisplayUpdater *iface, IRandomAccessStreamReference *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI display_updater_get_MusicProperties( ISystemMediaTransportControlsDisplayUpdater *iface, IMusicDisplayProperties **value )
{
    struct music_properties *impl;
#ifdef __REACTOS__
    struct display_updater *updater = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );
#endif

    TRACE( "iface %p, value %p\n", iface, value );

#ifdef __REACTOS__
    if (updater->music)
    {
        *value = &updater->music->IMusicDisplayProperties_iface;
        IMusicDisplayProperties_AddRef( *value );
        return S_OK;
    }
#endif
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IMusicDisplayProperties_iface.lpVtbl = &music_properties_vtbl;
    impl->IMusicDisplayProperties2_iface.lpVtbl = &music_properties2_vtbl;
    impl->ref = 1;

#ifdef __REACTOS__
    updater->music = impl;
    IMusicDisplayProperties_AddRef( &impl->IMusicDisplayProperties_iface );
#endif
    *value = &impl->IMusicDisplayProperties_iface;
    TRACE( "created IMusicDisplayProperties %p.\n", *value );
    return S_OK;
}

static HRESULT WINAPI display_updater_get_VideoProperties( ISystemMediaTransportControlsDisplayUpdater *iface, __x_ABI_CWindows_CMedia_CIVideoDisplayProperties **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_get_ImageProperties( ISystemMediaTransportControlsDisplayUpdater *iface, __x_ABI_CWindows_CMedia_CIImageDisplayProperties **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_CopyFromFileAsync( ISystemMediaTransportControlsDisplayUpdater *iface, MediaPlaybackType type, IStorageFile *source,
    IAsyncOperation_boolean **operation )
{
    FIXME( "iface %p, type %d, source %p, operation %p stub!\n", iface, type, source, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_ClearAll( ISystemMediaTransportControlsDisplayUpdater *iface )
{
#ifdef __REACTOS__
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );

    TRACE( "iface %p\n", iface );

    if (impl->music)
    {
        WindowsDeleteString( impl->music->album_title );
        WindowsDeleteString( impl->music->artist );
        WindowsDeleteString( impl->music->title );
        impl->music->album_title = impl->music->artist = impl->music->title = NULL;
    }
    display_updater_publish( impl );
#else
    FIXME( "iface %p stub!\n", iface );
#endif
    return S_OK;
}

static HRESULT WINAPI display_updater_Update( ISystemMediaTransportControlsDisplayUpdater *iface )
{
#ifdef __REACTOS__
    TRACE( "iface %p\n", iface );
    display_updater_publish( impl_from_ISystemMediaTransportControlsDisplayUpdater( iface ) );
#else
    FIXME( "iface %p stub!\n", iface );
#endif
    return S_OK;
}

static const struct ISystemMediaTransportControlsDisplayUpdaterVtbl display_updater_vtbl =
{
    display_updater_QueryInterface,
    display_updater_AddRef,
    display_updater_Release,
    /* IInspectable methods */
    display_updater_GetIids,
    display_updater_GetRuntimeClassName,
    display_updater_GetTrustLevel,
    /* ISystemMediaTransportControlsDisplayUpdater methods */
    display_updater_get_Type,
    display_updater_put_Type,
    display_updater_get_AppMediaId,
    display_updater_put_AppMediaId,
    display_updater_get_Thumbnail,
    display_updater_put_Thumbnail,
    display_updater_get_MusicProperties,
    display_updater_get_VideoProperties,
    display_updater_get_ImageProperties,
    display_updater_CopyFromFileAsync,
    display_updater_ClearAll,
    display_updater_Update,
};

struct media_control
{
    ISystemMediaTransportControls ISystemMediaTransportControls_iface;
    LONG ref;

    HWND window;
    MediaPlaybackStatus media_playback_status;
    boolean is_play_enabled;
    boolean is_stop_enabled;
    boolean is_pause_enabled;
    boolean is_previous_enabled;
    boolean is_next_enabled;
    boolean is_enabled;
#ifdef __REACTOS__
    HWND session;
    struct list button_handlers;
    struct display_updater *updater;
#endif
};

static inline struct media_control *impl_from_ISystemMediaTransportControls( ISystemMediaTransportControls *iface )
{
    return CONTAINING_RECORD( iface, struct media_control, ISystemMediaTransportControls_iface );
}

#ifdef __REACTOS__
static CRITICAL_SECTION session_cs;
static CRITICAL_SECTION_DEBUG session_cs_debug =
{
    0, 0, &session_cs,
    { &session_cs_debug.ProcessLocksList, &session_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": session_cs") }
};
static CRITICAL_SECTION session_cs = { &session_cs_debug, -1, 0, 0, 0, 0 };
static EventRegistrationToken next_button_token = {.value = 1};

struct button_handler
{
    struct list entry;
    EventRegistrationToken token;
    ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs *handler;
};

struct button_args
{
    ISystemMediaTransportControlsButtonPressedEventArgs ISystemMediaTransportControlsButtonPressedEventArgs_iface;
    LONG ref;
    SystemMediaTransportControlsButton button;
};

static inline struct button_args *impl_from_ISystemMediaTransportControlsButtonPressedEventArgs( ISystemMediaTransportControlsButtonPressedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct button_args, ISystemMediaTransportControlsButtonPressedEventArgs_iface );
}

static HRESULT WINAPI button_args_QueryInterface( ISystemMediaTransportControlsButtonPressedEventArgs *iface, REFIID iid, void **out )
{
    struct button_args *impl = impl_from_ISystemMediaTransportControlsButtonPressedEventArgs( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ISystemMediaTransportControlsButtonPressedEventArgs ))
    {
        *out = &impl->ISystemMediaTransportControlsButtonPressedEventArgs_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI button_args_AddRef( ISystemMediaTransportControlsButtonPressedEventArgs *iface )
{
    struct button_args *impl = impl_from_ISystemMediaTransportControlsButtonPressedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI button_args_Release( ISystemMediaTransportControlsButtonPressedEventArgs *iface )
{
    struct button_args *impl = impl_from_ISystemMediaTransportControlsButtonPressedEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI button_args_GetIids( ISystemMediaTransportControlsButtonPressedEventArgs *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI button_args_GetRuntimeClassName( ISystemMediaTransportControlsButtonPressedEventArgs *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI button_args_GetTrustLevel( ISystemMediaTransportControlsButtonPressedEventArgs *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI button_args_get_Button( ISystemMediaTransportControlsButtonPressedEventArgs *iface, SystemMediaTransportControlsButton *value )
{
    struct button_args *impl = impl_from_ISystemMediaTransportControlsButtonPressedEventArgs( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->button;
    return S_OK;
}

static const struct ISystemMediaTransportControlsButtonPressedEventArgsVtbl button_args_vtbl =
{
    button_args_QueryInterface,
    button_args_AddRef,
    button_args_Release,
    /* IInspectable methods */
    button_args_GetIids,
    button_args_GetRuntimeClassName,
    button_args_GetTrustLevel,
    /* ISystemMediaTransportControlsButtonPressedEventArgs methods */
    button_args_get_Button,
};

static void session_update( struct media_control *impl, BOOL activity )
{
    if (!impl->session) return;

    if (impl->is_enabled)
        SetPropW( impl->session, MEDIA_SESSION_STATUS, (HANDLE)(ULONG_PTR)(impl->media_playback_status + 1) );
    else
        RemovePropW( impl->session, MEDIA_SESSION_STATUS );
    if (activity)
        SetPropW( impl->session, MEDIA_SESSION_TICK, (HANDLE)(ULONG_PTR)GetTickCount() );
}

static BOOL session_button( struct media_control *impl, UINT command, SystemMediaTransportControlsButton *button )
{
    if (!impl->is_enabled) return FALSE;

    switch (command)
    {
    case APPCOMMAND_MEDIA_PLAY_PAUSE:
        if (impl->media_playback_status == MediaPlaybackStatus_Playing)
        {
            *button = SystemMediaTransportControlsButton_Pause;
            return impl->is_pause_enabled;
        }
        *button = SystemMediaTransportControlsButton_Play;
        return impl->is_play_enabled;
    case APPCOMMAND_MEDIA_PLAY:
        *button = SystemMediaTransportControlsButton_Play;
        return impl->is_play_enabled;
    case APPCOMMAND_MEDIA_PAUSE:
        *button = SystemMediaTransportControlsButton_Pause;
        return impl->is_pause_enabled;
    case APPCOMMAND_MEDIA_STOP:
        *button = SystemMediaTransportControlsButton_Stop;
        return impl->is_stop_enabled;
    case APPCOMMAND_MEDIA_NEXTTRACK:
        *button = SystemMediaTransportControlsButton_Next;
        return impl->is_next_enabled;
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:
        *button = SystemMediaTransportControlsButton_Previous;
        return impl->is_previous_enabled;
    }
    return FALSE;
}

static void session_notify( struct media_control *impl, SystemMediaTransportControlsButton button )
{
    struct button_handler *entry, *next, *copy;
    struct list copies = LIST_INIT( copies );
    struct button_args *args;

    if (!(args = calloc( 1, sizeof(*args) ))) return;
    args->ISystemMediaTransportControlsButtonPressedEventArgs_iface.lpVtbl = &button_args_vtbl;
    args->ref = 1;
    args->button = button;

    EnterCriticalSection( &session_cs );
    LIST_FOR_EACH_ENTRY( entry, &impl->button_handlers, struct button_handler, entry )
    {
        if (!(copy = calloc( 1, sizeof(*copy) ))) continue;
        copy->handler = entry->handler;
        ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs_AddRef( copy->handler );
        list_add_tail( &copies, &copy->entry );
    }
    LeaveCriticalSection( &session_cs );

    LIST_FOR_EACH_ENTRY_SAFE( entry, next, &copies, struct button_handler, entry )
    {
        ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs_Invoke( entry->handler,
            &impl->ISystemMediaTransportControls_iface, &args->ISystemMediaTransportControlsButtonPressedEventArgs_iface );
        ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs_Release( entry->handler );
        list_remove( &entry->entry );
        free( entry );
    }

    ISystemMediaTransportControlsButtonPressedEventArgs_Release( &args->ISystemMediaTransportControlsButtonPressedEventArgs_iface );
}

static LRESULT CALLBACK session_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    SystemMediaTransportControlsButton button;
    struct media_control *impl;
    LONG ref;

    if (msg != WM_APPCOMMAND) return DefWindowProcW( hwnd, msg, wparam, lparam );

    EnterCriticalSection( &session_cs );
    impl = (struct media_control *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );
    while (impl)
    {
        if (!(ref = impl->ref)) impl = NULL;
        else if (InterlockedCompareExchange( &impl->ref, ref + 1, ref ) == ref) break;
    }
    LeaveCriticalSection( &session_cs );
    if (!impl) return 0;

    TRACE( "session %p command %u\n", hwnd, GET_APPCOMMAND_LPARAM( lparam ) );
    if (session_button( impl, GET_APPCOMMAND_LPARAM( lparam ), &button ))
    {
        session_update( impl, TRUE );
        session_notify( impl, button );
    }
    ISystemMediaTransportControls_Release( &impl->ISystemMediaTransportControls_iface );
    return TRUE;
}

static BOOL WINAPI session_register_class( INIT_ONCE *once, void *param, void **context )
{
    WNDCLASSEXW wc = { sizeof(wc) };

    GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        (const WCHAR *)session_wndproc, &wc.hInstance );
    wc.lpfnWndProc = session_wndproc;
    wc.lpszClassName = MEDIA_SESSION_CLASS;
    return RegisterClassExW( &wc ) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static void session_create( struct media_control *impl )
{
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    HINSTANCE instance = NULL;

    list_init( &impl->button_handlers );
    if (!InitOnceExecuteOnce( &once, session_register_class, NULL, NULL )) return;

    GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        (const WCHAR *)session_wndproc, &instance );
    impl->session = CreateWindowExW( 0, MEDIA_SESSION_CLASS, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, instance, NULL );
    if (!impl->session) return;
    SetWindowLongPtrW( impl->session, GWLP_USERDATA, (LONG_PTR)impl );
    session_update( impl, TRUE );
}

static void session_destroy( struct media_control *impl )
{
    struct button_handler *entry, *next;

    if (impl->session)
    {
        EnterCriticalSection( &session_cs );
        SetWindowLongPtrW( impl->session, GWLP_USERDATA, 0 );
        LeaveCriticalSection( &session_cs );
        RemovePropW( impl->session, MEDIA_SESSION_STATUS );
        RemovePropW( impl->session, MEDIA_SESSION_TICK );
        if (GetWindowThreadProcessId( impl->session, NULL ) == GetCurrentThreadId())
            DestroyWindow( impl->session );
        else
            PostMessageW( impl->session, WM_CLOSE, 0, 0 );
    }

    LIST_FOR_EACH_ENTRY_SAFE( entry, next, &impl->button_handlers, struct button_handler, entry )
    {
        list_remove( &entry->entry );
        ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs_Release( entry->handler );
        free( entry );
    }

    if (impl->updater)
    {
        impl->updater->session = NULL;
        ISystemMediaTransportControlsDisplayUpdater_Release( &impl->updater->ISystemMediaTransportControlsDisplayUpdater_iface );
        impl->updater = NULL;
    }
}
#endif

static HRESULT WINAPI media_control_QueryInterface( ISystemMediaTransportControls *iface, REFIID iid, void **out )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ISystemMediaTransportControls ))
    {
        *out = &impl->ISystemMediaTransportControls_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI media_control_AddRef( ISystemMediaTransportControls *iface )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI media_control_Release( ISystemMediaTransportControls *iface )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

#ifdef __REACTOS__
    if (!ref)
    {
        session_destroy( impl );
        free( impl );
    }
#else
    if (!ref) free( impl );
#endif
    return ref;
}

static HRESULT WINAPI media_control_GetIids( ISystemMediaTransportControls *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_GetRuntimeClassName( ISystemMediaTransportControls *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_GetTrustLevel( ISystemMediaTransportControls *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_PlaybackStatus( ISystemMediaTransportControls *iface, MediaPlaybackStatus *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->media_playback_status;
    return S_OK;
}

static HRESULT WINAPI media_control_put_PlaybackStatus( ISystemMediaTransportControls *iface, MediaPlaybackStatus value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    if (value < MediaPlaybackStatus_Closed || value > MediaPlaybackStatus_Paused) return E_INVALIDARG;
#ifdef __REACTOS__
    if (impl->media_playback_status != value)
    {
        impl->media_playback_status = value;
        session_update( impl, value == MediaPlaybackStatus_Playing );
    }
#else
    impl->media_playback_status = value;
#endif
    return S_OK;
}

static HRESULT WINAPI media_control_get_DisplayUpdater( ISystemMediaTransportControls *iface, ISystemMediaTransportControlsDisplayUpdater **value )
{
    struct display_updater *impl;
#ifdef __REACTOS__
    struct media_control *control = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    if (control->updater)
    {
        *value = &control->updater->ISystemMediaTransportControlsDisplayUpdater_iface;
        ISystemMediaTransportControlsDisplayUpdater_AddRef( *value );
        return S_OK;
    }
#else
    FIXME( "iface %p, value %p semi-stub!\n", iface, value );
#endif

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->ISystemMediaTransportControlsDisplayUpdater_iface.lpVtbl = &display_updater_vtbl;
    impl->ref = 1;
#ifdef __REACTOS__
    impl->session = control->session;
    control->updater = impl;
    ISystemMediaTransportControlsDisplayUpdater_AddRef( &impl->ISystemMediaTransportControlsDisplayUpdater_iface );
#endif

    *value = &impl->ISystemMediaTransportControlsDisplayUpdater_iface;
    TRACE( "created ISystemMediaTransportControlsDisplayUpdater %p.\n", *value );
    return S_OK;
}

static HRESULT WINAPI media_control_get_SoundLevel( ISystemMediaTransportControls *iface, SoundLevel *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_IsEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_enabled = value;
#ifdef __REACTOS__
    session_update( impl, value );
#endif
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsPlayEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_play_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsPlayEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_play_enabled = value;
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsStopEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_stop_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsStopEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_stop_enabled = value;
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsPauseEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_pause_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsPauseEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_pause_enabled = value;
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsRecordEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_put_IsRecordEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    FIXME( "iface %p, value %d stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_IsFastForwardEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_put_IsFastForwardEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    FIXME( "iface %p, value %d stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_IsRewindEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_put_IsRewindEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    FIXME( "iface %p, value %d stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_IsPreviousEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_previous_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsPreviousEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_previous_enabled = value;
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsNextEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_next_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsNextEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_next_enabled = value;
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsChannelUpEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_put_IsChannelUpEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    FIXME( "iface %p, value %d stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_IsChannelDownEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_put_IsChannelDownEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    FIXME( "iface %p, value %d stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_add_ButtonPressed( ISystemMediaTransportControls *iface,
    ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs *handler, EventRegistrationToken *token )
{
#ifdef __REACTOS__
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );
    struct button_handler *entry;

    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );

    if (!handler || !token) return E_POINTER;
    if (!(entry = calloc( 1, sizeof(*entry) ))) return E_OUTOFMEMORY;
    entry->handler = handler;
    ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs_AddRef( handler );

    EnterCriticalSection( &session_cs );
    *token = entry->token = next_button_token;
    next_button_token.value++;
    list_add_tail( &impl->button_handlers, &entry->entry );
    LeaveCriticalSection( &session_cs );
    return S_OK;
#else
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    *token = dummy_token;
    return S_OK;
#endif
}

static HRESULT WINAPI media_control_remove_ButtonPressed( ISystemMediaTransportControls *iface, EventRegistrationToken token )
{
#ifdef __REACTOS__
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );
    struct button_handler *entry;
    BOOL found = FALSE;

    TRACE( "iface %p, token %#I64x\n", iface, token.value );

    EnterCriticalSection( &session_cs );
    LIST_FOR_EACH_ENTRY( entry, &impl->button_handlers, struct button_handler, entry )
        if ((found = (entry->token.value == token.value))) break;
    if (found) list_remove( &entry->entry );
    LeaveCriticalSection( &session_cs );

    if (found)
    {
        ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs_Release( entry->handler );
        free( entry );
    }
    return S_OK;
#else
    FIXME( "iface %p, token %#I64x stub!\n", iface, token.value );
    return S_OK;
#endif
}

static HRESULT WINAPI media_control_add_PropertyChanged( ISystemMediaTransportControls *iface,
    ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsPropertyChangedEventArgs *handler, EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_remove_PropertyChanged( ISystemMediaTransportControls *iface, EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static const struct ISystemMediaTransportControlsVtbl media_control_vtbl =
{
    media_control_QueryInterface,
    media_control_AddRef,
    media_control_Release,
    /* IInspectable methods */
    media_control_GetIids,
    media_control_GetRuntimeClassName,
    media_control_GetTrustLevel,
    /* ISystemMediaTransportControls methods */
    media_control_get_PlaybackStatus,
    media_control_put_PlaybackStatus,
    media_control_get_DisplayUpdater,
    media_control_get_SoundLevel,
    media_control_get_IsEnabled,
    media_control_put_IsEnabled,
    media_control_get_IsPlayEnabled,
    media_control_put_IsPlayEnabled,
    media_control_get_IsStopEnabled,
    media_control_put_IsStopEnabled,
    media_control_get_IsPauseEnabled,
    media_control_put_IsPauseEnabled,
    media_control_get_IsRecordEnabled,
    media_control_put_IsRecordEnabled,
    media_control_get_IsFastForwardEnabled,
    media_control_put_IsFastForwardEnabled,
    media_control_get_IsRewindEnabled,
    media_control_put_IsRewindEnabled,
    media_control_get_IsPreviousEnabled,
    media_control_put_IsPreviousEnabled,
    media_control_get_IsNextEnabled,
    media_control_put_IsNextEnabled,
    media_control_get_IsChannelUpEnabled,
    media_control_put_IsChannelUpEnabled,
    media_control_get_IsChannelDownEnabled,
    media_control_put_IsChannelDownEnabled,
    media_control_add_ButtonPressed,
    media_control_remove_ButtonPressed,
    media_control_add_PropertyChanged,
    media_control_remove_PropertyChanged,
};

DEFINE_IINSPECTABLE( media_control_statics, ISystemMediaTransportControlsInterop, struct media_control_statics, IActivationFactory_iface )

static HRESULT WINAPI media_control_statics_GetForWindow( ISystemMediaTransportControlsInterop *iface, HWND window, REFIID riid, void **control )
{
    struct media_control *impl;
    HRESULT hr;

    TRACE( "iface %p, window %p, riid %s, control %p\n", iface, window, debugstr_guid( riid ), control );

    if (!window) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->ISystemMediaTransportControls_iface.lpVtbl = &media_control_vtbl;
    impl->ref = 2;
    impl->window = window;
#ifdef __REACTOS__
    session_create( impl );
#endif

    TRACE( "created ISystemMediaTransportControls %p.\n", *control );

    hr = ISystemMediaTransportControls_QueryInterface( &impl->ISystemMediaTransportControls_iface, riid, control );
    ISystemMediaTransportControls_Release( &impl->ISystemMediaTransportControls_iface );
    return hr;
}

static const struct ISystemMediaTransportControlsInteropVtbl media_control_statics_vtbl =
{
    media_control_statics_QueryInterface,
    media_control_statics_AddRef,
    media_control_statics_Release,
    /* IInspectable methods */
    media_control_statics_GetIids,
    media_control_statics_GetRuntimeClassName,
    media_control_statics_GetTrustLevel,
    /* ISystemMediaTransportControlsInterop methods */
    media_control_statics_GetForWindow,
};

static struct media_control_statics media_control_statics =
{
    {&factory_vtbl},
    {&media_control_statics_vtbl},
    1,
};

static IActivationFactory *media_control_factory = &media_control_statics.IActivationFactory_iface;

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_Media_SystemMediaTransportControls ))
        IActivationFactory_QueryInterface( media_control_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return CLASS_E_CLASSNOTAVAILABLE;
}
