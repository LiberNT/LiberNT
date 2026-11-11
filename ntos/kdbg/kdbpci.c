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
        return;

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
KdbpPrintPciCapabilities(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ UCHAR First)
{
    PCI_CAPABILITIES_HEADER Header;
    UCHAR Offset = First;
    ULONG Count;

    for (Count = 0; (Count < 48) && (Offset >= PCI_COMMON_HDR_LENGTH); Count++)
    {
        if (!KdbpReadPciConfig(Bus, Slot, Offset & ~3, &Header, sizeof(Header)))
            break;

        KdbpPrint("  cap 0x%02x at 0x%02x\n", Header.CapabilityID, Offset & ~3);

        if (Header.CapabilityID == PCI_CAPABILITY_ID_PCI_EXPRESS)
            KdbpPrintPciExpress(Bus, Slot, Offset & ~3);

        Offset = Header.Next;
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
KdbpPrintPciFunction(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ PPCI_COMMON_CONFIG Config,
    _In_ BOOLEAN Verbose)
{
    ULONG Index;

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
        for (Index = 0; Index < PCI_TYPE0_ADDRESSES; Index++)
        {
            if (Config->u.type0.BaseAddresses[Index] != 0)
                KdbpPrint("  bar%lu 0x%08lx\n", Index, Config->u.type0.BaseAddresses[Index]);
        }

        KdbpPrint("  interrupt line %u pin %u\n",
                  Config->u.type0.InterruptLine,
                  Config->u.type0.InterruptPin);
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

    for (Index = 1; (Index < Argc) && (Index <= 3); Index++)
    {
        if (!KdbpGetHexNumber(Argv[Index], &Value))
        {
            KdbpPrint("%s is not a number\n", Argv[Index]);
            return TRUE;
        }

        Wanted[Index - 1] = (LONG)Value;
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
