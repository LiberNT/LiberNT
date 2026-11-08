/* wintrust asn functions
 *
 * Copyright 2007 Juan Lang
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
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wincrypt.h"
#include "wintrust.h"
#include "snmp.h"
#include "winternl.h"
#include "wine/debug.h"
#include "wine/exception.h"

WINE_DEFAULT_DEBUG_CHANNEL(cryptasn);

#ifdef WORDS_BIGENDIAN

#define hton16(x) (x)
#define n16toh(x) (x)

#else

#define hton16(x) RtlUshortByteSwap(x)
#define n16toh(x) RtlUshortByteSwap(x)

#endif

#define ASN_BOOL            (ASN_UNIVERSAL | ASN_PRIMITIVE | 0x01)
#define ASN_BITSTRING       (ASN_UNIVERSAL | ASN_PRIMITIVE | 0x03)
#define ASN_BMPSTRING       (ASN_UNIVERSAL | ASN_PRIMITIVE | 0x1e)

static BOOL CRYPT_EncodeLen(DWORD len, BYTE *pbEncoded, DWORD *pcbEncoded)
{
    DWORD bytesNeeded, significantBytes = 0;

    if (len <= 0x7f)
        bytesNeeded = 1;
    else
    {
        DWORD temp;

        for (temp = len, significantBytes = sizeof(temp); !(temp & 0xff000000);
         temp <<= 8, significantBytes--)
            ;
        bytesNeeded = significantBytes + 1;
    }
    if (!pbEncoded)
    {
        *pcbEncoded = bytesNeeded;
        return TRUE;
    }
    if (*pcbEncoded < bytesNeeded)
    {
        SetLastError(ERROR_MORE_DATA);
        return FALSE;
    }
    if (len <= 0x7f)
        *pbEncoded = (BYTE)len;
    else
    {
        DWORD i;

        *pbEncoded++ = significantBytes | 0x80;
        for (i = 0; i < significantBytes; i++)
        {
            *(pbEncoded + significantBytes - i - 1) = (BYTE)(len & 0xff);
            len >>= 8;
        }
    }
    *pcbEncoded = bytesNeeded;
    return TRUE;
}

static BOOL WINAPI CRYPT_AsnEncodeOctets(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret = TRUE;
    const CRYPT_DATA_BLOB *blob = pvStructInfo;
    DWORD bytesNeeded, lenBytes;

    TRACE("(%ld, %p), %p, %ld\n", blob->cbData, blob->pbData, pbEncoded,
     *pcbEncoded);

    CRYPT_EncodeLen(blob->cbData, NULL, &lenBytes);
    bytesNeeded = 1 + lenBytes + blob->cbData;
    if (!pbEncoded)
        *pcbEncoded = bytesNeeded;
    else if (*pcbEncoded < bytesNeeded)
    {
        *pcbEncoded = bytesNeeded;
        SetLastError(ERROR_MORE_DATA);
        ret = FALSE;
    }
    else
    {
        *pbEncoded++ = ASN_OCTETSTRING;
        CRYPT_EncodeLen(blob->cbData, pbEncoded, &lenBytes);
        pbEncoded += lenBytes;
        if (blob->cbData)
            memcpy(pbEncoded, blob->pbData, blob->cbData);
    }
    TRACE("returning %d\n", ret);
    return ret;
}

BOOL WINAPI WVTAsn1SpcLinkEncode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret = FALSE;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType,
     debugstr_a(lpszStructType), pvStructInfo, pbEncoded,
     pcbEncoded);

    __TRY
    {
        const SPC_LINK *link = pvStructInfo;
        DWORD bytesNeeded, lenBytes;

        switch (link->dwLinkChoice)
        {
        case SPC_FILE_LINK_CHOICE:
        {
            DWORD fileNameLen, fileNameLenBytes;
            LPWSTR ptr;

            fileNameLen = link->pwszFile ?
             lstrlenW(link->pwszFile) * sizeof(WCHAR) : 0;
            CRYPT_EncodeLen(fileNameLen, NULL, &fileNameLenBytes);
            CRYPT_EncodeLen(1 + fileNameLenBytes + fileNameLen, NULL,
             &lenBytes);
            bytesNeeded = 2 + lenBytes + fileNameLenBytes + fileNameLen;
            if (!pbEncoded)
            {
                *pcbEncoded = bytesNeeded;
                ret = TRUE;
            }
            else if (*pcbEncoded < bytesNeeded)
            {
                SetLastError(ERROR_MORE_DATA);
                *pcbEncoded = bytesNeeded;
            }
            else
            {
                *pcbEncoded = bytesNeeded;
                *pbEncoded++ = ASN_CONSTRUCTOR | ASN_CONTEXT | 2;
                CRYPT_EncodeLen(1 + fileNameLenBytes + fileNameLen, pbEncoded,
                 &lenBytes);
                pbEncoded += lenBytes;
                *pbEncoded++ = ASN_CONTEXT;
                CRYPT_EncodeLen(fileNameLen, pbEncoded, &fileNameLenBytes);
                pbEncoded += fileNameLenBytes;
                for (ptr = link->pwszFile; ptr && *ptr; ptr++)
                {
                    *(WCHAR *)pbEncoded = hton16(*ptr);
                    pbEncoded += sizeof(WCHAR);
                }
                ret = TRUE;
            }
            break;
        }
        case SPC_MONIKER_LINK_CHOICE:
        {
            DWORD classIdLenBytes, dataLenBytes, dataLen;
            CRYPT_DATA_BLOB classId = { sizeof(link->Moniker.ClassId),
             (BYTE *)link->Moniker.ClassId };

            CRYPT_EncodeLen(classId.cbData, NULL, &classIdLenBytes);
            CRYPT_EncodeLen(link->Moniker.SerializedData.cbData, NULL,
             &dataLenBytes);
            dataLen = 2 + classIdLenBytes + classId.cbData +
             dataLenBytes + link->Moniker.SerializedData.cbData;
            CRYPT_EncodeLen(dataLen, NULL, &lenBytes);
            bytesNeeded = 1 + dataLen + lenBytes;
            if (!pbEncoded)
            {
                *pcbEncoded = bytesNeeded;
                ret = TRUE;
            }
            else if (*pcbEncoded < bytesNeeded)
            {
                SetLastError(ERROR_MORE_DATA);
                *pcbEncoded = bytesNeeded;
            }
            else
            {
                DWORD size;

                *pcbEncoded = bytesNeeded;
                *pbEncoded++ = ASN_CONSTRUCTOR | ASN_CONTEXT | 1;
                CRYPT_EncodeLen(dataLen, pbEncoded, &lenBytes);
                pbEncoded += lenBytes;
                size = 1 + classIdLenBytes + classId.cbData;
                CRYPT_AsnEncodeOctets(X509_ASN_ENCODING, NULL, &classId,
                 pbEncoded, &size);
                pbEncoded += size;
                size = 1 + dataLenBytes + link->Moniker.SerializedData.cbData;
                CRYPT_AsnEncodeOctets(X509_ASN_ENCODING, NULL,
                 &link->Moniker.SerializedData, pbEncoded, &size);
                pbEncoded += size;
                ret = TRUE;
            }
            break;
        }
        case SPC_URL_LINK_CHOICE:
        {
            LPWSTR ptr;
            DWORD urlLen;

            /* Check for invalid characters in URL */
            ret = TRUE;
            urlLen = 0;
            for (ptr = link->pwszUrl; ptr && *ptr && ret; ptr++)
                if (*ptr > 0x7f)
                {
                    *pcbEncoded = 0;
                    SetLastError(CRYPT_E_INVALID_IA5_STRING);
                    ret = FALSE;
                }
                else
                    urlLen++;
            if (ret)
            {
                CRYPT_EncodeLen(urlLen, NULL, &lenBytes);
                bytesNeeded = 1 + lenBytes + urlLen;
                if (!pbEncoded)
                    *pcbEncoded = bytesNeeded;
                else if (*pcbEncoded < bytesNeeded)
                {
                    SetLastError(ERROR_MORE_DATA);
                    *pcbEncoded = bytesNeeded;
                    ret = FALSE;
                }
                else
                {
                    *pcbEncoded = bytesNeeded;
                    *pbEncoded++ = ASN_CONTEXT;
                    CRYPT_EncodeLen(urlLen, pbEncoded, &lenBytes);
                    pbEncoded += lenBytes;
                    for (ptr = link->pwszUrl; ptr && *ptr; ptr++)
                        *pbEncoded++ = (BYTE)*ptr;
                }
            }
            break;
        }
        default:
            SetLastError(E_INVALIDARG);
        }
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    TRACE("returning %d\n", ret);
    return ret;
}

typedef BOOL (WINAPI *CryptEncodeObjectFunc)(DWORD, LPCSTR, const void *,
 BYTE *, DWORD *);

struct AsnEncodeSequenceItem
{
    const void           *pvStructInfo;
    CryptEncodeObjectFunc encodeFunc;
    DWORD                 size; /* used during encoding, not for your use */
};

static BOOL CRYPT_AsnEncodeSequence(DWORD dwCertEncodingType,
 struct AsnEncodeSequenceItem items[], DWORD cItem, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret;
    DWORD i, dataLen = 0;

    TRACE("%p, %ld, %p, %ld\n", items, cItem, pbEncoded, *pcbEncoded);
    for (i = 0, ret = TRUE; ret && i < cItem; i++)
    {
        ret = items[i].encodeFunc(dwCertEncodingType, NULL,
         items[i].pvStructInfo, NULL, &items[i].size);
        /* Some functions propagate their errors through the size */
        if (!ret)
            *pcbEncoded = items[i].size;
        dataLen += items[i].size;
    }
    if (ret)
    {
        DWORD lenBytes, bytesNeeded;

        CRYPT_EncodeLen(dataLen, NULL, &lenBytes);
        bytesNeeded = 1 + lenBytes + dataLen;
        if (!pbEncoded)
            *pcbEncoded = bytesNeeded;
        else if (*pcbEncoded < bytesNeeded)
        {
            *pcbEncoded = bytesNeeded;
            SetLastError(ERROR_MORE_DATA);
            ret = FALSE;
        }
        else
        {
            *pcbEncoded = bytesNeeded;
            *pbEncoded++ = ASN_SEQUENCE;
            CRYPT_EncodeLen(dataLen, pbEncoded, &lenBytes);
            pbEncoded += lenBytes;
            for (i = 0; ret && i < cItem; i++)
            {
                ret = items[i].encodeFunc(dwCertEncodingType, NULL,
                 items[i].pvStructInfo, pbEncoded, &items[i].size);
                /* Some functions propagate their errors through the size */
                if (!ret)
                    *pcbEncoded = items[i].size;
                pbEncoded += items[i].size;
            }
        }
    }
    TRACE("returning %d\n", ret);
    return ret;
}

static BOOL WINAPI CRYPT_AsnEncodeBits(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret = FALSE;

    __TRY
    {
        const CRYPT_BIT_BLOB *blob = pvStructInfo;
        DWORD bytesNeeded, lenBytes, dataBytes;
        BYTE unusedBits;

        /* yep, MS allows cUnusedBits to be >= 8 */
        if (!blob->cUnusedBits)
        {
            dataBytes = blob->cbData;
            unusedBits = 0;
        }
        else if (blob->cbData * 8 > blob->cUnusedBits)
        {
            dataBytes = (blob->cbData * 8 - blob->cUnusedBits) / 8 + 1;
            unusedBits = blob->cUnusedBits >= 8 ? blob->cUnusedBits / 8 :
             blob->cUnusedBits;
        }
        else
        {
            dataBytes = 0;
            unusedBits = 0;
        }
        CRYPT_EncodeLen(dataBytes + 1, NULL, &lenBytes);
        bytesNeeded = 1 + lenBytes + dataBytes + 1;
        if (!pbEncoded)
        {
            *pcbEncoded = bytesNeeded;
            ret = TRUE;
        }
        else if (*pcbEncoded < bytesNeeded)
        {
            *pcbEncoded = bytesNeeded;
            SetLastError(ERROR_MORE_DATA);
        }
        else
        {
            ret = TRUE;
            *pcbEncoded = bytesNeeded;
            *pbEncoded++ = ASN_BITSTRING;
            CRYPT_EncodeLen(dataBytes + 1, pbEncoded, &lenBytes);
            pbEncoded += lenBytes;
            *pbEncoded++ = unusedBits;
            if (dataBytes)
            {
                BYTE mask = 0xff << unusedBits;

                if (dataBytes > 1)
                {
                    memcpy(pbEncoded, blob->pbData, dataBytes - 1);
                    pbEncoded += dataBytes - 1;
                }
                *pbEncoded = *(blob->pbData + dataBytes - 1) & mask;
            }
        }
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}

struct AsnConstructedItem
{
    BYTE                  tag;
    const void           *pvStructInfo;
    CryptEncodeObjectFunc encodeFunc;
};

static BOOL WINAPI CRYPT_AsnEncodeConstructed(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret;
    const struct AsnConstructedItem *item = pvStructInfo;
    DWORD len;

    if ((ret = item->encodeFunc(dwCertEncodingType, lpszStructType,
     item->pvStructInfo, NULL, &len)))
    {
        DWORD dataLen, bytesNeeded;

        CRYPT_EncodeLen(len, NULL, &dataLen);
        bytesNeeded = 1 + dataLen + len;
        if (!pbEncoded)
            *pcbEncoded = bytesNeeded;
        else if (*pcbEncoded < bytesNeeded)
        {
            *pcbEncoded = bytesNeeded;
            SetLastError(ERROR_MORE_DATA);
            ret = FALSE;
        }
        else
        {
            *pcbEncoded = bytesNeeded;
            *pbEncoded++ = ASN_CONTEXT | ASN_CONSTRUCTOR | item->tag;
            CRYPT_EncodeLen(len, pbEncoded, &dataLen);
            pbEncoded += dataLen;
            ret = item->encodeFunc(dwCertEncodingType, lpszStructType,
             item->pvStructInfo, pbEncoded, &len);
            if (!ret)
            {
                /* Some functions propagate their errors through the size */
                *pcbEncoded = len;
            }
        }
    }
    else
    {
        /* Some functions propagate their errors through the size */
        *pcbEncoded = len;
    }
    return ret;
}


BOOL WINAPI WVTAsn1SpcPeImageDataEncode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    const SPC_PE_IMAGE_DATA *imageData = pvStructInfo;
    BOOL ret = FALSE;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType,
     debugstr_a(lpszStructType), pvStructInfo, pbEncoded,
     pcbEncoded);

    __TRY
    {
        struct AsnEncodeSequenceItem items[2] = {
         { 0 }
        };
        struct AsnConstructedItem constructed = { 0, imageData->pFile,
         WVTAsn1SpcLinkEncode };
        DWORD cItem = 0;

        if (imageData->Flags.cbData)
        {
            items[cItem].pvStructInfo = &imageData->Flags;
            items[cItem].encodeFunc = CRYPT_AsnEncodeBits;
            cItem++;
        }
        if (imageData->pFile)
        {
            items[cItem].pvStructInfo = &constructed;
            items[cItem].encodeFunc = CRYPT_AsnEncodeConstructed;
            cItem++;
        }

        ret = CRYPT_AsnEncodeSequence(dwCertEncodingType, items, cItem,
         pbEncoded, pcbEncoded);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    TRACE("returning %d\n", ret);
    return ret;
}

