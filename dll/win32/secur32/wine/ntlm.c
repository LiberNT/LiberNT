/*
 * Copyright 2005, 2006 Kai Blin
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
 *
 * This file implements the NTLM security provider.
 */

#include "precomp.h"

#include <assert.h>
#include <stdio.h>
#include <wincred.h>
#include <lm.h>

#include "hmac_md5.h"

#include <wine/unicode.h>
#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(ntlm);

#define NTLM_MAX_BUF 2888

/***********************************************************************
 *              QueryCredentialsAttributesA
 */
static SECURITY_STATUS SEC_ENTRY ntlm_QueryCredentialsAttributesA(
        PCredHandle phCredential, ULONG ulAttribute, PVOID pBuffer)
{
    SECURITY_STATUS ret;

    TRACE("(%p, %d, %p)\n", phCredential, ulAttribute, pBuffer);

    if(ulAttribute == SECPKG_ATTR_NAMES)
    {
        FIXME("SECPKG_CRED_ATTR_NAMES: stub\n");
        ret = SEC_E_UNSUPPORTED_FUNCTION;
    }
    else
        ret = SEC_E_UNSUPPORTED_FUNCTION;
    
    return ret;
}

/***********************************************************************
 *              QueryCredentialsAttributesW
 */
static SECURITY_STATUS SEC_ENTRY ntlm_QueryCredentialsAttributesW(
        PCredHandle phCredential, ULONG ulAttribute, PVOID pBuffer)
{
    SECURITY_STATUS ret;

    TRACE("(%p, %d, %p)\n", phCredential, ulAttribute, pBuffer);

    if(ulAttribute == SECPKG_ATTR_NAMES)
    {
        FIXME("SECPKG_CRED_ATTR_NAMES: stub\n");
        ret = SEC_E_UNSUPPORTED_FUNCTION;
    }
    else
        ret = SEC_E_UNSUPPORTED_FUNCTION;
    
    return ret;
}

static BOOL ntlm_DupString(WCHAR **dst, ULONG *dst_len, const WCHAR *src, ULONG len)
{
    *dst = HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR));
    if (!*dst) return FALSE;
    if (len) memcpy(*dst, src, len * sizeof(WCHAR));
    (*dst)[len] = 0;
    *dst_len = len;
    return TRUE;
}

static void ntlm_FreeCredentials(PNtlmCredentials ntlm_cred)
{
    if (!ntlm_cred) return;
    if (ntlm_cred->password)
        memset(ntlm_cred->password, 0, ntlm_cred->password_len * sizeof(WCHAR));
    HeapFree(GetProcessHeap(), 0, ntlm_cred->password);
    HeapFree(GetProcessHeap(), 0, ntlm_cred->user);
    HeapFree(GetProcessHeap(), 0, ntlm_cred->domain);
    HeapFree(GetProcessHeap(), 0, ntlm_cred);
}

/***********************************************************************
 *              AcquireCredentialsHandleW
 */
SECURITY_STATUS SEC_ENTRY ntlm_AcquireCredentialsHandleW(
 SEC_WCHAR *pszPrincipal, SEC_WCHAR *pszPackage, ULONG fCredentialUse,
 PLUID pLogonID, PVOID pAuthData, SEC_GET_KEY_FN pGetKeyFn,
 PVOID pGetKeyArgument, PCredHandle phCredential, PTimeStamp ptsExpiry)
{
    SECURITY_STATUS ret;
    PNtlmCredentials ntlm_cred = NULL;
    LPWSTR domain = NULL, user = NULL, password = NULL;
    PSEC_WINNT_AUTH_IDENTITY_W auth_data = NULL;

    TRACE("(%s, %s, 0x%08x, %p, %p, %p, %p, %p, %p)\n",
     debugstr_w(pszPrincipal), debugstr_w(pszPackage), fCredentialUse,
     pLogonID, pAuthData, pGetKeyFn, pGetKeyArgument, phCredential, ptsExpiry);

    switch(fCredentialUse)
    {
        case SECPKG_CRED_INBOUND:
            ntlm_cred = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ntlm_cred));
            if (!ntlm_cred)
                ret = SEC_E_INSUFFICIENT_MEMORY;
            else
            {
                ntlm_cred->use = fCredentialUse;

                phCredential->dwUpper = fCredentialUse;
                phCredential->dwLower = (ULONG_PTR)ntlm_cred;
                ret = SEC_E_OK;
            }
            break;
        case SECPKG_CRED_OUTBOUND:
        case SECPKG_CRED_BOTH:
            {
                auth_data = pAuthData;
                ntlm_cred = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ntlm_cred));
                if (!ntlm_cred)
                {
                    ret = SEC_E_INSUFFICIENT_MEMORY;
                    break;
                }
                ntlm_cred->use = fCredentialUse;
                ntlm_cred->default_credentials = (pAuthData == NULL);

                if(pAuthData != NULL)
                {
                    int domain_len = 0, user_len = 0, password_len = 0;

                    if (auth_data->Flags & SEC_WINNT_AUTH_IDENTITY_ANSI)
                    {
                        if (auth_data->DomainLength)
                        {
                            domain_len = MultiByteToWideChar(CP_ACP, 0, (char *)auth_data->Domain,
                                                             auth_data->DomainLength, NULL, 0);
                            domain = HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR) * domain_len);
                            if(!domain)
                            {
                                ret = SEC_E_INSUFFICIENT_MEMORY;
                                break;
                            }
                            MultiByteToWideChar(CP_ACP, 0, (char *)auth_data->Domain, auth_data->DomainLength,
                                                domain, domain_len);
                        }

                        if (auth_data->UserLength)
                        {
                            user_len = MultiByteToWideChar(CP_ACP, 0, (char *)auth_data->User,
                                                           auth_data->UserLength, NULL, 0);
                            user = HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR) * user_len);
                            if(!user)
                            {
                                ret = SEC_E_INSUFFICIENT_MEMORY;
                                break;
                            }
                            MultiByteToWideChar(CP_ACP, 0, (char *)auth_data->User, auth_data->UserLength,
                                                user, user_len);
                        }

                        if (auth_data->PasswordLength)
                        {
                            password_len = MultiByteToWideChar(CP_ACP, 0,(char *)auth_data->Password,
                                                               auth_data->PasswordLength, NULL, 0);
                            password = HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR) * password_len);
                            if(!password)
                            {
                                ret = SEC_E_INSUFFICIENT_MEMORY;
                                break;
                            }
                            MultiByteToWideChar(CP_ACP, 0, (char *)auth_data->Password, auth_data->PasswordLength,
                                                password, password_len);
                        }
                    }
                    else
                    {
                        domain = auth_data->Domain;
                        domain_len = auth_data->DomainLength;

                        user = auth_data->User;
                        user_len = auth_data->UserLength;

                        password = auth_data->Password;
                        password_len = auth_data->PasswordLength;
                    }

                    TRACE("Username is %s\n", debugstr_wn(user, user_len));
                    TRACE("Domain name is %s\n", debugstr_wn(domain, domain_len));

                    if (!ntlm_DupString(&ntlm_cred->user, &ntlm_cred->user_len, user, user_len) ||
                        !ntlm_DupString(&ntlm_cred->domain, &ntlm_cred->domain_len, domain, domain_len) ||
                        !ntlm_DupString(&ntlm_cred->password, &ntlm_cred->password_len, password, password_len))
                    {
                        ret = SEC_E_INSUFFICIENT_MEMORY;
                        break;
                    }
                    ntlm_cred->have_credentials = (user_len != 0 || password_len != 0);
                }

                phCredential->dwUpper = fCredentialUse;
                phCredential->dwLower = (ULONG_PTR)ntlm_cred;
                TRACE("ACH phCredential->dwUpper: 0x%08lx, dwLower: 0x%08lx\n",
                      phCredential->dwUpper, phCredential->dwLower);
                ret = SEC_E_OK;
                break;
            }
        default:
            phCredential = NULL;
            ret = SEC_E_UNKNOWN_CREDENTIALS;
    }

    if (auth_data && auth_data->Flags & SEC_WINNT_AUTH_IDENTITY_ANSI)
    {
        HeapFree(GetProcessHeap(), 0, domain);
        HeapFree(GetProcessHeap(), 0, user);
        HeapFree(GetProcessHeap(), 0, password);
    }
    if (ret != SEC_E_OK)
        ntlm_FreeCredentials(ntlm_cred);
    return ret;
}

