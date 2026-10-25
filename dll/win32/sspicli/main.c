/*
 * Copyright 2016 Hans Leidekker for CodeWeavers
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
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
#include <stdlib.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "rpc.h"
#include "sspi.h"
#include "ntsecapi.h"
#include "ntsecpkg.h"
#include "wincred.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(sspicli);

#define IDENTITY_ENCRYPTED_FLAGS (SEC_WINNT_AUTH_IDENTITY_FLAGS_PROCESS_ENCRYPTED | \
                                  SEC_WINNT_AUTH_IDENTITY_FLAGS_SYSTEM_PROTECTED | \
                                  SEC_WINNT_AUTH_IDENTITY_FLAGS_USER_PROTECTED)

#define IDENTITY_LOGON_FLAGS (SEC_WINNT_AUTH_IDENTITY_FLAGS_SYSTEM_PROTECTED | \
                              SEC_WINNT_AUTH_IDENTITY_FLAGS_USER_PROTECTED)

struct identity
{
    BOOL   ex;
    ULONG  char_size;
    void  *user;
    ULONG  user_len;
    void  *domain;
    ULONG  domain_len;
    void  *password;
    ULONG  password_len;
    void  *package_list;
    ULONG  package_list_len;
    ULONG *flags;
};

struct wide_identity
{
    WCHAR *user;
    ULONG  user_len;
    WCHAR *domain;
    ULONG  domain_len;
    WCHAR *password;
    ULONG  password_len;
    WCHAR *package_list;
    ULONG  package_list_len;
};

static void get_identity( void *opaque, struct identity *id )
{
    SEC_WINNT_AUTH_IDENTITY_EXW *ex = opaque;

    if (ex->Version == SEC_WINNT_AUTH_IDENTITY_VERSION)
    {
        id->ex               = TRUE;
        id->user             = ex->User;
        id->user_len         = ex->User ? ex->UserLength : 0;
        id->domain           = ex->Domain;
        id->domain_len       = ex->Domain ? ex->DomainLength : 0;
        id->password         = ex->Password;
        id->password_len     = ex->Password ? ex->PasswordLength : 0;
        id->package_list     = ex->PackageList;
        id->package_list_len = ex->PackageList ? ex->PackageListLength : 0;
        id->flags            = &ex->Flags;
    }
    else
    {
        SEC_WINNT_AUTH_IDENTITY_W *w = opaque;

        id->ex               = FALSE;
        id->user             = w->User;
        id->user_len         = w->User ? w->UserLength : 0;
        id->domain           = w->Domain;
        id->domain_len       = w->Domain ? w->DomainLength : 0;
        id->password         = w->Password;
        id->password_len     = w->Password ? w->PasswordLength : 0;
        id->package_list     = NULL;
        id->package_list_len = 0;
        id->flags            = &w->Flags;
    }
    id->char_size = (*id->flags & SEC_WINNT_AUTH_IDENTITY_ANSI) ? sizeof(char) : sizeof(WCHAR);
}

static BOOL is_encrypted( const struct identity *id )
{
    return (*id->flags & IDENTITY_ENCRYPTED_FLAGS) != 0;
}

static BOOL to_wide( const struct identity *id, const void *str, ULONG len, WCHAR **ret, ULONG *ret_len )
{
    int count;

    *ret = NULL;
    *ret_len = 0;
    if (!str) return TRUE;

    if (id->char_size == sizeof(char))
        count = len ? MultiByteToWideChar( CP_ACP, 0, str, len, NULL, 0 ) : 0;
    else
        count = len;

    if (!(*ret = malloc( (count + 1) * sizeof(WCHAR) ))) return FALSE;
    if (id->char_size == sizeof(char))
    {
        if (count) MultiByteToWideChar( CP_ACP, 0, str, len, *ret, count );
    }
    else
        memcpy( *ret, str, count * sizeof(WCHAR) );
    (*ret)[count] = 0;
    *ret_len = count;
    return TRUE;
}

static void free_wide_identity( struct wide_identity *w )
{
    free( w->user );
    free( w->domain );
    if (w->password) SecureZeroMemory( w->password, w->password_len * sizeof(WCHAR) );
    free( w->password );
    free( w->package_list );
    memset( w, 0, sizeof(*w) );
}

static BOOL get_wide_identity( const struct identity *id, struct wide_identity *w )
{
    memset( w, 0, sizeof(*w) );
    if (to_wide( id, id->user, id->user_len, &w->user, &w->user_len ) &&
        to_wide( id, id->domain, id->domain_len, &w->domain, &w->domain_len ) &&
        to_wide( id, id->password, id->password_len, &w->password, &w->password_len ) &&
        to_wide( id, id->package_list, id->package_list_len, &w->package_list, &w->package_list_len ))
        return TRUE;
    free_wide_identity( w );
    return FALSE;
}

static SEC_WINNT_AUTH_IDENTITY_EXW *alloc_marshalled_identity( ULONG flags, ULONG char_size,
    const void *user, ULONG user_len, const void *domain, ULONG domain_len,
    const void *password, ULONG password_len, const void *package_list, ULONG package_list_len )
{
    SEC_WINNT_AUTH_IDENTITY_EXW *id;
    SIZE_T size = sizeof(*id);
    BYTE *ptr;

    if (user) size += ((SIZE_T)user_len + 1) * char_size;
    if (domain) size += ((SIZE_T)domain_len + 1) * char_size;
    if (password) size += ((SIZE_T)password_len + 1) * char_size;
    if (package_list) size += ((SIZE_T)package_list_len + 1) * char_size;
    if (!(id = calloc( 1, size ))) return NULL;

    id->Version = SEC_WINNT_AUTH_IDENTITY_VERSION;
    id->Length  = sizeof(*id);
    id->Flags   = flags;
    ptr = (BYTE *)(id + 1);

    if (user)
    {
        memcpy( ptr, user, user_len * char_size );
        id->User       = (void *)ptr;
        id->UserLength = user_len;
        ptr += ((SIZE_T)user_len + 1) * char_size;
    }
    if (domain)
    {
        memcpy( ptr, domain, domain_len * char_size );
        id->Domain       = (void *)ptr;
        id->DomainLength = domain_len;
        ptr += ((SIZE_T)domain_len + 1) * char_size;
    }
    if (password)
    {
        memcpy( ptr, password, password_len * char_size );
        id->Password       = (void *)ptr;
        id->PasswordLength = password_len;
        ptr += ((SIZE_T)password_len + 1) * char_size;
    }
    if (package_list)
    {
        memcpy( ptr, package_list, package_list_len * char_size );
        id->PackageList       = (void *)ptr;
        id->PackageListLength = package_list_len;
    }
    return id;
}

static SIZE_T crypt_size( ULONG len, ULONG char_size )
{
    return ((SIZE_T)len * char_size + RTL_ENCRYPT_MEMORY_SIZE - 1) & ~(SIZE_T)(RTL_ENCRYPT_MEMORY_SIZE - 1);
}

static SIZE_T slot_size( ULONG len, ULONG char_size )
{
    return (((SIZE_T)len + 1) * char_size + RTL_ENCRYPT_MEMORY_SIZE - 1) & ~(SIZE_T)(RTL_ENCRYPT_MEMORY_SIZE - 1);
}

static NTSTATUS crypt_identity( const struct identity *id, BOOL encrypt, ULONG option )
{
    void *fields[3] = { id->user, id->domain, id->password };
    ULONG lens[3] = { id->user_len, id->domain_len, id->password_len };
    NTSTATUS status = STATUS_SUCCESS;
    unsigned int i, done;

    for (i = 0; i < ARRAYSIZE(fields); i++)
    {
        if (!fields[i] || !lens[i]) continue;
        if (encrypt)
            status = RtlEncryptMemory( fields[i], crypt_size( lens[i], id->char_size ), option );
        else
            status = RtlDecryptMemory( fields[i], crypt_size( lens[i], id->char_size ), option );
        if (status) break;
    }
    if (!status) return STATUS_SUCCESS;

    for (done = 0; done < i; done++)
    {
        if (!fields[done] || !lens[done]) continue;
        if (encrypt)
            RtlDecryptMemory( fields[done], crypt_size( lens[done], id->char_size ), option );
        else
            RtlEncryptMemory( fields[done], crypt_size( lens[done], id->char_size ), option );
    }
    return status;
}

static BOOL caller_is_local_system(void)
{
    DWORD_PTR buffer[(sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE + sizeof(DWORD_PTR) - 1) / sizeof(DWORD_PTR)];
    TOKEN_USER *user = (TOKEN_USER *)buffer;
    HANDLE token;
    DWORD size;
    BOOL ret;

    if (!OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &token ) &&
        !OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ))
        return FALSE;
    ret = GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &size ) &&
          IsWellKnownSid( user->User.Sid, WinLocalSystemSid );
    CloseHandle( token );
    return ret;
}

static BOOL strings_equal( const WCHAR *a, ULONG a_len, const WCHAR *b, ULONG b_len, BOOL ignore_case )
{
    if (!a_len || !b_len) return a_len == b_len;
    return CompareStringOrdinal( a, a_len, b, b_len, ignore_case ) == CSTR_EQUAL;
}

static BOOL list_contains( const WCHAR *list, ULONG list_len, const WCHAR *item, ULONG item_len )
{
    ULONG start = 0, end;

    while (start <= list_len)
    {
        for (end = start; end < list_len && list[end] != ','; end++);
        if (end - start == item_len && !memcmp( list + start, item, item_len * sizeof(WCHAR) )) return TRUE;
        start = end + 1;
    }
    return FALSE;
}

/***********************************************************************
 *		SspiEncodeStringsAsAuthIdentity (SECUR32.0)
 */
