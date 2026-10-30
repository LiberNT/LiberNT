/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User/kernel ABI for user-mode built Gen command batches
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#pragma once

#define HDGFX_GEN_CONTEXT_MAGIC         0x43474748UL
#define HDGFX_GEN_CONTEXT_VERSION       1UL
#define HDGFX_GEN_SUBMIT_MAGIC          0x53474748UL
#define HDGFX_GEN_SUBMIT_BLITTER        0x1UL
#define HDGFX_GEN_SUBMIT_VERSION        1UL

#define HDGFX_GEN_COMMAND_BUFFER_SIZE   (384UL * 1024)
#define HDGFX_GEN_MAX_BATCH_SIZE        (256UL * 1024)
#define HDGFX_GEN_MAX_STATE_SIZE        (64UL * 1024)
#define HDGFX_GEN_ALLOCATION_LIST_SIZE  1024UL
#define HDGFX_GEN_PATCH_LIST_SIZE       8192UL

#define HDGFX_GEN_PATCH_ALLOCATION      0UL
#define HDGFX_GEN_PATCH_STATE           2UL

typedef struct _HDGFX_GEN_CONTEXT_DATA
{
    UINT32 Magic;
    UINT32 Version;
} HDGFX_GEN_CONTEXT_DATA;

typedef struct _HDGFX_GEN_SUBMIT
{
    UINT32 Magic;
    UINT32 Version;
    UINT32 BatchOffset;
    UINT32 BatchLength;
    UINT32 StateOffset;
    UINT32 StateLength;
    UINT32 Flags;
    UINT32 Reserved;
} HDGFX_GEN_SUBMIT;
