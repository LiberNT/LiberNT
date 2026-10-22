/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.Foundation.Collections PropertySet and ValueSet observable maps
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <winstring.h>
#include <objbase.h>
#include <wine/debug.h>
#include <activation.h>

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL( wintypes );

struct map_handler
{
    IMapChangedEventHandler_HSTRING_IInspectable *handler;
    EventRegistrationToken token;
};

struct observable_map
{
    IPropertySet IPropertySet_iface;
    IObservableMap_HSTRING_IInspectable IObservableMap_HSTRING_IInspectable_iface;
    IMap_HSTRING_IInspectable IMap_HSTRING_IInspectable_iface;
    IIterable_IKeyValuePair_HSTRING_IInspectable IIterable_IKeyValuePair_HSTRING_IInspectable_iface;
    LONG ref;

    BOOL value_set;
    IInspectable *map_inner;
    IMap_HSTRING_IInspectable *map;
    IIterable_IKeyValuePair_HSTRING_IInspectable *iterable;

    SRWLOCK lock;
    struct map_handler *handlers;
    UINT32 handler_count;
    UINT32 handler_capacity;
    LONG64 next_token;
};

struct map_changed_args
{
    IMapChangedEventArgs_HSTRING IMapChangedEventArgs_HSTRING_iface;
    LONG ref;
    CollectionChange change;
    HSTRING key;
};

static inline struct observable_map *impl_from_IPropertySet( IPropertySet *iface )
{
    return CONTAINING_RECORD( iface, struct observable_map, IPropertySet_iface );
}

static inline struct map_changed_args *impl_from_IMapChangedEventArgs_HSTRING( IMapChangedEventArgs_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct map_changed_args, IMapChangedEventArgs_HSTRING_iface );
}

static HRESULT get_class_name( const WCHAR *name, HSTRING *class_name )
{
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT get_iids( const IID *const *list, ULONG count, ULONG *iid_count, IID **iids )
{
    ULONG i;

    if (!iid_count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( count * sizeof(IID) ))) return E_OUTOFMEMORY;
    for (i = 0; i < count; i++) (*iids)[i] = *list[i];
    *iid_count = count;
    return S_OK;
}