SECURITY_STATUS SEC_ENTRY SspiEncodeStringsAsAuthIdentity(
    const WCHAR *username, const WCHAR *domainname, const WCHAR *creds,
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE *opaque_id )
{
    SEC_WINNT_AUTH_IDENTITY_EXW *id;

    TRACE( "%s %s %s %p\n", debugstr_w(username), debugstr_w(domainname),
           debugstr_w(creds), opaque_id );

    if (!username && !domainname && !creds) return SEC_E_INVALID_TOKEN;

    id = alloc_marshalled_identity( SEC_WINNT_AUTH_IDENTITY_UNICODE | SEC_WINNT_AUTH_IDENTITY_MARSHALLED,
                                    sizeof(WCHAR),
                                    username, username ? lstrlenW( username ) : 0,
                                    domainname, domainname ? lstrlenW( domainname ) : 0,
                                    creds, creds ? lstrlenW( creds ) : 0, NULL, 0 );
    if (!id) return SEC_E_INSUFFICIENT_MEMORY;

    *opaque_id = id;
    return SEC_E_OK;
}

/***********************************************************************
 *		SspiZeroAuthIdentity (SECUR32.0)
 */
void SEC_ENTRY SspiZeroAuthIdentity( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id )
{
    struct identity id;

    TRACE( "%p\n", opaque_id );

    if (!opaque_id) return;
    get_identity( opaque_id, &id );
    if (!id.password) return;
    if (*id.flags & SEC_WINNT_AUTH_IDENTITY_FLAGS_RESERVED)
        SecureZeroMemory( id.password, crypt_size( id.password_len, id.char_size ) );
    else
        SecureZeroMemory( id.password, (SIZE_T)id.password_len * id.char_size );
}

