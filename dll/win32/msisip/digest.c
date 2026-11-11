/*
 * PROJECT:     LiberNT MSI SIP
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Digest of MSI compound files for Authenticode
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include "windef.h"
#include "winbase.h"
#include "wincrypt.h"
#include "wintrust.h"
#include "mssip.h"
#define COBJMACROS
#include "objbase.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(msisip);

static const GUID msi_subject = { 0x000c10f1, 0x0000, 0x0000, { 0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46 }};
static const WCHAR signature_name[] = L"\5DigitalSignature";
static const WCHAR signature_ex_name[] = L"\5MsiDigitalSignatureEx";
static const BYTE sip_info[] =
{
    0x30, 0x24, 0x02, 0x01, 0x02, 0x04, 0x10, 0xf1, 0x10, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00,
    0x02, 0x01, 0x00, 0x02, 0x01, 0x00
};

static int __cdecl compare_elements(const void *a, const void *b)
{
    const STATSTG *x = a, *y = b;
    size_t x_len = (wcslen(x->pwcsName) + 1) * sizeof(WCHAR);
    size_t y_len = (wcslen(y->pwcsName) + 1) * sizeof(WCHAR);
    int diff = memcmp(x->pwcsName, y->pwcsName, min(x_len, y_len));

    if (diff) return diff;
    return x_len < y_len ? -1 : x_len > y_len;
}

static void free_elements(STATSTG *elements, ULONG count)
{
    ULONG i;

    for (i = 0; i < count; i++) CoTaskMemFree(elements[i].pwcsName);
    free(elements);
}

static HRESULT list_elements(IStorage *stg, BOOL root, STATSTG **elements, ULONG *count)
{
    IEnumSTATSTG *enumerator;
    STATSTG stat, *grown;
    ULONG fetched, capacity = 0;
    HRESULT hr;

    *elements = NULL;
    *count = 0;
    hr = IStorage_EnumElements(stg, 0, NULL, 0, &enumerator);
    if (FAILED(hr)) return hr;
    while ((hr = IEnumSTATSTG_Next(enumerator, 1, &stat, &fetched)) == S_OK && fetched == 1)
    {
        if (root && (!wcscmp(stat.pwcsName, signature_name) || !wcscmp(stat.pwcsName, signature_ex_name)))
        {
            CoTaskMemFree(stat.pwcsName);
            continue;
        }
        if (*count == capacity)
        {
            capacity = capacity ? capacity * 2 : 16;
            if (!(grown = realloc(*elements, capacity * sizeof(**elements))))
            {
                CoTaskMemFree(stat.pwcsName);
                hr = E_OUTOFMEMORY;
                break;
            }
            *elements = grown;
        }
        (*elements)[(*count)++] = stat;
    }
    IEnumSTATSTG_Release(enumerator);
    if (FAILED(hr))
    {
        free_elements(*elements, *count);
        *elements = NULL;
        *count = 0;
        return hr;
    }
    qsort(*elements, *count, sizeof(**elements), compare_elements);
    return S_OK;
}

static BOOL hash_entry(HCRYPTHASH hash, const STATSTG *stat, BOOL root)
{
    if (!root && !CryptHashData(hash, (const BYTE *)stat->pwcsName, wcslen(stat->pwcsName) * sizeof(WCHAR), 0))
        return FALSE;
    if (stat->type == STGTY_STREAM)
    {
        if (!CryptHashData(hash, (const BYTE *)&stat->cbSize.LowPart, sizeof(stat->cbSize.LowPart), 0))
            return FALSE;
    }
    else if (!CryptHashData(hash, (const BYTE *)&stat->clsid, sizeof(stat->clsid), 0))
        return FALSE;
    if (!CryptHashData(hash, (const BYTE *)&stat->grfStateBits, sizeof(stat->grfStateBits), 0))
        return FALSE;
    if (root) return TRUE;
    return CryptHashData(hash, (const BYTE *)&stat->ctime, sizeof(stat->ctime), 0) &&
           CryptHashData(hash, (const BYTE *)&stat->mtime, sizeof(stat->mtime), 0);
}

static HRESULT hash_metadata(IStorage *stg, const STATSTG *self, BOOL root, HCRYPTHASH hash)
{
    STATSTG *elements;
    IStorage *child;
    ULONG count, i;
    HRESULT hr;

    if (!hash_entry(hash, self, root)) return HRESULT_FROM_WIN32(GetLastError());
    hr = list_elements(stg, root, &elements, &count);
    if (FAILED(hr)) return hr;
    for (i = 0; SUCCEEDED(hr) && i < count; i++)
    {
        if (elements[i].type == STGTY_STREAM)
        {
            if (!hash_entry(hash, &elements[i], FALSE)) hr = HRESULT_FROM_WIN32(GetLastError());
        }
        else if (elements[i].type == STGTY_STORAGE)
        {
            hr = IStorage_OpenStorage(stg, elements[i].pwcsName, NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, 0, &child);
            if (SUCCEEDED(hr))
            {
                hr = hash_metadata(child, &elements[i], FALSE, hash);
                IStorage_Release(child);
            }
        }
    }
    free_elements(elements, count);
    return hr;
}

static HRESULT hash_stream(IStorage *stg, const WCHAR *name, HCRYPTHASH hash)
{
    IStream *stm;
    BYTE *buffer;
    ULONG read;
    HRESULT hr;

    if (!(buffer = malloc(0x10000))) return E_OUTOFMEMORY;
    hr = IStorage_OpenStream(stg, name, NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stm);
    if (SUCCEEDED(hr))
    {
        while (SUCCEEDED(hr = IStream_Read(stm, buffer, 0x10000, &read)) && read)
        {
            if (!CryptHashData(hash, buffer, read, 0))
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
        }
        IStream_Release(stm);
    }
    free(buffer);
    return hr;
}

static HRESULT hash_content(IStorage *stg, const CLSID *clsid, BOOL root, HCRYPTHASH hash)
{
    STATSTG *elements, stat;
    IStorage *child;
    ULONG count, i;
    HRESULT hr;

    hr = list_elements(stg, root, &elements, &count);
    if (FAILED(hr)) return hr;
    for (i = 0; SUCCEEDED(hr) && i < count; i++)
    {
        if (elements[i].type == STGTY_STREAM)
            hr = hash_stream(stg, elements[i].pwcsName, hash);
        else if (elements[i].type == STGTY_STORAGE)
        {
            hr = IStorage_OpenStorage(stg, elements[i].pwcsName, NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, 0, &child);
            if (SUCCEEDED(hr))
            {
                hr = IStorage_Stat(child, &stat, STATFLAG_NONAME);
                if (SUCCEEDED(hr)) hr = hash_content(child, &stat.clsid, FALSE, hash);
                IStorage_Release(child);
            }
        }
    }
    free_elements(elements, count);
    if (SUCCEEDED(hr) && !CryptHashData(hash, (const BYTE *)clsid, sizeof(*clsid), 0))
        hr = HRESULT_FROM_WIN32(GetLastError());
    return hr;
}

static BOOL finish_hash(HCRYPTHASH hash, HRESULT hr, BYTE *digest, DWORD len)
{
    if (FAILED(hr))
    {
        SetLastError(hr);
        return FALSE;
    }
    return CryptGetHashParam(hash, HP_HASHVAL, digest, &len, 0);
}

static BOOL metadata_digest(IStorage *stg, HCRYPTPROV prov, ALG_ID alg, BYTE *digest, DWORD len)
{
    HCRYPTHASH hash;
    STATSTG stat;
    HRESULT hr;
    BOOL ret;

    if (!CryptCreateHash(prov, alg, 0, 0, &hash)) return FALSE;
    hr = IStorage_Stat(stg, &stat, STATFLAG_NONAME);
    if (SUCCEEDED(hr)) hr = hash_metadata(stg, &stat, TRUE, hash);
    ret = finish_hash(hash, hr, digest, len);
    CryptDestroyHash(hash);
    return ret;
}

static BOOL content_digest(IStorage *stg, HCRYPTPROV prov, ALG_ID alg, const BYTE *prefix, DWORD prefix_len,
                           BYTE *digest, DWORD len)
{
    HCRYPTHASH hash;
    STATSTG stat;
    HRESULT hr;
    BOOL ret;

    if (!CryptCreateHash(prov, alg, 0, 0, &hash)) return FALSE;
    if (prefix_len && !CryptHashData(hash, prefix, prefix_len, 0))
    {
        CryptDestroyHash(hash);
        return FALSE;
    }
    hr = IStorage_Stat(stg, &stat, STATFLAG_NONAME);
    if (SUCCEEDED(hr)) hr = hash_content(stg, &stat.clsid, TRUE, hash);
    ret = finish_hash(hash, hr, digest, len);
    CryptDestroyHash(hash);
    return ret;
}

static BOOL begin_digest(SIP_SUBJECTINFO *info, const char *oid, ALG_ID *alg, HCRYPTPROV *prov, DWORD *hash_len)
{
    HCRYPTHASH hash;
    DWORD len = sizeof(*hash_len);
    BOOL ret;

    if (!IsEqualGUID(info->pgSubjectType, &msi_subject))
    {
        SetLastError(TRUST_E_SUBJECT_FORM_UNKNOWN);
        return FALSE;
    }
    if (!(*alg = CertOIDToAlgId(oid)) || GET_ALG_CLASS(*alg) != ALG_CLASS_HASH)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    *prov = info->hProv;
    if (!*prov && !CryptAcquireContextW(prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return FALSE;
    ret = CryptCreateHash(*prov, *alg, 0, 0, &hash);
    if (ret)
    {
        ret = CryptGetHashParam(hash, HP_HASHSIZE, (BYTE *)hash_len, &len, 0);
        CryptDestroyHash(hash);
    }
    if (!ret && *prov != info->hProv)
    {
        CryptReleaseContext(*prov, 0);
        *prov = 0;
    }
    return ret;
}

static void end_digest(SIP_SUBJECTINFO *info, HCRYPTPROV prov)
{
    if (prov && prov != info->hProv) CryptReleaseContext(prov, 0);
}

static BOOL create_indirect(SIP_SUBJECTINFO *info, HCRYPTPROV prov, ALG_ID alg, DWORD hash_len, DWORD *data_len,
                            SIP_INDIRECT_DATA *data)
{
    DWORD oid_len, size, written;
    BYTE metadata[64], *p;
    IStorage *stg;
    IStream *stm;
    HRESULT hr;
    BOOL ret = FALSE;

    if (hash_len > sizeof(metadata))
    {
        SetLastError(NTE_BAD_ALGID);
        return FALSE;
    }

    oid_len = strlen(info->DigestAlgorithm.pszObjId) + 1;
    size = sizeof(*data) + sizeof(SPC_SIGINFO_OBJID) + sizeof(sip_info) + oid_len + hash_len;
    if (!data)
    {
        *data_len = size;
        return TRUE;
    }
    if (*data_len < size)
    {
        *data_len = size;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    hr = StgOpenStorage(info->pwsFileName, NULL, STGM_DIRECT | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, NULL, 0, &stg);
    if (FAILED(hr))
    {
        SetLastError(hr);
        return FALSE;
    }
    p = (BYTE *)(data + 1);
    if (metadata_digest(stg, prov, alg, metadata, hash_len))
    {
        hr = IStorage_CreateStream(stg, signature_ex_name, STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &stm);
        if (SUCCEEDED(hr))
        {
            hr = IStream_Write(stm, metadata, hash_len, &written);
            if (SUCCEEDED(hr) && written != hash_len) hr = STG_E_MEDIUMFULL;
            IStream_Release(stm);
        }
        if (FAILED(hr))
            SetLastError(hr);
        else
            ret = content_digest(stg, prov, alg, metadata, hash_len,
                                 p + sizeof(SPC_SIGINFO_OBJID) + sizeof(sip_info) + oid_len, hash_len);
    }
    IStorage_Release(stg);
    if (!ret) return FALSE;

    data->Data.pszObjId = (char *)p;
    memcpy(p, SPC_SIGINFO_OBJID, sizeof(SPC_SIGINFO_OBJID));
    p += sizeof(SPC_SIGINFO_OBJID);
    data->Data.Value.cbData = sizeof(sip_info);
    data->Data.Value.pbData = p;
    memcpy(p, sip_info, sizeof(sip_info));
    p += sizeof(sip_info);
    data->DigestAlgorithm.pszObjId = (char *)p;
    memcpy(p, info->DigestAlgorithm.pszObjId, oid_len);
    p += oid_len;
    data->DigestAlgorithm.Parameters.cbData = 0;
    data->DigestAlgorithm.Parameters.pbData = NULL;
    data->Digest.cbData = hash_len;
    data->Digest.pbData = p;
    return TRUE;
}

static BOOL verify_indirect(SIP_SUBJECTINFO *info, SIP_INDIRECT_DATA *data, DWORD sip_version, HCRYPTPROV prov,
                            ALG_ID alg, DWORD hash_len)
{
    BYTE metadata[64], stored[65], digest[64];
    DWORD stored_len = 0, prefix_len = 0;
    IStorage *stg;
    IStream *stm;
    HRESULT hr;
    BOOL ret = FALSE;

    if (hash_len > sizeof(digest))
    {
        SetLastError(NTE_BAD_ALGID);
        return FALSE;
    }

    hr = StgOpenStorage(info->pwsFileName, NULL, STGM_DIRECT | STGM_READ | STGM_SHARE_DENY_WRITE, NULL, 0, &stg);
    if (FAILED(hr))
    {
        SetLastError(hr);
        return FALSE;
    }
    hr = IStorage_OpenStream(stg, signature_ex_name, NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stm);
    if (SUCCEEDED(hr))
    {
        hr = IStream_Read(stm, stored, sizeof(stored), &stored_len);
        IStream_Release(stm);
        if (FAILED(hr))
            SetLastError(hr);
        else if (metadata_digest(stg, prov, alg, metadata, hash_len))
        {
            if (stored_len != hash_len || memcmp(stored, metadata, hash_len))
                SetLastError(TRUST_E_BAD_DIGEST);
            else
            {
                prefix_len = hash_len;
                ret = TRUE;
            }
        }
    }
    else if (hr == STG_E_FILENOTFOUND)
    {
        if (sip_version >= 2)
            SetLastError(ERROR_BAD_FORMAT);
        else
            ret = TRUE;
    }
    else
        SetLastError(hr);

    if (ret)
    {
        ret = content_digest(stg, prov, alg, metadata, prefix_len, digest, hash_len);
        if (ret && (data->Digest.cbData != hash_len || memcmp(data->Digest.pbData, digest, hash_len)))
        {
            SetLastError(TRUST_E_BAD_DIGEST);
            ret = FALSE;
        }
    }
    IStorage_Release(stg);
    return ret;
}

/***********************************************************************
 *              MsiSIPCreateIndirectData (MSISIP.@)
 */
