/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Encode and decode assigned memory and I/O resources
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <ntoskrnl.h>

ULONGLONG
NTAPI
RtlCmDecodeMemIoResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
    _Out_opt_ PULONGLONG Start)
{
    ULONG Shift = 0;

    switch (Descriptor->Type)
    {
        case CmResourceTypePort:
        case CmResourceTypeMemory:
            break;

        case CmResourceTypeMemoryLarge:
            switch (Descriptor->Flags & CM_RESOURCE_MEMORY_LARGE)
            {
                case CM_RESOURCE_MEMORY_LARGE_40: Shift = 8; break;
                case CM_RESOURCE_MEMORY_LARGE_48: Shift = 16; break;
                case CM_RESOURCE_MEMORY_LARGE_64: Shift = 32; break;
                default: return 0;
            }
            break;

        default:
            return 0;
    }

    if (Start != NULL)
        *Start = Descriptor->u.Memory.Start.QuadPart;
    return (ULONGLONG)Descriptor->u.Memory.Length << Shift;
}

NTSTATUS
NTAPI
RtlCmEncodeMemIoResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
    _In_ UCHAR Type,
    _In_ ULONGLONG Length,
    _In_ ULONGLONG Start)
{
    USHORT Flags = 0;
    ULONG Shift = 0;

    if ((Type != CmResourceTypePort &&
         Type != CmResourceTypeMemory &&
         Type != CmResourceTypeMemoryLarge) ||
        (Type == CmResourceTypePort && Length > MAXULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Descriptor->u.Memory.Start.QuadPart = Start;
    if (Type == CmResourceTypePort)
    {
        Descriptor->Type = CmResourceTypePort;
        Descriptor->u.Port.Length = (ULONG)Length;
        return STATUS_SUCCESS;
    }

    /* These fields change even when the memory length cannot be encoded. */
    Descriptor->Flags &= ~CM_RESOURCE_MEMORY_LARGE;
    if (Length > MAXULONG)
    {
        if (Length <= CM_RESOURCE_MEMORY_LARGE_40_MAXLEN && !(Length & 0xff))
        {
            Shift = 8;
            Flags = CM_RESOURCE_MEMORY_LARGE_40;
        }
        else if (Length <= CM_RESOURCE_MEMORY_LARGE_48_MAXLEN && !(Length & 0xffff))
        {
            Shift = 16;
            Flags = CM_RESOURCE_MEMORY_LARGE_48;
        }
        else if (!(Length & MAXULONG))
        {
            Shift = 32;
            Flags = CM_RESOURCE_MEMORY_LARGE_64;
        }
        else
        {
            return STATUS_UNSUCCESSFUL;
        }
    }

    Descriptor->Type = Shift ? CmResourceTypeMemoryLarge : CmResourceTypeMemory;
    Descriptor->Flags |= Flags;
    Descriptor->u.Memory.Length = (ULONG)(Length >> Shift);
    return STATUS_SUCCESS;
}

static
ULONG
RtlpMemIoLargeShift(
    _In_ USHORT Flags)
{
    switch (Flags & CM_RESOURCE_MEMORY_LARGE)
    {
        case CM_RESOURCE_MEMORY_LARGE_40: return 8;
        case CM_RESOURCE_MEMORY_LARGE_48: return 16;
        case CM_RESOURCE_MEMORY_LARGE_64: return 32;
        default: return 0;
    }
}

ULONGLONG
NTAPI
RtlIoDecodeMemIoResource(
    _In_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _Out_opt_ PULONGLONG Alignment,
    _Out_opt_ PULONGLONG MinimumAddress,
    _Out_opt_ PULONGLONG MaximumAddress)
{
    ULONG Shift = 0;

    switch (Descriptor->Type)
    {
        case CmResourceTypePort:
        case CmResourceTypeMemory:
            break;

        case CmResourceTypeMemoryLarge:
            Shift = RtlpMemIoLargeShift(Descriptor->Flags);
            if (!Shift)
                return 0;
            break;

        default:
            return 0;
    }

    if (Alignment != NULL)
        *Alignment = (ULONGLONG)Descriptor->u.Memory.Alignment << Shift;
    if (MinimumAddress != NULL)
        *MinimumAddress = Descriptor->u.Memory.MinimumAddress.QuadPart;
    if (MaximumAddress != NULL)
        *MaximumAddress = Descriptor->u.Memory.MaximumAddress.QuadPart;
    return (ULONGLONG)Descriptor->u.Memory.Length << Shift;
}

NTSTATUS
NTAPI
RtlIoEncodeMemIoResource(
    _In_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ UCHAR Type,
    _In_ ULONGLONG Length,
    _In_ ULONGLONG Alignment,
    _In_ ULONGLONG MinimumAddress,
    _In_ ULONGLONG MaximumAddress)
{
    static const struct
    {
        ULONG Shift;
        USHORT Flags;
    } Encodings[] =
    {
        { 8, CM_RESOURCE_MEMORY_LARGE_40 },
        { 16, CM_RESOURCE_MEMORY_LARGE_48 },
        { 32, CM_RESOURCE_MEMORY_LARGE_64 },
    };
    ULONGLONG Mask;
    USHORT Flags = 0;
    ULONG Shift = 0, i;

    if ((Type != CmResourceTypePort &&
         Type != CmResourceTypeMemory &&
         Type != CmResourceTypeMemoryLarge) ||
        (Type == CmResourceTypePort && (Length > MAXULONG || Alignment > MAXULONG)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Descriptor->u.Memory.MinimumAddress.QuadPart = MinimumAddress;
    Descriptor->u.Memory.MaximumAddress.QuadPart = MaximumAddress;
    if (Type == CmResourceTypePort)
    {
        Descriptor->Type = CmResourceTypePort;
        Descriptor->u.Port.Length = (ULONG)Length;
        Descriptor->u.Port.Alignment = (ULONG)Alignment;
        return STATUS_SUCCESS;
    }

    Descriptor->Flags &= ~CM_RESOURCE_MEMORY_LARGE;
    if (Length > MAXULONG || Alignment > MAXULONG)
    {
        for (i = 0; i < RTL_NUMBER_OF(Encodings); i++)
        {
            Mask = (1ULL << Encodings[i].Shift) - 1;
            if (!(Length & Mask) && !(Alignment & Mask) &&
                (Length >> Encodings[i].Shift) <= MAXULONG &&
                (Alignment >> Encodings[i].Shift) <= MAXULONG)
            {
                Shift = Encodings[i].Shift;
                Flags = Encodings[i].Flags;
                break;
            }
        }
        if (!Shift)
            return STATUS_UNSUCCESSFUL;
    }

    Descriptor->Type = Shift ? CmResourceTypeMemoryLarge : CmResourceTypeMemory;
    Descriptor->Flags |= Flags;
    Descriptor->u.Memory.Length = (ULONG)(Length >> Shift);
    Descriptor->u.Memory.Alignment = (ULONG)(Alignment >> Shift);
    return STATUS_SUCCESS;
}
