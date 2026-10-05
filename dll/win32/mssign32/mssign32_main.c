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
#include "wininet.h"
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

static const BYTE timestamp_request_oid[] = { 0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x03, 0x02, 0x01 };

struct signer_subject
{
    GUID              guid;
    SIP_DISPATCH_INFO sip;
    SIP_SUBJECTINFO   info;
    HANDLE            file;
    BOOL              close;
};

static HRESULT signer_check_statement(const CERT_CONTEXT *cert, const SIGNER_ATTR_AUTHCODE *authcode)
{
    CERT_KEY_USAGE_RESTRICTION_INFO *restriction;
    const CERT_EXTENSION *ext;
    const char *purpose;
    BOOL allowed = FALSE;
    DWORD size, i, j;

    if (!authcode->fCommercial && !authcode->fIndividual) return S_OK;
    purpose = authcode->fCommercial ? SPC_COMMERCIAL_SP_KEY_PURPOSE_OBJID : SPC_INDIVIDUAL_SP_KEY_PURPOSE_OBJID;
    if (!(ext = CertFindExtension(szOID_KEY_USAGE_RESTRICTION, cert->pCertInfo->cExtension,
                                  cert->pCertInfo->rgExtension)))
        allowed = authcode->fIndividual;
    else if (CryptDecodeObjectEx(X509_ASN_ENCODING, X509_KEY_USAGE_RESTRICTION, ext->Value.pbData,
                                 ext->Value.cbData, CRYPT_DECODE_ALLOC_FLAG, NULL, &restriction, &size))
    {
        for (i = 0; !allowed && i < restriction->cCertPolicyId; i++)
            for (j = 0; !allowed && j < restriction->rgCertPolicyId[i].cCertPolicyElementId; j++)
                allowed = !strcmp(restriction->rgCertPolicyId[i].rgpszCertPolicyElementId[j], purpose);
        LocalFree(restriction);
    }
    return allowed ? S_OK : TYPE_E_TYPEMISMATCH;
}

