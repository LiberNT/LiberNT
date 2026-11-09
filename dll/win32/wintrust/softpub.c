/*
 * Copyright 2007 Juan Lang
 * Copyright 2016 Mark Jansen
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
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "wintrust.h"
#include "mssip.h"
#ifdef __REACTOS__
#include "mscat.h"
#include "wininet.h"
#include "wintrust_priv.h"
#endif
#include "softpub.h"
#include "winnls.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wintrust);

HRESULT WINAPI SoftpubDefCertInit(CRYPT_PROVIDER_DATA *data)
{
    HRESULT ret = S_FALSE;

    TRACE("(%p)\n", data);

    if (data->padwTrustStepErrors &&
     !data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_WVTINIT])
        ret = S_OK;
    TRACE("returning %08lx\n", ret);
    return ret;
}

HRESULT WINAPI SoftpubInitialize(CRYPT_PROVIDER_DATA *data)
{
    HRESULT ret = S_FALSE;

    TRACE("(%p)\n", data);

    if (data->padwTrustStepErrors &&
     !data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_WVTINIT])
        ret = S_OK;
#ifdef __REACTOS__
    if (!data->pszUsageOID)
        data->pszUsageOID = (char *)szOID_PKIX_KP_CODE_SIGNING;
#endif
    TRACE("returning %08lx\n", ret);
    return ret;
}

#ifndef __REACTOS__
HRESULT WINAPI DriverInitializePolicy(CRYPT_PROVIDER_DATA *data)
{
    FIXME("stub\n");
    return S_OK;
}

HRESULT WINAPI DriverCleanupPolicy(CRYPT_PROVIDER_DATA *data)
{
    FIXME("stub\n");
    return S_OK;
}

HRESULT WINAPI DriverFinalPolicy(CRYPT_PROVIDER_DATA *data)
{
    FIXME("stub\n");
    return S_OK;
}
#endif

/* Assumes data->pWintrustData->u.pFile exists.  Makes sure a file handle is
 * open for the file.
 */
static DWORD SOFTPUB_OpenFile(CRYPT_PROVIDER_DATA *data)
{
    DWORD err = ERROR_SUCCESS;

    /* PSDK implies that all values should be initialized to NULL, so callers
     * typically have hFile as NULL rather than INVALID_HANDLE_VALUE.  Check
     * for both.
     */
    if (!data->pWintrustData->pFile->hFile ||
     data->pWintrustData->pFile->hFile == INVALID_HANDLE_VALUE)
    {
        data->pWintrustData->pFile->hFile =
            CreateFileW(data->pWintrustData->pFile->pcwszFilePath, GENERIC_READ,
          FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (data->pWintrustData->pFile->hFile != INVALID_HANDLE_VALUE)
            data->fOpenedFile = TRUE;
        else
#ifdef __REACTOS__
            err = CRYPT_E_FILE_ERROR;
#else
            err = GetLastError();
#endif
    }
    if (!err)
        GetFileTime(data->pWintrustData->pFile->hFile, &data->sftSystemTime,
         NULL, NULL);
    TRACE("returning %ld\n", err);
    return err;
}

/* Assumes data->pWintrustData->pFile exists.  Sets data->pPDSip->gSubject to
 * the file's subject GUID.
 */
static DWORD SOFTPUB_GetFileSubject(CRYPT_PROVIDER_DATA *data)
{
    DWORD err = ERROR_SUCCESS;

    if (!WVT_ISINSTRUCT(WINTRUST_FILE_INFO,
     data->pWintrustData->pFile->cbStruct, pgKnownSubject) ||
     !data->pWintrustData->pFile->pgKnownSubject)
    {
        if (!CryptSIPRetrieveSubjectGuid(
         data->pWintrustData->pFile->pcwszFilePath,
         data->pWintrustData->pFile->hFile,
         &data->pPDSip->gSubject))
        {
            LARGE_INTEGER fileSize;
            DWORD sipError = GetLastError();

            /* Special case for empty files: the error is expected to be
             * TRUST_E_SUBJECT_FORM_UNKNOWN, rather than whatever
             * CryptSIPRetrieveSubjectGuid returns.
             */
#ifdef __REACTOS__
            UNREFERENCED_PARAMETER(fileSize);
            UNREFERENCED_PARAMETER(sipError);
            err = TRUST_E_SUBJECT_FORM_UNKNOWN;
#else
            if (GetFileSizeEx(data->pWintrustData->pFile->hFile, &fileSize)
             && !fileSize.QuadPart)
                err = TRUST_E_SUBJECT_FORM_UNKNOWN;
            else
                err = sipError;
#endif
        }
    }
    else
        data->pPDSip->gSubject = *data->pWintrustData->pFile->pgKnownSubject;
    TRACE("returning %ld\n", err);
    return err;
}

/* Assumes data->pPDSip exists, and its gSubject member set.
 * Allocates data->pPDSip->pSip and loads it, if possible.
 */
static DWORD SOFTPUB_GetSIP(CRYPT_PROVIDER_DATA *data)
{
    DWORD err = ERROR_SUCCESS;

    data->pPDSip->pSip = data->psPfns->pfnAlloc(sizeof(SIP_DISPATCH_INFO));
    if (data->pPDSip->pSip)
    {
        if (!CryptSIPLoad(&data->pPDSip->gSubject, 0, data->pPDSip->pSip))
            err = GetLastError();
    }
    else
        err = ERROR_OUTOFMEMORY;
    TRACE("returning %ld\n", err);
    return err;
}

/* Assumes data->pPDSip has been loaded, and data->pPDSip->pSip allocated.
 * Calls data->pPDSip->pSip->pfGet to construct data->hMsg.
 */
static DWORD SOFTPUB_GetMessageFromFile(CRYPT_PROVIDER_DATA *data, HANDLE file,
 LPCWSTR filePath)
{
    DWORD err = ERROR_SUCCESS;
    BOOL ret;
    LPBYTE buf = NULL;
    DWORD size = 0;

    data->pPDSip->psSipSubjectInfo =
     data->psPfns->pfnAlloc(sizeof(SIP_SUBJECTINFO));
    if (!data->pPDSip->psSipSubjectInfo)
        return ERROR_OUTOFMEMORY;

    data->pPDSip->psSipSubjectInfo->cbSize = sizeof(SIP_SUBJECTINFO);
    data->pPDSip->psSipSubjectInfo->pgSubjectType = &data->pPDSip->gSubject;
    data->pPDSip->psSipSubjectInfo->hFile = file;
    data->pPDSip->psSipSubjectInfo->pwsFileName = filePath;
    data->pPDSip->psSipSubjectInfo->hProv = data->hProv;
    ret = data->pPDSip->pSip->pfGet(data->pPDSip->psSipSubjectInfo,
     &data->dwEncoding, 0, &size, 0);
    if (!ret)
    {
#ifdef __REACTOS__
        data->padwTrustStepErrors[TRUSTERROR_STEP_SIP] = GetLastError();
        data->padwTrustStepErrors[TRUSTERROR_STEP_MSG_SIGNERCOUNT] = GetLastError();
#endif
        return TRUST_E_NOSIGNATURE;
    }

    buf = data->psPfns->pfnAlloc(size);
    if (!buf)
        return ERROR_OUTOFMEMORY;

    ret = data->pPDSip->pSip->pfGet(data->pPDSip->psSipSubjectInfo,
     &data->dwEncoding, 0, &size, buf);
    if (ret)
    {
        data->hMsg = CryptMsgOpenToDecode(data->dwEncoding, 0, 0, data->hProv,
         NULL, NULL);
        if (data->hMsg)
        {
            ret = CryptMsgUpdate(data->hMsg, buf, size, TRUE);
            if (!ret)
                err = GetLastError();
        }
    }
    else
        err = GetLastError();

    data->psPfns->pfnFree(buf);
    TRACE("returning %ld\n", err);
    return err;
}

static BOOL hash_file_data( HANDLE file, DWORD start, DWORD end, HCRYPTHASH hash )
{
    DWORD bytes_read, size = end - start;
    DWORD buffer_size = min( size, 1024*1024 );
    BYTE *buffer = malloc( buffer_size );

    if (!buffer) return FALSE;
    SetFilePointer( file, start, NULL, FILE_BEGIN );
    while (size)
    {
        if (!ReadFile( file, buffer, min( buffer_size, size ), &bytes_read, NULL )) break;
        if (!bytes_read) break;
        if (!CryptHashData( hash, buffer, bytes_read, 0 )) break;
        size -= bytes_read;
    }
    free( buffer );
    return !size;
}

/* See https://www.cs.auckland.ac.nz/~pgut001/pubs/authenticode.txt
 * for details about the hashing.
 */
static BOOL SOFTPUB_HashPEFile(HANDLE file, HCRYPTHASH hash)
{
    DWORD checksum, security_dir;
    IMAGE_DOS_HEADER dos_header;
    union
    {
        IMAGE_NT_HEADERS32 nt32;
        IMAGE_NT_HEADERS64 nt64;
    } nt_header;
    IMAGE_DATA_DIRECTORY secdir;
    LARGE_INTEGER file_size;
    DWORD bytes_read;
    BOOL ret;

    if (!GetFileSizeEx(file, &file_size))
        return FALSE;

    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    ret = ReadFile(file, &dos_header, sizeof(dos_header), &bytes_read, NULL);
    if (!ret || bytes_read != sizeof(dos_header))
        return FALSE;

    if (dos_header.e_magic != IMAGE_DOS_SIGNATURE)
        return FALSE;
    if (dos_header.e_lfanew >= 256 * 1024 * 1024) /* see RtlImageNtHeaderEx */
        return FALSE;
    if (dos_header.e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader.MajorLinkerVersion) > file_size.QuadPart)
        return FALSE;

    SetFilePointer(file, dos_header.e_lfanew, NULL, FILE_BEGIN);
    ret = ReadFile(file, &nt_header, sizeof(nt_header), &bytes_read, NULL);
    if (!ret || bytes_read < FIELD_OFFSET(IMAGE_NT_HEADERS32, OptionalHeader.Magic) +
                             sizeof(nt_header.nt32.OptionalHeader.Magic))
        return FALSE;

    if (nt_header.nt32.Signature != IMAGE_NT_SIGNATURE)
        return FALSE;

    if (nt_header.nt32.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        if (bytes_read < sizeof(nt_header.nt32))
            return FALSE;

        checksum     = dos_header.e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS32, OptionalHeader.CheckSum);
        security_dir = dos_header.e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS32, OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY]);
        secdir       = nt_header.nt32.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
    }
    else if (nt_header.nt32.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        if (bytes_read < sizeof(nt_header.nt64))
            return FALSE;

        checksum     = dos_header.e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS64, OptionalHeader.CheckSum);
        security_dir = dos_header.e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS64, OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY]);
        secdir       = nt_header.nt64.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
    }
    else
        return FALSE;

    if (secdir.VirtualAddress < security_dir + sizeof(IMAGE_DATA_DIRECTORY))
        return FALSE;
    if (secdir.VirtualAddress > file_size.QuadPart)
        return FALSE;
    if (secdir.VirtualAddress + secdir.Size != file_size.QuadPart)
        return FALSE;

    if (!hash_file_data( file, 0, checksum, hash )) return FALSE;
    if (!hash_file_data( file, checksum + sizeof(DWORD), security_dir, hash )) return FALSE;
    if (!hash_file_data( file, security_dir + sizeof(IMAGE_DATA_DIRECTORY), secdir.VirtualAddress, hash ))
        return FALSE;

    return TRUE;
}

#ifdef __REACTOS__
static DWORD SOFTPUB_VerifySubjectHash(CRYPT_PROVIDER_DATA *data)
{
    DWORD err;

    if (!data->pPDSip->pSip || !data->pPDSip->pSip->pfVerify || !data->pPDSip->psSipSubjectInfo)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    if (data->pPDSip->pSip->pfVerify(data->pPDSip->psSipSubjectInfo,
                                     (SIP_INDIRECT_DATA *)data->pPDSip->psIndirectData))
        return ERROR_SUCCESS;
    err = GetLastError();
    return err ? err : TRUST_E_BAD_DIGEST;
}

