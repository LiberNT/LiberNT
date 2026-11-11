/*
 * PROJECT:     LiberNT Windows Error Reporting
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading the WER registrations of another process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _WERREMOTE_H_
#define _WERREMOTE_H_

#pragma once

typedef struct _WER_REMOTE_ENTRY
{
    ULONG64 Address;
    ULONG Size;
    ULONG Type;
    ULONG Flags;
    WCHAR Name[MAX_PATH];
} WER_REMOTE_ENTRY, *PWER_REMOTE_ENTRY;

HRESULT
WerRemoteReadRegistration(
    _In_ HANDLE Process,
    _Out_ PULONG64 Address,
    _Out_ PBASE_WER_REGISTRATION Registration);

HRESULT
WerRemoteReadEntries(
    _In_ HANDLE Process,
    _In_ ULONG64 Head,
    _In_ ULONG MaxEntries,
    _Out_ PWER_REMOTE_ENTRY *Entries,
    _Out_ PULONG Count);

#endif /* _WERREMOTE_H_ */