static BOOL WINAPI CRYPT_AsnEncodeOid(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    LPCSTR pszObjId = pvStructInfo;
    DWORD bytesNeeded = 0, lenBytes;
    BOOL ret = TRUE;
    int firstPos = 0;
    BYTE firstByte = 0;

    TRACE("%s\n", debugstr_a(pszObjId));

#ifdef __REACTOS__
    if (pszObjId && *pszObjId)
#else
    if (pszObjId)
#endif
    {
        const char *ptr;
        int val1, val2;

        if (sscanf(pszObjId, "%d.%d%n", &val1, &val2, &firstPos) != 2)
        {
            SetLastError(CRYPT_E_ASN1_ERROR);
            return FALSE;
        }
        bytesNeeded++;
        firstByte = val1 * 40 + val2;
        ptr = pszObjId + firstPos;
        if (*ptr == '.')
        {
            ptr++;
            firstPos++;
        }
        while (ret && *ptr)
        {
            int pos;

            /* note I assume each component is at most 32-bits long in base 2 */
            if (sscanf(ptr, "%d%n", &val1, &pos) == 1)
            {
                if (val1 >= 0x10000000)
                    bytesNeeded += 5;
                else if (val1 >= 0x200000)
                    bytesNeeded += 4;
                else if (val1 >= 0x4000)
                    bytesNeeded += 3;
                else if (val1 >= 0x80)
                    bytesNeeded += 2;
                else
                    bytesNeeded += 1;
                ptr += pos;
                if (*ptr == '.')
                    ptr++;
            }
            else
            {
                SetLastError(CRYPT_E_ASN1_ERROR);
                return FALSE;
            }
        }
        CRYPT_EncodeLen(bytesNeeded, NULL, &lenBytes);
    }
    else
        lenBytes = 1;
    bytesNeeded += 1 + lenBytes;
    if (pbEncoded)
    {
        if (*pcbEncoded < bytesNeeded)
        {
            SetLastError(ERROR_MORE_DATA);
            ret = FALSE;
        }
        else
        {
            *pbEncoded++ = ASN_OBJECTIDENTIFIER;
            CRYPT_EncodeLen(bytesNeeded - 1 - lenBytes, pbEncoded, &lenBytes);
            pbEncoded += lenBytes;
            if (pszObjId)
            {
                const char *ptr;
                int val, pos;

                *pbEncoded++ = firstByte;
                ptr = pszObjId + firstPos;
                while (ret && *ptr)
                {
                    sscanf(ptr, "%d%n", &val, &pos);
                    {
                        unsigned char outBytes[5];
                        int numBytes, i;

                        if (val >= 0x10000000)
                            numBytes = 5;
                        else if (val >= 0x200000)
                            numBytes = 4;
                        else if (val >= 0x4000)
                            numBytes = 3;
                        else if (val >= 0x80)
                            numBytes = 2;
                        else
                            numBytes = 1;
                        for (i = numBytes; i > 0; i--)
                        {
                            outBytes[i - 1] = val & 0x7f;
                            val >>= 7;
                        }
                        for (i = 0; i < numBytes - 1; i++)
                            *pbEncoded++ = outBytes[i] | 0x80;
                        *pbEncoded++ = outBytes[i];
                        ptr += pos;
                        if (*ptr == '.')
                            ptr++;
                    }
                }
            }
        }
    }
    *pcbEncoded = bytesNeeded;
    return ret;
}

static BOOL WINAPI CRYPT_CopyEncodedBlob(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    const CRYPT_DER_BLOB *blob = pvStructInfo;
    BOOL ret = TRUE;

    if (!pbEncoded)
        *pcbEncoded = blob->cbData;
    else if (*pcbEncoded < blob->cbData)
    {
        *pcbEncoded = blob->cbData;
        SetLastError(ERROR_MORE_DATA);
        ret = FALSE;
    }
    else
    {
        if (blob->cbData)
            memcpy(pbEncoded, blob->pbData, blob->cbData);
        *pcbEncoded = blob->cbData;
    }
    return ret;
}

/* Different from the one in crypt32 */
static BOOL WINAPI CRYPT_AsnEncodeAlgorithmIdWithNullParams(
 DWORD dwCertEncodingType, LPCSTR lpszStructType, const void *pvStructInfo,
 BYTE *pbEncoded, DWORD *pcbEncoded)
{
    const CRYPT_ALGORITHM_IDENTIFIER *algo = pvStructInfo;
    static const BYTE asn1Null[] = { ASN_NULL, 0 };
    static const CRYPT_DATA_BLOB nullBlob = { sizeof(asn1Null),
     (LPBYTE)asn1Null };
    BOOL ret;
    struct AsnEncodeSequenceItem items[2] = {
     { algo->pszObjId, CRYPT_AsnEncodeOid, 0 },
     { NULL,           CRYPT_CopyEncodedBlob, 0 },
    };
    DWORD cItem = 2;

    if (algo->Parameters.cbData)
        items[1].pvStructInfo = &algo->Parameters;
    else if (algo->pszObjId)
        items[1].pvStructInfo = &nullBlob;
    else
        cItem -= 1;
    ret = CRYPT_AsnEncodeSequence(dwCertEncodingType, items, cItem,
     pbEncoded, pcbEncoded);
    return ret;
}

static BOOL WINAPI CRYPT_AsnEncodeAttributeTypeValue(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    const CRYPT_ATTRIBUTE_TYPE_VALUE *typeValue = pvStructInfo;
    struct AsnEncodeSequenceItem items[] = {
     { typeValue->pszObjId,  CRYPT_AsnEncodeOid, 0 },
     { &typeValue->Value,    CRYPT_CopyEncodedBlob, 0 },
    };

    return CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, ARRAY_SIZE(items),
     pbEncoded, pcbEncoded);
}

struct SPCDigest
{
    CRYPT_ALGORITHM_IDENTIFIER DigestAlgorithm;
    CRYPT_HASH_BLOB            Digest;
};

static BOOL WINAPI CRYPT_AsnEncodeSPCDigest(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    const struct SPCDigest *digest = pvStructInfo;
    struct AsnEncodeSequenceItem items[] = {
     { &digest->DigestAlgorithm, CRYPT_AsnEncodeAlgorithmIdWithNullParams, 0 },
     { &digest->Digest,          CRYPT_AsnEncodeOctets, 0 },
    };

    return CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, ARRAY_SIZE(items),
     pbEncoded, pcbEncoded);
}

BOOL WINAPI WVTAsn1SpcIndirectDataContentEncode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret = FALSE;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType,
     debugstr_a(lpszStructType), pvStructInfo, pbEncoded, pcbEncoded);

    __TRY
    {
        const SPC_INDIRECT_DATA_CONTENT *data = pvStructInfo;
        struct AsnEncodeSequenceItem items[] = {
         { &data->Data,            CRYPT_AsnEncodeAttributeTypeValue, 0 },
         { &data->DigestAlgorithm, CRYPT_AsnEncodeSPCDigest, 0 },
        };

        ret = CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, ARRAY_SIZE(items),
         pbEncoded, pcbEncoded);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}

static BOOL WINAPI CRYPT_AsnEncodeBMPString(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret = TRUE;
    LPCWSTR str = pvStructInfo;
    DWORD bytesNeeded, lenBytes, strLen;

    if (str)
        strLen = lstrlenW(str);
    else
        strLen = 0;
    CRYPT_EncodeLen(strLen * 2, NULL, &lenBytes);
    bytesNeeded = 1 + lenBytes + strLen * 2;
    if (!pbEncoded)
        *pcbEncoded = bytesNeeded;
    else if (*pcbEncoded < bytesNeeded)
    {
        *pcbEncoded = bytesNeeded;
        SetLastError(ERROR_MORE_DATA);
        ret = FALSE;
    }
    else
    {
        DWORD i;

        *pcbEncoded = bytesNeeded;
        *pbEncoded++ = ASN_BMPSTRING;
        CRYPT_EncodeLen(strLen * 2, pbEncoded, &lenBytes);
        pbEncoded += lenBytes;
        for (i = 0; i < strLen; i++)
        {
            *pbEncoded++ = (str[i] & 0xff00) >> 8;
            *pbEncoded++ = str[i] & 0x00ff;
        }
    }
    return ret;
}

struct AsnEncodeTagSwappedItem
{
    BYTE                  tag;
    const void           *pvStructInfo;
    CryptEncodeObjectFunc encodeFunc;
};

/* Sort of a wacky hack, it encodes something using the struct
 * AsnEncodeTagSwappedItem's encodeFunc, then replaces the tag byte with the tag
 * given in the struct AsnEncodeTagSwappedItem.
 */
static BOOL WINAPI CRYPT_AsnEncodeSwapTag(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret;
    const struct AsnEncodeTagSwappedItem *item = pvStructInfo;

    ret = item->encodeFunc(dwCertEncodingType, lpszStructType,
     item->pvStructInfo, pbEncoded, pcbEncoded);
    if (ret && pbEncoded)
        *pbEncoded = item->tag;
    return ret;
}

BOOL WINAPI WVTAsn1SpcSpOpusInfoEncode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret = FALSE;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType,
     debugstr_a(lpszStructType), pvStructInfo, pbEncoded, pcbEncoded);

    __TRY
    {
        const SPC_SP_OPUS_INFO *info = pvStructInfo;

        if (info->pMoreInfo &&
         info->pMoreInfo->dwLinkChoice != SPC_URL_LINK_CHOICE &&
         info->pMoreInfo->dwLinkChoice != SPC_MONIKER_LINK_CHOICE &&
         info->pMoreInfo->dwLinkChoice != SPC_FILE_LINK_CHOICE)
            SetLastError(E_INVALIDARG);
        else if (info->pPublisherInfo &&
         info->pPublisherInfo->dwLinkChoice != SPC_URL_LINK_CHOICE &&
         info->pPublisherInfo->dwLinkChoice != SPC_MONIKER_LINK_CHOICE &&
         info->pPublisherInfo->dwLinkChoice != SPC_FILE_LINK_CHOICE)
            SetLastError(E_INVALIDARG);
        else
        {
            struct AsnEncodeSequenceItem items[3] = { { 0 } };
            struct AsnConstructedItem constructed[3] = { { 0 } };
            struct AsnEncodeTagSwappedItem swapped;
            DWORD cItem = 0, cConstructed = 0;

            if (info->pwszProgramName)
            {
                swapped.tag = ASN_CONTEXT;
                swapped.pvStructInfo = info->pwszProgramName;
                swapped.encodeFunc = CRYPT_AsnEncodeBMPString;
                constructed[cConstructed].tag = 0;
                constructed[cConstructed].pvStructInfo = &swapped;
                constructed[cConstructed].encodeFunc = CRYPT_AsnEncodeSwapTag;
                items[cItem].pvStructInfo = &constructed[cConstructed];
                items[cItem].encodeFunc = CRYPT_AsnEncodeConstructed;
                cConstructed++;
                cItem++;
            }
            if (info->pMoreInfo)
            {
                constructed[cConstructed].tag = 1;
                constructed[cConstructed].pvStructInfo = info->pMoreInfo;
                constructed[cConstructed].encodeFunc = WVTAsn1SpcLinkEncode;
                items[cItem].pvStructInfo = &constructed[cConstructed];
                items[cItem].encodeFunc = CRYPT_AsnEncodeConstructed;
                cConstructed++;
                cItem++;
            }
            if (info->pPublisherInfo)
            {
                constructed[cConstructed].tag = 2;
                constructed[cConstructed].pvStructInfo = info->pPublisherInfo;
                constructed[cConstructed].encodeFunc = WVTAsn1SpcLinkEncode;
                items[cItem].pvStructInfo = &constructed[cConstructed];
                items[cItem].encodeFunc = CRYPT_AsnEncodeConstructed;
                cConstructed++;
                cItem++;
            }
            ret = CRYPT_AsnEncodeSequence(X509_ASN_ENCODING,
             items, cItem, pbEncoded, pcbEncoded);
        }
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}