#endif
static DWORD SOFTPUB_VerifyImageHash(CRYPT_PROVIDER_DATA *data, HANDLE file)
{
    SPC_INDIRECT_DATA_CONTENT *indirect = (SPC_INDIRECT_DATA_CONTENT *)data->pPDSip->psIndirectData;
    DWORD err, hash_size, length;
    BYTE *hash_data;
    BOOL release_prov = FALSE;
    HCRYPTPROV prov = data->hProv;
    HCRYPTHASH hash = 0;
    ALG_ID algID;

    if (((ULONG_PTR)indirect->Data.pszObjId >> 16) == 0 ||
        strcmp(indirect->Data.pszObjId, SPC_PE_IMAGE_DATA_OBJID))
    {
#ifdef __REACTOS__
        return SOFTPUB_VerifySubjectHash(data);
#else
        FIXME("Cannot verify hash for pszObjId=%s\n", debugstr_a(indirect->Data.pszObjId));
        return ERROR_SUCCESS;
#endif
    }

#ifdef __REACTOS__
    algID = CertOIDToAlgId(indirect->DigestAlgorithm.pszObjId);
    if (GET_ALG_CLASS(algID) != ALG_CLASS_HASH)
        return TRUST_E_BAD_DIGEST;
#else
    if (!(algID = CertOIDToAlgId(indirect->DigestAlgorithm.pszObjId)))
        return TRUST_E_SYSTEM_ERROR; /* FIXME */
#endif

    if (!prov)
    {
        if (!CryptAcquireContextW(&prov, NULL, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            return GetLastError();
        release_prov = TRUE;
    }

    if (!CryptCreateHash(prov, algID, 0, 0, &hash))
    {
        err = GetLastError();
        goto done;
    }

    if (!SOFTPUB_HashPEFile(file, hash))
    {
        err = TRUST_E_NOSIGNATURE;
        goto done;
    }

    length = sizeof(hash_size);
    if (!CryptGetHashParam(hash, HP_HASHSIZE, (BYTE *)&hash_size, &length, 0))
    {
        err = GetLastError();
        goto done;
    }

    if (!(hash_data = data->psPfns->pfnAlloc(hash_size)))
    {
        err = ERROR_OUTOFMEMORY;
        goto done;
    }

    if (!CryptGetHashParam(hash, HP_HASHVAL, hash_data, &hash_size, 0))
    {
        err = GetLastError();
        data->psPfns->pfnFree(hash_data);
        goto done;
    }

    err = (hash_size == indirect->Digest.cbData &&
           !memcmp(hash_data, indirect->Digest.pbData, hash_size)) ? S_OK : TRUST_E_BAD_DIGEST;
    data->psPfns->pfnFree(hash_data);

done:
    if (hash)
        CryptDestroyHash(hash);
    if (release_prov)
        CryptReleaseContext(prov, 0);
    return err;
}


static DWORD SOFTPUB_CreateStoreFromMessage(CRYPT_PROVIDER_DATA *data)
{
    DWORD err = ERROR_SUCCESS;
    HCERTSTORE store;

    store = CertOpenStore(CERT_STORE_PROV_MSG, data->dwEncoding,
     data->hProv, CERT_STORE_NO_CRYPT_RELEASE_FLAG, data->hMsg);
    if (store)
    {
        if (!data->psPfns->pfnAddStore2Chain(data, store))
            err = GetLastError();
        CertCloseStore(store, 0);
    }
    else
        err = GetLastError();
    TRACE("returning %ld\n", err);
    return err;
}

static DWORD SOFTPUB_DecodeInnerContent(CRYPT_PROVIDER_DATA *data)
{
    BOOL ret;
    DWORD size, err = ERROR_SUCCESS;
    LPSTR oid = NULL;
    LPBYTE buf = NULL;

    ret = CryptMsgGetParam(data->hMsg, CMSG_INNER_CONTENT_TYPE_PARAM, 0, NULL,
     &size);
    if (!ret)
    {
        err = GetLastError();
        goto error;
    }
    oid = data->psPfns->pfnAlloc(size);
    if (!oid)
    {
        err = ERROR_OUTOFMEMORY;
        goto error;
    }
    ret = CryptMsgGetParam(data->hMsg, CMSG_INNER_CONTENT_TYPE_PARAM, 0, oid,
     &size);
    if (!ret)
    {
        err = GetLastError();
        goto error;
    }
    ret = CryptMsgGetParam(data->hMsg, CMSG_CONTENT_PARAM, 0, NULL, &size);
    if (!ret)
    {
        err = GetLastError();
        goto error;
    }
    buf = data->psPfns->pfnAlloc(size);
    if (!buf)
    {
        err = ERROR_OUTOFMEMORY;
        goto error;
    }
    ret = CryptMsgGetParam(data->hMsg, CMSG_CONTENT_PARAM, 0, buf, &size);
    if (!ret)
    {
        err = GetLastError();
        goto error;
    }
    ret = CryptDecodeObject(data->dwEncoding, oid, buf, size, 0, NULL, &size);
    if (!ret)
    {
        err = GetLastError();
        goto error;
    }
    data->pPDSip->psIndirectData = data->psPfns->pfnAlloc(size);
    if (!data->pPDSip->psIndirectData)
    {
        err = ERROR_OUTOFMEMORY;
        goto error;
    }
    ret = CryptDecodeObject(data->dwEncoding, oid, buf, size, 0,
     data->pPDSip->psIndirectData, &size);
    if (!ret)
        err = GetLastError();

error:
    TRACE("returning %ld\n", err);
    data->psPfns->pfnFree(oid);
    data->psPfns->pfnFree(buf);
    return err;
}

#ifdef __REACTOS__
static BOOL SOFTPUB_HasIndirectData(CRYPT_PROVIDER_DATA *data)
{
    char oid[sizeof(SPC_INDIRECT_DATA_OBJID)];
    DWORD size = sizeof(oid);

    return CryptMsgGetParam(data->hMsg, CMSG_INNER_CONTENT_TYPE_PARAM, 0, oid, &size) &&
           !strcmp(oid, SPC_INDIRECT_DATA_OBJID);
}

#endif
static DWORD SOFTPUB_LoadCertMessage(CRYPT_PROVIDER_DATA *data)
{
    DWORD err = ERROR_SUCCESS;

    if (data->pWintrustData->pCert &&
     WVT_IS_CBSTRUCT_GT_MEMBEROFFSET(WINTRUST_CERT_INFO,
     data->pWintrustData->pCert->cbStruct, psCertContext))
    {
        if (data->psPfns)
        {
            CRYPT_PROVIDER_SGNR signer = { sizeof(signer), { 0 } };
            DWORD i;
            BOOL ret;

            /* Add a signer with nothing but the time to verify, so we can
             * add a cert to it
             */
            if (WVT_ISINSTRUCT(WINTRUST_CERT_INFO,
             data->pWintrustData->pCert->cbStruct, psftVerifyAsOf) &&
             data->pWintrustData->pCert->psftVerifyAsOf)
#ifdef __REACTOS__
                signer.sftVerifyAsOf = *data->pWintrustData->pCert->psftVerifyAsOf;
#else
                data->sftSystemTime = signer.sftVerifyAsOf;
#endif
            else
            {
                SYSTEMTIME sysTime;

                GetSystemTime(&sysTime);
                SystemTimeToFileTime(&sysTime, &signer.sftVerifyAsOf);
            }
            ret = data->psPfns->pfnAddSgnr2Chain(data, FALSE, 0, &signer);
            if (ret)
            {
                ret = data->psPfns->pfnAddCert2Chain(data, 0, FALSE, 0,
                 data->pWintrustData->pCert->psCertContext);
                if (WVT_ISINSTRUCT(WINTRUST_CERT_INFO,
                 data->pWintrustData->pCert->cbStruct, pahStores))
                        for (i = 0;
                         ret && i < data->pWintrustData->pCert->chStores; i++)
                            ret = data->psPfns->pfnAddStore2Chain(data,
                             data->pWintrustData->pCert->pahStores[i]);
            }
            if (!ret)
                err = GetLastError();
        }
    }
    else
        err = ERROR_INVALID_PARAMETER;
    return err;
}

static DWORD SOFTPUB_LoadFileMessage(CRYPT_PROVIDER_DATA *data)
{
    DWORD err = ERROR_SUCCESS;

    if (!data->pWintrustData->pFile)
    {
        err = ERROR_INVALID_PARAMETER;
        goto error;
    }
    err = SOFTPUB_OpenFile(data);
    if (err)
        goto error;
    err = SOFTPUB_GetFileSubject(data);
#ifdef __REACTOS__
    if (err)
    {
        data->padwTrustStepErrors[TRUSTERROR_STEP_SIP] = err;
        data->padwTrustStepErrors[TRUSTERROR_STEP_MSG_SIGNERCOUNT] = err;
    }
#endif
    if (err)
        goto error;
    err = SOFTPUB_GetSIP(data);
    if (err)
        goto error;
    err = SOFTPUB_GetMessageFromFile(data, data->pWintrustData->pFile->hFile,
     data->pWintrustData->pFile->pcwszFilePath);
    if (err)
        goto error;
    err = SOFTPUB_CreateStoreFromMessage(data);
    if (err)
        goto error;
    err = SOFTPUB_DecodeInnerContent(data);
    if (err)
        goto error;
#ifdef __REACTOS__
    if (SOFTPUB_HasIndirectData(data))
        err = SOFTPUB_VerifyImageHash(data, data->pWintrustData->pFile->hFile);
#else
    err = SOFTPUB_VerifyImageHash(data, data->pWintrustData->pFile->hFile);
#endif

error:
    if (err && data->fOpenedFile && data->pWintrustData->pFile)
    {
        /* The caller won't expect the file to be open on failure, so close it.
         */
        CloseHandle(data->pWintrustData->pFile->hFile);
        data->pWintrustData->pFile->hFile = INVALID_HANDLE_VALUE;
        data->fOpenedFile = FALSE;
    }
    return err;
}

#ifdef __REACTOS__
static DWORD SOFTPUB_LoadBlobMessage(CRYPT_PROVIDER_DATA *data)
{
    WINTRUST_BLOB_INFO *blob = data->pWintrustData->pBlob;
    DWORD err;

    if (!blob || !WVT_ISINSTRUCT(WINTRUST_BLOB_INFO, blob->cbStruct, pbMemObject))
        return ERROR_INVALID_PARAMETER;
    data->pPDSip->gSubject = blob->gSubject;
    if ((err = SOFTPUB_GetSIP(data)))
        return err;
    if ((err = SOFTPUB_GetMessageFromFile(data, NULL, blob->pcwszDisplayName)))
        return err;
    if ((err = SOFTPUB_CreateStoreFromMessage(data)))
        return err;
    return SOFTPUB_DecodeInnerContent(data);
}

#define SOFTPUB_SHA1_HASH_SIZE 20

static BOOL SOFTPUB_HexTag(const BYTE *hash, DWORD size, WCHAR *tag)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    DWORD i;

    for (i = 0; i < size; i++)
    {
        tag[i * 2] = hex[hash[i] >> 4];
        tag[i * 2 + 1] = hex[hash[i] & 0xf];
    }
    tag[size * 2] = 0;
    return TRUE;
}

static BOOL SOFTPUB_IsCatalogV2(HANDLE cat)
{
    CRYPTCATSTORE *store = CryptCATStoreFromHandle(cat);

    return store && store->dwPublicVersion == CRYPTCAT_VERSION_2;
}

static DWORD SOFTPUB_VerifyCatalogMemberFile(CRYPT_PROVIDER_DATA *data, HANDLE cat, CRYPTCATMEMBER *member)
{
    WINTRUST_CATALOG_INFO *catalog = data->pWintrustData->pCatalog;
    SIP_DISPATCH_INFO sip = { sizeof(sip) };
    SIP_INDIRECT_DATA *computed = NULL;
    SIP_SUBJECTINFO subject;
    HANDLE file = catalog->hMemberFile;
    DWORD err = ERROR_SUCCESS, size = 0;

    if (!CryptSIPLoad(&member->gSubjectType, 0, &sip) || !sip.pfCreate)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    if (!file || file == INVALID_HANDLE_VALUE)
    {
        file = CreateFileW(catalog->pcwszMemberFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (file == INVALID_HANDLE_VALUE)
            return GetLastError();
    }

    memset(&subject, 0, sizeof(subject));
    subject.cbSize = sizeof(subject);
    subject.pgSubjectType = &member->gSubjectType;
    subject.hFile = file;
    subject.pwsFileName = catalog->pcwszMemberFilePath;
    subject.hProv = data->hProv;
    subject.DigestAlgorithm = member->pIndirectData->DigestAlgorithm;
    if (SOFTPUB_IsCatalogV2(cat))
        subject.DigestAlgorithm.pszObjId = (char *)szOID_NIST_sha256;
    subject.dwFlags = SPC_EXC_PE_PAGE_HASHES_FLAG;
    if (!sip.pfCreate(&subject, &size, NULL) || !(computed = data->psPfns->pfnAlloc(size)) ||
        !sip.pfCreate(&subject, &size, computed))
        err = TRUST_E_BAD_DIGEST;
    else if (computed->Digest.cbData != member->pIndirectData->Digest.cbData ||
             memcmp(computed->Digest.pbData, member->pIndirectData->Digest.pbData, computed->Digest.cbData))
        err = TRUST_E_BAD_DIGEST;
    if (computed)
        data->psPfns->pfnFree(computed);
    if (file != catalog->hMemberFile)
        CloseHandle(file);
    return err;
}

static CRYPTCATMEMBER *SOFTPUB_FindCatalogMember(HANDLE cat, const WINTRUST_CATALOG_INFO *catalog)
{
    WCHAR hash_tag[SOFTPUB_SHA1_HASH_SIZE * 2 + 1];

    if (catalog->pbCalculatedFileHash && catalog->cbCalculatedFileHash == SOFTPUB_SHA1_HASH_SIZE)
    {
        SOFTPUB_HexTag(catalog->pbCalculatedFileHash, SOFTPUB_SHA1_HASH_SIZE, hash_tag);
        return CryptCATGetMemberInfo(cat, hash_tag);
    }
    if (!catalog->pcwszMemberTag)
        return NULL;
    return CryptCATGetMemberInfo(cat, (WCHAR *)catalog->pcwszMemberTag);
}

static DWORD SOFTPUB_VerifyCatalogMember(CRYPT_PROVIDER_DATA *data)
{
    WINTRUST_CATALOG_INFO *catalog = data->pWintrustData->pCatalog;
    CRYPTCATMEMBER *member;
    HANDLE cat;
    DWORD err;

    if (!catalog->pcwszMemberTag && !(catalog->pbCalculatedFileHash && catalog->cbCalculatedFileHash))
        return TRUST_E_NOSIGNATURE;

    cat = CryptCATOpen((WCHAR *)catalog->pcwszCatalogFilePath, CRYPTCAT_OPEN_EXISTING, data->hProv, 0, 0);
    if (cat == INVALID_HANDLE_VALUE)
        return GetLastError();

    member = SOFTPUB_FindCatalogMember(cat, catalog);
    if (catalog->pbCalculatedFileHash && catalog->cbCalculatedFileHash == SOFTPUB_SHA1_HASH_SIZE)
        err = member ? ERROR_SUCCESS : TRUST_E_NOSIGNATURE;
    else if (!member || !member->pIndirectData)
        err = TRUST_E_NOSIGNATURE;
    else if (catalog->pbCalculatedFileHash && catalog->cbCalculatedFileHash)
    {
        /* Windows 11 refuses a supplied hash that is not SHA-1 sized even when it matches the member */
        if (catalog->cbCalculatedFileHash != member->pIndirectData->Digest.cbData ||
            memcmp(catalog->pbCalculatedFileHash, member->pIndirectData->Digest.pbData, catalog->cbCalculatedFileHash))
            err = TRUST_E_BAD_DIGEST;
        else
            err = TRUST_E_NOSIGNATURE;
    }
    else
        err = SOFTPUB_VerifyCatalogMemberFile(data, cat, member);
    CryptCATClose(cat);
    return err;
}

#endif
static DWORD SOFTPUB_LoadCatalogMessage(CRYPT_PROVIDER_DATA *data)
{
    DWORD err;
    HANDLE catalog = INVALID_HANDLE_VALUE;

    if (!data->pWintrustData->pCatalog)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    catalog = CreateFileW(data->pWintrustData->pCatalog->pcwszCatalogFilePath,
     GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
     NULL);
    if (catalog == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (!CryptSIPRetrieveSubjectGuid(
     data->pWintrustData->pCatalog->pcwszCatalogFilePath, catalog,
     &data->pPDSip->gSubject))
    {
        err = GetLastError();
        goto error;
    }
    err = SOFTPUB_GetSIP(data);
    if (err)
        goto error;
    err = SOFTPUB_GetMessageFromFile(data, catalog,
     data->pWintrustData->pCatalog->pcwszCatalogFilePath);
    if (err)
        goto error;
    err = SOFTPUB_CreateStoreFromMessage(data);
    if (err)
        goto error;
    err = SOFTPUB_DecodeInnerContent(data);
#ifdef __REACTOS__
    if (!err)
        err = SOFTPUB_VerifyCatalogMember(data);
#else
    /* FIXME: this loads the catalog file, but doesn't validate the member. */
#endif
error:
    CloseHandle(catalog);
    return err;
}

HRESULT WINAPI SoftpubLoadMessage(CRYPT_PROVIDER_DATA *data)
{
    DWORD err = ERROR_SUCCESS;

    TRACE("(%p)\n", data);

    if (!data->padwTrustStepErrors)
        return S_FALSE;

    switch (data->pWintrustData->dwUnionChoice)
    {
    case WTD_CHOICE_CERT:
        err = SOFTPUB_LoadCertMessage(data);
#ifdef __REACTOS__
        free(data->pPDSip);
        data->pPDSip = NULL;
#endif
        break;
    case WTD_CHOICE_FILE:
        err = SOFTPUB_LoadFileMessage(data);
        break;
    case WTD_CHOICE_CATALOG:
        err = SOFTPUB_LoadCatalogMessage(data);
        break;
#ifdef __REACTOS__
    case WTD_CHOICE_BLOB:
        err = SOFTPUB_LoadBlobMessage(data);
        break;
#endif
    default:
        FIXME("unimplemented for %ld\n", data->pWintrustData->dwUnionChoice);
        err = ERROR_INVALID_PARAMETER;
    }

    if (err)
        data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_OBJPROV] = err;
    TRACE("returning %ld (%08lx)\n", !err ? S_OK : S_FALSE,
     data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_OBJPROV]);
    return !err ? S_OK : S_FALSE;
}

static CMSG_SIGNER_INFO *WINTRUST_GetSigner(CRYPT_PROVIDER_DATA *data,
 DWORD signerIdx)
{
    BOOL ret;
    CMSG_SIGNER_INFO *signerInfo = NULL;
    DWORD size;

    ret = CryptMsgGetParam(data->hMsg, CMSG_SIGNER_INFO_PARAM, signerIdx,
     NULL, &size);
    if (ret)
    {
        signerInfo = data->psPfns->pfnAlloc(size);
        if (signerInfo)
        {
            ret = CryptMsgGetParam(data->hMsg, CMSG_SIGNER_INFO_PARAM,
             signerIdx, signerInfo, &size);
            if (!ret)
            {
                data->psPfns->pfnFree(signerInfo);
                signerInfo = NULL;
            }
        }
        else
            SetLastError(ERROR_OUTOFMEMORY);
    }
    return signerInfo;
}

#ifndef __REACTOS__
static BOOL WINTRUST_GetTimeFromCounterSigner(
 const CMSG_CMS_SIGNER_INFO *counterSignerInfo, FILETIME *time)
{
    DWORD i;
    BOOL foundTimeStamp = FALSE;

    for (i = 0; !foundTimeStamp && i < counterSignerInfo->AuthAttrs.cAttr; i++)
    {
        if (!strcmp(counterSignerInfo->AuthAttrs.rgAttr[i].pszObjId,
         szOID_RSA_signingTime))
        {
            const CRYPT_ATTRIBUTE *attr =
             &counterSignerInfo->AuthAttrs.rgAttr[i];
            DWORD j;

            for (j = 0; !foundTimeStamp && j < attr->cValue; j++)
            {
                static const DWORD encoding = X509_ASN_ENCODING |
                 PKCS_7_ASN_ENCODING;
                DWORD size = sizeof(FILETIME);

                foundTimeStamp = CryptDecodeObjectEx(encoding,
                 X509_CHOICE_OF_TIME,
                 attr->rgValue[j].pbData, attr->rgValue[j].cbData, 0, NULL,
                 time, &size);
            }
        }
    }
    return foundTimeStamp;
}
#endif

static BOOL WINTRUST_GetTimeFromTimeStampToken(const CRYPT_ATTR_BLOB *token,
 FILETIME *time)
{
    static const DWORD encoding = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    char contentType[sizeof(szOID_TIMESTAMP_TOKEN)];
    CRYPT_SEQUENCE_OF_ANY *info = NULL;
    CRYPT_DATA_BLOB *octets = NULL;
    const BYTE *encoded;
    BYTE *content = NULL;
    DWORD size, encodedSize;
    HCRYPTMSG msg;
    BOOL ret = FALSE;

    msg = CryptMsgOpenToDecode(encoding, 0, 0, 0, NULL, NULL);
    if (!msg)
        return FALSE;
    size = sizeof(contentType);
    if (!CryptMsgUpdate(msg, token->pbData, token->cbData, TRUE) ||
     !CryptMsgGetParam(msg, CMSG_INNER_CONTENT_TYPE_PARAM, 0, contentType,
     &size) || strcmp(contentType, szOID_TIMESTAMP_TOKEN) ||
     !CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, NULL, &encodedSize) ||
     !(content = malloc(encodedSize)) ||
     !CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, content, &encodedSize))
        goto done;
    encoded = content;
    if (CryptDecodeObjectEx(encoding, X509_OCTET_STRING, content, encodedSize,
     CRYPT_DECODE_ALLOC_FLAG, NULL, &octets, &size))
    {
        encoded = octets->pbData;
        encodedSize = octets->cbData;
    }
    if (CryptDecodeObjectEx(encoding, X509_SEQUENCE_OF_ANY, encoded,
     encodedSize, CRYPT_DECODE_ALLOC_FLAG, NULL, &info, &size) &&
     info->cValue > 4)
    {
        size = sizeof(*time);
        ret = CryptDecodeObjectEx(encoding, X509_CHOICE_OF_TIME,
         info->rgValue[4].pbData, info->rgValue[4].cbData, 0, NULL, time,
         &size);
    }

