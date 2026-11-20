/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 HAL services in the kernel; hal.dll forwards every export to these
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/hal/acpi_pci.h>
#include <reactos/hal/msi.h>
#include <reactos/hal/acpifault.h>
#define NDEBUG
#include <debug.h>

#undef KeAcquireSpinLock
#undef KeRaiseIrql
#undef KeLowerIrql

/* Device interrupts all run at one IRQL between DISPATCH_LEVEL and the clock. */
#define KI_ARM_DEVICE_IRQL 6

/* PSCI 0.2 function identifiers (ARM DEN 0022). */
#define PSCI_SYSTEM_OFF   0x84000008
#define PSCI_SYSTEM_RESET 0x84000009

/* PrimeCell RTC (PL031) TRM DDI 0224: data and load registers. */
#define PL031_DR 0x00
#define PL031_LR 0x08

static PUCHAR KiArmRtc;
static BOOLEAN KiArmRtcProbed;
static BOOLEAN KiArmPsciUsesSmc;

/* PCI configuration access through the ECAM window of "pci-host-ecam-generic". */
static PHYSICAL_ADDRESS KiArmEcamBase;
static ULONG KiArmEcamBuses;
static PUCHAR KiArmEcamBus[256];
static KSPIN_LOCK KiArmPciLock;

BOOLEAN
NTAPI
HalInitSystem(
    _In_ ULONG BootPhase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    static const CHAR * const Ecam[] = { "pci-host-ecam-generic", NULL };
    PHYSICAL_ADDRESS Address;
    ULONG Length;

    if (BootPhase == 0)
    {
        KeGetPcr()->StallScaleFactor = 1;
        return TRUE;
    }

    if (BootPhase == 1)
    {
        KiArmInitializePlatform(LoaderBlock);
        if (KiArmFindDevice(Ecam, 0, &Address, &Length))
        {
            KiArmEcamBase = Address;
            KiArmEcamBuses = min(Length >> 20, RTL_NUMBER_OF(KiArmEcamBus));
        }
        KiArmPsciUsesSmc = KiArmPsciConduitIsSmc();
        _enable();
    }
    return TRUE;
}

VOID
NTAPI
HalInitializeProcessor(
    _In_ ULONG ProcessorNumber,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(ProcessorNumber);
    UNREFERENCED_PARAMETER(LoaderBlock);
}

BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    return TRUE;
}

BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    UNREFERENCED_PARAMETER(ProcessorState);
    /* TODO: start secondary processors through PSCI CPU_ON */
    return FALSE;
}

VOID
NTAPI
HalRequestIpi(
    _In_ KAFFINITY TargetProcessors)
{
    KiArmSendInterprocessorInterrupt((ULONG)TargetProcessors, 0);
}

VOID
NTAPI
HalProcessorIdle(VOID)
{
    /* Entered with interrupts masked: WFI wakes on a pending one, which runs once enabled. */
    __wfi();
    _enable();
}

static
VOID
KiArmPsciCall(
    _In_ ULONG Function)
{
    register ULONG R0 __asm__("r0") = Function;

    if (KiArmPsciUsesSmc)
        __asm__ __volatile__(".arch_extension sec\n\tsmc #0" : "+r"(R0) :: "r1", "r2", "r3", "memory");
    else
        __asm__ __volatile__(".arch_extension virt\n\thvc #0" : "+r"(R0) :: "r1", "r2", "r3", "memory");
}

VOID
NTAPI
HalReturnToFirmware(
    _In_ FIRMWARE_REENTRY Action)
{
    _disable();
    switch (Action)
    {
        case HalPowerDownRoutine:
            KiArmPsciCall(PSCI_SYSTEM_OFF);
            break;
        case HalRestartRoutine:
        case HalRebootRoutine:
        default:
            KiArmPsciCall(PSCI_SYSTEM_RESET);
            break;
    }
    for (;;)
        __wfi();
}

