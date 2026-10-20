/*
 * WoW64 registry functions
 *
 * Copyright 2021 Alexandre Julliard
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

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winnt.h"
#include "winternl.h"
#include "wow64_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wow);

static const UNICODE_STRING wow6432node_str = RTL_CONSTANT_STRING( L"Wow6432Node" );
static const UNICODE_STRING registry_str = RTL_CONSTANT_STRING( L"\\REGISTRY" );
static const UNICODE_STRING classes_str = RTL_CONSTANT_STRING( L"\\REGISTRY\\MACHINE\\SOFTWARE\\Classes" );
static const UNICODE_STRING classes_wow64_str = RTL_CONSTANT_STRING( L"\\REGISTRY\\MACHINE\\SOFTWARE\\Classes\\Wow6432Node" );

static USHORT element_length( const UNICODE_STRING *name )
{
    USHORT len = 0;

    while (len < name->Length && name->Buffer[len / sizeof(WCHAR)] != '\\') len += sizeof(WCHAR);
    return len;
}

static void skip_element( UNICODE_STRING *name, USHORT len )
{
    while (len < name->Length && name->Buffer[len / sizeof(WCHAR)] == '\\') len += sizeof(WCHAR);
    name->Buffer += len / sizeof(WCHAR);
    name->Length -= len;
    name->MaximumLength = name->Length;
}

static BOOL is_wow6432node_element( const UNICODE_STRING *name )
{
    UNICODE_STRING element = { element_length( name ), element_length( name ), name->Buffer };

    return RtlEqualUnicodeString( &element, &wow6432node_str, TRUE );
}

static NTSTATUS open_relative( HANDLE *ret, HANDLE root, const UNICODE_STRING *name, ACCESS_MASK access )
{
    OBJECT_ATTRIBUTES attr;

    InitializeObjectAttributes( &attr, (UNICODE_STRING *)name, OBJ_CASE_INSENSITIVE, root, NULL );
    return NtOpenKeyEx( ret, access, &attr, 0 );
}

static HANDLE open_wow6432node( HANDLE key, ACCESS_MASK access )
{
    HANDLE ret;

    if (open_relative( &ret, key, &wow6432node_str, access )) return 0;
    return ret;
}

static NTSTATUS final_open( HANDLE *ret, ACCESS_MASK access, OBJECT_ATTRIBUTES *attr, ULONG options, HANDLE transaction )
{
    if (transaction) return NtOpenKeyTransactedEx( ret, access, attr, options, transaction );
    return NtOpenKeyEx( ret, access, attr, options );
}

static NTSTATUS final_create( HANDLE *ret, ACCESS_MASK access, OBJECT_ATTRIBUTES *attr, ULONG index,
                              UNICODE_STRING *class, ULONG options, HANDLE transaction, ULONG *dispos )
{
    if (transaction) return NtCreateKeyTransacted( ret, access, attr, index, class, options, transaction, dispos );
    return NtCreateKey( ret, access, attr, index, class, options, dispos );
}

static NTSTATUS final_open_relative( HANDLE *ret, HANDLE root, const UNICODE_STRING *name, ACCESS_MASK access,
                                     ULONG attributes, HANDLE transaction )
{
    OBJECT_ATTRIBUTES attr;

    InitializeObjectAttributes( &attr, (UNICODE_STRING *)name, OBJ_CASE_INSENSITIVE | attributes, root, NULL );
    return final_open( ret, access, &attr, 0, transaction );
}

static BOOL key_has_name( HANDLE key, const UNICODE_STRING *expect )
{
    char buffer[sizeof(KEY_NAME_INFORMATION) + 128 * sizeof(WCHAR)];
    KEY_NAME_INFORMATION *info = (KEY_NAME_INFORMATION *)buffer;
    UNICODE_STRING name;
    ULONG len;

    if (NtQueryKey( key, KeyNameInformation, info, sizeof(buffer), &len )) return FALSE;
    name.Buffer = info->Name;
    name.Length = name.MaximumLength = info->NameLength;
    return RtlEqualUnicodeString( &name, expect, TRUE );
}

static NTSTATUS open_shared_classes( HANDLE *ret )
{
    return open_relative( ret, 0, &classes_str, MAXIMUM_ALLOWED );
}

static NTSTATUS open_view_by_name( HANDLE *ret, HANDLE key )
{
    char buffer[sizeof(KEY_NAME_INFORMATION) + 512 * sizeof(WCHAR)];
    KEY_NAME_INFORMATION *info = (KEY_NAME_INFORMATION *)buffer;
    UNICODE_STRING name, element;
    HANDLE current, child, view;
    NTSTATUS status;
    ULONG len;

    if ((status = NtQueryKey( key, KeyNameInformation, info, sizeof(buffer), &len ))) return status;
    name.Buffer = info->Name;
    name.Length = name.MaximumLength = info->NameLength;
    element.Buffer = name.Buffer;
    element.Length = element.MaximumLength = min( name.Length, registry_str.Length );
    if (!RtlEqualUnicodeString( &element, &registry_str, TRUE )) return STATUS_OBJECT_NAME_NOT_FOUND;
    if ((status = open_relative( &current, 0, &registry_str, MAXIMUM_ALLOWED ))) return status;
    skip_element( &name, registry_str.Length );

    while (name.Length)
    {
        BOOL descend = !is_wow6432node_element( &name );

        element.Buffer = name.Buffer;
        element.Length = element.MaximumLength = element_length( &name );
        status = open_relative( &child, current, &element, MAXIMUM_ALLOWED );
        NtClose( current );
        if (status) return status;
        skip_element( &name, element.Length );
        if (descend && (!name.Length || !is_wow6432node_element( &name )) && (view = open_wow6432node( child, MAXIMUM_ALLOWED )))
        {
            NtClose( child );
            child = view;
        }
        current = child;
    }
    *ret = current;
    return STATUS_SUCCESS;
}

static BOOL is_valid_key_path( const UNICODE_STRING *name )
{
    UNICODE_STRING rest = *name;
    USHORT len;

    if ((name->Length & 1) || name->Length > name->MaximumLength || (name->Length && !name->Buffer)) return FALSE;
    skip_element( &rest, 0 );
    while (rest.Length)
    {
        len = element_length( &rest );
        if (len > 255 * sizeof(WCHAR)) return FALSE;
        skip_element( &rest, len );
        if (!len && rest.Length) return FALSE;
    }
    return TRUE;
}

static NTSTATUS open_redirected_key( HANDLE *ret, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr, ULONG options,
                                     BOOL create, ULONG index, UNICODE_STRING *class, HANDLE transaction, ULONG *dispos )
{
    ACCESS_MASK native_access = access & ~KEY_WOW64_RES;
    UNICODE_STRING name, element;
    OBJECT_ATTRIBUTES final_attr;
    HANDLE current, owned = 0, child, view, shared;
    BOOL final_is_wow6432node, wowshare;
    NTSTATUS status;

    if (!attr || attr->Length != sizeof(*attr) || !attr->ObjectName || !is_valid_key_path( attr->ObjectName ))
        return STATUS_NOT_SUPPORTED;
    name = *attr->ObjectName;
    current = attr->RootDirectory;

    if (!current)
    {
        element.Buffer = name.Buffer;
        element.Length = element.MaximumLength = min( name.Length, registry_str.Length );
        if (!RtlEqualUnicodeString( &element, &registry_str, TRUE ) ||
            (name.Length > registry_str.Length && name.Buffer[registry_str.Length / sizeof(WCHAR)] != '\\'))
            return STATUS_NOT_SUPPORTED;
        if (open_relative( &owned, 0, &registry_str, MAXIMUM_ALLOWED )) return STATUS_NOT_SUPPORTED;
        current = owned;
        skip_element( &name, registry_str.Length );
    }
    else if (name.Length && name.Buffer[0] == '\\')
    {
        return STATUS_NOT_SUPPORTED;
    }
    else if (!name.Length || !is_wow6432node_element( &name ))
    {
        if (access & KEY_WOW64_32KEY)
        {
            if (!open_view_by_name( &view, current ))
            {
                owned = current = view;
            }
        }
        else if (key_has_name( current, &classes_str ) && (view = open_wow6432node( current, MAXIMUM_ALLOWED )))
        {
            owned = current = view;
        }
    }

    for (;;)
    {
        element.Buffer = name.Buffer;
        element.Length = element.MaximumLength = element_length( &name );
        skip_element( &name, element.Length );
        if (!name.Length) break;

        status = open_relative( &child, current, &element, MAXIMUM_ALLOWED );
        if (status == STATUS_OBJECT_NAME_NOT_FOUND && key_has_name( current, &classes_wow64_str ) &&
            !open_shared_classes( &shared ))
        {
            status = open_relative( &child, shared, &element, MAXIMUM_ALLOWED );
            NtClose( shared );
        }
        if (status)
        {
            if (owned) NtClose( owned );
            return status == STATUS_ACCESS_DENIED ? STATUS_NOT_SUPPORTED : status;
        }
        if (!is_wow6432node_element( &name ) && (view = open_wow6432node( child, MAXIMUM_ALLOWED )))
        {
            NtClose( child );
            child = view;
        }
        if (owned) NtClose( owned );
        owned = current = child;
    }

    final_is_wow6432node = element.Length && RtlEqualUnicodeString( &element, &wow6432node_str, TRUE );
    InitializeObjectAttributes( &final_attr, &element, attr->Attributes, current, attr->SecurityDescriptor );
    final_attr.SecurityQualityOfService = attr->SecurityQualityOfService;

    if (create && (options & REG_OPTION_CREATE_LINK))
    {
        status = final_create( ret, native_access, &final_attr, index, class, options, transaction, dispos );
        if (owned) NtClose( owned );
        return status;
    }

    wowshare = key_has_name( current, &classes_wow64_str );
    if (create && !wowshare)
    {
        status = final_create( ret, native_access, &final_attr, index, class, options, transaction, dispos );
    }
    else
    {
        status = final_open( ret, native_access, &final_attr, create ? 0 : options, transaction );
        if (status == STATUS_OBJECT_NAME_NOT_FOUND && wowshare && !open_shared_classes( &shared ))
        {
            final_attr.RootDirectory = shared;
            if (create) status = final_create( ret, native_access, &final_attr, index, class, options, transaction, dispos );
            else status = final_open( ret, native_access, &final_attr, options, transaction );
            final_attr.RootDirectory = current;
            NtClose( shared );
        }
        else if (create && !status && dispos)
        {
            *dispos = REG_OPENED_EXISTING_KEY;
        }
    }

    if (!status && !final_is_wow6432node)
    {
        if (key_has_name( *ret, &classes_wow64_str ))
        {
            if (!final_open_relative( &shared, 0, &classes_str, native_access, attr->Attributes & OBJ_INHERIT, transaction ))
            {
                NtClose( *ret );
                *ret = shared;
            }
        }
        else if (!final_open_relative( &view, *ret, &wow6432node_str, native_access, attr->Attributes & OBJ_INHERIT, transaction ))
        {
            if (key_has_name( view, &classes_wow64_str )) NtClose( view );
            else
            {
                NtClose( *ret );
                *ret = view;
            }
        }
    }

    if (owned) NtClose( owned );
    return status;
}

static NTSTATUS wow64_open_key( HANDLE *ret, ACCESS_MASK access, OBJECT_ATTRIBUTES *attr, ULONG options,
                                HANDLE transaction )
{
    NTSTATUS status;

    if (!(access & KEY_WOW64_64KEY))
    {
        status = open_redirected_key( ret, access, attr, options, FALSE, 0, NULL, transaction, NULL );
        if (status != STATUS_NOT_SUPPORTED) return status;
    }
    return final_open( ret, access, attr, options, transaction );
}

static NTSTATUS wow64_create_key( HANDLE *ret, ACCESS_MASK access, OBJECT_ATTRIBUTES *attr, ULONG index,
                                  UNICODE_STRING *class, ULONG options, HANDLE transaction, ULONG *dispos )
{
    NTSTATUS status;

    if (!(access & KEY_WOW64_64KEY))
    {
        status = open_redirected_key( ret, access, attr, options, TRUE, index, class, transaction, dispos );
        if (status != STATUS_NOT_SUPPORTED) return status;
    }
    return final_create( ret, access, attr, index, class, options, transaction, dispos );
}


/**********************************************************************
 *           wow64_NtCreateKey
 */
