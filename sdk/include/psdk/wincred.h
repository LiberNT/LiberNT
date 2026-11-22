/*
 * Copyright (C) 2006 Robert Shearman (for CodeWeavers)
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

#ifndef _WINCRED_H_
#define _WINCRED_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _ADVAPI32_
#define WINADVAPI
#else
#define WINADVAPI DECLSPEC_IMPORT
#endif

#ifndef __SECHANDLE_DEFINED__
#define __SECHANDLE_DEFINED__
typedef struct _SecHandle
{
    ULONG_PTR dwLower;
    ULONG_PTR dwUpper;
} SecHandle, *PSecHandle;
#endif

#ifndef __WINE_CTXTHANDLE_DEFINED__
#define __WINE_CTXTHANDLE_DEFINED__
typedef SecHandle CtxtHandle;
typedef PSecHandle PCtxtHandle;
#endif

typedef struct _CREDENTIAL_ATTRIBUTEA
{
    LPSTR Keyword;
    DWORD Flags;
    DWORD ValueSize;
    LPBYTE Value;
} CREDENTIAL_ATTRIBUTEA, *PCREDENTIAL_ATTRIBUTEA;

typedef struct _CREDENTIAL_ATTRIBUTEW
{
    LPWSTR Keyword;
    DWORD Flags;
    DWORD ValueSize;
    LPBYTE Value;
} CREDENTIAL_ATTRIBUTEW, *PCREDENTIAL_ATTRIBUTEW;

DECL_WINELIB_TYPE_AW(CREDENTIAL_ATTRIBUTE)
DECL_WINELIB_TYPE_AW(PCREDENTIAL_ATTRIBUTE)

typedef struct _CREDENTIALA
{
    DWORD Flags;
    DWORD Type;
    LPSTR TargetName;
    LPSTR Comment;
    FILETIME LastWritten;
    DWORD CredentialBlobSize;
    _Field_size_bytes_(CredentialBlobSize) LPBYTE CredentialBlob;
    DWORD Persist;
    DWORD AttributeCount;
    PCREDENTIAL_ATTRIBUTEA Attributes;
    LPSTR TargetAlias;
    LPSTR UserName;
} CREDENTIALA, *PCREDENTIALA;

typedef struct _CREDENTIALW
{
    DWORD Flags;
    DWORD Type;
    LPWSTR TargetName;
    LPWSTR Comment;
    FILETIME LastWritten;
    DWORD CredentialBlobSize;
    LPBYTE CredentialBlob;
    DWORD Persist;
    DWORD AttributeCount;
    PCREDENTIAL_ATTRIBUTEW Attributes;
    LPWSTR TargetAlias;
    LPWSTR UserName;
} CREDENTIALW, *PCREDENTIALW;

DECL_WINELIB_TYPE_AW(CREDENTIAL)
DECL_WINELIB_TYPE_AW(PCREDENTIAL)

typedef struct _CREDENTIAL_TARGET_INFORMATIONA
{
    LPSTR TargetName;
    LPSTR NetbiosServerName;
    LPSTR DnsServerName;
    LPSTR NetbiosDomainName;
    LPSTR DnsDomainName;
    LPSTR DnsTreeName;
    LPSTR PackageName;
    DWORD Flags;
    DWORD CredTypeCount;
    LPDWORD CredTypes;
} CREDENTIAL_TARGET_INFORMATIONA, *PCREDENTIAL_TARGET_INFORMATIONA;

typedef struct _CREDENTIAL_TARGET_INFORMATIONW
{
    LPWSTR TargetName;
    LPWSTR NetbiosServerName;
    LPWSTR DnsServerName;
    LPWSTR NetbiosDomainName;
    LPWSTR DnsDomainName;
    LPWSTR DnsTreeName;
    LPWSTR PackageName;
    DWORD Flags;
    DWORD CredTypeCount;
    LPDWORD CredTypes;
} CREDENTIAL_TARGET_INFORMATIONW, *PCREDENTIAL_TARGET_INFORMATIONW;

DECL_WINELIB_TYPE_AW(CREDENTIAL_TARGET_INFORMATION)
DECL_WINELIB_TYPE_AW(PCREDENTIAL_TARGET_INFORMATION)

typedef struct _CREDUI_INFOA
{
    DWORD cbSize;
    HWND hwndParent;
    PCSTR pszMessageText;
    PCSTR pszCaptionText;
    HBITMAP hbmBanner;
} CREDUI_INFOA, *PCREDUI_INFOA;

typedef struct _CREDUI_INFOW
{
    DWORD cbSize;
    HWND hwndParent;
    PCWSTR pszMessageText;
    PCWSTR pszCaptionText;
    HBITMAP hbmBanner;
} CREDUI_INFOW, *PCREDUI_INFOW;

DECL_WINELIB_TYPE_AW(CREDUI_INFO)
DECL_WINELIB_TYPE_AW(PCREDUI_INFO)

typedef enum _CRED_MARSHAL_TYPE
{
    CertCredential = 1,
    UsernameTargetCredential,
    BinaryBlobCredential,
    UsernameForPackedCredentials = 4,
    BinaryBlobForSystem = 5,
} CRED_MARSHAL_TYPE, *PCRED_MARSHAL_TYPE;

#define CERT_HASH_LENGTH    20

typedef struct _CERT_CREDENTIAL_INFO
{
    ULONG cbSize;
    UCHAR rgbHashOfCert[CERT_HASH_LENGTH];
} CERT_CREDENTIAL_INFO, *PCERT_CREDENTIAL_INFO;

typedef struct _USERNAME_TARGET_CREDENTIAL_INFO
{
    LPWSTR UserName;
} USERNAME_TARGET_CREDENTIAL_INFO;

typedef struct _BINARY_BLOB_CREDENTIAL_INFO
{
    ULONG cbBlob;
    LPBYTE pbBlob;
} BINARY_BLOB_CREDENTIAL_INFO, *PBINARY_BLOB_CREDENTIAL_INFO;

#define CRED_MAX_STRING_LENGTH              256
#define CRED_MAX_USERNAME_LENGTH            513
#define CRED_MAX_GENERIC_TARGET_NAME_LENGTH 32767
#define CRED_MAX_DOMAIN_TARGET_NAME_LENGTH  337
#define CRED_MAX_VALUE_SIZE                 256
#define CRED_MAX_ATTRIBUTES                 64

#define CRED_MAX_BLOB_SIZE                  512
#define CRED_MAX_CREDENTIAL_BLOB_SIZE       (5 * CRED_MAX_BLOB_SIZE)

#define CREDUI_MAX_MESSAGE_LENGTH 1024
#define CREDUI_MAX_CAPTION_LENGTH 128
#define CREDUI_MAX_GENERIC_TARGET_LENGTH CRED_MAX_GENERIC_TARGET_NAME_LENGTH
#define CREDUI_MAX_DOMAIN_TARGET_LENGTH CRED_MAX_DOMAIN_TARGET_NAME_LENGTH
#define CREDUI_MAX_USERNAME_LENGTH CRED_MAX_USERNAME_LENGTH
#define CREDUI_MAX_PASSWORD_LENGTH (512 / 2)

/* flags for CREDENTIAL::Flags */
#define CRED_FLAGS_PASSWORD_FOR_CERT                0x0001
#define CRED_FLAGS_PROMPT_NOW                       0x0002
#define CRED_FLAGS_USERNAME_TARGET                  0x0004
#define CRED_FLAGS_OWF_CRED_BLOB                    0x0008
#define CRED_FLAGS_VALID_FLAGS                      0xF0FF

