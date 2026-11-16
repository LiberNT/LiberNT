/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/legacy/bus/bushndlr.c
 * PURPOSE:         Generic HAL Bus Handler Support
 * PROGRAMMERS:     Stefan Ginsberg (stefan.ginsberg@reactos.org)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

KSPIN_LOCK HalpBusDatabaseSpinLock;
KEVENT HalpBusDatabaseEvent;
LIST_ENTRY HalpAllBusHandlers;
PARRAY HalpBusTable;
PARRAY HalpConfigTable;

/* PRIVATE FUNCTIONS **********************************************************/

PARRAY
NTAPI
HalpAllocateArray(IN ULONG ArraySize)
{
    PARRAY Array;
    ULONG Size;

    /* Compute array size */
    if (ArraySize == MAXULONG) ArraySize = 0;
    Size = ArraySize * sizeof(PARRAY) + sizeof(ARRAY);

    /* Allocate the array */
    Array = ExAllocatePoolWithTag(NonPagedPool,
                                  Size,
                                  TAG_BUS_HANDLER);
    if (!Array) KeBugCheckEx(HAL_MEMORY_ALLOCATION, Size, 0, (ULONG_PTR)__FILE__, __LINE__);

    /* Initialize it */
    Array->ArraySize = ArraySize;
    RtlZeroMemory(Array->Element, sizeof(PVOID) * (ArraySize + 1));
    return Array;
}

VOID
NTAPI
HalpGrowArray(IN PARRAY *CurrentArray,
              IN PARRAY *NewArray)
{
    PVOID Tmp;

    /* Check if the current array doesn't exist yet, or if it's smaller than the new one */
    if (!(*CurrentArray) || ((*NewArray)->ArraySize > (*CurrentArray)->ArraySize))
    {
        /* Does it exist (and can it fit?) */
        if (*CurrentArray)
        {
            /* Copy the current array into the new one */
            RtlCopyMemory(&(*NewArray)->Element,
                          &(*CurrentArray)->Element,
                          sizeof(PVOID) * ((*CurrentArray)->ArraySize + 1));
        }

        /* Swap the pointers (XOR swap would be more l33t) */
        Tmp = *CurrentArray;
        *CurrentArray = *NewArray;
        *NewArray = Tmp;
    }
}

PBUS_HANDLER
FASTCALL
HalpLookupHandler(IN PARRAY Array,
                  IN ULONG Type,
                  IN ULONG Number,
                  IN BOOLEAN AddReference)
{
    PHAL_BUS_HANDLER Bus;
    PBUS_HANDLER Handler = NULL;

    /* Make sure the entry exists */
    if (Array->ArraySize >= Type)
    {
        /* Retrieve it */
        Array = Array->Element[Type];

        /* Make sure the entry array exists */
        if ((Array) && (Array->ArraySize >= Number))
        {
            /* Retrieve the bus and its handler */
            Bus = Array->Element[Number];
            Handler = &Bus->Handler;

            /* Reference the handler if needed */
            if (AddReference) Bus->ReferenceCount++;
        }
    }

    /* Return the handler */
    return Handler;
}

/**
 * @brief   Handler for buses without configuration space.
 **/
ULONG
NTAPI
HalpNoBusData(
    _In_ PBUS_HANDLER BusHandler,
    _In_ PBUS_HANDLER RootHandler,
    _In_ ULONG SlotNumber,
    _In_ PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    DPRINT1("STUB GetSetBusData(Bus %x:%x.%x)\n",
            BusHandler->InterfaceType,
            BusHandler->ConfigurationType,
            BusHandler->BusNumber);

    /* Just return a dummy value */
    return 0;
}

/**
 * @brief   Handler for buses without configuration space.
 **/
NTSTATUS
NTAPI
HalpNoAdjustResourceList(
    _In_ PBUS_HANDLER BusHandler,
    _In_ PBUS_HANDLER RootHandler,
    _Inout_ PIO_RESOURCE_REQUIREMENTS_LIST* pResourceList)
{
    DPRINT1("STUB Adjustment(Bus %x:%x.%x)\n",
            BusHandler->InterfaceType,
            BusHandler->ConfigurationType,
            BusHandler->BusNumber);

    return STATUS_UNSUCCESSFUL;
}

/**
 * @brief   Handler for buses without configuration space.
 **/