BOOL WINAPI MsiSIPCreateIndirectData(SIP_SUBJECTINFO *pSubjectInfo, DWORD *pcbIndirectData,
 SIP_INDIRECT_DATA *pIndirectData)
{
    HCRYPTPROV prov = 0;
    DWORD hash_len;
    ALG_ID alg;
    BOOL ret;

    TRACE("(%p %p %p)\n", pSubjectInfo, pcbIndirectData, pIndirectData);

    if (!pSubjectInfo || !pcbIndirectData || !pSubjectInfo->pgSubjectType || !pSubjectInfo->DigestAlgorithm.pszObjId)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!begin_digest(pSubjectInfo, pSubjectInfo->DigestAlgorithm.pszObjId, &alg, &prov, &hash_len))
        return FALSE;
    ret = create_indirect(pSubjectInfo, prov, alg, hash_len, pcbIndirectData, pIndirectData);
    end_digest(pSubjectInfo, prov);
    return ret;
}

/***********************************************************************
 *              MsiSIPVerifyIndirectData (MSISIP.@)
 */
BOOL WINAPI MsiSIPVerifyIndirectData(SIP_SUBJECTINFO *pSubjectInfo, SIP_INDIRECT_DATA *pIndirectData)
{
    SPC_SIGINFO sig_info;
    HCRYPTPROV prov = 0;
    DWORD hash_len, size;
    ALG_ID alg;
    BOOL ret;

    TRACE("(%p %p)\n", pSubjectInfo, pIndirectData);

    if (!pSubjectInfo || !pIndirectData || !pSubjectInfo->pgSubjectType || !pIndirectData->DigestAlgorithm.pszObjId)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    size = sizeof(sig_info);
    if (!pIndirectData->Data.pszObjId || strcmp(pIndirectData->Data.pszObjId, SPC_SIGINFO_OBJID) ||
        !CryptDecodeObject(X509_ASN_ENCODING, SPC_SIGINFO_OBJID, pIndirectData->Data.Value.pbData,
                           pIndirectData->Data.Value.cbData, 0, &sig_info, &size))
    {
        WARN("unexpected indirect data %s\n", debugstr_a(pIndirectData->Data.pszObjId));
        SetLastError(TRUST_E_SUBJECT_FORM_UNKNOWN);
        return FALSE;
    }
    if (!begin_digest(pSubjectInfo, pIndirectData->DigestAlgorithm.pszObjId, &alg, &prov, &hash_len))
        return FALSE;
    ret = verify_indirect(pSubjectInfo, pIndirectData, sig_info.dwSipVersion, prov, alg, hash_len);
    end_digest(pSubjectInfo, prov);
    return ret;
}
