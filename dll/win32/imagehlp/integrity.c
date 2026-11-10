/*
 *	IMAGEHLP library
 *
 *	Copyright 1998	Patrik Stridvall
 *	Copyright 2003	Mike McCormack
 *	Copyright 2009  Owen Rudge for CodeWeavers
 *	Copyright 2010  Juan Lang
 *	Copyright 2010  Andrey Turkin
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
#include "winerror.h"
#include "winternl.h"
#include "winnt.h"
#include "winver.h"
#include "imagehlp.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(imagehlp);

/*
 * These functions are partially documented at:
 *   http://www.cs.auckland.ac.nz/~pgut001/pubs/authenticode.txt
 */

#define HDR_FAIL   -1
#define HDR_NT32    0
#define HDR_NT64    1

/***********************************************************************
 * IMAGEHLP_GetNTHeaders (INTERNAL)
 *
 * Return the IMAGE_NT_HEADERS for a PE file, after validating magic
 * numbers and distinguishing between 32-bit and 64-bit files.
 */
static int IMAGEHLP_GetNTHeaders(HANDLE handle, DWORD *pe_offset, IMAGE_NT_HEADERS32 *nt32, IMAGE_NT_HEADERS64 *nt64)
{
    IMAGE_DOS_HEADER dos_hdr;
    DWORD count;
    BOOL r;

    TRACE("handle %p\n", handle);

    if ((!nt32) || (!nt64))
        return HDR_FAIL;

    /* read the DOS header */
    count = SetFilePointer(handle, 0, NULL, FILE_BEGIN);

    if (count == INVALID_SET_FILE_POINTER)
        return HDR_FAIL;

    count = 0;

    r = ReadFile(handle, &dos_hdr, sizeof dos_hdr, &count, NULL);

    if (!r)
        return HDR_FAIL;

    if (count != sizeof dos_hdr)
        return HDR_FAIL;

    /* verify magic number of 'MZ' */
    if (dos_hdr.e_magic != IMAGE_DOS_SIGNATURE)
        return HDR_FAIL;

    if (pe_offset != NULL)
        *pe_offset = dos_hdr.e_lfanew;

    /* read the PE header */
    count = SetFilePointer(handle, dos_hdr.e_lfanew, NULL, FILE_BEGIN);

    if (count == INVALID_SET_FILE_POINTER)
        return HDR_FAIL;

    count = 0;

    r = ReadFile(handle, nt32, sizeof(IMAGE_NT_HEADERS32), &count, NULL);

    if (!r)
        return HDR_FAIL;

    if (count != sizeof(IMAGE_NT_HEADERS32))
        return HDR_FAIL;

    /* verify NT signature */
    if (nt32->Signature != IMAGE_NT_SIGNATURE)
        return HDR_FAIL;

    /* check if we have a 32-bit or 64-bit executable */
    switch (nt32->OptionalHeader.Magic)
    {
        case IMAGE_NT_OPTIONAL_HDR32_MAGIC:
            return HDR_NT32;

        case IMAGE_NT_OPTIONAL_HDR64_MAGIC:
            /* Re-read as 64-bit */

            count = SetFilePointer(handle, dos_hdr.e_lfanew, NULL, FILE_BEGIN);

            if (count == INVALID_SET_FILE_POINTER)
                return HDR_FAIL;

            count = 0;

            r = ReadFile(handle, nt64, sizeof(IMAGE_NT_HEADERS64), &count, NULL);

            if (!r)
                return HDR_FAIL;

            if (count != sizeof(IMAGE_NT_HEADERS64))
                return HDR_FAIL;

            /* verify NT signature */
            if (nt64->Signature != IMAGE_NT_SIGNATURE)
                return HDR_FAIL;

            return HDR_NT64;
    }

    return HDR_FAIL;
}

/***********************************************************************
 * IMAGEHLP_GetSecurityDirOffset (INTERNAL)
 *
 * Read a file's PE header, and return the offset and size of the
 *  security directory.
 */
static BOOL IMAGEHLP_GetSecurityDirOffset( HANDLE handle,
                                           DWORD *pdwOfs, DWORD *pdwSize )
{
    IMAGE_NT_HEADERS32 nt_hdr32;
    IMAGE_NT_HEADERS64 nt_hdr64;
    IMAGE_DATA_DIRECTORY *sd;
    int ret;

    ret = IMAGEHLP_GetNTHeaders(handle, NULL, &nt_hdr32, &nt_hdr64);

    if (ret == HDR_NT32)
        sd = &nt_hdr32.OptionalHeader.DataDirectory[IMAGE_FILE_SECURITY_DIRECTORY];
    else if (ret == HDR_NT64)
        sd = &nt_hdr64.OptionalHeader.DataDirectory[IMAGE_FILE_SECURITY_DIRECTORY];
    else
        return FALSE;

    TRACE("ret = %d size = %lx addr = %lx\n", ret, sd->Size, sd->VirtualAddress);

    *pdwSize = sd->Size;
    *pdwOfs = sd->VirtualAddress;

    return TRUE;
}

