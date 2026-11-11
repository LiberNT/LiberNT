/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The PCI functions of the machine as the debugger sees them: !pci
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#define NDEBUG
#include <debug.h>

#define KDB_PCI_MSI_ENABLE 0x0001
#define KDB_PCI_MSI_CAPABLE_SHIFT 1
#define KDB_PCI_MSI_ENABLED_SHIFT 4
#define KDB_PCI_MSI_COUNT_MASK 0x7
#define KDB_PCI_MSI_64BIT 0x0080
#define KDB_PCI_MSI_MASKABLE 0x0100
#define KDB_PCI_MSIX_TABLE_SIZE_MASK 0x07FF
#define KDB_PCI_MSIX_FUNCTION_MASK 0x4000
#define KDB_PCI_MSIX_ENABLE 0x8000
#define KDB_PCI_MSIX_BIR_MASK 0x7UL

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
KdbpReadPciConfig(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    ULONG Read = 0;

    return NT_SUCCESS(KdpSysReadBusData(PCIConfiguration,
                                        Bus,
                                        Slot.u.AsULONG,
                                        Offset,
                                        Buffer,
                                        Length,
                                        &Read)) &&
           (Read == Length);
}

static
VOID
KdbpPrintPciExpress(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ ULONG Offset)
{
    PCI_EXPRESS_CAPABILITY Express;

    if (!KdbpReadPciConfig(Bus, Slot, Offset, &Express, FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, SlotCapabilities)))
    {
        KdbPuts("\n");
        return;
    }

    KdbpPrint(" PCI Express v%u type %u\n",
              Express.ExpressCapabilities.CapabilityVersion,
              Express.ExpressCapabilities.DeviceType);
    KdbpPrint("  PCIe devctl 0x%04x devsta 0x%04x lnkcap 0x%08lx lnkctl 0x%04x lnksta 0x%04x (gen%u x%u)\n",
              Express.DeviceControl.AsUSHORT,
              Express.DeviceStatus.AsUSHORT,
              Express.LinkCapabilities.AsULONG,
              Express.LinkControl.AsUSHORT,
              Express.LinkStatus.AsUSHORT,
              Express.LinkStatus.LinkSpeed,
              Express.LinkStatus.LinkWidth);
}

static
VOID
KdbpPrintPciPowerManagement(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ ULONG Offset)
{
    PCI_PM_CAPABILITY Pm;

    if (!KdbpReadPciConfig(Bus, Slot, Offset, &Pm, sizeof(Pm)))
    {
        KdbPuts("\n");
        return;
    }

    KdbpPrint(" power management v%u, D%u, PME enable %u status %u\n",
              Pm.PMC.Capabilities.Version,
              Pm.PMCSR.ControlStatus.PowerState,
              Pm.PMCSR.ControlStatus.PMEEnable,
              Pm.PMCSR.ControlStatus.PMEStatus);
}

static
VOID
KdbpPrintPciMsi(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ ULONG Offset)
{
    USHORT Control;

    if (!KdbpReadPciConfig(Bus, Slot, Offset + 2, &Control, sizeof(Control)))
    {
        KdbPuts("\n");
        return;
    }

    KdbpPrint(" MSI %s, %u of %u messages%s%s\n",
              (Control & KDB_PCI_MSI_ENABLE) ? "enabled" : "disabled",
              1U << ((Control >> KDB_PCI_MSI_ENABLED_SHIFT) & KDB_PCI_MSI_COUNT_MASK),
              1U << ((Control >> KDB_PCI_MSI_CAPABLE_SHIFT) & KDB_PCI_MSI_COUNT_MASK),
              (Control & KDB_PCI_MSI_64BIT) ? ", 64-bit" : "",
              (Control & KDB_PCI_MSI_MASKABLE) ? ", maskable" : "");
}

static
VOID
KdbpPrintPciMsix(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ ULONG Offset)
{
    USHORT Control;
    ULONG Table;
    ULONG Pending;

    if (!KdbpReadPciConfig(Bus, Slot, Offset + 2, &Control, sizeof(Control)) ||
        !KdbpReadPciConfig(Bus, Slot, Offset + 4, &Table, sizeof(Table)) ||
        !KdbpReadPciConfig(Bus, Slot, Offset + 8, &Pending, sizeof(Pending)))
    {
        KdbPuts("\n");
        return;
    }

    KdbpPrint(" MSI-X %s%s, %u entries, table bar%lu+0x%lx, pending bits bar%lu+0x%lx\n",
              (Control & KDB_PCI_MSIX_ENABLE) ? "enabled" : "disabled",
              (Control & KDB_PCI_MSIX_FUNCTION_MASK) ? " masked" : "",
              (Control & KDB_PCI_MSIX_TABLE_SIZE_MASK) + 1,
              Table & KDB_PCI_MSIX_BIR_MASK,
              Table & ~KDB_PCI_MSIX_BIR_MASK,
              Pending & KDB_PCI_MSIX_BIR_MASK,
              Pending & ~KDB_PCI_MSIX_BIR_MASK);
}

