/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon pin controller: pin functions, GPIO outputs and GPIO interrupts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/applegpio.h>
#include <reactos/drivers/appleadmac.h>

#define GPIO_DATA           0x00000001
#define GPIO_MODE           0x0000000e
#define GPIO_MODE_OUTPUT    0x00000002
#define GPIO_PERIPH         0x00000060
#define GPIO_PERIPH_SHIFT   5
#define GPIO_INPUT_ENABLE   0x00000200
#define GPIO_MAX_FUNCTION   3

#define GPIO_MODE_SHIFT         1
#define GPIO_MODE_IRQ_HIGH      2
#define GPIO_MODE_IRQ_LOW       3
#define GPIO_MODE_IRQ_RISING    4
#define GPIO_MODE_IRQ_FALLING   5
#define GPIO_MODE_IRQ_BOTH      6
#define GPIO_MODE_IRQ_OFF       7
#define GPIO_GROUP              0x00070000
#define GPIO_GROUP_SHIFT        16
#define GPIO_MAX_GROUPS         8
#define GPIO_IRQ_PENDING(Group, Pin) (0x800 + 0x40 * (Group) + 4 * ((Pin) >> 5))
#define GPIO_MAX_IRQ_CELLS      4

#define DT_IRQ_TYPE_EDGE_RISING     1
#define DT_IRQ_TYPE_EDGE_FALLING    2
#define DT_IRQ_TYPE_EDGE_BOTH       3
#define DT_IRQ_TYPE_LEVEL_HIGH      4
#define DT_IRQ_TYPE_LEVEL_LOW       8

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

static
ULONG
AppleGpioInterruptParent(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node)
{
    ULONG Phandle;

    for (; Node != FDT_NO_NODE; Node = FdtDriverFindParentNode(Tree, Node))
    {
        if (FdtReadU32(&Tree->Fdt, Node, "interrupt-parent", &Phandle))
            return FdtDriverFindPhandle(Tree, Phandle);
    }
    return FDT_NO_NODE;
}

static
NTSTATUS
AppleGpioFindInterrupt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _Out_ PULONG Controller,
    _Out_ PULONG Pin,
    _Out_ PULONG Mode)
{
    const VOID *Property;
    ULONG Length, Cell = 0, Entry, Cells = 0, Pins, Parent = FDT_NO_NODE;
    ULONG64 Value, Type;

    *Controller = FDT_NO_NODE;
    *Pin = 0;
    *Mode = GPIO_MODE_IRQ_OFF;
    Property = FdtGetProperty(&Tree->Fdt, Node, "interrupts-extended", &Length);
    if (Property)
    {
        for (Entry = 0;; ++Entry)
        {
            if (!FdtReadCells(Property, Length, Cell, 1, &Value) ||
                (Parent = FdtDriverFindPhandle(Tree, (ULONG)Value)) == FDT_NO_NODE ||
                !FdtReadU32(&Tree->Fdt, Parent, "#interrupt-cells", &Cells) || !Cells ||
                Cells > GPIO_MAX_IRQ_CELLS)
            {
                return STATUS_NOT_FOUND;
            }
            ++Cell;
            if (Entry == Index)
                break;
            Cell += Cells;
        }
    }
    else
    {
        Property = FdtGetProperty(&Tree->Fdt, Node, "interrupts", &Length);
        Parent = AppleGpioInterruptParent(Tree, Node);
        if (!Property || Parent == FDT_NO_NODE || !FdtReadU32(&Tree->Fdt, Parent, "#interrupt-cells", &Cells) ||
            !Cells || Cells > GPIO_MAX_IRQ_CELLS)
        {
            return STATUS_NOT_FOUND;
        }
        Cell = Index * Cells;
    }
    if (!AppleGpioIsController(Tree, Parent) || Cells != 2 ||
        !FdtReadU32(&Tree->Fdt, Parent, "apple,npins", &Pins) ||
        !FdtReadCells(Property, Length, Cell, 1, &Value) || !FdtReadCells(Property, Length, Cell + 1, 1, &Type))
    {
        return STATUS_NOT_FOUND;
    }
    if (Value >= Pins)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    switch ((ULONG)Type)
    {
        case DT_IRQ_TYPE_EDGE_RISING:
            *Mode = GPIO_MODE_IRQ_RISING;
            break;
        case DT_IRQ_TYPE_EDGE_FALLING:
            *Mode = GPIO_MODE_IRQ_FALLING;
            break;
        case DT_IRQ_TYPE_EDGE_BOTH:
            *Mode = GPIO_MODE_IRQ_BOTH;
            break;
        case DT_IRQ_TYPE_LEVEL_HIGH:
            *Mode = GPIO_MODE_IRQ_HIGH;
            break;
        case DT_IRQ_TYPE_LEVEL_LOW:
            *Mode = GPIO_MODE_IRQ_LOW;
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }
    *Controller = Parent;
    *Pin = (ULONG)Value;
    return STATUS_SUCCESS;
}