/* values for CREDENTIAL::Type */
#define CRED_TYPE_GENERIC                           1
#define CRED_TYPE_DOMAIN_PASSWORD                   2
#define CRED_TYPE_DOMAIN_CERTIFICATE                3
#define CRED_TYPE_DOMAIN_VISIBLE_PASSWORD           4
#define CRED_TYPE_GENERIC_CERTIFICATE               5
#define CRED_TYPE_DOMAIN_EXTENDED                   6
#define CRED_TYPE_MAXIMUM                           7
#define CRED_TYPE_MAXIMUM_EX                        (CRED_TYPE_MAXIMUM+1000)

/* values for CREDENTIAL::Persist */
#define CRED_PERSIST_NONE                           0
#define CRED_PERSIST_SESSION                        1
#define CRED_PERSIST_LOCAL_MACHINE                  2
#define CRED_PERSIST_ENTERPRISE                     3

/* values for CREDENTIAL_TARGET_INFORMATION::Flags */
#define CRED_TI_SERVER_FORMAT_UNKNOWN               1
#define CRED_TI_DOMAIN_FORMAT_UNKNOWN               2
#define CRED_TI_ONLY_PASSWORD_REQUIRED              4

#define CREDUI_FLAGS_INCORRECT_PASSWORD             0x00000001
#define CREDUI_FLAGS_DO_NOT_PERSIST                 0x00000002
#define CREDUI_FLAGS_REQUEST_ADMINISTRATOR          0x00000004
#define CREDUI_FLAGS_EXCLUDE_CERTIFICATES           0x00000008
#define CREDUI_FLAGS_REQUIRE_CERTIFICATE            0x00000010
#define CREDUI_FLAGS_SHOW_SAVE_CHECK_BOX            0x00000040
#define CREDUI_FLAGS_ALWAYS_SHOW_UI                 0x00000080
#define CREDUI_FLAGS_REQUIRE_SMARTCARD              0x00000100
#define CREDUI_FLAGS_PASSWORD_ONLY_OK               0x00000200
#define CREDUI_FLAGS_VALIDATE_USERNAME              0x00000400
#define CREDUI_FLAGS_COMPLETE_USERNAME              0x00000800
#define CREDUI_FLAGS_PERSIST                        0x00001000
#define CREDUI_FLAGS_SERVER_CREDENTIAL              0x00004000
#define CREDUI_FLAGS_EXPECT_CONFIRMATION            0x00020000
#define CREDUI_FLAGS_GENERIC_CREDENTIALS            0x00040000
#define CREDUI_FLAGS_USERNAME_TARGET_CREDENTIALS    0x00080000
#define CREDUI_FLAGS_KEEP_USERNAME                  0x00100000

/* flags for CredWrite and CredWriteDomainCredentials */
#define CRED_PRESERVE_CREDENTIAL_BLOB               0x00000001

WINADVAPI BOOL WINAPI CredDeleteA(_In_ LPCSTR, _In_ DWORD, _Reserved_ DWORD);
WINADVAPI BOOL WINAPI CredDeleteW(_In_ LPCWSTR, _In_ DWORD, _Reserved_ DWORD);
#define CredDelete WINELIB_NAME_AW(CredDelete)

