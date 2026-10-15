/*
 * PROJECT:     ReactOS Application compatibility module
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Query file attributes used to match exe's
 * COPYRIGHT:   Copyright 2011 André Hentschel
 *              Copyright 2013 Mislav Blaževic
 *              Copyright 2015-2017 Mark Jansen (mark.jansen@reactos.org)
 */

#define WIN32_NO_STATUS
#include "windef.h"
#include "apphelp.h"
#include "strsafe.h"
#include "winver.h"
#include "rtlfuncs.h"


enum APPHELP_ATTRIBUTE
{
    ATTR_SIZE,
    ATTR_FILESIZE,
    ATTR_MODULE_TYPE,
    ATTR_SIZE_OF_IMAGE,
    ATTR_PE_CHECKSUM,
    ATTR_LINKER_VERSION,
    ATTR_LINK_DATE,
    ATTR_FROM_LINK_DATE,
    ATTR_UPTO_LINK_DATE,
    ATTR_FILE_KIND_DETAIL,
    ATTR_BIN_PRODUCT_VERSION,
    ATTR_FROM_BIN_PRODUCT_VERSION,
    ATTR_UPTO_BIN_PRODUCT_VERSION,
    ATTR_BIN_FILE_VERSION,
    ATTR_FROM_BIN_FILE_VERSION,
    ATTR_UPTO_BIN_FILE_VERSION,
    ATTR_PRODUCT_NAME,
    ATTR_PRODUCT_VERSION,
    ATTR_FROM_PRODUCT_VERSION,
    ATTR_UPTO_PRODUCT_VERSION,
    ATTR_FILE_VERSION,
    ATTR_FROM_FILE_VERSION,
    ATTR_UPTO_FILE_VERSION,
    ATTR_FILE_DESCRIPTION,
    ATTR_COMPANY_NAME,
    ATTR_ORIGINAL_FILENAME,
    ATTR_INTERNAL_NAME,
    ATTR_LEGAL_COPYRIGHT,
    ATTR_VER_LANGUAGE,
    ATTR_VERDATEHI,
    ATTR_VERDATELO,
    ATTR_VERFILEOS,
    ATTR_VERFILETYPE,
    ATTR_IS_ARM64X,
    ATTR_EXPORT_NAME,
    ATTR_EXE_WRAPPER,
    ATTR_16BIT_DESCRIPTION,
    ATTR_16BIT_MODULE_NAME,
    ATTR_CHECKSUM,
    ATTR_CRC_CHECKSUM,
    ATTR_CRC_CHECKSUM_BOOT,
    NUM_ATTRIBUTES
};

static const TAG g_AttributeTags[NUM_ATTRIBUTES] =
{
    TAG_SIZE,
    TAG_FILESIZE,
    TAG_MODULE_TYPE,
    TAG_SIZE_OF_IMAGE,
    TAG_PE_CHECKSUM,
    TAG_LINKER_VERSION,
    TAG_LINK_DATE,
    TAG_FROM_LINK_DATE,
    TAG_UPTO_LINK_DATE,
    TAG_FILE_KIND_DETAIL,
    TAG_BIN_PRODUCT_VERSION,
    TAG_FROM_BIN_PRODUCT_VERSION,
    TAG_UPTO_BIN_PRODUCT_VERSION,
    TAG_BIN_FILE_VERSION,
    TAG_FROM_BIN_FILE_VERSION,
    TAG_UPTO_BIN_FILE_VERSION,
    TAG_PRODUCT_NAME,
    TAG_PRODUCT_VERSION,
    TAG_FROM_PRODUCT_VERSION,
    TAG_UPTO_PRODUCT_VERSION,
    TAG_FILE_VERSION,
    TAG_FROM_FILE_VERSION,
    TAG_UPTO_FILE_VERSION,
    TAG_FILE_DESCRIPTION,
    TAG_COMPANY_NAME,
    TAG_ORIGINAL_FILENAME,
    TAG_INTERNAL_NAME,
    TAG_LEGAL_COPYRIGHT,
    TAG_VER_LANGUAGE,
    TAG_VERDATEHI,
    TAG_VERDATELO,
    TAG_VERFILEOS,
    TAG_VERFILETYPE,
    TAG_IS_ARM64X,
    TAG_EXPORT_NAME,
    TAG_EXE_WRAPPER,
    TAG_16BIT_DESCRIPTION,
    TAG_16BIT_MODULE_NAME,
    TAG_CHECKSUM,
    TAG_CRC_CHECKSUM,
    TAG_CRC_CHECKSUM_BOOT,
};
enum APPHELP_MODULETYPE
{
    MODTYPE_UNKNOWN = 0,
    MODTYPE_DOS = 1,
    MODTYPE_NE = 2,
    MODTYPE_PE = 3,
};


