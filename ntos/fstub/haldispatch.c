/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The tables a platform fills in, and what each entry does until it does
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fstubp.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/* Stops the machine. Without a platform to stop it, there is nothing to do */
VOID
NTAPI
xHalHaltSystem(VOID)
{
    /* TODO: a platform that can cut its own power says so here */
    for (;;)
    {
        YieldProcessor();
    }
}

/* Tells the platform that the boot is over */
VOID
NTAPI
xHalEndOfBoot(VOID)
{
    NOTHING;
}

/* Says whether the machine may be woken by its own hardware */
VOID
NTAPI
xHalSetWakeEnable(
    _In_ BOOLEAN Enable)
{
    UNREFERENCED_PARAMETER(Enable);

    /* TODO: a platform with a wake-capable clock arms it here */
}

/* Asks the platform to wake the machine at a time of the caller's choosing */
VOID
NTAPI
xHalSetWakeAlarm(
    _In_ ULONGLONG AlartTime,
    _In_ ULONGLONG DcWakeTime)
{
    UNREFERENCED_PARAMETER(AlartTime);
    UNREFERENCED_PARAMETER(DcWakeTime);

    /* TODO: a platform with a wake-capable clock sets it here */
}

/* Which entry of the interrupt table a vector arrives through */
UCHAR
NTAPI
xHalVectorToIDTEntry(
    _In_ ULONG Vector)
{
    return (UCHAR)Vector;
}

/* The handler for a bus the platform never registered */
PBUS_HANDLER
FASTCALL
xHalHandlerForBus(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);

    return NULL;
}

/* Counting a bus handler nobody handed out costs nothing */
VOID
FASTCALL
xHalReferenceHandler(
    _In_ PBUS_HANDLER BusHandler)
{
    UNREFERENCED_PARAMETER(BusHandler);
}

/* Brings up the driver that owns the platform's own devices */
NTSTATUS
NTAPI
xHalInitPnpDriver(VOID)
{
    /* TODO: a platform with devices of its own enumerates them here */
    return STATUS_SUCCESS;
}

/* Joins the kernel's power management to the platform's */
NTSTATUS
NTAPI
xHalInitPowerManagement(
    _In_ PPM_DISPATCH_TABLE PmDriverDispatchTable,
    _Out_ PPM_DISPATCH_TABLE *PmHalDispatchTable)
{
    UNREFERENCED_PARAMETER(PmDriverDispatchTable);

    if (PmHalDispatchTable != NULL) *PmHalDispatchTable = NULL;

    /* TODO: a platform with its own power states answers here */
    return STATUS_SUCCESS;
}

/* Begins keeping a second copy of memory somewhere safe */
NTSTATUS
NTAPI
xHalStartMirroring(VOID)
{
    return STATUS_NOT_SUPPORTED;
}

/* Stops doing that */
NTSTATUS
NTAPI
xHalEndMirroring(
    _In_ ULONG PassNumber)
{
    UNREFERENCED_PARAMETER(PassNumber);

    return STATUS_NOT_SUPPORTED;
}

/* Copies a stretch of memory into the second copy */
NTSTATUS
NTAPI
xHalMirrorPhysicalMemory(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ LARGE_INTEGER NumberOfBytes)
{
    UNREFERENCED_PARAMETER(PhysicalAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);

    return STATUS_NOT_SUPPORTED;
}

/* Which slots of a bus have something in them */
NTSTATUS
NTAPI
xHalQueryBusSlots(
    _In_ PBUS_HANDLER BusHandler,
    _In_ ULONG BufferSize,
    _Out_ PULONG SlotNumbers,
    _Out_ PULONG ReturnedLength)
{
    UNREFERENCED_PARAMETER(BusHandler);
    UNREFERENCED_PARAMETER(BufferSize);
    UNREFERENCED_PARAMETER(SlotNumbers);

    if (ReturnedLength != NULL) *ReturnedLength = 0;

    return STATUS_NOT_SUPPORTED;
}