static BOOL CRYPT_AsnEncodeInteger(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret = FALSE;

    __TRY
    {
        DWORD significantBytes, lenBytes, bytesNeeded;
        BYTE padByte = 0;
        BOOL pad = FALSE;
        const CRYPT_INTEGER_BLOB *blob = pvStructInfo;

        significantBytes = blob->cbData;
        if (significantBytes)
        {
            if (blob->pbData[significantBytes - 1] & 0x80)
            {
                /* negative, lop off leading (little-endian) 0xffs */
                for (; significantBytes > 0 &&
                 blob->pbData[significantBytes - 1] == 0xff; significantBytes--)
                    ;
                if (blob->pbData[significantBytes - 1] < 0x80)
                {
                    padByte = 0xff;
                    pad = TRUE;
                }
            }
            else
            {
                /* positive, lop off leading (little-endian) zeroes */
                for (; significantBytes > 0 &&
                 !blob->pbData[significantBytes - 1]; significantBytes--)
                    ;
                if (significantBytes == 0)
                    significantBytes = 1;
                if (blob->pbData[significantBytes - 1] > 0x7f)
                {
                    padByte = 0;
                    pad = TRUE;
                }
            }
        }
        if (pad)
            CRYPT_EncodeLen(significantBytes + 1, NULL, &lenBytes);
        else
            CRYPT_EncodeLen(significantBytes, NULL, &lenBytes);
        bytesNeeded = 1 + lenBytes + significantBytes;
        if (pad)
            bytesNeeded++;
        if (!pbEncoded)
        {
            *pcbEncoded = bytesNeeded;
            ret = TRUE;
        }
        else if (*pcbEncoded < bytesNeeded)
        {
            *pcbEncoded = bytesNeeded;
            SetLastError(ERROR_MORE_DATA);
            ret = FALSE;
        }
        else
        {
            *pcbEncoded = bytesNeeded;
            *pbEncoded++ = ASN_INTEGER;
            if (pad)
            {
                CRYPT_EncodeLen(significantBytes + 1, pbEncoded, &lenBytes);
                pbEncoded += lenBytes;
                *pbEncoded++ = padByte;
            }
            else
            {
                CRYPT_EncodeLen(significantBytes, pbEncoded, &lenBytes);
                pbEncoded += lenBytes;
            }
            for (; significantBytes > 0; significantBytes--)
                *(pbEncoded++) = blob->pbData[significantBytes - 1];
            ret = TRUE;
        }
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
        ret = FALSE;
    }
    __ENDTRY
    return ret;
}

static BOOL WINAPI CRYPT_AsnEncodeInt(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    CRYPT_INTEGER_BLOB blob = { sizeof(INT), (BYTE *)pvStructInfo };

    return CRYPT_AsnEncodeInteger(dwCertEncodingType, X509_MULTI_BYTE_INTEGER,
     &blob, pbEncoded, pcbEncoded);
}

BOOL WINAPI WVTAsn1CatMemberInfoEncode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret = FALSE;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType,
     debugstr_a(lpszStructType), pvStructInfo, pbEncoded, pcbEncoded);

    __TRY
    {
        const CAT_MEMBERINFO *info = pvStructInfo;
        struct AsnEncodeSequenceItem items[] = {
         { info->pwszSubjGuid, CRYPT_AsnEncodeBMPString, 0 },
         { &info->dwCertVersion, CRYPT_AsnEncodeInt, 0 },
        };

        ret = CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, ARRAY_SIZE(items),
         pbEncoded, pcbEncoded);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}

BOOL WINAPI WVTAsn1CatNameValueEncode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret = FALSE;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType,
     debugstr_a(lpszStructType), pvStructInfo, pbEncoded, pcbEncoded);

    __TRY
    {
        const CAT_NAMEVALUE *value = pvStructInfo;
        struct AsnEncodeSequenceItem items[] = {
         { value->pwszTag,   CRYPT_AsnEncodeBMPString, 0 },
         { &value->fdwFlags, CRYPT_AsnEncodeInt, 0 },
         { &value->Value,    CRYPT_AsnEncodeOctets, 0 },
        };

        ret = CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, ARRAY_SIZE(items),
         pbEncoded, pcbEncoded);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}

static BOOL WINAPI CRYPT_AsnEncodeBool(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL val = *(const BOOL *)pvStructInfo, ret;

    TRACE("%d\n", val);

    if (!pbEncoded)
    {
        *pcbEncoded = 3;
        ret = TRUE;
    }
    else if (*pcbEncoded < 3)
    {
        *pcbEncoded = 3;
        SetLastError(ERROR_MORE_DATA);
        ret = FALSE;
    }
    else
    {
        *pcbEncoded = 3;
        *pbEncoded++ = ASN_BOOL;
        *pbEncoded++ = 1;
        *pbEncoded++ = val ? 0xff : 0;
        ret = TRUE;
    }
    TRACE("returning %d (%08lx)\n", ret, GetLastError());
    return ret;
}

BOOL WINAPI WVTAsn1SpcFinancialCriteriaInfoEncode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const void *pvStructInfo, BYTE *pbEncoded,
 DWORD *pcbEncoded)
{
    BOOL ret = FALSE;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType,
     debugstr_a(lpszStructType), pvStructInfo, pbEncoded, pcbEncoded);

    __TRY
    {
        const SPC_FINANCIAL_CRITERIA *criteria = pvStructInfo;
        struct AsnEncodeSequenceItem items[] = {
         { &criteria->fFinancialInfoAvailable, CRYPT_AsnEncodeBool, 0 },
         { &criteria->fMeetsCriteria,          CRYPT_AsnEncodeBool, 0 },
        };

        ret = CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, ARRAY_SIZE(items),
         pbEncoded, pcbEncoded);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}

/* Gets the number of length bytes from the given (leading) length byte */
#define GET_LEN_BYTES(b) ((b) <= 0x7f ? 1 : 1 + ((b) & 0x7f))

/* Helper function to get the encoded length of the data starting at pbEncoded,
 * where pbEncoded[0] is the tag.  If the data are too short to contain a
 * length or if the length is too large for cbEncoded, sets an appropriate
 * error code and returns FALSE.
 */
static BOOL CRYPT_GetLen(const BYTE *pbEncoded, DWORD cbEncoded, DWORD *len)
{
    BOOL ret;

    if (cbEncoded <= 1)
    {
        SetLastError(CRYPT_E_ASN1_CORRUPT);
        ret = FALSE;
    }
    else if (pbEncoded[1] <= 0x7f)
    {
        if (pbEncoded[1] + 1 > cbEncoded)
        {
            SetLastError(CRYPT_E_ASN1_EOD);
            ret = FALSE;
        }
        else
        {
            *len = pbEncoded[1];
            ret = TRUE;
        }
    }
    else if (pbEncoded[1] == 0x80)
    {
        FIXME("unimplemented for indefinite-length encoding\n");
        SetLastError(CRYPT_E_ASN1_CORRUPT);
        ret = FALSE;
    }
    else
    {
        BYTE lenLen = GET_LEN_BYTES(pbEncoded[1]);

        if (lenLen > sizeof(DWORD) + 1)
        {
            SetLastError(CRYPT_E_ASN1_LARGE);
            ret = FALSE;
        }
        else if (lenLen + 2 > cbEncoded)
        {
            SetLastError(CRYPT_E_ASN1_CORRUPT);
            ret = FALSE;
        }
        else
        {
            DWORD out = 0;

            pbEncoded += 2;
            while (--lenLen)
            {
                out <<= 8;
                out |= *pbEncoded++;
            }
            if (out + lenLen + 1 > cbEncoded)
            {
                SetLastError(CRYPT_E_ASN1_EOD);
                ret = FALSE;
            }
            else
            {
                *len = out;
                ret = TRUE;
            }
        }
    }
    return ret;
}

static BOOL WINAPI CRYPT_AsnDecodeOctets(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret;
    DWORD bytesNeeded, dataLen;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    if (!cbEncoded)
    {
        SetLastError(CRYPT_E_ASN1_CORRUPT);
        ret = FALSE;
    }
    else if (pbEncoded[0] != ASN_OCTETSTRING)
    {
        SetLastError(CRYPT_E_ASN1_BADTAG);
        ret = FALSE;
    }
    else if ((ret = CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen)))
    {
        if (dwFlags & CRYPT_DECODE_NOCOPY_FLAG)
            bytesNeeded = sizeof(CRYPT_DATA_BLOB);
        else
            bytesNeeded = dataLen + sizeof(CRYPT_DATA_BLOB);
        if (!pvStructInfo)
            *pcbStructInfo = bytesNeeded;
        else if (*pcbStructInfo < bytesNeeded)
        {
            SetLastError(ERROR_MORE_DATA);
            *pcbStructInfo = bytesNeeded;
            ret = FALSE;
        }
        else
        {
            CRYPT_DATA_BLOB *blob;
            BYTE lenBytes = GET_LEN_BYTES(pbEncoded[1]);

            blob = pvStructInfo;
            blob->cbData = dataLen;
            if (dwFlags & CRYPT_DECODE_NOCOPY_FLAG)
                blob->pbData = (BYTE *)pbEncoded + 1 + lenBytes;
            else
            {
                assert(blob->pbData);
                if (blob->cbData)
                    memcpy(blob->pbData, pbEncoded + 1 + lenBytes,
                     blob->cbData);
            }
        }
    }
    return ret;
}

static BOOL CRYPT_AsnDecodeSPCLinkInternal(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;
    DWORD bytesNeeded = sizeof(SPC_LINK), dataLen;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    if ((ret = CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen)))
    {
        BYTE lenBytes = GET_LEN_BYTES(pbEncoded[1]);
        DWORD realDataLen;

        switch (pbEncoded[0])
        {
        case ASN_CONTEXT:
            bytesNeeded += (dataLen + 1) * sizeof(WCHAR);
            if (!pvStructInfo)
                *pcbStructInfo = bytesNeeded;
            else if (*pcbStructInfo < bytesNeeded)
            {
                *pcbStructInfo = bytesNeeded;
                SetLastError(ERROR_MORE_DATA);
                ret = FALSE;
            }
            else
            {
                PSPC_LINK link = pvStructInfo;
                DWORD i;

                link->dwLinkChoice = SPC_URL_LINK_CHOICE;
                for (i = 0; i < dataLen; i++)
                    link->pwszUrl[i] =
                     *(pbEncoded + 1 + lenBytes + i);
                link->pwszUrl[i] = '\0';
                TRACE("returning url %s\n", debugstr_w(link->pwszUrl));
            }
            break;
        case ASN_CONSTRUCTOR | ASN_CONTEXT | 1:
        {
            CRYPT_DATA_BLOB classId;
            DWORD size = sizeof(classId);

            if ((ret = CRYPT_AsnDecodeOctets(dwCertEncodingType, NULL,
             pbEncoded + 1 + lenBytes, cbEncoded - 1 - lenBytes,
             CRYPT_DECODE_NOCOPY_FLAG, &classId, &size)))
            {
                if (classId.cbData != sizeof(SPC_UUID))
                {
                    SetLastError(CRYPT_E_BAD_ENCODE);
                    ret = FALSE;
                }
                else
                {
                    CRYPT_DATA_BLOB data;

                    /* The tag length for the classId must be 1 since the
                     * length is correct.
                     */
                    size = sizeof(data);
                    if ((ret = CRYPT_AsnDecodeOctets(dwCertEncodingType, NULL,
                     pbEncoded + 3 + lenBytes + classId.cbData,
                     cbEncoded - 3 - lenBytes - classId.cbData,
                     CRYPT_DECODE_NOCOPY_FLAG, &data, &size)))
                    {
                        bytesNeeded += data.cbData;
                        if (!pvStructInfo)
                            *pcbStructInfo = bytesNeeded;
                        else if (*pcbStructInfo < bytesNeeded)
                        {
                            *pcbStructInfo = bytesNeeded;
                            SetLastError(ERROR_MORE_DATA);
                            ret = FALSE;
                        }
                        else
                        {
                            PSPC_LINK link = pvStructInfo;

                            link->dwLinkChoice = SPC_MONIKER_LINK_CHOICE;
                            /* pwszFile pointer was set by caller, copy it
                             * before overwriting it
                             */
                            link->Moniker.SerializedData.pbData =
                             (BYTE *)link->pwszFile;
                            memcpy(link->Moniker.ClassId, classId.pbData,
                             classId.cbData);
                            memcpy(link->Moniker.SerializedData.pbData,
                             data.pbData, data.cbData);
                            link->Moniker.SerializedData.cbData = data.cbData;
                        }
                    }
                }
            }
            break;
        }
        case ASN_CONSTRUCTOR | ASN_CONTEXT | 2:
            if (dataLen && pbEncoded[1 + lenBytes] != ASN_CONTEXT)
                SetLastError(CRYPT_E_ASN1_BADTAG);
            else if ((ret = CRYPT_GetLen(pbEncoded + 1 + lenBytes, dataLen,
             &realDataLen)))
            {
                BYTE realLenBytes = GET_LEN_BYTES(pbEncoded[2 + lenBytes]);

                bytesNeeded += realDataLen + sizeof(WCHAR);
                if (!pvStructInfo)
                    *pcbStructInfo = bytesNeeded;
                else if (*pcbStructInfo < bytesNeeded)
                {
                    *pcbStructInfo = bytesNeeded;
                    SetLastError(ERROR_MORE_DATA);
                    ret = FALSE;
                }
                else
                {
                    PSPC_LINK link = pvStructInfo;
                    DWORD i;
                    const BYTE *ptr = pbEncoded + 2 + lenBytes + realLenBytes;

                    link->dwLinkChoice = SPC_FILE_LINK_CHOICE;
                    for (i = 0; i < realDataLen / sizeof(WCHAR); i++)
                        link->pwszFile[i] =
                         hton16(*(const WORD *)(ptr + i * sizeof(WCHAR)));
                    link->pwszFile[realDataLen / sizeof(WCHAR)] = '\0';
                    TRACE("returning file %s\n", debugstr_w(link->pwszFile));
                }
            }
            else
            {
                bytesNeeded += sizeof(WCHAR);
                if (!pvStructInfo)
                    *pcbStructInfo = bytesNeeded;
                else if (*pcbStructInfo < bytesNeeded)
                {
                    *pcbStructInfo = bytesNeeded;
                    SetLastError(ERROR_MORE_DATA);
                    ret = FALSE;
                }
                else
                {
                    PSPC_LINK link = pvStructInfo;

                    link->dwLinkChoice = SPC_FILE_LINK_CHOICE;
                    link->pwszFile[0] = '\0';
                    ret = TRUE;
                }
            }
            break;
        default:
            SetLastError(CRYPT_E_ASN1_BADTAG);
        }
    }
    TRACE("returning %d\n", ret);
    return ret;
}