NTSTATUS WINAPI wow64_NtCreateKey( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG index = get_ulong( &args );
    UNICODE_STRING32 *class32 = get_ptr( &args );
    ULONG options = get_ulong( &args );
    ULONG *dispos = get_ptr( &args );

    struct object_attr64 attr;
    UNICODE_STRING class;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = wow64_create_key( &handle, access, objattr_32to64( &attr, attr32 ), index, unicode_str_32to64( &class, class32 ), options, NULL, dispos );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtCreateKeyTransacted
 */
NTSTATUS WINAPI wow64_NtCreateKeyTransacted( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG index = get_ulong( &args );
    UNICODE_STRING32 *class32 = get_ptr( &args );
    ULONG options = get_ulong( &args );
    HANDLE transacted = get_handle( &args );
    ULONG *dispos = get_ptr( &args );

    struct object_attr64 attr;
    UNICODE_STRING class;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = wow64_create_key( &handle, access, objattr_32to64( &attr, attr32 ), index, unicode_str_32to64( &class, class32 ), options, transacted, dispos );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtDeleteKey
 */
NTSTATUS WINAPI wow64_NtDeleteKey( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtDeleteKey( handle );
}


/**********************************************************************
 *           wow64_NtDeleteValueKey
 */
NTSTATUS WINAPI wow64_NtDeleteValueKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    UNICODE_STRING32 *str32 = get_ptr( &args );

    UNICODE_STRING str;

    return NtDeleteValueKey( handle, unicode_str_32to64( &str, str32 ));
}


/**********************************************************************
 *           wow64_NtEnumerateKey
 */
NTSTATUS WINAPI wow64_NtEnumerateKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    ULONG index = get_ulong( &args );
    KEY_INFORMATION_CLASS class = get_ulong( &args );
    void *ptr = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG *retlen = get_ptr( &args );

    return NtEnumerateKey( handle, index, class, ptr, len, retlen );
}