/* Changes something about the platform */
NTSTATUS
NTAPI
xHalSetSystemInformation(
    _In_ HAL_SET_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(BufferSize);
    UNREFERENCED_PARAMETER(Buffer);

    return STATUS_INVALID_LEVEL;
}

/* Asks something about the platform */
NTSTATUS
NTAPI
xHalQuerySystemInformation(
    _In_ HAL_QUERY_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _Inout_ PVOID Buffer,
    _Out_ PULONG ReturnedLength)
{
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(BufferSize);
    UNREFERENCED_PARAMETER(Buffer);

    PAGED_CODE();

    if (ReturnedLength != NULL) *ReturnedLength = 0;

    return STATUS_INVALID_LEVEL;
}

/* Marks the stretches of memory that must survive a hibernation */
VOID
NTAPI
xHalLocateHiberRanges(
    _In_ PVOID MemoryMap)
{
    UNREFERENCED_PARAMETER(MemoryMap);
}

/* Takes on a bus the platform wants the kernel to know about */
NTSTATUS
NTAPI
xHalRegisterBusHandler(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ BUS_DATA_TYPE ConfigSpace,
    _In_ ULONG BusNumber,
    _In_ INTERFACE_TYPE ParentInterfaceType,
    _In_ ULONG ParentBusNumber,
    _In_ ULONG ContextSize,
    _In_ PINSTALL_BUS_HANDLER InstallCallback,
    _Out_ PBUS_HANDLER *BusHandler)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(ConfigSpace);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(ParentInterfaceType);
    UNREFERENCED_PARAMETER(ParentBusNumber);
    UNREFERENCED_PARAMETER(ContextSize);
    UNREFERENCED_PARAMETER(InstallCallback);

    if (BusHandler != NULL) *BusHandler = NULL;

    return STATUS_NOT_SUPPORTED;
}

/* Turns an address as a bus sees it into one the processor can use */
BOOLEAN
NTAPI
xHalTranslateBusAddress(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);

    if (TranslatedAddress == NULL || AddressSpace == NULL) return FALSE;

    *TranslatedAddress = BusAddress;

    return TRUE;
}

/* Hands out the registers a transfer needs to be mapped through */
NTSTATUS
NTAPI
xHalAllocateMapRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Unknown,
    _In_ ULONG Unknown2,
    PMAP_REGISTER_ENTRY Registers)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Unknown);
    UNREFERENCED_PARAMETER(Unknown2);
    UNREFERENCED_PARAMETER(Registers);

    return STATUS_NOT_SUPPORTED;
}

/* Makes a device on a bus usable as a debugging port */
NTSTATUS
NTAPI
xKdSetupPciDeviceForDebugging(
    _In_opt_ PVOID LoaderBlock,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR PciDevice)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    UNREFERENCED_PARAMETER(PciDevice);

    return STATUS_NOT_SUPPORTED;
}

/* Gives it back */
NTSTATUS
NTAPI
xKdReleasePciDeviceForDebugging(
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR PciDevice)
{
    UNREFERENCED_PARAMETER(PciDevice);

    return STATUS_NOT_SUPPORTED;
}

/* Finds a firmware table before the kernel can read the firmware itself */
PVOID
NTAPI
xKdGetAcpiTablePhase(
    _In_ struct _LOADER_PARAMETER_BLOCK *LoaderBlock,
    _In_ ULONG Signature)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    UNREFERENCED_PARAMETER(Signature);

    return NULL;
}

/* Maps memory for the debugger before the memory manager can be asked */
PVOID
NTAPI
xKdMapPhysicalMemory64(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG NumberPages,
    _In_ BOOLEAN FlushCurrentTLB)
{
    UNREFERENCED_PARAMETER(PhysicalAddress);
    UNREFERENCED_PARAMETER(NumberPages);
    UNREFERENCED_PARAMETER(FlushCurrentTLB);

    return NULL;
}