done:
    LocalFree(info);
    LocalFree(octets);
    free(content);
    CryptMsgClose(msg);
    return ret;
}

static LPCSTR filetime_to_str(const FILETIME *time)
{
    static char date[80];
    char dateFmt[80]; /* sufficient for all versions of LOCALE_SSHORTDATE */
    SYSTEMTIME sysTime;

    if (!time) return NULL;

    GetLocaleInfoA(LOCALE_SYSTEM_DEFAULT, LOCALE_SSHORTDATE, dateFmt, ARRAY_SIZE(dateFmt));
    FileTimeToSystemTime(time, &sysTime);
    GetDateFormatA(LOCALE_SYSTEM_DEFAULT, 0, &sysTime, dateFmt, date, ARRAY_SIZE(date));
    return date;
}

static FILETIME WINTRUST_GetTimeFromSigner(const CRYPT_PROVIDER_DATA *data,
 const CMSG_SIGNER_INFO *signerInfo)
{
    DWORD i;
    FILETIME time;
    BOOL foundTimeStamp = FALSE;

    for (i = 0; !foundTimeStamp && i < signerInfo->UnauthAttrs.cAttr; i++)
    {
#ifndef __REACTOS__
        if (!strcmp(signerInfo->UnauthAttrs.rgAttr[i].pszObjId,
         szOID_RSA_counterSign))
        {
            const CRYPT_ATTRIBUTE *attr = &signerInfo->UnauthAttrs.rgAttr[i];
            DWORD j;

            for (j = 0; j < attr->cValue; j++)
            {
                static const DWORD encoding = X509_ASN_ENCODING |
                 PKCS_7_ASN_ENCODING;
                CMSG_CMS_SIGNER_INFO *counterSignerInfo;
                DWORD size;
                BOOL ret = CryptDecodeObjectEx(encoding, CMS_SIGNER_INFO,
                 attr->rgValue[j].pbData, attr->rgValue[j].cbData,
                 CRYPT_DECODE_ALLOC_FLAG, NULL, &counterSignerInfo, &size);
                if (ret)
                {
                    /* FIXME: need to verify countersigner signature too */
                    foundTimeStamp = WINTRUST_GetTimeFromCounterSigner(
                     counterSignerInfo, &time);
                    LocalFree(counterSignerInfo);
                }
            }
        }
        else
#endif
        if (!strcmp(signerInfo->UnauthAttrs.rgAttr[i].pszObjId,
         szOID_RFC3161_counterSign))
        {
            const CRYPT_ATTRIBUTE *attr = &signerInfo->UnauthAttrs.rgAttr[i];
            DWORD j;

            for (j = 0; !foundTimeStamp && j < attr->cValue; j++)
                foundTimeStamp = WINTRUST_GetTimeFromTimeStampToken(
                 &attr->rgValue[j], &time);
        }
    }
    if (!foundTimeStamp)
    {
        TRACE("returning system time %s\n",
         filetime_to_str(&data->sftSystemTime));
        time = data->sftSystemTime;
    }
    else
        TRACE("returning time from message %s\n", filetime_to_str(&time));
    return time;
}

static DWORD WINTRUST_SaveSigner(CRYPT_PROVIDER_DATA *data, DWORD signerIdx)
{
    DWORD err;
    CMSG_SIGNER_INFO *signerInfo = WINTRUST_GetSigner(data, signerIdx);

    if (signerInfo)
    {
        CRYPT_PROVIDER_SGNR sgnr = { sizeof(sgnr), { 0 } };

        sgnr.psSigner = signerInfo;
        sgnr.sftVerifyAsOf = WINTRUST_GetTimeFromSigner(data, signerInfo);
        if (!data->psPfns->pfnAddSgnr2Chain(data, FALSE, signerIdx, &sgnr))
            err = GetLastError();
        else
            err = ERROR_SUCCESS;
    }
    else
        err = GetLastError();
    return err;
}

static CERT_INFO *WINTRUST_GetSignerCertInfo(CRYPT_PROVIDER_DATA *data,
 DWORD signerIdx)
{
    BOOL ret;
    CERT_INFO *certInfo = NULL;
    DWORD size;

    ret = CryptMsgGetParam(data->hMsg, CMSG_SIGNER_CERT_INFO_PARAM, signerIdx,
     NULL, &size);
    if (ret)
    {
        certInfo = data->psPfns->pfnAlloc(size);
        if (certInfo)
        {
            ret = CryptMsgGetParam(data->hMsg, CMSG_SIGNER_CERT_INFO_PARAM,
             signerIdx, certInfo, &size);
            if (!ret)
            {
                data->psPfns->pfnFree(certInfo);
                certInfo = NULL;
            }
        }
        else
            SetLastError(ERROR_OUTOFMEMORY);
    }
    return certInfo;
}

static DWORD WINTRUST_VerifySigner(CRYPT_PROVIDER_DATA *data, DWORD signerIdx)
{
    DWORD err;
    CERT_INFO *certInfo = WINTRUST_GetSignerCertInfo(data, signerIdx);

    if (certInfo)
    {
#ifdef __REACTOS__
        PCCERT_CONTEXT subject = NULL;
        DWORD store;

        for (store = 0; !subject && store < data->chStores; store++)
            subject = CertGetSubjectCertificateFromStore(
             data->pahStores[store], data->dwEncoding, certInfo);
#else
        PCCERT_CONTEXT subject = CertGetSubjectCertificateFromStore(
         data->pahStores[0], data->dwEncoding, certInfo);
#endif

        if (subject)
        {
            CMSG_CTRL_VERIFY_SIGNATURE_EX_PARA para = { sizeof(para), 0,
             signerIdx, CMSG_VERIFY_SIGNER_CERT, (LPVOID)subject };

            if (!CryptMsgControl(data->hMsg, 0, CMSG_CTRL_VERIFY_SIGNATURE_EX,
             &para))
                err = TRUST_E_CERT_SIGNATURE;
            else
            {
                data->psPfns->pfnAddCert2Chain(data, signerIdx, FALSE, 0,
                 subject);
                err = ERROR_SUCCESS;
            }
            CertFreeCertificateContext(subject);
        }
        else
            err = TRUST_E_NO_SIGNER_CERT;
        data->psPfns->pfnFree(certInfo);
    }
    else
        err = GetLastError();
    return err;
}

static void load_secondary_signatures(CRYPT_PROVIDER_DATA *data, HCRYPTMSG msg)
{
    CRYPT_PROVIDER_SIGSTATE *s = data->pSigState;
    CRYPT_ATTRIBUTES *attrs;
    unsigned int i, j;
    DWORD size;

    if (!CryptMsgGetParam(msg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, 0, NULL, &size))
        return;

    if (!(attrs = data->psPfns->pfnAlloc(size)))
    {
        ERR("No memory.\n");
        return;
    }
    if (!CryptMsgGetParam(msg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, 0, attrs, &size))
        goto done;

    for (i = 0; i < attrs->cAttr; ++i)
    {
        if (strcmp(attrs->rgAttr[i].pszObjId, szOID_NESTED_SIGNATURE))
            continue;

        if (!(s->rhSecondarySigs = data->psPfns->pfnAlloc(attrs->rgAttr[i].cValue * sizeof(*s->rhSecondarySigs))))
        {
            ERR("No memory.\n");
            goto done;
        }
        s->cSecondarySigs = 0;
        for (j = 0; j < attrs->rgAttr[i].cValue; ++j)
        {
            if (!(msg = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL)))
            {
                ERR("Could not create crypt message.\n");
                goto done;
            }
            if (!CryptMsgUpdate(msg, attrs->rgAttr[i].rgValue[j].pbData, attrs->rgAttr[i].rgValue[j].cbData, TRUE))
            {
                ERR("Could not update crypt message, err %lu.\n", GetLastError());
                CryptMsgClose(msg);
                goto done;
            }
            s->rhSecondarySigs[j] = msg;
            ++s->cSecondarySigs;
        }
        break;
    }
done:
    data->psPfns->pfnFree(attrs);
}

#ifdef __REACTOS__
static DWORD SOFTPUB_SelectSecondarySignature(CRYPT_PROVIDER_DATA *data, DWORD index)
{
    HCRYPTMSG msg = CryptMsgDuplicate(data->pSigState->rhSecondarySigs[index]);
    DWORD err;

    CryptMsgClose(data->hMsg);
    data->hMsg = msg;
    err = SOFTPUB_CreateStoreFromMessage(data);
    if (!err)
    {
        data->psPfns->pfnFree(data->pPDSip->psIndirectData);
        data->pPDSip->psIndirectData = NULL;
        err = SOFTPUB_DecodeInnerContent(data);
    }
    if (!err && data->pWintrustData->dwUnionChoice == WTD_CHOICE_FILE &&
     data->pWintrustData->pFile)
        err = SOFTPUB_VerifyImageHash(data, data->pWintrustData->pFile->hFile);
    return err;
}
#endif

