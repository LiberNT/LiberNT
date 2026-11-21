/*
 * Copyright 2022 Hans Leidekker for CodeWeavers
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

#ifndef __WINE_MINSCHANNEL_H__
#define __WINE_MINSCHANNEL_H__

#define SECPKG_ATTR_ISSUER_LIST             0x50
#define SECPKG_ATTR_REMOTE_CRED             0x51
#define SECPKG_ATTR_LOCAL_CRED              0x52
#define SECPKG_ATTR_REMOTE_CERT_CONTEXT     0x53
#define SECPKG_ATTR_LOCAL_CERT_CONTEXT      0x54
#define SECPKG_ATTR_ROOT_STORE              0x55
#define SECPKG_ATTR_SUPPORTED_ALGS          0x56
#define SECPKG_ATTR_CIPHER_STRENGTHS        0x57
#define SECPKG_ATTR_SUPPORTED_PROTOCOLS     0x58
#define SECPKG_ATTR_ISSUER_LIST_EX          0x59
#define SECPKG_ATTR_CONNECTION_INFO         0x5a
#define SECPKG_ATTR_EAP_KEY_BLOCK           0x5b
#define SECPKG_ATTR_MAPPED_CRED_ATTR        0x5c
#define SECPKG_ATTR_SESSION_INFO            0x5d
#define SECPKG_ATTR_APP_DATA                0x5e
#define SECPKG_ATTR_REMOTE_CERTIFICATES     0x5f
#define SECPKG_ATTR_CLIENT_CERT_POLICY      0x60
#define SECPKG_ATTR_CC_POLICY_RESULT        0x61
#define SECPKG_ATTR_USE_NCRYPT              0x62
#define SECPKG_ATTR_LOCAL_CERT_INFO         0x63
#define SECPKG_ATTR_CIPHER_INFO             0x64

#ifndef SECPKG_ATTR_CERT_CHECK_RESULT
#define SECPKG_ATTR_CERT_CHECK_RESULT 0x71
#endif
#ifndef SECPKG_ATTR_CERT_CHECK_RESULT_INPROC
#define SECPKG_ATTR_CERT_CHECK_RESULT_INPROC 0x72
#endif
#ifndef SECPKG_ATTR_CONNECTION_INFO_EX
#define SECPKG_ATTR_CONNECTION_INFO_EX 0x6e
#endif
#ifndef SECPKG_ATTR_EAP_PRF_INFO
#define SECPKG_ATTR_EAP_PRF_INFO 0x65
#endif
#ifndef SECPKG_ATTR_EARLY_START
#define SECPKG_ATTR_EARLY_START 0x69
#endif
#ifndef SECPKG_ATTR_KEYING_MATERIAL
#define SECPKG_ATTR_KEYING_MATERIAL 0x6b
#endif
#ifndef SECPKG_ATTR_KEYING_MATERIAL_INFO
#define SECPKG_ATTR_KEYING_MATERIAL_INFO 0x6a
#endif
#ifndef SECPKG_ATTR_KEYING_MATERIAL_INPROC
#define SECPKG_ATTR_KEYING_MATERIAL_INPROC 0x70
#endif
#ifndef SECPKG_ATTR_KEYING_MATERIAL_TOKEN_BINDING
#define SECPKG_ATTR_KEYING_MATERIAL_TOKEN_BINDING 0x6f
#endif
#ifndef SECPKG_ATTR_REMOTE_CERT_CHAIN
#define SECPKG_ATTR_REMOTE_CERT_CHAIN 0x67
#endif
#ifndef SECPKG_ATTR_SERIALIZED_REMOTE_CERT_CONTEXT
#define SECPKG_ATTR_SERIALIZED_REMOTE_CERT_CONTEXT 0x75
#endif
#ifndef SECPKG_ATTR_SERIALIZED_REMOTE_CERT_CONTEXT_INPROC
#define SECPKG_ATTR_SERIALIZED_REMOTE_CERT_CONTEXT_INPROC 0x74
#endif
#ifndef SECPKG_ATTR_SESSION_TICKET_KEYS
#define SECPKG_ATTR_SESSION_TICKET_KEYS 0x73
#endif
#ifndef SECPKG_ATTR_SRTP_PARAMETERS
#define SECPKG_ATTR_SRTP_PARAMETERS 0x6c
#endif
#ifndef SECPKG_ATTR_SUPPORTED_SIGNATURES
#define SECPKG_ATTR_SUPPORTED_SIGNATURES 0x66
#endif
#ifndef SECPKG_ATTR_TOKEN_BINDING
#define SECPKG_ATTR_TOKEN_BINDING 0x6d
#endif
#ifndef SECPKG_ATTR_UI_INFO
#define SECPKG_ATTR_UI_INFO 0x68
#endif
#ifndef SESSION_TICKET_INFO_V0
#define SESSION_TICKET_INFO_V0 0
#endif

typedef struct _SecPkgCred_ClientCertPolicy {
  DWORD dwFlags;
  GUID guidPolicyId;
  DWORD dwCertFlags;
  DWORD dwUrlRetrievalTimeout;
  BOOL fCheckRevocationFreshnessTime;
  DWORD dwRevocationFreshnessTime;
  BOOL fOmitUsageCheck;
  LPWSTR pwszSslCtlStoreName;
  LPWSTR pwszSslCtlIdentifier;
} SecPkgCred_ClientCertPolicy, *PSecPkgCred_ClientCertPolicy;

typedef struct _SecPkgCred_SessionTicketKey {
  DWORD TicketInfoVersion;
  BYTE KeyId[16];
  BYTE KeyingMaterial[64];
  BYTE KeyingMaterialSize;
} SecPkgCred_SessionTicketKey, *PSecPkgCred_SessionTicketKey;

typedef struct _SecPkgCred_SessionTicketKeys {
  DWORD cSessionTicketKeys;
  PSecPkgCred_SessionTicketKey pSessionTicketKeys;
} SecPkgCred_SessionTicketKeys, *PSecPkgCred_SessionTicketKeys;

#endif /* __WINE_MINSCHANNEL_H__ */
