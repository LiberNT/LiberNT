/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Map the guest system DLL the kernel would map for an AMD64 process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "amd64emu.h"

static
NTSTATUS
EmuProtectPage(
    _In_ PVOID Page,
    _In_ ULONG Protection,
    _Out_ PULONG OldProtection)
{
    SIZE_T Length = PAGE_SIZE;

    return NtProtectVirtualMemory(NtCurrentProcess(), &Page, &Length, Protection, OldProtection);
}

static
NTSTATUS
EmuRelocateImage(
    _In_ PVOID ImageBase,
    _In_ SIZE_T ViewSize)
{
    PIMAGE_NT_HEADERS64 Headers = RtlImageNtHeader(ImageBase);
    PIMAGE_DATA_DIRECTORY Directory;
    PIMAGE_BASE_RELOCATION Block;
    ULONG Offset = 0, Count, Index, Type, PageProtection, NextProtection;
    PUCHAR Page, Target;
    PUSHORT Fixups;
    ULONG_PTR Delta;
    SIZE_T Rva;
    NTSTATUS Status;

    if (!Headers ||
        Headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        Headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        Headers->OptionalHeader.SizeOfImage > ViewSize)
    {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    Delta = (ULONG_PTR)ImageBase - Headers->OptionalHeader.ImageBase;
    if (!Delta) return STATUS_SUCCESS;
    if (Headers->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED) return STATUS_CONFLICTING_ADDRESSES;

    Directory = &Headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!Directory->Size) return STATUS_SUCCESS;
    if (Directory->VirtualAddress >= ViewSize || Directory->Size > ViewSize - Directory->VirtualAddress)
        return STATUS_INVALID_IMAGE_FORMAT;

    while (Offset < Directory->Size)
    {
        if (Directory->Size - Offset < sizeof(*Block)) return STATUS_INVALID_IMAGE_FORMAT;
        Block = (PVOID)((PUCHAR)ImageBase + Directory->VirtualAddress + Offset);
        if (Block->SizeOfBlock < sizeof(*Block) ||
            Block->SizeOfBlock > Directory->Size - Offset ||
            (Block->SizeOfBlock & 1) ||
            (Block->VirtualAddress & (PAGE_SIZE - 1)) ||
            Block->VirtualAddress >= ViewSize)
        {
            return STATUS_INVALID_IMAGE_FORMAT;
        }

        Count = (Block->SizeOfBlock - sizeof(*Block)) / sizeof(USHORT);
        Fixups = (PUSHORT)(Block + 1);
        Page = (PUCHAR)ImageBase + Block->VirtualAddress;
        Status = EmuProtectPage(Page, PAGE_READWRITE, &PageProtection);
        if (!NT_SUCCESS(Status)) return Status;

        for (Index = 0; Index < Count; Index++)
        {
            Type = Fixups[Index] >> 12;
            Rva = (SIZE_T)Block->VirtualAddress + (Fixups[Index] & 0xFFF);
            if (Type == IMAGE_REL_BASED_ABSOLUTE) continue;
            if (Type != IMAGE_REL_BASED_DIR64 || sizeof(ULONG64) > ViewSize - Rva)
            {
                Status = STATUS_INVALID_IMAGE_FORMAT;
                break;
            }

            Target = (PUCHAR)ImageBase + Rva;
            if ((Fixups[Index] & 0xFFF) <= PAGE_SIZE - sizeof(ULONG64))
            {
                *(UNALIGNED ULONG64 *)Target += Delta;
                continue;
            }

            Status = EmuProtectPage(Page + PAGE_SIZE, PAGE_READWRITE, &NextProtection);
            if (!NT_SUCCESS(Status)) break;
            *(UNALIGNED ULONG64 *)Target += Delta;
            EmuProtectPage(Page + PAGE_SIZE, NextProtection, &NextProtection);
        }

        EmuProtectPage(Page, PageProtection, &PageProtection);
        if (!NT_SUCCESS(Status)) return Status;
        Offset += Block->SizeOfBlock;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
EmuMapGuestImage(
    _In_ PCUNICODE_STRING Name,
    _Out_ PVOID *ImageBase)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle, SectionHandle;
    SIZE_T ViewSize = 0;
    NTSTATUS Status;

    *ImageBase = NULL;
    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_EXECUTE | FILE_READ_DATA | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_DELETE,
                        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status)) return Status;

    Status = NtCreateSection(&SectionHandle, SECTION_ALL_ACCESS, NULL, NULL, PAGE_EXECUTE, SEC_IMAGE, FileHandle);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status)) return Status;

    Status = NtMapViewOfSection(SectionHandle,
                                NtCurrentProcess(),
                                ImageBase,
                                0,
                                0,
                                NULL,
                                &ViewSize,
                                ViewShare,
                                0,
                                PAGE_EXECUTE_READ);
    NtClose(SectionHandle);
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        Status = EmuRelocateImage(*ImageBase, ViewSize);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_INVALID_IMAGE_FORMAT;
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        NtUnmapViewOfSection(NtCurrentProcess(), *ImageBase);
        *ImageBase = NULL;
    }

    return Status;
}