#ifdef __REACTOS__
static BOOL SOFTPUB_GetSigningTime(const CRYPT_ATTRIBUTES *attrs, FILETIME *time)
{
    DWORD i, j, size;

    for (i = 0; i < attrs->cAttr; i++)
    {
        if (strcmp(attrs->rgAttr[i].pszObjId, szOID_RSA_signingTime))
            continue;
        for (j = 0; j < attrs->rgAttr[i].cValue; j++)
        {
            size = sizeof(*time);
            if (CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, X509_CHOICE_OF_TIME,
             attrs->rgAttr[i].rgValue[j].pbData, attrs->rgAttr[i].rgValue[j].cbData, 0, NULL, time, &size))
                return TRUE;
        }
    }
    return FALSE;
}

static BOOL SOFTPUB_IsTimeStampCert(PCCERT_CONTEXT cert)
{
    CERT_ENHKEY_USAGE *usage;
    DWORD size = 0, i;
    BOOL ret = FALSE;

    if (!CertGetEnhancedKeyUsage(cert, CERT_FIND_EXT_ONLY_ENHKEY_USAGE_FLAG, NULL, &size) ||
     !(usage = malloc(size)))
        return FALSE;
    if (CertGetEnhancedKeyUsage(cert, CERT_FIND_EXT_ONLY_ENHKEY_USAGE_FLAG, usage, &size))
        for (i = 0; !ret && i < usage->cUsageIdentifier; i++)
            ret = !strcmp(usage->rgpszUsageIdentifier[i], szOID_PKIX_KP_TIMESTAMP_SIGNING);
    free(usage);
    return ret;
}

static void *SOFTPUB_DecodeAlloc(CRYPT_PROVIDER_DATA *data, LPCSTR type, const BYTE *encoded, DWORD size)
{
    DWORD needed = 0;
    void *out;

    if (!CryptDecodeObjectEx(data->dwEncoding, type, encoded, size, 0, NULL, NULL, &needed) ||
     !(out = data->psPfns->pfnAlloc(needed)))
        return NULL;
    if (CryptDecodeObjectEx(data->dwEncoding, type, encoded, size, 0, NULL, out, &needed))
        return out;
    data->psPfns->pfnFree(out);
    return NULL;
}

static DWORD SOFTPUB_SaveCounterSigner(CRYPT_PROVIDER_DATA *data, DWORD signerIdx, BYTE *signer,
 DWORD signerSize, const CRYPT_ATTR_BLOB *value)
{
    CRYPT_PROVIDER_SGNR sgnr = { sizeof(sgnr), { 0 } }, *counter;
    PCCERT_CONTEXT cert = NULL;
    CERT_INFO certInfo;
    FILETIME time;
    DWORD store, err = ERROR_SUCCESS;

    if (!(sgnr.psSigner = SOFTPUB_DecodeAlloc(data, PKCS7_SIGNER_INFO, value->pbData, value->cbData)))
        return GetLastError();
    sgnr.sftVerifyAsOf = data->sftSystemTime;
    if (!data->psPfns->pfnAddSgnr2Chain(data, TRUE, signerIdx, &sgnr))
    {
        err = GetLastError();
        data->psPfns->pfnFree(sgnr.psSigner);
        return err;
    }
    counter = &data->pasSigners[signerIdx].pasCounterSigners[data->pasSigners[signerIdx].csCounterSigners - 1];

    memset(&certInfo, 0, sizeof(certInfo));
    certInfo.Issuer = counter->psSigner->Issuer;
    certInfo.SerialNumber = counter->psSigner->SerialNumber;
    for (store = 0; !cert && store < data->chStores; store++)
        cert = CertGetSubjectCertificateFromStore(data->pahStores[store], data->dwEncoding, &certInfo);
    if (!cert)
    {
        counter->dwError = TRUST_E_NO_SIGNER_CERT;
        data->padwTrustStepErrors[TRUSTERROR_STEP_MSG_COUNTERSIGCERT] = CRYPT_E_NOT_FOUND;
        return TRUST_E_COUNTER_SIGNER;
    }

    data->psPfns->pfnAddCert2Chain(data, signerIdx, TRUE, data->pasSigners[signerIdx].csCounterSigners - 1, cert);
    if (!CryptMsgVerifyCountersignatureEncodedEx(0, data->dwEncoding, signer, signerSize, value->pbData,
     value->cbData, CMSG_VERIFY_SIGNER_CERT, (void *)cert, 0, NULL))
    {
        counter->dwError = GetLastError();
        data->padwTrustStepErrors[TRUSTERROR_STEP_MSG_COUNTERSIGINFO] = counter->dwError;
        err = TRUST_E_COUNTER_SIGNER;
    }
    else if (SOFTPUB_IsTimeStampCert(cert) && SOFTPUB_GetSigningTime(&counter->psSigner->AuthAttrs, &time))
    {
        counter->dwSignerType = SGNR_TYPE_TIMESTAMP;
        counter->sftVerifyAsOf = time;
        data->pasSigners[signerIdx].sftVerifyAsOf = time;
    }
    CertFreeCertificateContext(cert);
    return err;
}

static DWORD SOFTPUB_SaveCounterSigners(CRYPT_PROVIDER_DATA *data, DWORD signerIdx)
{
    const CRYPT_ATTRIBUTES *attrs = &data->pasSigners[signerIdx].psSigner->UnauthAttrs;
    BYTE *signer = NULL;
    DWORD signerSize = 0, i, j, err = ERROR_SUCCESS;

    for (i = 0; !err && i < attrs->cAttr; i++)
    {
        if (strcmp(attrs->rgAttr[i].pszObjId, szOID_RSA_counterSign))
            continue;
        if (!signer)
        {
            if (!CryptMsgGetParam(data->hMsg, CMSG_ENCODED_SIGNER, signerIdx, NULL, &signerSize))
                err = GetLastError();
            else if (!(signer = data->psPfns->pfnAlloc(signerSize)))
                err = ERROR_OUTOFMEMORY;
            else if (!CryptMsgGetParam(data->hMsg, CMSG_ENCODED_SIGNER, signerIdx, signer, &signerSize))
                err = GetLastError();
        }
        for (j = 0; !err && j < attrs->rgAttr[i].cValue; j++)
            err = SOFTPUB_SaveCounterSigner(data, signerIdx, signer, signerSize, &attrs->rgAttr[i].rgValue[j]);
    }
    data->psPfns->pfnFree(signer);
    return err;
}

#endif
HRESULT WINAPI SoftpubLoadSignature(CRYPT_PROVIDER_DATA *data)
{
    DWORD err = ERROR_SUCCESS;

    TRACE("(%p)\n", data);

    if (!data->padwTrustStepErrors)
        return S_FALSE;

#ifdef __REACTOS__
    if (data->pSigState)
    {
        if (data->pSigState->fFirstAttemptMade)
            data->pSigState->dwCurrentIndex++;
    }
    else if (!(data->pSigState = data->psPfns->pfnAlloc(sizeof(*data->pSigState))))
    {
        err = ERROR_OUTOFMEMORY;
    }
    else
    {
        data->pSigState->cbStruct = sizeof(*data->pSigState);
        data->pSigState->fSupportMultiSig = TRUE;
        data->pSigState->dwCryptoPolicySupport = WSS_SIGTRUST_SUPPORT | WSS_OBJTRUST_SUPPORT | WSS_CERTTRUST_SUPPORT;
        if (data->hMsg)
        {
            data->pSigState->hPrimarySig = CryptMsgDuplicate(data->hMsg);
            load_secondary_signatures(data, data->pSigState->hPrimarySig);
        }
        if (data->pSigSettings)
        {
            if (data->pSigSettings->dwFlags & WSS_GET_SECONDARY_SIG_COUNT)
                data->pSigSettings->cSecondarySigs = data->pSigState->cSecondarySigs;
            if (data->pSigSettings->dwFlags & WSS_VERIFY_SPECIFIC)
                data->pSigState->dwCurrentIndex = data->pSigSettings->dwIndex;
        }
    }
    if (!err && data->hMsg)
    {
        CRYPT_PROVIDER_SIGSTATE *state = data->pSigState;

        state->fFirstAttemptMade = TRUE;
        state->iAttemptCount++;
        state->fNoMoreSigs = state->dwCurrentIndex >= state->cSecondarySigs ||
         (data->pSigSettings && (data->pSigSettings->dwFlags & WSS_VERIFY_SPECIFIC));
        if (state->dwCurrentIndex > state->cSecondarySigs)
            err = TRUST_E_NOSIGNATURE;
        else if (state->dwCurrentIndex)
        {
            err = SOFTPUB_SelectSecondarySignature(data, state->dwCurrentIndex - 1);
            if (data->pSigSettings)
                data->pSigSettings->dwVerifiedSigIndex = state->dwCurrentIndex;
        }
    }
#else
    if (data->pSigState)
    {
        /* We did not initialize this, probably an unsupported usage. */
        FIXME("pSigState %p already initialized.\n", data->pSigState);
    }
    if (!(data->pSigState = data->psPfns->pfnAlloc(sizeof(*data->pSigState))))
    {
        err = ERROR_OUTOFMEMORY;
    }
    else
    {
        data->pSigState->cbStruct = sizeof(*data->pSigState);
        data->pSigState->fSupportMultiSig = TRUE;
        data->pSigState->dwCryptoPolicySupport = WSS_SIGTRUST_SUPPORT | WSS_OBJTRUST_SUPPORT | WSS_CERTTRUST_SUPPORT;
        if (data->hMsg)
        {
            data->pSigState->hPrimarySig = CryptMsgDuplicate(data->hMsg);
            load_secondary_signatures(data, data->pSigState->hPrimarySig);
        }
        if (data->pSigSettings)
        {
            if (data->pSigSettings->dwFlags & WSS_GET_SECONDARY_SIG_COUNT)
                data->pSigSettings->cSecondarySigs = data->pSigState->cSecondarySigs;
        }
    }
#endif

    if (!err && data->hMsg)
    {
        DWORD signerCount, size;

        size = sizeof(signerCount);
        if (CryptMsgGetParam(data->hMsg, CMSG_SIGNER_COUNT_PARAM, 0,
         &signerCount, &size))
        {
            DWORD i;

            err = ERROR_SUCCESS;
            for (i = 0; !err && i < signerCount; i++)
            {
                if (!(err = WINTRUST_SaveSigner(data, i)))
                    err = WINTRUST_VerifySigner(data, i);
#ifdef __REACTOS__
                if (!err)
                    err = SOFTPUB_SaveCounterSigners(data, i);
#endif
            }
        }
        else
            err = TRUST_E_NOSIGNATURE;
    }
#ifdef __REACTOS__
    else if (!err && data->pWintrustData->dwUnionChoice != WTD_CHOICE_CERT)
        err = TRUST_E_NOSIGNATURE;
#endif

    if (err)
        data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_SIGPROV] = err;
    return !err ? S_OK : S_FALSE;
}

static DWORD WINTRUST_TrustStatusToConfidence(DWORD errorStatus)
{
    DWORD confidence = 0;

    confidence = 0;
    if (!(errorStatus & CERT_TRUST_IS_NOT_SIGNATURE_VALID))
        confidence |= CERT_CONFIDENCE_SIG;
    if (!(errorStatus & CERT_TRUST_IS_NOT_TIME_VALID))
        confidence |= CERT_CONFIDENCE_TIME;
    if (!(errorStatus & CERT_TRUST_IS_NOT_TIME_NESTED))
        confidence |= CERT_CONFIDENCE_TIMENEST;
    return confidence;
}

BOOL WINAPI SoftpubCheckCert(CRYPT_PROVIDER_DATA *data, DWORD idxSigner,
 BOOL fCounterSignerChain, DWORD idxCounterSigner)
{
    BOOL ret;

    TRACE("(%p, %ld, %d, %ld)\n", data, idxSigner, fCounterSignerChain,
     idxCounterSigner);

#ifdef __REACTOS__
    {
        CRYPT_PROVIDER_SGNR *sgnr = fCounterSignerChain ?
         &data->pasSigners[idxSigner].pasCounterSigners[idxCounterSigner] : &data->pasSigners[idxSigner];
        PCERT_SIMPLE_CHAIN simpleChain = sgnr->pChainContext->rgpChain[0];
        DWORD i;

        ret = TRUE;
        for (i = 0; i < simpleChain->cElement && i < sgnr->csCertChain; i++)
        {
            sgnr->pasCertChain[i].dwConfidence =
             WINTRUST_TrustStatusToConfidence(simpleChain->rgpElement[i]->TrustStatus.dwErrorStatus);
            if (!(simpleChain->rgpElement[i]->TrustStatus.dwErrorStatus & CERT_TRUST_IS_UNTRUSTED_ROOT))
                sgnr->pasCertChain[i].fTrustedRoot = TRUE;
            if (simpleChain->rgpElement[i]->TrustStatus.dwInfoStatus & CERT_TRUST_IS_SELF_SIGNED)
                sgnr->pasCertChain[i].fSelfSigned = TRUE;
            if (simpleChain->rgpElement[i]->TrustStatus.dwErrorStatus & CERT_TRUST_IS_CYCLIC)
                sgnr->pasCertChain[i].fIsCyclic = TRUE;
        }
    }
#else
    if (fCounterSignerChain)
    {
        FIXME("unimplemented for counter signers\n");
        ret = FALSE;
    }
    else
    {
        PCERT_SIMPLE_CHAIN simpleChain =
         data->pasSigners[idxSigner].pChainContext->rgpChain[0];
        DWORD i;

        ret = TRUE;
        for (i = 0; i < simpleChain->cElement; i++)
        {
            /* Set confidence */
            data->pasSigners[idxSigner].pasCertChain[i].dwConfidence =
             WINTRUST_TrustStatusToConfidence(
             simpleChain->rgpElement[i]->TrustStatus.dwErrorStatus);
            /* Set additional flags */
            if (!(simpleChain->rgpElement[i]->TrustStatus.dwErrorStatus &
             CERT_TRUST_IS_UNTRUSTED_ROOT))
                data->pasSigners[idxSigner].pasCertChain[i].fTrustedRoot = TRUE;
            if (simpleChain->rgpElement[i]->TrustStatus.dwInfoStatus &
             CERT_TRUST_IS_SELF_SIGNED)
                data->pasSigners[idxSigner].pasCertChain[i].fSelfSigned = TRUE;
            if (simpleChain->rgpElement[i]->TrustStatus.dwErrorStatus &
             CERT_TRUST_IS_CYCLIC)
                data->pasSigners[idxSigner].pasCertChain[i].fIsCyclic = TRUE;
        }
    }
#endif
    return ret;
}

