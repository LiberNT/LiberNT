/*
 * PROJECT:     LiberNT Secure Attention Sequence Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SendSAS
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windef.h>
#include <winbase.h>
#include <sas.h>

VOID
WINAPI
SendSAS(
    _In_ BOOL AsUser)
{
    UNREFERENCED_PARAMETER(AsUser);
    /* TODO: have winlogon raise the SAS for the caller's session; nothing is sent yet */
}
