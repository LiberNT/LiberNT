/* windows.Devices.Bluetooth.Advertisement Implementation
 *
 * Copyright 2025 Vibhav Pant
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

#include "private.h"

#include "wine/debug.h"
#ifdef __REACTOS__
#include "wine/list.h"
#endif

WINE_DEFAULT_DEBUG_CHANNEL( bluetooth );

#ifdef __REACTOS__
struct watcher_handler
{
    struct list entry;
    UINT64 token;
    IUnknown *handler;
};

struct signal_filter
{
    IBluetoothSignalStrengthFilter IBluetoothSignalStrengthFilter_iface;
    LONG ref;
    IReference_INT16 *in_range;
    IReference_INT16 *out_of_range;
    IReference_TimeSpan *timeout;
    IReference_TimeSpan *interval;
};

static inline struct signal_filter *impl_from_IBluetoothSignalStrengthFilter( IBluetoothSignalStrengthFilter *iface )
{
    return CONTAINING_RECORD( iface, struct signal_filter, IBluetoothSignalStrengthFilter_iface );
}

static HRESULT WINAPI signal_filter_QueryInterface( IBluetoothSignalStrengthFilter *iface, REFIID iid, void **out )
{
    struct signal_filter *impl = impl_from_IBluetoothSignalStrengthFilter( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothSignalStrengthFilter ))
    {
        IBluetoothSignalStrengthFilter_AddRef(( *out = &impl->IBluetoothSignalStrengthFilter_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI signal_filter_AddRef( IBluetoothSignalStrengthFilter *iface )
{
    struct signal_filter *impl = impl_from_IBluetoothSignalStrengthFilter( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI signal_filter_Release( IBluetoothSignalStrengthFilter *iface )
{
    struct signal_filter *impl = impl_from_IBluetoothSignalStrengthFilter( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->in_range) IReference_INT16_Release( impl->in_range );
        if (impl->out_of_range) IReference_INT16_Release( impl->out_of_range );
        if (impl->timeout) IReference_TimeSpan_Release( impl->timeout );
        if (impl->interval) IReference_TimeSpan_Release( impl->interval );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI signal_filter_GetIids( IBluetoothSignalStrengthFilter *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI signal_filter_GetRuntimeClassName( IBluetoothSignalStrengthFilter *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI signal_filter_GetTrustLevel( IBluetoothSignalStrengthFilter *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

#define SIGNAL_FILTER_PROPERTY( name, type, field ) \
    static HRESULT WINAPI signal_filter_get_##name( IBluetoothSignalStrengthFilter *iface, type **value ) \
    { \
        struct signal_filter *impl = impl_from_IBluetoothSignalStrengthFilter( iface ); \
        TRACE( "(%p, %p)\n", iface, value ); \
        if (!value) return E_POINTER; \
        if ((*value = impl->field)) type##_AddRef( *value ); \
        return S_OK; \
    } \
    static HRESULT WINAPI signal_filter_put_##name( IBluetoothSignalStrengthFilter *iface, type *value ) \
    { \
        struct signal_filter *impl = impl_from_IBluetoothSignalStrengthFilter( iface ); \
        TRACE( "(%p, %p)\n", iface, value ); \
        if (value) type##_AddRef( value ); \
        if (impl->field) type##_Release( impl->field ); \
        impl->field = value; \
        return S_OK; \
    }

SIGNAL_FILTER_PROPERTY( InRangeThresholdInDBm, IReference_INT16, in_range )
SIGNAL_FILTER_PROPERTY( OutOfRangeThresholdInDBm, IReference_INT16, out_of_range )
SIGNAL_FILTER_PROPERTY( OutOfRangeTimeout, IReference_TimeSpan, timeout )
SIGNAL_FILTER_PROPERTY( SamplingInterval, IReference_TimeSpan, interval )

static const IBluetoothSignalStrengthFilterVtbl signal_filter_vtbl =
{
    signal_filter_QueryInterface,
    signal_filter_AddRef,
    signal_filter_Release,
    signal_filter_GetIids,
    signal_filter_GetRuntimeClassName,
    signal_filter_GetTrustLevel,
    signal_filter_get_InRangeThresholdInDBm,
    signal_filter_put_InRangeThresholdInDBm,
    signal_filter_get_OutOfRangeThresholdInDBm,
    signal_filter_put_OutOfRangeThresholdInDBm,
    signal_filter_get_OutOfRangeTimeout,
    signal_filter_put_OutOfRangeTimeout,
    signal_filter_get_SamplingInterval,
    signal_filter_put_SamplingInterval,
};

static HRESULT signal_filter_create( IBluetoothSignalStrengthFilter **out )
{
    struct signal_filter *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IBluetoothSignalStrengthFilter_iface.lpVtbl = &signal_filter_vtbl;
    impl->ref = 1;
    *out = &impl->IBluetoothSignalStrengthFilter_iface;
    return S_OK;
}

struct watcher_stopped_args
{
    IBluetoothLEAdvertisementWatcherStoppedEventArgs IBluetoothLEAdvertisementWatcherStoppedEventArgs_iface;
    LONG ref;
    BluetoothError error;
};

static inline struct watcher_stopped_args *impl_from_IBluetoothLEAdvertisementWatcherStoppedEventArgs( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct watcher_stopped_args, IBluetoothLEAdvertisementWatcherStoppedEventArgs_iface );
}

static HRESULT WINAPI stopped_args_QueryInterface( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface, REFIID iid, void **out )
{
    struct watcher_stopped_args *impl = impl_from_IBluetoothLEAdvertisementWatcherStoppedEventArgs( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothLEAdvertisementWatcherStoppedEventArgs ))
    {
        IBluetoothLEAdvertisementWatcherStoppedEventArgs_AddRef(( *out = &impl->IBluetoothLEAdvertisementWatcherStoppedEventArgs_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI stopped_args_AddRef( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface )
{
    struct watcher_stopped_args *impl = impl_from_IBluetoothLEAdvertisementWatcherStoppedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI stopped_args_Release( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface )
{
    struct watcher_stopped_args *impl = impl_from_IBluetoothLEAdvertisementWatcherStoppedEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI stopped_args_GetIids( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI stopped_args_GetRuntimeClassName( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI stopped_args_GetTrustLevel( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI stopped_args_get_Error( IBluetoothLEAdvertisementWatcherStoppedEventArgs *iface, BluetoothError *value )
{
    struct watcher_stopped_args *impl = impl_from_IBluetoothLEAdvertisementWatcherStoppedEventArgs( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->error;
    return S_OK;
}

static const IBluetoothLEAdvertisementWatcherStoppedEventArgsVtbl stopped_args_vtbl =
{
    stopped_args_QueryInterface,
    stopped_args_AddRef,
    stopped_args_Release,
    stopped_args_GetIids,
    stopped_args_GetRuntimeClassName,
    stopped_args_GetTrustLevel,
    stopped_args_get_Error,
};

struct adv_watcher
{
    IBluetoothLEAdvertisementWatcher IBluetoothLEAdvertisementWatcher_iface;
    LONG ref;
    CRITICAL_SECTION cs;
    BluetoothLEAdvertisementWatcherStatus status;
    BluetoothLEScanningMode mode;
    IBluetoothSignalStrengthFilter *filter;
    struct list received;
    struct list stopped;
    UINT64 next_token;
};

struct stopped_notify
{
    struct adv_watcher *watcher;
    BluetoothError error;
};

static inline struct adv_watcher *impl_from_IBluetoothLEAdvertisementWatcher( IBluetoothLEAdvertisementWatcher *iface )
{
    return CONTAINING_RECORD( iface, struct adv_watcher, IBluetoothLEAdvertisementWatcher_iface );
}

static void watcher_free_handlers( struct list *handlers )
{
    struct watcher_handler *cur, *next;

    LIST_FOR_EACH_ENTRY_SAFE( cur, next, handlers, struct watcher_handler, entry )
    {
        list_remove( &cur->entry );
        IUnknown_Release( cur->handler );
        free( cur );
    }
}

static HRESULT WINAPI adv_watcher_QueryInterface( IBluetoothLEAdvertisementWatcher *iface, REFIID iid, void **out )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothLEAdvertisementWatcher ))
    {
        IBluetoothLEAdvertisementWatcher_AddRef(( *out = &impl->IBluetoothLEAdvertisementWatcher_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI adv_watcher_AddRef( IBluetoothLEAdvertisementWatcher *iface )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI adv_watcher_Release( IBluetoothLEAdvertisementWatcher *iface )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "(%p)\n", iface );

    if (!ref)
    {
        watcher_free_handlers( &impl->received );
        watcher_free_handlers( &impl->stopped );
        if (impl->filter) IBluetoothSignalStrengthFilter_Release( impl->filter );
        impl->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI adv_watcher_GetIids( IBluetoothLEAdvertisementWatcher *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_GetRuntimeClassName( IBluetoothLEAdvertisementWatcher *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_GetTrustLevel( IBluetoothLEAdvertisementWatcher *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_get_MinSamplingInternal( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 1000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_MaxSamplingInternal( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 255000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_MinOutOfRangeTimeout( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 10000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_MaxOutOfRangeTimeout( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 600000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_Status( IBluetoothLEAdvertisementWatcher *iface, BluetoothLEAdvertisementWatcherStatus *status )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %p)\n", iface, status );

    if (!status) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *status = impl->status;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_ScanningMode( IBluetoothLEAdvertisementWatcher *iface, BluetoothLEScanningMode *value )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %p)\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->mode;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_put_ScanningMode( IBluetoothLEAdvertisementWatcher *iface, BluetoothLEScanningMode value )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %d)\n", iface, value );

    impl->mode = value;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_SignalStrengthFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothSignalStrengthFilter **filter )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %p)\n", iface, filter );

    if (!filter) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    if ((*filter = impl->filter)) IBluetoothSignalStrengthFilter_AddRef( *filter );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI adv_watcher_put_SignalStrengthFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothSignalStrengthFilter *filter )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %p)\n", iface, filter );

    if (!filter) return E_POINTER;
    IBluetoothSignalStrengthFilter_AddRef( filter );
    EnterCriticalSection( &impl->cs );
    if (impl->filter) IBluetoothSignalStrengthFilter_Release( impl->filter );
    impl->filter = filter;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_AdvertisementFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothLEAdvertisementFilter **filter )
{
    FIXME( "(%p, %p): stub!\n", iface, filter );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_put_AdvertisementFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothLEAdvertisementFilter *filter )
{
    FIXME( "(%p, %p): stub!\n", iface, filter );
    return E_NOTIMPL;
}

static DWORD WINAPI watcher_stopped_thread( void *arg )
{
    struct stopped_notify *notify = arg;
    struct adv_watcher *impl = notify->watcher;
    struct watcher_stopped_args *args;
    struct watcher_handler *cur;
    IUnknown **handlers = NULL;
    UINT count = 0, i = 0;

    if ((args = calloc( 1, sizeof( *args ) )))
    {
        args->IBluetoothLEAdvertisementWatcherStoppedEventArgs_iface.lpVtbl = &stopped_args_vtbl;
        args->ref = 1;
        args->error = notify->error;

        EnterCriticalSection( &impl->cs );
        LIST_FOR_EACH_ENTRY( cur, &impl->stopped, struct watcher_handler, entry ) count++;
        if (count && (handlers = calloc( count, sizeof( *handlers ) )))
        {
            LIST_FOR_EACH_ENTRY( cur, &impl->stopped, struct watcher_handler, entry )
            {
                handlers[i] = cur->handler;
                IUnknown_AddRef( handlers[i] );
                i++;
            }
        }
        LeaveCriticalSection( &impl->cs );

        for (i = 0; handlers && i < count; i++)
        {
            ITypedEventHandler_BluetoothLEAdvertisementWatcher_BluetoothLEAdvertisementWatcherStoppedEventArgs_Invoke(
                    (ITypedEventHandler_BluetoothLEAdvertisementWatcher_BluetoothLEAdvertisementWatcherStoppedEventArgs *)handlers[i],
                    &impl->IBluetoothLEAdvertisementWatcher_iface, &args->IBluetoothLEAdvertisementWatcherStoppedEventArgs_iface );
            IUnknown_Release( handlers[i] );
        }
        free( handlers );
        IBluetoothLEAdvertisementWatcherStoppedEventArgs_Release( &args->IBluetoothLEAdvertisementWatcherStoppedEventArgs_iface );
    }

    IBluetoothLEAdvertisementWatcher_Release( &impl->IBluetoothLEAdvertisementWatcher_iface );
    free( notify );
    return 0;
}

static void watcher_notify_stopped( struct adv_watcher *impl, BluetoothError error )
{
    struct stopped_notify *notify;
    HANDLE thread;

    if (!(notify = malloc( sizeof( *notify ) ))) return;
    notify->watcher = impl;
    notify->error = error;
    IBluetoothLEAdvertisementWatcher_AddRef( &impl->IBluetoothLEAdvertisementWatcher_iface );
    if (!(thread = CreateThread( NULL, 0, watcher_stopped_thread, notify, 0, NULL )))
    {
        IBluetoothLEAdvertisementWatcher_Release( &impl->IBluetoothLEAdvertisementWatcher_iface );
        free( notify );
        return;
    }
    CloseHandle( thread );
}

static BOOL watcher_has_radio( void )
{
    BLUETOOTH_FIND_RADIO_PARAMS params = { sizeof( params ) };
    HBLUETOOTH_RADIO_FIND find;
    HANDLE radio;

    if (!(find = BluetoothFindFirstRadio( &params, &radio ))) return FALSE;
    CloseHandle( radio );
    BluetoothFindRadioClose( find );
    return TRUE;
}

static HRESULT WINAPI adv_watcher_Start( IBluetoothLEAdvertisementWatcher *iface )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );
    BOOL notify = FALSE;

    TRACE( "(%p)\n", iface );

    EnterCriticalSection( &impl->cs );
    if (impl->status != BluetoothLEAdvertisementWatcherStatus_Started)
    {
        if (watcher_has_radio())
            impl->status = BluetoothLEAdvertisementWatcherStatus_Started;
        else
        {
            impl->status = BluetoothLEAdvertisementWatcherStatus_Aborted;
            notify = TRUE;
        }
    }
    LeaveCriticalSection( &impl->cs );

    if (notify) watcher_notify_stopped( impl, BluetoothError_RadioNotAvailable );
    return S_OK;
}

static HRESULT WINAPI adv_watcher_Stop( IBluetoothLEAdvertisementWatcher *iface )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );
    BOOL notify = FALSE;

    TRACE( "(%p)\n", iface );

    EnterCriticalSection( &impl->cs );
    if (impl->status == BluetoothLEAdvertisementWatcherStatus_Started)
    {
        impl->status = BluetoothLEAdvertisementWatcherStatus_Stopped;
        notify = TRUE;
    }
    LeaveCriticalSection( &impl->cs );

    if (notify) watcher_notify_stopped( impl, BluetoothError_Success );
    return S_OK;
}

static HRESULT watcher_add_handler( struct adv_watcher *impl, struct list *handlers, IUnknown *handler, EventRegistrationToken *token )
{
    struct watcher_handler *entry;

    if (!handler || !token) return E_POINTER;
    if (!(entry = calloc( 1, sizeof( *entry ) ))) return E_OUTOFMEMORY;
    entry->handler = handler;
    IUnknown_AddRef( handler );

    EnterCriticalSection( &impl->cs );
    entry->token = ++impl->next_token;
    list_add_tail( handlers, &entry->entry );
    LeaveCriticalSection( &impl->cs );

    token->value = entry->token;
    return S_OK;
}

static HRESULT watcher_remove_handler( struct adv_watcher *impl, struct list *handlers, EventRegistrationToken token )
{
    struct watcher_handler *cur, *found = NULL;

    EnterCriticalSection( &impl->cs );
    LIST_FOR_EACH_ENTRY( cur, handlers, struct watcher_handler, entry )
    {
        if (cur->token == token.value)
        {
            list_remove( &cur->entry );
            found = cur;
            break;
        }
    }
    LeaveCriticalSection( &impl->cs );

    if (found)
    {
        IUnknown_Release( found->handler );
        free( found );
    }
    return S_OK;
}

static HRESULT WINAPI adv_watcher_add_Received( IBluetoothLEAdvertisementWatcher *iface,
                                                ITypedEventHandler_BluetoothLEAdvertisementWatcher_BluetoothLEAdvertisementReceivedEventArgs *handler,
                                                EventRegistrationToken *token )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %p, %p)\n", iface, handler, token );

    return watcher_add_handler( impl, &impl->received, (IUnknown *)handler, token );
}

static HRESULT WINAPI adv_watcher_remove_Received( IBluetoothLEAdvertisementWatcher *iface, EventRegistrationToken token )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %I64x)\n", iface, token.value );

    return watcher_remove_handler( impl, &impl->received, token );
}

static HRESULT WINAPI adv_watcher_add_Stopped( IBluetoothLEAdvertisementWatcher *iface,
                                               ITypedEventHandler_BluetoothLEAdvertisementWatcher_BluetoothLEAdvertisementWatcherStoppedEventArgs *handler,
                                               EventRegistrationToken *token )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %p, %p)\n", iface, handler, token );

    return watcher_add_handler( impl, &impl->stopped, (IUnknown *)handler, token );
}

static HRESULT WINAPI adv_watcher_remove_Stopped( IBluetoothLEAdvertisementWatcher *iface, EventRegistrationToken token )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %I64x)\n", iface, token.value );

    return watcher_remove_handler( impl, &impl->stopped, token );
}

static const IBluetoothLEAdvertisementWatcherVtbl adv_watcher_vtbl =
{
    /* IUnknown */
    adv_watcher_QueryInterface,
    adv_watcher_AddRef,
    adv_watcher_Release,
    /* IInspectable */
    adv_watcher_GetIids,
    adv_watcher_GetRuntimeClassName,
    adv_watcher_GetTrustLevel,
    /* IBluetoothLEAdvertisementWatcher */
    adv_watcher_get_MinSamplingInternal,
    adv_watcher_get_MaxSamplingInternal,
    adv_watcher_get_MinOutOfRangeTimeout,
    adv_watcher_get_MaxOutOfRangeTimeout,
    adv_watcher_get_Status,
    adv_watcher_get_ScanningMode,
    adv_watcher_put_ScanningMode,
    adv_watcher_get_SignalStrengthFilter,
    adv_watcher_put_SignalStrengthFilter,
    adv_watcher_get_AdvertisementFilter,
    adv_watcher_put_AdvertisementFilter,
    adv_watcher_Start,
    adv_watcher_Stop,
    adv_watcher_add_Received,
    adv_watcher_remove_Received,
    adv_watcher_add_Stopped,
    adv_watcher_remove_Stopped
};

