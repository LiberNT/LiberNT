/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Allow-list scan of Ivy Bridge user batches, which run privileged
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#define IVB_CLIENT_MI               0u
#define IVB_CLIENT_BLT              2u
#define IVB_CLIENT_GFX              3u
#define IVB_GEN6                    0x01u
#define IVB_GEN7                    0x02u
#define IVB_GENS                    (IVB_GEN6 | IVB_GEN7)
#define IVB_RENDER                  0x01u
#define IVB_BLITTER                 0x02u
#define IVB_RINGS                   (IVB_RENDER | IVB_BLITTER)
#define IVB_MI_NOOP                 0x00u
#define IVB_MI_FLUSH                0x04u
#define IVB_MI_ARB_CHECK            0x05u
#define IVB_MI_REPORT_HEAD          0x07u
#define IVB_MI_BATCH_BUFFER_END     0x0Au
#define IVB_MI_SUSPEND_FLUSH        0x0Bu
#define IVB_MI_PREDICATE            0x0Cu
#define IVB_MI_TOPOLOGY_FILTER      0x0Du
#define IVB_MI_SET_APPID            0x0Eu
#define IVB_MI_URB_CLEAR            0x19u
#define IVB_MI_STORE_DATA_IMM       0x20u
#define IVB_MI_LOAD_REGISTER_IMM    0x22u
#define IVB_MI_STORE_REGISTER_MEM   0x24u
#define IVB_MI_FLUSH_DW             0x26u
#define IVB_MI_LOAD_REGISTER_MEM    0x29u
#define IVB_BLT_XY_COLOR_BLT        0x50u
#define IVB_BLT_XY_SRC_COPY_BLT     0x53u
#define IVB_GFX_PIPE_CONTROL        0x1A00u
#define IVB_MI_REGISTER_MEM_DWORDS  3u
#define IVB_PC_FORBIDDEN            0x00A00000u
#define IVB_PC_NOTIFY               0x00000100u
#define IVB_FLUSH_DW_STORE_INDEX    0x00200000u
#define IVB_FLUSH_DW_NOTIFY         0x00000100u
#define IVB_INSTPM_CONSTANT_OFFSET  0x00400040u

typedef struct _IVB_COMMAND
{
    uint16_t First;
    uint16_t Last;
    uint8_t Gens;
    uint8_t Rings;
    uint32_t Length;
    uint32_t Wide;
} IVB_COMMAND;

