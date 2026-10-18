/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     API set presence query
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winternl.h>

BOOL
WINAPI
IsApiSetImplemented(
    _In_ PCSTR Contract)
{
    UNICODE_STRING Name;
    BOOLEAN Present;
    NTSTATUS Status;

    if (!RtlCreateUnicodeStringFromAsciiz(&Name, Contract))
        return FALSE;

    Status = ApiSetQueryApiSetPresence(&Name, &Present);
    RtlFreeUnicodeString(&Name);

    return NT_SUCCESS(Status) && Present;
}
