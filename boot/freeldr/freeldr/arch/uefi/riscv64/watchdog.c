/*
 * PROJECT:     LiberNT FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stop the board watchdogs the firmware leaves running
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <uefildr.h>
#include <Fdt.h>
#include <reactos/riscv64/fdtlib.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

extern EFI_SYSTEM_TABLE* GlobalSystemTable;

#define UEFI_FDT_MAXIMUM_DEPTH  16

#define K1X_I2C_CONTROL         0x00
#define K1X_I2C_STATUS          0x04
#define K1X_I2C_DATA            0x0C
#define K1X_I2C_CR_START        (1UL << 0)
#define K1X_I2C_CR_STOP         (1UL << 1)
#define K1X_I2C_CR_NAK          (1UL << 2)
#define K1X_I2C_CR_BYTE         (1UL << 3)
#define K1X_I2C_CR_FIFO         (1UL << 5)
#define K1X_I2C_CR_DMA          (1UL << 7)
#define K1X_I2C_CR_MASTER_ABORT (1UL << 12)
#define K1X_I2C_CR_CLOCK        (1UL << 13)
#define K1X_I2C_CR_UNIT         (1UL << 14)
#define K1X_I2C_CR_INTERRUPTS   0xFFFC0000UL
#define K1X_I2C_SR_NAK          (1UL << 14)
#define K1X_I2C_SR_UNIT_BUSY    (1UL << 15)
#define K1X_I2C_SR_BUS_BUSY     (1UL << 16)
#define K1X_I2C_SR_LOST         (1UL << 18)
#define K1X_I2C_SR_TX_EMPTY     (1UL << 19)
#define K1X_I2C_SR_RX_FULL      (1UL << 20)
#define K1X_I2C_SR_BUS_ERROR    (1UL << 22)
#define K1X_I2C_SR_EVENTS       0xFFFC0000UL
#define K1X_I2C_POLLS           20000
#define K1X_I2C_POLL_US         5

#define P1_WDT_CONTROL          0x44
#define P1_WDT_ENABLE           (1U << 3)

#define K1X_WDT_WFAR            0xB0
#define K1X_WDT_WSAR            0xB4
#define K1X_WDT_WMER            0xB8
#define K1X_WDT_WCR             0xC8
#define K1X_WDT_KEY1            0xBABAUL
#define K1X_WDT_KEY2            0xEB10UL
#define K1X_MPMU_APRR_WDTR      (1UL << 4)

static const CHAR *const K1I2cCompatibles[] = { "spacemit,i2c", "spacemit,k1x-i2c", "ky,i2c", NULL };
static const CHAR *const P1Compatibles[] = { "spacemit,spm8821", "ky,spm8821", NULL };
static const CHAR *const K1WdtCompatibles[] = { "spacemit,k1x-wdt", "ky,x1-wdt", NULL };

static
BOOLEAN
UefiFdtNodeIs(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_ const CHAR *const *Compatibles)
{
    const VOID *Property;
    ULONG Length;

    Property = RiscvFdtGetProperty(Fdt, Node, "compatible", &Length);
    if (Property == NULL)
        return FALSE;
    for (; *Compatibles; ++Compatibles)
    {
        if (RiscvFdtStringListContains(Property, Length, *Compatibles))
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
UefiFdtNodeEnabled(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node)
{
    const VOID *Property;
    ULONG Length;

    Property = RiscvFdtGetProperty(Fdt, Node, "status", &Length);
    return !Property || RiscvFdtStringListContains(Property, Length, "okay") ||
           RiscvFdtStringListContains(Property, Length, "ok");
}

static
ULONG
K1I2cRead(
    _In_ PUCHAR Registers,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Registers + Offset));
}

static
VOID
K1I2cWrite(
    _In_ PUCHAR Registers,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Registers + Offset), Value);
}

static
ULONG
K1I2cControlBase(
    _In_ PUCHAR Registers)
{
    return (K1I2cRead(Registers, K1X_I2C_CONTROL) &
            ~(K1X_I2C_CR_START | K1X_I2C_CR_STOP | K1X_I2C_CR_NAK | K1X_I2C_CR_BYTE |
              K1X_I2C_CR_FIFO | K1X_I2C_CR_DMA | K1X_I2C_CR_MASTER_ABORT | K1X_I2C_CR_INTERRUPTS)) |
           K1X_I2C_CR_UNIT | K1X_I2C_CR_CLOCK;
}

static
BOOLEAN
K1I2cTransferByte(
    _In_ PUCHAR Registers,
    _In_ ULONG Flags,
    _In_ ULONG Event,
    _Inout_ PUCHAR Byte)
{
    ULONG Loops = K1X_I2C_POLLS, Status;

    if (Event == K1X_I2C_SR_TX_EMPTY)
        K1I2cWrite(Registers, K1X_I2C_DATA, *Byte);
    K1I2cWrite(Registers, K1X_I2C_CONTROL, K1I2cControlBase(Registers) | Flags | K1X_I2C_CR_BYTE);
    while (Loops--)
    {
        Status = K1I2cRead(Registers, K1X_I2C_STATUS);
        if (Status & (K1X_I2C_SR_LOST | K1X_I2C_SR_BUS_ERROR))
        {
            K1I2cWrite(Registers, K1X_I2C_STATUS, Status & K1X_I2C_SR_EVENTS);
            break;
        }
        if (Status & Event)
        {
            K1I2cWrite(Registers, K1X_I2C_STATUS, Event);
            if (Event == K1X_I2C_SR_RX_FULL)
                *Byte = (UCHAR)K1I2cRead(Registers, K1X_I2C_DATA);
            else if ((Status & K1X_I2C_SR_NAK) && !(Flags & K1X_I2C_CR_STOP))
                break;
            return TRUE;
        }
        StallExecutionProcessor(K1X_I2C_POLL_US);
    }
    K1I2cWrite(Registers, K1X_I2C_CONTROL, K1I2cControlBase(Registers) | K1X_I2C_CR_MASTER_ABORT);
    return FALSE;
}

static
BOOLEAN
P1Access(
    _In_ PUCHAR Registers,
    _In_ UCHAR Address,
    _In_ UCHAR Register,
    _Inout_ PUCHAR Value,
    _In_ BOOLEAN Write)
{
    ULONG Loops = K1X_I2C_POLLS;
    UCHAR Byte;

    while (K1I2cRead(Registers, K1X_I2C_STATUS) & (K1X_I2C_SR_UNIT_BUSY | K1X_I2C_SR_BUS_BUSY))
    {
        if (!Loops--)
            return FALSE;
        StallExecutionProcessor(K1X_I2C_POLL_US);
    }
    K1I2cWrite(Registers, K1X_I2C_STATUS, K1X_I2C_SR_EVENTS);

    Byte = (UCHAR)(Address << 1);
    if (!K1I2cTransferByte(Registers, K1X_I2C_CR_START, K1X_I2C_SR_TX_EMPTY, &Byte))
        return FALSE;
    Byte = Register;
    if (!K1I2cTransferByte(Registers, 0, K1X_I2C_SR_TX_EMPTY, &Byte))
        return FALSE;
    if (Write)
        return K1I2cTransferByte(Registers, K1X_I2C_CR_STOP, K1X_I2C_SR_TX_EMPTY, Value);

    Byte = (UCHAR)((Address << 1) | 1);
    if (!K1I2cTransferByte(Registers, K1X_I2C_CR_START, K1X_I2C_SR_TX_EMPTY, &Byte))
        return FALSE;
    return K1I2cTransferByte(Registers, K1X_I2C_CR_STOP | K1X_I2C_CR_NAK, K1X_I2C_SR_RX_FULL, Value);
}

static
VOID
UefiStopP1Watchdog(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_ ULONG Bus,
    _In_ ULONG BusParent)
{
    ULONGLONG BusBase, BusSize, Address, Unused;
    PUCHAR Registers;
    UCHAR Control;

    if (Bus == RISCV_FDT_NO_NODE || BusParent == RISCV_FDT_NO_NODE ||
        !UefiFdtNodeIs(Fdt, Bus, K1I2cCompatibles) || !UefiFdtNodeEnabled(Fdt, Bus) ||
        !RiscvFdtReadReg(Fdt, Bus, BusParent, 0, &BusBase, &BusSize) ||
        BusSize < K1X_I2C_DATA + sizeof(ULONG) ||
        !RiscvFdtReadReg(Fdt, Node, Bus, 0, &Address, &Unused) || Address >= 0x80)
    {
        return;
    }

    Registers = (PUCHAR)(ULONG_PTR)BusBase;
    if (!P1Access(Registers, (UCHAR)Address, P1_WDT_CONTROL, &Control, FALSE))
    {
        ERR("Unable to read the PMIC watchdog control over I2C at 0x%llx\n", BusBase);
        return;
    }
    if (!(Control & P1_WDT_ENABLE))
        return;

    Control &= ~P1_WDT_ENABLE;
    if (!P1Access(Registers, (UCHAR)Address, P1_WDT_CONTROL, &Control, TRUE))
    {
        ERR("Unable to stop the PMIC watchdog over I2C at 0x%llx\n", BusBase);
        return;
    }
    WARN("Stopped the PMIC watchdog left running by the firmware\n");
}

static
VOID
K1WdtWrite(
    _In_ PUCHAR Registers,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Registers + K1X_WDT_WFAR), K1X_WDT_KEY1);
    WRITE_REGISTER_ULONG((PULONG)(Registers + K1X_WDT_WSAR), K1X_WDT_KEY2);
    WRITE_REGISTER_ULONG((PULONG)(Registers + Offset), Value);
}

static
VOID
UefiStopK1Watchdog(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_ ULONG Parent)
{
    ULONGLONG Base, Size, AprrBase, AprrSize;
    PUCHAR Registers;

    if (Parent == RISCV_FDT_NO_NODE ||
        !RiscvFdtReadReg(Fdt, Node, Parent, 0, &Base, &Size) ||
        Size < K1X_WDT_WCR + sizeof(ULONG) ||
        !RiscvFdtReadReg(Fdt, Node, Parent, 1, &AprrBase, &AprrSize) ||
        AprrSize < sizeof(ULONG))
    {
        return;
    }

    if (!(READ_REGISTER_ULONG((PULONG)(ULONG_PTR)AprrBase) & K1X_MPMU_APRR_WDTR))
        return;

    Registers = (PUCHAR)(ULONG_PTR)Base;
    K1WdtWrite(Registers, K1X_WDT_WCR, 1);
    K1WdtWrite(Registers, K1X_WDT_WMER, 0);
    WARN("Stopped the SoC watchdog left running by the firmware\n");
}

static
VOID
UefiStopWatchdogsBelow(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Parent,
    _In_ ULONG GrandParent,
    _In_ ULONG Depth)
{
    ULONG Node;

    if (Depth >= UEFI_FDT_MAXIMUM_DEPTH)
        return;

    for (Node = RiscvFdtFirstChild(Fdt, Parent);
         Node != RISCV_FDT_NO_NODE;
         Node = RiscvFdtNextSibling(Fdt, Node))
    {
        if (UefiFdtNodeEnabled(Fdt, Node))
        {
            if (UefiFdtNodeIs(Fdt, Node, K1WdtCompatibles))
                UefiStopK1Watchdog(Fdt, Node, Parent);
            else if (UefiFdtNodeIs(Fdt, Node, P1Compatibles))
                UefiStopP1Watchdog(Fdt, Node, Parent, GrandParent);
        }
        UefiStopWatchdogsBelow(Fdt, Node, Parent, Depth + 1);
    }
}

VOID
UefiStopFirmwareWatchdogs(VOID)
{
    EFI_GUID DeviceTreeGuid = FDT_TABLE_GUID;
    RISCV_FDT Fdt;
    ULONG Root;
    UINTN Index;

    if (!GlobalSystemTable || !GlobalSystemTable->ConfigurationTable)
        return;

    for (Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; ++Index)
    {
        EFI_CONFIGURATION_TABLE *Entry = &GlobalSystemTable->ConfigurationTable[Index];

        if (memcmp(&Entry->VendorGuid, &DeviceTreeGuid, sizeof(DeviceTreeGuid)) ||
            !RiscvFdtOpen(Entry->VendorTable, RISCV_FDT_MAXIMUM_SIZE, &Fdt))
        {
            continue;
        }

        Root = RiscvFdtRootNode(&Fdt);
        if (Root != RISCV_FDT_NO_NODE)
            UefiStopWatchdogsBelow(&Fdt, Root, RISCV_FDT_NO_NODE, 0);
        return;
    }
}