static DWORD WINTRUST_TrustStatusToError(DWORD errorStatus)
{
    DWORD error;

    if (errorStatus & CERT_TRUST_IS_NOT_SIGNATURE_VALID)
        error = TRUST_E_CERT_SIGNATURE;
    else if (errorStatus & CERT_TRUST_IS_UNTRUSTED_ROOT)
        error = CERT_E_UNTRUSTEDROOT;
    else if (errorStatus & CERT_TRUST_IS_NOT_TIME_VALID)
        error = CERT_E_EXPIRED;
    else if (errorStatus & CERT_TRUST_IS_NOT_TIME_NESTED)
        error = CERT_E_VALIDITYPERIODNESTING;
    else if (errorStatus & CERT_TRUST_IS_REVOKED)
        error = CERT_E_REVOKED;
    else if (errorStatus & CERT_TRUST_IS_OFFLINE_REVOCATION ||
     errorStatus & CERT_TRUST_REVOCATION_STATUS_UNKNOWN)
        error = CERT_E_REVOCATION_FAILURE;
    else if (errorStatus & CERT_TRUST_IS_NOT_VALID_FOR_USAGE)
        error = CERT_E_WRONG_USAGE;
    else if (errorStatus & CERT_TRUST_IS_CYCLIC)
        error = CERT_E_CHAINING;
    else if (errorStatus & CERT_TRUST_INVALID_EXTENSION)
        error = CERT_E_CRITICAL;
    else if (errorStatus & CERT_TRUST_INVALID_POLICY_CONSTRAINTS)
        error = CERT_E_INVALID_POLICY;
    else if (errorStatus & CERT_TRUST_INVALID_BASIC_CONSTRAINTS)
        error = TRUST_E_BASIC_CONSTRAINTS;
    else if (errorStatus & CERT_TRUST_INVALID_NAME_CONSTRAINTS ||
     errorStatus & CERT_TRUST_HAS_NOT_SUPPORTED_NAME_CONSTRAINT ||
     errorStatus & CERT_TRUST_HAS_NOT_DEFINED_NAME_CONSTRAINT ||
     errorStatus & CERT_TRUST_HAS_NOT_PERMITTED_NAME_CONSTRAINT ||
     errorStatus & CERT_TRUST_HAS_EXCLUDED_NAME_CONSTRAINT)
        error = CERT_E_INVALID_NAME;
    else if (errorStatus & CERT_TRUST_NO_ISSUANCE_CHAIN_POLICY)
        error = CERT_E_INVALID_POLICY;
    else if (errorStatus)
    {
        FIXME("unknown error status %08lx\n", errorStatus);
        error = TRUST_E_SYSTEM_ERROR;
    }
    else
        error = S_OK;
    return error;
}

static DWORD WINTRUST_CopyChain(CRYPT_PROVIDER_DATA *data, DWORD signerIdx)
{
    DWORD err, i;
    PCERT_SIMPLE_CHAIN simpleChain =
     data->pasSigners[signerIdx].pChainContext->rgpChain[0];

    data->pasSigners[signerIdx].pasCertChain[0].dwConfidence =
     WINTRUST_TrustStatusToConfidence(
     simpleChain->rgpElement[0]->TrustStatus.dwErrorStatus);
    data->pasSigners[signerIdx].pasCertChain[0].pChainElement =
     simpleChain->rgpElement[0];
    err = ERROR_SUCCESS;
    for (i = 1; !err && i < simpleChain->cElement; i++)
    {
        if (data->psPfns->pfnAddCert2Chain(data, signerIdx, FALSE, 0,
         simpleChain->rgpElement[i]->pCertContext))
        {
            data->pasSigners[signerIdx].pasCertChain[i].pChainElement =
             simpleChain->rgpElement[i];
            data->pasSigners[signerIdx].pasCertChain[i].dwConfidence =
             WINTRUST_TrustStatusToConfidence(
             simpleChain->rgpElement[i]->TrustStatus.dwErrorStatus);
        }
        else
            err = GetLastError();
    }
    data->pasSigners[signerIdx].pasCertChain[simpleChain->cElement - 1].dwError
     = WINTRUST_TrustStatusToError(
     simpleChain->rgpElement[simpleChain->cElement - 1]->
     TrustStatus.dwErrorStatus);
    return err;
}

static void WINTRUST_CreateChainPolicyCreateInfo(
 const CRYPT_PROVIDER_DATA *data, PWTD_GENERIC_CHAIN_POLICY_CREATE_INFO info,
 PCERT_CHAIN_PARA chainPara)
{
    chainPara->cbSize = sizeof(CERT_CHAIN_PARA);
    if (data->pRequestUsage)
        chainPara->RequestedUsage = *data->pRequestUsage;
#ifdef __REACTOS__
    else if (data->pszUsageOID)
    {
        chainPara->RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
        chainPara->RequestedUsage.Usage.cUsageIdentifier = 1;
        chainPara->RequestedUsage.Usage.rgpszUsageIdentifier = (LPSTR *)&data->pszUsageOID;
    }
#endif
    else
    {
        chainPara->RequestedUsage.dwType = 0;
        chainPara->RequestedUsage.Usage.cUsageIdentifier = 0;
    }
    info->cbSize = sizeof(WTD_GENERIC_CHAIN_POLICY_CREATE_INFO);
    info->hChainEngine = NULL;
    info->pChainPara = chainPara;
    if (data->dwProvFlags & CPD_REVOCATION_CHECK_END_CERT)
        info->dwFlags = CERT_CHAIN_REVOCATION_CHECK_END_CERT;
    else if (data->dwProvFlags & CPD_REVOCATION_CHECK_CHAIN)
        info->dwFlags = CERT_CHAIN_REVOCATION_CHECK_CHAIN;
    else if (data->dwProvFlags & CPD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT)
        info->dwFlags = CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
#ifdef __REACTOS__
    else if (data->dwProvFlags & WTD_REVOCATION_CHECK_NONE)
        info->dwFlags = 0;
    else if (data->dwProvFlags & WTD_REVOCATION_CHECK_END_CERT)
        info->dwFlags = CERT_CHAIN_REVOCATION_CHECK_END_CERT;
    else if (data->dwProvFlags & WTD_REVOCATION_CHECK_CHAIN)
        info->dwFlags = CERT_CHAIN_REVOCATION_CHECK_CHAIN;
    else
        info->dwFlags = CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    if (data->dwProvFlags & WTD_CACHE_ONLY_URL_RETRIEVAL)
        info->dwFlags |= CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL;
#else
    else
        info->dwFlags = 0;
#endif
    info->pvReserved = NULL;
}

#ifdef __REACTOS__
static DWORD SOFTPUB_CreateChainForCounterSigner(CRYPT_PROVIDER_DATA *data, DWORD signerIdx,
 DWORD counterIdx, HCERTSTORE store, PWTD_GENERIC_CHAIN_POLICY_CREATE_INFO createInfo)
{
    CRYPT_PROVIDER_SGNR *counter = &data->pasSigners[signerIdx].pasCounterSigners[counterIdx];
    CERT_CHAIN_PARA chainPara;
    PCERT_SIMPLE_CHAIN simpleChain;
    DWORD i, flags;

    if (!counter->csCertChain)
        return TRUST_E_COUNTER_SIGNER;
    memset(&chainPara, 0, sizeof(chainPara));
    chainPara.cbSize = sizeof(chainPara);
    flags = createInfo->dwFlags;
    if ((counter->dwSignerType & SGNR_TYPE_TIMESTAMP) && (data->dwRegPolicySettings & WTPF_IGNOREREVOCATIONONTS) &&
     data->pWintrustData->fdwRevocationChecks != WTD_REVOKE_WHOLECHAIN &&
     !(data->dwProvFlags & (WTD_REVOCATION_CHECK_END_CERT | WTD_REVOCATION_CHECK_CHAIN |
     WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT)))
        flags &= ~(CERT_CHAIN_REVOCATION_CHECK_END_CERT | CERT_CHAIN_REVOCATION_CHECK_CHAIN |
         CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT);
    if (!CertGetCertificateChain(createInfo->hChainEngine, counter->pasCertChain[0].pCert,
     &counter->sftVerifyAsOf, store, &chainPara, flags, createInfo->pvReserved,
     &counter->pChainContext))
        return GetLastError();

    simpleChain = counter->pChainContext->rgpChain[0];
    counter->pasCertChain[0].pChainElement = simpleChain->rgpElement[0];
    for (i = 1; i < simpleChain->cElement; i++)
    {
        if (!data->psPfns->pfnAddCert2Chain(data, signerIdx, TRUE, counterIdx,
         simpleChain->rgpElement[i]->pCertContext))
            return GetLastError();
        counter->pasCertChain[i].pChainElement = simpleChain->rgpElement[i];
    }
    counter->pasCertChain[simpleChain->cElement - 1].dwError =
     WINTRUST_TrustStatusToError(simpleChain->rgpElement[simpleChain->cElement - 1]->TrustStatus.dwErrorStatus);
    if (data->psPfns->pfnCertCheckPolicy &&
     !data->psPfns->pfnCertCheckPolicy(data, signerIdx, TRUE, counterIdx))
        return GetLastError();
    return ERROR_SUCCESS;
}

#endif
static DWORD WINTRUST_CreateChainForSigner(CRYPT_PROVIDER_DATA *data,
 DWORD signer, PWTD_GENERIC_CHAIN_POLICY_CREATE_INFO createInfo,
 PCERT_CHAIN_PARA chainPara)
{
    DWORD err = ERROR_SUCCESS;
    HCERTSTORE store = NULL;

    if (data->chStores)
    {
        store = CertOpenStore(CERT_STORE_PROV_COLLECTION, 0, 0,
         CERT_STORE_CREATE_NEW_FLAG, NULL);
        if (store)
        {
            DWORD i;

            for (i = 0; i < data->chStores; i++)
                CertAddStoreToCollection(store, data->pahStores[i], 0, 0);
        }
        else
            err = GetLastError();
    }
    if (!err)
    {
        /* Expect the end certificate for each signer to be the only cert in
         * the chain:
         */
        if (data->pasSigners[signer].csCertChain)
        {
            BOOL ret;

            /* Create a certificate chain for each signer */
            ret = CertGetCertificateChain(createInfo->hChainEngine,
             data->pasSigners[signer].pasCertChain[0].pCert,
             &data->pasSigners[signer].sftVerifyAsOf, store,
             chainPara, createInfo->dwFlags, createInfo->pvReserved,
             &data->pasSigners[signer].pChainContext);
            if (ret)
            {
                if (data->pasSigners[signer].pChainContext->cChain != 1)
                {
                    FIXME("unimplemented for more than 1 simple chain\n");
                    err = E_NOTIMPL;
                }
                else
                {
                    if (!(err = WINTRUST_CopyChain(data, signer)))
                    {
                        if (data->psPfns->pfnCertCheckPolicy)
                        {
                            ret = data->psPfns->pfnCertCheckPolicy(data, signer,
                             FALSE, 0);
                            if (!ret)
                                err = GetLastError();
                        }
                        else
                            TRACE(
                             "no cert check policy, skipping policy check\n");
                    }
                }
            }
            else
                err = GetLastError();
        }
#ifdef __REACTOS__
        {
            DWORD i;

            for (i = 0; !err && i < data->pasSigners[signer].csCounterSigners; i++)
                err = SOFTPUB_CreateChainForCounterSigner(data, signer, i, store, createInfo);
        }
#endif
        CertCloseStore(store, 0);
    }
    return err;
}

HRESULT WINAPI WintrustCertificateTrust(CRYPT_PROVIDER_DATA *data)
{
    DWORD err;

    TRACE("(%p)\n", data);

    if (!data->csSigners)
        err = TRUST_E_NOSIGNATURE;
    else
    {
        DWORD i;
        WTD_GENERIC_CHAIN_POLICY_CREATE_INFO createInfo;
        CERT_CHAIN_PARA chainPara;

        WINTRUST_CreateChainPolicyCreateInfo(data, &createInfo, &chainPara);
        err = ERROR_SUCCESS;
        for (i = 0; !err && i < data->csSigners; i++)
            err = WINTRUST_CreateChainForSigner(data, i, &createInfo,
             &chainPara);
    }
    if (err)
        data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_CERTPROV] = err;
    TRACE("returning %ld (%08lx)\n", !err ? S_OK : S_FALSE,
     data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_CERTPROV]);
    return !err ? S_OK : S_FALSE;
}

HRESULT WINAPI GenericChainCertificateTrust(CRYPT_PROVIDER_DATA *data)
{
    DWORD err;
    WTD_GENERIC_CHAIN_POLICY_DATA *policyData =
     data->pWintrustData->pPolicyCallbackData;

    TRACE("(%p)\n", data);

    if (policyData && policyData->cbSize !=
     sizeof(WTD_GENERIC_CHAIN_POLICY_CREATE_INFO))
    {
        err = ERROR_INVALID_PARAMETER;
        goto end;
    }
    if (!data->csSigners)
        err = TRUST_E_NOSIGNATURE;
    else
    {
        DWORD i;
        WTD_GENERIC_CHAIN_POLICY_CREATE_INFO createInfo, *pCreateInfo;
        CERT_CHAIN_PARA chainPara, *pChainPara;

        if (policyData)
        {
            pCreateInfo = policyData->pSignerChainInfo;
            pChainPara = pCreateInfo->pChainPara;
        }
        else
        {
            WINTRUST_CreateChainPolicyCreateInfo(data, &createInfo, &chainPara);
            pChainPara = &chainPara;
            pCreateInfo = &createInfo;
        }
        err = ERROR_SUCCESS;
        for (i = 0; !err && i < data->csSigners; i++)
            err = WINTRUST_CreateChainForSigner(data, i, pCreateInfo,
             pChainPara);
    }

end:
    if (err)
        data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_CERTPROV] = err;
    TRACE("returning %ld (%08lx)\n", !err ? S_OK : S_FALSE,
     data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_CERTPROV]);
    return !err ? S_OK : S_FALSE;
}

#ifdef __REACTOS__
static DWORD SOFTPUB_FirstStepError(const CRYPT_PROVIDER_DATA *data, const DWORD *steps, DWORD count)
{
    DWORD i;

    for (i = 0; i < count; i++)
    {
        if (data->padwTrustStepErrors[steps[i]])
            return data->padwTrustStepErrors[steps[i]];
    }
    return ERROR_SUCCESS;
}

static const DWORD SOFTPUB_PriorSteps[] = { TRUSTERROR_STEP_FINAL_WVTINIT, TRUSTERROR_STEP_FINAL_OBJPROV,
                                            TRUSTERROR_STEP_FINAL_SIGPROV, TRUSTERROR_STEP_FINAL_CERTPROV,
                                            TRUSTERROR_STEP_FINAL_CERTCHKPROV };

