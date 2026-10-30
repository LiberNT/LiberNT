/*
 * PROJECT:     LiberNT Intel HD Graphics Driver tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Decode and rebuild the 2570p Ivy Bridge display state on the host
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REGISTERS 1024

typedef struct _FAKE_IVB
{
    uint32_t Count;
    uint32_t Offset[MAX_REGISTERS];
    uint32_t Value[MAX_REGISTERS];
    uint32_t Writes;
    uint32_t FailBitLockAttempts;
    uint32_t BitLockPolls;
    uint32_t ScanLine;
    bool DpllSelectedBeforeEnable;
} FAKE_IVB;

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
    FAKE_IVB *Fake,
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

static uint32_t
FakeRead(
    void *Context,
    uint32_t Offset)
{
    FAKE_IVB *Fake = Context;
    uint32_t Value = *FakeSlot(Fake, Offset);

    if (Offset == HDGFX_REG(PIPE_DSL, 0))
        return Fake->ScanLine++ % 790;
    if (Offset == HDGFX_REG(FDI_RX_ISR, 0))
    {
        uint32_t Tx = *FakeSlot(Fake, HDGFX_REG(FDI_TX_CTL, 0));
        uint32_t Rx = *FakeSlot(Fake, HDGFX_REG(FDI_RX_CTL, 0));
        bool Linked = HDGFX_GET(Tx, FDI_TX_ENABLE) && HDGFX_GET(Rx, FDI_RX_ENABLE) &&
                      HDGFX_GET(Rx, FDI_RX_PCDCLK) && HDGFX_GET(Tx, FDI_TX_PLL_ENABLE) &&
                      HDGFX_GET(Tx, FDI_TX_TRAINING_IVB) == HDGFX_GET(Rx, FDI_RX_TRAINING_CPT);

        if (Linked && HDGFX_GET(Rx, FDI_RX_TRAINING_CPT) == 0)
        {
            if (Fake->BitLockPolls++ < Fake->FailBitLockAttempts)
                return 0;
            return HDGFX_SET(FDI_RX_BIT_LOCK, 1);
        }
        return HDGFX_SET(FDI_RX_SYMBOL_LOCK, Linked && HDGFX_GET(Rx, FDI_RX_TRAINING_CPT) == 1);
    }
    return Value;
}

static void
FakeWrite(
    void *Context,
    uint32_t Offset,
    uint32_t Value)
{
    FAKE_IVB *Fake = Context;

    Fake->Writes++;
    if (Offset == HDGFX_REG(PIPE_CONF, 0))
        Value = HDGFX_REPLACE(Value, PIPE_CONF_STATE, HDGFX_GET(Value, PIPE_CONF_ENABLE));
    else if (Offset == HDGFX_REG(TRANS_CONF, 0))
        Value = HDGFX_REPLACE(Value, TRANS_CONF_STATE, HDGFX_GET(Value, TRANS_CONF_ENABLE));
    else if (Offset == HDGFX_REG(PCH_PP_CONTROL, 0))
    {
        *FakeSlot(Fake, HDGFX_REG(PCH_PP_STATUS, 0)) =
            HDGFX_SET(PP_STATUS_ON, HDGFX_GET(Value, PP_CONTROL_TARGET_ON)) | HDGFX_SET(PP_STATUS_READY, 1);
    }
    else if (Offset == HDGFX_REG(PCH_DPLL, 0) && HDGFX_GET(Value, DPLL_VCO_ENABLE))
        Fake->DpllSelectedBeforeEnable = (*FakeSlot(Fake, HDGFX_REG(PCH_DPLL_SEL, 0)) & 8) != 0;
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

static FAKE_IVB *
FakeLoad(
    const char *Directory,
    HDGFX_MMIO *Mmio)
{
    FAKE_IVB *Fake = calloc(1, sizeof(*Fake));
    char Path[1024];
    char Line[512];
    FILE *File;

    snprintf(Path, sizeof(Path), "%s/2570p-lvds-1366x768/intel_reg-dump.txt", Directory);
    File = fopen(Path, "r");
    if (!Fake || !File)
    {
        perror(Path);
        exit(2);
    }
    while (fgets(Line, sizeof(Line), File))
    {
        const char *Open = strstr(Line, "(0x");
        unsigned Offset;
        unsigned Value;

        if (Open && sscanf(Open, "(0x%x): 0x%x", &Offset, &Value) == 2)
            *FakeSlot(Fake, Offset) = Value;
    }
    fclose(File);

    Mmio->Read = FakeRead;
    Mmio->Context = Fake;
    Mmio->Write = FakeWrite;
    Mmio->Stall = FakeDelay;
    Mmio->Sleep = FakeDelay;
    return Fake;
}

static void
TestDevice(void)
{
    const HDGFX_GENERATION *Generation = HdgfxFindGeneration(0x8086, 0x0166);

    if (!Generation)
    {
        fprintf(stderr, "8086:0166 has no generation\n");
        exit(1);
    }
    CHECK_EQ(Generation->Gen, 7);
    CHECK_EQ(Generation->GseInterrupt, 1u << 29);
    CHECK_EQ(Generation->PchInterrupt, 1u << 28);
    CHECK_EQ(Generation->CrtHotplugInterrupt, 1u << 19);
    CHECK_EQ(Generation->CursorStride, 0x1000);
    CHECK_EQ(Generation->PanelDdcPins, 3);
    CHECK_EQ(Generation->CrtDdcPins, 2);
    CHECK_EQ(Generation->GttEntry(0x13EDFF000ull), 0x3EDFF013);
    CHECK_EQ(Generation->GttEntry(0x00400000ull), 0x00400003);
}

static void
TestReadout(
    const char *Directory)
{
    HDGFX_MMIO Mmio;
    FAKE_IVB *Fake = FakeLoad(Directory, &Mmio);
    HDGFX_DISPLAY_STATE State;
    const HDGFX_PIPE *Pipe = &State.Pipes[0];
    const HDGFX_TRANSCODER *Transcoder = &State.Transcoders[0];
    const HDGFX_DPLL *Dpll = &State.Dplls[0];

    HdgfxIvbReadDisplayState(&Mmio, &State);

    CHECK_EQ(State.Gen, 7);
    CHECK_EQ(State.FdiPllVcoKHz, 2700000);
    CHECK_EQ(State.RawClockKHz, 125000);
    CHECK_EQ(State.SscReference, 1);
    CHECK_EQ(State.NonSpreadReference, 1);

    CHECK_EQ(Pipe->Active, 1);
    CHECK_EQ(Pipe->Bpc, 6);
    CHECK_EQ(Pipe->Dither, 1);
    CHECK_EQ(Pipe->Timings.HActive, 1366);
    CHECK_EQ(Pipe->Timings.HSyncStart, 1414);
    CHECK_EQ(Pipe->Timings.HSyncEnd, 1446);
    CHECK_EQ(Pipe->Timings.HTotal, 1578);
    CHECK_EQ(Pipe->Timings.VActive, 768);
    CHECK_EQ(Pipe->Timings.VSyncStart, 770);
    CHECK_EQ(Pipe->Timings.VSyncEnd, 775);
    CHECK_EQ(Pipe->Timings.VTotal, 790);
    CHECK_EQ(Pipe->Fdi.TuSize, 64);
    CHECK_EQ(Pipe->Fdi.DataM, 0x500000);
    CHECK_EQ(Pipe->Fdi.LinkM, 0x238E3);
    CHECK_EQ(Pipe->FdiTx.Enabled, 1);
    CHECK_EQ(Pipe->FdiTx.Training, 3);
    CHECK_EQ(Pipe->FdiTx.Lanes, 1);
    CHECK_EQ(Pipe->Primary.Format, 6);
    CHECK_EQ(Pipe->Primary.XTiled, 1);
    CHECK_EQ(Pipe->Primary.Stride, 5632);
    CHECK_EQ(Pipe->Primary.Surface, 0x03040000);

    CHECK_EQ(Transcoder->Active, 1);
    CHECK_EQ(Transcoder->Dpll, 0);
    CHECK_EQ(Transcoder->FdiRx.Training, 3);
    CHECK_EQ(Transcoder->FdiRx.Lanes, 1);
    CHECK_EQ(Transcoder->FdiRx.Bpc, 6);

    CHECK_EQ(Dpll->Mode, HdgfxDpllModeLvds);
    CHECK_EQ(Dpll->Reference, 3);
    CHECK_EQ(Dpll->N, 4);
    CHECK_EQ(Dpll->M1, 19);
    CHECK_EQ(Dpll->M2, 10);
    CHECK_EQ(Dpll->P1, 3);
    CHECK_EQ(Dpll->P2, 14);
    CHECK_EQ(Dpll->DotKHz, 75000);

    CHECK_EQ(State.Lvds.Enabled, 1);
    CHECK_EQ(State.Lvds.Detected, 1);
    CHECK_EQ(State.Lvds.Transcoder, 0);
    CHECK_EQ(State.Lvds.Bpc, 6);
    CHECK_EQ(State.Lvds.DualChannel, 0);
    CHECK_EQ(State.Lvds.HSyncActiveLow, 0);
    CHECK_EQ(State.Lvds.VSyncActiveLow, 1);
    CHECK_EQ(State.Crt.Enabled, 0);

    CHECK_EQ(State.PanelPower.On, 1);
    CHECK_EQ(State.Backlight.CpuEnabled, 1);
    CHECK_EQ(State.Backlight.CpuPipe, 0);
    CHECK_EQ(State.Backlight.Duty, 3456);
    CHECK_EQ(State.Backlight.Period, 3484);
    free(Fake);
}

static bool
LoadPanel(
    const char *Directory,
    HDGFX_EDID *Edid)
{
    char Path[1024];
    uint8_t Bytes[HDGFX_EDID_SIZE];
    FILE *File;
    size_t Size;

    snprintf(Path, sizeof(Path), "%s/2570p-lvds-1366x768/edid.bin", Directory);
    File = fopen(Path, "rb");
    if (!File)
    {
        perror(Path);
        exit(2);
    }
    Size = fread(Bytes, 1, sizeof(Bytes), File);
    fclose(File);
    return Size == sizeof(Bytes) && HdgfxEdidParse(Bytes, Edid) && Edid->DtdCount != 0;
}

static HDGFX_MODESET_STATUS
SetNativeMode(
    const HDGFX_MMIO *Mmio,
    const HDGFX_EDID *Edid,
    HDGFX_DISPLAY_STATE *Current,
    HDGFX_DISPLAY_STATE *Target)
{
    HDGFX_LVDS_MODE Mode;
    HDGFX_MODESET_STATUS Status;

    memset(&Mode, 0, sizeof(Mode));
    Mode.Panel = &Edid->Dtds[0];
    Mode.SourceWidth = 1366;
    Mode.SourceHeight = 768;
    Mode.Stride = 5504;
    Mode.Surface = 0x00400000;
    Mode.Scaling = HdgfxScalingAspect;
    Mode.Ssc = true;

    HdgfxIvbReadDisplayState(Mmio, Current);
    Status = HdgfxIlkComputeLvds(Current, &Mode, Target);
    if (Status != HdgfxModesetSuccess)
        return Status;
    return HdgfxIlkCommitLvds(Mmio, Current, Target);
}

static void
TestModeset(
    const char *Directory)
{
    static const struct
    {
        uint32_t Offset;
        uint32_t Value;
    } Golden[] =
    {
        { 0x60100, 0x80044B00 }, { 0xF000C, 0x8C022B50 }, { 0xF0010, 0x00200090 },
        { 0xF0030, 0x7E000000 }, { 0xF0008, 0xC0000000 }, { 0xE1180, 0x80200302 },
        { 0xC7000, 0x00000008 }, { 0xC7204, 0xABCD0003 }, { 0x60000, 0x06290555 },
        { 0x60004, 0x06290555 }, { 0x60008, 0x05A50585 }, { 0x6000C, 0x031502FF },
        { 0x60010, 0x031502FF }, { 0x60014, 0x03060301 }, { 0x6001C, 0x055502FF },
        { 0xE0000, 0x06290555 }, { 0xE000C, 0x031502FF }, { 0x60034, 0x00800000 },
        { 0x60044, 0x00080000 }, { 0x68080, 0x00000000 },
    };
    HDGFX_MMIO Mmio;
    FAKE_IVB *Fake = FakeLoad(Directory, &Mmio);
    HDGFX_DISPLAY_STATE *Current = malloc(sizeof(*Current));
    HDGFX_DISPLAY_STATE *Target = malloc(sizeof(*Target));
    HDGFX_EDID Edid;
    const char *Mismatch;
    uint32_t Writes;
    size_t Index;

    if (!LoadPanel(Directory, &Edid))
    {
        fprintf(stderr, "no usable 2570p EDID\n");
        exit(1);
    }

    CHECK_EQ(SetNativeMode(&Mmio, &Edid, Current, Target), HdgfxModesetSuccess);
    for (Index = 0; Index < sizeof(Golden) / sizeof(Golden[0]); Index++)
    {
        uint32_t Value = FakeRead(Fake, Golden[Index].Offset);

        if (Value != Golden[Index].Value)
        {
            fprintf(stderr, "register 0x%05x = 0x%08x, Mint has 0x%08x\n",
                    Golden[Index].Offset, Value, Golden[Index].Value);
            Failures++;
        }
    }
    CHECK_EQ(Fake->DpllSelectedBeforeEnable, 1);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(TRANS_CHICKEN2, 0)) >> 31, 1);
    CHECK_EQ(*FakeSlot(Fake, HDGFX_REG(FDI_RX_IMR, 0)) & 0x300, 0);

    HdgfxIvbReadDisplayState(&Mmio, Current);
    Mismatch = HdgfxCompareLvdsState(Target, Current);
    if (Mismatch)
    {
        fprintf(stderr, "readback differs in %s\n", Mismatch);
        Failures++;
    }

    Writes = Fake->Writes;
    CHECK_EQ(HdgfxIlkCommitLvds(&Mmio, Current, Target), HdgfxModesetSuccess);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(PIPE_CONF, 0)), PIPE_CONF_ENABLE), 1);
    if (Fake->Writes - Writes > 8)
    {
        fprintf(stderr, "a same-mode commit took a full mode set\n");
        Failures++;
    }

    free(Target);
    free(Current);
    free(Fake);
}

static void
TestTrainingRetry(
    const char *Directory)
{
    HDGFX_MMIO Mmio;
    FAKE_IVB *Fake = FakeLoad(Directory, &Mmio);
    HDGFX_DISPLAY_STATE *Current = malloc(sizeof(*Current));
    HDGFX_DISPLAY_STATE *Target = malloc(sizeof(*Target));
    HDGFX_EDID Edid;

    if (!LoadPanel(Directory, &Edid))
        exit(1);

    Fake->FailBitLockAttempts = 8 + 8 + 2;
    CHECK_EQ(SetNativeMode(&Mmio, &Edid, Current, Target), HdgfxModesetSuccess);
    CHECK_EQ(HDGFX_GET(*FakeSlot(Fake, HDGFX_REG(FDI_TX_CTL, 0)), FDI_TX_SWING_EMPHASIS), 0x3A);
    free(Fake);

    Fake = FakeLoad(Directory, &Mmio);
    Fake->FailBitLockAttempts = 1000;
    CHECK_EQ(SetNativeMode(&Mmio, &Edid, Current, Target), HdgfxModesetFdiBitLockFailed);
    free(Fake);
    free(Target);
    free(Current);
}

int
main(
    int argc,
    char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s FIXTURE_DIR\n", argv[0]);
        return 2;
    }
    TestDevice();
    TestReadout(argv[1]);
    TestModeset(argv[1]);
    TestTrainingRetry(argv[1]);
    if (Failures)
    {
        fprintf(stderr, "%u checks failed\n", Failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
