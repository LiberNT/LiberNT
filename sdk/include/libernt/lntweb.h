/*
 * PROJECT:     LiberNT Web View
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Small HTML/CSS view used by the network sign-in window
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef BOOL (CALLBACK *LNTWEB_FETCH_ROUTINE)(PVOID Context, PCSTR Url, PSTR *Data, SIZE_T *Length);
typedef VOID (CALLBACK *LNTWEB_FREE_ROUTINE)(PVOID Context, PSTR Data);
typedef VOID (CALLBACK *LNTWEB_NAVIGATE_ROUTINE)(PVOID Context, PCSTR Url, PCSTR Body, SIZE_T BodyLength,
                                                 PCSTR ContentType);

typedef struct _LNTWEB_HOST
{
    ULONG Size;
    PVOID Context;
    LNTWEB_FETCH_ROUTINE Fetch;
    LNTWEB_FREE_ROUTINE Free;
    LNTWEB_NAVIGATE_ROUTINE Navigate;
} LNTWEB_HOST, *PLNTWEB_HOST;

typedef HWND (WINAPI *PFN_LNTWEB_CREATE)(HWND Parent, INT Id, const LNTWEB_HOST *Host);
typedef BOOL (WINAPI *PFN_LNTWEB_LOAD)(HWND View, PCSTR Html, SIZE_T Length, PCSTR Url);

HWND WINAPI LntWebCreate(HWND Parent, INT Id, const LNTWEB_HOST *Host);
BOOL WINAPI LntWebLoad(HWND View, PCSTR Html, SIZE_T Length, PCSTR Url);

#ifdef __cplusplus
}
#endif