NTSTATUS
NTAPI
HalpNoAssignSlotResources(
    _In_ PBUS_HANDLER BusHandler,
    _In_ PBUS_HANDLER RootHandler,
    _In_ PUNICODE_STRING RegistryPath,
    _In_opt_ PUNICODE_STRING DriverClassName,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SlotNumber,
    _Inout_ PCM_RESOURCE_LIST* AllocatedResources)
{
    DPRINT1("STUB Assignment(Bus %x:%x.%x)\n",
            BusHandler->InterfaceType,
            BusHandler->ConfigurationType,
            BusHandler->BusNumber);

    return STATUS_NOT_SUPPORTED;
}

VOID
FASTCALL
HaliReferenceBusHandler(IN PBUS_HANDLER Handler)
{
    PHAL_BUS_HANDLER Bus;

    /* Find and reference the bus handler */
    Bus = CONTAINING_RECORD(Handler, HAL_BUS_HANDLER, Handler);
    Bus->ReferenceCount++;
}

VOID
FASTCALL
HaliDereferenceBusHandler(IN PBUS_HANDLER Handler)
{
    PHAL_BUS_HANDLER Bus;

    /* Find and dereference the bus handler */
    Bus = CONTAINING_RECORD(Handler, HAL_BUS_HANDLER, Handler);
    Bus->ReferenceCount--;
    ASSERT(Bus->ReferenceCount != 0);
}

PBUS_HANDLER
FASTCALL
HaliHandlerForBus(IN INTERFACE_TYPE InterfaceType,
                  IN ULONG BusNumber)
{
    /* Lookup the interface in the bus table */
    return HalpLookupHandler(HalpBusTable, InterfaceType, BusNumber, FALSE);
}

PBUS_HANDLER
FASTCALL
HaliHandlerForConfigSpace(IN BUS_DATA_TYPE ConfigType,
                          IN ULONG BusNumber)
{
    /* Lookup the configuration in the configuration table */
    return HalpLookupHandler(HalpConfigTable, ConfigType, BusNumber, FALSE);
}

PBUS_HANDLER
FASTCALL
HaliReferenceHandlerForBus(IN INTERFACE_TYPE InterfaceType,
                           IN ULONG BusNumber)
{
    /* Lookup the interface in the bus table, and reference the handler */
    return HalpLookupHandler(HalpBusTable, InterfaceType, BusNumber, TRUE);
}

PBUS_HANDLER
FASTCALL
HaliReferenceHandlerForConfigSpace(IN BUS_DATA_TYPE ConfigType,
                                   IN ULONG BusNumber)
{
    /* Lookup the configuration in the configuration table and add a reference */
    return HalpLookupHandler(HalpConfigTable, ConfigType, BusNumber, TRUE);
}

PBUS_HANDLER
NTAPI
HalpContextToBusHandler(IN ULONG_PTR ContextValue)
{
    PLIST_ENTRY NextEntry;
    PHAL_BUS_HANDLER BusHandler, ThisHandler;

    /* Start lookup */
    NextEntry = HalpAllBusHandlers.Flink;
    ThisHandler = CONTAINING_RECORD(NextEntry, HAL_BUS_HANDLER, AllHandlers);
    if (ContextValue)
    {
        /* If the list is empty, quit */
        if (IsListEmpty(&HalpAllBusHandlers)) return NULL;

        /* Otherwise, scan the list */
        BusHandler = CONTAINING_RECORD(ContextValue, HAL_BUS_HANDLER, Handler);
        do
        {
            /* Check if we've reached the right one */
            ThisHandler = CONTAINING_RECORD(NextEntry, HAL_BUS_HANDLER, AllHandlers);
            if (ThisHandler == BusHandler) break;

            /* Try the next one */
            NextEntry = NextEntry->Flink;
        } while (NextEntry != &HalpAllBusHandlers);
    }

    /* If we looped back to the end, we didn't find anything */
    if (NextEntry == &HalpAllBusHandlers) return NULL;

    /* Otherwise return the handler */
    return &ThisHandler->Handler;
}


VOID
NTAPI
HalpInitBusHandler(VOID)
{
    /* Setup the bus lock */
    KeInitializeSpinLock(&HalpBusDatabaseSpinLock);

    /* Setup the bus event */
    KeInitializeEvent(&HalpBusDatabaseEvent, SynchronizationEvent, TRUE);

    /* Setup the bus configuration and bus table */
    HalpBusTable = HalpAllocateArray(0);
    HalpConfigTable = HalpAllocateArray(0);

    /* Setup the bus list */
    InitializeListHead(&HalpAllBusHandlers);

    /* Setup the HAL Dispatch routines */
    HalPciAssignSlotResources = HalpAssignSlotResources;
    HalPciTranslateBusAddress = HaliTranslateBusAddress; /* PCI Driver can override */
    if (!HalFindBusAddressTranslation) HalFindBusAddressTranslation = HaliFindBusAddressTranslation;
}

/* EOF */
