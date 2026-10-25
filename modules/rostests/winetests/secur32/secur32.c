/*
 * tests
 *
 * Copyright 2006 Robert Reif
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
#include <stdio.h>
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <rpc.h>
#include <rpcdce.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <wincred.h>
#include <winsock2.h>

#include "wine/test.h"

static HMODULE secdll;

static SECURITY_STATUS (SEC_ENTRY *pSspiEncodeAuthIdentityAsStrings)
    (PSEC_WINNT_AUTH_IDENTITY_OPAQUE, PCWSTR *, PCWSTR *, PCWSTR *);
static SECURITY_STATUS (SEC_ENTRY *pSspiEncodeStringsAsAuthIdentity)
    (PCWSTR, PCWSTR, PCWSTR, PSEC_WINNT_AUTH_IDENTITY_OPAQUE *);
static void (SEC_ENTRY *pSspiFreeAuthIdentity)
    (PSEC_WINNT_AUTH_IDENTITY_OPAQUE);
static void (SEC_ENTRY *pSspiLocalFree)
    (void *);
static SECURITY_STATUS (SEC_ENTRY *pSspiPrepareForCredWrite)
    (PSEC_WINNT_AUTH_IDENTITY_OPAQUE, PCWSTR, PULONG, PCWSTR *, PCWSTR *, PUCHAR *, PULONG);
static void (SEC_ENTRY *pSspiZeroAuthIdentity)
    (PSEC_WINNT_AUTH_IDENTITY_OPAQUE);
#ifdef __REACTOS__
static SECURITY_STATUS (SEC_ENTRY *pSspiCopyAuthIdentity)
    (PSEC_WINNT_AUTH_IDENTITY_OPAQUE, PSEC_WINNT_AUTH_IDENTITY_OPAQUE *);
static SECURITY_STATUS (SEC_ENTRY *pSspiValidateAuthIdentity)(PSEC_WINNT_AUTH_IDENTITY_OPAQUE);
static BOOLEAN (SEC_ENTRY *pSspiIsAuthIdentityEncrypted)(PSEC_WINNT_AUTH_IDENTITY_OPAQUE);
static SECURITY_STATUS (SEC_ENTRY *pSspiEncryptAuthIdentity)(PSEC_WINNT_AUTH_IDENTITY_OPAQUE);
static SECURITY_STATUS (SEC_ENTRY *pSspiDecryptAuthIdentity)(PSEC_WINNT_AUTH_IDENTITY_OPAQUE);
static SECURITY_STATUS (SEC_ENTRY *pSspiEncryptAuthIdentityEx)(ULONG, PSEC_WINNT_AUTH_IDENTITY_OPAQUE);
static SECURITY_STATUS (SEC_ENTRY *pSspiDecryptAuthIdentityEx)(ULONG, PSEC_WINNT_AUTH_IDENTITY_OPAQUE);
static SECURITY_STATUS (SEC_ENTRY *pSspiCompareAuthIdentities)
    (PSEC_WINNT_AUTH_IDENTITY_OPAQUE, PSEC_WINNT_AUTH_IDENTITY_OPAQUE, PBOOLEAN, PBOOLEAN);
static SECURITY_STATUS (SEC_ENTRY *pSspiMarshalAuthIdentity)
    (PSEC_WINNT_AUTH_IDENTITY_OPAQUE, unsigned long *, char **);
static SECURITY_STATUS (SEC_ENTRY *pSspiUnmarshalAuthIdentity)
    (unsigned long, char *, PSEC_WINNT_AUTH_IDENTITY_OPAQUE *);
static SECURITY_STATUS (SEC_ENTRY *pSspiPrepareForCredRead)
    (PSEC_WINNT_AUTH_IDENTITY_OPAQUE, PCWSTR, PULONG, PCWSTR *);
static SECURITY_STATUS (SEC_ENTRY *pSspiExcludePackage)
    (PSEC_WINNT_AUTH_IDENTITY_OPAQUE, PCWSTR, PSEC_WINNT_AUTH_IDENTITY_OPAQUE *);
static SECURITY_STATUS (SEC_ENTRY *pSspiGetTargetHostName)(PCWSTR, PWSTR *);
#endif

static BOOLEAN (WINAPI * pGetComputerObjectNameA)(EXTENDED_NAME_FORMAT NameFormat, LPSTR lpNameBuffer, PULONG lpnSize);
static BOOLEAN (WINAPI * pGetComputerObjectNameW)(EXTENDED_NAME_FORMAT NameFormat, LPWSTR lpNameBuffer, PULONG lpnSize);
static BOOLEAN (WINAPI * pGetUserNameExA)(EXTENDED_NAME_FORMAT NameFormat, LPSTR lpNameBuffer, PULONG lpnSize);
static BOOLEAN (WINAPI * pGetUserNameExW)(EXTENDED_NAME_FORMAT NameFormat, LPWSTR lpNameBuffer, PULONG lpnSize);
static PSecurityFunctionTableA (SEC_ENTRY * pInitSecurityInterfaceA)(void);
static PSecurityFunctionTableW (SEC_ENTRY * pInitSecurityInterfaceW)(void);

static EXTENDED_NAME_FORMAT formats[] = {
    NameUnknown, NameFullyQualifiedDN, NameSamCompatible, NameDisplay,
    NameUniqueId, NameCanonical, NameUserPrincipal, NameCanonicalEx,
    NameServicePrincipal, NameDnsDomain
};

static void testGetComputerObjectNameA(void)
{
    char name[256];
    ULONG size;
    BOOLEAN rc;
    UINT i;

    for (i = 0; i < ARRAY_SIZE(formats); i++) {
        size = 0;
        SetLastError(0xdeadbeef);
        rc = pGetComputerObjectNameA(formats[i], NULL, &size);
        ok(!rc, "GetComputerObjectName(%u) should fail\n", formats[i]);
        switch (formats[i])
        {
        case NameUnknown:
            ok(GetLastError() == ERROR_INVALID_PARAMETER, "%u: got %lu\n", formats[i], GetLastError());
            break;
        default:
            ok(GetLastError() == ERROR_NONE_MAPPED ||
               GetLastError() == ERROR_NO_SUCH_USER ||
               GetLastError() == ERROR_CANT_ACCESS_DOMAIN_INFO ||
               GetLastError() == ERROR_INSUFFICIENT_BUFFER,
               "%u: got %lu\n", formats[i], GetLastError());
            break;
        }

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) continue;

        size = sizeof(name);
        SetLastError(0xdeadbeef);
        rc = pGetComputerObjectNameA(formats[i], name, &size);
        switch (formats[i])
        {
        case NameUnknown:
            ok(!rc, "GetComputerObjectName(%u) should fail\n", formats[i]);
            ok(GetLastError() == ERROR_INVALID_PARAMETER, "%u: got %lu\n", formats[i], GetLastError());
            break;
        default:
            ok(rc, "GetComputerObjectName(%u) error %lu\n", formats[i], GetLastError());
            trace("GetComputerObjectName(%u) returned %s\n", formats[i], name);
            break;
        }
    }
}

static void testGetComputerObjectNameW(void)
{
    WCHAR nameW[256];
    ULONG size;
    BOOLEAN rc;
    UINT i;

    for (i = 0; i < ARRAY_SIZE(formats); i++) {
        size = 0;
        SetLastError(0xdeadbeef);
        rc = pGetComputerObjectNameW(formats[i], NULL, &size);
        ok(!rc || broken(rc) /* win10 */, "GetComputerObjectName(%u) should fail\n", formats[i]);
        switch (formats[i])
        {
        case NameUnknown:
            ok(GetLastError() == ERROR_INVALID_PARAMETER, "%u: got %lu\n", formats[i], GetLastError());
            break;
        default:
            ok(GetLastError() == ERROR_NONE_MAPPED ||
               GetLastError() == ERROR_NO_SUCH_USER ||
               GetLastError() == ERROR_CANT_ACCESS_DOMAIN_INFO ||
               GetLastError() == WSAHOST_NOT_FOUND ||
               GetLastError() == ERROR_INSUFFICIENT_BUFFER,
               "%u: got %lu\n", formats[i], GetLastError());
            break;
        }

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) continue;

        size = ARRAY_SIZE(nameW);
        SetLastError(0xdeadbeef);
        rc = pGetComputerObjectNameW(formats[i], nameW, &size);
        switch (formats[i])
        {
        case NameUnknown:
            ok(!rc, "GetComputerObjectName(%u) should fail\n", formats[i]);
            ok(GetLastError() == ERROR_INVALID_PARAMETER, "%u: got %lu\n", formats[i], GetLastError());
            break;
        default:
            ok(rc, "GetComputerObjectName(%u) error %lu\n", formats[i], GetLastError());
            trace("GetComputerObjectName(%u) returned %s\n", formats[i], wine_dbgstr_w(nameW));
            break;
        }
    }
}