static BYTE *signer_der(BYTE tag, const BYTE *data, DWORD len, DWORD *out_len)
{
    DWORD len_bytes = len < 0x80 ? 1 : len < 0x100 ? 2 : len < 0x10000 ? 3 : len < 0x1000000 ? 4 : 5, i;
    BYTE *out;

    if (!(out = malloc(1 + len_bytes + len)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    out[0] = tag;
    if (len_bytes == 1)
        out[1] = len;
    else
    {
        out[1] = 0x80 | (len_bytes - 1);
        for (i = 0; i < len_bytes - 1; i++)
            out[2 + i] = len >> (8 * (len_bytes - 2 - i));
    }
    memcpy(out + 1 + len_bytes, data, len);
    *out_len = 1 + len_bytes + len;
    return out;
}

static HRESULT signer_http_post(const WCHAR *url, const char *body, DWORD body_len, BYTE **response,
                                DWORD *response_len)
{
    static const WCHAR headers[] = L"Content-Type: application/octet-stream\r\nCache-Control: no-cache\r\n";
    WCHAR host[INTERNET_MAX_HOST_NAME_LENGTH], path[INTERNET_MAX_URL_LENGTH], extra[INTERNET_MAX_URL_LENGTH];
    URL_COMPONENTSW components = { sizeof(components) };
    HINTERNET internet = NULL, connection = NULL, request = NULL;
    DWORD read, capacity = 4096;
    BYTE *buffer = NULL, *grown;
    HRESULT hr = E_FAIL;

    *response = NULL;
    *response_len = 0;
    components.lpszHostName = host;
    components.dwHostNameLength = ARRAYSIZE(host);
    components.lpszUrlPath = path;
    components.dwUrlPathLength = ARRAYSIZE(path);
    components.lpszExtraInfo = extra;
    components.dwExtraInfoLength = ARRAYSIZE(extra);
    if (!InternetCrackUrlW(url, 0, 0, &components))
        goto error;
    if (!path[0])
        lstrcpyW(path, L"/");
    if (lstrlenW(path) + lstrlenW(extra) >= ARRAYSIZE(path))
    {
        hr = E_INVALIDARG;
        goto done;
    }
    lstrcatW(path, extra);
    if (
        !(internet = InternetOpenW(NULL, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0)) ||
        !(connection = InternetConnectW(internet, host, components.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0)) ||
        !(request = HttpOpenRequestW(connection, L"POST", path, NULL, NULL, NULL,
                                     INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD |
                                     (components.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_FLAG_SECURE : 0), 0)) ||
        !HttpSendRequestW(request, headers, -1, (void *)body, body_len))
        goto error;
    if (!(buffer = malloc(capacity)))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    for (;;)
    {
        if (*response_len == capacity)
        {
            if (!(grown = realloc(buffer, capacity * 2)))
            {
                hr = E_OUTOFMEMORY;
                goto done;
            }
            buffer = grown;
            capacity *= 2;
        }
        if (!InternetReadFile(request, buffer + *response_len, capacity - *response_len, &read)) goto error;
        if (!read) break;
        *response_len += read;
    }
    *response = buffer;
    buffer = NULL;
    hr = S_OK;
    goto done;

error:
    hr = signer_last_error();
done:
    free(buffer);
    if (request) InternetCloseHandle(request);
    if (connection) InternetCloseHandle(connection);
    if (internet) InternetCloseHandle(internet);
    return hr;
}

static HRESULT signer_timestamp_message(BYTE **message, DWORD *message_len, const WCHAR *url,
                                        CRYPT_ATTRIBUTES *request)
{
    static char counter_sign[] = szOID_RSA_counterSign;
    static char data_oid[] = szOID_RSA_data;
    CMSG_CTRL_ADD_SIGNER_UNAUTH_ATTR_PARA add = { sizeof(add) };
    CRYPT_CONTENT_INFO content = { data_oid, { 0, NULL } };
    CRYPT_DATA_BLOB digest = { 0, NULL }, signer = { 0, NULL };
    CRYPT_ATTRIBUTE attr = { counter_sign, 1, &signer };
    CRYPT_ATTRIBUTES *unauth = NULL;
    HCRYPTMSG msg = NULL, response = NULL;
    BYTE *content_der = NULL, *attrs_der = NULL, *body = NULL, *req = NULL, *reply = NULL, *der = NULL;
    BYTE *out = NULL, *cert = NULL, *existing = NULL, *p;
    DWORD signers = 0, len, content_len = 0, attrs_len = 0, body_len, req_len, reply_len = 0, der_len = 0;
    DWORD count = 0, have = 0, i, j, existing_len;
    char *base64 = NULL;
    BOOL present;
    HRESULT hr;

    if (!(msg = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL)) ||
        !CryptMsgUpdate(msg, *message, *message_len, TRUE))
        goto error;
    len = sizeof(signers);
    if (!CryptMsgGetParam(msg, CMSG_SIGNER_COUNT_PARAM, 0, &signers, &len)) goto error;
    if (!signers)
    {
        hr = TRUST_E_NOSIGNATURE;
        goto done;
    }
    if (!CryptMsgGetParam(msg, CMSG_ENCRYPTED_DIGEST, 0, NULL, &digest.cbData)) goto error;
    if (!(digest.pbData = malloc(digest.cbData))) goto oom;
    if (!CryptMsgGetParam(msg, CMSG_ENCRYPTED_DIGEST, 0, digest.pbData, &digest.cbData)) goto error;

    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_OCTET_STRING, &digest, CRYPT_ENCODE_ALLOC_FLAG, NULL,
                             &content.Content.pbData, &content.Content.cbData) ||
        !CryptEncodeObjectEx(X509_ASN_ENCODING, PKCS_CONTENT_INFO, &content, CRYPT_ENCODE_ALLOC_FLAG, NULL,
                             &content_der, &content_len))
        goto error;
    if (request && request->cAttr &&
        !CryptEncodeObjectEx(X509_ASN_ENCODING, PKCS_ATTRIBUTES, request, CRYPT_ENCODE_ALLOC_FLAG, NULL,
                             &attrs_der, &attrs_len))
        goto error;
    body_len = sizeof(timestamp_request_oid) + attrs_len + content_len;
    if (!(body = malloc(body_len))) goto oom;
    memcpy(body, timestamp_request_oid, sizeof(timestamp_request_oid));
    if (attrs_len) memcpy(body + sizeof(timestamp_request_oid), attrs_der, attrs_len);
    memcpy(body + sizeof(timestamp_request_oid) + attrs_len, content_der, content_len);
    if (!(req = signer_der(0x30, body, body_len, &req_len))) goto error;
    if (!CryptBinaryToStringA(req, req_len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &len)) goto error;
    if (!(base64 = malloc(len))) goto oom;
    if (!CryptBinaryToStringA(req, req_len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, base64, &len)) goto error;

    if (FAILED(hr = signer_http_post(url, base64, strlen(base64), &reply, &reply_len))) goto done;
    if (CryptStringToBinaryA((char *)reply, reply_len, CRYPT_STRING_BASE64_ANY, NULL, &der_len, NULL, NULL) &&
        (der = malloc(der_len)) &&
        CryptStringToBinaryA((char *)reply, reply_len, CRYPT_STRING_BASE64_ANY, der, &der_len, NULL, NULL))
        p = der;
    else
    {
        p = reply;
        der_len = reply_len;
    }
    if (!(response = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL)) ||
        !CryptMsgUpdate(response, p, der_len, TRUE) ||
        !CryptMsgGetParam(response, CMSG_ENCODED_SIGNER, 0, NULL, &signer.cbData))
        goto error;
    if (!(signer.pbData = malloc(signer.cbData))) goto oom;
    if (!CryptMsgGetParam(response, CMSG_ENCODED_SIGNER, 0, signer.pbData, &signer.cbData)) goto error;

    if (CryptMsgGetParam(msg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, 0, NULL, &len) && (unauth = malloc(len)) &&
        CryptMsgGetParam(msg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, 0, unauth, &len))
    {
        for (i = unauth->cAttr; i > 0; i--)
        {
            CMSG_CTRL_DEL_SIGNER_UNAUTH_ATTR_PARA del = { sizeof(del), 0, i - 1 };

            if (!strcmp(unauth->rgAttr[i - 1].pszObjId, szOID_RSA_counterSign) &&
                !CryptMsgControl(msg, 0, CMSG_CTRL_DEL_SIGNER_UNAUTH_ATTR, &del))
                goto error;
        }
    }
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_ATTRIBUTE, &attr, CRYPT_ENCODE_ALLOC_FLAG,
                             NULL, &add.blob.pbData, &add.blob.cbData))
        goto error;
    add.dwSignerIndex = 0;
    if (!CryptMsgControl(msg, 0, CMSG_CTRL_ADD_SIGNER_UNAUTH_ATTR, &add)) goto error;

    len = sizeof(count);
    if (!CryptMsgGetParam(response, CMSG_CERT_COUNT_PARAM, 0, &count, &len)) count = 0;
    len = sizeof(have);
    if (!CryptMsgGetParam(msg, CMSG_CERT_COUNT_PARAM, 0, &have, &len)) have = 0;
    for (i = 0; i < count; i++)
    {
        CERT_BLOB blob;

        if (!CryptMsgGetParam(response, CMSG_CERT_PARAM, i, NULL, &blob.cbData)) goto error;
        free(cert);
        if (!(cert = malloc(blob.cbData))) goto oom;
        blob.pbData = cert;
        if (!CryptMsgGetParam(response, CMSG_CERT_PARAM, i, cert, &blob.cbData)) goto error;
        for (j = 0, present = FALSE; !present && j < have; j++)
        {
            if (!CryptMsgGetParam(msg, CMSG_CERT_PARAM, j, NULL, &existing_len)) goto error;
            free(existing);
            if (!(existing = malloc(existing_len))) goto oom;
            if (!CryptMsgGetParam(msg, CMSG_CERT_PARAM, j, existing, &existing_len)) goto error;
            present = existing_len == blob.cbData && !memcmp(existing, blob.pbData, blob.cbData);
        }
        if (!present && !CryptMsgControl(msg, 0, CMSG_CTRL_ADD_CERT, &blob)) goto error;
    }

    if (!CryptMsgGetParam(msg, CMSG_ENCODED_MESSAGE, 0, NULL, &len)) goto error;
    if (!(out = malloc(len))) goto oom;
    if (!CryptMsgGetParam(msg, CMSG_ENCODED_MESSAGE, 0, out, &len)) goto error;
    free(*message);
    *message = out;
    *message_len = len;
    out = NULL;
    hr = S_OK;
    goto done;

