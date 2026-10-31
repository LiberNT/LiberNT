/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OpRegion and Video BIOS Table parsing
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"

#include <string.h>

#define OPREGION_SIGNATURE          "IntelGraphicsMem"
#define OPREGION_SIGNATURE_SIZE     16
#define OPREGION_SIZE_KB            0x10
#define OPREGION_MBOX               0x58
#define OPREGION_MBOX_VBT           0x08
#define OPREGION_VBT_OFFSET         0x400
#define OPREGION_VBT_SIZE           0x1800

#define VBT_SIGNATURE_SIZE          20
#define VBT_HEADER_VBT_SIZE         24
#define VBT_HEADER_BDB_OFFSET       28
#define VBT_HEADER_SIZE             48
#define BDB_SIGNATURE               "BIOS_DATA_BLOCK"
#define BDB_SIGNATURE_SIZE          15
#define BDB_VERSION                 16
#define BDB_HEADER_SIZE             18
#define BDB_SIZE                    20
#define BDB_BLOCK_HEADER_SIZE       3

#define BDB_GENERAL_FEATURES        1
#define BDB_LFP_OPTIONS             40
#define BDB_LFP_DATA_PTRS           41
#define BDB_LFP_DATA                42
#define BDB_LFP_BACKLIGHT           43

#define VBT_PANEL_TYPES             16
#define LFP_PTR_ENTRY_SIZE          9
#define LFP_PTR_DVO_TIMING          3
#define DTD_SIZE                    18
#define BACKLIGHT_MIN_ENTRY_SIZE    4

typedef struct _HDGFX_BLOB
{
    const uint8_t *Data;
    size_t Size;
} HDGFX_BLOB;

static bool
BlobRange(
    HDGFX_BLOB Blob,
    size_t Offset,
    size_t Length)
{
    return Offset <= Blob.Size && Length <= Blob.Size - Offset;
}

static uint32_t
Read8(
    HDGFX_BLOB Blob,
    size_t Offset)
{
    return BlobRange(Blob, Offset, 1) ? Blob.Data[Offset] : 0;
}

static uint32_t
Read16(
    HDGFX_BLOB Blob,
    size_t Offset)
{
    return Read8(Blob, Offset) | (Read8(Blob, Offset + 1) << 8);
}

static uint32_t
Read32(
    HDGFX_BLOB Blob,
    size_t Offset)
{
    return Read16(Blob, Offset) | (Read16(Blob, Offset + 2) << 16);
}

static bool
BdbFindBlock(
    HDGFX_BLOB Bdb,
    uint32_t Id,
    HDGFX_BLOB *Block,
    size_t *BlockOffset)
{
    size_t Offset = Read16(Bdb, BDB_HEADER_SIZE);

    while (BlobRange(Bdb, Offset, BDB_BLOCK_HEADER_SIZE))
    {
        uint32_t BlockId = Read8(Bdb, Offset);
        size_t Size = Read16(Bdb, Offset + 1);

        Offset += BDB_BLOCK_HEADER_SIZE;
        if (!BlobRange(Bdb, Offset, Size))
            return false;
        if (BlockId == Id)
        {
            Block->Data = Bdb.Data + Offset;
            Block->Size = Size;
            if (BlockOffset)
                *BlockOffset = Offset;
            return true;
        }
        Offset += Size;
    }
    return false;
}

