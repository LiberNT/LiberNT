/*
 * Copyright (C) 2005 Juan Lang
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
#ifndef __SCHANNEL_H__
#define __SCHANNEL_H__

#include <minschannel.h>
#include <wincrypt.h>

/* Package names */
#define UNISP_NAME_A "Microsoft Unified Security Protocol Provider"
#if defined(_MSC_VER) || defined(__MINGW32__)
#define UNISP_NAME_W L"Microsoft Unified Security Protocol Provider"
#else
static const WCHAR UNISP_NAME_W[] = { 'M','i','c','r','o','s','o','f','t',
 ' ','U','n','i','f','i','e','d',' ','S','e','c','u','r','i','t','y',' ',
 'P','r','o','t','o','c','o','l',' ','P','r','o','v','i','d','e','r',0 };
#endif
#define UNISP_NAME WINELIB_NAME_AW(UNISP_NAME_)

#define SSL2SP_NAME_A   "Microsoft SSL 2.0"
#if defined(_MSC_VER) || defined(__MINGW32__)
#define SSL2SP_NAME_W  L"Microsoft SSL 2.0"
#else
static const WCHAR SSL2SP_NAME_W[] = { 'M','i','c','r','o','s','o','f','t',
 ' ','S','S','L',' ','2','.','0',0 };
#endif
#define SSL2SP_NAME WINELIB_NAME_AW(SSL2SP_NAME_)

#define SSL3SP_NAME_A   "Microsoft SSL 3.0"
#if defined(_MSC_VER) || defined(__MINGW32__)
#define SSL3SP_NAME_W  L"Microsoft SSL 3.0"
#else
static const WCHAR SSL3SP_NAME_W[] = { 'M','i','c','r','o','s','o','f','t',
 ' ','S','S','L',' ','3','.','0',0 };
#endif
#define SSL3SP_NAME WINELIB_NAME_AW(SSL3SP_NAME_)

#define TLS1SP_NAME_A   "Microsoft TLS 1.0"
#if defined(_MSC_VER) || defined(__MINGW32__)
#define TLS1SP_NAME_W  L"Microsoft TLS 1.0"
#else
static const WCHAR TLS1SP_NAME_W[] = { 'M','i','c','r','o','s','o','f','t',
 ' ','T','L','S',' ','1','.','0',0 };
#endif
#define TLS1SP_NAME WINELIB_NAME_AW(TLS1SP_NAME_)

#define PCT1SP_NAME_A   "Microsoft PCT 1.0"
#if defined(_MSC_VER) || defined(__MINGW32__)
#define PCT1SP_NAME_W  L"Microsoft PCT 1.0"
#else
static const WCHAR PCT1SP_NAME_W[] = { 'M','i','c','r','o','s','o','f','t',
 ' ','P','C','T',' ','1','.','0',0 };
#endif
#define PCT1SP_NAME WINELIB_NAME_AW(PCT1SP_NAME_)

#define SCHANNEL_NAME_A "Schannel"
#if defined(_MSC_VER) || defined(__MINGW32__)
#define SCHANNEL_NAME_W  L"Schannel"
#else
static const WCHAR SCHANNEL_NAME_W[] = { 'S','c','h','a','n','n','e','l',0 };
#endif
#define SCHANNEL_NAME WINELIB_NAME_AW(SCHANNEL_NAME_)

#define SCH_CRED_V1           1
#define SCH_CRED_V2           2
#define SCH_CRED_VERSION      2
#define SCH_CRED_V3           3
#define SCHANNEL_CRED_VERSION 4
#define SCH_CREDENTIALS_VERSION 5

#define SCHANNEL_RENEGOTIATE 0
#define SCHANNEL_SHUTDOWN    1
#define SCHANNEL_ALERT       2
#define SCHANNEL_SESSION     3

typedef struct _SCHANNEL_ALERT_TOKEN
{
    DWORD dwTokenType;
    DWORD dwAlertType;
    DWORD dwAlertNumber;
} SCHANNEL_ALERT_TOKEN;

#define TLS1_ALERT_WARNING  1
#define TLS1_ALERT_FATAL    2

#define TLS1_ALERT_CLOSE_NOTIFY         0
#define TLS1_ALERT_UNEXPECTED_MESSAGE   10
#define TLS1_ALERT_BAD_RECORD_MAC       20
#define TLS1_ALERT_DECRYPTION_FAILED    21
#define TLS1_ALERT_RECORD_OVERFLOW      22
#define TLS1_ALERT_DECOMPRESSION_FAIL   30
#define TLS1_ALERT_HANDSHAKE_FAILURE    40
#define TLS1_ALERT_BAD_CERTIFICATE      42
#define TLS1_ALERT_UNSUPPORTED_CERT     43
#define TLS1_ALERT_CERTIFICATE_REVOKED  44
#define TLS1_ALERT_CERTIFICATE_EXPIRED  45
#define TLS1_ALERT_CERTIFICATE_UNKNOWN  46
#define TLS1_ALERT_ILLEGAL_PARAMETER    47
#define TLS1_ALERT_UNKNOWN_CA           48
#define TLS1_ALERT_ACCESS_DENIED        49
#define TLS1_ALERT_DECODE_ERROR         50
#define TLS1_ALERT_DECRYPT_ERROR        51
#define TLS1_ALERT_EXPORT_RESTRICTION   60
#define TLS1_ALERT_PROTOCOL_VERSION     70
#define TLS1_ALERT_INSUFFIENT_SECURITY  71
#define TLS1_ALERT_INTERNAL_ERROR       80
#define TLS1_ALERT_USER_CANCELED        90
#define TLS1_ALERT_NO_RENEGOTIATION     100
#define TLS1_ALERT_UNSUPPORTED_EXT      110
#define TLS1_ALERT_UNKNOWN_PSK_IDENTITY 115
#define TLS1_ALERT_NO_APP_PROTOCOL      120