/***********************************************************************
 * IMAGEHLP_SetSecurityDirOffset (INTERNAL)
 *
 * Read a file's PE header, and update the offset and size of the
 *  security directory.
 */
static BOOL IMAGEHLP_SetSecurityDirOffset(HANDLE handle,
                                          DWORD dwOfs, DWORD dwSize)
{
    IMAGE_NT_HEADERS32 nt_hdr32;
    IMAGE_NT_HEADERS64 nt_hdr64;
    IMAGE_DATA_DIRECTORY *sd;
    int ret, nt_hdr_size = 0;
    DWORD pe_offset;
    void *nt_hdr;
    DWORD count;
    BOOL r;

    ret = IMAGEHLP_GetNTHeaders(handle, &pe_offset, &nt_hdr32, &nt_hdr64);

    if (ret == HDR_NT32)
    {
        sd = &nt_hdr32.OptionalHeader.DataDirectory[IMAGE_FILE_SECURITY_DIRECTORY];

        nt_hdr = &nt_hdr32;
        nt_hdr_size = sizeof(IMAGE_NT_HEADERS32);
    }
    else if (ret == HDR_NT64)
    {
        sd = &nt_hdr64.OptionalHeader.DataDirectory[IMAGE_FILE_SECURITY_DIRECTORY];

        nt_hdr = &nt_hdr64;
        nt_hdr_size = sizeof(IMAGE_NT_HEADERS64);
    }
    else
        return FALSE;

    sd->Size = dwSize;
    sd->VirtualAddress = dwOfs;

    TRACE("size = %lx addr = %lx\n", sd->Size, sd->VirtualAddress);

    /* write the header back again */
    count = SetFilePointer(handle, pe_offset, NULL, FILE_BEGIN);

    if (count == INVALID_SET_FILE_POINTER)
        return FALSE;

    count = 0;

    r = WriteFile(handle, nt_hdr, nt_hdr_size, &count, NULL);

    if (!r)
        return FALSE;

    if (count != nt_hdr_size)
        return FALSE;

    return TRUE;
}

/***********************************************************************
 * IMAGEHLP_GetCertificateOffset (INTERNAL)
 *
 * Read a file's PE header, and return the offset and size of the 
 *  security directory.
 */
static BOOL IMAGEHLP_GetCertificateOffset( HANDLE handle, DWORD num,
                                           DWORD *pdwOfs, DWORD *pdwSize )
{
    DWORD size, count, offset, len, sd_VirtualAddr;
    BOOL r;

    r = IMAGEHLP_GetSecurityDirOffset( handle, &sd_VirtualAddr, &size );
    if( !r )
        return FALSE;
#ifdef __REACTOS__
    if( !size )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
#endif

    offset = 0;
    /* take the n'th certificate */
    while( 1 )
    {
        /* read the length of the current certificate */
        count = SetFilePointer( handle, sd_VirtualAddr + offset,
                                 NULL, FILE_BEGIN );
        if( count == INVALID_SET_FILE_POINTER )
            return FALSE;
        r = ReadFile( handle, &len, sizeof len, &count, NULL );
        if( !r )
            return FALSE;
        if( count != sizeof len )
            return FALSE;

        /* check the certificate is not too big or too small */
        if( len < sizeof len )
            return FALSE;
        if( len > (size-offset) )
            return FALSE;
        if( !num-- )
            break;

        /* calculate the offset of the next certificate */
        offset += len;

        /* padded out to the nearest 8-byte boundary */
        if( len % 8 )
            offset += 8 - (len % 8);

        if( offset >= size )
        {
#ifdef __REACTOS__
            SetLastError( ERROR_INVALID_PARAMETER );
#endif
            return FALSE;
        }
    }

    *pdwOfs = sd_VirtualAddr + offset;
    *pdwSize = len;

    TRACE("len = %lx addr = %lx\n", len, sd_VirtualAddr + offset);

    return TRUE;
}

/***********************************************************************
 * IMAGEHLP_RecalculateChecksum (INTERNAL)
 *
 * Update the NT header checksum for the specified file.
 */