/***********************************************************************
 *		SspiEncodeAuthIdentityAsStrings (SECUR32.0)
 */
SECURITY_STATUS SEC_ENTRY SspiEncodeAuthIdentityAsStrings(
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id, PCWSTR *username,
    PCWSTR *domainname, PCWSTR *creds )
{
    struct wide_identity w;
    struct identity id;

    TRACE( "%p %p %p %p\n", opaque_id, username, domainname, creds );

    *username = *domainname = *creds = NULL;
    get_identity( opaque_id, &id );
    if (is_encrypted( &id )) return SEC_E_INVALID_TOKEN;
    if (id.package_list_len)
        FIXME( "packed credentials for package list %s are not implemented\n",
               id.char_size == sizeof(WCHAR) ? debugstr_wn( id.package_list, id.package_list_len )
                                             : debugstr_an( id.package_list, id.package_list_len ) );

    if (!get_wide_identity( &id, &w )) return SEC_E_INSUFFICIENT_MEMORY;
    free( w.package_list );
    *username = w.user;
    *domainname = w.domain;
    *creds = w.password;
    return SEC_E_OK;
}

SECURITY_STATUS SEC_ENTRY SspiCopyAuthIdentity( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id,
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE *opaque_copy )
{
    struct identity id;
    SIZE_T header, size;
    void **fields[4];
    void *src[4];
    ULONG lens[4];
    BYTE *copy, *ptr;
    unsigned int i;
    BOOL encrypted;

    TRACE( "%p %p\n", opaque_id, opaque_copy );

    get_identity( opaque_id, &id );
    encrypted = is_encrypted( &id );
    header = id.ex ? sizeof(SEC_WINNT_AUTH_IDENTITY_EXW) : sizeof(SEC_WINNT_AUTH_IDENTITY_W);
    header = (header + RTL_ENCRYPT_MEMORY_SIZE - 1) & ~(SIZE_T)(RTL_ENCRYPT_MEMORY_SIZE - 1);

    src[0] = id.user;         lens[0] = id.user_len;
    src[1] = id.domain;       lens[1] = id.domain_len;
    src[2] = id.password;     lens[2] = id.password_len;
    src[3] = id.package_list; lens[3] = id.package_list_len;

    size = header;
    for (i = 0; i < ARRAYSIZE(src); i++)
        if (src[i]) size += slot_size( lens[i], id.char_size );
    if (!(copy = calloc( 1, size ))) return SEC_E_INSUFFICIENT_MEMORY;

    if (id.ex)
    {
        SEC_WINNT_AUTH_IDENTITY_EXW *ex = (SEC_WINNT_AUTH_IDENTITY_EXW *)copy;

        memcpy( ex, opaque_id, sizeof(*ex) );
        ex->Length = sizeof(*ex);
        fields[0] = (void **)&ex->User;
        fields[1] = (void **)&ex->Domain;
        fields[2] = (void **)&ex->Password;
        fields[3] = (void **)&ex->PackageList;
        ex->Flags = (ex->Flags & ~SEC_WINNT_AUTH_IDENTITY_MARSHALLED) | SEC_WINNT_AUTH_IDENTITY_FLAGS_RESERVED;
    }
    else
    {
        SEC_WINNT_AUTH_IDENTITY_W *w = (SEC_WINNT_AUTH_IDENTITY_W *)copy;

        memcpy( w, opaque_id, sizeof(*w) );
        fields[0] = (void **)&w->User;
        fields[1] = (void **)&w->Domain;
        fields[2] = (void **)&w->Password;
        fields[3] = NULL;
        w->Flags = (w->Flags & ~SEC_WINNT_AUTH_IDENTITY_MARSHALLED) | SEC_WINNT_AUTH_IDENTITY_FLAGS_RESERVED;
    }

    ptr = copy + header;
    for (i = 0; i < ARRAYSIZE(src); i++)
    {
        if (!fields[i]) continue;
        if (!src[i])
        {
            *fields[i] = NULL;
            continue;
        }
        if (encrypted && i < 3)
            memcpy( ptr, src[i], crypt_size( lens[i], id.char_size ) );
        else
            memcpy( ptr, src[i], (SIZE_T)lens[i] * id.char_size );
        *fields[i] = ptr;
        ptr += slot_size( lens[i], id.char_size );
    }

    *opaque_copy = copy;
    return SEC_E_OK;
}

