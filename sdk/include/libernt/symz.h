/*
 * PROJECT:     LiberNT Compressed Symbol Table Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Format and lookup interface of the .symz image section
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define SYMZ_SECTION_NAME       ".symz"
#define SYMZ_MAGIC              0x5A4D5953
#define SYMZ_VERSION            2
#define SYMZ_BLOCK_SIZE         0x10000
#define SYMZ_MAX_NAME           0x400
#define SYMZ_MAX_BLOCK_SIZE     (SYMZ_BLOCK_SIZE + SYMZ_MAX_NAME + 8)
#define SYMZ_LZMA_PROPS_SIZE    5
#define SYMZ_DECODER_WORK_SIZE  0x8000

typedef struct _SYMZ_HEADER
{
    ULONG Magic;
    USHORT Version;
    USHORT HeaderSize;
    ULONG SymbolBlockCount;
    ULONG LineBlockCount;
    ULONG FileBlockCount;
    ULONG SymbolCount;
    ULONG LineCount;
    ULONG FileCount;
    ULONG MaxUnpackedSize;
    UCHAR LzmaProperties[SYMZ_LZMA_PROPS_SIZE];
    UCHAR Reserved[3];
} SYMZ_HEADER, *PSYMZ_HEADER;

typedef struct _SYMZ_BLOCK
{
    ULONG First;
    ULONG DataOffset;
    ULONG PackedSize;
    ULONG UnpackedSize;
} SYMZ_BLOCK, *PSYMZ_BLOCK;

typedef BOOLEAN
(*PSYMZ_ENUM_ROUTINE)(
    PVOID Context,
    ULONG Rva,
    const char *Name);

typedef BOOLEAN
(*PSYMZ_LINE_ROUTINE)(
    PVOID Context,
    ULONG Rva,
    ULONG File,
    ULONG Line);

BOOLEAN
SymzCheckHeader(
    const SYMZ_HEADER *Header,
    ULONG TableSize);

BOOLEAN
SymzDecodeBlock(
    const SYMZ_HEADER *Header,
    const VOID *Packed,
    ULONG PackedSize,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize);

BOOLEAN
SymzSearchBlock(
    const VOID *Unpacked,
    ULONG UnpackedSize,
    ULONG FirstRva,
    ULONG Rva,
    const char **Name,
    PULONG SymbolRva);

BOOLEAN
SymzEnumerateBlock(
    const VOID *Unpacked,
    ULONG UnpackedSize,
    ULONG FirstRva,
    PSYMZ_ENUM_ROUTINE Routine,
    PVOID Context);

BOOLEAN
SymzSearchLineBlock(
    const VOID *Unpacked,
    ULONG UnpackedSize,
    ULONG FirstRva,
    ULONG Rva,
    PULONG File,
    PULONG Line);

BOOLEAN
SymzSearchFileBlock(
    const VOID *Unpacked,
    ULONG UnpackedSize,
    ULONG FirstFile,
    ULONG File,
    const char **Name);

BOOLEAN
SymzLookup(
    const VOID *Table,
    ULONG TableSize,
    ULONG Rva,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize,
    const char **Name,
    PULONG SymbolRva);

BOOLEAN
SymzLookupLine(
    const VOID *Table,
    ULONG TableSize,
    ULONG Rva,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize,
    const char **FileName,
    PULONG Line);

BOOLEAN
SymzEnumerate(
    const VOID *Table,
    ULONG TableSize,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize,
    PSYMZ_ENUM_ROUTINE Routine,
    PVOID Context);

BOOLEAN
SymzEnumerateFiles(
    const VOID *Table,
    ULONG TableSize,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize,
    PSYMZ_ENUM_ROUTINE Routine,
    PVOID Context);

BOOLEAN
SymzEnumerateLines(
    const VOID *Table,
    ULONG TableSize,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize,
    PSYMZ_LINE_ROUTINE Routine,
    PVOID Context);

#ifdef __cplusplus
}
#endif
