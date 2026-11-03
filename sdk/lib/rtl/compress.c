/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * PURPOSE:         Compression and decompression functions
 * FILE:            lib/rtl/compress.c
 * PROGRAMER:       Eric Kohl
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

/* MACROS *******************************************************************/

#define COMPRESSION_FORMAT_MASK  0x00FF
#define COMPRESSION_ENGINE_MASK  0xFF00




/* FUNCTIONS ****************************************************************/

static ULONG
lznt1_compress_chunk(const UCHAR *src, ULONG src_size, UCHAR *dst, ULONG dst_size)
{
    ULONG pos = 0, out = 0, flag_pos, bit, displacement_bits, length_bits;
    ULONG max_length, max_displacement, best_length, best_displacement, length, displacement;
    USHORT code;

    while (pos < src_size)
    {
        if (out >= dst_size)
            return 0;
        flag_pos = out++;
        dst[flag_pos] = 0;

        for (bit = 0; bit < 8 && pos < src_size; bit++)
        {
            for (displacement_bits = 12; displacement_bits > 4; displacement_bits--)
                if ((1UL << (displacement_bits - 1)) < pos) break;
            length_bits = 16 - displacement_bits;
            max_length = min((1UL << length_bits) + 2, src_size - pos);
            max_displacement = min(pos, 1UL << displacement_bits);

            best_length = 0;
            best_displacement = 0;
            for (displacement = 1; displacement <= max_displacement; displacement++)
            {
                if (src[pos] != src[pos - displacement])
                    continue;
                for (length = 1; length < max_length; length++)
                {
                    if (src[pos + length] != src[pos + length - displacement])
                        break;
                }
                if (length > best_length)
                {
                    best_length = length;
                    best_displacement = displacement;
                    if (length == max_length)
                        break;
                }
            }

            if (best_length >= 3)
            {
                if (out + sizeof(WORD) > dst_size)
                    return 0;
                code = (USHORT)(((best_displacement - 1) << length_bits) | (best_length - 3));
                dst[out++] = (UCHAR)code;
                dst[out++] = (UCHAR)(code >> 8);
                dst[flag_pos] |= (UCHAR)(1 << bit);
                pos += best_length;
            }
            else
            {
                if (out >= dst_size)
                    return 0;
                dst[out++] = src[pos++];
            }
        }
    }

    return out;
}

static NTSTATUS
RtlpCompressBufferLZNT1(UCHAR *src, ULONG src_size, UCHAR *dst, ULONG dst_size,
                        ULONG chunk_size, ULONG *final_size, UCHAR *workspace)
{
        UCHAR *src_cur = src, *src_end = src + src_size;
        UCHAR *dst_cur = dst, *dst_end = dst + dst_size;
        ULONG block_size, packed_size;

        while (src_cur < src_end)
        {
            block_size = min(0x1000, src_end - src_cur);
            if (dst_cur + sizeof(WORD) > dst_end)
                return STATUS_BUFFER_TOO_SMALL;

            packed_size = lznt1_compress_chunk(src_cur, block_size, dst_cur + sizeof(WORD),
                                               min(block_size - 1, (ULONG)(dst_end - dst_cur - sizeof(WORD))));
            if (packed_size)
            {
                *(WORD *)dst_cur = 0xB000 | (packed_size - 1);
                dst_cur += sizeof(WORD) + packed_size;
            }
            else
            {
                if (dst_cur + sizeof(WORD) + block_size > dst_end)
                    return STATUS_BUFFER_TOO_SMALL;

                *(WORD *)dst_cur = 0x3000 | (block_size - 1);
                dst_cur += sizeof(WORD);
                memcpy(dst_cur, src_cur, block_size);
                dst_cur += block_size;
            }
            src_cur += block_size;
        }

        if (final_size)
            *final_size = dst_cur - dst;

        return STATUS_SUCCESS;
}


static NTSTATUS
RtlpWorkSpaceSizeLZNT1(USHORT Engine,
                       PULONG BufferAndWorkSpaceSize,
                       PULONG FragmentWorkSpaceSize)
{
   if (Engine == COMPRESSION_ENGINE_STANDARD)
   {
      *BufferAndWorkSpaceSize = 0x8010;
      *FragmentWorkSpaceSize = 0x1000;
      return(STATUS_SUCCESS);
   }
   else if (Engine == COMPRESSION_ENGINE_MAXIMUM)
   {
      *BufferAndWorkSpaceSize = 0x10;
      *FragmentWorkSpaceSize = 0x1000;
      return(STATUS_SUCCESS);
   }

   return(STATUS_NOT_SUPPORTED);
}


