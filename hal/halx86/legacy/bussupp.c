/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halx86/legacy/bussupp.c
 * PURPOSE:         HAL Legacy Bus Support Code
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

CODE_SEG("INIT")
PBUS_HANDLER
NTAPI
HalpAllocateAndInitPciBusHandler(
    IN ULONG PciType,
    IN ULONG BusNo,
    IN BOOLEAN TestAllocation
);

CODE_SEG("INIT")
VOID
NTAPI
HalpFixupPciSupportedRanges(
    IN ULONG BusCount
);

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpGetChipHacks(
    IN USHORT VendorId,
    IN USHORT DeviceId,
    IN UCHAR RevisionId,
    IN PULONG HackFlags
);

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpGetPciBridgeConfig(
    IN ULONG PciType,
    IN PUCHAR BusCount
);

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsBridgeDevice(
    IN PPCI_COMMON_CONFIG PciData
);

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsIdeDevice(
    IN PPCI_COMMON_CONFIG PciData
);

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsRecognizedCard(
    IN PPCI_REGISTRY_INFO_INTERNAL PciRegistryInfo,
    IN PPCI_COMMON_CONFIG PciData,
    IN ULONG Flags
);

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsValidPCIDevice(
    IN PBUS_HANDLER BusHandler,
    IN PCI_SLOT_NUMBER Slot
);

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpMarkChipsetDecode(
    IN BOOLEAN OverrideEnable
);

CODE_SEG("INIT")
VOID
NTAPI
HalpRegisterInternalBusHandlers(
    VOID
);

CODE_SEG("INIT")
VOID
NTAPI
ShowSize(
    IN ULONG Size
);

/* GLOBALS ********************************************************************/

extern KSPIN_LOCK HalpPCIConfigLock;
ULONG HalpPciIrqMask;

/* PRIVATE FUNCTIONS **********************************************************/

PBUS_HANDLER
NTAPI
HalpAllocateBusHandler(IN INTERFACE_TYPE InterfaceType,
                       IN BUS_DATA_TYPE BusDataType,
                       IN ULONG BusNumber,
                       IN INTERFACE_TYPE ParentBusInterfaceType,
                       IN ULONG ParentBusNumber,
                       IN ULONG BusSpecificData)
{
    PBUS_HANDLER Bus;

    /* Register the bus handler */
    HalRegisterBusHandler(InterfaceType,
                          BusDataType,
                          BusNumber,
                          ParentBusInterfaceType,
                          ParentBusNumber,
                          BusSpecificData,
                          NULL,
                          &Bus);
    if (!Bus)
    {
        return NULL;
    }

    /* Check for a valid interface */
    if (InterfaceType != InterfaceTypeUndefined)
    {
        /* Allocate address ranges and zero them out */
        Bus->BusAddresses = ExAllocatePoolWithTag(NonPagedPoolMustSucceed,
                                                  sizeof(SUPPORTED_RANGES),
                                                  TAG_HAL);
        RtlZeroMemory(Bus->BusAddresses, sizeof(SUPPORTED_RANGES));

        /* Build the data structure */
        Bus->BusAddresses->Version = HAL_SUPPORTED_RANGE_VERSION;
        Bus->BusAddresses->Dma.Limit = 7;
        Bus->BusAddresses->Memory.Limit = 0xFFFFFFFF;
        Bus->BusAddresses->IO.Limit = 0xFFFF;
        Bus->BusAddresses->IO.SystemAddressSpace = 1;
        Bus->BusAddresses->PrefetchMemory.Base = 1;
    }

    /* Return the bus address */
    return Bus;
}



CODE_SEG("INIT")
VOID
NTAPI
HalpInitializePciBus(VOID)
{
}


NTSTATUS
NTAPI
HalpAssignSlotResources(IN PUNICODE_STRING RegistryPath,
                        IN PUNICODE_STRING DriverClassName,
                        IN PDRIVER_OBJECT DriverObject,
                        IN PDEVICE_OBJECT DeviceObject,
                        IN INTERFACE_TYPE BusType,
                        IN ULONG BusNumber,
                        IN ULONG SlotNumber,
                        IN OUT PCM_RESOURCE_LIST *AllocatedResources)
{
    PBUS_HANDLER Handler;
    NTSTATUS Status;
    PAGED_CODE();
    DPRINT1("Slot assignment for %d on bus %u\n", BusType, BusNumber);

    /* Find the handler */
    Handler = HalReferenceHandlerForBus(BusType, BusNumber);
    if (!Handler) return STATUS_NOT_FOUND;

    /* Do the assignment */
    Status = Handler->AssignSlotResources(Handler,
                                          Handler,
                                          RegistryPath,
                                          DriverClassName,
                                          DriverObject,
                                          DeviceObject,
                                          SlotNumber,
                                          AllocatedResources);

    /* Dereference the handler and return */
    HalDereferenceBusHandler(Handler);
    return Status;
}