static
BOOLEAN
KiArmMapRtc(VOID)
{
    static const CHAR * const Rtc[] = { "arm,pl031", NULL };
    PHYSICAL_ADDRESS Address;
    ULONG Length;

    if (!KiArmRtcProbed && (KeGetCurrentIrql() <= DISPATCH_LEVEL))
    {
        KiArmRtcProbed = TRUE;
        if (KiArmFindDevice(Rtc, 0, &Address, &Length))
            KiArmRtc = MmMapIoSpace(Address, PAGE_SIZE, MmNonCached);
    }
    return KiArmRtc != NULL;
}

BOOLEAN
NTAPI
HalQueryRealTimeClock(
    _Out_ PTIME_FIELDS Time)
{
    LARGE_INTEGER SystemTime;

    if (!KiArmMapRtc())
        return FALSE;

    /* The PL031 counts seconds since 1970-01-01 00:00:00. */
    RtlSecondsSince1970ToTime(READ_REGISTER_ULONG((PULONG)(KiArmRtc + PL031_DR)), &SystemTime);
    RtlTimeToTimeFields(&SystemTime, Time);
    return TRUE;
}

BOOLEAN
NTAPI
HalSetRealTimeClock(
    _In_ PTIME_FIELDS Time)
{
    LARGE_INTEGER SystemTime;
    ULONG Seconds;

    if (!KiArmMapRtc() || !RtlTimeFieldsToTime(Time, &SystemTime) ||
        !RtlTimeToSecondsSince1970(&SystemTime, &Seconds))
    {
        return FALSE;
    }
    WRITE_REGISTER_ULONG((PULONG)(KiArmRtc + PL031_LR), Seconds);
    return TRUE;
}

ARC_STATUS
NTAPI
HalGetEnvironmentVariable(
    _In_ PCH Name,
    _In_ USHORT ValueLength,
    _Out_writes_z_(ValueLength) PCH Value)
{
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(ValueLength);
    UNREFERENCED_PARAMETER(Value);
    return ENOENT;
}

ARC_STATUS
NTAPI
HalSetEnvironmentVariable(
    _In_ PCH Name,
    _In_ PCH Value)
{
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Value);
    return ENOMEM;
}