/*
 * @implemented
 */
NTSTATUS NTAPI
RtlCompressBuffer(IN USHORT CompressionFormatAndEngine,
                  IN PUCHAR UncompressedBuffer,
                  IN ULONG UncompressedBufferSize,
                  OUT PUCHAR CompressedBuffer,
                  IN ULONG CompressedBufferSize,
                  IN ULONG UncompressedChunkSize,
                  OUT PULONG FinalCompressedSize,
                  IN PVOID WorkSpace)
{
   USHORT Format = CompressionFormatAndEngine & COMPRESSION_FORMAT_MASK;
   /* USHORT Engine = CompressionFormatAndEngine & COMPRESSION_ENGINE_MASK; */

   if ((Format == COMPRESSION_FORMAT_NONE) ||
         (Format == COMPRESSION_FORMAT_DEFAULT))
      return(STATUS_INVALID_PARAMETER);

   if (Format == COMPRESSION_FORMAT_LZNT1)
      return(RtlpCompressBufferLZNT1(UncompressedBuffer,
                                     UncompressedBufferSize,
                                     CompressedBuffer,
                                     CompressedBufferSize,
                                     UncompressedChunkSize,
                                     FinalCompressedSize,
                                     WorkSpace));

   return(STATUS_UNSUPPORTED_COMPRESSION);
}


/*
 * @unimplemented
 */
NTSTATUS NTAPI
RtlCompressChunks(IN PUCHAR UncompressedBuffer,
                  IN ULONG UncompressedBufferSize,
                  OUT PUCHAR CompressedBuffer,
                  IN ULONG CompressedBufferSize,
                  IN OUT PCOMPRESSED_DATA_INFO CompressedDataInfo,
                  IN ULONG CompressedDataInfoLength,
                  IN PVOID WorkSpace)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS NTAPI
RtlDecompressChunks(OUT PUCHAR UncompressedBuffer,
                    IN ULONG UncompressedBufferSize,
                    IN PUCHAR CompressedBuffer,
                    IN ULONG CompressedBufferSize,
                    IN PUCHAR CompressedTail,
                    IN ULONG CompressedTailSize,
                    IN PCOMPRESSED_DATA_INFO CompressedDataInfo)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS NTAPI
RtlDescribeChunk(IN USHORT CompressionFormat,
                 IN OUT PUCHAR *CompressedBuffer,
                 IN PUCHAR EndOfCompressedBufferPlus1,
                 OUT PUCHAR *ChunkBuffer,
                 OUT PULONG ChunkSize)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}


/*
 * @unimplemented
 */
NTSTATUS NTAPI
RtlGetCompressionWorkSpaceSize(IN USHORT CompressionFormatAndEngine,
                               OUT PULONG CompressBufferAndWorkSpaceSize,
                               OUT PULONG CompressFragmentWorkSpaceSize)
{
   USHORT Format = CompressionFormatAndEngine & COMPRESSION_FORMAT_MASK;
   USHORT Engine = CompressionFormatAndEngine & COMPRESSION_ENGINE_MASK;

   if ((Format == COMPRESSION_FORMAT_NONE) ||
         (Format == COMPRESSION_FORMAT_DEFAULT))
      return(STATUS_INVALID_PARAMETER);

   if (Format == COMPRESSION_FORMAT_LZNT1)
      return(RtlpWorkSpaceSizeLZNT1(Engine,
                                    CompressBufferAndWorkSpaceSize,
                                    CompressFragmentWorkSpaceSize));

   return(STATUS_UNSUPPORTED_COMPRESSION);
}



/*
 * @unimplemented
 */
NTSTATUS NTAPI
RtlReserveChunk(IN USHORT CompressionFormat,
                IN OUT PUCHAR *CompressedBuffer,
                IN PUCHAR EndOfCompressedBufferPlus1,
                OUT PUCHAR *ChunkBuffer,
                IN ULONG ChunkSize)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
