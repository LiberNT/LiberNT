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
    HDGFX_MMIO Mmio = { DumpRead, Dump, NULL, NULL, NULL };
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
    HDGFX_MMIO Mmio = { FakeGmbusRead, &Bus, FakeGmbusWrite, FakeGmbusStall, NULL };
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
    HDGFX_MMIO Io = { MemoryRead, OpRegion, MemoryWrite, NULL, NULL };
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

typedef struct _FAKE_ILK
{
    REGISTER_DUMP Registers;
    uint64_t NowUs;
    uint64_t PipeReadyUs[2];
    uint64_t TranscoderReadyUs[2];
    uint64_t RxPllUs;
    uint64_t PanelDoneUs;
    uint64_t CycleDoneUs;
    bool PanelOn;
    bool PanelPoweringUp;
    bool PanelPoweringDown;
    bool FdiLocks;
    bool PanelStuck;
    bool CrtAttached;
    uint64_t CrtDetectDoneUs;
    unsigned Violations;
    unsigned PipeEnables;
} FAKE_ILK;

#define FAKE_LINE_US 20

static uint32_t *
FakeSlot(
    FAKE_ILK *Fake,
    uint32_t Offset)
{
    REGISTER_DUMP *Dump = &Fake->Registers;
    uint32_t Index;

    for (Index = 0; Index < Dump->Count; Index++)
    {
        if (Dump->Offset[Index] == Offset)
            return &Dump->Value[Index];
    }
    if (Dump->Count == MAX_REGISTERS)
    {
        fprintf(stderr, "fake register table full\n");
        exit(2);
    }
    Dump->Offset[Dump->Count] = Offset;
    Dump->Value[Dump->Count] = 0;
    return &Dump->Value[Dump->Count++];
}

static uint32_t
FakeGet(
    FAKE_ILK *Fake,
    uint32_t Offset)
{
    return *FakeSlot(Fake, Offset);
}

static void
FakeViolation(
    FAKE_ILK *Fake,
    const char *What)
{
    fprintf(stderr, "sequence violation at %llu us: %s\n", (unsigned long long)Fake->NowUs, What);
    Fake->Violations++;
}

static uint32_t
FakeDelayUs(
    FAKE_ILK *Fake,
    uint32_t Offset,
    bool High)
{
    uint32_t Value = FakeGet(Fake, Offset);

    return (High ? HDGFX_GET(Value, PP_DELAY_HIGH) : HDGFX_GET(Value, PP_DELAY_LOW)) * 100;
}

static bool
FakePanelAssetsReady(
    FAKE_ILK *Fake)
{
    uint32_t Lvds = FakeGet(Fake, HDGFX_REG(PCH_LVDS, 0));
    uint32_t Index = HDGFX_GET(Lvds, LVDS_TRANSCODER);

    return HDGFX_GET(Lvds, LVDS_ENABLE) &&
           HDGFX_GET(FakeGet(Fake, HDGFX_REG(PCH_DPLL, Index)), DPLL_VCO_ENABLE) &&
           HDGFX_GET(FakeGet(Fake, HDGFX_REG(TRANS_CONF, Index)), TRANS_CONF_STATE);
}

static void
FakeUpdatePanel(
    FAKE_ILK *Fake)
{
    bool Target = HDGFX_GET(FakeGet(Fake, HDGFX_REG(PCH_PP_CONTROL, 0)), PP_CONTROL_TARGET_ON) != 0;
    uint32_t Status;

    if (Fake->PanelStuck)
        return;
    if ((Fake->PanelPoweringUp || Fake->PanelPoweringDown) && Fake->NowUs >= Fake->PanelDoneUs)
    {
        Fake->PanelOn = Fake->PanelPoweringUp;
        if (Fake->PanelPoweringDown)
            Fake->CycleDoneUs = Fake->NowUs + HDGFX_GET(FakeGet(Fake, HDGFX_REG(PCH_PP_DIVISOR, 0)),
                                                        PP_DIVISOR_CYCLE) * 100000;
        Fake->PanelPoweringUp = Fake->PanelPoweringDown = false;
    }
    if (!Fake->PanelPoweringUp && !Fake->PanelPoweringDown)
    {
        if (Target && !Fake->PanelOn && FakePanelAssetsReady(Fake))
        {
            uint64_t Start = Fake->NowUs > Fake->CycleDoneUs ? Fake->NowUs : Fake->CycleDoneUs;

            Fake->PanelPoweringUp = true;
            Fake->PanelDoneUs = Start + FakeDelayUs(Fake, HDGFX_REG(PCH_PP_ON_DELAYS, 0), true) +
                                FakeDelayUs(Fake, HDGFX_REG(PCH_PP_ON_DELAYS, 0), false);
        }
        else if (!Target && Fake->PanelOn)
        {
            Fake->PanelPoweringDown = true;
            Fake->PanelDoneUs = Fake->NowUs + FakeDelayUs(Fake, HDGFX_REG(PCH_PP_OFF_DELAYS, 0), true) +
                                FakeDelayUs(Fake, HDGFX_REG(PCH_PP_OFF_DELAYS, 0), false);
        }
    }

    Status = HDGFX_SET(PP_STATUS_ON, Fake->PanelOn || Fake->PanelPoweringDown) |
             HDGFX_SET(PP_STATUS_READY, FakePanelAssetsReady(Fake)) |
             HDGFX_SET(PP_STATUS_SEQUENCE, Fake->PanelPoweringUp ? 1 : Fake->PanelPoweringDown ? 2 : 0) |
             HDGFX_SET(PP_STATUS_CYCLE_ACTIVE, Fake->NowUs < Fake->CycleDoneUs);
    *FakeSlot(Fake, HDGFX_REG(PCH_PP_STATUS, 0)) = Status;
}