/***********************************************************************
 *              AcquireCredentialsHandleA
 */
static SECURITY_STATUS SEC_ENTRY ntlm_AcquireCredentialsHandleA(
 SEC_CHAR *pszPrincipal, SEC_CHAR *pszPackage, ULONG fCredentialUse,
 PLUID pLogonID, PVOID pAuthData, SEC_GET_KEY_FN pGetKeyFn,
 PVOID pGetKeyArgument, PCredHandle phCredential, PTimeStamp ptsExpiry)
{
    SECURITY_STATUS ret;
    int user_sizeW, domain_sizeW, passwd_sizeW;
    
    SEC_WCHAR *user = NULL, *domain = NULL, *passwd = NULL, *package = NULL;
    
    PSEC_WINNT_AUTH_IDENTITY_W pAuthDataW = NULL;
    PSEC_WINNT_AUTH_IDENTITY_A identity  = NULL;

    TRACE("(%s, %s, 0x%08x, %p, %p, %p, %p, %p, %p)\n",
     debugstr_a(pszPrincipal), debugstr_a(pszPackage), fCredentialUse,
     pLogonID, pAuthData, pGetKeyFn, pGetKeyArgument, phCredential, ptsExpiry);
    
    if(pszPackage != NULL)
    {
        int package_sizeW = MultiByteToWideChar(CP_ACP, 0, pszPackage, -1,
                NULL, 0);

        package = HeapAlloc(GetProcessHeap(), 0, package_sizeW * 
                sizeof(SEC_WCHAR));
        MultiByteToWideChar(CP_ACP, 0, pszPackage, -1, package, package_sizeW);
    }

    
    if(pAuthData != NULL)
    {
        identity = pAuthData;

        if(identity->Flags == SEC_WINNT_AUTH_IDENTITY_ANSI)
        {
            pAuthDataW = HeapAlloc(GetProcessHeap(), 0, 
                    sizeof(SEC_WINNT_AUTH_IDENTITY_W));

            if(identity->UserLength != 0)
            {
                user_sizeW = MultiByteToWideChar(CP_ACP, 0, 
                    (LPCSTR)identity->User, identity->UserLength, NULL, 0);
                user = HeapAlloc(GetProcessHeap(), 0, user_sizeW * 
                        sizeof(SEC_WCHAR));
                MultiByteToWideChar(CP_ACP, 0, (LPCSTR)identity->User, 
                    identity->UserLength, user, user_sizeW);
            }
            else
            {
                user_sizeW = 0;
            }
             
            if(identity->DomainLength != 0)
            {
                domain_sizeW = MultiByteToWideChar(CP_ACP, 0, 
                    (LPCSTR)identity->Domain, identity->DomainLength, NULL, 0);
                domain = HeapAlloc(GetProcessHeap(), 0, domain_sizeW 
                    * sizeof(SEC_WCHAR));
                MultiByteToWideChar(CP_ACP, 0, (LPCSTR)identity->Domain, 
                    identity->DomainLength, domain, domain_sizeW);
            }
            else
            {
                domain_sizeW = 0;
            }

            if(identity->PasswordLength != 0)
            {
                passwd_sizeW = MultiByteToWideChar(CP_ACP, 0, 
                    (LPCSTR)identity->Password, identity->PasswordLength,
                    NULL, 0);
                passwd = HeapAlloc(GetProcessHeap(), 0, passwd_sizeW
                    * sizeof(SEC_WCHAR));
                MultiByteToWideChar(CP_ACP, 0, (LPCSTR)identity->Password,
                    identity->PasswordLength, passwd, passwd_sizeW);
            }
            else
            {
                passwd_sizeW = 0;
            }
            
            pAuthDataW->Flags = SEC_WINNT_AUTH_IDENTITY_UNICODE;
            pAuthDataW->User = user;
            pAuthDataW->UserLength = user_sizeW;
            pAuthDataW->Domain = domain;
            pAuthDataW->DomainLength = domain_sizeW;
            pAuthDataW->Password = passwd;
            pAuthDataW->PasswordLength = passwd_sizeW;
        }
        else
        {
            pAuthDataW = (PSEC_WINNT_AUTH_IDENTITY_W)identity;
        }
    }       
    
    ret = ntlm_AcquireCredentialsHandleW(NULL, package, fCredentialUse, 
            pLogonID, pAuthDataW, pGetKeyFn, pGetKeyArgument, phCredential,
            ptsExpiry);
    
    HeapFree(GetProcessHeap(), 0, package);
    HeapFree(GetProcessHeap(), 0, user);
    HeapFree(GetProcessHeap(), 0, domain);
    HeapFree(GetProcessHeap(), 0, passwd);
    if(pAuthDataW != (PSEC_WINNT_AUTH_IDENTITY_W)identity)
        HeapFree(GetProcessHeap(), 0, pAuthDataW);
    
    return ret;
}

/*************************************************************************
 *             ntlm_GetTokenBufferIndex
 * Calculates the index of the secbuffer with BufferType == SECBUFFER_TOKEN
 * Returns index if found or -1 if not found.
 */
static int ntlm_GetTokenBufferIndex(PSecBufferDesc pMessage)
{
    UINT i;

    TRACE("%p\n", pMessage);

    for( i = 0; i < pMessage->cBuffers; ++i )
    {
        if(pMessage->pBuffers[i].BufferType == SECBUFFER_TOKEN)
            return i;
    }

    return -1;
}

/*************************************************************************
 *             ntlm_GetDataBufferIndex
 * Calculates the index of the first secbuffer with BufferType == SECBUFFER_DATA
 * Returns index if found or -1 if not found.
 */
static int ntlm_GetDataBufferIndex(PSecBufferDesc pMessage)
{
    UINT i;

    TRACE("%p\n", pMessage);

    for( i = 0; i < pMessage->cBuffers; ++i )
    {
        if(pMessage->pBuffers[i].BufferType == SECBUFFER_DATA)
            return i;
    }

    return -1;
}

static BOOL ntlm_GetCachedCredential(const SEC_WCHAR *pszTargetName, PCREDENTIALW *cred)
{
    LPCWSTR p;
    LPCWSTR pszHost;
    LPWSTR pszHostOnly;
    BOOL ret;

    if (!pszTargetName)
        return FALSE;

    /* try to get the start of the hostname from service principal name (SPN) */
    pszHost = strchrW(pszTargetName, '/');
    if (pszHost)
    {
        /* skip slash character */
        pszHost++;

        /* find end of host by detecting start of instance port or start of referrer */
        p = strchrW(pszHost, ':');
        if (!p)
            p = strchrW(pszHost, '/');
        if (!p)
            p = pszHost + strlenW(pszHost);
    }
    else /* otherwise not an SPN, just a host */
    {
        pszHost = pszTargetName;
        p = pszHost + strlenW(pszHost);
    }

    pszHostOnly = HeapAlloc(GetProcessHeap(), 0, (p - pszHost + 1) * sizeof(WCHAR));
    if (!pszHostOnly)
        return FALSE;

    memcpy(pszHostOnly, pszHost, (p - pszHost) * sizeof(WCHAR));
    pszHostOnly[p - pszHost] = '\0';

    ret = CredReadW(pszHostOnly, CRED_TYPE_DOMAIN_PASSWORD, 0, cred);

    HeapFree(GetProcessHeap(), 0, pszHostOnly);
    return ret;
}

static SECURITY_STATUS ntlm_SetContextCredentials(PNegoHelper helper, PNtlmCredentials ntlm_cred,
        const SEC_WCHAR *pszTargetName)
{
    PCREDENTIALW cred;
    LPCWSTR user, domain = NULL;
    ULONG domain_len = 0;
    BOOL ok;

    if (ntlm_cred->default_credentials)
    {
        if (!ntlm_GetCachedCredential(pszTargetName, &cred))
            return SEC_E_OK;

        if (!cred->UserName)
        {
            CredFree(cred);
            return SEC_E_OK;
        }

        user = strchrW(cred->UserName, '\\');
        if (user)
        {
            domain = cred->UserName;
            domain_len = user - cred->UserName;
            user++;
        }
        else
            user = cred->UserName;

        ok = ntlm_DupString(&helper->user, &helper->user_len, user, strlenW(user)) &&
             ntlm_DupString(&helper->domain, &helper->domain_len, domain, domain_len) &&
             ntlm_DupString(&helper->password, &helper->password_len, (LPCWSTR)cred->CredentialBlob,
                            cred->CredentialBlobSize / sizeof(WCHAR));
        CredFree(cred);
        if (!ok)
            return SEC_E_INSUFFICIENT_MEMORY;

        helper->have_credentials = TRUE;
        return SEC_E_OK;
    }

    if (!ntlm_cred->have_credentials)
        return SEC_E_OK;

    if (!ntlm_DupString(&helper->user, &helper->user_len, ntlm_cred->user, ntlm_cred->user_len) ||
        !ntlm_DupString(&helper->domain, &helper->domain_len, ntlm_cred->domain, ntlm_cred->domain_len) ||
        !ntlm_DupString(&helper->password, &helper->password_len, ntlm_cred->password, ntlm_cred->password_len))
        return SEC_E_INSUFFICIENT_MEMORY;

    helper->have_credentials = TRUE;
    return SEC_E_OK;
}