static DWORD SOFTPUB_PriorStepError(const CRYPT_PROVIDER_DATA *data)
{
    return SOFTPUB_FirstStepError(data, SOFTPUB_PriorSteps, ARRAY_SIZE(SOFTPUB_PriorSteps));
}

#endif
#ifdef __REACTOS__
static HRESULT SOFTPUB_Authenticode(CRYPT_PROVIDER_DATA *data, const DWORD *prior_steps, DWORD prior_count)
#else
HRESULT WINAPI SoftpubAuthenticode(CRYPT_PROVIDER_DATA *data)
#endif
{
    BOOL ret;
    CERT_CHAIN_POLICY_STATUS policyStatus = { sizeof(policyStatus), 0 };

    TRACE("(%p)\n", data);

    if (data->pWintrustData->dwUIChoice != WTD_UI_NONE)
        FIXME("unimplemented for UI choice %ld\n",
         data->pWintrustData->dwUIChoice);
#ifdef __REACTOS__
    if ((policyStatus.dwError = SOFTPUB_FirstStepError(data, prior_steps, prior_count)))
        ret = FALSE;
    else
#endif
    if (!data->csSigners)
    {
        ret = FALSE;
        policyStatus.dwError = TRUST_E_NOSIGNATURE;
    }
    else
    {
        DWORD i;

        ret = TRUE;
        for (i = 0; ret && i < data->csSigners; i++)
        {
#ifdef __REACTOS__
            BYTE hash[64];
            DWORD size = sizeof(hash);

            if (CertGetCertificateContextProperty(
             data->pasSigners[i].pasCertChain[0].pCert,
             CERT_SIGNATURE_HASH_PROP_ID, hash, &size))
            {
                CRYPT_HASH_BLOB hashBlob = { size, hash };
#else
            BYTE hash[20];
            DWORD size = sizeof(hash);

            /* First make sure cert isn't disallowed */
            if ((ret = CertGetCertificateContextProperty(
             data->pasSigners[i].pasCertChain[0].pCert,
             CERT_SIGNATURE_HASH_PROP_ID, hash, &size)))
            {
#endif
                static const WCHAR disallowedW[] =
                 { 'D','i','s','a','l','l','o','w','e','d',0 };
                HCERTSTORE disallowed = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                 X509_ASN_ENCODING, 0, CERT_SYSTEM_STORE_CURRENT_USER,
                 disallowedW);

                if (disallowed)
                {
#ifdef __REACTOS__
                    PCCERT_CONTEXT found = CertFindCertificateInStore(
                     disallowed, X509_ASN_ENCODING, 0, CERT_FIND_SIGNATURE_HASH,
                     &hashBlob, NULL);
#else
                    PCCERT_CONTEXT found = CertFindCertificateInStore(
                     disallowed, X509_ASN_ENCODING, 0, CERT_FIND_SIGNATURE_HASH,
                     hash, NULL);
#endif

                    if (found)
                    {
                        /* Disallowed!  Can't verify it. */
                        policyStatus.dwError = TRUST_E_SUBJECT_NOT_TRUSTED;
                        ret = FALSE;
                        CertFreeCertificateContext(found);
                    }
                    CertCloseStore(disallowed, 0);
                }
            }
#ifdef __REACTOS__
            if (ret && !(WVT_ISINSTRUCT(WINTRUST_DATA, data->pWintrustData->cbStruct, dwProvFlags) &&
             (data->pWintrustData->dwProvFlags & WTD_HASH_ONLY_FLAG)))
#else
            if (ret)
#endif
            {
                CERT_CHAIN_POLICY_PARA policyPara = { sizeof(policyPara), 0 };

                if (data->dwRegPolicySettings & WTPF_TRUSTTEST)
                    policyPara.dwFlags |= CERT_CHAIN_POLICY_TRUST_TESTROOT_FLAG;
                if (data->dwRegPolicySettings & WTPF_TESTCANBEVALID)
                    policyPara.dwFlags |= CERT_CHAIN_POLICY_ALLOW_TESTROOT_FLAG;
                if (data->dwRegPolicySettings & WTPF_IGNOREEXPIRATION)
                    policyPara.dwFlags |=
                     CERT_CHAIN_POLICY_IGNORE_NOT_TIME_VALID_FLAG |
                     CERT_CHAIN_POLICY_IGNORE_CTL_NOT_TIME_VALID_FLAG |
                     CERT_CHAIN_POLICY_IGNORE_NOT_TIME_NESTED_FLAG;
                if (data->dwRegPolicySettings & WTPF_IGNOREREVOKATION)
                    policyPara.dwFlags |=
                     CERT_CHAIN_POLICY_IGNORE_END_REV_UNKNOWN_FLAG |
                     CERT_CHAIN_POLICY_IGNORE_CTL_SIGNER_REV_UNKNOWN_FLAG |
                     CERT_CHAIN_POLICY_IGNORE_CA_REV_UNKNOWN_FLAG |
                     CERT_CHAIN_POLICY_IGNORE_ROOT_REV_UNKNOWN_FLAG;
#ifdef __REACTOS__
                if (data->dwRegPolicySettings & (WTPF_OFFLINEOK_IND | WTPF_OFFLINEOK_COM))
                    policyPara.dwFlags |= CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS;
#endif
                CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_AUTHENTICODE,
                 data->pasSigners[i].pChainContext, &policyPara, &policyStatus);
                if (policyStatus.dwError != NO_ERROR)
                    ret = FALSE;
            }
#ifdef __REACTOS__
            if (ret)
            {
                DWORD j;

                for (j = 0; ret && j < data->pasSigners[i].csCounterSigners; j++)
                {
                    CRYPT_PROVIDER_SGNR *counter = &data->pasSigners[i].pasCounterSigners[j];
                    CERT_CHAIN_POLICY_PARA timePara = { sizeof(timePara),
                     (data->dwRegPolicySettings & (WTPF_OFFLINEOK_IND | WTPF_OFFLINEOK_COM)) ?
                     CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS : 0 };
                    CERT_CHAIN_POLICY_STATUS timeStatus = { sizeof(timeStatus), 0 };

                    if (!(counter->dwSignerType & SGNR_TYPE_TIMESTAMP) || !counter->pChainContext)
                        continue;
                    CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, counter->pChainContext,
                     &timePara, &timeStatus);
                    if (timeStatus.dwError != NO_ERROR)
                    {
                        policyStatus.dwError = TRUST_E_TIME_STAMP;
                        ret = FALSE;
                    }
                }
            }
#endif
        }
    }
#ifdef __REACTOS__
    TRACE("returning %08lx\n", ret ? S_OK : policyStatus.dwError);
    return ret ? S_OK : policyStatus.dwError;
#else
    if (!ret)
        data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV] =
         policyStatus.dwError;
    TRACE("returning %ld (%08lx)\n", ret ? S_OK : S_FALSE,
     data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV]);
    return ret ? S_OK : S_FALSE;
#endif
}

#ifdef __REACTOS__
HRESULT WINAPI SoftpubAuthenticode(CRYPT_PROVIDER_DATA *data)
{
    return SOFTPUB_Authenticode(data, SOFTPUB_PriorSteps, ARRAY_SIZE(SOFTPUB_PriorSteps));
}
#endif

static HRESULT WINAPI WINTRUST_DefaultPolicy(CRYPT_PROVIDER_DATA *pProvData,
 DWORD dwStepError, DWORD dwRegPolicySettings, DWORD cSigner,
 PWTD_GENERIC_CHAIN_POLICY_SIGNER_INFO rgpSigner, void *pvPolicyArg)
{
    DWORD i;
    CERT_CHAIN_POLICY_STATUS policyStatus = { sizeof(policyStatus), 0 };

    for (i = 0; !policyStatus.dwError && i < cSigner; i++)
    {
        CERT_CHAIN_POLICY_PARA policyPara = { sizeof(policyPara), 0 };

        if (dwRegPolicySettings & WTPF_IGNOREEXPIRATION)
            policyPara.dwFlags |=
             CERT_CHAIN_POLICY_IGNORE_NOT_TIME_VALID_FLAG |
             CERT_CHAIN_POLICY_IGNORE_CTL_NOT_TIME_VALID_FLAG |
             CERT_CHAIN_POLICY_IGNORE_NOT_TIME_NESTED_FLAG;
        if (dwRegPolicySettings & WTPF_IGNOREREVOKATION)
            policyPara.dwFlags |=
             CERT_CHAIN_POLICY_IGNORE_END_REV_UNKNOWN_FLAG |
             CERT_CHAIN_POLICY_IGNORE_CTL_SIGNER_REV_UNKNOWN_FLAG |
             CERT_CHAIN_POLICY_IGNORE_CA_REV_UNKNOWN_FLAG |
             CERT_CHAIN_POLICY_IGNORE_ROOT_REV_UNKNOWN_FLAG;
        CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE,
         rgpSigner[i].pChainContext, &policyPara, &policyStatus);
    }
    return policyStatus.dwError;
}

HRESULT WINAPI GenericChainFinalProv(CRYPT_PROVIDER_DATA *data)
{
    HRESULT err = NO_ERROR; /* not a typo, MS confused the types */
    WTD_GENERIC_CHAIN_POLICY_DATA *policyData =
     data->pWintrustData->pPolicyCallbackData;

    TRACE("(%p)\n", data);

    if (data->pWintrustData->dwUIChoice != WTD_UI_NONE)
        FIXME("unimplemented for UI choice %ld\n",
         data->pWintrustData->dwUIChoice);
#ifdef __REACTOS__
    err = SOFTPUB_PriorStepError(data);
    if (err)
        TRACE("earlier step failed with %08lx\n", err);
    else
#endif
    if (!data->csSigners)
        err = TRUST_E_NOSIGNATURE;
    else
    {
        PFN_WTD_GENERIC_CHAIN_POLICY_CALLBACK policyCallback;
        void *policyArg;
        WTD_GENERIC_CHAIN_POLICY_SIGNER_INFO *signers = NULL;

        if (policyData)
        {
            policyCallback = policyData->pfnPolicyCallback;
            policyArg = policyData->pvPolicyArg;
        }
        else
        {
            policyCallback = WINTRUST_DefaultPolicy;
            policyArg = NULL;
        }
        if (data->csSigners)
        {
            DWORD i;

            signers = data->psPfns->pfnAlloc(
             data->csSigners * sizeof(WTD_GENERIC_CHAIN_POLICY_SIGNER_INFO));
            if (signers)
            {
                for (i = 0; i < data->csSigners; i++)
                {
                    signers[i].cbSize =
                     sizeof(WTD_GENERIC_CHAIN_POLICY_SIGNER_INFO);
                    signers[i].pChainContext =
                     data->pasSigners[i].pChainContext;
                    signers[i].dwSignerType = data->pasSigners[i].dwSignerType;
                    signers[i].pMsgSignerInfo = data->pasSigners[i].psSigner;
                    signers[i].dwError = data->pasSigners[i].dwError;
                    if (data->pasSigners[i].csCounterSigners)
                        FIXME("unimplemented for counter signers\n");
                    signers[i].cCounterSigner = 0;
                    signers[i].rgpCounterSigner = NULL;
                }
            }
            else
                err = ERROR_OUTOFMEMORY;
        }
        if (err == NO_ERROR)
            err = policyCallback(data, TRUSTERROR_STEP_FINAL_POLICYPROV,
             data->dwRegPolicySettings, data->csSigners, signers, policyArg);
        data->psPfns->pfnFree(signers);
    }
    if (err != NO_ERROR)
        data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV] = err;
    TRACE("returning %ld (%08lx)\n", err == NO_ERROR ? S_OK : S_FALSE,
     data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV]);
#ifdef __REACTOS__
    return err;
#else
    return err == NO_ERROR ? S_OK : S_FALSE;
#endif
}

HRESULT WINAPI SoftpubCleanup(CRYPT_PROVIDER_DATA *data)
{
#ifdef __REACTOS__
    DWORD i;
#else
    DWORD i, j;
#endif

    for (i = 0; i < data->csSigners; i++)
    {
#ifdef __REACTOS__
        WINTRUST_FreeSgnr(data, &data->pasSigners[i]);
#else
        for (j = 0; j < data->pasSigners[i].csCertChain; j++)
            CertFreeCertificateContext(data->pasSigners[i].pasCertChain[j].pCert);
        data->psPfns->pfnFree(data->pasSigners[i].pasCertChain);
        data->psPfns->pfnFree(data->pasSigners[i].psSigner);
        CertFreeCertificateChain(data->pasSigners[i].pChainContext);
#endif
    }
    data->psPfns->pfnFree(data->pasSigners);

    for (i = 0; i < data->chStores; i++)
        CertCloseStore(data->pahStores[i], 0);
    data->psPfns->pfnFree(data->pahStores);

    if (data->pPDSip)
    {
        data->psPfns->pfnFree(data->pPDSip->pSip);
        data->psPfns->pfnFree(data->pPDSip->pCATSip);
        data->psPfns->pfnFree(data->pPDSip->psSipSubjectInfo);
        data->psPfns->pfnFree(data->pPDSip->psSipCATSubjectInfo);
        data->psPfns->pfnFree(data->pPDSip->psIndirectData);
    }

    if (WVT_ISINSTRUCT(CRYPT_PROVIDER_DATA, data->cbStruct, pSigState) && data->pSigState)
    {
        CryptMsgClose(data->pSigState->hPrimarySig);
        for (i = 0; i < data->pSigState->cSecondarySigs; ++i)
            CryptMsgClose(data->pSigState->rhSecondarySigs[i]);
        data->psPfns->pfnFree(data->pSigState);
    }
    CryptMsgClose(data->hMsg);

    if (data->fOpenedFile &&
     data->pWintrustData->dwUnionChoice == WTD_CHOICE_FILE &&
     data->pWintrustData->pFile)
    {
        CloseHandle(data->pWintrustData->pFile->hFile);
        data->pWintrustData->pFile->hFile = INVALID_HANDLE_VALUE;
        data->fOpenedFile = FALSE;
    }

    return S_OK;
}

HRESULT WINAPI HTTPSCertificateTrust(CRYPT_PROVIDER_DATA *data)
{
    FIXME("(%p)\n", data);
    return S_OK;
}

HRESULT WINAPI HTTPSFinalProv(CRYPT_PROVIDER_DATA *data)
{
#ifdef __REACTOS__
    DWORD err;

    FIXME("(%p)\n", data);
    if ((err = SOFTPUB_PriorStepError(data)))
        data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV] = err;
    return err;
#else
    FIXME("(%p)\n", data);
    return S_OK;
