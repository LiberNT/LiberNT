/*
 * Copyright 2005, 2006 Kai Blin
 * Copyright 2016 Jacek Caban for CodeWeavers
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

#include "precomp.h"

#include <assert.h>

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(secur32);

#define KERBEROS_MAX_BUF 12000

#ifdef __REACTOS__
struct kerberos_cred
{
    ULONG use;
    BOOL password;
    ULONG name_len;
    WCHAR *name;
};

static BOOL kerberos_is_builtin_session(const LUID *logon_id)
{
    static const LUID builtin[] = { SYSTEM_LUID, ANONYMOUS_LOGON_LUID, LOCALSERVICE_LUID, NETWORKSERVICE_LUID, IUSER_LUID };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(builtin); i++)
        if (logon_id->LowPart == builtin[i].LowPart && logon_id->HighPart == builtin[i].HighPart) return TRUE;
    return FALSE;
}

static BOOL kerberos_session_has_credentials(const LUID *logon_id, const SECURITY_LOGON_SESSION_DATA *session)
{
    if (kerberos_is_builtin_session(logon_id)) return FALSE;

    switch (session->LogonType)
    {
    case Interactive:
    case Batch:
    case Service:
    case Unlock:
    case NetworkCleartext:
    case NewCredentials:
    case RemoteInteractive:
    case CachedInteractive:
    case CachedRemoteInteractive:
    case CachedUnlock:
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL kerberos_get_caller(LUID *logon_id, BOOL *tcb)
{
    TOKEN_PRIVILEGES *privs = NULL;
    TOKEN_STATISTICS stats;
    HANDLE token;
    LUID tcb_luid;
    DWORD size, i;
    BOOL ret;

    *tcb = FALSE;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) &&
        !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return FALSE;

    ret = GetTokenInformation(token, TokenStatistics, &stats, sizeof(stats), &size);
    if (ret)
    {
        *logon_id = stats.AuthenticationId;
        if (LookupPrivilegeValueW(NULL, SE_TCB_NAME, &tcb_luid) &&
            !GetTokenInformation(token, TokenPrivileges, NULL, 0, &size) &&
            GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
            (privs = HeapAlloc(GetProcessHeap(), 0, size)) &&
            GetTokenInformation(token, TokenPrivileges, privs, size, &size))
        {
            for (i = 0; i < privs->PrivilegeCount; i++)
            {
                if (privs->Privileges[i].Luid.LowPart == tcb_luid.LowPart &&
                    privs->Privileges[i].Luid.HighPart == tcb_luid.HighPart &&
                    (privs->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED))
                    *tcb = TRUE;
            }
        }
        HeapFree(GetProcessHeap(), 0, privs);
    }
    CloseHandle(token);
    return ret;
}

static WCHAR *kerberos_identity_string(const SEC_WINNT_AUTH_IDENTITY_W *id, const void *str, ULONG len, ULONG *ret_len)
{
    WCHAR *ret;
    int count;

    if (!str || !len) return NULL;
    if (id->Flags & SEC_WINNT_AUTH_IDENTITY_ANSI)
    {
        count = MultiByteToWideChar(CP_ACP, 0, str, len, NULL, 0);
        if (!(ret = HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(WCHAR)))) return NULL;
        MultiByteToWideChar(CP_ACP, 0, str, len, ret, count);
    }
    else
    {
        count = len;
        if (!(ret = HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(WCHAR)))) return NULL;
        memcpy(ret, str, count * sizeof(WCHAR));
    }
    ret[count] = 0;
    *ret_len = count;
    return ret;
}

static WCHAR *kerberos_session_string(const LSA_UNICODE_STRING *str, BOOL lower, ULONG *ret_len)
{
    WCHAR *ret;
    USHORT i;

    if (!(ret = HeapAlloc(GetProcessHeap(), 0, str->Length + sizeof(WCHAR)))) return NULL;
    for (i = 0; i < str->Length / sizeof(WCHAR); i++)
        ret[i] = lower ? RtlDowncaseUnicodeChar(str->Buffer[i]) : str->Buffer[i];
    ret[i] = 0;
    *ret_len = i;
    return ret;
}

static SECURITY_STATUS kerberos_query_names(struct kerberos_cred *cred, ULONG attribute, void *buffer, BOOL ansi)
{
    char *name;
    int len;

    TRACE("(%p %u %p %d)\n", cred, attribute, buffer, ansi);

    if (attribute != SECPKG_CRED_ATTR_NAMES) return SEC_E_INVALID_TOKEN;

    if (!ansi)
    {
        len = (cred->name_len + 1) * sizeof(WCHAR);
        if (!(((SecPkgCredentials_NamesW *)buffer)->sUserName = HeapAlloc(GetProcessHeap(), 0, len)))
            return SEC_E_INSUFFICIENT_MEMORY;
        memcpy(((SecPkgCredentials_NamesW *)buffer)->sUserName, cred->name, len);
        return SEC_E_OK;
    }

    len = WideCharToMultiByte(CP_ACP, 0, cred->name, cred->name_len + 1, NULL, 0, NULL, NULL);
    if (!(name = HeapAlloc(GetProcessHeap(), 0, len))) return SEC_E_INSUFFICIENT_MEMORY;
    WideCharToMultiByte(CP_ACP, 0, cred->name, cred->name_len + 1, name, len, NULL, NULL);
    ((SecPkgCredentials_NamesA *)buffer)->sUserName = name;
    return SEC_E_OK;
}

static SECURITY_STATUS SEC_ENTRY kerberos_QueryCredentialsAttributesA(CredHandle *phCredential, ULONG ulAttribute, void *pBuffer)
{
    return kerberos_query_names((struct kerberos_cred *)phCredential->dwLower, ulAttribute, pBuffer, TRUE);
}

static SECURITY_STATUS SEC_ENTRY kerberos_QueryCredentialsAttributesW(CredHandle *phCredential, ULONG ulAttribute, void *pBuffer)
{
    return kerberos_query_names((struct kerberos_cred *)phCredential->dwLower, ulAttribute, pBuffer, FALSE);
}

static SECURITY_STATUS SEC_ENTRY kerberos_AcquireCredentialsHandleW(SEC_WCHAR *pszPrincipal, SEC_WCHAR *pszPackage, ULONG fCredentialUse,
        LUID *pLogonID, void *pAuthData, SEC_GET_KEY_FN pGetKeyFn, void *pGetKeyArgument, CredHandle *phCredential, TimeStamp *ptsExpiry)
{
    SEC_WINNT_AUTH_IDENTITY_W ex_view, *id = SECUR32_identityView(pAuthData, &ex_view);
    SECURITY_LOGON_SESSION_DATA *session = NULL;
    WCHAR *user = NULL, *domain = NULL;
    ULONG user_len = 0, domain_len = 0;
    struct kerberos_cred *cred = NULL;
    BOOL password = FALSE, tcb;
    LUID logon_id;
    SECURITY_STATUS ret;

    TRACE("(%s %s 0x%08x %p %p %p %p %p %p)\n", debugstr_w(pszPrincipal), debugstr_w(pszPackage), fCredentialUse,
          pLogonID, pAuthData, pGetKeyFn, pGetKeyArgument, phCredential, ptsExpiry);

    if (ptsExpiry)
    {
        ptsExpiry->LowPart = 0;
        ptsExpiry->HighPart = 0;
    }

    if (!(fCredentialUse & SECPKG_CRED_BOTH) ||
        (fCredentialUse & ~(SECPKG_CRED_BOTH | SECPKG_CRED_AUTOLOGON_RESTRICTED)))
        return SEC_E_INVALID_TOKEN;

    if (id && !(id->Flags & (SEC_WINNT_AUTH_IDENTITY_ANSI | SEC_WINNT_AUTH_IDENTITY_UNICODE)))
        return SEC_E_INVALID_TOKEN;

    if (!kerberos_get_caller(&logon_id, &tcb))
        return SEC_E_INTERNAL_ERROR;

    if (pLogonID && (pLogonID->LowPart || pLogonID->HighPart) &&
        (pLogonID->LowPart != logon_id.LowPart || pLogonID->HighPart != logon_id.HighPart))
    {
        if (!tcb) return SEC_E_NOT_OWNER;
        logon_id = *pLogonID;
        if (LsaGetLogonSessionData(&logon_id, &session) != STATUS_SUCCESS)
            return SEC_E_UNKNOWN_CREDENTIALS;
    }

    if (id)
    {
        if ((id->User && id->UserLength && !(user = kerberos_identity_string(id, id->User, id->UserLength, &user_len))) ||
            (id->Domain && id->DomainLength && !(domain = kerberos_identity_string(id, id->Domain, id->DomainLength, &domain_len))))
        {
            ret = SEC_E_INSUFFICIENT_MEMORY;
            goto done;
        }
        password = id->Password && id->PasswordLength;
    }

    if (!password && (user || domain))
    {
        ret = SEC_E_NO_CREDENTIALS;
        goto done;
    }

    if (ptsExpiry)
    {
        ptsExpiry->LowPart = 0xd5969fff;
        ptsExpiry->HighPart = 0x7fffff36;
    }

    if (!session && LsaGetLogonSessionData(&logon_id, &session) != STATUS_SUCCESS)
        session = NULL;

    if (!password && ((fCredentialUse & SECPKG_CRED_INBOUND) || !session ||
                      !kerberos_session_has_credentials(&logon_id, session)))
    {
        ret = SEC_E_NO_CREDENTIALS;
        goto done;
    }

    if (!user)
    {
        if (!session || !(user = kerberos_session_string(&session->UserName, kerberos_is_builtin_session(&logon_id), &user_len)))
        {
            ret = session ? SEC_E_INSUFFICIENT_MEMORY : SEC_E_NO_CREDENTIALS;
            goto done;
        }
    }
    if (!domain && !wcschr(user, '@') && session &&
        !(domain = kerberos_session_string(&session->LogonDomain, FALSE, &domain_len)))
    {
        ret = SEC_E_INSUFFICIENT_MEMORY;
        goto done;
    }

    if (!(cred = HeapAlloc(GetProcessHeap(), 0, sizeof(*cred))))
    {
        ret = SEC_E_INSUFFICIENT_MEMORY;
        goto done;
    }
    cred->name_len = user_len + (domain ? domain_len + 1 : 0);
    if (!(cred->name = HeapAlloc(GetProcessHeap(), 0, (cred->name_len + 1) * sizeof(WCHAR))))
    {
        HeapFree(GetProcessHeap(), 0, cred);
        ret = SEC_E_INSUFFICIENT_MEMORY;
        goto done;
    }
    memcpy(cred->name, user, user_len * sizeof(WCHAR));
    if (domain)
    {
        cred->name[user_len] = '@';
        memcpy(cred->name + user_len + 1, domain, domain_len * sizeof(WCHAR));
    }
    cred->name[cred->name_len] = 0;
    cred->use = fCredentialUse;
    cred->password = password;

    phCredential->dwUpper = fCredentialUse;
    phCredential->dwLower = (ULONG_PTR)cred;
    ret = SEC_E_OK;

done:
    if (session) LsaFreeReturnBuffer(session);
    HeapFree(GetProcessHeap(), 0, user);
    HeapFree(GetProcessHeap(), 0, domain);
    return ret;
}

static SECURITY_STATUS SEC_ENTRY kerberos_AcquireCredentialsHandleA(SEC_CHAR *pszPrincipal, SEC_CHAR *pszPackage, ULONG fCredentialUse,
        LUID *pLogonID, void *pAuthData, SEC_GET_KEY_FN pGetKeyFn, void *pGetKeyArgument, CredHandle *phCredential, TimeStamp *ptsExpiry)
{
    TRACE("(%s %s 0x%08x %p %p %p %p %p %p)\n", debugstr_a(pszPrincipal), debugstr_a(pszPackage), fCredentialUse,
          pLogonID, pAuthData, pGetKeyFn, pGetKeyArgument, phCredential, ptsExpiry);
    return kerberos_AcquireCredentialsHandleW(NULL, NULL, fCredentialUse, pLogonID, pAuthData, pGetKeyFn,
                                              pGetKeyArgument, phCredential, ptsExpiry);
}

#else
/***********************************************************************
 *              QueryCredentialsAttributesA
 */
