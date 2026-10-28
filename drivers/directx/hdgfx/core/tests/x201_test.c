/*
 * PROJECT:     LiberNT Intel HD Graphics Driver tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Decode X201 Ironlake register and OpRegion dumps on the host
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REGISTERS 1024

typedef struct _REGISTER_DUMP
{
    uint32_t Count;
    uint32_t Missing;
    uint32_t Offset[MAX_REGISTERS];
    uint32_t Value[MAX_REGISTERS];
} REGISTER_DUMP;

static unsigned Failures;

static const uint8_t X201PanelDtd[18] =
{
    0x12, 0x1B, 0x00, 0x7B, 0x50, 0x20, 0x15, 0x30, 0x30,
    0x20, 0x36, 0x00, 0x05, 0xA3, 0x10, 0x00, 0x00, 0x18
};

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
        fprintf(stderr, "line %d: %s = %llu, expected %llu\n", Line, Name, Actual, Expected);
        Failures++;
    }
}

static uint32_t
DumpRead(
    void *Context,
    uint32_t Offset)
{
    REGISTER_DUMP *Dump = Context;
    uint32_t Index;

    for (Index = 0; Index < Dump->Count; Index++)
    {
        if (Dump->Offset[Index] == Offset)
            return Dump->Value[Index];
    }
    fprintf(stderr, "register 0x%05x is not in the dump\n", Offset);
    Dump->Missing++;
    return 0;
}

static void
LoadRegisterDump(
    const char *Path,
    REGISTER_DUMP *Dump)
{
    char Line[512];
    FILE *File = fopen(Path, "r");

    if (!File)
    {
        perror(Path);
        exit(2);
    }
    memset(Dump, 0, sizeof(*Dump));
    while (fgets(Line, sizeof(Line), File))
    {
        const char *Open = strstr(Line, "(0x");
        unsigned Offset;
        unsigned Value;

        if (Open && sscanf(Open, "(0x%x): 0x%x", &Offset, &Value) == 2 && Dump->Count < MAX_REGISTERS)
        {
            Dump->Offset[Dump->Count] = Offset;
            Dump->Value[Dump->Count] = Value;
            Dump->Count++;
        }
    }
    fclose(File);
}

static uint8_t *
LoadFile(
    const char *Path,
    size_t *Size)
{
    FILE *File = fopen(Path, "rb");
    uint8_t *Data;
    long Length;

    if (!File)
        return NULL;
    fseek(File, 0, SEEK_END);
    Length = ftell(File);
    fseek(File, 0, SEEK_SET);
    Data = malloc(Length > 0 ? (size_t)Length : 1);
    if (Data && fread(Data, 1, (size_t)Length, File) != (size_t)Length)
    {
        free(Data);
        Data = NULL;
    }
    fclose(File);
    *Size = (size_t)Length;
    return Data;
}

static void
CheckTimings(
    const HDGFX_TIMINGS *Timings)
{
    CHECK_EQ(Timings->HActive, 1280);
    CHECK_EQ(Timings->HSyncStart, 1328);
    CHECK_EQ(Timings->HSyncEnd, 1360);
    CHECK_EQ(Timings->HTotal, 1403);
    CHECK_EQ(Timings->VActive, 800);
    CHECK_EQ(Timings->VSyncStart, 803);
    CHECK_EQ(Timings->VSyncEnd, 809);
    CHECK_EQ(Timings->VTotal, 821);
}

static void
TestNativeLvds(
    const char *Directory)
{
    char Path[1024];
    REGISTER_DUMP *Dump = malloc(sizeof(*Dump));
    HDGFX_MMIO Mmio = { DumpRead, Dump, NULL, NULL };
    HDGFX_DISPLAY_STATE State;
    const HDGFX_GENERATION *Generation;
    const HDGFX_PIPE *Pipe;
    const HDGFX_TRANSCODER *Transcoder;
    const HDGFX_DPLL *Dpll;
    unsigned long long FdiDotKHz;

    snprintf(Path, sizeof(Path), "%s/x201-lvds-1280x800/intel_reg-dump.txt", Directory);
    LoadRegisterDump(Path, Dump);

    Generation = HdgfxFindGeneration(0x8086, 0x0046);
    if (!Generation)
    {
        fprintf(stderr, "8086:0046 has no generation\n");
        exit(1);
    }
    CHECK_EQ(Generation->Gen, 5);
    Generation->ReadDisplayState(&Mmio, &State);
    CHECK_EQ(Dump->Missing, 0);

    Pipe = &State.Pipes[0];
    CHECK_EQ(Pipe->Enabled, 1);
    CHECK_EQ(Pipe->Active, 1);
    CHECK_EQ(Pipe->Bpc * 3, 18);
    CHECK_EQ(Pipe->Dither, 1);
    CHECK_EQ(Pipe->InterlaceMode, 0);
    CheckTimings(&Pipe->Timings);
    CHECK_EQ(Pipe->Timings.HBlankStart, 1280);
    CHECK_EQ(Pipe->Timings.HBlankEnd, 1403);
    CHECK_EQ(Pipe->Timings.VBlankStart, 800);
    CHECK_EQ(Pipe->Timings.VBlankEnd, 821);
    CHECK_EQ(Pipe->SourceWidth, 1280);
    CHECK_EQ(Pipe->SourceHeight, 800);
    CHECK_EQ(Pipe->PanelFitter.Enabled, 0);
    CHECK_EQ(Pipe->Fdi.TuSize, 64);
    CHECK_EQ(Pipe->Fdi.DataM, 4843457);
    CHECK_EQ(Pipe->Fdi.DataN, 8388608);
    CHECK_EQ(Pipe->Fdi.LinkM, 134540);
    CHECK_EQ(Pipe->Fdi.LinkN, 524288);

    CHECK_EQ(Pipe->Primary.Enabled, 1);
    CHECK_EQ(Pipe->Primary.Format, 6);
    CHECK_EQ(Pipe->Primary.XTiled, 1);
    CHECK_EQ(Pipe->Primary.Stride, 1280 * 4);
    CHECK_EQ(Pipe->Primary.Surface, 0x02380000);
    CHECK_EQ(Pipe->Primary.TileX, 0);
    CHECK_EQ(Pipe->Primary.TileY, 0);

    CHECK_EQ(State.Pipes[1].Enabled, 0);
    CHECK_EQ(State.Pipes[1].Primary.Enabled, 0);

    Transcoder = &State.Transcoders[0];
    CHECK_EQ(Transcoder->Enabled, 1);
    CHECK_EQ(Transcoder->Active, 1);
    CHECK_EQ(Transcoder->Dpll, 0);
    CheckTimings(&Transcoder->Timings);
    CHECK_EQ(memcmp(&Transcoder->Timings, &Pipe->Timings, sizeof(Pipe->Timings)), 0);
    CHECK_EQ(Transcoder->FdiRx.Enabled, 1);
    CHECK_EQ(Transcoder->FdiRx.PllEnabled, 1);
    CHECK_EQ(Transcoder->FdiRx.Training, 3);
    CHECK_EQ(Transcoder->FdiRx.Lanes, 1);
    CHECK_EQ(Transcoder->FdiRx.Bpc, Pipe->Bpc);
    CHECK_EQ(State.Transcoders[1].Enabled, 0);

    Dpll = &State.Dplls[0];
    CHECK_EQ(Dpll->Enabled, 1);
    CHECK_EQ(Dpll->Mode, HdgfxDpllModeLvds);
    CHECK_EQ(Dpll->Reference, 3);
    CHECK_EQ(Dpll->N, 4);
    CHECK_EQ(Dpll->M1, 18);
    CHECK_EQ(Dpll->M2, 7);
    CHECK_EQ(Dpll->P1, 3);
    CHECK_EQ(Dpll->P2, 14);
    CHECK_EQ(Dpll->VcoKHz, 2910000);
    CHECK_EQ(Dpll->DotKHz, 69286);
    CHECK_EQ(HdgfxRefreshMilliHz(&Pipe->Timings, Dpll->DotKHz), 60151);
    CHECK_EQ(State.Dplls[1].Enabled, 0);

    CHECK_EQ(State.FdiPllVcoKHz, 2700000);
    FdiDotKHz = ((unsigned long long)Pipe->Fdi.LinkM * (State.FdiPllVcoKHz / 10) + Pipe->Fdi.LinkN / 2) /
                Pipe->Fdi.LinkN;
    CHECK_EQ(FdiDotKHz, 69286);
    CHECK_EQ((unsigned long long)Pipe->Fdi.DataM * Transcoder->FdiRx.Lanes * (State.FdiPllVcoKHz / 10) * 8 /
             Pipe->Fdi.DataN / (Pipe->Bpc * 3), 69286);

    CHECK_EQ(State.Lvds.Enabled, 1);
    CHECK_EQ(State.Lvds.Detected, 1);
    CHECK_EQ(State.Lvds.Transcoder, 0);
    CHECK_EQ(State.Lvds.DualChannel, 0);
    CHECK_EQ(State.Lvds.Bpc, 6);
    CHECK_EQ(State.Lvds.HSyncActiveLow, 1);
    CHECK_EQ(State.Lvds.VSyncActiveLow, 1);

    CHECK_EQ(State.PanelPower.On, 1);
    CHECK_EQ(State.PanelPower.Ready, 1);
    CHECK_EQ(State.PanelPower.Sequence, 0);
    CHECK_EQ(State.PanelPower.TargetOn, 1);
    CHECK_EQ(State.PanelPower.BacklightEnabled, 0);
    CHECK_EQ(State.PanelPower.PowerDownOnReset, 1);
    CHECK_EQ(State.PanelPower.PowerUpUs, 25000);
    CHECK_EQ(State.PanelPower.BacklightOnUs, 250000);
    CHECK_EQ(State.PanelPower.PowerDownUs, 25000);
    CHECK_EQ(State.PanelPower.BacklightOffUs, 250000);
    CHECK_EQ(State.PanelPower.PowerCycleUs, 300000);
    CHECK_EQ(State.PanelPower.ReferenceDivider, 0x1869);

    CHECK_EQ(State.RawClockKHz, 125000);
    CHECK_EQ(State.Backlight.CpuEnabled, 1);
    CHECK_EQ(State.Backlight.CpuPipe, 0);
    CHECK_EQ(State.Backlight.Duty, 1421);
    CHECK_EQ(State.Backlight.PchEnabled, 1);
    CHECK_EQ(State.Backlight.ActiveLow, 0);
    CHECK_EQ(State.Backlight.Period, 1562);
    CHECK_EQ(State.Backlight.FrequencyHz, 625);

    free(Dump);
}

static void
CheckPanelVbt(
    const uint8_t *OpRegion,
    size_t Size,
    const char *Signature,
    uint32_t BdbVersion,
    uint32_t BacklightMinimum)
{
    const uint8_t *Vbt;
    size_t VbtSize;
    HDGFX_VBT Result;

    CHECK_EQ(HdgfxOpRegionFindVbt(OpRegion, Size, &Vbt, &VbtSize), 1);
    CHECK_EQ(HdgfxVbtParse(Vbt, VbtSize, &Result), 1);
    CHECK_EQ(strncmp(Result.Signature, Signature, strlen(Signature)), 0);
    CHECK_EQ(Result.BdbVersion, BdbVersion);
    CHECK_EQ(Result.LvdsSscEnabled, 1);
    CHECK_EQ(Result.Panel.PanelType, 1);
    CHECK_EQ(Result.Panel.Present, 1);
    CHECK_EQ(Result.Panel.DotKHz, 69300);
    CheckTimings(&Result.Panel.Timings);
    CHECK_EQ(Result.Panel.HSyncPositive, 0);
    CHECK_EQ(Result.Panel.VSyncPositive, 0);
    CHECK_EQ(Result.Panel.WidthMm, 261);
    CHECK_EQ(Result.Panel.HeightMm, 163);
    CHECK_EQ(Result.Panel.BacklightPresent, 1);
    CHECK_EQ(Result.Panel.BacklightType, 2);
    CHECK_EQ(Result.Panel.BacklightActiveLow, 0);
    CHECK_EQ(Result.Panel.BacklightPwmHz, 625);
    CHECK_EQ(Result.Panel.BacklightMinimum, BacklightMinimum);
}

static size_t
PutBlock(
    uint8_t *Bdb,
    size_t Offset,
    uint8_t Id,
    const uint8_t *Data,
    size_t Size)
{
    Bdb[Offset] = Id;
    Bdb[Offset + 1] = (uint8_t)Size;
    Bdb[Offset + 2] = (uint8_t)(Size >> 8);
    memcpy(Bdb + Offset + 3, Data, Size);
    return Offset + 3 + Size;
}

static void
TestSyntheticVbt(void)
{
    static const uint8_t General[] = { 0x00, 0x02 };
    static const uint8_t Options[] = { 0x01 };
    uint8_t *OpRegion = calloc(1, 0x2000);
    uint8_t *Vbt = OpRegion + 0x400;
    uint8_t *Bdb = Vbt + 48;
    uint8_t Pointers[1 + 16 * 9] = { 16 };
    uint8_t Data[64] = { 0 };
    uint8_t Backlight[1 + 16 * 5] = { 5 };
    size_t Offset = 22;
    size_t DataOffset;

    CHECK_EQ(HdgfxFindGeneration(0x8086, 0x0046)->GseInterrupt, 1u << 18);
    memcpy(OpRegion, "IntelGraphicsMem", 16);
    OpRegion[0x10] = 8;
    OpRegion[0x58] = 0x08;
    memcpy(Vbt, "$VBT LIBERNT-TEST", 17);
    Vbt[28] = 48;
    memcpy(Bdb, "BIOS_DATA_BLOCK ", 16);
    Bdb[16] = 163;
    Bdb[18] = 22;

    Offset = PutBlock(Bdb, Offset, 1, General, sizeof(General));
    Offset = PutBlock(Bdb, Offset, 40, Options, sizeof(Options));
    DataOffset = Offset + 3 + sizeof(Pointers) + 3;
    Pointers[1 + 9 + 3] = (uint8_t)(DataOffset + 7);
    Pointers[1 + 9 + 4] = (uint8_t)((DataOffset + 7) >> 8);
    Offset = PutBlock(Bdb, Offset, 41, Pointers, sizeof(Pointers));
    memcpy(Data + 7, X201PanelDtd, sizeof(X201PanelDtd));
    Offset = PutBlock(Bdb, Offset, 42, Data, sizeof(Data));
    Backlight[1 + 5] = 0x02;
    Backlight[1 + 5 + 1] = 625 & 0xFF;
    Backlight[1 + 5 + 2] = 625 >> 8;
    Backlight[1 + 5 + 3] = 4;
    Offset = PutBlock(Bdb, Offset, 43, Backlight, sizeof(Backlight));
    Bdb[20] = (uint8_t)Offset;
    Bdb[21] = (uint8_t)(Offset >> 8);
    Vbt[24] = (uint8_t)(48 + Offset);
    Vbt[25] = (uint8_t)((48 + Offset) >> 8);

    CheckPanelVbt(OpRegion, 0x2000, "$VBT LIBERNT-TEST", 163, 4);

    OpRegion[0x58] = 0;
    CHECK_EQ(HdgfxOpRegionFindVbt(OpRegion, 0x2000, (const uint8_t **)&Vbt, &Offset), 0);
    free(OpRegion);
}

typedef struct _FAKE_GMBUS
{
    const uint8_t *Data;
    uint32_t Size;
    uint32_t Pins;
    uint32_t Gmbus0;
    uint32_t Count;
    uint32_t Position;
    uint32_t StalledUs;
    bool InUse;
    bool HeldElsewhere;
    bool HardwareReady;
    bool Active;
    bool Nak;
    bool Cleared;
} FAKE_GMBUS;

static uint32_t
FakeGmbusRead(
    void *Context,
    uint32_t Offset)
{
    FAKE_GMBUS *Bus = Context;
    uint32_t Value = 0;
    uint32_t Byte;

    if (Offset == HDGFX_REG(GMBUS2, 0))
    {
        Value = HDGFX_SET(GMBUS2_INUSE, Bus->InUse || Bus->HeldElsewhere) |
                HDGFX_SET(GMBUS2_HW_RDY, Bus->HardwareReady) |
                HDGFX_SET(GMBUS2_NAK, Bus->Nak) |
                HDGFX_SET(GMBUS2_ACTIVE, Bus->Active);
        Bus->InUse = true;
        return Value;
    }
    if (Offset == HDGFX_REG(GMBUS3, 0))
    {
        CHECK_EQ(Bus->HardwareReady, 1);
        for (Byte = 0; Byte < 4 && Bus->Position < Bus->Count; Byte++)
            Value |= (uint32_t)Bus->Data[Bus->Position++] << (Byte * 8);
        Bus->Active = Bus->Position < Bus->Count;
        Bus->HardwareReady = true;
        return Value;
    }
    fprintf(stderr, "unexpected GMBUS read 0x%05x\n", Offset);
    Failures++;
    return 0;
}

static void
FakeGmbusWrite(
    void *Context,
    uint32_t Offset,
    uint32_t Value)
{
    FAKE_GMBUS *Bus = Context;

    if (Offset == HDGFX_REG(GMBUS0, 0))
    {
        Bus->Gmbus0 = Value;
    }
    else if (Offset == HDGFX_REG(GMBUS1, 0))
    {
        if (HDGFX_GET(Value, GMBUS1_SW_CLR_INT))
        {
            Bus->Nak = false;
            Bus->Active = false;
            Bus->HardwareReady = true;
            Bus->Cleared = true;
            return;
        }
        if (!HDGFX_GET(Value, GMBUS1_SW_RDY))
            return;
        CHECK_EQ(Bus->Cleared, 1);
        CHECK_EQ(HDGFX_GET(Bus->Gmbus0, GMBUS0_RATE), 0);
        CHECK_EQ(HDGFX_GET(Value, GMBUS1_ENABLE_TIMEOUT), 1);
        CHECK_EQ(HDGFX_GET(Value, GMBUS1_CYCLE), 7);
        CHECK_EQ(HDGFX_GET(Value, GMBUS1_READ), 1);
        if (HDGFX_GET(Bus->Gmbus0, GMBUS0_PINS) != Bus->Pins ||
            HDGFX_GET(Value, GMBUS1_ADDRESS) != HDGFX_DDC_ADDRESS)
        {
            Bus->Nak = true;
            Bus->Active = false;
            return;
        }
        Bus->Position = HDGFX_GET(Value, GMBUS1_INDEX);
        Bus->Count = Bus->Position + HDGFX_GET(Value, GMBUS1_BYTE_COUNT);
        CHECK_EQ(Bus->Count <= Bus->Size, 1);
        Bus->Active = true;
        Bus->HardwareReady = true;
    }
    else if (Offset == HDGFX_REG(GMBUS2, 0))
    {
        if (HDGFX_GET(Value, GMBUS2_INUSE))
            Bus->InUse = false;
    }
    else if (Offset == HDGFX_REG(GMBUS5, 0))
    {
        CHECK_EQ(Value, 0);
    }
    else
    {
        fprintf(stderr, "unexpected GMBUS write 0x%05x\n", Offset);
        Failures++;
    }
}

static void
FakeGmbusStall(
    void *Context,
    uint32_t Microseconds)
{
    ((FAKE_GMBUS *)Context)->StalledUs += Microseconds;
}

static void
TestGmbusEdid(
    const char *FixtureDir)
{
    const HDGFX_GENERATION *Generation = HdgfxFindGeneration(0x8086, 0x0046);
    uint8_t Buffer[HDGFX_EDID_SIZE];
    FAKE_GMBUS Bus;
    HDGFX_MMIO Mmio = { FakeGmbusRead, &Bus, FakeGmbusWrite, FakeGmbusStall };
    HDGFX_EDID Parsed;
    char Path[1024];
    uint8_t *Edid;
    size_t Size;

    snprintf(Path, sizeof(Path), "%s/x201-lvds-1280x800/edid.bin", FixtureDir);
    Edid = LoadFile(Path, &Size);
    if (!Edid || Size != HDGFX_EDID_SIZE)
    {
        fprintf(stderr, "%s: missing or not %u bytes\n", Path, HDGFX_EDID_SIZE);
        Failures++;
        free(Edid);
        return;
    }
    memset(&Bus, 0, sizeof(Bus));
    Bus.Data = Edid;
    Bus.Size = HDGFX_EDID_SIZE;
    Bus.Pins = 3;
    Bus.HardwareReady = true;
    CHECK_EQ(Generation->PanelDdcPins, 3);
    CHECK_EQ(HdgfxGmbusRead(&Mmio, Generation->PanelDdcPins, HDGFX_DDC_ADDRESS, 0, Buffer, sizeof(Buffer)),
             HdgfxGmbusSuccess);
    CHECK_EQ(memcmp(Buffer, Edid, HDGFX_EDID_SIZE), 0);
    CHECK_EQ(Bus.Gmbus0, 0);
    CHECK_EQ(Bus.InUse, 0);

    CHECK_EQ(HdgfxGmbusRead(&Mmio, Generation->PanelDdcPins, HDGFX_DDC_ADDRESS, 126, Buffer, 2),
             HdgfxGmbusSuccess);
    CHECK_EQ(Buffer[0], Edid[126]);
    CHECK_EQ(Buffer[1], Edid[127]);

    CHECK_EQ(HdgfxGmbusRead(&Mmio, 2, HDGFX_DDC_ADDRESS, 0, Buffer, sizeof(Buffer)),
             HdgfxGmbusNoAcknowledge);
    CHECK_EQ(Bus.Nak, 0);
    CHECK_EQ(Bus.Gmbus0, 0);
    CHECK_EQ(Bus.InUse, 0);

    Bus.HeldElsewhere = true;
    Bus.Cleared = false;
    CHECK_EQ(HdgfxGmbusRead(&Mmio, Generation->PanelDdcPins, HDGFX_DDC_ADDRESS, 0, Buffer, sizeof(Buffer)),
             HdgfxGmbusBusy);
    CHECK_EQ(Bus.Cleared, 0);
    CHECK_EQ(Bus.StalledUs >= 25000 && Bus.StalledUs <= 26000, 1);
    Bus.HeldElsewhere = false;

    CHECK_EQ(HdgfxGmbusRead(&Mmio, Generation->PanelDdcPins, HDGFX_DDC_ADDRESS, 0, Buffer, 0),
             HdgfxGmbusInvalidParameter);

    CHECK_EQ(HdgfxEdidParse(Edid, &Parsed), 1);
    CHECK_EQ(strcmp(Parsed.Manufacturer, "LEN"), 0);
    CHECK_EQ(Parsed.ProductCode, 0x4011);
    CHECK_EQ(Parsed.SerialNumber, 0);
    CHECK_EQ(Parsed.Version, 1);
    CHECK_EQ(Parsed.Revision, 3);
    CHECK_EQ(Parsed.WidthCm, 26);
    CHECK_EQ(Parsed.HeightCm, 16);
    CHECK_EQ(Parsed.Extensions, 0);
    CHECK_EQ(Parsed.DtdCount, 2);
    CHECK_EQ(Parsed.Dtds[0].DotKHz, 69300);
    CheckTimings(&Parsed.Dtds[0].Timings);
    CHECK_EQ(Parsed.Dtds[0].HSyncPositive, 0);
    CHECK_EQ(Parsed.Dtds[0].WidthMm, 261);
    CHECK_EQ(Parsed.Dtds[1].DotKHz, 57590);
    CheckTimings(&Parsed.Dtds[1].Timings);

    Edid[20] ^= 1;
    CHECK_EQ(HdgfxEdidParse(Edid, &Parsed), 0);
    free(Edid);
}

static void
TestFirmwareVbt(
    const char *OpRegionPath)
{
    size_t Size;
    uint8_t *OpRegion = LoadFile(OpRegionPath, &Size);

    if (!OpRegion)
    {
        printf("SKIP firmware vbt: %s not found\n", OpRegionPath);
        return;
    }
    CheckPanelVbt(OpRegion, Size, "$VBT IRONLAKE-MOBILE", 163, 4);
    free(OpRegion);
}

static uint32_t
MemoryRead(
    void *Context,
    uint32_t Offset)
{
    uint32_t Value;

    memcpy(&Value, (uint8_t *)Context + Offset, sizeof(Value));
    return Value;
}

static void
MemoryWrite(
    void *Context,
    uint32_t Offset,
    uint32_t Value)
{
    memcpy((uint8_t *)Context + Offset, &Value, sizeof(Value));
}

static uint32_t AsleLevel;
static bool AsleResult;

static bool
RecordBacklight(
    void *Context,
    uint32_t Level)
{
    (void)Context;
    AsleLevel = Level;
    return AsleResult;
}

static void
TestOpRegionAsle(void)
{
    uint8_t *OpRegion = calloc(1, 0x2000);
    HDGFX_MMIO Io = { MemoryRead, OpRegion, MemoryWrite, NULL };
    uint32_t Events;

    CHECK_EQ(HdgfxFindGeneration(0x8086, 0x0046)->GseInterrupt, 1u << 18);
    memcpy(OpRegion, "IntelGraphicsMem", 16);
    MemoryWrite(OpRegion, 0x58, 0x1B);
    CHECK_EQ(HdgfxOpRegionDriverReady(&Io, 0x400, 91), 0);
    CHECK_EQ(MemoryRead(OpRegion, 0x100), 0);
    CHECK_EQ(MemoryRead(OpRegion, 0x120), 0);

    MemoryWrite(OpRegion, 0x58, 0x1F);
    MemoryWrite(OpRegion, 0x124, 0x100);
    MemoryWrite(OpRegion, 0x164, 0x100);
    CHECK_EQ(HdgfxOpRegionDriverReady(&Io, 0x400, 91), 1);
    CHECK_EQ(MemoryRead(OpRegion, 0x120), 0x400);
    CHECK_EQ(MemoryRead(OpRegion, 0x124), 0);
    CHECK_EQ(MemoryRead(OpRegion, 0x160), 0x400);
    CHECK_EQ(MemoryRead(OpRegion, 0x164), 0);
    CHECK_EQ(MemoryRead(OpRegion, 0x318), 0x8000005B);
    CHECK_EQ(MemoryRead(OpRegion, 0x308), 0x2);
    CHECK_EQ(MemoryRead(OpRegion, 0x300), 1);
    CHECK_EQ(MemoryRead(OpRegion, 0x100), 1);

    AsleResult = true;
    CHECK_EQ(HdgfxOpRegionServiceAsle(&Io, RecordBacklight, NULL), 0);

    AsleLevel = 0;
    MemoryWrite(OpRegion, 0x310, 0x80000065);
    MemoryWrite(OpRegion, 0x304, 0x0A);
    CHECK_EQ(HdgfxOpRegionServiceAsle(&Io, RecordBacklight, NULL), 1);
    CHECK_EQ(AsleLevel, 0x65);
    CHECK_EQ(MemoryRead(OpRegion, 0x304), 1u << 16);
    CHECK_EQ(MemoryRead(OpRegion, 0x318), 0x80000028);

    AsleLevel = 0;
    MemoryWrite(OpRegion, 0x310, 0xE8);
    MemoryWrite(OpRegion, 0x304, 0x02);
    CHECK_EQ(HdgfxOpRegionServiceAsle(&Io, RecordBacklight, NULL), 1);
    CHECK_EQ(AsleLevel, 0);
    CHECK_EQ(MemoryRead(OpRegion, 0x304), 1u << 12);
    CHECK_EQ(MemoryRead(OpRegion, 0x318), 0x80000028);

    MemoryWrite(OpRegion, 0x310, 0x80000100);
    MemoryWrite(OpRegion, 0x304, 0x02);
    CHECK_EQ(HdgfxOpRegionServiceAsle(&Io, RecordBacklight, NULL), 1);
    CHECK_EQ(AsleLevel, 0);
    CHECK_EQ(MemoryRead(OpRegion, 0x304), 1u << 12);

    AsleResult = false;
    MemoryWrite(OpRegion, 0x310, 0x800000FF);
    MemoryWrite(OpRegion, 0x304, 0x02);
    CHECK_EQ(HdgfxOpRegionServiceAsle(&Io, RecordBacklight, NULL), 1);
    CHECK_EQ(AsleLevel, 0xFF);
    CHECK_EQ(MemoryRead(OpRegion, 0x304), 1u << 12);
    CHECK_EQ(MemoryRead(OpRegion, 0x318), 0x80000028);

    CHECK_EQ(HdgfxOpRegionBeginNotification(&Io, &Events), 0);
    CHECK_EQ(Events, 0);

    MemoryWrite(OpRegion, 0x108, 0x04);
    MemoryWrite(OpRegion, 0x104, 0x03);
    CHECK_EQ(HdgfxOpRegionBeginNotification(&Io, &Events), 1);
    CHECK_EQ(Events, HDGFX_OPREGION_EVENT_DOCK);
    CHECK_EQ(MemoryRead(OpRegion, 0x104), 2);
    HdgfxOpRegionEndNotification(&Io, true);
    CHECK_EQ(MemoryRead(OpRegion, 0x104), 0);
    CHECK_EQ(HdgfxOpRegionBeginNotification(&Io, &Events), 0);

    MemoryWrite(OpRegion, 0x108, 0xFFFFFFF9);
    MemoryWrite(OpRegion, 0x104, 0x03);
    CHECK_EQ(HdgfxOpRegionBeginNotification(&Io, &Events), 1);
    CHECK_EQ(Events, HDGFX_OPREGION_EVENT_HOTKEY);
    HdgfxOpRegionEndNotification(&Io, false);
    CHECK_EQ(MemoryRead(OpRegion, 0x104), 1);

    HdgfxOpRegionSetBrightness(&Io, 100);
    CHECK_EQ(MemoryRead(OpRegion, 0x318), 0x80000064);
    HdgfxOpRegionDriverNotReady(&Io);
    CHECK_EQ(MemoryRead(OpRegion, 0x100), 0);
    CHECK_EQ(MemoryRead(OpRegion, 0x300), 0);
    free(OpRegion);
}

int
main(
    int argc,
    char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s FIXTURE_DIR [OPREGION]\n", argv[0]);
        return 2;
    }
    TestNativeLvds(argv[1]);
    TestSyntheticVbt();
    TestGmbusEdid(argv[1]);
    TestOpRegionAsle();
    if (argc > 2)
        TestFirmwareVbt(argv[2]);
    if (Failures)
    {
        fprintf(stderr, "%u checks failed\n", Failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
