/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UTF-8 counted string routines
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl_vista.h>

VOID
NTAPI
RtlInitUTF8String(OUT PUTF8_STRING DestinationString,
                  IN PCSZ SourceString)
{
    RtlInitAnsiString(DestinationString, SourceString);
}

NTSTATUS
NTAPI
RtlUTF8StringToUnicodeString(
    IN OUT PUNICODE_STRING DestinationString,
    IN PUTF8_STRING SourceString,
    IN BOOLEAN AllocateDestinationString)
{
    NTSTATUS Status;
    ULONG Length;
    ULONG Written;

    PAGED_CODE_RTL();

    Status = RtlUTF8ToUnicodeN(NULL, 0, &Length, SourceString->Buffer, SourceString->Length);
    if (!NT_SUCCESS(Status)) return Status;
    if (Length + sizeof(WCHAR) > MAXUSHORT) return STATUS_INVALID_PARAMETER_2;
    DestinationString->Length = (USHORT)Length;

    if (AllocateDestinationString)
    {
        DestinationString->Buffer = RtlpAllocateStringMemory(Length + sizeof(WCHAR), TAG_USTR);
        DestinationString->MaximumLength = (USHORT)(Length + sizeof(WCHAR));
        if (!DestinationString->Buffer) return STATUS_NO_MEMORY;
    }
    else if (DestinationString->Length >= DestinationString->MaximumLength)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    Status = RtlUTF8ToUnicodeN(DestinationString->Buffer,
                               DestinationString->Length,
                               &Written,
                               SourceString->Buffer,
                               SourceString->Length);
    if (!NT_SUCCESS(Status))
    {
        if (AllocateDestinationString)
        {
            RtlpFreeStringMemory(DestinationString->Buffer, TAG_USTR);
            DestinationString->Buffer = NULL;
        }
        return Status;
    }

    DestinationString->Buffer[Written / sizeof(WCHAR)] = UNICODE_NULL;
    return Status;
}