WINADVAPI
BOOL
WINAPI
CredEnumerateA(
  _In_opt_ LPCSTR,
  _Reserved_ DWORD,
  _Out_ DWORD *Count,
  _Outptr_result_buffer_(*Count) PCREDENTIALA **);

WINADVAPI
BOOL
WINAPI
CredEnumerateW(
  _In_opt_ LPCWSTR,
  _Reserved_ DWORD,
  _Out_ DWORD *Count,
  _Outptr_result_buffer_(*Count) PCREDENTIALW **);

#define CredEnumerate WINELIB_NAME_AW(CredEnumerate)

WINADVAPI VOID WINAPI CredFree(_In_ PVOID);

WINADVAPI
BOOL
WINAPI
CredGetSessionTypes(
  _In_ DWORD MaximumPersistCount,
  _Out_writes_(MaximumPersistCount) LPDWORD);

WINADVAPI BOOL WINAPI CredIsMarshaledCredentialA(_In_ LPCSTR);
WINADVAPI BOOL WINAPI CredIsMarshaledCredentialW(_In_ LPCWSTR);
#define CredIsMarshaledCredential WINELIB_NAME_AW(CredIsMarshaledCredential)

WINADVAPI
BOOL
WINAPI
CredMarshalCredentialA(
  _In_ CRED_MARSHAL_TYPE,
  _In_ PVOID,
  _Out_ LPSTR *);

WINADVAPI
BOOL
WINAPI
CredMarshalCredentialW(
  _In_ CRED_MARSHAL_TYPE,
  _In_ PVOID,
  _Out_ LPWSTR *);

#define CredMarshalCredential WINELIB_NAME_AW(CredMarshalCredential)

WINADVAPI
BOOL
WINAPI
CredReadA(
  _In_ LPCSTR,
  _In_ DWORD,
  _Reserved_ DWORD,
  _Out_ PCREDENTIALA *);

WINADVAPI
BOOL
WINAPI
CredReadW(
  _In_ LPCWSTR,
  _In_ DWORD,
  _Reserved_ DWORD,
  _Out_ PCREDENTIALW *);

#define CredRead WINELIB_NAME_AW(CredRead)

WINADVAPI
BOOL
WINAPI
CredReadDomainCredentialsA(
  _In_ PCREDENTIAL_TARGET_INFORMATIONA,
  _In_ DWORD,
  _Out_ DWORD *Count,
  _Outptr_result_buffer_(*Count) PCREDENTIALA **);

WINADVAPI
BOOL
WINAPI
CredReadDomainCredentialsW(
  _In_ PCREDENTIAL_TARGET_INFORMATIONW,
  _In_ DWORD,
  _Out_ DWORD *Count,
  _Outptr_result_buffer_(*Count) PCREDENTIALW **);

#define CredReadDomainCredentials WINELIB_NAME_AW(CredReadDomainCredentials)

WINADVAPI
BOOL
WINAPI
CredRenameA(
  _In_ LPCSTR,
  _In_ LPCSTR,
  _In_ DWORD,
  _Reserved_ DWORD);

WINADVAPI
BOOL
WINAPI
CredRenameW(
  _In_ LPCWSTR,
  _In_ LPCWSTR,
  _In_ DWORD,
  _Reserved_ DWORD);

#define CredRename WINELIB_NAME_AW(CredRename)

WINADVAPI
BOOL
WINAPI
CredUnmarshalCredentialA(
  _In_ LPCSTR,
  _Out_ PCRED_MARSHAL_TYPE,
  _Out_ PVOID *);

WINADVAPI
BOOL
WINAPI
CredUnmarshalCredentialW(
  _In_ LPCWSTR,
  _Out_ PCRED_MARSHAL_TYPE,
  _Out_ PVOID *);

#define CredUnmarshalCredential WINELIB_NAME_AW(CredUnmarshalCredential)

WINADVAPI BOOL WINAPI CredWriteA(_In_ PCREDENTIALA, _In_ DWORD);
WINADVAPI BOOL WINAPI CredWriteW(_In_ PCREDENTIALW, _In_ DWORD);
#define CredWrite WINELIB_NAME_AW(CredWrite)

DWORD
WINAPI
CredUICmdLinePromptForCredentialsW(
  _In_opt_ PCWSTR,
  _Reserved_ PCtxtHandle,
  _In_ DWORD,
  _Inout_updates_(ulUserBufferSize) PWSTR,
  _In_ ULONG ulUserBufferSize,
  _Inout_updates_(ulPasswordBufferSize) PWSTR,
  _In_ ULONG ulPasswordBufferSize,
  _Inout_opt_ PBOOL,
  _In_ DWORD);

DWORD
WINAPI
CredUICmdLinePromptForCredentialsA(
  _In_opt_ PCSTR,
  _Reserved_ PCtxtHandle,
  _In_ DWORD,
  _Inout_updates_(ulUserBufferSize) PSTR,
  _In_ ULONG ulUserBufferSize,
  _Inout_updates_(ulPasswordBufferSize) PSTR,
  _In_ ULONG ulPasswordBufferSize,
  _Inout_opt_ PBOOL,
  _In_ DWORD);

#define CredUICmdLinePromptForCredentials WINELIB_NAME_AW(CredUICmdLinePromptForCredentials)

DWORD WINAPI CredUIConfirmCredentialsW(_In_ PCWSTR, _In_ BOOL);
DWORD WINAPI CredUIConfirmCredentialsA(_In_ PCSTR, _In_ BOOL);
#define CredUIConfirmCredentials WINELIB_NAME_AW(CredUIConfirmCredentials)