#endif
}

#ifdef __REACTOS__
static BOOL DRIVER_GetOsAttribute(CRYPT_PROVIDER_DATA *data, WCHAR *value, DWORD count)
{
    WINTRUST_CATALOG_INFO *catalog = data->pWintrustData->pCatalog;
    CRYPTCATATTRIBUTE *attr = NULL;
    CRYPTCATMEMBER *member;
    DWORD length = 0;
    HANDLE cat;

    value[0] = 0;
    if (!catalog)
        return FALSE;
    cat = CryptCATOpen((WCHAR *)catalog->pcwszCatalogFilePath, CRYPTCAT_OPEN_EXISTING, data->hProv, 0, 0);
    if (cat == INVALID_HANDLE_VALUE)
        return FALSE;

    member = SOFTPUB_FindCatalogMember(cat, catalog);
    if (member)
        attr = CryptCATGetAttrInfo(cat, member, (WCHAR *)L"OSAttr");
    if (!attr)
        attr = CryptCATGetCatAttrInfo(cat, (WCHAR *)L"OSAttr");
    if (attr && attr->pbValue)
    {
        const WCHAR *text = (const WCHAR *)attr->pbValue;

        while (length < attr->cbValue / sizeof(WCHAR) && length < count - 1 && text[length])
        {
            value[length] = text[length];
            length++;
        }
        value[length] = 0;
    }
    CryptCATClose(cat);
    return attr != NULL;
}

static BOOL DRIVER_ParseNumber(const WCHAR **text, DWORD *number)
{
    WCHAR *end;

    *number = wcstoul(*text, &end, 10);
    if (end == *text)
        return FALSE;
    *text = end;
    return TRUE;
}

static int DRIVER_CompareVersion(DWORD major, DWORD minor, const DRIVER_VER_MAJORMINOR *bound)
{
    if (major != bound->dwMajor)
        return major < bound->dwMajor ? -1 : 1;
    if (minor != bound->dwMinor)
        return minor < bound->dwMinor ? -1 : 1;
    return 0;
}

static BOOL DRIVER_OsEntryMatches(const DRIVER_VER_INFO *info, const WCHAR *entry)
{
    static const DRIVER_VER_MAJORMINOR none = { 0, 0 };
    const DRIVER_VER_MAJORMINOR *version_low = &none, *version_high = &none;
    DWORD platform, major, minor = 0, build = 0, build_low = 0, build_high = 0;
    BOOL has_build = FALSE;
    int low, high;

    if (WVT_ISINSTRUCT(DRIVER_VER_INFO, info->cbStruct, sOSVersionHigh))
    {
        version_low = &info->sOSVersionLow;
        version_high = &info->sOSVersionHigh;
    }
    if (WVT_ISINSTRUCT(DRIVER_VER_INFO, info->cbStruct, dwBuildNumberHigh))
    {
        build_low = info->dwBuildNumberLow;
        build_high = info->dwBuildNumberHigh;
    }

    if (!DRIVER_ParseNumber(&entry, &platform) || *entry++ != ':' || !DRIVER_ParseNumber(&entry, &major))
        return FALSE;
    if (*entry == '.')
    {
        entry++;
        if (!DRIVER_ParseNumber(&entry, &minor))
            return FALSE;
        if (*entry == '.')
        {
            entry++;
            has_build = DRIVER_ParseNumber(&entry, &build);
        }
    }
    if (platform != info->dwPlatform)
        return FALSE;

    low = DRIVER_CompareVersion(major, minor, version_low);
    high = DRIVER_CompareVersion(major, minor, version_high);
    if (low < 0 || high > 0)
        return FALSE;
    if (!has_build || (!build_low && !build_high))
        return TRUE;
    if (!low && build < build_low)
        return FALSE;
    if (!high && build > build_high)
        return FALSE;
    return TRUE;
}

static BOOL DRIVER_MatchOsAttribute(const DRIVER_VER_INFO *info, const WCHAR *os_attr, WCHAR *version)
{
    const WCHAR *entry = os_attr, *end;

    while (*entry)
    {
        end = wcschr(entry, ',');
        if (!end)
            end = entry + wcslen(entry);
        if (DRIVER_OsEntryMatches(info, entry))
        {
            memcpy(version, os_attr, (end - os_attr) * sizeof(WCHAR));
            version[end - os_attr] = 0;
            return TRUE;
        }
        entry = *end ? end + 1 : end;
    }
    return FALSE;
}

HRESULT WINAPI DriverInitializePolicy(CRYPT_PROVIDER_DATA *data)
{
    TRACE("(%p)\n", data);

    if (!data->pszUsageOID)
        data->pszUsageOID = (char *)szOID_WHQL_CRYPTO;
    return SoftpubInitialize(data);
}

static DWORD DRIVER_CheckMicrosoftRoot(CRYPT_PROVIDER_DATA *data)
{
    CERT_CHAIN_POLICY_STATUS status = { sizeof(status) };
    CERT_CHAIN_POLICY_PARA para = { sizeof(para) };
    DWORD i;

    for (i = 0; i < data->csSigners; i++)
    {
        if (!data->pasSigners[i].pChainContext ||
            !CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_MICROSOFT_ROOT, data->pasSigners[i].pChainContext,
                                              &para, &status))
            return CERT_E_UNTRUSTEDROOT;
        if (status.dwError)
            return status.dwError;
    }
    return ERROR_SUCCESS;
}

HRESULT WINAPI DriverFinalPolicy(CRYPT_PROVIDER_DATA *data)
{
    static const DWORD signature_steps[] = { TRUSTERROR_STEP_FINAL_WVTINIT, TRUSTERROR_STEP_FINAL_SIGPROV,
                                             TRUSTERROR_STEP_FINAL_CERTPROV, TRUSTERROR_STEP_FINAL_CERTCHKPROV };
    DRIVER_VER_INFO *info = data->pWintrustData->pPolicyCallbackData;
    WCHAR os_attr[MAX_PATH];
    PCCERT_CONTEXT signer;
    HRESULT ret;
    DWORD err;

    const DWORD *steps = data->hMsg ? signature_steps : SOFTPUB_PriorSteps;
    DWORD count = data->hMsg ? ARRAY_SIZE(signature_steps) : ARRAY_SIZE(SOFTPUB_PriorSteps);

    TRACE("(%p)\n", data);

    if ((err = SOFTPUB_FirstStepError(data, steps, count)))
        return err;
    err = DRIVER_CheckMicrosoftRoot(data);
    if (!err && info && !WVT_ISINSTRUCT(DRIVER_VER_INFO, info->cbStruct, pcSignerCertContext))
        err = ERROR_INVALID_PARAMETER;
    if (err)
    {
        data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV] = err;
        return err;
    }
    if ((ret = SOFTPUB_Authenticode(data, steps, count)) != S_OK)
        return ret;

    if (info)
    {
        info->wszVersion[0] = 0;
        if (data->csSigners && data->pasSigners[0].csCertChain)
        {
            signer = data->pasSigners[0].pasCertChain[0].pCert;
            CertGetNameStringW(signer, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, info->wszSignedBy, MAX_PATH);
            info->pcSignerCertContext = CertDuplicateCertificateContext(signer);
        }
    }
    if ((err = data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_OBJPROV]))
    {
        data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV] = err;
        return err;
    }
    if (!info)
        return S_OK;

    if (data->pWintrustData->dwUnionChoice != WTD_CHOICE_CATALOG)
        return S_OK;
    if (!DRIVER_GetOsAttribute(data, os_attr, ARRAY_SIZE(os_attr)))
    {
        /* TODO: INFO_NEEDED: Windows 11 result for a signed catalog member without OSAttr */
        return S_OK;
    }
    if (!info->dwPlatform)
    {
        wcscpy(info->wszVersion, os_attr);
        return S_OK;
    }
    if (DRIVER_MatchOsAttribute(info, os_attr, info->wszVersion))
        return S_OK;
    data->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV] = ERROR_APP_WRONG_OS;
    return ERROR_APP_WRONG_OS;
}

HRESULT WINAPI DriverCleanupPolicy(CRYPT_PROVIDER_DATA *data)
{
    TRACE("(%p)\n", data);

    return SoftpubCleanup(data);
}
#endif

#ifdef __REACTOS__
/***********************************************************************
 *		SoftpubLoadDefUsageCallData (WINTRUST.@)
 */
BOOL SoftpubLoadDefUsageCallData(const char *pszUsageOID, CRYPT_PROVIDER_DEFUSAGE *psDefUsage)
{
    HTTPSPolicyCallbackData *policy;

    TRACE("(%s %p)\n", debugstr_a(pszUsageOID), psDefUsage);

    if (!pszUsageOID || !psDefUsage)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!(policy = calloc(1, sizeof(*policy))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    policy->cbStruct = sizeof(*policy);
    policy->dwAuthType = strcmp(pszUsageOID, szOID_PKIX_KP_CLIENT_AUTH) ? AUTHTYPE_SERVER : AUTHTYPE_CLIENT;
    policy->fdwChecks = SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    psDefUsage->pDefPolicyCallbackData = policy;
    psDefUsage->pDefSIPClientData = NULL;
    return TRUE;
}

/***********************************************************************
 *		SoftpubFreeDefUsageCallData (WINTRUST.@)
 */
BOOL SoftpubFreeDefUsageCallData(const char *pszUsageOID, CRYPT_PROVIDER_DEFUSAGE *psDefUsage)
{
    TRACE("(%s %p)\n", debugstr_a(pszUsageOID), psDefUsage);

    if (psDefUsage)
        free(psDefUsage->pDefPolicyCallbackData);
    return TRUE;
}
#endif

#ifdef __REACTOS__
/***********************************************************************
 *		OfficeInitializePolicy (WINTRUST.@)
 */
HRESULT WINAPI OfficeInitializePolicy(CRYPT_PROVIDER_DATA *data)
{
    TRACE("(%p)\n", data);

    return E_NOTIMPL;
}

/***********************************************************************
 *		OfficeCleanupPolicy (WINTRUST.@)
 */
HRESULT WINAPI OfficeCleanupPolicy(CRYPT_PROVIDER_DATA *data)
{
    TRACE("(%p)\n", data);

    return E_NOTIMPL;
}
#endif

#ifdef __REACTOS__
static void DUMP_Line(HANDLE file, const char *format, ...)
{
    char line[1024];
    DWORD written;
    va_list args;
    int len;

    va_start(args, format);
    len = vsnprintf(line, sizeof(line) - 3, format, args);
    va_end(args);
    if (len < 0 || len > (int)sizeof(line) - 3)
        len = sizeof(line) - 3;
    memcpy(line + len, "\r\n", 3);
    WriteFile(file, line, len + 3, &written, NULL);
}

static const char *DUMP_Time(const FILETIME *time, char *buffer, DWORD size)
{
    SYSTEMTIME st;

    if (!FileTimeToSystemTime(time, &st))
        st = (SYSTEMTIME){ 0 };
    snprintf(buffer, size, "%u/%u/%u - %u:%02u:%02u %s", st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute,
             st.wSecond, st.wHour < 12 ? "AM" : "PM");
    return buffer;
}

static const char *DUMP_Guid(const GUID *guid, char *buffer, DWORD size)
{
    static const GUID zero;

    if (!guid)
        guid = &zero;
    snprintf(buffer, size, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", guid->Data1, guid->Data2,
             guid->Data3, guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4],
             guid->Data4[5], guid->Data4[6], guid->Data4[7]);
    return buffer;
}

static void DUMP_Certificate(HANDLE file, const CRYPT_PROVIDER_CERT *cert, const char *indent, BOOL last)
{
    char issuer[256] = "", subject[256] = "", before[64], after[64];

    if (cert->pCert)
    {
        CertGetNameStringA(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, NULL, issuer,
                           sizeof(issuer));
        CertGetNameStringA(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, subject, sizeof(subject));
    }
    DUMP_Line(file, "%s|   |.. cbStruct:             %lu", indent, cert->cbStruct);
    DUMP_Line(file, "%s|   |.. pCert:                0x%p", indent, cert->pCert);
    if (cert->pCert)
    {
        DUMP_Line(file, "%s|   |   |.. dwCertEncoding:   0x%08lx", indent, cert->pCert->dwCertEncodingType);
        DUMP_Line(file, "%s|   |   |.. pCertInfo:", indent);
        DUMP_Line(file, "%s|   |   |   |.. Issuer:       %s", indent, issuer);
        DUMP_Line(file, "%s|   |   |   |.. NotBefore:    %s", indent,
                  DUMP_Time(&cert->pCert->pCertInfo->NotBefore, before, sizeof(before)));
        DUMP_Line(file, "%s|   |   |   |.. NotAfter:     %s", indent,
                  DUMP_Time(&cert->pCert->pCertInfo->NotAfter, after, sizeof(after)));
        DUMP_Line(file, "%s|   |   |   +.. Subject:      %s", indent, subject);
        DUMP_Line(file, "%s|   |   +.. hCertStore:       0x%p", indent, cert->pCert->hCertStore);
    }
    DUMP_Line(file, "%s|   |.. fCommercial:          %s", indent, cert->fCommercial ? "True" : "False");
    DUMP_Line(file, "%s|   |.. fTrustedRoot:         %s", indent, cert->fTrustedRoot ? "True" : "False");
    DUMP_Line(file, "%s|   |.. fSelfSigned:          %s", indent, cert->fSelfSigned ? "True" : "False");
    DUMP_Line(file, "%s|   |.. fTestCert:            %s", indent, cert->fTestCert ? "True" : "False");
    DUMP_Line(file, "%s|   |.. dwRevokedReason:      0x%08lx", indent, cert->dwRevokedReason);
    DUMP_Line(file, "%s|   |.. dwConfidence:         0x%08lx", indent, cert->dwConfidence);
    DUMP_Line(file, "%s|   |.. pTrustListContext:    0x%p", indent, cert->pTrustListContext);
    if (last)
        DUMP_Line(file, "|   |   +-- +.. dwError:              0x%08lx", cert->dwError);
    else
        DUMP_Line(file, "%s|   +.. dwError:              0x%08lx", indent, cert->dwError);
}

