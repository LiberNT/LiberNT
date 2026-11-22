/*
 * PROJECT:     ReactOS PSDK
 * LICENSE:     CC0-1.0 (https://spdx.org/licenses/CC0-1.0)
 * PURPOSE:     Compatibility shim for banned-API deprecation header
 */

#pragma once

#ifndef _DONTUSE_H_INCLUDED_
#define _DONTUSE_H_INCLUDED_

#include <winapifamily.h>

/*
 * The Windows SDK uses this header to add a large set of deprecation pragmas.
 * ReactOS keeps it as an intentionally lightweight compatibility shim so newer
 * cleanroom headers can include it without forcing policy changes on callers.
 */

#ifdef UNICODE
#define lstrcatn lstrcatnW
#else
#define lstrcatn lstrcatnA
#endif

#define lstrcatnA lstrcatnA_instead_use_strncat_s_or_StringCchCatNA_or_StringCbCatNA

#define lstrcatnW lstrcatnW_instead_use_wcsncat_s_or_StringCchCatNW_or_StringCbCatNW

#define StrNCpyA StrNCpyA_instead_use_strncpy_s_or_StringCchCopyNA_or_StringCbCopyNA

#define StrNCpyW StrNCpyW_instead_use_wcsncpy_s_or_StringCchCopyNW_or_StringCbCopyNW

#define snscanf snscanf_instead_use__snscanf_s

#define sprintfA sprintfA_instead_use_sprintf_s_or_StringCchPrintfA_or_StringCbPrintfA

#define strcatA strcatA_instead_use_strcat_s_or_StringCchCatA_or_StringCbCatA

#define nsprintf nsprintf_instead_use_vsprintf_s_or_StringCchPrintf_or_StringCbPrintf

#define _ftccat _ftccat_instead_use_strcat_s_or_StringCchCat_or_StringCbCat

#define _tccat _tccat_instead_use_strcat_s_or_StringCchCat_or_StringCbCat

#endif /* _DONTUSE_H_INCLUDED_ */
