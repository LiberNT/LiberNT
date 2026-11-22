/*
 * Definitions for the System File Checker (Windows File Protection)
 *
 * Copyright 2006 Detlef Riekenberg
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

#ifndef __WINE_SFC_H
#define __WINE_SFC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Types */

typedef struct _PROTECTED_FILE_DATA {
 WCHAR FileName[MAX_PATH];
 DWORD FileNumber;
} PROTECTED_FILE_DATA, *PPROTECTED_FILE_DATA;

/* Functions */

BOOL WINAPI SfcGetNextProtectedFile(HANDLE, PPROTECTED_FILE_DATA);
BOOL WINAPI SfcIsFileProtected(HANDLE, LPCWSTR);
BOOL WINAPI SfcIsKeyProtected(HKEY, LPCWSTR, REGSAM);
BOOL WINAPI SfpVerifyFile(LPCSTR, LPSTR, DWORD);

#ifdef __cplusplus
}
#endif

#ifndef SFC_DISABLE_ASK
#define SFC_DISABLE_ASK 1
#endif
#ifndef SFC_DISABLE_NOPOPUPS
#define SFC_DISABLE_NOPOPUPS 4
#endif
#ifndef SFC_DISABLE_NORMAL
#define SFC_DISABLE_NORMAL 0
#endif
#ifndef SFC_DISABLE_ONCE
#define SFC_DISABLE_ONCE 2
#endif
#ifndef SFC_DISABLE_SETUP
#define SFC_DISABLE_SETUP 3
#endif
#ifndef SFC_QUOTA_DEFAULT
#define SFC_QUOTA_DEFAULT 50
#endif
#ifndef SFC_SCAN_ALWAYS
#define SFC_SCAN_ALWAYS 1
#endif
#ifndef SFC_SCAN_IMMEDIATE
#define SFC_SCAN_IMMEDIATE 3
#endif
#ifndef SFC_SCAN_NORMAL
#define SFC_SCAN_NORMAL 0
#endif
#ifndef SFC_SCAN_ONCE
#define SFC_SCAN_ONCE 2
#endif

#define _SFC_
#define SFC_IDLE_TRIGGER L"WFP_IDLE_TRIGGER"
#define SFC_QUOTA_ALL_FILES ((ULONG)0xFFFFFFFF)

#endif