/***********************************************************************
 *		SspiFreeAuthIdentity (SECUR32.0)
 */
void SEC_ENTRY SspiFreeAuthIdentity( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id )
{
    TRACE( "%p\n", opaque_id );
    free( opaque_id );
}

/***********************************************************************
 *		SspiLocalFree (SECUR32.0)
 */
void SEC_ENTRY SspiLocalFree( void *ptr )
{
    TRACE( "%p\n", ptr );
    free( ptr );
}

SECURITY_STATUS SEC_ENTRY SspiValidateAuthIdentity( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id )
{
    struct identity id;

    TRACE( "%p\n", opaque_id );

    get_identity( opaque_id, &id );
    if (id.ex && ((SEC_WINNT_AUTH_IDENTITY_EXW *)opaque_id)->Length < sizeof(SEC_WINNT_AUTH_IDENTITY_EXW))
        return SEC_E_INVALID_TOKEN;
    if (!(*id.flags & (SEC_WINNT_AUTH_IDENTITY_ANSI | SEC_WINNT_AUTH_IDENTITY_UNICODE)))
        return SEC_E_INVALID_TOKEN;
    return SEC_E_OK;
}

BOOLEAN SEC_ENTRY SspiIsAuthIdentityEncrypted( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id )
{
    struct identity id;

    TRACE( "%p\n", opaque_id );

    get_identity( opaque_id, &id );
    return is_encrypted( &id );
}

