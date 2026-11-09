/*
 * WinTrust Cryptography functions
 *
 * Copyright 2006 James Hawkins
 * Copyright 2000-2002 Stuart Caie
 * Copyright 2002 Patrik Stridvall
 * Copyright 2003 Greg Turner
 * Copyright 2008 Juan Lang
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
#include <stdio.h>
#ifdef __REACTOS__
#include <stdlib.h>
#endif
#include "windef.h"
#include "winbase.h"
#include "wintrust.h"
#include "winver.h"
#include "mscat.h"
#include "mssip.h"
#include "imagehlp.h"
#include "winternl.h"
#ifdef __REACTOS__
#include "rpc.h"
#endif

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wintrust);

#define CATADMIN_MAGIC 0x43415441 /* 'CATA' */
#define CRYPTCAT_MAGIC 0x43415443 /* 'CATC' */
#define CATINFO_MAGIC  0x43415449 /* 'CATI' */

struct cryptcat
{
    DWORD     magic;
    HCRYPTMSG msg;
    DWORD     encoding;
    CTL_INFO *inner;
    DWORD     inner_len;
    GUID      subject;
    DWORD     attr_count;
    CRYPTCATATTRIBUTE *attr;
#ifdef __REACTOS__
    WCHAR    *filename;
    DWORD     version;
    DWORD     member_count;
    CRYPTCATMEMBER **members;
    struct catattrs *cat_attrs;
    DWORD     entry_attr_count;
    struct catattrs **entry_attrs;
    DWORD     found_count;
    CRYPTCATMEMBER **found;
    CRYPTCATSTORE store;
    DWORD     put_attr_count;
    CRYPTCATATTRIBUTE **put_attrs;
#endif
};

#ifdef __REACTOS__
struct catmember
{
    CRYPTCATMEMBER      member;
    DWORD               attr_count;
    CRYPTCATATTRIBUTE **attrs;
};

static void catalog_free_member(CRYPTCATMEMBER *member)
{
    struct catmember *cm = (struct catmember *)member;

    while (cm->attr_count)
        free(cm->attrs[--cm->attr_count]);
    free(cm->attrs);
    free(cm);
}

BOOL WINAPI WVTAsn1CatNameValueDecode(DWORD, LPCSTR, const BYTE *, DWORD, DWORD, void *, DWORD *);

struct catattrs
{
    DWORD count;
    CRYPTCATATTRIBUTE *attrs;
    void **values;
    BOOL replaced;
};

static void catalog_free_attrs(struct catattrs *ca)
{
    DWORD i;

    if (!ca) return;
    for (i = 0; i < ca->count; i++)
        free(ca->values[i]);
    free(ca->values);
    free(ca->attrs);
    free(ca);
}

static void catalog_free_found_member(CRYPTCATMEMBER *member)
{
    free(member->pIndirectData);
    free(member->pwszReferenceTag);
    free(member);
}

static void catalog_free_lookups(struct cryptcat *cc)
{
    DWORD i;

    catalog_free_attrs(cc->cat_attrs);
    for (i = 0; i < cc->entry_attr_count; i++)
        catalog_free_attrs(cc->entry_attrs[i]);
    free(cc->entry_attrs);
    for (i = 0; i < cc->found_count; i++)
        catalog_free_found_member(cc->found[i]);
    free(cc->found);
    for (i = 0; i < cc->put_attr_count; i++)
        free(cc->put_attrs[i]);
    free(cc->put_attrs);
}

static void catalog_init_store(struct cryptcat *cc, DWORD flags, HCRYPTPROV prov)
{
    cc->store.cbStruct = sizeof(cc->store);
    cc->store.dwPublicVersion = cc->version;
    cc->store.pwszP7File = cc->filename;
    cc->store.hProv = prov;
    cc->store.dwEncodingType = cc->encoding;
    cc->store.fdwStoreFlags = flags & ~(CRYPTCAT_OPEN_CREATENEW | CRYPTCAT_OPEN_ALWAYS | CRYPTCAT_OPEN_EXISTING);
    if (flags & CRYPTCAT_OPEN_NO_CONTENT_HCRYPTMSG)
        cc->store.hCryptMsg = cc->msg;
}