static SECURITY_STATUS SEC_ENTRY kerberos_QueryCredentialsAttributesA(CredHandle *phCredential, ULONG ulAttribute, void *pBuffer)
{
    FIXME("(%p %d %p)\n", phCredential, ulAttribute, pBuffer);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              QueryCredentialsAttributesW
 */
static SECURITY_STATUS SEC_ENTRY kerberos_QueryCredentialsAttributesW(CredHandle *phCredential, ULONG ulAttribute, void *pBuffer)
{
    FIXME("(%p, %d, %p)\n", phCredential, ulAttribute, pBuffer);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              AcquireCredentialsHandleW
 */
static SECURITY_STATUS SEC_ENTRY kerberos_AcquireCredentialsHandleW(SEC_WCHAR *pszPrincipal, SEC_WCHAR *pszPackage, ULONG fCredentialUse,
        LUID *pLogonID, void *pAuthData, SEC_GET_KEY_FN pGetKeyFn, void *pGetKeyArgument, CredHandle *phCredential, TimeStamp *ptsExpiry)
{
    FIXME("(%s %s 0x%08x %p %p %p %p %p %p)\n", debugstr_w(pszPrincipal), debugstr_w(pszPackage), fCredentialUse,
          pLogonID, pAuthData, pGetKeyFn, pGetKeyArgument, phCredential, ptsExpiry);
    return SEC_E_NO_CREDENTIALS;
}

/***********************************************************************
 *              AcquireCredentialsHandleA
 */
static SECURITY_STATUS SEC_ENTRY kerberos_AcquireCredentialsHandleA(SEC_CHAR *pszPrincipal, SEC_CHAR *pszPackage, ULONG fCredentialUse,
        LUID *pLogonID, void *pAuthData, SEC_GET_KEY_FN pGetKeyFn, void *pGetKeyArgument, CredHandle *phCredential, TimeStamp *ptsExpiry)
{
    FIXME("(%s %s 0x%08x %p %p %p %p %p %p)\n", debugstr_a(pszPrincipal), debugstr_a(pszPackage), fCredentialUse,
          pLogonID, pAuthData, pGetKeyFn, pGetKeyArgument, phCredential, ptsExpiry);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

#endif
#ifdef __REACTOS__
static SECURITY_STATUS SEC_ENTRY kerberos_InitializeSecurityContextW(CredHandle *phCredential, CtxtHandle *phContext, SEC_WCHAR *pszTargetName,
        ULONG fContextReq, ULONG Reserved1, ULONG TargetDataRep, SecBufferDesc *pInput, ULONG Reserved2, CtxtHandle *phNewContext,
        SecBufferDesc *pOutput, ULONG *pfContextAttr, TimeStamp *ptsExpiry)
{
    struct kerberos_cred *cred = phCredential ? (struct kerberos_cred *)phCredential->dwLower : NULL;

    TRACE("(%p %p %s 0x%08x %d %d %p %d %p %p %p %p)\n", phCredential, phContext, debugstr_w(pszTargetName),
          fContextReq, Reserved1, TargetDataRep, pInput, Reserved2, phNewContext, pOutput, pfContextAttr, ptsExpiry);

    if (pfContextAttr) *pfContextAttr = 0;
    if (ptsExpiry)
    {
        ptsExpiry->LowPart = 0;
        ptsExpiry->HighPart = 0;
    }

    if (!cred) return SEC_E_INVALID_HANDLE;
    if (!(cred->use & SECPKG_CRED_OUTBOUND)) return SEC_E_UNSUPPORTED_FUNCTION;
    if (!cred->password) return SEC_E_NO_CREDENTIALS;

    FIXME("no KDC exchange for %s to %s\n", debugstr_w(cred->name), debugstr_w(pszTargetName));
    return SEC_E_NO_AUTHENTICATING_AUTHORITY;
}

static SECURITY_STATUS SEC_ENTRY kerberos_InitializeSecurityContextA(CredHandle *phCredential, CtxtHandle *phContext, SEC_CHAR *pszTargetName,
        ULONG fContextReq, ULONG Reserved1, ULONG TargetDataRep, SecBufferDesc *pInput, ULONG Reserved2, CtxtHandle *phNewContext,
        SecBufferDesc *pOutput, ULONG *pfContextAttr, TimeStamp *ptsExpiry)
{
    TRACE("%p %p %s %d %d %d %p %d %p %p %p %p\n", phCredential, phContext, debugstr_a(pszTargetName), fContextReq,
          Reserved1, TargetDataRep, pInput, Reserved2, phNewContext, pOutput, pfContextAttr, ptsExpiry);
    return kerberos_InitializeSecurityContextW(phCredential, phContext, NULL, fContextReq, Reserved1, TargetDataRep,
                                               pInput, Reserved2, phNewContext, pOutput, pfContextAttr, ptsExpiry);
}

#else
/***********************************************************************
 *              InitializeSecurityContextW
 */
static SECURITY_STATUS SEC_ENTRY kerberos_InitializeSecurityContextW(CredHandle *phCredential, CtxtHandle *phContext, SEC_WCHAR *pszTargetName,
        ULONG fContextReq, ULONG Reserved1, ULONG TargetDataRep, SecBufferDesc *pInput, ULONG Reserved2, CtxtHandle *phNewContext,
        SecBufferDesc *pOutput, ULONG *pfContextAttr, TimeStamp *ptsExpiry)
{
    FIXME("(%p %p %s 0x%08x %d %d %p %d %p %p %p %p)\n", phCredential, phContext, debugstr_w(pszTargetName),
          fContextReq, Reserved1, TargetDataRep, pInput, Reserved1, phNewContext, pOutput, pfContextAttr, ptsExpiry);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              InitializeSecurityContextA
 */
static SECURITY_STATUS SEC_ENTRY kerberos_InitializeSecurityContextA(CredHandle *phCredential, CtxtHandle *phContext, SEC_CHAR *pszTargetName,
        ULONG fContextReq, ULONG Reserved1, ULONG TargetDataRep, SecBufferDesc *pInput, ULONG Reserved2, CtxtHandle *phNewContext,
        SecBufferDesc *pOutput, ULONG *pfContextAttr, TimeStamp *ptsExpiry)
{
    FIXME("%p %p %s %d %d %d %p %d %p %p %p %p\n", phCredential, phContext, debugstr_a(pszTargetName), fContextReq,
          Reserved1, TargetDataRep, pInput, Reserved1, phNewContext, pOutput, pfContextAttr, ptsExpiry);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

#endif
/***********************************************************************
 *              AcceptSecurityContext
 */
static SECURITY_STATUS SEC_ENTRY kerberos_AcceptSecurityContext(CredHandle *phCredential, CtxtHandle *phContext, SecBufferDesc *pInput,
        ULONG fContextReq, ULONG TargetDataRep, CtxtHandle *phNewContext, SecBufferDesc *pOutput, ULONG *pfContextAttr, TimeStamp *ptsExpiry)
{
    FIXME("(%p %p %p %d %d %p %p %p %p)\n", phCredential, phContext, pInput, fContextReq, TargetDataRep, phNewContext, pOutput,
          pfContextAttr, ptsExpiry);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              CompleteAuthToken
 */
static SECURITY_STATUS SEC_ENTRY kerberos_CompleteAuthToken(CtxtHandle *phContext, SecBufferDesc *pToken)
{
    FIXME("(%p %p)\n", phContext, pToken);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              DeleteSecurityContext
 */
static SECURITY_STATUS SEC_ENTRY kerberos_DeleteSecurityContext(CtxtHandle *phContext)
{
    FIXME("(%p)\n", phContext);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              QueryContextAttributesW
 */
static SECURITY_STATUS SEC_ENTRY kerberos_QueryContextAttributesW(CtxtHandle *phContext, ULONG ulAttribute, void *pBuffer)
{
    FIXME("(%p %d %p)\n", phContext, ulAttribute, pBuffer);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              QueryContextAttributesA
 */
static SECURITY_STATUS SEC_ENTRY kerberos_QueryContextAttributesA(CtxtHandle *phContext, ULONG ulAttribute, void *pBuffer)
{
    FIXME("(%p %d %p)\n", phContext, ulAttribute, pBuffer);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              ImpersonateSecurityContext
 */
static SECURITY_STATUS SEC_ENTRY kerberos_ImpersonateSecurityContext(CtxtHandle *phContext)
{
    FIXME("(%p)\n", phContext);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              RevertSecurityContext
 */
static SECURITY_STATUS SEC_ENTRY kerberos_RevertSecurityContext(CtxtHandle *phContext)
{
    FIXME("(%p)\n", phContext);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              MakeSignature
 */
static SECURITY_STATUS SEC_ENTRY kerberos_MakeSignature(CtxtHandle *phContext, ULONG fQOP, SecBufferDesc *pMessage, ULONG MessageSeqNo)
{
    FIXME("(%p %d %p %d)\n", phContext, fQOP, pMessage, MessageSeqNo);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              VerifySignature
 */
static SECURITY_STATUS SEC_ENTRY kerberos_VerifySignature(CtxtHandle *phContext, SecBufferDesc *pMessage, ULONG MessageSeqNo, PULONG pfQOP)
{
    FIXME("(%p %p %d %p)\n", phContext, pMessage, MessageSeqNo, pfQOP);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

#ifdef __REACTOS__
static SECURITY_STATUS SEC_ENTRY kerberos_FreeCredentialsHandle(PCredHandle phCredential)
{
    struct kerberos_cred *cred;

    TRACE("(%p)\n", phCredential);

    if (!phCredential) return SEC_E_INVALID_HANDLE;
    cred = (struct kerberos_cred *)phCredential->dwLower;
    if (cred)
    {
        HeapFree(GetProcessHeap(), 0, cred->name);
        HeapFree(GetProcessHeap(), 0, cred);
    }
    phCredential->dwUpper = 0;
    phCredential->dwLower = 0;
    return SEC_E_OK;
}

#else
/***********************************************************************
 *             FreeCredentialsHandle
 */
static SECURITY_STATUS SEC_ENTRY kerberos_FreeCredentialsHandle(PCredHandle phCredential)
{
    FIXME("(%p)\n", phCredential);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

#endif
/***********************************************************************
 *             EncryptMessage
 */
static SECURITY_STATUS SEC_ENTRY kerberos_EncryptMessage(CtxtHandle *phContext, ULONG fQOP, SecBufferDesc *pMessage, ULONG MessageSeqNo)
{
    FIXME("(%p %d %p %d)\n", phContext, fQOP, pMessage, MessageSeqNo);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *             DecryptMessage
 */
static SECURITY_STATUS SEC_ENTRY kerberos_DecryptMessage(CtxtHandle *phContext, SecBufferDesc *pMessage, ULONG MessageSeqNo, PULONG pfQOP)
{
    FIXME("(%p %p %d %p)\n", phContext, pMessage, MessageSeqNo, pfQOP);
    return SEC_E_UNSUPPORTED_FUNCTION;
}

static const SecurityFunctionTableA kerberosTableA = {
    1,
    NULL,   /* EnumerateSecurityPackagesA */
    kerberos_QueryCredentialsAttributesA,   /* QueryCredentialsAttributesA */
    kerberos_AcquireCredentialsHandleA,     /* AcquireCredentialsHandleA */
    kerberos_FreeCredentialsHandle,         /* FreeCredentialsHandle */
    NULL,   /* Reserved2 */
    kerberos_InitializeSecurityContextA,    /* InitializeSecurityContextA */
    kerberos_AcceptSecurityContext,         /* AcceptSecurityContext */
    kerberos_CompleteAuthToken,             /* CompleteAuthToken */
    kerberos_DeleteSecurityContext,         /* DeleteSecurityContext */
    NULL,  /* ApplyControlToken */
    kerberos_QueryContextAttributesA,       /* QueryContextAttributesA */
    kerberos_ImpersonateSecurityContext,    /* ImpersonateSecurityContext */
    kerberos_RevertSecurityContext,         /* RevertSecurityContext */
    kerberos_MakeSignature,                 /* MakeSignature */
    kerberos_VerifySignature,               /* VerifySignature */
    FreeContextBuffer,                      /* FreeContextBuffer */
    NULL,   /* QuerySecurityPackageInfoA */
    NULL,   /* Reserved3 */
    NULL,   /* Reserved4 */
    NULL,   /* ExportSecurityContext */
    NULL,   /* ImportSecurityContextA */
    NULL,   /* AddCredentialsA */
    NULL,   /* Reserved8 */
    NULL,   /* QuerySecurityContextToken */
    kerberos_EncryptMessage,                /* EncryptMessage */
    kerberos_DecryptMessage,                /* DecryptMessage */
    NULL,   /* SetContextAttributesA */
};

static const SecurityFunctionTableW kerberosTableW = {
    1,
    NULL,   /* EnumerateSecurityPackagesW */
    kerberos_QueryCredentialsAttributesW,   /* QueryCredentialsAttributesW */
    kerberos_AcquireCredentialsHandleW,     /* AcquireCredentialsHandleW */
    kerberos_FreeCredentialsHandle,         /* FreeCredentialsHandle */
    NULL,   /* Reserved2 */
    kerberos_InitializeSecurityContextW,    /* InitializeSecurityContextW */
    kerberos_AcceptSecurityContext,         /* AcceptSecurityContext */
    kerberos_CompleteAuthToken,             /* CompleteAuthToken */
    kerberos_DeleteSecurityContext,         /* DeleteSecurityContext */
    NULL,  /* ApplyControlToken */
    kerberos_QueryContextAttributesW,       /* QueryContextAttributesW */
    kerberos_ImpersonateSecurityContext,    /* ImpersonateSecurityContext */
    kerberos_RevertSecurityContext,         /* RevertSecurityContext */
    kerberos_MakeSignature,                 /* MakeSignature */
    kerberos_VerifySignature,               /* VerifySignature */
    FreeContextBuffer,                      /* FreeContextBuffer */
    NULL,   /* QuerySecurityPackageInfoW */
    NULL,   /* Reserved3 */
    NULL,   /* Reserved4 */
    NULL,   /* ExportSecurityContext */
    NULL,   /* ImportSecurityContextW */
    NULL,   /* AddCredentialsW */
    NULL,   /* Reserved8 */
    NULL,   /* QuerySecurityContextToken */
    kerberos_EncryptMessage,                /* EncryptMessage */
    kerberos_DecryptMessage,                /* DecryptMessage */
    NULL,   /* SetContextAttributesW */
};

#define KERBEROS_COMMENT \
    {'M','i','c','r','o','s','o','f','t',' ','K','e','r','b','e','r','o','s',' ','V','1','.','0',0}
static CHAR kerberos_comment_A[] = KERBEROS_COMMENT;
static WCHAR kerberos_comment_W[] = KERBEROS_COMMENT;

#define KERBEROS_NAME {'K','e','r','b','e','r','o','s',0}
static char kerberos_name_A[] = KERBEROS_NAME;
static WCHAR kerberos_name_W[] = KERBEROS_NAME;

#define CAPS \
    ( SECPKG_FLAG_INTEGRITY \
    | SECPKG_FLAG_PRIVACY \
    | SECPKG_FLAG_TOKEN_ONLY \
    | SECPKG_FLAG_DATAGRAM \
    | SECPKG_FLAG_CONNECTION \
    | SECPKG_FLAG_MULTI_REQUIRED \
    | SECPKG_FLAG_EXTENDED_ERROR \
    | SECPKG_FLAG_IMPERSONATION \
    | SECPKG_FLAG_ACCEPT_WIN32_NAME \
    | SECPKG_FLAG_NEGOTIABLE \
    | SECPKG_FLAG_GSS_COMPATIBLE \
    | SECPKG_FLAG_LOGON \
    | SECPKG_FLAG_MUTUAL_AUTH \
    | SECPKG_FLAG_DELEGATION \
    | SECPKG_FLAG_READONLY_WITH_CHECKSUM \
    | SECPKG_FLAG_RESTRICTED_TOKENS \
    | SECPKG_FLAG_APPCONTAINER_CHECKS)

static const SecPkgInfoW infoW = {
    CAPS,
    1,
    RPC_C_AUTHN_GSS_KERBEROS,
    KERBEROS_MAX_BUF,
    kerberos_name_W,
    kerberos_comment_W
};

static const SecPkgInfoA infoA = {
    CAPS,
    1,
    RPC_C_AUTHN_GSS_KERBEROS,
    KERBEROS_MAX_BUF,
    kerberos_name_A,
    kerberos_comment_A
};

void SECUR32_initKerberosSP(void)
{
    SecureProvider *provider = SECUR32_addProvider(&kerberosTableA, &kerberosTableW, NULL);
    SECUR32_addPackages(provider, 1, &infoA, &infoW);
}
