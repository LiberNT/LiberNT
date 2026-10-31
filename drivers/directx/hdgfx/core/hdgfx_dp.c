/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PCH DisplayPort AUX channel, sink discovery and link training
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#include <string.h>

#define DP_PORT_COUNT               3
#define DP_AUX_NATIVE_WRITE         0x8
#define DP_AUX_NATIVE_READ          0x9
#define DP_AUX_I2C_WRITE            0x0
#define DP_AUX_I2C_READ             0x1
#define DP_AUX_I2C_MOT              0x4
#define DP_AUX_REPLY_NACK           1
#define DP_AUX_REPLY_DEFER          2
#define DP_AUX_MAX_MESSAGE          20
#define DP_AUX_MAX_PAYLOAD          16
#define DP_AUX_HEADER               4
#define DP_AUX_DEFERS               32
#define DP_AUX_FAILURES             5
#define DP_AUX_RETRY_US             500
#define DP_AUX_POLL_US              10
#define DP_AUX_TIMEOUT_US           10000
#define DP_AUX_BIT_CLOCK_KHZ        2000

#define DPCD_REVISION               0x000
#define DPCD_CAPS_SIZE              16
#define DPCD_MAX_LINK_RATE          1
#define DPCD_MAX_LANE_COUNT         2
#define DPCD_LANE_COUNT_MASK        0x1F
#define DPCD_ENHANCED_FRAMING       0x80
#define DPCD_AUX_READ_INTERVAL      14
#define DPCD_AUX_READ_INTERVAL_MASK 0x7F
#define DPCD_LINK_BW_SET            0x100
#define DPCD_TRAINING_PATTERN_SET   0x102
#define DPCD_DOWNSPREAD_CTRL        0x107
#define DPCD_LANE0_1_STATUS         0x202
#define DPCD_STATUS_SIZE            6
#define DPCD_STATUS_ALIGN           2
#define DPCD_STATUS_ADJUST          4
#define DPCD_SET_POWER              0x600
#define DPCD_POWER_D0               1
#define DPCD_CODING_8B10B           1

#define DP_LANE_CR_DONE             1
#define DP_LANE_EQ_DONE             7
#define DP_INTERLANE_ALIGN_DONE     1
#define DP_LEVEL_MAX                3
#define DP_LEVEL_MAX_SWING_REACHED  0x04
#define DP_LEVEL_EMPHASIS_SHIFT     3
#define DP_LEVEL_MAX_EMPHASIS_REACHED 0x20
#define DP_TRAINING_NO_SCRAMBLE     0x20
#define DP_SET_PATTERN_1            1
#define DP_SET_PATTERN_2            2
#define DP_SET_PATTERN_NONE         0
#define DP_PORT_PATTERN_1           0
#define DP_PORT_PATTERN_2           1
#define DP_CR_ATTEMPTS              32
#define DP_CR_SAME_LEVEL_ATTEMPTS   5
#define DP_EQ_ATTEMPTS              6
#define DP_CR_DELAY_US              100
#define DP_EQ_DELAY_US              400
#define DP_READ_INTERVAL_UNIT_US    4000
#define DP_SINK_WAKE_US             1000
#define DP_LINK_RATE_270            0x0A
#define DP_LINK_BW_UNIT_KHZ         27000
#define DP_LINK_162_KHZ             162000
#define DP_LINK_270_KHZ             270000

typedef enum _DP_AUX_RESULT
{
    DpAuxSuccess,
    DpAuxRetry,
    DpAuxTimeout
} DP_AUX_RESULT;

static uint32_t
DpRead(
    const HDGFX_MMIO *Mmio,
    uint32_t Offset)
{
    return Mmio->Read(Mmio->Context, Offset);
}

static void
DpWrite(
    const HDGFX_MMIO *Mmio,
    uint32_t Offset,
    uint32_t Value)
{
    Mmio->Write(Mmio->Context, Offset, Value);
}

static void
DpDelay(
    const HDGFX_MMIO *Mmio,
    uint32_t Microseconds)
{
    if (Microseconds >= 1000 && Mmio->Sleep)
        Mmio->Sleep(Mmio->Context, Microseconds / 1000);
    else
        Mmio->Stall(Mmio->Context, Microseconds);
}

