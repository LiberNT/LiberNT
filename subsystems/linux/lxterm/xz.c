/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Streaming decoder of .xz files with LZMA2 blocks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdlib.h>
#include <string.h>

#include <windef.h>
#include <winbase.h>

#include <LzmaDec.h>

#include "lxinstall.h"

#define LX_XZ_INPUT_SIZE        (256 * 1024)
#define LX_XZ_MAX_DICTIONARY    (64 * 1024 * 1024)
#define LX_XZ_FILTER_LZMA2      0x21

void LzmaDec_InitDicAndState(CLzmaDec *p, BoolInt initDic, BoolInt initState);

struct _LX_XZ
{
    HANDLE File;
    PUCHAR Input;
    ULONG InputLength;
    ULONG InputPosition;
    ULONG64 Consumed;
    CLzmaDec Lzma;
    PUCHAR Dictionary;
    SIZE_T DictionarySize;
    SIZE_T OutputStart;
    UInt32 BlockDictionary;
    ULONG CheckSize;
    ULONG64 BlockBytes;
    ULONG ChunkLeft;
    ULONG PackedLength;
    ULONG PackedPosition;
    ULONG Streams;
    BOOLEAN InStream;
    BOOLEAN InBlock;
    BOOLEAN ChunkLzma;
    BOOLEAN NeedDictionaryReset;
    BOOLEAN NeedStateReset;
    BOOLEAN NeedProperties;
    BOOLEAN Finished;
    BOOLEAN Failed;
    UCHAR Packed[65536];
};