BOOL WINAPI WVTAsn1SpcLinkDecode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    __TRY
    {
        DWORD bytesNeeded;

        ret = CRYPT_AsnDecodeSPCLinkInternal(dwCertEncodingType,
         lpszStructType, pbEncoded, cbEncoded, dwFlags, NULL, &bytesNeeded);
        if (ret)
        {
            if (!pvStructInfo)
                *pcbStructInfo = bytesNeeded;
            else if (*pcbStructInfo < bytesNeeded)
            {
                *pcbStructInfo = bytesNeeded;
                SetLastError(ERROR_MORE_DATA);
                ret = FALSE;
            }
            else
            {
                SPC_LINK *link = pvStructInfo;

                link->pwszFile =
                 (LPWSTR)((BYTE *)pvStructInfo + sizeof(SPC_LINK));
                ret = CRYPT_AsnDecodeSPCLinkInternal(dwCertEncodingType,
                 lpszStructType, pbEncoded, cbEncoded, dwFlags, pvStructInfo,
                 pcbStructInfo);
            }
        }
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    TRACE("returning %d\n", ret);
    return ret;
}

typedef BOOL (WINAPI *CryptDecodeObjectFunc)(DWORD, LPCSTR, const BYTE *,
 DWORD, DWORD, void *, DWORD *);

/* tag:
 *     The expected tag of the item.  If tag is 0, decodeFunc is called
 *     regardless of the tag value seen.
 * offset:
 *     A sequence is decoded into a struct.  The offset member is the
 *     offset of this item within that struct.
 * decodeFunc:
 *     The decoder function to use.  If this is NULL, then the member isn't
 *     decoded, but minSize space is reserved for it.
 * minSize:
 *     The minimum amount of space occupied after decoding.  You must set this.
 * optional:
 *     If true, and the tag doesn't match the expected tag for this item,
 *     or the decodeFunc fails with CRYPT_E_ASN1_BADTAG, then minSize space is
 *     filled with 0 for this member.
 * hasPointer, pointerOffset:
 *     If the item has dynamic data, set hasPointer to TRUE, pointerOffset to
 *     the offset within the struct of the data pointer (or to the
 *     first data pointer, if more than one exist).
 * size:
 *     Used by CRYPT_AsnDecodeSequence, not for your use.
 */
struct AsnDecodeSequenceItem
{
    BYTE                  tag;
    DWORD                 offset;
    CryptDecodeObjectFunc decodeFunc;
    DWORD                 minSize;
    BOOL                  optional;
    BOOL                  hasPointer;
    DWORD                 pointerOffset;
    DWORD                 size;
};

/* Align up to a DWORD_PTR boundary
 */
#define ALIGN_DWORD_PTR(x) (((x) + sizeof(DWORD_PTR) - 1) & ~(sizeof(DWORD_PTR) - 1))

#define FINALMEMBERSIZE(s, member) (sizeof(s) - offsetof(s, member))
#define MEMBERSIZE(s, member, nextmember) \
    (offsetof(s, nextmember) - offsetof(s, member))


/* Decodes the items in a sequence, where the items are described in items,
 * the encoded data are in pbEncoded with length cbEncoded.  Decodes into
 * pvStructInfo.  nextData is a pointer to the memory location at which the
 * first decoded item with a dynamic pointer should point.
 * Upon decoding, *cbDecoded is the total number of bytes decoded.
 */
static BOOL CRYPT_AsnDecodeSequenceItems(DWORD dwCertEncodingType,
 struct AsnDecodeSequenceItem items[], DWORD cItem, const BYTE *pbEncoded,
 DWORD cbEncoded, DWORD dwFlags, void *pvStructInfo, BYTE *nextData,
 DWORD *cbDecoded)
{
    BOOL ret;
    DWORD i, decoded = 0;
    const BYTE *ptr = pbEncoded;

    TRACE("%p, %ld, %p, %ld, %08lx, %p, %p, %p\n", items, cItem, pbEncoded,
     cbEncoded, dwFlags, pvStructInfo, nextData, cbDecoded);

    for (i = 0, ret = TRUE; ret && i < cItem; i++)
    {
        if (cbEncoded - (ptr - pbEncoded) != 0)
        {
            DWORD nextItemLen;

            if ((ret = CRYPT_GetLen(ptr, cbEncoded - (ptr - pbEncoded),
             &nextItemLen)))
            {
                BYTE nextItemLenBytes = GET_LEN_BYTES(ptr[1]);

                if (ptr[0] == items[i].tag || !items[i].tag)
                {
                    if (nextData && pvStructInfo && items[i].hasPointer)
                    {
                        TRACE("Setting next pointer to %p\n",
                         nextData);
                        *(BYTE **)((BYTE *)pvStructInfo +
                         items[i].pointerOffset) = nextData;
                    }
                    if (items[i].decodeFunc)
                    {
                        if (pvStructInfo)
                            TRACE("decoding item %ld\n", i);
                        else
                            TRACE("sizing item %ld\n", i);
                        ret = items[i].decodeFunc(dwCertEncodingType,
                         NULL, ptr, 1 + nextItemLenBytes + nextItemLen,
                         dwFlags & ~CRYPT_DECODE_ALLOC_FLAG,
                         pvStructInfo ?  (BYTE *)pvStructInfo + items[i].offset
                         : NULL, &items[i].size);
                        if (ret)
                        {
                            if (items[i].size < items[i].minSize)
                                items[i].size = items[i].minSize;
                            else if (items[i].size > items[i].minSize)
                            {
                                /* Account for alignment padding */
                                items[i].size = ALIGN_DWORD_PTR(items[i].size);
                            }
                            TRACE("item %ld size: %ld\n", i, items[i].size);
                            if (nextData && items[i].hasPointer &&
                             items[i].size > items[i].minSize)
                                nextData += items[i].size - items[i].minSize;
                            ptr += 1 + nextItemLenBytes + nextItemLen;
                            decoded += 1 + nextItemLenBytes + nextItemLen;
                            TRACE("item %ld: decoded %ld bytes\n", i,
                             1 + nextItemLenBytes + nextItemLen);
                        }
                        else if (items[i].optional &&
                         GetLastError() == CRYPT_E_ASN1_BADTAG)
                        {
                            TRACE("skipping optional item %ld\n", i);
                            items[i].size = items[i].minSize;
                            SetLastError(NOERROR);
                            ret = TRUE;
                        }
                        else
                            TRACE("item %ld failed: %08lx\n", i,
                             GetLastError());
                    }
                    else
                    {
                        TRACE("item %ld: decoded %ld bytes\n", i,
                         1 + nextItemLenBytes + nextItemLen);
                        ptr += 1 + nextItemLenBytes + nextItemLen;
                        decoded += 1 + nextItemLenBytes + nextItemLen;
                        items[i].size = items[i].minSize;
                    }
                }
                else if (items[i].optional)
                {
                    TRACE("skipping optional item %ld\n", i);
                    items[i].size = items[i].minSize;
                }
                else
                {
                    TRACE("item %ld: tag %02x doesn't match expected %02x\n",
                     i, ptr[0], items[i].tag);
                    SetLastError(CRYPT_E_ASN1_BADTAG);
                    ret = FALSE;
                }
            }
        }
        else if (items[i].optional)
        {
            TRACE("missing optional item %ld, skipping\n", i);
            items[i].size = items[i].minSize;
        }
        else
        {
            TRACE("not enough bytes for item %ld, failing\n", i);
            SetLastError(CRYPT_E_ASN1_CORRUPT);
            ret = FALSE;
        }
    }
    if (ret)
        *cbDecoded = decoded;
    TRACE("returning %d\n", ret);
    return ret;
}

/* This decodes an arbitrary sequence into a contiguous block of memory
 * (basically, a struct.)  Each element being decoded is described by a struct
 * AsnDecodeSequenceItem, see above.
 * startingPointer is an optional pointer to the first place where dynamic
 * data will be stored.  If you know the starting offset, you may pass it
 * here.  Otherwise, pass NULL, and one will be inferred from the items.
 */
static BOOL CRYPT_AsnDecodeSequence(DWORD dwCertEncodingType,
 struct AsnDecodeSequenceItem items[], DWORD cItem, const BYTE *pbEncoded,
 DWORD cbEncoded, DWORD dwFlags, void *pvStructInfo, DWORD *pcbStructInfo,
 void *startingPointer)
{
    BOOL ret;

    TRACE("%p, %ld, %p, %ld, %08lx, %p, %ld, %p\n", items, cItem, pbEncoded,
     cbEncoded, dwFlags, pvStructInfo, *pcbStructInfo, startingPointer);

    if (pbEncoded[0] == ASN_SEQUENCE)
    {
        DWORD dataLen;

        if ((ret = CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen)))
        {
            DWORD lenBytes = GET_LEN_BYTES(pbEncoded[1]), cbDecoded;
            const BYTE *ptr = pbEncoded + 1 + lenBytes;

            cbEncoded -= 1 + lenBytes;
            if (cbEncoded < dataLen)
            {
                TRACE("dataLen %ld exceeds cbEncoded %ld, failing\n", dataLen,
                 cbEncoded);
                SetLastError(CRYPT_E_ASN1_CORRUPT);
                ret = FALSE;
            }
            else
                ret = CRYPT_AsnDecodeSequenceItems(dwFlags, items, cItem, ptr,
                 cbEncoded, dwFlags, NULL, NULL, &cbDecoded);
            if (ret && cbDecoded != dataLen)
            {
                TRACE("expected %ld decoded, got %ld, failing\n", dataLen,
                 cbDecoded);
                SetLastError(CRYPT_E_ASN1_CORRUPT);
                ret = FALSE;
            }
            if (ret)
            {
                DWORD i, bytesNeeded = 0, structSize = 0;

                for (i = 0; i < cItem; i++)
                {
                    bytesNeeded += items[i].size;
                    structSize += items[i].minSize;
                }
                if (!pvStructInfo)
                    *pcbStructInfo = bytesNeeded;
                else if (*pcbStructInfo < bytesNeeded)
                {
                    SetLastError(ERROR_MORE_DATA);
                    *pcbStructInfo = bytesNeeded;
                    ret = FALSE;
                }
                else
                {
                    BYTE *nextData;

                    *pcbStructInfo = bytesNeeded;
                    if (startingPointer)
                        nextData = startingPointer;
                    else
                        nextData = (BYTE *)pvStructInfo + structSize;
                    memset(pvStructInfo, 0, structSize);
                    ret = CRYPT_AsnDecodeSequenceItems(dwFlags, items, cItem,
                     ptr, cbEncoded, dwFlags, pvStructInfo, nextData,
                     &cbDecoded);
                }
            }
        }
    }
    else
    {
        SetLastError(CRYPT_E_ASN1_BADTAG);
        ret = FALSE;
    }
    TRACE("returning %d (%08lx)\n", ret, GetLastError());
    return ret;
}

static BOOL WINAPI CRYPT_AsnDecodeBitsInternal(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret;

    TRACE("(%p, %ld, 0x%08lx, %p, %ld)\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    if (pbEncoded[0] == ASN_BITSTRING)
    {
        DWORD bytesNeeded, dataLen;

        if ((ret = CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen)))
        {
            if (dwFlags & CRYPT_DECODE_NOCOPY_FLAG)
                bytesNeeded = sizeof(CRYPT_BIT_BLOB);
            else
                bytesNeeded = dataLen - 1 + sizeof(CRYPT_BIT_BLOB);
            if (!pvStructInfo)
                *pcbStructInfo = bytesNeeded;
            else if (*pcbStructInfo < bytesNeeded)
            {
                *pcbStructInfo = bytesNeeded;
                SetLastError(ERROR_MORE_DATA);
                ret = FALSE;
            }
            else
            {
                CRYPT_BIT_BLOB *blob;

                blob = pvStructInfo;
                blob->cbData = dataLen - 1;
                blob->cUnusedBits = *(pbEncoded + 1 +
                 GET_LEN_BYTES(pbEncoded[1]));
                if (dwFlags & CRYPT_DECODE_NOCOPY_FLAG)
                {
                    blob->pbData = (BYTE *)pbEncoded + 2 +
                     GET_LEN_BYTES(pbEncoded[1]);
                }
                else
                {
                    assert(blob->pbData);
                    if (blob->cbData)
                    {
                        BYTE mask = 0xff << blob->cUnusedBits;

                        memcpy(blob->pbData, pbEncoded + 2 +
                         GET_LEN_BYTES(pbEncoded[1]), blob->cbData);
                        blob->pbData[blob->cbData - 1] &= mask;
                    }
                }
            }
        }
    }
    else
    {
        SetLastError(CRYPT_E_ASN1_BADTAG);
        ret = FALSE;
    }
    TRACE("returning %d (%08lx)\n", ret, GetLastError());
    return ret;
}