static BOOL IMAGEHLP_RecalculateChecksum(HANDLE handle)
{
    DWORD FileLength, count, HeaderSum, pe_offset, nt_hdr_size;
    IMAGE_NT_HEADERS32 nt_hdr32;
    IMAGE_NT_HEADERS64 nt_hdr64;
    LPVOID BaseAddress;
    HANDLE hMapping;
    DWORD *CheckSum;
    void *nt_hdr;
    int ret;
    BOOL r;

    TRACE("handle %p\n", handle);

    ret = IMAGEHLP_GetNTHeaders(handle, &pe_offset, &nt_hdr32, &nt_hdr64);

    if (ret == HDR_NT32)
    {
        CheckSum = &nt_hdr32.OptionalHeader.CheckSum;

        nt_hdr = &nt_hdr32;
        nt_hdr_size = sizeof(IMAGE_NT_HEADERS32);
    }
    else if (ret == HDR_NT64)
    {
        CheckSum = &nt_hdr64.OptionalHeader.CheckSum;

        nt_hdr = &nt_hdr64;
        nt_hdr_size = sizeof(IMAGE_NT_HEADERS64);
    }
    else
        return FALSE;

    hMapping = CreateFileMappingW(handle, NULL, PAGE_READONLY, 0, 0, NULL);

    if (!hMapping)
        return FALSE;

    BaseAddress = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);

    if (!BaseAddress)
    {
        CloseHandle(hMapping);
        return FALSE;
    }

    FileLength = GetFileSize(handle, NULL);

    *CheckSum = 0;
    CheckSumMappedFile(BaseAddress, FileLength, &HeaderSum, CheckSum);

    UnmapViewOfFile(BaseAddress);
    CloseHandle(hMapping);

    if (*CheckSum)
    {
        /* write the header back again */
        count = SetFilePointer(handle, pe_offset, NULL, FILE_BEGIN);

        if (count == INVALID_SET_FILE_POINTER)
            return FALSE;

        count = 0;

        r = WriteFile(handle, nt_hdr, nt_hdr_size, &count, NULL);

        if (!r)
            return FALSE;

        if (count != nt_hdr_size)
            return FALSE;

        return TRUE;
    }

    return FALSE;
}

/***********************************************************************
 *		ImageAddCertificate (IMAGEHLP.@)
 *
 * Adds the specified certificate to the security directory of
 * open PE file.
 */

BOOL WINAPI ImageAddCertificate(
  HANDLE FileHandle, LPWIN_CERTIFICATE Certificate, PDWORD Index)
{
    DWORD size = 0, count = 0, offset = 0, sd_VirtualAddr = 0, index = 0;
    WIN_CERTIFICATE hdr;
    const size_t cert_hdr_size = sizeof hdr - sizeof hdr.bCertificate;
    BOOL r;

    TRACE("(%p, %p, %p)\n", FileHandle, Certificate, Index);

    r = IMAGEHLP_GetSecurityDirOffset(FileHandle, &sd_VirtualAddr, &size);

    /* If we've already got a security directory, find the end of it */
    if ((r) && (sd_VirtualAddr != 0))
    {
        /* Check if the security directory is at the end of the file.
           If not, we should probably relocate it. */
        if (GetFileSize(FileHandle, NULL) != sd_VirtualAddr + size)
        {
            FIXME("Security directory already present but not located at EOF, not adding certificate\n");

            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;
        }

        while (offset < size)
        {
            /* read the length of the current certificate */
            count = SetFilePointer (FileHandle, sd_VirtualAddr + offset,
                                     NULL, FILE_BEGIN);

            if (count == INVALID_SET_FILE_POINTER)
                return FALSE;

            r = ReadFile(FileHandle, &hdr, cert_hdr_size, &count, NULL);

            if (!r)
                return FALSE;

            if (count != cert_hdr_size)
                return FALSE;

            /* check the certificate is not too big or too small */
            if (hdr.dwLength < cert_hdr_size)
                return FALSE;

            if (hdr.dwLength > (size-offset))
                return FALSE;

            /* next certificate */
            offset += hdr.dwLength;

            /* padded out to the nearest 8-byte boundary */
            if (hdr.dwLength % 8)
                offset += 8 - (hdr.dwLength % 8);

            index++;
        }

        count = SetFilePointer (FileHandle, sd_VirtualAddr + offset, NULL, FILE_BEGIN);

        if (count == INVALID_SET_FILE_POINTER)
            return FALSE;
    }
    else
    {
        sd_VirtualAddr = SetFilePointer(FileHandle, 0, NULL, FILE_END);

        if (sd_VirtualAddr == INVALID_SET_FILE_POINTER)
            return FALSE;
#ifdef __REACTOS__

        if (sd_VirtualAddr % 8)
        {
            static const BYTE padding[8];

            if (!WriteFile(FileHandle, padding, 8 - (sd_VirtualAddr % 8), &count, NULL))
                return FALSE;
            sd_VirtualAddr += count;
        }
#endif
    }

    /* Write the certificate to the file */
    r = WriteFile(FileHandle, Certificate, Certificate->dwLength, &count, NULL);

    if (!r)
        return FALSE;

    /* Pad out if necessary */
    if (Certificate->dwLength % 8)
    {
        char null[8];

        ZeroMemory(null, 8);
        WriteFile(FileHandle, null, 8 - (Certificate->dwLength % 8), &count, NULL);

        size += 8 - (Certificate->dwLength % 8);
    }

    size += Certificate->dwLength;

    /* Update the security directory offset and size */
    if (!IMAGEHLP_SetSecurityDirOffset(FileHandle, sd_VirtualAddr, size))
        return FALSE;

    if (!IMAGEHLP_RecalculateChecksum(FileHandle))
        return FALSE;

    if(Index)
        *Index = index;
    return TRUE;
}