oom:
    hr = E_OUTOFMEMORY;
    goto done;
error:
    hr = signer_last_error();
done:
    free(out);
    free(cert);
    free(existing);
    free(unauth);
    LocalFree(add.blob.pbData);
    free(signer.pbData);
    if (response) CryptMsgClose(response);
    free(der);
    free(reply);
    free(base64);
    free(req);
    free(body);
    LocalFree(attrs_der);
    LocalFree(content_der);
    LocalFree(content.Content.pbData);
    free(digest.pbData);
    if (msg) CryptMsgClose(msg);
    return hr;
}

static HRESULT signer_open_subject(SIGNER_SUBJECT_INFO *subject, void *sip_data, struct signer_subject *s)
{
    const SIGNER_FILE_INFO *file_info;

    memset(s, 0, sizeof(*s));
    s->file = INVALID_HANDLE_VALUE;
    if (!subject || subject->cbSize != sizeof(*subject))
        return E_INVALIDARG;
    if (subject->dwSubjectChoice != SIGNER_SUBJECT_FILE)
    {
        FIXME("subject choice %lu not supported\n", subject->dwSubjectChoice);
        return E_NOTIMPL;
    }
    file_info = subject->pSignerFileInfo;
    if (!file_info || file_info->cbSize != sizeof(*file_info))
        return E_INVALIDARG;
    if (file_info->hFile && file_info->hFile != INVALID_HANDLE_VALUE)
        s->file = file_info->hFile;
    else
    {
        s->file = CreateFileW(file_info->pwszFileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, 0, NULL);
        if (s->file == INVALID_HANDLE_VALUE)
            return signer_last_error();
        s->close = TRUE;
    }
    s->sip.cbSize = sizeof(s->sip);
    if (!CryptSIPRetrieveSubjectGuid(file_info->pwszFileName, s->file, &s->guid) ||
        !CryptSIPLoad(&s->guid, 0, &s->sip))
        return signer_last_error();
    s->info.cbSize = sizeof(s->info);
    s->info.pgSubjectType = &s->guid;
    s->info.hFile = s->file;
    s->info.pwsFileName = file_info->pwszFileName;
    s->info.dwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    s->info.pClientData = sip_data;
    return S_OK;
}