static ULONG ntlm_GetContextAttributes(PNegoHelper helper)
{
    ULONG attr = 0;

    if (helper->mode == NTLM_CLIENT)
    {
        if (helper->neg_flags & NTLMSSP_NEGOTIATE_SIGN)
            attr |= ISC_RET_INTEGRITY | ISC_RET_REPLAY_DETECT | ISC_RET_SEQUENCE_DETECT;
        if (helper->neg_flags & NTLMSSP_NEGOTIATE_SEAL)
            attr |= ISC_RET_CONFIDENTIALITY;
        if (helper->neg_flags & NTLMSSP_NEGOTIATE_ANONYMOUS)
            attr |= ISC_RET_NULL_SESSION;
    }
    else
    {
        if (helper->neg_flags & NTLMSSP_NEGOTIATE_SIGN)
            attr |= ASC_RET_INTEGRITY | ASC_RET_REPLAY_DETECT | ASC_RET_SEQUENCE_DETECT;
        if (helper->neg_flags & NTLMSSP_NEGOTIATE_SEAL)
            attr |= ASC_RET_CONFIDENTIALITY;
        if (helper->neg_flags & NTLMSSP_NEGOTIATE_ANONYMOUS)
            attr |= ASC_RET_NULL_SESSION;
    }
    return attr;
}

static void ntlm_FreeSessionSecurity(PNegoHelper helper)
{
    SECUR32_arc4Cleanup(helper->crypt.ntlm.a4i);
    SECUR32_arc4Cleanup(helper->crypt.ntlm2.send_a4i);
    SECUR32_arc4Cleanup(helper->crypt.ntlm2.recv_a4i);
    HeapFree(GetProcessHeap(), 0, helper->crypt.ntlm2.send_sign_key);
    HeapFree(GetProcessHeap(), 0, helper->crypt.ntlm2.send_seal_key);
    HeapFree(GetProcessHeap(), 0, helper->crypt.ntlm2.recv_sign_key);
    HeapFree(GetProcessHeap(), 0, helper->crypt.ntlm2.recv_seal_key);
    memset(&helper->crypt, 0, sizeof(helper->crypt));
}

static SECURITY_STATUS ntlm_InitSessionSecurity(PNegoHelper helper)
{
    ntlm_FreeSessionSecurity(helper);

    helper->crypt.ntlm.a4i = SECUR32_arc4Alloc();
    helper->crypt.ntlm2.send_a4i = SECUR32_arc4Alloc();
    helper->crypt.ntlm2.recv_a4i = SECUR32_arc4Alloc();
    if (!helper->crypt.ntlm.a4i || !helper->crypt.ntlm2.send_a4i || !helper->crypt.ntlm2.recv_a4i ||
        SECUR32_CreateNTLM2SubKeys(helper) != SEC_E_OK)
    {
        ntlm_FreeSessionSecurity(helper);
        return SEC_E_INSUFFICIENT_MEMORY;
    }

    SECUR32_arc4Init(helper->crypt.ntlm.a4i, helper->session_key, 16);
    helper->crypt.ntlm.seq_num = 0l;
    SECUR32_arc4Init(helper->crypt.ntlm2.send_a4i,
                     helper->crypt.ntlm2.send_seal_key, 16);
    SECUR32_arc4Init(helper->crypt.ntlm2.recv_a4i,
                     helper->crypt.ntlm2.recv_seal_key, 16);
    helper->crypt.ntlm2.send_seq_no = 0l;
    helper->crypt.ntlm2.recv_seq_no = 0l;
    return SEC_E_OK;
}

/***********************************************************************
 *              InitializeSecurityContextW
 */