#define SP_PROT_ALL           0xffffffff
#define SP_PROT_UNI_CLIENT    0x80000000
#define SP_PROT_UNI_SERVER    0x40000000
#define SP_PROT_DTLS1_2_SERVER 0x00040000
#define SP_PROT_DTLS1_2_CLIENT 0x00080000
#define SP_PROT_DTLS_SERVER    0x00010000
#define SP_PROT_DTLS_CLIENT    0x00020000
#define SP_PROT_DTLS1_0_SERVER SP_PROT_DTLS_SERVER
#define SP_PROT_DTLS1_0_CLIENT SP_PROT_DTLS_CLIENT
#define SP_PROT_TLS1_3_SERVER 0x00001000
#define SP_PROT_TLS1_3_CLIENT 0x00002000
#define SP_PROT_TLS1_2_CLIENT 0x00000800
#define SP_PROT_TLS1_2_SERVER 0x00000400
#define SP_PROT_TLS1_1_CLIENT 0x00000200
#define SP_PROT_TLS1_1_SERVER 0x00000100
#define SP_PROT_TLS1_0_CLIENT SP_PROT_TLS1_CLIENT
#define SP_PROT_TLS1_0_SERVER SP_PROT_TLS1_SERVER
#define SP_PROT_TLS1_CLIENT   0x00000080
#define SP_PROT_TLS1_SERVER   0x00000040
#define SP_PROT_SSL3_CLIENT   0x00000020
#define SP_PROT_SSL3_SERVER   0x00000010
#define SP_PROT_SSL2_CLIENT   0x00000008
#define SP_PROT_SSL2_SERVER   0x00000004
#define SP_PROT_PCT1_CLIENT   0x00000002
#define SP_PROT_PCT1_SERVER   0x00000001
#define SP_PROT_NONE          0x00000000

#define SP_PROT_UNI                (SP_PROT_UNI_CLIENT | SP_PROT_UNI_SERVER)
#define SP_PROT_DTLS               (SP_PROT_DTLS_SERVER | SP_PROT_DTLS_CLIENT)
#define SP_PROT_DTLS1_0            (SP_PROT_DTLS1_0_SERVER | SP_PROT_DTLS1_0_CLIENT)
#define SP_PROT_DTLS1_2            (SP_PROT_DTLS1_2_SERVER | SP_PROT_DTLS1_2_CLIENT)
#define SP_PROT_TLS1_3             (SP_PROT_TLS1_3_CLIENT | SP_PROT_TLS1_3_SERVER)
#define SP_PROT_TLS1_2             (SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_2_SERVER)
#define SP_PROT_TLS1_1             (SP_PROT_TLS1_1_CLIENT | SP_PROT_TLS1_1_SERVER)
#define SP_PROT_TLS1_0             (SP_PROT_TLS1_0_CLIENT | SP_PROT_TLS1_0_SERVER)
#define SP_PROT_TLS1               (SP_PROT_TLS1_CLIENT | SP_PROT_TLS1_SERVER)
#define SP_PROT_SSL3               (SP_PROT_SSL3_CLIENT | SP_PROT_SSL3_SERVER)
#define SP_PROT_SSL2               (SP_PROT_SSL2_CLIENT | SP_PROT_SSL2_SERVER)
#define SP_PROT_PCT1               (SP_PROT_PCT1_CLIENT | SP_PROT_PCT1_SERVER)

#define SP_PROT_SSL3TLS1_CLIENTS   (SP_PROT_SSL3_CLIENT | SP_PROT_TLS1_CLIENT)
#define SP_PROT_SSL3TLS1_SERVERS   (SP_PROT_SSL3_SERVER | SP_PROT_TLS1_SERVER)
#define SP_PROT_SSL3TLS1_X_CLIENTS (SP_PROT_SSL3_CLIENT | SP_PROT_TLS1_X_CLIENT)
#define SP_PROT_SSL3TLS1_X_SERVERS (SP_PROT_SSL3_SERVER | SP_PROT_TLS1_X_SERVER)
#define SP_PROT_TLS1_X_CLIENT      ( SP_PROT_TLS1_0_CLIENT \
                                   | SP_PROT_TLS1_1_CLIENT \
                                   | SP_PROT_TLS1_2_CLIENT \
                                   | SP_PROT_TLS1_3_CLIENT )
