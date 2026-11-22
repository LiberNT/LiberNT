/*
 * Copyright 2013 André Hentschel
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

#ifndef __WINE_FILEAPI_H
#define __WINE_FILEAPI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _CREATEFILE2_EXTENDED_PARAMETERS {
    DWORD dwSize;
    DWORD dwFileAttributes;
    DWORD dwFileFlags;
    DWORD dwSecurityQosFlags;
    LPSECURITY_ATTRIBUTES lpSecurityAttributes;
    HANDLE hTemplateFile;
} CREATEFILE2_EXTENDED_PARAMETERS, *PCREATEFILE2_EXTENDED_PARAMETERS, *LPCREATEFILE2_EXTENDED_PARAMETERS;

WINBASEAPI HANDLE WINAPI CreateFile2(LPCWSTR,DWORD,DWORD,DWORD,LPCREATEFILE2_EXTENDED_PARAMETERS);

#ifdef __cplusplus
}
#endif

typedef enum DIRECTORY_FLAGS {
    DIRECTORY_FLAGS_NONE = 0,
    DIRECTORY_FLAGS_DISALLOW_PATH_REDIRECTS = 1
} DIRECTORY_FLAGS;

#ifdef __cplusplus
extern "C" {
#endif
DWORD WINAPI GetTempPath2A(DWORD BufferLength, LPSTR Buffer);
DWORD WINAPI GetTempPath2W(DWORD BufferLength, LPWSTR Buffer);
BOOL WINAPI GetVolumeInformationByHandleW(HANDLE hFile, LPWSTR lpVolumeNameBuffer, DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags, LPWSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize);
#ifdef __cplusplus
}
#endif

typedef struct _CREATEFILE3_EXTENDED_PARAMETERS {
  DWORD dwSize;
  DWORD dwFileAttributes;
  DWORD dwFileFlags;
  DWORD dwSecurityQosFlags;
  LPSECURITY_ATTRIBUTES lpSecurityAttributes;
  HANDLE hTemplateFile;
} CREATEFILE3_EXTENDED_PARAMETERS, *PCREATEFILE3_EXTENDED_PARAMETERS, *LPCREATEFILE3_EXTENDED_PARAMETERS;

typedef struct DISK_SPACE_INFORMATION {
  ULONGLONG ActualTotalAllocationUnits;
  ULONGLONG ActualAvailableAllocationUnits;
  ULONGLONG ActualPoolUnavailableAllocationUnits;
  ULONGLONG CallerTotalAllocationUnits;
  ULONGLONG CallerAvailableAllocationUnits;
  ULONGLONG CallerPoolUnavailableAllocationUnits;
  ULONGLONG UsedAllocationUnits;
  ULONGLONG TotalReservedAllocationUnits;
  ULONGLONG VolumeStorageReserveAllocationUnits;
  ULONGLONG AvailableCommittedAllocationUnits;
  ULONGLONG PoolAvailableAllocationUnits;
  DWORD SectorsPerAllocationUnit;
  DWORD BytesPerSector;
} DISK_SPACE_INFORMATION;

#define _APISETFILE_
#ifdef UNICODE
#define CreateDirectory2 CreateDirectory2W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define CreateDirectory2 CreateDirectory2W
#else
#define CreateDirectory2 CreateDirectory2A
#endif
#endif
#endif
#ifdef UNICODE
#define DeleteFile2 DeleteFile2W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DeleteFile2 DeleteFile2W
#else
#define DeleteFile2 DeleteFile2A
#endif
#endif
#endif
#ifdef UNICODE
#define GetDiskSpaceInformation GetDiskSpaceInformationW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define GetDiskSpaceInformation GetDiskSpaceInformationW
#else
#define GetDiskSpaceInformation GetDiskSpaceInformationA
#endif
#endif
#endif
#ifdef UNICODE
#define GetTempPath2 GetTempPath2W
#else
#if (!defined(UNICODE))
#ifdef UNICODE
#define GetTempPath2 GetTempPath2W
#else
#define GetTempPath2 GetTempPath2A
#endif
#endif
#endif
#ifdef UNICODE
#define RemoveDirectory2 RemoveDirectory2W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define RemoveDirectory2 RemoveDirectory2W
#else
#define RemoveDirectory2 RemoveDirectory2A
#endif
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

HRESULT WINAPI GetDiskSpaceInformationA(LPCSTR rootPath, DISK_SPACE_INFORMATION *diskSpaceInfo);
HRESULT WINAPI GetDiskSpaceInformationW(LPCWSTR rootPath, DISK_SPACE_INFORMATION *diskSpaceInfo);

/* TODO: LiberNT does not implement the functions below yet */
BOOL AreShortNamesEnabled(HANDLE Handle, BOOL *Enabled);
HANDLE CreateDirectory2A(LPCSTR lpPathName, DWORD dwDesiredAccess, DWORD dwShareMode, DIRECTORY_FLAGS DirectoryFlags, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
HANDLE CreateDirectory2W(LPCWSTR lpPathName, DWORD dwDesiredAccess, DWORD dwShareMode, DIRECTORY_FLAGS DirectoryFlags, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
HANDLE CreateFile3(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwCreationDisposition, LPCREATEFILE3_EXTENDED_PARAMETERS pCreateExParams);
BOOL DeleteFile2A(LPCSTR lpFileName, DWORD Flags);
BOOL DeleteFile2W(LPCWSTR lpFileName, DWORD Flags);
BOOL RemoveDirectory2A(LPCSTR lpPathName, DIRECTORY_FLAGS DirectoryFlags);
BOOL RemoveDirectory2W(LPCWSTR lpPathName, DIRECTORY_FLAGS DirectoryFlags);

#ifdef __cplusplus
}
#endif

#endif  /* __WINE_FILEAPI_H */