static bool
FakePanelLocked(
    FAKE_ILK *Fake)
{
    FakeUpdatePanel(Fake);
    return HDGFX_GET(FakeGet(Fake, HDGFX_REG(PCH_PP_STATUS, 0)), PP_STATUS_ON) &&
           HDGFX_GET(FakeGet(Fake, HDGFX_REG(PCH_PP_CONTROL, 0)), PP_CONTROL_KEY) != 0xABCD;
}

static void
FakeUpdateStates(
    FAKE_ILK *Fake)
{
    uint32_t *Adpa;
    uint32_t Index;

    for (Index = 0; Index < 2; Index++)
    {
        uint32_t *Pipe = FakeSlot(Fake, HDGFX_REG(PIPE_CONF, Index));
        uint32_t *Transcoder = FakeSlot(Fake, HDGFX_REG(TRANS_CONF, Index));

        if (Fake->NowUs >= Fake->PipeReadyUs[Index])
            *Pipe = HDGFX_REPLACE(*Pipe, PIPE_CONF_STATE,
                                  HDGFX_GET(*Pipe, PIPE_CONF_ENABLE) &&
                                  HDGFX_GET(FakeGet(Fake, HDGFX_REG(PCH_DPLL, Index)), DPLL_VCO_ENABLE));
        if (Fake->NowUs >= Fake->TranscoderReadyUs[Index])
            *Transcoder = HDGFX_REPLACE(*Transcoder, TRANS_CONF_STATE, HDGFX_GET(*Transcoder, TRANS_CONF_ENABLE));
    }
    Adpa = FakeSlot(Fake, HDGFX_REG(PCH_ADPA, 0));
    if (HDGFX_GET(*Adpa, ADPA_FORCE_TRIGGER) && Fake->NowUs >= Fake->CrtDetectDoneUs)
    {
        *Adpa = HDGFX_REPLACE(*Adpa, ADPA_FORCE_TRIGGER, 0);
        *Adpa = HDGFX_REPLACE(*Adpa, ADPA_HOTPLUG_STATUS, Fake->CrtAttached ? 3 : 0);
    }
    FakeUpdatePanel(Fake);
}

static uint32_t
FakeScanLine(
    FAKE_ILK *Fake,
    uint32_t Index)
{
    uint32_t VTotal = HDGFX_GET(FakeGet(Fake, HDGFX_REG(PIPE_VTOTAL, Index)), TIMING_HIGH) + 1;

    if (!HDGFX_GET(FakeGet(Fake, HDGFX_REG(PIPE_CONF, Index)), PIPE_CONF_STATE))
        return 0;
    return (uint32_t)((Fake->NowUs / FAKE_LINE_US) % VTotal);
}

static bool
FakePipeRunning(
    FAKE_ILK *Fake,
    uint32_t Index)
{
    return HDGFX_GET(FakeGet(Fake, HDGFX_REG(PIPE_CONF, Index)), PIPE_CONF_STATE) != 0;
}

static uint32_t
FakeIlkRead(
    void *Context,
    uint32_t Offset)
{
    FAKE_ILK *Fake = Context;
    uint32_t Index;

    FakeUpdateStates(Fake);
    for (Index = 0; Index < 2; Index++)
    {
        if (Offset == HDGFX_REG(PIPE_DSL, Index))
            return FakeScanLine(Fake, Index);
        if (Offset == HDGFX_REG(FDI_RX_ISR, Index))
        {
            uint32_t Tx = FakeGet(Fake, HDGFX_REG(FDI_TX_CTL, Index));
            uint32_t Rx = FakeGet(Fake, HDGFX_REG(FDI_RX_CTL, Index));
            bool Linked = Fake->FdiLocks && HDGFX_GET(Tx, FDI_TX_ENABLE) && HDGFX_GET(Rx, FDI_RX_ENABLE) &&
                          HDGFX_GET(Rx, FDI_RX_PLL_ENABLE) && HDGFX_GET(Rx, FDI_RX_PCDCLK) &&
                          HDGFX_GET(Tx, FDI_TX_WIDTH) == HDGFX_GET(Rx, FDI_RX_WIDTH) &&
                          HDGFX_GET(Tx, FDI_TX_TRAINING) == HDGFX_GET(Rx, FDI_RX_TRAINING);

            return HDGFX_SET(FDI_RX_BIT_LOCK, Linked && HDGFX_GET(Rx, FDI_RX_TRAINING) == 0) |
                   HDGFX_SET(FDI_RX_SYMBOL_LOCK, Linked && HDGFX_GET(Rx, FDI_RX_TRAINING) == 1);
        }
    }
    return FakeGet(Fake, Offset);
}