/***********************************************************************
 *		ImageEnumerateCertificates (IMAGEHLP.@)
 */
BOOL WINAPI ImageEnumerateCertificates(
    HANDLE handle, WORD TypeFilter, PDWORD CertificateCount,
    PDWORD Indices, DWORD IndexCount)
{
    DWORD size, count, offset, sd_VirtualAddr, index;
    WIN_CERTIFICATE hdr;
    const size_t cert_hdr_size = sizeof hdr - sizeof hdr.bCertificate;
    BOOL r;

    TRACE("%p %hd %p %p %ld\n",
           handle, TypeFilter, CertificateCount, Indices, IndexCount);

    r = IMAGEHLP_GetSecurityDirOffset( handle, &sd_VirtualAddr, &size );
    if( !r )
        return FALSE;

    offset = 0;
    index = 0;
    *CertificateCount = 0;
    while( offset < size )
    {
        /* read the length of the current certificate */
        count = SetFilePointer( handle, sd_VirtualAddr + offset,
                                 NULL, FILE_BEGIN );
        if( count == INVALID_SET_FILE_POINTER )
            return FALSE;
        r = ReadFile( handle, &hdr, cert_hdr_size, &count, NULL );
        if( !r )
            return FALSE;
        if( count != cert_hdr_size )
            return FALSE;

        TRACE("Size = %08lx  id = %08hx\n",
               hdr.dwLength, hdr.wCertificateType );

        /* check the certificate is not too big or too small */
        if( hdr.dwLength < cert_hdr_size )
            return FALSE;
        if( hdr.dwLength > (size-offset) )
            return FALSE;
       
        if( (TypeFilter == CERT_SECTION_TYPE_ANY) ||
            (TypeFilter == hdr.wCertificateType) )
        {
            (*CertificateCount)++;
            if(Indices && *CertificateCount <= IndexCount)
                *Indices++ = index;
        }

        /* next certificate */
        offset += hdr.dwLength;

        /* padded out to the nearest 8-byte boundary */
        if (hdr.dwLength % 8)
            offset += 8 - (hdr.dwLength % 8);

        index++;
    }

    return TRUE;
}

/***********************************************************************
 *		ImageGetCertificateData (IMAGEHLP.@)
 *
 *  FIXME: not sure that I'm dealing with the Index the right way
 */
BOOL WINAPI ImageGetCertificateData(
                HANDLE handle, DWORD Index,
                LPWIN_CERTIFICATE Certificate, PDWORD RequiredLength)
{
    DWORD r, offset, ofs, size, count;

    TRACE("%p %ld %p %p\n", handle, Index, Certificate, RequiredLength);

    if( !RequiredLength)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }

    if( !IMAGEHLP_GetCertificateOffset( handle, Index, &ofs, &size ) )
        return FALSE;

    if( *RequiredLength < size )
    {
        *RequiredLength = size;
        SetLastError( ERROR_INSUFFICIENT_BUFFER );
        return FALSE;
    }

    if( !Certificate )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }

    *RequiredLength = size;

    offset = SetFilePointer( handle, ofs, NULL, FILE_BEGIN );
    if( offset == INVALID_SET_FILE_POINTER )
        return FALSE;

    r = ReadFile( handle, Certificate, size, &count, NULL );
    if( !r )
        return FALSE;
    if( count != size )
        return FALSE;

    TRACE("OK\n");
    SetLastError( NO_ERROR );

    return TRUE;
}

