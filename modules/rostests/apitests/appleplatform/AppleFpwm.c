/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unit tests for the Apple silicon PWM controller and PWM LED driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <ndk/ntndk.h>
#include <libernt/fdtlib.h>

#define UNIT_TEST

static ULONG SimData[0x40 / sizeof(ULONG)];
static ULONG SimWriteOffset[8];
static ULONG SimWriteValue[8];
static ULONG SimWriteCount;

#define SimBase ((PUCHAR)SimData)
#define SimRegister(Offset) SimData[(Offset) / sizeof(ULONG)]

static
VOID
SimWrite(volatile ULONG *Address, ULONG Value)
{
    if (SimWriteCount < RTL_NUMBER_OF(SimWriteOffset))
    {
        SimWriteOffset[SimWriteCount] = (ULONG)((PUCHAR)Address - SimBase);
        SimWriteValue[SimWriteCount] = Value;
    }
    ++SimWriteCount;
    *Address = Value;
}

#define READ_REGISTER_ULONG(Address) (*(Address))
#define WRITE_REGISTER_ULONG(Address, Value) SimWrite(Address, Value)

#include "../../../../drivers/platform/applefpwm/fpwm.c"
#include "../../../../drivers/platform/applefpwm/pwmled.c"
#include "../hal/fdtbuild.h"

#define TEST_CLOCK_HZ 24000000ULL

static
VOID
ResetSim(VOID)
{
    memset(SimData, 0, sizeof(SimData));
    memset(SimWriteOffset, 0, sizeof(SimWriteOffset));
    memset(SimWriteValue, 0, sizeof(SimWriteValue));
    SimWriteCount = 0;
}

static
VOID
TestApply(VOID)
{
    FPWM_OUTPUT Output;

    ResetSim();
    FpwmApply(SimBase, TEST_CLOCK_HZ, TRUE, 40000, 19921);
    ok(SimWriteCount == 3, "%lu writes\n", SimWriteCount);
    ok(SimWriteOffset[0] == 0x1C && SimWriteValue[0] == 478, "first write %lx = %lu\n", SimWriteOffset[0],
       SimWriteValue[0]);
    ok(SimWriteOffset[1] == 0x18 && SimWriteValue[1] == 482, "second write %lx = %lu\n", SimWriteOffset[1],
       SimWriteValue[1]);
    ok(SimWriteOffset[2] == 0x00 && SimWriteValue[2] == 0x4021, "third write %lx = %lx\n", SimWriteOffset[2],
       SimWriteValue[2]);

    FpwmReadOutput(SimBase, TEST_CLOCK_HZ, &Output);
    ok(Output.Enabled, "the output reads as off\n");
    ok(Output.Control == 0x4021 && Output.OnCycles == 478 && Output.OffCycles == 482,
       "control %lx, %lu on, %lu off\n", Output.Control, Output.OnCycles, Output.OffCycles);
    ok(Output.DutyNs == 19917 && Output.PeriodNs == 40000, "duty %I64u of %I64u ns\n", Output.DutyNs,
       Output.PeriodNs);

    ResetSim();
    FpwmApply(SimBase, TEST_CLOCK_HZ, TRUE, 40000, 40000);
    ok(SimRegister(0x1C) == 960 && SimRegister(0x18) == 0, "full duty: %lu on, %lu off\n", SimRegister(0x1C),
       SimRegister(0x18));
    ResetSim();
    FpwmApply(SimBase, TEST_CLOCK_HZ, TRUE, 40000, 0);
    ok(SimRegister(0x1C) == 0 && SimRegister(0x18) == 960, "no duty: %lu on, %lu off\n", SimRegister(0x1C),
       SimRegister(0x18));

    ResetSim();
    SimRegister(0x00) = 0x4021;
    SimRegister(0x1C) = 5;
    FpwmApply(SimBase, TEST_CLOCK_HZ, FALSE, 40000, 0);
    ok(SimWriteCount == 1 && SimWriteOffset[0] == 0 && SimWriteValue[0] == 0, "disable: %lu writes, %lx = %lx\n",
       SimWriteCount, SimWriteOffset[0], SimWriteValue[0]);
    FpwmReadOutput(SimBase, TEST_CLOCK_HZ, &Output);
    ok(!Output.Enabled, "a cleared control reads as on\n");

    ResetSim();
    SimRegister(0x00) = 0x0001;
    FpwmReadOutput(SimBase, TEST_CLOCK_HZ, &Output);
    ok(!Output.Enabled, "enable without output enable reads as on\n");
    SimRegister(0x00) = 0x4000;
    FpwmReadOutput(SimBase, TEST_CLOCK_HZ, &Output);
    ok(!Output.Enabled, "output enable without enable reads as on\n");
    ok(Output.DutyNs == 0 && Output.PeriodNs == 0, "duty %I64u of %I64u ns\n", Output.DutyNs, Output.PeriodNs);
}