static
VOID
KdbpPrintPciCapabilities(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ UCHAR First)
{
    PCI_CAPABILITIES_HEADER Header;
    ULONG Offset = First & ~3;
    ULONG Count;

    for (Count = 0; (Count < 48) && (Offset >= PCI_COMMON_HDR_LENGTH); Count++)
    {
        if (!KdbpReadPciConfig(Bus, Slot, Offset, &Header, sizeof(Header)))
            break;

        KdbpPrint("  cap 0x%02x at 0x%02lx", Header.CapabilityID, Offset);

        switch (Header.CapabilityID)
        {
            case PCI_CAPABILITY_ID_POWER_MANAGEMENT:
                KdbpPrintPciPowerManagement(Bus, Slot, Offset);
                break;

            case PCI_CAPABILITY_ID_MSI:
                KdbpPrintPciMsi(Bus, Slot, Offset);
                break;

            case PCI_CAPABILITY_ID_MSIX:
                KdbpPrintPciMsix(Bus, Slot, Offset);
                break;

            case PCI_CAPABILITY_ID_PCI_EXPRESS:
                KdbpPrintPciExpress(Bus, Slot, Offset);
                break;

            default:
                KdbPuts("\n");
                break;
        }

        Offset = Header.Next & ~3;
    }
}

static
VOID
KdbpPrintPciExtendedCapabilities(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot)
{
    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;
    PCI_EXPRESS_AER_CAPABILITY Aer;
    ULONG Offset = 0x100;
    ULONG Count;

    for (Count = 0; (Count < 64) && (Offset >= 0x100) && (Offset < 0x1000); Count++)
    {
        if (!KdbpReadPciConfig(Bus, Slot, Offset, &Header, sizeof(Header)) ||
            (Header.CapabilityID == 0) || (Header.CapabilityID == 0xFFFF))
        {
            break;
        }

        KdbpPrint("  ext cap 0x%04x v%u at 0x%03lx\n", Header.CapabilityID, Header.Version, Offset);

        if ((Header.CapabilityID == PCI_EXPRESS_ADVANCED_ERROR_REPORTING_CAP_ID) &&
            KdbpReadPciConfig(Bus, Slot, Offset, &Aer, FIELD_OFFSET(PCI_EXPRESS_AER_CAPABILITY, CapabilitiesAndControl)))
        {
            KdbpPrint("  AER uncor-sts 0x%08lx mask 0x%08lx sev 0x%08lx cor-sts 0x%08lx mask 0x%08lx\n",
                      Aer.UncorrectableErrorStatus.AsULONG,
                      Aer.UncorrectableErrorMask.AsULONG,
                      Aer.UncorrectableErrorSeverity.AsULONG,
                      Aer.CorrectableErrorStatus.AsULONG,
                      Aer.CorrectableErrorMask.AsULONG);
        }

        Offset = Header.Next;
    }
}