static void DUMP_Signer(HANDLE file, const CRYPT_PROVIDER_SGNR *signer)
{
    char time[64];
    DWORD i;

    DUMP_Line(file, "|   |.. cbStruct:                     %lu", signer->cbStruct);
    DUMP_Line(file, "|   |.. sftVerifyAsOf:                %s", DUMP_Time(&signer->sftVerifyAsOf, time, sizeof(time)));
    DUMP_Line(file, "|   |.. dwSignerType:                 0x%08lx", signer->dwSignerType);
    DUMP_Line(file, "|   |.. csCertChain:                  %lu", signer->csCertChain);
    for (i = 0; i < signer->csCertChain; i++)
    {
        DUMP_Line(file, i + 1 < signer->csCertChain ? "|   |   |.. casCertChain[%lu]:" : "|   |   +.. casCertChain[%lu]:", i);
        DUMP_Certificate(file, &signer->pasCertChain[i], "|   |   ", i + 1 == signer->csCertChain);
    }
    DUMP_Line(file, "|   |.. psSigner:                     0x%p", signer->psSigner);
    DUMP_Line(file, "|   |.. dwError:                      0x%08lx", signer->dwError);
    DUMP_Line(file, "|   +.. csCounterSigners:             %lu", signer->csCounterSigners);
}

/***********************************************************************
 *		SoftpubDumpStructure (WINTRUST.@)
 */
HRESULT WINAPI SoftpubDumpStructure(CRYPT_PROVIDER_DATA *data)
{
    WINTRUST_DATA *trust;
    char text[128], hash[129];
    DWORD i, j;
    HANDLE file;

    TRACE("(%p)\n", data);

    if (!data)
        return S_FALSE;
    file = CreateFileW(L"C:\\TRUSTPOL.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return S_FALSE;

    trust = data->pWintrustData;
    DUMP_Line(file, "CRYPT_PROVIDER_DATA:");
    DUMP_Line(file, "+======================================================");
    DUMP_Line(file, "+-- pWintrustData:");
    if (trust)
    {
        DUMP_Line(file, "|   |.. cbStruct:                     %lu", trust->cbStruct);
        DUMP_Line(file, "|   |.. pPolicyCallbackData:          %p", trust->pPolicyCallbackData);
        DUMP_Line(file, "|   |.. dwUIChoice:                   %lu", trust->dwUIChoice);
        DUMP_Line(file, "|   |.. fdRevocationChecks:           %lu", trust->fdwRevocationChecks);
        DUMP_Line(file, "|   |.. dwUnionChoice:                %lu", trust->dwUnionChoice);
        if (trust->dwUnionChoice == WTD_CHOICE_CATALOG && trust->pCatalog)
        {
            WINTRUST_CATALOG_INFO *catalog = trust->pCatalog;

            hash[0] = 0;
            for (i = 0; catalog->pbCalculatedFileHash && i < catalog->cbCalculatedFileHash && i < 64; i++)
                snprintf(hash + i * 2, 3, "%02X", catalog->pbCalculatedFileHash[i]);
            DUMP_Line(file, "|   +-- pCatalog:");
            DUMP_Line(file, "|       |.. cbStruct:                 %lu", catalog->cbStruct);
            DUMP_Line(file, "|       |.. dwCatalogVersion:         0x%lx", catalog->dwCatalogVersion);
            DUMP_Line(file, "|       |.. pcwszCatalogFilePath:     %ls", catalog->pcwszCatalogFilePath ? catalog->pcwszCatalogFilePath : L"(null)");
            DUMP_Line(file, "|       |.. pcwszMemberTag:           %ls", catalog->pcwszMemberTag ? catalog->pcwszMemberTag : L"(null)");
            DUMP_Line(file, "|       |.. pcwszMemberFilePath:      %ls", catalog->pcwszMemberFilePath ? catalog->pcwszMemberFilePath : L"(null)");
            DUMP_Line(file, "|       |.. hMemberFile:              0x%p", catalog->hMemberFile);
            DUMP_Line(file, "|       |.. pbCaclulatedFileHash:     %s", hash);
            DUMP_Line(file, "|       +-- cbCaclulatedFileHash:     %lu", catalog->cbCalculatedFileHash);
        }
        else if (trust->dwUnionChoice == WTD_CHOICE_FILE && trust->pFile)
        {
            DUMP_Line(file, "|   +-- pFile:");
            DUMP_Line(file, "|       |.. cbStruct:                 %lu", trust->pFile->cbStruct);
            DUMP_Line(file, "|       |.. pcwszFilePath:            %ls", trust->pFile->pcwszFilePath ? trust->pFile->pcwszFilePath : L"(null)");
            DUMP_Line(file, "|       |.. hFile:                    0x%p", trust->pFile->hFile);
            DUMP_Line(file, "|       +-- pgKnownSubject:           %s",
                      trust->pFile->pgKnownSubject ? DUMP_Guid(trust->pFile->pgKnownSubject, text, sizeof(text)) : "");
        }
        else if (trust->dwUnionChoice == WTD_CHOICE_BLOB && trust->pBlob)
        {
            DUMP_Line(file, "|   +-- pBlob:");
            DUMP_Line(file, "|       |.. cbStruct:                 %lu", trust->pBlob->cbStruct);
            DUMP_Line(file, "        |.. gSubject:                 %s", DUMP_Guid(&trust->pBlob->gSubject, text, sizeof(text)));
            DUMP_Line(file, "|       |.. pcwszDisplayName:         %ls", trust->pBlob->pcwszDisplayName ? trust->pBlob->pcwszDisplayName : L"(null)");
            DUMP_Line(file, "|       |.. cbMemObject:              %lu", trust->pBlob->cbMemObject);
            DUMP_Line(file, "|       |.. cbMemSignedMsg:           %lu", trust->pBlob->cbMemSignedMsg);
            DUMP_Line(file, "|       +.. pbMemSignedMsg:           0x%p", trust->pBlob->pbMemSignedMsg);
        }
        else if (trust->dwUnionChoice == WTD_CHOICE_CERT && trust->pCert)
        {
            DUMP_Line(file, "|   +-- pCert:");
            DUMP_Line(file, "|       |.. cbStruct:                 %lu", trust->pCert->cbStruct);
            DUMP_Line(file, "|       |.. pcwszDisplayName:         %ls", trust->pCert->pcwszDisplayName ? trust->pCert->pcwszDisplayName : L"(null)");
            DUMP_Line(file, "|       |.. psCertContext:            0x%p", trust->pCert->psCertContext);
            DUMP_Line(file, "|       |.. chStores:                 %lu", trust->pCert->chStores);
            for (i = 0; trust->pCert->pahStores && i < trust->pCert->chStores; i++)
                DUMP_Line(file, "|       |.. pahStores[%02lu]:        0x%p", i, trust->pCert->pahStores[i]);
            DUMP_Line(file, "|       |.. dwFlags:                  0x%08lx", trust->pCert->dwFlags);
            DUMP_Line(file, "|       |-- psftVerifyAsOf:               %s",
                      trust->pCert->psftVerifyAsOf ? DUMP_Time(trust->pCert->psftVerifyAsOf, text, sizeof(text)) : "");
        }
        /* TODO: INFO_NEEDED: the layout Windows 11 uses for the signer union choice */
    }
    DUMP_Line(file, "|.. WndParent:                        0x%p", data->hWndParent);
    DUMP_Line(file, "|.. pgActionID:                       %s", DUMP_Guid(data->pgActionID, text, sizeof(text)));
    DUMP_Line(file, "|.. hProv:                            0x%Iu", data->hProv);
    DUMP_Line(file, "|.. dwError:                          0x%08lx", data->dwError);
    DUMP_Line(file, "|.. dwRegSecuritySettings:            0x%08lx", data->dwRegSecuritySettings);
    DUMP_Line(file, "|.. dwRegPolicySettings:              0x%08lx", data->dwRegPolicySettings);
    DUMP_Line(file, "|.. dwEncoding:                       0x%08lx", data->dwEncoding);
    DUMP_Line(file, "|.. psPfns:");
    if (data->psPfns)
    {
        const CRYPT_PROVIDER_FUNCTIONS *f = data->psPfns;

        DUMP_Line(file, "|   |.. cbStruct:                     %lu", f->cbStruct);
        DUMP_Line(file, "|   |.. pfnAlloc:                     0x%p", f->pfnAlloc);
        DUMP_Line(file, "|   |.. pfnFree:                      0x%p", f->pfnFree);
        DUMP_Line(file, "|   |.. pfnAddStore2Chain:            0x%p", f->pfnAddStore2Chain);
        DUMP_Line(file, "|   |.. pfnAddSgnr2Chain:             0x%p", f->pfnAddSgnr2Chain);
        DUMP_Line(file, "|   |.. pfnAddCert2Chain:             0x%p", f->pfnAddCert2Chain);
        DUMP_Line(file, "|   |.. pfnAddPrivData2Chain:         0x%p", f->pfnAddPrivData2Chain);
        DUMP_Line(file, "|   |.. pfnInitialize:                0x%p", f->pfnInitialize);
        DUMP_Line(file, "|   |.. pfnObjectTrust:               0x%p", f->pfnObjectTrust);
        DUMP_Line(file, "|   |.. pfnSignatureTrust:            0x%p", f->pfnSignatureTrust);
        DUMP_Line(file, "|   |.. pfnCertificateTrust:          0x%p", f->pfnCertificateTrust);
        DUMP_Line(file, "|   |.. pfnFinalPolicy:               0x%p", f->pfnFinalPolicy);
        DUMP_Line(file, "|   |.. pfnCertCheckPolicy:           0x%p", f->pfnCertCheckPolicy);
        DUMP_Line(file, "|   |.. pfnTestFinalPolicy:           0x%p", f->pfnTestFinalPolicy);
        DUMP_Line(file, "|   |.. pfnCleanupPolicy:             0x%p", f->pfnCleanupPolicy);
        DUMP_Line(file, "|   +.. psUIpfns:");
        if (f->psUIpfns)
        {
            const CRYPT_PROVUI_FUNCS *ui = f->psUIpfns;

            DUMP_Line(file, "|       |.. cbStruct:                 %lu", ui->cbStruct);
            DUMP_Line(file, "|       |.. psUIData:");
            if (ui->psUIData)
            {
                DUMP_Line(file, "|       |   |.. cbStruct:             %lu", ui->psUIData->cbStruct);
                DUMP_Line(file, "|       |   |.. dwFinalError:         0x%08lx", ui->psUIData->dwFinalError);
                DUMP_Line(file, "|       |   |.. pYesButtonText:       %ls", ui->psUIData->pYesButtonText ? ui->psUIData->pYesButtonText : L"(null)");
                DUMP_Line(file, "|       |   |.. pNoButtonText:        %ls", ui->psUIData->pNoButtonText ? ui->psUIData->pNoButtonText : L"(null)");
                DUMP_Line(file, "|       |   |.. pMoreInfoButtonText:  %ls", ui->psUIData->pMoreInfoButtonText ? ui->psUIData->pMoreInfoButtonText : L"(null)");
                DUMP_Line(file, "|       |   |.. pAdvancedLinkText:    %ls", ui->psUIData->pAdvancedLinkText ? ui->psUIData->pAdvancedLinkText : L"(null)");
                DUMP_Line(file, "|       |   |.. pCopyActionText:      %ls", ui->psUIData->pCopyActionText ? ui->psUIData->pCopyActionText : L"(null)");
                DUMP_Line(file, "|       |   |.. pCopyActionTextNoTS:  %ls", ui->psUIData->pCopyActionTextNoTS ? ui->psUIData->pCopyActionTextNoTS : L"(null)");
                DUMP_Line(file, "|       |   |.. pCopyActionTextNotSigned:  %ls", ui->psUIData->pCopyActionTextNotSigned ? ui->psUIData->pCopyActionTextNotSigned : L"(null)");
            }
            DUMP_Line(file, "|       |.. pfnOnMoreInfoClick:       0x%p", ui->pfnOnMoreInfoClick);
            DUMP_Line(file, "|       |.. pfnOnMoreInfoClickDefault:0x%p", ui->pfnOnMoreInfoClickDefault);
            DUMP_Line(file, "|       |.. pfnOnAdvancedClick:       0x%p", ui->pfnOnAdvancedClick);
            DUMP_Line(file, "|       +.. pfnOnAdvancedClickDefault:0x%p", ui->pfnOnAdvancedClickDefault);
        }
    }
    DUMP_Line(file, "|.. padwTrustStepErrors:");
    for (i = 0; data->padwTrustStepErrors && i < data->cdwTrustStepErrors; i++)
        DUMP_Line(file, i + 1 < data->cdwTrustStepErrors ? "|   |.. Step[%02lu]:                     0x%08lx" :
                  "|   +.. Step[%02lu]:                     0x%08lx", i, data->padwTrustStepErrors[i]);
    DUMP_Line(file, "|.. pahStores:");
    for (i = 0; i < data->chStores; i++)
        DUMP_Line(file, i + 1 < data->chStores ? "|   |.. Store[%02lu]:                    0x%p" :
                  "|   +.. Store[%02lu]:                    0x%p", i, data->pahStores[i]);
    DUMP_Line(file, "|.. hMsg:                             0x%p", data->hMsg);
    DUMP_Line(file, "|.. pPDSip:");
    if (data->pPDSip)
    {
        DUMP_Line(file, "|   |.. gSubject:                     %s", DUMP_Guid(&data->pPDSip->gSubject, text, sizeof(text)));
        DUMP_Line(file, "|   |.. pSip:                         0x%p", data->pPDSip->pSip);
        DUMP_Line(file, "|   |.. pCATSip:                      0x%p", data->pPDSip->pCATSip);
        DUMP_Line(file, "|   |.. psSipSubjectInfo:             0x%p", data->pPDSip->psSipSubjectInfo);
        DUMP_Line(file, "|   |.. psSipCATSubjectInfo:          0x%p", data->pPDSip->psSipCATSubjectInfo);
        DUMP_Line(file, "|   +.. psIndirectData:               0x%p", data->pPDSip->psIndirectData);
    }
    DUMP_Line(file, "|.. csSigners:                        %lu", data->csSigners);
    for (i = 0; i < data->csSigners; i++)
    {
        DUMP_Line(file, "|.. pasSigners[%lu]:", i);
        DUMP_Signer(file, &data->pasSigners[i]);
        for (j = 0; j < data->pasSigners[i].csCounterSigners; j++)
        {
            DUMP_Line(file, "|.. pasSigners[%lu] - CounterSigner[%lu]:", i, j);
            DUMP_Signer(file, &data->pasSigners[i].pasCounterSigners[j]);
        }
    }
    DUMP_Line(file, "|.. pszUsageOID:                      %p", data->pszUsageOID);
    DUMP_Line(file, "|.. fRecallWithState:                 %s", data->fRecallWithState ? "TRUE" : "FALSE");
    DUMP_Line(file, "|.. sftSystemTime:                    %s", DUMP_Time(&data->sftSystemTime, text, sizeof(text)));
    DUMP_Line(file, "+======================================================");
    CloseHandle(file);
    return S_OK;
}
#endif