SECURITY_STATUS SEC_ENTRY SspiEncryptAuthIdentityEx( ULONG options, PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id )
{
    struct identity id;
    ULONG flag, rtl_option;

    TRACE( "%#lx %p\n", options, opaque_id );

    switch (options)
    {
    case SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_PROCESS:
        flag = SEC_WINNT_AUTH_IDENTITY_FLAGS_PROCESS_ENCRYPTED;
        rtl_option = RTL_ENCRYPT_OPTION_SAME_PROCESS;
        break;
    case SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_LOGON:
        flag = caller_is_local_system() ? SEC_WINNT_AUTH_IDENTITY_FLAGS_SYSTEM_PROTECTED
                                        : SEC_WINNT_AUTH_IDENTITY_FLAGS_USER_PROTECTED;
        rtl_option = RTL_ENCRYPT_OPTION_SAME_LOGON;
        break;
    default:
        if (options == 4) FIXME( "option %#lx is not implemented\n", options );
        return SEC_E_INVALID_TOKEN;
    }

    get_identity( opaque_id, &id );
    if (!(*id.flags & SEC_WINNT_AUTH_IDENTITY_FLAGS_RESERVED)) return SEC_E_INVALID_TOKEN;
    if (*id.flags & SEC_WINNT_AUTH_IDENTITY_FLAGS_PROCESS_ENCRYPTED)
        return options == SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_PROCESS ? SEC_E_OK : SEC_E_UNSUPPORTED_FUNCTION;
    if (options == SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_LOGON && (*id.flags & IDENTITY_LOGON_FLAGS))
        return SEC_E_OK;

    if (crypt_identity( &id, TRUE, rtl_option )) return SEC_E_INTERNAL_ERROR;
    *id.flags |= flag;
    return SEC_E_OK;
}

SECURITY_STATUS SEC_ENTRY SspiEncryptAuthIdentity( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id )
{
    TRACE( "%p\n", opaque_id );
    return SspiEncryptAuthIdentityEx( SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_PROCESS, opaque_id );
}

SECURITY_STATUS SEC_ENTRY SspiDecryptAuthIdentityEx( ULONG options, PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id )
{
    struct identity id;

    TRACE( "%#lx %p\n", options, opaque_id );

    if (options != SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_PROCESS &&
        options != SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_LOGON)
        return SEC_E_INVALID_TOKEN;

    get_identity( opaque_id, &id );
    if (!(*id.flags & SEC_WINNT_AUTH_IDENTITY_FLAGS_RESERVED)) return SEC_E_INVALID_TOKEN;

    if (options == SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_PROCESS)
    {
        if (!(*id.flags & SEC_WINNT_AUTH_IDENTITY_FLAGS_PROCESS_ENCRYPTED)) return SEC_E_OK;
        if (crypt_identity( &id, FALSE, RTL_ENCRYPT_OPTION_SAME_PROCESS )) return SEC_E_INTERNAL_ERROR;
        *id.flags &= ~SEC_WINNT_AUTH_IDENTITY_FLAGS_PROCESS_ENCRYPTED;
        return SEC_E_OK;
    }

    if (*id.flags & SEC_WINNT_AUTH_IDENTITY_FLAGS_PROCESS_ENCRYPTED) return SEC_E_UNSUPPORTED_FUNCTION;
    if (!(*id.flags & IDENTITY_LOGON_FLAGS)) return SEC_E_OK;
    if (crypt_identity( &id, FALSE, RTL_ENCRYPT_OPTION_SAME_LOGON )) return SEC_E_INTERNAL_ERROR;
    *id.flags &= ~IDENTITY_LOGON_FLAGS;
    return SEC_E_OK;
}