/**********************************************************************
 *           wow64_NtEnumerateValueKey
 */
NTSTATUS WINAPI wow64_NtEnumerateValueKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    ULONG index = get_ulong( &args );
    KEY_VALUE_INFORMATION_CLASS class = get_ulong( &args );
    void *ptr = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG *retlen = get_ptr( &args );

    return NtEnumerateValueKey( handle, index, class, ptr, len, retlen );
}


/**********************************************************************
 *           wow64_NtFlushKey
 */
NTSTATUS WINAPI wow64_NtFlushKey( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtFlushKey( handle );
}


/**********************************************************************
 *           wow64_NtLoadKey
 */
NTSTATUS WINAPI wow64_NtLoadKey( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    OBJECT_ATTRIBUTES32 *file32 = get_ptr( &args );

    struct object_attr64 attr, file;

    return NtLoadKey( objattr_32to64( &attr, attr32 ), objattr_32to64( &file, file32 ));
}


/**********************************************************************
 *           wow64_NtLoadKey2
 */
NTSTATUS WINAPI wow64_NtLoadKey2( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    OBJECT_ATTRIBUTES32 *file32 = get_ptr( &args );
    ULONG flags = get_ulong( &args );

    struct object_attr64 attr, file;

    return NtLoadKey2( objattr_32to64( &attr, attr32 ), objattr_32to64( &file, file32 ), flags );
}