/***********************************************************************
 *		ImageGetCertificateHeader (IMAGEHLP.@)
 */
BOOL WINAPI ImageGetCertificateHeader(
    HANDLE handle, DWORD index, LPWIN_CERTIFICATE pCert)
{
    DWORD r, offset, ofs, size, count;
    const size_t cert_hdr_size = sizeof *pCert - sizeof pCert->bCertificate;

    TRACE("%p %ld %p\n", handle, index, pCert);

    if( !IMAGEHLP_GetCertificateOffset( handle, index, &ofs, &size ) )
        return FALSE;

    if( size < cert_hdr_size )
        return FALSE;

    offset = SetFilePointer( handle, ofs, NULL, FILE_BEGIN );
    if( offset == INVALID_SET_FILE_POINTER )
        return FALSE;

    r = ReadFile( handle, pCert, cert_hdr_size, &count, NULL );
    if( !r )
        return FALSE;
    if( count != cert_hdr_size )
        return FALSE;

    TRACE("OK\n");

    return TRUE;
}

/* Finds the section named section in the array of IMAGE_SECTION_HEADERs hdr.  If
 * found, returns the offset to the section.  Otherwise returns 0.  If the section
 * is found, optionally returns the size of the section (in size) and the base
 * address of the section (in base.)
 */
static DWORD IMAGEHLP_GetSectionOffset( IMAGE_SECTION_HEADER *hdr,
    DWORD num_sections, LPCSTR section, PDWORD size, PDWORD base )
{
    DWORD i, offset = 0;

    for( i = 0; !offset && i < num_sections; i++, hdr++ )
    {
        if( !memcmp( hdr->Name, section, strlen(section) ) )
        {
            offset = hdr->PointerToRawData;
            if( size )
                *size = hdr->SizeOfRawData;
            if( base )
                *base = hdr->VirtualAddress;
        }
    }
    return offset;
}

/* Calls DigestFunction e bytes at offset offset from the file mapped at map.
 * Returns the return value of DigestFunction, or FALSE if the data is not available.
 */
static BOOL IMAGEHLP_ReportSectionFromOffset( DWORD offset, DWORD size,
    BYTE *map, DWORD fileSize, DIGEST_FUNCTION DigestFunction, DIGEST_HANDLE DigestHandle )
{
    if( offset > fileSize || size > fileSize - offset )
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return DigestFunction( DigestHandle, map + offset, size );
}

/* Finds the section named section among the IMAGE_SECTION_HEADERs in
 * section_headers and calls DigestFunction for this section.  Returns
 * the return value from DigestFunction, or FALSE if the data could not be read.
 */
static BOOL IMAGEHLP_ReportSection( IMAGE_SECTION_HEADER *section_headers,
    DWORD num_sections, LPCSTR section, BYTE *map, DWORD fileSize,
    DIGEST_FUNCTION DigestFunction, DIGEST_HANDLE DigestHandle )
{
    DWORD offset, size = 0;

    offset = IMAGEHLP_GetSectionOffset( section_headers, num_sections, section,
        &size, NULL );
    if( !offset )
        return TRUE;
    return IMAGEHLP_ReportSectionFromOffset( offset, size, map, fileSize,
            DigestFunction, DigestHandle );
}

/* Calls DigestFunction for all sections with the IMAGE_SCN_CNT_CODE flag set.
 * Returns the return value from * DigestFunction, or FALSE if a section could not be read.
 */
static BOOL IMAGEHLP_ReportCodeSections( IMAGE_SECTION_HEADER *hdr, DWORD num_sections,
    BYTE *map, DWORD fileSize, DIGEST_FUNCTION DigestFunction, DIGEST_HANDLE DigestHandle )
{
    DWORD i;
    BOOL ret = TRUE;

    for( i = 0; ret && i < num_sections; i++, hdr++ )
    {
        if( hdr->Characteristics & IMAGE_SCN_CNT_CODE )
            ret = IMAGEHLP_ReportSectionFromOffset( hdr->PointerToRawData,
                hdr->SizeOfRawData, map, fileSize, DigestFunction, DigestHandle );
    }
    return ret;
}

/* Reports the import section from the file FileHandle.  If
 * CERT_PE_IMAGE_DIGEST_ALL_IMPORT_INFO is set in DigestLevel, reports the entire
 * import section.
 * FIXME: if it's not set, the function currently fails.
 */
struct digest_exclusion
{
    DWORD start, end;
};