static bool
DpAuxIdle(
    const HDGFX_MMIO *Mmio,
    uint32_t Control,
    uint32_t *Status)
{
    uint32_t Elapsed;

    for (Elapsed = 0;; Elapsed += DP_AUX_POLL_US)
    {
        *Status = DpRead(Mmio, Control);
        if (!HDGFX_GET(*Status, DP_AUX_BUSY))
            return true;
        if (Elapsed >= DP_AUX_TIMEOUT_US)
            return false;
        Mmio->Stall(Mmio->Context, DP_AUX_POLL_US);
    }
}

static DP_AUX_RESULT
DpAuxTransfer(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DP_SINK *Sink,
    const uint8_t *Send,
    uint32_t SendSize,
    uint8_t *Receive,
    uint32_t *ReceiveSize)
{
    uint32_t Control = HDGFX_REG(DP_AUX_CTL, Sink->Port);
    uint32_t Data = HDGFX_REG(DP_AUX_DATA, Sink->Port);
    uint32_t RawKHz = HDGFX_GET(DpRead(Mmio, HDGFX_REG(PCH_RAWCLK_FREQ, 0)), RAWCLK_MHZ) * 1000;
    uint32_t Status;
    uint32_t Word;
    uint32_t Index;

    if (!DpAuxIdle(Mmio, Control, &Status))
        return DpAuxTimeout;

    for (Word = 0; Word * 4 < SendSize; Word++)
    {
        uint32_t Value = 0;

        for (Index = 0; Index < 4; Index++)
        {
            if (Word * 4 + Index < SendSize)
                Value |= (uint32_t)Send[Word * 4 + Index] << (24 - Index * 8);
        }
        DpWrite(Mmio, Data + Word * 4, Value);
    }

    DpWrite(Mmio, Control,
            HDGFX_SET(DP_AUX_BUSY, 1) | HDGFX_SET(DP_AUX_DONE, 1) | HDGFX_SET(DP_AUX_TIMEOUT, 1) |
            HDGFX_SET(DP_AUX_RECEIVE_ERROR, 1) | HDGFX_SET(DP_AUX_SIZE, SendSize) |
            HDGFX_SET(DP_AUX_PRECHARGE, Sink->Precharge) |
            HDGFX_SET(DP_AUX_CLOCK, (RawKHz + DP_AUX_BIT_CLOCK_KHZ / 2) / DP_AUX_BIT_CLOCK_KHZ));
    if (!DpAuxIdle(Mmio, Control, &Status))
        return DpAuxTimeout;
    DpWrite(Mmio, Control, Status);

    if (HDGFX_GET(Status, DP_AUX_TIMEOUT))
        return DpAuxTimeout;
    if (!HDGFX_GET(Status, DP_AUX_DONE) || HDGFX_GET(Status, DP_AUX_RECEIVE_ERROR))
        return DpAuxRetry;
    if (HDGFX_GET(Status, DP_AUX_SIZE) == 0 || HDGFX_GET(Status, DP_AUX_SIZE) > DP_AUX_MAX_MESSAGE)
        return DpAuxRetry;

    *ReceiveSize = HDGFX_GET(Status, DP_AUX_SIZE);
    for (Index = 0; Index < *ReceiveSize; Index++)
    {
        if (Index % 4 == 0)
            Word = DpRead(Mmio, Data + Index);
        Receive[Index] = (uint8_t)(Word >> (24 - (Index % 4) * 8));
    }
    return DpAuxSuccess;
}