/**********************************************************************
 *           wow64_NtLoadKeyEx
 */
NTSTATUS WINAPI wow64_NtLoadKeyEx( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    OBJECT_ATTRIBUTES32 *file32 = get_ptr( &args );
    ULONG flags = get_ulong( &args );
    HANDLE trustkey = get_handle( &args );
    HANDLE event = get_handle( &args );
    ACCESS_MASK desired_access = get_ulong( &args );
    HANDLE *rootkey = get_ptr( &args );
    IO_STATUS_BLOCK32 *io32 = get_ptr( &args );

    struct object_attr64 attr, file;
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    status = NtLoadKeyEx( objattr_32to64( &attr, attr32 ), objattr_32to64( &file, file32 ), flags, trustkey, event, desired_access, rootkey, iosb_32to64( &io, io32 ) );
    put_iosb( io32, &io );
    return status;
}


/**********************************************************************
 *           wow64_NtNotifyChangeKey
 */
NTSTATUS WINAPI wow64_NtNotifyChangeKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    HANDLE event = get_handle( &args );
    ULONG apc = get_ulong( &args );
    ULONG apc_param = get_ulong( &args );
    IO_STATUS_BLOCK32 *io32 = get_ptr( &args );
    ULONG filter = get_ulong( &args );
    BOOLEAN subtree = get_ulong( &args );
    void *buffer = get_ptr( &args );
    ULONG len = get_ulong( &args );
    BOOLEAN async = get_ulong( &args );

    IO_STATUS_BLOCK io;
    NTSTATUS status;

    status = NtNotifyChangeKey( handle, event, apc_32to64( apc ), apc_param_32to64( apc, apc_param ), iosb_32to64( &io, io32 ), filter, subtree, buffer, len, async );
    put_iosb( io32, &io );
    return status;
}