static int __cdecl IMAGEHLP_CompareExclusions(const void *left, const void *right)
{
    const struct digest_exclusion *a = left, *b = right;

    return (a->start > b->start) - (a->start < b->start);
}

static BOOL IMAGEHLP_ReportImportSection( IMAGE_NT_HEADERS *nt_hdr, IMAGE_SECTION_HEADER *hdr,
    DWORD num_sections, BYTE *map, DWORD fileSize, DWORD DigestLevel,
    DIGEST_FUNCTION DigestFunction, DIGEST_HANDLE DigestHandle )
{
    BOOL ret = FALSE;
    DWORD offset, size, base;

    /* Get import data */
    offset = IMAGEHLP_GetSectionOffset( hdr, num_sections, ".idata", &size,
        &base );
    if( !offset )
        return TRUE;

    /* If CERT_PE_IMAGE_DIGEST_ALL_IMPORT_INFO is set, the entire
     * section is reported.  Otherwise, the debug info section is
     * decoded and reported piecemeal.  See tests.  However, I haven't been
     * able to figure out how the native implementation decides which values
     * to report.  Either it's buggy or my understanding is flawed.
     */
    if( DigestLevel & CERT_PE_IMAGE_DIGEST_ALL_IMPORT_INFO )
        ret = IMAGEHLP_ReportSectionFromOffset( offset, size, map, fileSize,
                DigestFunction, DigestHandle );
    else
    {
        IMAGE_IMPORT_DESCRIPTOR *imports;
        struct digest_exclusion *excluded;
        DWORD import_size, count, i, cursor, thunk_size;
        BYTE *thunk, *end;

        if (offset > fileSize || size > fileSize - offset)
            goto invalid_parameter;
        imports = ImageDirectoryEntryToData(map, FALSE, IMAGE_DIRECTORY_ENTRY_IMPORT, &import_size);
        if (!imports)
            return IMAGEHLP_ReportSectionFromOffset(offset, size, map, fileSize,
                                                   DigestFunction, DigestHandle);
        if ((BYTE *)imports < map + offset || (BYTE *)imports > map + offset + size ||
            import_size > map + offset + size - (BYTE *)imports)
            goto invalid_parameter;
        for (count = 0; count < import_size / sizeof(*imports); ++count)
            if (!imports[count].Name) break;
        if (count == import_size / sizeof(*imports))
            goto invalid_parameter;
        excluded = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)count * 2 * sizeof(*excluded));
        if (!excluded)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        thunk_size = nt_hdr->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ?
            sizeof(IMAGE_THUNK_DATA64) : sizeof(IMAGE_THUNK_DATA32);
        end = map + offset + size;
        for (i = 0; i < count; ++i)
        {
            ULONGLONG value = 1;

            excluded[2 * i].start = (BYTE *)&imports[i].TimeDateStamp - map;
            excluded[2 * i].end = (BYTE *)&imports[i].Name - map;
            thunk = ImageRvaToVa(nt_hdr, map, imports[i].FirstThunk, NULL);
            if (!thunk || thunk < map + offset || thunk > end)
                break;
            excluded[2 * i + 1].start = thunk - map;
            do
            {
                if (end - thunk < thunk_size) break;
                value = 0;
                memcpy(&value, thunk, thunk_size);
                thunk += thunk_size;
            } while (value);
            if (value)
                break;
            excluded[2 * i + 1].end = thunk - map;
        }
        if (i != count)
        {
            HeapFree(GetProcessHeap(), 0, excluded);
            goto invalid_parameter;
        }
        qsort(excluded, count * 2, sizeof(*excluded), IMAGEHLP_CompareExclusions);
        cursor = offset;
        ret = TRUE;
        for (i = 0; ret && i < count * 2; ++i)
        {
            if (excluded[i].start > cursor)
                ret = DigestFunction(DigestHandle, map + cursor, excluded[i].start - cursor);
            if (excluded[i].end > cursor)
                cursor = excluded[i].end;
        }
        if (ret && cursor < offset + size)
            ret = DigestFunction(DigestHandle, map + cursor, offset + size - cursor);
        HeapFree(GetProcessHeap(), 0, excluded);
    }

    return ret;

invalid_parameter:
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