static
VOID
KdbpPrintPciBars(
    _In_reads_(Count) const ULONG *Bars,
    _In_ ULONG Count)
{
    ULONGLONG Address;
    ULONG Index;
    ULONG Bar;

    for (Index = 0; Index < Count; Index++)
    {
        if (Bars[Index] == 0)
            continue;

        if (Bars[Index] & PCI_ADDRESS_IO_SPACE)
        {
            KdbpPrint("  bar%lu I/O 0x%lx\n", Index, Bars[Index] & PCI_ADDRESS_IO_ADDRESS_MASK);
            continue;
        }

        Bar = Index;
        Address = Bars[Index] & PCI_ADDRESS_MEMORY_ADDRESS_MASK;

        if (((Bars[Bar] & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT) && (Index + 1 < Count))
            Address |= (ULONGLONG)Bars[++Index] << 32;

        KdbpPrint("  bar%lu memory 0x%I64x%s%s\n",
                  Bar,
                  Address,
                  (Index != Bar) ? " 64-bit" : "",
                  (Bars[Bar] & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? " prefetchable" : "");
    }
}

static
BOOLEAN
KdbpParsePciAddress(
    _In_ PCHAR Text,
    _Out_writes_(3) LONG Wanted[3])
{
    PCHAR Device = strchr(Text, ':');
    PCHAR Function = (Device != NULL) ? strchr(Device, '.') : NULL;
    ULONG_PTR Value;

    if (Function == NULL)
        return FALSE;

    *Device++ = ANSI_NULL;
    *Function++ = ANSI_NULL;

    if (!KdbpGetHexNumber(Text, &Value) || (Value > 0xFF))
        return FALSE;
    Wanted[0] = (LONG)Value;

    if (!KdbpGetHexNumber(Device, &Value) || (Value >= PCI_MAX_DEVICES))
        return FALSE;
    Wanted[1] = (LONG)Value;

    if (!KdbpGetHexNumber(Function, &Value) || (Value >= PCI_MAX_FUNCTION))
        return FALSE;
    Wanted[2] = (LONG)Value;

    return TRUE;
}

static
VOID
KdbpPrintPciFunction(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ PPCI_COMMON_CONFIG Config,
    _In_ BOOLEAN Verbose)
{
    KdbpPrint("%02lx:%02lx.%lx %04x:%04x class %02x%02x%02x rev %02x hdr %02x\n",
              Bus,
              Slot.u.bits.DeviceNumber,
              Slot.u.bits.FunctionNumber,
              Config->VendorID,
              Config->DeviceID,
              Config->BaseClass,
              Config->SubClass,
              Config->ProgIf,
              Config->RevisionID,
              Config->HeaderType);

    if (!Verbose)
        return;

    KdbpPrint("  command 0x%04x status 0x%04x\n", Config->Command, Config->Status);

    if (PCI_CONFIGURATION_TYPE(Config) == PCI_DEVICE_TYPE)
    {
        KdbpPrintPciBars(Config->u.type0.BaseAddresses, PCI_TYPE0_ADDRESSES);
        KdbpPrint("  subsystem %04x:%04x\n", Config->u.type0.SubVendorID, Config->u.type0.SubSystemID);
        KdbpPrint("  interrupt line %u pin %u\n",
                  Config->u.type0.InterruptLine,
                  Config->u.type0.InterruptPin);
    }
    else if (PCI_CONFIGURATION_TYPE(Config) == PCI_BRIDGE_TYPE)
    {
        KdbpPrintPciBars(Config->u.type1.BaseAddresses, PCI_TYPE1_ADDRESSES);
        KdbpPrint("  buses %u-%u-%u\n",
                  Config->u.type1.PrimaryBus,
                  Config->u.type1.SecondaryBus,
                  Config->u.type1.SubordinateBus);
        KdbpPrint("  interrupt line %u pin %u\n",
                  Config->u.type1.InterruptLine,
                  Config->u.type1.InterruptPin);
    }

    if (Config->Status & PCI_STATUS_CAPABILITIES_LIST)
    {
        KdbpPrintPciCapabilities(Bus,
                                 Slot,
                                 (PCI_CONFIGURATION_TYPE(Config) == PCI_BRIDGE_TYPE) ?
                                     Config->u.type1.CapabilitiesPtr : Config->u.type0.CapabilitiesPtr);
    }

    KdbpPrintPciExtendedCapabilities(Bus, Slot);
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
KdbpCmdPci(
    ULONG Argc,
    PCHAR Argv[])
{
    PCI_COMMON_CONFIG Config;
    PCI_SLOT_NUMBER Slot;
    ULONG_PTR Value;
    ULONG FirstBus = 0;
    ULONG LastBus = 255;
    ULONG Device;
    ULONG Function;
    ULONG Bus;
    LONG Wanted[3] = { -1, -1, -1 };
    ULONG Index;

    if ((Argc == 2) && (strchr(Argv[1], ':') != NULL))
    {
        if (!KdbpParsePciAddress(Argv[1], Wanted))
        {
            KdbPuts("Give the function as bus:device.function, in hexadecimal\n");
            return TRUE;
        }
    }
    else
    {
        for (Index = 1; (Index < Argc) && (Index <= 3); Index++)
        {
            if (!KdbpGetHexNumber(Argv[Index], &Value))
            {
                KdbpPrint("%s is not a number\n", Argv[Index]);
                return TRUE;
            }

            Wanted[Index - 1] = (LONG)Value;
        }
    }

    if (Wanted[0] >= 0)
        FirstBus = LastBus = (ULONG)Wanted[0];

    for (Bus = FirstBus; (Bus <= LastBus) && !KdbpIsOutputAborted(); Bus++)
    {
        for (Device = 0; (Device < PCI_MAX_DEVICES) && !KdbpIsOutputAborted(); Device++)
        {
            if ((Wanted[1] >= 0) && (Device != (ULONG)Wanted[1]))
                continue;

            for (Function = 0; Function < PCI_MAX_FUNCTION; Function++)
            {
                if ((Wanted[2] >= 0) && (Function != (ULONG)Wanted[2]))
                    continue;

                Slot.u.AsULONG = 0;
                Slot.u.bits.DeviceNumber = Device;
                Slot.u.bits.FunctionNumber = Function;

                if (!KdbpReadPciConfig(Bus, Slot, 0, &Config, PCI_COMMON_HDR_LENGTH) ||
                    (Config.VendorID == PCI_INVALID_VENDORID) || (Config.VendorID == 0))
                {
                    if (Function == 0)
                        break;

                    continue;
                }

                KdbpPrintPciFunction(Bus, Slot, &Config, (Wanted[1] >= 0));

                if ((Function == 0) && !(Config.HeaderType & PCI_MULTIFUNCTION))
                    break;
            }
        }
    }

    return TRUE;
}

/* EOF */