DWORD
WINAPI
CredUIParseUserNameW(
  _In_ PCWSTR,
  _Out_writes_(userBufferSize) PWSTR,
  _In_ ULONG userBufferSize,
  _Out_writes_(domainBufferSize) PWSTR,
  _In_ ULONG domainBufferSize);

DWORD
WINAPI
CredUIParseUserNameA(
  _In_ PCSTR,
  _Out_writes_(userBufferSize) PSTR,
  _In_ ULONG userBufferSize,
  _Out_writes_(domainBufferSize) PSTR,
  _In_ ULONG domainBufferSize);

#define CredUIParseUserName WINELIB_NAME_AW(CredUIParseUserName)

DWORD
WINAPI
CredUIPromptForCredentialsW(
  _In_opt_ PCREDUI_INFOW,
  _In_opt_ PCWSTR,
  _Reserved_ PCtxtHandle,
  _In_ DWORD,
  _Inout_updates_(ulUserNameBufferSize) PWSTR,
  _In_ ULONG ulUserNameBufferSize,
  _Inout_updates_(ulPasswordBufferSize) PWSTR,
  _In_ ULONG ulPasswordBufferSize,
  _Inout_opt_ PBOOL,
  _In_ DWORD);

DWORD
WINAPI
CredUIPromptForCredentialsA(
  _In_opt_ PCREDUI_INFOA,
  _In_opt_ PCSTR,
  _Reserved_ PCtxtHandle,
  _In_ DWORD,
  _Inout_updates_(ulUserNameBufferSize) PSTR,
  _In_ ULONG ulUserNameBufferSize,
  _Inout_updates_(ulPasswordBufferSize) PSTR,
  _In_ ULONG ulPasswordBufferSize,
  _Inout_opt_ PBOOL,
  _In_ DWORD);

#define CredUIPromptForCredentials WINELIB_NAME_AW(CredUIPromptForCredentials)

DWORD
WINAPI
CredUIStoreSSOCredW(
  _In_opt_ PCWSTR,
  _In_ PCWSTR,
  _In_ PCWSTR,
   _In_ BOOL);

/* Note: no CredUIStoreSSOCredA in PSDK header */
DWORD WINAPI CredUIReadSSOCredW(_In_opt_ PCWSTR, _Outptr_ PWSTR*);
/* Note: no CredUIReadSSOCredA in PSDK header */

#ifdef __cplusplus
}
#endif

#ifndef BACK_BUTTON_IDENTIFY_AUTH_PACKAGE
#define BACK_BUTTON_IDENTIFY_AUTH_PACKAGE 0xCAD00001
#endif
#ifndef CREDUIWIN_AUTHPACKAGE_ONLY
#define CREDUIWIN_AUTHPACKAGE_ONLY 0x00000010
#endif
#ifndef CREDUIWIN_CHECKBOX
#define CREDUIWIN_CHECKBOX 0x00000002
#endif
#ifndef CREDUIWIN_DOWNLEVEL_HELLO_AS_SMART_CARD
#define CREDUIWIN_DOWNLEVEL_HELLO_AS_SMART_CARD 0x80000000
#endif
#ifndef CREDUIWIN_ENUMERATE_ADMINS
#define CREDUIWIN_ENUMERATE_ADMINS 0x00000100
#endif
#ifndef CREDUIWIN_ENUMERATE_CURRENT_USER
#define CREDUIWIN_ENUMERATE_CURRENT_USER 0x00000200
#endif
#ifndef CREDUIWIN_GENERIC
#define CREDUIWIN_GENERIC 0x00000001
#endif
#ifndef CREDUIWIN_IGNORE_CLOUDAUTHORITY_NAME
#define CREDUIWIN_IGNORE_CLOUDAUTHORITY_NAME 0x00040000
#endif
#ifndef CREDUIWIN_IN_CRED_ONLY
#define CREDUIWIN_IN_CRED_ONLY 0x00000020
#endif
#ifndef CREDUIWIN_PACK_32_WOW
#define CREDUIWIN_PACK_32_WOW 0x10000000
#endif
#ifndef CREDUIWIN_PREPROMPTING
#define CREDUIWIN_PREPROMPTING 0X00002000
#endif
#ifndef CREDUIWIN_SECURE_PROMPT
#define CREDUIWIN_SECURE_PROMPT 0x00001000
#endif
#ifndef CREDUIWIN_USE_V2
#define CREDUIWIN_USE_V2 0x00000040
#endif
#ifndef CREDUI_FOOTER_LINK_AUTHPACKAGE_ID
#define CREDUI_FOOTER_LINK_AUTHPACKAGE_ID 0XCAD0002
#endif
#ifndef CREDUI_PICKERSCREEN_AUTHPACKAGE_ID
#define CREDUI_PICKERSCREEN_AUTHPACKAGE_ID 0XCAD0003
#endif
#ifndef CRED_FLAGS_NGC_CERT
#define CRED_FLAGS_NGC_CERT 0x0080
#endif
#ifndef CRED_FLAGS_REQUIRE_CONFIRMATION
#define CRED_FLAGS_REQUIRE_CONFIRMATION 0x0010
#endif
#ifndef CRED_FLAGS_VALID_INPUT_FLAGS
#define CRED_FLAGS_VALID_INPUT_FLAGS 0xF09F
#endif
#ifndef CRED_FLAGS_VSM_PROTECTED
#define CRED_FLAGS_VSM_PROTECTED 0x0040
#endif
#ifndef CRED_FLAGS_WILDCARD_MATCH
#define CRED_FLAGS_WILDCARD_MATCH 0x0020
#endif
#ifndef CRED_MAX_TARGETNAME_ATTRIBUTE_LENGTH
#define CRED_MAX_TARGETNAME_ATTRIBUTE_LENGTH 256
#endif
#ifndef CRED_MAX_TARGETNAME_NAMESPACE_LENGTH
#define CRED_MAX_TARGETNAME_NAMESPACE_LENGTH 256
#endif
#ifndef CRED_PACK_GENERIC_CREDENTIALS
#define CRED_PACK_GENERIC_CREDENTIALS 0x4
#endif
#ifndef CRED_PACK_ID_PROVIDER_CREDENTIALS
#define CRED_PACK_ID_PROVIDER_CREDENTIALS 0x8
#endif
#ifndef CRED_PACK_PROTECTED_CREDENTIALS
#define CRED_PACK_PROTECTED_CREDENTIALS 0x1
#endif
#ifndef CRED_PACK_WOW_BUFFER
#define CRED_PACK_WOW_BUFFER 0x2
#endif
#ifndef CRED_PROTECT_TO_SYSTEM
#define CRED_PROTECT_TO_SYSTEM 0x2
#endif
#ifndef CRED_TI_CREATE_EXPLICIT_CRED
#define CRED_TI_CREATE_EXPLICIT_CRED 0x0010
#endif
#ifndef CRED_TI_DNSTREE_IS_DFS_SERVER
#define CRED_TI_DNSTREE_IS_DFS_SERVER 0x0040
#endif
#ifndef CRED_TI_USERNAME_TARGET
#define CRED_TI_USERNAME_TARGET 0x0008
#endif
#ifndef CRED_TI_VALID_FLAGS
#define CRED_TI_VALID_FLAGS 0xF07F
#endif
#ifndef CRED_TI_WORKGROUP_MEMBER
#define CRED_TI_WORKGROUP_MEMBER 0x0020
#endif
#ifndef CRED_UNPROTECT_ALLOW_TO_SYSTEM
#define CRED_UNPROTECT_ALLOW_TO_SYSTEM 0x2
#endif

