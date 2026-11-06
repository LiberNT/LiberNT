/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Read-only squashfs 4.0 images on loop devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#include <zlib.h>
#include <LzmaDec.h>

#define NDEBUG
#include <debug.h>

void LzmaDec_InitDicAndState(CLzmaDec *p, BoolInt initDic, BoolInt initState);

#define LX_SQUASHFS_MAGIC               0x73717368
#define LX_SQUASHFS_METADATA_SIZE       8192
#define LX_SQUASHFS_METADATA_RAW        0x8000
#define LX_SQUASHFS_DATA_RAW            (1UL << 24)
#define LX_SQUASHFS_DATA_SIZE_MASK      0x00FFFFFF
#define LX_SQUASHFS_NO_FRAGMENT         0xFFFFFFFF
#define LX_SQUASHFS_NO_XATTR            0xFFFFFFFF
#define LX_SQUASHFS_NO_TABLE            0xFFFFFFFFFFFFFFFFULL
#define LX_SQUASHFS_XATTR_PREFIX_MASK   0x00FF
#define LX_SQUASHFS_XATTR_VALUE_OOL     0x0100
#define LX_SQUASHFS_META_CACHE          8
#define LX_SQUASHFS_NAME_MAX            256
#define LX_SQUASHFS_DIR_COUNT_MAX       256
#define LX_SQUASHFS_FRAGMENTS_PER_BLOCK (LX_SQUASHFS_METADATA_SIZE / sizeof(LX_SQUASHFS_FRAGMENT))

#define LX_SQUASHFS_ZLIB                1
#define LX_SQUASHFS_LZMA                2
#define LX_SQUASHFS_LZO                 3
#define LX_SQUASHFS_XZ                  4
#define LX_SQUASHFS_LZ4                 5
#define LX_SQUASHFS_ZSTD                6

#define LX_SQUASHFS_DIR                 1
#define LX_SQUASHFS_REG                 2
#define LX_SQUASHFS_SYMLINK             3
#define LX_SQUASHFS_BLKDEV              4
#define LX_SQUASHFS_CHRDEV              5
#define LX_SQUASHFS_FIFO                6
#define LX_SQUASHFS_SOCKET              7
#define LX_SQUASHFS_LDIR                8
#define LX_SQUASHFS_LREG                9
#define LX_SQUASHFS_LSYMLINK            10
#define LX_SQUASHFS_LBLKDEV             11
#define LX_SQUASHFS_LCHRDEV             12
#define LX_SQUASHFS_LFIFO               13
#define LX_SQUASHFS_LSOCKET             14

#define LX_XZ_FILTER_LZMA2              0x21

#pragma pack(push, 1)
typedef struct _LX_SQUASHFS_SUPER
{
    ULONG Magic;
    ULONG Inodes;
    ULONG MkfsTime;
    ULONG BlockSize;
    ULONG Fragments;
    USHORT Compression;
    USHORT BlockLog;
    USHORT Flags;
    USHORT NoIds;
    USHORT Major;
    USHORT Minor;
    ULONG64 RootInode;
    ULONG64 BytesUsed;
    ULONG64 IdTable;
    ULONG64 XattrIdTable;
    ULONG64 InodeTable;
    ULONG64 DirectoryTable;
    ULONG64 FragmentTable;
    ULONG64 LookupTable;
} LX_SQUASHFS_SUPER, *PLX_SQUASHFS_SUPER;

typedef struct _LX_SQUASHFS_BASE
{
    USHORT Type;
    USHORT Mode;
    USHORT Uid;
    USHORT Gid;
    ULONG Mtime;
    ULONG Number;
} LX_SQUASHFS_BASE, *PLX_SQUASHFS_BASE;

typedef struct _LX_SQUASHFS_DIR_HEADER
{
    ULONG Count;
    ULONG Start;
    ULONG Number;
} LX_SQUASHFS_DIR_HEADER, *PLX_SQUASHFS_DIR_HEADER;

typedef struct _LX_SQUASHFS_DIR_ENTRY
{
    USHORT Offset;
    SHORT InodeOffset;
    USHORT Type;
    USHORT Size;
} LX_SQUASHFS_DIR_ENTRY, *PLX_SQUASHFS_DIR_ENTRY;

typedef struct _LX_SQUASHFS_FRAGMENT
{
    ULONG64 Start;
    ULONG Size;
    ULONG Unused;
} LX_SQUASHFS_FRAGMENT, *PLX_SQUASHFS_FRAGMENT;

typedef struct _LX_SQUASHFS_XATTR_TABLE
{
    ULONG64 Start;
    ULONG Ids;
    ULONG Unused;
} LX_SQUASHFS_XATTR_TABLE, *PLX_SQUASHFS_XATTR_TABLE;

typedef struct _LX_SQUASHFS_XATTR_ID
{
    ULONG64 Reference;
    ULONG Count;
    ULONG Size;
} LX_SQUASHFS_XATTR_ID, *PLX_SQUASHFS_XATTR_ID;
#pragma pack(pop)

C_ASSERT(sizeof(LX_SQUASHFS_SUPER) == 96);
C_ASSERT(sizeof(LX_SQUASHFS_BASE) == 16);
C_ASSERT(sizeof(LX_SQUASHFS_DIR_HEADER) == 12);
C_ASSERT(sizeof(LX_SQUASHFS_DIR_ENTRY) == 8);
C_ASSERT(sizeof(LX_SQUASHFS_FRAGMENT) == 16);
C_ASSERT(sizeof(LX_SQUASHFS_XATTR_TABLE) == 16);
C_ASSERT(sizeof(LX_SQUASHFS_XATTR_ID) == 16);

typedef struct _LX_SQUASHFS_META
{
    ULONG64 Start;
    ULONG64 Next;
    ULONG Length;
    BOOLEAN Valid;
    UCHAR Data[LX_SQUASHFS_METADATA_SIZE];
} LX_SQUASHFS_META, *PLX_SQUASHFS_META;

typedef struct _LX_SQUASHFS_CURSOR
{
    ULONG64 Block;
    ULONG Offset;
} LX_SQUASHFS_CURSOR, *PLX_SQUASHFS_CURSOR;

struct _LX_SQUASHFS
{
    PLX_FILE Device;
    volatile LONG Users;
    KEVENT Lock;
    LX_SQUASHFS_SUPER Super;
    PULONG Ids;
    PULONG64 FragmentIndex;
    ULONG FragmentBlocks;
    PULONG64 XattrIndex;
    ULONG64 XattrStart;
    ULONG XattrIds;
    PUCHAR Raw;
    ULONG RawSize;
    PUCHAR Block;
    PUCHAR Fragment;
    ULONG64 FragmentStart;
    ULONG FragmentLength;
    BOOLEAN FragmentValid;
    PLX_SQUASHFS_META Meta;
    ULONG MetaNext;
    CLzmaDec Lzma;
    BOOLEAN LzmaReady;
    z_stream Zlib;
    BOOLEAN ZlibReady;
};

static
void *
LxSquashfsLzmaAlloc(
    ISzAllocPtr Allocator,
    size_t Size)
{
    UNREFERENCED_PARAMETER(Allocator);
    return LxAllocPaged(Size);
}

static
void
LxSquashfsLzmaFree(
    ISzAllocPtr Allocator,
    void *Address)
{
    UNREFERENCED_PARAMETER(Allocator);
    LxFreePaged(Address);
}

static const ISzAlloc LxSquashfsLzmaAllocator = { LxSquashfsLzmaAlloc, LxSquashfsLzmaFree };