SECURITY_STATUS SEC_ENTRY ntlm_InitializeSecurityContextW(
 PCredHandle phCredential, PCtxtHandle phContext, SEC_WCHAR *pszTargetName, 
 ULONG fContextReq, ULONG Reserved1, ULONG TargetDataRep, 
 PSecBufferDesc pInput, ULONG Reserved2, PCtxtHandle phNewContext, 
 PSecBufferDesc pOutput, ULONG *pfContextAttr, PTimeStamp ptsExpiry)
{
    SECURITY_STATUS ret;
    PNtlmCredentials ntlm_cred;
    PNegoHelper helper = NULL;
    ULONG ctxt_attr = 0;
    PBYTE bin;
    ULONG bin_len = 0;
    int token_idx;

    TRACE("%p %p %s 0x%08x %d %d %p %d %p %p %p %p\n", phCredential, phContext,
     debugstr_w(pszTargetName), fContextReq, Reserved1, TargetDataRep, pInput,
     Reserved1, phNewContext, pOutput, pfContextAttr, ptsExpiry);

    if(TargetDataRep == SECURITY_NETWORK_DREP){
        TRACE("Setting SECURITY_NETWORK_DREP\n");
    }

    bin = HeapAlloc(GetProcessHeap(), 0, sizeof(BYTE) * NTLM_MAX_BUF);
    if (!bin)
        return SEC_E_INSUFFICIENT_MEMORY;

    if((phContext == NULL) && (pInput == NULL))
    {
        TRACE("First time in ISC()\n");

        if(!phCredential)
        {
            ret = SEC_E_INVALID_HANDLE;
            goto isc_end;
        }

        /* As the server side of sspi never calls this, make sure that
         * the handler is a client handler.
         */
        ntlm_cred = (PNtlmCredentials)phCredential->dwLower;
        if(!(ntlm_cred->use & SECPKG_CRED_OUTBOUND))
        {
            TRACE("Cred mode = %d\n", ntlm_cred->use);
            ret = SEC_E_INVALID_HANDLE;
            goto isc_end;
        }

        helper = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*helper));
        if (!helper)
        {
            ret = SEC_E_INSUFFICIENT_MEMORY;
            goto isc_end;
        }
        helper->mode = NTLM_CLIENT;

        if((ret = ntlm_SetContextCredentials(helper, ntlm_cred, pszTargetName)) != SEC_E_OK)
        {
            NtlmFreeContext(helper);
            goto isc_end;
        }

        if(fContextReq & ISC_REQ_CONFIDENTIALITY)
            helper->want_flags |= NTLMSSP_NEGOTIATE_SEAL | NTLMSSP_NEGOTIATE_SIGN |
                                  NTLMSSP_NEGOTIATE_KEY_EXCHANGE;
        if(fContextReq & ISC_REQ_CONNECTION)
            ctxt_attr |= ISC_RET_CONNECTION;
        if(fContextReq & ISC_REQ_EXTENDED_ERROR)
            ctxt_attr |= ISC_RET_EXTENDED_ERROR;
        if(fContextReq & ISC_REQ_INTEGRITY)
            helper->want_flags |= NTLMSSP_NEGOTIATE_SIGN | NTLMSSP_NEGOTIATE_KEY_EXCHANGE;
        if(fContextReq & ISC_REQ_MUTUAL_AUTH)
            ctxt_attr |= ISC_RET_MUTUAL_AUTH;
        if(fContextReq & ISC_REQ_REPLAY_DETECT)
            helper->want_flags |= NTLMSSP_NEGOTIATE_SIGN | NTLMSSP_NEGOTIATE_KEY_EXCHANGE;
        if(fContextReq & ISC_REQ_SEQUENCE_DETECT)
            helper->want_flags |= NTLMSSP_NEGOTIATE_SIGN | NTLMSSP_NEGOTIATE_KEY_EXCHANGE;
        if(fContextReq & ISC_REQ_STREAM)
            FIXME("ISC_REQ_STREAM\n");
        if(fContextReq & ISC_REQ_USE_DCE_STYLE)
            ctxt_attr |= ISC_RET_USED_DCE_STYLE;
        if(fContextReq & ISC_REQ_DELEGATE)
            ctxt_attr |= ISC_RET_DELEGATE;

        if((ret = NtlmBuildNegotiate(helper, bin, NTLM_MAX_BUF, &bin_len)) != SEC_E_OK)
        {
            NtlmFreeContext(helper);
            goto isc_end;
        }

        /* put the decoded client blob into the out buffer */

        phNewContext->dwUpper = ctxt_attr;
        phNewContext->dwLower = (ULONG_PTR)helper;
        if (pfContextAttr)
            *pfContextAttr = fContextReq & (ISC_REQ_CONFIDENTIALITY | ISC_REQ_INTEGRITY | ISC_REQ_REPLAY_DETECT |
                                            ISC_REQ_SEQUENCE_DETECT | ISC_REQ_CONNECTION);

        ret = SEC_I_CONTINUE_NEEDED;
    }
    else
    {
        int input_token_idx;

        /* handle second call here */
        if (!pInput || ((input_token_idx = ntlm_GetTokenBufferIndex(pInput)) == -1))
        {
            ret = SEC_E_INVALID_TOKEN;
            goto isc_end;
        }

        if(!phContext)
        {
            ret = SEC_E_INVALID_HANDLE;
            goto isc_end;
        }

        /* As the server side of sspi never calls this, make sure that
         * the handler is a client handler.
         */
        helper = (PNegoHelper)phContext->dwLower;
        if(helper->mode != NTLM_CLIENT)
        {
            TRACE("Helper mode = %d\n", helper->mode);
            ret = SEC_E_INVALID_HANDLE;
            goto isc_end;
        }

        if (!pInput->pBuffers[input_token_idx].pvBuffer)
        {
            ret = SEC_E_INTERNAL_ERROR;
            goto isc_end;
        }

        if(pInput->pBuffers[input_token_idx].cbBuffer > NTLM_MAX_BUF)
        {
            TRACE("pInput->pBuffers[%d].cbBuffer is: %d\n",
                    input_token_idx,
                    pInput->pBuffers[input_token_idx].cbBuffer);
            ret = SEC_E_INVALID_TOKEN;
            goto isc_end;
        }

        if((ret = NtlmBuildAuthenticate(helper, pInput->pBuffers[input_token_idx].pvBuffer,
                        pInput->pBuffers[input_token_idx].cbBuffer, bin, NTLM_MAX_BUF,
                        &bin_len)) != SEC_E_OK)
        {
            goto isc_end;
        }

        phNewContext->dwUpper = ctxt_attr;
        phNewContext->dwLower = (ULONG_PTR)helper;

        ret = SEC_E_OK;
    }

    /* put the decoded client blob into the out buffer */

    if (!pOutput || ((token_idx = ntlm_GetTokenBufferIndex(pOutput)) == -1))
    {
        TRACE("no SECBUFFER_TOKEN buffer could be found\n");
        ret = SEC_E_BUFFER_TOO_SMALL;
        if ((phContext == NULL) && (pInput == NULL))
        {
            NtlmFreeContext(helper);
            phNewContext->dwUpper = 0;
            phNewContext->dwLower = 0;
        }
        goto isc_end;
    }

    if (fContextReq & ISC_REQ_ALLOCATE_MEMORY)
    {
        pOutput->pBuffers[token_idx].pvBuffer = HeapAlloc(GetProcessHeap(), 0, bin_len);
        pOutput->pBuffers[token_idx].cbBuffer = bin_len;
    }
    else if (pOutput->pBuffers[token_idx].cbBuffer < bin_len)
    {
        TRACE("out buffer is NULL or has not enough space\n");
        ret = SEC_E_BUFFER_TOO_SMALL;
        if ((phContext == NULL) && (pInput == NULL))
        {
            NtlmFreeContext(helper);
            phNewContext->dwUpper = 0;
            phNewContext->dwLower = 0;
        }
        goto isc_end;
    }

    if (!pOutput->pBuffers[token_idx].pvBuffer)
    {
        TRACE("out buffer is NULL\n");
        ret = SEC_E_INTERNAL_ERROR;
        if ((phContext == NULL) && (pInput == NULL))
        {
            NtlmFreeContext(helper);
            phNewContext->dwUpper = 0;
            phNewContext->dwLower = 0;
        }
        goto isc_end;
    }

    pOutput->pBuffers[token_idx].cbBuffer = bin_len;
    memcpy(pOutput->pBuffers[token_idx].pvBuffer, bin, bin_len);

    if(ret == SEC_E_OK)
    {
        ret = ntlm_InitSessionSecurity(helper);
        if (pfContextAttr)
            *pfContextAttr = ntlm_GetContextAttributes(helper);
    }

isc_end:
    HeapFree(GetProcessHeap(), 0, bin);
    return ret;
}

/***********************************************************************
 *              InitializeSecurityContextA
 */
static SECURITY_STATUS SEC_ENTRY ntlm_InitializeSecurityContextA(
 PCredHandle phCredential, PCtxtHandle phContext, SEC_CHAR *pszTargetName,
 ULONG fContextReq, ULONG Reserved1, ULONG TargetDataRep, 
 PSecBufferDesc pInput,ULONG Reserved2, PCtxtHandle phNewContext, 
 PSecBufferDesc pOutput, ULONG *pfContextAttr, PTimeStamp ptsExpiry)
{
    SECURITY_STATUS ret;
    SEC_WCHAR *target = NULL;

    TRACE("%p %p %s %d %d %d %p %d %p %p %p %p\n", phCredential, phContext,
     debugstr_a(pszTargetName), fContextReq, Reserved1, TargetDataRep, pInput,
     Reserved1, phNewContext, pOutput, pfContextAttr, ptsExpiry);

    if(pszTargetName != NULL)
    {
        int target_size = MultiByteToWideChar(CP_ACP, 0, pszTargetName,
            strlen(pszTargetName)+1, NULL, 0);
        target = HeapAlloc(GetProcessHeap(), 0, target_size *
                sizeof(SEC_WCHAR));
        MultiByteToWideChar(CP_ACP, 0, pszTargetName, strlen(pszTargetName)+1,
            target, target_size);
    }

    ret = ntlm_InitializeSecurityContextW(phCredential, phContext, target,
            fContextReq, Reserved1, TargetDataRep, pInput, Reserved2,
            phNewContext, pOutput, pfContextAttr, ptsExpiry);

    HeapFree(GetProcessHeap(), 0, target);
    return ret;
}

/***********************************************************************
 *              AcceptSecurityContext
 */