static void signer_close_subject(struct signer_subject *s)
{
    if (s->close) CloseHandle(s->file);
}

static HRESULT signer_store(struct signer_subject *s, DWORD *index, BYTE *message, DWORD message_len)
{
    DWORD error = GetLastError();

    s->sip.pfRemove(&s->info, 0);
    SetLastError(error);
    if (!s->sip.pfPut(&s->info, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, index, message_len, message))
        return signer_last_error();
    return S_OK;
}

static HRESULT signer_make_context(SIGNER_CONTEXT **context, const BYTE *message, DWORD message_len)
{
    SIGNER_CONTEXT *ctx;

    if (!context) return S_OK;
    if (!(ctx = malloc(sizeof(*ctx) + message_len))) return E_OUTOFMEMORY;
    ctx->cbSize = sizeof(*ctx);
    ctx->cbBlob = message_len;
    ctx->pbBlob = (BYTE *)(ctx + 1);
    memcpy(ctx->pbBlob, message, message_len);
    *context = ctx;
    return S_OK;
}

static HRESULT signer_sign(DWORD flags, SIGNER_SUBJECT_INFO *subject, SIGNER_CERT *cert,
                           SIGNER_SIGNATURE_INFO *signature, SIGNER_PROVIDER_INFO *provider, const WCHAR *timestamp,
                           CRYPT_ATTRIBUTES *request, void *sip_data, SIGNER_CONTEXT **context)
{
    CMSG_SIGNED_ENCODE_INFO sign_info = { sizeof(sign_info) };
    CMSG_SIGNER_ENCODE_INFO signer = { sizeof(signer) };
    const CERT_CHAIN_CONTEXT *chain = NULL;
    const SIGNER_CERT_STORE_INFO *store;
    CRYPT_ATTRIBUTE auth[2], *rg_auth = NULL;
    CRYPT_ATTR_BLOB values[2] = { { 0 } };
    BYTE *content = NULL, *encoded = NULL;
    DWORD content_len = 0, encoded_len, cert_count = 0, auth_count = 0, keyspec = 0, i;
    CERT_BLOB *certs = NULL;
    HCRYPTMSG decoded = NULL, msg = NULL;
    HCRYPTPROV prov = 0;
    BOOL free_prov = FALSE;
    LPCSTR hash_oid, inner_oid = NULL;
    struct signer_subject s;
    HRESULT hr;

    if (!subject || subject->cbSize != sizeof(*subject) || !cert || cert->cbSize != sizeof(*cert) ||
        !signature || signature->cbSize != sizeof(*signature))
        return E_INVALIDARG;
    if (cert->dwCertChoice != SIGNER_CERT_STORE)
    {
        FIXME("cert choice %lu not supported\n", cert->dwCertChoice);
        return E_NOTIMPL;
    }
    store = cert->pCertStoreInfo;
    if (!store || store->cbSize != sizeof(*store) || !store->pSigningCert)
        return E_INVALIDARG;
    if (signature->dwAttrChoice == SIGNER_AUTHCODE_ATTR &&
        (!signature->pAttrAuthcode || signature->pAttrAuthcode->cbSize != sizeof(*signature->pAttrAuthcode) ||
         (signature->pAttrAuthcode->fCommercial && signature->pAttrAuthcode->fIndividual)))
        return E_INVALIDARG;
    if (signature->dwAttrChoice != SIGNER_NO_ATTR && signature->dwAttrChoice != SIGNER_AUTHCODE_ATTR)
        return E_INVALIDARG;
    if (!(hash_oid = CertAlgIdToOID(signature->algidHash)))
        return NTE_BAD_ALGID;

    if (FAILED(hr = signer_open_subject(subject, sip_data, &s))) goto done;
    if (signature->dwAttrChoice == SIGNER_AUTHCODE_ATTR &&
        FAILED(hr = signer_check_statement(store->pSigningCert, signature->pAttrAuthcode)))
        goto done;

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

    s.info.DigestAlgorithm.pszObjId = (char *)hash_oid;
    s.info.dwFlags = flags;
    if (!signer_get_content(&s.sip, &s.info, &inner_oid, &content, &content_len, &decoded))
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
    if (!CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, encoded, &encoded_len))
        goto error;
    if (timestamp && FAILED(hr = signer_timestamp_message(&encoded, &encoded_len, timestamp, request)))
        goto done;
    if (FAILED(hr = signer_store(&s, subject->pdwIndex, encoded, encoded_len)))
        goto done;
    hr = signer_make_context(context, encoded, encoded_len);
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
    signer_close_subject(&s);
    return hr;
}