SECURITY_STATUS SEC_ENTRY SspiDecryptAuthIdentity( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id )
{
    TRACE( "%p\n", opaque_id );
    return SspiDecryptAuthIdentityEx( SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_PROCESS, opaque_id );
}

SECURITY_STATUS SEC_ENTRY SspiCompareAuthIdentities( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id1,
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id2, PBOOLEAN same_user, PBOOLEAN same_identity )
{
    struct wide_identity w1, w2;
    struct identity id1, id2;
    BOOL user, identity;

    TRACE( "%p %p %p %p\n", opaque_id1, opaque_id2, same_user, same_identity );

    if (same_user) *same_user = FALSE;
    if (same_identity) *same_identity = FALSE;

    if (!opaque_id1 || !opaque_id2)
    {
        if (!opaque_id1 && !opaque_id2)
        {
            if (same_user) *same_user = TRUE;
            if (same_identity) *same_identity = TRUE;
        }
        return SEC_E_OK;
    }

    get_identity( opaque_id1, &id1 );
    get_identity( opaque_id2, &id2 );
    if (is_encrypted( &id1 ) || is_encrypted( &id2 )) return SEC_E_INVALID_TOKEN;

    if (!get_wide_identity( &id1, &w1 )) return SEC_E_INSUFFICIENT_MEMORY;
    if (!get_wide_identity( &id2, &w2 ))
    {
        free_wide_identity( &w1 );
        return SEC_E_INSUFFICIENT_MEMORY;
    }

    user = strings_equal( w1.user, w1.user_len, w2.user, w2.user_len, TRUE ) &&
           strings_equal( w1.domain, w1.domain_len, w2.domain, w2.domain_len, TRUE );
    identity = user &&
               strings_equal( w1.password, w1.password_len, w2.password, w2.password_len, FALSE ) &&
               strings_equal( w1.package_list, w1.package_list_len, w2.package_list, w2.package_list_len, FALSE );

    free_wide_identity( &w1 );
    free_wide_identity( &w2 );

    if (same_user) *same_user = user;
    if (same_identity) *same_identity = identity;
    return SEC_E_OK;
}

SECURITY_STATUS SEC_ENTRY SspiMarshalAuthIdentity( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id,
    unsigned long *length, char **blob )
{
    SEC_WINNT_AUTH_IDENTITY_EX32 *header;
    struct identity id;
    ULONG offset;
    SIZE_T size;
    BYTE *buffer;

    TRACE( "%p %p %p\n", opaque_id, length, blob );

    get_identity( opaque_id, &id );
    if (is_encrypted( &id )) return SEC_E_INVALID_TOKEN;

    size = sizeof(*header) + ((SIZE_T)id.user_len + id.domain_len + id.password_len + id.package_list_len) * id.char_size +
           sizeof(ULONG);
    if (!(buffer = calloc( 1, size ))) return SEC_E_INSUFFICIENT_MEMORY;

    header = (SEC_WINNT_AUTH_IDENTITY_EX32 *)buffer;
    header->Version = SEC_WINNT_AUTH_IDENTITY_VERSION;
    header->Length  = sizeof(*header);
    header->Flags   = *id.flags;
    offset = sizeof(*header);

    if (id.user_len)
    {
        memcpy( buffer + offset, id.user, id.user_len * id.char_size );
        header->User       = offset;
        header->UserLength = id.user_len;
        offset += id.user_len * id.char_size;
    }
    if (id.domain_len)
    {
        memcpy( buffer + offset, id.domain, id.domain_len * id.char_size );
        header->Domain       = offset;
        header->DomainLength = id.domain_len;
        offset += id.domain_len * id.char_size;
    }
    if (id.password_len)
    {
        memcpy( buffer + offset, id.password, id.password_len * id.char_size );
        header->Password       = offset;
        header->PasswordLength = id.password_len;
        offset += id.password_len * id.char_size;
    }
    if (id.package_list_len)
    {
        memcpy( buffer + offset, id.package_list, id.package_list_len * id.char_size );
        header->PackageList       = offset;
        header->PackageListLength = id.package_list_len;
    }

    *length = size;
    *blob = (char *)buffer;
    return SEC_E_OK;
}