static void
FakeIlkWrite(
    void *Context,
    uint32_t Offset,
    uint32_t Value)
{
    FAKE_ILK *Fake = Context;
    uint32_t *Slot = FakeSlot(Fake, Offset);
    uint32_t Old = *Slot;
    uint32_t Index;

    FakeUpdateStates(Fake);
    if ((Offset == HDGFX_REG(PCH_LVDS, 0) || Offset == HDGFX_REG(PCH_DPLL, 0) ||
         Offset == HDGFX_REG(PCH_DPLL, 1) || Offset == HDGFX_REG(PCH_PP_ON_DELAYS, 0) ||
         Offset == HDGFX_REG(PCH_PP_OFF_DELAYS, 0)) && FakePanelLocked(Fake))
    {
        FakeViolation(Fake, "protected register written while the panel is on");
        return;
    }

    for (Index = 0; Index < 2; Index++)
    {
        if (FakePipeRunning(Fake, Index) &&
            ((Offset >= HDGFX_REG(PIPE_HTOTAL, Index) && Offset < HDGFX_REG(PIPE_SRC, Index)) ||
             (Offset >= HDGFX_REG(PIPE_DATA_M1, Index) && Offset <= HDGFX_REG(PIPE_LINK_N1, Index))))
        {
            FakeViolation(Fake, "timing register written on a running pipe");
        }
        if ((Offset == HDGFX_REG(PIPE_SRC, Index) || Offset == HDGFX_REG(DSP_STRIDE, Index)) &&
            FakePipeRunning(Fake, Index) &&
            FakeScanLine(Fake, Index) >= HDGFX_GET(FakeGet(Fake, HDGFX_REG(PIPE_VTOTAL, Index)), TIMING_ACTIVE) + 1)
        {
            FakeViolation(Fake, "live source update inside vertical blank");
        }
        if (Offset == HDGFX_REG(PIPE_CONF, Index))
        {
            Value = HDGFX_REPLACE(Value, PIPE_CONF_STATE, HDGFX_GET(Old, PIPE_CONF_STATE));
            if (!HDGFX_GET(Old, PIPE_CONF_ENABLE) && HDGFX_GET(Value, PIPE_CONF_ENABLE))
                Fake->PipeEnables++;
            if (HDGFX_GET(Old, PIPE_CONF_ENABLE) != HDGFX_GET(Value, PIPE_CONF_ENABLE))
                Fake->PipeReadyUs[Index] = Fake->NowUs + 16000;
            if (!HDGFX_GET(Value, PIPE_CONF_ENABLE) && HDGFX_GET(FakeGet(Fake, HDGFX_REG(DSP_CNTR, Index)), DSP_ENABLE))
                FakeViolation(Fake, "pipe disabled under an enabled plane");
            if (!HDGFX_GET(Value, PIPE_CONF_ENABLE) && Fake->PanelOn &&
                Index == HDGFX_GET(FakeGet(Fake, HDGFX_REG(PCH_LVDS, 0)), LVDS_TRANSCODER))
                FakeViolation(Fake, "pipe disabled while the panel is on");
        }
        if (Offset == HDGFX_REG(DSP_CNTR, Index) && HDGFX_GET(Value, DSP_ENABLE) &&
            !HDGFX_GET(FakeGet(Fake, HDGFX_REG(PIPE_CONF, Index)), PIPE_CONF_ENABLE))
        {
            FakeViolation(Fake, "plane enabled on an inactive pipe");
        }
        if (Offset == HDGFX_REG(TRANS_CONF, Index))
        {
            uint32_t Rx = FakeGet(Fake, HDGFX_REG(FDI_RX_CTL, Index));

            Value = HDGFX_REPLACE(Value, TRANS_CONF_STATE, HDGFX_GET(Old, TRANS_CONF_STATE));
            if (HDGFX_GET(Old, TRANS_CONF_ENABLE) != HDGFX_GET(Value, TRANS_CONF_ENABLE))
                Fake->TranscoderReadyUs[Index] = Fake->NowUs + 16000;
            if (HDGFX_GET(Value, TRANS_CONF_ENABLE) &&
                (HDGFX_GET(Rx, FDI_RX_TRAINING) != 3 ||
                 !HDGFX_GET(FakeGet(Fake, HDGFX_REG(PCH_DPLL, Index)), DPLL_VCO_ENABLE)))
            {
                FakeViolation(Fake, "transcoder enabled before FDI training or its DPLL");
            }
        }
        if (Offset == HDGFX_REG(FDI_RX_CTL, Index))
        {
            if (HDGFX_GET(Value, FDI_RX_PLL_ENABLE) && !HDGFX_GET(Old, FDI_RX_PLL_ENABLE))
                Fake->RxPllUs = Fake->NowUs;
            if (HDGFX_GET(Value, FDI_RX_ENABLE) && !HDGFX_GET(Old, FDI_RX_ENABLE) && Fake->NowUs < Fake->RxPllUs + 10)
                FakeViolation(Fake, "FDI link enabled within 10 us of its PLL");
            if (HDGFX_GET(Value, FDI_RX_PCDCLK) && !HDGFX_GET(Value, FDI_RX_PLL_ENABLE))
                FakeViolation(Fake, "PCDCLK selected without the FDI PLL");
        }
        if (Offset == HDGFX_REG(PCH_DPLL, Index) && HDGFX_GET(Value, DPLL_VCO_ENABLE) &&
            HDGFX_GET(Value, DPLL_REFERENCE) == 0 &&
            HDGFX_GET(FakeGet(Fake, HDGFX_REG(PCH_DREF_CONTROL, 0)), DREF_NONSPREAD_SOURCE) != 2)
        {
            FakeViolation(Fake, "DPLL enabled on the non-spread reference while it is off");
        }
        if (Offset == HDGFX_REG(PCH_DPLL, Index) && HDGFX_GET(Value, DPLL_VCO_ENABLE) &&
            HDGFX_GET(Value, DPLL_MODE) == 2 && !HDGFX_GET(FakeGet(Fake, HDGFX_REG(PCH_LVDS, 0)), LVDS_ENABLE))
        {
            FakeViolation(Fake, "LVDS DPLL enabled before the LVDS port");
        }
    }
    if (Offset == HDGFX_REG(PCH_ADPA, 0))
    {
        Value = HDGFX_REPLACE(Value, ADPA_HOTPLUG_STATUS,
                              HDGFX_GET(Old, ADPA_HOTPLUG_STATUS) & ~HDGFX_GET(Value, ADPA_HOTPLUG_STATUS));
        if (HDGFX_GET(Value, ADPA_FORCE_TRIGGER) && !HDGFX_GET(Old, ADPA_FORCE_TRIGGER))
            Fake->CrtDetectDoneUs = Fake->NowUs + 10000;
        if (HDGFX_GET(Value, ADPA_ENABLE) && !HDGFX_GET(Old, ADPA_ENABLE) &&
            !HDGFX_GET(FakeGet(Fake, HDGFX_REG(TRANS_CONF, HDGFX_GET(Value, ADPA_TRANSCODER))), TRANS_CONF_STATE))
        {
            FakeViolation(Fake, "CRT port enabled before its transcoder");
        }
    }
    *Slot = Value;
    FakeUpdateStates(Fake);
}

