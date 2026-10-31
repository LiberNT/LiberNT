/*
 * PROJECT:     LiberNT Intel HD Graphics Driver tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Sandy Bridge generation table, engine setup, ring sequences and VGA and DisplayPort mode set on the host
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REGISTERS 1024
#define SNB_DEVICE_HD3000 0x0122
#define TEST_STATUS_ADDRESS 0x00100000u
#define TEST_RING_DWORDS 1024

typedef struct _FAKE_SNB
{
    uint32_t Count;
    uint32_t Offset[MAX_REGISTERS];
    uint32_t Value[MAX_REGISTERS];
    uint32_t LockSwing;
    uint32_t SwingTx[64];
    uint32_t SwingWrites;
    uint32_t ForceWakeMtWrites;
    bool NeverLock;
    bool SinkPresent;
    bool NeverEqualize;
    bool NeverAlign;
    bool PortOffAfterFdi;
    uint32_t SinkPort;
    uint32_t RequiredSwing;
    uint32_t RequiredEmphasis;
    uint32_t AuxDefers;
    uint32_t AuxDefersLeft;
    uint32_t I2cOffset;
    uint32_t I2cStops;
    uint32_t PatternMismatches;
    uint32_t TransDpAtPortEnable;
    uint8_t Dpcd[0x700];
    uint8_t Edid[128];
} FAKE_SNB;

static unsigned Failures;

#define CHECK_EQ(Actual, Expected) \
    CheckEqual(#Actual, (unsigned long long)(Actual), (unsigned long long)(Expected), __LINE__)

static void
CheckEqual(
    const char *Name,
    unsigned long long Actual,
    unsigned long long Expected,
    int Line)
{
    if (Actual != Expected)
    {
        fprintf(stderr, "line %d: %s = 0x%llx, expected 0x%llx\n", Line, Name, Actual, Expected);
        Failures++;
    }
}

static uint32_t *
FakeSlot(
    FAKE_SNB *Fake,
    uint32_t Offset)
{
    uint32_t Index;

    for (Index = 0; Index < Fake->Count; Index++)
    {
        if (Fake->Offset[Index] == Offset)
            return &Fake->Value[Index];
    }
    if (Fake->Count == MAX_REGISTERS)
    {
        fprintf(stderr, "fake register table full\n");
        exit(2);
    }
    Fake->Offset[Fake->Count] = Offset;
    Fake->Value[Fake->Count] = 0;
    return &Fake->Value[Fake->Count++];
}

static void
FakeSinkStatus(
    FAKE_SNB *Fake)
{
    uint8_t Pattern = Fake->Dpcd[0x102] & 3;
    uint8_t Lane = Fake->Dpcd[0x103];
    bool Clock = (Lane & 3) >= Fake->RequiredSwing && ((Lane >> 3) & 3) >= Fake->RequiredEmphasis;
    uint8_t Status = 0;

    if (Pattern != 0 && Clock)
        Status = 1;
    if (Pattern == 2 && Clock && !Fake->NeverEqualize)
        Status = 7;
    Fake->Dpcd[0x202] = Fake->Dpcd[0x203] = (uint8_t)(Status | (Status << 4));
    Fake->Dpcd[0x204] = Status == 7 && !Fake->NeverAlign ? 1 : 0;
    Fake->Dpcd[0x206] = Fake->Dpcd[0x207] =
        (uint8_t)((Fake->RequiredSwing | (Fake->RequiredEmphasis << 2)) * 0x11);
}

static void
FakeAux(
    FAKE_SNB *Fake,
    uint32_t Port,
    uint32_t Control)
{
    uint32_t Data = HDGFX_REG(DP_AUX_DATA, Port);
    uint8_t Message[20];
    uint8_t Reply[20];
    uint32_t Size = HDGFX_GET(Control, DP_AUX_SIZE);
    uint32_t ReplySize = 1;
    uint32_t Address;
    uint32_t Command;
    uint32_t Length;
    uint32_t Index;

    if (!Fake->SinkPresent || Port != Fake->SinkPort)
    {
        *FakeSlot(Fake, HDGFX_REG(DP_AUX_CTL, Port)) = HDGFX_SET(DP_AUX_DONE, 1) | HDGFX_SET(DP_AUX_TIMEOUT, 1);
        return;
    }

    for (Index = 0; Index < Size; Index++)
        Message[Index] = (uint8_t)(*FakeSlot(Fake, Data + (Index & ~3u)) >> (24 - (Index % 4) * 8));
    Command = Message[0] >> 4;
    Address = ((Message[0] & 0xFu) << 16) | (Message[1] << 8) | Message[2];
    Length = Size > 3 ? Message[3] + 1u : 0;
    Reply[0] = 0;

    if (Fake->AuxDefersLeft != 0)
    {
        Fake->AuxDefersLeft--;
        Reply[0] = 2 << 4;
    }
    else if (Command == 0x8)
    {
        memcpy(&Fake->Dpcd[Address], &Message[4], Length);
        if (Address == 0x102 && HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(DP_CTL, Port)), DP_TRAINING) + 1 !=
            (Message[4] & 3u))
        {
            if ((Message[4] & 3u) != 0)
                Fake->PatternMismatches++;
        }
        FakeSinkStatus(Fake);
    }
    else if (Command == 0x9)
    {
        memcpy(&Reply[1], &Fake->Dpcd[Address], Length);
        ReplySize += Length;
    }
    else if (Address != 0x50)
    {
        Reply[0] = 1 << 6;
    }
    else if ((Command & 1) == 0)
    {
        Fake->I2cOffset = Message[4];
    }
    else if (Length != 0)
    {
        memcpy(&Reply[1], &Fake->Edid[Fake->I2cOffset], Length);
        Fake->I2cOffset += Length;
        ReplySize += Length;
    }
    else if ((Command & 4) == 0)
    {
        Fake->I2cStops++;
    }

    for (Index = 0; Index < 5; Index++)
        *FakeSlot(Fake, Data + Index * 4) = 0;
    for (Index = 0; Index < ReplySize; Index++)
        *FakeSlot(Fake, Data + (Index & ~3u)) |= (uint32_t)Reply[Index] << (24 - (Index % 4) * 8);
    *FakeSlot(Fake, HDGFX_REG(DP_AUX_CTL, Port)) = HDGFX_SET(DP_AUX_DONE, 1) | HDGFX_SET(DP_AUX_SIZE, ReplySize);
}

static uint32_t
FakeRead(
    void *Context,
    uint32_t Offset)
{
    FAKE_SNB *Fake = Context;
    uint32_t Pipe;

    if (Offset == HDGFX_REG(FORCEWAKE_ACK, 0))
        return *FakeSlot(Fake, HDGFX_REG(FORCEWAKE, 0)) & 1;
    for (Pipe = 0; Pipe < 2; Pipe++)
    {
        if (Offset == HDGFX_REG(FDI_RX_ISR, Pipe))
        {
            uint32_t Tx = *FakeSlot(Fake, HDGFX_REG(FDI_TX_CTL, Pipe));
            uint32_t Rx = *FakeSlot(Fake, HDGFX_REG(FDI_RX_CTL, Pipe));
            uint32_t Training = HDGFX_GET(Tx, FDI_TX_TRAINING);
            bool Linked = !Fake->NeverLock && HDGFX_GET(Tx, FDI_TX_ENABLE) && HDGFX_GET(Rx, FDI_RX_ENABLE) &&
                          HDGFX_GET(Tx, FDI_TX_PLL_ENABLE) && HDGFX_GET(Rx, FDI_RX_PCDCLK) &&
                          Training == HDGFX_GET(Rx, FDI_RX_TRAINING_CPT) &&
                          HDGFX_GET(Tx, FDI_TX_SWING_EMPHASIS) == Fake->LockSwing;

            if (!Linked)
                return 0;
            return Training == 0 ? HDGFX_SET(FDI_RX_BIT_LOCK, 1) : HDGFX_SET(FDI_RX_SYMBOL_LOCK, 1);
        }
    }
    return *FakeSlot(Fake, Offset);
}

static void
FakeWrite(
    void *Context,
    uint32_t Offset,
    uint32_t Value)
{
    FAKE_SNB *Fake = Context;
    uint32_t Pipe;

    if (Offset == HDGFX_REG(FORCEWAKE, 0))
    {
        uint32_t *Slot = FakeSlot(Fake, Offset);

        *Slot = (*Slot & ~(Value >> 16)) | (Value & (Value >> 16));
        return;
    }
    if (Offset == HDGFX_REG(FORCEWAKE_MT, 0))
        Fake->ForceWakeMtWrites++;
    for (Pipe = 0; Pipe < 3; Pipe++)
    {
        if (Offset == HDGFX_REG(DP_AUX_CTL, Pipe) && HDGFX_GET(Value, DP_AUX_BUSY))
        {
            FakeAux(Fake, Pipe, Value);
            return;
        }
        if (Offset == HDGFX_REG(DP_CTL, Pipe))
        {
            if (HDGFX_GET(Value, DP_ENABLE) && !HDGFX_GET(*FakeSlot(Fake, Offset), DP_ENABLE))
            {
                Fake->TransDpAtPortEnable = *FakeSlot(Fake, HDGFX_REG(TRANS_DP_CTL, 1));
            }
            else if (!HDGFX_GET(Value, DP_ENABLE) && HDGFX_GET(*FakeSlot(Fake, Offset), DP_ENABLE) &&
                     !HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(FDI_TX_CTL, 0)), FDI_TX_ENABLE) &&
                     !HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(FDI_TX_CTL, 1)), FDI_TX_ENABLE))
            {
                Fake->PortOffAfterFdi = true;
            }
            Value |= *FakeSlot(Fake, Offset) & HDGFX_MASK(DP_DETECTED);
        }
    }
    for (Pipe = 0; Pipe < 2; Pipe++)
    {
        if (Offset == HDGFX_REG(PIPE_CONF, Pipe))
            Value = HDGFX_REPLACE(Value, PIPE_CONF_STATE, HDGFX_GET(Value, PIPE_CONF_ENABLE));
        else if (Offset == HDGFX_REG(TRANS_CONF, Pipe))
            Value = HDGFX_REPLACE(Value, TRANS_CONF_STATE, HDGFX_GET(Value, TRANS_CONF_ENABLE));
        else if (Offset == HDGFX_REG(FDI_TX_CTL, Pipe) &&
                 Fake->SwingWrites < sizeof(Fake->SwingTx) / sizeof(Fake->SwingTx[0]))
        {
            Fake->SwingTx[Fake->SwingWrites++] = Value;
        }
    }
    *FakeSlot(Fake, Offset) = Value;
}

static void
FakeDelay(
    void *Context,
    uint32_t Time)
{
    (void)Context;
    (void)Time;
}

static FAKE_SNB *
FakeCreate(
    HDGFX_MMIO *Mmio)
{
    FAKE_SNB *Fake = calloc(1, sizeof(*Fake));

    if (!Fake)
        exit(2);
    Mmio->Read = FakeRead;
    Mmio->Context = Fake;
    Mmio->Write = FakeWrite;
    Mmio->Stall = FakeDelay;
    Mmio->Sleep = FakeDelay;
    *FakeSlot(Fake, HDGFX_REG(PCH_RAWCLK_FREQ, 0)) = 125;
    return Fake;
}

static void
TestGeneration(void)
{
    const HDGFX_GENERATION *Snb = HdgfxFindGeneration(0x8086, SNB_DEVICE_HD3000);

    if (!Snb)
    {
        fprintf(stderr, "HD Graphics 3000 is not listed\n");
        exit(1);
    }
    CHECK_EQ(Snb->Gen, 6);
    CHECK_EQ(Snb->CursorStride, 0x40);
    CHECK_EQ(Snb->PanelDdcPins, 3);
    CHECK_EQ(Snb->CrtDdcPins, 2);
    CHECK_EQ(Snb->GseInterrupt, 1u << 18);
    CHECK_EQ(Snb->PchInterrupt, 1u << 21);
    CHECK_EQ(Snb->CrtHotplugInterrupt, 1u << 19);
    CHECK_EQ(Snb->GtInterrupts, HDGFX_SET(GT_RENDER_USER, 1) | HDGFX_SET(GT_BLT_USER, 1));
    CHECK_EQ(Snb->BltStatusRegister, 0x24080);
    CHECK_EQ(Snb->GttPhysicalLimit, 0xFFFFFFFFFFull);
    CHECK_EQ(Snb->GttEntry(0x2345678000ull), 0x45678233);
}

static void
TestForceWake(void)
{
    HDGFX_MMIO Mmio;
    FAKE_SNB *Fake = FakeCreate(&Mmio);

    CHECK_EQ(HdgfxSnbForceWake(&Mmio), 1);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(FORCEWAKE, 0)), 1);
    CHECK_EQ(Fake->ForceWakeMtWrites, 0);
    HdgfxSnbForceWakeRelease(&Mmio);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(FORCEWAKE, 0)), 0);
    free(Fake);
}

static void
TestInitEngine(void)
{
    HDGFX_MMIO Mmio;
    FAKE_SNB *Fake = FakeCreate(&Mmio);

    *FakeSlot(Fake, HDGFX_REG(SNB_UCGCTL1, 0)) = 0x1;
    HdgfxSnbInitEngine(&Mmio);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(SNB_UCGCTL1, 0)), 0xA1);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(SNB_UCGCTL2, 0)), 0x1800);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(RING_MI_MODE, 0)), 0x40404040);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(SNB_GFX_MODE, 0)), 0x20002000);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(SNB_3D_CHICKEN, 0)), 0x04000400);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(CHICKEN_3D3, 0)), 0x00220022);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(SNB_GT_MODE, 0)), 0x02800200);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(SNB_CACHE_MODE_0, 0)), 0x00210000);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(RING_INSTPM, 0)), 0x00800080);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(GT_MODE, 0)), 0);
    free(Fake);
}

static void
RingInit(
    HDGFX_RING *Ring,
    uint32_t *Commands,
    bool Blitter)
{
    memset(Ring, 0, sizeof(*Ring));
    memset(Commands, 0, TEST_RING_DWORDS * sizeof(uint32_t));
    Ring->Commands = Commands;
    Ring->Size = TEST_RING_DWORDS * sizeof(uint32_t);
    Ring->StatusAddress = TEST_STATUS_ADDRESS;
    Ring->Gen = 6;
    Ring->Blitter = Blitter;
}

static void
TestRing(void)
{
    static uint32_t Commands[TEST_RING_DWORDS];
    uint32_t Scratch = TEST_STATUS_ADDRESS + 0xC0;
    uint32_t Fence = TEST_STATUS_ADDRESS + 0x84;
    uint32_t Batch[2];
    HDGFX_RING Ring;

    CHECK_EQ(HdgfxIvbMiBatchStart(Batch, 0x1000), 2);
    CHECK_EQ(Batch[0], 0x18800000);
    CHECK_EQ(Batch[1], 0x1000);

    RingInit(&Ring, Commands, false);
    CHECK_EQ(HdgfxIvbEmitInvalidate(&Ring, 0), 1);
    CHECK_EQ(Ring.Tail, 12 * 4);
    CHECK_EQ(Commands[0], 0x7A000002);
    CHECK_EQ(Commands[1], 0x00100002);
    CHECK_EQ(Commands[2], 0);
    CHECK_EQ(Commands[5], 0x00004000);
    CHECK_EQ(Commands[6], Scratch | 4);
    CHECK_EQ(Commands[9], 0x00144C1C);
    CHECK_EQ(Commands[10], Scratch | 4);

    RingInit(&Ring, Commands, false);
    CHECK_EQ(HdgfxRingEmitBreadcrumb(&Ring, 0, 0x21, 0xABCD), 0);
    RingInit(&Ring, Commands, false);
    CHECK_EQ(HdgfxRingEmitBreadcrumb(&Ring, 0, 0x22, 0xABCD), 1);
    Fence = TEST_STATUS_ADDRESS + 0x88;
    CHECK_EQ(Ring.Tail, 14 * 4);
    CHECK_EQ(Commands[1], 0x00100002);
    CHECK_EQ(Commands[5], 0x00004000);
    CHECK_EQ(Commands[9], 0x00105021);
    CHECK_EQ(Commands[10], Fence | 4);
    CHECK_EQ(Commands[11], 0xABCD);
    CHECK_EQ(Commands[12], 0x01000000);
    CHECK_EQ(Commands[13], 0);

    RingInit(&Ring, Commands, true);
    CHECK_EQ(HdgfxRingEmitBreadcrumb(&Ring, 0, 0x22, 0xABCD), 1);
    CHECK_EQ(Ring.Tail, 4 * 4);
    CHECK_EQ(Commands[0], 0x13204001);
    CHECK_EQ(Commands[1], 0x88 | 4);
    CHECK_EQ(Commands[2], 0xABCD);
    CHECK_EQ(Commands[3], 0x01000000);
}

static HDGFX_MODESET_STATUS
SetVga(
    const HDGFX_MMIO *Mmio,
    HDGFX_DISPLAY_STATE *Current,
    HDGFX_DISPLAY_STATE *Target)
{
    static const HDGFX_DTD Timing =
    {
        148500,
        { 1920, 1920, 2200, 2008, 2052, 2200, 1080, 1080, 1125, 1084, 1089, 1125 },
        true,
        true,
        0,
        0
    };
    HDGFX_MODESET_STATUS Status;
    HDGFX_CRT_MODE Mode;

    memset(&Mode, 0, sizeof(Mode));
    Mode.Timing = &Timing;
    Mode.Pipe = 1;
    Mode.SourceWidth = 1920;
    Mode.SourceHeight = 1080;
    Mode.Stride = 7680;
    Mode.Surface = 0x00400000;
    Mode.Scaling = HdgfxScalingAspect;

    HdgfxSnbReadDisplayState(Mmio, Current);
    Status = HdgfxIlkComputeCrt(Current, &Mode, Target);
    if (Status != HdgfxModesetSuccess)
        return Status;
    return HdgfxIlkCommitCrt(Mmio, Current, Target);
}

static void
TestVga(void)
{
    HDGFX_MMIO Mmio;
    FAKE_SNB *Fake = FakeCreate(&Mmio);
    HDGFX_DISPLAY_STATE *Current = malloc(sizeof(*Current));
    HDGFX_DISPLAY_STATE *Target = malloc(sizeof(*Target));
    const HDGFX_DPLL *Dpll;
    const char *Mismatch;
    uint32_t Tx;
    uint32_t Rx;
    uint32_t Index;
    bool SawPattern2 = false;

    if (!Current || !Target)
        exit(2);

    Fake->LockSwing = 0x39;
    CHECK_EQ(SetVga(&Mmio, Current, Target), HdgfxModesetSuccess);
    CHECK_EQ(Current->Gen, 6);

    Dpll = &Target->Dplls[1];
    CHECK_EQ(Dpll->Mode, HdgfxDpllModeNonLvds);
    CHECK_EQ(Dpll->DotKHz / 1000, 148);
    CHECK_EQ(Target->Crt.Transcoder, 1);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(PCH_ADPA, 0)), ADPA_ENABLE), 1);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(PCH_ADPA, 0)), ADPA_TRANSCODER_CPT), 1);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(PCH_DPLL_SEL, 0)) & 0xF0, 0x90);

    for (Index = 0; Index < Fake->SwingWrites; Index++)
    {
        if (HDGFX_GET(Fake->SwingTx[Index], FDI_TX_TRAINING) == 1)
        {
            if (!SawPattern2)
                CHECK_EQ(HDGFX_GET(Fake->SwingTx[Index], FDI_TX_SWING_EMPHASIS), 0);
            SawPattern2 = true;
        }
    }
    CHECK_EQ(SawPattern2, 1);

    Tx = *FakeSlot(Fake, HDGFX_REG(FDI_TX_CTL, 1));
    Rx = *FakeSlot(Fake, HDGFX_REG(FDI_RX_CTL, 1));
    CHECK_EQ(HDGFX_GET(Tx, FDI_TX_TRAINING), 3);
    CHECK_EQ(HDGFX_GET(Tx, FDI_TX_TRAINING_IVB), 0);
    CHECK_EQ(HDGFX_GET(Tx, FDI_TX_ENHANCED_FRAMING), 1);
    CHECK_EQ(HDGFX_GET(Tx, FDI_TX_COMPOSITE_SYNC), 0);
    CHECK_EQ(HDGFX_GET(Tx, FDI_TX_AUTO_TRAINING), 0);
    CHECK_EQ(HDGFX_GET(Rx, FDI_RX_TRAINING_CPT), 3);
    CHECK_EQ(HDGFX_GET(Rx, FDI_RX_ENHANCED_FRAMING), 1);
    CHECK_EQ(HDGFX_GET(Rx, FDI_RX_FS_ERROR_CORRECT), 0);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(FDI_RX_MISC, 1)), 0x00200090);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(TRANS_CHICKEN2, 1)) >> 31, 1);

    HdgfxSnbReadDisplayState(&Mmio, Current);
    Mismatch = HdgfxCompareCrtState(Target, Current);
    if (Mismatch)
    {
        fprintf(stderr, "readback differs in %s\n", Mismatch);
        Failures++;
    }
    free(Fake);

    Fake = FakeCreate(&Mmio);
    Fake->NeverLock = true;
    CHECK_EQ(SetVga(&Mmio, Current, Target), HdgfxModesetFdiBitLockFailed);
    free(Fake);

    free(Target);
    free(Current);
}

static const HDGFX_DTD DpTiming =
{
    148500,
    { 1920, 1920, 2200, 2008, 2052, 2200, 1080, 1080, 1125, 1084, 1089, 1125 },
    true,
    true,
    0,
    0
};

static void
FakeSinkInit(
    FAKE_SNB *Fake)
{
    static const uint8_t Caps[] = { 0x12, 0x0A, 0x84, 0x01, 0x01, 0x00, 0x01, 0x00, 0x02, 0x02, 0x06 };
    uint32_t Port;

    Fake->SinkPresent = true;
    Fake->SinkPort = 1;
    Fake->LockSwing = 0;
    memcpy(Fake->Dpcd, Caps, sizeof(Caps));
    for (Port = 0; Port < 3; Port++)
        *FakeSlot(Fake, HDGFX_REG(DP_CTL, Port)) = HDGFX_SET(DP_DETECTED, 1);
}

static void
DpMode(
    HDGFX_CRT_MODE *Mode,
    uint32_t Pipe)
{
    memset(Mode, 0, sizeof(*Mode));
    Mode->Timing = &DpTiming;
    Mode->Pipe = Pipe;
    Mode->SourceWidth = 1920;
    Mode->SourceHeight = 1080;
    Mode->Stride = 7680;
    Mode->Surface = 0x00400000;
    Mode->Scaling = HdgfxScalingAspect;
}

static HDGFX_MODESET_STATUS
SetDp(
    const HDGFX_MMIO *Mmio,
    uint32_t Pipe,
    HDGFX_DISPLAY_STATE *Current,
    HDGFX_DISPLAY_STATE *Target)
{
    HDGFX_CRT_MODE Mode;
    HDGFX_DP_SINK Sink;
    HDGFX_MODESET_STATUS Status;

    DpMode(&Mode, Pipe);
    CHECK_EQ(HdgfxDpFindSink(Mmio, 3, &Sink), 1);
    HdgfxSnbReadDisplayState(Mmio, Current);
    Status = HdgfxIlkComputeDp(Current, &Mode, &Sink, Target);
    if (Status != HdgfxModesetSuccess)
        return Status;
    return HdgfxIlkCommitDp(Mmio, &Sink, Current, Target);
}

static void
TestDpSink(void)
{
    HDGFX_MMIO Mmio;
    FAKE_SNB *Fake = FakeCreate(&Mmio);
    HDGFX_DP_SINK Sink;
    uint8_t Edid[128];
    uint32_t Index;

    CHECK_EQ(HdgfxDpFindSink(&Mmio, 3, &Sink), 0);

    FakeSinkInit(Fake);
    Fake->AuxDefersLeft = 3;
    CHECK_EQ(HdgfxDpFindSink(&Mmio, 3, &Sink), 1);
    CHECK_EQ(Sink.Port, 1);
    CHECK_EQ(Sink.Revision, 0x12);
    CHECK_EQ(Sink.MaxLanes, 4);
    CHECK_EQ(Sink.MaxLinkKHz, 270000);
    CHECK_EQ(Sink.EnhancedFraming, 1);
    CHECK_EQ(Sink.ReadIntervalUs, 0);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(DP_AUX_DATA, 1)), DP_AUX_DONE), 0);

    for (Index = 0; Index < sizeof(Fake->Edid); Index++)
        Fake->Edid[Index] = (uint8_t)(Index ^ 0x5A);
    CHECK_EQ(HdgfxDpReadI2c(&Mmio, &Sink, 0x50, Edid, sizeof(Edid)), 1);
    CHECK_EQ(memcmp(Edid, Fake->Edid, sizeof(Edid)), 0);
    CHECK_EQ(Fake->I2cStops, 1);
    CHECK_EQ(HdgfxDpReadI2c(&Mmio, &Sink, 0x51, Edid, sizeof(Edid)), 0);
    free(Fake);
}

static void
TestDpLinux(void)
{
    HDGFX_MMIO Mmio;
    FAKE_SNB *Fake = FakeCreate(&Mmio);
    HDGFX_DISPLAY_STATE *Current = malloc(sizeof(*Current));
    HDGFX_DISPLAY_STATE *Target = malloc(sizeof(*Target));
    const char *Mismatch;

    if (!Current || !Target)
        exit(2);

    FakeSinkInit(Fake);
    CHECK_EQ(SetDp(&Mmio, 0, Current, Target), HdgfxModesetSuccess);

    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(TRANS_DATA_M1, 0)), 0x7E580000);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(TRANS_DATA_N1, 0)), 0x00800000);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(TRANS_LINK_M1, 0)), 0x00075555);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(TRANS_LINK_N1, 0)), 0x00080000);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(TRANS_DP_CTL, 0)), 0xA0040018);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(DP_CTL, 1)), 0x80180304);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(PCH_DPLL, 0)), 0xC4020002);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(PCH_FP0, 0)), 0x00010C09);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(PCH_DPLL_SEL, 0)) & 0xF, 0x8);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(PIPE_DATA_M1, 0)) & 0xFFFF0000, 0x7E690000);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(PIPE_LINK_M1, 0)), 0x00046666);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(FDI_TX_CTL, 0)), FDI_TX_WIDTH), 1);
    CHECK_EQ(Fake->Dpcd[0x100], 0x06);
    CHECK_EQ(Fake->Dpcd[0x101], 0x84);
    CHECK_EQ(Fake->Dpcd[0x102], 0x00);
    CHECK_EQ(Fake->Dpcd[0x103], 0x00);
    CHECK_EQ(Fake->Dpcd[0x107], 0x00);
    CHECK_EQ(Fake->Dpcd[0x108], 0x01);
    CHECK_EQ(Fake->Dpcd[0x600], 0x01);
    CHECK_EQ(Fake->PatternMismatches, 0);

    HdgfxSnbReadDisplayState(&Mmio, Current);
    Mismatch = HdgfxCompareDpState(Target, Current);
    if (Mismatch)
    {
        fprintf(stderr, "DP readback differs in %s\n", Mismatch);
        Failures++;
    }
    free(Fake);
    free(Target);
    free(Current);
}

static void
TestDpTakeover(void)
{
    HDGFX_MMIO Mmio;
    FAKE_SNB *Fake = FakeCreate(&Mmio);
    HDGFX_DISPLAY_STATE *Current = malloc(sizeof(*Current));
    HDGFX_DISPLAY_STATE *Target = malloc(sizeof(*Target));
    const char *Mismatch;

    if (!Current || !Target)
        exit(2);

    FakeSinkInit(Fake);
    CHECK_EQ(SetDp(&Mmio, 0, Current, Target), HdgfxModesetSuccess);

    Fake->RequiredSwing = 1;
    Fake->RequiredEmphasis = 1;
    Fake->TransDpAtPortEnable = 0;
    CHECK_EQ(SetDp(&Mmio, 1, Current, Target), HdgfxModesetSuccess);

    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(PIPE_CONF, 0)), PIPE_CONF_ENABLE), 0);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(TRANS_CONF, 0)), TRANS_CONF_ENABLE), 0);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(TRANS_DP_CTL, 0)), 0x60000018);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(PCH_DPLL, 0)), DPLL_VCO_ENABLE), 0);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(TRANS_DP_CTL, 1)), 0xA0040018);
    CHECK_EQ(Fake->TransDpAtPortEnable, 0xA0040018);
    CHECK_EQ(Fake->PortOffAfterFdi, 0);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(PCH_DPLL_SEL, 0)) & 0xFF, 0x90);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(DP_CTL, 1)), DP_SWING), 1);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(DP_CTL, 1)), DP_EMPHASIS), 1);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(DP_CTL, 1)), DP_TRAINING), 3);
    CHECK_EQ(Fake->Dpcd[0x103], 0x09);
    CHECK_EQ(Fake->PatternMismatches, 0);

    HdgfxSnbReadDisplayState(&Mmio, Current);
    Mismatch = HdgfxCompareDpState(Target, Current);
    if (Mismatch)
    {
        fprintf(stderr, "DP readback differs in %s\n", Mismatch);
        Failures++;
    }

    CHECK_EQ(HdgfxIlkDisableDp(&Mmio, Current), HdgfxModesetSuccess);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(DP_CTL, 1)), DP_ENABLE), 0);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(TRANS_DP_CTL, 1)), 0x60000018);
    free(Fake);
    free(Target);
    free(Current);
}

static void
TestDpTrainingFailures(void)
{
    HDGFX_MMIO Mmio;
    FAKE_SNB *Fake = FakeCreate(&Mmio);
    HDGFX_DISPLAY_STATE *Current = malloc(sizeof(*Current));
    HDGFX_DISPLAY_STATE *Target = malloc(sizeof(*Target));
    HDGFX_CRT_MODE Mode;
    HDGFX_DP_SINK Sink;

    if (!Current || !Target)
        exit(2);

    FakeSinkInit(Fake);
    Fake->NeverEqualize = true;
    CHECK_EQ(SetDp(&Mmio, 1, Current, Target), HdgfxModesetDpEqualizationFailed);
    free(Fake);

    Fake = FakeCreate(&Mmio);
    FakeSinkInit(Fake);
    Fake->NeverAlign = true;
    CHECK_EQ(SetDp(&Mmio, 1, Current, Target), HdgfxModesetDpEqualizationFailed);
    free(Fake);

    Fake = FakeCreate(&Mmio);
    FakeSinkInit(Fake);
    Fake->RequiredSwing = 3;
    Fake->RequiredEmphasis = 3;
    CHECK_EQ(SetDp(&Mmio, 1, Current, Target), HdgfxModesetDpClockRecoveryFailed);
    free(Fake);

    Fake = FakeCreate(&Mmio);
    FakeSinkInit(Fake);
    Fake->Dpcd[1] = 0x06;
    Fake->Dpcd[2] = 0x82;
    CHECK_EQ(HdgfxDpFindSink(&Mmio, 3, &Sink), 1);
    DpMode(&Mode, 1);
    HdgfxSnbReadDisplayState(&Mmio, Current);
    CHECK_EQ(HdgfxIlkComputeDp(Current, &Mode, &Sink, Target), HdgfxModesetDpBandwidth);

    Fake->Dpcd[1] = 0x0A;
    CHECK_EQ(HdgfxDpFindSink(&Mmio, 3, &Sink), 1);
    CHECK_EQ(HdgfxIlkComputeDp(Current, &Mode, &Sink, Target), HdgfxModesetSuccess);
    CHECK_EQ(Target->Dp.LinkKHz, 270000);
    CHECK_EQ(Target->Dp.Lanes, 2);
    CHECK_EQ(Target->Dplls[1].N, 4);
    CHECK_EQ(Target->Dplls[1].M1, 16);
    CHECK_EQ(Target->Dplls[1].M2, 10);
    CHECK_EQ(Target->Dplls[1].P1, 1);
    free(Fake);
    free(Target);
    free(Current);
}

static void
TestBatchScan(void)
{
    static const uint32_t InstpmConstants[] = { 0x11000001, 0x20C0, 0x00400040, 0x05000000 };
    static const uint32_t Timestamp[] = { 0x12000001, 0x2358, 0, 0x05000000 };
    static const uint32_t SoPrimitives[] = { 0x12000001, 0x2288, 0, 0x05000000 };
    static const uint32_t L3Write[] = { 0x11000001, 0xB020, 0x00800000, 0x05000000 };
    static const uint32_t SoGen7Write[] = { 0x11000001, 0x5200, 0, 0x05000000 };
    static const uint32_t L3Read[] = { 0x12000001, 0xB020, 0, 0x05000000 };
    static const uint32_t StatisticsLoad[] = { 0x14800001, 0x2358, 0, 0x05000000 };
    uint32_t Offending = 0;

    CHECK_EQ(HdgfxIvbCheckBatch(6, InstpmConstants, 4, false, &Offending), 1);
    CHECK_EQ(HdgfxIvbCheckBatch(6, Timestamp, 4, false, &Offending), 1);
    CHECK_EQ(HdgfxIvbCheckBatch(6, SoPrimitives, 4, false, &Offending), 1);
    CHECK_EQ(HdgfxIvbCheckBatch(6, L3Write, 4, false, &Offending), 0);
    CHECK_EQ(HdgfxIvbCheckBatch(6, SoGen7Write, 4, false, &Offending), 0);
    CHECK_EQ(HdgfxIvbCheckBatch(6, L3Read, 4, false, &Offending), 0);
    CHECK_EQ(HdgfxIvbCheckBatch(6, StatisticsLoad, 4, false, &Offending), 0);
    CHECK_EQ(HdgfxIvbCheckBatch(7, L3Write, 4, false, &Offending), 1);
}

static void
TestVgaPlane(void)
{
    HDGFX_MMIO Mmio;
    FAKE_SNB *Fake = FakeCreate(&Mmio);

    *FakeSlot(Fake, HDGFX_REG(CPU_VGACNTRL, 0)) = 0x2C4E;
    HdgfxIlkDisableVgaPlane(&Mmio);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(CPU_VGACNTRL, 0)), 0x80002C4E);
    free(Fake);
}

int
main(void)
{
    TestGeneration();
    TestForceWake();
    TestInitEngine();
    TestRing();
    TestVga();
    TestBatchScan();
    TestDpSink();
    TestDpLinux();
    TestDpTakeover();
    TestDpTrainingFailures();
    TestVgaPlane();
    if (Failures)
    {
        fprintf(stderr, "%u checks failed\n", Failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