static const void *marshalled_string( const char *blob, unsigned long length, ULONG offset, ULONG len,
                                      ULONG char_size, BOOL *valid )
{
    if (!len) return NULL;
    if (offset > length || (SIZE_T)len * char_size > length - offset)
    {
        *valid = FALSE;
        return NULL;
    }
    return blob + offset;
}

SECURITY_STATUS SEC_ENTRY SspiUnmarshalAuthIdentity( unsigned long length, char *blob,
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE *opaque_id )
{
    const SEC_WINNT_AUTH_IDENTITY_EX32 *header = (const SEC_WINNT_AUTH_IDENTITY_EX32 *)blob;
    const void *user, *domain, *password, *package_list;
    SEC_WINNT_AUTH_IDENTITY_EXW *id;
    BOOL valid = TRUE;
    ULONG char_size;

    TRACE( "%lu %p %p\n", length, blob, opaque_id );

    if (length < sizeof(*header) || header->Version != SEC_WINNT_AUTH_IDENTITY_VERSION)
        return SEC_E_INVALID_TOKEN;

    char_size = (header->Flags & SEC_WINNT_AUTH_IDENTITY_ANSI) ? sizeof(char) : sizeof(WCHAR);
    user = marshalled_string( blob, length, header->User, header->UserLength, char_size, &valid );
    domain = marshalled_string( blob, length, header->Domain, header->DomainLength, char_size, &valid );
    password = marshalled_string( blob, length, header->Password, header->PasswordLength, char_size, &valid );
    package_list = marshalled_string( blob, length, header->PackageList, header->PackageListLength, char_size, &valid );
    if (!valid) return SEC_E_INVALID_TOKEN;

    id = alloc_marshalled_identity( header->Flags | SEC_WINNT_AUTH_IDENTITY_MARSHALLED, char_size,
                                    user, header->UserLength, domain, header->DomainLength,
                                    password, header->PasswordLength, package_list, header->PackageListLength );
    if (!id) return SEC_E_INSUFFICIENT_MEMORY;

    *opaque_id = id;
    return SEC_E_OK;
}

SECURITY_STATUS SEC_ENTRY SspiExcludePackage( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id, PCWSTR package,
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE *new_id )
{
    struct wide_identity w;
    SEC_WINNT_AUTH_IDENTITY_EXW *id;
    ULONG package_len = lstrlenW( package ), list_len;
    WCHAR *list = NULL;

    TRACE( "%p %s %p\n", opaque_id, debugstr_w(package), new_id );

    memset( &w, 0, sizeof(w) );
    if (opaque_id)
    {
        struct identity src;

        get_identity( opaque_id, &src );
        if (is_encrypted( &src )) return SEC_E_INVALID_TOKEN;
        if (!get_wide_identity( &src, &w )) return SEC_E_INSUFFICIENT_MEMORY;
    }

    list_len = w.package_list_len;
    if (package_len)
    {
        if (!(list = malloc( ((SIZE_T)w.package_list_len + package_len + 3) * sizeof(WCHAR) )))
        {
            free_wide_identity( &w );
            return SEC_E_INSUFFICIENT_MEMORY;
        }
        list[0] = '!';
        memcpy( list + 1, package, package_len * sizeof(WCHAR) );
        if (!list_contains( w.package_list, w.package_list_len, list, package_len + 1 ))
        {
            if (w.package_list_len)
            {
                memmove( list + w.package_list_len + 1, list, (package_len + 1) * sizeof(WCHAR) );
                memcpy( list, w.package_list, w.package_list_len * sizeof(WCHAR) );
                list[w.package_list_len] = ',';
                list_len = w.package_list_len + package_len + 2;
            }
            else
                list_len = package_len + 1;
        }
        else
        {
            free( list );
            list = NULL;
        }
    }

    id = alloc_marshalled_identity( SEC_WINNT_AUTH_IDENTITY_UNICODE | SEC_WINNT_AUTH_IDENTITY_MARSHALLED,
                                    sizeof(WCHAR), w.user, w.user_len, w.domain, w.domain_len,
                                    w.password, w.password_len,
                                    list ? list : w.package_list, list_len );
    free( list );
    free_wide_identity( &w );
    if (!id) return SEC_E_INSUFFICIENT_MEMORY;

    *new_id = id;
    return SEC_E_OK;
}