static
VOID
OpenTree(PFDT Fdt)
{
    FdtFinish();
    ok(FdtOpen(Builder.Blob, Builder.BlobSize, Fdt), "the test tree does not open\n");
}

static
ULONG
FindNode(const FDT *Fdt, const CHAR *Path)
{
    ULONG Node = FDT_NO_NODE;

    ok(FdtResolvePath(Fdt, Path, &Node, NULL, NULL), "%s is missing from the test tree\n", Path);
    return Node;
}

static
VOID
BuildLedTree(VOID)
{
    memset(&Builder, 0, sizeof(Builder));
    FdtBeginNode("");
    FdtBeginNode("led-controller");
    FdtStrings("compatible", LIST("pwm-leds"));
    FdtBeginNode("led-0");
    FdtCells("pwms", 3, 0xa8, 0, 0x9c40);
    FdtStrings("function", LIST("kbd_backlight"));
    FdtCells("color", 1, 0);
    FdtCells("max-brightness", 1, 0xff);
    FdtStrings("default-state", LIST("keep"));
    FdtEndNode();
    FdtBeginNode("led-1");
    FdtCells("pwms", 3, 0xa8, 0, 1000);
    FdtStrings("label", LIST("status"));
    FdtStrings("function", LIST("indicator"));
    FdtCells("max-brightness", 1, 100);
    FdtStrings("default-state", LIST("on"));
    FdtCells("default-brightness", 1, 40);
    FdtProperty("active-low", NULL, 0);
    FdtEndNode();
    FdtBeginNode("led-2");
    FdtCells("pwms", 3, 0xa8, 1, 1000);
    FdtCells("max-brightness", 1, 100);
    FdtStrings("default-state", LIST("on"));
    FdtCells("default-brightness", 1, 400);
    FdtEndNode();
    FdtBeginNode("led-3");
    FdtCells("pwms", 3, 0xa8, 0, 1000);
    FdtEndNode();
    FdtBeginNode("led-4");
    FdtCells("pwms", 3, 0xa8, 0, 0);
    FdtCells("max-brightness", 1, 10);
    FdtEndNode();
    FdtBeginNode("led-5");
    FdtCells("pwms", 4, 0xa8, 0, 1000, 0);
    FdtCells("max-brightness", 1, 10);
    FdtEndNode();
    FdtEndNode();
    FdtEndNode();
}

static
VOID
TestParse(VOID)
{
    PWM_LED Led;
    NTSTATUS Status;
    FDT Fdt;

    BuildLedTree();
    OpenTree(&Fdt);

    Status = PwmLedParse(&Fdt, FindNode(&Fdt, "/led-controller/led-0"), 2, &Led);
    ok(Status == STATUS_SUCCESS, "keyboard backlight: %lx\n", Status);
    ok(Led.Provider == 0xa8 && Led.Channel == 0 && Led.PeriodNs == 40000, "PWM %lx channel %lu, %lu ns\n",
       Led.Provider, Led.Channel, Led.PeriodNs);
    ok(Led.MaxBrightness == 255 && Led.DefaultBrightness == 255, "brightness %lu, default %lu\n", Led.MaxBrightness,
       Led.DefaultBrightness);
    ok(Led.Default == PwmLedDefaultKeep && !Led.ActiveLow, "default %d, active low %d\n", Led.Default, Led.ActiveLow);
    ok(!strcmp(Led.Name, "kbd_backlight"), "name %s\n", Led.Name);

    Status = PwmLedParse(&Fdt, FindNode(&Fdt, "/led-controller/led-1"), 2, &Led);
    ok(Status == STATUS_SUCCESS, "status LED: %lx\n", Status);
    ok(Led.Default == PwmLedDefaultOn && Led.ActiveLow && Led.DefaultBrightness == 40,
       "default %d, active low %d, default brightness %lu\n", Led.Default, Led.ActiveLow, Led.DefaultBrightness);
    ok(!strcmp(Led.Name, "status"), "name %s\n", Led.Name);

    Status = PwmLedParse(&Fdt, FindNode(&Fdt, "/led-controller/led-2"), 2, &Led);
    ok(Status == STATUS_SUCCESS, "unnamed LED: %lx\n", Status);
    ok(Led.Channel == 1 && Led.DefaultBrightness == 100, "channel %lu, default brightness %lu\n", Led.Channel,
       Led.DefaultBrightness);
    ok(!strcmp(Led.Name, "led-2"), "name %s\n", Led.Name);

    Status = PwmLedParse(&Fdt, FindNode(&Fdt, "/led-controller/led-3"), 2, &Led);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "no max-brightness: %lx\n", Status);
    Status = PwmLedParse(&Fdt, FindNode(&Fdt, "/led-controller/led-4"), 2, &Led);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "a zero period: %lx\n", Status);
    Status = PwmLedParse(&Fdt, FindNode(&Fdt, "/led-controller/led-5"), 2, &Led);
    ok(Status == STATUS_DEVICE_CONFIGURATION_ERROR, "a specifier longer than the provider's: %lx\n", Status);
    Status = PwmLedParse(&Fdt, FindNode(&Fdt, "/led-controller/led-5"), 3, &Led);
    ok(Status == STATUS_NOT_SUPPORTED, "a provider with flag cells: %lx\n", Status);
}