static HRESULT adv_watcher_create( IBluetoothLEAdvertisementWatcher **watcher )
{
    struct adv_watcher *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    if (FAILED(hr = signal_filter_create( &impl->filter )))
    {
        free( impl );
        return hr;
    }
    impl->IBluetoothLEAdvertisementWatcher_iface.lpVtbl = &adv_watcher_vtbl;
    impl->ref = 1;
    impl->status = BluetoothLEAdvertisementWatcherStatus_Created;
    impl->mode = BluetoothLEScanningMode_Passive;
    list_init( &impl->received );
    list_init( &impl->stopped );
    InitializeCriticalSectionEx( &impl->cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO );
    impl->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": adv_watcher.cs");
    *watcher = &impl->IBluetoothLEAdvertisementWatcher_iface;
    return S_OK;
}
#else
struct adv_watcher
{
    IBluetoothLEAdvertisementWatcher IBluetoothLEAdvertisementWatcher_iface;
    LONG ref;
};

static inline struct adv_watcher *impl_from_IBluetoothLEAdvertisementWatcher( IBluetoothLEAdvertisementWatcher *iface )
{
    return CONTAINING_RECORD( iface, struct adv_watcher, IBluetoothLEAdvertisementWatcher_iface );
}

static HRESULT WINAPI adv_watcher_QueryInterface( IBluetoothLEAdvertisementWatcher *iface, REFIID iid, void **out )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBluetoothLEAdvertisementWatcher ))
    {
        IBluetoothLEAdvertisementWatcher_AddRef(( *out = &impl->IBluetoothLEAdvertisementWatcher_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI adv_watcher_AddRef( IBluetoothLEAdvertisementWatcher *iface )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI adv_watcher_Release( IBluetoothLEAdvertisementWatcher *iface )
{
    struct adv_watcher *impl = impl_from_IBluetoothLEAdvertisementWatcher( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "(%p)\n", iface );
    return ref;
}

static HRESULT WINAPI adv_watcher_GetIids( IBluetoothLEAdvertisementWatcher *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_GetRuntimeClassName( IBluetoothLEAdvertisementWatcher *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_GetTrustLevel( IBluetoothLEAdvertisementWatcher *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_get_MinSamplingInternal( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 1000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_MaxSamplingInternal( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 255000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_MinOutOfRangeTimeout( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 10000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_MaxOutOfRangeTimeout( IBluetoothLEAdvertisementWatcher *iface, TimeSpan *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    value->Duration = 600000000;
    return S_OK;
}

static HRESULT WINAPI adv_watcher_get_Status( IBluetoothLEAdvertisementWatcher *iface, BluetoothLEAdvertisementWatcherStatus *status )
{
    FIXME( "(%p, %p): stub!\n", iface, status );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_get_ScanningMode( IBluetoothLEAdvertisementWatcher *iface, BluetoothLEScanningMode *value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_put_ScanningMode( IBluetoothLEAdvertisementWatcher *iface, BluetoothLEScanningMode value )
{
    FIXME( "(%p, %d): stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_get_SignalStrengthFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothSignalStrengthFilter **filter )
{
    FIXME( "(%p, %p): stub!\n", iface, filter );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_put_SignalStrengthFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothSignalStrengthFilter *filter )
{
    FIXME( "(%p, %p): stub!\n", iface, filter );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_get_AdvertisementFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothLEAdvertisementFilter **filter )
{
    FIXME( "(%p, %p): stub!\n", iface, filter );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_put_AdvertisementFilter( IBluetoothLEAdvertisementWatcher *iface, IBluetoothLEAdvertisementFilter *filter )
{
    FIXME( "(%p, %p): stub!\n", iface, filter );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_Start( IBluetoothLEAdvertisementWatcher *iface )
{
    FIXME( "(%p): stub!\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_Stop( IBluetoothLEAdvertisementWatcher *iface )
{
    FIXME( "(%p): stub!\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_add_Received( IBluetoothLEAdvertisementWatcher *iface,
                                                ITypedEventHandler_BluetoothLEAdvertisementWatcher_BluetoothLEAdvertisementReceivedEventArgs *handler,
                                                EventRegistrationToken *token )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, handler, token );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_remove_Received( IBluetoothLEAdvertisementWatcher *iface, EventRegistrationToken token )
{
    FIXME( "(%p, %I64x): stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_add_Stopped( IBluetoothLEAdvertisementWatcher *iface,
                                               ITypedEventHandler_BluetoothLEAdvertisementWatcher_BluetoothLEAdvertisementWatcherStoppedEventArgs *handler,
                                               EventRegistrationToken *token )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, handler, token );
    return E_NOTIMPL;
}


static HRESULT WINAPI adv_watcher_remove_Stopped( IBluetoothLEAdvertisementWatcher *iface, EventRegistrationToken token )
{
    FIXME( "(%p, %I64x): stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static const IBluetoothLEAdvertisementWatcherVtbl adv_watcher_vtbl =
{
    /* IUnknown */
    adv_watcher_QueryInterface,
    adv_watcher_AddRef,
    adv_watcher_Release,
    /* IInspectable */
    adv_watcher_GetIids,
    adv_watcher_GetRuntimeClassName,
    adv_watcher_GetTrustLevel,
    /* IBluetoothLEAdvertisementWatcher */
    adv_watcher_get_MinSamplingInternal,
    adv_watcher_get_MaxSamplingInternal,
    adv_watcher_get_MinOutOfRangeTimeout,
    adv_watcher_get_MaxOutOfRangeTimeout,
    adv_watcher_get_Status,
    adv_watcher_get_ScanningMode,
    adv_watcher_put_ScanningMode,
    adv_watcher_get_SignalStrengthFilter,
    adv_watcher_put_SignalStrengthFilter,
    adv_watcher_get_AdvertisementFilter,
    adv_watcher_put_AdvertisementFilter,
    adv_watcher_Start,
    adv_watcher_Stop,
    adv_watcher_add_Received,
    adv_watcher_remove_Received,
    adv_watcher_add_Stopped,
    adv_watcher_remove_Stopped
};

static HRESULT adv_watcher_create( IBluetoothLEAdvertisementWatcher **watcher )
{
    struct adv_watcher *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IBluetoothLEAdvertisementWatcher_iface.lpVtbl = &adv_watcher_vtbl;
    impl->ref = 1;
    *watcher = &impl->IBluetoothLEAdvertisementWatcher_iface;
    return S_OK;
}

#endif

struct adv_watcher_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct adv_watcher_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct adv_watcher_factory, IActivationFactory_iface );
}

static HRESULT WINAPI adv_watcher_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct adv_watcher_factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef(( *out = &impl->IActivationFactory_iface ));
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI adv_watcher_factory_AddRef( IActivationFactory *iface )
{
    struct adv_watcher_factory *impl = impl_from_IActivationFactory( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI adv_watcher_factory_Release( IActivationFactory *iface )
{
    struct adv_watcher_factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "(%p)\n", iface );
    return ref;
}

static HRESULT WINAPI adv_watcher_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI adv_watcher_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    TRACE( "(%p, %p)\n", iface, instance );
    return adv_watcher_create( (IBluetoothLEAdvertisementWatcher **)instance );
}

static const struct IActivationFactoryVtbl adv_watcher_factory_vtbl =
{
    adv_watcher_factory_QueryInterface,
    adv_watcher_factory_AddRef,
    adv_watcher_factory_Release,
    /* IInspectable */
    adv_watcher_factory_GetIids,
    adv_watcher_factory_GetRuntimeClassName,
    adv_watcher_factory_GetTrustLevel,
    /* IActivationFactory */
    adv_watcher_factory_ActivateInstance
};

static struct adv_watcher_factory adv_watcher_factory =
{
    {&adv_watcher_factory_vtbl},
    1
};

IActivationFactory *advertisement_watcher_factory = &adv_watcher_factory.IActivationFactory_iface;
