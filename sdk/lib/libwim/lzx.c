/*
 * PROJECT:     LiberNT WIM library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     LZX decompression of WIM resource chunks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lzx.h"
#include <stdint.h>
#include <string.h>

#define LZX_MAIN_SYMBOLS (256 + 30 * 8)
#define LZX_LENGTH_SYMBOLS 249
#define LZX_PRECODE_SYMBOLS 20
#define LZX_ALIGNED_SYMBOLS 8
#define LZX_MAX_CODE_LENGTH 16
#define LZX_HUFFMAN_MAX_SYMBOLS 512
#define LZX_HUFFMAN_LENGTHS 17
#define LZX_DEFAULT_BLOCK_SIZE 32768
#define LZX_E8_FILE_SIZE 12000000

typedef struct {
    const uint8_t* next;
    const uint8_t* end;
    uint32_t bit_buffer;
    uint32_t bits_left;
    uint32_t padding_units;
    int failed;
} LzxBitStream;

typedef struct {
    uint32_t first_code[LZX_HUFFMAN_LENGTHS];
    uint16_t first_symbol[LZX_HUFFMAN_LENGTHS];
    uint16_t counts[LZX_HUFFMAN_LENGTHS];
    uint16_t sorted_symbols[LZX_HUFFMAN_MAX_SYMBOLS];
    uint16_t* fast_table;
    uint32_t fast_bits;
    uint32_t maximum_bits;
    int empty;
} LzxHuffman;

typedef struct {
    LzxHuffman main_code;
    LzxHuffman length_code;
    LzxHuffman pre_code;
    LzxHuffman aligned_code;
    uint16_t main_fast[1 << 11];
    uint16_t length_fast[1 << 10];
    uint16_t pre_fast[1 << 6];
    uint16_t aligned_fast[1 << 7];
    uint8_t main_lengths[LZX_MAIN_SYMBOLS];
    uint8_t length_lengths[LZX_LENGTH_SYMBOLS];
    uint8_t pre_lengths[LZX_PRECODE_SYMBOLS];
    uint8_t aligned_lengths[LZX_ALIGNED_SYMBOLS];
} LzxWorkspace;

_Static_assert(sizeof(LzxWorkspace) <= LZX_DECOMPRESS_WORKSPACE_SIZE, "LZX workspace too small");

static const uint8_t lzx_extra_offset_bits[30] =
{
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
    4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static const uint32_t lzx_offset_slot_base[31] =
{
    0, 1, 2, 3, 4, 6, 8, 12, 16, 24,
    32, 48, 64, 96, 128, 192, 256, 384, 512, 768,
    1024, 1536, 2048, 3072, 4096, 6144, 8192, 12288,
    16384, 24576, 32768
};

static uint16_t
lzx_read_le16(const uint8_t* buffer)
{
    return (uint16_t)(buffer[0] | (buffer[1] << 8));
}

static uint32_t
lzx_read_le32(const uint8_t* buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8) |
           ((uint32_t)buffer[2] << 16) |
           ((uint32_t)buffer[3] << 24);
}

static void
lzx_write_le32(uint8_t* buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
}

static void
lzx_init_stream(LzxBitStream* stream, const uint8_t* buffer, uint32_t size)
{
    stream->next = buffer;
    stream->end = buffer + size;
    stream->bit_buffer = 0;
    stream->bits_left = 0;
    stream->padding_units = 0;
    stream->failed = 0;
}

static void
lzx_ensure_bits(LzxBitStream* stream, uint32_t count)
{
    if (stream->failed || count > 16 || stream->bits_left >= count)
        return;

    if ((size_t)(stream->end - stream->next) < sizeof(uint16_t))
    {
        if (stream->padding_units != 0)
        {
            stream->failed = 1;
            return;
        }
        stream->padding_units++;
        stream->bits_left += 16;
        return;
    }

    stream->bit_buffer |= (uint32_t)lzx_read_le16(stream->next) << (16 - stream->bits_left);
    stream->next += sizeof(uint16_t);
    stream->bits_left += 16;
}

static uint32_t
lzx_peek_bits(const LzxBitStream* stream, uint32_t count)
{
    return count ? stream->bit_buffer >> (32 - count) : 0;
}

static void
lzx_remove_bits(LzxBitStream* stream, uint32_t count)
{
    if (stream->failed || count > stream->bits_left)
    {
        stream->failed = 1;
        return;
    }

    stream->bit_buffer <<= count;
    stream->bits_left -= count;
}

static uint32_t
lzx_read_bits(LzxBitStream* stream, uint32_t count)
{
    uint32_t value;

    if (count == 0)
        return 0;
    lzx_ensure_bits(stream, count);
    if (stream->failed)
        return 0;
    value = lzx_peek_bits(stream, count);
    lzx_remove_bits(stream, count);
    return value;
}

static uint8_t
lzx_read_byte(LzxBitStream* stream)
{
    if (stream->next >= stream->end)
    {
        stream->failed = 1;
        return 0;
    }
    return *stream->next++;
}

static uint32_t
lzx_read_u32(LzxBitStream* stream)
{
    uint32_t value;

    if ((size_t)(stream->end - stream->next) < sizeof(uint32_t))
    {
        stream->failed = 1;
        return 0;
    }
    value = lzx_read_le32(stream->next);
    stream->next += sizeof(uint32_t);
    return value;
}

static int
lzx_build_huffman(LzxHuffman* decoder,
                  const uint8_t* lengths,
                  uint32_t symbol_count,
                  uint32_t maximum_bits,
                  uint32_t fast_bits,
                  uint16_t* fast_table)
{
    uint16_t offsets[LZX_HUFFMAN_LENGTHS];
    uint32_t next_code[LZX_HUFFMAN_LENGTHS];
    uint32_t total_symbols = 0;
    uint32_t code, length, symbol;
    int32_t remaining;

    memset(decoder, 0, sizeof(*decoder));
    decoder->fast_table = fast_table;
    decoder->fast_bits = fast_bits;
    decoder->maximum_bits = maximum_bits;
    memset(fast_table, 0, ((size_t)1 << fast_bits) * sizeof(uint16_t));

    for (symbol = 0; symbol < symbol_count; symbol++)
    {
        length = lengths[symbol];
        if (length > maximum_bits)
            return 0;
        if (length != 0)
        {
            decoder->counts[length]++;
            total_symbols++;
        }
    }

    if (total_symbols == 0)
    {
        decoder->empty = 1;
        return 1;
    }

    remaining = 1;
    for (length = 1; length <= maximum_bits; length++)
    {
        remaining = (remaining << 1) - decoder->counts[length];
        if (remaining < 0)
            return 0;
    }
    if (remaining != 0)
        return 0;

    code = 0;
    for (length = 1; length <= maximum_bits; length++)
    {
        code = (code + decoder->counts[length - 1]) << 1;
        if (code + decoder->counts[length] > (1u << length))
            return 0;

        decoder->first_code[length] = code;
        decoder->first_symbol[length] = (uint16_t)(decoder->first_symbol[length - 1] + decoder->counts[length - 1]);
        offsets[length] = decoder->first_symbol[length];
        next_code[length] = code;
    }

    for (symbol = 0; symbol < symbol_count; symbol++)
    {
        length = lengths[symbol];
        if (length != 0)
            decoder->sorted_symbols[offsets[length]++] = (uint16_t)symbol;
    }

    for (symbol = 0; symbol < symbol_count; symbol++)
    {
        uint32_t prefix, fill, index;
        uint16_t entry;

        length = lengths[symbol];
        if (length == 0)
            continue;

        code = next_code[length]++;
        if (length > fast_bits)
            continue;

        prefix = code << (fast_bits - length);
        fill = 1u << (fast_bits - length);
        entry = (uint16_t)((length << 11) | symbol);
        for (index = 0; index < fill; index++)
            fast_table[prefix + index] = entry;
    }

    return 1;
}

static uint32_t
lzx_decode_huffman(const LzxHuffman* decoder, LzxBitStream* stream)
{
    uint32_t code = 0, length;
    uint16_t entry;

    if (decoder->empty)
    {
        stream->failed = 1;
        return 0;
    }

    lzx_ensure_bits(stream, decoder->fast_bits);
    if (stream->failed)
        return 0;

    entry = decoder->fast_table[lzx_peek_bits(stream, decoder->fast_bits)];
    if (entry != 0)
    {
        lzx_remove_bits(stream, entry >> 11);
        return entry & 0x7ff;
    }

    for (length = 1; length <= decoder->maximum_bits; length++)
    {
        uint32_t delta;

        code = (code << 1) | lzx_read_bits(stream, 1);
        if (stream->failed)
            return 0;
        if (code < decoder->first_code[length])
            continue;
        delta = code - decoder->first_code[length];
        if (delta < decoder->counts[length])
            return decoder->sorted_symbols[decoder->first_symbol[length] + delta];
    }

    stream->failed = 1;
    return 0;
}

static int
lzx_read_code_lengths(LzxWorkspace* ws, LzxBitStream* stream, uint8_t* lengths, uint32_t count)
{
    uint32_t position = 0, index;

    for (index = 0; index < LZX_PRECODE_SYMBOLS; index++)
        ws->pre_lengths[index] = (uint8_t)lzx_read_bits(stream, 4);
    if (stream->failed ||
        !lzx_build_huffman(&ws->pre_code, ws->pre_lengths, LZX_PRECODE_SYMBOLS, 15, 6, ws->pre_fast) ||
        ws->pre_code.empty)
    {
        return 0;
    }

    while (position < count)
    {
        uint32_t symbol = lzx_decode_huffman(&ws->pre_code, stream);
        uint32_t run = 1;
        int32_t delta;
        uint8_t value;

        if (stream->failed)
            return 0;

        if (symbol < 17)
        {
            delta = (int32_t)lengths[position] - (int32_t)symbol;
            value = (uint8_t)(delta < 0 ? delta + 17 : delta);
        }
        else if (symbol == 17)
        {
            run = 4 + lzx_read_bits(stream, 4);
            value = 0;
        }
        else if (symbol == 18)
        {
            run = 20 + lzx_read_bits(stream, 5);
            value = 0;
        }
        else if (symbol == 19)
        {
            uint32_t delta_symbol;

            run = 4 + lzx_read_bits(stream, 1);
            delta_symbol = lzx_decode_huffman(&ws->pre_code, stream);
            if (stream->failed || delta_symbol > 17)
                return 0;
            delta = (int32_t)lengths[position] - (int32_t)delta_symbol;
            value = (uint8_t)(delta < 0 ? delta + 17 : delta);
        }
        else
        {
            return 0;
        }

        if (stream->failed || run > count - position)
            return 0;
        while (run--)
            lengths[position++] = value;
    }

    return 1;
}

static int
lzx_read_block_header(LzxWorkspace* ws, LzxBitStream* stream,
                      uint32_t* block_type, uint32_t* block_size, uint32_t recent[3])
{
    uint32_t index;

    *block_type = lzx_read_bits(stream, 3);
    if (lzx_read_bits(stream, 1))
    {
        *block_size = LZX_DEFAULT_BLOCK_SIZE;
    }
    else
    {
        *block_size = lzx_read_bits(stream, 8) << 8;
        *block_size |= lzx_read_bits(stream, 8);
    }
    if (stream->failed)
        return 0;

    if (*block_type == 2)
    {
        for (index = 0; index < LZX_ALIGNED_SYMBOLS; index++)
            ws->aligned_lengths[index] = (uint8_t)lzx_read_bits(stream, 3);
        if (stream->failed ||
            !lzx_build_huffman(&ws->aligned_code, ws->aligned_lengths, LZX_ALIGNED_SYMBOLS, 7, 7, ws->aligned_fast) ||
            ws->aligned_code.empty)
        {
            return 0;
        }
    }

    if (*block_type == 1 || *block_type == 2)
    {
        return lzx_read_code_lengths(ws, stream, ws->main_lengths, 256) &&
               lzx_read_code_lengths(ws, stream, ws->main_lengths + 256, LZX_MAIN_SYMBOLS - 256) &&
               lzx_build_huffman(&ws->main_code, ws->main_lengths, LZX_MAIN_SYMBOLS,
                                 LZX_MAX_CODE_LENGTH, 11, ws->main_fast) &&
               !ws->main_code.empty &&
               lzx_read_code_lengths(ws, stream, ws->length_lengths, LZX_LENGTH_SYMBOLS) &&
               lzx_build_huffman(&ws->length_code, ws->length_lengths, LZX_LENGTH_SYMBOLS,
                                 LZX_MAX_CODE_LENGTH, 10, ws->length_fast);
    }

    if (*block_type == 3)
    {
        lzx_ensure_bits(stream, 1);
        stream->bit_buffer = 0;
        stream->bits_left = 0;
        recent[0] = lzx_read_u32(stream);
        recent[1] = lzx_read_u32(stream);
        recent[2] = lzx_read_u32(stream);
        return !stream->failed && recent[0] && recent[1] && recent[2];
    }

    return 0;
}

static void
lzx_copy_match(uint8_t* out, uint32_t offset, uint32_t length, uint32_t distance)
{
    uint32_t index;

    for (index = 0; index < length; index++)
        out[offset + index] = out[offset + index - distance];
}

static int
lzx_decompress_block(LzxWorkspace* ws, LzxBitStream* stream, uint32_t block_type,
                     uint8_t* out, uint32_t out_offset, uint32_t block_size, uint32_t recent[3])
{
    uint32_t block_offset = 0;

    while (block_offset < block_size)
    {
        uint32_t symbol = lzx_decode_huffman(&ws->main_code, stream);
        uint32_t match_length, offset_slot, match_offset;

        if (stream->failed)
            return 0;
        if (symbol < 256)
        {
            out[out_offset + block_offset++] = (uint8_t)symbol;
            continue;
        }

        symbol -= 256;
        match_length = symbol % 8;
        offset_slot = symbol / 8;
        if (offset_slot >= 30)
            return 0;
        if (match_length == 7)
            match_length += lzx_decode_huffman(&ws->length_code, stream);
        match_length += 2;

        if (offset_slot < 3)
        {
            match_offset = recent[offset_slot];
            recent[offset_slot] = recent[0];
            recent[0] = match_offset;
        }
        else
        {
            uint32_t extra_bits = lzx_extra_offset_bits[offset_slot];

            match_offset = lzx_offset_slot_base[offset_slot];
            if (block_type == 2 && extra_bits >= 3)
            {
                match_offset += lzx_read_bits(stream, extra_bits - 3) << 3;
                match_offset += lzx_decode_huffman(&ws->aligned_code, stream);
            }
            else
            {
                match_offset += lzx_read_bits(stream, extra_bits);
            }
            if (match_offset < 2)
                return 0;
            match_offset -= 2;
            recent[2] = recent[1];
            recent[1] = recent[0];
            recent[0] = match_offset;
        }

        if (stream->failed ||
            match_offset == 0 ||
            match_offset > out_offset + block_offset ||
            match_length > block_size - block_offset)
        {
            return 0;
        }

        lzx_copy_match(out, out_offset + block_offset, match_length, match_offset);
        block_offset += match_length;
    }

    return 1;
}

static void
lzx_undo_e8(uint8_t* buffer, uint32_t size)
{
    uint32_t index;

    if (size <= 10)
        return;

    for (index = 0; index < size - 6;)
    {
        int32_t absolute;

        if (buffer[index] != 0xe8)
        {
            index++;
            continue;
        }

        absolute = (int32_t)lzx_read_le32(buffer + index + 1);
        if (absolute >= 0 && absolute < LZX_E8_FILE_SIZE)
            lzx_write_le32(buffer + index + 1, (uint32_t)(absolute - (int32_t)index));
        else if (absolute < 0 && absolute >= -(int32_t)index)
            lzx_write_le32(buffer + index + 1, (uint32_t)(absolute + LZX_E8_FILE_SIZE));
        index += 5;
    }
}

LzxStatus
lzx_decompress_static(const uint8_t* in, uint32_t in_len,
                      uint8_t* out, uint32_t out_len,
                      void* workspace)
{
    LzxWorkspace* ws = (LzxWorkspace*)workspace;
    LzxBitStream stream;
    uint32_t recent[3] = {1, 1, 1};
    uint32_t out_offset = 0;
    int translate = 0;

    memset(ws->main_lengths, 0, sizeof(ws->main_lengths));
    memset(ws->length_lengths, 0, sizeof(ws->length_lengths));
    lzx_init_stream(&stream, in, in_len);

    while (out_offset < out_len)
    {
        uint32_t block_type, block_size;

        if (!lzx_read_block_header(ws, &stream, &block_type, &block_size, recent) ||
            block_size == 0 ||
            block_size > out_len - out_offset)
        {
            return LZX_BAD_DATA;
        }

        if (block_type == 3)
        {
            if ((size_t)(stream.end - stream.next) < block_size)
                return LZX_BAD_DATA;
            memcpy(out + out_offset, stream.next, block_size);
            stream.next += block_size;
            if (block_size & 1)
                (void)lzx_read_byte(&stream);
            translate = 1;
        }
        else
        {
            if (!lzx_decompress_block(ws, &stream, block_type, out, out_offset, block_size, recent))
                return LZX_BAD_DATA;
            if (ws->main_lengths[0xe8] != 0)
                translate = 1;
        }
        out_offset += block_size;
    }

    if (stream.failed)
        return LZX_BAD_DATA;
    if (translate)
        lzx_undo_e8(out, out_len);
    return LZX_OK;
}