static const IVB_COMMAND IvbMiCommands[] =
{
    { IVB_MI_NOOP, IVB_MI_NOOP, IVB_GENS, IVB_RINGS, 0, 0 },
    { IVB_MI_FLUSH, IVB_MI_FLUSH, IVB_GENS, IVB_RENDER, 0, 0 },
    { IVB_MI_ARB_CHECK, IVB_MI_ARB_CHECK, IVB_GENS, IVB_RINGS, 0, 0 },
    { IVB_MI_REPORT_HEAD, IVB_MI_REPORT_HEAD, IVB_GENS, IVB_RENDER, 0, 0 },
    { IVB_MI_BATCH_BUFFER_END, IVB_MI_BATCH_BUFFER_END, IVB_GENS, IVB_RINGS, 0, 0 },
    { IVB_MI_SUSPEND_FLUSH, IVB_MI_SUSPEND_FLUSH, IVB_GENS, IVB_RENDER, 0, 0 },
    { IVB_MI_PREDICATE, IVB_MI_SET_APPID, IVB_GEN7, IVB_RENDER, 0, 0 },
    { IVB_MI_URB_CLEAR, IVB_MI_URB_CLEAR, IVB_GEN7, IVB_RENDER, 0xFF, 0xFF },
    { IVB_MI_STORE_DATA_IMM, IVB_MI_STORE_DATA_IMM, IVB_GEN6, IVB_RENDER, 0x3F, 0xFF },
    { IVB_MI_STORE_DATA_IMM, IVB_MI_STORE_DATA_IMM, IVB_GEN7, IVB_RENDER, 0x3F, 0x3FF },
    { IVB_MI_LOAD_REGISTER_IMM, IVB_MI_LOAD_REGISTER_IMM, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { IVB_MI_STORE_REGISTER_MEM, IVB_MI_STORE_REGISTER_MEM, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { IVB_MI_FLUSH_DW, IVB_MI_FLUSH_DW, IVB_GENS, IVB_BLITTER, 0x3F, 0x3F },
    { IVB_MI_LOAD_REGISTER_MEM, IVB_MI_LOAD_REGISTER_MEM, IVB_GEN7, IVB_RENDER, 0xFF, 0xFF },
};

static const IVB_COMMAND IvbBltCommands[] =
{
    { IVB_BLT_XY_COLOR_BLT, IVB_BLT_XY_COLOR_BLT, IVB_GENS, IVB_BLITTER, 0xFF, 0xFF },
    { IVB_BLT_XY_SRC_COPY_BLT, IVB_BLT_XY_SRC_COPY_BLT, IVB_GENS, IVB_BLITTER, 0xFF, 0xFF },
};

static const IVB_COMMAND IvbGfxCommands[] =
{
    { 0x0003, 0x0003, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x0101, 0x0102, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x0103, 0x0103, IVB_GEN7, IVB_RENDER, 0xFF, 0xFF },
    { 0x080B, 0x080B, IVB_GENS, IVB_RENDER, 0, 0 },
    { 0x0904, 0x0904, IVB_GENS, IVB_RENDER, 0, 0 },
    { 0x1000, 0x1002, IVB_GENS, IVB_RENDER, 0xFFFF, 0xFFFF },
    { 0x1003, 0x1003, IVB_GEN6, IVB_RENDER, 0xFFFF, 0xFFFF },
    { 0x1004, 0x1004, IVB_GENS, IVB_RENDER, 0xFFFF, 0xFFFF },
    { 0x1100, 0x1100, IVB_GENS, IVB_RENDER, 0xFFFF, 0xFFFF },
    { 0x1102, 0x1103, IVB_GENS, IVB_RENDER, 0xFFFF, 0xFFFF },
    { 0x1104, 0x1105, IVB_GEN7, IVB_RENDER, 0xFF, 0xFF },
    { 0x1801, 0x1802, IVB_GEN6, IVB_RENDER, 0xFF, 0xFF },
    { 0x1804, 0x1804, IVB_GEN7, IVB_RENDER, 0xFF, 0xFF },
    { 0x1805, 0x1805, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x1806, 0x1807, IVB_GEN7, IVB_RENDER, 0xFF, 0xFF },
    { 0x1808, 0x180A, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x180D, 0x180D, IVB_GEN6, IVB_RENDER, 0xFF, 0xFF },
    { 0x180E, 0x1818, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x1819, 0x1821, IVB_GEN7, IVB_RENDER, 0xFF, 0xFF },
    { 0x1823, 0x1833, IVB_GEN7, IVB_RENDER, 0xFF, 0xFF },
    { 0x1900, 0x1900, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x1902, 0x1902, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x1904, 0x1904, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x1905, 0x1905, IVB_GEN6, IVB_RENDER, 0xFF, 0xFF },
    { 0x1906, 0x1908, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x190A, 0x190A, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x190B, 0x190B, IVB_GEN6, IVB_RENDER, 0xFF, 0xFFFF },
    { 0x190C, 0x190C, IVB_GEN6, IVB_RENDER, 0xFF, 0xFF },
    { 0x190D, 0x190D, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x190E, 0x1910, IVB_GEN6, IVB_RENDER, 0xFF, 0xFF },
    { 0x1911, 0x1911, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x1912, 0x1916, IVB_GEN7, IVB_RENDER, 0xFF, 0xFF },
    { 0x1917, 0x1917, IVB_GEN7, IVB_RENDER, 0x1FF, 0x1FF },
    { 0x1918, 0x1918, IVB_GEN7, IVB_RENDER, 0xFF, 0xFF },
    { IVB_GFX_PIPE_CONTROL, IVB_GFX_PIPE_CONTROL, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
    { 0x1B00, 0x1B00, IVB_GENS, IVB_RENDER, 0xFF, 0xFF },
};

static const struct
{
    uint32_t First;
    uint32_t Last;
} IvbUserRegisters[] =
{
    { 0x2290, 0x2297 },
    { 0x2300, 0x235F },
    { 0x2400, 0x2423 },
    { 0x2430, 0x2447 },
    { 0x2500, 0x250B },
    { 0x5200, 0x5293 },
    { 0xB010, 0xB013 },
    { 0xB020, 0xB027 },
};

static const struct
{
    uint32_t First;
    uint32_t Last;
} SnbReadableRegisters[] =
{
    { 0x2280, 0x228F },
    { 0x2310, 0x235F },
};

static bool
IvbUserRegister(
    uint32_t Offset)
{
    size_t Index;

    if (Offset % 4 != 0)
        return false;
    for (Index = 0; Index < sizeof(IvbUserRegisters) / sizeof(IvbUserRegisters[0]); Index++)
    {
        if (Offset >= IvbUserRegisters[Index].First && Offset <= IvbUserRegisters[Index].Last)
            return true;
    }
    return false;
}

static bool
SnbReadableRegister(
    uint32_t Offset)
{
    size_t Index;

    if (Offset % 4 != 0)
        return false;
    for (Index = 0; Index < sizeof(SnbReadableRegisters) / sizeof(SnbReadableRegisters[0]); Index++)
    {
        if (Offset >= SnbReadableRegisters[Index].First && Offset <= SnbReadableRegisters[Index].Last)
            return true;
    }
    return false;
}

static const IVB_COMMAND *
IvbFindCommand(
    const IVB_COMMAND *Commands,
    size_t Count,
    uint32_t Op,
    uint32_t Gen,
    bool Blitter)
{
    uint8_t Gens = Gen >= 7 ? IVB_GEN7 : IVB_GEN6;
    uint8_t Rings = Blitter ? IVB_BLITTER : IVB_RENDER;
    size_t Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Op >= Commands[Index].First && Op <= Commands[Index].Last &&
            (Commands[Index].Gens & Gens) != 0 && (Commands[Index].Rings & Rings) != 0)
        {
            return &Commands[Index];
        }
    }
    return NULL;
}

static bool
IvbCommandLength(
    const IVB_COMMAND *Command,
    uint32_t Header,
    uint32_t Left,
    uint32_t *Length)
{
    if (Command == NULL || (Header & Command->Wide & ~Command->Length) != 0)
        return false;
    *Length = Command->Length == 0 ? 1 : (Header & Command->Length) + 2;
    return *Length <= Left;
}

static bool
IvbCheckMi(
    uint32_t Gen,
    const uint32_t *Dwords,
    uint32_t Length,
    bool *End)
{
    uint32_t Header = Dwords[0];
    uint32_t Index;

    switch (HDGFX_GET(Header, MI_OPCODE))
    {
        case IVB_MI_BATCH_BUFFER_END:
            *End = true;
            return true;
        case IVB_MI_FLUSH_DW:
            return (Header & (IVB_FLUSH_DW_STORE_INDEX | IVB_FLUSH_DW_NOTIFY)) == 0;
        case IVB_MI_LOAD_REGISTER_IMM:
            if (Length % 2 == 0)
                return false;
            for (Index = 1; Index < Length; Index += 2)
            {
                if (!(Gen >= 7 && IvbUserRegister(Dwords[Index])) &&
                    !(Dwords[Index] == HDGFX_REG(RING_INSTPM, 0) && (Dwords[Index + 1] & ~IVB_INSTPM_CONSTANT_OFFSET) == 0))
                {
                    return false;
                }
            }
            return true;
        case IVB_MI_STORE_REGISTER_MEM:
            return Length == IVB_MI_REGISTER_MEM_DWORDS &&
                   (Gen >= 7 ? IvbUserRegister(Dwords[1]) : SnbReadableRegister(Dwords[1]));
        case IVB_MI_LOAD_REGISTER_MEM:
            return Length == IVB_MI_REGISTER_MEM_DWORDS && IvbUserRegister(Dwords[1]);
        default:
            return true;
    }
}

bool
HdgfxIvbCheckBatch(
    uint32_t Gen,
    const uint32_t *Dwords,
    uint32_t Count,
    bool Blitter,
    uint32_t *Offending)
{
    uint32_t Position = 0;
    bool End = false;

    while (Position < Count && !End)
    {
        uint32_t Header = Dwords[Position];
        uint32_t Client = Header >> 29;
        const IVB_COMMAND *Command;
        uint32_t Length = 0;
        bool Allowed;

        if (Client == IVB_CLIENT_MI)
        {
            Command = IvbFindCommand(IvbMiCommands, sizeof(IvbMiCommands) / sizeof(IvbMiCommands[0]),
                                     HDGFX_GET(Header, MI_OPCODE), Gen, Blitter);
            Allowed = IvbCommandLength(Command, Header, Count - Position, &Length) &&
                      IvbCheckMi(Gen, Dwords + Position, Length, &End);
        }
        else if (Client == IVB_CLIENT_GFX)
        {
            Command = IvbFindCommand(IvbGfxCommands, sizeof(IvbGfxCommands) / sizeof(IvbGfxCommands[0]),
                                     (Header >> 16) & 0x1FFF, Gen, Blitter);
            Allowed = IvbCommandLength(Command, Header, Count - Position, &Length) &&
                      (Command->First != IVB_GFX_PIPE_CONTROL ||
                       (Dwords[Position + 1] & (IVB_PC_FORBIDDEN | IVB_PC_NOTIFY)) == 0);
        }
        else if (Client == IVB_CLIENT_BLT)
        {
            Command = IvbFindCommand(IvbBltCommands, sizeof(IvbBltCommands) / sizeof(IvbBltCommands[0]),
                                     (Header >> 22) & 0x7F, Gen, Blitter);
            Allowed = IvbCommandLength(Command, Header, Count - Position, &Length);
        }
        else
            Allowed = false;

        if (!Allowed)
        {
            *Offending = Position;
            return false;
        }
        Position += Length;
    }
    return true;
}