NTSTATUS
NTAPI
HalGetEnvironmentVariableEx(
    _In_ PWSTR VariableName,
    _In_ LPCGUID VendorGuid,
    _Out_writes_bytes_opt_(*ValueLength) PVOID Value,
    _Inout_ PULONG ValueLength,
    _Out_opt_ PULONG Attributes)
{
    UNREFERENCED_PARAMETER(VariableName);
    UNREFERENCED_PARAMETER(VendorGuid);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(ValueLength);
    UNREFERENCED_PARAMETER(Attributes);
    /* TODO: reach the UEFI runtime variable services */
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HalSetEnvironmentVariableEx(
    _In_ PWSTR VariableName,
    _In_ LPCGUID VendorGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _In_ ULONG Attributes)
{
    UNREFERENCED_PARAMETER(VariableName);
    UNREFERENCED_PARAMETER(VendorGuid);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(ValueLength);
    UNREFERENCED_PARAMETER(Attributes);
    /* TODO: reach the UEFI runtime variable services */
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
HalMakeBeep(
    _In_ ULONG Frequency)
{
    UNREFERENCED_PARAMETER(Frequency);
    return FALSE;
}

VOID
NTAPI
HalCalibratePerformanceCounter(
    _In_ volatile PLONG Count,
    _In_ ULONGLONG NewCount)
{
    UNREFERENCED_PARAMETER(NewCount);
    /* The generic timer counts the same on every processor. */
    InterlockedDecrement(Count);
    while (*Count)
        YieldProcessor();
}

ULONG
NTAPI
HalSetTimeIncrement(
    _In_ ULONG Increment)
{
    UNREFERENCED_PARAMETER(Increment);
    return KeMaximumIncrement;
}

ULONG_PTR
NTAPI
HalSetProfileInterval(
    _In_ ULONG_PTR Interval)
{
    return Interval;
}

VOID
NTAPI
HalStartProfileInterrupt(
    _In_ KPROFILE_SOURCE ProfileSource)
{
    UNREFERENCED_PARAMETER(ProfileSource);
}

VOID
NTAPI
HalStopProfileInterrupt(
    _In_ KPROFILE_SOURCE ProfileSource)
{
    UNREFERENCED_PARAMETER(ProfileSource);
}

VOID
NTAPI
HalReportResourceUsage(VOID)
{
}

VOID
NTAPI
HalAcquireDisplayOwnership(
    _In_ PHAL_RESET_DISPLAY_PARAMETERS ResetDisplayParameters)
{
    UNREFERENCED_PARAMETER(ResetDisplayParameters);
}

VOID
NTAPI
HalDisplayString(
    _In_ PCSTR String)
{
    InbvDisplayString((PCHAR)String);
}

VOID
NTAPI
HalQueryDisplayParameters(
    _Out_ PULONG DispSizeX,
    _Out_ PULONG DispSizeY,
    _Out_ PULONG CursorPosX,
    _Out_ PULONG CursorPosY)
{
    *DispSizeX = *DispSizeY = *CursorPosX = *CursorPosY = 0;
}

VOID
NTAPI
HalSetDisplayParameters(
    _In_ ULONG CursorPosX,
    _In_ ULONG CursorPosY)
{
    UNREFERENCED_PARAMETER(CursorPosX);
    UNREFERENCED_PARAMETER(CursorPosY);
}

/* PCI configuration space through ECAM: one megabyte per bus, mapped on first use. */
static
PUCHAR
KiArmPciFunction(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot)
{
    PHYSICAL_ADDRESS Address;

    if (!KiArmEcamBuses || (Bus >= KiArmEcamBuses) || (KeGetCurrentIrql() > DISPATCH_LEVEL))
        return NULL;
    if (!KiArmEcamBus[Bus])
    {
        Address.QuadPart = KiArmEcamBase.QuadPart + ((LONGLONG)Bus << 20);
        KiArmEcamBus[Bus] = MmMapIoSpace(Address, 1 << 20, MmNonCached);
        if (!KiArmEcamBus[Bus])
            return NULL;
    }
    return KiArmEcamBus[Bus] + (Slot.u.bits.DeviceNumber << 15) + (Slot.u.bits.FunctionNumber << 12);
}

static
ULONG
KiArmPciAccess(
    _In_ ULONG Bus,
    _In_ ULONG SlotNumber,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _In_ BOOLEAN Write)
{
    PCI_SLOT_NUMBER Slot;
    PUCHAR Function, Data = Buffer;
    ULONG Index, Vendor;
    KIRQL OldIrql;

    Slot.u.AsULONG = SlotNumber;
    Function = KiArmPciFunction(Bus, Slot);
    if (!Function || (Offset >= PCI_EXTENDED_CONFIG_LENGTH))
        return 0;

    Length = min(Length, PCI_EXTENDED_CONFIG_LENGTH - Offset);
    OldIrql = KeAcquireSpinLockRaiseToDpc(&KiArmPciLock);
    Vendor = READ_REGISTER_USHORT((PUSHORT)Function);
    if (Vendor == PCI_INVALID_VENDORID)
    {
        KeReleaseSpinLock(&KiArmPciLock, OldIrql);
        if (Write || (Offset >= sizeof(USHORT)))
            return 0;
        /* Reads of an absent function return the invalid vendor identifier. */
        for (Index = 0; Index < min(Length, sizeof(USHORT) - Offset); Index++)
            Data[Index] = 0xFF;
        return Index;
    }

    for (Index = 0; Index < Length; Index++)
    {
        if (Write)
            WRITE_REGISTER_UCHAR(Function + Offset + Index, Data[Index]);
        else
            Data[Index] = READ_REGISTER_UCHAR(Function + Offset + Index);
    }
    KeReleaseSpinLock(&KiArmPciLock, OldIrql);
    return Length;
}

ULONG
NTAPI
HalGetBusDataByOffset(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    if (BusDataType != PCIConfiguration)
        return 0;
    return KiArmPciAccess(BusNumber, SlotNumber, Buffer, Offset, Length, FALSE);
}

ULONG
NTAPI
HalGetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    return HalGetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, 0, Length);
}