static bool
DpAuxRequest(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DP_SINK *Sink,
    uint32_t Command,
    uint32_t Address,
    const uint8_t *Write,
    uint32_t WriteSize,
    uint8_t *Read,
    uint32_t ReadSize,
    uint32_t *ReadDone)
{
    uint8_t Message[DP_AUX_MAX_MESSAGE];
    uint8_t Reply[DP_AUX_MAX_MESSAGE];
    uint32_t MessageSize = DP_AUX_HEADER - 1;
    uint32_t Payload = WriteSize != 0 ? WriteSize : ReadSize;
    uint32_t Defers = 0;
    uint32_t Failures = 0;
    uint32_t Received;

    if (WriteSize > DP_AUX_MAX_PAYLOAD || ReadSize > DP_AUX_MAX_PAYLOAD)
        return false;

    Message[0] = (uint8_t)((Command << 4) | ((Address >> 16) & 0xF));
    Message[1] = (uint8_t)(Address >> 8);
    Message[2] = (uint8_t)Address;
    if (Payload != 0)
    {
        Message[MessageSize++] = (uint8_t)(Payload - 1);
        if (WriteSize != 0)
        {
            memcpy(&Message[MessageSize], Write, WriteSize);
            MessageSize += WriteSize;
        }
    }

    while (Defers < DP_AUX_DEFERS && Failures < DP_AUX_FAILURES)
    {
        DP_AUX_RESULT Result = DpAuxTransfer(Mmio, Sink, Message, MessageSize, Reply, &Received);

        if (Result != DpAuxSuccess)
        {
            Failures++;
            Mmio->Stall(Mmio->Context, DP_AUX_RETRY_US);
            continue;
        }

        if (((Reply[0] >> 4) & 3) == DP_AUX_REPLY_NACK || (Reply[0] >> 6) == DP_AUX_REPLY_NACK)
            return false;
        if (((Reply[0] >> 4) & 3) == DP_AUX_REPLY_DEFER || (Reply[0] >> 6) == DP_AUX_REPLY_DEFER)
        {
            Defers++;
            Mmio->Stall(Mmio->Context, DP_AUX_RETRY_US);
            continue;
        }

        if (ReadSize != 0)
        {
            uint32_t Bytes = Received - 1 < ReadSize ? Received - 1 : ReadSize;

            memcpy(Read, &Reply[1], Bytes);
            *ReadDone = Bytes;
        }
        return true;
    }
    return false;
}

static bool
DpcdRead(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DP_SINK *Sink,
    uint32_t Address,
    uint8_t *Buffer,
    uint32_t Size)
{
    uint32_t Done = 0;
    uint32_t Attempts = 0;

    while (Done < Size && Attempts < DP_AUX_DEFERS)
    {
        uint32_t Chunk = Size - Done < DP_AUX_MAX_PAYLOAD ? Size - Done : DP_AUX_MAX_PAYLOAD;
        uint32_t Bytes = 0;

        if (!DpAuxRequest(Mmio, Sink, DP_AUX_NATIVE_READ, Address + Done, NULL, 0, Buffer + Done, Chunk, &Bytes))
            return false;
        Done += Bytes;
        Attempts++;
    }
    return Done == Size;
}

static bool
DpcdWrite(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DP_SINK *Sink,
    uint32_t Address,
    const uint8_t *Buffer,
    uint32_t Size)
{
    return DpAuxRequest(Mmio, Sink, DP_AUX_NATIVE_WRITE, Address, Buffer, Size, NULL, 0, NULL);
}

bool
HdgfxDpFindSink(
    const HDGFX_MMIO *Mmio,
    uint32_t Precharge,
    HDGFX_DP_SINK *Sink)
{
    uint8_t Caps[DPCD_CAPS_SIZE];
    uint32_t Port;

    for (Port = 0; Port < DP_PORT_COUNT; Port++)
    {
        uint32_t Lanes;

        if (!HDGFX_GET(DpRead(Mmio, HDGFX_REG(DP_CTL, Port)), DP_DETECTED))
            continue;

        memset(Sink, 0, sizeof(*Sink));
        Sink->Port = Port;
        Sink->Precharge = Precharge;
        if (!DpcdRead(Mmio, Sink, DPCD_REVISION, Caps, sizeof(Caps)) || Caps[DPCD_REVISION] == 0)
            continue;

        Lanes = Caps[DPCD_MAX_LANE_COUNT] & DPCD_LANE_COUNT_MASK;
        if (Lanes != 1 && Lanes != 2 && Lanes != 4)
            continue;

        Sink->Revision = Caps[DPCD_REVISION];
        Sink->MaxLanes = Lanes;
        Sink->MaxLinkKHz = Caps[DPCD_MAX_LINK_RATE] >= DP_LINK_RATE_270 ? DP_LINK_270_KHZ : DP_LINK_162_KHZ;
        Sink->EnhancedFraming = (Caps[DPCD_MAX_LANE_COUNT] & DPCD_ENHANCED_FRAMING) != 0;
        Sink->ReadIntervalUs = (Caps[DPCD_AUX_READ_INTERVAL] & DPCD_AUX_READ_INTERVAL_MASK) * DP_READ_INTERVAL_UNIT_US;
        return true;
    }
    return false;
}

