/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Show the guest its own system directory under the native name
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "amd64emu.h"

BOOLEAN
EmuRedirectObject(
    _In_ POBJECT_ATTRIBUTES Source,
    _Out_ POBJECT_ATTRIBUTES Redirected,
    _Out_ PUNICODE_STRING Name,
    _Out_writes_bytes_(BufferSize) PWCHAR Buffer,
    _In_ ULONG BufferSize)
{
    FILE_BASIC_INFORMATION Information;
    UNICODE_STRING Original, Rest;
    USHORT Prefix;

    if (!Source || Source->RootDirectory || !Source->ObjectName) return FALSE;

    Original = *Source->ObjectName;
    if (RtlPrefixUnicodeString(&EmuProcess.SystemDirectory, &Original, TRUE))
        Prefix = EmuProcess.SystemDirectory.Length;
    else if (RtlPrefixUnicodeString(&EmuProcess.SystemRootDirectory, &Original, TRUE))
        Prefix = EmuProcess.SystemRootDirectory.Length;
    else
        return FALSE;

    Rest.Buffer = (PWCHAR)((PUCHAR)Original.Buffer + Prefix);
    Rest.Length = Original.Length - Prefix;
    Rest.MaximumLength = Rest.Length;
    if (!Rest.Length) return FALSE;

    RtlInitEmptyUnicodeString(Name, Buffer, (USHORT)BufferSize);
    if (!NT_SUCCESS(RtlAppendUnicodeStringToString(Name, &EmuProcess.GuestDirectory)) ||
        !NT_SUCCESS(RtlAppendUnicodeStringToString(Name, &Rest)))
    {
        return FALSE;
    }

    *Redirected = *Source;
    Redirected->ObjectName = Name;
    return NT_SUCCESS(NtQueryAttributesFile(Redirected, &Information));
}

BOOLEAN
EmuIsKnownDllDirectory(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    static const UNICODE_STRING KnownDlls = RTL_CONSTANT_STRING(L"\\KnownDlls");
    static const UNICODE_STRING KnownDlls32 = RTL_CONSTANT_STRING(L"\\KnownDlls32");

    if (!ObjectAttributes || ObjectAttributes->RootDirectory || !ObjectAttributes->ObjectName) return FALSE;
    return RtlEqualUnicodeString(ObjectAttributes->ObjectName, &KnownDlls, TRUE) ||
           RtlEqualUnicodeString(ObjectAttributes->ObjectName, &KnownDlls32, TRUE);
}