typedef enum _CRED_PROTECTION_TYPE {
    CredUnprotected = 0,
    CredUserProtection = 1,
    CredTrustedProtection = 2,
    CredForSystemProtection = 3
} CRED_PROTECTION_TYPE, *PCRED_PROTECTION_TYPE;

typedef struct _USERNAME_TARGET_CREDENTIAL_INFO * PUSERNAME_TARGET_CREDENTIAL_INFO;

#define CRED_ALLOW_NAME_RESOLUTION 0x1
#define CRED_CACHE_TARGET_INFORMATION 0x1
#define CRED_ENUMERATE_ALL_CREDENTIALS 0x1
#define CRED_LOGON_TYPES_MASK 0xF000
#define CRED_PROTECT_AS_SELF 0x1
#define CRED_PROTECT_VALID_FLAGS 3
#if (!(defined(UNICODE)))
#define CRED_SESSION_WILDCARD_NAME CRED_SESSION_WILDCARD_NAME_A
#endif
#define CRED_SESSION_WILDCARD_NAME_A "*Session"
#define CRED_SESSION_WILDCARD_NAME_LENGTH 8
#define CRED_SESSION_WILDCARD_NAME_W L"*Session"
#ifdef UNICODE
#define CRED_TARGETNAME_ATTRIBUTE_BATCH CRED_TARGETNAME_ATTRIBUTE_BATCH_W
#else
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_ATTRIBUTE_BATCH CRED_TARGETNAME_ATTRIBUTE_BATCH_A
#endif
#endif
#define CRED_TARGETNAME_ATTRIBUTE_BATCH_A "batch"
#define CRED_TARGETNAME_ATTRIBUTE_BATCH_LENGTH (sizeof (CRED_TARGETNAME_ATTRIBUTE_BATCH_A) - 1)
#define CRED_TARGETNAME_ATTRIBUTE_BATCH_W L"batch"
#ifdef UNICODE
#define CRED_TARGETNAME_ATTRIBUTE_CACHEDINTERACTIVE CRED_TARGETNAME_ATTRIBUTE_CACHEDINTERACTIVE_W
#else
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_ATTRIBUTE_CACHEDINTERACTIVE CRED_TARGETNAME_ATTRIBUTE_CACHEDINTERACTIVE_A
#endif
#endif
#define CRED_TARGETNAME_ATTRIBUTE_CACHEDINTERACTIVE_A "cachedinteractive"
#define CRED_TARGETNAME_ATTRIBUTE_CACHEDINTERACTIVE_LENGTH (sizeof (CRED_TARGETNAME_ATTRIBUTE_CACHEDINTERACTIVE_A) - 1)
#define CRED_TARGETNAME_ATTRIBUTE_CACHEDINTERACTIVE_W L"cachedinteractive"
#ifdef UNICODE
#define CRED_TARGETNAME_ATTRIBUTE_INTERACTIVE CRED_TARGETNAME_ATTRIBUTE_INTERACTIVE_W
#else
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_ATTRIBUTE_INTERACTIVE CRED_TARGETNAME_ATTRIBUTE_INTERACTIVE_A
#endif
#endif
#define CRED_TARGETNAME_ATTRIBUTE_INTERACTIVE_A "interactive"
#define CRED_TARGETNAME_ATTRIBUTE_INTERACTIVE_LENGTH (sizeof (CRED_TARGETNAME_ATTRIBUTE_INTERACTIVE_A) - 1)
#define CRED_TARGETNAME_ATTRIBUTE_INTERACTIVE_W L"interactive"
#ifdef UNICODE
#define CRED_TARGETNAME_ATTRIBUTE_NAME CRED_TARGETNAME_ATTRIBUTE_NAME_W
#else
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_ATTRIBUTE_NAME CRED_TARGETNAME_ATTRIBUTE_NAME_A
#endif
#endif
#define CRED_TARGETNAME_ATTRIBUTE_NAME_A "name"
#define CRED_TARGETNAME_ATTRIBUTE_NAME_LENGTH (sizeof (CRED_TARGETNAME_ATTRIBUTE_NAME_A) - 1)
#define CRED_TARGETNAME_ATTRIBUTE_NAME_W L"name"
#ifdef UNICODE
#define CRED_TARGETNAME_ATTRIBUTE_NETWORK CRED_TARGETNAME_ATTRIBUTE_NETWORK_W
#else
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_ATTRIBUTE_NETWORK CRED_TARGETNAME_ATTRIBUTE_NETWORK_A
#endif
#endif
#define CRED_TARGETNAME_ATTRIBUTE_NETWORK_A "network"
#define CRED_TARGETNAME_ATTRIBUTE_NETWORK_LENGTH (sizeof (CRED_TARGETNAME_ATTRIBUTE_NETWORK_A) - 1)
#define CRED_TARGETNAME_ATTRIBUTE_NETWORK_W L"network"
#ifdef UNICODE
#define CRED_TARGETNAME_ATTRIBUTE_NETWORKCLEARTEXT CRED_TARGETNAME_ATTRIBUTE_NETWORKCLEARTEXT_W
#else
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_ATTRIBUTE_NETWORKCLEARTEXT CRED_TARGETNAME_ATTRIBUTE_NETWORKCLEARTEXT_A
#endif
#endif
#define CRED_TARGETNAME_ATTRIBUTE_NETWORKCLEARTEXT_A "networkcleartext"
#define CRED_TARGETNAME_ATTRIBUTE_NETWORKCLEARTEXT_LENGTH (sizeof (CRED_TARGETNAME_ATTRIBUTE_NETWORKCLEARTEXT_A) - 1)
#define CRED_TARGETNAME_ATTRIBUTE_NETWORKCLEARTEXT_W L"networkcleartext"
#ifdef UNICODE
#define CRED_TARGETNAME_ATTRIBUTE_REMOTEINTERACTIVE CRED_TARGETNAME_ATTRIBUTE_REMOTEINTERACTIVE_W
#else
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_ATTRIBUTE_REMOTEINTERACTIVE CRED_TARGETNAME_ATTRIBUTE_REMOTEINTERACTIVE_A
#endif
#endif
#define CRED_TARGETNAME_ATTRIBUTE_REMOTEINTERACTIVE_A "remoteinteractive"
#define CRED_TARGETNAME_ATTRIBUTE_REMOTEINTERACTIVE_LENGTH (sizeof (CRED_TARGETNAME_ATTRIBUTE_REMOTEINTERACTIVE_A) - 1)
#define CRED_TARGETNAME_ATTRIBUTE_REMOTEINTERACTIVE_W L"remoteinteractive"
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_ATTRIBUTE_SEPERATOR = CRED_TARGETNAME_ATTRIBUTE_SEPERATOR_A
#endif
#define CRED_TARGETNAME_ATTRIBUTE_SEPERATOR_A 0x3D
#define CRED_TARGETNAME_ATTRIBUTE_SEPERATOR_W 0x3D
#ifdef UNICODE
#define CRED_TARGETNAME_ATTRIBUTE_SERVICE CRED_TARGETNAME_ATTRIBUTE_SERVICE_W
#else
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_ATTRIBUTE_SERVICE CRED_TARGETNAME_ATTRIBUTE_SERVICE_A
#endif
#endif
#define CRED_TARGETNAME_ATTRIBUTE_SERVICE_A "service"
#define CRED_TARGETNAME_ATTRIBUTE_SERVICE_LENGTH (sizeof (CRED_TARGETNAME_ATTRIBUTE_SERVICE_A) - 1)
#define CRED_TARGETNAME_ATTRIBUTE_SERVICE_W L"service"
#ifdef UNICODE
#define CRED_TARGETNAME_ATTRIBUTE_TARGET CRED_TARGETNAME_ATTRIBUTE_TARGET_W
#else
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_ATTRIBUTE_TARGET CRED_TARGETNAME_ATTRIBUTE_TARGET_A
#endif
#endif
#define CRED_TARGETNAME_ATTRIBUTE_TARGET_A "target"
#define CRED_TARGETNAME_ATTRIBUTE_TARGET_LENGTH (sizeof (CRED_TARGETNAME_ATTRIBUTE_TARGET_A) - 1)
#define CRED_TARGETNAME_ATTRIBUTE_TARGET_W L"target"
#define CRED_TARGETNAME_DOMAIN_EXTENDED_USERNAME_SEPARATOR_A 0x7C
#define CRED_TARGETNAME_DOMAIN_EXTENDED_USERNAME_SEPARATOR_W 0x7C
#ifdef UNICODE
#define CRED_TARGETNAME_DOMAIN_NAMESPACE CRED_TARGETNAME_DOMAIN_NAMESPACE_W
#else
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_DOMAIN_NAMESPACE CRED_TARGETNAME_DOMAIN_NAMESPACE_A
#endif
#endif
#define CRED_TARGETNAME_DOMAIN_NAMESPACE_A "Domain"
#define CRED_TARGETNAME_DOMAIN_NAMESPACE_LENGTH (sizeof (CRED_TARGETNAME_DOMAIN_NAMESPACE_A) - 1)
#define CRED_TARGETNAME_DOMAIN_NAMESPACE_W L"Domain"
#define CRED_TARGETNAME_LEGACYGENERIC_NAMESPACE_A "LegacyGeneric"
#define CRED_TARGETNAME_LEGACYGENERIC_NAMESPACE_LENGTH (sizeof (CRED_TARGETNAME_LEGACYGENERIC_NAMESPACE_A) - 1)
#define CRED_TARGETNAME_LEGACYGENERIC_NAMESPACE_W L"LegacyGeneric"
#if (!(defined(UNICODE)))
#define CRED_TARGETNAME_NAMESPACE_SEPERATOR = CRED_TARGETNAME_NAMESPACE_SEPERATOR_A
#endif
#define CRED_TARGETNAME_NAMESPACE_SEPERATOR_A 0x3A
#define CRED_TARGETNAME_NAMESPACE_SEPERATOR_W 0x3A
#if (!(defined(UNICODE)))
#define CRED_UNIVERSAL_WILDCARD = CRED_UNIVERSAL_WILDCARD_A
#endif
#define CRED_UNIVERSAL_WILDCARD_A 0x2A
#define CRED_UNIVERSAL_WILDCARD_W 0x2A
#define CRED_UNPROTECT_AS_SELF 0x1
#define CRED_UNPROTECT_VALID_FLAGS 3
#ifdef UNICODE
#define CredFindBestCredential CredFindBestCredentialW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define CredFindBestCredential CredFindBestCredentialW
#else
#define CredFindBestCredential CredFindBestCredentialA
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define CredGetTargetInfo CredGetTargetInfoW
#else
#define CredGetTargetInfo CredGetTargetInfoA
#endif
#endif
#ifdef UNICODE
#define CredIsProtected CredIsProtectedW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define CredIsProtected CredIsProtectedW
#else
#define CredIsProtected CredIsProtectedA
#endif
#endif
#endif
#ifdef UNICODE
#define CredPackAuthenticationBuffer CredPackAuthenticationBufferW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define CredPackAuthenticationBuffer CredPackAuthenticationBufferW
#else
#define CredPackAuthenticationBuffer CredPackAuthenticationBufferA
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define CredProtect CredProtectW
#else
#define CredProtect CredProtectA
#endif
#endif
#define CREDUI_FLAGS_PROMPT_VALID 0x1E5FDF
#define CREDUI_MAX_CREDENTIALS_BLOB_SIZE (MAXUSHORT)
#define CREDUI_MAX_PACKED_CREDENTIALS_LENGTH ((MAXUSHORT / 2) - 2)
#define CREDUIAPI EXTERN_C DECLSPEC_IMPORT
#ifdef UNICODE
#define CredUIPromptForWindowsCredentials CredUIPromptForWindowsCredentialsW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define CredUIPromptForWindowsCredentials CredUIPromptForWindowsCredentialsW
#else
#define CredUIPromptForWindowsCredentials CredUIPromptForWindowsCredentialsA
#endif
#endif
#endif
#define CREDUIWIN_VALID_FLAGS 0x90043333
#ifdef UNICODE
#define CredUnPackAuthenticationBuffer CredUnPackAuthenticationBufferW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define CredUnPackAuthenticationBuffer CredUnPackAuthenticationBufferW
#else
#define CredUnPackAuthenticationBuffer CredUnPackAuthenticationBufferA
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define CredUnprotect CredUnprotectW
#else
#define CredUnprotect CredUnprotectA
#endif
#endif
#ifdef UNICODE
#define CredWriteDomainCredentials CredWriteDomainCredentialsW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define CredWriteDomainCredentials CredWriteDomainCredentialsW
#else
#define CredWriteDomainCredentials CredWriteDomainCredentialsA
#endif
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

