/*
 * PROJECT:     LiberNT Trust Verification API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Build catalog files from catalog definition files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "wincrypt.h"
#include "wintrust.h"
#include "mscat.h"
#include "mssip.h"
#include "winternl.h"
#include "wintrust_priv.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wintrust);

#define CDF_MAGIC 0x31464443
#define CDF_CERT_VERSION 0x200

struct cdf_line
{
    WCHAR *text;
    WCHAR *key;
    WCHAR *value;
};

struct cdf_attr
{
    struct cdf_line *line;
    CRYPTCATATTRIBUTE *attr;
    BOOL parsed;
};

struct cdf_member
{
    struct cdf_line *line;
    struct cdf_line *alt_sip;
    struct cdf_attr *attrs;
    DWORD attr_count;
    CRYPTCATMEMBER *member;
    BOOL owned;
    BOOL processed;
};

struct cdf
{
    CRYPTCATCDF public;
    DWORD magic;
    struct cdf_line *lines;
    DWORD line_count;
    struct cdf_attr *cat_attrs;
    DWORD cat_attr_count;
    struct cdf_member *members;
    DWORD member_count;
    DWORD version;
    BOOL literal_v2_members;
    DWORD sip_flags;
    const char *hash_oid;
};

static WCHAR *cdf_strdup_range(const WCHAR *start, const WCHAR *end)
{
    WCHAR *copy;

    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
    if (!(copy = malloc((end - start + 1) * sizeof(WCHAR)))) return NULL;
    memcpy(copy, start, (end - start) * sizeof(WCHAR));
    copy[end - start] = 0;
    return copy;
}

static WCHAR *cdf_read_text(HANDLE file)
{
    DWORD size, read;
    BYTE *bytes;
    WCHAR *text;
    INT count;

    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE) return NULL;
    if (!(bytes = malloc(size + 1)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    if (size && (!ReadFile(file, bytes, size, &read, NULL) || read != size))
    {
        free(bytes);
        return NULL;
    }

    if (size >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
    {
        count = (size - 2) / sizeof(WCHAR);
        if ((text = malloc((count + 1) * sizeof(WCHAR))))
        {
            memcpy(text, bytes + 2, count * sizeof(WCHAR));
            text[count] = 0;
        }
    }
    else
    {
        count = size ? MultiByteToWideChar(CP_ACP, 0, (char *)bytes, size, NULL, 0) : 0;
        if ((text = malloc((count + 1) * sizeof(WCHAR))))
        {
            if (count) MultiByteToWideChar(CP_ACP, 0, (char *)bytes, size, text, count);
            text[count] = 0;
        }
    }
    free(bytes);
    if (!text) SetLastError(ERROR_OUTOFMEMORY);
    return text;
}

static BOOL cdf_add_line(struct cdf *cdf, const WCHAR *start, const WCHAR *end)
{
    struct cdf_line *lines, *line;
    const WCHAR *equal;

    if (!(lines = realloc(cdf->lines, (cdf->line_count + 1) * sizeof(*lines))))
        return FALSE;
    cdf->lines = lines;
    line = &cdf->lines[cdf->line_count];
    memset(line, 0, sizeof(*line));
    if (!(line->text = cdf_strdup_range(start, end))) return FALSE;
    cdf->line_count++;

    for (equal = start; equal < end && *equal != '='; equal++)
        ;
    if (equal == end) return TRUE;
    return (line->key = cdf_strdup_range(start, equal)) && (line->value = cdf_strdup_range(equal + 1, end));
}

static BOOL cdf_split_lines(struct cdf *cdf, const WCHAR *text)
{
    const WCHAR *start = text, *p;

    for (p = text;; p++)
    {
        if (*p && *p != '\r' && *p != '\n') continue;
        if (p > start && !cdf_add_line(cdf, start, p))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        if (!*p) return TRUE;
        start = p + 1;
    }
}

static BOOL cdf_is_section(const struct cdf_line *line, const WCHAR *name)
{
    size_t len = wcslen(line->text);

    return len > 2 && line->text[0] == '[' && line->text[len - 1] == ']' &&
           len - 2 == wcslen(name) && !_wcsnicmp(line->text + 1, name, len - 2);
}

static BOOL cdf_is_any_section(const struct cdf_line *line)
{
    size_t len = wcslen(line->text);

    return len >= 2 && line->text[0] == '[' && line->text[len - 1] == ']';
}

static BOOL cdf_has_key_prefix(const WCHAR *key, const WCHAR *prefix)
{
    return !_wcsnicmp(key, prefix, wcslen(prefix));
}

static BOOL cdf_is_digits(const WCHAR *text)
{
    if (!*text) return FALSE;
    for (; *text; text++)
        if (*text < '0' || *text > '9') return FALSE;
    return TRUE;
}

static struct cdf_member *cdf_owner_member(struct cdf *cdf, const WCHAR *key, BOOL *alt_sip)
{
    struct cdf_member *owner = NULL;
    size_t best = 0, len;
    const WCHAR *rest;
    DWORD i;

    for (i = 0; i < cdf->member_count; i++)
    {
        len = wcslen(cdf->members[i].line->key);
        if (len <= best || _wcsnicmp(key, cdf->members[i].line->key, len)) continue;
        rest = key + len;
        if (!_wcsicmp(rest, L"ALTSIPID"))
        {
            *alt_sip = TRUE;
            owner = &cdf->members[i];
            best = len;
        }
        else if (!_wcsnicmp(rest, L"ATTR", 4) && cdf_is_digits(rest + 4))
        {
            *alt_sip = FALSE;
            owner = &cdf->members[i];
            best = len;
        }
    }
    return owner;
}

static BOOL cdf_append_attr(struct cdf_attr **attrs, DWORD *count, struct cdf_line *line)
{
    struct cdf_attr *array;
    DWORD i;

    for (i = 0; i < *count; i++)
        if (!_wcsicmp((*attrs)[i].line->key, line->key)) return TRUE;

    if (!(array = realloc(*attrs, (*count + 1) * sizeof(*array))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    *attrs = array;
    memset(&array[*count], 0, sizeof(*array));
    array[(*count)++].line = line;
    return TRUE;
}

static BOOL cdf_add_file_line(struct cdf *cdf, struct cdf_line *line)
{
    struct cdf_member *owner, *members;
    BOOL alt_sip = FALSE;
    DWORD i;

    if ((owner = cdf_owner_member(cdf, line->key, &alt_sip)))
    {
        if (alt_sip)
        {
            if (!owner->alt_sip) owner->alt_sip = line;
            return TRUE;
        }
        return cdf_append_attr(&owner->attrs, &owner->attr_count, line);
    }

    for (i = 0; i < cdf->member_count; i++)
        if (!_wcsicmp(cdf->members[i].line->key, line->key)) return TRUE;

    if (!(members = realloc(cdf->members, (cdf->member_count + 1) * sizeof(*members))))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    cdf->members = members;
    memset(&members[cdf->member_count], 0, sizeof(*members));
    members[cdf->member_count++].line = line;
    return TRUE;
}

static const WCHAR *cdf_header_value(struct cdf *cdf, const WCHAR *key)
{
    BOOL header = FALSE;
    DWORD i;

    for (i = 0; i < cdf->line_count; i++)
    {
        if (cdf_is_any_section(&cdf->lines[i]))
        {
            header = cdf_is_section(&cdf->lines[i], L"CatalogHeader");
            continue;
        }
        if (header && cdf->lines[i].key && !_wcsicmp(cdf->lines[i].key, key)) return cdf->lines[i].value;
    }
    return NULL;
}

static BOOL cdf_collect(struct cdf *cdf, BOOL *has_header)
{
    enum { SECTION_NONE, SECTION_HEADER, SECTION_FILES } section = SECTION_NONE;
    struct cdf_line *line;
    DWORD i;

    *has_header = FALSE;
    for (i = 0; i < cdf->line_count; i++)
    {
        line = &cdf->lines[i];
        if (cdf_is_any_section(line))
        {
            if (cdf_is_section(line, L"CatalogHeader"))
            {
                section = SECTION_HEADER;
                *has_header = TRUE;
            }
            else if (cdf_is_section(line, L"CatalogFiles"))
                section = SECTION_FILES;
            else
                section = SECTION_NONE;
            continue;
        }
        if (!line->key || !line->key[0]) continue;

        if (section == SECTION_HEADER && cdf_has_key_prefix(line->key, L"CATATTR"))
        {
            if (!cdf_append_attr(&cdf->cat_attrs, &cdf->cat_attr_count, line)) return FALSE;
        }
        else if (section == SECTION_FILES)
        {
            if (!cdf_add_file_line(cdf, line)) return FALSE;
        }
    }
    return TRUE;
}

static void cdf_free(struct cdf *cdf)
{
    DWORD i, j;

    for (i = 0; i < cdf->member_count; i++)
    {
        if (cdf->members[i].owned)
        {
            for (j = 0; j < cdf->members[i].attr_count; j++)
                free(cdf->members[i].attrs[j].attr);
            free(cdf->members[i].member);
        }
        free(cdf->members[i].attrs);
    }
    free(cdf->members);
    free(cdf->cat_attrs);
    for (i = 0; i < cdf->line_count; i++)
    {
        free(cdf->lines[i].text);
        free(cdf->lines[i].key);
        free(cdf->lines[i].value);
    }
    free(cdf->lines);
    free(cdf->public.pwszResultDir);
    if (cdf->public.hFile && cdf->public.hFile != INVALID_HANDLE_VALUE)
        CloseHandle(cdf->public.hFile);
    cdf->magic = 0;
    free(cdf);
}

static struct cdf *cdf_from_public(CRYPTCATCDF *pCDF)
{
    struct cdf *cdf = (struct cdf *)pCDF;

    if (!pCDF || pCDF->cbStruct != sizeof(*pCDF) || cdf->magic != CDF_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return cdf;
}

static BOOL cdf_catalog_path(struct cdf *cdf, const WCHAR *name, WCHAR *path)
{
    const WCHAR *result_dir = cdf_header_value(cdf, L"ResultDir");
    WCHAR combined[MAX_PATH];
    size_t dir_len = result_dir ? wcslen(result_dir) : 0;
    DWORD len;

    if (dir_len)
    {
        if (dir_len + wcslen(name) + 2 > ARRAY_SIZE(combined)) return FALSE;
        wcscpy(combined, result_dir);
        if (result_dir[dir_len - 1] != '\\' && result_dir[dir_len - 1] != '/') wcscat(combined, L"\\");
        wcscat(combined, name);
        if (!(cdf->public.pwszResultDir = cdf_strdup_range(result_dir, result_dir + dir_len))) return FALSE;
    }
    else
    {
        if (wcslen(name) + 1 > ARRAY_SIZE(combined)) return FALSE;
        wcscpy(combined, name);
    }

    len = GetFullPathNameW(combined, MAX_PATH, path, NULL);
    return len && len < MAX_PATH;
}

static DWORD cdf_header_number(struct cdf *cdf, const WCHAR *key, DWORD default_value)
{
    const WCHAR *value = cdf_header_value(cdf, key);

    if (!value || !value[0]) return default_value;
    return wcstoul(value, NULL, 0);
}

static void cdf_report(PFN_CDF_PARSE_ERROR_CALLBACK callback, DWORD area, DWORD error, WCHAR *line)
{
    TRACE("area %#lx, error %#lx, line %s\n", area, error, debugstr_w(line));
    if (callback) callback(area, error, line);
}

/***********************************************************************
 *      CryptCATCDFOpen  (WINTRUST.@)
 */