ULONG
NTAPI
HalSetBusDataByOffset(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    if (BusDataType != PCIConfiguration)
        return 0;
    return KiArmPciAccess(BusNumber, SlotNumber, Buffer, Offset, Length, TRUE);
}

ULONG
NTAPI
HalSetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    return HalSetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, 0, Length);
}

NTSTATUS
NTAPI
HalAssignSlotResources(
    _In_ PUNICODE_STRING RegistryPath,
    _In_opt_ PUNICODE_STRING DriverClassName,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ INTERFACE_TYPE BusType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Inout_ PCM_RESOURCE_LIST *AllocatedResources)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(DriverClassName);
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(BusType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(AllocatedResources);
    /* Resources come from Plug and Play on this platform. */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalAdjustResourceList(
    _Inout_ PIO_RESOURCE_REQUIREMENTS_LIST *ResourceList)
{
    UNREFERENCED_PARAMETER(ResourceList);
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
HalTranslateBusAddress(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);

    /* Device memory is identity mapped; there is no separate I/O space to translate into. */
    *TranslatedAddress = BusAddress;
    *AddressSpace = 0;
    return TRUE;
}

ULONG
NTAPI
HalGetInterruptVector(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG BusInterruptLevel,
    _In_ ULONG BusInterruptVector,
    _Out_ PKIRQL Irql,
    _Out_ PKAFFINITY Affinity)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(BusInterruptLevel);

    /* A bus interrupt vector is the GIC interrupt identifier. */
    *Irql = KI_ARM_DEVICE_IRQL;
    *Affinity = 1;
    return BusInterruptVector;
}

KIRQL
NTAPI
HalConvertDeviceIdtToIrql(
    _In_ ULONG Vector)
{
    UNREFERENCED_PARAMETER(Vector);
    return KI_ARM_DEVICE_IRQL;
}

NTSTATUS
NTAPI
HalGetMessageRoutingInfo(
    _Inout_ PHAL_MESSAGE_ROUTING_INFO RoutingInfo)
{
    UNREFERENCED_PARAMETER(RoutingInfo);
    /* TODO: route message-signalled interrupts through a GICv2m frame */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalpGetMessageRoutingInfo(
    _Inout_ PHAL_MESSAGE_ROUTING_INFO RoutingInfo)
{
    return HalGetMessageRoutingInfo(RoutingInfo);
}

NTSTATUS
NTAPI
HalGetProcessorIdByNtNumber(
    _In_ ULONG ProcessorNumber,
    _Out_ PULONG ProcessorId)
{
    if (!ProcessorId || (ProcessorNumber >= (ULONG)KeNumberProcessors))
        return STATUS_INVALID_PARAMETER;
    *ProcessorId = ProcessorNumber;
    return STATUS_SUCCESS;
}

VOID
NTAPI
HalpConfigurePciRootBridge(
    _In_ const HAL_ACPI_PCI_ROOT_INFO *Info)
{
    UNREFERENCED_PARAMETER(Info);
}

VOID
NTAPI
HalpRegisterPciRouteQuery(
    _In_opt_ PHAL_ACPI_PCI_ROUTE_QUERY Provider)
{
    UNREFERENCED_PARAMETER(Provider);
}

VOID
NTAPI
HalpSetPciRoutingMap(
    _In_reads_opt_(EntryCount) const HAL_ACPI_PCI_ROUTE_ENTRY *Entries,
    _In_ ULONG EntryCount)
{
    UNREFERENCED_PARAMETER(Entries);
    UNREFERENCED_PARAMETER(EntryCount);
}

VOID
NTAPI
HalpRecordPciMaxGsi(
    _In_ const HAL_ACPI_PCI_ROUTE_ENTRY *Entry)
{
    UNREFERENCED_PARAMETER(Entry);
}

ULONG
NTAPI
HalpAcpiQueryFaults(
    _Out_writes_bytes_opt_(Size) PHALP_ACPI_FAULT Buffer,
    _In_ ULONG Size,
    _Out_opt_ PULONG Copied)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Size);
    /* This platform has no ACPI tables to fault on. */
    if (Copied)
        *Copied = 0;
    return 0;
}