CRYPTCATATTRIBUTE *catalog_alloc_attribute(const WCHAR *name, DWORD flags, DWORD size, const BYTE *data)
{
    DWORD name_len = (lstrlenW(name) + 1) * sizeof(WCHAR);
    CRYPTCATATTRIBUTE *attr;

    if (!(attr = calloc(1, sizeof(*attr) + name_len + size)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    attr->cbStruct = sizeof(*attr);
    attr->pwszReferenceTag = (WCHAR *)(attr + 1);
    memcpy(attr->pwszReferenceTag, name, name_len);
    attr->dwAttrTypeAndAction = flags;
    attr->cbValue = size;
    attr->pbValue = (BYTE *)attr->pwszReferenceTag + name_len;
    if (size)
        memcpy(attr->pbValue, data, size);
    return attr;
}

static DWORD catalog_cat_attr_count(struct cryptcat *cc, struct catattrs *ca)
{
    if (cc->inner && !cc->inner->cCTLEntry) return 0;
    return ca->count;
}

static CRYPTCATATTRIBUTE *catalog_next_cat_attr(struct cryptcat *cc, struct catattrs *ca, CRYPTCATATTRIBUTE *prev)
{
    DWORD decoded = catalog_cat_attr_count(cc, ca), next = 0, i;

    if (prev)
    {
        if (prev >= ca->attrs && prev < ca->attrs + decoded)
            next = prev - ca->attrs + 1;
        else
        {
            for (i = 0; i < cc->put_attr_count && cc->put_attrs[i] != prev; i++)
                ;
            if (i == cc->put_attr_count)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return NULL;
            }
            next = decoded + i + 1;
        }
    }
    if (next < decoded) return &ca->attrs[next];
    if (next - decoded < cc->put_attr_count) return cc->put_attrs[next - decoded];
    SetLastError(CRYPT_E_NOT_FOUND);
    return NULL;
}

static struct catattrs *catalog_alloc_attrs(DWORD count)
{
    struct catattrs *ca;

    if (!(ca = calloc(1, sizeof(*ca))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    if (count && (!(ca->attrs = calloc(count, sizeof(*ca->attrs))) || !(ca->values = calloc(count, sizeof(*ca->values)))))
    {
        free(ca->attrs);
        free(ca);
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    return ca;
}

static BOOL catalog_add_name_value(struct cryptcat *cc, struct catattrs *ca, const CRYPT_DATA_BLOB *blob)
{
    CRYPTCATATTRIBUTE *attr = &ca->attrs[ca->count];
    CAT_NAMEVALUE *nv;
    DWORD size = 0;

    if (!WVTAsn1CatNameValueDecode(cc->encoding, CAT_NAMEVALUE_STRUCT, blob->pbData, blob->cbData, 0, NULL, &size))
        return FALSE;
    if (!(nv = malloc(size)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    if (!WVTAsn1CatNameValueDecode(cc->encoding, CAT_NAMEVALUE_STRUCT, blob->pbData, blob->cbData, 0, nv, &size))
    {
        free(nv);
        return FALSE;
    }

    attr->cbStruct = sizeof(*attr);
    attr->pwszReferenceTag = nv->pwszTag;
    attr->dwAttrTypeAndAction = nv->fdwFlags;
    attr->cbValue = nv->Value.cbData;
    attr->pbValue = nv->Value.pbData;
    attr->dwReserved = 0;
    ca->values[ca->count++] = nv;
    return TRUE;
}

static struct catattrs *catalog_cat_attrs(struct cryptcat *cc)
{
    struct catattrs *ca;
    DWORD i, count = cc->inner ? cc->inner->cExtension : 0;

    if (cc->cat_attrs) return cc->cat_attrs;
    if (!(ca = catalog_alloc_attrs(count))) return NULL;
    for (i = 0; i < count; i++)
    {
        CERT_EXTENSION *ext = &cc->inner->rgExtension[i];

        if (!ext->pszObjId || strcmp(ext->pszObjId, CAT_NAMEVALUE_OBJID)) continue;
        if (!catalog_add_name_value(cc, ca, &ext->Value))
        {
            catalog_free_attrs(ca);
            return NULL;
        }
    }
    cc->cat_attrs = ca;
    return ca;
}

static struct catattrs *catalog_entry_attrs(struct cryptcat *cc, DWORD index)
{
    struct catattrs *ca;
    CTL_ENTRY *entry;
    DWORD i;

    if (!cc->entry_attrs)
    {
        if (!(cc->entry_attrs = calloc(cc->inner->cCTLEntry, sizeof(*cc->entry_attrs))))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            return NULL;
        }
        cc->entry_attr_count = cc->inner->cCTLEntry;
    }
    if (cc->entry_attrs[index]) return cc->entry_attrs[index];

    entry = &cc->inner->rgCTLEntry[cc->inner->cCTLEntry - index - 1];
    if (!(ca = catalog_alloc_attrs(entry->cAttribute))) return NULL;
    for (i = 0; i < entry->cAttribute; i++)
    {
        CRYPT_ATTRIBUTE *attr = &entry->rgAttribute[i];

        if (!attr->pszObjId || strcmp(attr->pszObjId, CAT_NAMEVALUE_OBJID) || attr->cValue != 1) continue;
        if (!catalog_add_name_value(cc, ca, attr->rgValue))
        {
            catalog_free_attrs(ca);
            return NULL;
        }
    }
    cc->entry_attrs[index] = ca;
    return ca;
}

static CRYPTCATATTRIBUTE *catalog_next_attr(const struct catattrs *ca, CRYPTCATATTRIBUTE *prev)
{
    DWORD next = 0;

    if (prev)
    {
        if (prev < ca->attrs || prev >= ca->attrs + ca->count)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
        next = prev - ca->attrs + 1;
    }
    if (next >= ca->count)
    {
        SetLastError(CRYPT_E_NOT_FOUND);
        return NULL;
    }
    return &ca->attrs[next];
}

static struct catmember *catalog_created_member(struct cryptcat *cc, CRYPTCATMEMBER *member)
{
    DWORD i;

    for (i = 0; i < cc->member_count; i++)
    {
        if (cc->members[i] == member) return (struct catmember *)member;
    }
    return NULL;
}

static CRYPTCATATTRIBUTE *catalog_next_created_attr(struct catmember *cm, CRYPTCATATTRIBUTE *prev)
{
    DWORD i = 0;

    if (prev)
    {
        for (i = 0; i < cm->attr_count && cm->attrs[i] != prev; i++)
            ;
        if (i == cm->attr_count)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
        i++;
    }
    if (i >= cm->attr_count)
    {
        SetLastError(CRYPT_E_NOT_FOUND);
        return NULL;
    }
    return cm->attrs[i];
}

static BOOL catalog_keep_found_member(struct cryptcat *cc, CRYPTCATMEMBER *member)
{
    CRYPTCATMEMBER **found;

    if (!(found = realloc(cc->found, (cc->found_count + 1) * sizeof(*found))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    cc->found = found;
    cc->found[cc->found_count++] = member;
    return TRUE;
}
#endif

struct catadmin
{
    DWORD magic;
    WCHAR path[MAX_PATH];
    HANDLE find;
    ALG_ID alg;
    DWORD hashLength;
    const WCHAR *providerName;
    DWORD providerType;
};

struct catinfo
{
    DWORD magic;
    WCHAR file[MAX_PATH];
};

static const struct
{
    const WCHAR *name;
    ALG_ID id;
    DWORD length;
} catalog_hashes[] =
{
    {BCRYPT_SHA1_ALGORITHM, CALG_SHA1, 20},
    {BCRYPT_SHA256_ALGORITHM, CALG_SHA_256, 32},
    {BCRYPT_SHA384_ALGORITHM, CALG_SHA_384, 48},
    {BCRYPT_SHA512_ALGORITHM, CALG_SHA_512, 64}
};

static DWORD catalog_hash_policy(const CERT_STRONG_SIGN_PARA *policy, DWORD *allowed)
{
    const CERT_STRONG_SIGN_SERIALIZED_INFO *info;
    const WCHAR *entry, *end, *slash;
    DWORD i;

    *allowed = (1u << ARRAY_SIZE(catalog_hashes)) - 1;
    if (!policy) return ERROR_SUCCESS;
    if (policy->cbSize != sizeof(*policy)) return ERROR_INVALID_PARAMETER;

    if (policy->dwInfoChoice == CERT_STRONG_SIGN_OID_INFO_CHOICE)
    {
        if (!policy->pszOID) return ERROR_INVALID_PARAMETER;
        if (!strcmp(policy->pszOID, szOID_CERT_STRONG_SIGN_OS_1))
            *allowed &= ~1u;
        else if (strcmp(policy->pszOID, szOID_CERT_STRONG_KEY_OS_1))
            return ERROR_INVALID_PARAMETER;
        return ERROR_SUCCESS;
    }

    if (policy->dwInfoChoice != CERT_STRONG_SIGN_SERIALIZED_INFO_CHOICE ||
        !(info = policy->pSerializedInfo) || !info->pwszCNGSignHashAlgids ||
        (info->dwFlags & ~(CERT_STRONG_SIGN_ENABLE_CRL_CHECK | CERT_STRONG_SIGN_ENABLE_OCSP_CHECK)))
        return ERROR_INVALID_PARAMETER;

    *allowed = 0;
    for (entry = info->pwszCNGSignHashAlgids; *entry; entry = *end ? end + 1 : end)
    {
        end = wcschr(entry, ';');
        if (!end) end = entry + wcslen(entry);
        slash = wcschr(entry, '/');
        if (!slash || slash >= end || slash == entry || slash + 1 == end)
            return ERROR_INVALID_PARAMETER;

        if (!((slash - entry == 3 && !wcsncmp(entry, L"RSA", 3)) ||
              (slash - entry == 3 && !wcsncmp(entry, L"DSA", 3)) ||
              (slash - entry == 5 && !wcsncmp(entry, L"ECDSA", 5))))
            continue;

        for (i = 0; i < ARRAY_SIZE(catalog_hashes); ++i)
        {
            SIZE_T length = wcslen(catalog_hashes[i].name);
            if (end - slash - 1 == length && !wcsncmp(slash + 1, catalog_hashes[i].name, length))
                *allowed |= 1u << i;
        }
    }
    return ERROR_SUCCESS;
}

static HCATINFO create_catinfo(const WCHAR *filename)
{
    struct catinfo *ci;

    if (!(ci = malloc(sizeof(*ci))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return INVALID_HANDLE_VALUE;
    }
    lstrcpyW(ci->file, filename);
    ci->magic = CATINFO_MAGIC;
    return ci;
}

/***********************************************************************
 *      CryptCATAdminAcquireContext (WINTRUST.@)
 *
 * Get a catalog administrator context handle.
 *
 * PARAMS
 *   catAdmin  [O] Pointer to the context handle.
 *   sys       [I] Pointer to a GUID for the needed subsystem.
 *   dwFlags   [I] Reserved.
 *
 * RETURNS
 *   Success: TRUE. catAdmin contains the context handle.
 *   Failure: FALSE.
 *
 */
BOOL WINAPI CryptCATAdminAcquireContext(HCATADMIN *catAdmin,
                                        const GUID *sys, DWORD dwFlags)
{
    TRACE("%p %s %lx\n", catAdmin, debugstr_guid(sys), dwFlags);
    return CryptCATAdminAcquireContext2(catAdmin, sys, NULL, NULL, dwFlags);
}

/***********************************************************************
 *             CryptCATAdminAcquireContext2 (WINTRUST.@)
 * Get a catalog administrator context handle.
 *
 * PARAMS
 *   catAdmin  [O] Pointer to the context handle.
 *   sys       [I] Pointer to a GUID for the needed subsystem.
 *   algorithm [I] String of hashing algorithm to use for catalog (SHA1/SHA256).
 *   policy    [I] Pointer to policy structure for checking strong signatures.
 *   dwFlags   [I] Reserved.
 *
 * RETURNS
 *   Success: TRUE. catAdmin contains the context handle.
 *   Failure: FALSE.
 *
 */
BOOL WINAPI CryptCATAdminAcquireContext2(HCATADMIN *catAdmin, const GUID *sys, const WCHAR *algorithm,
                                         const CERT_STRONG_SIGN_PARA *policy, DWORD dwFlags)
{
    static const WCHAR catroot[] =
        {'\\','c','a','t','r','o','o','t',0};
    static const WCHAR catroot2[] =
        {'\\','c','a','t','r','o','o','t','2',0};
    static const WCHAR fmt[] =
        {'%','s','\\','{','%','0','8','x','-','%','0','4','x','-','%','0',
         '4','x','-','%','0','2','x','%','0','2','x','-','%','0','2','x',
         '%','0','2','x','%','0','2','x','%','0','2','x','%','0','2','x',
         '%','0','2','x','}',0};
    static const GUID defsys =
        {0x127d0a1d,0x4ef2,0x11d1,{0x86,0x08,0x00,0xc0,0x4f,0xc2,0x95,0xee}};

    WCHAR catroot_dir[MAX_PATH];
    WCHAR catdb_dir[MAX_PATH], catdb_path[MAX_PATH];
    struct catadmin *ca;
    ALG_ID alg;
    const WCHAR *providerName;
    DWORD providerType;
    DWORD index, allowed, error;

    TRACE("%p %s %s %p %lx\n", catAdmin, debugstr_guid(sys), debugstr_w(algorithm), policy, dwFlags);

    if (!catAdmin || dwFlags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    for (index = 0; algorithm && index < ARRAY_SIZE(catalog_hashes); ++index)
        if (!wcscmp(algorithm, catalog_hashes[index].name)) break;
    if (index == ARRAY_SIZE(catalog_hashes))
    {
        SetLastError(NTE_BAD_ALGID);
        return FALSE;
    }

    error = catalog_hash_policy(policy, &allowed);
    if (error)
    {
        SetLastError(error);
        return FALSE;
    }

    /* Select the lowest supported hash that satisfies both inputs. */
    while (index < ARRAY_SIZE(catalog_hashes) && !(allowed & (1u << index))) ++index;
    if (index == ARRAY_SIZE(catalog_hashes))
    {
        SetLastError(NTE_BAD_ALGID);
        return FALSE;
    }
    alg = catalog_hashes[index].id;
    providerName = index ? MS_ENH_RSA_AES_PROV_W : MS_DEF_PROV_W;
    providerType = index ? PROV_RSA_AES : PROV_RSA_FULL;

    if (!(ca = malloc(sizeof(*ca))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }

    ca->alg = alg;
    ca->hashLength = catalog_hashes[index].length;
    ca->providerName = providerName;
    ca->providerType = providerType;

#ifdef __REACTOS__
    error = GetLastError();
#endif
    GetSystemDirectoryW(catroot_dir, MAX_PATH);
    lstrcatW(catroot_dir, catroot);

    /* create the directory if it doesn't exist */
    CreateDirectoryW(catroot_dir, NULL);

    if (!sys) sys = &defsys;
    swprintf(ca->path, ARRAY_SIZE(ca->path), fmt, catroot_dir, sys->Data1, sys->Data2,
             sys->Data3, sys->Data4[0], sys->Data4[1], sys->Data4[2],
             sys->Data4[3], sys->Data4[4], sys->Data4[5], sys->Data4[6],
             sys->Data4[7]);

    /* create the directory if it doesn't exist */
    CreateDirectoryW(ca->path, NULL);

    GetSystemDirectoryW(catdb_dir, MAX_PATH);
    lstrcatW(catdb_dir, catroot2);
    CreateDirectoryW(catdb_dir, NULL);
    swprintf(catdb_path, ARRAY_SIZE(catdb_path), fmt, catdb_dir, sys->Data1, sys->Data2,
             sys->Data3, sys->Data4[0], sys->Data4[1], sys->Data4[2],
             sys->Data4[3], sys->Data4[4], sys->Data4[5], sys->Data4[6],
             sys->Data4[7]);
    CreateDirectoryW(catdb_path, NULL);

    ca->magic = CATADMIN_MAGIC;
    ca->find = INVALID_HANDLE_VALUE;

    *catAdmin = ca;
#ifdef __REACTOS__
    SetLastError(error);
#endif
    return TRUE;
}

#ifdef __REACTOS__
static BOOL is_valid_catalog_file(const WCHAR *filename)
{
    struct cryptcat *catalog;
    BOOL valid;

    catalog = CryptCATOpen((WCHAR *)filename, CRYPTCAT_OPEN_EXISTING, 0, 0, 0);
    if (catalog == INVALID_HANDLE_VALUE) return FALSE;

    valid = catalog->inner != NULL;
    CryptCATClose(catalog);
    if (!valid) SetLastError(ERROR_BAD_FORMAT);
    return valid;
}
#endif

/***********************************************************************
 *             CryptCATAdminAddCatalog (WINTRUST.@)
 */
HCATINFO WINAPI CryptCATAdminAddCatalog(HCATADMIN catAdmin, PWSTR catalogFile,
                                        PWSTR selectBaseName, DWORD flags)
{
    static const WCHAR slashW[] = {'\\',0};
    struct catadmin *ca = catAdmin;
    struct catinfo *ci;
#ifdef __REACTOS__
    WCHAR generated_name[MAX_PATH];
#endif
    WCHAR *target;
    DWORD len;

    TRACE("%p %s %s %ld\n", catAdmin, debugstr_w(catalogFile),
          debugstr_w(selectBaseName), flags);

#ifdef __REACTOS__
    if (!ca || ca->magic != CATADMIN_MAGIC || !catalogFile || flags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (!is_valid_catalog_file(catalogFile)) return NULL;

    if (!selectBaseName)
    {
        if (!GetTempFileNameW(ca->path, L"cat", 0, generated_name)) return NULL;
        selectBaseName = wcsrchr(generated_name, '\\');
        if (selectBaseName) selectBaseName++;
        else selectBaseName = generated_name;
    }
#else
    if (!selectBaseName)
    {
        FIXME("NULL basename not handled\n");
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (!ca || ca->magic != CATADMIN_MAGIC || !catalogFile || flags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
#endif

    len = lstrlenW(ca->path) + lstrlenW(selectBaseName) + 2;
    if (!(target = malloc(len * sizeof(WCHAR))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    lstrcpyW(target, ca->path);
    lstrcatW(target, slashW);
    lstrcatW(target, selectBaseName);

    if (!CopyFileW(catalogFile, target, FALSE))
    {
        free(target);
        return NULL;
    }
    SetFileAttributesW(target, FILE_ATTRIBUTE_SYSTEM);

    if (!(ci = malloc(sizeof(*ci))))
    {
        free(target);
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    ci->magic = CATINFO_MAGIC;
    lstrcpyW(ci->file, target);

    free(target);
    return ci;
}

static BOOL pe_image_hash( HANDLE file, HCRYPTHASH hash )
{
    UINT32 size, offset, file_size, sig_pos;
    HANDLE mapping;
    BYTE *view;
    IMAGE_NT_HEADERS *nt;
    BOOL ret = FALSE;

    if ((file_size = GetFileSize( file, NULL )) == INVALID_FILE_SIZE) return FALSE;

    if ((mapping = CreateFileMappingW( file, NULL, PAGE_READONLY, 0, 0, NULL )) == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!(view = MapViewOfFile( mapping, FILE_MAP_READ, 0, 0, 0 )) || !(nt = ImageNtHeader( view ))) goto done;

    if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        const IMAGE_NT_HEADERS64 *nt64 = (const IMAGE_NT_HEADERS64 *)nt;

        /* offset from start of file to checksum */
        offset = (BYTE *)&nt64->OptionalHeader.CheckSum - view;

        /* area between checksum and security directory entry */
        size = FIELD_OFFSET( IMAGE_OPTIONAL_HEADER64, DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY] ) -
               FIELD_OFFSET( IMAGE_OPTIONAL_HEADER64, Subsystem );

        if (nt64->OptionalHeader.NumberOfRvaAndSizes < IMAGE_FILE_SECURITY_DIRECTORY + 1) goto done;
        sig_pos = nt64->OptionalHeader.DataDirectory[IMAGE_FILE_SECURITY_DIRECTORY].VirtualAddress;
    }
    else if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        const IMAGE_NT_HEADERS32 *nt32 = (const IMAGE_NT_HEADERS32 *)nt;

        /* offset from start of file to checksum */
        offset = (BYTE *)&nt32->OptionalHeader.CheckSum - view;

        /* area between checksum and security directory entry */
        size = FIELD_OFFSET( IMAGE_OPTIONAL_HEADER32, DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY] ) -
               FIELD_OFFSET( IMAGE_OPTIONAL_HEADER32, Subsystem );

        if (nt32->OptionalHeader.NumberOfRvaAndSizes < IMAGE_FILE_SECURITY_DIRECTORY + 1) goto done;
        sig_pos = nt32->OptionalHeader.DataDirectory[IMAGE_FILE_SECURITY_DIRECTORY].VirtualAddress;
    }
    else goto done;

    if (!CryptHashData( hash, view, offset, 0 )) goto done;
    offset += sizeof(DWORD); /* skip checksum */
    if (!CryptHashData( hash, view + offset, size, 0 )) goto done;

    offset += size + sizeof(IMAGE_DATA_DIRECTORY); /* skip security entry */
    if (offset > file_size) goto done;
    if (sig_pos)
    {
        if (sig_pos < offset) goto done;
        if (sig_pos > file_size) goto done;
        size = sig_pos - offset; /* exclude signature */
    }
    else size = file_size - offset;

    if (!CryptHashData( hash, view + offset, size, 0 )) goto done;
    ret = TRUE;

    if (!sig_pos && (size = file_size % 8))
    {
        static const BYTE pad[7];
        ret = CryptHashData( hash, pad, 8 - size, 0 );
    }

done:
    UnmapViewOfFile( view );
    CloseHandle( mapping );
    return ret;
}

static BOOL catadmin_calc_hash_from_filehandle(HCATADMIN catAdmin, HANDLE hFile, DWORD *pcbHash,
                                               BYTE *pbHash, DWORD dwFlags)
{
    BOOL ret = FALSE;
    struct catadmin *ca = catAdmin;
    ALG_ID alg = CALG_SHA1;
    const WCHAR *providerName = MS_DEF_PROV_W;
    DWORD providerType = PROV_RSA_FULL;
    DWORD hashLength = 20;

    if (!hFile || !pcbHash || dwFlags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (ca)
    {
        alg = ca->alg;
        hashLength = ca->hashLength;
        providerName = ca->providerName;
        providerType = ca->providerType;
    }

    if (*pcbHash < hashLength)
    {
        *pcbHash = hashLength;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return TRUE;
    }

    *pcbHash = hashLength;
    if (pbHash)
    {
        HCRYPTPROV prov;
        HCRYPTHASH hash;
        DWORD bytes_read;
        BYTE *buffer;

        if (!(buffer = malloc(4096)))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        ret = CryptAcquireContextW(&prov, NULL, providerName, providerType, CRYPT_VERIFYCONTEXT);
        if (!ret)
        {
            free(buffer);
            return FALSE;
        }
        ret = CryptCreateHash(prov, alg, 0, 0, &hash);
        if (!ret)
        {
            free(buffer);
            CryptReleaseContext(prov, 0);
            return FALSE;
        }

        if (!(ret = pe_image_hash(hFile, hash)))
        {
            while ((ret = ReadFile(hFile, buffer, 4096, &bytes_read, NULL)) && bytes_read)
            {
                CryptHashData(hash, buffer, bytes_read, 0);
            }
        }
        if (ret) ret = CryptGetHashParam(hash, HP_HASHVAL, pbHash, pcbHash, 0);

        free(buffer);
        CryptDestroyHash(hash);
        CryptReleaseContext(prov, 0);
    }
    return ret;
}

/***********************************************************************
 *             CryptCATAdminCalcHashFromFileHandle (WINTRUST.@)
 */
BOOL WINAPI CryptCATAdminCalcHashFromFileHandle(HANDLE hFile, DWORD *pcbHash, BYTE *pbHash, DWORD dwFlags)
{
    TRACE("%p %p %p %lx\n", hFile, pcbHash, pbHash, dwFlags);
    return catadmin_calc_hash_from_filehandle(NULL, hFile, pcbHash, pbHash, dwFlags);
}

/***********************************************************************
 *    CryptCATAdminCalcHashFromFileHandle2 (WINTRUST.@)
 *
 * Calculate hash for a specific file using a catalog administrator context.
 *
 * PARAMS
 *   catAdmin  [I] Catalog administrator context handle.
 *   hFile     [I] Handle for the file to hash.
 *   pcbHash   [I] Pointer to the length of the hash.
 *   pbHash    [O] Pointer to the buffer that will store that hash
 *   dwFlags   [I] Reserved.
 *
 * RETURNS
 *   Success: TRUE. If pcbHash is too small, LastError will be set to ERROR_INSUFFICIENT_BUFFER.
 *                  pbHash contains the computed hash, if supplied.
 *   Failure: FALSE.
 *
 */
BOOL WINAPI CryptCATAdminCalcHashFromFileHandle2(HCATADMIN catAdmin, HANDLE hFile,  DWORD *pcbHash,
                                                 BYTE *pbHash, DWORD dwFlags)
{
    TRACE("%p %p %p %p %lx\n", catAdmin, hFile, pcbHash, pbHash, dwFlags);

    if (!catAdmin || ((struct catadmin *)catAdmin)->magic != CATADMIN_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return catadmin_calc_hash_from_filehandle(catAdmin, hFile, pcbHash, pbHash, dwFlags);
}


/***********************************************************************
 *             CryptCATAdminEnumCatalogFromHash (WINTRUST.@)
 */
HCATINFO WINAPI CryptCATAdminEnumCatalogFromHash(HCATADMIN hCatAdmin, BYTE* pbHash,
                                                 DWORD cbHash, DWORD dwFlags,
                                                 HCATINFO* phPrevCatInfo )
{
    static const WCHAR slashW[] = {'\\',0};
    static const WCHAR globW[]  = {'\\','*','.','c','a','t',0};

    struct catadmin *ca = hCatAdmin;
    WIN32_FIND_DATAW data;
    HCATINFO prev = NULL;
    HCRYPTPROV prov;
    DWORD size;
    BOOL ret;

    TRACE("%p %p %ld %lx %p\n", hCatAdmin, pbHash, cbHash, dwFlags, phPrevCatInfo);

    if (!ca || ca->magic != CATADMIN_MAGIC || !pbHash || cbHash != ca->hashLength || dwFlags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (phPrevCatInfo) prev = *phPrevCatInfo;

    ret = CryptAcquireContextW(&prov, NULL, ca->providerName, ca->providerType, CRYPT_VERIFYCONTEXT);
    if (!ret) return NULL;

    if (!prev)
    {
        WCHAR *path;

        size = lstrlenW(ca->path) * sizeof(WCHAR) + sizeof(globW);
        if (!(path = malloc(size)))
        {
            CryptReleaseContext(prov, 0);
            SetLastError(ERROR_OUTOFMEMORY);
            return NULL;
        }
        lstrcpyW(path, ca->path);
        lstrcatW(path, globW);

        FindClose(ca->find);
        ca->find = FindFirstFileW(path, &data);

        free(path);
        if (ca->find == INVALID_HANDLE_VALUE)
        {
            CryptReleaseContext(prov, 0);
            return NULL;
        }
    }
    else if (!FindNextFileW(ca->find, &data))
    {
        CryptCATAdminReleaseCatalogContext(hCatAdmin, prev, 0);
        CryptReleaseContext(prov, 0);
        return NULL;
    }

    while (1)
    {
        WCHAR *filename;
        CRYPTCATMEMBER *member = NULL;
        struct catinfo *ci;
        HANDLE hcat;

        size = (lstrlenW(ca->path) + lstrlenW(data.cFileName) + 2) * sizeof(WCHAR);
        if (!(filename = malloc(size)))
        {
            CryptReleaseContext(prov, 0);
            SetLastError(ERROR_OUTOFMEMORY);
            return NULL;
        }
        lstrcpyW(filename, ca->path);
        lstrcatW(filename, slashW);
        lstrcatW(filename, data.cFileName);

        hcat = CryptCATOpen(filename, CRYPTCAT_OPEN_EXISTING, prov, 0, 0);
        if (hcat == INVALID_HANDLE_VALUE)
        {
            WARN("couldn't open %s (%lu)\n", debugstr_w(filename), GetLastError());
            free(filename);
            goto next_catalog;
        }
        while ((member = CryptCATEnumerateMember(hcat, member)))
        {
            if (member->pIndirectData->Digest.cbData != cbHash)
            {
                WARN("amount of hash bytes differs: %lu/%lu\n", member->pIndirectData->Digest.cbData, cbHash);
                continue;
            }
            if (!memcmp(member->pIndirectData->Digest.pbData, pbHash, cbHash))
            {
                TRACE("file %s matches\n", debugstr_w(data.cFileName));

                CryptCATClose(hcat);
                CryptReleaseContext(prov, 0);
                if (!phPrevCatInfo)
                {
                    FindClose(ca->find);
                    ca->find = INVALID_HANDLE_VALUE;
                }
                ci = create_catinfo(filename);
                free(filename);
                return ci;
            }
        }
        CryptCATClose(hcat);
        free(filename);

next_catalog:
        if (!FindNextFileW(ca->find, &data))
        {
            FindClose(ca->find);
            ca->find = INVALID_HANDLE_VALUE;
            CryptReleaseContext(prov, 0);
            return NULL;
        }
    }
    return NULL;
}

/***********************************************************************
 *      CryptCATAdminReleaseCatalogContext (WINTRUST.@)
 *
 * Release a catalog context handle.
 *
 * PARAMS
 *   hCatAdmin [I] Context handle.
 *   hCatInfo  [I] Catalog handle.
 *   dwFlags   [I] Reserved.
 *
 * RETURNS
 *   Success: TRUE.
 *   Failure: FALSE.
 *
 */
BOOL WINAPI CryptCATAdminReleaseCatalogContext(HCATADMIN hCatAdmin,
                                               HCATINFO hCatInfo,
                                               DWORD dwFlags)
{
    struct catinfo *ci = hCatInfo;
    struct catadmin *ca = hCatAdmin;

    TRACE("%p %p %lx\n", hCatAdmin, hCatInfo, dwFlags);

    if (!ca || ca->magic != CATADMIN_MAGIC || !ci || ci->magic != CATINFO_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* Ensure compiler doesn't optimize out the assignment with 0. */
    SecureZeroMemory(&ci->magic, sizeof(ci->magic));
    free(ci);
    return TRUE;
}

#ifdef __REACTOS__
/***********************************************************************
 *      CryptCATVerifyMember (WINTRUST.@)
 */
BOOL WINAPI CryptCATVerifyMember(HANDLE hCatalog, CRYPTCATMEMBER *pCatMember, HANDLE hFileOrMemory)
{
    TRACE("(%p %p %p)\n", hCatalog, pCatMember, hFileOrMemory);

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || !pCatMember || !hFileOrMemory ||
        hFileOrMemory == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* TODO: INFO_NEEDED: when Windows 11 returns TRUE; member files, catalogs and state handles all give FALSE */
    return FALSE;
}

#endif
#ifdef __REACTOS__
/***********************************************************************
 *      MsCatConstructHashTag (WINTRUST.@)
 */
BOOL WINAPI MsCatConstructHashTag(DWORD cbDigest, const BYTE *pbDigest, LPWSTR *ppwszHashTag)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    WCHAR *tag;
    DWORD i;

    TRACE("(%lu %p %p)\n", cbDigest, pbDigest, ppwszHashTag);

    if (!pbDigest || !ppwszHashTag)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!(tag = malloc((cbDigest * 2 + 1) * sizeof(WCHAR))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    for (i = 0; i < cbDigest; i++)
    {
        tag[i * 2] = hex[pbDigest[i] >> 4];
        tag[i * 2 + 1] = hex[pbDigest[i] & 0xf];
    }
    tag[cbDigest * 2] = 0;
    *ppwszHashTag = tag;
    return TRUE;
}

/***********************************************************************
 *      MsCatFreeHashTag (WINTRUST.@)
 */
VOID WINAPI MsCatFreeHashTag(LPWSTR pwszHashTag)
{
    TRACE("(%p)\n", pwszHashTag);

    free(pwszHashTag);
}

#endif
#ifdef __REACTOS__
/***********************************************************************
 *      CryptCATAdminPauseServiceForBackup (WINTRUST.@)
 */
BOOL WINAPI CryptCATAdminPauseServiceForBackup(DWORD dwFlags, BOOL fResume)
{
    TRACE("(%#lx %d)\n", dwFlags, fResume);

    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

#endif
/***********************************************************************
 *      CryptCATAdminReleaseContext (WINTRUST.@)
 *
 * Release a catalog administrator context handle.
 *
 * PARAMS
 *   catAdmin  [I] Context handle.
 *   dwFlags   [I] Reserved.
 *
 * RETURNS
 *   Success: TRUE.
 *   Failure: FALSE.
 *
 */
BOOL WINAPI CryptCATAdminReleaseContext(HCATADMIN hCatAdmin, DWORD dwFlags )
{
    struct catadmin *ca = hCatAdmin;

    TRACE("%p %lx\n", hCatAdmin, dwFlags);

    if (!ca || ca->magic != CATADMIN_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (ca->find != INVALID_HANDLE_VALUE) FindClose(ca->find);
    /* Ensure compiler doesn't optimize out the assignment with 0. */
    SecureZeroMemory(&ca->magic, sizeof(ca->magic));
    free(ca);
    return TRUE;
}

/***********************************************************************
 *      CryptCATAdminRemoveCatalog (WINTRUST.@)
 *
 * Remove a catalog file.
 *
 * PARAMS
 *   catAdmin         [I] Context handle.
 *   pwszCatalogFile  [I] Catalog file.
 *   dwFlags          [I] Reserved.
 *
 * RETURNS
 *   Success: TRUE.
 *   Failure: FALSE.
 *
 */
BOOL WINAPI CryptCATAdminRemoveCatalog(HCATADMIN hCatAdmin, LPCWSTR pwszCatalogFile, DWORD dwFlags)
{
    struct catadmin *ca = hCatAdmin;

    TRACE("%p %s %lx\n", hCatAdmin, debugstr_w(pwszCatalogFile), dwFlags);

    if (!ca || ca->magic != CATADMIN_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* Only delete when there is a filename and no path */
    if (pwszCatalogFile && pwszCatalogFile[0] != 0 &&
        !wcschr(pwszCatalogFile, '\\') && !wcschr(pwszCatalogFile, '/') &&
        !wcschr(pwszCatalogFile, ':'))
    {
        static const WCHAR slashW[] = {'\\',0};
        WCHAR *target;
        DWORD len;

        len = lstrlenW(ca->path) + lstrlenW(pwszCatalogFile) + 2;
        if (!(target = malloc(len * sizeof(WCHAR))))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        lstrcpyW(target, ca->path);
        lstrcatW(target, slashW);
        lstrcatW(target, pwszCatalogFile);

        DeleteFileW(target);

        free(target);
    }

    return TRUE;
}

/***********************************************************************
 *      CryptCATAdminResolveCatalogPath  (WINTRUST.@)
 */
BOOL WINAPI CryptCATAdminResolveCatalogPath(HCATADMIN hcatadmin, WCHAR *catalog_file,
                                            CATALOG_INFO *info, DWORD flags)
{
    static const WCHAR slashW[] = {'\\',0};
    struct catadmin *ca = hcatadmin;

    TRACE("%p %s %p %lx\n", hcatadmin, debugstr_w(catalog_file), info, flags);

    if (!ca || ca->magic != CATADMIN_MAGIC || !info || info->cbStruct != sizeof(*info) || flags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    lstrcpyW(info->wszCatalogFile, ca->path);
    lstrcatW(info->wszCatalogFile, slashW);
    lstrcatW(info->wszCatalogFile, catalog_file);

    return TRUE;
}

/***********************************************************************
 *      CryptCATClose  (WINTRUST.@)
 */
BOOL WINAPI CryptCATClose(HANDLE hCatalog)
{
    struct cryptcat *cc = hCatalog;

    TRACE("(%p)\n", hCatalog);

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    free(cc->attr);
    free(cc->inner);
#ifdef __REACTOS__
    catalog_free_lookups(cc);
    while (cc->member_count)
        catalog_free_member(cc->members[--cc->member_count]);
    free(cc->members);
#endif
    CryptMsgClose(cc->msg);

    /* Ensure compiler doesn't optimize out the assignment with 0. */
    SecureZeroMemory(&cc->magic, sizeof(cc->magic));
    free(cc);
    return TRUE;
}

/***********************************************************************
 *      CryptCATGetAttrInfo  (WINTRUST.@)
 */
CRYPTCATATTRIBUTE * WINAPI CryptCATGetAttrInfo(HANDLE hCatalog, CRYPTCATMEMBER *member, LPWSTR tag)
{
    struct cryptcat *cc = hCatalog;
#ifdef __REACTOS__
    CRYPTCATATTRIBUTE *attr = NULL;

    TRACE("%p, %p, %s\n", hCatalog, member, debugstr_w(tag));

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC || !member || !tag)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    while ((attr = CryptCATEnumerateAttr(hCatalog, member, attr)))
    {
        if (attr->pwszReferenceTag && !wcsicmp(attr->pwszReferenceTag, tag)) return attr;
    }
    if (GetLastError() == CRYPT_E_NOT_FOUND || GetLastError() == ERROR_SUCCESS) SetLastError(CRYPT_E_NOT_FOUND);
    return NULL;
#else

    FIXME("%p, %p, %s\n", hCatalog, member, debugstr_w(tag));

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    SetLastError(CRYPT_E_NOT_FOUND);
    return NULL;
#endif
}

/***********************************************************************
 *      CryptCATGetCatAttrInfo  (WINTRUST.@)
 */
CRYPTCATATTRIBUTE * WINAPI CryptCATGetCatAttrInfo(HANDLE hCatalog, LPWSTR tag)
{
    struct cryptcat *cc = hCatalog;
#ifdef __REACTOS__
    CRYPTCATATTRIBUTE *attr = NULL;

    TRACE("%p, %s\n", hCatalog, debugstr_w(tag));

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC || !tag)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    while ((attr = CryptCATEnumerateCatAttr(hCatalog, attr)))
    {
        if (attr->pwszReferenceTag && !wcsicmp(attr->pwszReferenceTag, tag)) return attr;
    }
    if (GetLastError() == CRYPT_E_NOT_FOUND || GetLastError() == ERROR_SUCCESS) SetLastError(CRYPT_E_NOT_FOUND);
    return NULL;
#else

    FIXME("%p, %s\n", hCatalog, debugstr_w(tag));

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    SetLastError(CRYPT_E_NOT_FOUND);
    return NULL;
#endif
}

CRYPTCATMEMBER * WINAPI CryptCATGetMemberInfo(HANDLE hCatalog, LPWSTR tag)
{
    struct cryptcat *cc = hCatalog;
#ifdef __REACTOS__
    CRYPTCATMEMBER *member = NULL;
    DWORD i, error;

    TRACE("%p, %s\n", hCatalog, debugstr_w(tag));

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC || !tag)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    for (i = 0; i < cc->member_count; i++)
    {
        if (cc->members[i]->pwszReferenceTag && !wcscmp(cc->members[i]->pwszReferenceTag, tag)) return cc->members[i];
    }
    for (i = 0; i < cc->found_count; i++)
    {
        if (!wcscmp(cc->found[i]->pwszReferenceTag, tag)) return cc->found[i];
    }
    error = GetLastError();
    while ((member = CryptCATEnumerateMember(hCatalog, member)))
    {
        if (!member->pwszReferenceTag || wcscmp(member->pwszReferenceTag, tag)) continue;
        if (!catalog_keep_found_member(cc, member))
        {
            catalog_free_found_member(member);
            return NULL;
        }
        SetLastError(error);
        return member;
    }
    SetLastError(error);
    return NULL;
#else

    FIXME("%p, %s\n", hCatalog, debugstr_w(tag));

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    SetLastError(CRYPT_E_NOT_FOUND);
    return NULL;
#endif
}

#ifdef __REACTOS__
/***********************************************************************
 *      CryptCATStoreFromHandle  (WINTRUST.@)
 */
CRYPTCATSTORE * WINAPI CryptCATStoreFromHandle(HANDLE hCatalog)
{
    struct cryptcat *cc = hCatalog;

    TRACE("%p\n", hCatalog);

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return &cc->store;
}

/***********************************************************************
 *      CryptCATHandleFromStore  (WINTRUST.@)
 */
HANDLE WINAPI CryptCATHandleFromStore(CRYPTCATSTORE *pCatStore)
{
    struct cryptcat *cc;

    TRACE("%p\n", pCatStore);

    if (!pCatStore || pCatStore->cbStruct != sizeof(*pCatStore))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    cc = CONTAINING_RECORD(pCatStore, struct cryptcat, store);
    if (cc->magic != CRYPTCAT_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    return cc;
}

/***********************************************************************
 *      CryptCATAllocSortedMemberInfo  (WINTRUST.@)
 */
CRYPTCATMEMBER * WINAPI CryptCATAllocSortedMemberInfo(HANDLE hCatalog, LPWSTR pwszReferenceTag)
{
    struct cryptcat *cc = hCatalog;
    CRYPTCATMEMBER *member = NULL;

    TRACE("%p, %s\n", hCatalog, debugstr_w(pwszReferenceTag));

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC || !pwszReferenceTag)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    while ((member = CryptCATEnumerateMember(hCatalog, member)))
    {
        if (!member->pwszReferenceTag || wcscmp(member->pwszReferenceTag, pwszReferenceTag)) continue;
        member->fdwMemberFlags |= CRYPTCAT_MEMBER_SORTED;
        return member;
    }
    SetLastError(CRYPT_E_NOT_FOUND);
    return NULL;
}

/***********************************************************************
 *      CryptCATFreeSortedMemberInfo  (WINTRUST.@)
 */
void WINAPI CryptCATFreeSortedMemberInfo(HANDLE hCatalog, CRYPTCATMEMBER *pCatMember)
{
    struct cryptcat *cc = hCatalog;

    TRACE("%p, %p\n", hCatalog, pCatMember);

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC || !pCatMember ||
        !(pCatMember->fdwMemberFlags & CRYPTCAT_MEMBER_SORTED))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return;
    }
    catalog_free_found_member(pCatMember);
}

#endif
/***********************************************************************
 *      CryptCATEnumerateAttr  (WINTRUST.@)
 */
CRYPTCATATTRIBUTE * WINAPI CryptCATEnumerateAttr(HANDLE hCatalog, CRYPTCATMEMBER *member, CRYPTCATATTRIBUTE *prev)
{
    struct cryptcat *cc = hCatalog;
#ifdef __REACTOS__
    struct catmember *created;
    struct catattrs *ca;

    TRACE("%p, %p, %p\n", hCatalog, member, prev);

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC || !member)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if ((created = catalog_created_member(cc, member))) return catalog_next_created_attr(created, prev);
    if (!cc->inner || member->dwReserved >= cc->inner->cCTLEntry)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (!(ca = catalog_entry_attrs(cc, member->dwReserved))) return NULL;
    return catalog_next_attr(ca, prev);
#else

    FIXME("%p, %p, %p\n", hCatalog, member, prev);

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    SetLastError(CRYPT_E_NOT_FOUND);
    return NULL;
#endif
}

/***********************************************************************
 *      CryptCATEnumerateCatAttr  (WINTRUST.@)
 */
CRYPTCATATTRIBUTE * WINAPI CryptCATEnumerateCatAttr(HANDLE hCatalog, CRYPTCATATTRIBUTE *prev)
{
    struct cryptcat *cc = hCatalog;
#ifdef __REACTOS__
    struct catattrs *ca;

    TRACE("%p, %p\n", hCatalog, prev);

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (!(ca = catalog_cat_attrs(cc))) return NULL;
    return catalog_next_cat_attr(cc, ca, prev);
#else

    FIXME("%p, %p\n", hCatalog, prev);

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    SetLastError(CRYPT_E_NOT_FOUND);
    return NULL;
#endif
}

/***********************************************************************
 *      CryptCATEnumerateMember  (WINTRUST.@)
 */
#ifdef __REACTOS__
BOOL WINAPI WVTAsn1CatMemberInfo2Decode(DWORD, LPCSTR, const BYTE *, DWORD, DWORD, void *, DWORD *);

static BOOL catalog_is_v2(const struct cryptcat *cc)
{
    return cc->inner && cc->inner->SubjectAlgorithm.pszObjId &&
           !strcmp(cc->inner->SubjectAlgorithm.pszObjId, szOID_CATALOG_LIST_MEMBER2);
}

static SIP_INDIRECT_DATA *catalog_identifier_indirect_data(const CRYPT_DATA_BLOB *identifier)
{
    static char flat_data[] = SPC_CAB_DATA_OBJID, sha1[] = szOID_OIWSEC_sha1, sha256[] = szOID_NIST_sha256;
    SIP_INDIRECT_DATA *indirect;

    if (identifier->cbData != 20 && identifier->cbData != 32)
    {
        SetLastError(CRYPT_E_ATTRIBUTES_MISSING);
        return NULL;
    }
    if (!(indirect = calloc(1, sizeof(*indirect) + identifier->cbData)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    indirect->Data.pszObjId = flat_data;
    indirect->DigestAlgorithm.pszObjId = identifier->cbData == 20 ? sha1 : sha256;
    indirect->Digest.cbData = identifier->cbData;
    indirect->Digest.pbData = (BYTE *)(indirect + 1);
    memcpy(indirect->Digest.pbData, identifier->pbData, identifier->cbData);
    return indirect;
}

static BOOL catalog_hex_tag(const CRYPT_DATA_BLOB *blob, WCHAR **tag)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    WCHAR *p;
    DWORD i;

    if (!(p = realloc(*tag, (blob->cbData * 2 + 1) * sizeof(WCHAR))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    *tag = p;
    for (i = 0; i < blob->cbData; i++)
    {
        p[i * 2] = hex[blob->pbData[i] >> 4];
        p[i * 2 + 1] = hex[blob->pbData[i] & 0xf];
    }
    p[blob->cbData * 2] = 0;
    return TRUE;
}

#endif
CRYPTCATMEMBER * WINAPI CryptCATEnumerateMember(HANDLE hCatalog, CRYPTCATMEMBER *prev)
{
    struct cryptcat *cc = hCatalog;
    CRYPTCATMEMBER *member = prev;
    CTL_ENTRY *entry;
    DWORD size, i;

    TRACE("%p, %p\n", hCatalog, prev);

    if (!hCatalog || hCatalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
#ifdef __REACTOS__
    if (!cc->inner)
    {
        i = cc->member_count;
        if (prev)
        {
            for (i = 0; i < cc->member_count && cc->members[i] != prev; i++);
            if (i == cc->member_count)
                return NULL;
        }
        return i ? cc->members[i - 1] : NULL;
    }
#endif

    /* dumping the contents makes me think that dwReserved is the iteration number */
    if (!member)
    {
        if (!(member = malloc(sizeof(*member))))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            return NULL;
        }
        member->cbStruct = sizeof(*member);
        member->pwszFileName = member->pwszReferenceTag = NULL;
        member->dwReserved = 0;
        member->hReserved = NULL;
        member->gSubjectType = cc->subject;
        member->fdwMemberFlags = 0;
        member->pIndirectData = NULL;
        member->dwCertVersion = cc->inner->dwVersion;
    }
    else member->dwReserved++;

    if (member->dwReserved >= cc->inner->cCTLEntry)
    {
#ifdef __REACTOS__
        SetLastError(ERROR_SUCCESS);
#else
        SetLastError(ERROR_INVALID_PARAMETER);
#endif
        goto error;
    }

    /* list them backwards, like native */
    entry = &cc->inner->rgCTLEntry[cc->inner->cCTLEntry - member->dwReserved - 1];

    member->sEncodedIndirectData.cbData = member->sEncodedMemberInfo.cbData = 0;
    member->sEncodedIndirectData.pbData = member->sEncodedMemberInfo.pbData = NULL;
    free(member->pIndirectData);
    member->pIndirectData = NULL;

    for (i = 0; i < entry->cAttribute; i++)
    {
        CRYPT_ATTRIBUTE *attr = entry->rgAttribute + i;

        if (attr->cValue != 1)
        {
            ERR("Can't handle attr->cValue of %lu\n", attr->cValue);
            continue;
        }
        if (!strcmp(attr->pszObjId, CAT_MEMBERINFO_OBJID))
        {
            CAT_MEMBERINFO *mi;
            BOOL ret;

            member->sEncodedMemberInfo.cbData = attr->rgValue->cbData;
            member->sEncodedMemberInfo.pbData = attr->rgValue->pbData;

            CryptDecodeObject(cc->encoding, CAT_MEMBERINFO_OBJID, attr->rgValue->pbData, attr->rgValue->cbData, 0, NULL, &size);

            if (!(mi = malloc(size)))
            {
                SetLastError(ERROR_OUTOFMEMORY);
                goto error;
            }
            ret = CryptDecodeObject(cc->encoding, CAT_MEMBERINFO_OBJID, attr->rgValue->pbData, attr->rgValue->cbData, 0, mi, &size);
            if (ret)
            {
                UNICODE_STRING guid;

                member->dwCertVersion = mi->dwCertVersion;
                RtlInitUnicodeString(&guid, mi->pwszSubjGuid);
                if (RtlGUIDFromString(&guid, &member->gSubjectType))
                {
                    free(mi);
                    goto error;
                }
            }
            free(mi);
            if (!ret) goto error;
        }
#ifdef __REACTOS__
        else if (!strcmp(attr->pszObjId, CAT_MEMBERINFO2_OBJID))
        {
            CAT_MEMBERINFO2 mi2;

            member->sEncodedMemberInfo.cbData = attr->rgValue->cbData;
            member->sEncodedMemberInfo.pbData = attr->rgValue->pbData;
            size = sizeof(mi2);
            if (!WVTAsn1CatMemberInfo2Decode(cc->encoding, CAT_MEMBERINFO2_STRUCT, attr->rgValue->pbData,
                                             attr->rgValue->cbData, 0, &mi2, &size))
            {
                catalog_free_found_member(member);
                return NULL;
            }
            member->gSubjectType = mi2.SubjectGuid;
            member->dwCertVersion = mi2.dwCertVersion;
        }
        else if (!strcmp(attr->pszObjId, CAT_NAMEVALUE_OBJID))
            continue;
#endif
        else if (!strcmp(attr->pszObjId, SPC_INDIRECT_DATA_OBJID))
        {
            /* SPC_INDIRECT_DATA_CONTENT is equal to SIP_INDIRECT_DATA */

            member->sEncodedIndirectData.cbData = attr->rgValue->cbData;
            member->sEncodedIndirectData.pbData = attr->rgValue->pbData;

            CryptDecodeObject(cc->encoding, SPC_INDIRECT_DATA_OBJID, attr->rgValue->pbData, attr->rgValue->cbData, 0, NULL, &size);

            if (!(member->pIndirectData = malloc(size)))
            {
                SetLastError(ERROR_OUTOFMEMORY);
                goto error;
            }
            CryptDecodeObject(cc->encoding, SPC_INDIRECT_DATA_OBJID, attr->rgValue->pbData, attr->rgValue->cbData, 0, member->pIndirectData, &size);
        }
        else
            /* this object id should probably be handled in CryptCATEnumerateAttr */
            FIXME("unhandled object id \"%s\"\n", attr->pszObjId);
    }

#ifdef __REACTOS__
    if (catalog_is_v2(cc))
    {
        if (!member->sEncodedMemberInfo.cbData)
        {
            ERR("Corrupted catalog entry?\n");
            SetLastError(CRYPT_E_ATTRIBUTES_MISSING);
        }
        else if ((member->pIndirectData ||
                  (member->pIndirectData = catalog_identifier_indirect_data(&entry->SubjectIdentifier))) &&
                 catalog_hex_tag(&entry->SubjectIdentifier, &member->pwszReferenceTag))
            return member;
        catalog_free_found_member(member);
        return NULL;
    }
#endif
    if (!member->sEncodedMemberInfo.cbData || !member->sEncodedIndirectData.cbData)
    {
        ERR("Corrupted catalog entry?\n");
        SetLastError(CRYPT_E_ATTRIBUTES_MISSING);
        goto error;
    }
#ifdef __REACTOS__
    if (cc->version == CRYPTCAT_VERSION_1)
    {
        size = entry->SubjectIdentifier.cbData + sizeof(WCHAR);
        if (!(member->pwszReferenceTag = realloc(member->pwszReferenceTag, size)))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            goto error;
        }
        memcpy(member->pwszReferenceTag, entry->SubjectIdentifier.pbData, entry->SubjectIdentifier.cbData);
        member->pwszReferenceTag[entry->SubjectIdentifier.cbData / sizeof(WCHAR)] = 0;
        return member;
    }
#endif
    size = (2 * member->pIndirectData->Digest.cbData + 1) * sizeof(WCHAR);
    member->pwszReferenceTag = realloc(member->pwszReferenceTag, size);

    if (!member->pwszReferenceTag)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        goto error;
    }
    /* FIXME: reference tag is usually the file hash but doesn't have to be */
    for (i = 0; i < member->pIndirectData->Digest.cbData; i++)
    {
        DWORD sub;

        sub = member->pIndirectData->Digest.pbData[i] >> 4;
        member->pwszReferenceTag[i * 2] = (sub < 10 ? '0' + sub : 'A' + sub - 10);
        sub = member->pIndirectData->Digest.pbData[i] & 0xf;
        member->pwszReferenceTag[i * 2 + 1] = (sub < 10 ? '0' + sub : 'A' + sub - 10);
    }
    member->pwszReferenceTag[i * 2] = 0;
    return member;

error:
    free(member->pIndirectData);
    free(member->pwszReferenceTag);
    free(member);
    return NULL;
}

static CTL_INFO *decode_inner_content(HANDLE hmsg, DWORD encoding, DWORD *len)
{
    DWORD size;
    LPSTR oid = NULL;
    BYTE *buffer = NULL;
    CTL_INFO *inner = NULL;

    if (!CryptMsgGetParam(hmsg, CMSG_INNER_CONTENT_TYPE_PARAM, 0, NULL, &size)) return NULL;
    if (!(oid = malloc(size)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    if (!CryptMsgGetParam(hmsg, CMSG_INNER_CONTENT_TYPE_PARAM, 0, oid, &size)) goto out;
    if (!CryptMsgGetParam(hmsg, CMSG_CONTENT_PARAM, 0, NULL, &size)) goto out;
    if (!(buffer = malloc(size)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        goto out;
    }
    if (!CryptMsgGetParam(hmsg, CMSG_CONTENT_PARAM, 0, buffer, &size)) goto out;
    if (!CryptDecodeObject(encoding, oid, buffer, size, 0, NULL, &size)) goto out;
    if (!(inner = malloc(size)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        goto out;
    }
    if (!CryptDecodeObject(encoding, oid, buffer, size, 0, inner, &size)) goto out;
    *len = size;

out:
    free(oid);
    free(buffer);
    return inner;
}

/***********************************************************************
 *      CryptCATCatalogInfoFromContext  (WINTRUST.@)
 */
BOOL WINAPI CryptCATCatalogInfoFromContext(HCATINFO hcatinfo, CATALOG_INFO *info, DWORD flags)
{
    struct catinfo *ci = hcatinfo;

    TRACE("%p, %p, %lx\n", hcatinfo, info, flags);

    if (!hcatinfo || hcatinfo == INVALID_HANDLE_VALUE || ci->magic != CATINFO_MAGIC ||
        flags || !info || info->cbStruct != sizeof(*info))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    lstrcpyW(info->wszCatalogFile, ci->file);
    return TRUE;
}

/***********************************************************************
 *      CryptCATPutAttrInfo  (WINTRUST.@)
 */
CRYPTCATATTRIBUTE * WINAPI CryptCATPutAttrInfo(HANDLE catalog, CRYPTCATMEMBER *member,
        WCHAR *name, DWORD flags, DWORD size, BYTE *data)
{
#ifdef __REACTOS__
    struct cryptcat *cc = catalog;
    struct catmember *cm = (struct catmember *)member;
    CRYPTCATATTRIBUTE *attr, **attrs;
    DWORD i;

    TRACE("catalog %p, member %p, name %s, flags %#lx, size %lu, data %p\n",
          catalog, member, debugstr_w(name), flags, size, data);

    if (!catalog || catalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC || !member || !name ||
        (size && !data))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    for (i = 0; i < cc->member_count && cc->members[i] != member; i++)
        ;
    if (i == cc->member_count)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    if (!(attrs = realloc(cm->attrs, (cm->attr_count + 1) * sizeof(*attrs))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    cm->attrs = attrs;
    if (!(attr = catalog_alloc_attribute(name, flags, size, data))) return NULL;

    cm->attrs[cm->attr_count++] = attr;
    return attr;
#else
    FIXME("catalog %p, member %p, name %s, flags %#lx, size %lu, data %p, stub!\n",
            catalog, member, debugstr_w(name), flags, size, data);

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
#endif
}

/***********************************************************************
 *      CryptCATPutCatAttrInfo  (WINTRUST.@)
 */
CRYPTCATATTRIBUTE * WINAPI CryptCATPutCatAttrInfo(HANDLE catalog,
        WCHAR *name, DWORD flags, DWORD size, BYTE *data)
{
#ifdef __REACTOS__
    struct cryptcat *cc = catalog;
    CRYPTCATATTRIBUTE *attr, **attrs;
    struct catattrs *ca;
    DWORD i;

    TRACE("catalog %p, name %s, flags %#lx, size %lu, data %p\n", catalog, debugstr_w(name), flags, size, data);

    if (!catalog || catalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC || !name || (size && !data))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (!(attr = catalog_alloc_attribute(name, flags, size, data))) return NULL;

    for (i = 0; (flags & CRYPTCAT_ATTR_DATAREPLACE) && i < cc->put_attr_count; i++)
    {
        if (wcsicmp(cc->put_attrs[i]->pwszReferenceTag, name)) continue;
        free(cc->put_attrs[i]);
        cc->put_attrs[i] = attr;
        return attr;
    }
    if ((flags & CRYPTCAT_ATTR_DATAREPLACE) && (ca = catalog_cat_attrs(cc)))
    {
        for (i = 0; i < ca->count; i++)
        {
            if (wcsicmp(ca->attrs[i].pwszReferenceTag, name)) continue;
            free(ca->values[i]);
            ca->values[i] = attr;
            ca->attrs[i] = *attr;
            ca->replaced = TRUE;
            return &ca->attrs[i];
        }
    }
    if (!(attrs = realloc(cc->put_attrs, (cc->put_attr_count + 1) * sizeof(*attrs))))
    {
        free(attr);
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    cc->put_attrs = attrs;
    cc->put_attrs[cc->put_attr_count++] = attr;
    return attr;
#else
    FIXME("catalog %p, name %s, flags %#lx, size %lu, data %p, stub!\n",
            catalog, debugstr_w(name), flags, size, data);

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
#endif
}

#ifdef __REACTOS__
static BOOL catalog_decode_hash_tag(const WCHAR *tag, BYTE *hash, DWORD hash_len)
{
    BYTE value, any = 0;
    DWORD i;

    if (lstrlenW(tag) != hash_len * 2) return FALSE;
    for (i = 0; i < hash_len * 2; i++)
    {
        if (tag[i] >= '0' && tag[i] <= '9') value = tag[i] - '0';
        else if (tag[i] >= 'A' && tag[i] <= 'F') value = tag[i] - 'A' + 10;
        else return FALSE;
        if (hash) hash[i / 2] = (i & 1) ? (hash[i / 2] | value) : (value << 4);
        any |= value;
    }
    return any != 0;
}

#endif
/***********************************************************************
 *      CryptCATPutMemberInfo  (WINTRUST.@)
 */
CRYPTCATMEMBER * WINAPI CryptCATPutMemberInfo(HANDLE catalog, WCHAR *filename,
        WCHAR *member, GUID *subject, DWORD version, DWORD size, BYTE *data)
{
#ifdef __REACTOS__
    struct cryptcat *cc = catalog;
    const SIP_INDIRECT_DATA *indirect = (const SIP_INDIRECT_DATA *)data;
    DWORD tag_len, file_len, data_oid_len, alg_oid_len, total;
    CRYPTCATMEMBER *m, **members;
    struct catmember *cm;
    SIP_INDIRECT_DATA *copy;
    BYTE *p;

    TRACE("catalog %p, filename %s, member %s, subject %s, version %lu, size %lu, data %p\n",
          catalog, debugstr_w(filename), debugstr_w(member), debugstr_guid(subject), version, size, data);

    if (!catalog || catalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC || !member || !subject ||
        !data || size < sizeof(*indirect) ||
        (cc->version != CRYPTCAT_VERSION_1 && cc->version != CRYPTCAT_VERSION_2) ||
        (cc->version == CRYPTCAT_VERSION_2 && !catalog_decode_hash_tag(member, NULL, indirect->Digest.cbData)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    tag_len = (lstrlenW(member) + 1) * sizeof(WCHAR);
    file_len = filename ? (lstrlenW(filename) + 1) * sizeof(WCHAR) : 0;
    data_oid_len = indirect->Data.pszObjId ? strlen(indirect->Data.pszObjId) + 1 : 0;
    alg_oid_len = indirect->DigestAlgorithm.pszObjId ? strlen(indirect->DigestAlgorithm.pszObjId) + 1 : 0;
    total = sizeof(*cm) + sizeof(*copy) + tag_len + file_len + data_oid_len + indirect->Data.Value.cbData +
            alg_oid_len + indirect->DigestAlgorithm.Parameters.cbData + indirect->Digest.cbData;

    if (!(members = realloc(cc->members, (cc->member_count + 1) * sizeof(*members))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    cc->members = members;
    if (!(cm = calloc(1, total)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }

    m = &cm->member;
    copy = (SIP_INDIRECT_DATA *)(cm + 1);
    p = (BYTE *)(copy + 1);
    m->cbStruct = sizeof(*m);
    m->pwszReferenceTag = (WCHAR *)p;
    memcpy(p, member, tag_len);
    p += tag_len;
    if (filename)
    {
        m->pwszFileName = (WCHAR *)p;
        memcpy(p, filename, file_len);
        p += file_len;
    }
    m->gSubjectType = *subject;
    m->pIndirectData = copy;
    m->dwCertVersion = version;
    if (data_oid_len)
    {
        copy->Data.pszObjId = (char *)p;
        memcpy(p, indirect->Data.pszObjId, data_oid_len);
        p += data_oid_len;
    }
    copy->Data.Value.cbData = indirect->Data.Value.cbData;
    copy->Data.Value.pbData = p;
    memcpy(p, indirect->Data.Value.pbData, indirect->Data.Value.cbData);
    p += indirect->Data.Value.cbData;
    if (alg_oid_len)
    {
        copy->DigestAlgorithm.pszObjId = (char *)p;
        memcpy(p, indirect->DigestAlgorithm.pszObjId, alg_oid_len);
        p += alg_oid_len;
    }
    copy->DigestAlgorithm.Parameters.cbData = indirect->DigestAlgorithm.Parameters.cbData;
    copy->DigestAlgorithm.Parameters.pbData = p;
    memcpy(p, indirect->DigestAlgorithm.Parameters.pbData, indirect->DigestAlgorithm.Parameters.cbData);
    p += indirect->DigestAlgorithm.Parameters.cbData;
    copy->Digest.cbData = indirect->Digest.cbData;
    copy->Digest.pbData = p;
    memcpy(p, indirect->Digest.pbData, indirect->Digest.cbData);

    cc->members[cc->member_count++] = m;
    return m;
#else
    FIXME("catalog %p, filename %s, member %s, subject %s, version %lu, size %lu, data %p, stub!\n",
            catalog, debugstr_w(filename), debugstr_w(member), debugstr_guid(subject), version, size, data);

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
#endif
}

#ifdef __REACTOS__
BOOL WINAPI WVTAsn1SpcIndirectDataContentEncode(DWORD, LPCSTR, const void *, BYTE *, DWORD *);
BOOL WINAPI WVTAsn1CatMemberInfoEncode(DWORD, LPCSTR, const void *, BYTE *, DWORD *);
BOOL WINAPI WVTAsn1CatNameValueEncode(DWORD, LPCSTR, const void *, BYTE *, DWORD *);
BOOL WINAPI WVTAsn1CatMemberInfo2Encode(DWORD, LPCSTR, const void *, BYTE *, DWORD *);

struct catentry
{
    CTL_ENTRY        entry;
    CRYPT_ATTRIBUTE *attr;
    CRYPT_ATTR_BLOB *value;
    DWORD            count;
    BYTE            *identifier;
};

static int __cdecl catentry_compare(const void *a, const void *b)
{
    const CRYPT_DATA_BLOB *x = &((const CTL_ENTRY *)a)->SubjectIdentifier;
    const CRYPT_DATA_BLOB *y = &((const CTL_ENTRY *)b)->SubjectIdentifier;
    int r = memcmp(x->pbData, y->pbData, min(x->cbData, y->cbData));

    if (r) return r;
    return x->cbData < y->cbData ? -1 : x->cbData > y->cbData;
}

static BOOL catalog_encode(LPCSTR type, const void *info, BOOL (WINAPI *encode)(DWORD, LPCSTR, const void *, BYTE *, DWORD *),
                           CRYPT_ATTR_BLOB *blob)
{
    blob->cbData = 0;
    if (!encode(X509_ASN_ENCODING, type, info, NULL, &blob->cbData)) return FALSE;
    if (!(blob->pbData = malloc(blob->cbData)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    return encode(X509_ASN_ENCODING, type, info, blob->pbData, &blob->cbData);
}

static BOOL catalog_entry_attributes(const CRYPTCATMEMBER *m, struct catentry *e, DWORD base)
{
    const struct catmember *cm = (const struct catmember *)m;
    DWORD i;

    e->count = base + cm->attr_count;
    if (!(e->attr = calloc(e->count, sizeof(*e->attr))) || !(e->value = calloc(e->count, sizeof(*e->value))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    for (i = 0; i < e->count; i++)
    {
        e->attr[i].cValue = 1;
        e->attr[i].rgValue = &e->value[i];
    }
    e->entry.cAttribute = e->count;
    e->entry.rgAttribute = e->attr;

    for (i = 0; i < cm->attr_count; i++)
    {
        const CRYPTCATATTRIBUTE *attr = cm->attrs[i];
        CAT_NAMEVALUE value = { attr->pwszReferenceTag, attr->dwAttrTypeAndAction, { attr->cbValue, attr->pbValue } };

        e->attr[base + i].pszObjId = (char *)CAT_NAMEVALUE_OBJID;
        if (!catalog_encode(CAT_NAMEVALUE_STRUCT, &value, WVTAsn1CatNameValueEncode, &e->value[base + i]))
            return FALSE;
    }
    return TRUE;
}

static BOOL catalog_member_entry(const CRYPTCATMEMBER *m, struct catentry *e)
{
    WCHAR guid[39];
    CAT_MEMBERINFO info = { guid, m->dwCertVersion };

    swprintf(guid, ARRAY_SIZE(guid), L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
             m->gSubjectType.Data1, m->gSubjectType.Data2, m->gSubjectType.Data3,
             m->gSubjectType.Data4[0], m->gSubjectType.Data4[1], m->gSubjectType.Data4[2],
             m->gSubjectType.Data4[3], m->gSubjectType.Data4[4], m->gSubjectType.Data4[5],
             m->gSubjectType.Data4[6], m->gSubjectType.Data4[7]);

    if (!catalog_entry_attributes(m, e, 2)) return FALSE;
    e->entry.SubjectIdentifier.cbData = (lstrlenW(m->pwszReferenceTag) + 1) * sizeof(WCHAR);
    e->entry.SubjectIdentifier.pbData = (BYTE *)m->pwszReferenceTag;
    e->attr[0].pszObjId = (char *)SPC_INDIRECT_DATA_OBJID;
    e->attr[1].pszObjId = (char *)CAT_MEMBERINFO_OBJID;

    return catalog_encode(SPC_INDIRECT_DATA_CONTENT_STRUCT, m->pIndirectData, WVTAsn1SpcIndirectDataContentEncode,
                          &e->value[0]) &&
           catalog_encode(CAT_MEMBERINFO_STRUCT, &info, WVTAsn1CatMemberInfoEncode, &e->value[1]);
}

static BOOL catalog_member_entry_v2(const CRYPTCATMEMBER *m, struct catentry *e)
{
    DWORD hash_len = m->pIndirectData->Digest.cbData;
    CAT_MEMBERINFO2 info2;

    if (!catalog_entry_attributes(m, e, 1)) return FALSE;
    e->attr[0].pszObjId = (char *)CAT_MEMBERINFO2_OBJID;

    if (!(e->identifier = malloc(hash_len)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    catalog_decode_hash_tag(m->pwszReferenceTag, e->identifier, hash_len);
    e->entry.SubjectIdentifier.cbData = hash_len;
    e->entry.SubjectIdentifier.pbData = e->identifier;

    info2.SubjectGuid = m->gSubjectType;
    info2.dwCertVersion = m->dwCertVersion;
    return catalog_encode(CAT_MEMBERINFO2_STRUCT, &info2, WVTAsn1CatMemberInfo2Encode, &e->value[0]);
}

#endif
/***********************************************************************
 *      CryptCATPersistStore  (WINTRUST.@)
 */
#ifdef __REACTOS__
static BOOL catalog_build_extensions(struct cryptcat *cc, CTL_INFO *info)
{
    DWORD inner = cc->inner ? cc->inner->cExtension : 0, i, j;
    BOOL rebuild = cc->cat_attrs && cc->cat_attrs->replaced;
    CERT_EXTENSION *ext;

    info->cExtension = inner;
    info->rgExtension = inner ? cc->inner->rgExtension : NULL;
    if (!cc->put_attr_count && !rebuild) return TRUE;

    if (!(ext = calloc(inner + cc->put_attr_count, sizeof(*ext))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    if (inner) memcpy(ext, cc->inner->rgExtension, inner * sizeof(*ext));
    info->rgExtension = ext;
    info->cExtension = inner + cc->put_attr_count;
    for (i = 0, j = 0; rebuild && i < inner && j < cc->cat_attrs->count; i++)
    {
        const CRYPTCATATTRIBUTE *attr = &cc->cat_attrs->attrs[j];
        CAT_NAMEVALUE value = { attr->pwszReferenceTag, attr->dwAttrTypeAndAction, { attr->cbValue, attr->pbValue } };

        if (!ext[i].pszObjId || strcmp(ext[i].pszObjId, CAT_NAMEVALUE_OBJID)) continue;
        j++;
        if (!catalog_encode(CAT_NAMEVALUE_STRUCT, &value, WVTAsn1CatNameValueEncode, &ext[i].Value))
            return FALSE;
    }
    for (i = 0; i < cc->put_attr_count; i++)
    {
        const CRYPTCATATTRIBUTE *attr = cc->put_attrs[i];
        CAT_NAMEVALUE value = { attr->pwszReferenceTag, attr->dwAttrTypeAndAction, { attr->cbValue, attr->pbValue } };

        ext[inner + i].pszObjId = (char *)CAT_NAMEVALUE_OBJID;
        if (!catalog_encode(CAT_NAMEVALUE_STRUCT, &value, WVTAsn1CatNameValueEncode, &ext[inner + i].Value))
            return FALSE;
    }
    return TRUE;
}

static void catalog_free_extensions(struct cryptcat *cc, CTL_INFO *info)
{
    DWORD inner = cc->inner ? cc->inner->cExtension : 0, i;

    if (!info->rgExtension || info->rgExtension == (cc->inner ? cc->inner->rgExtension : NULL)) return;
    for (i = 0; i < info->cExtension; i++)
        if (i >= inner || info->rgExtension[i].Value.pbData != cc->inner->rgExtension[i].Value.pbData)
            free(info->rgExtension[i].Value.pbData);
    free(info->rgExtension);
}

#endif
BOOL WINAPI CryptCATPersistStore(HANDLE catalog)
{
#ifdef __REACTOS__
    static char catalog_list[] = szOID_CATALOG_LIST;
    struct cryptcat *cc = catalog;
    CMSG_SIGNED_ENCODE_INFO sign_info = { sizeof(sign_info) };
    LPSTR usage = catalog_list;
    struct catentry *entries = NULL;
    DWORD count = 0, old_count, i, j, size, written;
    BYTE *ctl = NULL, *buffer = NULL;
    HCRYPTMSG msg = NULL;
    CTL_ENTRY *rg = NULL;
    CTL_INFO info;
    HANDLE file;
    UUID list_id;
    BOOL ret = FALSE;

    TRACE("catalog %p\n", catalog);

    if (!catalog || catalog == INVALID_HANDLE_VALUE || cc->magic != CRYPTCAT_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(&info, 0, sizeof(info));
    old_count = cc->inner ? cc->inner->cCTLEntry : 0;
    if (!(entries = calloc(old_count + cc->member_count + 1, sizeof(*entries))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    for (i = 0; i < old_count; i++)
        entries[count++].entry = cc->inner->rgCTLEntry[i];
    for (i = 0; i < cc->member_count; i++)
    {
        struct catentry *e = &entries[count++];

        if (!(cc->version == CRYPTCAT_VERSION_2 ? catalog_member_entry_v2(cc->members[i], e) :
                                                  catalog_member_entry(cc->members[i], e)))
            goto done;
    }
    if (!(rg = calloc(count + 1, sizeof(*rg))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        goto done;
    }
    for (i = 0; i < count; i++)
        rg[i] = entries[i].entry;
    qsort(rg, count, sizeof(*rg), catentry_compare);

    info.dwVersion = CTL_V1;
    info.SubjectUsage.cUsageIdentifier = 1;
    info.SubjectUsage.rgpszUsageIdentifier = &usage;
    if (UuidCreate(&list_id) != RPC_S_OK)
    {
        SetLastError(NTE_FAIL);
        goto done;
    }
    info.ListIdentifier.cbData = sizeof(list_id);
    info.ListIdentifier.pbData = (BYTE *)&list_id;
    GetSystemTimeAsFileTime(&info.ThisUpdate);
    info.SubjectAlgorithm.pszObjId = (char *)(cc->version == CRYPTCAT_VERSION_2 ? szOID_CATALOG_LIST_MEMBER2 :
                                                                         szOID_CATALOG_LIST_MEMBER);
    if (!catalog_build_extensions(cc, &info)) goto done;
    info.cCTLEntry = count;
    info.rgCTLEntry = rg;

    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, PKCS_CTL, &info, CRYPT_ENCODE_ALLOC_FLAG, NULL, &ctl, &size))
        goto done;
    if (!(msg = CryptMsgOpenToEncode(cc->encoding, 0, CMSG_SIGNED, &sign_info, (LPSTR)szOID_CTL, NULL)))
        goto done;
    if (!CryptMsgUpdate(msg, ctl, size, TRUE)) goto done;
    if (!CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, NULL, &size)) goto done;
    if (!(buffer = malloc(size)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        goto done;
    }
    if (!CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, buffer, &size)) goto done;

    if (!cc->store.pwszP7File)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        goto done;
    }
    file = CreateFileW(cc->store.pwszP7File, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    ret = WriteFile(file, buffer, size, &written, NULL) && written == size;
    CloseHandle(file);
    if (ret)
        SetLastError(ERROR_SUCCESS);

done:
    catalog_free_extensions(cc, &info);
    free(buffer);
    if (msg) CryptMsgClose(msg);
    LocalFree(ctl);
    free(rg);
    for (i = old_count; i < count; i++)
    {
        for (j = 0; entries[i].value && j < entries[i].count; j++)
            free(entries[i].value[j].pbData);
        free(entries[i].value);
        free(entries[i].attr);
        free(entries[i].identifier);
    }
    free(entries);
    return ret;
#else
    FIXME("catalog %p, stub!\n", catalog);

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
#endif
}

/***********************************************************************
 *      CryptCATOpen  (WINTRUST.@)
 */
HANDLE WINAPI CryptCATOpen(WCHAR *filename, DWORD flags, HCRYPTPROV hProv,
                           DWORD dwPublicVersion, DWORD dwEncodingType)
{
    HANDLE file, hmsg;
    BYTE *buffer = NULL;
    DWORD size, open_mode = OPEN_ALWAYS;
    struct cryptcat *cc;
    BOOL valid;

    TRACE("filename %s, flags %#lx, provider %#Ix, version %#lx, type %#lx\n",
          debugstr_w(filename), flags, hProv, dwPublicVersion, dwEncodingType);

    if (!filename)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    if (!dwEncodingType)  dwEncodingType  = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;

    if (flags == CRYPTCAT_OPEN_EXISTING)
        open_mode = OPEN_EXISTING;
    if (flags & CRYPTCAT_OPEN_CREATENEW)
        open_mode = CREATE_ALWAYS;

    file = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL, open_mode, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    size = GetFileSize(file, NULL);
    if (!(buffer = malloc(size)))
    {
        CloseHandle(file);
        SetLastError(ERROR_OUTOFMEMORY);
        return INVALID_HANDLE_VALUE;
    }
    if (!(hmsg = CryptMsgOpenToDecode(dwEncodingType, 0, 0, hProv, NULL, NULL)))
    {
        CloseHandle(file);
        free(buffer);
        return INVALID_HANDLE_VALUE;
    }
    if (!size) valid = FALSE;
    else if (!ReadFile(file, buffer, size, &size, NULL))
    {
        CloseHandle(file);
        free(buffer);
        CryptMsgClose(hmsg);
        return INVALID_HANDLE_VALUE;
    }
    else valid = CryptMsgUpdate(hmsg, buffer, size, TRUE);
    free(buffer);
    CloseHandle(file);

    size = sizeof(DWORD);
#ifdef __REACTOS__
    if ((cc = calloc(1, sizeof(*cc) + (lstrlenW(filename) + 1) * sizeof(WCHAR))))
    {
        cc->filename = (WCHAR *)(cc + 1);
        lstrcpyW(cc->filename, filename);
        cc->version = dwPublicVersion;
    }
    else
#else
    if (!(cc = calloc(1, sizeof(*cc))))
#endif
    {
        CryptMsgClose(hmsg);
        SetLastError(ERROR_OUTOFMEMORY);
        return INVALID_HANDLE_VALUE;
    }

    cc->msg = hmsg;
    cc->encoding = dwEncodingType;
    if (!valid)
    {
        cc->magic = CRYPTCAT_MAGIC;
#ifdef __REACTOS__
        if (!cc->version)
            cc->version = CRYPTCAT_VERSION_1;
        catalog_init_store(cc, flags, hProv);
#endif
        SetLastError(ERROR_SUCCESS);
        return cc;
    }
    else if (CryptMsgGetParam(hmsg, CMSG_ATTR_CERT_COUNT_PARAM, 0, &cc->attr_count, &size))
    {
        DWORD i, sum = 0;
        BYTE *p;

        for (i = 0; i < cc->attr_count; i++)
        {
            if (!CryptMsgGetParam(hmsg, CMSG_ATTR_CERT_PARAM, i, NULL, &size))
            {
                CryptMsgClose(hmsg);
                free(cc);
                return INVALID_HANDLE_VALUE;
            }
            sum += size;
        }
        if (!(cc->attr = malloc(sizeof(*cc->attr) * cc->attr_count + sum)))
        {
            CryptMsgClose(hmsg);
            free(cc);
            SetLastError(ERROR_OUTOFMEMORY);
            return INVALID_HANDLE_VALUE;
        }
        p = (BYTE *)(cc->attr + cc->attr_count);
        for (i = 0; i < cc->attr_count; i++)
        {
            if (!CryptMsgGetParam(hmsg, CMSG_ATTR_CERT_PARAM, i, NULL, &size))
            {
                CryptMsgClose(hmsg);
                free(cc->attr);
                free(cc);
                return INVALID_HANDLE_VALUE;
            }
            if (!CryptMsgGetParam(hmsg, CMSG_ATTR_CERT_PARAM, i, p, &size))
            {
                CryptMsgClose(hmsg);
                free(cc->attr);
                free(cc);
                return INVALID_HANDLE_VALUE;
            }
            p += size;
        }
        cc->inner = decode_inner_content(hmsg, dwEncodingType, &cc->inner_len);
        if (!cc->inner || !CryptSIPRetrieveSubjectGuid(filename, NULL, &cc->subject))
        {
            CryptMsgClose(hmsg);
            free(cc->attr);
            free(cc->inner);
            free(cc);
            return INVALID_HANDLE_VALUE;
        }
#ifdef __REACTOS__
        if (!cc->version && cc->inner->SubjectAlgorithm.pszObjId)
        {
            if (!strcmp(cc->inner->SubjectAlgorithm.pszObjId, szOID_CATALOG_LIST_MEMBER))
                cc->version = CRYPTCAT_VERSION_1;
            else if (!strcmp(cc->inner->SubjectAlgorithm.pszObjId, szOID_CATALOG_LIST_MEMBER2))
                cc->version = CRYPTCAT_VERSION_2;
        }
        if (!cc->version)
            cc->version = CRYPTCAT_VERSION_1;
        catalog_init_store(cc, flags, hProv);
#endif
        cc->magic = CRYPTCAT_MAGIC;
        SetLastError(ERROR_SUCCESS);
        return cc;
    }
    free(cc);
    return INVALID_HANDLE_VALUE;
}

#ifdef __REACTOS__
BOOL WINAPI WVTAsn1SpcLinkEncode(DWORD, LPCSTR, const void *, BYTE *, DWORD *);

static BOOL include_pe_page_hashes;

struct cab_layout
{
    DWORD cabinet_size;
    DWORD shift;
    DWORD reserve_size;
    DWORD folders;
    DWORD strings_offset;
    DWORD strings_size;
    DWORD folders_offset;
    DWORD folder_size;
    DWORD rest_offset;
    BOOL  has_reserve;
};

static DWORD sip_get32(const BYTE *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24);
}

static void sip_put32(BYTE *p, DWORD v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static BOOL sip_cab_layout(const BYTE *data, DWORD size, struct cab_layout *cab)
{
    DWORD folder_reserve = 0, strings = 0, offset;

    if (size < 36 || memcmp(data, "MSCF", 4)) goto bad;
    cab->cabinet_size = sip_get32(data + 8);
    if (cab->cabinet_size < 36 || cab->cabinet_size > size) goto bad;
    cab->folders = data[26] | (data[27] << 8);
    cab->has_reserve = (data[30] & 4) != 0;
    if (cab->has_reserve)
    {
        if (cab->cabinet_size < 40) goto bad;
        cab->reserve_size = data[36] | (data[37] << 8);
        folder_reserve = data[38];
        cab->folders_offset = 40 + cab->reserve_size;
        cab->shift = cab->reserve_size < 20 ? 20 - cab->reserve_size : 0;
    }
    else
    {
        cab->reserve_size = 0;
        cab->folders_offset = 36;
        cab->shift = 24;
    }
    if (data[30] & 1) strings += 2;
    if (data[30] & 2) strings += 2;
    cab->strings_offset = cab->folders_offset;
    for (offset = cab->strings_offset; strings; strings--)
    {
        while (offset < cab->cabinet_size && data[offset]) offset++;
        if (offset++ >= cab->cabinet_size) goto bad;
    }
    cab->strings_size = offset - cab->strings_offset;
    cab->folders_offset = offset;
    cab->folder_size = 8 + folder_reserve;
    cab->rest_offset = cab->folders_offset + cab->folders * cab->folder_size;
    if (cab->rest_offset > cab->cabinet_size) goto bad;
    return TRUE;

bad:
    SetLastError(ERROR_BAD_FORMAT);
    return FALSE;
}

static void sip_cab_header(const BYTE *data, const struct cab_layout *cab, BYTE *header)
{
    memcpy(header, data, 36);
    sip_put32(header + 8, cab->cabinet_size + cab->shift);
    sip_put32(header + 16, sip_get32(data + 16) + cab->shift);
    header[30] |= 4;
}

static BOOL sip_hash_cab(const BYTE *data, const struct cab_layout *cab, HCRYPTHASH hash)
{
    BYTE header[36], tail[2] = { 0, 0 }, folder[4];
    DWORD i;

    sip_cab_header(data, cab, header);
    if (!CryptHashData(hash, header, 4, 0) || !CryptHashData(hash, header + 8, 28, 0) ||
        !CryptHashData(hash, tail, sizeof(tail), 0) ||
        !CryptHashData(hash, data + cab->strings_offset, cab->strings_size, 0))
        return FALSE;
    for (i = 0; i < cab->folders; i++)
    {
        const BYTE *entry = data + cab->folders_offset + i * cab->folder_size;

        sip_put32(folder, sip_get32(entry) + cab->shift);
        if (!CryptHashData(hash, folder, sizeof(folder), 0) ||
            !CryptHashData(hash, entry + 4, cab->folder_size - 4, 0))
            return FALSE;
    }
    return CryptHashData(hash, data + cab->rest_offset, cab->cabinet_size - cab->rest_offset, 0);
}

static void sip_cab_reserve_layout(const BYTE *data, const struct cab_layout *cab, DWORD signature_size, BYTE *out)
{
    static const BYTE reserve_magic[] = { 0x00, 0x00, 0x10, 0x00 };
    DWORD reserve = cab->reserve_size + cab->shift - (cab->has_reserve ? 0 : 4), i;
    BYTE *p;

    sip_cab_header(data, cab, out);
    p = out + 36;
    p[0] = reserve;
    p[1] = reserve >> 8;
    if (cab->has_reserve)
    {
        p[2] = data[38];
        p[3] = data[39];
        memcpy(p + 4, data + 40, min(cab->reserve_size, reserve));
    }
    memcpy(p + 4, reserve_magic, sizeof(reserve_magic));
    sip_put32(p + 8, signature_size ? cab->cabinet_size + cab->shift : 0);
    sip_put32(p + 12, signature_size);
    p += 4 + reserve;
    memcpy(p, data + cab->strings_offset, cab->strings_size);
    p += cab->strings_size;
    for (i = 0; i < cab->folders; i++)
    {
        const BYTE *entry = data + cab->folders_offset + i * cab->folder_size;

        sip_put32(p, sip_get32(entry) + cab->shift);
        memcpy(p + 4, entry + 4, cab->folder_size - 4);
        p += cab->folder_size;
    }
    memcpy(p, data + cab->rest_offset, cab->cabinet_size - cab->rest_offset);
}

static BOOL sip_cab_signing_layout(const BYTE *data, DWORD size, struct cab_layout *cab)
{
    DWORD offset, length;

    if (!cab->has_reserve)
        return TRUE;
    if (cab->reserve_size < 20 || data[39] || sip_get32(data + 40) != 0x00100000)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    offset = sip_get32(data + 44);
    length = sip_get32(data + 48);
    if (length && (offset > size || length > size - offset))
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    cab->shift = 20 - cab->reserve_size;
    return TRUE;
}

static BYTE *sip_cab_prepare(HANDLE file, const BYTE **view, HANDLE *mapping, DWORD *size, struct cab_layout *cab)
{
    DWORD prepared_size = cab->cabinet_size + cab->shift, written;
    BYTE *prepared;

    if (!(prepared = calloc(1, prepared_size + *size - cab->cabinet_size)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    sip_cab_reserve_layout(*view, cab, 0, prepared);
    memcpy(prepared + prepared_size, *view + cab->cabinet_size, *size - cab->cabinet_size);
    prepared_size += *size - cab->cabinet_size;
    UnmapViewOfFile(*view);
    *view = NULL;
    CloseHandle(*mapping);
    *mapping = NULL;
    if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
        !WriteFile(file, prepared, prepared_size, &written, NULL) || written != prepared_size ||
        !SetEndOfFile(file) || !sip_cab_layout(prepared, prepared_size, cab))
    {
        free(prepared);
        return NULL;
    }
    *size = prepared_size;
    return prepared;
}

static BYTE *sip_der(BYTE tag, const BYTE *data, DWORD len, DWORD *out_len)
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

static BYTE *sip_moniker(const char *oid, const BYTE *octets, DWORD octets_len, DWORD *out_len)
{
    static const BYTE class_id[] = SpcSerializedObjectAttributesClassId;
    CRYPT_DATA_BLOB octet_string = { octets_len, (BYTE *)octets };
    CRYPT_ATTR_BLOB value = { 0, NULL };
    CRYPT_ATTRIBUTE attr = { (char *)oid, 1, &value };
    CRYPT_ATTRIBUTES attrs = { 1, &attr };
    SPC_LINK link = { SPC_MONIKER_LINK_CHOICE };
    BYTE *serialized = NULL, *out = NULL;
    DWORD serialized_len;

    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_OCTET_STRING, &octet_string, CRYPT_ENCODE_ALLOC_FLAG, NULL,
                             &value.pbData, &value.cbData))
        return NULL;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, PKCS_ATTRIBUTES, &attrs, CRYPT_ENCODE_ALLOC_FLAG, NULL,
                             &serialized, &serialized_len))
        goto done;
    memcpy(link.Moniker.ClassId, class_id, sizeof(class_id));
    link.Moniker.SerializedData.cbData = serialized_len;
    link.Moniker.SerializedData.pbData = serialized;
    if (!WVTAsn1SpcLinkEncode(X509_ASN_ENCODING, SPC_LINK_STRUCT, &link, NULL, out_len)) goto done;
    if (!(out = malloc(*out_len)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        goto done;
    }
    if (!WVTAsn1SpcLinkEncode(X509_ASN_ENCODING, SPC_LINK_STRUCT, &link, out, out_len))
    {
        free(out);
        out = NULL;
    }

done:
    LocalFree(serialized);
    LocalFree(value.pbData);
    return out;
}

static BOOL sip_hash_page(HCRYPTPROV prov, ALG_ID alg, const BYTE *data, DWORD len, DWORD page_len, BYTE *out,
                          DWORD hash_len)
{
    HCRYPTHASH hash;
    BYTE *page;
    BOOL ret;

    if (!(page = calloc(1, page_len)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    memcpy(page, data, len);
    ret = CryptCreateHash(prov, alg, 0, 0, &hash);
    if (ret)
    {
        ret = CryptHashData(hash, page, page_len, 0) && CryptGetHashParam(hash, HP_HASHVAL, out, &hash_len, 0);
        CryptDestroyHash(hash);
    }
    free(page);
    return ret;
}

static const IMAGE_NT_HEADERS *sip_pe_header_layout(const BYTE *view, DWORD file_size, DWORD *checksum,
                                                    DWORD *security, DWORD *headers)
{
    const IMAGE_NT_HEADERS *nt;

    if (!(nt = ImageNtHeader((void *)view)))
        return NULL;
    if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        const IMAGE_NT_HEADERS64 *nt64 = (const IMAGE_NT_HEADERS64 *)nt;

        *checksum = (const BYTE *)&nt64->OptionalHeader.CheckSum - view;
        *security = (const BYTE *)&nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY] - view;
        *headers = nt64->OptionalHeader.SizeOfHeaders;
    }
    else if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        const IMAGE_NT_HEADERS32 *nt32 = (const IMAGE_NT_HEADERS32 *)nt;

        *checksum = (const BYTE *)&nt32->OptionalHeader.CheckSum - view;
        *security = (const BYTE *)&nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY] - view;
        *headers = nt32->OptionalHeader.SizeOfHeaders;
    }
    else
        return NULL;
    if (*headers > file_size || *security + sizeof(IMAGE_DATA_DIRECTORY) > *headers)
        return NULL;
    return nt;
}

static BOOL sip_pe_header_page_hash(const BYTE *view, DWORD file_size, HCRYPTPROV prov, ALG_ID alg, BYTE *out,
                                    DWORD hash_len)
{
    DWORD checksum, security, headers, header_len = 0;
    BYTE *header;
    BOOL ret;

    if (!sip_pe_header_layout(view, file_size, &checksum, &security, &headers))
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    if (!(header = malloc(headers)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    memcpy(header, view, checksum);
    header_len += checksum;
    memcpy(header + header_len, view + checksum + sizeof(DWORD), security - checksum - sizeof(DWORD));
    header_len += security - checksum - sizeof(DWORD);
    memcpy(header + header_len, view + security + sizeof(IMAGE_DATA_DIRECTORY), headers - security - sizeof(IMAGE_DATA_DIRECTORY));
    header_len += headers - security - sizeof(IMAGE_DATA_DIRECTORY);
    ret = sip_hash_page(prov, alg, header, header_len,
                        ((headers + 0xfff) & ~0xfff) - sizeof(DWORD) - sizeof(IMAGE_DATA_DIRECTORY), out, hash_len);
    free(header);
    return ret;
}

/***********************************************************************
 *      ComputeFirstPageHash  (WINTRUST.@)
 */
int WINAPI ComputeFirstPageHash(LPCWSTR pwszHashAlg, LPCWSTR pwszFile, PVOID pbHash, int cbHash)
{
    DWORD hash_len, size;
    BYTE digest[32], *view = NULL;
    HANDLE file, mapping = NULL;
    HCRYPTPROV prov = 0;
    ALG_ID alg;
    int ret = 0;

    TRACE("(%s %s %p %d)\n", debugstr_w(pwszHashAlg), debugstr_w(pwszFile), pbHash, cbHash);

    if (!pwszHashAlg || !pwszFile)
        return 0;
    if (!wcscmp(pwszHashAlg, L"SHA256"))
    {
        alg = CALG_SHA_256;
        hash_len = 32;
    }
    else if (!wcscmp(pwszHashAlg, L"SHA1"))
    {
        alg = CALG_SHA1;
        hash_len = 20;
    }
    else
        return 0;
    if (!pbHash || cbHash != (int)hash_len)
        return hash_len;

    file = CreateFileW(pwszFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    size = GetFileSize(file, NULL);
    if ((mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL)) &&
        (view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) &&
        CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        sip_pe_header_page_hash(view, size, prov, alg, digest, hash_len))
    {
        memcpy(pbHash, digest, hash_len);
        ret = hash_len;
    }
    if (prov)
        CryptReleaseContext(prov, 0);
    if (view)
        UnmapViewOfFile(view);
    if (mapping)
        CloseHandle(mapping);
    CloseHandle(file);
    if (ret)
        SetLastError(ERROR_SUCCESS);
    return ret;
}

static BYTE *sip_pe_page_hashes(const BYTE *view, DWORD file_size, HCRYPTPROV prov, ALG_ID alg, DWORD hash_len,
                                DWORD *table_len)
{
    const IMAGE_NT_HEADERS *nt;
    const IMAGE_SECTION_HEADER *section;
    DWORD checksum, security, headers, count = 2, entry = 4 + hash_len, i, offset, end = 0, len;
    BYTE *table, *p;

    if (!(nt = sip_pe_header_layout(view, file_size, &checksum, &security, &headers))) goto bad;

    section = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (!section[i].SizeOfRawData) continue;
        if (section[i].PointerToRawData > file_size) goto bad;
        count += (section[i].SizeOfRawData + 0xfff) / 0x1000;
    }

    *table_len = count * entry;
    if (!(table = calloc(1, *table_len)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    p = table;
    sip_put32(p, 0);
    if (!sip_pe_header_page_hash(view, file_size, prov, alg, p + 4, hash_len))
        goto fail;
    p += entry;
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (!section[i].SizeOfRawData) continue;
        end = section[i].PointerToRawData + section[i].SizeOfRawData;
        for (offset = section[i].PointerToRawData; offset < end; offset += 0x1000)
        {
            len = min(0x1000, end - offset);
            if (offset + len > file_size) len = offset < file_size ? file_size - offset : 0;
            sip_put32(p, offset);
            if (!sip_hash_page(prov, alg, view + offset, len, 0x1000, p + 4, hash_len)) goto fail;
            p += entry;
        }
    }
    sip_put32(p, end);
    return table;

fail:
    free(table);
    return NULL;

bad:
    SetLastError(ERROR_BAD_FORMAT);
    return NULL;
}

static BYTE *sip_pe_image_value(const BYTE *link, DWORD link_len, DWORD *out_len)
{
    static const BYTE flags[] = { 0x03, 0x01, 0x00 };
    BYTE *file, *seq, *body;
    DWORD file_len;

    if (!(file = sip_der(0xa0, link, link_len, &file_len))) return NULL;
    if (!(body = malloc(sizeof(flags) + file_len)))
    {
        free(file);
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    memcpy(body, flags, sizeof(flags));
    memcpy(body + sizeof(flags), file, file_len);
    seq = sip_der(0x30, body, sizeof(flags) + file_len, out_len);
    free(body);
    free(file);
    return seq;
}

/* TODO: INFO_NEEDED: the files Windows 11 handles with this SIP; every call tried on it does nothing */
static const GUID sip_18b3c141_guid = { 0x18B3C141, 0xAE0D, 0x40F9, { 0x94,0x65,0xE5,0x42,0xAF,0xC1,0xAB,0xC7 } };

static BOOL sip_create_indirect_data(SIP_SUBJECTINFO* pSubjectInfo, DWORD* pcbIndirectData,
                                     SIP_INDIRECT_DATA* pIndirectData, BOOL prepare)
{
    static const GUID peGUID = { 0xC689AAB8, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
    static const GUID cabGUID = { 0xC689AABA, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
    static const GUID flatGUID = { 0xDE351A42, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
    static const BYTE flat_value[] = { 0xa2, 0x02, 0x80, 0x00 };
    static const BYTE pe_value[] = { 0x30, 0x09, 0x03, 0x01, 0x00, 0xa0, 0x04, 0xa2, 0x02, 0x80, 0x00 };
    enum { SUBJECT_FLAT, SUBJECT_PE, SUBJECT_CAB } kind;
    DWORD value_len = 0, data_oid_len, alg_oid_len, hash_len, len, size, file_size = 0, table_len;
    HANDLE file = INVALID_HANDLE_VALUE, mapping = NULL;
    BYTE *value = NULL, *table = NULL, *link = NULL, *enhanced = NULL, *prepared = NULL, *p;
    const BYTE *view = NULL, *cab_data = NULL;
    struct cab_layout cab;
    BOOL ret = FALSE, page_hashes = FALSE;
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    const char *data_oid;
    ALG_ID alg;

    TRACE("(%p %p %p)\n", pSubjectInfo, pcbIndirectData, pIndirectData);

    if (!pSubjectInfo || !pSubjectInfo->pgSubjectType || !pcbIndirectData ||
        !pSubjectInfo->DigestAlgorithm.pszObjId)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (IsEqualGUID(pSubjectInfo->pgSubjectType, &flatGUID))
    {
        kind = SUBJECT_FLAT;
        data_oid = SPC_CAB_DATA_OBJID;
    }
    else if (IsEqualGUID(pSubjectInfo->pgSubjectType, &peGUID))
    {
        kind = SUBJECT_PE;
        data_oid = SPC_PE_IMAGE_DATA_OBJID;
        if (pSubjectInfo->dwFlags & SPC_EXC_PE_PAGE_HASHES_FLAG)
            page_hashes = FALSE;
        else if (pSubjectInfo->dwFlags & SPC_INC_PE_PAGE_HASHES_FLAG)
            page_hashes = TRUE;
        else
            page_hashes = include_pe_page_hashes;
    }
    else if (IsEqualGUID(pSubjectInfo->pgSubjectType, &cabGUID))
    {
        kind = SUBJECT_CAB;
        data_oid = SPC_LINK_OBJID;
    }
    else if (IsEqualGUID(pSubjectInfo->pgSubjectType, &sip_18b3c141_guid))
        return FALSE;
    else
    {
        FIXME("unimplemented for subject type %s\n", debugstr_guid(pSubjectInfo->pgSubjectType));
        SetLastError(TRUST_E_SUBJECT_FORM_UNKNOWN);
        return FALSE;
    }

    SetLastError(ERROR_SUCCESS);
    if (!(alg = CertOIDToAlgId(pSubjectInfo->DigestAlgorithm.pszObjId)) || GET_ALG_CLASS(alg) != ALG_CLASS_HASH)
        return FALSE;
    if (pSubjectInfo->hProv)
        prov = pSubjectInfo->hProv;
    else if (!CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return FALSE;
    if (!CryptCreateHash(prov, alg, 0, 0, &hash)) goto done;
    len = sizeof(hash_len);
    if (!CryptGetHashParam(hash, HP_HASHSIZE, (BYTE *)&hash_len, &len, 0)) goto done;

    if (pIndirectData || page_hashes)
    {
        if (pSubjectInfo->hFile && pSubjectInfo->hFile != INVALID_HANDLE_VALUE)
            file = pSubjectInfo->hFile;
        else if ((file = CreateFileW(pSubjectInfo->pwsFileName,
                                     kind == SUBJECT_CAB && prepare ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                                     FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE)
            goto done;
        file_size = GetFileSize(file, NULL);
        if (!(mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL))) goto done;
        if (!(view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0))) goto done;
    }

    if (kind == SUBJECT_CAB)
    {
        BYTE cabinet_size[4] = { 0 };
        HCRYPTHASH enhanced_hash;

        if (!(enhanced = calloc(1, hash_len)))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            goto done;
        }
        if (view)
        {
            if (!sip_cab_layout(view, file_size, &cab) ||
                (prepare && !sip_cab_signing_layout(view, file_size, &cab)) ||
                (prepare && cab.shift && !(prepared = sip_cab_prepare(file, &view, &mapping, &file_size, &cab))))
                goto done;
            if (!prepare)
                cab.shift = 0;
            cab_data = prepared ? prepared : view;
            sip_put32(cabinet_size, cab.cabinet_size + cab.shift);
            if (!CryptCreateHash(prov, alg, 0, 0, &enhanced_hash)) goto done;
            len = hash_len;
            ret = CryptHashData(enhanced_hash, cabinet_size, sizeof(cabinet_size), 0) &&
                  CryptGetHashParam(enhanced_hash, HP_HASHVAL, enhanced, &len, 0);
            CryptDestroyHash(enhanced_hash);
            if (!ret) goto done;
            ret = FALSE;
        }
        if (!(value = sip_moniker(szOID_ENHANCED_HASH, enhanced, hash_len, &value_len))) goto done;
    }
    else if (page_hashes)
    {
        SetLastError(ERROR_BAD_FORMAT);
        if (!(table = sip_pe_page_hashes(view, file_size, prov, alg, hash_len, &table_len))) goto done;
        if (!(link = sip_moniker(alg == CALG_SHA1 ? SPC_PE_IMAGE_PAGE_HASHES_V1_OBJID :
                                 SPC_PE_IMAGE_PAGE_HASHES_V2_OBJID, table, table_len, &len)))
            goto done;
        if (!(value = sip_pe_image_value(link, len, &value_len))) goto done;
    }
    else
    {
        const BYTE *fixed = kind == SUBJECT_PE ? pe_value : flat_value;

        value_len = kind == SUBJECT_PE ? sizeof(pe_value) : sizeof(flat_value);
        if (!(value = malloc(value_len)))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            goto done;
        }
        memcpy(value, fixed, value_len);
    }

    data_oid_len = strlen(data_oid) + 1;
    alg_oid_len = strlen(pSubjectInfo->DigestAlgorithm.pszObjId) + 1;
    size = sizeof(*pIndirectData) + data_oid_len + value_len + alg_oid_len + hash_len;
    if (!pIndirectData)
    {
        *pcbIndirectData = size;
        ret = TRUE;
        goto done;
    }
    if (*pcbIndirectData < size)
        goto done;

    if (kind == SUBJECT_PE)
    {
        SetLastError(ERROR_BAD_FORMAT);
        if (!pe_image_hash(file, hash)) goto done;
    }
    else if (kind == SUBJECT_CAB)
    {
        if (!sip_hash_cab(cab_data, &cab, hash)) goto done;
    }
    else if (!CryptHashData(hash, view, file_size, 0))
        goto done;

    p = (BYTE *)(pIndirectData + 1);
    pIndirectData->Data.pszObjId = (char *)p;
    memcpy(p, data_oid, data_oid_len);
    p += data_oid_len;
    pIndirectData->Data.Value.cbData = value_len;
    pIndirectData->Data.Value.pbData = p;
    memcpy(p, value, value_len);
    p += value_len;
    pIndirectData->DigestAlgorithm.pszObjId = (char *)p;
    memcpy(p, pSubjectInfo->DigestAlgorithm.pszObjId, alg_oid_len);
    p += alg_oid_len;
    pIndirectData->DigestAlgorithm.Parameters.cbData = 0;
    pIndirectData->DigestAlgorithm.Parameters.pbData = NULL;
    pIndirectData->Digest.cbData = hash_len;
    pIndirectData->Digest.pbData = p;
    if (!CryptGetHashParam(hash, HP_HASHVAL, p, &hash_len, 0)) goto done;
    ret = TRUE;

done:
    free(value);
    free(link);
    free(table);
    free(enhanced);
    free(prepared);
    if (view) UnmapViewOfFile(view);
    if (mapping) CloseHandle(mapping);
    if (file != INVALID_HANDLE_VALUE && file != pSubjectInfo->hFile) CloseHandle(file);
    if (hash) CryptDestroyHash(hash);
    if (prov != pSubjectInfo->hProv) CryptReleaseContext(prov, 0);
    return ret;
}

#endif
/***********************************************************************
 *      CryptSIPCreateIndirectData  (WINTRUST.@)
 */
BOOL WINAPI CryptSIPCreateIndirectData(SIP_SUBJECTINFO* pSubjectInfo, DWORD* pcbIndirectData,
                                       SIP_INDIRECT_DATA* pIndirectData)
{
#ifdef __REACTOS__
    return sip_create_indirect_data(pSubjectInfo, pcbIndirectData, pIndirectData, TRUE);
#else
    FIXME("(%p %p %p) stub\n", pSubjectInfo, pcbIndirectData, pIndirectData);
 
    return FALSE;
#endif
}


#ifdef __REACTOS__
void WINAPI WintrustSetDefaultIncludePEPageHashes(BOOL fIncludePEPageHashes)
{
    TRACE("(%d)\n", fIncludePEPageHashes);
    include_pe_page_hashes = fIncludePEPageHashes;
}

#endif
#ifndef __REACTOS__
/***********************************************************************
 *      CryptCATCDFClose  (WINTRUST.@)
 */
BOOL WINAPI CryptCATCDFClose(CRYPTCATCDF *pCDF)
{
    FIXME("(%p) stub\n", pCDF);

    return FALSE;
}

/***********************************************************************
 *      CryptCATCDFEnumCatAttributes  (WINTRUST.@)
 */
CRYPTCATATTRIBUTE * WINAPI CryptCATCDFEnumCatAttributes(CRYPTCATCDF *pCDF,
                                                        CRYPTCATATTRIBUTE *pPrevAttr,
                                                        PFN_CDF_PARSE_ERROR_CALLBACK pfnParseError)
{
    FIXME("(%p %p %p) stub\n", pCDF, pPrevAttr, pfnParseError);

    return NULL;
}

/***********************************************************************
 *      CryptCATCDFEnumMembersByCDFTagEx  (WINTRUST.@)
 */
LPWSTR WINAPI CryptCATCDFEnumMembersByCDFTagEx(CRYPTCATCDF *pCDF, LPWSTR pwszPrevCDFTag,
                                               PFN_CDF_PARSE_ERROR_CALLBACK pfnParseError,
                                               CRYPTCATMEMBER **ppMember, BOOL fContinueOnError,
                                               LPVOID pvReserved)
{
    FIXME("(%p %s %p %p %d %p) stub\n", pCDF, debugstr_w(pwszPrevCDFTag), pfnParseError,
          ppMember, fContinueOnError, pvReserved);

    return NULL;
}

/***********************************************************************
 *      CryptCATCDFOpen  (WINTRUST.@)
 */
CRYPTCATCDF * WINAPI CryptCATCDFOpen(LPWSTR pwszFilePath,
                                     PFN_CDF_PARSE_ERROR_CALLBACK pfnParseError)
{
    FIXME("(%s %p) stub\n", debugstr_w(pwszFilePath), pfnParseError);

    return NULL;
}
#endif

static BOOL WINTRUST_GetSignedMsgFromPEFile(SIP_SUBJECTINFO *pSubjectInfo,
 DWORD *pdwEncodingType, DWORD dwIndex, DWORD *pcbSignedDataMsg,
 BYTE *pbSignedDataMsg)
{
    BOOL ret;
    WIN_CERTIFICATE *pCert = NULL;
    HANDLE file;

    TRACE("(%p %p %ld %p %p)\n", pSubjectInfo, pdwEncodingType, dwIndex,
          pcbSignedDataMsg, pbSignedDataMsg);

    if(pSubjectInfo->hFile && pSubjectInfo->hFile!=INVALID_HANDLE_VALUE)
        file = pSubjectInfo->hFile;
    else
    {
        file = CreateFileW(pSubjectInfo->pwsFileName, GENERIC_READ,
                FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if(file == INVALID_HANDLE_VALUE)
            return FALSE;
    }
 
    if (!pbSignedDataMsg)
    {
        WIN_CERTIFICATE cert;

        /* app hasn't passed buffer, just get the length */
        ret = ImageGetCertificateHeader(file, dwIndex, &cert);
        if (ret)
        {
            switch (cert.wCertificateType)
            {
            case WIN_CERT_TYPE_X509:
            case WIN_CERT_TYPE_PKCS_SIGNED_DATA:
                *pcbSignedDataMsg = cert.dwLength;
                break;
            default:
                WARN("unknown certificate type %d\n", cert.wCertificateType);
                ret = FALSE;
            }
        }
    }
    else
    {
        DWORD len = 0;

        ret = ImageGetCertificateData(file, dwIndex, NULL, &len);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            goto error;
        pCert = malloc(len);
        if (!pCert)
        {
            ret = FALSE;
            goto error;
        }
        ret = ImageGetCertificateData(file, dwIndex, pCert, &len);
        if (!ret)
            goto error;
        pCert->dwLength -= FIELD_OFFSET(WIN_CERTIFICATE, bCertificate);
        if (*pcbSignedDataMsg < pCert->dwLength)
        {
            *pcbSignedDataMsg = pCert->dwLength;
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            ret = FALSE;
        }
        else
        {
            memcpy(pbSignedDataMsg, pCert->bCertificate, pCert->dwLength);
            *pcbSignedDataMsg = pCert->dwLength;
            switch (pCert->wCertificateType)
            {
            case WIN_CERT_TYPE_X509:
                *pdwEncodingType = X509_ASN_ENCODING;
                break;
            case WIN_CERT_TYPE_PKCS_SIGNED_DATA:
                *pdwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
                break;
            default:
                WARN("don't know what to do for encoding type %d\n",
                 pCert->wCertificateType);
                *pdwEncodingType = 0;
                ret = FALSE;
            }
        }
    }
error:
    if(pSubjectInfo->hFile != file)
        CloseHandle(file);
    free(pCert);
    return ret;
}

#ifdef __REACTOS__
static BOOL WINTRUST_IsWritableHandle(HANDLE file)
{
    OBJECT_BASIC_INFORMATION info;

    if (NtQueryObject(file, ObjectBasicInformation, &info, sizeof(info), NULL)) return TRUE;
    return (info.GrantedAccess & FILE_WRITE_DATA) != 0;
}

#endif
static BOOL WINTRUST_PutSignedMsgToPEFile(SIP_SUBJECTINFO* pSubjectInfo, DWORD pdwEncodingType,
        DWORD* pdwIndex, DWORD cbSignedDataMsg, BYTE* pbSignedDataMsg)
{
    WIN_CERTIFICATE *cert;
    HANDLE file;
    DWORD size;
    BOOL ret;

    if(pSubjectInfo->hFile && pSubjectInfo->hFile!=INVALID_HANDLE_VALUE)
    {
        file = pSubjectInfo->hFile;
#ifdef __REACTOS__
        if (!WINTRUST_IsWritableHandle(file))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
#endif
    }
    else
    {
        file = CreateFileW(pSubjectInfo->pwsFileName, GENERIC_READ|GENERIC_WRITE,
                FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if(file == INVALID_HANDLE_VALUE)
            return FALSE;
    }

    /* int aligned WIN_CERTIFICATE structure with cbSignedDataMsg+1 bytes of data */
#ifdef __REACTOS__
    size = (FIELD_OFFSET(WIN_CERTIFICATE, bCertificate[cbSignedDataMsg]) + 7) & ~7;
#else
    size = FIELD_OFFSET(WIN_CERTIFICATE, bCertificate[cbSignedDataMsg+4]) & (~3);
#endif
    cert = calloc(1, size);
    if(!cert)
        return FALSE;

    cert->dwLength = size;
    cert->wRevision = WIN_CERT_REVISION_2_0;
    cert->wCertificateType = WIN_CERT_TYPE_PKCS_SIGNED_DATA;
    memcpy(cert->bCertificate, pbSignedDataMsg, cbSignedDataMsg);
    ret = ImageAddCertificate(file, cert, pdwIndex);

    free(cert);
    if(file != pSubjectInfo->hFile)
        CloseHandle(file);
    return ret;
}

/* structure offsets */
#define cfhead_Signature         (0x00)
#define cfhead_CabinetSize       (0x08)
#define cfhead_MinorVersion      (0x18)
#define cfhead_MajorVersion      (0x19)
#define cfhead_Flags             (0x1E)
#define cfhead_SIZEOF            (0x24)
#define cfheadext_HeaderReserved (0x00)
#define cfheadext_SIZEOF         (0x04)
#define cfsigninfo_CertOffset    (0x04)
#define cfsigninfo_CertSize      (0x08)
#define cfsigninfo_SIZEOF        (0x0C)

/* flags */
#define cfheadRESERVE_PRESENT          (0x0004)

/* endian-neutral reading of little-endian data */
#define EndGetI32(a)  ((((a)[3])<<24)|(((a)[2])<<16)|(((a)[1])<<8)|((a)[0]))
#define EndGetI16(a)  ((((a)[1])<<8)|((a)[0]))

/* For documentation purposes only:  this is the structure in the reserved
 * area of a signed cabinet file.  The cert offset indicates where in the
 * cabinet file the signature resides, and the count indicates its size.
 */
typedef struct _CAB_SIGNINFO
{
    WORD unk0; /* always 0? */
    WORD unk1; /* always 0x0010? */
    DWORD dwCertOffset;
    DWORD cbCertBlock;
} CAB_SIGNINFO, *PCAB_SIGNINFO;

static BOOL WINTRUST_GetSignedMsgFromCabFile(SIP_SUBJECTINFO *pSubjectInfo,
 DWORD *pdwEncodingType, DWORD dwIndex, DWORD *pcbSignedDataMsg,
 BYTE *pbSignedDataMsg)
{
    int header_resv;
    LONG base_offset, cabsize;
    USHORT flags;
    BYTE buf[64];
    DWORD cert_offset, cert_size, dwRead;

    TRACE("(%p %p %ld %p %p)\n", pSubjectInfo, pdwEncodingType, dwIndex,
          pcbSignedDataMsg, pbSignedDataMsg);

    /* get basic offset & size info */
    base_offset = SetFilePointer(pSubjectInfo->hFile, 0L, NULL, FILE_CURRENT);

    if (SetFilePointer(pSubjectInfo->hFile, 0, NULL, FILE_END) == INVALID_SET_FILE_POINTER)
    {
        TRACE("seek error\n");
        return FALSE;
    }

    cabsize = SetFilePointer(pSubjectInfo->hFile, 0L, NULL, FILE_CURRENT);
    if ((cabsize == -1) || (base_offset == -1) ||
     (SetFilePointer(pSubjectInfo->hFile, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER))
    {
        TRACE("seek error\n");
        return FALSE;
    }

    /* read in the CFHEADER */
    if (!ReadFile(pSubjectInfo->hFile, buf, cfhead_SIZEOF, &dwRead, NULL) ||
     dwRead != cfhead_SIZEOF)
    {
        TRACE("reading header failed\n");
        return FALSE;
    }

    /* check basic MSCF signature */
    if (EndGetI32(buf+cfhead_Signature) != 0x4643534d)
    {
        WARN("cabinet signature not present\n");
        return FALSE;
    }

    /* Ignore the number of folders and files and the set and cabinet IDs */

    /* check the header revision */
    if ((buf[cfhead_MajorVersion] > 1) ||
        (buf[cfhead_MajorVersion] == 1 && buf[cfhead_MinorVersion] > 3))
    {
        WARN("cabinet format version > 1.3\n");
        return FALSE;
    }

    /* pull the flags out */
    flags = EndGetI16(buf+cfhead_Flags);

    if (!(flags & cfheadRESERVE_PRESENT))
    {
        TRACE("no header present, not signed\n");
        return FALSE;
    }

    if (!ReadFile(pSubjectInfo->hFile, buf, cfheadext_SIZEOF, &dwRead, NULL) ||
     dwRead != cfheadext_SIZEOF)
    {
        ERR("bunk reserve-sizes?\n");
        return FALSE;
    }

    header_resv = EndGetI16(buf+cfheadext_HeaderReserved);
    if (!header_resv)
    {
        TRACE("no header_resv, not signed\n");
        return FALSE;
    }
    else if (header_resv < cfsigninfo_SIZEOF)
    {
        TRACE("header_resv too small, not signed\n");
        return FALSE;
    }

    if (header_resv > 60000)
    {
        WARN("WARNING; header reserved space > 60000\n");
    }

    if (!ReadFile(pSubjectInfo->hFile, buf, cfsigninfo_SIZEOF, &dwRead, NULL) ||
     dwRead != cfsigninfo_SIZEOF)
    {
        ERR("couldn't read reserve\n");
        return FALSE;
    }

    cert_offset = EndGetI32(buf+cfsigninfo_CertOffset);
    TRACE("cert_offset: %ld\n", cert_offset);
    cert_size = EndGetI32(buf+cfsigninfo_CertSize);
    TRACE("cert_size: %ld\n", cert_size);

    /* The redundant checks are to avoid wraparound */
    if (cert_offset > cabsize || cert_size > cabsize ||
     cert_offset + cert_size > cabsize)
    {
        WARN("offset beyond file, not attempting to read\n");
        return FALSE;
    }

    SetFilePointer(pSubjectInfo->hFile, base_offset, NULL, FILE_BEGIN);
    if (!pbSignedDataMsg)
    {
        *pcbSignedDataMsg = cert_size;
        return TRUE;
    }
    if (*pcbSignedDataMsg < cert_size)
    {
        *pcbSignedDataMsg = cert_size;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (SetFilePointer(pSubjectInfo->hFile, cert_offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
    {
        ERR("couldn't seek to cert location\n");
        return FALSE;
    }
    if (!ReadFile(pSubjectInfo->hFile, pbSignedDataMsg, cert_size, &dwRead,
     NULL) || dwRead != cert_size)
    {
        ERR("couldn't read cert\n");
        SetFilePointer(pSubjectInfo->hFile, base_offset, NULL, FILE_BEGIN);
        return FALSE;
    }
    /* The encoding of the files I've seen appears to be in ASN.1
     * format, and there isn't a field indicating the type, so assume it
     * always is.
     */
    *pdwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    /* Restore base offset */
    SetFilePointer(pSubjectInfo->hFile, base_offset, NULL, FILE_BEGIN);
    return TRUE;
}

static BOOL WINTRUST_GetSignedMsgFromCatFile(SIP_SUBJECTINFO *pSubjectInfo,
 DWORD *pdwEncodingType, DWORD dwIndex, DWORD *pcbSignedDataMsg,
 BYTE *pbSignedDataMsg)
{
    BOOL ret;

    TRACE("(%p %p %ld %p %p)\n", pSubjectInfo, pdwEncodingType, dwIndex,
          pcbSignedDataMsg, pbSignedDataMsg);

    if (!pbSignedDataMsg)
    {
        *pcbSignedDataMsg = GetFileSize(pSubjectInfo->hFile, NULL);
         ret = TRUE;
    }
    else
    {
        DWORD len = GetFileSize(pSubjectInfo->hFile, NULL);

        if (*pcbSignedDataMsg < len)
        {
            *pcbSignedDataMsg = len;
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            ret = FALSE;
        }
        else
        {
            ret = ReadFile(pSubjectInfo->hFile, pbSignedDataMsg, len,
             pcbSignedDataMsg, NULL);
            if (ret)
                *pdwEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
        }
    }
    return ret;
}

#ifdef __REACTOS__
static BOOL WINTRUST_PutSignedMsgToCabFile(SIP_SUBJECTINFO *pSubjectInfo,
 DWORD *pdwIndex, DWORD cbSignedDataMsg, BYTE *pbSignedDataMsg)
{
    struct cab_layout cab;
    DWORD size, read, written, out_len;
    BYTE *data = NULL, *out = NULL;
    HANDLE file;
    BOOL ret = FALSE;

    if (pSubjectInfo->hFile && pSubjectInfo->hFile != INVALID_HANDLE_VALUE)
        file = pSubjectInfo->hFile;
    else
    {
        file = CreateFileW(pSubjectInfo->pwsFileName, GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (file == INVALID_HANDLE_VALUE)
            return FALSE;
    }

    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE) goto done;
    if (!(data = malloc(size)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        goto done;
    }
    if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
        !ReadFile(file, data, size, &read, NULL) || read != size)
        goto done;
    if (!sip_cab_layout(data, size, &cab)) goto done;

    out_len = cab.cabinet_size + cab.shift + cbSignedDataMsg;
    if (!(out = calloc(1, out_len)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        goto done;
    }
    sip_cab_reserve_layout(data, &cab, cbSignedDataMsg, out);
    memcpy(out + cab.cabinet_size + cab.shift, pbSignedDataMsg, cbSignedDataMsg);

    ret = SetFilePointer(file, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
          WriteFile(file, out, out_len, &written, NULL) && written == out_len && SetEndOfFile(file);
    if (ret && pdwIndex)
        *pdwIndex = 0;

done:
    free(out);
    free(data);
    if (file != pSubjectInfo->hFile)
        CloseHandle(file);
    return ret;
}

static BOOL WINTRUST_RemoveSignedMsgFromPEFile(SIP_SUBJECTINFO *pSubjectInfo, DWORD dwIndex)
{
    HANDLE file;
    BOOL ret;

    if (pSubjectInfo->hFile && pSubjectInfo->hFile != INVALID_HANDLE_VALUE)
        file = pSubjectInfo->hFile;
    else
    {
        file = CreateFileW(pSubjectInfo->pwsFileName, GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (file == INVALID_HANDLE_VALUE)
            return FALSE;
    }
    if (!(ret = ImageRemoveCertificate(file, dwIndex)))
        SetLastError(ERROR_INVALID_PARAMETER);
    if (file != pSubjectInfo->hFile)
        CloseHandle(file);
    return ret;
}

static BOOL WINTRUST_PutSignedMsgToCatFile(SIP_SUBJECTINFO *pSubjectInfo,
 DWORD *pdwIndex, DWORD cbSignedDataMsg, BYTE *pbSignedDataMsg)
{
    HANDLE file;
    DWORD written;
    BOOL ret;

    if (pSubjectInfo->hFile && pSubjectInfo->hFile != INVALID_HANDLE_VALUE)
        file = pSubjectInfo->hFile;
    else
    {
        file = CreateFileW(pSubjectInfo->pwsFileName, GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (file == INVALID_HANDLE_VALUE)
            return FALSE;
    }

    ret = SetFilePointer(file, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
          WriteFile(file, pbSignedDataMsg, cbSignedDataMsg, &written, NULL) &&
          written == cbSignedDataMsg && SetEndOfFile(file);
    if (ret && pdwIndex)
        *pdwIndex = 0;

    if (file != pSubjectInfo->hFile)
        CloseHandle(file);
    return ret;
}

#endif
/* GUIDs used by CryptSIPGetSignedDataMsg and CryptSIPPutSignedDataMsg */
static const GUID unknown = { 0xC689AAB8, 0x8E78, 0x11D0, { 0x8C,0x47,
    0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
static const GUID cabGUID = { 0xC689AABA, 0x8E78, 0x11D0, { 0x8C,0x47,
    0x00,0xC0,0x4F,0xC2,0x95,0xEE } };
static const GUID catGUID = { 0xDE351A43, 0x8E59, 0x11D0, { 0x8C,0x47,
     0x00,0xC0,0x4F,0xC2,0x95,0xEE }};
#ifdef __REACTOS__
static const GUID flatGUID = { 0xDE351A42, 0x8E59, 0x11D0, { 0x8C,0x47,
     0x00,0xC0,0x4F,0xC2,0x95,0xEE }};
static const GUID storageGUID = { 0x9BA61D3F, 0xE73A, 0x11D0, { 0x8C,0xD2,
     0x00,0xC0,0x4F,0xC2,0x95,0xEE }};
#endif

#ifdef __REACTOS__
typedef struct _SIP_INFORMATION
{
    DWORD cbSize;
    DWORD cgSubjects;
    const GUID *pgSubjects;
} SIP_INFORMATION;

/***********************************************************************
 *      CryptSIPGetInfo  (WINTRUST.@)
 */
BOOL WINAPI CryptSIPGetInfo(SIP_INFORMATION *pSipInfo)
{
    static const GUID subjects[] =
    {
        { 0xC689AAB8, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
        { 0xC689AABA, 0x8E78, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
        { 0xDE351A42, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
        { 0xDE351A43, 0x8E59, 0x11D0, { 0x8C,0x47,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
        { 0x9BA61D3F, 0xE73A, 0x11D0, { 0x8C,0xD2,0x00,0xC0,0x4F,0xC2,0x95,0xEE } },
        { 0x18B3C141, 0xAE0D, 0x40F9, { 0x94,0x65,0xE5,0x42,0xAF,0xC1,0xAB,0xC7 } },
    };

    TRACE("(%p)\n", pSipInfo);

    if (!pSipInfo)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    pSipInfo->cbSize = sizeof(*pSipInfo);
    pSipInfo->cgSubjects = ARRAY_SIZE(subjects);
    pSipInfo->pgSubjects = subjects;
    return TRUE;
}

#endif
#ifdef __REACTOS__
/***********************************************************************
 *      CryptSIPGetRegWorkingFlags  (WINTRUST.@)
 */
VOID WINAPI CryptSIPGetRegWorkingFlags(DWORD *pdwState)
{
    TRACE("(%p)\n", pdwState);

    WintrustGetRegPolicyFlags(pdwState);
}

#endif
#ifdef __REACTOS__
/***********************************************************************
 *      CryptSIPGetCaps  (WINTRUST.@)
 */
BOOL WINAPI CryptSIPGetCaps(SIP_SUBJECTINFO *pSubjInfo, SIP_CAP_SET *pCaps)
{
    TRACE("(%p %p)\n", pSubjInfo, pCaps);

    if (!pSubjInfo || !WVT_ISINSTRUCT(SIP_SUBJECTINFO, pSubjInfo->cbSize, pgSubjectType) ||
        !pSubjInfo->pgSubjectType || !pCaps || pCaps->cbSize < sizeof(SIP_CAP_SET) || pCaps->dwFlags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!IsEqualGUID(pSubjInfo->pgSubjectType, &unknown) && !IsEqualGUID(pSubjInfo->pgSubjectType, &cabGUID) &&
        !IsEqualGUID(pSubjInfo->pgSubjectType, &flatGUID) && !IsEqualGUID(pSubjInfo->pgSubjectType, &catGUID) &&
        !IsEqualGUID(pSubjInfo->pgSubjectType, &storageGUID))
    {
        SetLastError(TRUST_E_SUBJECT_FORM_UNKNOWN);
        return FALSE;
    }
    pCaps->cbSize = sizeof(SIP_CAP_SET);
    pCaps->dwVersion = SIP_CAP_SET_CUR_VER;
    pCaps->isMultiSign = TRUE;
    pCaps->dwFlags = IsEqualGUID(pSubjInfo->pgSubjectType, &unknown) ? SIP_CAP_FLAG_SEALING : 0;
    return TRUE;
}

#endif
/***********************************************************************
 *      CryptSIPGetSignedDataMsg  (WINTRUST.@)
 */
BOOL WINAPI CryptSIPGetSignedDataMsg(SIP_SUBJECTINFO* pSubjectInfo, DWORD* pdwEncodingType,
                                       DWORD dwIndex, DWORD* pcbSignedDataMsg, BYTE* pbSignedDataMsg)
{
    BOOL ret;

    TRACE("(%p %p %ld %p %p)\n", pSubjectInfo, pdwEncodingType, dwIndex,
          pcbSignedDataMsg, pbSignedDataMsg);

    if(!pSubjectInfo)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!memcmp(pSubjectInfo->pgSubjectType, &unknown, sizeof(unknown)))
        ret = WINTRUST_GetSignedMsgFromPEFile(pSubjectInfo, pdwEncodingType,
         dwIndex, pcbSignedDataMsg, pbSignedDataMsg);
    else if (!memcmp(pSubjectInfo->pgSubjectType, &cabGUID, sizeof(cabGUID)))
        ret = WINTRUST_GetSignedMsgFromCabFile(pSubjectInfo, pdwEncodingType,
         dwIndex, pcbSignedDataMsg, pbSignedDataMsg);
    else if (!memcmp(pSubjectInfo->pgSubjectType, &catGUID, sizeof(catGUID)))
        ret = WINTRUST_GetSignedMsgFromCatFile(pSubjectInfo, pdwEncodingType,
         dwIndex, pcbSignedDataMsg, pbSignedDataMsg);
#ifdef __REACTOS__
    else if (IsEqualGUID(pSubjectInfo->pgSubjectType, &sip_18b3c141_guid))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        ret = FALSE;
    }
#endif
    else
    {
        FIXME("unimplemented for subject type %s\n",
         debugstr_guid(pSubjectInfo->pgSubjectType));
        ret = FALSE;
    }

    TRACE("returning %d\n", ret);
    return ret;
}

/***********************************************************************
 *      CryptSIPPutSignedDataMsg  (WINTRUST.@)
 */
BOOL WINAPI CryptSIPPutSignedDataMsg(SIP_SUBJECTINFO* pSubjectInfo, DWORD pdwEncodingType,
        DWORD* pdwIndex, DWORD cbSignedDataMsg, BYTE* pbSignedDataMsg)
{
    TRACE("(%p %ld %p %ld %p)\n", pSubjectInfo, pdwEncodingType, pdwIndex,
          cbSignedDataMsg, pbSignedDataMsg);

    if(!pSubjectInfo) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if(!memcmp(pSubjectInfo->pgSubjectType, &unknown, sizeof(unknown)))
        return WINTRUST_PutSignedMsgToPEFile(pSubjectInfo, pdwEncodingType,
                pdwIndex, cbSignedDataMsg, pbSignedDataMsg);
#ifdef __REACTOS__
    else if (!memcmp(pSubjectInfo->pgSubjectType, &catGUID, sizeof(catGUID)))
        return WINTRUST_PutSignedMsgToCatFile(pSubjectInfo, pdwIndex,
                cbSignedDataMsg, pbSignedDataMsg);
    else if (!memcmp(pSubjectInfo->pgSubjectType, &cabGUID, sizeof(cabGUID)))
        return WINTRUST_PutSignedMsgToCabFile(pSubjectInfo, pdwIndex,
                cbSignedDataMsg, pbSignedDataMsg);
    else if (IsEqualGUID(pSubjectInfo->pgSubjectType, &sip_18b3c141_guid))
        return FALSE;
#endif
    else
        FIXME("unimplemented for subject type %s\n",
                debugstr_guid(pSubjectInfo->pgSubjectType));

    return FALSE;
}

/***********************************************************************
 *      CryptSIPRemoveSignedDataMsg  (WINTRUST.@)
 */
BOOL WINAPI CryptSIPRemoveSignedDataMsg(SIP_SUBJECTINFO* pSubjectInfo,
                                       DWORD dwIndex)
{
#ifdef __REACTOS__
    TRACE("(%p %ld)\n", pSubjectInfo, dwIndex);

    if (!pSubjectInfo || !pSubjectInfo->pgSubjectType)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!memcmp(pSubjectInfo->pgSubjectType, &unknown, sizeof(unknown)))
        return WINTRUST_RemoveSignedMsgFromPEFile(pSubjectInfo, dwIndex);
    if (!memcmp(pSubjectInfo->pgSubjectType, &cabGUID, sizeof(cabGUID)))
        SetLastError(ERROR_SUCCESS);
    else if (memcmp(pSubjectInfo->pgSubjectType, &catGUID, sizeof(catGUID)) &&
             !IsEqualGUID(pSubjectInfo->pgSubjectType, &sip_18b3c141_guid))
        FIXME("unimplemented for subject type %s\n", debugstr_guid(pSubjectInfo->pgSubjectType));
    return FALSE;
#else
    FIXME("(%p %ld) stub\n", pSubjectInfo, dwIndex);
 
    return FALSE;
#endif
}

/***********************************************************************
 *      CryptSIPVerifyIndirectData  (WINTRUST.@)
 */
BOOL WINAPI CryptSIPVerifyIndirectData(SIP_SUBJECTINFO* pSubjectInfo,
                                       SIP_INDIRECT_DATA* pIndirectData)
{
#ifdef __REACTOS__
    SIP_INDIRECT_DATA *computed;
    SIP_SUBJECTINFO info;
    DWORD size = 0;
    BOOL ret;

    TRACE("(%p %p)\n", pSubjectInfo, pIndirectData);

    if (!pSubjectInfo || !pSubjectInfo->pgSubjectType || !pIndirectData || !pIndirectData->DigestAlgorithm.pszObjId)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (IsEqualGUID(pSubjectInfo->pgSubjectType, &sip_18b3c141_guid))
        return TRUE;
    if (IsEqualGUID(pSubjectInfo->pgSubjectType, &flatGUID))
    {
        /* Windows 11 never verifies a flat image here: catalog members are compared by hash */
        SetLastError(pSubjectInfo->dwUnionChoice == MSSIP_ADDINFO_NONE ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS);
        return FALSE;
    }

    info = *pSubjectInfo;
    info.DigestAlgorithm = pIndirectData->DigestAlgorithm;
    info.dwFlags = (info.dwFlags & ~SPC_INC_PE_PAGE_HASHES_FLAG) | SPC_EXC_PE_PAGE_HASHES_FLAG;
    if (!sip_create_indirect_data(&info, &size, NULL, FALSE)) return FALSE;
    if (!(computed = malloc(size)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    ret = sip_create_indirect_data(&info, &size, computed, FALSE);
    if (ret && (computed->Digest.cbData != pIndirectData->Digest.cbData ||
                memcmp(computed->Digest.pbData, pIndirectData->Digest.pbData, computed->Digest.cbData)))
    {
        SetLastError(TRUST_E_BAD_DIGEST);
        ret = FALSE;
    }
    free(computed);
    return ret;
#else
    FIXME("(%p %p) stub\n", pSubjectInfo, pIndirectData);
 
    return FALSE;
#endif
}