BOOLEAN
NTAPI
HaliFindBusAddressTranslation(IN PHYSICAL_ADDRESS BusAddress,
                              IN OUT PULONG AddressSpace,
                              OUT PPHYSICAL_ADDRESS TranslatedAddress,
                              IN OUT PULONG_PTR Context,
                              IN BOOLEAN NextBus)
{
    PHAL_BUS_HANDLER BusHandler;
    PBUS_HANDLER Handler;
    PLIST_ENTRY NextEntry;

    /* Make sure we have a context */
    if (!Context)
        return FALSE;

    /* If we have data in the context, then this shouldn't be a new lookup */
    if ((*Context != 0) && (NextBus != FALSE))
        return FALSE;

    /* Find the bus handler */
    Handler = HalpContextToBusHandler(*Context);
    if (!Handler)
        return FALSE;

    /* Check if this is an ongoing lookup */
    if (NextBus)
    {
        /* Get the HAL bus handler */
        BusHandler = CONTAINING_RECORD(Handler, HAL_BUS_HANDLER, Handler);
        NextEntry = &BusHandler->AllHandlers;

        /* Get the next one if we were already with one */
        if (*Context)
            NextEntry = NextEntry->Flink;

        /* Start scanning */
        while (TRUE)
        {
            /* Check if this is the last one */
            if (NextEntry == &HalpAllBusHandlers)
            {
                /* Quit */
                *Context = 1;
                return FALSE;
            }

            /* Call this translator */
            BusHandler = CONTAINING_RECORD(NextEntry, HAL_BUS_HANDLER, AllHandlers);
            if (HalTranslateBusAddress(BusHandler->Handler.InterfaceType,
                                       BusHandler->Handler.BusNumber,
                                       BusAddress,
                                       AddressSpace,
                                       TranslatedAddress))
            {
                break;
            }

            /* Try the next one */
            NextEntry = NextEntry->Flink;
        }

        /* If we made it, we're done */
        *Context = (ULONG_PTR)&BusHandler->Handler;
        return TRUE;
    }

    /* Try the first one through */
    if (!HalTranslateBusAddress(Handler->InterfaceType,
                                Handler->BusNumber,
                                BusAddress,
                                AddressSpace,
                                TranslatedAddress))
    {
        return FALSE;
    }

    /* Remember for next time */
    *Context = (ULONG_PTR)Handler;
    return TRUE;
}

BOOLEAN
NTAPI
HaliTranslateBusAddress(IN INTERFACE_TYPE InterfaceType,
                        IN ULONG BusNumber,
                        IN PHYSICAL_ADDRESS BusAddress,
                        IN OUT PULONG AddressSpace,
                        OUT PPHYSICAL_ADDRESS TranslatedAddress)
{
    PBUS_HANDLER Handler;
    BOOLEAN Status;

    /* Find the handler */
    Handler = HalReferenceHandlerForBus(InterfaceType, BusNumber);
    if (!(Handler) || !(Handler->TranslateBusAddress))
    {
        DPRINT1("No translator Interface: %x, Bus: %x, Handler: %p, BusAddress: %x!\n", InterfaceType, BusNumber, Handler, BusAddress);
        return FALSE;
    }

    /* Do the assignment */
    Status = Handler->TranslateBusAddress(Handler,
                                          Handler,
                                          BusAddress,
                                          AddressSpace,
                                          TranslatedAddress);

    /* Dereference the handler and return */
    HalDereferenceBusHandler(Handler);
    return Status;
}

/* PUBLIC FUNCTIONS **********************************************************/


/*
 * @implemented
 */
NTSTATUS
NTAPI
HalAssignSlotResources(IN PUNICODE_STRING RegistryPath,
                       IN PUNICODE_STRING DriverClassName,
                       IN PDRIVER_OBJECT DriverObject,
                       IN PDEVICE_OBJECT DeviceObject,
                       IN INTERFACE_TYPE BusType,
                       IN ULONG BusNumber,
                       IN ULONG SlotNumber,
                       IN OUT PCM_RESOURCE_LIST *AllocatedResources)
{
    PAGED_CODE();

    /* Check the bus type */
    if (BusType != PCIBus)
    {
        /* Call our internal handler */
        return HalpAssignSlotResources(RegistryPath,
                                       DriverClassName,
                                       DriverObject,
                                       DeviceObject,
                                       BusType,
                                       BusNumber,
                                       SlotNumber,
                                       AllocatedResources);
    }
    else
    {
        /* Call the PCI registered function */
        return HalPciAssignSlotResources(RegistryPath,
                                         DriverClassName,
                                         DriverObject,
                                         DeviceObject,
                                         PCIBus,
                                         BusNumber,
                                         SlotNumber,
                                         AllocatedResources);
    }
}


/*
 * @implemented
 */
ULONG
NTAPI
HalGetBusDataByOffset(IN BUS_DATA_TYPE BusDataType,
                      IN ULONG BusNumber,
                      IN ULONG SlotNumber,
                      IN PVOID Buffer,
                      IN ULONG Offset,
                      IN ULONG Length)
{
    PBUS_HANDLER Handler;
    ULONG Status;

    /* Find the handler */
    Handler = HaliReferenceHandlerForConfigSpace(BusDataType, BusNumber);
    if (!Handler) return 0;

    /* Do the assignment */
    Status = Handler->GetBusData(Handler,
                                 Handler,
                                 SlotNumber,
                                 Buffer,
                                 Offset,
                                 Length);

    /* Dereference the handler and return */
    HalDereferenceBusHandler(Handler);
    return Status;
}


/*
 * @implemented
 */
BOOLEAN
NTAPI
HalTranslateBusAddress(IN INTERFACE_TYPE InterfaceType,
                       IN ULONG BusNumber,
                       IN PHYSICAL_ADDRESS BusAddress,
                       IN OUT PULONG AddressSpace,
                       OUT PPHYSICAL_ADDRESS TranslatedAddress)
{
    /* Look as the bus type */
    if (InterfaceType == PCIBus)
    {
        /* Call the PCI registered function */
        return HalPciTranslateBusAddress(PCIBus,
                                         BusNumber,
                                         BusAddress,
                                         AddressSpace,
                                         TranslatedAddress);
    }
    else
    {
        /* Call the bus handler */
        return HaliTranslateBusAddress(InterfaceType,
                                       BusNumber,
                                       BusAddress,
                                       AddressSpace,
                                       TranslatedAddress);
    }
}

/* EOF */
