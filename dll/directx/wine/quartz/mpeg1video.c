/*
 * PROJECT:     LiberNT DirectShow
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     MPEG-1 video (ISO/IEC 11172-2) decoder core
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mpeg1video.h"

enum
{
    PICTURE_I = 1,
    PICTURE_P = 2,
    PICTURE_B = 3,
    PICTURE_D = 4,
};

enum
{
    MB_QUANT = 0x01,
    MB_FORWARD = 0x02,
    MB_BACKWARD = 0x04,
    MB_PATTERN = 0x08,
    MB_INTRA = 0x10,
};

enum
{
    MBA_STUFFING = 34,
    MBA_ESCAPE = 35,
    DCT_EOB = 64,
    DCT_ESCAPE = 65,
};

struct vlc_code
{
    const char *bits;
    int16_t value;
    int8_t level;
};

struct vlc_entry
{
    int16_t value;
    int8_t level;
    uint8_t length;
};

struct vlc_table
{
    struct vlc_entry *entries;
    unsigned int bits;
};

static const struct vlc_code mba_codes[] =
{
    {"1", 1}, {"011", 2}, {"010", 3}, {"0011", 4}, {"0010", 5}, {"00011", 6}, {"00010", 7},
    {"0000111", 8}, {"0000110", 9}, {"00001011", 10}, {"00001010", 11}, {"00001001", 12},
    {"00001000", 13}, {"00000111", 14}, {"00000110", 15}, {"0000010111", 16}, {"0000010110", 17},
    {"0000010101", 18}, {"0000010100", 19}, {"0000010011", 20}, {"0000010010", 21},
    {"00000100011", 22}, {"00000100010", 23}, {"00000100001", 24}, {"00000100000", 25},
    {"00000011111", 26}, {"00000011110", 27}, {"00000011101", 28}, {"00000011100", 29},
    {"00000011011", 30}, {"00000011010", 31}, {"00000011001", 32}, {"00000011000", 33},
    {"00000001111", MBA_STUFFING}, {"00000001000", MBA_ESCAPE},
};

static const struct vlc_code mb_type_i_codes[] =
{
    {"1", MB_INTRA}, {"01", MB_QUANT | MB_INTRA},
};

static const struct vlc_code mb_type_p_codes[] =
{
    {"1", MB_FORWARD | MB_PATTERN}, {"01", MB_PATTERN}, {"001", MB_FORWARD}, {"00011", MB_INTRA},
    {"00010", MB_QUANT | MB_FORWARD | MB_PATTERN}, {"00001", MB_QUANT | MB_PATTERN},
    {"000001", MB_QUANT | MB_INTRA},
};

static const struct vlc_code mb_type_b_codes[] =
{
    {"10", MB_FORWARD | MB_BACKWARD}, {"11", MB_FORWARD | MB_BACKWARD | MB_PATTERN},
    {"010", MB_BACKWARD}, {"011", MB_BACKWARD | MB_PATTERN}, {"0010", MB_FORWARD},
    {"0011", MB_FORWARD | MB_PATTERN}, {"00011", MB_INTRA},
    {"00010", MB_QUANT | MB_FORWARD | MB_BACKWARD | MB_PATTERN},
    {"000011", MB_QUANT | MB_FORWARD | MB_PATTERN}, {"000010", MB_QUANT | MB_BACKWARD | MB_PATTERN},
    {"000001", MB_QUANT | MB_INTRA},
};

static const struct vlc_code mb_type_d_codes[] =
{
    {"1", MB_INTRA},
};

static const struct vlc_code cbp_codes[] =
{
    {"111", 60}, {"1101", 4}, {"1100", 8}, {"1011", 16}, {"1010", 32},
    {"10011", 12}, {"10010", 48}, {"10001", 20}, {"10000", 40}, {"01111", 28}, {"01110", 44},
    {"01101", 52}, {"01100", 56}, {"01011", 1}, {"01010", 61}, {"01001", 2}, {"01000", 62},
    {"001111", 24}, {"001110", 36}, {"001101", 3}, {"001100", 63},
    {"0010111", 5}, {"0010110", 9}, {"0010101", 17}, {"0010100", 33},
    {"0010011", 6}, {"0010010", 10}, {"0010001", 18}, {"0010000", 34},
    {"00011111", 7}, {"00011110", 11}, {"00011101", 19}, {"00011100", 35},
    {"00011011", 13}, {"00011010", 49}, {"00011001", 21}, {"00011000", 41},
    {"00010111", 14}, {"00010110", 50}, {"00010101", 22}, {"00010100", 42},
    {"00010011", 15}, {"00010010", 51}, {"00010001", 23}, {"00010000", 43},
    {"00001111", 25}, {"00001110", 37}, {"00001101", 26}, {"00001100", 38},
    {"00001011", 29}, {"00001010", 45}, {"00001001", 53}, {"00001000", 57},
    {"00000111", 30}, {"00000110", 46}, {"00000101", 54}, {"00000100", 58},
    {"000000111", 31}, {"000000110", 47}, {"000000101", 55}, {"000000100", 59},
    {"000000011", 27}, {"000000010", 39},
};

static const struct vlc_code motion_codes[] =
{
    {"00000011001", -16}, {"00000011011", -15}, {"00000011101", -14}, {"00000011111", -13},
    {"00000100001", -12}, {"00000100011", -11}, {"0000010011", -10}, {"0000010101", -9},
    {"0000010111", -8}, {"00000111", -7}, {"00001001", -6}, {"00001011", -5}, {"0000111", -4},
    {"00011", -3}, {"0011", -2}, {"011", -1}, {"1", 0}, {"010", 1}, {"0010", 2}, {"00010", 3},
    {"0000110", 4}, {"00001010", 5}, {"00001000", 6}, {"00000110", 7}, {"0000010110", 8},
    {"0000010100", 9}, {"0000010010", 10}, {"00000100010", 11}, {"00000100000", 12},
    {"00000011110", 13}, {"00000011100", 14}, {"00000011010", 15}, {"00000011000", 16},
};

static const struct vlc_code dc_luma_codes[] =
{
    {"100", 0}, {"00", 1}, {"01", 2}, {"101", 3}, {"110", 4}, {"1110", 5}, {"11110", 6},
    {"111110", 7}, {"1111110", 8},
};

static const struct vlc_code dc_chroma_codes[] =
{
    {"00", 0}, {"01", 1}, {"10", 2}, {"110", 3}, {"1110", 4}, {"11110", 5}, {"111110", 6},
    {"1111110", 7}, {"11111110", 8},
};

static const struct vlc_code dct_codes[] =
{
    {"10", DCT_EOB}, {"11", 0, 1}, {"011", 1, 1}, {"0100", 0, 2}, {"0101", 2, 1},
    {"00101", 0, 3}, {"00111", 3, 1}, {"00110", 4, 1}, {"000110", 1, 2}, {"000111", 5, 1},
    {"000101", 6, 1}, {"000100", 7, 1}, {"0000110", 0, 4}, {"0000100", 2, 2}, {"0000111", 8, 1},
    {"0000101", 9, 1}, {"000001", DCT_ESCAPE},
    {"00100110", 0, 5}, {"00100001", 0, 6}, {"00100101", 1, 3}, {"00100100", 3, 2},
    {"00100111", 10, 1}, {"00100011", 11, 1}, {"00100010", 12, 1}, {"00100000", 13, 1},
    {"0000001010", 0, 7}, {"0000001100", 1, 4}, {"0000001011", 2, 3}, {"0000001111", 4, 2},
    {"0000001001", 5, 2}, {"0000001110", 14, 1}, {"0000001101", 15, 1}, {"0000001000", 16, 1},
    {"000000011101", 0, 8}, {"000000011000", 0, 9}, {"000000010011", 0, 10}, {"000000010000", 0, 11},
    {"000000011011", 1, 5}, {"000000010100", 2, 4}, {"000000011100", 3, 3}, {"000000010010", 4, 3},
    {"000000011110", 6, 2}, {"000000010101", 7, 2}, {"000000010001", 8, 2}, {"000000011111", 17, 1},
    {"000000011010", 18, 1}, {"000000011001", 19, 1}, {"000000010111", 20, 1}, {"000000010110", 21, 1},
    {"0000000011010", 0, 12}, {"0000000011001", 0, 13}, {"0000000011000", 0, 14},
    {"0000000010111", 0, 15}, {"0000000010110", 1, 6}, {"0000000010101", 1, 7},
    {"0000000010100", 2, 5}, {"0000000010011", 3, 4}, {"0000000010010", 5, 3},
    {"0000000010001", 9, 2}, {"0000000010000", 10, 2}, {"0000000011111", 22, 1},
    {"0000000011110", 23, 1}, {"0000000011101", 24, 1}, {"0000000011100", 25, 1},
    {"0000000011011", 26, 1},
    {"00000000011111", 0, 16}, {"00000000011110", 0, 17}, {"00000000011101", 0, 18},
    {"00000000011100", 0, 19}, {"00000000011011", 0, 20}, {"00000000011010", 0, 21},
    {"00000000011001", 0, 22}, {"00000000011000", 0, 23}, {"00000000010111", 0, 24},
    {"00000000010110", 0, 25}, {"00000000010101", 0, 26}, {"00000000010100", 0, 27},
    {"00000000010011", 0, 28}, {"00000000010010", 0, 29}, {"00000000010001", 0, 30},
    {"00000000010000", 0, 31},
    {"000000000011000", 0, 32}, {"000000000010111", 0, 33}, {"000000000010110", 0, 34},
    {"000000000010101", 0, 35}, {"000000000010100", 0, 36}, {"000000000010011", 0, 37},
    {"000000000010010", 0, 38}, {"000000000010001", 0, 39}, {"000000000010000", 0, 40},
    {"000000000011111", 1, 8}, {"000000000011110", 1, 9}, {"000000000011101", 1, 10},
    {"000000000011100", 1, 11}, {"000000000011011", 1, 12}, {"000000000011010", 1, 13},
    {"000000000011001", 1, 14},
    {"0000000000010011", 1, 15}, {"0000000000010010", 1, 16}, {"0000000000010001", 1, 17},
    {"0000000000010000", 1, 18}, {"0000000000010100", 6, 3}, {"0000000000011010", 11, 2},
    {"0000000000011001", 12, 2}, {"0000000000011000", 13, 2}, {"0000000000010111", 14, 2},
    {"0000000000010110", 15, 2}, {"0000000000010101", 16, 2}, {"0000000000011111", 27, 1},
    {"0000000000011110", 28, 1}, {"0000000000011101", 29, 1}, {"0000000000011100", 30, 1},
    {"0000000000011011", 31, 1},
};

static const uint8_t zigzag[64] =
{
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static const uint8_t default_intra_matrix[64] =
{
     8, 16, 19, 22, 26, 27, 29, 34,
    16, 16, 22, 24, 27, 29, 34, 37,
    19, 22, 26, 27, 29, 34, 34, 38,
    22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48,
    26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69,
    27, 29, 35, 38, 46, 56, 69, 83,
};

struct tables
{
    struct vlc_table mba, mb_type[5], cbp, motion, dc_luma, dc_chroma, dct;
    float idct[8][8];
};

static struct tables *tables;

struct picture
{
    uint8_t *planes[3];
    int64_t time;
    int has_time;
    int type;
};

struct time_mark
{
    uint64_t offset;
    int64_t time;
};

struct mpeg1video
{
    uint8_t *buffer;
    size_t size, capacity, pos;
    uint64_t base;

    struct time_mark *marks;
    size_t mark_count, mark_capacity;

    unsigned int width, height, mb_width, mb_height;
    uint8_t intra_matrix[64], non_intra_matrix[64];
    int have_sequence;
    int closed_gop;

    struct picture pictures[3];
    struct picture *forward, *backward, *current;
    int backward_shown;
    int picture_active, picture_type;
    int full_pel[2], f_code[2];

    const struct picture *output;
    struct mpeg1video_frame frame;

    struct
    {
        const uint8_t *data;
        size_t size, pos;
    } bits;
    int quant;
    int dc_pred[3];
    int mv_pred[2][2];
    int mv[2][2];
    int last_type;
    int error;
};

static int build_table(struct vlc_table *table, const struct vlc_code *codes, size_t count)
{
    unsigned int bits = 0, length, i, j, code;

    for (i = 0; i < count; ++i)
        if ((length = strlen(codes[i].bits)) > bits)
            bits = length;

    if (!(table->entries = calloc((size_t)1 << bits, sizeof(*table->entries))))
        return 0;
    table->bits = bits;

    for (i = 0; i < count; ++i)
    {
        length = strlen(codes[i].bits);
        for (code = 0, j = 0; j < length; ++j)
            code = (code << 1) | (codes[i].bits[j] == '1');
        for (j = code << (bits - length); j < (code + 1) << (bits - length); ++j)
        {
            table->entries[j].value = codes[i].value;
            table->entries[j].level = codes[i].level;
            table->entries[j].length = length;
        }
    }
    return 1;
}

static int init_tables(void)
{
    struct tables *t;
    unsigned int x, u;

    if (tables)
        return 1;

    if (!(t = calloc(1, sizeof(*t))))
        return 0;

    if (!build_table(&t->mba, mba_codes, sizeof(mba_codes) / sizeof(*mba_codes))
            || !build_table(&t->mb_type[PICTURE_I], mb_type_i_codes, sizeof(mb_type_i_codes) / sizeof(*mb_type_i_codes))
            || !build_table(&t->mb_type[PICTURE_P], mb_type_p_codes, sizeof(mb_type_p_codes) / sizeof(*mb_type_p_codes))
            || !build_table(&t->mb_type[PICTURE_B], mb_type_b_codes, sizeof(mb_type_b_codes) / sizeof(*mb_type_b_codes))
            || !build_table(&t->mb_type[PICTURE_D], mb_type_d_codes, sizeof(mb_type_d_codes) / sizeof(*mb_type_d_codes))
            || !build_table(&t->cbp, cbp_codes, sizeof(cbp_codes) / sizeof(*cbp_codes))
            || !build_table(&t->motion, motion_codes, sizeof(motion_codes) / sizeof(*motion_codes))
            || !build_table(&t->dc_luma, dc_luma_codes, sizeof(dc_luma_codes) / sizeof(*dc_luma_codes))
            || !build_table(&t->dc_chroma, dc_chroma_codes, sizeof(dc_chroma_codes) / sizeof(*dc_chroma_codes))
            || !build_table(&t->dct, dct_codes, sizeof(dct_codes) / sizeof(*dct_codes)))
    {
        free(t->mba.entries);
        for (x = 0; x < 5; ++x)
            free(t->mb_type[x].entries);
        free(t->cbp.entries);
        free(t->motion.entries);
        free(t->dc_luma.entries);
        free(t->dc_chroma.entries);
        free(t->dct.entries);
        free(t);
        return 0;
    }

    for (x = 0; x < 8; ++x)
        for (u = 0; u < 8; ++u)
            t->idct[x][u] = (float)((u ? 0.5 : 0.5 / sqrt(2.0)) * cos((2 * x + 1) * u * 3.14159265358979323846 / 16.0));

    tables = t;
    return 1;
}

static uint32_t show_bits(struct mpeg1video *dec, unsigned int count)
{
    size_t byte = dec->bits.pos >> 3;
    uint32_t value = 0;
    unsigned int i;

    for (i = 0; i < 4; ++i)
        value = (value << 8) | (byte + i < dec->bits.size ? dec->bits.data[byte + i] : 0);
    value <<= dec->bits.pos & 7;
    return value >> (32 - count);
}

static void skip_bits(struct mpeg1video *dec, unsigned int count)
{
    dec->bits.pos += count;
    if (dec->bits.pos > dec->bits.size * 8)
        dec->error = 1;
}

static uint32_t get_bits(struct mpeg1video *dec, unsigned int count)
{
    uint32_t value = show_bits(dec, count);
    skip_bits(dec, count);
    return value;
}

static const struct vlc_entry *get_vlc(struct mpeg1video *dec, const struct vlc_table *table)
{
    const struct vlc_entry *entry = &table->entries[show_bits(dec, table->bits)];

    if (!entry->length)
    {
        dec->error = 1;
        return NULL;
    }
    skip_bits(dec, entry->length);
    return entry;
}

static void free_pictures(struct mpeg1video *dec)
{
    unsigned int i, j;

    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
        {
            free(dec->pictures[i].planes[j]);
            dec->pictures[i].planes[j] = NULL;
        }
    dec->forward = dec->backward = dec->current = NULL;
    dec->output = NULL;
}

static int alloc_pictures(struct mpeg1video *dec)
{
    size_t luma = (size_t)dec->mb_width * 16 * dec->mb_height * 16;
    unsigned int i;

    free_pictures(dec);
    for (i = 0; i < 3; ++i)
    {
        dec->pictures[i].planes[0] = malloc(luma);
        dec->pictures[i].planes[1] = malloc(luma / 4);
        dec->pictures[i].planes[2] = malloc(luma / 4);
        if (!dec->pictures[i].planes[0] || !dec->pictures[i].planes[1] || !dec->pictures[i].planes[2])
        {
            free_pictures(dec);
            return 0;
        }
        memset(dec->pictures[i].planes[0], 16, luma);
        memset(dec->pictures[i].planes[1], 128, luma / 4);
        memset(dec->pictures[i].planes[2], 128, luma / 4);
    }
    return 1;
}

static void parse_sequence_header(struct mpeg1video *dec)
{
    unsigned int width, height, i;

    width = get_bits(dec, 12);
    height = get_bits(dec, 12);
    skip_bits(dec, 4 + 4 + 18 + 1 + 10 + 1);

    if (get_bits(dec, 1))
    {
        for (i = 0; i < 64; ++i)
            dec->intra_matrix[zigzag[i]] = get_bits(dec, 8);
    }
    else
    {
        memcpy(dec->intra_matrix, default_intra_matrix, sizeof(dec->intra_matrix));
    }

    if (get_bits(dec, 1))
    {
        for (i = 0; i < 64; ++i)
            dec->non_intra_matrix[zigzag[i]] = get_bits(dec, 8);
    }
    else
    {
        memset(dec->non_intra_matrix, 16, sizeof(dec->non_intra_matrix));
    }

    if (dec->error || !width || !height)
        return;

    if (!dec->have_sequence || width != dec->width || height != dec->height)
    {
        dec->width = width;
        dec->height = height;
        dec->mb_width = (width + 15) / 16;
        dec->mb_height = (height + 15) / 16;
        dec->have_sequence = alloc_pictures(dec);
        dec->backward_shown = 1;
    }
}

static void take_time(struct mpeg1video *dec, uint64_t offset, struct picture *picture)
{
    size_t i = 0;

    picture->has_time = 0;
    while (i + 1 < dec->mark_count && dec->marks[i + 1].offset <= offset)
        ++i;
    if (i < dec->mark_count && dec->marks[i].offset <= offset)
    {
        picture->time = dec->marks[i].time;
        picture->has_time = 1;
        ++i;
    }
    memmove(dec->marks, dec->marks + i, (dec->mark_count - i) * sizeof(*dec->marks));
    dec->mark_count -= i;
}

static struct picture *free_picture(struct mpeg1video *dec)
{
    unsigned int i;

    for (i = 0; i < 3; ++i)
        if (&dec->pictures[i] != dec->forward && &dec->pictures[i] != dec->backward
                && (dec->picture_type == PICTURE_B || &dec->pictures[i] != dec->current))
            return &dec->pictures[i];
    return NULL;
}

static void start_picture(struct mpeg1video *dec, uint64_t offset)
{
    struct picture unused, *picture;
    int type, i;

    skip_bits(dec, 10);
    type = get_bits(dec, 3);
    skip_bits(dec, 16);
    for (i = 0; i < 2; ++i)
    {
        dec->full_pel[i] = 0;
        dec->f_code[i] = 1;
    }
    if (type == PICTURE_P || type == PICTURE_B)
    {
        dec->full_pel[0] = get_bits(dec, 1);
        dec->f_code[0] = get_bits(dec, 3);
    }
    if (type == PICTURE_B)
    {
        dec->full_pel[1] = get_bits(dec, 1);
        dec->f_code[1] = get_bits(dec, 3);
    }

    if (dec->error || !dec->have_sequence || type < PICTURE_I || type > PICTURE_D
            || !dec->f_code[0] || !dec->f_code[1]
            || (type == PICTURE_P && !dec->backward)
            || (type == PICTURE_B && (!dec->backward || (!dec->forward && !dec->closed_gop))))
    {
        take_time(dec, offset, &unused);
        return;
    }

    dec->picture_type = type;
    if (type == PICTURE_B)
    {
        picture = free_picture(dec);
    }
    else
    {
        if (dec->backward && !dec->backward_shown)
        {
            dec->output = dec->backward;
            dec->backward_shown = 1;
        }
        dec->current = NULL;
        picture = free_picture(dec);
        dec->forward = dec->backward;
        dec->backward = picture;
        dec->backward_shown = 0;
    }

    picture->type = type;
    take_time(dec, offset, picture);
    dec->current = picture;
    dec->picture_active = 1;
}

static void finish_picture(struct mpeg1video *dec)
{
    if (!dec->picture_active)
        return;
    dec->picture_active = 0;
    if (dec->picture_type == PICTURE_B)
        dec->output = dec->current;
}

static void flush_reference(struct mpeg1video *dec)
{
    if (dec->backward && !dec->backward_shown && !dec->output)
    {
        dec->output = dec->backward;
        dec->backward_shown = 1;
    }
}

static int decode_motion_component(struct mpeg1video *dec, int dir, int component)
{
    int r_size = dec->f_code[dir] - 1, f = 1 << r_size;
    const struct vlc_entry *entry;
    int code, r = 0, little, big, value;

    if (!(entry = get_vlc(dec, &tables->motion)))
        return 0;
    code = entry->value;
    if (f != 1 && code)
        r = get_bits(dec, r_size);

    if (f == 1 || !code)
        little = code;
    else if (code > 0)
        little = code * f - (f - 1 - r);
    else
        little = code * f + (f - 1 - r);
    big = little > 0 ? little - 32 * f : little + 32 * f;

    value = dec->mv_pred[dir][component] + little;
    if (value > 16 * f - 1 || value < -16 * f)
        value = dec->mv_pred[dir][component] + big;
    dec->mv_pred[dir][component] = value;

    return dec->full_pel[dir] ? value * 2 : value;
}

static void predict_block(uint8_t *dst, unsigned int dst_stride, const uint8_t *ref, unsigned int ref_stride,
        unsigned int ref_width, unsigned int ref_height, int x, int y, int mvx, int mvy, unsigned int size, int average)
{
    int sx = x + (mvx >> 1), sy = y + (mvy >> 1), hx = mvx & 1, hy = mvy & 1;
    unsigned int i, j;
    int a, b, c, d, v;

    for (j = 0; j < size; ++j)
    {
        int y0 = sy + j, y1 = y0 + hy;

        if (y0 < 0) y0 = 0;
        if (y0 >= (int)ref_height) y0 = ref_height - 1;
        if (y1 < 0) y1 = 0;
        if (y1 >= (int)ref_height) y1 = ref_height - 1;

        for (i = 0; i < size; ++i)
        {
            int x0 = sx + i, x1 = x0 + hx;

            if (x0 < 0) x0 = 0;
            if (x0 >= (int)ref_width) x0 = ref_width - 1;
            if (x1 < 0) x1 = 0;
            if (x1 >= (int)ref_width) x1 = ref_width - 1;

            a = ref[y0 * ref_stride + x0];
            b = ref[y0 * ref_stride + x1];
            c = ref[y1 * ref_stride + x0];
            d = ref[y1 * ref_stride + x1];
            if (hx && hy)
                v = (a + b + c + d + 2) >> 2;
            else if (hx)
                v = (a + b + 1) >> 1;
            else if (hy)
                v = (a + c + 1) >> 1;
            else
                v = a;

            if (average)
                dst[j * dst_stride + i] = (dst[j * dst_stride + i] + v + 1) >> 1;
            else
                dst[j * dst_stride + i] = v;
        }
    }
}

static void predict_macroblock(struct mpeg1video *dec, unsigned int mb_x, unsigned int mb_y, int type)
{
    unsigned int luma_stride = dec->mb_width * 16, chroma_stride = dec->mb_width * 8;
    unsigned int luma_height = dec->mb_height * 16, chroma_height = dec->mb_height * 8;
    struct picture *cur = dec->current;
    const struct picture *refs[2];
    int dir, average = 0, plane;

    refs[0] = dec->forward ? dec->forward : dec->backward;
    refs[1] = dec->picture_type == PICTURE_B ? dec->backward : NULL;

    for (dir = 0; dir < 2; ++dir)
    {
        int mvx = dec->mv[dir][0], mvy = dec->mv[dir][1];

        if (!(type & (dir ? MB_BACKWARD : MB_FORWARD)) || !refs[dir])
            continue;

        predict_block(cur->planes[0] + mb_y * 16 * luma_stride + mb_x * 16, luma_stride,
                refs[dir]->planes[0], luma_stride, luma_stride, luma_height,
                mb_x * 16, mb_y * 16, mvx, mvy, 16, average);
        for (plane = 1; plane < 3; ++plane)
            predict_block(cur->planes[plane] + mb_y * 8 * chroma_stride + mb_x * 8, chroma_stride,
                    refs[dir]->planes[plane], chroma_stride, chroma_stride, chroma_height,
                    mb_x * 8, mb_y * 8, mvx / 2, mvy / 2, 8, average);
        average = 1;
    }
}

static void idct_put(const int *coef, uint8_t *dst, unsigned int stride, int add)
{
    float tmp[64], sum;
    unsigned int x, y, u;
    int value, nonzero = 0;

    for (u = 1; u < 64; ++u)
        nonzero |= coef[u];

    for (y = 0; y < 8; ++y)
    {
        for (x = 0; x < 8; ++x)
        {
            if (nonzero)
            {
                for (sum = 0, u = 0; u < 8; ++u)
                    sum += tables->idct[x][u] * coef[y * 8 + u];
                tmp[y * 8 + x] = sum;
            }
        }
    }

    for (y = 0; y < 8; ++y)
    {
        for (x = 0; x < 8; ++x)
        {
            if (nonzero)
            {
                for (sum = 0, u = 0; u < 8; ++u)
                    sum += tables->idct[y][u] * tmp[u * 8 + x];
            }
            else
            {
                sum = coef[0] / 8.0f;
            }
            value = (int)(sum + (sum < 0 ? -0.5f : 0.5f));
            if (value < -256) value = -256;
            if (value > 255) value = 255;
            if (add)
                value += dst[y * stride + x];
            dst[y * stride + x] = value < 0 ? 0 : value > 255 ? 255 : value;
        }
    }
}

static int decode_block(struct mpeg1video *dec, int intra, unsigned int index, uint8_t *dst, unsigned int stride)
{
    const uint8_t *matrix = intra ? dec->intra_matrix : dec->non_intra_matrix;
    const struct vlc_entry *entry;
    int coef[64] = {0};
    int i = 0, run, level, value, size, diff, sign;

    if (intra)
    {
        unsigned int component = index < 4 ? 0 : index - 3;

        if (!(entry = get_vlc(dec, component ? &tables->dc_chroma : &tables->dc_luma)))
            return 0;
        size = entry->value;
        diff = 0;
        if (size)
        {
            diff = get_bits(dec, size);
            if (!(diff >> (size - 1)))
                diff += 1 - (1 << size);
        }
        dec->dc_pred[component] += diff * 8;
        coef[0] = dec->dc_pred[component];
        i = 1;

        if (dec->picture_type == PICTURE_D)
        {
            idct_put(coef, dst, stride, 0);
            return 1;
        }
    }
    else if (show_bits(dec, 1))
    {
        skip_bits(dec, 1);
        level = get_bits(dec, 1) ? -1 : 1;
        value = ((2 * level + (level > 0 ? 1 : -1)) * dec->quant * matrix[0]) / 16;
        if (!(value & 1))
            value -= level;
        if (value > 2047) value = 2047;
        if (value < -2048) value = -2048;
        coef[0] = value;
        i = 1;
    }

    for (;;)
    {
        if (!(entry = get_vlc(dec, &tables->dct)))
            return 0;
        if (entry->value == DCT_EOB)
            break;
        if (entry->value == DCT_ESCAPE)
        {
            run = get_bits(dec, 6);
            level = get_bits(dec, 8);
            if (!level)
                level = get_bits(dec, 8);
            else if (level == 128)
                level = (int)get_bits(dec, 8) - 256;
            else if (level > 128)
                level -= 256;
        }
        else
        {
            run = entry->value;
            level = entry->level;
            if (get_bits(dec, 1))
                level = -level;
        }

        i += run;
        if (i > 63 || dec->error)
        {
            dec->error = 1;
            return 0;
        }

        sign = level > 0 ? 1 : -1;
        if (intra)
            value = (2 * level * dec->quant * matrix[zigzag[i]]) / 16;
        else
            value = ((2 * level + sign) * dec->quant * matrix[zigzag[i]]) / 16;
        if (value && !(value & 1))
            value -= sign;
        if (value > 2047) value = 2047;
        if (value < -2048) value = -2048;
        coef[zigzag[i]] = value;
        ++i;
    }

    idct_put(coef, dst, stride, !intra);
    return !dec->error;
}

static void decode_slice(struct mpeg1video *dec, unsigned int row)
{
    unsigned int luma_stride = dec->mb_width * 16, chroma_stride = dec->mb_width * 8;
    unsigned int mb_count = dec->mb_width * dec->mb_height, mb_x, mb_y, i;
    struct picture *cur = dec->current;
    const struct vlc_entry *entry;
    int address, increment, type, cbp, first = 1;

    if (row >= dec->mb_height)
        return;

    dec->quant = get_bits(dec, 5);
    while (get_bits(dec, 1) && !dec->error)
        skip_bits(dec, 8);

    address = row * dec->mb_width - 1;
    dec->dc_pred[0] = dec->dc_pred[1] = dec->dc_pred[2] = 1024;
    memset(dec->mv_pred, 0, sizeof(dec->mv_pred));
    memset(dec->mv, 0, sizeof(dec->mv));
    dec->last_type = 0;

    while (!dec->error && show_bits(dec, 23))
    {
        increment = 0;
        for (;;)
        {
            if (!(entry = get_vlc(dec, &tables->mba)))
                return;
            if (entry->value == MBA_STUFFING)
                continue;
            if (entry->value == MBA_ESCAPE)
            {
                increment += 33;
                continue;
            }
            increment += entry->value;
            break;
        }

        if (!first)
        {
            for (i = 1; i < (unsigned int)increment && address + i < mb_count; ++i)
            {
                mb_x = (address + i) % dec->mb_width;
                mb_y = (address + i) / dec->mb_width;
                dec->dc_pred[0] = dec->dc_pred[1] = dec->dc_pred[2] = 1024;
                if (dec->picture_type == PICTURE_P)
                {
                    memset(dec->mv_pred, 0, sizeof(dec->mv_pred));
                    memset(dec->mv, 0, sizeof(dec->mv));
                    predict_macroblock(dec, mb_x, mb_y, MB_FORWARD);
                }
                else if (dec->picture_type == PICTURE_B)
                {
                    predict_macroblock(dec, mb_x, mb_y, dec->last_type);
                }
            }
        }
        first = 0;
        address += increment;
        if (address < 0 || (unsigned int)address >= mb_count)
            return;
        mb_x = address % dec->mb_width;
        mb_y = address / dec->mb_width;

        if (!(entry = get_vlc(dec, &tables->mb_type[dec->picture_type])))
            return;
        type = entry->value;

        if (type & MB_QUANT)
            dec->quant = get_bits(dec, 5);

        if (type & MB_FORWARD)
        {
            dec->mv[0][0] = decode_motion_component(dec, 0, 0);
            dec->mv[0][1] = decode_motion_component(dec, 0, 1);
        }
        if (type & MB_BACKWARD)
        {
            dec->mv[1][0] = decode_motion_component(dec, 1, 0);
            dec->mv[1][1] = decode_motion_component(dec, 1, 1);
        }

        if (type & MB_PATTERN)
        {
            if (!(entry = get_vlc(dec, &tables->cbp)))
                return;
            cbp = entry->value;
        }
        else
        {
            cbp = (type & MB_INTRA) ? 63 : 0;
        }

        if (dec->error)
            return;

        if (type & MB_INTRA)
        {
            memset(dec->mv_pred, 0, sizeof(dec->mv_pred));
        }
        else
        {
            dec->dc_pred[0] = dec->dc_pred[1] = dec->dc_pred[2] = 1024;
            if (dec->picture_type == PICTURE_P && !(type & MB_FORWARD))
            {
                memset(dec->mv_pred, 0, sizeof(dec->mv_pred));
                memset(dec->mv, 0, sizeof(dec->mv));
                type |= MB_FORWARD;
            }
            predict_macroblock(dec, mb_x, mb_y, type);
        }

        for (i = 0; i < 6; ++i)
        {
            uint8_t *dst;
            unsigned int stride;

            if (i < 4)
            {
                stride = luma_stride;
                dst = cur->planes[0] + (mb_y * 16 + (i >> 1) * 8) * stride + mb_x * 16 + (i & 1) * 8;
            }
            else
            {
                stride = chroma_stride;
                dst = cur->planes[i - 3] + mb_y * 8 * stride + mb_x * 8;
            }

            if ((cbp & (32 >> i)) && !decode_block(dec, type & MB_INTRA, i, dst, stride))
                return;
        }

        if (dec->picture_type == PICTURE_D)
            skip_bits(dec, 1);

        dec->last_type = type & (MB_FORWARD | MB_BACKWARD);
    }
}

static int process_unit(struct mpeg1video *dec, size_t start, size_t end)
{
    unsigned int code = dec->buffer[start + 3];

    dec->bits.data = dec->buffer + start + 4;
    dec->bits.size = end - start - 4;
    dec->bits.pos = 0;
    dec->error = 0;

    if (code >= 0x01 && code <= 0xaf)
    {
        if (dec->picture_active)
            decode_slice(dec, code - 1);
        return 1;
    }

    if (code != 0x00 && code != 0xb3 && code != 0xb7 && code != 0xb8)
        return 1;

    finish_picture(dec);
    if (dec->output)
        return 0;

    if (code == 0x00)
    {
        start_picture(dec, dec->base + start);
    }
    else if (code == 0xb3)
    {
        parse_sequence_header(dec);
    }
    else if (code == 0xb8)
    {
        skip_bits(dec, 25);
        dec->closed_gop = get_bits(dec, 1);
    }
    else
    {
        flush_reference(dec);
    }
    return 1;
}

static size_t find_start_code(const struct mpeg1video *dec, size_t pos)
{
    for (; pos + 3 < dec->size; ++pos)
    {
        if (!dec->buffer[pos] && !dec->buffer[pos + 1] && dec->buffer[pos + 2] == 1)
            return pos;
    }
    return dec->size;
}

struct mpeg1video *mpeg1video_create(void)
{
    struct mpeg1video *dec;

    if (!init_tables())
        return NULL;
    if (!(dec = calloc(1, sizeof(*dec))))
        return NULL;
    dec->backward_shown = 1;
    return dec;
}

void mpeg1video_destroy(struct mpeg1video *dec)
{
    free_pictures(dec);
    free(dec->buffer);
    free(dec->marks);
    free(dec);
}

void mpeg1video_reset(struct mpeg1video *dec)
{
    dec->base += dec->size;
    dec->size = dec->pos = 0;
    dec->mark_count = 0;
    dec->forward = dec->backward = dec->current = NULL;
    dec->backward_shown = 1;
    dec->picture_active = 0;
    dec->output = NULL;
}

int mpeg1video_push(struct mpeg1video *dec, const uint8_t *data, size_t size, int has_time, int64_t time)
{
    if (dec->pos)
    {
        memmove(dec->buffer, dec->buffer + dec->pos, dec->size - dec->pos);
        dec->size -= dec->pos;
        dec->base += dec->pos;
        dec->pos = 0;
    }

    if (dec->size + size > dec->capacity)
    {
        size_t capacity = dec->capacity ? dec->capacity : 65536;
        uint8_t *buffer;

        while (capacity < dec->size + size)
            capacity *= 2;
        if (!(buffer = realloc(dec->buffer, capacity)))
            return 0;
        dec->buffer = buffer;
        dec->capacity = capacity;
    }

    if (has_time)
    {
        if (dec->mark_count == dec->mark_capacity)
        {
            size_t capacity = dec->mark_capacity ? dec->mark_capacity * 2 : 16;
            struct time_mark *marks;

            if (!(marks = realloc(dec->marks, capacity * sizeof(*marks))))
                return 0;
            dec->marks = marks;
            dec->mark_capacity = capacity;
        }
        dec->marks[dec->mark_count].offset = dec->base + dec->size;
        dec->marks[dec->mark_count].time = time;
        ++dec->mark_count;
    }

    memcpy(dec->buffer + dec->size, data, size);
    dec->size += size;
    return 1;
}

const struct mpeg1video_frame *mpeg1video_next(struct mpeg1video *dec, int eos)
{
    size_t start, end;

    for (;;)
    {
        if (dec->output)
        {
            const struct picture *picture = dec->output;

            dec->output = NULL;
            dec->frame.planes[0] = picture->planes[0];
            dec->frame.planes[1] = picture->planes[1];
            dec->frame.planes[2] = picture->planes[2];
            dec->frame.strides[0] = dec->mb_width * 16;
            dec->frame.strides[1] = dec->frame.strides[2] = dec->mb_width * 8;
            dec->frame.width = dec->width;
            dec->frame.height = dec->height;
            dec->frame.time = picture->time;
            dec->frame.has_time = picture->has_time;
            dec->frame.type = picture->type;
            return &dec->frame;
        }

        start = find_start_code(dec, dec->pos);
        if (start >= dec->size)
        {
            if (!eos)
            {
                dec->pos = dec->size > 3 ? dec->size - 3 : 0;
                return NULL;
            }
            dec->pos = dec->size;
            finish_picture(dec);
            if (!dec->output)
                flush_reference(dec);
            if (!dec->output)
                return NULL;
            continue;
        }

        end = find_start_code(dec, start + 4);
        if (end >= dec->size && !eos)
        {
            dec->pos = start;
            return NULL;
        }

        if (process_unit(dec, start, end))
            dec->pos = end;
        else
            dec->pos = start;
    }
}