static void
FakeIlkStall(
    void *Context,
    uint32_t Microseconds)
{
    ((FAKE_ILK *)Context)->NowUs += Microseconds;
}

static void
FakeIlkSleep(
    void *Context,
    uint32_t Milliseconds)
{
    ((FAKE_ILK *)Context)->NowUs += (uint64_t)Milliseconds * 1000;
}

static FAKE_ILK *
FakeIlkCreate(
    const char *Directory,
    HDGFX_MMIO *Mmio)
{
    char Path[1024];
    FAKE_ILK *Fake = calloc(1, sizeof(*Fake));

    snprintf(Path, sizeof(Path), "%s/x201-lvds-1280x800/intel_reg-dump.txt", Directory);
    LoadRegisterDump(Path, &Fake->Registers);
    Fake->PanelOn = true;
    Fake->FdiLocks = true;
    Mmio->Read = FakeIlkRead;
    Mmio->Context = Fake;
    Mmio->Write = FakeIlkWrite;
    Mmio->Stall = FakeIlkStall;
    Mmio->Sleep = FakeIlkSleep;
    return Fake;
}

static HDGFX_MODESET_STATUS
CommitAndCompare(
    const HDGFX_MMIO *Mmio,
    const HDGFX_LVDS_MODE *Mode,
    HDGFX_DISPLAY_STATE *Target)
{
    const HDGFX_GENERATION *Generation = HdgfxFindGeneration(0x8086, 0x0046);
    HDGFX_DISPLAY_STATE Current;
    HDGFX_DISPLAY_STATE Actual;
    HDGFX_MODESET_STATUS Status;
    const char *Mismatch;

    Generation->ReadDisplayState(Mmio, &Current);
    Status = Generation->ComputeLvds(&Current, Mode, Target);
    if (Status != HdgfxModesetSuccess)
        return Status;
    Status = Generation->CommitLvds(Mmio, &Current, Target);
    if (Status != HdgfxModesetSuccess)
        return Status;
    Generation->ReadDisplayState(Mmio, &Actual);
    Mismatch = HdgfxCompareLvdsState(Target, &Actual);
    if (Mismatch)
    {
        fprintf(stderr, "readback differs from the target in %s\n", Mismatch);
        Failures++;
    }
    return Status;
}