static BOOL WINAPI CRYPT_AsnDecodeSPCLinkPointer(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;
    DWORD dataLen;

    if ((ret = CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen)))
    {
        BYTE lenBytes = GET_LEN_BYTES(pbEncoded[1]);
        DWORD size;
        SPC_LINK **pLink = pvStructInfo;

        ret = CRYPT_AsnDecodeSPCLinkInternal(dwCertEncodingType, lpszStructType,
         pbEncoded + 1 + lenBytes, dataLen, dwFlags, NULL, &size);
        if (ret)
        {
            if (!pvStructInfo)
                *pcbStructInfo = size + sizeof(PSPC_LINK);
            else if (*pcbStructInfo < size + sizeof(PSPC_LINK))
            {
                *pcbStructInfo = size + sizeof(PSPC_LINK);
                SetLastError(ERROR_MORE_DATA);
                ret = FALSE;
            }
            else
            {
                *pcbStructInfo = size + sizeof(PSPC_LINK);
                /* Set imageData's pointer if necessary */
                if (size > sizeof(SPC_LINK))
                {
                    (*pLink)->pwszUrl =
                     (LPWSTR)((BYTE *)*pLink + sizeof(SPC_LINK));
                }
                ret = CRYPT_AsnDecodeSPCLinkInternal(dwCertEncodingType,
                 lpszStructType, pbEncoded + 1 + lenBytes, dataLen, dwFlags,
                 *pLink, pcbStructInfo);
            }
        }
    }
    return ret;
}

BOOL WINAPI WVTAsn1SpcPeImageDataDecode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    __TRY
    {
        struct AsnDecodeSequenceItem items[] = {
         { ASN_BITSTRING, offsetof(SPC_PE_IMAGE_DATA, Flags),
           CRYPT_AsnDecodeBitsInternal, sizeof(CRYPT_BIT_BLOB), TRUE, TRUE,
           offsetof(SPC_PE_IMAGE_DATA, Flags.pbData), 0 },
         { ASN_CONSTRUCTOR | ASN_CONTEXT, offsetof(SPC_PE_IMAGE_DATA, pFile),
           CRYPT_AsnDecodeSPCLinkPointer, sizeof(PSPC_LINK), TRUE, TRUE,
           offsetof(SPC_PE_IMAGE_DATA, pFile), 0 },
        };

        ret = CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items),
         pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo, NULL);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    TRACE("returning %d\n", ret);
    return ret;
}

static BOOL WINAPI CRYPT_AsnDecodeOidIgnoreTag(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = TRUE;
    DWORD dataLen;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    if ((ret = CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen)))
    {
        BYTE lenBytes = GET_LEN_BYTES(pbEncoded[1]);
        DWORD bytesNeeded = sizeof(LPSTR);

        if (dataLen)
        {
            /* The largest possible string for the first two components
             * is 2.175 (= 2 * 40 + 175 = 255), so this is big enough.
             */
            char firstTwo[8];
            const BYTE *ptr;

            sprintf(firstTwo, "%d.%d",
             pbEncoded[1 + lenBytes] / 40,
             pbEncoded[1 + lenBytes] - (pbEncoded[1 + lenBytes] / 40)
             * 40);
            bytesNeeded += strlen(firstTwo) + 1;
            for (ptr = pbEncoded + 2 + lenBytes; ret &&
             ptr - pbEncoded - 1 - lenBytes < dataLen; )
            {
                /* large enough for ".4000000" */
                char str[9];
                int val = 0;

                while (ptr - pbEncoded - 1 - lenBytes < dataLen &&
                 (*ptr & 0x80))
                {
                    val <<= 7;
                    val |= *ptr & 0x7f;
                    ptr++;
                }
                if (ptr - pbEncoded - 1 - lenBytes >= dataLen ||
                 (*ptr & 0x80))
                {
                    SetLastError(CRYPT_E_ASN1_CORRUPT);
                    ret = FALSE;
                }
                else
                {
                    val <<= 7;
                    val |= *ptr++;
                    snprintf(str, sizeof(str), ".%d", val);
                    bytesNeeded += strlen(str);
                }
            }
        }
        else
            bytesNeeded += 1;
        if (!pvStructInfo)
            *pcbStructInfo = bytesNeeded;
        else if (*pcbStructInfo < bytesNeeded)
        {
            *pcbStructInfo = bytesNeeded;
            SetLastError(ERROR_MORE_DATA);
            ret = FALSE;
        }
        else
        {
            LPSTR pszObjId = *(LPSTR *)pvStructInfo;

            *pszObjId = 0;
            if (dataLen)
            {
                const BYTE *ptr;

                pszObjId += sprintf(pszObjId, "%d.%d", pbEncoded[1 + lenBytes] / 40,
                 pbEncoded[1 + lenBytes] - (pbEncoded[1 + lenBytes] /
                 40) * 40);
                for (ptr = pbEncoded + 2 + lenBytes; ret &&
                 ptr - pbEncoded - 1 - lenBytes < dataLen; )
                {
                    int val = 0;

                    while (ptr - pbEncoded - 1 - lenBytes < dataLen &&
                     (*ptr & 0x80))
                    {
                        val <<= 7;
                        val |= *ptr & 0x7f;
                        ptr++;
                    }
                    val <<= 7;
                    val |= *ptr++;
                    pszObjId += sprintf(pszObjId, ".%d", val);
                }
            }
            *pcbStructInfo = bytesNeeded;
        }
    }
    return ret;
}

static BOOL WINAPI CRYPT_AsnDecodeOid(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    if (!cbEncoded)
        SetLastError(CRYPT_E_ASN1_CORRUPT);
    else if (pbEncoded[0] == ASN_OBJECTIDENTIFIER)
        ret = CRYPT_AsnDecodeOidIgnoreTag(dwCertEncodingType, lpszStructType,
         pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo);
    else
        SetLastError(CRYPT_E_ASN1_BADTAG);
    return ret;
}

static BOOL WINAPI CRYPT_AsnDecodeCopyBytes(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = TRUE;
    DWORD bytesNeeded = sizeof(CRYPT_OBJID_BLOB);

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    if (!(dwFlags & CRYPT_DECODE_NOCOPY_FLAG))
        bytesNeeded += cbEncoded;
    if (!pvStructInfo)
        *pcbStructInfo = bytesNeeded;
    else if (*pcbStructInfo < bytesNeeded)
    {
        SetLastError(ERROR_MORE_DATA);
        *pcbStructInfo = bytesNeeded;
        ret = FALSE;
    }
    else
    {
        PCRYPT_OBJID_BLOB blob = pvStructInfo;

        *pcbStructInfo = bytesNeeded;
        blob->cbData = cbEncoded;
        if (dwFlags & CRYPT_DECODE_NOCOPY_FLAG)
            blob->pbData = (LPBYTE)pbEncoded;
        else
        {
            assert(blob->pbData);
            memcpy(blob->pbData, pbEncoded, blob->cbData);
        }
    }
    return ret;
}

static BOOL WINAPI CRYPT_AsnDecodeAttributeTypeValue(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    CRYPT_ATTRIBUTE_TYPE_VALUE *typeValue = pvStructInfo;
    struct AsnDecodeSequenceItem items[] = {
     { ASN_OBJECTIDENTIFIER, offsetof(CRYPT_ATTRIBUTE_TYPE_VALUE, pszObjId),
       CRYPT_AsnDecodeOid, sizeof(LPSTR), FALSE, TRUE,
       offsetof(CRYPT_ATTRIBUTE_TYPE_VALUE, pszObjId), 0 },
     { 0, offsetof(CRYPT_ATTRIBUTE_TYPE_VALUE, Value),
       CRYPT_AsnDecodeCopyBytes, sizeof(CRYPT_DATA_BLOB), TRUE, TRUE,
       offsetof(CRYPT_ATTRIBUTE_TYPE_VALUE, Value.pbData), 0 },
    };

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    return CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items),
     pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo,
     typeValue ? typeValue->pszObjId : NULL);
}

static BOOL WINAPI CRYPT_AsnDecodeAlgorithmId(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    CRYPT_ALGORITHM_IDENTIFIER *algo = pvStructInfo;
    BOOL ret = TRUE;
    struct AsnDecodeSequenceItem items[] = {
     { ASN_OBJECTIDENTIFIER, offsetof(CRYPT_ALGORITHM_IDENTIFIER, pszObjId),
       CRYPT_AsnDecodeOidIgnoreTag, sizeof(LPSTR), FALSE, TRUE,
       offsetof(CRYPT_ALGORITHM_IDENTIFIER, pszObjId), 0 },
     { 0, offsetof(CRYPT_ALGORITHM_IDENTIFIER, Parameters),
       CRYPT_AsnDecodeCopyBytes, sizeof(CRYPT_OBJID_BLOB), TRUE, TRUE,
       offsetof(CRYPT_ALGORITHM_IDENTIFIER, Parameters.pbData), 0 },
    };

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    ret = CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items),
     pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo, algo ? algo->pszObjId : NULL);
    if (ret && pvStructInfo)
    {
        TRACE("pszObjId is %p (%s)\n", algo->pszObjId,
         debugstr_a(algo->pszObjId));
    }
    return ret;
}

static BOOL WINAPI CRYPT_AsnDecodeSPCDigest(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    struct SPCDigest *digest = pvStructInfo;
    struct AsnDecodeSequenceItem items[] = {
     { ASN_SEQUENCEOF, offsetof(struct SPCDigest, DigestAlgorithm),
       CRYPT_AsnDecodeAlgorithmId, sizeof(CRYPT_ALGORITHM_IDENTIFIER),
       FALSE, TRUE,
       offsetof(struct SPCDigest, DigestAlgorithm.pszObjId), 0 },
     { ASN_OCTETSTRING, offsetof(struct SPCDigest, Digest),
       CRYPT_AsnDecodeOctets, sizeof(CRYPT_DER_BLOB),
       FALSE, TRUE, offsetof(struct SPCDigest, Digest.pbData), 0 },
    };

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    return CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items),
     pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo,
     digest ? digest->DigestAlgorithm.pszObjId : NULL);
}

BOOL WINAPI WVTAsn1SpcIndirectDataContentDecode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    __TRY
    {
        struct AsnDecodeSequenceItem items[] = {
         { ASN_SEQUENCEOF, offsetof(SPC_INDIRECT_DATA_CONTENT, Data),
           CRYPT_AsnDecodeAttributeTypeValue,
           sizeof(CRYPT_ATTRIBUTE_TYPE_VALUE), FALSE, TRUE,
           offsetof(SPC_INDIRECT_DATA_CONTENT, Data.pszObjId), 0 },
         { ASN_SEQUENCEOF, offsetof(SPC_INDIRECT_DATA_CONTENT, DigestAlgorithm),
           CRYPT_AsnDecodeSPCDigest, sizeof(struct SPCDigest),
           FALSE, TRUE,
           offsetof(SPC_INDIRECT_DATA_CONTENT, DigestAlgorithm.pszObjId), 0 },
        };

        ret = CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items),
         pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo, NULL);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    TRACE("returning %d\n", ret);
    return ret;
}

static BOOL WINAPI CRYPT_AsnDecodeBMPString(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret;
    DWORD bytesNeeded, dataLen;

    if ((ret = CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen)))
    {
        BYTE lenBytes = GET_LEN_BYTES(pbEncoded[1]);

        bytesNeeded = dataLen + 2 + sizeof(LPWSTR);
        if (!pvStructInfo)
            *pcbStructInfo = bytesNeeded;
        else if (*pcbStructInfo < bytesNeeded)
        {
            *pcbStructInfo = bytesNeeded;
            SetLastError(ERROR_MORE_DATA);
            ret = FALSE;
        }
        else
        {
            LPWSTR str;
            DWORD i;

            *pcbStructInfo = bytesNeeded;
            assert(pvStructInfo);
            str = *(LPWSTR *)pvStructInfo;
            for (i = 0; i < dataLen / 2; i++)
                str[i] = (pbEncoded[1 + lenBytes + 2 * i] << 8) |
                 pbEncoded[1 + lenBytes + 2 * i + 1];
            /* Decoded string is always NULL-terminated */
            str[i] = '\0';
        }
    }
    return ret;
}

static BOOL WINAPI CRYPT_AsnDecodeProgramName(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;
    DWORD dataLen;

    TRACE("(%p, %ld, %08lx, %p, %ld)\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, pvStructInfo ? *pcbStructInfo : 0);

    if ((ret = CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen)))
    {
        BYTE lenBytes = GET_LEN_BYTES(pbEncoded[1]);

        ret = CRYPT_AsnDecodeBMPString(dwCertEncodingType, lpszStructType,
         pbEncoded + 1 + lenBytes, dataLen, dwFlags, pvStructInfo,
         pcbStructInfo);
    }
    return ret;
}

BOOL WINAPI WVTAsn1SpcSpOpusInfoDecode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    __TRY
    {
        struct AsnDecodeSequenceItem items[] = {
         { ASN_CONSTRUCTOR | ASN_CONTEXT,
           offsetof(SPC_SP_OPUS_INFO, pwszProgramName),
           CRYPT_AsnDecodeProgramName, sizeof(LPCWSTR), TRUE, TRUE,
           offsetof(SPC_SP_OPUS_INFO, pwszProgramName), 0 },
         { ASN_CONSTRUCTOR | ASN_CONTEXT | 1,
           offsetof(SPC_SP_OPUS_INFO, pMoreInfo),
           CRYPT_AsnDecodeSPCLinkPointer, sizeof(PSPC_LINK), TRUE, TRUE,
           offsetof(SPC_SP_OPUS_INFO, pMoreInfo), 0 },
         { ASN_CONSTRUCTOR | ASN_CONTEXT | 2,
           offsetof(SPC_SP_OPUS_INFO, pPublisherInfo),
           CRYPT_AsnDecodeSPCLinkPointer, sizeof(PSPC_LINK), TRUE, TRUE,
           offsetof(SPC_SP_OPUS_INFO, pPublisherInfo), 0 },
        };

        ret = CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items),
         pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo, NULL);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    TRACE("returning %d\n", ret);
    return ret;
}