static void testGetUserNameExA(void)
{
    char name[256];
    ULONG size;
    BOOLEAN rc;
    UINT i;

    for (i = 0; i < ARRAY_SIZE(formats); i++) {
        size = sizeof(name);
        ZeroMemory(name, sizeof(name));
        rc = pGetUserNameExA(formats[i], name, &size);
        ok(rc ||
           (formats[i] == NameUnknown &&
            GetLastError() == ERROR_NO_SUCH_USER) ||
           GetLastError() == ERROR_NONE_MAPPED ||
           broken(formats[i] == NameDnsDomain &&
                  GetLastError() == ERROR_INVALID_PARAMETER),
           "GetUserNameExW(%d) failed: %lu\n",
           formats[i], GetLastError());
    }

    if (0) /* Crashes on Windows */
        pGetUserNameExA(NameSamCompatible, NULL, NULL);

    size = 0;
    rc = pGetUserNameExA(NameSamCompatible, NULL, &size);
    ok(! rc && GetLastError() == ERROR_MORE_DATA, "Expected fail with ERROR_MORE_DATA, got %d with %lu\n", rc, GetLastError());
    ok(size != 0, "Expected size to be set to required size\n");

    if (0) /* Crashes on Windows with big enough size */
    {
        /* Returned size is already big enough */
        pGetUserNameExA(NameSamCompatible, NULL, &size);
    }

    size = 0;
    rc = pGetUserNameExA(NameSamCompatible, name, &size);
    ok(! rc && GetLastError() == ERROR_MORE_DATA, "Expected fail with ERROR_MORE_DATA, got %d with %lu\n", rc, GetLastError());
    ok(size != 0, "Expected size to be set to required size\n");
    size = 1;
    name[0] = 0xff;
    rc = pGetUserNameExA(NameSamCompatible, name, &size);
    ok(! rc && GetLastError() == ERROR_MORE_DATA, "Expected fail with ERROR_MORE_DATA, got %d with %lu\n", rc, GetLastError());
    ok(1 < size, "Expected size to be set to required size\n");
    ok(name[0] == (char) 0xff, "Expected unchanged buffer\n");
}

static void testGetUserNameExW(void)
{
    WCHAR nameW[256];
    ULONG size;
    BOOLEAN rc;
    UINT i;

    for (i = 0; i < ARRAY_SIZE(formats); i++) {
        size = sizeof(nameW);
        ZeroMemory(nameW, sizeof(nameW));
        rc = pGetUserNameExW(formats[i], nameW, &size);
        ok(rc ||
           (formats[i] == NameUnknown &&
            GetLastError() == ERROR_NO_SUCH_USER) ||
           GetLastError() == ERROR_NONE_MAPPED ||
           broken(formats[i] == NameDnsDomain &&
                  GetLastError() == ERROR_INVALID_PARAMETER),
           "GetUserNameExW(%d) failed: %lu\n",
           formats[i], GetLastError());
    }

    if (0) /* Crashes on Windows */
        pGetUserNameExW(NameSamCompatible, NULL, NULL);

    size = 0;
    rc = pGetUserNameExW(NameSamCompatible, NULL, &size);
    ok(! rc && GetLastError() == ERROR_MORE_DATA, "Expected fail with ERROR_MORE_DATA, got %d with %lu\n", rc, GetLastError());
    ok(size != 0, "Expected size to be set to required size\n");

    if (0) /* Crashes on Windows with big enough size */
    {
        /* Returned size is already big enough */
        pGetUserNameExW(NameSamCompatible, NULL, &size);
    }

    size = 0;
    rc = pGetUserNameExW(NameSamCompatible, nameW, &size);
    ok(! rc && GetLastError() == ERROR_MORE_DATA, "Expected fail with ERROR_MORE_DATA, got %d with %lu\n", rc, GetLastError());
    ok(size != 0, "Expected size to be set to required size\n");
    size = 1;
    nameW[0] = 0xff;
    rc = pGetUserNameExW(NameSamCompatible, nameW, &size);
    ok(! rc && GetLastError() == ERROR_MORE_DATA, "Expected fail with ERROR_MORE_DATA, got %d with %lu\n", rc, GetLastError());
    ok(1 < size, "Expected size to be set to required size\n");
    ok(nameW[0] == (WCHAR) 0xff, "Expected unchanged buffer\n");
}

static void test_InitSecurityInterface(void)
{
    PSecurityFunctionTableA sftA;
    PSecurityFunctionTableW sftW;

    sftA = pInitSecurityInterfaceA();
    ok(sftA != NULL, "pInitSecurityInterfaceA failed\n");
    ok(sftA->dwVersion == SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION, "wrong dwVersion %lu in security function table\n", sftA->dwVersion);
    ok(!sftA->Reserved2,
       "Reserved2 should be NULL instead of %p in security function table\n",
       sftA->Reserved2);
    ok(sftA->Reserved3 == sftA->EncryptMessage,
       "Reserved3 should be equal to EncryptMessage in the security function table\n");
    ok(sftA->Reserved4 == sftA->DecryptMessage,
       "Reserved4 should be equal to DecryptMessage in the security function table\n");

    if (!pInitSecurityInterfaceW)
    {
        win_skip("InitSecurityInterfaceW not exported by secur32.dll\n");
        return;
    }

    sftW = pInitSecurityInterfaceW();
    ok(sftW != NULL, "pInitSecurityInterfaceW failed\n");
    ok(sftW->dwVersion == SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION, "wrong dwVersion %lu in security function table\n", sftW->dwVersion);
    ok(!sftW->Reserved2, "Reserved2 should be NULL instead of %p in security function table\n", sftW->Reserved2);
    ok(sftW->Reserved3 == sftW->EncryptMessage, "Reserved3 should be equal to EncryptMessage in the security function table\n");
    ok(sftW->Reserved4 == sftW->DecryptMessage, "Reserved4 should be equal to DecryptMessage in the security function table\n");
}

