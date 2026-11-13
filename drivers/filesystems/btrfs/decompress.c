/*
 * PROJECT:     LiberNT Btrfs file system driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Compressed extents: zlib, LZO1X in btrfs segments, and zstd
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "btrfsp.h"
#include <zlib.h>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

static
voidpf
BtrfsZlibAlloc(
    _In_ voidpf Opaque,
    _In_ uInt Items,
    _In_ uInt Size)
{
    UNREFERENCED_PARAMETER(Opaque);

    if (Size != 0 && Items > MAXULONG / Size) return Z_NULL;
    return ExAllocatePoolUninitialized(PagedPool, (SIZE_T)Items * Size, TAG_BTRFS_IO);
}

static
void
BtrfsZlibFree(
    _In_ voidpf Opaque,
    _In_ voidpf Address)
{
    UNREFERENCED_PARAMETER(Opaque);

    if (Address != NULL) ExFreePoolWithTag(Address, TAG_BTRFS_IO);
}

static
NTSTATUS
BtrfsInflate(
    _In_reads_bytes_(InputLength) const UCHAR *Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_(OutputLength) PUCHAR Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG Produced)
{
    z_stream Stream;
    int Result;

    *Produced = 0;
    RtlZeroMemory(&Stream, sizeof(Stream));
    Stream.zalloc = BtrfsZlibAlloc;
    Stream.zfree = BtrfsZlibFree;
    Stream.next_in = (Bytef *)Input;
    Stream.avail_in = InputLength;
    Stream.next_out = Output;
    Stream.avail_out = OutputLength;

    if (inflateInit(&Stream) != Z_OK) return STATUS_INSUFFICIENT_RESOURCES;

    do
    {
        Result = inflate(&Stream, Z_FINISH);
    } while (Result == Z_OK && Stream.avail_out != 0 && Stream.avail_in != 0);

    *Produced = OutputLength - Stream.avail_out;
    inflateEnd(&Stream);

    if (Result == Z_STREAM_END || Stream.avail_out == 0) return STATUS_SUCCESS;
    return STATUS_FILE_CORRUPT_ERROR;
}

/* LZO1X: each instruction ends by naming 0 to 3 literals to copy; State keeps that count, or 4 after a long literal run */
static
NTSTATUS
BtrfsLzo1xDecompress(
    _In_reads_bytes_(InputLength) const UCHAR *Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_(OutputLength) PUCHAR Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG Produced)
{
    const UCHAR *In = Input, *InEnd = Input + InputLength;
    PUCHAR Out = Output, OutEnd = Output + OutputLength;
    ULONG State = 0;
    ULONG Op, Length, Distance;

#define LZO_NEED_IN(n)  do { if ((SIZE_T)(InEnd - In) < (SIZE_T)(n)) return STATUS_FILE_CORRUPT_ERROR; } while (0)
#define LZO_NEED_OUT(n) do { if ((SIZE_T)(OutEnd - Out) < (SIZE_T)(n)) return STATUS_FILE_CORRUPT_ERROR; } while (0)

    *Produced = 0;
    LZO_NEED_IN(1);

    if (*In > 17)
    {
        Length = *In++ - 17;
        LZO_NEED_IN(Length);
        LZO_NEED_OUT(Length);
        RtlCopyMemory(Out, In, Length);
        Out += Length;
        In += Length;
        State = (Length < 4) ? Length : 4;
    }

    for (;;)
    {
        ULONG Trailing;

        LZO_NEED_IN(1);
        Op = *In++;

        if (Op < 16)
        {
            if (State == 0)
            {
                Length = Op;
                if (Length == 0)
                {
                    Length = 15;
                    for (;;)
                    {
                        LZO_NEED_IN(1);
                        if (*In != 0) break;
                        if (Length > MAXULONG - 255) return STATUS_FILE_CORRUPT_ERROR;
                        Length += 255;
                        In++;
                    }
                    Length += *In++;
                }
                Length += 3;
                LZO_NEED_IN(Length);
                LZO_NEED_OUT(Length);
                RtlCopyMemory(Out, In, Length);
                Out += Length;
                In += Length;
                State = 4;
                continue;
            }

            LZO_NEED_IN(1);
            Distance = ((ULONG)*In++ << 2) + ((Op >> 2) & 3) + ((State == 4) ? 2049 : 1);
            Length = (State == 4) ? 3 : 2;
            Trailing = Op & 3;
        }
        else if (Op < 32)
        {
            ULONG Value;

            Length = Op & 7;
            if (Length == 0)
            {
                Length = 7;
                for (;;)
                {
                    LZO_NEED_IN(1);
                    if (*In != 0) break;
                    if (Length > MAXULONG - 255) return STATUS_FILE_CORRUPT_ERROR;
                    Length += 255;
                    In++;
                }
                Length += *In++;
            }
            Length += 2;
            LZO_NEED_IN(2);
            Value = In[0] | ((ULONG)In[1] << 8);
            In += 2;
            Distance = ((Op & 8) << 11) + (Value >> 2);
            if (Distance == 0) break;
            Distance += 16384;
            Trailing = Value & 3;
        }
        else if (Op < 64)
        {
            ULONG Value;

            Length = Op & 31;
            if (Length == 0)
            {
                Length = 31;
                for (;;)
                {
                    LZO_NEED_IN(1);
                    if (*In != 0) break;
                    if (Length > MAXULONG - 255) return STATUS_FILE_CORRUPT_ERROR;
                    Length += 255;
                    In++;
                }
                Length += *In++;
            }
            Length += 2;
            LZO_NEED_IN(2);
            Value = In[0] | ((ULONG)In[1] << 8);
            In += 2;
            Distance = (Value >> 2) + 1;
            Trailing = Value & 3;
        }
        else
        {
            LZO_NEED_IN(1);
            Distance = ((ULONG)*In++ << 3) + ((Op >> 2) & 7) + 1;
            Length = (Op < 128) ? 3 + ((Op >> 5) & 1) : 5 + ((Op >> 5) & 3);
            Trailing = Op & 3;
        }

        if (Distance > (ULONG)(Out - Output)) return STATUS_FILE_CORRUPT_ERROR;
        LZO_NEED_OUT(Length);
        while (Length-- != 0)
        {
            *Out = *(Out - Distance);
            Out++;
        }

        LZO_NEED_IN(Trailing);
        LZO_NEED_OUT(Trailing);
        RtlCopyMemory(Out, In, Trailing);
        Out += Trailing;
        In += Trailing;
        State = Trailing;
    }

#undef LZO_NEED_IN
#undef LZO_NEED_OUT

    *Produced = (ULONG)(Out - Output);
    return STATUS_SUCCESS;
}