#define SP_PROT_TLS1_X_SERVER      ( SP_PROT_TLS1_0_SERVER \
                                   | SP_PROT_TLS1_1_SERVER \
                                   | SP_PROT_TLS1_2_SERVER \
                                   | SP_PROT_TLS1_3_SERVER )
#define SP_PROT_TLS1_1PLUS_CLIENT  ( SP_PROT_TLS1_1_CLIENT \
                                   | SP_PROT_TLS1_2_CLIENT \
                                   | SP_PROT_TLS1_3_CLIENT)
#define SP_PROT_TLS1_1PLUS_SERVER  ( SP_PROT_TLS1_1_SERVER \
                                   | SP_PROT_TLS1_2_SERVER \
                                   | SP_PROT_TLS1_3_SERVER )
#define SP_PROT_DTLS1_X_SERVER     ( SP_PROT_DTLS1_0_SERVER \
                                   | SP_PROT_DTLS1_2_SERVER )
#define SP_PROT_DTLS1_X_CLIENT     ( SP_PROT_DTLS1_0_CLIENT \
                                   | SP_PROT_DTLS1_2_CLIENT )
#define SP_PROT_DTLS1_X            ( SP_PROT_DTLS1_X_SERVER \
                                   | SP_PROT_DTLS1_X_CLIENT )
#define SP_PROT_CLIENTS            (SP_PROT_PCT1_CLIENT | SP_PROT_SSL2_CLIENT | SP_PROT_SSL3_CLIENT \
                                  | SP_PROT_TLS1_CLIENT | SP_PROT_UNI_CLIENT)
#define SP_PROT_SERVERS            (SP_PROT_PCT1_SERVER | SP_PROT_SSL2_SERVER | SP_PROT_SSL3_SERVER \
                                  | SP_PROT_TLS1_SERVER | SP_PROT_UNI_SERVER)
#define SP_PROT_X_CLIENTS          ( SP_PROT_CLIENTS \
                                   | SP_PROT_TLS1_X_CLIENT \
                                   | SP_PROT_DTLS1_X_CLIENT)
#define SP_PROT_X_SERVERS          ( SP_PROT_SERVERS \
                                   | SP_PROT_TLS1_X_SERVER \
                                   | SP_PROT_DTLS1_X_SERVER)

#define SP_PROT_SSL3TLS1           (SP_PROT_SSL3 | SP_PROT_TLS1)
#define SP_PROT_SSL3TLS1_X         (SP_PROT_SSL3 | SP_PROT_TLS1_X)
#define SP_PROT_TLS1_X             (SP_PROT_TLS1_X_CLIENT | SP_PROT_TLS1_X_SERVER)
#define SP_PROT_TLS1_1PLUS         (SP_PROT_TLS1_1PLUS_CLIENT | SP_PROT_TLS1_1PLUS_SERVER)

#define SCH_CRED_NO_SYSTEM_MAPPER                    2
#define SCH_CRED_NO_SERVERNAME_CHECK                 4
#define SCH_CRED_MANUAL_CRED_VALIDATION              8
#define SCH_CRED_NO_DEFAULT_CREDS                    16
#define SCH_CRED_AUTO_CRED_VALIDATION                32
#define SCH_CRED_USE_DEFAULT_CREDS                   64
#define SCH_CRED_REVOCATION_CHECK_CHAIN_END_CERT     256
#define SCH_CRED_REVOCATION_CHECK_CHAIN              512
#define SCH_CRED_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT 1024
#define SCH_CRED_IGNORE_NO_REVOCATION_CHECK          2048
#define SCH_CRED_IGNORE_REVOCATION_OFFLINE           4096

#define UNISP_RPC_ID 14

struct _HMAPPER;

typedef struct _SCHANNEL_CRED
{
    DWORD dwVersion;
    DWORD cCreds;
    PCCERT_CONTEXT *paCred;
    HCERTSTORE hRootStore;
    DWORD cMappers;
    struct _HMAPPER **aphMappers;
    DWORD cSupportedAlgs;
    ALG_ID *palgSupportedAlgs;
    DWORD grbitEnabledProtocols;
    DWORD dwMinimumCipherStrength;
    DWORD dwMaximumCipherStrength;
    DWORD dwSessionLifespan;
    DWORD dwFlags;
    DWORD dwCredFormat;
} SCHANNEL_CRED, *PSCHANNEL_CRED;

#ifdef SCHANNEL_USE_BLACKLISTS

typedef enum _eTlsAlgorithmUsage
{
    TlsParametersCngAlgUsageKeyExchange,
    TlsParametersCngAlgUsageSignature,
    TlsParametersCngAlgUsageCipher,
    TlsParametersCngAlgUsageDigest,
    TlsParametersCngAlgUsageCertSig,
} eTlsAlgorithmUsage;