static void WINAPI SdbpSetDWORDAttr(PATTRINFO attr, TAG tag, DWORD value)
{
    attr->type = tag;
    attr->flags = ATTRIBUTE_AVAILABLE;
    attr->dwattr = value;
}

static void WINAPI SdbpSetQWORDAttr(PATTRINFO attr, TAG tag, QWORD value)
{
    attr->type = tag;
    attr->flags = ATTRIBUTE_AVAILABLE;
    attr->qwattr = value;
}

static void WINAPI SdbpSetStringAttr(PATTRINFO attr, TAG tag, WCHAR *string)
{
    if (!string)
        return;

    attr->type = tag;
    attr->flags = ATTRIBUTE_AVAILABLE;
    attr->lpattr = SdbpStrDup(string);
}

static WCHAR* WINAPI SdbpGetStringAttr(LPWSTR translation, LPCWSTR attr, PVOID file_info)
{
    UINT size = 0;
    PVOID buffer;
    WCHAR value[128] = {0};

    if (!file_info)
        return NULL;

    StringCchPrintfW(value, ARRAYSIZE(value), translation, attr);
    if (VerQueryValueW(file_info, value, &buffer, &size) && size != 0)
        return (WCHAR*)buffer;

    return NULL;
}

static void WINAPI SdbpSetStringAttrFromAnsiString(PATTRINFO attr, TAG tag, PBYTE string, size_t len)
{
    WCHAR* dest;
    if (!string)
        return;

    attr->type = tag;
    attr->flags = ATTRIBUTE_AVAILABLE;
    dest = attr->lpattr = SdbAlloc((len+1) * sizeof(WCHAR));
    while (len--)
        *(dest++) = *(string++);
    *dest = 0;
}

static void WINAPI SdbpSetStringAttrFromPascalString(PATTRINFO attr, TAG tag, PBYTE string)
{
    if (!string)
        return;

    SdbpSetStringAttrFromAnsiString(attr, tag, string + 1, *string);
}

static void SdbpReadFileVersion(PATTRINFO attr_info, PVOID file_info)
{
    static const WCHAR str_root[] = {'\\',0};

    VS_FIXEDFILEINFO* fixed_info;
    UINT size;
    if (file_info && VerQueryValueW(file_info, str_root, (LPVOID*)&fixed_info, &size) && size)
    {
        if (fixed_info->dwSignature == VS_FFI_SIGNATURE)
        {
            LARGE_INTEGER version;
            version.HighPart = fixed_info->dwProductVersionMS;
            version.LowPart = fixed_info->dwProductVersionLS;
            SdbpSetQWORDAttr(&attr_info[ATTR_BIN_PRODUCT_VERSION], TAG_BIN_PRODUCT_VERSION, version.QuadPart);
            SdbpSetQWORDAttr(&attr_info[ATTR_FROM_BIN_PRODUCT_VERSION], TAG_FROM_BIN_PRODUCT_VERSION, version.QuadPart);
            SdbpSetQWORDAttr(&attr_info[ATTR_UPTO_BIN_PRODUCT_VERSION], TAG_UPTO_BIN_PRODUCT_VERSION, version.QuadPart);
            version.HighPart = fixed_info->dwFileVersionMS;
            version.LowPart = fixed_info->dwFileVersionLS;
            SdbpSetQWORDAttr(&attr_info[ATTR_BIN_FILE_VERSION], TAG_BIN_FILE_VERSION, version.QuadPart);
            SdbpSetQWORDAttr(&attr_info[ATTR_FROM_BIN_FILE_VERSION], TAG_FROM_BIN_FILE_VERSION, version.QuadPart);
            SdbpSetQWORDAttr(&attr_info[ATTR_UPTO_BIN_FILE_VERSION], TAG_UPTO_BIN_FILE_VERSION, version.QuadPart);

            SdbpSetDWORDAttr(&attr_info[ATTR_VERDATEHI], TAG_VERDATEHI, fixed_info->dwFileDateMS);
            SdbpSetDWORDAttr(&attr_info[ATTR_VERDATELO], TAG_VERDATELO, fixed_info->dwFileDateLS);
            SdbpSetDWORDAttr(&attr_info[ATTR_VERFILEOS], TAG_VERFILEOS, fixed_info->dwFileOS);
            SdbpSetDWORDAttr(&attr_info[ATTR_VERFILETYPE], TAG_VERFILETYPE, fixed_info->dwFileType);
        }
    }
}