static void test_SspiEncodeStringsAsAuthIdentity(void)
{
    static const WCHAR username[] = {'u','s','e','r','n','a','m','e',0};
    static const WCHAR domainname[] = {'d','o','m','a','i','n','n','a','m','e',0};
    static const WCHAR password[] = {'p','a','s','s','w','o','r','d',0};
    const WCHAR *username_ptr, *domainname_ptr, *password_ptr;
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE id;
    SECURITY_STATUS status;

    if (!pSspiEncodeStringsAsAuthIdentity)
    {
        win_skip( "SspiEncodeAuthIdentityAsStrings not exported by secur32.dll\n" );
        return;
    }

    status = pSspiEncodeStringsAsAuthIdentity( NULL, NULL, NULL, NULL );
    ok( status == SEC_E_INVALID_TOKEN, "got %08lx\n", status );

    id = (PSEC_WINNT_AUTH_IDENTITY_OPAQUE)0xdeadbeef;
    status = pSspiEncodeStringsAsAuthIdentity( NULL, NULL, NULL, &id );
    ok( status == SEC_E_INVALID_TOKEN, "got %08lx\n", status );
    ok( id == (PSEC_WINNT_AUTH_IDENTITY_OPAQUE)0xdeadbeef, "id set\n" );

    id = NULL;
    status = pSspiEncodeStringsAsAuthIdentity( NULL, NULL, password, &id );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( id != NULL, "id not set\n" );
    pSspiFreeAuthIdentity( id );

    id = NULL;
    status = pSspiEncodeStringsAsAuthIdentity( NULL, domainname, password, &id );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( id != NULL, "id not set\n" );
    pSspiFreeAuthIdentity( id );

    id = NULL;
    status = pSspiEncodeStringsAsAuthIdentity( username, NULL, password, &id );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( id != NULL, "id not set\n" );
    pSspiFreeAuthIdentity( id );

    id = NULL;
    status = pSspiEncodeStringsAsAuthIdentity( username, NULL, NULL, &id );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( id != NULL, "id not set\n" );
    pSspiFreeAuthIdentity( id );

    id = NULL;
    status = pSspiEncodeStringsAsAuthIdentity( username, domainname, password, &id );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( id != NULL, "id not set\n" );

    username_ptr = domainname_ptr = password_ptr = NULL;
    status = pSspiEncodeAuthIdentityAsStrings( id, &username_ptr, &domainname_ptr, &password_ptr );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( !lstrcmpW( username, username_ptr ), "wrong username\n" );
    ok( !lstrcmpW( domainname, domainname_ptr ), "wrong domainname\n" );
    ok( !lstrcmpW( password, password_ptr ), "wrong password\n" );

    pSspiZeroAuthIdentity( id );

    pSspiLocalFree( (void *)username_ptr );
    pSspiLocalFree( (void *)domainname_ptr );
    pSspiLocalFree( (void *)password_ptr );
    pSspiFreeAuthIdentity( id );

    id = NULL;
    status = pSspiEncodeStringsAsAuthIdentity( username, NULL, password, &id );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( id != NULL, "id not set\n" );

    username_ptr = password_ptr = NULL;
    domainname_ptr = (const WCHAR *)0xdeadbeef;
    status = pSspiEncodeAuthIdentityAsStrings( id, &username_ptr, &domainname_ptr, &password_ptr );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( !lstrcmpW( username, username_ptr ), "wrong username\n" );
    ok( domainname_ptr == NULL, "domainname_ptr not cleared\n" );
    ok( !lstrcmpW( password, password_ptr ), "wrong password\n" );

    pSspiLocalFree( (void *)username_ptr );
    pSspiLocalFree( (void *)password_ptr );
    pSspiFreeAuthIdentity( id );
}

#ifdef __REACTOS__
static void test_SspiCopyAuthIdentity(void)
{
    static const WCHAR username[] = {'u','s','e','r','n','a','m','e',0};
    static const WCHAR domainname[] = {'d','o','m','a','i','n','n','a','m','e',0};
    static const WCHAR password[] = {'p','a','s','s','w','o','r','d',0};
    const WCHAR *username_ptr, *domainname_ptr, *password_ptr;
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE id, copy;
    SECURITY_STATUS status;

    if (!pSspiCopyAuthIdentity)
    {
        win_skip( "SspiCopyAuthIdentity not exported by secur32.dll\n" );
        return;
    }

    id = NULL;
    status = pSspiEncodeStringsAsAuthIdentity( username, domainname, password, &id );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( id != NULL, "id not set\n" );

    copy = NULL;
    status = pSspiCopyAuthIdentity( id, &copy );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( copy != NULL, "copy not set\n" );
    ok( copy != id, "copy is the original\n" );

    pSspiZeroAuthIdentity( id );
    pSspiFreeAuthIdentity( id );

    username_ptr = domainname_ptr = password_ptr = NULL;
    status = pSspiEncodeAuthIdentityAsStrings( copy, &username_ptr, &domainname_ptr, &password_ptr );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( username_ptr && !lstrcmpW( username, username_ptr ), "wrong username\n" );
    ok( domainname_ptr && !lstrcmpW( domainname, domainname_ptr ), "wrong domainname\n" );
    ok( password_ptr && !lstrcmpW( password, password_ptr ), "wrong password\n" );

    pSspiLocalFree( (void *)username_ptr );
    pSspiLocalFree( (void *)domainname_ptr );
    pSspiLocalFree( (void *)password_ptr );
    pSspiFreeAuthIdentity( copy );

    id = NULL;
    status = pSspiEncodeStringsAsAuthIdentity( username, NULL, password, &id );
    ok( status == SEC_E_OK, "got %08lx\n", status );

    copy = NULL;
    status = pSspiCopyAuthIdentity( id, &copy );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( copy != NULL, "copy not set\n" );
    pSspiFreeAuthIdentity( id );

    username_ptr = password_ptr = NULL;
    domainname_ptr = (const WCHAR *)0xdeadbeef;
    status = pSspiEncodeAuthIdentityAsStrings( copy, &username_ptr, &domainname_ptr, &password_ptr );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( username_ptr && !lstrcmpW( username, username_ptr ), "wrong username\n" );
    ok( domainname_ptr == NULL, "domainname_ptr not cleared\n" );
    ok( password_ptr && !lstrcmpW( password, password_ptr ), "wrong password\n" );

    pSspiLocalFree( (void *)username_ptr );
    pSspiLocalFree( (void *)password_ptr );
    pSspiFreeAuthIdentity( copy );
}

static void check_identity_strings( int line, PSEC_WINNT_AUTH_IDENTITY_OPAQUE id, const WCHAR *user,
                                    const WCHAR *domain, const WCHAR *password )
{
    const WCHAR *u = NULL, *d = NULL, *p = NULL;
    SECURITY_STATUS status;

    status = pSspiEncodeAuthIdentityAsStrings( id, &u, &d, &p );
    ok_(__FILE__, line)( status == SEC_E_OK, "got %08lx\n", status );
    if (status != SEC_E_OK) return;
    ok_(__FILE__, line)( user ? (u && !lstrcmpW( u, user )) : !u, "got user %s\n", wine_dbgstr_w(u) );
    ok_(__FILE__, line)( domain ? (d && !lstrcmpW( d, domain )) : !d, "got domain %s\n", wine_dbgstr_w(d) );
    ok_(__FILE__, line)( password ? (p && !lstrcmpW( p, password )) : !p, "got password %s\n", wine_dbgstr_w(p) );
    pSspiLocalFree( (void *)u );
    pSspiLocalFree( (void *)d );
    pSspiLocalFree( (void *)p );
}