BOOL WINAPI CredPackAuthenticationBufferW(DWORD dwFlags, LPWSTR pszUserName, LPWSTR pszPassword, PBYTE pPackedCredentials, DWORD *pcbPackedCredentials);
DWORD WINAPI CredUIPromptForWindowsCredentialsW(PCREDUI_INFOW pUiInfo, DWORD dwAuthError, ULONG *pulAuthPackage, LPCVOID pvInAuthBuffer, ULONG ulInAuthBufferSize, LPVOID *ppvOutAuthBuffer, ULONG *pulOutAuthBufferSize, BOOL *pfSave, DWORD dwFlags);
BOOL WINAPI CredUnPackAuthenticationBufferW(DWORD dwFlags, PVOID pAuthBuffer, DWORD cbAuthBuffer, LPWSTR pszUserName, DWORD *pcchMaxUserName, LPWSTR pszDomainName, DWORD *pcchMaxDomainName, LPWSTR pszPassword, DWORD *pcchMaxPassword);

/* TODO: LiberNT does not implement the functions below yet */
BOOL CredFindBestCredentialA(LPCSTR TargetName, DWORD Type, DWORD Flags, PCREDENTIALA *Credential);
BOOL CredFindBestCredentialW(LPCWSTR TargetName, DWORD Type, DWORD Flags, PCREDENTIALW *Credential);
BOOL CredGetTargetInfoA(LPCSTR TargetName, DWORD Flags, PCREDENTIAL_TARGET_INFORMATIONA *TargetInfo);
BOOL CredGetTargetInfoW(LPCWSTR TargetName, DWORD Flags, PCREDENTIAL_TARGET_INFORMATIONW *TargetInfo);
BOOL CredIsProtectedA(LPSTR pszProtectedCredentials, CRED_PROTECTION_TYPE *pProtectionType);
BOOL CredIsProtectedW(LPWSTR pszProtectedCredentials, CRED_PROTECTION_TYPE *pProtectionType);
BOOL CredPackAuthenticationBufferA(DWORD dwFlags, LPSTR pszUserName, LPSTR pszPassword, PBYTE pPackedCredentials, DWORD *pcbPackedCredentials);
BOOL CredProtectA(BOOL fAsSelf, LPSTR pszCredentials, DWORD cchCredentials, LPSTR pszProtectedCredentials, DWORD *pcchMaxChars, CRED_PROTECTION_TYPE *ProtectionType);
BOOL CredProtectEx(ULONG Flags, LPWSTR pszCredentials, DWORD cchCredentials, LPWSTR pszProtectedCredentials, DWORD *pcchMaxChars, CRED_PROTECTION_TYPE *ProtectionType);
BOOL CredProtectW(BOOL fAsSelf, LPWSTR pszCredentials, DWORD cchCredentials, LPWSTR pszProtectedCredentials, DWORD *pcchMaxChars, CRED_PROTECTION_TYPE *ProtectionType);
DWORD CredUIPromptForWindowsCredentialsA(PCREDUI_INFOA pUiInfo, DWORD dwAuthError, ULONG *pulAuthPackage, LPCVOID pvInAuthBuffer, ULONG ulInAuthBufferSize, LPVOID *ppvOutAuthBuffer, ULONG *pulOutAuthBufferSize, BOOL *pfSave, DWORD dwFlags);
BOOL CredUnPackAuthenticationBufferA(DWORD dwFlags, PVOID pAuthBuffer, DWORD cbAuthBuffer, LPSTR pszUserName, DWORD *pcchlMaxUserName, LPSTR pszDomainName, DWORD *pcchMaxDomainName, LPSTR pszPassword, DWORD *pcchMaxPassword);
BOOL CredUnprotectA(BOOL fAsSelf, LPSTR pszProtectedCredentials, DWORD cchProtectedCredentials, LPSTR pszCredentials, DWORD *pcchMaxChars);
BOOL CredUnprotectEx(ULONG Flags, LPWSTR pszProtectedCredentials, DWORD cchProtectedCredentials, LPWSTR pszCredentials, DWORD *pcchMaxChars);
BOOL CredUnprotectW(BOOL fAsSelf, LPWSTR pszProtectedCredentials, DWORD cchProtectedCredentials, LPWSTR pszCredentials, DWORD *pcchMaxChars);
BOOL CredWriteDomainCredentialsA(PCREDENTIAL_TARGET_INFORMATIONA TargetInfo, PCREDENTIALA Credential, DWORD Flags);
BOOL CredWriteDomainCredentialsW(PCREDENTIAL_TARGET_INFORMATIONW TargetInfo, PCREDENTIALW Credential, DWORD Flags);