bool
HdgfxDpReadI2c(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DP_SINK *Sink,
    uint8_t Address,
    uint8_t *Buffer,
    uint32_t Size)
{
    uint8_t Offset = 0;
    uint32_t Done = 0;
    uint32_t Attempts = 0;
    bool Success;

    if (!DpAuxRequest(Mmio, Sink, DP_AUX_I2C_WRITE | DP_AUX_I2C_MOT, Address, &Offset, 1, NULL, 0, NULL))
        return false;

    while (Done < Size && Attempts < DP_AUX_DEFERS)
    {
        uint32_t Chunk = Size - Done < DP_AUX_MAX_PAYLOAD ? Size - Done : DP_AUX_MAX_PAYLOAD;
        uint32_t Bytes = 0;

        if (!DpAuxRequest(Mmio, Sink, DP_AUX_I2C_READ | DP_AUX_I2C_MOT, Address, NULL, 0,
                          Buffer + Done, Chunk, &Bytes))
        {
            break;
        }
        Done += Bytes;
        Attempts++;
    }
    Success = Done == Size;
    (void)DpAuxRequest(Mmio, Sink, DP_AUX_I2C_READ, Address, NULL, 0, NULL, 0, NULL);
    return Success;
}

static uint32_t
DpPortControl(
    const HDGFX_DP *Dp,
    uint32_t Training,
    uint32_t Swing,
    uint32_t Emphasis)
{
    return HDGFX_SET(DP_ENABLE, 1) | HDGFX_SET(DP_SWING, Swing) | HDGFX_SET(DP_EMPHASIS, Emphasis) |
           HDGFX_SET(DP_WIDTH, Dp->Lanes - 1) | HDGFX_SET(DP_TRAINING, Training);
}

static bool
DpSetLevels(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DP_SINK *Sink,
    const HDGFX_DP *Dp,
    uint32_t PortPattern,
    uint32_t SinkPattern,
    uint32_t Swing,
    uint32_t Emphasis)
{
    uint8_t Set[1 + 4];
    uint32_t Lane;

    DpWrite(Mmio, HDGFX_REG(DP_CTL, Dp->Port), DpPortControl(Dp, PortPattern, Swing, Emphasis));

    Set[0] = (uint8_t)(SinkPattern | DP_TRAINING_NO_SCRAMBLE);
    for (Lane = 0; Lane < Dp->Lanes; Lane++)
    {
        Set[1 + Lane] = (uint8_t)(Swing | (Emphasis << DP_LEVEL_EMPHASIS_SHIFT) |
                                  (Swing == DP_LEVEL_MAX ? DP_LEVEL_MAX_SWING_REACHED : 0) |
                                  (Emphasis == DP_LEVEL_MAX - Swing ? DP_LEVEL_MAX_EMPHASIS_REACHED : 0));
    }
    return DpcdWrite(Mmio, Sink, DPCD_TRAINING_PATTERN_SET, Set, 1 + Dp->Lanes);
}

static uint32_t
DpLaneNibble(
    const uint8_t *Bytes,
    uint32_t Lane)
{
    return (Bytes[Lane / 2] >> ((Lane & 1) * 4)) & 0xF;
}

static bool
DpLanesDone(
    const uint8_t *Status,
    uint32_t Lanes,
    uint32_t Mask)
{
    uint32_t Lane;

    for (Lane = 0; Lane < Lanes; Lane++)
    {
        if ((DpLaneNibble(Status, Lane) & Mask) != Mask)
            return false;
    }
    return true;
}

static void
DpRequestedLevels(
    const uint8_t *Status,
    uint32_t Lanes,
    uint32_t *Swing,
    uint32_t *Emphasis)
{
    uint32_t Lane;

    *Swing = 0;
    *Emphasis = 0;
    for (Lane = 0; Lane < Lanes; Lane++)
    {
        uint32_t Request = DpLaneNibble(&Status[DPCD_STATUS_ADJUST], Lane);

        if ((Request & 3) > *Swing)
            *Swing = Request & 3;
        if ((Request >> 2) > *Emphasis)
            *Emphasis = Request >> 2;
    }
    if (*Emphasis > DP_LEVEL_MAX - *Swing)
        *Emphasis = DP_LEVEL_MAX - *Swing;
}