/* Lets that mapping go */
VOID
NTAPI
xKdUnmapVirtualAddress(
    _In_ PVOID VirtualAddress,
    _In_ ULONG NumberPages,
    _In_ BOOLEAN FlushCurrentTLB)
{
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(NumberPages);
    UNREFERENCED_PARAMETER(FlushCurrentTLB);
}

/* GLOBALS ********************************************************************/

/* Each callback's name is also a macro that reaches through the table */
#undef HalQuerySystemInformation
#undef HalSetSystemInformation
#undef HalQueryBusSlots
#undef HalReferenceHandlerForBus
#undef HalReferenceBusHandler
#undef HalDereferenceBusHandler
#undef HalInitPnpDriver
#undef HalInitPowerManagement
#undef HalGetInterruptTranslator
#undef HalStartMirroring
#undef HalEndMirroring
#undef HalMirrorPhysicalMemory
#undef HalEndOfBoot
#undef HalHandlerForBus
#undef HalLocateHiberRanges
#undef HalRegisterBusHandler
#undef HalSetWakeEnable
#undef HalSetWakeAlarm
#undef HalPciTranslateBusAddress
#undef HalHaltSystem
#undef HalAllocateMapRegisters
#undef KdSetupPciDeviceForDebugging
#undef KdReleasePciDeviceforDebugging
#undef KdGetAcpiTablePhase0
#undef HalVectorToIDTEntry
#undef KdMapPhysicalMemory64
#undef KdUnmapVirtualAddress

HAL_DISPATCH HalDispatchTable =
{
    .Version = HAL_DISPATCH_VERSION,
    .HalQuerySystemInformation = xHalQuerySystemInformation,
    .HalSetSystemInformation = xHalSetSystemInformation,
    .HalQueryBusSlots = xHalQueryBusSlots,
    .HalExamineMBR = xHalExamineMBR,
    .HalIoReadPartitionTable = xHalIoReadPartitionTable,
    .HalIoSetPartitionInformation = xHalIoSetPartitionInformation,
    .HalIoWritePartitionTable = xHalIoWritePartitionTable,
    .HalReferenceHandlerForBus = xHalHandlerForBus,
    .HalReferenceBusHandler = xHalReferenceHandler,
    .HalDereferenceBusHandler = xHalReferenceHandler,
    .HalInitPnpDriver = xHalInitPnpDriver,
    .HalInitPowerManagement = xHalInitPowerManagement,
    .HalGetInterruptTranslator = xHalGetInterruptTranslator,
    .HalStartMirroring = xHalStartMirroring,
    .HalEndMirroring = xHalEndMirroring,
    .HalMirrorPhysicalMemory = xHalMirrorPhysicalMemory,
    .HalEndOfBoot = xHalEndOfBoot
};

HAL_PRIVATE_DISPATCH HalPrivateDispatchTable =
{
    .Version = HAL_PRIVATE_DISPATCH_VERSION,
    .HalHandlerForBus = xHalHandlerForBus,
    .HalLocateHiberRanges = xHalLocateHiberRanges,
    .HalRegisterBusHandler = xHalRegisterBusHandler,
    .HalSetWakeEnable = xHalSetWakeEnable,
    .HalSetWakeAlarm = xHalSetWakeAlarm,
    .HalPciTranslateBusAddress = xHalTranslateBusAddress,
    .HalHaltSystem = xHalHaltSystem,
    .HalAllocateMapRegisters = xHalAllocateMapRegisters,
    .KdSetupPciDeviceForDebugging = xKdSetupPciDeviceForDebugging,
    .KdReleasePciDeviceforDebugging = xKdReleasePciDeviceForDebugging,
    .KdGetAcpiTablePhase0 = xKdGetAcpiTablePhase,
    .HalVectorToIDTEntry = xHalVectorToIDTEntry,
    .KdMapPhysicalMemory64 = xKdMapPhysicalMemory64,
    .KdUnmapVirtualAddress = xKdUnmapVirtualAddress
};

/* EOF */