static void test_SspiAuthIdentityLayout(void)
{
    SEC_WINNT_AUTH_IDENTITY_W w = { (unsigned short *)L"user", 4, (unsigned short *)L"DOMAIN", 6,
                                    (unsigned short *)L"password", 8, SEC_WINNT_AUTH_IDENTITY_UNICODE };
    SEC_WINNT_AUTH_IDENTITY_EXW ex = { SEC_WINNT_AUTH_IDENTITY_VERSION, sizeof(ex), (unsigned short *)L"user", 4,
                                       (unsigned short *)L"DOMAIN", 6, (unsigned short *)L"password", 8,
                                       SEC_WINNT_AUTH_IDENTITY_UNICODE, NULL, 0 };
    WCHAR zu[] = L"user", zd[] = L"DOMAIN", zp[] = L"password";
    SEC_WINNT_AUTH_IDENTITY_W zw = { (unsigned short *)zu, 4, (unsigned short *)zd, 6, (unsigned short *)zp, 8,
                                     SEC_WINNT_AUTH_IDENTITY_UNICODE };
    SEC_WINNT_AUTH_IDENTITY_EXW *encoded = NULL;
    SEC_WINNT_AUTH_IDENTITY_W *copy = NULL;
    SECURITY_STATUS status;

    if (!pSspiValidateAuthIdentity)
    {
        win_skip( "SspiValidateAuthIdentity not exported by secur32.dll\n" );
        return;
    }

    status = pSspiEncodeStringsAsAuthIdentity( L"user", L"DOMAIN", L"password", (void **)&encoded );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( encoded->Version == SEC_WINNT_AUTH_IDENTITY_VERSION, "got %#lx\n", encoded->Version );
    ok( encoded->Length == sizeof(*encoded), "got %lu\n", encoded->Length );
    ok( encoded->Flags == (SEC_WINNT_AUTH_IDENTITY_UNICODE | SEC_WINNT_AUTH_IDENTITY_MARSHALLED), "got %#lx\n", encoded->Flags );
    ok( encoded->UserLength == 4 && encoded->DomainLength == 6 && encoded->PasswordLength == 8, "got %lu %lu %lu\n",
        encoded->UserLength, encoded->DomainLength, encoded->PasswordLength );
    ok( !encoded->PackageList && !encoded->PackageListLength, "got %p %lu\n", encoded->PackageList, encoded->PackageListLength );
    ok( pSspiValidateAuthIdentity( encoded ) == SEC_E_OK, "encoded identity not valid\n" );
    ok( !pSspiIsAuthIdentityEncrypted( encoded ), "encoded identity encrypted\n" );
    pSspiFreeAuthIdentity( encoded );

    encoded = NULL;
    status = pSspiEncodeStringsAsAuthIdentity( L"", L"", L"", (void **)&encoded );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( encoded->User && !encoded->UserLength, "got %p %lu\n", encoded->User, encoded->UserLength );
    ok( encoded->Password && !encoded->PasswordLength, "got %p %lu\n", encoded->Password, encoded->PasswordLength );
    pSspiFreeAuthIdentity( encoded );

    encoded = NULL;
    status = pSspiEncodeStringsAsAuthIdentity( L"user", NULL, L"password", (void **)&encoded );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( !encoded->Domain && !encoded->DomainLength, "got %p %lu\n", encoded->Domain, encoded->DomainLength );
    pSspiFreeAuthIdentity( encoded );

    ok( pSspiValidateAuthIdentity( &w ) == SEC_E_OK, "W identity not valid\n" );
    ok( pSspiValidateAuthIdentity( &ex ) == SEC_E_OK, "EX identity not valid\n" );
    w.Flags = 0;
    ok( pSspiValidateAuthIdentity( &w ) == SEC_E_INVALID_TOKEN, "flags 0 accepted\n" );
    w.Flags = SEC_WINNT_AUTH_IDENTITY_UNICODE;
    ex.Length = 0;
    ok( pSspiValidateAuthIdentity( &ex ) == SEC_E_INVALID_TOKEN, "EX length 0 accepted\n" );
    check_identity_strings( __LINE__, &ex, L"user", L"DOMAIN", L"password" );
    ex.Length = sizeof(ex);

    status = pSspiCopyAuthIdentity( &w, (void **)&copy );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( copy->Flags == (SEC_WINNT_AUTH_IDENTITY_UNICODE | SEC_WINNT_AUTH_IDENTITY_FLAGS_RESERVED), "got %#lx\n", copy->Flags );
    ok( copy->UserLength == 4 && copy->DomainLength == 6 && copy->PasswordLength == 8, "got %lu %lu %lu\n",
        copy->UserLength, copy->DomainLength, copy->PasswordLength );
    check_identity_strings( __LINE__, copy, L"user", L"DOMAIN", L"password" );
    pSspiFreeAuthIdentity( copy );

    pSspiZeroAuthIdentity( &zw );
    ok( !lstrcmpW( zu, L"user" ) && !lstrcmpW( zd, L"DOMAIN" ), "user or domain zeroed\n" );
    ok( !zp[0] && !zp[7], "password not zeroed\n" );
    ok( zw.UserLength == 4 && zw.PasswordLength == 8 && zw.Flags == SEC_WINNT_AUTH_IDENTITY_UNICODE, "header changed\n" );
}

static void test_SspiCompareAuthIdentities(void)
{
    static const struct
    {
        const WCHAR *user, *domain, *password;
        BOOLEAN same_user, same_identity;
    }
    tests[] =
    {
        { L"user", L"DOMAIN", L"password", TRUE, TRUE },
        { L"user", L"DOMAIN", L"other", TRUE, FALSE },
        { L"USER", L"domain", L"password", TRUE, TRUE },
        { L"user", L"DOMAIN", L"PASSWORD", TRUE, FALSE },
        { L"user2", L"DOMAIN", L"password", FALSE, FALSE },
        { L"user", L"DOMAIN2", L"password", FALSE, FALSE },
        { L"user", NULL, L"password", FALSE, FALSE },
    };
    SEC_WINNT_AUTH_IDENTITY_W w = { (unsigned short *)L"user", 4, (unsigned short *)L"DOMAIN", 6,
                                    (unsigned short *)L"password", 8, SEC_WINNT_AUTH_IDENTITY_UNICODE };
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE id = NULL, other;
    BOOLEAN same_user, same_identity;
    SECURITY_STATUS status;
    unsigned int i;

    if (!pSspiCompareAuthIdentities)
    {
        win_skip( "SspiCompareAuthIdentities not exported by secur32.dll\n" );
        return;
    }

    pSspiEncodeStringsAsAuthIdentity( L"user", L"DOMAIN", L"password", &id );
    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        other = NULL;
        pSspiEncodeStringsAsAuthIdentity( tests[i].user, tests[i].domain, tests[i].password, &other );
        same_user = same_identity = 0x55;
        status = pSspiCompareAuthIdentities( id, other, &same_user, &same_identity );
        ok( status == SEC_E_OK, "%u: got %08lx\n", i, status );
        ok( same_user == tests[i].same_user, "%u: got %u\n", i, same_user );
        ok( same_identity == tests[i].same_identity, "%u: got %u\n", i, same_identity );
        pSspiFreeAuthIdentity( other );
    }

    same_user = same_identity = 0x55;
    status = pSspiCompareAuthIdentities( id, &w, &same_user, &same_identity );
    ok( status == SEC_E_OK && same_user && same_identity, "got %08lx %u %u\n", status, same_user, same_identity );
    same_user = same_identity = 0x55;
    status = pSspiCompareAuthIdentities( NULL, NULL, &same_user, &same_identity );
    ok( status == SEC_E_OK && same_user && same_identity, "got %08lx %u %u\n", status, same_user, same_identity );
    same_user = same_identity = 0x55;
    status = pSspiCompareAuthIdentities( id, NULL, &same_user, &same_identity );
    ok( status == SEC_E_OK && !same_user && !same_identity, "got %08lx %u %u\n", status, same_user, same_identity );
    pSspiFreeAuthIdentity( id );
}