/* A total length, then segments each with a length; a segment header never crosses a sector, which is padded instead */
static
NTSTATUS
BtrfsLzoDecompress(
    _In_ PBTRFS_VCB Vcb,
    _In_reads_bytes_(InputLength) const UCHAR *Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_(OutputLength) PUCHAR Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG Produced)
{
    ULONG Total, Cursor = 4, Done = 0;
    NTSTATUS Status;

    *Produced = 0;
    if (InputLength < 4) return STATUS_FILE_CORRUPT_ERROR;
    RtlCopyMemory(&Total, Input, sizeof(Total));
    if (Total < 4 || Total > InputLength) return STATUS_FILE_CORRUPT_ERROR;

    while (Cursor < Total && Done < OutputLength)
    {
        ULONG Left = Vcb->SectorSize - (Cursor % Vcb->SectorSize);
        ULONG Segment, Got;

        if (Left < 4)
        {
            Cursor += Left;
            if (Cursor >= Total) break;
        }

        if (Total - Cursor < 4) return STATUS_FILE_CORRUPT_ERROR;
        RtlCopyMemory(&Segment, Input + Cursor, sizeof(Segment));
        Cursor += 4;
        if (Segment == 0 || Segment > Total - Cursor) return STATUS_FILE_CORRUPT_ERROR;

        Status = BtrfsLzo1xDecompress(Input + Cursor, Segment, Output + Done, min(Vcb->SectorSize, OutputLength - Done), &Got);
        if (!NT_SUCCESS(Status)) return Status;

        Done += Got;
        Cursor += Segment;
    }

    *Produced = Done;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BtrfsZstdDecompress(
    _In_reads_bytes_(InputLength) const UCHAR *Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_(OutputLength) PUCHAR Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG Produced)
{
    SIZE_T WorkspaceSize = ZSTD_estimateDCtxSize();
    SIZE_T FrameSize, Result;
    ZSTD_DCtx *Context;
    PVOID Workspace;

    *Produced = 0;

    FrameSize = ZSTD_findFrameCompressedSize(Input, InputLength);
    if (ZSTD_isError(FrameSize)) return STATUS_FILE_CORRUPT_ERROR;

    Workspace = ExAllocatePoolUninitialized(PagedPool, WorkspaceSize, TAG_BTRFS_IO);
    if (Workspace == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Context = ZSTD_initStaticDCtx(Workspace, WorkspaceSize);
    if (Context == NULL)
    {
        ExFreePoolWithTag(Workspace, TAG_BTRFS_IO);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Result = ZSTD_decompressDCtx(Context, Output, OutputLength, Input, FrameSize);
    ExFreePoolWithTag(Workspace, TAG_BTRFS_IO);

    if (ZSTD_isError(Result)) return STATUS_FILE_CORRUPT_ERROR;

    *Produced = (ULONG)Result;
    return STATUS_SUCCESS;
}

NTSTATUS
BtrfsDecompress(
    _In_ PBTRFS_VCB Vcb,
    _In_ UCHAR Type,
    _In_reads_bytes_(InputLength) const UCHAR *Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_(OutputLength) PUCHAR Output,
    _In_ ULONG OutputLength)
{
    ULONG Produced = 0;
    NTSTATUS Status;

    ASSERT(OutputLength <= BTRFS_MAX_UNCOMPRESSED || Type == BTRFS_COMPRESS_NONE);

    switch (Type)
    {
        case BTRFS_COMPRESS_ZLIB:
            Status = BtrfsInflate(Input, InputLength, Output, OutputLength, &Produced);
            break;
        case BTRFS_COMPRESS_LZO:
            Status = BtrfsLzoDecompress(Vcb, Input, InputLength, Output, OutputLength, &Produced);
            break;
        case BTRFS_COMPRESS_ZSTD:
            Status = BtrfsZstdDecompress(Input, InputLength, Output, OutputLength, &Produced);
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }

    if (!NT_SUCCESS(Status)) return Status;

    ASSERT(Produced <= OutputLength);
    if (Produced < OutputLength) RtlZeroMemory(Output + Produced, OutputLength - Produced);
    return STATUS_SUCCESS;
}
