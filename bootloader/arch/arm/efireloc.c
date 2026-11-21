/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The base relocations an ARMv7 UEFI application applies to itself
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <Cpu.h>

/*
 * This runs before the image is relocated. Nothing here may take the address
 * of a function or of data, call through a pointer or switch on a value: every
 * one of those needs a relocation that has not been applied yet.
 */

typedef struct _ARM_EFI_RELOCATIONS
{
    ULONG Signature[2];
    ULONG TableRva;
    ULONG TableSize;
    ULONG ImageBase;
    ULONG ImageSize;
    ULONG Reserved;
    ULONG EmptyBlock[2];
} ARM_EFI_RELOCATIONS;

static
ULONG
ArmEfiRead(
    _In_ const volatile UCHAR *At,
    _In_ ULONG Bytes)
{
    ULONG Value = 0, Index;

    for (Index = 0; Index < Bytes; Index++)
        Value |= (ULONG)At[Index] << (8 * Index);
    return Value;
}

static
VOID
ArmEfiWrite(
    _Out_ volatile UCHAR *At,
    _In_ ULONG Bytes,
    _In_ ULONG Value)
{
    ULONG Index;

    for (Index = 0; Index < Bytes; Index++)
        At[Index] = (UCHAR)(Value >> (8 * Index));
}

/* The 16 bits a MOVW or MOVT holds: imm4 and i in the first half-word, imm3 and imm8 in the second. */
static
ULONG
ArmEfiReadMove(
    _In_ const volatile UCHAR *At)
{
    ULONG First = ArmEfiRead(At, 2), Second = ArmEfiRead(At + 2, 2);

    return ((First & 0xF) << 12) | (((First >> 10) & 1) << 11) | (((Second >> 12) & 7) << 8) | (Second & 0xFF);
}

static
VOID
ArmEfiWriteMove(
    _Inout_ volatile UCHAR *At,
    _In_ ULONG Value)
{
    ULONG First = ArmEfiRead(At, 2), Second = ArmEfiRead(At + 2, 2);

    First = (First & 0xFBF0) | ((Value >> 12) & 0xF) | (((Value >> 11) & 1) << 10);
    Second = (Second & 0x8F00) | (((Value >> 8) & 7) << 12) | (Value & 0xFF);
    ArmEfiWrite(At, 2, First);
    ArmEfiWrite(At + 2, 2, Second);
}

/*
 * Firmware that write-protects the code of the images it loads does so after
 * applying the relocations it was shown, which were none. The processor
 * protocol of such firmware gives the pages their write access back; firmware
 * without it does not protect them.
 */
static
VOID
ArmEfiMakeWritable(
    _In_ EFI_SYSTEM_TABLE *SystemTable,
    _In_ ULONG Base,
    _In_ ULONG Size)
{
    volatile ULONG Guid[4];
    EFI_CPU_ARCH_PROTOCOL *Cpu = NULL;

    if (!SystemTable || (SystemTable->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE) || !SystemTable->BootServices)
        return;

    /* EFI_CPU_ARCH_PROTOCOL_GUID, built here because a constant would be reached through a relocation */
    Guid[0] = 0x26BACCB1;
    Guid[1] = 0x11D46F42;
    Guid[2] = 0x8000E7BC;
    Guid[3] = 0x81883CC7;

    if (EFI_ERROR(SystemTable->BootServices->LocateProtocol((EFI_GUID *)Guid, NULL, (VOID **)&Cpu)) || !Cpu)
        return;

    Cpu->SetMemoryAttributes(Cpu, Base, (Size + 0xFFF) & ~0xFFFUL, 0);
}

VOID
ArmEfiRelocate(
    _In_ const ARM_EFI_RELOCATIONS *Relocations,
    _In_ ULONG Delta,
    _In_ EFI_SYSTEM_TABLE *SystemTable)
{
    volatile UCHAR *Image = (volatile UCHAR *)(ULONG_PTR)(Relocations->ImageBase + Delta);
    volatile UCHAR *Block = Image + Relocations->TableRva;
    volatile UCHAR *End = Block + Relocations->TableSize;
    volatile UCHAR *Entry, *Target;
    ULONG Page, Size, Value, Type, Line, Address;

    if ((Relocations->TableRva == 0) || (Relocations->TableSize == 0))
        return;

    ArmEfiMakeWritable(SystemTable, (ULONG)(ULONG_PTR)Image, Relocations->ImageSize);

    while ((ULONG)(End - Block) >= 8)
    {
        Page = ArmEfiRead(Block, 4);
        Size = ArmEfiRead(Block + 4, 4);
        if ((Size < 8) || (Size > (ULONG)(End - Block)))
            break;

        for (Entry = Block + 8; (ULONG)(Entry - Block) + 2 <= Size; Entry += 2)
        {
            Value = ArmEfiRead(Entry, 2);
            Type = Value >> 12;
            Target = Image + Page + (Value & 0xFFF);

            if (Type == IMAGE_REL_BASED_HIGHLOW)
            {
                ArmEfiWrite(Target, 4, ArmEfiRead(Target, 4) + Delta);
            }
            else if (Type == IMAGE_REL_BASED_THUMB_MOV32)
            {
                Value = (ArmEfiReadMove(Target) | (ArmEfiReadMove(Target + 4) << 16)) + Delta;
                ArmEfiWriteMove(Target, Value & 0xFFFF);
                ArmEfiWriteMove(Target + 4, Value >> 16);
            }
        }

        Block += Size;
    }

    /* Instructions were rewritten: clean them out of the data cache (CTR gives its line) and drop what was fetched. */
    __asm__ __volatile__("mrc p15, 0, %0, c0, c0, 1" : "=r"(Line));
    Line = 4UL << ((Line >> 16) & 0xF);
    Address = (ULONG)(ULONG_PTR)Image & ~(Line - 1);
    for (; Address < (ULONG)(ULONG_PTR)Image + Relocations->ImageSize; Address += Line)
        __asm__ __volatile__("mcr p15, 0, %0, c7, c11, 1" : : "r"(Address));
    Address = 0;
    __asm__ __volatile__("dsb\n\tmcr p15, 0, %0, c7, c5, 0\n\tmcr p15, 0, %0, c7, c5, 6\n\tdsb\n\tisb" : : "r"(Address));
}