typedef struct _CRYPTO_SETTINGS
{
    eTlsAlgorithmUsage eAlgorithmUsage;
    UNICODE_STRING     strCngAlgId;
    DWORD              cChainingModes;
    PUNICODE_STRING    rgstrChainingModes;
    DWORD              dwMinBitLength;
    DWORD              dwMaxBitLength;
} CRYPTO_SETTINGS, *PCRYPTO_SETTINGS;

typedef struct _TLS_PARAMETERS
{
    DWORD            cAlpnIds;
    PUNICODE_STRING  rgstrAlpnIds;
    DWORD            grbitDisabledProtocols;
    DWORD            cDisabledCrypto;
    PCRYPTO_SETTINGS pDisabledCrypto;
    DWORD            dwFlags;
} TLS_PARAMETERS, *PTLS_PARAMETERS;

typedef struct _SCH_CREDENTIALS
{
    DWORD             dwVersion;
    DWORD             dwCredFormat;
    DWORD             cCreds;
    PCCERT_CONTEXT   *paCred;
    HCERTSTORE        hRootStore;
    DWORD             cMappers;
    struct _HMAPPER **aphMappers;
    DWORD             dwSessionLifespan;
    DWORD             dwFlags;
    DWORD             cTlsParameters;
    PTLS_PARAMETERS   pTlsParameters;
} SCH_CREDENTIALS, *PSCH_CREDENTIALS;

#endif

typedef struct _SecPkgCred_SupportedAlgs
{
    DWORD cSupportedAlgs;
    ALG_ID *palgSupportedAlgs;
} SecPkgCred_SupportedAlgs, *PSecPkgCred_SupportedAlgs;

typedef struct _SecPkgCred_CipherStrengths
{
    DWORD dwMinimumCipherStrength;
    DWORD dwMaximumCipherStrength;
} SecPkgCred_CipherStrengths, *PSecPkgCred_CipherStrengths;

typedef struct _SecPkgCred_SupportedProtocols
{
    DWORD grbitProtocol;
} SecPkgCred_SupportedProtocols, *PSecPkgCred_SupportedProtocols;

typedef struct _SecPkgContext_IssuerListInfoEx
{
    PCERT_NAME_BLOB aIssuers;
    DWORD cIssuers;
} SecPkgContext_IssuerListInfoEx, *PSecPkgContext_IssuerListInfoEx;

typedef struct _SecPkgContext_ConnectionInfo
{
    DWORD dwProtocol;
    ALG_ID aiCipher;
    DWORD dwCipherStrength;
    ALG_ID aiHash;
    DWORD dwHashStrength;
    ALG_ID aiExch;
    DWORD dwExchStrength;
} SecPkgContext_ConnectionInfo, *PSecPkgContext_ConnectionInfo;

#define SECPKGCONTEXT_CIPHERINFO_V1 1
#define SZ_ALG_MAX_SIZE 64

typedef struct _SecPkgContext_CipherInfo
{
    DWORD dwVersion;
    DWORD dwProtocol;
    DWORD dwCipherSuite;
    DWORD dwBaseCipherSuite;
    WCHAR szCipherSuite[SZ_ALG_MAX_SIZE];
    WCHAR szCipher[SZ_ALG_MAX_SIZE];
    DWORD dwCipherLen;
    DWORD dwCipherBlockLen;
    WCHAR szHash[SZ_ALG_MAX_SIZE];
    DWORD dwHashLen;
    WCHAR szExchange[SZ_ALG_MAX_SIZE];
    DWORD dwMinExchangeLen;
    DWORD dwMaxExchangeLen;
    WCHAR szCertificate[SZ_ALG_MAX_SIZE];
    DWORD dwKeyType;
} SecPkgContext_CipherInfo, *PSecPkgContext_CipherInfo;

