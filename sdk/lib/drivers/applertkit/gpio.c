/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon pin controller: pin functions and GPIO outputs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/applegpio.h>

#define GPIO_DATA           0x00000001
#define GPIO_MODE           0x0000000e
#define GPIO_MODE_OUTPUT    0x00000002
#define GPIO_PERIPH         0x00000060
#define GPIO_PERIPH_SHIFT   5
#define GPIO_INPUT_ENABLE   0x00000200
#define GPIO_MAX_FUNCTION   3

static
BOOLEAN
AppleGpioIsController(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node)
{
    const VOID *Compatible;
    ULONG Length;

    Compatible = FdtGetProperty(&Tree->Fdt, Node, "compatible", &Length);
    return FdtStringListContains(Compatible, Length, "apple,t8103-pinctrl") ||
           FdtStringListContains(Compatible, Length, "apple,pinctrl");
}

static
PUCHAR
AppleGpioMapController(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Controller,
    _Out_ PULONG Size)
{
    ULONG Pins;

    *Size = 0;
    if (!AppleGpioIsController(Tree, Controller) || !NT_SUCCESS(ApplePmgrPowerOnDevice(Tree, Controller)))
        return NULL;
    if (!FdtReadU32(&Tree->Fdt, Controller, "apple,npins", &Pins))
        return NULL;
    return FdtDriverMapReg(Tree, Controller, 0, NULL, Size);
}

static
VOID
AppleGpioModify(
    _In_ PUCHAR Registers,
    _In_ ULONG Pin,
    _In_ ULONG Mask,
    _In_ ULONG Value)
{
    volatile ULONG *Register = (volatile ULONG *)(Registers + Pin * sizeof(ULONG));

    WRITE_REGISTER_ULONG(Register, (READ_REGISTER_ULONG(Register) & ~Mask) | (Value & Mask));
}

NTSTATUS
NTAPI
AppleGpioApplyPinctrl(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node)
{
    const VOID *States, *Pinmux;
    ULONG StatesLength, PinmuxLength, State, Controller, Size, Pins, Index, Entry;
    ULONG64 Value;
    PUCHAR Registers;

    States = FdtGetProperty(&Tree->Fdt, Node, "pinctrl-0", &StatesLength);
    for (Index = 0; States && FdtReadCells(States, StatesLength, Index, 1, &Value); ++Index)
    {
        if ((State = FdtDriverFindPhandle(Tree, (ULONG)Value)) == FDT_NO_NODE ||
            (Controller = FdtDriverFindParentNode(Tree, State)) == FDT_NO_NODE ||
            !FdtReadU32(&Tree->Fdt, Controller, "apple,npins", &Pins) ||
            (Registers = AppleGpioMapController(Tree, Controller, &Size)) == NULL)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Pinmux = FdtGetProperty(&Tree->Fdt, State, "pinmux", &PinmuxLength);
        for (Entry = 0; Pinmux && FdtReadCells(Pinmux, PinmuxLength, Entry, 1, &Value); ++Entry)
        {
            ULONG Pin = (ULONG)Value & 0xffff, Function = (ULONG)Value >> 16;

            if (Pin >= Pins || (Pin + 1) * sizeof(ULONG) > Size || Function > GPIO_MAX_FUNCTION)
            {
                MmUnmapIoSpace(Registers, Size);
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            AppleGpioModify(Registers, Pin, GPIO_PERIPH | GPIO_INPUT_ENABLE,
                            (Function << GPIO_PERIPH_SHIFT) | GPIO_INPUT_ENABLE);
        }
        MmUnmapIoSpace(Registers, Size);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleGpioOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Property,
    _Out_ PAPPLE_GPIO Gpio)
{
    const VOID *Specifier;
    ULONG Length, Controller, Cells, Pins;
    ULONG64 Phandle, Pin, Flags;

    RtlZeroMemory(Gpio, sizeof(*Gpio));
    Specifier = FdtGetProperty(&Tree->Fdt, Node, Property, &Length);
    if (!Specifier || !FdtReadCells(Specifier, Length, 0, 1, &Phandle))
        return STATUS_NOT_FOUND;
    if ((Controller = FdtDriverFindPhandle(Tree, (ULONG)Phandle)) == FDT_NO_NODE ||
        !FdtReadU32(&Tree->Fdt, Controller, "#gpio-cells", &Cells) || Cells != 2 ||
        !FdtReadU32(&Tree->Fdt, Controller, "apple,npins", &Pins) ||
        !FdtReadCells(Specifier, Length, 1, 1, &Pin) ||
        !FdtReadCells(Specifier, Length, 2, 1, &Flags) || Pin >= Pins)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Gpio->Registers = AppleGpioMapController(Tree, Controller, &Gpio->Size);
    if (!Gpio->Registers)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if ((Pin + 1) * sizeof(ULONG) > Gpio->Size)
    {
        AppleGpioClose(Gpio);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Gpio->Pin = (ULONG)Pin;
    Gpio->ActiveLow = (Flags & 1) != 0;
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleGpioClose(
    _Inout_ PAPPLE_GPIO Gpio)
{
    if (Gpio->Registers)
        MmUnmapIoSpace(Gpio->Registers, Gpio->Size);
    RtlZeroMemory(Gpio, sizeof(*Gpio));
}

VOID
NTAPI
AppleGpioSet(
    _In_ PAPPLE_GPIO Gpio,
    _In_ BOOLEAN Asserted)
{
    BOOLEAN High = Asserted != Gpio->ActiveLow;

    if (!Gpio->Registers)
        return;
    AppleGpioModify(Gpio->Registers, Gpio->Pin, GPIO_PERIPH | GPIO_MODE | GPIO_DATA,
                    GPIO_MODE_OUTPUT | (High ? GPIO_DATA : 0));
}
