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
#define IVB_SUBCLIENT_SINGLE        1u
#define IVB_SUBCLIENT_MEDIA         2u
#define IVB_MEDIA_GPGPU_OBJECT      0x7104u
#define IVB_MEDIA_GPGPU_WALKER      0x7105u
#define IVB_MI_FIRST_VARIABLE       0x10u
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
#define IVB_MI_REPORT_PERF_COUNT    0x28u
#define IVB_MI_LOAD_REGISTER_MEM    0x29u
#define IVB_MI_REGISTER_MEM_DWORDS  3u
#define IVB_PIPE_CONTROL_HEADER     0x7A000000u
#define IVB_PIPE_CONTROL_MASK       0xFFFF0000u
#define IVB_PC_FORBIDDEN            0x00A00000u
#define IVB_FLUSH_DW_STORE_INDEX    0x00200000u
#define IVB_INSTPM_CONSTANT_OFFSET  0x00400040u

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
IvbCheckMi(
    const uint32_t *Dwords,
    uint32_t Left,
    bool Blitter,
    uint32_t *Length,
    bool *End)
{
    uint32_t Header = Dwords[0];
    uint32_t Opcode = HDGFX_GET(Header, MI_OPCODE);
    uint32_t Index;

    *Length = Opcode < IVB_MI_FIRST_VARIABLE ? 1 : (Header & 0x3F) + 2;
    if (*Length > Left)
        return false;

    switch (Opcode)
    {
        case IVB_MI_BATCH_BUFFER_END:
            *End = true;
            return true;
        case IVB_MI_NOOP:
        case IVB_MI_ARB_CHECK:
            return true;
        case IVB_MI_FLUSH_DW:
            return Blitter && *Length >= 2 && (Header & IVB_FLUSH_DW_STORE_INDEX) == 0;
        case IVB_MI_FLUSH:
        case IVB_MI_REPORT_HEAD:
        case IVB_MI_SUSPEND_FLUSH:
        case IVB_MI_PREDICATE:
        case IVB_MI_TOPOLOGY_FILTER:
        case IVB_MI_SET_APPID:
        case IVB_MI_URB_CLEAR:
        case IVB_MI_STORE_DATA_IMM:
        case IVB_MI_REPORT_PERF_COUNT:
            return !Blitter;
        case IVB_MI_LOAD_REGISTER_IMM:
            if (Blitter || *Length % 2 == 0)
                return false;
            for (Index = 1; Index < *Length; Index += 2)
            {
                if (!IvbUserRegister(Dwords[Index]) &&
                    !(Dwords[Index] == HDGFX_REG(RING_INSTPM, 0) && (Dwords[Index + 1] & ~IVB_INSTPM_CONSTANT_OFFSET) == 0))
                {
                    return false;
                }
            }
            return true;
        case IVB_MI_STORE_REGISTER_MEM:
        case IVB_MI_LOAD_REGISTER_MEM:
            return !Blitter && *Length == IVB_MI_REGISTER_MEM_DWORDS && IvbUserRegister(Dwords[1]);
        default:
            return false;
    }
}

static bool
IvbCheckGfx(
    const uint32_t *Dwords,
    uint32_t Left,
    uint32_t *Length)
{
    uint32_t Header = Dwords[0];
    uint32_t Subclient = (Header >> 27) & 3;

    if (Subclient == IVB_SUBCLIENT_SINGLE)
        *Length = 1;
    else if (Subclient == IVB_SUBCLIENT_MEDIA && (Header >> 16) != IVB_MEDIA_GPGPU_OBJECT &&
             (Header >> 16) != IVB_MEDIA_GPGPU_WALKER)
        *Length = (Header & 0xFFFF) + 2;
    else
        *Length = (Header & 0xFF) + 2;
    if (*Length > Left)
        return false;
    if ((Header & IVB_PIPE_CONTROL_MASK) == IVB_PIPE_CONTROL_HEADER && (*Length < 2 || (Dwords[1] & IVB_PC_FORBIDDEN) != 0))
        return false;
    return true;
}

bool
HdgfxIvbCheckBatch(
    const uint32_t *Dwords,
    uint32_t Count,
    bool Blitter,
    uint32_t *Offending)
{
    uint32_t Position = 0;
    bool End = false;

    while (Position < Count && !End)
    {
        uint32_t Client = Dwords[Position] >> 29;
        uint32_t Length = 0;
        bool Allowed;

        if (Client == IVB_CLIENT_MI)
            Allowed = IvbCheckMi(Dwords + Position, Count - Position, Blitter, &Length, &End);
        else if (Client == IVB_CLIENT_GFX && !Blitter)
            Allowed = IvbCheckGfx(Dwords + Position, Count - Position, &Length);
        else if (Client == IVB_CLIENT_BLT && Blitter)
        {
            Length = (Dwords[Position] & 0xFF) + 2;
            Allowed = Length <= Count - Position;
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