/* Ignores tag.  Only allows integers 4 bytes or smaller in size. */
static BOOL WINAPI CRYPT_AsnDecodeInt(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret;
    DWORD dataLen;

    if ((ret = CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen)))
    {
        BYTE lenBytes = GET_LEN_BYTES(pbEncoded[1]);

        if (dataLen > sizeof(int))
        {
            SetLastError(CRYPT_E_ASN1_LARGE);
            ret = FALSE;
        }
        else if (!pvStructInfo)
            *pcbStructInfo = sizeof(int);
        else if (*pcbStructInfo < sizeof(int))
        {
            *pcbStructInfo = sizeof(int);
            SetLastError(ERROR_MORE_DATA);
            ret = FALSE;
        }
        else
        {
            int val;
            DWORD i;

            *pcbStructInfo = sizeof(int);
            if (dataLen && pbEncoded[1 + lenBytes] & 0x80)
            {
                /* initialize to a negative value to sign-extend */
                val = -1;
            }
            else
                val = 0;
            for (i = 0; i < dataLen; i++)
                val = (int)(((unsigned int)val << 8) | pbEncoded[1 + lenBytes + i]);
            memcpy(pvStructInfo, &val, sizeof(int));
        }
    }
    return ret;
}

BOOL WINAPI WVTAsn1CatMemberInfoDecode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    __TRY
    {
        struct AsnDecodeSequenceItem items[] = {
         { ASN_BMPSTRING, offsetof(CAT_MEMBERINFO, pwszSubjGuid),
           CRYPT_AsnDecodeBMPString, sizeof(LPWSTR), FALSE, TRUE,
           offsetof(CAT_MEMBERINFO, pwszSubjGuid), 0 },
         { ASN_INTEGER, offsetof(CAT_MEMBERINFO, dwCertVersion),
           CRYPT_AsnDecodeInt, FINALMEMBERSIZE(CAT_MEMBERINFO, dwCertVersion),
           FALSE, FALSE, 0, 0 },
        };

        ret = CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items),
         pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo, NULL);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    TRACE("returning %d\n", ret);
    return ret;
}

BOOL WINAPI WVTAsn1CatNameValueDecode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    __TRY
    {
        struct AsnDecodeSequenceItem items[] = {
         { ASN_BMPSTRING, offsetof(CAT_NAMEVALUE, pwszTag),
           CRYPT_AsnDecodeBMPString, sizeof(LPWSTR), FALSE, TRUE,
           offsetof(CAT_NAMEVALUE, pwszTag), 0 },
         { ASN_INTEGER, offsetof(CAT_NAMEVALUE, fdwFlags),
           CRYPT_AsnDecodeInt, MEMBERSIZE(CAT_NAMEVALUE, fdwFlags, Value),
           FALSE, FALSE, 0, 0 },
         { ASN_OCTETSTRING, offsetof(CAT_NAMEVALUE, Value),
           CRYPT_AsnDecodeOctets, sizeof(CRYPT_DER_BLOB), FALSE, TRUE,
           offsetof(CAT_NAMEVALUE, Value.pbData), 0 },
        };

        ret = CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items),
         pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo, NULL);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    TRACE("returning %d\n", ret);
    return ret;
}

static BOOL WINAPI CRYPT_AsnDecodeBool(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret;

    if (cbEncoded < 3)
    {
        SetLastError(CRYPT_E_ASN1_CORRUPT);
        return FALSE;
    }
    if (GET_LEN_BYTES(pbEncoded[1]) > 1)
    {
        SetLastError(CRYPT_E_ASN1_CORRUPT);
        return FALSE;
    }
    if (pbEncoded[1] > 1)
    {
        SetLastError(CRYPT_E_ASN1_CORRUPT);
        return FALSE;
    }
    if (!pvStructInfo)
    {
        *pcbStructInfo = sizeof(BOOL);
        ret = TRUE;
    }
    else if (*pcbStructInfo < sizeof(BOOL))
    {
        *pcbStructInfo = sizeof(BOOL);
        SetLastError(ERROR_MORE_DATA);
        ret = FALSE;
    }
    else
    {
        *pcbStructInfo = sizeof(BOOL);
        *(BOOL *)pvStructInfo = pbEncoded[2] != 0;
        ret = TRUE;
    }
    TRACE("returning %d (%08lx)\n", ret, GetLastError());
    return ret;
}

BOOL WINAPI WVTAsn1SpcFinancialCriteriaInfoDecode(DWORD dwCertEncodingType,
 LPCSTR lpszStructType, const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
 void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;

    TRACE("(%p, %ld, %08lx, %p, %ld)\n", pbEncoded, cbEncoded, dwFlags,
     pvStructInfo, *pcbStructInfo);

    __TRY
    {
        struct AsnDecodeSequenceItem items[] = {
         { ASN_BOOL, offsetof(SPC_FINANCIAL_CRITERIA, fFinancialInfoAvailable),
           CRYPT_AsnDecodeBool, MEMBERSIZE(SPC_FINANCIAL_CRITERIA,
           fFinancialInfoAvailable, fMeetsCriteria), FALSE, FALSE, 0, 0 },
         { ASN_BOOL, offsetof(SPC_FINANCIAL_CRITERIA, fMeetsCriteria),
           CRYPT_AsnDecodeBool, FINALMEMBERSIZE(SPC_FINANCIAL_CRITERIA,
           fMeetsCriteria), FALSE, FALSE, 0, 0 },
        };

        ret = CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items),
         pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo, NULL);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    TRACE("returning %d\n", ret);
    return ret;
}