SECURITY_STATUS SEC_ENTRY ntlm_AcceptSecurityContext(
 PCredHandle phCredential, PCtxtHandle phContext, PSecBufferDesc pInput,
 ULONG fContextReq, ULONG TargetDataRep, PCtxtHandle phNewContext, 
 PSecBufferDesc pOutput, ULONG *pfContextAttr, PTimeStamp ptsExpiry)
{
    SECURITY_STATUS ret;
    PBYTE bin;
    ULONG bin_len;
    ULONG ctxt_attr = 0;
    PNegoHelper helper;
    PNtlmCredentials ntlm_cred;

    TRACE("%p %p %p %d %d %p %p %p %p\n", phCredential, phContext, pInput,
     fContextReq, TargetDataRep, phNewContext, pOutput, pfContextAttr,
     ptsExpiry);

    bin    = HeapAlloc(GetProcessHeap(),0, sizeof(BYTE) * NTLM_MAX_BUF);
    if (!bin)
        return SEC_E_INSUFFICIENT_MEMORY;

    if(TargetDataRep == SECURITY_NETWORK_DREP){
        TRACE("Using SECURITY_NETWORK_DREP\n");
    }

    if(phContext == NULL)
    {
        if (!phCredential)
        {
            ret = SEC_E_INVALID_HANDLE;
            goto asc_end;
        }

        ntlm_cred = (PNtlmCredentials)phCredential->dwLower;

        if(!(ntlm_cred->use & SECPKG_CRED_INBOUND))
        {
            ret = SEC_E_INVALID_HANDLE;
            goto asc_end;
        }

        /* This is the first call to AcceptSecurityHandle */
        if(pInput == NULL)
        {
            ret = SEC_E_INCOMPLETE_MESSAGE;
            goto asc_end;
        }

        if(pInput->cBuffers < 1)
        {
            ret = SEC_E_INCOMPLETE_MESSAGE;
            goto asc_end;
        }

        if(pInput->pBuffers[0].cbBuffer > NTLM_MAX_BUF)
        {
            ret = SEC_E_INVALID_TOKEN;
            goto asc_end;
        }

        helper = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*helper));
        if (!helper)
        {
            ret = SEC_E_INSUFFICIENT_MEMORY;
            goto asc_end;
        }
        helper->mode = NTLM_SERVER;

        /* Handle all the flags */
        if(fContextReq & ASC_REQ_CONNECTION)
        {
            ctxt_attr |= ASC_RET_CONNECTION;
        }
        if(fContextReq & ASC_REQ_EXTENDED_ERROR)
        {
            FIXME("ASC_REQ_EXTENDED_ERROR stub\n");
        }
        if(fContextReq & ASC_REQ_MUTUAL_AUTH)
        {
            FIXME("ASC_REQ_MUTUAL_AUTH stub\n");
        }
        if(fContextReq & ASC_REQ_REPLAY_DETECT)
        {
            FIXME("ASC_REQ_REPLAY_DETECT stub\n");
        }
        if(fContextReq & ISC_REQ_SEQUENCE_DETECT)
        {
            FIXME("ASC_REQ_SEQUENCE_DETECT stub\n");
        }
        if(fContextReq & ISC_REQ_STREAM)
        {
            FIXME("ASC_REQ_STREAM stub\n");
        }
        /* Done with the flags */

        if((ret = NtlmBuildChallenge(helper, pInput->pBuffers[0].pvBuffer,
                        pInput->pBuffers[0].cbBuffer, bin, NTLM_MAX_BUF,
                        &bin_len)) != SEC_E_OK)
        {
            NtlmFreeContext(helper);
            goto asc_end;
        }

        /* send this to the client */
        if(pOutput == NULL)
        {
            ret = SEC_E_INSUFFICIENT_MEMORY;
            NtlmFreeContext(helper);
            goto asc_end;
        }

        if(pOutput->cBuffers < 1)
        {
            ret = SEC_E_INSUFFICIENT_MEMORY;
            NtlmFreeContext(helper);
            goto asc_end;
        }

        if(fContextReq & ASC_REQ_ALLOCATE_MEMORY)
        {
            pOutput->pBuffers[0].pvBuffer = HeapAlloc(GetProcessHeap(), 0, bin_len);
            if(!pOutput->pBuffers[0].pvBuffer)
            {
                ret = SEC_E_INSUFFICIENT_MEMORY;
                NtlmFreeContext(helper);
                goto asc_end;
            }
        }
        else if(pOutput->pBuffers[0].cbBuffer < bin_len)
        {
            ret = SEC_E_BUFFER_TOO_SMALL;
            NtlmFreeContext(helper);
            goto asc_end;
        }

        pOutput->pBuffers[0].cbBuffer = bin_len;
        memcpy(pOutput->pBuffers[0].pvBuffer, bin, bin_len);
        if (pfContextAttr)
            *pfContextAttr = ctxt_attr | (ntlm_GetContextAttributes(helper) & ~ASC_RET_INTEGRITY);
        ret = SEC_I_CONTINUE_NEEDED;

    }
    else
    {
        if(pInput == NULL)
        {
            ret = SEC_E_INCOMPLETE_MESSAGE;
            goto asc_end;
        }

        if(pInput->cBuffers < 1)
        {
            ret = SEC_E_INCOMPLETE_MESSAGE;
            goto asc_end;
        }

        helper = (PNegoHelper)phContext->dwLower;

        if(helper->mode != NTLM_SERVER)
        {
            ret = SEC_E_INVALID_HANDLE;
            goto asc_end;
        }

        if(pInput->pBuffers[0].cbBuffer > NTLM_MAX_BUF)
        {
            ret = SEC_E_INVALID_TOKEN;
            goto asc_end;
        }

        if((ret = NtlmAcceptAuthenticate(helper, pInput->pBuffers[0].pvBuffer,
                        pInput->pBuffers[0].cbBuffer)) != SEC_E_OK)
        {
            if (pfContextAttr)
                *pfContextAttr = ASC_RET_THIRD_LEG_FAILED;
            goto asc_end;
        }

        if(pOutput && pOutput->cBuffers >= 1)
            pOutput->pBuffers[0].cbBuffer = 0;

        if((ret = ntlm_InitSessionSecurity(helper)) != SEC_E_OK)
            goto asc_end;

        if (pfContextAttr)
            *pfContextAttr = ntlm_GetContextAttributes(helper);
    }

    phNewContext->dwUpper = ctxt_attr;
    phNewContext->dwLower = (ULONG_PTR)helper;

asc_end:
    HeapFree(GetProcessHeap(), 0, bin);
    return ret;
}

/***********************************************************************
 *              CompleteAuthToken
 */
static SECURITY_STATUS SEC_ENTRY ntlm_CompleteAuthToken(PCtxtHandle phContext,
 PSecBufferDesc pToken)
{
    /* We never need to call CompleteAuthToken anyway */
    TRACE("%p %p\n", phContext, pToken);
    if (!phContext)
        return SEC_E_INVALID_HANDLE;
    
    return SEC_E_OK;
}

/***********************************************************************
 *              DeleteSecurityContext
 */
SECURITY_STATUS SEC_ENTRY ntlm_DeleteSecurityContext(PCtxtHandle phContext)
{
    PNegoHelper helper;

    TRACE("%p\n", phContext);
    if (!phContext)
        return SEC_E_INVALID_HANDLE;

    helper = (PNegoHelper)phContext->dwLower;

    phContext->dwUpper = 0;
    phContext->dwLower = 0;

    ntlm_FreeSessionSecurity(helper);
    NtlmFreeContext(helper);

    return SEC_E_OK;
}


static SECURITY_STATUS ntlm_QueryNegotiationInfo(SecPkgContext_NegotiationInfoW *info, BOOL unicode)
{
    const SecPkgInfoW *source = unicode ? ntlm_package_infoW : (const SecPkgInfoW *)ntlm_package_infoA;
    ULONG name_size, comment_size;
    SecPkgInfoW *package;

    if (unicode)
    {
        name_size = (strlenW(source->Name) + 1) * sizeof(WCHAR);
        comment_size = (strlenW(source->Comment) + 1) * sizeof(WCHAR);
    }
    else
    {
        name_size = strlen((const char *)source->Name) + 1;
        comment_size = strlen((const char *)source->Comment) + 1;
    }

    package = HeapAlloc(GetProcessHeap(), 0, sizeof(*package) + name_size + comment_size);
    if (!package)
        return SEC_E_INSUFFICIENT_MEMORY;

    *package = *source;
    package->Name = (SEC_WCHAR *)(package + 1);
    memcpy(package->Name, source->Name, name_size);
    package->Comment = (SEC_WCHAR *)((BYTE *)package->Name + name_size);
    memcpy(package->Comment, source->Comment, comment_size);

    info->PackageInfo = package;
    info->NegotiationState = SECPKG_NEGOTIATION_COMPLETE;
    return SEC_E_OK;
}


/***********************************************************************
 *              QueryContextAttributesW
 */