/**********************************************************************
 *           wow64_NtNotifyChangeMultipleKeys
 */
NTSTATUS WINAPI wow64_NtNotifyChangeMultipleKeys( UINT *args )
{
    HANDLE handle = get_handle( &args );
    ULONG count = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE event = get_handle( &args );
    ULONG apc = get_ulong( &args );
    ULONG apc_param = get_ulong( &args );
    IO_STATUS_BLOCK32 *io32 = get_ptr( &args );
    ULONG filter = get_ulong( &args );
    BOOLEAN subtree = get_ulong( &args );
    void *buffer = get_ptr( &args );
    ULONG len = get_ulong( &args );
    BOOLEAN async = get_ulong( &args );

    struct object_attr64 attr;
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    status = NtNotifyChangeMultipleKeys( handle, count, objattr_32to64( &attr, attr32 ), event, apc_32to64( apc ), apc_param_32to64( apc, apc_param ), iosb_32to64( &io, io32 ), filter, subtree, buffer, len, async );
    put_iosb( io32, &io );
    return status;
}


/**********************************************************************
 *           wow64_NtOpenKey
 */
NTSTATUS WINAPI wow64_NtOpenKey( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = wow64_open_key( &handle, access, objattr_32to64( &attr, attr32 ), 0, NULL );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtOpenKeyEx
 */
NTSTATUS WINAPI wow64_NtOpenKeyEx( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG options = get_ulong( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = wow64_open_key( &handle, access, objattr_32to64( &attr, attr32 ), options, NULL );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtOpenKeyTransacted
 */
NTSTATUS WINAPI wow64_NtOpenKeyTransacted( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE transaction = get_handle( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = wow64_open_key( &handle, access, objattr_32to64( &attr, attr32 ), 0, transaction );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtOpenKeyTransactedEx
 */
NTSTATUS WINAPI wow64_NtOpenKeyTransactedEx( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG options = get_ulong( &args );
    HANDLE transaction = get_handle( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = wow64_open_key( &handle, access, objattr_32to64( &attr, attr32 ), options, transaction );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtQueryKey
 */
NTSTATUS WINAPI wow64_NtQueryKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    KEY_INFORMATION_CLASS class = get_ulong( &args );
    void *info = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG *retlen = get_ptr( &args );

    return NtQueryKey( handle, class, info, len, retlen );
}


/**********************************************************************
 *           wow64_NtQueryMultipleValueKey
 */
NTSTATUS WINAPI wow64_NtQueryMultipleValueKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    KEY_MULTIPLE_VALUE_INFORMATION *info = get_ptr( &args );
    ULONG count = get_ulong( &args );
    void *ptr = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG *retlen = get_ptr( &args );

    FIXME( "%p %p %lu %p %lu %p: stub\n", handle, info, count, ptr, len, retlen );
    return STATUS_SUCCESS;
}


/**********************************************************************
 *           wow64_NtQueryValueKey
 */
NTSTATUS WINAPI wow64_NtQueryValueKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    UNICODE_STRING32 *str32 = get_ptr( &args );
    KEY_VALUE_INFORMATION_CLASS class = get_ulong( &args );
    void *ptr = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG *retlen = get_ptr( &args );

    UNICODE_STRING str;

    return NtQueryValueKey( handle, unicode_str_32to64( &str, str32 ), class, ptr, len, retlen );
}


/**********************************************************************
 *           wow64_NtRenameKey
 */
NTSTATUS WINAPI wow64_NtRenameKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    UNICODE_STRING32 *str32 = get_ptr( &args );

    UNICODE_STRING str;

    return NtRenameKey( handle, unicode_str_32to64( &str, str32 ));
}


/**********************************************************************
 *           wow64_NtReplaceKey
 */
NTSTATUS WINAPI wow64_NtReplaceKey( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE handle = get_handle( &args );
    OBJECT_ATTRIBUTES32 *replace32 = get_ptr( &args );

    struct object_attr64 attr, replace;

    return NtReplaceKey( objattr_32to64( &attr, attr32 ), handle, objattr_32to64( &replace, replace32 ));
}


/**********************************************************************
 *           wow64_NtRestoreKey
 */
NTSTATUS WINAPI wow64_NtRestoreKey( UINT *args )
{
    HANDLE key = get_handle( &args );
    HANDLE file = get_handle( &args );
    ULONG flags = get_ulong( &args );

    return NtRestoreKey( key, file, flags );
}


/**********************************************************************
 *           wow64_NtSaveKey
 */
NTSTATUS WINAPI wow64_NtSaveKey( UINT *args )
{
    HANDLE key = get_handle( &args );
    HANDLE file = get_handle( &args );

    return NtSaveKey( key, file );
}


/**********************************************************************
 *           wow64_NtSetInformationKey
 */
NTSTATUS WINAPI wow64_NtSetInformationKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    int class = get_ulong( &args );
    void *info = get_ptr( &args );
    ULONG len = get_ulong( &args );

    return NtSetInformationKey( handle, class, info, len );
}


/**********************************************************************
 *           wow64_NtSetValueKey
 */
NTSTATUS WINAPI wow64_NtSetValueKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    const UNICODE_STRING32 *str32 = get_ptr( &args );
    ULONG index = get_ulong( &args );
    ULONG type = get_ulong( &args );
    const void *data = get_ptr( &args );
    ULONG count = get_ulong( &args );

    UNICODE_STRING str;

    return NtSetValueKey( handle, unicode_str_32to64( &str, str32 ), index, type, data, count );
}


/**********************************************************************
 *           wow64_NtUnloadKey
 */
NTSTATUS WINAPI wow64_NtUnloadKey( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );

    struct object_attr64 attr;

    return NtUnloadKey( objattr_32to64( &attr, attr32 ));
}