static void test_SspiMarshalAuthIdentity(void)
{
    static const BYTE expected[] =
    {
        0x00,0x02,0x00,0x00, 0x2c,0x00,0x00,0x00, 0x2c,0x00,0x00,0x00, 0x04,0x00,0x00,0x00,
        0x34,0x00,0x00,0x00, 0x06,0x00,0x00,0x00, 0x40,0x00,0x00,0x00, 0x08,0x00,0x00,0x00,
        0x06,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        'u',0,'s',0,'e',0,'r',0, 'D',0,'O',0,'M',0,'A',0,'I',0,'N',0,
        'p',0,'a',0,'s',0,'s',0,'w',0,'o',0,'r',0,'d',0, 0,0,0,0
    };
    SEC_WINNT_AUTH_IDENTITY_W w = { (unsigned short *)L"user", 4, (unsigned short *)L"DOMAIN", 6,
                                    (unsigned short *)L"password", 8, SEC_WINNT_AUTH_IDENTITY_UNICODE };
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE id = NULL;
    SEC_WINNT_AUTH_IDENTITY_EXW *un;
    SECURITY_STATUS status;
    unsigned long len;
    char *blob;

    if (!pSspiMarshalAuthIdentity)
    {
        win_skip( "SspiMarshalAuthIdentity not exported by secur32.dll\n" );
        return;
    }

    pSspiEncodeStringsAsAuthIdentity( L"user", L"DOMAIN", L"password", &id );
    blob = NULL;
    status = pSspiMarshalAuthIdentity( id, &len, &blob );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( len == sizeof(expected), "got %lu\n", len );
    ok( blob && !memcmp( blob, expected, sizeof(expected) ), "wrong blob\n" );
    if (status == SEC_E_OK)
    {
        un = NULL;
        status = pSspiUnmarshalAuthIdentity( len, blob, (void **)&un );
        ok( status == SEC_E_OK, "got %08lx\n", status );
        ok( un->Version == SEC_WINNT_AUTH_IDENTITY_VERSION, "got %#lx\n", un->Version );
        ok( un->Flags == (SEC_WINNT_AUTH_IDENTITY_UNICODE | SEC_WINNT_AUTH_IDENTITY_MARSHALLED), "got %#lx\n", un->Flags );
        check_identity_strings( __LINE__, un, L"user", L"DOMAIN", L"password" );
        pSspiFreeAuthIdentity( un );
        blob[0] ^= 0xff;
        status = pSspiUnmarshalAuthIdentity( len, blob, (void **)&un );
        ok( status == SEC_E_INVALID_TOKEN, "got %08lx\n", status );
        pSspiLocalFree( blob );
    }
    status = pSspiUnmarshalAuthIdentity( 0, (char *)expected, (void **)&un );
    ok( status == SEC_E_INVALID_TOKEN, "got %08lx\n", status );
    pSspiFreeAuthIdentity( id );

    blob = NULL;
    status = pSspiMarshalAuthIdentity( &w, &len, &blob );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    if (status == SEC_E_OK)
    {
        ok( len == sizeof(expected) && ((ULONG *)blob)[8] == SEC_WINNT_AUTH_IDENTITY_UNICODE, "got %lu\n", len );
        un = NULL;
        status = pSspiUnmarshalAuthIdentity( len, blob, (void **)&un );
        ok( status == SEC_E_OK, "got %08lx\n", status );
        ok( un->Flags == (SEC_WINNT_AUTH_IDENTITY_UNICODE | SEC_WINNT_AUTH_IDENTITY_MARSHALLED), "got %#lx\n", un->Flags );
        pSspiFreeAuthIdentity( un );
        pSspiLocalFree( blob );
    }
}

static void test_SspiEncryptAuthIdentity(void)
{
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE id = NULL, copy = NULL;
    SEC_WINNT_AUTH_IDENTITY_EXW *ex;
    BOOLEAN same_user, same_identity;
    SECURITY_STATUS status;
    unsigned long len;
    char *blob;

    if (!pSspiEncryptAuthIdentity)
    {
        win_skip( "SspiEncryptAuthIdentity not exported by secur32.dll\n" );
        return;
    }

    pSspiEncodeStringsAsAuthIdentity( L"user", L"DOMAIN", L"password", &id );
    status = pSspiEncryptAuthIdentity( id );
    ok( status == SEC_E_INVALID_TOKEN, "got %08lx\n", status );
    status = pSspiDecryptAuthIdentity( id );
    ok( status == SEC_E_INVALID_TOKEN, "got %08lx\n", status );

    status = pSspiCopyAuthIdentity( id, &copy );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ex = copy;
    ok( ex->Flags == (SEC_WINNT_AUTH_IDENTITY_UNICODE | SEC_WINNT_AUTH_IDENTITY_FLAGS_RESERVED), "got %#lx\n", ex->Flags );

    status = pSspiEncryptAuthIdentity( copy );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( pSspiIsAuthIdentityEncrypted( copy ), "not encrypted\n" );
    ok( ex->Flags == (SEC_WINNT_AUTH_IDENTITY_UNICODE | SEC_WINNT_AUTH_IDENTITY_FLAGS_RESERVED |
                      SEC_WINNT_AUTH_IDENTITY_FLAGS_PROCESS_ENCRYPTED), "got %#lx\n", ex->Flags );
    ok( ex->UserLength == 4 && ex->PasswordLength == 8, "got %lu %lu\n", ex->UserLength, ex->PasswordLength );
    ok( memcmp( ex->Password, L"password", 16 ), "password not encrypted\n" );
    ok( pSspiValidateAuthIdentity( copy ) == SEC_E_OK, "encrypted identity not valid\n" );
    status = pSspiEncryptAuthIdentity( copy );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    same_user = same_identity = 0x55;
    status = pSspiCompareAuthIdentities( id, copy, &same_user, &same_identity );
    ok( status == SEC_E_INVALID_TOKEN && !same_user && !same_identity, "got %08lx %u %u\n", status, same_user, same_identity );
    status = pSspiMarshalAuthIdentity( copy, &len, &blob );
    ok( status == SEC_E_INVALID_TOKEN, "got %08lx\n", status );
    if (pSspiDecryptAuthIdentityEx)
    {
        status = pSspiDecryptAuthIdentityEx( SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_LOGON, copy );
        ok( status == SEC_E_UNSUPPORTED_FUNCTION, "got %08lx\n", status );
        status = pSspiDecryptAuthIdentityEx( 0, copy );
        ok( status == SEC_E_INVALID_TOKEN, "got %08lx\n", status );
    }

    status = pSspiDecryptAuthIdentity( copy );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( !pSspiIsAuthIdentityEncrypted( copy ), "still encrypted\n" );
    ok( ex->Flags == (SEC_WINNT_AUTH_IDENTITY_UNICODE | SEC_WINNT_AUTH_IDENTITY_FLAGS_RESERVED), "got %#lx\n", ex->Flags );
    check_identity_strings( __LINE__, copy, L"user", L"DOMAIN", L"password" );
    status = pSspiDecryptAuthIdentity( copy );
    ok( status == SEC_E_OK, "got %08lx\n", status );

    if (pSspiEncryptAuthIdentityEx)
    {
        status = pSspiEncryptAuthIdentityEx( 0, copy );
        ok( status == SEC_E_INVALID_TOKEN, "got %08lx\n", status );
        status = pSspiEncryptAuthIdentityEx( 3, copy );
        ok( status == SEC_E_INVALID_TOKEN, "got %08lx\n", status );
        status = pSspiEncryptAuthIdentityEx( SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_LOGON, copy );
        ok( status == SEC_E_OK, "got %08lx\n", status );
        ok( (ex->Flags & ~SEC_WINNT_AUTH_IDENTITY_FLAGS_SYSTEM_PROTECTED & ~SEC_WINNT_AUTH_IDENTITY_FLAGS_USER_PROTECTED) ==
            (SEC_WINNT_AUTH_IDENTITY_UNICODE | SEC_WINNT_AUTH_IDENTITY_FLAGS_RESERVED) &&
            (ex->Flags & (SEC_WINNT_AUTH_IDENTITY_FLAGS_SYSTEM_PROTECTED | SEC_WINNT_AUTH_IDENTITY_FLAGS_USER_PROTECTED)),
            "got %#lx\n", ex->Flags );
        status = pSspiDecryptAuthIdentity( copy );
        ok( status == SEC_E_OK && pSspiIsAuthIdentityEncrypted( copy ), "got %08lx\n", status );
        status = pSspiDecryptAuthIdentityEx( SEC_WINNT_AUTH_IDENTITY_ENCRYPT_SAME_LOGON, copy );
        ok( status == SEC_E_OK, "got %08lx\n", status );
        check_identity_strings( __LINE__, copy, L"user", L"DOMAIN", L"password" );
    }
    pSspiFreeAuthIdentity( copy );
    pSspiFreeAuthIdentity( id );
}