#ifndef ENABLE_TLS_CLIENT_EARLY_START
#define ENABLE_TLS_CLIENT_EARLY_START 0x00000001
#endif
#ifndef KERN_CONTEXT_CERT_INFO_V1
#define KERN_CONTEXT_CERT_INFO_V1 0x00000000
#endif
#ifndef LCRED_CRED_EXISTS
#define LCRED_CRED_EXISTS 0x00000001
#endif
#ifndef LCRED_STATUS_NOCRED
#define LCRED_STATUS_NOCRED 0x00000000
#endif
#ifndef LCRED_STATUS_UNKNOWN_ISSUER
#define LCRED_STATUS_UNKNOWN_ISSUER 0x00000002
#endif
#ifndef RCRED_CRED_EXISTS
#define RCRED_CRED_EXISTS 0x00000001
#endif
#ifndef RCRED_STATUS_NOCRED
#define RCRED_STATUS_NOCRED 0x00000000
#endif
#ifndef RCRED_STATUS_UNKNOWN_ISSUER
#define RCRED_STATUS_UNKNOWN_ISSUER 0x00000002
#endif
#ifndef SCHANNEL_SECRET_PRIVKEY
#define SCHANNEL_SECRET_PRIVKEY 0x00000002
#endif
#ifndef SCH_ALLOW_NULL_ENCRYPTION
#define SCH_ALLOW_NULL_ENCRYPTION 0x02000000
#endif
#ifndef SCH_CRED_CACHE_ONLY_URL_RETRIEVAL
#define SCH_CRED_CACHE_ONLY_URL_RETRIEVAL 0x00008000
#endif
#ifndef SCH_CRED_CACHE_ONLY_URL_RETRIEVAL_ON_CREATE
#define SCH_CRED_CACHE_ONLY_URL_RETRIEVAL_ON_CREATE 0x00020000
#endif
#ifndef SCH_CRED_CERT_CONTEXT
#define SCH_CRED_CERT_CONTEXT 0x00000003
#endif
#ifndef SCH_CRED_DEFERRED_CRED_VALIDATION
#define SCH_CRED_DEFERRED_CRED_VALIDATION 0x04000000
#endif
#ifndef SCH_CRED_DISABLE_RECONNECTS
#define SCH_CRED_DISABLE_RECONNECTS 0x00000080
#endif
#ifndef SCH_CRED_FORMAT_CERT_CONTEXT
#define SCH_CRED_FORMAT_CERT_CONTEXT 0x00000000
#endif
#ifndef SCH_CRED_FORMAT_CERT_HASH
#define SCH_CRED_FORMAT_CERT_HASH 0x00000001
#endif
#ifndef SCH_CRED_FORMAT_CERT_HASH_STORE
#define SCH_CRED_FORMAT_CERT_HASH_STORE 0x00000002
#endif
#ifndef SCH_CRED_MAX_STORE_NAME_SIZE
#define SCH_CRED_MAX_STORE_NAME_SIZE 128
#endif
#ifndef SCH_CRED_MAX_SUPPORTED_ALGS
#define SCH_CRED_MAX_SUPPORTED_ALGS 256
#endif
#ifndef SCH_CRED_MAX_SUPPORTED_CERTS
#define SCH_CRED_MAX_SUPPORTED_CERTS 100
#endif
#ifndef SCH_CRED_MEMORY_STORE_CERT
#define SCH_CRED_MEMORY_STORE_CERT 0x00010000
#endif
#ifndef SCH_CRED_RESTRICTED_ROOTS
#define SCH_CRED_RESTRICTED_ROOTS 0x00002000
#endif
#ifndef SCH_CRED_REVOCATION_CHECK_CACHE_ONLY
#define SCH_CRED_REVOCATION_CHECK_CACHE_ONLY 0x00004000
#endif
#ifndef SCH_CRED_REVOCATION_CHECK_END_CERT
#define SCH_CRED_REVOCATION_CHECK_END_CERT 0x00000100
#endif
#ifndef SCH_CRED_SNI_CREDENTIAL
#define SCH_CRED_SNI_CREDENTIAL 0x00080000
#endif
#ifndef SCH_CRED_SNI_ENABLE_OCSP
#define SCH_CRED_SNI_ENABLE_OCSP 0x00100000
#endif
#ifndef SCH_CRED_X509_CAPI
#define SCH_CRED_X509_CAPI 0x00000002
#endif
#ifndef SCH_CRED_X509_CERTCHAIN
#define SCH_CRED_X509_CERTCHAIN 0x00000001
#endif
#ifndef SCH_MACHINE_CERT_HASH
#define SCH_MACHINE_CERT_HASH 0x00000001
#endif
#ifndef SCH_MAX_EXT_SUBSCRIPTIONS
#define SCH_MAX_EXT_SUBSCRIPTIONS 2
#endif
#ifndef SCH_SEND_AUX_RECORD
#define SCH_SEND_AUX_RECORD 0x00200000
#endif
#ifndef SCH_SEND_ROOT_CERT
#define SCH_SEND_ROOT_CERT 0x00040000
#endif
#ifndef SCH_USE_DTLS_ONLY
#define SCH_USE_DTLS_ONLY 0x01000000
#endif
#ifndef SCH_USE_PRESHAREDKEY_ONLY
#define SCH_USE_PRESHAREDKEY_ONLY 0x00800000
#endif
#ifndef SCH_USE_STRONG_CRYPTO
#define SCH_USE_STRONG_CRYPTO 0x00400000
#endif
#ifndef SECPKGCONTEXT_CONNECTION_INFO_EX_V1
#define SECPKGCONTEXT_CONNECTION_INFO_EX_V1 1
#endif
#ifndef SSL_SESSION_DISABLE_RECONNECTS
#define SSL_SESSION_DISABLE_RECONNECTS 2
#endif
#ifndef SSL_SESSION_ENABLE_RECONNECTS
#define SSL_SESSION_ENABLE_RECONNECTS 1
#endif
#ifndef SSL_SESSION_RECONNECT
#define SSL_SESSION_RECONNECT 1
#endif

typedef enum _eTlsHashAlgorithm {
    TlsHashAlgorithm_None = 0,
    TlsHashAlgorithm_Md5 = 1,
    TlsHashAlgorithm_Sha1 = 2,
    TlsHashAlgorithm_Sha224 = 3,
    TlsHashAlgorithm_Sha256 = 4,
    TlsHashAlgorithm_Sha384 = 5,
    TlsHashAlgorithm_Sha512 = 6
} eTlsHashAlgorithm;

