/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What KdSystemDebugControl serves a kernel-mode caller while a debugger is attached
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/kdfuncs.h>

static
NTSTATUS
BusData(
    _In_ SYSDBG_COMMAND Command,
    _In_ ULONG Offset,
    _Inout_updates_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size,
    _Out_ PULONG Length)
{
    SYSDBG_BUS_DATA Request;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Address = Offset;
    Request.Buffer = Buffer;
    Request.Request = Size;
    Request.BusDataType = PCIConfiguration;
    Request.BusNumber = 0;
    Request.SlotNumber = 0;

    *Length = 0xDEADBEEF;
    return KdSystemDebugControl(Command, &Request, sizeof(Request), NULL, 0, Length, KernelMode);
}

static
VOID
TestBusData(VOID)
{
    const ULONG LineOffset = FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.InterruptLine);
    ULONG Expected = 0, Value = 0, Length, HalLength;
    UCHAR Line = 0;
    NTSTATUS Status;

    HalLength = HalGetBusDataByOffset(PCIConfiguration, 0, 0, &Expected, 0, sizeof(Expected));
    if (skip(HalLength == sizeof(Expected) && (USHORT)Expected != PCI_INVALID_VENDORID,
             "No PCI function answers at 0:0.0\n"))
    {
        return;
    }

    Status = BusData(SysDbgReadBusData, 0, &Value, sizeof(Value), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Length, HalLength);
    ok_eq_hex(Value, Expected);

    HalLength = HalGetBusDataByOffset(PCIConfiguration, 0, 0, &Line, LineOffset, sizeof(Line));
    ok_eq_ulong(HalLength, (ULONG)sizeof(Line));
    HalLength = HalSetBusDataByOffset(PCIConfiguration, 0, 0, &Line, LineOffset, sizeof(Line));
    if (skip(HalLength == sizeof(Line), "The HAL does not write the interrupt line of 0:0.0\n"))
        return;

    Status = BusData(SysDbgWriteBusData, LineOffset, &Line, sizeof(Line), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Length, (ULONG)sizeof(Line));
}

static
VOID
TestCheckLowMemory(VOID)
{
    ULONG Length;
    NTSTATUS Status;

    /* Nothing below 4 GB carries the /NOLOWMEM pattern, so no corruption can be found */
    Status = KdSystemDebugControl(SysDbgCheckLowMemory, NULL, 0, NULL, 0, &Length, KernelMode);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

START_TEST(KdDebuggerPresent)
{
    BOOLEAN NotPresent;

    if (skip(KD_DEBUGGER_ENABLED, "The kernel debugger is not enabled\n"))
        return;

    /* KD_DEBUGGER_NOT_PRESENT is documented as writable by any kernel-mode binary */
    NotPresent = KD_DEBUGGER_NOT_PRESENT;
    KD_DEBUGGER_NOT_PRESENT = FALSE;

    TestBusData();
    TestCheckLowMemory();

    KD_DEBUGGER_NOT_PRESENT = NotPresent;
}

/* EOF */
