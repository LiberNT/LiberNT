/*
 * PROJECT:     LiberNT WIM library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     LZX decompression of WIM resource chunks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef LIBWIM_LZX_H
#define LIBWIM_LZX_H

#include <stdint.h>

typedef enum {
    LZX_OK = 0,
    LZX_BAD_DATA = -1
} LzxStatus;

#define LZX_DECOMPRESS_WORKSPACE_SIZE 32768

LzxStatus lzx_decompress_static(
    const uint8_t* in, uint32_t in_len,
    uint8_t* out, uint32_t out_len,
    void* workspace);

#endif /* LIBWIM_LZX_H */