static void test_SspiExcludePackage(void)
{
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE id = NULL;
    SEC_WINNT_AUTH_IDENTITY_EXW *ex = NULL, *ex2;
    SECURITY_STATUS status;

    if (!pSspiExcludePackage)
    {
        win_skip( "SspiExcludePackage not exported by secur32.dll\n" );
        return;
    }

    pSspiEncodeStringsAsAuthIdentity( L"user", L"DOMAIN", L"password", &id );
    status = pSspiExcludePackage( id, L"NTLM", (void **)&ex );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( ex->Version == SEC_WINNT_AUTH_IDENTITY_VERSION, "got %#lx\n", ex->Version );
    ok( ex->Flags == (SEC_WINNT_AUTH_IDENTITY_UNICODE | SEC_WINNT_AUTH_IDENTITY_MARSHALLED), "got %#lx\n", ex->Flags );
    ok( ex->PackageListLength == 5 && !memcmp( ex->PackageList, L"!NTLM", 10 ), "got %s\n",
        wine_dbgstr_wn( ex->PackageList, ex->PackageListLength ) );
    ok( ex->UserLength == 4 && !memcmp( ex->User, L"user", 8 ), "wrong user\n" );

    ex2 = NULL;
    status = pSspiExcludePackage( ex, L"Kerberos", (void **)&ex2 );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( ex2->PackageListLength == 15 && !memcmp( ex2->PackageList, L"!NTLM,!Kerberos", 30 ), "got %s\n",
        wine_dbgstr_wn( ex2->PackageList, ex2->PackageListLength ) );
    pSspiFreeAuthIdentity( ex2 );

    ex2 = NULL;
    status = pSspiExcludePackage( ex, L"NTLM", (void **)&ex2 );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( ex2->PackageListLength == 5, "got %s\n", wine_dbgstr_wn( ex2->PackageList, ex2->PackageListLength ) );
    pSspiFreeAuthIdentity( ex2 );

    ex2 = NULL;
    status = pSspiExcludePackage( ex, L"ntlm", (void **)&ex2 );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( ex2->PackageListLength == 11 && !memcmp( ex2->PackageList, L"!NTLM,!ntlm", 22 ), "got %s\n",
        wine_dbgstr_wn( ex2->PackageList, ex2->PackageListLength ) );
    pSspiFreeAuthIdentity( ex2 );
    pSspiFreeAuthIdentity( ex );

    ex = NULL;
    status = pSspiExcludePackage( NULL, L"NTLM", (void **)&ex );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( !ex->User && !ex->Domain && !ex->Password, "got %p %p %p\n", ex->User, ex->Domain, ex->Password );
    ok( ex->PackageListLength == 5, "got %lu\n", ex->PackageListLength );
    pSspiFreeAuthIdentity( ex );
    pSspiFreeAuthIdentity( id );
}

static void test_SspiGetTargetHostName(void)
{
    static const struct
    {
        const WCHAR *target, *host;
    }
    tests[] =
    {
        { L"HTTP/www.example.com", L"www.example.com" },
        { L"HTTP/www.example.com:8080", L"www.example.com" },
        { L"HTTP/www.example.com:8080/svc", L"www.example.com" },
        { L"HTTP/www.example.com/svc", L"www.example.com" },
        { L"www.example.com", L"www.example.com" },
        { L"www.example.com:8080", L"www.example.com:8080" },
        { L"host/a.b@REALM.EXAMPLE", L"a.b" },
        { L"user@host", L"user" },
        { L"", L"" },
        { L"HTTP/", L"" },
        { L"/x", L"x" },
        { L"a/b/c/d", L"b" },
        { L"a@b/c", L"c" },
        { L"MSSQLSvc/db.example.com:1433", L"db.example.com" },
        { L"HTTP/[::1]:80", L"[::1]" },
        { L"HTTP/[::1]", L"[:" },
        { L"[::1]", L"[::1]" },
    };
    SECURITY_STATUS status;
    unsigned int i;
    WCHAR *host;

    if (!pSspiGetTargetHostName)
    {
        win_skip( "SspiGetTargetHostName not exported by secur32.dll\n" );
        return;
    }

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        host = NULL;
        status = pSspiGetTargetHostName( tests[i].target, &host );
        ok( status == SEC_E_OK, "%s: got %08lx\n", wine_dbgstr_w(tests[i].target), status );
        ok( host && !lstrcmpW( host, tests[i].host ), "%s: got %s\n", wine_dbgstr_w(tests[i].target), wine_dbgstr_w(host) );
        if (status == SEC_E_OK) pSspiLocalFree( host );
    }
}

static void test_SspiPrepareForCredRead(void)
{
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE id = NULL;
    const WCHAR *target = NULL;
    SECURITY_STATUS status;
    ULONG type = 0;

    if (!pSspiPrepareForCredRead)
    {
        win_skip( "SspiPrepareForCredRead not exported by secur32.dll\n" );
        return;
    }

    pSspiEncodeStringsAsAuthIdentity( L"user", L"DOMAIN", L"password", &id );
    status = pSspiPrepareForCredRead( id, L"server.example.com", &type, &target );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( type == CRED_TYPE_DOMAIN_PASSWORD, "got %lu\n", type );
    ok( target && !lstrcmpW( target, L"server.example.com" ), "got %s\n", wine_dbgstr_w(target) );
    if (status == SEC_E_OK) pSspiLocalFree( (void *)target );
    pSspiFreeAuthIdentity( id );
}