static HRESULT WINAPI map_changed_args_QueryInterface( IMapChangedEventArgs_HSTRING *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IMapChangedEventArgs_HSTRING ))
    {
        *out = iface;
        IMapChangedEventArgs_HSTRING_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI map_changed_args_AddRef( IMapChangedEventArgs_HSTRING *iface )
{
    return InterlockedIncrement( &impl_from_IMapChangedEventArgs_HSTRING( iface )->ref );
}

static ULONG WINAPI map_changed_args_Release( IMapChangedEventArgs_HSTRING *iface )
{
    struct map_changed_args *impl = impl_from_IMapChangedEventArgs_HSTRING( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->key );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI map_changed_args_GetIids( IMapChangedEventArgs_HSTRING *iface, ULONG *iid_count, IID **iids )
{
    static const IID *const list[] = { &IID_IMapChangedEventArgs_HSTRING };
    return get_iids( list, ARRAY_SIZE(list), iid_count, iids );
}

static HRESULT WINAPI map_changed_args_GetRuntimeClassName( IMapChangedEventArgs_HSTRING *iface, HSTRING *class_name )
{
    return get_class_name( L"Windows.Foundation.Collections.IMapChangedEventArgs`1<String>", class_name );
}

static HRESULT WINAPI map_changed_args_GetTrustLevel( IMapChangedEventArgs_HSTRING *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI map_changed_args_get_CollectionChanged( IMapChangedEventArgs_HSTRING *iface, CollectionChange *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IMapChangedEventArgs_HSTRING( iface )->change;
    return S_OK;
}

static HRESULT WINAPI map_changed_args_get_Key( IMapChangedEventArgs_HSTRING *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl_from_IMapChangedEventArgs_HSTRING( iface )->key, value );
}

static const IMapChangedEventArgs_HSTRINGVtbl map_changed_args_vtbl =
{
    map_changed_args_QueryInterface,
    map_changed_args_AddRef,
    map_changed_args_Release,
    map_changed_args_GetIids,
    map_changed_args_GetRuntimeClassName,
    map_changed_args_GetTrustLevel,
    map_changed_args_get_CollectionChanged,
    map_changed_args_get_Key,
};

static void raise_map_changed( struct observable_map *impl, CollectionChange change, HSTRING key )
{
    IMapChangedEventHandler_HSTRING_IInspectable **handlers = NULL;
    struct map_changed_args *args;
    UINT32 count, i;

    AcquireSRWLockShared( &impl->lock );
    if ((count = impl->handler_count) && (handlers = malloc( count * sizeof(*handlers) )))
    {
        for (i = 0; i < count; i++)
        {
            handlers[i] = impl->handlers[i].handler;
            IMapChangedEventHandler_HSTRING_IInspectable_AddRef( handlers[i] );
        }
    }
    ReleaseSRWLockShared( &impl->lock );
    if (!handlers) return;

    if ((args = calloc( 1, sizeof(*args) )))
    {
        args->IMapChangedEventArgs_HSTRING_iface.lpVtbl = &map_changed_args_vtbl;
        args->ref = 1;
        args->change = change;
        if (key) WindowsDuplicateString( key, &args->key );
        for (i = 0; i < count; i++)
            IMapChangedEventHandler_HSTRING_IInspectable_Invoke( handlers[i], &impl->IObservableMap_HSTRING_IInspectable_iface,
                                                                 &args->IMapChangedEventArgs_HSTRING_iface );
        IMapChangedEventArgs_HSTRING_Release( &args->IMapChangedEventArgs_HSTRING_iface );
    }
    for (i = 0; i < count; i++)
        IMapChangedEventHandler_HSTRING_IInspectable_Release( handlers[i] );
    free( handlers );
}

static HRESULT WINAPI observable_map_QueryInterface( IPropertySet *iface, REFIID iid, void **out )
{
    struct observable_map *impl = impl_from_IPropertySet( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertySet ))
        *out = &impl->IPropertySet_iface;
    else if (IsEqualGUID( iid, &IID_IObservableMap_HSTRING_IInspectable ))
        *out = &impl->IObservableMap_HSTRING_IInspectable_iface;
    else if (IsEqualGUID( iid, &IID_IMap_HSTRING_IInspectable ))
        *out = &impl->IMap_HSTRING_IInspectable_iface;
    else if (IsEqualGUID( iid, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable ))
        *out = &impl->IIterable_IKeyValuePair_HSTRING_IInspectable_iface;
    else
    {
        WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}

static ULONG WINAPI observable_map_AddRef( IPropertySet *iface )
{
    return InterlockedIncrement( &impl_from_IPropertySet( iface )->ref );
}

static ULONG WINAPI observable_map_Release( IPropertySet *iface )
{
    struct observable_map *impl = impl_from_IPropertySet( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    UINT32 i;

    if (!ref)
    {
        InterlockedIncrement( &impl->ref );
        for (i = 0; i < impl->handler_count; i++)
            IMapChangedEventHandler_HSTRING_IInspectable_Release( impl->handlers[i].handler );
        free( impl->handlers );
        IInspectable_Release( impl->map_inner );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI observable_map_GetIids( IPropertySet *iface, ULONG *iid_count, IID **iids )
{
    static const IID *const list[] =
    {
        &IID_IPropertySet,
        &IID_IObservableMap_HSTRING_IInspectable,
        &IID_IMap_HSTRING_IInspectable,
        &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
    };
    return get_iids( list, ARRAY_SIZE(list), iid_count, iids );
}

static HRESULT WINAPI observable_map_GetRuntimeClassName( IPropertySet *iface, HSTRING *class_name )
{
    struct observable_map *impl = impl_from_IPropertySet( iface );

    return get_class_name( impl->value_set ? L"Windows.Foundation.Collections.ValueSet"
                                           : L"Windows.Foundation.Collections.PropertySet", class_name );
}

static HRESULT WINAPI observable_map_GetTrustLevel( IPropertySet *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static const IPropertySetVtbl observable_map_vtbl =
{
    observable_map_QueryInterface,
    observable_map_AddRef,
    observable_map_Release,
    observable_map_GetIids,
    observable_map_GetRuntimeClassName,
    observable_map_GetTrustLevel,
};

DEFINE_IINSPECTABLE( observable, IObservableMap_HSTRING_IInspectable, struct observable_map, IPropertySet_iface );

static HRESULT WINAPI observable_add_MapChanged( IObservableMap_HSTRING_IInspectable *iface,
                                                 IMapChangedEventHandler_HSTRING_IInspectable *handler,
                                                 EventRegistrationToken *token )
{
    struct observable_map *impl = impl_from_IObservableMap_HSTRING_IInspectable( iface );
    struct map_handler *handlers;
    UINT32 capacity;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!handler) return E_INVALIDARG;
    if (!token) return E_POINTER;

    AcquireSRWLockExclusive( &impl->lock );
    if (impl->handler_count == impl->handler_capacity)
    {
        capacity = max( 4, impl->handler_capacity * 2 );
        if (!(handlers = realloc( impl->handlers, capacity * sizeof(*handlers) )))
        {
            ReleaseSRWLockExclusive( &impl->lock );
            return E_OUTOFMEMORY;
        }
        impl->handlers = handlers;
        impl->handler_capacity = capacity;
    }
    IMapChangedEventHandler_HSTRING_IInspectable_AddRef( handler );
    impl->handlers[impl->handler_count].handler = handler;
    impl->handlers[impl->handler_count].token.value = ++impl->next_token;
    *token = impl->handlers[impl->handler_count++].token;
    ReleaseSRWLockExclusive( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI observable_remove_MapChanged( IObservableMap_HSTRING_IInspectable *iface, EventRegistrationToken token )
{
    struct observable_map *impl = impl_from_IObservableMap_HSTRING_IInspectable( iface );
    IMapChangedEventHandler_HSTRING_IInspectable *handler = NULL;
    UINT32 i;

    TRACE( "iface %p, token %I64d.\n", iface, token.value );

    AcquireSRWLockExclusive( &impl->lock );
    for (i = 0; i < impl->handler_count; i++)
    {
        if (impl->handlers[i].token.value != token.value) continue;
        handler = impl->handlers[i].handler;
        memmove( impl->handlers + i, impl->handlers + i + 1, (impl->handler_count - i - 1) * sizeof(*impl->handlers) );
        impl->handler_count--;
        break;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    if (handler) IMapChangedEventHandler_HSTRING_IInspectable_Release( handler );
    return S_OK;
}

static const IObservableMap_HSTRING_IInspectableVtbl observable_vtbl =
{
    observable_QueryInterface,
    observable_AddRef,
    observable_Release,
    observable_GetIids,
    observable_GetRuntimeClassName,
    observable_GetTrustLevel,
    observable_add_MapChanged,
    observable_remove_MapChanged,
};

DEFINE_IINSPECTABLE( map, IMap_HSTRING_IInspectable, struct observable_map, IPropertySet_iface );

static HRESULT WINAPI map_Lookup( IMap_HSTRING_IInspectable *iface, HSTRING key, IInspectable **value )
{
    return IMap_HSTRING_IInspectable_Lookup( impl_from_IMap_HSTRING_IInspectable( iface )->map, key, value );
}

static HRESULT WINAPI map_get_Size( IMap_HSTRING_IInspectable *iface, UINT32 *size )
{
    return IMap_HSTRING_IInspectable_get_Size( impl_from_IMap_HSTRING_IInspectable( iface )->map, size );
}

static HRESULT WINAPI map_HasKey( IMap_HSTRING_IInspectable *iface, HSTRING key, boolean *found )
{
    return IMap_HSTRING_IInspectable_HasKey( impl_from_IMap_HSTRING_IInspectable( iface )->map, key, found );
}

static HRESULT WINAPI map_GetView( IMap_HSTRING_IInspectable *iface, IMapView_HSTRING_IInspectable **view )
{
    return IMap_HSTRING_IInspectable_GetView( impl_from_IMap_HSTRING_IInspectable( iface )->map, view );
}

static HRESULT WINAPI map_Insert( IMap_HSTRING_IInspectable *iface, HSTRING key, IInspectable *value, boolean *replaced )
{
    struct observable_map *impl = impl_from_IMap_HSTRING_IInspectable( iface );
    boolean existed = FALSE;
    HRESULT hr;

    TRACE( "iface %p, key %s, value %p, replaced %p.\n", iface, debugstr_hstring( key ), value, replaced );

    if (!replaced) return E_POINTER;
    if (FAILED(hr = IMap_HSTRING_IInspectable_Insert( impl->map, key, value, &existed ))) return hr;
    *replaced = existed;
    raise_map_changed( impl, existed ? CollectionChange_ItemChanged : CollectionChange_ItemInserted, key );
    return S_OK;
}

static HRESULT WINAPI map_Remove( IMap_HSTRING_IInspectable *iface, HSTRING key )
{
    struct observable_map *impl = impl_from_IMap_HSTRING_IInspectable( iface );
    HRESULT hr;

    TRACE( "iface %p, key %s.\n", iface, debugstr_hstring( key ) );

    if (FAILED(hr = IMap_HSTRING_IInspectable_Remove( impl->map, key ))) return hr;
    raise_map_changed( impl, CollectionChange_ItemRemoved, key );
    return S_OK;
}

static HRESULT WINAPI map_Clear( IMap_HSTRING_IInspectable *iface )
{
    struct observable_map *impl = impl_from_IMap_HSTRING_IInspectable( iface );
    HRESULT hr;

    TRACE( "iface %p.\n", iface );

    if (FAILED(hr = IMap_HSTRING_IInspectable_Clear( impl->map ))) return hr;
    raise_map_changed( impl, CollectionChange_Reset, NULL );
    return S_OK;
}

static const IMap_HSTRING_IInspectableVtbl map_vtbl =
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

DEFINE_IINSPECTABLE( iterable, IIterable_IKeyValuePair_HSTRING_IInspectable, struct observable_map, IPropertySet_iface );

static HRESULT WINAPI iterable_First( IIterable_IKeyValuePair_HSTRING_IInspectable *iface,
                                      IIterator_IKeyValuePair_HSTRING_IInspectable **value )
{
    struct observable_map *impl = impl_from_IIterable_IKeyValuePair_HSTRING_IInspectable( iface );
    IMapView_HSTRING_IInspectable *view;
    IIterable_IKeyValuePair_HSTRING_IInspectable *iterable;
    HRESULT hr;

    if (FAILED(hr = IMap_HSTRING_IInspectable_GetView( impl->map, &view ))) return hr;
    hr = IMapView_HSTRING_IInspectable_QueryInterface( view, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
                                                       (void **)&iterable );
    IMapView_HSTRING_IInspectable_Release( view );
    if (FAILED(hr)) return hr;
    hr = IIterable_IKeyValuePair_HSTRING_IInspectable_First( iterable, value );
    IIterable_IKeyValuePair_HSTRING_IInspectable_Release( iterable );
    return hr;
}

static const IIterable_IKeyValuePair_HSTRING_IInspectableVtbl iterable_vtbl =
{
    iterable_QueryInterface,
    iterable_AddRef,
    iterable_Release,
    iterable_GetIids,
    iterable_GetRuntimeClassName,
    iterable_GetTrustLevel,
    iterable_First,
};

static HRESULT observable_map_create( BOOL value_set, IInspectable **instance )
{
    static const struct map_iids iids =
    {
        .map = &IID_IMap_HSTRING_IInspectable,
        .view = &IID_IMapView_HSTRING_IInspectable,
        .iterable = &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
        .iterator = &IID_IIterator_IKeyValuePair_HSTRING_IInspectable,
        .pair = &IID_IKeyValuePair_HSTRING_IInspectable,
    };
    struct observable_map *impl;
    HRESULT hr;

    if (!instance) return E_POINTER;
    *instance = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IPropertySet_iface.lpVtbl = &observable_map_vtbl;
    impl->IObservableMap_HSTRING_IInspectable_iface.lpVtbl = &observable_vtbl;
    impl->IMap_HSTRING_IInspectable_iface.lpVtbl = &map_vtbl;
    impl->IIterable_IKeyValuePair_HSTRING_IInspectable_iface.lpVtbl = &iterable_vtbl;
    impl->ref = 1;
    impl->value_set = value_set;
    InitializeSRWLock( &impl->lock );

    if (FAILED(hr = multi_threaded_map_create( &iids, (IInspectable *)&impl->IPropertySet_iface, &impl->map_inner )))
    {
        free( impl );
        return hr;
    }
    if (FAILED(hr = IInspectable_QueryInterface( impl->map_inner, &IID_IMap_HSTRING_IInspectable, (void **)&impl->map )))
    {
        IInspectable_Release( impl->map_inner );
        free( impl );
        return hr;
    }
    IPropertySet_Release( &impl->IPropertySet_iface );

    *instance = (IInspectable *)&impl->IPropertySet_iface;
    return S_OK;
}

struct observable_map_factory
{
    IActivationFactory IActivationFactory_iface;
    BOOL value_set;
};

static inline struct observable_map_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct observable_map_factory, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    return 2;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    return 1;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    static const IID *const list[] = { &IID_IActivationFactory };
    return get_iids( list, ARRAY_SIZE(list), iid_count, iids );
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return get_class_name( impl_from_IActivationFactory( iface )->value_set ? L"Windows.Foundation.Collections.ValueSet"
                                                                            : L"Windows.Foundation.Collections.PropertySet",
                           class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );
    return observable_map_create( impl_from_IActivationFactory( iface )->value_set, instance );
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

static struct observable_map_factory property_set_statics = { { &factory_vtbl }, FALSE };
static struct observable_map_factory value_set_statics = { { &factory_vtbl }, TRUE };

IActivationFactory *property_set_factory = &property_set_statics.IActivationFactory_iface;
IActivationFactory *value_set_factory = &value_set_statics.IActivationFactory_iface;