static const UCHAR LxXzMagic[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
static const UCHAR LxXzCheckSizes[16] = { 0, 4, 4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 32, 64, 64, 64 };

static
void *
LxXzAlloc(
    ISzAllocPtr Allocator,
    size_t Size)
{
    UNREFERENCED_PARAMETER(Allocator);
    return malloc(Size);
}

static
void
LxXzFree(
    ISzAllocPtr Allocator,
    void *Address)
{
    UNREFERENCED_PARAMETER(Allocator);
    free(Address);
}

static const ISzAlloc LxXzAllocator = { LxXzAlloc, LxXzFree };

static
BOOL
LxXzFill(
    _Inout_ PLX_XZ Xz)
{
    DWORD Read;

    if (Xz->InputPosition < Xz->InputLength)
        return TRUE;
    if (!ReadFile(Xz->File, Xz->Input, LX_XZ_INPUT_SIZE, &Read, NULL) || (Read == 0))
        return FALSE;
    Xz->InputLength = Read;
    Xz->InputPosition = 0;
    return TRUE;
}

static
BOOL
LxXzReadBytes(
    _Inout_ PLX_XZ Xz,
    _Out_writes_bytes_opt_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PUCHAR Target = Buffer;

    while (Length != 0)
    {
        ULONG Count;

        if (!LxXzFill(Xz))
            return FALSE;
        Count = min(Length, Xz->InputLength - Xz->InputPosition);
        if (Target != NULL)
        {
            memcpy(Target, Xz->Input + Xz->InputPosition, Count);
            Target += Count;
        }
        Xz->InputPosition += Count;
        Xz->Consumed += Count;
        Length -= Count;
    }
    return TRUE;
}

static
BOOL
LxXzParseNumber(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length,
    _Inout_ PULONG Position,
    _Out_ PULONG64 Value)
{
    ULONG Shift = 0;

    *Value = 0;
    while ((*Position < Length) && (Shift <= 56))
    {
        UCHAR Byte = Buffer[(*Position)++];

        *Value |= (ULONG64)(Byte & 0x7F) << Shift;
        if (!(Byte & 0x80))
            return (Byte != 0) || (Shift == 0);
        Shift += 7;
    }
    return FALSE;
}

static
BOOL
LxXzReadNumber(
    _Inout_ PLX_XZ Xz,
    _Out_ PULONG64 Value,
    _Inout_ PULONG64 Length)
{
    ULONG Shift = 0;
    UCHAR Byte;

    *Value = 0;
    do
    {
        if ((Shift > 56) || !LxXzReadBytes(Xz, &Byte, 1))
            return FALSE;
        (*Length)++;
        *Value |= (ULONG64)(Byte & 0x7F) << Shift;
        Shift += 7;
    } while (Byte & 0x80);
    return TRUE;
}

static
BOOL
LxXzStartStream(
    _Inout_ PLX_XZ Xz)
{
    UCHAR Header[12];
    static const UCHAR Zero[4] = { 0 };

    for (;;)
    {
        if (!LxXzFill(Xz))
        {
            if (Xz->Streams == 0)
                return FALSE;
            Xz->Finished = TRUE;
            return TRUE;
        }
        if (!LxXzReadBytes(Xz, Header, 4))
            return FALSE;
        if ((Xz->Streams != 0) && (memcmp(Header, Zero, 4) == 0))
            continue;
        if (!LxXzReadBytes(Xz, Header + 4, 8))
            return FALSE;
        if ((memcmp(Header, LxXzMagic, sizeof(LxXzMagic)) != 0) || (Header[6] != 0) || (Header[7] & 0xF0))
            return FALSE;
        Xz->CheckSize = LxXzCheckSizes[Header[7] & 0x0F];
        Xz->Streams++;
        Xz->InStream = TRUE;
        return TRUE;
    }
}

static
BOOL
LxXzSkipIndex(
    _Inout_ PLX_XZ Xz)
{
    ULONG64 Records, Record, Value, Length = 1;
    UCHAR Footer[12];

    if (!LxXzReadNumber(Xz, &Records, &Length))
        return FALSE;
    for (Record = 0; Record < Records; Record++)
    {
        if (!LxXzReadNumber(Xz, &Value, &Length) || !LxXzReadNumber(Xz, &Value, &Length))
            return FALSE;
    }
    if (!LxXzReadBytes(Xz, NULL, (ULONG)((4 - (Length & 3)) & 3) + 4))
        return FALSE;
    if (!LxXzReadBytes(Xz, Footer, sizeof(Footer)) || (Footer[10] != 'Y') || (Footer[11] != 'Z'))
        return FALSE;
    Xz->InStream = FALSE;
    return TRUE;
}

static
BOOL
LxXzStartBlock(
    _Inout_ PLX_XZ Xz,
    _In_ UCHAR SizeByte)
{
    UCHAR Header[1024];
    ULONG HeaderSize = ((ULONG)SizeByte + 1) * 4, Position = 2;
    ULONG64 Value, Id, PropertiesSize;
    UCHAR Flags, Property;
    SIZE_T DictionarySize;

    Header[0] = SizeByte;
    if (!LxXzReadBytes(Xz, Header + 1, HeaderSize - 1))
        return FALSE;
    Flags = Header[1];
    if ((Flags & 0x3C) || ((Flags & 0x03) != 0))
        return FALSE;
    if ((Flags & 0x40) && !LxXzParseNumber(Header, HeaderSize - 4, &Position, &Value))
        return FALSE;
    if ((Flags & 0x80) && !LxXzParseNumber(Header, HeaderSize - 4, &Position, &Value))
        return FALSE;
    if (!LxXzParseNumber(Header, HeaderSize - 4, &Position, &Id) ||
        !LxXzParseNumber(Header, HeaderSize - 4, &Position, &PropertiesSize))
    {
        return FALSE;
    }
    if ((Id != LX_XZ_FILTER_LZMA2) || (PropertiesSize != 1) || (Position >= HeaderSize - 4))
        return FALSE;
    Property = Header[Position];
    if (Property > 40)
        return FALSE;
    DictionarySize = (Property == 40) ? 0xFFFFFFFF : ((SIZE_T)(2 | (Property & 1)) << (Property / 2 + 11));
    if (DictionarySize > LX_XZ_MAX_DICTIONARY)
        return FALSE;

    if (Xz->DictionarySize < DictionarySize)
    {
        PUCHAR Dictionary = realloc(Xz->Dictionary, DictionarySize);

        if (Dictionary == NULL)
            return FALSE;
        Xz->Dictionary = Dictionary;
        Xz->DictionarySize = DictionarySize;
    }
    Xz->Lzma.dic = Xz->Dictionary;
    Xz->Lzma.dicBufSize = Xz->DictionarySize;
    Xz->Lzma.dicPos = 0;
    Xz->Lzma.prop.dicSize = (UInt32)DictionarySize;
    Xz->OutputStart = 0;
    Xz->BlockDictionary = (UInt32)DictionarySize;
    Xz->BlockBytes = HeaderSize;
    Xz->ChunkLeft = 0;
    Xz->NeedDictionaryReset = TRUE;
    Xz->NeedStateReset = TRUE;
    Xz->NeedProperties = TRUE;
    Xz->InBlock = TRUE;
    return TRUE;
}

static
BOOL
LxXzSetProperties(
    _Inout_ PLX_XZ Xz,
    _In_ UCHAR Value)
{
    UCHAR Header[5];

    if ((Value >= 9 * 5 * 5) || ((Value % 9) + ((Value / 9) % 5) > 4))
        return FALSE;
    Header[0] = Value;
    Header[1] = (UCHAR)Xz->BlockDictionary;
    Header[2] = (UCHAR)(Xz->BlockDictionary >> 8);
    Header[3] = (UCHAR)(Xz->BlockDictionary >> 16);
    Header[4] = (UCHAR)(Xz->BlockDictionary >> 24);
    return LzmaDec_AllocateProbs(&Xz->Lzma, Header, sizeof(Header), &LxXzAllocator) == SZ_OK;
}

static
BOOL
LxXzStartChunk(
    _Inout_ PLX_XZ Xz)
{
    UCHAR Control, Bytes[5];
    ULONG Reset, Length;

    if (!LxXzReadBytes(Xz, &Control, 1))
        return FALSE;
    Xz->BlockBytes++;

    if (Control == 0x00)
    {
        if (!LxXzReadBytes(Xz, NULL, (ULONG)((4 - (Xz->BlockBytes & 3)) & 3) + Xz->CheckSize))
            return FALSE;
        Xz->InBlock = FALSE;
        return TRUE;
    }

    if (Control < 0x80)
    {
        if ((Control > 2) || !LxXzReadBytes(Xz, Bytes, 2))
            return FALSE;
        Xz->BlockBytes += 2;
        if (Control == 1)
        {
            LzmaDec_InitDicAndState(&Xz->Lzma, True, False);
            Xz->NeedDictionaryReset = FALSE;
            Xz->NeedStateReset = TRUE;
        }
        else if (Xz->NeedDictionaryReset)
        {
            return FALSE;
        }
        Xz->ChunkLeft = (((ULONG)Bytes[0] << 8) | Bytes[1]) + 1;
        Xz->ChunkLzma = FALSE;
        return TRUE;
    }

    Reset = (Control >> 5) & 3;
    Length = (Reset >= 2) ? 5 : 4;
    if (!LxXzReadBytes(Xz, Bytes, Length))
        return FALSE;
    Xz->BlockBytes += Length;
    if ((Xz->NeedDictionaryReset && (Reset != 3)) || (Xz->NeedStateReset && (Reset == 0)))
        return FALSE;
    if (Reset >= 2)
    {
        if (!LxXzSetProperties(Xz, Bytes[4]))
            return FALSE;
        Xz->NeedProperties = FALSE;
    }
    else if (Xz->NeedProperties)
    {
        return FALSE;
    }

    LzmaDec_InitDicAndState(&Xz->Lzma, Reset == 3, Reset >= 1);
    Xz->NeedDictionaryReset = FALSE;
    Xz->NeedStateReset = FALSE;
    Xz->ChunkLeft = (((ULONG)(Control & 0x1F) << 16) | ((ULONG)Bytes[0] << 8) | Bytes[1]) + 1;
    Xz->PackedLength = (((ULONG)Bytes[2] << 8) | Bytes[3]) + 1;
    if (!LxXzReadBytes(Xz, Xz->Packed, Xz->PackedLength))
        return FALSE;
    Xz->BlockBytes += Xz->PackedLength;
    Xz->PackedPosition = 0;
    Xz->ChunkLzma = TRUE;
    return TRUE;
}

static
BOOL
LxXzProduce(
    _Inout_ PLX_XZ Xz)
{
    SIZE_T Start, Limit;

    if (Xz->Lzma.dicPos == Xz->DictionarySize)
        Xz->Lzma.dicPos = 0;
    Start = Xz->Lzma.dicPos;
    Limit = min(Xz->DictionarySize, Start + Xz->ChunkLeft);

    if (!Xz->ChunkLzma)
    {
        ULONG Count = (ULONG)(Limit - Start);

        if (!LxXzReadBytes(Xz, Xz->Dictionary + Start, Count))
            return FALSE;
        Xz->BlockBytes += Count;
        if ((Xz->Lzma.checkDicSize == 0) && (Xz->Lzma.prop.dicSize - Xz->Lzma.processedPos <= Count))
            Xz->Lzma.checkDicSize = Xz->Lzma.prop.dicSize;
        Xz->Lzma.processedPos += Count;
        Xz->Lzma.dicPos = Limit;
    }
    else
    {
        SizeT Length = Xz->PackedLength - Xz->PackedPosition;
        ELzmaStatus Status;

        if (LzmaDec_DecodeToDic(&Xz->Lzma, Limit, Xz->Packed + Xz->PackedPosition, &Length,
                                LZMA_FINISH_ANY, &Status) != SZ_OK)
        {
            return FALSE;
        }
        Xz->PackedPosition += (ULONG)Length;
        if ((Xz->Lzma.dicPos == Start) && (Length == 0))
            return FALSE;
    }

    Xz->OutputStart = Start;
    Xz->ChunkLeft -= (ULONG)(Xz->Lzma.dicPos - Start);
    if ((Xz->ChunkLeft == 0) && Xz->ChunkLzma && (Xz->PackedPosition != Xz->PackedLength))
        return FALSE;
    return TRUE;
}

static
BOOL
LxXzStep(
    _Inout_ PLX_XZ Xz)
{
    UCHAR Byte;

    if (!Xz->InStream)
        return LxXzStartStream(Xz);
    if (!Xz->InBlock)
    {
        if (!LxXzReadBytes(Xz, &Byte, 1))
            return FALSE;
        return (Byte == 0) ? LxXzSkipIndex(Xz) : LxXzStartBlock(Xz, Byte);
    }
    if (Xz->ChunkLeft == 0)
        return LxXzStartChunk(Xz);
    return LxXzProduce(Xz);
}

PLX_XZ
LxXzOpen(
    _In_ HANDLE File)
{
    PLX_XZ Xz = calloc(1, sizeof(*Xz));

    if (Xz == NULL)
        return NULL;
    Xz->Input = malloc(LX_XZ_INPUT_SIZE);
    if (Xz->Input == NULL)
    {
        free(Xz);
        return NULL;
    }
    Xz->File = File;
    LzmaDec_Construct(&Xz->Lzma);
    return Xz;
}

LONG
LxXzRead(
    _Inout_ PLX_XZ Xz,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    ULONG Done = 0;

    while (Done < Size)
    {
        SIZE_T Available = Xz->InBlock ? (Xz->Lzma.dicPos - Xz->OutputStart) : 0;

        if (Available != 0)
        {
            ULONG Count = (ULONG)min(Available, (SIZE_T)(Size - Done));

            memcpy((PUCHAR)Buffer + Done, Xz->Dictionary + Xz->OutputStart, Count);
            Xz->OutputStart += Count;
            Done += Count;
            continue;
        }
        if (Xz->Failed)
            return -1;
        if (Xz->Finished)
            break;
        if (!LxXzStep(Xz))
        {
            Xz->Failed = TRUE;
            return -1;
        }
    }
    return (LONG)Done;
}

ULONG64
LxXzPosition(
    _In_ PLX_XZ Xz)
{
    return Xz->Consumed;
}

ULONG64
LxXzContentSize(
    _In_reads_bytes_(TailLength) const UCHAR *Tail,
    _In_ ULONG TailLength,
    _In_ ULONG64 FileSize)
{
    static const UCHAR Zero[4] = { 0 };
    ULONG64 TailStart, End = FileSize, Total = 0;

    if (FileSize < TailLength)
        return 0;
    TailStart = FileSize - TailLength;
    while (End > 0)
    {
        ULONG64 Records, Record, Unpadded, Uncompressed, Blocks = 0;
        ULONG IndexSize, Position = 1;
        const UCHAR *Footer, *Index;

        if ((End < TailStart + 12) || (End < 24))
            return 0;
        Footer = Tail + (End - 12 - TailStart);
        if (memcmp(Footer + 8, Zero, 4) == 0)
        {
            End -= 4;
            continue;
        }
        if ((Footer[10] != 'Y') || (Footer[11] != 'Z'))
            return 0;
        IndexSize = (((ULONG)Footer[4] | ((ULONG)Footer[5] << 8) | ((ULONG)Footer[6] << 16) |
                      ((ULONG)Footer[7] << 24)) + 1) * 4;
        if (((ULONG64)IndexSize + 24 > End) || (End - 12 - IndexSize < TailStart))
            return 0;
        Index = Tail + (End - 12 - IndexSize - TailStart);
        if ((Index[0] != 0) || !LxXzParseNumber(Index, IndexSize, &Position, &Records))
            return 0;
        for (Record = 0; Record < Records; Record++)
        {
            if (!LxXzParseNumber(Index, IndexSize, &Position, &Unpadded) ||
                !LxXzParseNumber(Index, IndexSize, &Position, &Uncompressed))
            {
                return 0;
            }
            Blocks += (Unpadded + 3) & ~3ULL;
            Total += Uncompressed;
        }
        if (Blocks + IndexSize + 24 > End)
            return 0;
        End -= Blocks + IndexSize + 24;
    }
    return Total;
}

VOID
LxXzClose(
    _In_ PLX_XZ Xz)
{
    LzmaDec_FreeProbs(&Xz->Lzma, &LxXzAllocator);
    free(Xz->Dictionary);
    free(Xz->Input);
    free(Xz);
}