static void
TestLvdsModeset(
    const char *Directory)
{
    static const struct
    {
        uint32_t Offset;
        uint32_t Value;
    } Golden[] =
    {
        { 0xC6014, 0x88046004 }, { 0xC6040, 0x00021005 }, { 0xC6044, 0x00021005 },
        { 0xE1180, 0x80300302 }, { 0xF000C, 0xB0022050 }, { 0x60100, 0xB0044000 },
        { 0xF0008, 0xC0000040 }, { 0xC7204, 0xABCD0003 }, { 0x60040, 0x00020D8C },
        { 0x60044, 0x00080000 }, { 0x60034, 0x00800000 }, { 0x6001C, 0x04FF031F },
        { 0x68080, 0x00000000 }, { 0x70188, 0x00001400 },
        { 0x60000, 0x057A04FF }, { 0x60004, 0x057A04FF }, { 0x60008, 0x054F052F },
        { 0x6000C, 0x0334031F }, { 0x60010, 0x0334031F }, { 0x60014, 0x03280322 },
        { 0xE0000, 0x057A04FF }, { 0xE0004, 0x057A04FF }, { 0xE0008, 0x054F052F },
        { 0xE000C, 0x0334031F }, { 0xE0010, 0x0334031F }, { 0xE0014, 0x03280322 },
    };
    HDGFX_MMIO Mmio;
    FAKE_ILK *Fake = FakeIlkCreate(Directory, &Mmio);
    HDGFX_DISPLAY_STATE Current;
    HDGFX_DISPLAY_STATE Target;
    HDGFX_LVDS_MODE Mode;
    HDGFX_EDID Edid;
    char Path[1024];
    uint8_t *EdidData;
    size_t Size;
    uint64_t Start;
    unsigned Enables;
    unsigned Index;

    snprintf(Path, sizeof(Path), "%s/x201-lvds-1280x800/edid.bin", Directory);
    EdidData = LoadFile(Path, &Size);
    if (!EdidData || Size != HDGFX_EDID_SIZE || !HdgfxEdidParse(EdidData, &Edid) || Edid.DtdCount < 2)
    {
        fprintf(stderr, "%s: no usable EDID\n", Path);
        Failures++;
        free(EdidData);
        free(Fake);
        return;
    }
    free(EdidData);

    memset(&Mode, 0, sizeof(Mode));
    Mode.Panel = &Edid.Dtds[0];
    Mode.SourceWidth = 1280;
    Mode.SourceHeight = 800;
    Mode.Stride = 1280 * 4;
    Mode.Scaling = HdgfxScalingAspect;
    Mode.Ssc = true;

    HdgfxIlkReadDisplayState(&Mmio, &Current);
    CHECK_EQ(Current.SscReference, 1);
    CHECK_EQ(Current.NonSpreadReference, 1);
    CHECK_EQ(HdgfxIlkComputeLvds(&Current, &Mode, &Target), HdgfxModesetSuccess);
    CHECK_EQ(Target.Dplls[0].N, 4);
    CHECK_EQ(Target.Dplls[0].M1, 18);
    CHECK_EQ(Target.Dplls[0].M2, 7);
    CHECK_EQ(Target.Dplls[0].P1, 3);
    CHECK_EQ(Target.Dplls[0].P2, 14);
    CHECK_EQ(Target.Dplls[0].Reference, 3);
    CHECK_EQ(Target.Dplls[0].DotKHz, 69286);
    CHECK_EQ(Target.Pipes[0].FdiTx.Lanes, 1);
    CHECK_EQ(Target.Pipes[0].Fdi.LinkM, 134540);
    CHECK_EQ(Target.Pipes[0].Fdi.DataM, 4843442);
    CHECK_EQ(Target.Pipes[0].PanelFitter.Enabled, 0);
    CHECK_EQ(Target.Lvds.HSyncActiveLow, 1);
    CHECK_EQ(Target.Lvds.VSyncActiveLow, 1);

    Mode.SourceWidth = 1024;
    Mode.SourceHeight = 768;
    Mode.Stride = 1024 * 4;
    CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetSuccess);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_CTL_1, 0)), 0x80800000);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_WIN_POS, 0)), 107u << 16);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_WIN_SZ, 0)), (1066u << 16) | 800);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PIPE_SRC, 0)), (1023u << 16) | 767);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(DSP_CNTR, 0)), 0x98004000);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(WM0_PIPE, 0)), 0x00783818);
    CHECK_EQ(Fake->Violations, 0);

    Mode.SourceWidth = 800;
    Mode.SourceHeight = 600;
    Mode.Stride = 800 * 4;
    Mode.Scaling = HdgfxScalingStretch;
    CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetSuccess);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_WIN_POS, 0)), 0);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_WIN_SZ, 0)), (1280u << 16) | 800);

    Mode.SourceWidth = 1280;
    Mode.SourceHeight = 720;
    Mode.Stride = 1280 * 4;
    Mode.Scaling = HdgfxScalingCenter;
    CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetSuccess);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_WIN_POS, 0)), 40);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_WIN_SZ, 0)), (1280u << 16) | 720);

    Mode.SourceHeight = 800;
    Mode.Scaling = HdgfxScalingAspect;
    Enables = Fake->PipeEnables;
    for (Index = 0; Index < 100; Index++)
    {
        Mode.SourceWidth = Index & 1 ? 1280 : 1024;
        Mode.SourceHeight = Index & 1 ? 800 : 768;
        Mode.Stride = Mode.SourceWidth * 4;
        Start = Fake->NowUs;
        CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetSuccess);
        CHECK_EQ(Fake->NowUs - Start < 2 * 821 * FAKE_LINE_US, 1);
    }
    CHECK_EQ(Fake->PipeEnables, Enables);
    CHECK_EQ(Fake->Violations, 0);
    for (Index = 0; Index < sizeof(Golden) / sizeof(Golden[0]); Index++)
    {
        if (FakeGet(Fake, Golden[Index].Offset) != Golden[Index].Value)
        {
            fprintf(stderr, "register 0x%05x = 0x%08x, Mint has 0x%08x\n",
                    Golden[Index].Offset, FakeGet(Fake, Golden[Index].Offset), Golden[Index].Value);
            Failures++;
        }
    }
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PIPE_CONF, 0)), 0xC0000050);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PIPE_DATA_M1, 0)), 0x7E49E7B2);

    for (Index = 0; Index < 10; Index++)
    {
        HdgfxIlkReadDisplayState(&Mmio, &Current);
        CHECK_EQ(HdgfxIlkDisableLvds(&Mmio, &Current), HdgfxModesetSuccess);
        HdgfxIlkReadDisplayState(&Mmio, &Current);
        CHECK_EQ(Current.PanelPower.On, 0);
        CHECK_EQ(Current.Pipes[0].Enabled, 0);
        CHECK_EQ(Current.Pipes[0].Primary.Enabled, 0);
        CHECK_EQ(Current.Transcoders[0].Enabled, 0);
        CHECK_EQ(Current.Dplls[0].Enabled, 0);
        CHECK_EQ(Current.Lvds.Enabled, 0);
        CHECK_EQ(HdgfxIlkDisableLvds(&Mmio, &Current), HdgfxModesetSuccess);
        CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetSuccess);
    }
    CHECK_EQ(Fake->Violations, 0);

    HdgfxIlkReadDisplayState(&Mmio, &Current);
    Mode.SourceWidth = 1281;
    CHECK_EQ(HdgfxIlkComputeLvds(&Current, &Mode, &Target), HdgfxModesetInvalidMode);
    Mode.SourceWidth = 1280;
    Mode.Stride = 1280 * 4 + 32;
    CHECK_EQ(HdgfxIlkComputeLvds(&Current, &Mode, &Target), HdgfxModesetInvalidMode);
    Mode.Stride = 1280 * 4;
    Mode.Surface = 0x800;
    CHECK_EQ(HdgfxIlkComputeLvds(&Current, &Mode, &Target), HdgfxModesetInvalidMode);
    Mode.Surface = 0;
    Current.SscReference = false;
    CHECK_EQ(HdgfxIlkComputeLvds(&Current, &Mode, &Target), HdgfxModesetNoClock);

    Enables = Fake->PipeEnables;
    Mode.Panel = &Edid.Dtds[1];
    CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetSuccess);
    CHECK_EQ(Fake->PipeEnables, Enables + 1);
    CHECK_EQ(Target.Dplls[0].N, 6);
    CHECK_EQ(Target.Dplls[0].M1, 22);
    CHECK_EQ(Target.Dplls[0].M2, 11);
    CHECK_EQ(Target.Dplls[0].P1, 3);
    CHECK_EQ(Target.Dplls[0].P2, 14);
    CHECK_EQ(Target.Dplls[0].DotKHz, 57619);
    CHECK_EQ(HdgfxRefreshMilliHz(&Target.Pipes[0].Timings, Target.Dplls[0].DotKHz), 50022);
    CHECK_EQ(Target.Pipes[0].FdiTx.Lanes, 1);
    Mode.SourceWidth = 1024;
    Mode.SourceHeight = 768;
    Mode.Stride = 1024 * 4;
    CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetSuccess);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_CTL_1, 0)), 0x80800000);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_WIN_SZ, 0)), (1066u << 16) | 800);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(DSP_STRIDE, 0)), 1024 * 4);
    CHECK_EQ(Fake->PipeEnables, Enables + 1);
    Mode.Panel = &Edid.Dtds[0];
    CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetSuccess);
    CHECK_EQ(Fake->PipeEnables, Enables + 2);
    Mode.SourceWidth = 1280;
    Mode.SourceHeight = 800;
    Mode.Stride = 1280 * 4;
    CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetSuccess);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_CTL_1, 0)), 0);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_WIN_SZ, 0)), 0);
    CHECK_EQ(Fake->PipeEnables, Enables + 2);
    CHECK_EQ(Fake->Violations, 0);

    Fake->FdiLocks = false;
    Mode.Panel = &Edid.Dtds[1];
    CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetFdiBitLockFailed);
    Fake->FdiLocks = true;
    CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetSuccess);
    Mode.Panel = &Edid.Dtds[0];
    Fake->PanelStuck = true;
    CHECK_EQ(CommitAndCompare(&Mmio, &Mode, &Target), HdgfxModesetPanelOffTimeout);
    CHECK_EQ(Fake->Violations, 0);
    free(Fake);
}