#ifdef __REACTOS__
static const GUID catalog_member_subjects[] =
{
    { 0xC689AAB8, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
    { 0xC689AABA, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
    { 0xDE351A42, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
};

#define CAT_MEMBERINFO2_DEFAULT_VERSION 0x200
#define CAT_MEMBERINFO2_GUID_TAG        (ASN_CONTEXT | 5)

static BOOL WVTAsn1_ReturnEncoded(const BYTE *encoded, DWORD size, BYTE *pbEncoded, DWORD *pcbEncoded)
{
    if (!pbEncoded)
    {
        *pcbEncoded = size;
        return TRUE;
    }
    if (*pcbEncoded < size)
    {
        *pcbEncoded = size;
        SetLastError(ERROR_MORE_DATA);
        return FALSE;
    }
    memcpy(pbEncoded, encoded, size);
    *pcbEncoded = size;
    return TRUE;
}

/***********************************************************************
 *      WVTAsn1CatMemberInfo2Encode  (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1CatMemberInfo2Encode(DWORD dwCertEncodingType, LPCSTR lpszStructType, const void *pvStructInfo,
                                        BYTE *pbEncoded, DWORD *pcbEncoded)
{
    const CAT_MEMBERINFO2 *info = pvStructInfo;
    BYTE encoded[2 + sizeof(GUID) + 8];
    DWORD size, i;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType, debugstr_a(lpszStructType), pvStructInfo,
          pbEncoded, pcbEncoded);

    if (!info || !pcbEncoded)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    for (i = 0; info->dwCertVersion == CAT_MEMBERINFO2_DEFAULT_VERSION && i < ARRAY_SIZE(catalog_member_subjects); i++)
    {
        if (!IsEqualGUID(&info->SubjectGuid, &catalog_member_subjects[i])) continue;
        encoded[0] = ASN_CONTEXT | i;
        encoded[1] = 0;
        return WVTAsn1_ReturnEncoded(encoded, 2, pbEncoded, pcbEncoded);
    }

    encoded[0] = CAT_MEMBERINFO2_GUID_TAG;
    encoded[1] = sizeof(GUID);
    memcpy(encoded + 2, &info->SubjectGuid, sizeof(GUID));
    size = sizeof(encoded) - 2 - sizeof(GUID);
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_INTEGER, &info->dwCertVersion, 0, NULL,
                             encoded + 2 + sizeof(GUID), &size))
        return FALSE;
    return WVTAsn1_ReturnEncoded(encoded, 2 + sizeof(GUID) + size, pbEncoded, pcbEncoded);
}

/***********************************************************************
 *      WVTAsn1CatMemberInfo2Decode  (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1CatMemberInfo2Decode(DWORD dwCertEncodingType, LPCSTR lpszStructType, const BYTE *pbEncoded,
                                        DWORD cbEncoded, DWORD dwFlags, void *pvStructInfo, DWORD *pcbStructInfo)
{
    CAT_MEMBERINFO2 info;
    DWORD size;

    TRACE("(0x%08lx, %s, %p, %ld, 0x%08lx, %p, %p)\n", dwCertEncodingType, debugstr_a(lpszStructType), pbEncoded,
          cbEncoded, dwFlags, pvStructInfo, pcbStructInfo);

    if (!pbEncoded || !pcbStructInfo)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (cbEncoded >= 2 && pbEncoded[1] == 0 && (pbEncoded[0] & ~0x03) == ASN_CONTEXT &&
        (pbEncoded[0] & 0x03) < ARRAY_SIZE(catalog_member_subjects))
    {
        info.SubjectGuid = catalog_member_subjects[pbEncoded[0] & 0x03];
        info.dwCertVersion = CAT_MEMBERINFO2_DEFAULT_VERSION;
    }
    else if (cbEncoded > 2 + sizeof(GUID) && pbEncoded[0] == CAT_MEMBERINFO2_GUID_TAG && pbEncoded[1] == sizeof(GUID))
    {
        memcpy(&info.SubjectGuid, pbEncoded + 2, sizeof(GUID));
        size = sizeof(info.dwCertVersion);
        if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_INTEGER, pbEncoded + 2 + sizeof(GUID),
                                 cbEncoded - 2 - sizeof(GUID), 0, NULL, &info.dwCertVersion, &size))
            return FALSE;
    }
    else
    {
        SetLastError(CRYPT_E_ASN1_BADTAG);
        return FALSE;
    }

    if (!pvStructInfo)
    {
        *pcbStructInfo = sizeof(info);
        return TRUE;
    }
    if (*pcbStructInfo < sizeof(info))
    {
        *pcbStructInfo = sizeof(info);
        SetLastError(ERROR_MORE_DATA);
        return FALSE;
    }
    memcpy(pvStructInfo, &info, sizeof(info));
    *pcbStructInfo = sizeof(info);
    return TRUE;
}
#endif

#ifdef __REACTOS__
static BOOL WINAPI CRYPT_AsnEncodeSPCImage(DWORD dwCertEncodingType, LPCSTR lpszStructType, const void *pvStructInfo,
                                           BYTE *pbEncoded, DWORD *pcbEncoded)
{
    const SPC_IMAGE *image = pvStructInfo;
    const CRYPT_DATA_BLOB *blobs[4] = { &image->Bitmap, &image->Metafile, &image->EnhancedMetafile, &image->GifFile };
    struct AsnEncodeSequenceItem items[5] = { { 0 } };
    struct AsnEncodeTagSwappedItem swapped[4];
    struct AsnConstructedItem constructed = { 0 };
    DWORD cItem = 0, i;

    if (image->pImageLink)
    {
        constructed.tag = 0;
        constructed.pvStructInfo = image->pImageLink;
        constructed.encodeFunc = WVTAsn1SpcLinkEncode;
        items[cItem].pvStructInfo = &constructed;
        items[cItem].encodeFunc = CRYPT_AsnEncodeConstructed;
        cItem++;
    }
    for (i = 0; i < ARRAY_SIZE(blobs); i++)
    {
        if (!blobs[i]->cbData)
            continue;
        swapped[i].tag = ASN_CONTEXT | (i + 1);
        swapped[i].pvStructInfo = blobs[i];
        swapped[i].encodeFunc = CRYPT_AsnEncodeOctets;
        items[cItem].pvStructInfo = &swapped[i];
        items[cItem].encodeFunc = CRYPT_AsnEncodeSwapTag;
        cItem++;
    }
    return CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, cItem, pbEncoded, pcbEncoded);
}

/***********************************************************************
 *      WVTAsn1SpcSpAgencyInfoEncode  (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SpcSpAgencyInfoEncode(DWORD dwCertEncodingType, LPCSTR lpszStructType, const void *pvStructInfo,
                                         BYTE *pbEncoded, DWORD *pcbEncoded)
{
    const SPC_SP_AGENCY_INFO *info = pvStructInfo;
    struct AsnEncodeSequenceItem items[4] = { { 0 } };
    struct AsnConstructedItem constructed[3] = { { 0 } };
    struct AsnEncodeTagSwappedItem text, image;
    DWORD cItem = 0, cConstructed = 0;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType, debugstr_a(lpszStructType), pvStructInfo,
          pbEncoded, pcbEncoded);

    if (!info || !pcbEncoded)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (info->pPolicyInformation)
    {
        constructed[cConstructed].tag = 0;
        constructed[cConstructed].pvStructInfo = info->pPolicyInformation;
        constructed[cConstructed].encodeFunc = WVTAsn1SpcLinkEncode;
        items[cItem].pvStructInfo = &constructed[cConstructed++];
        items[cItem++].encodeFunc = CRYPT_AsnEncodeConstructed;
    }
    if (info->pwszPolicyDisplayText)
    {
        text.tag = ASN_CONTEXT;
        text.pvStructInfo = info->pwszPolicyDisplayText;
        text.encodeFunc = CRYPT_AsnEncodeBMPString;
        constructed[cConstructed].tag = 1;
        constructed[cConstructed].pvStructInfo = &text;
        constructed[cConstructed].encodeFunc = CRYPT_AsnEncodeSwapTag;
        items[cItem].pvStructInfo = &constructed[cConstructed++];
        items[cItem++].encodeFunc = CRYPT_AsnEncodeConstructed;
    }
    if (info->pLogoImage)
    {
        image.tag = ASN_CONSTRUCTOR | ASN_CONTEXT | 2;
        image.pvStructInfo = info->pLogoImage;
        image.encodeFunc = CRYPT_AsnEncodeSPCImage;
        items[cItem].pvStructInfo = &image;
        items[cItem++].encodeFunc = CRYPT_AsnEncodeSwapTag;
    }
    if (info->pLogoLink)
    {
        constructed[cConstructed].tag = 3;
        constructed[cConstructed].pvStructInfo = info->pLogoLink;
        constructed[cConstructed].encodeFunc = WVTAsn1SpcLinkEncode;
        items[cItem].pvStructInfo = &constructed[cConstructed++];
        items[cItem++].encodeFunc = CRYPT_AsnEncodeConstructed;
    }
    return CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, cItem, pbEncoded, pcbEncoded);
}

static BOOL CRYPT_AsnDecodeSPCImageItems(DWORD dwCertEncodingType, LPCSTR lpszStructType, const BYTE *content,
                                         DWORD dataLen, DWORD dwFlags, SPC_IMAGE *image, DWORD *linkSize,
                                         DWORD *blobSize)
{
    CRYPT_DATA_BLOB *blobs[4] = { NULL };
    DWORD offset, itemLen, itemTotal, tag;
    BYTE *data = NULL;
    const BYTE *item;

    if (image)
    {
        blobs[0] = &image->Bitmap;
        blobs[1] = &image->Metafile;
        blobs[2] = &image->EnhancedMetafile;
        blobs[3] = &image->GifFile;
        data = (BYTE *)(image + 1) + *linkSize;
    }
    for (offset = 0; offset < dataLen; offset += itemTotal)
    {
        item = content + offset;
        if (dataLen - offset < 2 || !CRYPT_GetLen(item, dataLen - offset, &itemLen))
            return FALSE;
        itemTotal = 1 + GET_LEN_BYTES(item[1]) + itemLen;
        tag = item[0];
        if (tag == (ASN_CONSTRUCTOR | ASN_CONTEXT))
        {
            if (!image && !CRYPT_AsnDecodeSPCLinkPointer(dwCertEncodingType, lpszStructType, item, itemTotal, dwFlags,
                                                         NULL, linkSize))
                return FALSE;
            if (image)
            {
                image->pImageLink = (SPC_LINK *)(image + 1);
                if (!CRYPT_AsnDecodeSPCLinkPointer(dwCertEncodingType, lpszStructType, item, itemTotal, dwFlags,
                                                   &image->pImageLink, linkSize))
                    return FALSE;
            }
        }
        else if (tag >= (ASN_CONTEXT | 1) && tag <= (ASN_CONTEXT | 4))
        {
            if (!image)
            {
                *blobSize += itemLen;
                continue;
            }
            blobs[tag - (ASN_CONTEXT | 1)]->cbData = itemLen;
            blobs[tag - (ASN_CONTEXT | 1)]->pbData = data;
            memcpy(data, item + itemTotal - itemLen, itemLen);
            data += itemLen;
        }
        else
        {
            SetLastError(CRYPT_E_ASN1_BADTAG);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL WINAPI CRYPT_AsnDecodeSPCImagePointer(DWORD dwCertEncodingType, LPCSTR lpszStructType,
                                                  const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
                                                  void *pvStructInfo, DWORD *pcbStructInfo)
{
    DWORD dataLen, linkSize = 0, blobSize = 0, bytesNeeded;
    const BYTE *content;
    SPC_IMAGE *image;
    BYTE lenBytes;

    if (!CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen))
        return FALSE;
    lenBytes = GET_LEN_BYTES(pbEncoded[1]);
    content = pbEncoded + 1 + lenBytes;
    if (!CRYPT_AsnDecodeSPCImageItems(dwCertEncodingType, lpszStructType, content, dataLen, dwFlags, NULL, &linkSize,
                                      &blobSize))
        return FALSE;

    bytesNeeded = ALIGN_DWORD_PTR(sizeof(PSPC_IMAGE) + sizeof(SPC_IMAGE) + linkSize + blobSize);
    if (!pvStructInfo)
    {
        *pcbStructInfo = bytesNeeded;
        return TRUE;
    }
    if (*pcbStructInfo < bytesNeeded)
    {
        *pcbStructInfo = bytesNeeded;
        SetLastError(ERROR_MORE_DATA);
        return FALSE;
    }

    *pcbStructInfo = bytesNeeded;
    image = *(SPC_IMAGE **)pvStructInfo;
    memset(image, 0, sizeof(*image));
    return CRYPT_AsnDecodeSPCImageItems(dwCertEncodingType, lpszStructType, content, dataLen, dwFlags, image,
                                        &linkSize, &blobSize);
}

/***********************************************************************
 *      WVTAsn1SpcSpAgencyInfoDecode  (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SpcSpAgencyInfoDecode(DWORD dwCertEncodingType, LPCSTR lpszStructType, const BYTE *pbEncoded,
                                         DWORD cbEncoded, DWORD dwFlags, void *pvStructInfo, DWORD *pcbStructInfo)
{
    struct AsnDecodeSequenceItem items[] = {
     { ASN_CONSTRUCTOR | ASN_CONTEXT, offsetof(SPC_SP_AGENCY_INFO, pPolicyInformation),
       CRYPT_AsnDecodeSPCLinkPointer, sizeof(PSPC_LINK), TRUE, TRUE,
       offsetof(SPC_SP_AGENCY_INFO, pPolicyInformation), 0 },
     { ASN_CONSTRUCTOR | ASN_CONTEXT | 1, offsetof(SPC_SP_AGENCY_INFO, pwszPolicyDisplayText),
       CRYPT_AsnDecodeProgramName, sizeof(LPWSTR), TRUE, TRUE,
       offsetof(SPC_SP_AGENCY_INFO, pwszPolicyDisplayText), 0 },
     { ASN_CONSTRUCTOR | ASN_CONTEXT | 2, offsetof(SPC_SP_AGENCY_INFO, pLogoImage),
       CRYPT_AsnDecodeSPCImagePointer, sizeof(PSPC_IMAGE), TRUE, TRUE,
       offsetof(SPC_SP_AGENCY_INFO, pLogoImage), 0 },
     { ASN_CONSTRUCTOR | ASN_CONTEXT | 3, offsetof(SPC_SP_AGENCY_INFO, pLogoLink),
       CRYPT_AsnDecodeSPCLinkPointer, sizeof(PSPC_LINK), TRUE, TRUE,
       offsetof(SPC_SP_AGENCY_INFO, pLogoLink), 0 },
    };

    TRACE("(%p, %ld, %08lx, %p, %p)\n", pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo);

    if (!pbEncoded || !cbEncoded || !pcbStructInfo)
    {
        SetLastError(CRYPT_E_ASN1_EOD);
        return FALSE;
    }
    return CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items), pbEncoded, cbEncoded, dwFlags,
                                   pvStructInfo, pcbStructInfo, NULL);
}

/***********************************************************************
 *      WVTAsn1SpcStatementTypeEncode  (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SpcStatementTypeEncode(DWORD dwCertEncodingType, LPCSTR lpszStructType, const void *pvStructInfo,
                                          BYTE *pbEncoded, DWORD *pcbEncoded)
{
    const SPC_STATEMENT_TYPE *statement = pvStructInfo;
    struct AsnEncodeSequenceItem *items;
    DWORD i;
    BOOL ret;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType, debugstr_a(lpszStructType), pvStructInfo,
          pbEncoded, pcbEncoded);

    if (!statement || !pcbEncoded || (statement->cKeyPurposeId && !statement->rgpszKeyPurposeId))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!(items = calloc(statement->cKeyPurposeId + 1, sizeof(*items))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    for (i = 0; i < statement->cKeyPurposeId; i++)
    {
        items[i].pvStructInfo = statement->rgpszKeyPurposeId[i];
        items[i].encodeFunc = CRYPT_AsnEncodeOid;
    }
    ret = CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, statement->cKeyPurposeId, pbEncoded, pcbEncoded);
    free(items);
    return ret;
}

/***********************************************************************
 *      WVTAsn1SpcStatementTypeDecode  (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SpcStatementTypeDecode(DWORD dwCertEncodingType, LPCSTR lpszStructType, const BYTE *pbEncoded,
                                          DWORD cbEncoded, DWORD dwFlags, void *pvStructInfo, DWORD *pcbStructInfo)
{
    SPC_STATEMENT_TYPE *statement = pvStructInfo;
    DWORD dataLen, itemLen, oidSize, count = 0, bytesNeeded = sizeof(SPC_STATEMENT_TYPE), offset;
    const BYTE *content, *ptr;
    char *strings;

    TRACE("(%p, %ld, %08lx, %p, %p)\n", pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo);

    if (!pbEncoded || !cbEncoded || !pcbStructInfo)
    {
        SetLastError(CRYPT_E_ASN1_EOD);
        return FALSE;
    }
    if (pbEncoded[0] != ASN_SEQUENCE)
    {
        SetLastError(CRYPT_E_ASN1_BADTAG);
        return FALSE;
    }
    if (!CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen))
        return FALSE;
    content = pbEncoded + 1 + GET_LEN_BYTES(pbEncoded[1]);

    for (ptr = content; ptr < content + dataLen; ptr += itemLen)
    {
        if (ptr[0] != ASN_OBJECTIDENTIFIER || !CRYPT_GetLen(ptr, content + dataLen - ptr, &itemLen))
        {
            SetLastError(CRYPT_E_ASN1_CORRUPT);
            return FALSE;
        }
        itemLen += 1 + GET_LEN_BYTES(ptr[1]);
        if (!CRYPT_AsnDecodeOidIgnoreTag(dwCertEncodingType, lpszStructType, ptr, itemLen, dwFlags, NULL, &oidSize))
            return FALSE;
        bytesNeeded += oidSize;
        count++;
    }

    if (!pvStructInfo)
    {
        *pcbStructInfo = bytesNeeded;
        return TRUE;
    }
    if (*pcbStructInfo < bytesNeeded)
    {
        *pcbStructInfo = bytesNeeded;
        SetLastError(ERROR_MORE_DATA);
        return FALSE;
    }

    *pcbStructInfo = bytesNeeded;
    statement->cKeyPurposeId = count;
    statement->rgpszKeyPurposeId = count ? (LPSTR *)(statement + 1) : NULL;
    strings = (char *)(statement + 1) + count * sizeof(LPSTR);
    for (ptr = content, offset = 0; offset < count; ptr += itemLen, offset++)
    {
        CRYPT_GetLen(ptr, content + dataLen - ptr, &itemLen);
        itemLen += 1 + GET_LEN_BYTES(ptr[1]);
        CRYPT_AsnDecodeOidIgnoreTag(dwCertEncodingType, lpszStructType, ptr, itemLen, dwFlags, NULL, &oidSize);
        statement->rgpszKeyPurposeId[offset] = strings;
        CRYPT_AsnDecodeOidIgnoreTag(dwCertEncodingType, lpszStructType, ptr, itemLen, dwFlags,
                                    &statement->rgpszKeyPurposeId[offset], &oidSize);
        strings += oidSize - sizeof(LPSTR);
    }
    return TRUE;
}

/***********************************************************************
 *      WVTAsn1SpcSigInfoEncode  (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SpcSigInfoEncode(DWORD dwCertEncodingType, LPCSTR lpszStructType, const void *pvStructInfo,
                                    BYTE *pbEncoded, DWORD *pcbEncoded)
{
    const SPC_SIGINFO *info = pvStructInfo;
    CRYPT_DATA_BLOB guid;
    struct AsnEncodeSequenceItem items[7];

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType, debugstr_a(lpszStructType), pvStructInfo,
          pbEncoded, pcbEncoded);

    if (!info || !pcbEncoded)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    guid.cbData = sizeof(info->gSIPGuid);
    guid.pbData = (BYTE *)&info->gSIPGuid;
    memset(items, 0, sizeof(items));
    items[0].pvStructInfo = &info->dwSipVersion;
    items[0].encodeFunc = CRYPT_AsnEncodeInt;
    items[1].pvStructInfo = &guid;
    items[1].encodeFunc = CRYPT_AsnEncodeOctets;
    items[2].pvStructInfo = &info->dwReserved1;
    items[2].encodeFunc = CRYPT_AsnEncodeInt;
    items[3].pvStructInfo = &info->dwReserved2;
    items[3].encodeFunc = CRYPT_AsnEncodeInt;
    items[4].pvStructInfo = &info->dwReserved3;
    items[4].encodeFunc = CRYPT_AsnEncodeInt;
    items[5].pvStructInfo = &info->dwReserved4;
    items[5].encodeFunc = CRYPT_AsnEncodeInt;
    items[6].pvStructInfo = &info->dwReserved5;
    items[6].encodeFunc = CRYPT_AsnEncodeInt;
    return CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, ARRAY_SIZE(items), pbEncoded, pcbEncoded);
}

/***********************************************************************
 *      WVTAsn1SpcSigInfoDecode  (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SpcSigInfoDecode(DWORD dwCertEncodingType, LPCSTR lpszStructType, const BYTE *pbEncoded,
                                    DWORD cbEncoded, DWORD dwFlags, void *pvStructInfo, DWORD *pcbStructInfo)
{
    DWORD *fields[7], dataLen, itemLen, size, i;
    SPC_SIGINFO info;
    const BYTE *ptr, *end;

    TRACE("(%p, %ld, %08lx, %p, %p)\n", pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo);

    if (!pbEncoded || !cbEncoded || !pcbStructInfo)
    {
        SetLastError(CRYPT_E_ASN1_EOD);
        return FALSE;
    }
    if (pbEncoded[0] != ASN_SEQUENCE)
    {
        SetLastError(CRYPT_E_ASN1_BADTAG);
        return FALSE;
    }
    if (!CRYPT_GetLen(pbEncoded, cbEncoded, &dataLen))
        return FALSE;

    memset(&info, 0, sizeof(info));
    fields[0] = &info.dwSipVersion;
    fields[1] = NULL;
    fields[2] = &info.dwReserved1;
    fields[3] = &info.dwReserved2;
    fields[4] = &info.dwReserved3;
    fields[5] = &info.dwReserved4;
    fields[6] = &info.dwReserved5;
    ptr = pbEncoded + 1 + GET_LEN_BYTES(pbEncoded[1]);
    end = ptr + dataLen;
    for (i = 0; i < ARRAY_SIZE(fields); i++, ptr += itemLen)
    {
        if (ptr >= end || !CRYPT_GetLen(ptr, end - ptr, &itemLen))
        {
            SetLastError(CRYPT_E_ASN1_CORRUPT);
            return FALSE;
        }
        itemLen += 1 + GET_LEN_BYTES(ptr[1]);
        if (!fields[i])
        {
            if (ptr[0] != ASN_OCTETSTRING || itemLen != 2 + sizeof(GUID))
            {
                SetLastError(CRYPT_E_ASN1_CORRUPT);
                return FALSE;
            }
            memcpy(&info.gSIPGuid, ptr + 2, sizeof(GUID));
            continue;
        }
        size = sizeof(DWORD);
        if (ptr[0] != ASN_INTEGER ||
            !CRYPT_AsnDecodeInt(dwCertEncodingType, lpszStructType, ptr, itemLen, dwFlags, fields[i], &size))
        {
            SetLastError(CRYPT_E_ASN1_CORRUPT);
            return FALSE;
        }
    }
    if (ptr != end)
    {
        SetLastError(CRYPT_E_ASN1_CORRUPT);
        return FALSE;
    }

    if (!pvStructInfo)
    {
        *pcbStructInfo = sizeof(info);
        return TRUE;
    }
    if (*pcbStructInfo < sizeof(info))
    {
        *pcbStructInfo = sizeof(info);
        SetLastError(ERROR_MORE_DATA);
        return FALSE;
    }
    *pcbStructInfo = sizeof(info);
    memcpy(pvStructInfo, &info, sizeof(info));
    return TRUE;
}

/***********************************************************************
 *      WVTAsn1SpcMinimalCriteriaInfoEncode  (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SpcMinimalCriteriaInfoEncode(DWORD dwCertEncodingType, LPCSTR lpszStructType,
                                                const void *pvStructInfo, BYTE *pbEncoded, DWORD *pcbEncoded)
{
    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType, debugstr_a(lpszStructType), pvStructInfo,
          pbEncoded, pcbEncoded);

    if (!pvStructInfo || !pcbEncoded)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return CRYPT_AsnEncodeBool(dwCertEncodingType, lpszStructType, pvStructInfo, pbEncoded, pcbEncoded);
}

/***********************************************************************
 *      WVTAsn1SpcMinimalCriteriaInfoDecode  (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SpcMinimalCriteriaInfoDecode(DWORD dwCertEncodingType, LPCSTR lpszStructType,
                                                const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
                                                void *pvStructInfo, DWORD *pcbStructInfo)
{
    TRACE("(%p, %ld, %08lx, %p, %p)\n", pbEncoded, cbEncoded, dwFlags, pvStructInfo, pcbStructInfo);

    if (!pbEncoded || !pcbStructInfo)
    {
        SetLastError(CRYPT_E_ASN1_EOD);
        return FALSE;
    }
    if (!cbEncoded || pbEncoded[0] != ASN_BOOL)
    {
        SetLastError(CRYPT_E_ASN1_BADTAG);
        return FALSE;
    }
    return CRYPT_AsnDecodeBool(dwCertEncodingType, lpszStructType, pbEncoded, cbEncoded, dwFlags, pvStructInfo,
                               pcbStructInfo);
}
#endif

#ifdef __REACTOS__
static BOOL WINAPI CRYPT_AsnEncodeBoolean(DWORD dwCertEncodingType, LPCSTR lpszStructType, const void *pvStructInfo,
                                          BYTE *pbEncoded, DWORD *pcbEncoded)
{
    BOOL value = *(const BOOLEAN *)pvStructInfo;

    return CRYPT_AsnEncodeBool(dwCertEncodingType, lpszStructType, &value, pbEncoded, pcbEncoded);
}

static BOOL WINAPI CRYPT_AsnDecodeBoolean(DWORD dwCertEncodingType, LPCSTR lpszStructType, const BYTE *pbEncoded,
                                          DWORD cbEncoded, DWORD dwFlags, void *pvStructInfo, DWORD *pcbStructInfo)
{
    DWORD size = sizeof(BOOL);
    BOOL value;

    if (!CRYPT_AsnDecodeBool(dwCertEncodingType, lpszStructType, pbEncoded, cbEncoded, dwFlags, &value, &size))
        return FALSE;
    if (pvStructInfo && *pcbStructInfo < sizeof(BOOLEAN))
    {
        *pcbStructInfo = sizeof(BOOLEAN);
        SetLastError(ERROR_MORE_DATA);
        return FALSE;
    }
    *pcbStructInfo = sizeof(BOOLEAN);
    if (pvStructInfo)
        *(BOOLEAN *)pvStructInfo = value != FALSE;
    return TRUE;
}

/***********************************************************************
 *		WVTAsn1IntentToSealAttributeEncode (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1IntentToSealAttributeEncode(DWORD dwCertEncodingType, LPCSTR lpszStructType,
                                               const void *pvStructInfo, BYTE *pbEncoded, DWORD *pcbEncoded)
{
    BOOL ret = FALSE;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType, debugstr_a(lpszStructType), pvStructInfo, pbEncoded,
          pcbEncoded);

    __TRY
    {
        const INTENT_TO_SEAL_ATTRIBUTE *intent = pvStructInfo;
        struct AsnEncodeSequenceItem items[] = {
         { &intent->version, CRYPT_AsnEncodeInt, 0 },
         { &intent->seal, CRYPT_AsnEncodeBoolean, 0 },
        };

        ret = CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, ARRAY_SIZE(items), pbEncoded, pcbEncoded);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}

/***********************************************************************
 *		WVTAsn1IntentToSealAttributeDecode (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1IntentToSealAttributeDecode(DWORD dwCertEncodingType, LPCSTR lpszStructType, const BYTE *pbEncoded,
                                               DWORD cbEncoded, DWORD dwFlags, void *pvStructInfo,
                                               DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags, pvStructInfo, *pcbStructInfo);

    __TRY
    {
        struct AsnDecodeSequenceItem items[] = {
         { ASN_INTEGER, offsetof(INTENT_TO_SEAL_ATTRIBUTE, version), CRYPT_AsnDecodeInt,
           MEMBERSIZE(INTENT_TO_SEAL_ATTRIBUTE, version, seal), FALSE, FALSE, 0, 0 },
         { ASN_BOOL, offsetof(INTENT_TO_SEAL_ATTRIBUTE, seal), CRYPT_AsnDecodeBoolean,
           FINALMEMBERSIZE(INTENT_TO_SEAL_ATTRIBUTE, seal), FALSE, FALSE, 0, 0 },
        };
        INTENT_TO_SEAL_ATTRIBUTE intent;
        DWORD size = sizeof(intent);

        ret = CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items), pbEncoded, cbEncoded, dwFlags,
                                      &intent, &size, NULL);
        if (ret && pvStructInfo && *pcbStructInfo < sizeof(intent))
        {
            SetLastError(ERROR_MORE_DATA);
            ret = FALSE;
        }
        else if (ret && pvStructInfo)
        {
            ((INTENT_TO_SEAL_ATTRIBUTE *)pvStructInfo)->version = intent.version;
            ((INTENT_TO_SEAL_ATTRIBUTE *)pvStructInfo)->seal = intent.seal;
        }
        if (ret || GetLastError() == ERROR_MORE_DATA)
            *pcbStructInfo = sizeof(intent);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}

/***********************************************************************
 *		WVTAsn1SealingSignatureAttributeEncode (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SealingSignatureAttributeEncode(DWORD dwCertEncodingType, LPCSTR lpszStructType,
                                                   const void *pvStructInfo, BYTE *pbEncoded, DWORD *pcbEncoded)
{
    BOOL ret = FALSE;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType, debugstr_a(lpszStructType), pvStructInfo, pbEncoded,
          pcbEncoded);

    __TRY
    {
        const SEALING_SIGNATURE_ATTRIBUTE *signature = pvStructInfo;
        struct AsnEncodeSequenceItem items[] = {
         { &signature->version, CRYPT_AsnEncodeInt, 0 },
         { &signature->signerIndex, CRYPT_AsnEncodeInt, 0 },
         { &signature->signatureAlgorithm, CRYPT_AsnEncodeAlgorithmIdWithNullParams, 0 },
         { &signature->encryptedDigest, CRYPT_AsnEncodeOctets, 0 },
        };

        ret = CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, ARRAY_SIZE(items), pbEncoded, pcbEncoded);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}

/***********************************************************************
 *		WVTAsn1SealingSignatureAttributeDecode (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SealingSignatureAttributeDecode(DWORD dwCertEncodingType, LPCSTR lpszStructType,
                                                   const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
                                                   void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags, pvStructInfo, *pcbStructInfo);

    __TRY
    {
        struct AsnDecodeSequenceItem items[] = {
         { ASN_INTEGER, offsetof(SEALING_SIGNATURE_ATTRIBUTE, version), CRYPT_AsnDecodeInt,
           MEMBERSIZE(SEALING_SIGNATURE_ATTRIBUTE, version, signerIndex), FALSE, FALSE, 0, 0 },
         { ASN_INTEGER, offsetof(SEALING_SIGNATURE_ATTRIBUTE, signerIndex), CRYPT_AsnDecodeInt,
           MEMBERSIZE(SEALING_SIGNATURE_ATTRIBUTE, signerIndex, signatureAlgorithm), FALSE, FALSE, 0, 0 },
         { ASN_SEQUENCEOF, offsetof(SEALING_SIGNATURE_ATTRIBUTE, signatureAlgorithm), CRYPT_AsnDecodeAlgorithmId,
           sizeof(CRYPT_ALGORITHM_IDENTIFIER), FALSE, TRUE,
           offsetof(SEALING_SIGNATURE_ATTRIBUTE, signatureAlgorithm.pszObjId), 0 },
         { ASN_OCTETSTRING, offsetof(SEALING_SIGNATURE_ATTRIBUTE, encryptedDigest), CRYPT_AsnDecodeOctets,
           sizeof(CRYPT_DIGEST_BLOB), FALSE, TRUE, offsetof(SEALING_SIGNATURE_ATTRIBUTE, encryptedDigest.pbData), 0 },
        };

        ret = CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items), pbEncoded, cbEncoded, dwFlags,
                                      pvStructInfo, pcbStructInfo, NULL);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}

/***********************************************************************
 *		WVTAsn1SealingTimestampAttributeEncode (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SealingTimestampAttributeEncode(DWORD dwCertEncodingType, LPCSTR lpszStructType,
                                                   const void *pvStructInfo, BYTE *pbEncoded, DWORD *pcbEncoded)
{
    BOOL ret = FALSE;

    TRACE("(0x%08lx, %s, %p, %p, %p)\n", dwCertEncodingType, debugstr_a(lpszStructType), pvStructInfo, pbEncoded,
          pcbEncoded);

    __TRY
    {
        const SEALING_TIMESTAMP_ATTRIBUTE *timestamp = pvStructInfo;
        struct AsnEncodeSequenceItem items[] = {
         { &timestamp->version, CRYPT_AsnEncodeInt, 0 },
         { &timestamp->signerIndex, CRYPT_AsnEncodeInt, 0 },
         { &timestamp->sealTimeStampToken, CRYPT_AsnEncodeOctets, 0 },
        };

        ret = CRYPT_AsnEncodeSequence(X509_ASN_ENCODING, items, ARRAY_SIZE(items), pbEncoded, pcbEncoded);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}

/***********************************************************************
 *		WVTAsn1SealingTimestampAttributeDecode (WINTRUST.@)
 */
BOOL WINAPI WVTAsn1SealingTimestampAttributeDecode(DWORD dwCertEncodingType, LPCSTR lpszStructType,
                                                   const BYTE *pbEncoded, DWORD cbEncoded, DWORD dwFlags,
                                                   void *pvStructInfo, DWORD *pcbStructInfo)
{
    BOOL ret = FALSE;

    TRACE("%p, %ld, %08lx, %p, %ld\n", pbEncoded, cbEncoded, dwFlags, pvStructInfo, *pcbStructInfo);

    __TRY
    {
        struct AsnDecodeSequenceItem items[] = {
         { ASN_INTEGER, offsetof(SEALING_TIMESTAMP_ATTRIBUTE, version), CRYPT_AsnDecodeInt,
           MEMBERSIZE(SEALING_TIMESTAMP_ATTRIBUTE, version, signerIndex), FALSE, FALSE, 0, 0 },
         { ASN_INTEGER, offsetof(SEALING_TIMESTAMP_ATTRIBUTE, signerIndex), CRYPT_AsnDecodeInt,
           MEMBERSIZE(SEALING_TIMESTAMP_ATTRIBUTE, signerIndex, sealTimeStampToken), FALSE, FALSE, 0, 0 },
         { ASN_OCTETSTRING, offsetof(SEALING_TIMESTAMP_ATTRIBUTE, sealTimeStampToken), CRYPT_AsnDecodeOctets,
           sizeof(CRYPT_DATA_BLOB), FALSE, TRUE, offsetof(SEALING_TIMESTAMP_ATTRIBUTE, sealTimeStampToken.pbData), 0 },
        };

        ret = CRYPT_AsnDecodeSequence(dwCertEncodingType, items, ARRAY_SIZE(items), pbEncoded, cbEncoded, dwFlags,
                                      pvStructInfo, pcbStructInfo, NULL);
    }
    __EXCEPT_PAGE_FAULT
    {
        SetLastError(STATUS_ACCESS_VIOLATION);
    }
    __ENDTRY
    return ret;
}
#endif