static void
ParsePanel(
    HDGFX_BLOB Bdb,
    HDGFX_VBT *Result)
{
    HDGFX_VBT_PANEL *Panel = &Result->Panel;
    HDGFX_BLOB Block;
    HDGFX_BLOB Data;
    size_t DataOffset;
    size_t DtdOffset;
    uint32_t EntrySize;

    if (!BdbFindBlock(Bdb, BDB_LFP_OPTIONS, &Block, NULL) || Block.Size < 1)
        return;
    Panel->PanelType = Read8(Block, 0);
    if (Panel->PanelType >= VBT_PANEL_TYPES)
        return;

    if (BdbFindBlock(Bdb, BDB_LFP_DATA_PTRS, &Block, NULL) &&
        Panel->PanelType < Read8(Block, 0) &&
        BlobRange(Block, 1 + (size_t)Panel->PanelType * LFP_PTR_ENTRY_SIZE, LFP_PTR_ENTRY_SIZE) &&
        BdbFindBlock(Bdb, BDB_LFP_DATA, &Data, &DataOffset))
    {
        DtdOffset = Read16(Block, 1 + (size_t)Panel->PanelType * LFP_PTR_ENTRY_SIZE + LFP_PTR_DVO_TIMING);
        if (DtdOffset >= DataOffset && BlobRange(Data, DtdOffset - DataOffset, DTD_SIZE))
        {
            HDGFX_DTD Dtd;

            HdgfxDecodeDtd(Data.Data + (DtdOffset - DataOffset), &Dtd);
            Panel->DotKHz = Dtd.DotKHz;
            Panel->Timings = Dtd.Timings;
            Panel->HSyncPositive = Dtd.HSyncPositive;
            Panel->VSyncPositive = Dtd.VSyncPositive;
            Panel->WidthMm = Dtd.WidthMm;
            Panel->HeightMm = Dtd.HeightMm;
            Panel->Present = true;
        }
    }

    if (BdbFindBlock(Bdb, BDB_LFP_BACKLIGHT, &Block, NULL))
    {
        EntrySize = Read8(Block, 0);
        if (EntrySize >= BACKLIGHT_MIN_ENTRY_SIZE &&
            BlobRange(Block, 1 + (size_t)Panel->PanelType * EntrySize, EntrySize))
        {
            size_t Entry = 1 + (size_t)Panel->PanelType * EntrySize;

            Panel->BacklightPresent = true;
            Panel->BacklightType = Read8(Block, Entry) & 3;
            Panel->BacklightActiveLow = (Read8(Block, Entry) & 4) != 0;
            Panel->BacklightPwmHz = Read16(Block, Entry + 1);
            Panel->BacklightMinimum = Read8(Block, Entry + 3);
        }
    }
}

bool
HdgfxOpRegionFindVbt(
    const uint8_t *OpRegion,
    size_t Size,
    const uint8_t **Vbt,
    size_t *VbtSize)
{
    HDGFX_BLOB Region = { OpRegion, Size };

    if (!BlobRange(Region, 0, OPREGION_VBT_OFFSET + OPREGION_VBT_SIZE) ||
        memcmp(OpRegion, OPREGION_SIGNATURE, OPREGION_SIGNATURE_SIZE) != 0 ||
        (size_t)Read32(Region, OPREGION_SIZE_KB) * 1024 < OPREGION_VBT_OFFSET + OPREGION_VBT_SIZE ||
        (Read32(Region, OPREGION_MBOX) & OPREGION_MBOX_VBT) == 0)
    {
        return false;
    }
    *Vbt = OpRegion + OPREGION_VBT_OFFSET;
    *VbtSize = OPREGION_VBT_SIZE;
    return true;
}

bool
HdgfxVbtParse(
    const uint8_t *Vbt,
    size_t Size,
    HDGFX_VBT *Result)
{
    HDGFX_BLOB Blob = { Vbt, Size };
    HDGFX_BLOB Bdb;
    HDGFX_BLOB Block;
    size_t VbtSize;
    size_t BdbOffset;

    memset(Result, 0, sizeof(*Result));
    if (!BlobRange(Blob, 0, VBT_HEADER_SIZE) || memcmp(Vbt, "$VBT", 4) != 0)
        return false;
    VbtSize = Read16(Blob, VBT_HEADER_VBT_SIZE);
    BdbOffset = Read32(Blob, VBT_HEADER_BDB_OFFSET);
    if (VbtSize > Size || !BlobRange(Blob, BdbOffset, BDB_BLOCK_HEADER_SIZE) || BdbOffset > VbtSize)
        return false;

    Bdb.Data = Vbt + BdbOffset;
    Bdb.Size = VbtSize - BdbOffset;
    if (Bdb.Size < BDB_SIZE + 2 || memcmp(Bdb.Data, BDB_SIGNATURE, BDB_SIGNATURE_SIZE) != 0)
        return false;
    if (Read16(Bdb, BDB_SIZE) > Bdb.Size)
        return false;
    Bdb.Size = Read16(Bdb, BDB_SIZE);

    memcpy(Result->Signature, Vbt, VBT_SIGNATURE_SIZE);
    Result->Signature[VBT_SIGNATURE_SIZE] = 0;
    Result->BdbVersion = Read16(Bdb, BDB_VERSION);

    if (BdbFindBlock(Bdb, BDB_GENERAL_FEATURES, &Block, NULL) && Block.Size >= 2)
    {
        Result->LvdsSscEnabled = (Read8(Block, 1) & 0x02) != 0;
        Result->LvdsSscFrequencyBit = (Read8(Block, 1) & 0x04) != 0;
    }

    ParsePanel(Bdb, Result);
    return true;
}
