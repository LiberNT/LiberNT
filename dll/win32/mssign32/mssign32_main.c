/*
 * MSSIGN32 implementation
 *
 * Copyright 2009 Austin English
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
#ifdef __REACTOS__
#include <stdlib.h>
#endif

#include "windef.h"
#include "winbase.h"
#include "wincrypt.h"
#ifdef __REACTOS__
#include "wintrust.h"
#endif

#include "wine/debug.h"
#include "wine/mssign.h"

WINE_DEFAULT_DEBUG_CHANNEL(mssign);

HRESULT WINAPI PvkGetCryptProv(HWND hwnd, LPCWSTR pwszCaption, LPCWSTR pwszCapiProvider,
                    DWORD dwProviderType, LPCWSTR pwszPvkFile, LPCWSTR pwszKeyContainerName,
                    DWORD *pdwKeySpec, LPWSTR *ppwszTmpContainer, HCRYPTPROV *phCryptProv)
{
    FIXME("%p %s %s %ld %s %s %p %p %p stub\n", hwnd, debugstr_w(pwszCaption), debugstr_w(pwszCapiProvider),
                    dwProviderType, debugstr_w(pwszPvkFile), debugstr_w(pwszKeyContainerName),
                    pdwKeySpec, ppwszTmpContainer, phCryptProv);

    return E_FAIL;
}

BOOL WINAPI PvkPrivateKeyAcquireContextFromMemory(LPCWSTR pwszProvName, DWORD dwProvType,
                    BYTE *pbData, DWORD cbData, HWND hwndOwner, LPCWSTR pwszKeyName,
                    DWORD *pdwKeySpec, HCRYPTPROV *phCryptProv, LPWSTR *ppwszTmpContainer)
{
    FIXME("%s %ld %p %ld %p %s %p %p %p stub\n", debugstr_w(pwszProvName), dwProvType,
                    pbData, cbData, hwndOwner, debugstr_w(pwszKeyName), pdwKeySpec,
                    phCryptProv, ppwszTmpContainer);

    return FALSE;
}

void WINAPI PvkFreeCryptProv(HCRYPTPROV hProv, LPCWSTR pwszCapiProvider, DWORD dwProviderType,
                    LPWSTR pwszTmpContainer)
{
    FIXME("%08Ix %s %ld %s stub\n", hProv, debugstr_w(pwszCapiProvider), dwProviderType,
                    debugstr_w(pwszTmpContainer));
}

#ifdef __REACTOS__
#define SIGNER_SUBJECT_FILE     1
#define PVK_TYPE_KEYCONTAINER   2

static const GUID catalog_subject = { 0xDE351A43, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };

static HRESULT signer_last_error(void)
{
    DWORD error = GetLastError();

    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

static BOOL signer_encode(LPCSTR type, const void *info, CRYPT_ATTR_BLOB *blob)
{
    return CryptEncodeObjectEx(X509_ASN_ENCODING, type, info, CRYPT_ENCODE_ALLOC_FLAG, NULL,
                               &blob->pbData, &blob->cbData);
}

static BOOL signer_add_cert(CERT_BLOB **certs, DWORD *count, const CERT_CONTEXT *cert)
{
    CERT_BLOB *blobs;
    DWORD i;

    for (i = 0; i < *count; i++)
        if ((*certs)[i].cbData == cert->cbCertEncoded &&
            !memcmp((*certs)[i].pbData, cert->pbCertEncoded, cert->cbCertEncoded))
            return TRUE;
    if (!(blobs = realloc(*certs, (*count + 1) * sizeof(*blobs))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    blobs[*count].cbData = cert->cbCertEncoded;
    blobs[*count].pbData = cert->pbCertEncoded;
    *certs = blobs;
    (*count)++;
    return TRUE;
}

static BOOL signer_collect_certs(const SIGNER_CERT_STORE_INFO *store, CERT_BLOB **certs, DWORD *count,
                                 const CERT_CHAIN_CONTEXT **chain)
{
    const CERT_CONTEXT *cert = NULL;
    CERT_CHAIN_PARA para = { sizeof(para) };
    DWORD i;

    if (!signer_add_cert(certs, count, store->pSigningCert)) return FALSE;
    if ((store->dwCertPolicy & SIGNER_CERT_POLICY_STORE) && store->hCertStore)
    {
        while ((cert = CertEnumCertificatesInStore(store->hCertStore, cert)))
            if (!signer_add_cert(certs, count, cert))
            {
                CertFreeCertificateContext(cert);
                return FALSE;
            }
    }
    if (store->dwCertPolicy & (SIGNER_CERT_POLICY_CHAIN | SIGNER_CERT_POLICY_CHAIN_NO_ROOT))
    {
        const CERT_SIMPLE_CHAIN *simple;

        if (!CertGetCertificateChain(NULL, store->pSigningCert, NULL, store->hCertStore, &para, 0, NULL, chain))
            return FALSE;
        simple = (*chain)->rgpChain[0];
        for (i = 0; i < simple->cElement; i++)
        {
            const CERT_CONTEXT *element = simple->rgpElement[i]->pCertContext;

            if ((store->dwCertPolicy & SIGNER_CERT_POLICY_CHAIN_NO_ROOT) && i == simple->cElement - 1 &&
                CertCompareCertificateName(X509_ASN_ENCODING, &element->pCertInfo->Subject,
                                           &element->pCertInfo->Issuer))
                break;
            if (!signer_add_cert(certs, count, element)) return FALSE;
        }
    }
    return TRUE;
}

static BOOL signer_get_content(SIP_DISPATCH_INFO *sip, SIP_SUBJECTINFO *info, LPCSTR *inner_oid,
                               BYTE **content, DWORD *content_len, HCRYPTMSG *decoded)
{
    SIP_INDIRECT_DATA *indirect;
    DWORD encoding, size = 0;
    BYTE *buffer;
    BOOL ret;

    if (IsEqualGUID(info->pgSubjectType, &catalog_subject))
    {
        if (!sip->pfGet(info, &encoding, 0, &size, NULL)) return FALSE;
        if (!(buffer = malloc(size)))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        ret = sip->pfGet(info, &encoding, 0, &size, buffer) &&
              (*decoded = CryptMsgOpenToDecode(encoding, 0, 0, 0, NULL, NULL)) &&
              CryptMsgUpdate(*decoded, buffer, size, TRUE);
        free(buffer);
        if (!ret) return FALSE;
        if (!CryptMsgGetParam(*decoded, CMSG_INNER_CONTENT_TYPE_PARAM, 0, NULL, &size)) return FALSE;
        if (!(*inner_oid = malloc(size)))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        if (!CryptMsgGetParam(*decoded, CMSG_INNER_CONTENT_TYPE_PARAM, 0, (void *)*inner_oid, &size)) return FALSE;
        if (!CryptMsgGetParam(*decoded, CMSG_CONTENT_PARAM, 0, NULL, content_len)) return FALSE;
        if (!(*content = malloc(*content_len)))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        return CryptMsgGetParam(*decoded, CMSG_CONTENT_PARAM, 0, *content, content_len);
    }

    if (!sip->pfCreate(info, &size, NULL)) return FALSE;
    if (!(indirect = malloc(size)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    ret = sip->pfCreate(info, &size, indirect) &&
          CryptEncodeObjectEx(X509_ASN_ENCODING, SPC_INDIRECT_DATA_CONTENT_STRUCT, indirect, 0, NULL, NULL, content_len) &&
          (*content = malloc(*content_len)) &&
          CryptEncodeObjectEx(X509_ASN_ENCODING, SPC_INDIRECT_DATA_CONTENT_STRUCT, indirect, 0, NULL, *content, content_len);
    free(indirect);
    if (ret && (*inner_oid = malloc(sizeof(SPC_INDIRECT_DATA_OBJID))))
        strcpy((char *)*inner_oid, SPC_INDIRECT_DATA_OBJID);
    else if (ret)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        ret = FALSE;
    }
    return ret;
}

#endif
HRESULT WINAPI SignerSign(SIGNER_SUBJECT_INFO *subject, SIGNER_CERT *cert, SIGNER_SIGNATURE_INFO *signature,
        SIGNER_PROVIDER_INFO *provider, const WCHAR *timestamp, CRYPT_ATTRIBUTES *attrs, void *sip_data)
{
#ifdef __REACTOS__
    CMSG_SIGNED_ENCODE_INFO sign_info = { sizeof(sign_info) };
    CMSG_SIGNER_ENCODE_INFO signer = { sizeof(signer) };
    SIP_SUBJECTINFO info = { sizeof(info) };
    SIP_DISPATCH_INFO sip = { sizeof(sip) };
    const CERT_CHAIN_CONTEXT *chain = NULL;
    const SIGNER_CERT_STORE_INFO *store;
    const SIGNER_FILE_INFO *file_info;
    CRYPT_ATTRIBUTE auth[2], *rg_auth = NULL;
    CRYPT_ATTR_BLOB values[2] = { { 0 } };
    BYTE *content = NULL, *encoded = NULL;
    DWORD content_len = 0, encoded_len, cert_count = 0, auth_count = 0, keyspec = 0, i;
    CERT_BLOB *certs = NULL;
    HCRYPTMSG decoded = NULL, msg = NULL;
    HCRYPTPROV prov = 0;
    BOOL free_prov = FALSE;
    HANDLE file = INVALID_HANDLE_VALUE;
    LPCSTR hash_oid, inner_oid = NULL;
    GUID subject_guid;
    HRESULT hr;

    TRACE("%p %p %p %p %s %p %p\n", subject, cert, signature, provider, debugstr_w(timestamp), attrs, sip_data);

    if (!subject || subject->cbSize != sizeof(*subject) || !cert || cert->cbSize != sizeof(*cert) ||
        !signature || signature->cbSize != sizeof(*signature))
        return E_INVALIDARG;
    if (subject->dwSubjectChoice != SIGNER_SUBJECT_FILE || cert->dwCertChoice != SIGNER_CERT_STORE || timestamp)
    {
        FIXME("subject choice %lu, cert choice %lu, timestamp %s not supported\n",
              subject->dwSubjectChoice, cert->dwCertChoice, debugstr_w(timestamp));
        return E_NOTIMPL;
    }
    file_info = subject->pSignerFileInfo;
    store = cert->pCertStoreInfo;
    if (!file_info || file_info->cbSize != sizeof(*file_info) || !store || store->cbSize != sizeof(*store) ||
        !store->pSigningCert)
        return E_INVALIDARG;
    if (signature->dwAttrChoice == SIGNER_AUTHCODE_ATTR &&
        (!signature->pAttrAuthcode || signature->pAttrAuthcode->cbSize != sizeof(*signature->pAttrAuthcode) ||
         (signature->pAttrAuthcode->fCommercial && signature->pAttrAuthcode->fIndividual)))
        return E_INVALIDARG;
    if (signature->dwAttrChoice != SIGNER_NO_ATTR && signature->dwAttrChoice != SIGNER_AUTHCODE_ATTR)
        return E_INVALIDARG;
    if (!(hash_oid = CertAlgIdToOID(signature->algidHash)))
        return NTE_BAD_ALGID;

    if (file_info->hFile && file_info->hFile != INVALID_HANDLE_VALUE)
        file = file_info->hFile;
    else if ((file = CreateFileW(file_info->pwszFileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE)
        return signer_last_error();

    hr = E_FAIL;
    if (!CryptSIPRetrieveSubjectGuid(file_info->pwszFileName, file, &subject_guid) ||
        !CryptSIPLoad(&subject_guid, 0, &sip))
        goto error;

    if (provider)
    {
        if (provider->cbSize != sizeof(*provider) || provider->dwPvkChoice != PVK_TYPE_KEYCONTAINER)
        {
            FIXME("provider key choice %lu not supported\n", provider->dwPvkChoice);
            hr = E_NOTIMPL;
            goto done;
        }
        if (!CryptAcquireContextW(&prov, provider->pwszKeyContainer, provider->pwszProviderName,
                                  provider->dwProviderType, 0))
            goto error;
        keyspec = provider->dwKeySpec;
        free_prov = TRUE;
    }
    else if (!CryptAcquireCertificatePrivateKey(store->pSigningCert, 0, NULL, &prov, &keyspec, &free_prov))
        goto error;

    info.pgSubjectType = &subject_guid;
    info.hFile = file;
    info.pwsFileName = file_info->pwszFileName;
    info.DigestAlgorithm.pszObjId = (char *)hash_oid;
    info.dwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    info.pClientData = sip_data;
    if (!signer_get_content(&sip, &info, &inner_oid, &content, &content_len, &decoded))
        goto error;

    if (signature->dwAttrChoice == SIGNER_AUTHCODE_ATTR)
    {
        const SIGNER_ATTR_AUTHCODE *authcode = signature->pAttrAuthcode;
        SPC_LINK more_info = { SPC_URL_LINK_CHOICE };
        SPC_SP_OPUS_INFO opus = { 0 };
        LPSTR purpose;
        CERT_ENHKEY_USAGE statement = { 1, &purpose };

        opus.pwszProgramName = authcode->pwszName;
        if (authcode->pwszInfo)
        {
            more_info.pwszUrl = (WCHAR *)authcode->pwszInfo;
            opus.pMoreInfo = &more_info;
        }
        if (!signer_encode(SPC_SP_OPUS_INFO_STRUCT, &opus, &values[auth_count])) goto error;
        auth[auth_count].pszObjId = (char *)SPC_SP_OPUS_INFO_OBJID;
        auth[auth_count].cValue = 1;
        auth[auth_count].rgValue = &values[auth_count];
        auth_count++;
        if (authcode->fCommercial || authcode->fIndividual)
        {
            purpose = (char *)(authcode->fCommercial ? SPC_COMMERCIAL_SP_KEY_PURPOSE_OBJID :
                                                       SPC_INDIVIDUAL_SP_KEY_PURPOSE_OBJID);
            if (!signer_encode(X509_ENHANCED_KEY_USAGE, &statement, &values[auth_count])) goto error;
            auth[auth_count].pszObjId = (char *)SPC_STATEMENT_TYPE_OBJID;
            auth[auth_count].cValue = 1;
            auth[auth_count].rgValue = &values[auth_count];
            auth_count++;
        }
    }
    if (signature->psAuthenticated && signature->psAuthenticated->cAttr)
    {
        if (!(rg_auth = malloc((auth_count + signature->psAuthenticated->cAttr) * sizeof(*rg_auth))))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        memcpy(rg_auth, auth, auth_count * sizeof(*auth));
        memcpy(rg_auth + auth_count, signature->psAuthenticated->rgAttr,
               signature->psAuthenticated->cAttr * sizeof(*rg_auth));
        signer.cAuthAttr = auth_count + signature->psAuthenticated->cAttr;
        signer.rgAuthAttr = rg_auth;
    }
    else
    {
        signer.cAuthAttr = auth_count;
        signer.rgAuthAttr = auth;
    }
    if (signature->psUnauthenticated)
    {
        signer.cUnauthAttr = signature->psUnauthenticated->cAttr;
        signer.rgUnauthAttr = signature->psUnauthenticated->rgAttr;
    }

    if (!signer_collect_certs(store, &certs, &cert_count, &chain))
        goto error;

    signer.pCertInfo = store->pSigningCert->pCertInfo;
    signer.hCryptProv = prov;
    signer.dwKeySpec = keyspec;
    signer.HashAlgorithm.pszObjId = (char *)hash_oid;
    sign_info.cSigners = 1;
    sign_info.rgSigners = &signer;
    sign_info.cCertEncoded = cert_count;
    sign_info.rgCertEncoded = certs;

    if (!(msg = CryptMsgOpenToEncode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CMSG_SIGNED, &sign_info,
                                     (LPSTR)inner_oid, NULL)) ||
        !CryptMsgUpdate(msg, content, content_len, TRUE) ||
        !CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, NULL, &encoded_len))
        goto error;
    if (!(encoded = malloc(encoded_len)))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (!CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, encoded, &encoded_len) ||
        !sip.pfPut(&info, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, subject->pdwIndex, encoded_len, encoded))
        goto error;
    hr = S_OK;
    goto done;

error:
    hr = signer_last_error();
done:
    free(encoded);
    if (msg) CryptMsgClose(msg);
    if (decoded) CryptMsgClose(decoded);
    if (chain) CertFreeCertificateChain(chain);
    free(certs);
    free(rg_auth);
    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        LocalFree(values[i].pbData);
    free(content);
    free((void *)inner_oid);
    if (free_prov) CryptReleaseContext(prov, 0);
    if (file != file_info->hFile) CloseHandle(file);
    return hr;
#else
    FIXME("%p %p %p %p %s %p %p stub\n", subject, cert, signature, provider, debugstr_w(timestamp), attrs, sip_data);
    return E_NOTIMPL;
#endif
}

HRESULT WINAPI SignerSignEx(DWORD flags, SIGNER_SUBJECT_INFO *subject_info, SIGNER_CERT *signer_cert,
                            SIGNER_SIGNATURE_INFO *signature_info, SIGNER_PROVIDER_INFO *provider_info,
                            const WCHAR *http_time_stamp, CRYPT_ATTRIBUTES *request, void *sip_data,
                            SIGNER_CONTEXT **signer_context)
{
    FIXME("%lx %p %p %p %p %s %p %p %p stub\n", flags, subject_info, signer_cert, signature_info, provider_info,
                    wine_dbgstr_w(http_time_stamp), request, sip_data, signer_cert);
    return E_NOTIMPL;
}

HRESULT WINAPI SignerFreeSignerContext(SIGNER_CONTEXT *signer_context)
{
    return S_OK;
}