VOID
FASTCALL
HalSweepIcache(VOID)
{
    KeSweepICache(NULL, 0);
}

VOID
FASTCALL
HalSweepDcache(VOID)
{
    KiArmDsb();
}

VOID
NTAPI
KeFlushWriteBuffer(VOID)
{
    KiArmDsb();
}

/* The ARM HAL keeps the classic exports behind the inline WDK forms. */
VOID
NTAPI
KeAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql)
{
    *OldIrql = KeAcquireSpinLockRaiseToDpc(SpinLock);
}

KIRQL
FASTCALL
KfAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    return KeAcquireSpinLockRaiseToDpc(SpinLock);
}

VOID
FASTCALL
KfReleaseSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
    KeReleaseSpinLock(SpinLock, OldIrql);
}

VOID
NTAPI
KeRaiseIrql(
    _In_ KIRQL NewIrql,
    _Out_ PKIRQL OldIrql)
{
    *OldIrql = KfRaiseIrql(NewIrql);
}

VOID
NTAPI
KeLowerIrql(
    _In_ KIRQL NewIrql)
{
    KfLowerIrql(NewIrql);
}

/* Port space does not exist on ARM: a port address is a device memory address. */
UCHAR NTAPI READ_PORT_UCHAR(_In_ PUCHAR Port) { return READ_REGISTER_UCHAR(Port); }
USHORT NTAPI READ_PORT_USHORT(_In_ PUSHORT Port) { return READ_REGISTER_USHORT(Port); }
ULONG NTAPI READ_PORT_ULONG(_In_ PULONG Port) { return READ_REGISTER_ULONG(Port); }
VOID NTAPI WRITE_PORT_UCHAR(_In_ PUCHAR Port, _In_ UCHAR Value) { WRITE_REGISTER_UCHAR(Port, Value); }
VOID NTAPI WRITE_PORT_USHORT(_In_ PUSHORT Port, _In_ USHORT Value) { WRITE_REGISTER_USHORT(Port, Value); }
VOID NTAPI WRITE_PORT_ULONG(_In_ PULONG Port, _In_ ULONG Value) { WRITE_REGISTER_ULONG(Port, Value); }
VOID NTAPI READ_PORT_BUFFER_UCHAR(_In_ PUCHAR Port, _Out_writes_all_(Count) PUCHAR Buffer, _In_ ULONG Count) { while (Count--) *Buffer++ = READ_REGISTER_UCHAR(Port); }
VOID NTAPI READ_PORT_BUFFER_USHORT(_In_ PUSHORT Port, _Out_writes_all_(Count) PUSHORT Buffer, _In_ ULONG Count) { while (Count--) *Buffer++ = READ_REGISTER_USHORT(Port); }
VOID NTAPI READ_PORT_BUFFER_ULONG(_In_ PULONG Port, _Out_writes_all_(Count) PULONG Buffer, _In_ ULONG Count) { while (Count--) *Buffer++ = READ_REGISTER_ULONG(Port); }
VOID NTAPI WRITE_PORT_BUFFER_UCHAR(_In_ PUCHAR Port, _In_reads_(Count) PUCHAR Buffer, _In_ ULONG Count) { while (Count--) WRITE_REGISTER_UCHAR(Port, *Buffer++); }
VOID NTAPI WRITE_PORT_BUFFER_USHORT(_In_ PUSHORT Port, _In_reads_(Count) PUSHORT Buffer, _In_ ULONG Count) { while (Count--) WRITE_REGISTER_USHORT(Port, *Buffer++); }
VOID NTAPI WRITE_PORT_BUFFER_ULONG(_In_ PULONG Port, _In_reads_(Count) PULONG Buffer, _In_ ULONG Count) { while (Count--) WRITE_REGISTER_ULONG(Port, *Buffer++); }