typedef enum _eTlsSignatureAlgorithm {
    TlsSignatureAlgorithm_Anonymous = 0,
    TlsSignatureAlgorithm_Rsa = 1,
    TlsSignatureAlgorithm_Dsa = 2,
    TlsSignatureAlgorithm_Ecdsa = 3
} eTlsSignatureAlgorithm;

typedef enum _SchGetExtensionsOptions {
    SCH_EXTENSIONS_OPTIONS_NONE = 0,
    SCH_NO_RECORD_HEADER = 1
} SchGetExtensionsOptions;

#ifdef __cplusplus
extern "C" {
#endif
BOOL WINAPI SslEmptyCacheA(LPSTR pszTargetName, DWORD dwFlags);
BOOL WINAPI SslEmptyCacheW(LPWSTR pszTargetName, DWORD dwFlags);
#ifdef __cplusplus
}
#endif

typedef struct _PctPublicKey {
  DWORD Type;
  DWORD cbKey;
  UCHAR pKey[1];
} PctPublicKey;

typedef struct _SCH_CRED {
  DWORD dwVersion;
  DWORD cCreds;
  PVOID * paSecret;
  PVOID * paPublic;
  DWORD cMappers;
  struct _HMAPPER ** aphMappers;
} SCH_CRED, *PSCH_CRED;

typedef struct _SCH_CRED_PUBLIC_CERTCHAIN {
  DWORD dwType;
  DWORD cbCertChain;
  PBYTE pCertChain;
} SCH_CRED_PUBLIC_CERTCHAIN, *PSCH_CRED_PUBLIC_CERTCHAIN;

typedef struct _SCH_CRED_SECRET_CAPI {
  DWORD dwType;
  HCRYPTPROV hProv;
} SCH_CRED_SECRET_CAPI, *PSCH_CRED_SECRET_CAPI;

typedef struct _SCH_CRED_SECRET_PRIVKEY {
  DWORD dwType;
  PBYTE pPrivateKey;
  DWORD cbPrivateKey;
  PSTR pszPassword;
} SCH_CRED_SECRET_PRIVKEY, *PSCH_CRED_SECRET_PRIVKEY;

typedef struct _SCH_EXTENSION_DATA {
  WORD ExtensionType;
  const BYTE * pExtData;
  DWORD cbExtData;
} SCH_EXTENSION_DATA;

typedef struct _SCHANNEL_CERT_HASH {
  DWORD dwLength;
  DWORD dwFlags;
  HCRYPTPROV hProv;
  BYTE ShaHash[20];
} SCHANNEL_CERT_HASH, *PSCHANNEL_CERT_HASH;

typedef struct _SCHANNEL_CERT_HASH_STORE {
  DWORD dwLength;
  DWORD dwFlags;
  HCRYPTPROV hProv;
  BYTE ShaHash[20];
  WCHAR pwszStoreName[128];
} SCHANNEL_CERT_HASH_STORE, *PSCHANNEL_CERT_HASH_STORE;

typedef struct _SCHANNEL_CLIENT_SIGNATURE {
  DWORD cbLength;
  ALG_ID aiHash;
  DWORD cbHash;
  BYTE HashValue[36];
  BYTE CertThumbprint[20];
} SCHANNEL_CLIENT_SIGNATURE, *PSCHANNEL_CLIENT_SIGNATURE;

typedef struct _SCHANNEL_SESSION_TOKEN {
  DWORD dwTokenType;
  DWORD dwFlags;
} SCHANNEL_SESSION_TOKEN;

typedef struct _SecPkgContext_Certificates {
  DWORD cCertificates;
  DWORD cbCertificateChain;
  PBYTE pbCertificateChain;
} SecPkgContext_Certificates, *PSecPkgContext_Certificates;

typedef struct _SecPkgContext_CertificateValidationResult {
  DWORD dwChainErrorStatus;
  HRESULT hrVerifyChainStatus;
} SecPkgContext_CertificateValidationResult, *PSecPkgContext_CertificateValidationResult;

typedef struct _SecPkgContext_CertInfo {
  DWORD dwVersion;
  DWORD cbSubjectName;
  LPWSTR pwszSubjectName;
  DWORD cbIssuerName;
  LPWSTR pwszIssuerName;
  DWORD dwKeySize;
} SecPkgContext_CertInfo, *PSecPkgContext_CertInfo;

typedef struct _SecPkgContext_ClientCertPolicyResult {
  HRESULT dwPolicyResult;
  GUID guidPolicyId;
} SecPkgContext_ClientCertPolicyResult, *PSecPkgContext_ClientCertPolicyResult;