static HRESULT signer_timestamp(SIGNER_SUBJECT_INFO *subject, const WCHAR *timestamp, CRYPT_ATTRIBUTES *request,
                                void *sip_data, SIGNER_CONTEXT **context)
{
    struct signer_subject s;
    DWORD encoding, len = 0;
    BYTE *message = NULL;
    HRESULT hr;

    if (!timestamp)
        return E_INVALIDARG;
    if (FAILED(hr = signer_open_subject(subject, sip_data, &s))) goto done;
    if (!s.sip.pfGet(&s.info, &encoding, subject->pdwIndex ? *subject->pdwIndex : 0, &len, NULL) ||
        !(message = malloc(len)) ||
        !s.sip.pfGet(&s.info, &encoding, subject->pdwIndex ? *subject->pdwIndex : 0, &len, message))
    {
        hr = message || !len ? TRUST_E_NOSIGNATURE : E_OUTOFMEMORY;
        goto done;
    }
    if (FAILED(hr = signer_timestamp_message(&message, &len, timestamp, request))) goto done;
    if (FAILED(hr = signer_store(&s, subject->pdwIndex, message, len))) goto done;
    hr = signer_make_context(context, message, len);

done:
    free(message);
    signer_close_subject(&s);
    return hr;
}

#endif
HRESULT WINAPI SignerSign(SIGNER_SUBJECT_INFO *subject, SIGNER_CERT *cert, SIGNER_SIGNATURE_INFO *signature,
        SIGNER_PROVIDER_INFO *provider, const WCHAR *timestamp, CRYPT_ATTRIBUTES *attrs, void *sip_data)
{
#ifdef __REACTOS__
    TRACE("%p %p %p %p %s %p %p\n", subject, cert, signature, provider, debugstr_w(timestamp), attrs, sip_data);
    return signer_sign(0, subject, cert, signature, provider, timestamp, attrs, sip_data, NULL);
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
#ifdef __REACTOS__
    TRACE("%lx %p %p %p %p %s %p %p %p\n", flags, subject_info, signer_cert, signature_info, provider_info,
          debugstr_w(http_time_stamp), request, sip_data, signer_context);
    return signer_sign(flags, subject_info, signer_cert, signature_info, provider_info, http_time_stamp, request,
                       sip_data, signer_context);
#else
    FIXME("%lx %p %p %p %p %s %p %p %p stub\n", flags, subject_info, signer_cert, signature_info, provider_info,
                    wine_dbgstr_w(http_time_stamp), request, sip_data, signer_cert);
    return E_NOTIMPL;
#endif
}

#ifdef __REACTOS__
HRESULT WINAPI SignerTimeStamp(SIGNER_SUBJECT_INFO *subject, const WCHAR *http_time_stamp,
                               CRYPT_ATTRIBUTES *request, void *sip_data)
{
    TRACE("%p %s %p %p\n", subject, debugstr_w(http_time_stamp), request, sip_data);
    return signer_timestamp(subject, http_time_stamp, request, sip_data, NULL);
}

HRESULT WINAPI SignerTimeStampEx(DWORD flags, SIGNER_SUBJECT_INFO *subject, const WCHAR *http_time_stamp,
                                 CRYPT_ATTRIBUTES *request, void *sip_data, SIGNER_CONTEXT **signer_context)
{
    TRACE("%lx %p %s %p %p %p\n", flags, subject, debugstr_w(http_time_stamp), request, sip_data, signer_context);
    return signer_timestamp(subject, http_time_stamp, request, sip_data, signer_context);
}

#endif
HRESULT WINAPI SignerFreeSignerContext(SIGNER_CONTEXT *signer_context)
{
#ifdef __REACTOS__
    free(signer_context);
#endif
    return S_OK;
}