/* DMA: no adapter is created until the generic coherent adapter is shared with this port. */
PADAPTER_OBJECT
NTAPI
HalGetAdapter(
    _In_ PDEVICE_DESCRIPTION DeviceDescription,
    _Out_ PULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(DeviceDescription);
    /* TODO: build DMA adapters (share the generic adapter of hal/halriscv64/dma.c) */
    *NumberOfMapRegisters = 0;
    return NULL;
}

PVOID
NTAPI
HalAllocateCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _Out_ PPHYSICAL_ADDRESS LogicalAddress,
    _In_ BOOLEAN CacheEnabled)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(CacheEnabled);
    LogicalAddress->QuadPart = 0;
    return NULL;
}

VOID
NTAPI
HalFreeCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ PVOID VirtualAddress,
    _In_ BOOLEAN CacheEnabled)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(CacheEnabled);
}

BOOLEAN
NTAPI
HalFlushCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ PVOID VirtualAddress)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(VirtualAddress);
    return TRUE;
}

NTSTATUS
NTAPI
HalAllocateAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PWAIT_CONTEXT_BLOCK Wcb,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Wcb);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
    UNREFERENCED_PARAMETER(ExecutionRoutine);
    return STATUS_INSUFFICIENT_RESOURCES;
}

VOID
NTAPI
IoFreeAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    UNREFERENCED_PARAMETER(AdapterObject);
}

VOID
NTAPI
IoFreeMapRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PVOID MapRegisterBase,
    _In_ ULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
}

PHYSICAL_ADDRESS
NTAPI
IoMapTransfer(
    _In_opt_ PADAPTER_OBJECT AdapterObject,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _Inout_ PULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    PHYSICAL_ADDRESS Address;

    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(MapRegisterBase);

    /* A bus master sees physical memory directly; keep the data visible to it. */
    Address = MmGetPhysicalAddress(CurrentVa);
    *Length = min(*Length, MmGetMdlByteCount(Mdl) - (ULONG)((PUCHAR)CurrentVa - (PUCHAR)MmGetMdlVirtualAddress(Mdl)));
    KiArmFlushDmaRange(CurrentVa, *Length, !WriteToDevice);
    return Address;
}

BOOLEAN
NTAPI
IoFlushAdapterBuffers(
    _In_opt_ PADAPTER_OBJECT AdapterObject,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    if (!WriteToDevice)
        KiArmFlushDmaRange(CurrentVa, Length, TRUE);
    return TRUE;
}

ULONG
NTAPI
HalReadDmaCounter(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    return 0;
}

PVOID
NTAPI
HalAllocateCrashDumpRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _Inout_ PULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    *NumberOfMapRegisters = 0;
    return NULL;
}

BOOLEAN
NTAPI
HalEnableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE InterruptMode)
{
    return KiArmEnableInterruptLine(Vector, Irql, InterruptMode);
}

VOID
NTAPI
HalDisableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql)
{
    UNREFERENCED_PARAMETER(Irql);
    KiArmDisableInterruptLine(Vector);
}

BOOLEAN
NTAPI
HalBeginSystemInterrupt(
    _In_ KIRQL Irql,
    _In_ ULONG Vector,
    _Out_ PKIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(Vector);
    *OldIrql = KfRaiseIrql(Irql);
    return TRUE;
}

VOID
NTAPI
HalEndSystemInterrupt(
    _In_ KIRQL OldIrql,
    _In_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    KfLowerIrql(OldIrql);
}

ULONG
NTAPI
HalpKdReadPciConfig(
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    return KiArmPciAccess(BusNumber, SlotNumber, Buffer, Offset, Length, FALSE);
}