static
VOID
TestBrightness(VOID)
{
    FPWM_OUTPUT Output;
    PWM_LED Led, Indicator;
    ULONG Brightness;
    FDT Fdt;

    BuildLedTree();
    OpenTree(&Fdt);
    ok(PwmLedParse(&Fdt, FindNode(&Fdt, "/led-controller/led-0"), 2, &Led) == STATUS_SUCCESS, "led-0\n");
    ok(PwmLedParse(&Fdt, FindNode(&Fdt, "/led-controller/led-1"), 2, &Indicator) == STATUS_SUCCESS, "led-1\n");

    memset(&Output, 0, sizeof(Output));
    Brightness = PwmLedStartBrightness(&Led, &Output);
    ok(Brightness == 127, "kept dark LED starts at %lu\n", Brightness);
    Output.Enabled = TRUE;
    Output.PeriodNs = 40000;
    Brightness = PwmLedStartBrightness(&Led, &Output);
    ok(Brightness == 127, "kept LED at zero duty starts at %lu\n", Brightness);
    Output.DutyNs = 10000;
    Brightness = PwmLedStartBrightness(&Led, &Output);
    ok(Brightness == 63, "kept lit LED starts at %lu\n", Brightness);
    Output.Enabled = FALSE;
    Brightness = PwmLedStartBrightness(&Led, &Output);
    ok(Brightness == 127, "kept LED whose PWM is off starts at %lu\n", Brightness);

    Brightness = PwmLedStartBrightness(&Indicator, &Output);
    ok(Brightness == 40, "default-state on starts at %lu\n", Brightness);
    Indicator.Default = PwmLedDefaultKeep;
    Output.Enabled = TRUE;
    Output.PeriodNs = 1000;
    Output.DutyNs = 900;
    Brightness = PwmLedStartBrightness(&Indicator, &Output);
    ok(Brightness == 10, "kept active-low LED starts at %lu\n", Brightness);
    Indicator.Default = PwmLedDefaultOff;
    Brightness = PwmLedStartBrightness(&Indicator, &Output);
    ok(Brightness == 0, "default-state off starts at %lu\n", Brightness);

    ok(PwmLedDutyNs(&Led, 127) == 19921, "duty %I64u\n", PwmLedDutyNs(&Led, 127));
    ok(PwmLedDutyNs(&Led, 255) == 40000, "duty %I64u\n", PwmLedDutyNs(&Led, 255));
    ok(PwmLedDutyNs(&Led, 0) == 0, "duty %I64u\n", PwmLedDutyNs(&Led, 0));
    ok(PwmLedDutyNs(&Indicator, 40) == 600, "active-low duty %I64u\n", PwmLedDutyNs(&Indicator, 40));
    ok(PwmLedDutyNs(&Indicator, 0) == 1000, "active-low dark duty %I64u\n", PwmLedDutyNs(&Indicator, 0));

    ResetSim();
    memset(&Output, 0, sizeof(Output));
    FpwmApply(SimBase, TEST_CLOCK_HZ, TRUE, Led.PeriodNs, PwmLedDutyNs(&Led, PwmLedStartBrightness(&Led, &Output)));
    ok(SimRegister(0x1C) == 478 && SimRegister(0x18) == 482 && SimRegister(0x00) == 0x4021,
       "keyboard backlight start: %lu on, %lu off, control %lx\n", SimRegister(0x1C), SimRegister(0x18),
       SimRegister(0x00));
}

START_TEST(AppleFpwm)
{
    TestApply();
    TestParse();
    TestBrightness();
}