SECURITY_STATUS SEC_ENTRY ntlm_QueryContextAttributesW(PCtxtHandle phContext,
 ULONG ulAttribute, void *pBuffer)
{
    TRACE("%p %d %p\n", phContext, ulAttribute, pBuffer);
    if (!phContext)
        return SEC_E_INVALID_HANDLE;

    switch(ulAttribute)
    {
#define _x(x) case (x) : FIXME(#x" stub\n"); break
        _x(SECPKG_ATTR_ACCESS_TOKEN);
        _x(SECPKG_ATTR_AUTHORITY);
        _x(SECPKG_ATTR_DCE_INFO);
        case SECPKG_ATTR_FLAGS:
        {
            PSecPkgContext_Flags spcf = (PSecPkgContext_Flags)pBuffer;
            PNegoHelper helper = (PNegoHelper)phContext->dwLower;

            spcf->Flags = ntlm_GetContextAttributes(helper);
            return SEC_E_OK;
        }
        _x(SECPKG_ATTR_KEY_INFO);
        _x(SECPKG_ATTR_LIFESPAN);
        _x(SECPKG_ATTR_NAMES);
        _x(SECPKG_ATTR_NATIVE_NAMES);
        case SECPKG_ATTR_NEGOTIATION_INFO:
            return ntlm_QueryNegotiationInfo(pBuffer, TRUE);
        _x(SECPKG_ATTR_PACKAGE_INFO);
        _x(SECPKG_ATTR_PASSWORD_EXPIRY);
        _x(SECPKG_ATTR_SESSION_KEY);
        case SECPKG_ATTR_SIZES:
        {
            PSecPkgContext_Sizes spcs = (PSecPkgContext_Sizes)pBuffer;
            PNegoHelper helper = (PNegoHelper)phContext->dwLower;

            spcs->cbMaxToken = NTLM_MAX_BUF;
            spcs->cbMaxSignature = 16;
            spcs->cbBlockSize = (helper->neg_flags & NTLMSSP_NEGOTIATE_SEAL) ? 1 : 0;
            spcs->cbSecurityTrailer = 16;
            return SEC_E_OK;
        }
        _x(SECPKG_ATTR_STREAM_SIZES);
        _x(SECPKG_ATTR_TARGET_INFORMATION);
#undef _x
        default:
            TRACE("Unknown value %d passed for ulAttribute\n", ulAttribute);
    }

    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              QueryContextAttributesA
 */
SECURITY_STATUS SEC_ENTRY ntlm_QueryContextAttributesA(PCtxtHandle phContext,
 ULONG ulAttribute, void *pBuffer)
{
    if (phContext && ulAttribute == SECPKG_ATTR_NEGOTIATION_INFO)
        return ntlm_QueryNegotiationInfo(pBuffer, FALSE);

    return ntlm_QueryContextAttributesW(phContext, ulAttribute, pBuffer);
}

/***********************************************************************
 *              ImpersonateSecurityContext
 */
static SECURITY_STATUS SEC_ENTRY ntlm_ImpersonateSecurityContext(PCtxtHandle phContext)
{
    PNegoHelper helper;
    BOOL ret;

    TRACE("%p\n", phContext);
    if (!phContext)
        return SEC_E_INVALID_HANDLE;

    helper = (PNegoHelper)phContext->dwLower;
    if (helper->mode != NTLM_SERVER || !helper->crypt.ntlm.a4i)
        return SEC_E_INVALID_HANDLE;

    if (helper->token)
        ret = ImpersonateLoggedOnUser(helper->token);
    else
        ret = ImpersonateAnonymousToken(GetCurrentThread());

    return ret ? SEC_E_OK : SEC_E_NO_IMPERSONATION;
}

/***********************************************************************
 *              RevertSecurityContext
 */
static SECURITY_STATUS SEC_ENTRY ntlm_RevertSecurityContext(PCtxtHandle phContext)
{
    TRACE("%p\n", phContext);
    if (!phContext)
        return SEC_E_INVALID_HANDLE;

    return RevertToSelf() ? SEC_E_OK : SEC_E_NO_IMPERSONATION;
}


/***********************************************************************
 *             ntlm_CreateSignature
 * As both MakeSignature and VerifySignature need this, but different keys
 * are needed for NTLM2, the logic goes into a helper function.
 * To ensure maximal reusability, we can specify the direction as NTLM_SEND for
 * signing/encrypting and NTLM_RECV for verifying/decrypting. When encrypting,
 * the signature is encrypted after the message was encrypted, so
 * CreateSignature shouldn't do it. In this case, encrypt_sig can be set to
 * false.
 */
static SECURITY_STATUS ntlm_CreateSignature(PNegoHelper helper, PSecBufferDesc pMessage,
        int token_idx, SignDirection direction, BOOL encrypt_sig)
{
    ULONG sign_version = 1;
    UINT i;
    PBYTE sig;
    TRACE("%p, %p, %d, %d, %d\n", helper, pMessage, token_idx, direction,
            encrypt_sig);

    sig = pMessage->pBuffers[token_idx].pvBuffer;

    if(helper->neg_flags & NTLMSSP_NEGOTIATE_NTLM2 &&
            helper->neg_flags & NTLMSSP_NEGOTIATE_SIGN)
    {
        BYTE digest[16];
        BYTE seq_no[4];
        HMAC_MD5_CTX hmac_md5_ctx;

        TRACE("Signing NTLM2 style\n");

        if(direction == NTLM_SEND)
        {
            seq_no[0] = (helper->crypt.ntlm2.send_seq_no >>  0) & 0xff;
            seq_no[1] = (helper->crypt.ntlm2.send_seq_no >>  8) & 0xff;
            seq_no[2] = (helper->crypt.ntlm2.send_seq_no >> 16) & 0xff;
            seq_no[3] = (helper->crypt.ntlm2.send_seq_no >> 24) & 0xff;

            ++(helper->crypt.ntlm2.send_seq_no);

            HMACMD5Init(&hmac_md5_ctx, helper->crypt.ntlm2.send_sign_key, 16);
        }
        else
        {
            seq_no[0] = (helper->crypt.ntlm2.recv_seq_no >>  0) & 0xff;
            seq_no[1] = (helper->crypt.ntlm2.recv_seq_no >>  8) & 0xff;
            seq_no[2] = (helper->crypt.ntlm2.recv_seq_no >> 16) & 0xff;
            seq_no[3] = (helper->crypt.ntlm2.recv_seq_no >> 24) & 0xff;

            ++(helper->crypt.ntlm2.recv_seq_no);

            HMACMD5Init(&hmac_md5_ctx, helper->crypt.ntlm2.recv_sign_key, 16);
        }

        HMACMD5Update(&hmac_md5_ctx, seq_no, 4);
        for( i = 0; i < pMessage->cBuffers; ++i )
        {
            if(pMessage->pBuffers[i].BufferType & SECBUFFER_DATA)
                HMACMD5Update(&hmac_md5_ctx, pMessage->pBuffers[i].pvBuffer,
                        pMessage->pBuffers[i].cbBuffer);
        }

        HMACMD5Final(&hmac_md5_ctx, digest);

        if(encrypt_sig && helper->neg_flags & NTLMSSP_NEGOTIATE_KEY_EXCHANGE)
        {
            if(direction == NTLM_SEND)
                SECUR32_arc4Process(helper->crypt.ntlm2.send_a4i, digest, 8);
            else
                SECUR32_arc4Process(helper->crypt.ntlm2.recv_a4i, digest, 8);
        }

        /* The NTLM2 signature is the sign version */
        sig[ 0] = (sign_version >>  0) & 0xff;
        sig[ 1] = (sign_version >>  8) & 0xff;
        sig[ 2] = (sign_version >> 16) & 0xff;
        sig[ 3] = (sign_version >> 24) & 0xff;
        /* The first 8 bytes of the digest */
        memcpy(sig+4, digest, 8);
        /* And the sequence number */
        memcpy(sig+12, seq_no, 4);

        pMessage->pBuffers[token_idx].cbBuffer = 16;

        return SEC_E_OK;
    }
    if(helper->neg_flags & NTLMSSP_NEGOTIATE_SIGN)
    {
        ULONG crc = 0U;
        TRACE("Signing NTLM1 style\n");

        for(i=0; i < pMessage->cBuffers; ++i)
        {
            if(pMessage->pBuffers[i].BufferType & SECBUFFER_DATA)
            {
                crc = ComputeCrc32(pMessage->pBuffers[i].pvBuffer,
                    pMessage->pBuffers[i].cbBuffer, crc);
            }
        }

        sig[ 0] = (sign_version >>  0) & 0xff;
        sig[ 1] = (sign_version >>  8) & 0xff;
        sig[ 2] = (sign_version >> 16) & 0xff;
        sig[ 3] = (sign_version >> 24) & 0xff;
        memset(sig+4, 0, 4);
        sig[ 8] = (crc >>  0) & 0xff;
        sig[ 9] = (crc >>  8) & 0xff;
        sig[10] = (crc >> 16) & 0xff;
        sig[11] = (crc >> 24) & 0xff;
        sig[12] = (helper->crypt.ntlm.seq_num >>  0) & 0xff;
        sig[13] = (helper->crypt.ntlm.seq_num >>  8) & 0xff;
        sig[14] = (helper->crypt.ntlm.seq_num >> 16) & 0xff;
        sig[15] = (helper->crypt.ntlm.seq_num >> 24) & 0xff;

        ++(helper->crypt.ntlm.seq_num);

        if(encrypt_sig)
            SECUR32_arc4Process(helper->crypt.ntlm.a4i, sig+4, 12);
        return SEC_E_OK;
    }

    if(helper->neg_flags & NTLMSSP_NEGOTIATE_ALWAYS_SIGN || helper->neg_flags == 0)
    {
        TRACE("Creating a dummy signature.\n");
        /* A dummy signature is 0x01 followed by 15 bytes of 0x00 */
        memset(pMessage->pBuffers[token_idx].pvBuffer, 0, 16);
        memset(pMessage->pBuffers[token_idx].pvBuffer, 0x01, 1);
        pMessage->pBuffers[token_idx].cbBuffer = 16;
        return SEC_E_OK;
    }

    return SEC_E_UNSUPPORTED_FUNCTION;
}

/***********************************************************************
 *              MakeSignature
 */
SECURITY_STATUS SEC_ENTRY ntlm_MakeSignature(PCtxtHandle phContext,
    ULONG fQOP, PSecBufferDesc pMessage, ULONG MessageSeqNo)
{
    PNegoHelper helper;
    int token_idx;

    TRACE("%p %d %p %d\n", phContext, fQOP, pMessage, MessageSeqNo);
    if (!phContext)
        return SEC_E_INVALID_HANDLE;

    if(fQOP)
        FIXME("Ignoring fQOP 0x%08x\n", fQOP);

    if(MessageSeqNo)
        FIXME("Ignoring MessageSeqNo\n");

    if(!pMessage || !pMessage->pBuffers || pMessage->cBuffers < 2)
        return SEC_E_INVALID_TOKEN;

    /* If we didn't find a SECBUFFER_TOKEN type buffer */
    if((token_idx = ntlm_GetTokenBufferIndex(pMessage)) == -1)
        return SEC_E_INVALID_TOKEN;

    if(pMessage->pBuffers[token_idx].cbBuffer < 16)
        return SEC_E_BUFFER_TOO_SMALL;

    helper = (PNegoHelper)phContext->dwLower;
    TRACE("Negotiated flags are: 0x%08x\n", helper->neg_flags);

    return ntlm_CreateSignature(helper, pMessage, token_idx, NTLM_SEND, TRUE);
}

/***********************************************************************
 *              VerifySignature
 */
SECURITY_STATUS SEC_ENTRY ntlm_VerifySignature(PCtxtHandle phContext,
    PSecBufferDesc pMessage, ULONG MessageSeqNo, PULONG pfQOP)
{
    PNegoHelper helper;
    UINT i;
    int token_idx;
    SECURITY_STATUS ret;
    SecBufferDesc local_desc;
    PSecBuffer     local_buff;
    BYTE          local_sig[16];

    TRACE("%p %p %d %p\n", phContext, pMessage, MessageSeqNo, pfQOP);
    if(!phContext)
        return SEC_E_INVALID_HANDLE;

    if(!pMessage || !pMessage->pBuffers || pMessage->cBuffers < 2)
        return SEC_E_INVALID_TOKEN;

    if((token_idx = ntlm_GetTokenBufferIndex(pMessage)) == -1)
        return SEC_E_INVALID_TOKEN;

    if(pMessage->pBuffers[token_idx].cbBuffer < 16)
        return SEC_E_BUFFER_TOO_SMALL;

    if(MessageSeqNo)
        FIXME("Ignoring MessageSeqNo\n");

    helper = (PNegoHelper)phContext->dwLower;
    TRACE("Negotiated flags: 0x%08x\n", helper->neg_flags);

    local_buff = HeapAlloc(GetProcessHeap(), 0, pMessage->cBuffers * sizeof(SecBuffer));

    local_desc.ulVersion = SECBUFFER_VERSION;
    local_desc.cBuffers = pMessage->cBuffers;
    local_desc.pBuffers = local_buff;

    for(i=0; i < pMessage->cBuffers; ++i)
    {
        if(pMessage->pBuffers[i].BufferType == SECBUFFER_TOKEN)
        {
            local_buff[i].BufferType = SECBUFFER_TOKEN;
            local_buff[i].cbBuffer = 16;
            local_buff[i].pvBuffer = local_sig;
        }
        else
        {
            local_buff[i].BufferType = pMessage->pBuffers[i].BufferType;
            local_buff[i].cbBuffer = pMessage->pBuffers[i].cbBuffer;
            local_buff[i].pvBuffer = pMessage->pBuffers[i].pvBuffer;
        }
    }

    if((ret = ntlm_CreateSignature(helper, &local_desc, token_idx, NTLM_RECV, TRUE)) != SEC_E_OK)
        return ret;

    if(memcmp(((PBYTE)local_buff[token_idx].pvBuffer) + 8,
                ((PBYTE)pMessage->pBuffers[token_idx].pvBuffer) + 8, 8))
        ret = SEC_E_MESSAGE_ALTERED;
    else
        ret = SEC_E_OK;

    HeapFree(GetProcessHeap(), 0, local_buff);

    return ret;

}

/***********************************************************************
 *             FreeCredentialsHandle
 */
SECURITY_STATUS SEC_ENTRY ntlm_FreeCredentialsHandle(PCredHandle phCredential)
{
    SECURITY_STATUS ret;

    if(phCredential){
        PNtlmCredentials ntlm_cred = (PNtlmCredentials) phCredential->dwLower;
        phCredential->dwUpper = 0;
        phCredential->dwLower = 0;
        ntlm_FreeCredentials(ntlm_cred);
        ret = SEC_E_OK;
    }
    else
        ret = SEC_E_OK;
    
    return ret;
}

/***********************************************************************
 *             EncryptMessage
 */
SECURITY_STATUS SEC_ENTRY ntlm_EncryptMessage(PCtxtHandle phContext,
    ULONG fQOP, PSecBufferDesc pMessage, ULONG MessageSeqNo)
{
    PNegoHelper helper;
    int token_idx, data_idx;

    TRACE("(%p %d %p %d)\n", phContext, fQOP, pMessage, MessageSeqNo);

    if(!phContext)
        return SEC_E_INVALID_HANDLE;

    if(fQOP)
        FIXME("Ignoring fQOP\n");

    if(MessageSeqNo)
        FIXME("Ignoring MessageSeqNo\n");

    if(!pMessage || !pMessage->pBuffers || pMessage->cBuffers < 2)
        return SEC_E_INVALID_TOKEN;

    if((token_idx = ntlm_GetTokenBufferIndex(pMessage)) == -1)
        return SEC_E_INVALID_TOKEN;

    if((data_idx = ntlm_GetDataBufferIndex(pMessage)) ==-1 )
        return SEC_E_INVALID_TOKEN;

    if(pMessage->pBuffers[token_idx].cbBuffer < 16)
        return SEC_E_BUFFER_TOO_SMALL;

    helper = (PNegoHelper) phContext->dwLower;

    if(!(helper->neg_flags & (NTLMSSP_NEGOTIATE_SIGN | NTLMSSP_NEGOTIATE_SEAL)))
        return SEC_E_UNSUPPORTED_FUNCTION;

    if(helper->neg_flags & NTLMSSP_NEGOTIATE_NTLM2)
    { 
        ULONG save_flags = helper->neg_flags;

        helper->neg_flags |= NTLMSSP_NEGOTIATE_SIGN;
        ntlm_CreateSignature(helper, pMessage, token_idx, NTLM_SEND, FALSE);
        helper->neg_flags = save_flags;
        SECUR32_arc4Process(helper->crypt.ntlm2.send_a4i,
                pMessage->pBuffers[data_idx].pvBuffer,
                pMessage->pBuffers[data_idx].cbBuffer);

        if(helper->neg_flags & NTLMSSP_NEGOTIATE_KEY_EXCHANGE)
            SECUR32_arc4Process(helper->crypt.ntlm2.send_a4i,
                    ((BYTE *)pMessage->pBuffers[token_idx].pvBuffer)+4, 8);
    }
    else
    {
        PBYTE sig;
        ULONG save_flags;

        /* EncryptMessage always produces real signatures, so make sure
         * NTLMSSP_NEGOTIATE_SIGN is set*/
        save_flags = helper->neg_flags;
        helper->neg_flags |= NTLMSSP_NEGOTIATE_SIGN;
        ntlm_CreateSignature(helper, pMessage, token_idx, NTLM_SEND, FALSE);
        helper->neg_flags = save_flags;

        sig = pMessage->pBuffers[token_idx].pvBuffer;

        SECUR32_arc4Process(helper->crypt.ntlm.a4i,
                pMessage->pBuffers[data_idx].pvBuffer,
                pMessage->pBuffers[data_idx].cbBuffer);
        SECUR32_arc4Process(helper->crypt.ntlm.a4i, sig+4, 12);

        if(helper->neg_flags & NTLMSSP_NEGOTIATE_ALWAYS_SIGN || helper->neg_flags == 0)
            memset(sig+4, 0, 4);
    }
    return SEC_E_OK;
}

/***********************************************************************
 *             DecryptMessage
 */
SECURITY_STATUS SEC_ENTRY ntlm_DecryptMessage(PCtxtHandle phContext,
    PSecBufferDesc pMessage, ULONG MessageSeqNo, PULONG pfQOP)
{
    SECURITY_STATUS ret;
    ULONG ntlmssp_flags_save;
    PNegoHelper helper;
    int token_idx, data_idx;
    TRACE("(%p %p %d %p)\n", phContext, pMessage, MessageSeqNo, pfQOP);

    if(!phContext)
        return SEC_E_INVALID_HANDLE;

    if(MessageSeqNo)
        FIXME("Ignoring MessageSeqNo\n");

    if(!pMessage || !pMessage->pBuffers || pMessage->cBuffers < 2)
        return SEC_E_INVALID_TOKEN;

    if((token_idx = ntlm_GetTokenBufferIndex(pMessage)) == -1)
        return SEC_E_INVALID_TOKEN;

    if((data_idx = ntlm_GetDataBufferIndex(pMessage)) ==-1)
        return SEC_E_INVALID_TOKEN;

    if(pMessage->pBuffers[token_idx].cbBuffer < 16)
        return SEC_E_BUFFER_TOO_SMALL;

    helper = (PNegoHelper) phContext->dwLower;

    if(!(helper->neg_flags & (NTLMSSP_NEGOTIATE_SIGN | NTLMSSP_NEGOTIATE_SEAL)))
        return SEC_E_UNSUPPORTED_FUNCTION;

    if(helper->neg_flags & NTLMSSP_NEGOTIATE_NTLM2)
    {
        SECUR32_arc4Process(helper->crypt.ntlm2.recv_a4i,
                pMessage->pBuffers[data_idx].pvBuffer,
                pMessage->pBuffers[data_idx].cbBuffer);
    }
    else
    {
        SECUR32_arc4Process(helper->crypt.ntlm.a4i,
                pMessage->pBuffers[data_idx].pvBuffer,
                pMessage->pBuffers[data_idx].cbBuffer);
    }

    /* Make sure we use a session key for the signature check, EncryptMessage
     * always does that, even in the dummy case */
    ntlmssp_flags_save = helper->neg_flags;

    helper->neg_flags |= NTLMSSP_NEGOTIATE_SIGN;
    ret = ntlm_VerifySignature(phContext, pMessage, MessageSeqNo, pfQOP);

    helper->neg_flags = ntlmssp_flags_save;

    return ret;
}

static const SecurityFunctionTableA ntlmTableA = {
    1,
    NULL,   /* EnumerateSecurityPackagesA */
    ntlm_QueryCredentialsAttributesA,   /* QueryCredentialsAttributesA */
    ntlm_AcquireCredentialsHandleA,     /* AcquireCredentialsHandleA */
    ntlm_FreeCredentialsHandle,         /* FreeCredentialsHandle */
    NULL,   /* Reserved2 */
    ntlm_InitializeSecurityContextA,    /* InitializeSecurityContextA */
    ntlm_AcceptSecurityContext,         /* AcceptSecurityContext */
    ntlm_CompleteAuthToken,             /* CompleteAuthToken */
    ntlm_DeleteSecurityContext,         /* DeleteSecurityContext */
    NULL,  /* ApplyControlToken */
    ntlm_QueryContextAttributesA,       /* QueryContextAttributesA */
    ntlm_ImpersonateSecurityContext,    /* ImpersonateSecurityContext */
    ntlm_RevertSecurityContext,         /* RevertSecurityContext */
    ntlm_MakeSignature,                 /* MakeSignature */
    ntlm_VerifySignature,               /* VerifySignature */
    FreeContextBuffer,                  /* FreeContextBuffer */
    NULL,   /* QuerySecurityPackageInfoA */
    NULL,   /* Reserved3 */
    NULL,   /* Reserved4 */
    NULL,   /* ExportSecurityContext */
    NULL,   /* ImportSecurityContextA */
    NULL,   /* AddCredentialsA */
    NULL,   /* Reserved8 */
    NULL,   /* QuerySecurityContextToken */
    ntlm_EncryptMessage,                /* EncryptMessage */
    ntlm_DecryptMessage,                /* DecryptMessage */
    NULL,   /* SetContextAttributesA */
};

static const SecurityFunctionTableW ntlmTableW = {
    1,
    NULL,   /* EnumerateSecurityPackagesW */
    ntlm_QueryCredentialsAttributesW,   /* QueryCredentialsAttributesW */
    ntlm_AcquireCredentialsHandleW,     /* AcquireCredentialsHandleW */
    ntlm_FreeCredentialsHandle,         /* FreeCredentialsHandle */
    NULL,   /* Reserved2 */
    ntlm_InitializeSecurityContextW,    /* InitializeSecurityContextW */
    ntlm_AcceptSecurityContext,         /* AcceptSecurityContext */
    ntlm_CompleteAuthToken,             /* CompleteAuthToken */
    ntlm_DeleteSecurityContext,         /* DeleteSecurityContext */
    NULL,  /* ApplyControlToken */
    ntlm_QueryContextAttributesW,       /* QueryContextAttributesW */
    ntlm_ImpersonateSecurityContext,    /* ImpersonateSecurityContext */
    ntlm_RevertSecurityContext,         /* RevertSecurityContext */
    ntlm_MakeSignature,                 /* MakeSignature */
    ntlm_VerifySignature,               /* VerifySignature */
    FreeContextBuffer,                  /* FreeContextBuffer */
    NULL,   /* QuerySecurityPackageInfoW */
    NULL,   /* Reserved3 */
    NULL,   /* Reserved4 */
    NULL,   /* ExportSecurityContext */
    NULL,   /* ImportSecurityContextW */
    NULL,   /* AddCredentialsW */
    NULL,   /* Reserved8 */
    NULL,   /* QuerySecurityContextToken */
    ntlm_EncryptMessage,                /* EncryptMessage */
    ntlm_DecryptMessage,                /* DecryptMessage */
    NULL,   /* SetContextAttributesW */
};

#define NTLM_COMMENT \
   { 'N', 'T', 'L', 'M', ' ', \
     'S', 'e', 'c', 'u', 'r', 'i', 't', 'y', ' ', \
     'P', 'a', 'c', 'k', 'a', 'g', 'e', 0}

static CHAR ntlm_comment_A[] = NTLM_COMMENT;
static WCHAR ntlm_comment_W[] = NTLM_COMMENT;

#define NTLM_NAME {'N', 'T', 'L', 'M', 0}

static char ntlm_name_A[] = NTLM_NAME;
static WCHAR ntlm_name_W[] = NTLM_NAME;

/* According to Windows, NTLM has the following capabilities.  */
#define CAPS ( \
    SECPKG_FLAG_INTEGRITY  | \
    SECPKG_FLAG_PRIVACY    | \
    SECPKG_FLAG_TOKEN_ONLY | \
    SECPKG_FLAG_CONNECTION | \
    SECPKG_FLAG_MULTI_REQUIRED    | \
    SECPKG_FLAG_IMPERSONATION     | \
    SECPKG_FLAG_ACCEPT_WIN32_NAME | \
    SECPKG_FLAG_NEGOTIABLE        | \
    SECPKG_FLAG_LOGON             | \
    SECPKG_FLAG_RESTRICTED_TOKENS )

static const SecPkgInfoW infoW = {
    CAPS,
    1,
    RPC_C_AUTHN_WINNT,
    NTLM_MAX_BUF,
    ntlm_name_W,
    ntlm_comment_W
};

static const SecPkgInfoA infoA = {
    CAPS,
    1,
    RPC_C_AUTHN_WINNT,
    NTLM_MAX_BUF,
    ntlm_name_A,
    ntlm_comment_A
};

SecPkgInfoA *ntlm_package_infoA = (SecPkgInfoA *)&infoA;
SecPkgInfoW *ntlm_package_infoW = (SecPkgInfoW *)&infoW;

void SECUR32_initNTLMSP(void)
{
    SecureProvider *provider = SECUR32_addProvider(&ntlmTableA, &ntlmTableW, NULL);
    SECUR32_addPackages(provider, 1L, ntlm_package_infoA, ntlm_package_infoW);
}