static void
TestCrtModeset(
    const char *Directory)
{
    const HDGFX_GENERATION *Generation = HdgfxFindGeneration(0x8086, 0x0046);
    HDGFX_MMIO Mmio;
    FAKE_ILK *Fake = FakeIlkCreate(Directory, &Mmio);
    HDGFX_DISPLAY_STATE Current;
    HDGFX_DISPLAY_STATE Target;
    HDGFX_DISPLAY_STATE Actual;
    HDGFX_CRT_MODE Mode;
    HDGFX_DTD Dtd;
    const char *Mismatch;
    bool Attached;
    unsigned Enables;

    CHECK_EQ(Generation->CrtDdcPins, 2);
    CHECK_EQ(Generation->DetectCrt(&Mmio, &Attached), HdgfxModesetSuccess);
    CHECK_EQ(Attached, 0);
    Fake->CrtAttached = true;
    CHECK_EQ(Generation->DetectCrt(&Mmio, &Attached), HdgfxModesetSuccess);
    CHECK_EQ(Attached, 1);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PCH_ADPA, 0)), 0x00F40000);
    Generation->EnableCrtHotplug(&Mmio, false);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PCH_ADPA, 0)), 0x00740000);
    Generation->EnableCrtHotplug(&Mmio, true);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PCH_ADPA, 0)), 0x00F40000);
    *FakeSlot(Fake, HDGFX_REG(PCH_DREF_CONTROL, 0)) = 0x00001000;

    memset(&Dtd, 0, sizeof(Dtd));
    Dtd.DotKHz = 148500;
    Dtd.Timings.HActive = 1920;
    Dtd.Timings.HSyncStart = 2008;
    Dtd.Timings.HSyncEnd = 2052;
    Dtd.Timings.HTotal = 2200;
    Dtd.Timings.VActive = 1080;
    Dtd.Timings.VSyncStart = 1084;
    Dtd.Timings.VSyncEnd = 1089;
    Dtd.Timings.VTotal = 1125;
    Dtd.HSyncPositive = true;
    Dtd.VSyncPositive = true;

    memset(&Mode, 0, sizeof(Mode));
    Mode.Timing = &Dtd;
    Mode.Pipe = 1;
    Mode.Stride = 1920 * 4;
    Mode.Surface = 0x1000000;

    HdgfxIlkReadDisplayState(&Mmio, &Current);
    CHECK_EQ(Current.Crt.Enabled, 0);
    CHECK_EQ(Current.NonSpreadReference, 0);
    CHECK_EQ(Generation->DisableCrt(&Mmio, &Current), HdgfxModesetSuccess);
    HdgfxIlkReadDisplayState(&Mmio, &Actual);
    CHECK_EQ(Actual.Pipes[0].Active, 1);
    CHECK_EQ(Actual.Transcoders[0].Active, 1);
    CHECK_EQ(Generation->ComputeCrt(&Current, &Mode, &Target), HdgfxModesetSuccess);
    CHECK_EQ(Target.Dplls[1].N, 4);
    CHECK_EQ(Target.Dplls[1].M1, 18);
    CHECK_EQ(Target.Dplls[1].M2, 9);
    CHECK_EQ(Target.Dplls[1].P1, 2);
    CHECK_EQ(Target.Dplls[1].P2, 10);
    CHECK_EQ(Target.Dplls[1].DotKHz, 148500);
    CHECK_EQ(Target.Pipes[1].FdiTx.Lanes, 2);
    CHECK_EQ(Target.Pipes[1].Fdi.DataM, 6920602);
    CHECK_EQ(Target.Pipes[1].Fdi.LinkM, 288358);
    CHECK_EQ(HdgfxRefreshMilliHz(&Target.Pipes[1].Timings, Target.Dplls[1].DotKHz), 60000);

    Enables = Fake->PipeEnables;
    CHECK_EQ(Generation->CommitCrt(&Mmio, &Current, &Target), HdgfxModesetSuccess);
    CHECK_EQ(Fake->PipeEnables, Enables + 1);
    HdgfxIlkReadDisplayState(&Mmio, &Actual);
    Mismatch = HdgfxCompareCrtState(&Target, &Actual);
    if (Mismatch)
    {
        fprintf(stderr, "CRT readback differs from the target in %s\n", Mismatch);
        Failures++;
    }
    Mismatch = HdgfxCompareLvdsState(&Current, &Actual);
    if (Mismatch)
    {
        fprintf(stderr, "LVDS changed under the CRT commit in %s\n", Mismatch);
        Failures++;
    }
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PCH_ADPA, 0)), 0xC0F40018);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PCH_DREF_CONTROL, 0)), 0x00001400);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PCH_DPLL, 1)), 0x84020002);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PCH_FP0, 1)), 0x00021007);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PIPE_HTOTAL, 1)), 0x0897077F);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(TRANS_VSYNC, 1)), 0x0440043B);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PIPE_CONF, 1)), 0xC0000000);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(WM0_PIPE, 1)), 0x00783818);
    CHECK_EQ(Fake->Violations, 0);

    Current = Actual;
    Mode.Surface = 0x2000000;
    CHECK_EQ(Generation->ComputeCrt(&Current, &Mode, &Target), HdgfxModesetSuccess);
    CHECK_EQ(Generation->CommitCrt(&Mmio, &Current, &Target), HdgfxModesetSuccess);
    CHECK_EQ(Fake->PipeEnables, Enables + 1);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(DSP_SURF, 1)), 0x2000000);

    HdgfxIlkReadDisplayState(&Mmio, &Current);
    Mode.SourceWidth = 1280;
    Mode.SourceHeight = 800;
    Mode.Stride = 1280 * 4;
    Mode.Surface = 0;
    Mode.Scaling = HdgfxScalingAspect;
    CHECK_EQ(Generation->ComputeCrt(&Current, &Mode, &Target), HdgfxModesetSuccess);
    CHECK_EQ(Target.Pipes[1].SourceWidth, 1280);
    CHECK_EQ(Target.Pipes[1].PanelFitter.Enabled, 1);
    CHECK_EQ(Target.Pipes[1].PanelFitter.X, 96);
    CHECK_EQ(Target.Pipes[1].PanelFitter.Y, 0);
    CHECK_EQ(Target.Pipes[1].PanelFitter.Width, 1728);
    CHECK_EQ(Target.Pipes[1].PanelFitter.Height, 1080);
    CHECK_EQ(Generation->CommitCrt(&Mmio, &Current, &Target), HdgfxModesetSuccess);
    CHECK_EQ(Fake->PipeEnables, Enables + 1);
    HdgfxIlkReadDisplayState(&Mmio, &Actual);
    Mismatch = HdgfxCompareCrtState(&Target, &Actual);
    if (Mismatch)
    {
        fprintf(stderr, "Scaled CRT readback differs from the target in %s\n", Mismatch);
        Failures++;
    }
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PIPE_SRC, 1)), 0x04FF031F);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_WIN_POS, 1)), 0x00600000);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_WIN_SZ, 1)), 0x06C00438);
    CHECK_EQ(FakeGet(Fake, HDGFX_REG(PF_CTL_1, 1)) & HDGFX_MASK(PF_ENABLE), HDGFX_MASK(PF_ENABLE));
    CHECK_EQ(Fake->Violations, 0);

    Current = Actual;
    Mode.SourceWidth = 2560;
    CHECK_EQ(Generation->ComputeCrt(&Current, &Mode, &Target), HdgfxModesetInvalidMode);
    Mode.SourceWidth = 1280;

    HdgfxIlkReadDisplayState(&Mmio, &Current);
    CHECK_EQ(Generation->DisableCrt(&Mmio, &Current), HdgfxModesetSuccess);
    HdgfxIlkReadDisplayState(&Mmio, &Actual);
    CHECK_EQ(Actual.Crt.Enabled, 0);
    CHECK_EQ(Actual.Pipes[1].Enabled, 0);
    CHECK_EQ(Actual.Transcoders[1].Enabled, 0);
    CHECK_EQ(Actual.Dplls[1].Enabled, 0);
    CHECK_EQ(Actual.Pipes[0].Active, 1);
    CHECK_EQ(Actual.Transcoders[0].Active, 1);
    CHECK_EQ(Actual.Transcoders[0].FdiRx.PllEnabled, 1);
    CHECK_EQ(Fake->Violations, 0);

    Mode.Pipe = 0;
    CHECK_EQ(Generation->ComputeCrt(&Actual, &Mode, &Target), HdgfxModesetInvalidMode);
    Mode.Pipe = 1;
    Mode.Stride = 1919 * 4;
    CHECK_EQ(Generation->ComputeCrt(&Actual, &Mode, &Target), HdgfxModesetInvalidMode);
    Mode.Stride = 1920 * 4;
    Dtd.DotKHz = 241500;
    CHECK_EQ(Generation->ComputeCrt(&Actual, &Mode, &Target), HdgfxModesetNoClock);
    free(Fake);
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
    TestLvdsModeset(argv[1]);
    TestCrtModeset(argv[1]);
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