#endif
static void test_SspiPrepareForCredWrite(void)
{
    static const WCHAR usernameW[] =
        {'u','s','e','r','n','a','m','e',0};
    static const WCHAR domainnameW[] =
        {'d','o','m','a','i','n','n','a','m','e',0};
    static const WCHAR passwordW[] =
        {'p','a','s','s','w','o','r','d',0};
    static const WCHAR targetW[] =
        {'d','o','m','a','i','n','n','a','m','e','\\','u','s','e','r','n','a','m','e',0};
    static const WCHAR target2W[] =
        {'d','o','m','a','i','n','n','a','m','e','2','\\','u','s','e','r','n','a','m','e','2',0};
    const WCHAR *target, *username;
    PSEC_WINNT_AUTH_IDENTITY_OPAQUE id;
    SECURITY_STATUS status;
    ULONG type, size;
    UCHAR *blob;

    if (!pSspiPrepareForCredWrite)
    {
        win_skip( "SspiPrepareForCredWrite not exported by secur32.dll\n" );
        return;
    }

    status = pSspiEncodeStringsAsAuthIdentity( usernameW, domainnameW, passwordW, &id );
    ok( status == SEC_E_OK, "got %08lx\n", status );

    type = size = 0;
    status = pSspiPrepareForCredWrite( id, NULL, &type, &target, &username, &blob, &size );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( type == CRED_TYPE_DOMAIN_PASSWORD, "got %lu\n", type );
    ok( !lstrcmpW( target, targetW ), "got %s\n", wine_dbgstr_w(target) );
    ok( !lstrcmpW( username, targetW ), "got %s\n", wine_dbgstr_w(username) );
    ok( !memcmp( blob, passwordW, sizeof(passwordW) - sizeof(WCHAR) ), "wrong data\n" );
    ok( size == sizeof(passwordW) - sizeof(WCHAR), "got %lu\n", size );
    pSspiLocalFree( (void *)target );
    pSspiLocalFree( (void *)username );
    pSspiLocalFree( blob );

    type = size = 0;
    status = pSspiPrepareForCredWrite( id, target2W, &type, &target, &username, &blob, &size );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    ok( type == CRED_TYPE_DOMAIN_PASSWORD, "got %lu\n", type );
    ok( !lstrcmpW( target, target2W ), "got %s\n", wine_dbgstr_w(target) );
    ok( !lstrcmpW( username, targetW ), "got %s\n", wine_dbgstr_w(username) );
    ok( !memcmp( blob, passwordW, sizeof(passwordW) - sizeof(WCHAR) ), "wrong data\n" );
    ok( size == sizeof(passwordW) - sizeof(WCHAR), "got %lu\n", size );
    pSspiLocalFree( (void *)target );
    pSspiLocalFree( (void *)username );
    pSspiLocalFree( blob );

    pSspiFreeAuthIdentity( id );
}

#ifdef __REACTOS__
static void test_kerberos_credentials(void)
{
    static const ULONG bad_use[] = { 0, SECPKG_CRED_DEFAULT, SECPKG_CRED_INBOUND | SECPKG_CRED_DEFAULT,
                                     SECPKG_CRED_OUTBOUND | SECPKG_CRED_DEFAULT, SECPKG_CRED_OUTBOUND | 0x20,
                                     SECPKG_CRED_OUTBOUND | 0x10000000 };
    SEC_WCHAR provider[] = {'K','e','r','b','e','r','o','s',0};
    SEC_WINNT_AUTH_IDENTITY_W id = { (unsigned short *)L"user", 4, (unsigned short *)L"DOMAIN", 6,
                                     (unsigned short *)L"password", 8, SEC_WINNT_AUTH_IDENTITY_UNICODE };
    SEC_WINNT_AUTH_IDENTITY_W user_only = { (unsigned short *)L"user", 4, NULL, 0, NULL, 0,
                                            SEC_WINNT_AUTH_IDENTITY_UNICODE };
    SecPkgCredentials_NamesW names;
    SECURITY_STATUS status;
    CredHandle cred;
    TimeStamp ttl;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(bad_use); i++)
    {
        ttl.LowPart = ttl.HighPart = 0x12345678;
        status = AcquireCredentialsHandleW( NULL, provider, bad_use[i], NULL, NULL, NULL, NULL, &cred, &ttl );
        ok( status == SEC_E_INVALID_TOKEN, "%lx: got %08lx\n", bad_use[i], status );
        ok( !ttl.LowPart && !ttl.HighPart, "%lx: got %08lx%08lx\n", bad_use[i], ttl.HighPart, ttl.LowPart );
    }

    status = AcquireCredentialsHandleW( NULL, provider, SECPKG_CRED_OUTBOUND, NULL, &id, NULL, NULL, &cred, &ttl );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    if (status == SEC_E_OK)
    {
        ok( ttl.HighPart == 0x7fffff36 && ttl.LowPart == 0xd5969fff, "got %08lx%08lx\n", ttl.HighPart, ttl.LowPart );
        names.sUserName = NULL;
        status = QueryCredentialsAttributesW( &cred, SECPKG_CRED_ATTR_NAMES, &names );
        ok( status == SEC_E_OK, "got %08lx\n", status );
        ok( names.sUserName && !lstrcmpW( names.sUserName, L"user@DOMAIN" ), "got %s\n", wine_dbgstr_w(names.sUserName) );
        if (status == SEC_E_OK) FreeContextBuffer( names.sUserName );
        FreeCredentialsHandle( &cred );
    }

    status = AcquireCredentialsHandleW( NULL, provider, SECPKG_CRED_INBOUND, NULL, &id, NULL, NULL, &cred, &ttl );
    ok( status == SEC_E_OK, "got %08lx\n", status );
    if (status == SEC_E_OK) FreeCredentialsHandle( &cred );

    ttl.LowPart = ttl.HighPart = 0x12345678;
    status = AcquireCredentialsHandleW( NULL, provider, SECPKG_CRED_OUTBOUND, NULL, &user_only, NULL, NULL, &cred, &ttl );
    ok( status == SEC_E_NO_CREDENTIALS, "got %08lx\n", status );
    ok( !ttl.LowPart && !ttl.HighPart, "got %08lx%08lx\n", ttl.HighPart, ttl.LowPart );

    id.Flags = 0;
    status = AcquireCredentialsHandleW( NULL, provider, SECPKG_CRED_OUTBOUND, NULL, &id, NULL, NULL, &cred, &ttl );
    ok( status == SEC_E_INVALID_TOKEN, "got %08lx\n", status );
}