typedef struct _SecPkgContext_ConnectionInfoEx {
  DWORD dwVersion;
  DWORD dwProtocol;
  WCHAR szCipher[64];
  DWORD dwCipherStrength;
  WCHAR szHash[64];
  DWORD dwHashStrength;
  WCHAR szExchange[64];
  DWORD dwExchStrength;
} SecPkgContext_ConnectionInfoEx, *PSecPkgContext_ConnectionInfoEx;

typedef struct _SecPkgContext_EapKeyBlock {
  BYTE rgbKeys[128];
  BYTE rgbIVs[64];
} SecPkgContext_EapKeyBlock, *PSecPkgContext_EapKeyBlock;

typedef struct _SecPkgContext_EapPrfInfo {
  DWORD dwVersion;
  DWORD cbPrfData;
  PBYTE pbPrfData;
} SecPkgContext_EapPrfInfo, *PSecPkgContext_EapPrfInfo;

typedef struct _SecPkgContext_EarlyStart {
  DWORD dwEarlyStartFlags;
} SecPkgContext_EarlyStart, *PSecPkgContext_EarlyStart;

typedef struct _SecPkgContext_KeyingMaterial {
  DWORD cbKeyingMaterial;
  PBYTE pbKeyingMaterial;
} SecPkgContext_KeyingMaterial, *PSecPkgContext_KeyingMaterial;

typedef struct _SecPkgContext_KeyingMaterial_Inproc {
  WORD cbLabel;
  LPSTR pszLabel;
  WORD cbContextValue;
  PBYTE pbContextValue;
  DWORD cbKeyingMaterial;
  PBYTE pbKeyingMaterial;
} SecPkgContext_KeyingMaterial_Inproc, *PSecPkgContext_KeyingMaterial_Inproc;

typedef struct _SecPkgContext_KeyingMaterialInfo {
  WORD cbLabel;
  LPSTR pszLabel;
  WORD cbContextValue;
  PBYTE pbContextValue;
  DWORD cbKeyingMaterial;
} SecPkgContext_KeyingMaterialInfo, *PSecPkgContext_KeyingMaterialInfo;

typedef struct _SecPkgContext_LocalCredentialInfo {
  DWORD cbCertificateChain;
  PBYTE pbCertificateChain;
  DWORD cCertificates;
  DWORD fFlags;
  DWORD dwBits;
} SecPkgContext_LocalCredentialInfo, *PSecPkgContext_LocalCredentialInfo;

typedef struct _SecPkgContext_MappedCredAttr {
  DWORD dwAttribute;
  PVOID pvBuffer;
} SecPkgContext_MappedCredAttr, *PSecPkgContext_MappedCredAttr;

typedef struct _SecPkgContext_RemoteCredentialInfo {
  DWORD cbCertificateChain;
  PBYTE pbCertificateChain;
  DWORD cCertificates;
  DWORD fFlags;
  DWORD dwBits;
} SecPkgContext_RemoteCredentialInfo, *PSecPkgContext_RemoteCredentialInfo;

typedef struct _SecPkgContext_SessionAppData {
  DWORD dwFlags;
  DWORD cbAppData;
  PBYTE pbAppData;
} SecPkgContext_SessionAppData, *PSecPkgContext_SessionAppData;

typedef struct _SecPkgContext_SessionInfo {
  DWORD dwFlags;
  DWORD cbSessionId;
  BYTE rgbSessionId[32];
} SecPkgContext_SessionInfo, *PSecPkgContext_SessionInfo;

typedef struct _SecPkgContext_SrtpParameters {
  WORD ProtectionProfile;
  BYTE MasterKeyIdentifierSize;
  PBYTE MasterKeyIdentifier;
} SecPkgContext_SrtpParameters, *PSecPkgContext_SrtpParameters;

typedef struct _SecPkgContext_SupportedSignatures {
  WORD cSignatureAndHashAlgorithms;
  WORD * pSignatureAndHashAlgorithms;
} SecPkgContext_SupportedSignatures, *PSecPkgContext_SupportedSignatures;

typedef struct _SecPkgContext_TokenBinding {
  BYTE MajorVersion;
  BYTE MinorVersion;
  WORD KeyParametersSize;
  PBYTE KeyParameters;
} SecPkgContext_TokenBinding, *PSecPkgContext_TokenBinding;

typedef struct _SecPkgContext_UiInfo {
  HWND hParentWindow;
} SecPkgContext_UiInfo, *PSecPkgContext_UiInfo;

typedef struct _SEND_GENERIC_TLS_EXTENSION {
  WORD ExtensionType;
  WORD HandshakeType;
  DWORD Flags;
  WORD BufferSize;
  UCHAR Buffer[1];
} SEND_GENERIC_TLS_EXTENSION, *PSEND_GENERIC_TLS_EXTENSION;

typedef struct _SSL_CREDENTIAL_CERTIFICATE {
  DWORD cbPrivateKey;
  PBYTE pPrivateKey;
  DWORD cbCertificate;
  PBYTE pCertificate;
  PSTR pszPassword;
} SSL_CREDENTIAL_CERTIFICATE, *PSSL_CREDENTIAL_CERTIFICATE;