static bool
DpTrainClock(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DP_SINK *Sink,
    const HDGFX_DP *Dp,
    uint32_t *Swing,
    uint32_t *Emphasis)
{
    uint8_t Status[DPCD_STATUS_SIZE];
    uint32_t Same = 0;
    uint32_t Attempt;

    for (Attempt = 0; Attempt < DP_CR_ATTEMPTS; Attempt++)
    {
        uint32_t NewSwing;
        uint32_t NewEmphasis;

        if (!DpSetLevels(Mmio, Sink, Dp, DP_PORT_PATTERN_1, DP_SET_PATTERN_1, *Swing, *Emphasis))
            return false;
        DpDelay(Mmio, Sink->ReadIntervalUs != 0 ? Sink->ReadIntervalUs : DP_CR_DELAY_US);
        if (!DpcdRead(Mmio, Sink, DPCD_LANE0_1_STATUS, Status, sizeof(Status)))
            return false;
        if (DpLanesDone(Status, Dp->Lanes, DP_LANE_CR_DONE))
            return true;

        DpRequestedLevels(Status, Dp->Lanes, &NewSwing, &NewEmphasis);
        Same = NewSwing == *Swing && NewEmphasis == *Emphasis ? Same + 1 : 0;
        if (Same == DP_CR_SAME_LEVEL_ATTEMPTS)
            return false;
        *Swing = NewSwing;
        *Emphasis = NewEmphasis;
    }
    return false;
}

static bool
DpTrainEqualization(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DP_SINK *Sink,
    const HDGFX_DP *Dp,
    uint32_t *Swing,
    uint32_t *Emphasis)
{
    uint8_t Status[DPCD_STATUS_SIZE];
    uint32_t Attempt;

    for (Attempt = 0; Attempt < DP_EQ_ATTEMPTS; Attempt++)
    {
        if (!DpSetLevels(Mmio, Sink, Dp, DP_PORT_PATTERN_2, DP_SET_PATTERN_2, *Swing, *Emphasis))
            return false;
        DpDelay(Mmio, Sink->ReadIntervalUs != 0 ? Sink->ReadIntervalUs : DP_EQ_DELAY_US);
        if (!DpcdRead(Mmio, Sink, DPCD_LANE0_1_STATUS, Status, sizeof(Status)))
            return false;
        if (!DpLanesDone(Status, Dp->Lanes, DP_LANE_CR_DONE))
            return false;
        if (DpLanesDone(Status, Dp->Lanes, DP_LANE_EQ_DONE) && (Status[DPCD_STATUS_ALIGN] & DP_INTERLANE_ALIGN_DONE))
            return true;
        DpRequestedLevels(Status, Dp->Lanes, Swing, Emphasis);
    }
    return false;
}

HDGFX_MODESET_STATUS
HdgfxDpTrain(
    const HDGFX_MMIO *Mmio,
    const HDGFX_DP_SINK *Sink,
    const HDGFX_DP *Dp)
{
    uint8_t Power = DPCD_POWER_D0;
    uint8_t Link[2];
    uint8_t Coding[2] = { 0, DPCD_CODING_8B10B };
    uint8_t Pattern = DP_SET_PATTERN_NONE;
    uint32_t Swing = 0;
    uint32_t Emphasis = 0;

    (void)DpcdWrite(Mmio, Sink, DPCD_SET_POWER, &Power, 1);
    DpDelay(Mmio, DP_SINK_WAKE_US);

    Link[0] = (uint8_t)(Dp->LinkKHz / DP_LINK_BW_UNIT_KHZ);
    Link[1] = (uint8_t)(Dp->Lanes | (Dp->EnhancedFraming ? DPCD_ENHANCED_FRAMING : 0));
    DpWrite(Mmio, HDGFX_REG(DP_CTL, Dp->Port), DpPortControl(Dp, DP_PORT_PATTERN_1, Swing, Emphasis));
    if (!DpcdWrite(Mmio, Sink, DPCD_LINK_BW_SET, Link, sizeof(Link)) ||
        !DpcdWrite(Mmio, Sink, DPCD_DOWNSPREAD_CTRL, Coding, sizeof(Coding)))
    {
        return HdgfxModesetDpAuxFailed;
    }

    if (!DpTrainClock(Mmio, Sink, Dp, &Swing, &Emphasis))
        return HdgfxModesetDpClockRecoveryFailed;
    if (!DpTrainEqualization(Mmio, Sink, Dp, &Swing, &Emphasis))
        return HdgfxModesetDpEqualizationFailed;

    DpWrite(Mmio, HDGFX_REG(DP_CTL, Dp->Port), DpPortControl(Dp, HDGFX_DP_TRAINING_NORMAL, Swing, Emphasis));
    if (!DpcdWrite(Mmio, Sink, DPCD_TRAINING_PATTERN_SET, &Pattern, 1))
        return HdgfxModesetDpAuxFailed;
    return HdgfxModesetSuccess;
}
