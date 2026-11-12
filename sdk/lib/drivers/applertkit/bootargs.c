/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot argument records an Apple silicon coprocessor reads from its memory when it starts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applertkit.h>

#define RTKIT_BOOTARG_HEADER_SIZE   (2 * sizeof(ULONG))
#define RTKIT_BOOTARG_MAX_BLOCK     0x10000
#define RTKIT_BOOTARG_TAG           'aBpA'

BOOLEAN
NTAPI
AppleRtkitPatchBootArgumentBlock(
    _Inout_updates_bytes_(Size) PUCHAR Block,
    _In_ ULONG Size,
    _In_reads_(Count) const APPLE_RTKIT_BOOT_ARGUMENT *Patches,
    _In_ ULONG Count,
    _Out_opt_ PULONG Patched)
{
    ULONG Offset, Key, Length, Patch, Done = 0;

    if (Patched)
        *Patched = 0;
    for (Offset = 0; Offset < Size; Offset += RTKIT_BOOTARG_HEADER_SIZE + Length)
    {
        if (Size - Offset < RTKIT_BOOTARG_HEADER_SIZE)
            return FALSE;
        RtlCopyMemory(&Key, Block + Offset, sizeof(Key));
        RtlCopyMemory(&Length, Block + Offset + sizeof(ULONG), sizeof(Length));
        if (Length > Size - Offset - RTKIT_BOOTARG_HEADER_SIZE)
            return FALSE;
        for (Patch = 0; Patch < Count; ++Patch)
        {
            if (Patches[Patch].Key == Key && (!Length || Length > sizeof(ULONG64)))
                return FALSE;
        }
    }

    for (Offset = 0; Offset < Size; Offset += RTKIT_BOOTARG_HEADER_SIZE + Length)
    {
        RtlCopyMemory(&Key, Block + Offset, sizeof(Key));
        RtlCopyMemory(&Length, Block + Offset + sizeof(ULONG), sizeof(Length));
        for (Patch = 0; Patch < Count; ++Patch)
        {
            if (Patches[Patch].Key != Key)
                continue;
            RtlCopyMemory(Block + Offset + RTKIT_BOOTARG_HEADER_SIZE, &Patches[Patch].Value, Length);
            Done++;
            break;
        }
    }
    if (Patched)
        *Patched = Done;
    return TRUE;
}

NTSTATUS
NTAPI
AppleRtkitPatchBootArguments(
    _In_ PUCHAR Memory,
    _In_ ULONG MemorySize,
    _In_ ULONG OffsetRegister,
    _In_ ULONG SizeRegister,
    _In_reads_(Count) const APPLE_RTKIT_BOOT_ARGUMENT *Patches,
    _In_ ULONG Count,
    _In_z_ PCSTR Name)
{
    ULONG Offset, Size, Index, Patched;
    PUCHAR Block;

    ASSERT(Memory && OffsetRegister + sizeof(ULONG) <= MemorySize && SizeRegister + sizeof(ULONG) <= MemorySize);
    Offset = READ_REGISTER_ULONG((volatile ULONG *)(Memory + OffsetRegister));
    Size = READ_REGISTER_ULONG((volatile ULONG *)(Memory + SizeRegister));
    if (!Size || Size > RTKIT_BOOTARG_MAX_BLOCK || Offset > MemorySize || Size > MemorySize - Offset)
    {
        DbgPrint("%s: boot arguments at %lx+%lx are outside the coprocessor memory\n", Name, Offset, Size);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Block = ExAllocatePoolWithTag(NonPagedPool, Size, RTKIT_BOOTARG_TAG);
    if (!Block)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Index = 0; Index < Size; ++Index)
        Block[Index] = READ_REGISTER_UCHAR(Memory + Offset + Index);
    if (!AppleRtkitPatchBootArgumentBlock(Block, Size, Patches, Count, &Patched))
    {
        DbgPrint("%s: boot arguments at %lx+%lx are malformed, left as they are\n", Name, Offset, Size);
        ExFreePoolWithTag(Block, RTKIT_BOOTARG_TAG);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (Index = 0; Index < Size; ++Index)
        WRITE_REGISTER_UCHAR(Memory + Offset + Index, Block[Index]);
    KeMemoryBarrier();
    ExFreePoolWithTag(Block, RTKIT_BOOTARG_TAG);
    DbgPrint("%s: boot arguments at %lx+%lx, %lu of %lu patched\n", Name, Offset, Size, Patched, Count);
    return STATUS_SUCCESS;
}