#endif
static void test_kerberos(void)
{
    SecPkgInfoA *info;
    TimeStamp ttl;
    CredHandle cred;
    SECURITY_STATUS status;

    SEC_CHAR provider[] = {'K','e','r','b','e','r','o','s',0};

    static const ULONG expected_flags =
          SECPKG_FLAG_INTEGRITY
        | SECPKG_FLAG_PRIVACY
        | SECPKG_FLAG_TOKEN_ONLY
        | SECPKG_FLAG_DATAGRAM
        | SECPKG_FLAG_CONNECTION
        | SECPKG_FLAG_MULTI_REQUIRED
        | SECPKG_FLAG_EXTENDED_ERROR
        | SECPKG_FLAG_IMPERSONATION
        | SECPKG_FLAG_ACCEPT_WIN32_NAME
        | SECPKG_FLAG_NEGOTIABLE
        | SECPKG_FLAG_GSS_COMPATIBLE
        | SECPKG_FLAG_LOGON
        | SECPKG_FLAG_MUTUAL_AUTH
        | SECPKG_FLAG_DELEGATION
        | SECPKG_FLAG_READONLY_WITH_CHECKSUM;
    static const ULONG optional_mask =
          SECPKG_FLAG_RESTRICTED_TOKENS
        | SECPKG_FLAG_APPCONTAINER_CHECKS
        | 0x02000000; /* not defined in the SDK */


    status = QuerySecurityPackageInfoA(provider, &info);
    ok(status == SEC_E_OK, "Kerberos package not installed, skipping test\n");
    if(status != SEC_E_OK)
        return;

    ok( (info->fCapabilities & ~optional_mask) == expected_flags, "got %08lx, expected %08lx\n", info->fCapabilities, expected_flags );
    ok( info->wVersion == 1, "got %u\n", info->wVersion );
    ok( info->wRPCID == RPC_C_AUTHN_GSS_KERBEROS, "got %u\n", info->wRPCID );
    ok( info->cbMaxToken >= 12000, "got %lu\n", info->cbMaxToken );
    ok( !lstrcmpA( info->Name, "Kerberos" ), "got %s\n", info->Name );
    ok( !lstrcmpA( info->Comment, "Microsoft Kerberos V1.0" ), "got %s\n", info->Comment );
    FreeContextBuffer( info );

    status = AcquireCredentialsHandleA( NULL, provider, SECPKG_CRED_OUTBOUND, NULL,
                                        NULL, NULL, NULL, &cred, &ttl );
    todo_wine ok( status == SEC_E_OK, "AcquireCredentialsHandleA returned %08lx\n", status );
#ifdef __REACTOS__
    if(status == SEC_E_OK)
    {
        SecPkgCredentials_NamesA names;

        ok( ttl.HighPart == 0x7fffff36 && ttl.LowPart == 0xd5969fff, "got %08lx%08lx\n", ttl.HighPart, ttl.LowPart );
        names.sUserName = NULL;
        status = QueryCredentialsAttributesA( &cred, SECPKG_CRED_ATTR_NAMES, &names );
        ok( status == SEC_E_OK, "QueryCredentialsAttributesA returned %08lx\n", status );
        ok( names.sUserName && strchr( names.sUserName, '@' ), "got %s\n", debugstr_a(names.sUserName) );
        if (status == SEC_E_OK) FreeContextBuffer( names.sUserName );
        status = QueryCredentialsAttributesA( &cred, 0x7777, &names );
        ok( status == SEC_E_INVALID_TOKEN, "QueryCredentialsAttributesA returned %08lx\n", status );
        status = FreeCredentialsHandle( &cred );
        ok( status == SEC_E_OK, "FreeCredentialsHandle returned %08lx\n", status );
        status = FreeCredentialsHandle( &cred );
        ok( status == SEC_E_INVALID_HANDLE, "FreeCredentialsHandle returned %08lx\n", status );
    }
    test_kerberos_credentials();
#else
    if(status == SEC_E_OK)
        FreeCredentialHandle( &cred );
#endif
}

START_TEST(secur32)
{
    secdll = LoadLibraryA("secur32.dll");

    if (!secdll)
        secdll = LoadLibraryA("security.dll");

    if (secdll)
    {
        pSspiEncodeAuthIdentityAsStrings = (void *)GetProcAddress(secdll, "SspiEncodeAuthIdentityAsStrings");
        pSspiEncodeStringsAsAuthIdentity = (void *)GetProcAddress(secdll, "SspiEncodeStringsAsAuthIdentity");
        pSspiFreeAuthIdentity = (void *)GetProcAddress(secdll, "SspiFreeAuthIdentity");
        pSspiLocalFree = (void *)GetProcAddress(secdll, "SspiLocalFree");
        pSspiPrepareForCredWrite = (void *)GetProcAddress(secdll, "SspiPrepareForCredWrite");
        pSspiZeroAuthIdentity = (void *)GetProcAddress(secdll, "SspiZeroAuthIdentity");
#ifdef __REACTOS__
        pSspiCopyAuthIdentity = (void *)GetProcAddress(secdll, "SspiCopyAuthIdentity");
        pSspiValidateAuthIdentity = (void *)GetProcAddress(secdll, "SspiValidateAuthIdentity");
        pSspiIsAuthIdentityEncrypted = (void *)GetProcAddress(secdll, "SspiIsAuthIdentityEncrypted");
        pSspiEncryptAuthIdentity = (void *)GetProcAddress(secdll, "SspiEncryptAuthIdentity");
        pSspiDecryptAuthIdentity = (void *)GetProcAddress(secdll, "SspiDecryptAuthIdentity");
        pSspiEncryptAuthIdentityEx = (void *)GetProcAddress(GetModuleHandleA("sspicli.dll"), "SspiEncryptAuthIdentityEx");
        pSspiDecryptAuthIdentityEx = (void *)GetProcAddress(GetModuleHandleA("sspicli.dll"), "SspiDecryptAuthIdentityEx");
        pSspiCompareAuthIdentities = (void *)GetProcAddress(secdll, "SspiCompareAuthIdentities");
        pSspiMarshalAuthIdentity = (void *)GetProcAddress(secdll, "SspiMarshalAuthIdentity");
        pSspiUnmarshalAuthIdentity = (void *)GetProcAddress(secdll, "SspiUnmarshalAuthIdentity");
        pSspiPrepareForCredRead = (void *)GetProcAddress(secdll, "SspiPrepareForCredRead");
        pSspiExcludePackage = (void *)GetProcAddress(secdll, "SspiExcludePackage");
        pSspiGetTargetHostName = (void *)GetProcAddress(secdll, "SspiGetTargetHostName");
#endif
        pGetComputerObjectNameA = (PVOID)GetProcAddress(secdll, "GetComputerObjectNameA");
        pGetComputerObjectNameW = (PVOID)GetProcAddress(secdll, "GetComputerObjectNameW");
        pGetUserNameExA = (PVOID)GetProcAddress(secdll, "GetUserNameExA");
        pGetUserNameExW = (PVOID)GetProcAddress(secdll, "GetUserNameExW");
        pInitSecurityInterfaceA = (PVOID)GetProcAddress(secdll, "InitSecurityInterfaceA");
        pInitSecurityInterfaceW = (PVOID)GetProcAddress(secdll, "InitSecurityInterfaceW");

        if (pGetComputerObjectNameA)
            testGetComputerObjectNameA();
        else
            win_skip("GetComputerObjectNameA not exported by secur32.dll\n");

        if (pGetComputerObjectNameW)
            testGetComputerObjectNameW();
        else
            win_skip("GetComputerObjectNameW not exported by secur32.dll\n");

        if (pGetUserNameExA)
            testGetUserNameExA();
        else
            win_skip("GetUserNameExA not exported by secur32.dll\n");

        if (pGetUserNameExW)
            testGetUserNameExW();
        else
            win_skip("GetUserNameExW not exported by secur32.dll\n");

        test_InitSecurityInterface();
        test_SspiEncodeStringsAsAuthIdentity();
        test_SspiPrepareForCredWrite();
#ifdef __REACTOS__
        test_SspiCopyAuthIdentity();
        test_SspiAuthIdentityLayout();
        test_SspiCompareAuthIdentities();
        test_SspiMarshalAuthIdentity();
        test_SspiEncryptAuthIdentity();
        test_SspiExcludePackage();
        test_SspiGetTargetHostName();
        test_SspiPrepareForCredRead();
#endif

        FreeLibrary(secdll);
    }

    test_kerberos();
}