/***********************************************************************
 *		ImageGetDigestStream (IMAGEHLP.@)
 *
 * Gets a stream of bytes from a PE file over which a hash might be computed to
 * verify that the image has not changed.  Useful for creating a certificate to
 * be added to the file with ImageAddCertificate.
 *
 * PARAMS
 *  FileHandle     [In] File for which to return a stream.
 *  DigestLevel    [In] Flags to control which portions of the file to return.
 *                      0 is allowed, as is any combination of:
 *                       CERT_PE_IMAGE_DIGEST_ALL_IMPORT_INFO: reports the entire
 *                        import section rather than selected portions of it.
 *                       CERT_PE_IMAGE_DIGEST_DEBUG_INFO: reports the debug section.
 *                       CERT_PE_IMAGE_DIGEST_RESOURCES: reports the resources
                          section.
 *  DigestFunction [In] Callback function.
 *  DigestHandle   [In] Handle passed as first parameter to DigestFunction.
 *
 * RETURNS
 *  TRUE if successful.
 *  FALSE if unsuccessful.  GetLastError returns more about the error.
 *
 * NOTES
 *  Reports data in the following order:
 *  1. The file headers are reported first
 *  2. Any code sections are reported next.
 *  3. The data (".data" and ".rdata") sections are reported next.
 *  4. The import section is reported next.
 *  5. If CERT_PE_IMAGE_DIGEST_DEBUG_INFO is set in DigestLevel, the debug section is
 *     reported next.
 *  6. If CERT_PE_IMAGE_DIGEST_RESOURCES is set in DigestLevel, the resources section
 *     is reported next.
 *
 * BUGS
 *  CERT_PE_IMAGE_DIGEST_ALL_IMPORT_INFO must be specified, returns an error if not.
 */
BOOL WINAPI ImageGetDigestStream(
  HANDLE FileHandle, DWORD DigestLevel,
  DIGEST_FUNCTION DigestFunction, DIGEST_HANDLE DigestHandle)
{
    DWORD error = 0;
    BOOL ret = FALSE;
    DWORD offset, size, num_sections, fileSize;
    HANDLE hMap = INVALID_HANDLE_VALUE;
    BYTE *map = NULL;
    IMAGE_DOS_HEADER *dos_hdr;
    IMAGE_NT_HEADERS *nt_hdr;
    IMAGE_SECTION_HEADER *section_headers;

    TRACE("(%p, %ld, %p, %p)\n", FileHandle, DigestLevel, DigestFunction,
        DigestHandle);

    /* Get the file size */
    if( !FileHandle || !DigestFunction )
        goto invalid_parameter;
    fileSize = GetFileSize( FileHandle, NULL );
    if(fileSize == INVALID_FILE_SIZE )
        goto invalid_parameter;

    /* map file */
    hMap = CreateFileMappingW( FileHandle, NULL, PAGE_READONLY, 0, 0, NULL );
    if( !hMap )
        goto invalid_parameter;
    map = MapViewOfFile( hMap, FILE_MAP_COPY, 0, 0, 0 );
    if( !map )
        goto invalid_parameter;

    /* Read the file header */
    if( fileSize < sizeof(IMAGE_DOS_HEADER) )
        goto invalid_parameter;
    dos_hdr = (IMAGE_DOS_HEADER *)map;

    if( dos_hdr->e_magic != IMAGE_DOS_SIGNATURE )
        goto invalid_parameter;
    offset = dos_hdr->e_lfanew;
    if( !offset || offset > fileSize )
        goto invalid_parameter;
    ret = DigestFunction( DigestHandle, map, offset );
    if( !ret )
        goto end;

    /* Read the NT header */
    if( sizeof(IMAGE_NT_HEADERS32) > fileSize - offset )
        goto invalid_parameter;
    nt_hdr = (IMAGE_NT_HEADERS *)(map + offset);
    if( nt_hdr->Signature != IMAGE_NT_SIGNATURE )
        goto invalid_parameter;
    if( nt_hdr->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
        nt_hdr->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC )
        goto invalid_parameter;
    size = sizeof(nt_hdr->Signature) + sizeof(nt_hdr->FileHeader) +
        nt_hdr->FileHeader.SizeOfOptionalHeader;
    if( size > fileSize - offset ||
        (nt_hdr->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
         nt_hdr->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32)) ||
        (nt_hdr->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
         nt_hdr->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64)) )
        goto invalid_parameter;
    /* It's clear why the checksum is cleared, but why only these size headers?
     */
    nt_hdr->OptionalHeader.SizeOfInitializedData = 0;
    nt_hdr->OptionalHeader.SizeOfImage = 0;
    nt_hdr->OptionalHeader.CheckSum = 0;
    ret = DigestFunction( DigestHandle, map + offset, size );
    if( !ret )
        goto end;

    /* Read the section headers */
    offset += size;
    num_sections = nt_hdr->FileHeader.NumberOfSections;
    size = num_sections * sizeof(IMAGE_SECTION_HEADER);
    if( size > fileSize - offset )
        goto invalid_parameter;
    ret = DigestFunction( DigestHandle, map + offset, size );
    if( !ret )
        goto end;

    section_headers = (IMAGE_SECTION_HEADER *)(map + offset);
    ret = IMAGEHLP_ReportCodeSections( section_headers, num_sections,
        map, fileSize, DigestFunction, DigestHandle ) &&
        IMAGEHLP_ReportSection( section_headers, num_sections, ".data",
        map, fileSize, DigestFunction, DigestHandle ) &&
        IMAGEHLP_ReportSection( section_headers, num_sections, ".rdata",
        map, fileSize, DigestFunction, DigestHandle ) &&
        IMAGEHLP_ReportImportSection( nt_hdr, section_headers, num_sections,
        map, fileSize, DigestLevel, DigestFunction, DigestHandle );
    if( ret && (DigestLevel & CERT_PE_IMAGE_DIGEST_RESOURCES) )
        ret = IMAGEHLP_ReportSection( section_headers, num_sections, ".rsrc",
            map, fileSize, DigestFunction, DigestHandle );
    if( ret && (DigestLevel & CERT_PE_IMAGE_DIGEST_DEBUG_INFO) )
        ret = IMAGEHLP_ReportSection( section_headers, num_sections, ".debug",
            map, fileSize, DigestFunction, DigestHandle );

