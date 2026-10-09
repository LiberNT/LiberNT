/*
 * PROJECT:     LiberNT Applications Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Segmented parallel HTTP downloads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <wininet.h>
#include <sha1.h>

struct DOWNLOAD_SINK
{
    PVOID Context;
    BOOL (*IsCancelled)(PVOID Context);
    LONGLONG volatile *Received;
};

struct DOWNLOAD_RESOURCE
{
    DWORD Status;
    ULONGLONG Length;
    BOOL AcceptsRanges;
    CStringW Url;
    CStringW Validator;
};

HINTERNET
OpenHttpDownload(HINTERNET hSession, LPCWSTR Url, DWORD Flags, DOWNLOAD_RESOURCE &Resource);

DWORD
DownloadToFile(
    HINTERNET hSession,
    HINTERNET hRequest,
    const DOWNLOAD_RESOURCE &Resource,
    DWORD Flags,
    HANDLE hFile,
    PSHA_CTX Hash,
    const DOWNLOAD_SINK &Sink,
    ULONGLONG &Received);

BOOL
VerifyIntegDigest(LPCWSTR lpSHA1Hash, PSHA_CTX Context);