static void SdbpSetFileKindDetail(PATTRINFO attr, PMEMMAPPED mapping, DWORD module_type, PIMAGE_NT_HEADERS headers)
{
    if (!mapping->size)
        SdbpSetDWORDAttr(attr, TAG_FILE_KIND_DETAIL, 1);
    else if (module_type == MODTYPE_NE)
        SdbpSetDWORDAttr(attr, TAG_FILE_KIND_DETAIL, 4);
    else if (module_type == MODTYPE_PE && headers && headers->FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
        SdbpSetDWORDAttr(attr, TAG_FILE_KIND_DETAIL, 6);
    else if (module_type == MODTYPE_PE && headers && headers->FileHeader.Machine == IMAGE_FILE_MACHINE_ARM64)
        SdbpSetDWORDAttr(attr, TAG_FILE_KIND_DETAIL, 0xA);
    else if (module_type != MODTYPE_PE && mapping->size >= 2 && ((PIMAGE_DOS_HEADER)mapping->view)->e_magic == IMAGE_DOS_SIGNATURE)
        SdbpSetDWORDAttr(attr, TAG_FILE_KIND_DETAIL, 2);
}

static void SdbpSetCrcChecksum(PATTRINFO attr, PMEMMAPPED mapping)
{
    BYTE page[0x1000] = {0};

    if (!mapping->size || mapping->size > sizeof(page))
        return;

    memcpy(page, mapping->view, mapping->size);
    SdbpSetDWORDAttr(attr, TAG_CRC_CHECKSUM, RtlComputeCrc32(0, page, sizeof(page)));
}

static DWORD WINAPI SdbpCalculateFileChecksum(PMEMMAPPED mapping)
{
    size_t n, size;
    PDWORD data;
    DWORD checks = 0, carry = 0;

    if (mapping->size < 4)
        return 0;

    if (mapping->size >= 0x1000)
    {
        size = 0x1000;
        if (mapping->size < 0x1200)
            data = (PDWORD)(mapping->view + mapping->size - size);
        else
            data = (PDWORD)mapping->view + (0x200 / 4);
    }
    else
    {
        data = (PDWORD)mapping->view;
        size = mapping->size;
    }

    for (n = 0; n < size / 4; ++n)
    {
        checks += *data;
        carry = (checks & 1) ? 0x80000000 : 0;
        checks >>= 1;
        checks |= carry;
        ++data;
    }
    return checks;
}

static DWORD WINAPI SdbpGetModuleType(PMEMMAPPED mapping, PIMAGE_NT_HEADERS* nt_headers)
{
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)mapping->view;
    PIMAGE_OS2_HEADER os2;

    *nt_headers = NULL;

    if (mapping->size < sizeof(IMAGE_DOS_HEADER) || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return MODTYPE_UNKNOWN;

    if (mapping->size < sizeof(IMAGE_DOS_HEADER) || mapping->size < (dos->e_lfanew+2))
        return MODTYPE_DOS;

    os2 = (PIMAGE_OS2_HEADER)((PBYTE)dos + dos->e_lfanew);
    if (os2->ne_magic == IMAGE_OS2_SIGNATURE || os2->ne_magic == IMAGE_OS2_SIGNATURE_LE)
    {
        *nt_headers = (PIMAGE_NT_HEADERS)os2;
        return MODTYPE_NE;
    }

    if (mapping->size >= (dos->e_lfanew + 4) && ((PIMAGE_NT_HEADERS)os2)->Signature == IMAGE_NT_SIGNATURE)
    {
        *nt_headers = (PIMAGE_NT_HEADERS)os2;
        return MODTYPE_PE;
    }

    return MODTYPE_DOS;
}

/**
 * Frees attribute data allocated by SdbGetFileAttributes.
 *
 * @note Unlike Windows, this implementation will not crash if attr_info is NULL.
 *
 * @param [in]  attr_info   Pointer to array of ATTRINFO which will be freed.
 *
 * @return  TRUE if it succeeds, FALSE if it fails.
 */
BOOL WINAPI SdbFreeFileAttributes(PATTRINFO attr_info)
{
    WORD i;

    if (!attr_info)
        return FALSE;

    for (i = 0; i < NUM_ATTRIBUTES; i++)
        if ((attr_info[i].type & TAG_TYPE_MASK) == TAG_TYPE_STRINGREF && attr_info[i].lpattr)
            SdbFree(attr_info[i].lpattr);
    SdbFree(attr_info);
    return TRUE;
}

/**
 * Retrieves attribute data shim database requires to match a file with database entry
 *
 * @note You must free the attr_info allocated by this function by calling SdbFreeFileAttributes.
 *
 * @param [in]  path            Path to the file.
 * @param [out] attr_info_ret   Pointer to array of ATTRINFO. Contains attribute data.
 * @param [out] attr_count      Number of attributes in attr_info.
 *
 * @return  TRUE if it succeeds, FALSE if it fails.
 */
BOOL WINAPI SdbGetFileAttributes(LPCWSTR path, PATTRINFO *attr_info_ret, LPDWORD attr_count)
{
    static const WCHAR str_tinfo[] = {'\\','V','a','r','F','i','l','e','I','n','f','o','\\','T','r','a','n','s','l','a','t','i','o','n',0};
    static const WCHAR str_trans[] = {'\\','S','t','r','i','n','g','F','i','l','e','I','n','f','o','\\','%','0','4','x','%','0','4','x','\\','%','%','s',0};
    static const WCHAR str_CompanyName[] = {'C','o','m','p','a','n','y','N','a','m','e',0};
    static const WCHAR str_FileDescription[] = {'F','i','l','e','D','e','s','c','r','i','p','t','i','o','n',0};
    static const WCHAR str_FileVersion[] = {'F','i','l','e','V','e','r','s','i','o','n',0};
    static const WCHAR str_InternalName[] = {'I','n','t','e','r','n','a','l','N','a','m','e',0};
    static const WCHAR str_LegalCopyright[] = {'L','e','g','a','l','C','o','p','y','r','i','g','h','t',0};
    static const WCHAR str_OriginalFilename[] = {'O','r','i','g','i','n','a','l','F','i','l','e','n','a','m','e',0};
    static const WCHAR str_ProductName[] = {'P','r','o','d','u','c','t','N','a','m','e',0};
    static const WCHAR str_ProductVersion[] = {'P','r','o','d','u','c','t','V','e','r','s','i','o','n',0};

    PIMAGE_NT_HEADERS headers;
    MEMMAPPED mapped;
    PBYTE mapping_end;
    PVOID file_info = 0;
    DWORD module_type;
    WCHAR translation[128] = {0};
    PATTRINFO attr_info;
    WCHAR *string;
    DWORD n;

    struct LANGANDCODEPAGE {
        WORD language;
        WORD code_page;
    } *lang_page;

    if (!SdbpOpenMemMappedFile(path, &mapped))
    {
        SHIM_ERR("Error retrieving FILEINFO structure\n");
        return FALSE;
    }
    mapping_end = mapped.view + mapped.size;

    attr_info = (PATTRINFO)SdbAlloc(NUM_ATTRIBUTES * sizeof(ATTRINFO));
    for (n = 0; n < NUM_ATTRIBUTES; ++n)
        attr_info[n].type = g_AttributeTags[n];

    SdbpSetDWORDAttr(&attr_info[ATTR_SIZE], TAG_SIZE, mapped.size);
    if (mapped.size)
    {
        SdbpSetQWORDAttr(&attr_info[ATTR_FILESIZE], TAG_FILESIZE, mapped.size);
        SdbpSetDWORDAttr(&attr_info[ATTR_CHECKSUM], TAG_CHECKSUM, SdbpCalculateFileChecksum(&mapped));
    }
    SdbpSetCrcChecksum(&attr_info[ATTR_CRC_CHECKSUM], &mapped);

    module_type = SdbpGetModuleType(&mapped, &headers);
    SdbpSetDWORDAttr(&attr_info[ATTR_MODULE_TYPE], TAG_MODULE_TYPE, module_type);

    if (headers && module_type == MODTYPE_PE && ((PBYTE)(headers+1) > mapping_end))
        headers = NULL;
    SdbpSetFileKindDetail(&attr_info[ATTR_FILE_KIND_DETAIL], &mapped, module_type, headers);

    if (headers && module_type == MODTYPE_PE)
    {
        DWORD info_size;
        ULONG export_dir_size;
        PIMAGE_EXPORT_DIRECTORY export_dir;
        LPCWSTR dos_path;

        /* Check if the caller passed an NT style path (full path, prefixed wih "\\??\\") */
        if (!wcsncmp(path, L"\\??\\", 4) && (path[4] != UNICODE_NULL) && (path[5] == L':'))
        {
            /* Skip NT prefix, if it is present (GetFileVersionInfoSizeW doesn't handle NT paths) */
            dos_path = path + 4;
        }
        else
        {
            dos_path = path;
        }

        info_size = GetFileVersionInfoSizeW(dos_path, NULL);
        if (info_size != 0)
        {
            UINT page_size = 0;
            file_info = SdbAlloc(info_size);
            GetFileVersionInfoW(dos_path, 0, info_size, file_info);
            VerQueryValueW(file_info, str_tinfo, (LPVOID)&lang_page, &page_size);
            StringCchPrintfW(translation, ARRAYSIZE(translation), str_trans, lang_page->language, lang_page->code_page);
        }

        SdbpReadFileVersion(attr_info, file_info);

        SdbpSetStringAttr(&attr_info[ATTR_PRODUCT_NAME], TAG_PRODUCT_NAME, SdbpGetStringAttr(translation, str_ProductName, file_info));
        string = SdbpGetStringAttr(translation, str_ProductVersion, file_info);
        SdbpSetStringAttr(&attr_info[ATTR_PRODUCT_VERSION], TAG_PRODUCT_VERSION, string);
        SdbpSetStringAttr(&attr_info[ATTR_FROM_PRODUCT_VERSION], TAG_FROM_PRODUCT_VERSION, string);
        SdbpSetStringAttr(&attr_info[ATTR_UPTO_PRODUCT_VERSION], TAG_UPTO_PRODUCT_VERSION, string);
        string = SdbpGetStringAttr(translation, str_FileVersion, file_info);
        SdbpSetStringAttr(&attr_info[ATTR_FILE_VERSION], TAG_FILE_VERSION, string);
        SdbpSetStringAttr(&attr_info[ATTR_FROM_FILE_VERSION], TAG_FROM_FILE_VERSION, string);
        SdbpSetStringAttr(&attr_info[ATTR_UPTO_FILE_VERSION], TAG_UPTO_FILE_VERSION, string);
        SdbpSetStringAttr(&attr_info[ATTR_FILE_DESCRIPTION], TAG_FILE_DESCRIPTION, SdbpGetStringAttr(translation, str_FileDescription, file_info));
        SdbpSetStringAttr(&attr_info[ATTR_COMPANY_NAME], TAG_COMPANY_NAME, SdbpGetStringAttr(translation, str_CompanyName, file_info));
        SdbpSetStringAttr(&attr_info[ATTR_ORIGINAL_FILENAME], TAG_ORIGINAL_FILENAME, SdbpGetStringAttr(translation, str_OriginalFilename, file_info));
        SdbpSetStringAttr(&attr_info[ATTR_INTERNAL_NAME], TAG_INTERNAL_NAME, SdbpGetStringAttr(translation, str_InternalName, file_info));
        SdbpSetStringAttr(&attr_info[ATTR_LEGAL_COPYRIGHT], TAG_LEGAL_COPYRIGHT, SdbpGetStringAttr(translation, str_LegalCopyright, file_info));

        /* https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_optional_header32 */

        SdbpSetDWORDAttr(&attr_info[ATTR_SIZE_OF_IMAGE], TAG_SIZE_OF_IMAGE, headers->OptionalHeader.SizeOfImage);
        SdbpSetDWORDAttr(&attr_info[ATTR_PE_CHECKSUM], TAG_PE_CHECKSUM, headers->OptionalHeader.CheckSum);

        SdbpSetDWORDAttr(&attr_info[ATTR_LINKER_VERSION], TAG_LINKER_VERSION,     /* mislabeled! */
            ((DWORD)headers->OptionalHeader.MajorImageVersion) << 16 | headers->OptionalHeader.MinorImageVersion);

        SdbpSetDWORDAttr(&attr_info[ATTR_LINK_DATE], TAG_LINK_DATE, headers->FileHeader.TimeDateStamp);
        SdbpSetDWORDAttr(&attr_info[ATTR_FROM_LINK_DATE], TAG_FROM_LINK_DATE, headers->FileHeader.TimeDateStamp);
        SdbpSetDWORDAttr(&attr_info[ATTR_UPTO_LINK_DATE], TAG_UPTO_LINK_DATE, headers->FileHeader.TimeDateStamp);

        export_dir = (PIMAGE_EXPORT_DIRECTORY)RtlImageDirectoryEntryToData(mapped.view, FALSE, IMAGE_DIRECTORY_ENTRY_EXPORT, &export_dir_size);
        if (export_dir && ((PBYTE)(export_dir+1) <= mapping_end))
        {
            PIMAGE_SECTION_HEADER section = NULL;
            PBYTE export_name = RtlImageRvaToVa(headers, mapped.view, export_dir->Name, &section);
            if (export_name)
                SdbpSetStringAttrFromAnsiString(&attr_info[ATTR_EXPORT_NAME], TAG_EXPORT_NAME, export_name, strlen((char*)export_name));
        }

        if (info_size)
            SdbpSetDWORDAttr(&attr_info[ATTR_VER_LANGUAGE], TAG_VER_LANGUAGE, lang_page->language);

        SdbpSetDWORDAttr(&attr_info[ATTR_EXE_WRAPPER], TAG_EXE_WRAPPER, 0); /* boolean */
    }
    else if (module_type == MODTYPE_NE)
    {
        PBYTE ptr;
        PIMAGE_OS2_HEADER os2 = (PIMAGE_OS2_HEADER)headers;
        if ((PBYTE)(os2 + 1) <= mapping_end)
        {
            ptr = mapped.view + os2->ne_nrestab;
            if (ptr <= mapping_end && (ptr + 1 + *ptr) <= mapping_end)
                SdbpSetStringAttrFromPascalString(&attr_info[ATTR_16BIT_DESCRIPTION], TAG_16BIT_DESCRIPTION, ptr);
            ptr = (PBYTE)os2 + os2->ne_restab;
            if (ptr <= mapping_end && (ptr + 1 + *ptr) <= mapping_end)
                SdbpSetStringAttrFromPascalString(&attr_info[ATTR_16BIT_MODULE_NAME], TAG_16BIT_MODULE_NAME, ptr);
        }
    }

    *attr_info_ret = attr_info;
    *attr_count = NUM_ATTRIBUTES;

    SdbFree(file_info);
    SdbpCloseMemMappedFile(&mapped);
    return TRUE;
}
