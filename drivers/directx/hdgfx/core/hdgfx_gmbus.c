/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GMBUS I2C reads through the PCH GMBUS controller
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#define GMBUS_RATE_100KHZ       0
#define GMBUS_CYCLE_INDEX_STOP  7
#define GMBUS_MAX_BYTES         256
#define GMBUS_POLL_US           50
#define GMBUS_TIMEOUT_US        25000

static uint32_t
GmbusRead(
    const HDGFX_MMIO *Mmio,
    uint32_t Offset)
{
    return Mmio->Read(Mmio->Context, Offset);
}

static void
GmbusWrite(
    const HDGFX_MMIO *Mmio,
    uint32_t Offset,
    uint32_t Value)
{
    Mmio->Write(Mmio->Context, Offset, Value);
}

static HDGFX_GMBUS_STATUS
GmbusWait(
    const HDGFX_MMIO *Mmio,
    uint32_t Mask,
    uint32_t Value)
{
    uint32_t Waited;
    uint32_t Status;

    for (Waited = 0; ; Waited += GMBUS_POLL_US)
    {
        Status = GmbusRead(Mmio, HDGFX_REG(GMBUS2, 0));
        if (HDGFX_GET(Status, GMBUS2_NAK))
            return HdgfxGmbusNoAcknowledge;
        if (HDGFX_GET(Status, GMBUS2_STALL_TIMEOUT))
            return HdgfxGmbusSlaveTimeout;
        if ((Status & Mask) == Value)
            return HdgfxGmbusSuccess;
        if (Waited >= GMBUS_TIMEOUT_US)
            return HdgfxGmbusTimeout;
        Mmio->Stall(Mmio->Context, GMBUS_POLL_US);
    }
}

static void
GmbusResetController(
    const HDGFX_MMIO *Mmio)
{
    GmbusWrite(Mmio, HDGFX_REG(GMBUS1, 0), HDGFX_SET(GMBUS1_SW_CLR_INT, 1));
    GmbusWrite(Mmio, HDGFX_REG(GMBUS1, 0), 0);
}

HDGFX_GMBUS_STATUS
HdgfxGmbusRead(
    const HDGFX_MMIO *Mmio,
    uint32_t Pins,
    uint8_t Address,
    uint8_t Index,
    uint8_t *Buffer,
    uint32_t Size)
{
    HDGFX_GMBUS_STATUS Status;
    uint32_t Received = 0;
    uint32_t Data;
    uint32_t Byte;
    uint32_t Waited;

    if (Size == 0 || Size > GMBUS_MAX_BYTES || Pins == 0 || Pins > HDGFX_BITS(~0u, 2, 0))
        return HdgfxGmbusInvalidParameter;

    for (Waited = 0; HDGFX_GET(GmbusRead(Mmio, HDGFX_REG(GMBUS2, 0)), GMBUS2_INUSE); Waited += GMBUS_POLL_US)
    {
        if (Waited >= GMBUS_TIMEOUT_US)
            return HdgfxGmbusBusy;
        Mmio->Stall(Mmio->Context, GMBUS_POLL_US);
    }

    GmbusResetController(Mmio);
    GmbusWrite(Mmio, HDGFX_REG(GMBUS5, 0), 0);
    GmbusWrite(Mmio, HDGFX_REG(GMBUS0, 0),
               HDGFX_SET(GMBUS0_RATE, GMBUS_RATE_100KHZ) | HDGFX_SET(GMBUS0_PINS, Pins));
    GmbusWrite(Mmio, HDGFX_REG(GMBUS1, 0),
               HDGFX_SET(GMBUS1_SW_RDY, 1) |
               HDGFX_SET(GMBUS1_ENABLE_TIMEOUT, 1) |
               HDGFX_SET(GMBUS1_CYCLE, GMBUS_CYCLE_INDEX_STOP) |
               HDGFX_SET(GMBUS1_BYTE_COUNT, Size) |
               HDGFX_SET(GMBUS1_INDEX, Index) |
               HDGFX_SET(GMBUS1_ADDRESS, Address) |
               HDGFX_SET(GMBUS1_READ, 1));

    Status = HdgfxGmbusSuccess;
    while (Received < Size)
    {
        Status = GmbusWait(Mmio, HDGFX_SET(GMBUS2_HW_RDY, 1), HDGFX_SET(GMBUS2_HW_RDY, 1));
        if (Status != HdgfxGmbusSuccess)
            break;
        Data = GmbusRead(Mmio, HDGFX_REG(GMBUS3, 0));
        for (Byte = 0; Byte < 4 && Received < Size; Byte++, Data >>= 8)
            Buffer[Received++] = (uint8_t)Data;
    }
    if (Status == HdgfxGmbusSuccess)
        Status = GmbusWait(Mmio, HDGFX_SET(GMBUS2_ACTIVE, 1), 0);

    if (Status != HdgfxGmbusSuccess)
    {
        GmbusResetController(Mmio);
        (void)GmbusWait(Mmio, HDGFX_SET(GMBUS2_ACTIVE, 1), 0);
    }
    GmbusWrite(Mmio, HDGFX_REG(GMBUS0, 0), 0);
    GmbusWrite(Mmio, HDGFX_REG(GMBUS2, 0), HDGFX_SET(GMBUS2_INUSE, 1));
    return Status;
}

const char *
HdgfxGmbusStatusName(
    HDGFX_GMBUS_STATUS Status)
{
    static const char *const Names[] =
    {
        "success",
        "invalid parameter",
        "in use",
        "no acknowledge",
        "slave stall timeout",
        "timeout"
    };

    return (uint32_t)Status < sizeof(Names) / sizeof(Names[0]) ? Names[Status] : "unknown";
}
