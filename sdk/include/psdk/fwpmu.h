/*
 * PROJECT:     LiberNT PSDK headers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform user-mode management API
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once
#define _FWPMU_

#ifdef _KRPCENV_
#define FWPMU_KRPCENV_DEFINED
#endif

#ifdef GUID_DEFS_ONLY
#include <fwpmk.h>
#else
#define GUID_DEFS_ONLY
#include <fwpmk.h>
#undef GUID_DEFS_ONLY
#endif

#ifndef FWPMU_KRPCENV_DEFINED
#undef _KRPCENV_
#endif
#undef FWPMU_KRPCENV_DEFINED

#ifndef GUID_DEFS_ONLY

#ifdef __cplusplus
extern "C" {
#endif

#define FWPM_TXN_READ_ONLY (0x00000001)

void
WINAPI
FwpmFreeMemory0(
    _Inout_ void **p);

DWORD
WINAPI
FwpmEngineOpen0(
    _In_opt_ const wchar_t *serverName,
    _In_ UINT32 authnService,
    _In_opt_ SEC_WINNT_AUTH_IDENTITY_W *authIdentity,
    _In_opt_ const FWPM_SESSION0 *session,
    _Out_ HANDLE *engineHandle);

DWORD
WINAPI
FwpmEngineClose0(
    _Inout_ HANDLE engineHandle);

DWORD
WINAPI
FwpmTransactionBegin0(
    _In_ HANDLE engineHandle,
    _In_ UINT32 flags);

DWORD
WINAPI
FwpmTransactionCommit0(
    _In_ HANDLE engineHandle);

DWORD
WINAPI
FwpmTransactionAbort0(
    _In_ HANDLE engineHandle);

DWORD
WINAPI
FwpmProviderAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_PROVIDER0 *provider,
    _In_opt_ PSECURITY_DESCRIPTOR sd);

DWORD
WINAPI
FwpmProviderDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key);

DWORD
WINAPI
FwpmSubLayerAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_SUBLAYER0 *subLayer,
    _In_opt_ PSECURITY_DESCRIPTOR sd);

DWORD
WINAPI
FwpmSubLayerDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key);

DWORD
WINAPI
FwpmSubLayerGetByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key,
    _Outptr_ FWPM_SUBLAYER0 **subLayer);

DWORD
WINAPI
FwpmCalloutAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_CALLOUT0 *callout,
    _In_opt_ PSECURITY_DESCRIPTOR sd,
    _Out_opt_ UINT32 *id);

DWORD
WINAPI
FwpmCalloutDeleteById0(
    _In_ HANDLE engineHandle,
    _In_ UINT32 id);

DWORD
WINAPI
FwpmCalloutDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key);

DWORD
WINAPI
FwpmFilterAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_FILTER0 *filter,
    _In_opt_ PSECURITY_DESCRIPTOR sd,
    _Out_opt_ UINT64 *id);

DWORD
WINAPI
FwpmFilterDeleteById0(
    _In_ HANDLE engineHandle,
    _In_ UINT64 id);

DWORD
WINAPI
FwpmFilterDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key);

DWORD
WINAPI
FwpmGetAppIdFromFileName0(
    _In_ PCWSTR fileName,
    _Outptr_ FWP_BYTE_BLOB **appId);

#ifdef __cplusplus
}
#endif

#endif

#define FWPM_NOTIFY_ADDRESSES_ALL 3
#define FWPM_NOTIFY_ADDRESSES_AUTO_RESOLVE 1
#define FWPM_NOTIFY_ADDRESSES_NON_AUTO_RESOLVE 2
#define FWPM_NOTIFY_GRANULAR 4

typedef void (WINAPI *FWPM_CALLOUT_CHANGE_CALLBACK0)(void *, const FWPM_CALLOUT_CHANGE0 *);

typedef void (WINAPI *FWPM_CONNECTION_CALLBACK0)(void *, FWPM_CONNECTION_EVENT_TYPE, const FWPM_CONNECTION0 *);

typedef void (WINAPI *FWPM_DYNAMIC_KEYWORD_CALLBACK0)(void *, void *);

typedef void (WINAPI *FWPM_FILTER_CHANGE_CALLBACK0)(void *, const FWPM_FILTER_CHANGE0 *);

typedef void (WINAPI *FWPM_NET_EVENT_CALLBACK0)(void *, const FWPM_NET_EVENT1 *);

typedef void (WINAPI *FWPM_NET_EVENT_CALLBACK1)(void *, const FWPM_NET_EVENT2 *);

typedef void (WINAPI *FWPM_NET_EVENT_CALLBACK2)(void *, const FWPM_NET_EVENT3 *);

typedef void (WINAPI *FWPM_NET_EVENT_CALLBACK3)(void *, const FWPM_NET_EVENT4 *);

typedef void (WINAPI *FWPM_NET_EVENT_CALLBACK4)(void *, const FWPM_NET_EVENT5 *);

typedef void (WINAPI *FWPM_PROVIDER_CHANGE_CALLBACK0)(void *, const FWPM_PROVIDER_CHANGE0 *);

typedef void (WINAPI *FWPM_PROVIDER_CONTEXT_CHANGE_CALLBACK0)(void *, const FWPM_PROVIDER_CONTEXT_CHANGE0 *);

typedef void (WINAPI *FWPM_SUBLAYER_CHANGE_CALLBACK0)(void *, const FWPM_SUBLAYER_CHANGE0 *);

typedef void (WINAPI *FWPM_SYSTEM_PORTS_CALLBACK0)(void *, const FWPM_SYSTEM_PORTS0 *);

typedef DWORD (WINAPI *FWPM_VSWITCH_EVENT_CALLBACK0)(void *, const FWPM_VSWITCH_EVENT0 *);

typedef DWORD (WINAPI *IPSEC_KEY_MANAGER_DICTATE_KEY0)(IPSEC_SA_DETAILS1 *, IPSEC_SA_DETAILS1 *, BOOL *);

typedef void (WINAPI *IPSEC_KEY_MANAGER_KEY_DICTATION_CHECK0)(const IKEEXT_TRAFFIC0 *, BOOL *, UINT32 *);

typedef void (WINAPI *IPSEC_KEY_MANAGER_NOTIFY_KEY0)(const IPSEC_SA_DETAILS1 *, const IPSEC_SA_DETAILS1 *);

struct _IPSEC_KEY_MANAGER_CALLBACKS0
{
    GUID reserved;
    UINT32 flags;
    IPSEC_KEY_MANAGER_KEY_DICTATION_CHECK0 keyDictationCheck;
    IPSEC_KEY_MANAGER_DICTATE_KEY0 keyDictation;
    IPSEC_KEY_MANAGER_NOTIFY_KEY0 keyNotify;
};

typedef struct _IPSEC_KEY_MANAGER_CALLBACKS0 IPSEC_KEY_MANAGER_CALLBACKS0;

typedef void (WINAPI *IPSEC_SA_CONTEXT_CALLBACK0)(void *, const IPSEC_SA_CONTEXT_CHANGE0 *);

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
DWORD WINAPI FwpmDynamicKeywordUnsubscribe0(HANDLE subscriptionHandle);
DWORD WINAPI FwpmNetEventSubscribe0(HANDLE engineHandle, const FWPM_NET_EVENT_SUBSCRIPTION0 *subscription, FWPM_NET_EVENT_CALLBACK0 callback, void *context, HANDLE *eventsHandle);
DWORD WINAPI FwpmNetEventSubscribe1(HANDLE engineHandle, const FWPM_NET_EVENT_SUBSCRIPTION0 *subscription, FWPM_NET_EVENT_CALLBACK1 callback, void *context, HANDLE *eventsHandle);
DWORD WINAPI FwpmNetEventSubscribe2(HANDLE engineHandle, const FWPM_NET_EVENT_SUBSCRIPTION0 *subscription, FWPM_NET_EVENT_CALLBACK2 callback, void *context, HANDLE *eventsHandle);
DWORD WINAPI FwpmNetEventSubscribe3(HANDLE engineHandle, const FWPM_NET_EVENT_SUBSCRIPTION0 *subscription, FWPM_NET_EVENT_CALLBACK3 callback, void *context, HANDLE *eventsHandle);
DWORD WINAPI IPsecKeyManagerAddAndRegister0(HANDLE engineHandle, const IPSEC_KEY_MANAGER0 *keyManager, const IPSEC_KEY_MANAGER_CALLBACKS0 *keyManagerCallbacks, HANDLE *keyMgmtHandle);
DWORD WINAPI IPsecKeyManagerGetSecurityInfoByKey0(HANDLE engineHandle, const void *reserved, SECURITY_INFORMATION securityInfo, PSID *sidOwner, PSID *sidGroup, PACL *dacl, PACL *sacl, PSECURITY_DESCRIPTOR *securityDescriptor);
DWORD WINAPI IPsecKeyManagerSetSecurityInfoByKey0(HANDLE engineHandle, const void *reserved, SECURITY_INFORMATION securityInfo, const SID *sidOwner, const SID *sidGroup, const ACL *dacl, const ACL *sacl);
DWORD WINAPI IPsecKeyManagersGet0(HANDLE engineHandle, IPSEC_KEY_MANAGER0 ***entries, UINT32 *numEntries);
DWORD WINAPI IPsecKeyManagerUnregisterAndDelete0(HANDLE engineHandle, HANDLE keyMgmtHandle);
DWORD WINAPI IPsecSaContextSubscribe0(HANDLE engineHandle, const IPSEC_SA_CONTEXT_SUBSCRIPTION0 *subscription, IPSEC_SA_CONTEXT_CALLBACK0 callback, void *context, HANDLE *eventsHandle);
DWORD WINAPI IPsecSaContextSubscriptionsGet0(HANDLE engineHandle, IPSEC_SA_CONTEXT_SUBSCRIPTION0 ***entries, UINT32 *numEntries);
DWORD WINAPI IPsecSaContextUnsubscribe0(HANDLE engineHandle, HANDLE eventsHandle);

#ifdef __cplusplus
}
#endif