typedef struct _TLS_EXTENSION_SUBSCRIPTION {
  WORD ExtensionType;
  WORD HandshakeType;
} TLS_EXTENSION_SUBSCRIPTION, *PTLS_EXTENSION_SUBSCRIPTION;

typedef struct _SUBSCRIBE_GENERIC_TLS_EXTENSION {
  DWORD Flags;
  DWORD SubscriptionsCount;
  TLS_EXTENSION_SUBSCRIPTION Subscriptions[1];
} SUBSCRIBE_GENERIC_TLS_EXTENSION, *PSUBSCRIBE_GENERIC_TLS_EXTENSION;

typedef struct _X509Certificate {
  DWORD Version;
  DWORD SerialNumber[4];
  ALG_ID SignatureAlgorithm;
  FILETIME ValidFrom;
  FILETIME ValidUntil;
  PSTR pszIssuer;
  PSTR pszSubject;
  PctPublicKey * pPublicKey;
} X509Certificate, *PX509Certificate;

#ifdef UNICODE
#define DEFAULT_TLS_SSP_NAME DEFAULT_TLS_SSP_NAME_W
#else
#define DEFAULT_TLS_SSP_NAME DEFAULT_TLS_SSP_NAME_A
#endif
#define DEFAULT_TLS_SSP_NAME_A "Default TLS SSP"
#define DEFAULT_TLS_SSP_NAME_W L"Default TLS SSP"
#define SCHANNEL_SECRET_TYPE_CAPI 0x00000001
#define SP_PROT_TLS1_3PLUS 0x3000
#define SP_PROT_TLS1_3PLUS_CLIENT 0x2000
#define SP_PROT_TLS1_3PLUS_SERVER 0x1000
#define SSL_CRACK_CERTIFICATE_NAME 0
#define SSL_EMPTY_CACHE_FN 0
#define SSL_FREE_CERTIFICATE_NAME TEXT("SslFreeCertificate")
#ifdef UNICODE
#define SslEmptyCache SslEmptyCacheW
#else
#ifdef UNICODE
#define SslEmptyCache SslEmptyCacheW
#else
#define SslEmptyCache SslEmptyCacheA
#endif
#endif

typedef SecPkgContext_LocalCredentialInfo *PSecPkgContext_LocalCredenitalInfo;

typedef SecPkgContext_RemoteCredentialInfo *PSecPkgContext_RemoteCredenitalInfo;

typedef SecPkgContext_LocalCredentialInfo SecPkgContext_LocalCredenitalInfo;

typedef SecPkgContext_RemoteCredentialInfo SecPkgContext_RemoteCredenitalInfo;

typedef BOOL (WINAPI *SSL_CRACK_CERTIFICATE_FN)(PUCHAR, DWORD, BOOL, PX509Certificate *);

typedef BOOL (WINAPI *SSL_EMPTY_CACHE_FN_A)(LPSTR, DWORD);

typedef BOOL (WINAPI *SSL_EMPTY_CACHE_FN_W)(LPWSTR, DWORD);

typedef void (WINAPI *SSL_FREE_CERTIFICATE_FN)(PX509Certificate);

typedef SECURITY_STATUS (WINAPI *SslDeserializeCertificateStoreFn)(CERT_BLOB, PCCERT_CONTEXT *);

typedef SECURITY_STATUS (WINAPI *SslGetExtensionsFn)(const BYTE *, DWORD, SCH_EXTENSION_DATA *, BYTE, DWORD *, SchGetExtensionsOptions);

typedef SECURITY_STATUS (WINAPI *SslGetServerIdentityFn)(PBYTE, DWORD, PBYTE *, PDWORD, DWORD);

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
BOOL WINAPI SslCrackCertificate(PUCHAR pbCertificate, DWORD cbCertificate, DWORD dwFlags, PX509Certificate *ppCertificate);
SECURITY_STATUS SslDeserializeCertificateStore(CERT_BLOB SerializedCertificateStore, PCCERT_CONTEXT *ppCertContext);
void SslFreeCertificate(PX509Certificate pCertificate);
BOOL WINAPI SslGenerateKeyPair(PSSL_CREDENTIAL_CERTIFICATE pCerts, PSTR pszDN, PSTR pszPassword, DWORD Bits);
void SslGenerateRandomBits(PUCHAR pRandomData, LONG cRandomData);
BOOL WINAPI SslGetDefaultIssuers(PBYTE pbIssuers, DWORD *pcbIssuers);
SECURITY_STATUS SslGetExtensions(const BYTE *clientHello, DWORD clientHelloByteSize, SCH_EXTENSION_DATA *genericExtensions, BYTE genericExtensionsCount, DWORD *bytesToRead, SchGetExtensionsOptions flags);
DWORD WINAPI SslGetMaximumKeySize(DWORD Reserved);
SECURITY_STATUS SslGetServerIdentity(PBYTE ClientHello, DWORD ClientHelloSize, PBYTE *ServerIdentity, PDWORD ServerIdentitySize, DWORD Flags);

#ifdef __cplusplus
}
#endif

#endif /* __SCHANNEL_H__ */