#ifdef __cplusplus
}
#endif

#define CredAppendLogonTypeToFlags(Flags, LogonType) (Flags) |= ((LogonType) << 12)
#define CredGetLogonTypeFromFlags(Flags) ((SECURITY_LOGON_TYPE) (((Flags) & CRED_LOGON_TYPES_MASK) >> 12))
#define CredRemoveLogonTypeFromFlags(Flags) (Flags) &= ~CRED_LOGON_TYPES_MASK
#define CREDUIP_IS_DOWNGRADE_ERROR(_Status) ((_Status) == ERROR_DOWNGRADE_DETECTED || (_Status) == __HRESULT_FROM_WIN32(ERROR_DOWNGRADE_DETECTED) || (_Status) == STATUS_DOWNGRADE_DETECTED || (_Status) == HRESULT_FROM_NT(STATUS_DOWNGRADE_DETECTED) || (_Status) == SEC_E_DOWNGRADE_DETECTED)
#define CREDUIP_IS_EXPIRED_ERROR(_Status) ((_Status) == ERROR_PASSWORD_EXPIRED || (_Status) == __HRESULT_FROM_WIN32(ERROR_PASSWORD_EXPIRED) || (_Status) == STATUS_PASSWORD_EXPIRED || (_Status) == HRESULT_FROM_NT(STATUS_PASSWORD_EXPIRED) || (_Status) == ERROR_PASSWORD_MUST_CHANGE || (_Status) == __HRESULT_FROM_WIN32(ERROR_PASSWORD_MUST_CHANGE) || (_Status) == STATUS_PASSWORD_MUST_CHANGE || (_Status) == HRESULT_FROM_NT(STATUS_PASSWORD_MUST_CHANGE) || (_Status) == NERR_PasswordExpired || (_Status) == __HRESULT_FROM_WIN32(NERR_PasswordExpired))

#endif /* _WINCRED_H_ */