SECURITY_STATUS SEC_ENTRY SspiGetTargetHostName( PCWSTR target, PWSTR *host )
{
    const WCHAR *start = target, *end, *slash, *p;
    WCHAR *ret;

    TRACE( "%s %p\n", debugstr_w(target), host );

    if ((slash = wcschr( target, '/' )))
    {
        start = slash + 1;
        if (!(end = wcschr( start, '/' ))) end = start + lstrlenW( start );
    }
    else
        end = target + lstrlenW( target );

    for (p = start; p < end; p++)
    {
        if (*p == '@')
        {
            end = p;
            break;
        }
    }
    if (slash)
    {
        for (p = end; p > start; p--)
        {
            if (p[-1] == ':')
            {
                end = p - 1;
                break;
            }
        }
    }

    if (!(ret = malloc( (end - start + 1) * sizeof(WCHAR) ))) return SEC_E_INSUFFICIENT_MEMORY;
    memcpy( ret, start, (end - start) * sizeof(WCHAR) );
    ret[end - start] = 0;
    *host = ret;
    return SEC_E_OK;
}

SECURITY_STATUS SEC_ENTRY SspiPrepareForCredRead( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id, PCWSTR target,
    PULONG type, PCWSTR *targetname )
{
    struct identity id;
    WCHAR *str;

    TRACE( "%p %s %p %p\n", opaque_id, debugstr_w(target), type, targetname );

    get_identity( opaque_id, &id );
    if (is_encrypted( &id )) return SEC_E_INVALID_TOKEN;
    if (!(str = wcsdup( target ))) return SEC_E_INSUFFICIENT_MEMORY;

    *type = CRED_TYPE_DOMAIN_PASSWORD;
    *targetname = str;
    return SEC_E_OK;
}

/***********************************************************************
 *		SspiPrepareForCredWrite (SECUR32.0)
 */
SECURITY_STATUS SEC_ENTRY SspiPrepareForCredWrite( PSEC_WINNT_AUTH_IDENTITY_OPAQUE opaque_id,
    PCWSTR target, PULONG type, PCWSTR *targetname, PCWSTR *username, PUCHAR *blob, PULONG size )
{
    struct wide_identity w;
    struct identity id;
    WCHAR *str, *str2;
    UCHAR *password;
    ULONG len;

    TRACE( "%p %s %p %p %p %p %p\n", opaque_id, debugstr_w(target), type, targetname, username,
           blob, size );

    get_identity( opaque_id, &id );
    if (is_encrypted( &id )) return SEC_E_INVALID_TOKEN;
    if (!get_wide_identity( &id, &w )) return SEC_E_INSUFFICIENT_MEMORY;

    if (w.domain_len)
    {
        len = (w.domain_len + w.user_len + 2) * sizeof(WCHAR);
        if (!(str = malloc( len ))) goto oom;
        memcpy( str, w.domain, w.domain_len * sizeof(WCHAR) );
        str[w.domain_len] = '\\';
        memcpy( str + w.domain_len + 1, w.user, w.user_len * sizeof(WCHAR) );
        str[w.domain_len + 1 + w.user_len] = 0;
    }
    else
    {
        len = (w.user_len + 1) * sizeof(WCHAR);
        if (!(str = malloc( len ))) goto oom;
        memcpy( str, w.user, w.user_len * sizeof(WCHAR) );
        str[w.user_len] = 0;
    }

    str2 = target ? wcsdup( target ) : wcsdup( str );
    if (!str2)
    {
        free( str );
        goto oom;
    }

    len = w.password_len * sizeof(WCHAR);
    if (!(password = malloc( len )))
    {
        free( str );
        free( str2 );
        goto oom;
    }
    memcpy( password, w.password, len );

    *type = CRED_TYPE_DOMAIN_PASSWORD;
    *username = str;
    *targetname = str2;
    *blob = password;
    *size = len;
    free_wide_identity( &w );
    return SEC_E_OK;

oom:
    free_wide_identity( &w );
    return SEC_E_INSUFFICIENT_MEMORY;
}
