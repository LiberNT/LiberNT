/*
 * PROJECT:     LiberNT Bluetooth APIs
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GATT descriptors, writes, reliable writes and value change events
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include <windef.h>
#include <winbase.h>
#include <winioctl.h>

#include <bthsdpdef.h>
#include <bluetoothapis.h>
#include <bthdef.h>
#include <bthledef.h>
#include <bluetoothleapis.h>
#include <drivers/bthleio.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL( bluetoothapis );

struct gatt_event_registration
{
    HANDLE device;
    HANDLE thread;
    DWORD thread_id;
    volatile LONG stopping;
    BOOL detached;
    PFNBLUETOOTH_GATT_EVENT_CALLBACK callback;
    void *context;
    USHORT count;
    USHORT handles[1];
};

struct reliable_write
{
    HANDLE device;
    BTH_LE_GATT_RELIABLE_WRITE_CONTEXT context;
};

static CRITICAL_SECTION reliable_cs;
static CRITICAL_SECTION_DEBUG reliable_cs_debug =
{
    0, 0, &reliable_cs,
    { &reliable_cs_debug.ProcessLocksList, &reliable_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": reliable_cs") }
};
static CRITICAL_SECTION reliable_cs = { &reliable_cs_debug, -1, 0, 0, 0, 0 };
static struct reliable_write reliable_writes[16];
static BTH_LE_GATT_RELIABLE_WRITE_CONTEXT next_reliable_context;

static HRESULT att_error_to_hresult( UCHAR error )
{
    if (error >= BTH_LE_ERROR_INVALID_HANDLE && error <= BTH_LE_ERROR_INSUFFICIENT_RESOURCES)
        return MAKE_HRESULT( SEVERITY_ERROR, FACILITY_BLUETOOTH_ATT, error );
    return E_BLUETOOTH_ATT_UNKNOWN_ERROR;
}

static BOOL gatt_ioctl( HANDLE device, DWORD code, void *in, DWORD in_size, void *out, DWORD out_size, DWORD *bytes )
{
    OVERLAPPED ovl = {0};
    BOOL ret;

    ovl.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    if (!ovl.hEvent)
        return FALSE;
    ret = DeviceIoControl( device, code, in, in_size, out, out_size, bytes, &ovl );
    if (!ret && GetLastError() == ERROR_IO_PENDING)
        ret = GetOverlappedResult( device, &ovl, bytes, TRUE );
    CloseHandle( ovl.hEvent );
    return ret;
}

HRESULT WINAPI BluetoothGATTGetDescriptors( HANDLE device, BTH_LE_GATT_CHARACTERISTIC *chrc, USHORT count,
                                            BTH_LE_GATT_DESCRIPTOR *buf, USHORT *actual, ULONG flags )
{
    BTHLE_GATT_DESCRIPTORS *descs;
    DWORD size, bytes;

    TRACE( "(%p, %p, %u, %p, %p, %#lx)\n", device, chrc, count, buf, actual, flags );

    if (!chrc)
        return E_INVALIDARG;
    if (!actual)
        return E_POINTER;
    if (buf && !count)
        return E_INVALIDARG;

    size = offsetof( BTHLE_GATT_DESCRIPTORS, Descriptors[count] );
    if (!(descs = calloc( 1, size )))
        return HRESULT_FROM_WIN32( ERROR_NO_SYSTEM_RESOURCES );
    descs->CharacteristicHandle = chrc->AttributeHandle;
    if (!gatt_ioctl( device, IOCTL_BTHLE_GATT_GET_DESCRIPTORS, descs, size, descs, size, &bytes ) &&
        GetLastError() != ERROR_MORE_DATA)
    {
        free( descs );
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    *actual = descs->Count;
    if (!descs->Count)
    {
        free( descs );
        return S_OK;
    }
    if (!buf)
    {
        free( descs );
        return HRESULT_FROM_WIN32( ERROR_MORE_DATA );
    }
    memcpy( buf, descs->Descriptors, min( count, descs->Count ) * sizeof( *buf ) );
    free( descs );
    if (count < *actual)
        return HRESULT_FROM_WIN32( ERROR_INVALID_USER_BUFFER );
    return S_OK;
}

HRESULT WINAPI BluetoothGATTGetIncludedServices( HANDLE device, BTH_LE_GATT_SERVICE *parent, USHORT count,
                                                 BTH_LE_GATT_SERVICE *buf, USHORT *actual, ULONG flags )
{
    BTHLE_GATT_INCLUDED_SERVICES *services;
    DWORD size, bytes;

    TRACE( "(%p, %p, %u, %p, %p, %#lx)\n", device, parent, count, buf, actual, flags );

    if (!actual)
        return E_POINTER;
    if (buf && !count)
        return E_INVALIDARG;

    size = offsetof( BTHLE_GATT_INCLUDED_SERVICES, Services[count] );
    if (!(services = calloc( 1, size )))
        return HRESULT_FROM_WIN32( ERROR_NO_SYSTEM_RESOURCES );
    services->ServiceHandle = parent ? parent->AttributeHandle : 0;
    if (!gatt_ioctl( device, IOCTL_BTHLE_GATT_GET_INCLUDED_SERVICES, services, size, services, size, &bytes ) &&
        GetLastError() != ERROR_MORE_DATA)
    {
        free( services );
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    *actual = services->Count;
    if (!services->Count)
    {
        free( services );
        return S_OK;
    }
    if (!buf)
    {
        free( services );
        return HRESULT_FROM_WIN32( ERROR_MORE_DATA );
    }
    memcpy( buf, services->Services, min( count, services->Count ) * sizeof( *buf ) );
    free( services );
    if (count < *actual)
        return HRESULT_FROM_WIN32( ERROR_INVALID_USER_BUFFER );
    return S_OK;
}

static BTH_LE_UUID short_uuid( const BYTE *data )
{
    BTH_LE_UUID uuid = {0};

    uuid.IsShortUuid = TRUE;
    uuid.Value.ShortUuid = data[0] | (data[1] << 8);
    return uuid;
}

HRESULT WINAPI BluetoothGATTGetDescriptorValue( HANDLE device, BTH_LE_GATT_DESCRIPTOR *desc, ULONG size,
                                                BTH_LE_GATT_DESCRIPTOR_VALUE *val, USHORT *required, ULONG flags )
{
    BYTE buffer[offsetof( BTHLE_GATT_VALUE, Data[BTH_LE_ATT_MAX_VALUE_SIZE] )];
    BTHLE_GATT_VALUE *params = (BTHLE_GATT_VALUE *)buffer;
    const BYTE *data;
    ULONG needed;
    DWORD bytes;

    TRACE( "(%p, %p, %lu, %p, %p, %#lx)\n", device, desc, size, val, required, flags );

    if (!device)
        return E_HANDLE;
    if (!desc || !(size || val || required) || (size && size < sizeof( *val )))
        return E_INVALIDARG;
    if (size && !val)
        return E_POINTER;

    memset( params, 0, offsetof( BTHLE_GATT_VALUE, Data ) );
    params->Handle = desc->AttributeHandle;
    if (flags & BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_DEVICE)
        params->Flags |= BTHLE_GATT_READ_FROM_DEVICE;
    if (flags & BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_CACHE)
        params->Flags |= BTHLE_GATT_READ_FROM_CACHE;
    if (!gatt_ioctl( device, IOCTL_BTHLE_GATT_READ_VALUE, params, offsetof( BTHLE_GATT_VALUE, Data ), params,
                     sizeof( buffer ), &bytes ))
        return HRESULT_FROM_WIN32( GetLastError() );
    if (params->AttError)
        return att_error_to_hresult( params->AttError );

    needed = max( offsetof( BTH_LE_GATT_DESCRIPTOR_VALUE, Data[params->Size] ), sizeof( *val ) );
    if (required)
        *required = needed;
    if (!val)
        return HRESULT_FROM_WIN32( ERROR_MORE_DATA );
    if (size < needed)
        return HRESULT_FROM_WIN32( ERROR_INVALID_USER_BUFFER );

    data = params->Data;
    memset( val, 0, offsetof( BTH_LE_GATT_DESCRIPTOR_VALUE, Data ) );
    val->DescriptorType = desc->DescriptorType;
    val->DescriptorUuid = desc->DescriptorUuid;
    switch (desc->DescriptorType)
    {
    case CharacteristicExtendedProperties:
        if (params->Size >= 1)
        {
            val->CharacteristicExtendedProperties.IsReliableWriteEnabled = !!(data[0] & 0x01);
            val->CharacteristicExtendedProperties.IsAuxiliariesWritable = !!(data[0] & 0x02);
        }
        break;
    case ClientCharacteristicConfiguration:
        if (params->Size >= 1)
        {
            val->ClientCharacteristicConfiguration.IsSubscribeToNotification = !!(data[0] & 0x01);
            val->ClientCharacteristicConfiguration.IsSubscribeToIndication = !!(data[0] & 0x02);
        }
        break;
    case ServerCharacteristicConfiguration:
        if (params->Size >= 1)
            val->ServerCharacteristicConfiguration.IsBroadcast = !!(data[0] & 0x01);
        break;
    case CharacteristicFormat:
        if (params->Size >= 7)
        {
            val->CharacteristicFormat.Format = data[0];
            val->CharacteristicFormat.Exponent = data[1];
            val->CharacteristicFormat.Unit = short_uuid( data + 2 );
            val->CharacteristicFormat.NameSpace = data[4];
            val->CharacteristicFormat.Description = short_uuid( data + 5 );
        }
        break;
    default:
        break;
    }
    val->DataSize = params->Size;
    memcpy( val->Data, params->Data, params->Size );
    return S_OK;
}

static HRESULT gatt_write( HANDLE device, USHORT handle, const BYTE *data, ULONG size, ULONG write_flags )
{
    BYTE buffer[offsetof( BTHLE_GATT_VALUE, Data[BTH_LE_ATT_MAX_VALUE_SIZE] )];
    BTHLE_GATT_VALUE *params = (BTHLE_GATT_VALUE *)buffer;
    DWORD bytes;

    if (size > BTH_LE_ATT_MAX_VALUE_SIZE)
        return E_INVALIDARG;
    memset( params, 0, offsetof( BTHLE_GATT_VALUE, Data ) );
    params->Handle = handle;
    params->Flags = write_flags;
    params->Size = size;
    memcpy( params->Data, data, size );
    if (!gatt_ioctl( device, IOCTL_BTHLE_GATT_WRITE_VALUE, params, offsetof( BTHLE_GATT_VALUE, Data[size] ), params,
                     offsetof( BTHLE_GATT_VALUE, Data ), &bytes ))
        return HRESULT_FROM_WIN32( GetLastError() );
    if (params->AttError)
        return att_error_to_hresult( params->AttError );
    return S_OK;
}

static struct reliable_write *find_reliable_write( HANDLE device )
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE( reliable_writes ); i++)
    {
        if (reliable_writes[i].context && reliable_writes[i].device == device)
            return &reliable_writes[i];
    }
    return NULL;
}

HRESULT WINAPI BluetoothGATTSetCharacteristicValue( HANDLE device, BTH_LE_GATT_CHARACTERISTIC *chrc,
                                                    BTH_LE_GATT_CHARACTERISTIC_VALUE *val,
                                                    BTH_LE_GATT_RELIABLE_WRITE_CONTEXT context, ULONG flags )
{
    ULONG write_flags = 0;
    struct reliable_write *reliable;

    TRACE( "(%p, %p, %p, %s, %#lx)\n", device, chrc, val, wine_dbgstr_longlong( context ), flags );

    if (!device)
        return E_HANDLE;
    if (!chrc || !val || !IS_BLUETOOTH_GATT_FLAG_VALID( flags ))
        return E_INVALIDARG;

    if (flags & BLUETOOTH_GATT_FLAG_WRITE_WITHOUT_RESPONSE)
        write_flags |= BTHLE_GATT_WRITE_WITHOUT_RESPONSE;
    if (flags & BLUETOOTH_GATT_FLAG_SIGNED_WRITE)
        write_flags |= BTHLE_GATT_WRITE_SIGNED;
    EnterCriticalSection( &reliable_cs );
    reliable = find_reliable_write( device );
    if (context)
    {
        if (!reliable || reliable->context != context)
        {
            LeaveCriticalSection( &reliable_cs );
            return HRESULT_FROM_WIN32( ERROR_INVALID_FUNCTION );
        }
        write_flags |= BTHLE_GATT_WRITE_PREPARE;
    }
    else if (reliable)
    {
        LeaveCriticalSection( &reliable_cs );
        return HRESULT_FROM_WIN32( ERROR_INVALID_FUNCTION );
    }
    LeaveCriticalSection( &reliable_cs );
    return gatt_write( device, chrc->CharacteristicValueHandle, val->Data, val->DataSize, write_flags );
}

HRESULT WINAPI BluetoothGATTSetDescriptorValue( HANDLE device, BTH_LE_GATT_DESCRIPTOR *desc,
                                                BTH_LE_GATT_DESCRIPTOR_VALUE *val, ULONG flags )
{
    BYTE config[2] = {0};

    TRACE( "(%p, %p, %p, %#lx)\n", device, desc, val, flags );

    if (!device)
        return E_HANDLE;
    if (!desc || !val || !IS_BLUETOOTH_GATT_FLAG_VALID( flags ))
        return E_INVALIDARG;

    EnterCriticalSection( &reliable_cs );
    if (find_reliable_write( device ))
    {
        LeaveCriticalSection( &reliable_cs );
        return HRESULT_FROM_WIN32( ERROR_INVALID_FUNCTION );
    }
    LeaveCriticalSection( &reliable_cs );

    switch (desc->DescriptorType)
    {
    case ClientCharacteristicConfiguration:
        config[0] = (val->ClientCharacteristicConfiguration.IsSubscribeToNotification ? 0x01 : 0) |
                    (val->ClientCharacteristicConfiguration.IsSubscribeToIndication ? 0x02 : 0);
        return gatt_write( device, desc->AttributeHandle, config, sizeof( config ), 0 );
    case ServerCharacteristicConfiguration:
        config[0] = val->ServerCharacteristicConfiguration.IsBroadcast ? 0x01 : 0;
        return gatt_write( device, desc->AttributeHandle, config, sizeof( config ), 0 );
    default:
        return gatt_write( device, desc->AttributeHandle, val->Data, val->DataSize, 0 );
    }
}

HRESULT WINAPI BluetoothGATTBeginReliableWrite( HANDLE device, BTH_LE_GATT_RELIABLE_WRITE_CONTEXT *context, ULONG flags )
{
    unsigned int i;

    TRACE( "(%p, %p, %#lx)\n", device, context, flags );

    if (!device)
        return E_HANDLE;
    if (!context || flags)
        return E_INVALIDARG;

    EnterCriticalSection( &reliable_cs );
    if (find_reliable_write( device ))
    {
        LeaveCriticalSection( &reliable_cs );
        return HRESULT_FROM_WIN32( ERROR_INVALID_FUNCTION );
    }
    for (i = 0; i < ARRAY_SIZE( reliable_writes ); i++)
    {
        if (!reliable_writes[i].context)
        {
            reliable_writes[i].device = device;
            reliable_writes[i].context = ++next_reliable_context;
            *context = reliable_writes[i].context;
            LeaveCriticalSection( &reliable_cs );
            return S_OK;
        }
    }
    LeaveCriticalSection( &reliable_cs );
    return HRESULT_FROM_WIN32( ERROR_NO_SYSTEM_RESOURCES );
}

static HRESULT finish_reliable_write( HANDLE device, BTH_LE_GATT_RELIABLE_WRITE_CONTEXT context, BOOLEAN commit )
{
    struct reliable_write *reliable;
    BTHLE_GATT_EXECUTE_WRITE params = {0};
    DWORD bytes;

    if (!device)
        return E_HANDLE;
    EnterCriticalSection( &reliable_cs );
    reliable = find_reliable_write( device );
    if (!reliable || reliable->context != context)
    {
        LeaveCriticalSection( &reliable_cs );
        return HRESULT_FROM_WIN32( ERROR_INVALID_FUNCTION );
    }
    reliable->context = 0;
    reliable->device = NULL;
    LeaveCriticalSection( &reliable_cs );

    params.Commit = commit;
    if (!gatt_ioctl( device, IOCTL_BTHLE_GATT_EXECUTE_WRITE, &params, sizeof( params ), &params, sizeof( params ), &bytes ))
        return HRESULT_FROM_WIN32( GetLastError() );
    if (params.AttError)
        return att_error_to_hresult( params.AttError );
    return S_OK;
}

HRESULT WINAPI BluetoothGATTEndReliableWrite( HANDLE device, BTH_LE_GATT_RELIABLE_WRITE_CONTEXT context, ULONG flags )
{
    TRACE( "(%p, %s, %#lx)\n", device, wine_dbgstr_longlong( context ), flags );
    return finish_reliable_write( device, context, TRUE );
}

HRESULT WINAPI BluetoothGATTAbortReliableWrite( HANDLE device, BTH_LE_GATT_RELIABLE_WRITE_CONTEXT context, ULONG flags )
{
    TRACE( "(%p, %s, %#lx)\n", device, wine_dbgstr_longlong( context ), flags );
    return finish_reliable_write( device, context, FALSE );
}

static DWORD WINAPI gatt_event_thread( void *param )
{
    struct gatt_event_registration *reg = param;
    BTHLE_GATT_NOTIFICATION *notification;
    BYTE value_buffer[offsetof( BTH_LE_GATT_CHARACTERISTIC_VALUE, Data[BTH_LE_ATT_MAX_VALUE_SIZE] )];
    BTH_LE_GATT_CHARACTERISTIC_VALUE *value = (BTH_LE_GATT_CHARACTERISTIC_VALUE *)value_buffer;
    DWORD bytes;
    USHORT i;

    if (!(notification = calloc( 1, sizeof( *notification ) )))
        return 0;
    while (!reg->stopping)
    {
        if (!gatt_ioctl( reg->device, IOCTL_BTHLE_GATT_WAIT_NOTIFICATION, notification, sizeof( *notification ),
                         notification, sizeof( *notification ), &bytes ))
        {
            if (reg->stopping)
                break;
            if (GetLastError() == ERROR_OPERATION_ABORTED)
                continue;
            WARN( "notification wait failed %lu\n", GetLastError() );
            break;
        }
        if (!notification->Handle || reg->stopping)
            continue;
        for (i = 0; i < reg->count; i++)
        {
            BLUETOOTH_GATT_VALUE_CHANGED_EVENT event;

            if (reg->handles[i] != notification->Handle)
                continue;
            value->DataSize = notification->Size;
            memcpy( value->Data, notification->Data, notification->Size );
            event.ChangedAttributeHandle = notification->Handle;
            event.CharacteristicValueDataSize = notification->Size;
            event.CharacteristicValue = value;
            reg->callback( CharacteristicValueChangedEvent, &event, reg->context );
            break;
        }
    }
    free( notification );
    if (reg->detached)
    {
        CloseHandle( reg->thread );
        CloseHandle( reg->device );
        free( reg );
    }
    return 0;
}

HRESULT WINAPI BluetoothGATTRegisterEvent( HANDLE service, BTH_LE_GATT_EVENT_TYPE type, PVOID params,
                                           PFNBLUETOOTH_GATT_EVENT_CALLBACK callback, PVOID context,
                                           BLUETOOTH_GATT_EVENT_HANDLE *handle, ULONG flags )
{
    BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION *registration = params;
    struct gatt_event_registration *reg;
    USHORT i;

    TRACE( "(%p, %d, %p, %p, %p, %p, %#lx)\n", service, type, params, callback, context, handle, flags );

    if (!service)
        return E_HANDLE;
    if (type != CharacteristicValueChangedEvent || !registration || !registration->NumCharacteristics || !callback ||
        !handle || !IS_BLUETOOTH_GATT_FLAG_VALID( flags ))
        return E_INVALIDARG;

    reg = calloc( 1, offsetof( struct gatt_event_registration, handles[registration->NumCharacteristics] ) );
    if (!reg)
        return HRESULT_FROM_WIN32( ERROR_NO_SYSTEM_RESOURCES );
    reg->callback = callback;
    reg->context = context;
    reg->count = registration->NumCharacteristics;
    for (i = 0; i < reg->count; i++)
        reg->handles[i] = registration->Characteristics[i].CharacteristicValueHandle;
    if (!DuplicateHandle( GetCurrentProcess(), service, GetCurrentProcess(), &reg->device, 0, FALSE,
                          DUPLICATE_SAME_ACCESS ))
    {
        free( reg );
        return HRESULT_FROM_WIN32( GetLastError() );
    }
    reg->thread = CreateThread( NULL, 0, gatt_event_thread, reg, 0, &reg->thread_id );
    if (!reg->thread)
    {
        DWORD error = GetLastError();

        CloseHandle( reg->device );
        free( reg );
        return HRESULT_FROM_WIN32( error );
    }
    *handle = reg;
    return S_OK;
}

HRESULT WINAPI BluetoothGATTUnregisterEvent( BLUETOOTH_GATT_EVENT_HANDLE handle, ULONG flags )
{
    struct gatt_event_registration *reg = handle;

    TRACE( "(%p, %#lx)\n", handle, flags );

    if (!reg)
        return E_INVALIDARG;
    InterlockedExchange( &reg->stopping, 1 );
    if (reg->thread_id == GetCurrentThreadId())
    {
        reg->detached = TRUE;
        return S_OK;
    }
    while (WaitForSingleObject( reg->thread, 100 ) == WAIT_TIMEOUT)
    {
        CancelIoEx( reg->device, NULL );
        CancelSynchronousIo( reg->thread );
    }
    CloseHandle( reg->thread );
    CloseHandle( reg->device );
    free( reg );
    return S_OK;
}