static
voidpf
LxSquashfsZlibAlloc(
    voidpf Opaque,
    uInt Items,
    uInt Size)
{
    UNREFERENCED_PARAMETER(Opaque);
    if ((Size != 0) && (Items > MAXULONG / Size))
        return NULL;
    return LxAllocPaged((SIZE_T)Items * Size);
}

static
void
LxSquashfsZlibFree(
    voidpf Opaque,
    voidpf Address)
{
    UNREFERENCED_PARAMETER(Opaque);
    LxFreePaged(Address);
}

static
VOID
LxSquashfsLock(
    _In_ PLX_SQUASHFS Squash)
{
    KeWaitForSingleObject(&Squash->Lock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
LxSquashfsUnlock(
    _In_ PLX_SQUASHFS Squash)
{
    KeSetEvent(&Squash->Lock, IO_NO_INCREMENT, FALSE);
}

static
LONG
LxSquashfsCopy(
    _Out_writes_bytes_(Length) PVOID Destination,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ SIZE_T Length)
{
    LONG Error = 0;

    _SEH2_TRY
    {
        RtlCopyMemory(Destination, Source, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Error = -LX_EIO;
    }
    _SEH2_END;
    return Error;
}

static
LONG
LxSquashfsReadRaw(
    _In_ PLX_SQUASHFS Squash,
    _In_ ULONG64 Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    if ((Squash->Device == NULL) || (Offset > Squash->Super.BytesUsed) || (Length > Squash->Super.BytesUsed - Offset))
        return -LX_EIO;
    if (LxLoopReadKernel(Squash->Device, Buffer, Length, Offset) != (LONG64)Length)
        return -LX_EIO;
    return 0;
}

static
BOOLEAN
LxSquashfsVli(
    _In_reads_(End) const UCHAR *Data,
    _In_ ULONG End,
    _Inout_ PULONG Position,
    _Out_ PULONG64 Value)
{
    ULONG Shift;

    *Value = 0;
    for (Shift = 0; (Shift < 63) && (*Position < End); Shift += 7)
    {
        UCHAR Byte = Data[(*Position)++];

        *Value |= (ULONG64)(Byte & 0x7F) << Shift;
        if (!(Byte & 0x80))
            return TRUE;
    }
    return FALSE;
}

static
LONG
LxSquashfsXzHeader(
    _In_reads_(Length) const UCHAR *Input,
    _In_ ULONG Length,
    _Out_ PULONG Position,
    _Out_ PUCHAR Properties)
{
    static const UCHAR Magic[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
    ULONG Header, End, Filters, Index, Cursor;
    ULONG64 Value, Id, Size;
    UCHAR Flags;

    if ((Length < 13) || (RtlCompareMemory(Input, Magic, sizeof(Magic)) != sizeof(Magic)))
        return -LX_EIO;
    if (Input[12] == 0)
    {
        *Position = 12;
        return 1;
    }
    Header = ((ULONG)Input[12] + 1) * 4;
    if ((Header < 8) || (12 + Header > Length))
        return -LX_EIO;
    Flags = Input[13];
    if (Flags & 0x3C)
        return -LX_EIO;
    Filters = (Flags & 3) + 1;
    Cursor = 14;
    End = 12 + Header - 4;
    if (((Flags & 0x40) && !LxSquashfsVli(Input, End, &Cursor, &Value)) ||
        ((Flags & 0x80) && !LxSquashfsVli(Input, End, &Cursor, &Value)))
    {
        return -LX_EIO;
    }
    for (Index = 0; Index < Filters; Index++)
    {
        if (!LxSquashfsVli(Input, End, &Cursor, &Id) || !LxSquashfsVli(Input, End, &Cursor, &Size) ||
            (Size > End - Cursor))
        {
            return -LX_EIO;
        }
        if (Index + 1 < Filters)
        {
            /* TODO: xz branch/call/jump filters (mksquashfs -Xbcj) */
            DPRINT1("Linux squashfs: xz filter 0x%I64x is not supported\n", Id);
            return -LX_EIO;
        }
        if ((Id != LX_XZ_FILTER_LZMA2) || (Size != 1))
            return -LX_EIO;
        *Properties = Input[Cursor];
        Cursor += (ULONG)Size;
    }
    *Position = 12 + Header;
    return 0;
}

static
LONG
LxSquashfsXz(
    _In_ PLX_SQUASHFS Squash,
    _In_reads_(Length) const UCHAR *Input,
    _In_ ULONG Length,
    _Out_writes_to_(Capacity, *Produced) PUCHAR Output,
    _In_ ULONG Capacity,
    _Out_ PULONG Produced)
{
    CLzmaDec *Lzma = &Squash->Lzma;
    BOOLEAN NeedDictionary = TRUE, NeedState = TRUE, NeedProperties = TRUE;
    ULONG Position, Dictionary;
    UCHAR Properties;
    LONG Error;

    *Produced = 0;
    Error = LxSquashfsXzHeader(Input, Length, &Position, &Properties);
    if (Error != 0)
        return (Error > 0) ? 0 : Error;
    if (Properties > 40)
        return -LX_EIO;
    Dictionary = (Properties == 40) ? MAXULONG : ((2UL | (Properties & 1)) << (Properties / 2 + 11));
    if (!Squash->LzmaReady)
    {
        LzmaDec_Construct(Lzma);
        Squash->LzmaReady = TRUE;
    }
    Lzma->dic = Output;
    Lzma->dicBufSize = Capacity;
    Lzma->dicPos = 0;

    for (;;)
    {
        ULONG Unpacked, Packed;
        UCHAR Control;

        if (Position >= Length)
            return -LX_EIO;
        Control = Input[Position++];
        if (Control == 0)
            break;
        if (Control < 0x80)
        {
            if ((Control > 2) || (Length - Position < 2))
                return -LX_EIO;
            Unpacked = (((ULONG)Input[Position] << 8) | Input[Position + 1]) + 1;
            Position += 2;
            if (Control == 1)
            {
                LzmaDec_InitDicAndState(Lzma, True, False);
                NeedDictionary = FALSE;
                NeedState = TRUE;
            }
            else if (NeedDictionary)
            {
                return -LX_EIO;
            }
            if ((Unpacked > Length - Position) || (Unpacked > Capacity - Lzma->dicPos))
                return -LX_EIO;
            RtlCopyMemory(Output + Lzma->dicPos, Input + Position, Unpacked);
            Position += Unpacked;
            if ((Lzma->checkDicSize == 0) && (Lzma->prop.dicSize - Lzma->processedPos <= Unpacked))
                Lzma->checkDicSize = Lzma->prop.dicSize;
            Lzma->processedPos += Unpacked;
            Lzma->dicPos += Unpacked;
            continue;
        }

        {
            ULONG Reset = (Control >> 5) & 3, Need = (Reset >= 2) ? 5 : 4;
            SIZE_T Limit;

            if (Length - Position < Need)
                return -LX_EIO;
            Unpacked = (((ULONG)(Control & 0x1F) << 16) | ((ULONG)Input[Position] << 8) | Input[Position + 1]) + 1;
            Packed = (((ULONG)Input[Position + 2] << 8) | Input[Position + 3]) + 1;
            if ((NeedDictionary && (Reset != 3)) || (NeedState && (Reset == 0)))
                return -LX_EIO;
            if (Reset >= 2)
            {
                UCHAR Header[LZMA_PROPS_SIZE];
                UCHAR Lc = Input[Position + 4] % 9, Lp = (Input[Position + 4] / 9) % 5;

                if ((Input[Position + 4] >= 9 * 5 * 5) || (Lc + Lp > 4))
                    return -LX_EIO;
                Header[0] = Input[Position + 4];
                Header[1] = (UCHAR)Dictionary;
                Header[2] = (UCHAR)(Dictionary >> 8);
                Header[3] = (UCHAR)(Dictionary >> 16);
                Header[4] = (UCHAR)(Dictionary >> 24);
                if (LzmaDec_AllocateProbs(Lzma, Header, sizeof(Header), &LxSquashfsLzmaAllocator) != SZ_OK)
                    return -LX_ENOMEM;
                NeedProperties = FALSE;
            }
            else if (NeedProperties)
            {
                return -LX_EIO;
            }
            Position += Need;
            LzmaDec_InitDicAndState(Lzma, Reset == 3, Reset >= 1);
            NeedDictionary = FALSE;
            NeedState = FALSE;
            if ((Unpacked > Capacity - Lzma->dicPos) || (Packed > Length - Position))
                return -LX_EIO;
            Limit = Lzma->dicPos + Unpacked;
            while (Lzma->dicPos < Limit)
            {
                SizeT Available = Packed;
                SIZE_T Before = Lzma->dicPos;
                ELzmaStatus Status;

                if (LzmaDec_DecodeToDic(Lzma, Limit, Input + Position, &Available, LZMA_FINISH_ANY,
                                        &Status) != SZ_OK)
                {
                    return -LX_EIO;
                }
                Position += (ULONG)Available;
                Packed -= (ULONG)Available;
                if ((Lzma->dicPos == Before) && (Available == 0))
                    return -LX_EIO;
            }
            if (Packed != 0)
                return -LX_EIO;
        }
    }
    /* TODO: verify the integrity check of the xz block */
    *Produced = (ULONG)Lzma->dicPos;
    return 0;
}

static
LONG
LxSquashfsZlib(
    _In_ PLX_SQUASHFS Squash,
    _In_reads_(Length) const UCHAR *Input,
    _In_ ULONG Length,
    _Out_writes_to_(Capacity, *Produced) PUCHAR Output,
    _In_ ULONG Capacity,
    _Out_ PULONG Produced)
{
    z_stream *Stream = &Squash->Zlib;

    *Produced = 0;
    if (!Squash->ZlibReady)
    {
        RtlZeroMemory(Stream, sizeof(*Stream));
        Stream->zalloc = LxSquashfsZlibAlloc;
        Stream->zfree = LxSquashfsZlibFree;
        if (inflateInit(Stream) != Z_OK)
            return -LX_ENOMEM;
        Squash->ZlibReady = TRUE;
    }
    else if (inflateReset(Stream) != Z_OK)
    {
        return -LX_EIO;
    }
    Stream->next_in = (Bytef *)Input;
    Stream->avail_in = Length;
    Stream->next_out = Output;
    Stream->avail_out = Capacity;
    if (inflate(Stream, Z_FINISH) != Z_STREAM_END)
        return -LX_EIO;
    *Produced = Capacity - Stream->avail_out;
    return 0;
}

static
BOOLEAN
LxSquashfsLzoLength(
    _Inout_ const UCHAR **Input,
    _In_ const UCHAR *End,
    _In_ ULONG Base,
    _In_ ULONG Limit,
    _Out_ PULONG Length)
{
    ULONG Total = Base;

    for (;;)
    {
        if (*Input >= End)
            return FALSE;
        if (**Input != 0)
            break;
        (*Input)++;
        Total += 255;
        if (Total > Limit)
            return FALSE;
    }
    Total += *(*Input)++;
    *Length = Total;
    return Total <= Limit;
}

static
LONG
LxSquashfsLzo(
    _In_reads_(Length) const UCHAR *Input,
    _In_ ULONG Length,
    _Out_writes_to_(Capacity, *Produced) PUCHAR Output,
    _In_ ULONG Capacity,
    _Out_ PULONG Produced)
{
    const UCHAR *In = Input, *InEnd = Input + Length;
    PUCHAR Out = Output, OutEnd = Output + Capacity;
    ULONG State = 0, Count, Distance, Next, Word;
    UCHAR Code;

    *Produced = 0;
    if (Length == 0)
        return -LX_EIO;
    if (*In > 17)
    {
        Count = *In++ - 17;
        if (((ULONG)(InEnd - In) < Count) || ((ULONG)(OutEnd - Out) < Count))
            return -LX_EIO;
        RtlCopyMemory(Out, In, Count);
        In += Count;
        Out += Count;
        State = (Count < 4) ? Count : 4;
    }

    for (;;)
    {
        if (In >= InEnd)
            return -LX_EIO;
        Code = *In++;
        if (Code < 16)
        {
            if (State == 0)
            {
                Count = Code;
                if ((Count == 0) && !LxSquashfsLzoLength(&In, InEnd, 15, Capacity, &Count))
                    return -LX_EIO;
                Count += 3;
                if (((ULONG)(InEnd - In) < Count) || ((ULONG)(OutEnd - Out) < Count))
                    return -LX_EIO;
                RtlCopyMemory(Out, In, Count);
                In += Count;
                Out += Count;
                State = 4;
                continue;
            }
            if (In >= InEnd)
                return -LX_EIO;
            Distance = ((ULONG)*In++ << 2) + (Code >> 2) + ((State == 4) ? 2049 : 1);
            Count = (State == 4) ? 3 : 2;
            Next = Code & 3;
        }
        else if (Code < 32)
        {
            Count = Code & 7;
            if ((Count == 0) && !LxSquashfsLzoLength(&In, InEnd, 7, Capacity, &Count))
                return -LX_EIO;
            Count += 2;
            if (InEnd - In < 2)
                return -LX_EIO;
            Word = In[0] | ((ULONG)In[1] << 8);
            In += 2;
            Distance = ((ULONG)(Code & 8) << 11) + (Word >> 2);
            if (Distance == 0)
                break;
            Distance += 16384;
            Next = Word & 3;
        }
        else if (Code < 64)
        {
            Count = Code & 31;
            if ((Count == 0) && !LxSquashfsLzoLength(&In, InEnd, 31, Capacity, &Count))
                return -LX_EIO;
            Count += 2;
            if (InEnd - In < 2)
                return -LX_EIO;
            Word = In[0] | ((ULONG)In[1] << 8);
            In += 2;
            Distance = (Word >> 2) + 1;
            Next = Word & 3;
        }
        else
        {
            if (In >= InEnd)
                return -LX_EIO;
            Count = (Code < 128) ? 3 + ((Code >> 5) & 1) : 5 + ((Code >> 5) & 3);
            Distance = ((ULONG)*In++ << 3) + ((Code >> 2) & 7) + 1;
            Next = Code & 3;
        }

        if ((Distance > (ULONG)(Out - Output)) || ((ULONG)(OutEnd - Out) < Count))
            return -LX_EIO;
        while (Count-- != 0)
        {
            *Out = Out[-(LONG_PTR)Distance];
            Out++;
        }
        if (((ULONG)(InEnd - In) < Next) || ((ULONG)(OutEnd - Out) < Next))
            return -LX_EIO;
        RtlCopyMemory(Out, In, Next);
        In += Next;
        Out += Next;
        State = Next;
    }
    *Produced = (ULONG)(Out - Output);
    return 0;
}

static
LONG
LxSquashfsLz4(
    _In_reads_(Length) const UCHAR *Input,
    _In_ ULONG Length,
    _Out_writes_to_(Capacity, *Produced) PUCHAR Output,
    _In_ ULONG Capacity,
    _Out_ PULONG Produced)
{
    const UCHAR *In = Input, *InEnd = Input + Length;
    PUCHAR Out = Output, OutEnd = Output + Capacity;

    *Produced = 0;
    while (In < InEnd)
    {
        UCHAR Token = *In++;
        ULONG Count = Token >> 4, Distance;
        UCHAR Byte;

        if (Count == 15)
        {
            do
            {
                if (In >= InEnd)
                    return -LX_EIO;
                Byte = *In++;
                Count += Byte;
                if (Count > Capacity)
                    return -LX_EIO;
            } while (Byte == 255);
        }
        if (((ULONG)(InEnd - In) < Count) || ((ULONG)(OutEnd - Out) < Count))
            return -LX_EIO;
        RtlCopyMemory(Out, In, Count);
        In += Count;
        Out += Count;
        if (In == InEnd)
            break;
        if (InEnd - In < 2)
            return -LX_EIO;
        Distance = In[0] | ((ULONG)In[1] << 8);
        In += 2;
        Count = Token & 15;
        if (Count == 15)
        {
            do
            {
                if (In >= InEnd)
                    return -LX_EIO;
                Byte = *In++;
                Count += Byte;
                if (Count > Capacity)
                    return -LX_EIO;
            } while (Byte == 255);
        }
        Count += 4;
        if ((Distance == 0) || (Distance > (ULONG)(Out - Output)) || ((ULONG)(OutEnd - Out) < Count))
            return -LX_EIO;
        while (Count-- != 0)
        {
            *Out = Out[-(LONG_PTR)Distance];
            Out++;
        }
    }
    *Produced = (ULONG)(Out - Output);
    return 0;
}

static
LONG
LxSquashfsDecode(
    _In_ PLX_SQUASHFS Squash,
    _In_reads_(Length) const UCHAR *Input,
    _In_ ULONG Length,
    _Out_writes_to_(Capacity, *Produced) PUCHAR Output,
    _In_ ULONG Capacity,
    _Out_ PULONG Produced)
{
    switch (Squash->Super.Compression)
    {
        case LX_SQUASHFS_ZLIB:
            return LxSquashfsZlib(Squash, Input, Length, Output, Capacity, Produced);
        case LX_SQUASHFS_LZO:
            return LxSquashfsLzo(Input, Length, Output, Capacity, Produced);
        case LX_SQUASHFS_XZ:
            return LxSquashfsXz(Squash, Input, Length, Output, Capacity, Produced);
        case LX_SQUASHFS_LZ4:
            return LxSquashfsLz4(Input, Length, Output, Capacity, Produced);
        default:
            *Produced = 0;
            return -LX_EIO;
    }
}

static
LONG
LxSquashfsMetaFetch(
    _In_ PLX_SQUASHFS Squash,
    _In_ ULONG64 Start,
    _Out_ PLX_SQUASHFS_META *Result)
{
    PLX_SQUASHFS_META Meta;
    USHORT Header;
    ULONG Index, Length, Produced;
    LONG Error;

    for (Index = 0; Index < LX_SQUASHFS_META_CACHE; Index++)
    {
        if (Squash->Meta[Index].Valid && (Squash->Meta[Index].Start == Start))
        {
            *Result = &Squash->Meta[Index];
            return 0;
        }
    }
    Meta = &Squash->Meta[Squash->MetaNext];
    Squash->MetaNext = (Squash->MetaNext + 1) % LX_SQUASHFS_META_CACHE;
    Meta->Valid = FALSE;
    Error = LxSquashfsReadRaw(Squash, Start, &Header, sizeof(Header));
    if (Error != 0)
        return Error;
    Length = Header & ~LX_SQUASHFS_METADATA_RAW;
    if ((Length == 0) || (Length > LX_SQUASHFS_METADATA_SIZE))
        return -LX_EIO;
    Error = LxSquashfsReadRaw(Squash, Start + sizeof(Header), Squash->Raw, Length);
    if (Error != 0)
        return Error;
    if (Header & LX_SQUASHFS_METADATA_RAW)
    {
        RtlCopyMemory(Meta->Data, Squash->Raw, Length);
        Produced = Length;
    }
    else
    {
        Error = LxSquashfsDecode(Squash, Squash->Raw, Length, Meta->Data, LX_SQUASHFS_METADATA_SIZE, &Produced);
        if (Error != 0)
            return Error;
        if (Produced == 0)
            return -LX_EIO;
    }
    Meta->Start = Start;
    Meta->Next = Start + sizeof(Header) + Length;
    Meta->Length = Produced;
    Meta->Valid = TRUE;
    *Result = Meta;
    return 0;
}

static
LONG
LxSquashfsMetaRead(
    _In_ PLX_SQUASHFS Squash,
    _Inout_ PLX_SQUASHFS_CURSOR Cursor,
    _Out_writes_bytes_opt_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PUCHAR Out = Buffer;
    LONG Error = 0;

    LxSquashfsLock(Squash);
    while (Length != 0)
    {
        PLX_SQUASHFS_META Meta;
        ULONG Chunk;

        Error = LxSquashfsMetaFetch(Squash, Cursor->Block, &Meta);
        if (Error != 0)
            break;
        if (Cursor->Offset >= Meta->Length)
        {
            if (Cursor->Offset != Meta->Length)
            {
                Error = -LX_EIO;
                break;
            }
            Cursor->Block = Meta->Next;
            Cursor->Offset = 0;
            continue;
        }
        Chunk = min(Length, Meta->Length - Cursor->Offset);
        if (Out != NULL)
        {
            RtlCopyMemory(Out, Meta->Data + Cursor->Offset, Chunk);
            Out += Chunk;
        }
        Length -= Chunk;
        Cursor->Offset += Chunk;
    }
    LxSquashfsUnlock(Squash);
    return Error;
}

static
LONG
LxSquashfsLoadIds(
    _In_ PLX_SQUASHFS Squash)
{
    ULONG Count = Squash->Super.NoIds, Blocks = (Count * sizeof(ULONG) + LX_SQUASHFS_METADATA_SIZE - 1) /
                                                LX_SQUASHFS_METADATA_SIZE;
    PULONG64 Index = LxAllocPaged(Blocks * sizeof(ULONG64));
    ULONG Block, Done = 0;
    LONG Error;

    Squash->Ids = LxAllocPaged(Count * sizeof(ULONG));
    if ((Index == NULL) || (Squash->Ids == NULL))
    {
        LxFreePaged(Index);
        return -LX_ENOMEM;
    }
    Error = LxSquashfsReadRaw(Squash, Squash->Super.IdTable, Index, Blocks * sizeof(ULONG64));
    for (Block = 0; (Error == 0) && (Block < Blocks); Block++)
    {
        LX_SQUASHFS_CURSOR Cursor = { Index[Block], 0 };
        ULONG Chunk = min(Count - Done, LX_SQUASHFS_METADATA_SIZE / sizeof(ULONG));

        Error = LxSquashfsMetaRead(Squash, &Cursor, Squash->Ids + Done, Chunk * sizeof(ULONG));
        Done += Chunk;
    }
    LxFreePaged(Index);
    return Error;
}

static
LONG
LxSquashfsLoadFragments(
    _In_ PLX_SQUASHFS Squash)
{
    if (Squash->Super.Fragments == 0)
        return 0;
    Squash->FragmentBlocks = (ULONG)((Squash->Super.Fragments + LX_SQUASHFS_FRAGMENTS_PER_BLOCK - 1) /
                                     LX_SQUASHFS_FRAGMENTS_PER_BLOCK);
    Squash->FragmentIndex = LxAllocPaged(Squash->FragmentBlocks * sizeof(ULONG64));
    if (Squash->FragmentIndex == NULL)
        return -LX_ENOMEM;
    return LxSquashfsReadRaw(Squash, Squash->Super.FragmentTable, Squash->FragmentIndex,
                             Squash->FragmentBlocks * sizeof(ULONG64));
}

static
LONG
LxSquashfsLoadXattrs(
    _In_ PLX_SQUASHFS Squash)
{
    LX_SQUASHFS_XATTR_TABLE Table;
    ULONG64 Blocks;
    LONG Error;

    if (Squash->Super.XattrIdTable == LX_SQUASHFS_NO_TABLE)
        return 0;
    Error = LxSquashfsReadRaw(Squash, Squash->Super.XattrIdTable, &Table, sizeof(Table));
    if ((Error != 0) || (Table.Ids == 0))
        return Error;
    Blocks = ((ULONG64)Table.Ids * sizeof(LX_SQUASHFS_XATTR_ID) + LX_SQUASHFS_METADATA_SIZE - 1) /
             LX_SQUASHFS_METADATA_SIZE;
    if ((Table.Start >= Squash->Super.BytesUsed) || (Blocks * sizeof(ULONG64) > Squash->Super.BytesUsed))
        return -LX_EIO;
    Squash->XattrIndex = LxAllocPaged((SIZE_T)Blocks * sizeof(ULONG64));
    if (Squash->XattrIndex == NULL)
        return -LX_ENOMEM;
    Error = LxSquashfsReadRaw(Squash, Squash->Super.XattrIdTable + sizeof(Table), Squash->XattrIndex,
                              (ULONG)(Blocks * sizeof(ULONG64)));
    if (Error != 0)
        return Error;
    Squash->XattrStart = Table.Start;
    Squash->XattrIds = Table.Ids;
    return 0;
}

static
LONG
LxSquashfsXattrCursor(
    _In_ PLX_SQUASHFS Squash,
    _In_ ULONG64 Reference,
    _Out_ PLX_SQUASHFS_CURSOR Cursor)
{
    Cursor->Block = Squash->XattrStart + (Reference >> 16);
    Cursor->Offset = (ULONG)(Reference & 0xFFFF);
    if ((Cursor->Offset >= LX_SQUASHFS_METADATA_SIZE) ||
        ((Reference >> 16) >= Squash->Super.BytesUsed - Squash->XattrStart))
    {
        return -LX_EIO;
    }
    return 0;
}

static
LONG
LxSquashfsXattrEntry(
    _In_ PLX_SQUASHFS Squash,
    _Inout_ PLX_SQUASHFS_CURSOR Cursor,
    _Inout_updates_bytes_(LX_XATTR_BLOB_MAX) PUCHAR Blob,
    _Inout_ PULONG Used)
{
    static const PCSTR Prefixes[] = { "user.", "trusted.", "security." };
    USHORT Entry[2], Lengths[2];
    LX_SQUASHFS_CURSOR ValueCursor;
    PLX_SQUASHFS_CURSOR Value = Cursor;
    ULONG Type, PrefixLength, NameLength, ValueLength, Offset = *Used;
    ULONG64 Reference;
    LONG Error;

    Error = LxSquashfsMetaRead(Squash, Cursor, Entry, sizeof(Entry));
    if (Error != 0)
        return Error;
    Type = Entry[0] & LX_SQUASHFS_XATTR_PREFIX_MASK;
    if (Type >= RTL_NUMBER_OF(Prefixes))
    {
        Error = LxSquashfsMetaRead(Squash, Cursor, NULL, Entry[1]);
        if (Error == 0)
            Error = LxSquashfsMetaRead(Squash, Cursor, &ValueLength, sizeof(ValueLength));
        if (Error == 0)
            Error = LxSquashfsMetaRead(Squash, Cursor, NULL, ValueLength);
        return Error;
    }
    PrefixLength = (ULONG)strlen(Prefixes[Type]);
    NameLength = PrefixLength + Entry[1];
    if ((Entry[1] == 0) || (NameLength > LX_XATTR_NAME_MAX) || (Offset + 4 + NameLength > LX_XATTR_BLOB_MAX))
        return -LX_EIO;
    RtlCopyMemory(Blob + Offset + 4, Prefixes[Type], PrefixLength);
    Error = LxSquashfsMetaRead(Squash, Cursor, Blob + Offset + 4 + PrefixLength, Entry[1]);
    if (Error == 0)
        Error = LxSquashfsMetaRead(Squash, Cursor, &ValueLength, sizeof(ValueLength));
    if ((Error == 0) && (Entry[0] & LX_SQUASHFS_XATTR_VALUE_OOL))
    {
        if (ValueLength != sizeof(Reference))
            return -LX_EIO;
        Error = LxSquashfsMetaRead(Squash, Cursor, &Reference, sizeof(Reference));
        if (Error == 0)
            Error = LxSquashfsXattrCursor(Squash, Reference, &ValueCursor);
        if (Error == 0)
            Error = LxSquashfsMetaRead(Squash, &ValueCursor, &ValueLength, sizeof(ValueLength));
        Value = &ValueCursor;
    }
    if (Error != 0)
        return Error;
    if ((ValueLength > MAXUSHORT) || (Offset + 4 + NameLength + ValueLength > LX_XATTR_BLOB_MAX))
        return -LX_EIO;
    Error = LxSquashfsMetaRead(Squash, Value, Blob + Offset + 4 + NameLength, ValueLength);
    if (Error != 0)
        return Error;
    Lengths[0] = (USHORT)NameLength;
    Lengths[1] = (USHORT)ValueLength;
    RtlCopyMemory(Blob + Offset, Lengths, sizeof(Lengths));
    *Used = Offset + 4 + NameLength + ValueLength;
    return 0;
}

LONG
LxSquashfsReadXattrs(
    _In_ PLX_SQUASHFS Squash,
    _In_ PLX_SQUASHFS_INODE Info,
    _Outptr_result_bytebuffer_maybenull_(*Length) PUCHAR *Result,
    _Out_ PULONG Length)
{
    ULONG64 Position = (ULONG64)Info->Xattr * sizeof(LX_SQUASHFS_XATTR_ID);
    LX_SQUASHFS_CURSOR Cursor;
    LX_SQUASHFS_XATTR_ID Id;
    PUCHAR Blob;
    ULONG Index, Used = 0;
    LONG Error;

    *Result = NULL;
    *Length = 0;
    if ((Info->Xattr == LX_SQUASHFS_NO_XATTR) || (Squash->XattrIndex == NULL))
        return 0;
    if (Info->Xattr >= Squash->XattrIds)
        return -LX_EIO;
    Cursor.Block = Squash->XattrIndex[Position / LX_SQUASHFS_METADATA_SIZE];
    Cursor.Offset = (ULONG)(Position % LX_SQUASHFS_METADATA_SIZE);
    Error = LxSquashfsMetaRead(Squash, &Cursor, &Id, sizeof(Id));
    if (Error == 0)
        Error = LxSquashfsXattrCursor(Squash, Id.Reference, &Cursor);
    if ((Error != 0) || (Id.Count == 0))
        return Error;
    Blob = LxAllocPaged(LX_XATTR_BLOB_MAX);
    if (Blob == NULL)
        return -LX_ENOMEM;
    for (Index = 0; (Error == 0) && (Index < Id.Count); Index++)
        Error = LxSquashfsXattrEntry(Squash, &Cursor, Blob, &Used);
    if ((Error == 0) && (Used != 0))
    {
        *Result = LxAlloc(Used);
        if (*Result == NULL)
            Error = -LX_ENOMEM;
        else
        {
            RtlCopyMemory(*Result, Blob, Used);
            *Length = Used;
        }
    }
    LxFreePaged(Blob);
    return Error;
}

static
LONG
LxSquashfsId(
    _In_ PLX_SQUASHFS Squash,
    _In_ USHORT Index,
    _Out_ PULONG Id)
{
    if (Index >= Squash->Super.NoIds)
        return -LX_EIO;
    *Id = Squash->Ids[Index];
    return 0;
}

static
LONG
LxSquashfsInodeCursor(
    _In_ PLX_SQUASHFS Squash,
    _In_ ULONG64 Inode,
    _Out_ PLX_SQUASHFS_CURSOR Cursor)
{
    Cursor->Block = Squash->Super.InodeTable + (Inode >> 16);
    Cursor->Offset = (ULONG)(Inode & 0xFFFF);
    if ((Cursor->Offset >= LX_SQUASHFS_METADATA_SIZE) || ((Inode >> 16) >= Squash->Super.BytesUsed))
        return -LX_EIO;
    return 0;
}

LONG
LxSquashfsReadInode(
    _In_ PLX_SQUASHFS Squash,
    _In_ ULONG64 Inode,
    _Out_ PLX_SQUASHFS_INODE Info)
{
    LX_SQUASHFS_CURSOR Cursor;
    LX_SQUASHFS_BASE Base;
    ULONG Fields[10];
    LONG Error;

    RtlZeroMemory(Info, sizeof(*Info));
    Error = LxSquashfsInodeCursor(Squash, Inode, &Cursor);
    if (Error == 0)
        Error = LxSquashfsMetaRead(Squash, &Cursor, &Base, sizeof(Base));
    if (Error == 0)
        Error = LxSquashfsId(Squash, Base.Uid, &Info->Uid);
    if (Error == 0)
        Error = LxSquashfsId(Squash, Base.Gid, &Info->Gid);
    if (Error != 0)
        return Error;
    Info->Mtime = Base.Mtime;
    Info->Number = Base.Number;
    Info->Nlink = 1;
    Info->Mode = Base.Mode & 07777;
    Info->Xattr = LX_SQUASHFS_NO_XATTR;

    switch (Base.Type)
    {
        case LX_SQUASHFS_DIR:
            Error = LxSquashfsMetaRead(Squash, &Cursor, Fields, 16);
            if (Error != 0)
                return Error;
            Info->Mode |= LX_S_IFDIR;
            Info->DirBlock = Fields[0];
            Info->Nlink = Fields[1];
            Info->Size = Fields[2] & 0xFFFF;
            Info->DirOffset = Fields[2] >> 16;
            break;

        case LX_SQUASHFS_LDIR:
            Error = LxSquashfsMetaRead(Squash, &Cursor, Fields, 24);
            if (Error != 0)
                return Error;
            Info->Mode |= LX_S_IFDIR;
            Info->Nlink = Fields[0];
            Info->Size = Fields[1];
            Info->DirBlock = Fields[2];
            Info->DirOffset = Fields[4] >> 16;
            Info->Xattr = Fields[5];
            break;

        case LX_SQUASHFS_REG:
            Error = LxSquashfsMetaRead(Squash, &Cursor, Fields, 16);
            if (Error != 0)
                return Error;
            Info->Mode |= LX_S_IFREG;
            Info->DataStart = Fields[0];
            Info->Fragment = Fields[1];
            Info->FragmentOffset = Fields[2];
            Info->Size = Fields[3];
            break;

        case LX_SQUASHFS_LREG:
            Error = LxSquashfsMetaRead(Squash, &Cursor, Fields, 40);
            if (Error != 0)
                return Error;
            Info->Mode |= LX_S_IFREG;
            Info->DataStart = Fields[0] | ((ULONG64)Fields[1] << 32);
            Info->Size = Fields[2] | ((ULONG64)Fields[3] << 32);
            Info->Nlink = Fields[6];
            Info->Fragment = Fields[7];
            Info->FragmentOffset = Fields[8];
            Info->Xattr = Fields[9];
            break;

        case LX_SQUASHFS_SYMLINK:
        case LX_SQUASHFS_LSYMLINK:
            Error = LxSquashfsMetaRead(Squash, &Cursor, Fields, 8);
            if (Error != 0)
                return Error;
            Info->Mode |= LX_S_IFLNK;
            Info->Nlink = Fields[0];
            Info->Size = Fields[1];
            if ((Info->Size == 0) || (Info->Size >= LX_PATH_MAX))
                return -LX_EIO;
            if (Base.Type == LX_SQUASHFS_LSYMLINK)
            {
                LX_SQUASHFS_CURSOR Xattr = Cursor;

                Error = LxSquashfsMetaRead(Squash, &Xattr, NULL, (ULONG)Info->Size);
                if (Error == 0)
                    Error = LxSquashfsMetaRead(Squash, &Xattr, &Info->Xattr, sizeof(Info->Xattr));
                if (Error != 0)
                    return Error;
            }
            break;

        case LX_SQUASHFS_BLKDEV:
        case LX_SQUASHFS_CHRDEV:
        case LX_SQUASHFS_LBLKDEV:
        case LX_SQUASHFS_LCHRDEV:
        {
            ULONG Major, Minor;

            Error = LxSquashfsMetaRead(Squash, &Cursor, Fields, (Base.Type >= LX_SQUASHFS_LBLKDEV) ? 12 : 8);
            if (Error != 0)
                return Error;
            if (Base.Type >= LX_SQUASHFS_LBLKDEV)
                Info->Xattr = Fields[2];
            Info->Mode |= ((Base.Type == LX_SQUASHFS_BLKDEV) || (Base.Type == LX_SQUASHFS_LBLKDEV)) ?
                          LX_S_IFBLK : LX_S_IFCHR;
            Info->Nlink = Fields[0];
            Major = (Fields[1] >> 8) & 0xFFF;
            Minor = (Fields[1] & 0xFF) | ((Fields[1] >> 12) & 0xFFF00);
            Info->Device = ((ULONG64)Major << 32) | Minor;
            break;
        }

        case LX_SQUASHFS_FIFO:
        case LX_SQUASHFS_SOCKET:
        case LX_SQUASHFS_LFIFO:
        case LX_SQUASHFS_LSOCKET:
            Error = LxSquashfsMetaRead(Squash, &Cursor, Fields, (Base.Type >= LX_SQUASHFS_LFIFO) ? 8 : 4);
            if (Error != 0)
                return Error;
            if (Base.Type >= LX_SQUASHFS_LFIFO)
                Info->Xattr = Fields[1];
            Info->Mode |= ((Base.Type == LX_SQUASHFS_FIFO) || (Base.Type == LX_SQUASHFS_LFIFO)) ?
                          LX_S_IFIFO : LX_S_IFSOCK;
            Info->Nlink = Fields[0];
            break;

        default:
            return -LX_EIO;
    }
    Info->ListBlock = Cursor.Block;
    Info->ListOffset = Cursor.Offset;
    return 0;
}

LONG
LxSquashfsReadLink(
    _In_ PLX_SQUASHFS Squash,
    _In_ PLX_SQUASHFS_INODE Info,
    _Out_ PSTR *Target)
{
    LX_SQUASHFS_CURSOR Cursor = { Info->ListBlock, Info->ListOffset };
    PSTR Buffer;
    LONG Error;

    *Target = NULL;
    if (((Info->Mode & LX_S_IFMT) != LX_S_IFLNK) || (Info->Size >= LX_PATH_MAX))
        return -LX_EINVAL;
    Buffer = LxAlloc((SIZE_T)Info->Size + 1);
    if (Buffer == NULL)
        return -LX_ENOMEM;
    Error = LxSquashfsMetaRead(Squash, &Cursor, Buffer, (ULONG)Info->Size);
    if ((Error == 0) && (memchr(Buffer, 0, (SIZE_T)Info->Size) != NULL))
        Error = -LX_EIO;
    if (Error != 0)
    {
        LxFree(Buffer);
        return Error;
    }
    Buffer[Info->Size] = 0;
    *Target = Buffer;
    return 0;
}

static
BOOLEAN
LxSquashfsValidName(
    _In_reads_(Length) PCSTR Name,
    _In_ ULONG Length)
{
    if (((Length == 1) && (Name[0] == '.')) || ((Length == 2) && (Name[0] == '.') && (Name[1] == '.')))
        return FALSE;
    return (memchr(Name, '/', Length) == NULL) && (memchr(Name, 0, Length) == NULL);
}

LONG
LxSquashfsListDirectory(
    _In_ PLX_SQUASHFS Squash,
    _In_ ULONG64 Inode,
    _In_ PLX_SQUASHFS_DIR_ROUTINE Routine,
    _In_ PVOID Context)
{
    LX_SQUASHFS_INODE Info;
    LX_SQUASHFS_CURSOR Cursor;
    ULONG64 Left;
    CHAR Name[LX_SQUASHFS_NAME_MAX + 1];
    LONG Error = LxSquashfsReadInode(Squash, Inode, &Info);

    if (Error != 0)
        return Error;
    if ((Info.Mode & LX_S_IFMT) != LX_S_IFDIR)
        return -LX_ENOTDIR;
    if ((Info.DirOffset >= LX_SQUASHFS_METADATA_SIZE) || (Info.DirBlock >= Squash->Super.BytesUsed))
        return -LX_EIO;
    Cursor.Block = Squash->Super.DirectoryTable + Info.DirBlock;
    Cursor.Offset = Info.DirOffset;
    Left = (Info.Size > 3) ? Info.Size - 3 : 0;

    while (Left != 0)
    {
        LX_SQUASHFS_DIR_HEADER Header;
        ULONG Index;

        if (Left < sizeof(Header))
            return -LX_EIO;
        Error = LxSquashfsMetaRead(Squash, &Cursor, &Header, sizeof(Header));
        if (Error != 0)
            return Error;
        Left -= sizeof(Header);
        if ((Header.Count >= LX_SQUASHFS_DIR_COUNT_MAX) || (Header.Start >= Squash->Super.BytesUsed))
            return -LX_EIO;
        for (Index = 0; Index <= Header.Count; Index++)
        {
            LX_SQUASHFS_DIR_ENTRY Entry;
            ULONG Length;

            if (Left < sizeof(Entry))
                return -LX_EIO;
            Error = LxSquashfsMetaRead(Squash, &Cursor, &Entry, sizeof(Entry));
            if (Error != 0)
                return Error;
            Left -= sizeof(Entry);
            Length = (ULONG)Entry.Size + 1;
            if ((Length > LX_SQUASHFS_NAME_MAX) || (Left < Length))
                return -LX_EIO;
            Error = LxSquashfsMetaRead(Squash, &Cursor, Name, Length);
            if (Error != 0)
                return Error;
            Left -= Length;
            Name[Length] = 0;
            if (!LxSquashfsValidName(Name, Length) || (Entry.Offset >= LX_SQUASHFS_METADATA_SIZE))
                return -LX_EIO;
            Error = Routine(Context, Name, Length, ((ULONG64)Header.Start << 16) | Entry.Offset);
            if (Error != 0)
                return Error;
        }
    }
    return 0;
}

static
LONG
LxSquashfsBlock(
    _In_ PLX_SQUASHFS Squash,
    _In_ ULONG64 Start,
    _In_ ULONG Entry,
    _Out_writes_to_(Squash->Super.BlockSize, *Produced) PUCHAR Output,
    _Out_ PULONG Produced)
{
    ULONG Length = Entry & LX_SQUASHFS_DATA_SIZE_MASK;
    LONG Error;

    *Produced = 0;
    if ((Length == 0) || (Length > Squash->Super.BlockSize))
        return -LX_EIO;
    Error = LxSquashfsReadRaw(Squash, Start, Squash->Raw, Length);
    if (Error != 0)
        return Error;
    if (Entry & LX_SQUASHFS_DATA_RAW)
    {
        RtlCopyMemory(Output, Squash->Raw, Length);
        *Produced = Length;
        return 0;
    }
    return LxSquashfsDecode(Squash, Squash->Raw, Length, Output, Squash->Super.BlockSize, Produced);
}

static
LONG
LxSquashfsReadFragment(
    _In_ PLX_SQUASHFS Squash,
    _In_ PLX_SQUASHFS_INODE Info,
    _Out_writes_bytes_(Length) PUCHAR Output,
    _In_ ULONG Length)
{
    LX_SQUASHFS_FRAGMENT Fragment;
    LX_SQUASHFS_CURSOR Cursor;
    LONG Error;

    if ((Info->Fragment >= Squash->Super.Fragments) || (Squash->FragmentIndex == NULL))
        return -LX_EIO;
    Cursor.Block = Squash->FragmentIndex[Info->Fragment / LX_SQUASHFS_FRAGMENTS_PER_BLOCK];
    Cursor.Offset = (Info->Fragment % LX_SQUASHFS_FRAGMENTS_PER_BLOCK) * sizeof(Fragment);
    Error = LxSquashfsMetaRead(Squash, &Cursor, &Fragment, sizeof(Fragment));
    if (Error != 0)
        return Error;

    LxSquashfsLock(Squash);
    if (!Squash->FragmentValid || (Squash->FragmentStart != Fragment.Start))
    {
        Squash->FragmentValid = FALSE;
        Error = LxSquashfsBlock(Squash, Fragment.Start, Fragment.Size, Squash->Fragment, &Squash->FragmentLength);
        if (Error == 0)
        {
            Squash->FragmentStart = Fragment.Start;
            Squash->FragmentValid = TRUE;
        }
    }
    if ((Error == 0) && ((Info->FragmentOffset > Squash->FragmentLength) ||
                         (Length > Squash->FragmentLength - Info->FragmentOffset)))
    {
        Error = -LX_EIO;
    }
    if (Error == 0)
        Error = LxSquashfsCopy(Output, Squash->Fragment + Info->FragmentOffset, Length);
    LxSquashfsUnlock(Squash);
    return Error;
}

LONG
LxSquashfsReadFile(
    _In_ PLX_SQUASHFS Squash,
    _In_ ULONG64 Inode,
    _Out_writes_bytes_(Capacity) PUCHAR View,
    _In_ ULONG64 Capacity)
{
    LX_SQUASHFS_INODE Info;
    LX_SQUASHFS_CURSOR List;
    ULONG BlockSize = Squash->Super.BlockSize;
    ULONG64 Blocks, Index, Done = 0, Disk;
    LONG Error = LxSquashfsReadInode(Squash, Inode, &Info);

    if (Error != 0)
        return Error;
    if (((Info.Mode & LX_S_IFMT) != LX_S_IFREG) || (Info.Size > Capacity))
        return -LX_EIO;
    Blocks = (Info.Fragment == LX_SQUASHFS_NO_FRAGMENT) ? (Info.Size + BlockSize - 1) >> Squash->Super.BlockLog :
                                                          Info.Size >> Squash->Super.BlockLog;
    List.Block = Info.ListBlock;
    List.Offset = Info.ListOffset;
    Disk = Info.DataStart;

    for (Index = 0; Index < Blocks; Index++)
    {
        ULONG Entry, Want = (ULONG)min((ULONG64)BlockSize, Info.Size - Done), Produced;

        Error = LxSquashfsMetaRead(Squash, &List, &Entry, sizeof(Entry));
        if (Error != 0)
            return Error;
        if (Entry != 0)
        {
            LxSquashfsLock(Squash);
            Error = LxSquashfsBlock(Squash, Disk, Entry, Squash->Block, &Produced);
            if ((Error == 0) && (Produced < Want))
                Error = -LX_EIO;
            if (Error == 0)
                Error = LxSquashfsCopy(View + Done, Squash->Block, Want);
            LxSquashfsUnlock(Squash);
            if (Error != 0)
                return Error;
        }
        Disk += Entry & LX_SQUASHFS_DATA_SIZE_MASK;
        Done += Want;
    }
    if ((Info.Fragment != LX_SQUASHFS_NO_FRAGMENT) && (Done < Info.Size))
        Error = LxSquashfsReadFragment(Squash, &Info, View + Done, (ULONG)(Info.Size - Done));
    return Error;
}

VOID
LxSquashfsAddUser(
    _In_ PLX_SQUASHFS Squash)
{
    InterlockedIncrement(&Squash->Users);
}

VOID
LxSquashfsRemoveUser(
    _In_ PLX_SQUASHFS Squash)
{
    PLX_FILE Device;

    if (InterlockedDecrement(&Squash->Users) != 0)
        return;
    LxSquashfsLock(Squash);
    Device = Squash->Device;
    Squash->Device = NULL;
    LxSquashfsUnlock(Squash);
    if (Device != NULL)
        LxFileDereference(Device);
}

ULONG64
LxSquashfsRoot(
    _In_ PLX_SQUASHFS Squash)
{
    return Squash->Super.RootInode;
}

VOID
LxSquashfsStatfs(
    _In_ PLX_SQUASHFS Squash,
    _Out_ PULONG64 Bytes,
    _Out_ PULONG64 Inodes)
{
    *Bytes = Squash->Super.BytesUsed;
    *Inodes = Squash->Super.Inodes;
}

VOID
LxSquashfsClose(
    _In_opt_ PLX_SQUASHFS Squash)
{
    if (Squash == NULL)
        return;
    if (Squash->LzmaReady)
        LzmaDec_FreeProbs(&Squash->Lzma, &LxSquashfsLzmaAllocator);
    if (Squash->ZlibReady)
        inflateEnd(&Squash->Zlib);
    LxFreePaged(Squash->Ids);
    LxFreePaged(Squash->FragmentIndex);
    LxFreePaged(Squash->XattrIndex);
    LxFreePaged(Squash->Raw);
    LxFreePaged(Squash->Block);
    LxFreePaged(Squash->Fragment);
    LxFreePaged(Squash->Meta);
    if (Squash->Device != NULL)
        LxFileDereference(Squash->Device);
    LxFree(Squash);
}

static
LONG
LxSquashfsCheckSuper(
    _In_ PLX_SQUASHFS_SUPER Super,
    _In_ ULONG64 DeviceSize)
{
    if (Super->Magic != LX_SQUASHFS_MAGIC)
        return -LX_EINVAL;
    if ((Super->Major != 4) || (Super->Minor != 0))
    {
        DPRINT1("Linux squashfs: version %u.%u is not supported\n", Super->Major, Super->Minor);
        return -LX_EINVAL;
    }
    if ((Super->BlockLog < 12) || (Super->BlockLog > 20) || (Super->BlockSize != (1UL << Super->BlockLog)))
        return -LX_EINVAL;
    if ((Super->BytesUsed < sizeof(*Super)) || (Super->BytesUsed > DeviceSize) || (Super->NoIds == 0))
        return -LX_EINVAL;
    if ((Super->InodeTable >= Super->BytesUsed) || (Super->DirectoryTable >= Super->BytesUsed) ||
        (Super->IdTable >= Super->BytesUsed) || ((Super->RootInode & 0xFFFF) >= LX_SQUASHFS_METADATA_SIZE) ||
        ((Super->Fragments != 0) && (Super->FragmentTable >= Super->BytesUsed)))
    {
        return -LX_EINVAL;
    }
    switch (Super->Compression)
    {
        case LX_SQUASHFS_ZLIB:
        case LX_SQUASHFS_LZO:
        case LX_SQUASHFS_XZ:
        case LX_SQUASHFS_LZ4:
            return 0;
        case LX_SQUASHFS_ZSTD:
            /* TODO: zstd compressed squashfs images */
        default:
            DPRINT1("Linux squashfs: compression %u is not supported\n", Super->Compression);
            return -LX_EINVAL;
    }
}

LONG
LxSquashfsOpen(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Source,
    _Out_ PLX_SQUASHFS *Result)
{
    PLX_SQUASHFS Squash;
    PLX_FILE Device;
    LX_STAT Stat;
    LONG Error;

    *Result = NULL;
    Error = LxVfsOpenResolve(Process, LX_AT_FDCWD, Source, LX_O_RDONLY, 0, 0, &Device);
    if (Error != 0)
        return Error;
    if (!LxLoopIsDevice(Device))
    {
        Error = ((LxFileStat(Device, &Stat) == 0) && ((Stat.Mode & LX_S_IFMT) == LX_S_IFBLK)) ? -LX_ENXIO :
                                                                                               -LX_ENOTBLK;
        LxFileDereference(Device);
        return Error;
    }
    Squash = LxAlloc(sizeof(*Squash));
    if (Squash == NULL)
    {
        LxFileDereference(Device);
        return -LX_ENOMEM;
    }
    Squash->Device = Device;
    Squash->Users = 1;
    KeInitializeEvent(&Squash->Lock, SynchronizationEvent, TRUE);
    Squash->Super.BytesUsed = sizeof(Squash->Super);
    Error = LxSquashfsReadRaw(Squash, 0, &Squash->Super, sizeof(Squash->Super));
    if (Error == 0)
        Error = LxSquashfsCheckSuper(&Squash->Super, LxLoopCapacity(Device));
    else
        Error = -LX_EINVAL;
    if (Error == 0)
    {
        Squash->RawSize = max(Squash->Super.BlockSize, LX_SQUASHFS_METADATA_SIZE);
        Squash->Raw = LxAllocPaged(Squash->RawSize);
        Squash->Block = LxAllocPaged(Squash->Super.BlockSize);
        Squash->Fragment = LxAllocPaged(Squash->Super.BlockSize);
        Squash->Meta = LxAllocPaged(LX_SQUASHFS_META_CACHE * sizeof(LX_SQUASHFS_META));
        if ((Squash->Raw == NULL) || (Squash->Block == NULL) || (Squash->Fragment == NULL) || (Squash->Meta == NULL))
            Error = -LX_ENOMEM;
    }
    if (Error == 0)
        Error = LxSquashfsLoadIds(Squash);
    if (Error == 0)
        Error = LxSquashfsLoadFragments(Squash);
    if (Error == 0)
        Error = LxSquashfsLoadXattrs(Squash);
    if (Error != 0)
    {
        LxSquashfsClose(Squash);
        return Error;
    }
    *Result = Squash;
    return 0;
}