end:
    if( map )
        UnmapViewOfFile( map );
    if( hMap && hMap != INVALID_HANDLE_VALUE )
        CloseHandle( hMap );
    if( error )
        SetLastError(error);
    return ret;

invalid_parameter:
    error = ERROR_INVALID_PARAMETER;
    ret = FALSE;
    goto end;
}

/***********************************************************************
 *		ImageRemoveCertificate (IMAGEHLP.@)
 */
BOOL WINAPI ImageRemoveCertificate(HANDLE FileHandle, DWORD Index)
{
    DWORD size = 0, count = 0, sd_VirtualAddr = 0, offset = 0;
    DWORD data_size = 0, cert_size = 0, cert_size_padded = 0, ret = 0;
    LPVOID cert_data;
    BOOL r;

    TRACE("(%p, %ld)\n", FileHandle, Index);

    r = ImageEnumerateCertificates(FileHandle, CERT_SECTION_TYPE_ANY, &count, NULL, 0);

    if ((!r) || (count == 0))
        return FALSE;

    if ((!IMAGEHLP_GetSecurityDirOffset(FileHandle, &sd_VirtualAddr, &size)) ||
        (!IMAGEHLP_GetCertificateOffset(FileHandle, Index, &offset, &cert_size)))
        return FALSE;

    /* Ignore any padding we have, too */
    if (cert_size % 8)
        cert_size_padded = cert_size + (8 - (cert_size % 8));
    else
        cert_size_padded = cert_size;

    data_size = size - (offset - sd_VirtualAddr) - cert_size_padded;

    if (data_size == 0)
    {
        ret = SetFilePointer(FileHandle, sd_VirtualAddr, NULL, FILE_BEGIN);

        if (ret == INVALID_SET_FILE_POINTER)
            return FALSE;
    }
    else
    {
        cert_data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, data_size);

        if (!cert_data)
            return FALSE;

        ret = SetFilePointer(FileHandle, offset + cert_size_padded, NULL, FILE_BEGIN);

        if (ret == INVALID_SET_FILE_POINTER)
            goto error;

        /* Read any subsequent certificates */
        r = ReadFile(FileHandle, cert_data, data_size, &count, NULL);

        if ((!r) || (count != data_size))
            goto error;

        SetFilePointer(FileHandle, offset, NULL, FILE_BEGIN);

        /* Write them one index back */
        r = WriteFile(FileHandle, cert_data, data_size, &count, NULL);

        if ((!r) || (count != data_size))
            goto error;

        HeapFree(GetProcessHeap(), 0, cert_data);
    }

    /* If security directory is at end of file, trim the file */
    if (GetFileSize(FileHandle, NULL) == sd_VirtualAddr + size)
        SetEndOfFile(FileHandle);

    if (count == 1)
        r = IMAGEHLP_SetSecurityDirOffset(FileHandle, 0, 0);
    else
        r = IMAGEHLP_SetSecurityDirOffset(FileHandle, sd_VirtualAddr, size - cert_size_padded);

    if (!r)
        return FALSE;

    if (!IMAGEHLP_RecalculateChecksum(FileHandle))
        return FALSE;

    return TRUE;

error:
    HeapFree(GetProcessHeap(), 0, cert_data);
    return FALSE;
}