static
ULONG
AppleGpioGroupCount(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Controller)
{
    ULONG Parent = AppleGpioInterruptParent(Tree, Controller), Cells, Length;

    if (Parent == FDT_NO_NODE || !FdtReadU32(&Tree->Fdt, Parent, "#interrupt-cells", &Cells) || !Cells ||
        !FdtGetProperty(&Tree->Fdt, Controller, "interrupts", &Length))
    {
        return 0;
    }
    return min(Length / (Cells * (ULONG)sizeof(ULONG)), GPIO_MAX_GROUPS);
}

static
NTSTATUS
AppleGpioGroupInterrupt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Controller,
    _In_ ULONG Group,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered)
{
    const VOID *Interrupts;
    const CHAR *Compatible;
    ULONG Parent = AppleGpioInterruptParent(Tree, Controller), CellCount, Cell, Length, CompatibleLength;
    ULONG Cells[GPIO_MAX_IRQ_CELLS];
    ULONG64 Value;

    *Gsi = 0;
    *EdgeTriggered = FALSE;
    if (Parent == FDT_NO_NODE || !FdtReadU32(&Tree->Fdt, Parent, "#interrupt-cells", &CellCount) || !CellCount ||
        CellCount > GPIO_MAX_IRQ_CELLS)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Interrupts = FdtGetProperty(&Tree->Fdt, Controller, "interrupts", &Length);
    for (Cell = 0; Cell < CellCount; ++Cell)
    {
        if (!Interrupts || !FdtReadCells(Interrupts, Length, Group * CellCount + Cell, 1, &Value))
            return STATUS_NOT_FOUND;
        Cells[Cell] = (ULONG)Value;
    }
    Compatible = FdtGetProperty(&Tree->Fdt, Parent, "compatible", &CompatibleLength);
    if (!Compatible ||
        !HalArm64TranslateDeviceTreeInterrupt(Compatible, CompatibleLength, Cells, CellCount, Gsi, EdgeTriggered))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleGpioQueryInterrupt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered)
{
    ULONG Controller, Pin, Mode, Size, Groups, Group, Other, Pins, Value, Busy = 0;
    PUCHAR Registers;
    NTSTATUS Status;

    *Gsi = 0;
    *EdgeTriggered = FALSE;
    Status = AppleGpioFindInterrupt(Tree, Node, Index, &Controller, &Pin, &Mode);
    if (!NT_SUCCESS(Status))
        return Status;
    Groups = AppleGpioGroupCount(Tree, Controller);
    if (!Groups || !FdtReadU32(&Tree->Fdt, Controller, "apple,npins", &Pins))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Registers = AppleGpioMapController(Tree, Controller, &Size);
    if (!Registers)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (Pins * sizeof(ULONG) > Size)
    {
        MmUnmapIoSpace(Registers, Size);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (Other = 0; Other < Pins; ++Other)
    {
        if (Other == Pin)
            continue;
        Value = READ_REGISTER_ULONG((volatile ULONG *)(Registers + Other * sizeof(ULONG)));
        Mode = (Value & GPIO_MODE) >> GPIO_MODE_SHIFT;
        if (Mode >= GPIO_MODE_IRQ_HIGH && Mode <= GPIO_MODE_IRQ_BOTH)
            Busy |= 1UL << ((Value & GPIO_GROUP) >> GPIO_GROUP_SHIFT);
    }
    MmUnmapIoSpace(Registers, Size);
    for (Group = 0; Group < Groups && (Busy & (1UL << Group)); ++Group)
        ;
    if (Group == Groups)
        return STATUS_INSUFFICIENT_RESOURCES;
    return AppleGpioGroupInterrupt(Tree, Controller, Group, Gsi, EdgeTriggered);
}

NTSTATUS
NTAPI
AppleGpioOpenInterrupt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _In_ ULONG Gsi,
    _Out_ PAPPLE_GPIO_INTERRUPT Interrupt)
{
    ULONG Controller, Pin, Mode, Groups, Group, GroupGsi;
    BOOLEAN Edge = FALSE;
    NTSTATUS Status;

    RtlZeroMemory(Interrupt, sizeof(*Interrupt));
    Status = AppleGpioFindInterrupt(Tree, Node, Index, &Controller, &Pin, &Mode);
    if (!NT_SUCCESS(Status))
        return Status;
    Groups = AppleGpioGroupCount(Tree, Controller);
    for (Group = 0; Group < Groups; ++Group)
    {
        if (NT_SUCCESS(AppleGpioGroupInterrupt(Tree, Controller, Group, &GroupGsi, &Edge)) && GroupGsi == Gsi)
            break;
    }
    if (Group == Groups)
        return STATUS_NOT_FOUND;
    Interrupt->Registers = AppleGpioMapController(Tree, Controller, &Interrupt->Size);
    if (!Interrupt->Registers)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if ((Pin + 1) * sizeof(ULONG) > Interrupt->Size || GPIO_IRQ_PENDING(Group, Pin) + sizeof(ULONG) > Interrupt->Size)
    {
        AppleGpioCloseInterrupt(Interrupt);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Interrupt->Pin = Pin;
    Interrupt->Group = Group;
    Interrupt->Mode = Mode;
    Interrupt->Gsi = Gsi;
    Interrupt->EdgeTriggered = Edge;
    AppleGpioModify(Interrupt->Registers, Pin, GPIO_PERIPH | GPIO_MODE | GPIO_DATA | GPIO_INPUT_ENABLE | GPIO_GROUP,
                    (GPIO_MODE_IRQ_OFF << GPIO_MODE_SHIFT) | GPIO_INPUT_ENABLE | (Group << GPIO_GROUP_SHIFT));
    WRITE_REGISTER_ULONG((volatile ULONG *)(Interrupt->Registers + GPIO_IRQ_PENDING(Group, Pin)), 1UL << (Pin & 31));
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleGpioCloseInterrupt(
    _Inout_ PAPPLE_GPIO_INTERRUPT Interrupt)
{
    if (Interrupt->Registers && Interrupt->Mode)
    {
        AppleGpioMaskInterrupt(Interrupt);
        WRITE_REGISTER_ULONG((volatile ULONG *)(Interrupt->Registers + GPIO_IRQ_PENDING(Interrupt->Group,
                                                                                      Interrupt->Pin)),
                             1UL << (Interrupt->Pin & 31));
    }
    if (Interrupt->Registers)
        MmUnmapIoSpace(Interrupt->Registers, Interrupt->Size);
    RtlZeroMemory(Interrupt, sizeof(*Interrupt));
}

VOID
NTAPI
AppleGpioMaskInterrupt(
    _In_ PAPPLE_GPIO_INTERRUPT Interrupt)
{
    ASSERT(Interrupt->Registers);
    AppleGpioModify(Interrupt->Registers, Interrupt->Pin, GPIO_MODE, GPIO_MODE_IRQ_OFF << GPIO_MODE_SHIFT);
}

VOID
NTAPI
AppleGpioUnmaskInterrupt(
    _In_ PAPPLE_GPIO_INTERRUPT Interrupt)
{
    ASSERT(Interrupt->Registers && Interrupt->Mode >= GPIO_MODE_IRQ_HIGH && Interrupt->Mode <= GPIO_MODE_IRQ_BOTH);
    AppleGpioModify(Interrupt->Registers, Interrupt->Pin, GPIO_MODE, Interrupt->Mode << GPIO_MODE_SHIFT);
}

VOID
NTAPI
AppleGpioTestInterrupt(
    _In_ PAPPLE_GPIO_INTERRUPT Interrupt)
{
    ULONG Value;

    ASSERT(Interrupt->Registers);
    Value = READ_REGISTER_ULONG((volatile ULONG *)(Interrupt->Registers + Interrupt->Pin * sizeof(ULONG)));
    AppleGpioModify(Interrupt->Registers, Interrupt->Pin, GPIO_MODE,
                    ((Value & GPIO_DATA) ? GPIO_MODE_IRQ_HIGH : GPIO_MODE_IRQ_LOW) << GPIO_MODE_SHIFT);
}

BOOLEAN
NTAPI
AppleGpioClaimInterrupt(
    _In_ PAPPLE_GPIO_INTERRUPT Interrupt)
{
    volatile ULONG *Pending;
    ULONG Bit = 1UL << (Interrupt->Pin & 31);

    ASSERT(Interrupt->Registers);
    Pending = (volatile ULONG *)(Interrupt->Registers + GPIO_IRQ_PENDING(Interrupt->Group, Interrupt->Pin));
    if (!(READ_REGISTER_ULONG(Pending) & Bit))
        return FALSE;
    AppleGpioMaskInterrupt(Interrupt);
    WRITE_REGISTER_ULONG(Pending, Bit);
    return TRUE;
}