CRYPTCATCDF * WINAPI CryptCATCDFOpen(LPWSTR pwszFilePath, PFN_CDF_PARSE_ERROR_CALLBACK pfnParseError)
{
    WCHAR catalog_path[MAX_PATH];
    const WCHAR *name, *hash;
    DWORD flags = CRYPTCAT_OPEN_CREATENEW, encoding;
    struct cdf *cdf;
    HANDLE file;
    WCHAR *text;
    BOOL has_header;

    TRACE("(%s %p)\n", debugstr_w(pwszFilePath), pfnParseError);

    if (!pwszFilePath)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    file = CreateFileW(pwszFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;

    if (!(cdf = calloc(1, sizeof(*cdf))))
    {
        CloseHandle(file);
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    cdf->public.cbStruct = sizeof(cdf->public);
    cdf->public.hFile = file;
    cdf->public.hCATStore = INVALID_HANDLE_VALUE;
    cdf->magic = CDF_MAGIC;

    if (!(text = cdf_read_text(file)))
    {
        cdf_free(cdf);
        return NULL;
    }
    if (!cdf_split_lines(cdf, text) || !cdf_collect(cdf, &has_header))
    {
        free(text);
        cdf_free(cdf);
        return NULL;
    }
    free(text);

    if (!has_header)
    {
        cdf_report(pfnParseError, CRYPTCAT_E_AREA_HEADER, CRYPTCAT_E_CDF_TAGNOTFOUND, (WCHAR *)L"[CatalogHeader]");
        cdf_free(cdf);
        SetLastError(ERROR_SUCCESS);
        return NULL;
    }

    name = cdf_header_value(cdf, L"Name");
    if (!name || !name[0])
    {
        cdf_free(cdf);
        SetLastError(ERROR_SHARING_VIOLATION);
        return NULL;
    }
    if (!cdf_catalog_path(cdf, name, catalog_path))
    {
        cdf_free(cdf);
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }

    cdf->version = cdf_header_number(cdf, L"CatalogVersion", 1) == 2 ? CRYPTCAT_VERSION_2 : CRYPTCAT_VERSION_1;
    hash = cdf_header_value(cdf, L"HashAlgorithms");
    if (cdf->version == CRYPTCAT_VERSION_2 && (!hash || !hash[0]))
    {
        cdf_free(cdf);
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (hash && hash[0])
        cdf->hash_oid = !_wcsicmp(hash, L"SHA256") ? szOID_NIST_sha256 : szOID_OIWSEC_sha1;
    else
        cdf->hash_oid = cdf->version == CRYPTCAT_VERSION_2 ? szOID_NIST_sha256 : szOID_OIWSEC_sha1;
    encoding = cdf_header_number(cdf, L"EncodingType", X509_ASN_ENCODING | PKCS_7_ASN_ENCODING);
    if (cdf_header_value(cdf, L"PageHashes"))
    {
        BOOL include = !_wcsicmp(cdf_header_value(cdf, L"PageHashes"), L"true");

        flags |= include ? CRYPTCAT_OPEN_INCLUDE_PAGE_HASHES : CRYPTCAT_OPEN_EXCLUDE_PAGE_HASHES;
        cdf->sip_flags = include ? SPC_INC_PE_PAGE_HASHES_FLAG : SPC_EXC_PE_PAGE_HASHES_FLAG;
    }
    /* TODO: apply the PublicVersion header value to the catalog */

    cdf->public.hCATStore = CryptCATOpen(catalog_path, flags, 0, cdf->version, encoding);
    if (cdf->public.hCATStore == INVALID_HANDLE_VALUE)
    {
        cdf_free(cdf);
        return NULL;
    }

    SetLastError(ERROR_SUCCESS);
    return &cdf->public;
}

/***********************************************************************
 *      CryptCATCDFClose  (WINTRUST.@)
 */
BOOL WINAPI CryptCATCDFClose(CRYPTCATCDF *pCDF)
{
    struct cdf *cdf;
    DWORD error;
    BOOL ret;

    TRACE("(%p)\n", pCDF);

    if (!(cdf = cdf_from_public(pCDF))) return FALSE;

    if (cdf->literal_v2_members)
    {
        CryptCATClose(cdf->public.hCATStore);
        cdf_free(cdf);
        SetLastError(ERROR_SUCCESS);
        return FALSE;
    }

    ret = CryptCATPersistStore(cdf->public.hCATStore);
    error = GetLastError();
    CryptCATClose(cdf->public.hCATStore);
    cdf_free(cdf);
    SetLastError(ret ? ERROR_SUCCESS : error);
    return ret;
}

static BOOL cdf_attr_data(DWORD flags, const WCHAR *text, BYTE **data, DWORD *size)
{
    if (flags & CRYPTCAT_ATTR_DATAASCII)
    {
        *size = (wcslen(text) + 1) * sizeof(WCHAR);
        if (!(*data = malloc(*size))) return FALSE;
        memcpy(*data, text, *size);
        return TRUE;
    }

    *size = 0;
    if (!CryptStringToBinaryW(text, 0, CRYPT_STRING_BASE64, NULL, size, NULL, NULL)) return FALSE;
    if (!(*data = malloc(*size ? *size : 1))) return FALSE;
    return CryptStringToBinaryW(text, 0, CRYPT_STRING_BASE64, *data, size, NULL, NULL);
}

static BOOL cdf_is_literal_member(struct cdf *cdf, CRYPTCATMEMBER *member)
{
    DWORD i;

    for (i = 0; i < cdf->member_count; i++)
        if (cdf->members[i].member == member)
            return cdf->members[i].owned;
    return FALSE;
}

static CRYPTCATATTRIBUTE *cdf_put_attr(struct cdf *cdf, struct cdf_line *line, CRYPTCATMEMBER *member,
                                       PFN_CDF_PARSE_ERROR_CALLBACK callback)
{
    WCHAR *first, *second, *name;
    CRYPTCATATTRIBUTE *attr;
    DWORD flags, name_type, data_type, size;
    BYTE *data = NULL;

    first = wcschr(line->value, ':');
    second = first ? wcschr(first + 1, ':') : NULL;
    if (!second)
    {
        cdf_report(callback, CRYPTCAT_E_AREA_ATTRIBUTE, CRYPTCAT_E_CDF_ATTR_TOOFEWVALUES, line->text);
        return NULL;
    }

    flags = wcstoul(line->value, NULL, 0);
    name_type = flags & (CRYPTCAT_ATTR_NAMEASCII | CRYPTCAT_ATTR_NAMEOBJID);
    data_type = flags & (CRYPTCAT_ATTR_DATAASCII | CRYPTCAT_ATTR_DATABASE64);
    if ((name_type != CRYPTCAT_ATTR_NAMEASCII && name_type != CRYPTCAT_ATTR_NAMEOBJID) ||
        (data_type != CRYPTCAT_ATTR_DATAASCII && data_type != CRYPTCAT_ATTR_DATABASE64))
    {
        cdf_report(callback, CRYPTCAT_E_AREA_ATTRIBUTE, CRYPTCAT_E_CDF_ATTR_TYPECOMBO, line->text);
        return NULL;
    }

    if (!(name = cdf_strdup_range(first + 1, second)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    if (!cdf_attr_data(flags, second + 1, &data, &size))
    {
        free(data);
        data = NULL;
        size = 0;
    }

    if (member && cdf_is_literal_member(cdf, member))
        attr = catalog_alloc_attribute(name, flags, size, data);
    else if (member)
        attr = CryptCATPutAttrInfo(cdf->public.hCATStore, member, name, flags, size, data);
    else
        attr = CryptCATPutCatAttrInfo(cdf->public.hCATStore, name, flags, size, data);
    free(data);
    free(name);
    return attr;
}

static CRYPTCATATTRIBUTE *cdf_next_attr(struct cdf *cdf, struct cdf_attr *attrs, DWORD count, CRYPTCATMEMBER *member,
                                        CRYPTCATATTRIBUTE *prev, PFN_CDF_PARSE_ERROR_CALLBACK callback)
{
    DWORD i = 0;

    if (prev)
    {
        for (i = 0; i < count && attrs[i].attr != prev; i++)
            ;
        if (i == count)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
        i++;
    }

    for (; i < count; i++)
    {
        if (!attrs[i].parsed)
        {
            attrs[i].parsed = TRUE;
            attrs[i].attr = cdf_put_attr(cdf, attrs[i].line, member, callback);
        }
        if (attrs[i].attr) return attrs[i].attr;
    }
    return NULL;
}

/***********************************************************************
 *      CryptCATCDFEnumCatAttributes  (WINTRUST.@)
 */
CRYPTCATATTRIBUTE * WINAPI CryptCATCDFEnumCatAttributes(CRYPTCATCDF *pCDF, CRYPTCATATTRIBUTE *pPrevAttr,
                                                        PFN_CDF_PARSE_ERROR_CALLBACK pfnParseError)
{
    struct cdf *cdf;

    TRACE("(%p %p %p)\n", pCDF, pPrevAttr, pfnParseError);

    if (!(cdf = cdf_from_public(pCDF))) return NULL;
    return cdf_next_attr(cdf, cdf->cat_attrs, cdf->cat_attr_count, NULL, pPrevAttr, pfnParseError);
}

static BOOL cdf_hash_tag(const SIP_INDIRECT_DATA *indirect, WCHAR **tag)
{
    DWORD i;

    if (!(*tag = malloc((indirect->Digest.cbData * 2 + 1) * sizeof(WCHAR)))) return FALSE;
    for (i = 0; i < indirect->Digest.cbData; i++)
        swprintf(*tag + i * 2, 3, L"%02X", indirect->Digest.pbData[i]);
    (*tag)[indirect->Digest.cbData * 2] = 0;
    return TRUE;
}

static BOOL cdf_member_subject(struct cdf_member *m, const WCHAR *path, GUID *subject,
                               PFN_CDF_PARSE_ERROR_CALLBACK callback)
{
    UNICODE_STRING guid;

    if (m->alt_sip)
    {
        RtlInitUnicodeString(&guid, m->alt_sip->value);
        if (!RtlGUIDFromString(&guid, subject)) return TRUE;
        cdf_report(callback, CRYPTCAT_E_AREA_ATTRIBUTE, CRYPTCAT_E_CDF_BAD_GUID_CONV, m->alt_sip->text);
        return FALSE;
    }
    if (CryptSIPRetrieveSubjectGuidForCatalogFile(path, NULL, subject)) return TRUE;
    cdf_report(callback, CRYPTCAT_E_AREA_MEMBER, CRYPTCAT_E_CDF_MEMBER_INDIRECTDATA, m->line->text);
    return FALSE;
}

static SIP_INDIRECT_DATA *cdf_indirect_data(struct cdf *cdf, const WCHAR *path, GUID *subject, DWORD *size)
{
    SIP_DISPATCH_INFO sip;
    SIP_SUBJECTINFO info;
    SIP_INDIRECT_DATA *indirect;
    HANDLE file;
    BOOL ret;

    memset(&sip, 0, sizeof(sip));
    sip.cbSize = sizeof(sip);
    if (!CryptSIPLoad(subject, 0, &sip) || !sip.pfCreate) return NULL;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;

    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.pgSubjectType = subject;
    info.hFile = file;
    info.pwsFileName = path;
    info.DigestAlgorithm.pszObjId = (char *)cdf->hash_oid;
    info.dwFlags = cdf->sip_flags;

    *size = 0;
    indirect = NULL;
    ret = sip.pfCreate(&info, size, NULL) && (indirect = malloc(*size)) && sip.pfCreate(&info, size, indirect);
    CloseHandle(file);
    if (ret) return indirect;
    free(indirect);
    return NULL;
}

static void cdf_relocate(void *field, const void *old_base, DWORD size, void *new_base)
{
    BYTE **ptr = field;

    if (*ptr >= (const BYTE *)old_base && *ptr < (const BYTE *)old_base + size)
        *ptr = (BYTE *)new_base + (*ptr - (const BYTE *)old_base);
}

static CRYPTCATMEMBER *cdf_literal_member(const WCHAR *tag, const WCHAR *path, const GUID *subject,
                                          const SIP_INDIRECT_DATA *indirect, DWORD size)
{
    DWORD tag_size = (wcslen(tag) + 1) * sizeof(WCHAR), path_size = (wcslen(path) + 1) * sizeof(WCHAR);
    CRYPTCATMEMBER *member;
    BYTE *p;

    if (!(member = calloc(1, sizeof(*member) + tag_size + path_size + size)))
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    p = (BYTE *)(member + 1);
    member->cbStruct = sizeof(*member);
    member->pwszReferenceTag = memcpy(p, tag, tag_size);
    member->pwszFileName = memcpy(p + tag_size, path, path_size);
    member->gSubjectType = *subject;
    member->pIndirectData = memcpy(p + tag_size + path_size, indirect, size);
    cdf_relocate(&member->pIndirectData->Data.pszObjId, indirect, size, member->pIndirectData);
    cdf_relocate(&member->pIndirectData->Data.Value.pbData, indirect, size, member->pIndirectData);
    cdf_relocate(&member->pIndirectData->DigestAlgorithm.pszObjId, indirect, size, member->pIndirectData);
    cdf_relocate(&member->pIndirectData->DigestAlgorithm.Parameters.pbData, indirect, size, member->pIndirectData);
    cdf_relocate(&member->pIndirectData->Digest.pbData, indirect, size, member->pIndirectData);
    member->dwCertVersion = CDF_CERT_VERSION;
    return member;
}

static CRYPTCATMEMBER *cdf_add_member(struct cdf *cdf, struct cdf_member *m, BOOL hash_tags,
                                      PFN_CDF_PARSE_ERROR_CALLBACK callback)
{
    SIP_INDIRECT_DATA *indirect;
    CRYPTCATMEMBER *member;
    WCHAR path[MAX_PATH], *tag;
    GUID subject;
    DWORD size, len;

    len = GetFullPathNameW(m->line->value, ARRAY_SIZE(path), path, NULL);
    if (!m->line->value[0] || !len || len >= ARRAY_SIZE(path))
    {
        cdf_report(callback, CRYPTCAT_E_AREA_MEMBER, CRYPTCAT_E_CDF_MEMBER_FILE_PATH, m->line->text);
        return NULL;
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
    {
        cdf_report(callback, CRYPTCAT_E_AREA_MEMBER, CRYPTCAT_E_CDF_MEMBER_FILENOTFOUND, m->line->text);
        return NULL;
    }
    if (!cdf_member_subject(m, path, &subject, callback)) return NULL;
    if (!(indirect = cdf_indirect_data(cdf, path, &subject, &size)))
    {
        cdf_report(callback, CRYPTCAT_E_AREA_MEMBER, CRYPTCAT_E_CDF_MEMBER_INDIRECTDATA, m->line->text);
        return NULL;
    }

    if (hash_tags && cdf_has_key_prefix(m->line->key, L"<HASH>"))
    {
        if (!cdf_hash_tag(indirect, &tag))
        {
            free(indirect);
            SetLastError(ERROR_OUTOFMEMORY);
            return NULL;
        }
    }
    else
        tag = NULL;

    if (cdf->version == CRYPTCAT_VERSION_2)
    {
        /* Windows 11 hands out the literal tags of a version 2 definition and then writes no catalog */
        member = cdf_literal_member(m->line->key, path, &subject, indirect, size);
        m->owned = member != NULL;
        cdf->literal_v2_members |= m->owned;
    }
    else
        member = CryptCATPutMemberInfo(cdf->public.hCATStore, path, tag ? tag : m->line->key, &subject,
                                       CDF_CERT_VERSION, size, (BYTE *)indirect);
    free(tag);
    free(indirect);
    if (!member)
        cdf_report(callback, CRYPTCAT_E_AREA_MEMBER, CRYPTCAT_E_CDF_MEMBER_INDIRECTDATA, m->line->text);
    return member;
}

static struct cdf_member *cdf_process_member(struct cdf *cdf, DWORD index, BOOL hash_tags,
                                             PFN_CDF_PARSE_ERROR_CALLBACK callback)
{
    struct cdf_member *m = &cdf->members[index];

    if (!m->processed)
    {
        m->processed = TRUE;
        m->member = cdf_add_member(cdf, m, hash_tags, callback);
    }
    return m;
}

/***********************************************************************
 *      CryptCATCDFEnumMembersByCDFTagEx  (WINTRUST.@)
 */
LPWSTR WINAPI CryptCATCDFEnumMembersByCDFTagEx(CRYPTCATCDF *pCDF, LPWSTR pwszPrevCDFTag,
                                               PFN_CDF_PARSE_ERROR_CALLBACK pfnParseError,
                                               CRYPTCATMEMBER **ppMember, BOOL fContinueOnError,
                                               LPVOID pvReserved)
{
    struct cdf_member *m;
    struct cdf *cdf;
    DWORD i = 0;

    TRACE("(%p %s %p %p %d %p)\n", pCDF, debugstr_w(pwszPrevCDFTag), pfnParseError, ppMember,
          fContinueOnError, pvReserved);

    if (!(cdf = cdf_from_public(pCDF))) return NULL;
    if (!ppMember || cdf->version == CRYPTCAT_VERSION_2)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    *ppMember = NULL;

    if (pwszPrevCDFTag)
    {
        for (i = 0; i < cdf->member_count && cdf->members[i].line->key != pwszPrevCDFTag &&
                    _wcsicmp(cdf->members[i].line->key, pwszPrevCDFTag); i++)
            ;
        if (i == cdf->member_count)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
        i++;
    }

    for (; i < cdf->member_count; i++)
    {
        m = cdf_process_member(cdf, i, TRUE, pfnParseError);
        if (m->member)
        {
            *ppMember = m->member;
            return m->line->key;
        }
        if (!fContinueOnError) return NULL;
    }
    SetLastError(ERROR_SUCCESS);
    return NULL;
}

/***********************************************************************
 *      CryptCATCDFEnumMembers  (WINTRUST.@)
 */
CRYPTCATMEMBER * WINAPI CryptCATCDFEnumMembers(CRYPTCATCDF *pCDF, CRYPTCATMEMBER *pPrevMember,
                                               PFN_CDF_PARSE_ERROR_CALLBACK pfnParseError)
{
    struct cdf_member *m;
    struct cdf *cdf;
    DWORD i = 0;

    TRACE("(%p %p %p)\n", pCDF, pPrevMember, pfnParseError);

    if (!(cdf = cdf_from_public(pCDF))) return NULL;

    if (pPrevMember)
    {
        for (i = 0; i < cdf->member_count && cdf->members[i].member != pPrevMember; i++)
            ;
        if (i == cdf->member_count)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
        i++;
    }

    for (; i < cdf->member_count; i++)
    {
        m = cdf_process_member(cdf, i, FALSE, pfnParseError);
        if (m->member) return m->member;
    }
    SetLastError(ERROR_SUCCESS);
    return NULL;
}

/***********************************************************************
 *      CryptCATCDFEnumAttributes  (WINTRUST.@)
 */
CRYPTCATATTRIBUTE * WINAPI CryptCATCDFEnumAttributes(CRYPTCATCDF *pCDF, CRYPTCATMEMBER *pMember,
                                                     CRYPTCATATTRIBUTE *pPrevAttr,
                                                     PFN_CDF_PARSE_ERROR_CALLBACK pfnParseError)
{
    struct cdf *cdf;
    DWORD i;

    TRACE("(%p %p %p %p)\n", pCDF, pMember, pPrevAttr, pfnParseError);

    if (!(cdf = cdf_from_public(pCDF))) return NULL;
    for (i = 0; pMember && i < cdf->member_count && cdf->members[i].member != pMember; i++)
        ;
    if (!pMember || i == cdf->member_count)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return cdf_next_attr(cdf, cdf->members[i].attrs, cdf->members[i].attr_count, pMember, pPrevAttr, pfnParseError);
}

/***********************************************************************
 *      CryptCATCDFEnumAttributesWithCDFTag  (WINTRUST.@)
 */
CRYPTCATATTRIBUTE * WINAPI CryptCATCDFEnumAttributesWithCDFTag(CRYPTCATCDF *pCDF, LPWSTR pwszMemberTag,
                                                               CRYPTCATMEMBER *pMember,
                                                               CRYPTCATATTRIBUTE *pPrevAttr,
                                                               PFN_CDF_PARSE_ERROR_CALLBACK pfnParseError)
{
    struct cdf *cdf;
    DWORD i;

    TRACE("(%p %s %p %p %p)\n", pCDF, debugstr_w(pwszMemberTag), pMember, pPrevAttr, pfnParseError);

    if (!(cdf = cdf_from_public(pCDF))) return NULL;
    for (i = 0; pwszMemberTag && i < cdf->member_count && cdf->members[i].line->key != pwszMemberTag &&
                _wcsicmp(cdf->members[i].line->key, pwszMemberTag); i++)
        ;
    if (!pwszMemberTag || !pMember || i == cdf->member_count || cdf->members[i].member != pMember)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return cdf_next_attr(cdf, cdf->members[i].attrs, cdf->members[i].attr_count, pMember, pPrevAttr, pfnParseError);
}

/***********************************************************************
 *      CryptCATCDFEnumMembersByCDFTag  (WINTRUST.@)
 */
LPWSTR WINAPI CryptCATCDFEnumMembersByCDFTag(CRYPTCATCDF *pCDF, LPWSTR pwszPrevCDFTag,
                                             PFN_CDF_PARSE_ERROR_CALLBACK pfnParseError, CRYPTCATMEMBER **ppMember)
{
    TRACE("(%p %s %p %p)\n", pCDF, debugstr_w(pwszPrevCDFTag), pfnParseError, ppMember);

    return CryptCATCDFEnumMembersByCDFTagEx(pCDF, pwszPrevCDFTag, pfnParseError, ppMember, FALSE, NULL);
}
